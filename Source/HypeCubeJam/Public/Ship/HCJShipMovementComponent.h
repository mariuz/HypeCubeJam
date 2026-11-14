#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HCJShipMovementComponent.generated.h"

/**
 * Zero-gravity flying movement for AHCJShipCharacter with a client-predicted boost.
 *
 * Constructor defaults (the ship relies on them): DefaultLandMovementMode = DefaultWaterMovementMode = MOVE_Flying,
 * GravityScale = 0, MaxFlySpeed = 3000 uu/s, MaxAcceleration = 6000 uu/s^2, BrakingDecelerationFlying = 1200,
 * bOrientRotationToMovement = false, bUseControllerDesiredRotation = false (the pawn follows the control
 * rotation through bUseControllerRotationPitch/Yaw), NavAgentProps irrelevant (no navmesh).
 *
 * Boost: bWantsToBoost travels to the server inside the saved-move compressed flags (FSavedMove_Character::FLAG_Custom_0),
 * so the owning client and the server compute the same GetMaxSpeed/GetMaxAcceleration for every move and
 * boosting causes no corrections. The FSavedMove / FNetworkPredictionData_Client subclasses are private to the .cpp.
 *
 * Boost meter: boost is a charge (BoostEnergy 0..1) that drains while boosting and refills otherwise
 * (HCJBalance::StepBoost; once empty it stays locked until it has refilled to BoostRelockLevel). The meter is part
 * of the movement simulation: UpdateCharacterStateBeforeMovement steps it for every move on the owning client and
 * on the server from the same inputs (boost intent, delta time), so both agree on when boost is in effect without
 * anything extra being sent. The client's saved moves remember the meter at their start, so combined moves and
 * correction replays re-run with the values they were predicted with. The HUD reads the local component.
 *
 * Overdrive (power-up): a second speed / acceleration multiplier. Unlike boost it is NOT the client's to claim:
 * the server switches it on and off (AHCJShipCharacter, replicated power-up flags) and uses its own value for every
 * move; the owning client applies the replicated flag to its prediction and stores it in each saved move so that
 * a correction replays every move with the multiplier it was predicted with. The two ends disagree only for the
 * moves in flight when the flag flips (a handful of small corrections at the start and the end of an overdrive).
 * The multipliers come from the replicated match config, so both ends use the same numbers.
 *
 * Black holes: see PhysFlying.
 */
UCLASS()
class HYPECUBEJAM_API UHCJShipMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UHCJShipMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Sets the boost intent. Call on the machine that controls the pawn: the owning client for players
	 * (replicated to the server via saved moves), the server for bots. Not meaningful on simulated proxies.
	 */
	void SetWantsToBoost(bool bNewWantsToBoost) { bWantsToBoost = bNewWantsToBoost; }
	bool WantsToBoost() const { return bWantsToBoost; }

	/** Charge left, 0..1 (owning client and server; meaningless on simulated proxies). */
	float GetBoostEnergy() const { return BoostEnergy; }
	/** Boost is in effect for the current move: wanted, charged and not locked. */
	bool IsBoostActive() const { return bBoostActive; }
	/** Empty and not yet refilled to the relock level. */
	bool IsBoostLocked() const { return bBoostLocked; }

	/** Steps the boost meter for this move (owning client, server; not simulated proxies). */
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** MaxFlySpeed * (boost active ? BoostSpeedMultiplier : 1) while flying; Super otherwise. */
	virtual float GetMaxSpeed() const override;
	/** MaxAcceleration * (bWantsToBoost ? BoostAccelerationMultiplier : 1). */
	virtual float GetMaxAcceleration() const override;
	/** Lazily allocates the prediction data that creates boost-aware saved moves. */
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/** Un-boosted cruise speed (MaxFlySpeed), uu/s. Used by bots for arrival/lead estimates. */
	float GetCruiseSpeed() const { return MaxFlySpeed; }

	/** Server and owning client (from the replicated power-up flags): switches the overdrive multipliers on or off. */
	void SetOverdrive(bool bNewOverdrive, float SpeedMultiplier, float AccelerationMultiplier);
	bool IsOverdriven() const { return bOverdrive; }

	/** Movement corrections this process received from a server for its own ships since it started (for logs). */
	static int32 GetNumClientCorrections();

	/** Counts the correction, then the engine's handling. */
	virtual void OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation,
		FVector NewVelocity, struct FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase,
		bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection) override;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Movement")
	float BoostSpeedMultiplier = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Movement")
	float BoostAccelerationMultiplier = 2.0f;

	/** Seconds of boost in a full meter, seconds to refill an empty one, and the charge at which a drained meter works again. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Movement")
	float BoostDrainSeconds = 3.5f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Movement")
	float BoostRechargeSeconds = 7.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Movement")
	float BoostRelockLevel = 0.25f;

	/** The boost meter; public for the saved-move class. Not replicated: both ends simulate it. */
	float BoostEnergy = 1.0f;
	bool bBoostLocked = false;
	uint8 bBoostActive : 1;

	/** Boost intent (initialised to false in the constructor); public so the saved-move class in the .cpp can read and restore it. Not replicated. */
	uint8 bWantsToBoost : 1;

	/** Overdrive active (see the class comment); public for the saved-move class. Not replicated here: the ship's power-up flags are. */
	uint8 bOverdrive : 1;

	float OverdriveSpeedMultiplier = 1.0f;
	float OverdriveAccelerationMultiplier = 1.0f;

protected:
	/**
	 * Flying physics plus the black hole: after the normal step the ship is moved (swept, sliding along rocks) by
	 * AHCJUniverse::GetGravityDrift(position) * deltaTime. Velocity is left alone: the pull is a drift on top of the
	 * ship's own motion, a pure function of where it is. This runs for every simulated move - the owning client's
	 * prediction, the server's processing of that client's moves, the replay after a correction, and the server's
	 * own ships (bots, listen host) - so both ends compute the same thing and gravity causes no corrections.
	 * Simulated proxies just follow replication.
	 */
	virtual void PhysFlying(float deltaTime, int32 Iterations) override;

	/** Server: restores bWantsToBoost from FLAG_Custom_0 of the received move. */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	/** Owning client: keeps the live boost intent across a correction replay (PrepMoveFor overwrites it per replayed move). */
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
};
