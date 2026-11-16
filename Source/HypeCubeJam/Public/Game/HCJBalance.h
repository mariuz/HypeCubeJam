#pragma once

#include "CoreMinimal.h"

/**
 * How well a bot fights at one difficulty level. Plain numbers (tested on their own); AHCJBotController reads the
 * row of AHCJGameMode::GetBotDifficulty every Think, so a change in the menu applies to the next match at once.
 */
struct HYPECUBEJAM_API FHCJBotSkill
{
	/** Random aim offset, degrees (re-rolled every think). */
	float AimErrorDeg = 2.0f;
	/** Seconds from getting a target (or a new one) to the first shot. */
	float ReactionDelay = 0.4f;
	/** Max turn rate of the aim, degrees per second. */
	float TurnRateDeg = 150.0f;
	/** The bot fires in bursts: seconds firing / seconds holding fire (0 pause = continuous). */
	float BurstSeconds = 2.0f;
	float BurstPause = 0.5f;
	/** Aggression: the share of EngageRange inside which it opens fire, and the hull fraction below which it breaks off for a repair pad. */
	float EngageRangeFactor = 1.0f;
	float RetreatHullFraction = 0.3f;
	/** Strength of the sideways strafe while attacking, 0..1 (a weak strafe is easy to track). */
	float StrafeStrength = 0.6f;
	/**
	 * Phaser discipline: the bot holds the beam only while its aim is within this many degrees of the target (a
	 * careless pilot sprays the bank into space), and lets go when the bank is down to this share unless the
	 * target's hull is nearly gone (so that it is not locked out in the middle of a fight).
	 */
	float PhaserConeDeg = 4.0f;
	float PhaserReserve = 0.0f;
	/**
	 * Ship systems, as chances: to warp on a long way to a portal / to flee by warp when the hull is low (rolled once
	 * per trip / per retreat), to raise the cloak for an approach or a retreat and to use the tractor beam on a target
	 * in reach (rolled once a second), and to boost free when a beam catches the bot (rolled once per catch).
	 */
	float WarpTravelChance = 0.5f;
	float WarpFleeChance = 0.3f;
	float CloakChance = 0.1f;
	float TractorChance = 0.08f;
	float BreakFreeChance = 0.7f;
	/**
	 * Tactics, 0..1: how often a bot makes the clever choice where there is one (rolled per occasion) - the target by
	 * threat and weakness instead of the nearest, breaking off behind a rock when its shields are gone, turning on a
	 * ship that shoots it in the back, the tractor beam before a torpedo, flying its ship type's own way, shooting a
	 * small rock out of the line of fire, keeping out of a station's reach.
	 */
	float Tactics = 0.6f;
	/** Chance per decision (a few times a second) to put a torpedo through a facing of the target that is DOWN (the phasers opened it). */
	float ComboChance = 0.3f;

	// ---- fairness towards people (see docs/DEVLOG_NOTES.md, item 29) ----
	// These apply only when the ship a bot deals with is flown by a person; bots among themselves are untouched. The
	// defaults here are "off" - what the bots did before, and what the Ace level and -HCJNoFair still do.

	/** At most this many bots attack one human ship at a time; one the human shot may always answer (0 = no limit). */
	int32 MaxAttackersOnHuman = 0;
	/** Bots that travel stop routing to a human's sector once it holds this many bot ships (0 = no limit). */
	int32 MaxHuntersPerHuman = 0;
	/** A bot that is in a human's rear arc holds its fire for this long after getting there (seconds), and its aim error is multiplied by RearAimErrorScale while it is there. */
	float RearFireDelay = 0.0f;
	float RearAimErrorScale = 1.0f;
	/** How strongly a bot that fights a human slides round to the front of it, 0..1 (added to the strafe). */
	float FrontBias = 0.0f;
	/** Target score of a human ship that shows the bot its tail (bots among themselves: +1.5, "it is busy with somebody else"). */
	float HumanTailScore = 1.5f;
	/** Flank tactics against humans: the scout's approach under cloak to the tail, the boosted close pass. */
	bool bFlankHumans = true;
	/** Seconds after a human ship appears in a sector (spawn, portal, wormhole) before a bot may pick it as a target; also while its spawn shield is up. */
	float ArrivalGrace = 0.0f;
	/**
	 * A breather between fights: once no bot is attacking a human ship any more (its attacker was destroyed, broke off or
	 * lost it), no other bot may pick it for this many seconds - time for its shields to come back and its pilot to look around.
	 */
	float RespiteSeconds = 0.0f;
	/** Damage a bot does to a human ship is multiplied by this on the server (every weapon). */
	float DamageToHumanScale = 1.0f;
	/** A bot that would fire a photon torpedo at a human ship does so with this chance (a torpedo through an open facing nearly ends a ship). */
	float TorpedoOnHumanChance = 1.0f;

	// ---- help for a beginner (docs/DEVLOG_NOTES.md, item 38) ----
	// Easy only; like the fairness fields these are about ships flown by a person, are "off" by default and are
	// cleared by ClearFairness (-HCJNoFair) and, by themselves, by ClearBeginnerHelp (-HCJClassicEasy: Easy as it was in 0.20.0).

	/** Damage a person's ship does to a bot's ship is multiplied by this on the server (every weapon). */
	float DamageFromHumanScale = 1.0f;
	/** Hull points per second a person's ship mends by itself once nothing has hit it for HCJBalance::HullRegenDelay seconds (0 = never). */
	float HumanHullRegenRate = 0.0f;
	/** A repair pad restores this many times the usual amount on a person's ship. */
	float HumanRepairScale = 1.0f;

	/** Sets the fairness fields and the beginner's help back to the defaults above ("off"). */
	void ClearFairness();
	/** Sets only the beginner's help back to "off" and the damage scale of Easy to what it was before item 38. */
	void ClearBeginnerHelp();

	/** The row for Difficulty (0 easy, 1 normal, 2 hard, 3 ace; clamped). */
	static FHCJBotSkill Get(int32 Difficulty);
};

/** Things bots do on purpose; counted per match (AHCJGameMode::NotifyBotTactic, the "Tactics" status line and the playthrough tests). */
enum class EHCJBotTactic : uint8
{
	/** Broke off a fight: shields gone or hull low (for a repair pad, a rock, or just away). */
	Retreat,
	/** Came back to the fight after breaking off. */
	Reengage,
	/** Got a rock between itself and its attacker while breaking off. */
	Cover,
	/** Fired a torpedo at a facing of the target that was down. */
	ComboTorpedo,
	/** Held the target with the tractor beam and fired a torpedo at it. */
	TractorTorpedo,
	/** Turned on a ship that was shooting it from the side or from behind. */
	ThreatTurn,
	/** Shot at a small rock that blocked its line of fire. */
	RockShot,
	/** Took another target than the one most bots were already on. */
	SpreadTarget,
	/** Went to warp to reach a fight far away. */
	WarpChase,
	/** Steered out of the reach of a station's turrets. */
	StationSteer,
	/** A light ship started a fast, close pass around a heavier one. */
	ClosePass,
	/** Dropped its cloak behind a target. */
	Ambush,

	Count
};

namespace HCJBalance
{
	/** Bot difficulty levels: 0 easy, 1 normal, 2 hard, 3 ace (hard without the fairness rules). */
	inline constexpr int32 NumBotDifficulties = 4;
	/**
	 * "Behind": the direction to the other ship makes more than about 107 degrees with the nose (dot product below
	 * this). Used for the rear arc of a human ship (bots), of the view (HUD) and for the "Fair:" log lines.
	 */
	inline constexpr float RearArcDot = -0.3f;
	/** "From the side" in the death recap: more than 60 degrees off the nose (and not behind). */
	inline constexpr float SideArcDot = 0.5f;
	/** A bot that a human hit this recently may answer, whatever the attack cap, the grace or the rear hold say (seconds). */
	inline constexpr float ProvokedSeconds = 4.0f;
	/** Beginner's help: a person's hull starts to mend this long after the last damage (seconds), and mends up to this share of the full hull - the rest is what repair pads are for. */
	inline constexpr float HullRegenDelay = 5.0f;
	inline constexpr float HullRegenCeiling = 0.75f;
	/** DamageToHumanScale of Easy before item 38 (-HCJClassicEasy). */
	inline constexpr float ClassicEasyDamageToHumanScale = 0.5f;

	/** "Easy", "Normal", "Hard", "Ace" (clamped). */
	HYPECUBEJAM_API const TCHAR* GetDifficultyName(int32 Difficulty);
	/** One line for the menu: what the level does to the player (under 74 characters). */
	HYPECUBEJAM_API const TCHAR* GetDifficultyDescription(int32 Difficulty);
	/**
	 * Angle in degrees between Forward and the direction ToOther (0 dead ahead, 180 dead astern); -1 when ToOther has
	 * no length. Pure.
	 */
	HYPECUBEJAM_API float BearingDegrees(const FVector& Forward, const FVector& ToOther);
	/** Whether a bearing of BearingDegrees is in the rear arc (RearArcDot). */
	HYPECUBEJAM_API bool IsRearBearing(float BearingDeg);

	/** Name of a tactic for log lines ("retreats"). */
	HYPECUBEJAM_API const TCHAR* GetTacticName(EHCJBotTactic Tactic);
	/** Key of a tactic for the structured "Playthrough:" lines ("bot_retreats"). */
	HYPECUBEJAM_API const TCHAR* GetTacticKey(EHCJBotTactic Tactic);
}

/** What a possible spawn point looks like to the rules; filled by the game mode from the world. */
struct FHCJSpawnCandidate
{
	/** Distance to the nearest live hostile ship (uu); very large when there is none in the sector. */
	float NearestEnemyDistance = 1.0e9f;
	/** Live hostile ships in the sector of the point. */
	int32 EnemiesInSector = 0;
	/** Inside the range of a live station turret. */
	bool bInTurretRange = false;
	/** Pull of a black hole at the point (uu/s); 0 outside every gravity well. */
	float GravityPull = 0.0f;
};

namespace HCJBalance
{
	/** A ship that spawns closer than this to an enemy is in a fight before its pilot has looked around (uu). */
	inline constexpr float SpawnSafeEnemyDistance = 5000.0f;

	/**
	 * Score of a spawn point, higher is better. Never next to an enemy, never under a station's guns, never in a
	 * gravity well; among the safe points, prefer a sector where there is somebody to fight (one to three enemies)
	 * over an empty one, and distance to the nearest enemy up to a comfortable 12 000 uu. Pure.
	 */
	HYPECUBEJAM_API float ScoreSpawn(const FHCJSpawnCandidate& Candidate);
	/** Index of the best candidate (first of equals); INDEX_NONE for an empty list. */
	HYPECUBEJAM_API int32 PickBestSpawn(TConstArrayView<FHCJSpawnCandidate> Candidates);

	/**
	 * One step of the shield regeneration rule: the new shield value. Nothing happens for RegenDelay seconds after
	 * the last damage (SecondsSinceDamage), on a destroyed ship, or to a shield at or above MaxShield (an overcharge
	 * from a shield cell stays); otherwise RegenRate points per second up to MaxShield. Pure; the server runs it.
	 */
	HYPECUBEJAM_API float StepShieldRegen(float Shield, float MaxShield, float SecondsSinceDamage, float DeltaSeconds, float RegenDelay, float RegenRate, bool bDepleted);

	/**
	 * One step of the hull mending of a beginner's ship: the new hull value. Nothing happens for HullRegenDelay seconds
	 * after the last damage, on a destroyed ship, without a rate, or to a hull at or above HullRegenCeiling of MaxHull
	 * (a repair pad may have taken it higher: it stays); otherwise RegenRate points per second up to that ceiling.
	 * Pure; the server runs it for ships flown by a person.
	 */
	HYPECUBEJAM_API float StepHullRegen(float Hull, float MaxHull, float SecondsSinceDamage, float DeltaSeconds, float RegenRate, bool bDepleted);

	/**
	 * One step of the boost meter (energy 0..1). Drains over DrainSeconds while boosting, refills over
	 * RechargeSeconds otherwise. Once empty, boost stays locked until the meter is back at RelockLevel, so holding
	 * the key does not make the engine stutter. Returns whether boost is in effect during this step. Pure, and run
	 * with the same inputs by the owning client's prediction and the server.
	 */
	HYPECUBEJAM_API bool StepBoost(float& Energy, bool& bLocked, bool bWantsBoost, float DeltaSeconds, float DrainSeconds, float RechargeSeconds, float RelockLevel);

	/**
	 * The server's side of the boost meter for a ship flown by a remote client. The client's own meter decides which
	 * moves are boosted and says so in every move (bClientBoosting), so the two never disagree about a move because
	 * their meters differ by a frame; the server's meter only limits it: boost is refused once the server's meter is
	 * Grace below empty, or while it is locked and more than Grace short of the relock level. Returns whether this
	 * move is boosted.
	 */
	HYPECUBEJAM_API bool StepBoostFollow(float& Energy, bool& bLocked, bool bClientBoosting, float DeltaSeconds, float DrainSeconds, float RechargeSeconds,
		float RelockLevel, float Grace = 0.08f);
}
