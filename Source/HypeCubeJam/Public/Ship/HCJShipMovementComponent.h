#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Ship/HCJShipSystems.h"
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
 * Boost on the wire: FLAG_Custom_0 carries whether the move WAS boosted (the client's meter decided), not the key. The
 * server keeps its own meter only as a limit (HCJBalance::StepBoostFollow). Before, both ends ran the full meter rule
 * and disagreed about a few moves every time it ran dry or unlocked - a burst of small corrections.
 *
 * Warp drive (Ship/HCJShipSystems.h, FHCJWarpDrive): part of the simulation in the same way. The key is FLAG_Custom_1,
 * "this move was flown at warp" is FLAG_Custom_2; the saved moves keep the drive's state. At warp CalcVelocity gives
 * the ship a fixed velocity along the direction it had when the jump started. The drive drops out by itself before
 * rock (a sphere sweep ahead), at the edge of the playable sector and on an impact; all three are functions of the
 * ship's position and of static geometry, so both ends decide alike.
 *
 * Tractor beam: a ship held by a beam (SetTractor, from the replicated AHCJShipCharacter::TractorHolder) is slowed
 * and pulled towards the holder in PhysFlying - HCJTractor::ComputePull, a function of the two positions, like the
 * drift of a black hole. The owning client uses the holder's replicated position, which lags the server's by its
 * interpolation delay, so a held ship gets small corrections while it is pulled.
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

	// ---- ship type (AHCJShipCharacter::ApplyShipType, same values on every machine) ----
	/** Cruise speed, acceleration, braking and the boost meter's times of the ship's type. */
	void SetFlightModel(float InCruiseSpeed, float InAcceleration, float InBraking, float InBoostDrainSeconds, float InBoostRechargeSeconds);

	// ---- warp drive ----
	/** Warp key. Call on the machine that controls the pawn, like SetWantsToBoost. */
	void SetWantsToWarp(bool bNewWantsToWarp) { bWantsToWarp = bNewWantsToWarp; }
	bool WantsToWarp() const { return bWantsToWarp; }
	/** The drive as this machine simulates it (owning client, server). */
	const FHCJWarpDrive& GetWarpDrive() const { return Warp; }
	const FHCJWarpParams& GetWarpParams() const { return WarpParams; }
	void SetWarpParams(const FHCJWarpParams& InParams) { WarpParams = InParams; }
	/** This move is flown at warp. */
	bool IsWarpActive() const { return bWarpActive; }
	/** Something forbids warp (the ship is cloaked). Set by the ship on the server and on the owning client. */
	void SetWarpBlocked(bool bBlocked) { bWarpBlocked = bBlocked; }
	/** Server (a hit while charging, a tractor beam, a portal) and owning client (told by the server): the drive stops, with a cooldown. */
	void CancelWarp(float CooldownSeconds);
	/** Server: the ship hit something at warp since the last call (the drive has already dropped out). */
	bool ConsumeWarpImpact();
	/** A new ship: drive idle. */
	void ResetWarp() { Warp = FHCJWarpDrive(); bWarpActive = false; bWarpImpact = false; }

	// ---- tractor beam (the ship held by one) ----
	/** Server and every client that knows the holder: the actor whose beam holds this ship (nullptr: none) and how hard it pulls. */
	void SetTractor(AActor* Holder, float PullSpeed);
	bool IsTractored() const { return TractorHolder.IsValid(); }

	/** Steps the boost meter and the warp drive for this move (owning client, server; not simulated proxies). */
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** MaxFlySpeed * (boost active ? BoostSpeedMultiplier : 1) while flying; Super otherwise. */
	virtual float GetMaxSpeed() const override;
	/** MaxAcceleration * (bWantsToBoost ? BoostAccelerationMultiplier : 1). */
	virtual float GetMaxAcceleration() const override;
	/** At warp: the fixed warp velocity instead of thrust and drag. */
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	/** At warp the ship flies straight ahead whatever the stick says. */
	virtual void ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds) override;
	/** Hitting something at warp ends the warp. */
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f, const FVector& MoveDelta = FVector::ZeroVector) override;
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

	/** Server, for a ship flown by a remote client: what the last received move said (FLAG_Custom_0 / FLAG_Custom_2). */
	uint8 bClientBoostActive : 1;
	uint8 bClientWarpActive : 1;

	/** Warp key (FLAG_Custom_1), the drive, whether the current move is at warp; public for the saved-move class. */
	uint8 bWantsToWarp : 1;
	uint8 bWarpActive : 1;
	uint8 bWarpBlocked : 1;
	uint8 bWarpImpact : 1;
	/** The move just simulated was flown at warp (what FLAG_Custom_2 of its saved move says), and it ran into something. */
	uint8 bMoveWasWarp : 1;
	uint8 bWarpHit : 1;
	FHCJWarpDrive Warp;
	FHCJWarpParams WarpParams;

	/** The ship whose tractor beam holds this one, and its pull (uu/s). */
	TWeakObjectPtr<AActor> TractorHolder;
	float TractorPullSpeed = 0.0f;

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

	/** The warp drive must drop out now: rock ahead or the edge of the playable sector. */
	bool ShouldWarpDropOut() const;
	/** The server is simulating the move of a remote client (its own ships decide for themselves). */
	bool IsFollowingClient() const;

	/** Server: what the received move says - boosted (FLAG_Custom_0), warp key (FLAG_Custom_1), flown at warp (FLAG_Custom_2). */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	/** Owning client: keeps the live boost intent across a correction replay (PrepMoveFor overwrites it per replayed move). */
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
};
