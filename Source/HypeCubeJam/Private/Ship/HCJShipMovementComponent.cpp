#include "Ship/HCJShipMovementComponent.h"

#include "Game/HCJBalance.h"
#include "HypeCubeJam.h"
#include "GameFramework/Character.h"
#include "World/HCJUniverse.h"

namespace
{
	int32 GNumClientCorrections = 0;

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
			}
		}

		virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* C, APlayerController* PC, const FVector& OldStartLocation) override
		{
			Super::CombineWith(OldMove, C, PC, OldStartLocation);
			// The character was put back to the start of the old move: so is the meter, and this move now starts there.
			const FHCJSavedMove_Ship* Old = static_cast<const FHCJSavedMove_Ship*>(OldMove);
			SavedBoostEnergy = Old->SavedBoostEnergy;
			bSavedBoostLocked = Old->bSavedBoostLocked;
			if (UHCJShipMovementComponent* Movement = C ? Cast<UHCJShipMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				Movement->BoostEnergy = SavedBoostEnergy;
				Movement->bBoostLocked = bSavedBoostLocked;
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
			return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual uint8 GetCompressedFlags() const override
		{
			uint8 Result = Super::GetCompressedFlags();
			if (bSavedWantsToBoost)
			{
				Result |= FLAG_Custom_0;
			}
			return Result;
		}

	private:
		bool bSavedWantsToBoost = false;
		bool bSavedOverdrive = false;
		float SavedBoostEnergy = 1.0f;
		bool bSavedBoostLocked = false;
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
		return MaxFlySpeed * (bBoostActive ? BoostSpeedMultiplier : 1.0f) * (bOverdrive ? OverdriveSpeedMultiplier : 1.0f);
	}
	return Super::GetMaxSpeed();
}

float UHCJShipMovementComponent::GetMaxAcceleration() const
{
	return MaxAcceleration * (bBoostActive ? BoostAccelerationMultiplier : 1.0f) * (bOverdrive ? OverdriveAccelerationMultiplier : 1.0f);
}

void UHCJShipMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	if (CharacterOwner != nullptr && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		bBoostActive = HCJBalance::StepBoost(BoostEnergy, bBoostLocked, bWantsToBoost, DeltaSeconds, BoostDrainSeconds, BoostRechargeSeconds, BoostRelockLevel);
	}
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
	UE_LOG(LogHCJ, Verbose, TEXT("Correction %d: %.0f uu off, speed %.0f, boost %s (meter %.2f%s), overdrive %s, mode %d -> %d"), GNumClientCorrections,
		FVector::Dist(UpdatedComponent != nullptr ? UpdatedComponent->GetComponentLocation() : NewLocation, NewLocation), Velocity.Size(),
		IsBoostActive() ? TEXT("on") : TEXT("off"), GetBoostEnergy(), IsBoostLocked() ? TEXT(", locked") : TEXT(""), bOverdrive ? TEXT("on") : TEXT("off"),
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

	if (deltaTime <= 0.0f || UpdatedComponent == nullptr || CharacterOwner == nullptr || MovementMode != MOVE_Flying)
	{
		return;
	}
	const AHCJUniverse* Universe = AHCJUniverse::Get(CharacterOwner);
	if (Universe == nullptr || !Universe->IsBuilt())
	{
		return;
	}

	const FVector Drift = Universe->GetGravityDrift(UpdatedComponent->GetComponentLocation());
	if (Drift.IsNearlyZero())
	{
		return;
	}

	const FVector Delta = Drift * deltaTime;
	FHitResult Hit(1.0f);
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		// Pressed against a rock: keep sliding around it instead of sticking.
		SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
	}
}

void UHCJShipMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToBoost = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

bool UHCJShipMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	const bool bRealWantsToBoost = bWantsToBoost;
	const bool bRealOverdrive = bOverdrive;
	const float RealBoostEnergy = BoostEnergy;
	const bool bRealBoostLocked = bBoostLocked;
	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
	bWantsToBoost = bRealWantsToBoost;
	bOverdrive = bRealOverdrive;
	BoostEnergy = RealBoostEnergy;
	bBoostLocked = bRealBoostLocked;
	return bResult;
}
