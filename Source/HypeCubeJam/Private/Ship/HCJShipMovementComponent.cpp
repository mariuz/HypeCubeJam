#include "Ship/HCJShipMovementComponent.h"

#include "Game/HCJBalance.h"
#include "HypeCubeJam.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJUniverse.h"

namespace
{
	int32 GNumClientCorrections = 0;

	/** A ship at warp drops out this far (uu) from the sector origin when it is heading outwards (the safe radius is 26 000). */
	constexpr float WarpEdgeRadius = 22500.0f;
	/** Seconds of flight the drive looks ahead for rock. */
	constexpr float WarpLookAheadSeconds = 0.3f;

	/** Saved move that carries the boost intent in FLAG_Custom_0. */
	class FHCJSavedMove_Ship : public FSavedMove_Character
	{
	public:
		typedef FSavedMove_Character Super;

		virtual void Clear() override
		{
			Super::Clear();
			bSavedWantsToBoost = false;
			bSavedOverdrive = false;
			SavedBoostEnergy = 1.0f;
			bSavedBoostLocked = false;
			bSavedBoostActive = false;
			bSavedWantsToWarp = false;
			bSavedWarpActive = false;
			SavedWarp = FHCJWarpDrive();
		}

		virtual void PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode) override
		{
			Super::PostUpdate(C, PostUpdateMode);
			// What the move turned out to be: this, not the key, is what the server is told.
			if (const UHCJShipMovementComponent* Movement = C ? Cast<UHCJShipMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				bSavedBoostActive = Movement->bBoostActive;
				bSavedWarpActive = Movement->bMoveWasWarp;
			}
		}

		virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
			if (const UHCJShipMovementComponent* Movement = C ? Cast<UHCJShipMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				bSavedWantsToBoost = Movement->bWantsToBoost;
				bSavedOverdrive = Movement->bOverdrive;
				// The meter as it stands before this move is simulated.
				SavedBoostEnergy = Movement->BoostEnergy;
				bSavedBoostLocked = Movement->bBoostLocked;
				bSavedWantsToWarp = Movement->bWantsToWarp;
				SavedWarp = Movement->Warp;
			}
		}

		virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* C, APlayerController* PC, const FVector& OldStartLocation) override
		{
			Super::CombineWith(OldMove, C, PC, OldStartLocation);
			// The character was put back to the start of the old move: so is the meter, and this move now starts there.
			const FHCJSavedMove_Ship* Old = static_cast<const FHCJSavedMove_Ship*>(OldMove);
			SavedBoostEnergy = Old->SavedBoostEnergy;
			bSavedBoostLocked = Old->bSavedBoostLocked;
			SavedWarp = Old->SavedWarp;
			if (UHCJShipMovementComponent* Movement = C ? Cast<UHCJShipMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				Movement->BoostEnergy = SavedBoostEnergy;
				Movement->bBoostLocked = bSavedBoostLocked;
				Movement->Warp = SavedWarp;
			}
		}

		virtual void PrepMoveFor(ACharacter* C) override
		{
			Super::PrepMoveFor(C);
			if (UHCJShipMovementComponent* Movement = C ? Cast<UHCJShipMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				Movement->bWantsToBoost = bSavedWantsToBoost;
				// Replay with the multiplier this move was predicted with (not sent: the server uses its own).
				Movement->bOverdrive = bSavedOverdrive;
				Movement->BoostEnergy = SavedBoostEnergy;
				Movement->bBoostLocked = bSavedBoostLocked;
				Movement->bWantsToWarp = bSavedWantsToWarp;
				Movement->Warp = SavedWarp;
			}
		}

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
		{
			const FHCJSavedMove_Ship* Other = static_cast<const FHCJSavedMove_Ship*>(NewMove.Get());
			if (Other == nullptr || Other->bSavedWantsToBoost != bSavedWantsToBoost || Other->bSavedOverdrive != bSavedOverdrive
				|| Other->bSavedBoostLocked != bSavedBoostLocked || (Other->SavedBoostEnergy > 0.0f) != (SavedBoostEnergy > 0.0f))
			{
				return false;
			}
			// A move while the drive spools up or runs stays a move of its own: the jump starts and ends on a move boundary.
			if (Other->bSavedWantsToWarp != bSavedWantsToWarp || SavedWarp.IsBusy() || Other->SavedWarp.IsBusy() || bSavedWarpActive)
			{
				return false;
			}
			return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual uint8 GetCompressedFlags() const override
		{
			uint8 Result = Super::GetCompressedFlags();
			if (bSavedBoostActive)
			{
				Result |= FLAG_Custom_0;
			}
			if (bSavedWantsToWarp)
			{
				Result |= FLAG_Custom_1;
			}
			if (bSavedWarpActive)
			{
				Result |= FLAG_Custom_2;
			}
			return Result;
		}

	private:
		bool bSavedWantsToBoost = false;
		bool bSavedOverdrive = false;
		float SavedBoostEnergy = 1.0f;
		bool bSavedBoostLocked = false;
		/** Known once the move was simulated (PostUpdate): it was boosted / flown at warp. */
		bool bSavedBoostActive = false;
		bool bSavedWarpActive = false;
		bool bSavedWantsToWarp = false;
		/** The drive as it stood before this move. */
		FHCJWarpDrive SavedWarp;
	};

	class FHCJNetworkPredictionData_Client_Ship : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FHCJNetworkPredictionData_Client_Ship(const UCharacterMovementComponent& ClientMovement)
			: FNetworkPredictionData_Client_Character(ClientMovement)
		{
		}

		virtual FSavedMovePtr AllocateNewMove() override
		{
			return FSavedMovePtr(new FHCJSavedMove_Ship());
		}
	};
}

UHCJShipMovementComponent::UHCJShipMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWantsToBoost = false;
	bOverdrive = false;
	bBoostActive = false;
	bClientBoostActive = false;
	bClientWarpActive = false;
	bWantsToWarp = false;
	bWarpActive = false;
	bWarpBlocked = false;
	bWarpImpact = false;
	bMoveWasWarp = false;
	bWarpHit = false;

	DefaultLandMovementMode = MOVE_Flying;
	DefaultWaterMovementMode = MOVE_Flying;
	GravityScale = 0.0f;
	MaxFlySpeed = 3000.0f;
	MaxAcceleration = 6000.0f;
	// Enough drag to stop a cruise in under two seconds: a dogfight needs ships that answer the stick.
	BrakingDecelerationFlying = 1900.0f;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;

	// The collision sphere pitches with the view; never try to "step up" asteroids.
	MaxStepHeight = 0.0f;
	bEnablePhysicsInteraction = false;
}

float UHCJShipMovementComponent::GetMaxSpeed() const
{
	if (MovementMode == MOVE_Flying)
	{
		if (bWarpActive)
		{
			return WarpParams.Speed;
		}
		return MaxFlySpeed * (bBoostActive ? BoostSpeedMultiplier : 1.0f) * (bOverdrive ? OverdriveSpeedMultiplier : 1.0f)
			* (TractorHolder.IsValid() ? HCJTractor::SpeedScale : 1.0f);
	}
	return Super::GetMaxSpeed();
}

float UHCJShipMovementComponent::GetMaxAcceleration() const
{
	return MaxAcceleration * (bBoostActive ? BoostAccelerationMultiplier : 1.0f) * (bOverdrive ? OverdriveAccelerationMultiplier : 1.0f);
}

bool UHCJShipMovementComponent::IsFollowingClient() const
{
	return CharacterOwner != nullptr && CharacterOwner->HasAuthority() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy;
}

void UHCJShipMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	if (CharacterOwner == nullptr || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}

	// A remote client's own meter and drive decided what this move was; the server's copies are the limits.
	const bool bFollow = IsFollowingClient();
	bBoostActive = bFollow
		? HCJBalance::StepBoostFollow(BoostEnergy, bBoostLocked, bClientBoostActive, DeltaSeconds, BoostDrainSeconds, BoostRechargeSeconds, BoostRelockLevel)
		: HCJBalance::StepBoost(BoostEnergy, bBoostLocked, bWantsToBoost, DeltaSeconds, BoostDrainSeconds, BoostRechargeSeconds, BoostRelockLevel);

	const bool bBlocked = bWarpBlocked || TractorHolder.IsValid() || MovementMode != MOVE_Flying;
	bWarpActive = bFollow
		? Warp.StepFollow(bWantsToWarp, bClientWarpActive, bBlocked, DeltaSeconds, WarpParams)
		: Warp.Step(bWantsToWarp, bBlocked, DeltaSeconds, WarpParams);
	if (Warp.bJustEntered)
	{
		// Straight ahead: the thrust of this move (ControlledCharacterMove made it the nose), which the server gets with the move.
		Warp.Direction = Acceleration.GetSafeNormal(UE_SMALL_NUMBER, UpdatedComponent != nullptr ? UpdatedComponent->GetForwardVector() : FVector::ForwardVector);
	}
	if (bWarpActive && ShouldWarpDropOut())
	{
		// Rock ahead or the edge of the sector. A jump that never got going costs only the short cooldown.
		Warp.Cancel(Warp.Time < 0.25f ? WarpParams.AbortCooldown : WarpParams.Cooldown);
		bWarpActive = false;
	}
	if (Warp.bJustExited)
	{
		Velocity = Velocity.GetClampedToMaxSize(WarpParams.ExitSpeed);
	}
	bMoveWasWarp = bWarpActive;
	bWarpHit = false;
}

bool UHCJShipMovementComponent::ShouldWarpDropOut() const
{
	const UWorld* World = GetWorld();
	const AHCJUniverse* Universe = AHCJUniverse::Get(CharacterOwner);
	if (World == nullptr || UpdatedComponent == nullptr || Universe == nullptr || !Universe->IsBuilt())
	{
		return false;
	}
	const FVector Location = UpdatedComponent->GetComponentLocation();
	const int32 Sector = Universe->GetSectorAtLocation(Location);
	if (Sector != INDEX_NONE && HCJWarp::ShouldDropOutAtEdge(Location - Universe->GetSectorOrigin(Sector), Warp.Direction, WarpEdgeRadius))
	{
		return true;
	}
	// Rock, a station or a turret ahead (object type WorldStatic: the same on every machine).
	const float Radius = (CharacterOwner != nullptr ? CharacterOwner->GetSimpleCollisionRadius() : HCJ::ShipCollisionRadius) + 60.0f;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(HCJWarpAhead), false, CharacterOwner);
	return World->SweepTestByObjectType(Location, Location + Warp.Direction * (WarpParams.Speed * WarpLookAheadSeconds), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Radius), Params);
}

void UHCJShipMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (bWarpActive)
	{
		Velocity = Warp.Direction * WarpParams.Speed;
		return;
	}
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UHCJShipMovementComponent::ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds)
{
	// At warp, and in the move the jump may start on, the thrust is the nose: that is the direction of the jump.
	const bool bNose = UpdatedComponent != nullptr && bWantsToWarp
		&& (Warp.Phase == EHCJWarpPhase::Active || (Warp.Phase == EHCJWarpPhase::Charging && Warp.Time + 2.0f * DeltaSeconds >= WarpParams.ChargeSeconds));
	Super::ControlledCharacterMove(bNose ? UpdatedComponent->GetForwardVector() : InputVector, DeltaSeconds);
}

void UHCJShipMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
	if (bWarpActive)
	{
		bWarpHit = true;
	}
}

void UHCJShipMovementComponent::CancelWarp(float CooldownSeconds)
{
	if (Warp.IsBusy())
	{
		const bool bWasActive = Warp.Phase == EHCJWarpPhase::Active;
		Warp.Cancel(CooldownSeconds);
		bWarpActive = false;
		if (bWasActive)
		{
			Velocity = Velocity.GetClampedToMaxSize(WarpParams.ExitSpeed);
		}
	}
}

bool UHCJShipMovementComponent::ConsumeWarpImpact()
{
	const bool bImpact = bWarpImpact;
	bWarpImpact = false;
	return bImpact;
}

void UHCJShipMovementComponent::SetTractor(AActor* Holder, float PullSpeed)
{
	TractorHolder = Holder;
	TractorPullSpeed = Holder != nullptr ? FMath::Max(PullSpeed, 0.0f) : 0.0f;
}

void UHCJShipMovementComponent::SetFlightModel(float InCruiseSpeed, float InAcceleration, float InBraking, float InBoostDrainSeconds, float InBoostRechargeSeconds)
{
	MaxFlySpeed = FMath::Max(InCruiseSpeed, 100.0f);
	MaxAcceleration = FMath::Max(InAcceleration, 100.0f);
	BrakingDecelerationFlying = FMath::Max(InBraking, 0.0f);
	BoostDrainSeconds = FMath::Max(InBoostDrainSeconds, 0.1f);
	BoostRechargeSeconds = FMath::Max(InBoostRechargeSeconds, 0.1f);
}

void UHCJShipMovementComponent::SetOverdrive(bool bNewOverdrive, float SpeedMultiplier, float AccelerationMultiplier)
{
	bOverdrive = bNewOverdrive;
	OverdriveSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 1.0f, 3.0f);
	OverdriveAccelerationMultiplier = FMath::Clamp(AccelerationMultiplier, 1.0f, 3.0f);
}

int32 UHCJShipMovementComponent::GetNumClientCorrections()
{
	return GNumClientCorrections;
}

void UHCJShipMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation,
	FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase,
	bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	++GNumClientCorrections;
	// What kind of correction: a small slip, or a jump (respawn, portal). Only with -LogCmds="LogHCJ Verbose".
	static const TCHAR* WarpNames[] = { TEXT("idle"), TEXT("charging"), TEXT("at warp"), TEXT("cooling down") };
	UE_LOG(LogHCJ, Verbose, TEXT("Correction %d: %.0f uu off, speed %.0f, boost %s (meter %.2f%s), overdrive %s, warp %s, tractor %s, mode %d -> %d"), GNumClientCorrections,
		FVector::Dist(UpdatedComponent != nullptr ? UpdatedComponent->GetComponentLocation() : NewLocation, NewLocation), Velocity.Size(),
		IsBoostActive() ? TEXT("on") : TEXT("off"), GetBoostEnergy(), IsBoostLocked() ? TEXT(", locked") : TEXT(""), bOverdrive ? TEXT("on") : TEXT("off"),
		WarpNames[FMath::Min<int32>(static_cast<int32>(Warp.Phase), 3)], TractorHolder.IsValid() ? TEXT("held") : TEXT("free"),
		static_cast<int32>(MovementMode.GetValue()), static_cast<int32>(ServerMovementMode));
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewMovementBaseInterfaceData, NewBaseBoneName, bHasBase,
		bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
}

FNetworkPredictionData_Client* UHCJShipMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UHCJShipMovementComponent* MutableThis = const_cast<UHCJShipMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FHCJNetworkPredictionData_Client_Ship(*this);
	}
	return ClientPredictionData;
}

void UHCJShipMovementComponent::PhysFlying(float deltaTime, int32 Iterations)
{
	Super::PhysFlying(deltaTime, Iterations);

	if (bWarpHit)
	{
		// Ran into something at warp (a ship, or rock the look-ahead did not see): the jump is over.
		bWarpHit = false;
		if (bWarpActive)
		{
			bWarpActive = false;
			Warp.Cancel(WarpParams.Cooldown);
			Velocity = Velocity.GetClampedToMaxSize(0.5f * WarpParams.ExitSpeed);
			if (CharacterOwner != nullptr && CharacterOwner->HasAuthority())
			{
				bWarpImpact = true;
			}
		}
	}

	if (deltaTime <= 0.0f || UpdatedComponent == nullptr || CharacterOwner == nullptr || MovementMode != MOVE_Flying)
	{
		return;
	}

	const auto Drift = [this](const FVector& Delta)
	{
		if (Delta.IsNearlyZero())
		{
			return;
		}
		FHitResult Hit(1.0f);
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
		if (Hit.IsValidBlockingHit())
		{
			// Pressed against a rock: keep sliding around it instead of sticking.
			SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
		}
	};

	const AHCJUniverse* Universe = AHCJUniverse::Get(CharacterOwner);
	if (Universe != nullptr && Universe->IsBuilt())
	{
		Drift(Universe->GetGravityDrift(UpdatedComponent->GetComponentLocation()) * deltaTime);
	}

	// Held by a tractor beam: pulled towards the holder, as this machine sees it.
	if (const AActor* Holder = TractorHolder.Get())
	{
		Drift(HCJTractor::ComputePull(UpdatedComponent->GetComponentLocation(), Holder->GetActorLocation(), TractorPullSpeed, deltaTime));
	}
}

void UHCJShipMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bClientBoostActive = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bClientWarpActive = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
	bWantsToWarp = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	if (IsFollowingClient())
	{
		// (For WantsToBoost() on the server; the meter itself follows bClientBoostActive.)
		bWantsToBoost = bClientBoostActive;
	}
}

bool UHCJShipMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	const bool bRealWantsToBoost = bWantsToBoost;
	const bool bRealOverdrive = bOverdrive;
	const float RealBoostEnergy = BoostEnergy;
	const bool bRealBoostLocked = bBoostLocked;
	const bool bRealWantsToWarp = bWantsToWarp;
	const FHCJWarpDrive RealWarp = Warp;
	const bool bRealWarpActive = bWarpActive;
	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
	bWantsToWarp = bRealWantsToWarp;
	Warp = RealWarp;
	bWarpActive = bRealWarpActive;
	bWantsToBoost = bRealWantsToBoost;
	bOverdrive = bRealOverdrive;
	BoostEnergy = RealBoostEnergy;
	bBoostLocked = bRealBoostLocked;
	return bResult;
}
