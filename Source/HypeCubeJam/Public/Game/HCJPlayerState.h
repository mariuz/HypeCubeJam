#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Ship/HCJShipTypes.h"
#include "HCJPlayerState.generated.h"

/**
 * Per-combatant replicated score line. Humans and bots both have one (AHCJBotController sets bWantsPlayerState;
 * the engine marks it IsABot() automatically). Always relevant, so it is also the only way a client learns about
 * combatants in other sectors (their ships are net-culled).
 *
 * Authority: every mutator below is SERVER ONLY and is called only by AHCJGameMode. APlayerState::Score mirrors Kills.
 */
UCLASS()
class HYPECUBEJAM_API AHCJPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AHCJPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Carries Kills/Deaths over when the engine copies a player state (seamless travel / reconnect). */
	virtual void CopyProperties(APlayerState* PlayerState) override;
	/** Engine reset hook: same as ResetMatchStats. */
	virtual void Reset() override;

	int32 GetKills() const { return Kills; }
	int32 GetDeaths() const { return Deaths; }
	/** Station turrets destroyed this match (a small score next to the kills: it breaks ties, it does not count towards the frag limit). */
	int32 GetTurretKills() const { return TurretKills; }
	/** SERVER ONLY. */
	void AddTurretKill();

	/** Sector the combatant's ship is in, INDEX_NONE while it has no live ship. Updated by the server ~4x per second. */
	int32 GetCurrentSector() const { return CurrentSector; }

	/** True while the combatant has no live ship and a respawn is scheduled. */
	bool IsWaitingToRespawn() const { return RespawnServerTime > 0.0f; }
	/** Server-clock time (AGameStateBase::GetServerWorldTimeSeconds) of the automatic respawn; 0 when alive. */
	float GetRespawnServerTime() const { return RespawnServerTime; }
	/** Server-clock time from which a manual respawn request is accepted; 0 when alive. */
	float GetEarliestRespawnServerTime() const { return EarliestRespawnServerTime; }

	/**
	 * The ship this combatant chose (EHCJShipType, hull within the type, paint; HCJShipTypes::Auto = derived from the
	 * player id / colour). Replicated to everybody: scoreboard, colour. The next ship the server spawns for it is of
	 * this type (AHCJShipCharacter carries what it actually is).
	 */
	uint8 GetShipType() const { return ShipType; }
	uint8 GetShipVariant() const { return ShipVariant; }
	uint8 GetShipSkin() const { return ShipSkin; }
	/** SERVER ONLY: stores a choice after validation (HCJShipTypes::Sanitize*). */
	void SetShipChoice(int32 InType, int32 InVariant, int32 InSkin);

	/**
	 * Display colour: the colour of the chosen paint (lifted when it is very dark), else derived from GetPlayerId()
	 * only (golden-ratio hue, full saturation). Identical on every machine. Used for the hull tint, scoreboard rows
	 * and hypercube map dots.
	 */
	FLinearColor GetPlayerColor() const;

	// ---- server-only mutators (AHCJGameMode) ----

	/** Kills += 1 and SetScore(Kills). */
	void AddKill();
	/** Deaths += 1. */
	void AddDeath();
	/** Kills = Deaths = 0, Score = 0. Called for everyone when a new match starts. */
	void ResetMatchStats();
	void SetCurrentSector(int32 NewSector);
	/** Pass (0, 0) when the combatant gets a ship. */
	void SetRespawnTimes(float InEarliestRespawnServerTime, float InRespawnServerTime);

protected:
	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;

	UPROPERTY(Replicated)
	int32 TurretKills = 0;

	UPROPERTY(Replicated)
	int32 CurrentSector = INDEX_NONE;

	UPROPERTY(Replicated)
	float EarliestRespawnServerTime = 0.0f;

	UPROPERTY(Replicated)
	float RespawnServerTime = 0.0f;

	UPROPERTY(Replicated)
	uint8 ShipType = static_cast<uint8>(EHCJShipType::Fighter);

	UPROPERTY(Replicated)
	uint8 ShipVariant = HCJShipTypes::Auto;

	UPROPERTY(Replicated)
	uint8 ShipSkin = HCJShipTypes::Auto;
};
