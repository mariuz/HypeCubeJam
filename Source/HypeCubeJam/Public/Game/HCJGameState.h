#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Hypercube/HCJMatchConfig.h"
#include "Game/HCJUiLogic.h"
#include "HCJGameState.generated.h"

class AHCJPlayerState;
class AHCJUniverse;
struct FHCJHypercubeGraph;

/** Phase of the endless match loop; owned by AHCJGameMode, replicated through AHCJGameState. */
UENUM()
enum class EHCJMatchPhase : uint8
{
	/** Ships fly and fight; kills count. */
	Playing,
	/** Match over: no ships exist, scoreboard is forced on; a new match (new seed) starts when the phase ends. */
	Intermission
};

/** One line of the local kill feed (not replicated; filled from MulticastKillEvent). */
struct FHCJKillFeedEntry
{
	/** Empty for an environment death / suicide. */
	FString KillerName;
	FString VictimName;
	/** Weapon name or what else did it ("TURRET", "BLACK HOLE", "THE VOID", "TURRET DOWN"); may be empty. */
	FString Cause;
	/** Local GetWorld()->GetTimeSeconds() when the event arrived. */
	float LocalTime = 0.0f;
	/** EHCJShipType of the killer's and the victim's ship; 15 = none / unknown. */
	uint8 KillerType = 15;
	uint8 VictimType = 15;
	/** The local player is the killer / the victim (rows are highlighted). */
	bool bLocalKiller = false;
	bool bLocalVictim = false;
};

/** A big line in the middle of the local HUD (kill, killed by, streak, match start); not replicated. */
struct FHCJCenterMessage
{
	FString Text;
	FString SubText;
	FLinearColor Color = FLinearColor::White;
	float LocalTime = -1000.0f;
};

/**
 * Replicated match state and the owner of the local AHCJUniverse.
 *
 * SEED -> WORLD: MatchConfig is the only replicated world-generation state.
 *  - Server: AHCJGameMode calls SetMatchConfig (first from InitGameState, then once per new match).
 *    SetMatchConfig stores the config, calls ApplyMatchConfig immediately and ForceNetUpdate.
 *  - Clients: OnRep_MatchConfig -> ApplyMatchConfig. The default-constructed config is invalid
 *    (Dimension 0) so the OnRep always fires for late joiners once the first valid config arrives.
 *  - ApplyMatchConfig (every machine): if no universe exists, spawn one locally
 *    (GetWorld()->SpawnActor<AHCJUniverse>() at the origin, no owner, not replicated), then
 *    Universe->Rebuild(MatchConfig), then broadcast OnMatchConfigApplied.
 */
UCLASS()
class HYPECUBEJAM_API AHCJGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AHCJGameState();

	/** The game state of WorldContextObject's world, or nullptr (e.g. on a client before it replicated). */
	static AHCJGameState* Get(const UObject* WorldContextObject);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---- world generation ----

	/** SERVER ONLY. Publishes a new match config and rebuilds the server's universe synchronously. */
	void SetMatchConfig(const FHCJMatchConfig& NewConfig);
	const FHCJMatchConfig& GetMatchConfig() const { return MatchConfig; }

	/** Local universe; nullptr until the first valid config was applied on this machine. */
	AHCJUniverse* GetUniverse() const { return Universe; }
	/** The built match graph, or nullptr while no universe is built on this machine. */
	const FHCJHypercubeGraph* GetGraph() const;

	// ---- destroyed asteroids (World/HCJAsteroidField.h) ----

	/**
	 * SERVER ONLY, called by AHCJUniverse when a rock's health is used up: adds Key to the replicated list of
	 * destroyed rocks, applies it to the server's universe (the pieces appear) and tells the game mode. Clients get
	 * the list by replication (OnRep_DestroyedAsteroids) and apply what is new; a late joiner gets all of it at once.
	 */
	void ServerDestroyAsteroid(uint32 Key, AController* DamageInstigator);
	/** Keys (HCJAsteroids::PackKey) of the rocks destroyed in the current match, in the order it happened. */
	const TArray<uint32>& GetDestroyedAsteroids() const { return DestroyedAsteroids; }

	/** Fired on this machine after every ApplyMatchConfig (universe rebuilt for a new match). */
	FSimpleMulticastDelegate OnMatchConfigApplied;

	// ---- match phase ----

	/**
	 * SERVER ONLY. Sets the phase, its end time (now + DurationSeconds; DurationSeconds <= 0 means "no time limit",
	 * PhaseEndServerTime = 0) and the winner shown during Intermission (null while Playing).
	 */
	void SetMatchPhase(EHCJMatchPhase NewPhase, float DurationSeconds, AHCJPlayerState* Winner);
	EHCJMatchPhase GetMatchPhase() const { return MatchPhase; }
	/** Seconds until the current phase ends, by the synchronised server clock; 0 when elapsed or unlimited. */
	float GetPhaseTimeRemaining() const;
	/** True when the current phase has an end time. */
	bool HasPhaseTimeLimit() const { return PhaseEndServerTime > 0.0f; }
	AHCJPlayerState* GetMatchWinner() const { return MatchWinner; }

	/** SERVER ONLY. Kills needed to win a match; 0 = no frag limit. */
	void SetFragLimit(int32 NewFragLimit);
	int32 GetFragLimit() const { return FragLimit; }

	// ---- scoreboard / feed ----

	/** All AHCJPlayerStates (humans and bots) sorted by kills descending, then deaths ascending, then name. */
	void GetSortedPlayerStates(TArray<AHCJPlayerState*>& OutPlayerStates) const;

	/**
	 * Called by the server for every death; runs on every machine and appends to the local kill feed
	 * (capped at MaxKillFeedEntries, oldest dropped). KillerName empty = environment death / suicide.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastKillEvent(const FString& KillerName, const FString& VictimName, const FString& Cause, uint8 ShipTypes);

	/** The ShipTypes byte of MulticastKillEvent: killer's type in the low nibble, victim's in the high one; 15 = none. */
	static uint8 PackKillTypes(int32 KillerType, int32 VictimType)
	{
		const auto Nibble = [](int32 Type) { return static_cast<uint8>((Type >= 0 && Type < 15) ? Type : 15); };
		return static_cast<uint8>(Nibble(KillerType) | (Nibble(VictimType) << 4));
	}

	/** Local: shows a big centre line on this machine's HUD for a few seconds (replaces the one before). */
	void ShowCenterMessage(const FString& Text, const FString& SubText, const FLinearColor& Color);
	const FHCJCenterMessage& GetCenterMessage() const { return CenterMessage; }
	/** Local: kills of the local player without dying (for the HUD). */
	int32 GetLocalKillStreak() const { return LocalStreak.GetLifeStreak(); }

	/** Local kill feed, oldest first. The HUD hides entries older than a few seconds itself. */
	const TArray<FHCJKillFeedEntry>& GetKillFeed() const { return KillFeed; }

	static constexpr int32 MaxKillFeedEntries = 6;

protected:
	UFUNCTION()
	void OnRep_MatchConfig();

	/** Every machine: spawn-if-needed + Rebuild the local universe from MatchConfig (see class comment). */
	void ApplyMatchConfig();

	UPROPERTY(ReplicatedUsing = OnRep_MatchConfig)
	FHCJMatchConfig MatchConfig;

	UFUNCTION()
	void OnRep_DestroyedAsteroids();

	/** Every machine: brings the local universe up to the replicated list, when both belong to the same match. */
	void SyncDestroyedAsteroids();

	/**
	 * Rocks destroyed in the match DestroyedAsteroidsMatch: 4 bytes each, only ever appended to within a match
	 * (the engine sends the new elements), emptied when a new match starts.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_DestroyedAsteroids)
	TArray<uint32> DestroyedAsteroids;

	/** FHCJMatchConfig::MatchNumber the list belongs to: a list of the match before is never applied to a new universe. */
	UPROPERTY(ReplicatedUsing = OnRep_DestroyedAsteroids)
	int32 DestroyedAsteroidsMatch = 0;

	UPROPERTY(Replicated)
	EHCJMatchPhase MatchPhase = EHCJMatchPhase::Playing;

	/** GetServerWorldTimeSeconds() at which the phase ends; 0 = no limit. */
	UPROPERTY(Replicated)
	float PhaseEndServerTime = 0.0f;

	UPROPERTY(Replicated)
	int32 FragLimit = 0;

	UPROPERTY(Replicated)
	TObjectPtr<AHCJPlayerState> MatchWinner;

	/** Locally spawned, never replicated. Destroyed in EndPlay. */
	UPROPERTY(Transient)
	TObjectPtr<AHCJUniverse> Universe;

	TArray<FHCJKillFeedEntry> KillFeed;
	FHCJCenterMessage CenterMessage;
	/** Streak detection for the local player, fed from the kill events. */
	FHCJStreakTracker LocalStreak;
	int32 StreakMatchNumber = 0;
};
