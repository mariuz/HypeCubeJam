#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/GameModeBase.h"
#include "Math/RandomStream.h"
#include "Templates/SubclassOf.h"
#include "Game/HCJBalance.h"
#include "Hypercube/HCJMatchConfig.h"
#include "Ship/HCJWeapons.h"
#include "Ship/HCJShipTypes.h"
#include "HCJGameMode.generated.h"

class AHCJBotController;
class AHCJPlayerController;
class AHCJGameState;
class AHCJPlayerState;
class AHCJShipCharacter;
class AHCJUniverse;

/**
 * Server-only rules of the free-for-all deathmatch (everyone is hostile to everyone). No server travel: the
 * match loop runs forever inside one world.
 *
 * STARTUP ORDER (server)
 *  InitGame        parse URL options, seed GameplayRandom, compute the first FHCJMatchConfig (MatchNumber 1).
 *  InitGameState   Super, then AHCJGameState::SetMatchConfig(first config) - the universe is built here, before
 *                  any login and before BeginPlay - then SetFragLimit and SetMatchPhase(Playing, TimeLimitSeconds, null).
 *  StartPlay       Super, start the GameUpdate timer (GameUpdateInterval), run UpdateBotCount once.
 *  PostLogin       Super (-> HandleStartingNewPlayer -> RestartPlayer when PlayerCanRestart), then UpdateBotCount.
 *
 * URL OPTIONS (all optional): ?Seed=<int> fixed seed (match k uses Seed for k == 1, HCJ::MixSeed(Seed, k) after),
 * ?Dim=<2..6>, ?EdgeP=<0..1>, ?Bots=<MinCombatants>, ?MinBots=<n>, ?FragLimit=<n>, ?TimeLimit=<seconds>.
 * Without ?Seed every match gets a fresh seed from MakeRandomSeed (clock based; FMath::Rand is never used).
 *
 * SPAWNING: every ship comes from RestartPlayer -> RestartPlayerAtTransform at
 * AHCJUniverse::GetRandomSpawnTransform(random sector, GameplayRandom). There are no APlayerStart actors.
 *
 * DEATH / RESPAWN: AHCJShipCharacter::Die calls NotifyShipKilled. Humans and bots are respawned by the timers in
 * RespawnTimers after RespawnDelay; a human may ask earlier (AHCJPlayerController::ServerRequestRespawn ->
 * RequestRespawn) once MinRespawnDelay has passed. GameUpdate re-schedules any shipless controller that somehow
 * has no pending timer, so nobody stays dead.
 *
 * BOT FILL: desired bots = clamp(max(MinBots, MinCombatants - humans), 0, MaxBots), re-evaluated on login, logout
 * and every GameUpdate. Too few: SpawnBot. Too many: RemoveBot, preferring bots without a live ship, then the
 * lowest score. Bots are AHCJBotController actors with their own AHCJPlayerState, named "Bot-NN".
 *
 * MATCH LOOP: Playing ends when someone reaches FragLimit or TimeLimitSeconds elapse -> EndMatch (Intermission:
 * all ships destroyed, respawns cancelled, winner published) -> after IntermissionSeconds StartNewMatch (new
 * config -> universe rebuilt everywhere, stats reset, everybody respawned after MatchStartRespawnDelay).
 */
UCLASS(Config = Game)
class HYPECUBEJAM_API AHCJGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** Sets DefaultPawnClass, PlayerControllerClass, PlayerStateClass, GameStateClass, HUDClass and BotControllerClass. */
	AHCJGameMode();

	//~ AGameModeBase
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	/** Also reached for bots (AController::Destroyed calls Logout): drops the bot from Bots, cancels its respawn timer. */
	virtual void Logout(AController* Exiting) override;
	/**
	 * Names are unique in a match: a second "User" becomes "User-2" (found by the playthrough tests - three machines
	 * with the default pilot name were three rows "User" on the board and "User destroyed User" in the kill feed).
	 */
	virtual void ChangeName(AController* Controller, const FString& NewName, bool bNameChange) override;
	/**
	 * Spawns and possesses a new ship for NewPlayer (human or bot) at a random spawn point of a random sector.
	 * No-op when the phase is not Playing, the universe is not built, or the controller already has a live ship.
	 * On success clears the controller's respawn timer and its player state's respawn times.
	 */
	virtual void RestartPlayer(AController* NewPlayer) override;
	/** Guards the engine's own restart paths: Super && Playing phase && earliest respawn time reached. */
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---- called by other classes (server) ----

	/**
	 * Called by AHCJShipCharacter::Die while the ship is still possessed by VictimController.
	 * Does, in order: victim AddDeath; killer AddKill when KillerController is non-null and != VictimController;
	 * AHCJGameState::MulticastKillEvent; for a human victim APlayerController::SetSpawnLocation(ship location) (death
	 * camera); ScheduleRespawn(VictimController, RespawnDelay); CheckMatchEnd.
	 * Must NOT destroy or unpossess the ship (the ship does that itself right after this returns).
	 * Ignored when VictimController is null or the phase is not Playing.
	 */
	void NotifyShipKilled(AController* VictimController, AController* KillerController, AHCJShipCharacter* VictimShip, AActor* Causer = nullptr);

	// ---- pickups (server; called by AHCJPickup) ----

	/** Item for a pad that is being (re)filled: UHCJGameplaySettings::PickItem with a number of GameplayRandom. */
	EHCJPickupItem ChoosePickupItem();
	/** Seconds until an emptied pad is refilled: uniform in the configured range. */
	float RandomPickupDelay();
	/** A ship of Collector took Item from a pad: counts it and tells a human collector (HUD message). */
	void NotifyPickupCollected(EHCJPickupItem Item, AController* Collector);

	/**
	 * Manual respawn request (AHCJPlayerController::ServerRequestRespawn). Respawns immediately when the controller
	 * has no live ship, the phase is Playing and its earliest respawn time has passed; otherwise ignored.
	 */
	void RequestRespawn(AController* Controller);

	// ---- title menu (listen host; called by the local AHCJPlayerController) ----

	/**
	 * The host pressed Play: applies the menu's options (bots that fill the match, dimension, seed with 0 = random,
	 * bot difficulty 0..3), makes Host a combatant again and starts a fresh match with a new universe.
	 */
	void StartMatchFromMenu(AHCJPlayerController* Host, int32 NumBots, int32 Dimension, int32 Seed, int32 Difficulty);
	/** The host went back to the title menu: its ship is removed without a score and it stops counting as a combatant; the bots play on. */
	void ReturnToMenu(AHCJPlayerController* Host);
	/** Development: ends the running match now (intermission, then the next match). */
	void DebugEndMatch() { EndMatch(); }
	/** Bot skill of this match: 0 easy, 1 normal, 2 hard, 3 ace (config BotDifficulty, URL ?Difficulty=, or the menu). */
	int32 GetBotDifficulty() const { return BotDifficulty; }
	/**
	 * The host changed the level in the in-match menu: it applies to the running match at once (bots read their skill
	 * row every Think; the share of bots that fly "smart" follows as they get new ships).
	 */
	void SetBotDifficulty(int32 Difficulty);

	// ---- fairness towards human players (docs/DEVLOG_NOTES.md, item 29; the "Fair:" log lines) ----

	/**
	 * -HCJNoFair: bots treat a human ship like any other (no attack cap, no rear rule, no grace, no hunters cap, full
	 * damage, every torpedo), for comparisons. Not quite the game before the rules: the spawn changes of the same item
	 * apply to every ship with or without this switch (3 s of spawn shield instead of 2, no spawn within 80 m of an
	 * enemy, a new ship faces the nearest one), and so does everything the HUD tells the player.
	 */
	static bool IsNoFair();
	/**
	 * -HCJClassicEasy: Easy without the beginner's help of item 38 (a person's guns at their plain damage, no hull
	 * mending, the plain repair pad, bots at half damage instead of 0.4) - Easy as build 0.20.0 had it, in the same
	 * binary, for comparisons. Nothing else changes; the other levels have no such help to switch off.
	 */
	static bool IsClassicEasy();
	/**
	 * -HCJClassicEasy20: Easy without any of the beginner's help (items 38 and 40) - Easy as build 0.20.0 had it.
	 * Item 40: -HCJClassicEasy now takes only the help of item 40 away (Easy of build 0.21.0); see also
	 * -HCJClassicTorpedo (HCJTorpedo::IsClassicHoming), the torpedo of 0.21.0 for a person on every level.
	 */
	static bool IsClassicEasy20();
	/** The skill row of the difficulty in force, with what -HCJNoFair, -HCJClassicEasy and -HCJClassicEasy20 switch off already cleared. */
	FHCJBotSkill GetFairSkill() const;
	/**
	 * More help for a beginner (item 40; Easy only, "off" elsewhere and with the switches above): the reach within which
	 * a person's ship takes a pad's item (uu, 0 = by touch only), the weapon a person's new ship holds (Count = none),
	 * the aim assist of a person's shots (degrees, 0 = none), and whether the HUD points to a weapon pad.
	 */
	float GetHumanPickupReach() const { return FMath::Clamp(GetFairSkill().HumanPickupReach, 0.0f, 6000.0f); }
	EHCJWeapon GetHumanSpawnWeapon() const { return GetFairSkill().HumanSpawnWeapon; }
	float GetHumanAimAssistDeg() const { return FMath::Clamp(GetFairSkill().HumanAimAssistDeg, 0.0f, 15.0f); }
	bool ShowsWeaponPadPointer() const { return GetFairSkill().bHumanWeaponPointer; }
	/** Item 42: copies ShowsWeaponPadPointer to the replicated AHCJGameState (match start, change of the level). */
	void PublishWeaponPadPointer();
	/** Telemetry of item 40 (the "Fair: event=status" line): see FMatchStats. */
	void NotifyHumanReachPickup() { ++MatchStats.HelpReachPickups; }
	void NotifyHumanArmedSpawn() { ++MatchStats.HelpArmedSpawns; }
	void NotifyHumanBolt(bool bAssisted) { ++MatchStats.HelpBolts; MatchStats.HelpAssistedBolts += bAssisted ? 1 : 0; }
	void NotifyHumanBoltHit(bool bAssisted) { ++MatchStats.HelpBoltHits; MatchStats.HelpAssistedHits += bAssisted ? 1 : 0; }
	void NotifyHumanTorpedoFired(bool bHoming, bool bAcquired) { ++MatchStats.HumanTorpFired; MatchStats.HumanTorpHoming += bHoming ? 1 : 0; MatchStats.HumanTorpAcquired += bAcquired ? 1 : 0; }
	void NotifyHumanTorpedoLost() { ++MatchStats.HumanTorpLost; }
	void NotifyHumanTorpedoDetonated(bool bHitShip) { MatchStats.HumanTorpHit += bHitShip ? 1 : 0; }
	/** What a bot's damage to a human ship is multiplied by at this difficulty (1 with -HCJNoFair). */
	float GetBotDamageToHumanScale() const;
	/** Beginner's help (Easy; 1 / 0 / 1 elsewhere and with -HCJNoFair or -HCJClassicEasy): what a person's damage to a bot's ship is multiplied by, the hull points a second a person's ship mends out of combat, and what a repair pad's amount is multiplied by for a person. */
	float GetHumanDamageToBotScale() const;
	float GetHumanHullRegenRate() const;
	float GetHumanRepairScale() const;
	/** Telemetry of the beginner's help: damage a person did to bots (as applied; Bonus = the part the scale added), hull points mended, a repair pad taken by a person. */
	void NotifyHumanDamageToBot(float Applied, float Bonus) { MatchStats.HelpDamageDealt += Applied; MatchStats.HelpDamageBonus += Bonus; }
	void NotifyHumanHullRegen(float Points) { MatchStats.HelpHullRegen += Points; }
	void NotifyHumanRepair(float Points) { MatchStats.HelpRepairs += 1; MatchStats.HelpRepairPoints += Points; }
	/**
	 * A hit on a human ship (every damage step, also each step of a phaser beam). Applied is what the health component
	 * absorbed, Unscaled the same before the difficulty's damage scale, BearingDeg the angle between the victim's nose and
	 * the direction to the attacker (-1 unknown), bAft whether it landed on the aft facing. Only hits by bots are added up.
	 */
	void NotifyFairHit(float Applied, float Unscaled, float BearingDeg, bool bAft, bool bFatal, bool bAttackerIsBot);
	/** NumAttackers bots are in Attack on one human ship right now (the peak of a match is reported). */
	void NotifyHumanAttackers(int32 NumAttackers) { MatchStats.FairPeakAttackers = FMath::Max(MatchStats.FairPeakAttackers, NumAttackers); }
	/** A bot did not take / dropped a human target because of the attack cap, held its fire in a human's rear arc, left a human alone that had just arrived (once per bot Think). */
	void NotifyFairHeldCap() { ++MatchStats.FairHeldCap; }
	void NotifyFairHeldRear() { ++MatchStats.FairHeldRear; }
	void NotifyFairHeldGrace() { ++MatchStats.FairHeldGrace; }
	/** ... or because its attacker had just gone (the breather between fights). */
	void NotifyFairHeldRespite() { ++MatchStats.FairHeldRespite; }
	/** ... or a bot that travels did not go to a person's sector that has its share of bots (once per choice of a way). */
	void NotifyFairHeldHunters() { ++MatchStats.FairHeldHunters; }
	/** Number of bots that are in Attack on Target right now. */
	int32 CountBotsAttacking(const AHCJShipCharacter* Target) const;

	// ---- telemetry hooks (server): per-match counters reported by the periodic "Status" log line ----

	void NotifyShotFired() { ++MatchStats.Shots; }
	/** Amount is what the health component actually absorbed. */
	void NotifyDamageDealt(float Amount) { ++MatchStats.Hits; MatchStats.Damage += Amount; }
	void NotifyPortalJump() { ++MatchStats.PortalJumps; }
	/** Balance statistics only: a hit that touched the hull or not, a shield facing that went down. */
	void NotifyHitLanded(bool bReachedHull) { ++(bReachedHull ? MatchStats.HitsHull : MatchStats.HitsShieldOnly); }
	void NotifyFacingCollapsed() { ++MatchStats.FacingsCollapsed; }
	/** A bot that is breaking off started to turn a healthier shield facing towards its attacker. */
	void NotifyBotShieldTurn() { ++MatchStats.BotShieldTurns; }
	/** Photon torpedoes: one fired (with or without a target to home on), one that lost its target, one that went off (armed or a dud; ships it damaged), one hit on a ship. */
	void NotifyTorpedoFired(bool bHoming) { ++MatchStats.TorpedoesFired; MatchStats.TorpedoesHoming += bHoming ? 1 : 0; }
	void NotifyTorpedoLostLock() { ++MatchStats.TorpedoesDodged; }
	void NotifyTorpedoDetonated(bool bArmed, int32 NumShipsHit) { ++(bArmed ? MatchStats.TorpedoDetonations : MatchStats.TorpedoDuds); MatchStats.TorpedoesConnected += NumShipsHit > 0 ? 1 : 0; }
	void NotifyTorpedoHit(float Amount, bool bFacingWasDown)
	{
		++MatchStats.TorpedoHits;
		MatchStats.TorpedoHitsOpen += bFacingWasDown ? 1 : 0;
		MatchStats.TorpedoDamage += Amount;
		MatchStats.Damage += Amount;
	}
	void NotifyBotTorpedoEvade() { ++MatchStats.BotTorpedoEvades; }
	/** Phasers: a beam ran for Seconds (bOnShip: and ended on a ship); damage one of its steps did to a ship. */
	void NotifyPhaserBeam(float Seconds, bool bOnShip) { MatchStats.PhaserSeconds += Seconds; MatchStats.PhaserOnShipSeconds += bOnShip ? Seconds : 0.0f; }
	void NotifyPhaserDamage(float Amount, bool bReachedHull)
	{
		MatchStats.PhaserDamage += Amount;
		++(bReachedHull ? MatchStats.PhaserStepsHull : MatchStats.PhaserStepsShield);
	}
	/** A rock was destroyed (by the phaser or torpedo); NumPieces it broke into. */
	void NotifyAsteroidDestroyed(const AController* DamageInstigator, float Radius, int32 NumPieces);
	/** The shockwave of an explosion pushed NumShips ships. */
	void NotifyShockwave(int32 NumShips) { ++MatchStats.Shockwaves; MatchStats.ShockwavePushes += NumShips; }
	void NotifyWormholeJump() { ++MatchStats.WormholeJumps; }
	/** Warp drive, cloak and tractor beam (the "Systems" log line). */
	void NotifyWarp(bool bBot) { ++MatchStats.Warps; MatchStats.BotWarps += bBot ? 1 : 0; }
	void NotifyWarpCancelled() { ++MatchStats.WarpCancels; }
	void NotifyWarpImpact() { ++MatchStats.WarpImpacts; }
	void NotifyCloak(bool bBot) { ++MatchStats.Cloaks; MatchStats.BotCloaks += bBot ? 1 : 0; }
	void NotifyCloakRevealed() { ++MatchStats.CloakReveals; }
	void NotifyTractorHold(bool bBot) { ++MatchStats.TractorHolds; MatchStats.BotTractorHolds += bBot ? 1 : 0; }
	void NotifyTractorRelease(bool bBroken) { MatchStats.TractorBreaks += bBroken ? 1 : 0; }
	void NotifyTractorSeconds(float Seconds) { MatchStats.TractorSeconds += Seconds; }
	void NotifyBotBrokeFree() { ++MatchStats.BotTractorEscapes; }
	/** A bot did one of the things of EHCJBotTactic on purpose (the "Tactics" log line). */
	void NotifyBotTactic(EHCJBotTactic Tactic) { ++MatchStats.BotTactics[static_cast<int32>(Tactic)]; }
	int32 GetBotTacticCount(EHCJBotTactic Tactic) const { return MatchStats.BotTactics[static_cast<int32>(Tactic)]; }
	/** A torpedo hit Victim through a facing that was down (a kill by that torpedo in the same instant counts as an open-facing torpedo kill). */
	void NotifyTorpedoThroughOpenFacing(const AHCJShipCharacter* Victim);

	// ---- playthrough tests: what the automated matches assert on (see docs/tools/playthrough.py) ----

	/** Numbers of the running match the playthrough tests read without parsing a log. */
	struct FPlaythroughSnapshot
	{
		int32 MatchNumber = 0;
		int32 Seed = 0;
		uint32 LayoutChecksum = 0;
		int32 Kills = 0;
		int32 Deaths = 0;
		int32 PortalJumps = 0;
		int32 WormholeJumps = 0;
		int32 PickupsTaken = 0;
		int32 Shots = 0;
		int32 StuckEvents = 0;
		int32 OutOfBoundsEvents = 0;
		int32 NaNEvents = 0;
		int32 MatchesEnded = 0;
		FString LastWinner;
		int32 LastWinnerKills = 0;
		int32 LastBoardRows = 0;
	};
	FPlaythroughSnapshot GetPlaythroughSnapshot() const;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * SERVER: a player's choice of ship (type, hull, paint; anything invalid becomes the fighter / "pick for me").
	 * Stored in its player state for the next ship; a ship that is still under its spawn shield is changed at once.
	 */
	void SetShipChoice(AController* Controller, int32 Type, int32 Variant, int32 Skin);
	/** Bots fly fighters and leave warp, cloak and tractor beam alone (-HCJClassic: the game before the ship systems, for comparisons). */
	static bool IsClassicBots();
	void NotifyTurretShot() { ++MatchStats.TurretShots; }
	/** A station turret was destroyed (by Killer, may be null): small score for the killer, kill feed line. */
	void NotifyTurretDestroyed(AController* Killer);
	/** A ship of Victim is about to die in a black hole (the death itself still goes through NotifyShipKilled). */
	void NotifyBlackHoleDeath(const AController* Victim);

	// ---- queries (server) ----

	/** Connected human players (APlayerControllers), with or without a ship. */
	int32 GetNumHumanPlayers() const;
	int32 GetNumBots() const { return Bots.Num(); }
	/** clamp(max(MinBots, MinCombatants - GetNumHumanPlayers()), 0, MaxBots). */
	int32 GetDesiredBotCount() const;
	/** Config of the running match. */
	const FHCJMatchConfig& GetMatchConfig() const { return CurrentConfig; }
	AHCJGameState* GetHCJGameState() const;
	/** The server's universe (through the game state); nullptr before InitGameState. */
	AHCJUniverse* GetUniverse() const;

protected:
	// ---- match loop ----

	/** Config for match number InMatchNumber (1-based) from the URL overrides / config defaults / a fresh seed. */
	FHCJMatchConfig MakeMatchConfig(int32 InMatchNumber) const;
	/** Clock-derived seed: HCJ::MixSeed of FPlatformTime::Cycles64 and FDateTime::UtcNow ticks. */
	static int32 MakeRandomSeed();
	/** Timer, every GameUpdateInterval: UpdatePlayerSectors, UpdateBotCount, EnsureRespawnsScheduled, CheckMatchEnd. */
	void GameUpdate();
	/** Ends the match when Playing and (a player reached FragLimit or the phase time ran out). */
	void CheckMatchEnd();
	/** Playing -> Intermission (see class comment). Winner = top of AHCJGameState::GetSortedPlayerStates, may be null. */
	void EndMatch();
	/** Intermission -> Playing with a new config (see class comment). */
	void StartNewMatch();
	/** From any phase: ships and respawns cleared, stats reset, config for match InMatchNumber published, everybody respawned after MatchStartRespawnDelay. */
	void BeginMatch(int32 InMatchNumber);
	/** Destroys every AHCJShipCharacter without scoring; humans keep their camera at the ship's last location. */
	void DestroyAllShips();
	/** Destroys the pickups of the last match and spawns one AHCJPickup per pad of the current universe. */
	void RebuildPickups();
	/** Destroys the turrets of the last match and spawns one AHCJTurret per turret mount of the current universe's stations. */
	void RebuildStations();

	// ---- spawning ----

	/**
	 * Where ForController's next ship appears: the best of a dozen random spawn points (HCJBalance::ScoreSpawn) -
	 * not next to an enemy, not under a station's guns, not in a gravity well, preferably in a sector with somebody
	 * to fight. Returns the sector, INDEX_NONE when there is no universe.
	 */
	int32 ChooseSpawn(const AController* ForController, FTransform& OutTransform);
	/** Uniformly random valid sector from GameplayRandom. */
	int32 ChooseSpawnSector(const AController* ForController);
	/** Starts (or restarts) the respawn timer and publishes the times in the player state. */
	void ScheduleRespawn(AController* Controller, float Delay);
	void CancelRespawn(AController* Controller);
	void CancelAllRespawns();
	/** Timer callback: RestartPlayer when the controller is still valid. */
	void RespawnController(TWeakObjectPtr<AController> Controller);
	/** Schedules a respawn for every shipless controller without a pending timer (Playing phase only). */
	void EnsureRespawnsScheduled();
	/** True when Controller possesses an AHCJShipCharacter that is not dead. */
	static bool HasLiveShip(const AController* Controller);

	// ---- bots ----

	/** Spawns/removes bots until GetNumBots() == GetDesiredBotCount(). Playing phase only spawns ships. */
	void UpdateBotCount();
	/** Spawns an AHCJBotController (BotControllerClass), names its player state, RestartPlayer(bot). */
	AHCJBotController* SpawnBot();
	/** Destroys the bot's ship (no score) and the controller (its player state goes with it). */
	void RemoveBot(AHCJBotController* Bot);

	// ---- bookkeeping ----

	/** Writes each controller's current sector (INDEX_NONE without a live ship) into its AHCJPlayerState. */
	void UpdatePlayerSectors();
	/** Calls Visitor for every human and bot controller. */
	void ForEachCombatant(TFunctionRef<void(AController*)> Visitor) const;
	/** One LogHCJ line: player/bot/ship counts, occupied sectors and the MatchStats counters. */
	void LogStatus() const;

	// ---- configuration (DefaultGame.ini [/Script/HypeCubeJam.HCJGameMode], overridable by URL options) ----

	/** Bots fill up to this many total combatants. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Bots")
	int32 MinCombatants = 8;

	/** Bots that stay even when enough humans are connected. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Bots")
	int32 MinBots = 0;

	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Bots")
	int32 MaxBots = 16;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bots")
	TSubclassOf<AHCJBotController> BotControllerClass;

	/** 0 easy (a first-time player can win), 1 normal, 2 hard, 3 ace (hard without the fairness rules towards people). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Bots", meta = (ClampMin = "0", ClampMax = "3"))
	int32 BotDifficulty = 0;

	/** Hypercube dimension when ?Dim is absent; clamped to 2..6. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|World")
	int32 DefaultDimension = 4;

	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|World")
	float DefaultEdgeProbability = 0.5f;

	// Black holes (FHCJBlackHoleSettings; sent to the clients inside the match config). URL: ?BlackHoles=0|1, ?BlackHoleP=<0..1>.

	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|BlackHoles")
	bool bBlackHoles = true;

	/** Share of the sectors with a black hole. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|BlackHoles", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlackHoleSectorFraction = 0.25f;

	/** Radius (uu) inside which ships and bolts are pulled. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|BlackHoles", meta = (ClampMin = "1000.0", ClampMax = "12000.0"))
	float BlackHoleInfluenceRadius = 6000.0f;

	/** Radius (uu) inside which a ship is destroyed. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|BlackHoles", meta = (ClampMin = "100.0"))
	float BlackHoleHorizonRadius = 600.0f;

	/** Strength: drift speed (uu/s) at 71% of the influence radius; see FHCJBlackHole for the curve. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|BlackHoles", meta = (ClampMin = "0.0"))
	float BlackHolePullSpeed = 200.0f;

	/** Clamp of the drift speed near the centre (uu/s). Above the boost speed (6000) for a point of no return. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|BlackHoles", meta = (ClampMin = "0.0"))
	float BlackHoleMaxPullSpeed = 9000.0f;

	// Pickups: the rules are in UHCJGameplaySettings; URL ?Pickups=0|1 overrides its master switch.

	// Space stations (FHCJStationSettings; sent to the clients inside the match config). URL: ?Stations=0|1.
	// Turret behaviour is in UHCJGameplaySettings.

	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Stations")
	bool bStations = true;

	/** Share of the sectors with a station. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Stations", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StationSectorFraction = 0.25f;

	// Wormholes (FHCJWormholeSettings; sent to the clients inside the match config). URL: ?Wormholes=<n> (0 = none).

	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Wormholes")
	bool bWormholes = true;

	/** Number of wormholes; negative = a quarter of the number of sectors. None below dimension 3. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Wormholes")
	int32 WormholeCount = -1;

	/** Seconds from death to the automatic respawn. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Rules")
	float RespawnDelay = 4.0f;

	/** Seconds from death before a manual respawn request is accepted. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Rules")
	float MinRespawnDelay = 1.5f;

	/** Kills that win a match; 0 disables. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Rules")
	int32 FragLimit = 25;

	/** Match duration in seconds; 0 disables. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Rules")
	float TimeLimitSeconds = 600.0f;

	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Rules")
	float IntermissionSeconds = 8.0f;

	/** Delay between publishing a new match config and spawning ships, so clients can rebuild first. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Rules")
	float MatchStartRespawnDelay = 1.0f;

	/** Seconds between "Status" log lines on the server; 0 disables them. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "HCJ|Rules")
	float StatusLogInterval = 15.0f;

	static constexpr float GameUpdateInterval = 0.25f;

	// ---- runtime state ----

	struct FMatchStats
	{
		int32 Shots = 0;
		int32 Hits = 0;
		float Damage = 0.0f;
		int32 Kills = 0;
		/** Deaths nobody was credited for (out of bounds, self-inflicted). */
		int32 UncreditedDeaths = 0;
		int32 PortalJumps = 0;
		int32 WormholeJumps = 0;
		/** Station turrets: shots fired, ships they destroyed (an environment death shown as "Station"), turrets destroyed by ships. */
		int32 TurretShots = 0;
		int32 TurretDeaths = 0;
		int32 TurretsDestroyed = 0;
		/** Ships destroyed by an event horizon, and how many of them were bots. */
		int32 BlackHoleDeaths = 0;
		int32 BotBlackHoleDeaths = 0;
		/** Items taken from pads, by EHCJPickupItem, and how many of them by bots. */
		int32 Pickups[static_cast<int32>(EHCJPickupItem::Count)] = {};
		int32 BotPickups = 0;
		/** Credited kills by the weapon of the projectile that did it (EHCJWeapon). */
		int32 KillsByWeapon[static_cast<int32>(EHCJWeapon::Count)] = {};
		/** Time to kill: seconds from the first hit of a ship's last engagement to its death, summed over credited kills. */
		float TimeToKillSum = 0.0f;
		int32 TimeToKillCount = 0;
		/** Hits by where they landed: a shield facing took all of it, shield and hull shared it, hull only. */
		int32 HitsShieldOnly = 0;
		int32 HitsHull = 0;
		int32 FacingsCollapsed = 0;
		int32 BotShieldTurns = 0;
		/** Photon torpedoes: fired, of those with a target to home on, that lost it (dodged), went off armed / as duds, damaged at least one ship; hits on ships, of those on a facing that was down; damage. */
		int32 TorpedoesFired = 0;
		int32 TorpedoesHoming = 0;
		int32 TorpedoesDodged = 0;
		int32 TorpedoDetonations = 0;
		int32 TorpedoDuds = 0;
		int32 TorpedoesConnected = 0;
		int32 TorpedoHits = 0;
		int32 TorpedoHitsOpen = 0;
		float TorpedoDamage = 0.0f;
		int32 BotTorpedoEvades = 0;
		/** Phasers: seconds of beam, of those on a ship, damage to ships, damage steps that stayed on a shield / reached the hull. */
		float PhaserSeconds = 0.0f;
		float PhaserOnShipSeconds = 0.0f;
		float PhaserDamage = 0.0f;
		int32 PhaserStepsShield = 0;
		int32 PhaserStepsHull = 0;
		/** Rocks destroyed (of those by bots, of those that broke into pieces), and shockwaves that pushed ships. */
		int32 RocksDestroyed = 0;
		int32 RocksDestroyedByBots = 0;
		int32 RocksSplit = 0;
		int32 Shockwaves = 0;
		int32 ShockwavePushes = 0;
		/** Warp jumps (of those by bots), drives stopped by the server, collisions at warp; cloaks raised, reveals; tractor holds, torn free, seconds held. */
		int32 Warps = 0;
		int32 BotWarps = 0;
		int32 WarpCancels = 0;
		int32 WarpImpacts = 0;
		int32 Cloaks = 0;
		int32 BotCloaks = 0;
		int32 CloakReveals = 0;
		int32 TractorHolds = 0;
		int32 BotTractorHolds = 0;
		int32 TractorBreaks = 0;
		int32 BotTractorEscapes = 0;
		float TractorSeconds = 0.0f;
		/** Credited kills and all deaths by EHCJShipType. */
		int32 KillsByType[HCJShipTypes::NumTypes] = {};
		int32 DeathsByType[HCJShipTypes::NumTypes] = {};
		/** What bots did on purpose, by EHCJBotTactic, and torpedo kills through a facing that was down. */
		int32 BotTactics[static_cast<int32>(EHCJBotTactic::Count)] = {};
		int32 TorpedoOpenFacingKills = 0;
		/** Playthrough watch: ships that sat still, were alive outside the sector, or had a broken position; server frames and the longest. */
		int32 StuckEvents = 0;
		int32 OutOfBoundsEvents = 0;
		int32 NaNEvents = 0;
		int32 Ticks = 0;
		double TickSeconds = 0.0;
		float MaxTickSeconds = 0.0f;
		int32 SlowTicks = 0;
		float StartTime = 0.0f;
		/**
		 * Fairness towards humans, summed over all human ships (the "Fair: event=status" line): damage bots did to them
		 * (as applied; before the difficulty's damage scale; from their rear arc; on their aft facing), their deaths (all
		 * causes; by a bot; by a bot in their rear arc), the most bots in Attack on one of them, and how often a bot was
		 * held back by the attack cap / the rear rule / the arrival grace.
		 */
		float FairDamage = 0.0f;
		float FairDamageUnscaled = 0.0f;
		float FairDamageRear = 0.0f;
		float FairDamageAft = 0.0f;
		int32 FairDeaths = 0;
		int32 FairBotDeaths = 0;
		int32 FairRearDeaths = 0;
		int32 FairPeakAttackers = 0;
		int32 FairHeldCap = 0;
		int32 FairHeldRear = 0;
		int32 FairHeldGrace = 0;
		int32 FairHeldRespite = 0;
		int32 FairHeldHunters = 0;
		/** Beginner's help (item 38), summed over all human ships: damage they did to bots and the part of it the scale added, hull points mended out of combat, repair pads taken and what they restored, kills by people. */
		float HelpDamageDealt = 0.0f;
		float HelpDamageBonus = 0.0f;
		float HelpHullRegen = 0.0f;
		int32 HelpRepairs = 0;
		float HelpRepairPoints = 0.0f;
		int32 HelpHumanKills = 0;
		/**
		 * Item 40, summed over all human ships: pads people took (any way) and of them the ones taken from beyond the pad's
		 * own trigger (the Easy reach), new ships that started with a weapon, the bolts people fired (one per pellet) and
		 * of them the ones the aim assist turned, their bolts that hit a ship and of those the assisted ones; and people's
		 * photon torpedoes (every level): fired, homing at launch, of those the ones that found a ship without a lock in
		 * its cone, lost their target, damaged a ship.
		 */
		int32 HelpPickups = 0;
		int32 HelpReachPickups = 0;
		int32 HelpArmedSpawns = 0;
		int32 HelpBolts = 0;
		int32 HelpAssistedBolts = 0;
		int32 HelpBoltHits = 0;
		int32 HelpAssistedHits = 0;
		int32 HumanTorpFired = 0;
		int32 HumanTorpHoming = 0;
		int32 HumanTorpAcquired = 0;
		int32 HumanTorpLost = 0;
		int32 HumanTorpHit = 0;
	};
	FMatchStats MatchStats;
	float NextStatusLogTime = 0.0f;

	/** One structured "Playthrough: event=stats ..." line with every counter of the running match. */
	void LogPlaythroughStats() const;
	/** "Fair: event=status ..." - the fairness counters of the running match. */
	void LogFairStatus() const;
	/** "Fair: event=rules ..." - the fairness numbers in force (match start, change of the level). */
	void LogFairRules() const;
	/** What ChooseSpawn found for the point it chose (the "Fair: event=spawn" line of a human). */
	float LastSpawnNearestEnemy = -1.0f;
	int32 LastSpawnEnemiesInSector = 0;
	/** "Playthrough: event=match_start ..." for the match that just began. */
	void LogPlaythroughMatchStart() const;
	/** Once a second: ships that do not move, are alive outside the sector or have a broken position. */
	void UpdatePlaythroughWatch();
	struct FShipWatch
	{
		FVector Anchor = FVector::ZeroVector;
		float AnchorTime = 0.0f;
		float OutsideSince = -1.0f;
		bool bStuckReported = false;
	};
	TMap<TWeakObjectPtr<AController>, FShipWatch> ShipWatches;
	float NextWatchTime = 0.0f;
	/** The last torpedo hit through an open facing: who and when (NotifyShipKilled reads it in the same instant). */
	TWeakObjectPtr<const AHCJShipCharacter> LastOpenTorpedoVictim;
	float LastOpenTorpedoTime = -1000.0f;
	int32 NumMatchesEnded = 0;
	FString LastWinnerName;
	int32 LastWinnerKills = 0;
	int32 LastBoardRows = 0;

	/** Gameplay randomness (spawn sector/point). Seeded from MakeRandomSeed in InitGame; never used for world generation. */
	FRandomStream GameplayRandom;

	FHCJMatchConfig CurrentConfig;
	bool bHasSeedOverride = false;
	int32 SeedOverride = 0;
	/** Dimension / edge probability after applying URL options. */
	int32 MatchDimension = 4;
	float MatchEdgeProbability = 0.5f;
	FHCJBlackHoleSettings MatchBlackHoles;
	FHCJPickupSettings MatchPickups;
	FHCJWormholeSettings MatchWormholes;
	FHCJStationSettings MatchStations;
	FHCJAsteroidRules MatchAsteroids;
	int32 NextBotNumber = 1;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AHCJBotController>> Bots;

	TMap<TWeakObjectPtr<AController>, FTimerHandle> RespawnTimers;
	FTimerHandle GameUpdateTimerHandle;
	FTimerHandle IntermissionTimerHandle;
};
