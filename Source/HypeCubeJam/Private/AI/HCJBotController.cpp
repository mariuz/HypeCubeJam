#include "AI/HCJBotController.h"

#include "Game/HCJGameMode.h"

#include "Game/HCJGameplaySettings.h"
#include "Ship/HCJHealthComponent.h"
#include "Ship/HCJProjectile.h"
#include "Ship/HCJTorpedo.h"
#include "World/HCJPickup.h"
#include "World/HCJTurret.h"

#include "HypeCubeJam.h"
#include "Game/HCJPlayerState.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Ship/HCJShipCharacter.h"
#include "Ship/HCJShipMovementComponent.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJUniverse.h"

#include "CollisionQueryParams.h"
#include "Ship/HCJShipSystems.h"
#include "Ship/HCJShipTypes.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"

namespace
{
	/** Control pitch is kept off the poles so the yaw stays well defined. */
	constexpr double HCJBotMaxPitchDeg = 89.0;
	/** Time constant of the velocity-matching thrust controller, seconds. */
	constexpr float HCJBotVelocityResponseTime = 0.3f;
	/** Fraction of the max acceleration assumed available for braking when arriving. */
	constexpr float HCJBotArriveBrakeFraction = 0.5f;
	/** Lead prediction is capped so a far, fast target does not send the aim point across the sector. */
	constexpr float HCJBotMaxLeadSeconds = 4.0f;
	/** A wander point is abandoned after this long. */
	constexpr float HCJBotWanderPointLifetime = 14.0f;
	/** The bot switches to a clearly nearer hostile only when it is this much closer than the current target. */
	constexpr float HCJBotRetargetRatio = 0.6f;
	/** Sideways share of the thrust while circling a target. */
	constexpr float HCJBotStrafeStrength = 0.8f;

	FCollisionQueryParams MakeBotQueryParams(const AActor& IgnoredActor)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HCJBotQuery), false, &IgnoredActor);
		Params.bReturnPhysicalMaterial = false;
		return Params;
	}
}

AHCJBotController::AHCJBotController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bWantsPlayerState = true;
	// The control rotation is owned by TickAim, never derived from the pawn or a focus actor.
	bSetControlRotationFromPawnOrientation = false;
}

void AHCJBotController::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	BotRandom.Initialize(HCJ::MixSeed(static_cast<int32>(FPlatformTime::Cycles()), static_cast<int32>(GetUniqueID())));
}

AHCJShipCharacter* AHCJBotController::GetShip() const
{
	AHCJShipCharacter* Ship = Cast<AHCJShipCharacter>(GetPawn());
	return (IsValid(Ship) && !Ship->IsDead()) ? Ship : nullptr;
}

void AHCJBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	BotState = EHCJBotState::Wander;
	TargetShip.Reset();
	bHasAimPoint = false;
	bHasWanderPoint = false;
	WanderPointExpireTime = 0.0f;
	TravelDim = INDEX_NONE;
	TravelFromSector = INDEX_NONE;
	LastArrivalDim = INDEX_NONE;
	AimErrorOffset = FVector::ZeroVector;
	StrafeSign = (BotRandom.FRand() < 0.5f) ? -1.0f : 1.0f;
	BurstPhase = BotRandom.FRandRange(0.0f, 4.0f);
	TargetAcquiredTime = Now;
	bRetreating = false;
	EvadeUntilTime = 0.0f;
	NextEvadeDecisionTime = 0.0f;
	LastTorpedoTime = -1000.0f;
	NextSystemsTime = Now + BotRandom.FRandRange(2.0f, 4.0f);
	WarpUntilTime = 0.0f;
	bWarpFlee = false;
	WarpRolledForDim = INDEX_NONE;
	bWarpFleeRolled = false;
	TractorUntilTime = 0.0f;
	CloakUntilTime = 0.0f;
	bCaughtDecided = false;
	BoostFreeUntilTime = 0.0f;
	LostTargetFireUntil = 0.0f;
	NextStrafeFlipTime = Now + BotRandom.FRandRange(1.5f, 4.0f);
	StateEnterTime = Now;
	LastHostileSeenTime = Now;
	LastTargetVisibleTime = Now;
	// Spread the bots' think ticks over the interval.
	NextThinkTime = Now + BotRandom.FRand() * ThinkInterval;

	if (InPawn)
	{
		FRotator StartRotation = InPawn->GetActorRotation();
		StartRotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(StartRotation.Pitch), -HCJBotMaxPitchDeg, HCJBotMaxPitchDeg);
		StartRotation.Roll = 0.0;
		SetControlRotation(StartRotation);
	}
}

void AHCJBotController::OnUnPossess()
{
	// Also for a dead ship (GetShip() would hide it): it must not keep shooting during its death linger.
	if (AHCJShipCharacter* Ship = Cast<AHCJShipCharacter>(GetPawn()))
	{
		Ship->SetFiring(false);
		Ship->SetBoosting(false);
		Ship->SetWarping(false);
		Ship->SetTractoring(false);
	}

	TargetShip.Reset();
	bHasAimPoint = false;
	bHasWanderPoint = false;
	TravelDim = INDEX_NONE;
	TravelFromSector = INDEX_NONE;
	BotState = EHCJBotState::Wander;

	Super::OnUnPossess();
}

namespace
{
	/** Development: bots hold still and hold their fire (see SetDebugIdle). */
	bool GHCJBotsIdle = false;
}

void AHCJBotController::SetDebugIdle(bool bIdle)
{
	GHCJBotsIdle = bIdle;
}

void AHCJBotController::Tick(float DeltaTime)
{
	AHCJShipCharacter* Ship = GetShip();
	const UWorld* World = GetWorld();
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);

	if (GHCJBotsIdle)
	{
		if (Ship != nullptr)
		{
			// (Warp, cloak and tractor keys are left alone: the development commands of the pictures set them.)
			Ship->SetFiring(false);
			Ship->SetBoosting(false);
		}
		Super::Tick(DeltaTime);
		return;
	}

	if (Ship && World && Universe && Universe->IsBuilt() && Ship->GetCurrentSector() != INDEX_NONE)
	{
		const float Now = World->GetTimeSeconds();
		if (Now >= NextThinkTime)
		{
			NextThinkTime = Now + ThinkInterval;
			Think(*Ship, *Universe);
		}

		switch (BotState)
		{
		case EHCJBotState::Attack:
			TickAttack(DeltaTime, *Ship, *Universe);
			break;
		case EHCJBotState::Travel:
			TickTravel(DeltaTime, *Ship, *Universe);
			break;
		case EHCJBotState::Wander:
		default:
			TickWander(DeltaTime, *Ship, *Universe);
			break;
		}

		TickShieldFacing(*Ship);
		TickSystems(*Ship);
		TickAim(DeltaTime, *Ship);
	}
	else if (Ship)
	{
		// No world to fly in yet (universe not built): idle.
		Ship->SetFiring(false);
		Ship->SetBoosting(false);
	}

	// AAIController::Tick ends with UpdateControlRotation, which turns the ship to the new control rotation.
	Super::Tick(DeltaTime);
}

void AHCJBotController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	if (!bUpdatePawn)
	{
		return;
	}

	if (APawn* MyPawn = GetPawn())
	{
		MyPawn->FaceRotation(GetControlRotation(), DeltaTime);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Decisions
// ---------------------------------------------------------------------------------------------------------------

void AHCJBotController::SetBotState(EHCJBotState NewState)
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	AHCJShipCharacter* Ship = Cast<AHCJShipCharacter>(GetPawn());

	if (NewState != EHCJBotState::Attack)
	{
		TargetShip.Reset();
		AimErrorOffset = FVector::ZeroVector;
		if (Ship)
		{
			Ship->SetFiring(false);
		}
	}

	if (NewState != EHCJBotState::Travel)
	{
		TravelDim = INDEX_NONE;
		TravelFromSector = INDEX_NONE;
	}

	if (NewState == EHCJBotState::Wander)
	{
		bHasWanderPoint = false;
	}
	else if (NewState == EHCJBotState::Attack)
	{
		TargetAcquiredTime = Now;
		LastTargetVisibleTime = Now;
		LastHostileSeenTime = Now;
	}

	if (Ship)
	{
		Ship->SetBoosting(false);
	}

	if (BotState != NewState)
	{
		UE_LOG(LogHCJ, Verbose, TEXT("%s: bot state %d -> %d"), *GetName(), static_cast<int32>(BotState), static_cast<int32>(NewState));
	}

	BotState = NewState;
	StateEnterTime = Now;
}

void AHCJBotController::Think(AHCJShipCharacter& Ship, const AHCJUniverse& Universe)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const int32 Sector = Ship.GetCurrentSector();
	const FVector ShipLocation = Ship.GetActorLocation();

	// Something useful on a pad nearby? In a fight only a short detour, otherwise worth a trip.
	PickupGoal = FindUsefulPickup(Ship, Universe, BotState == EHCJBotState::Attack ? PickupDetourRange : PickupSeekRange);

	if (const AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
	{
		Skill = FHCJBotSkill::Get(GameMode->GetBotDifficulty());
	}

	// Low on hull: break off and make for a repair pad anywhere in the sector, when one holds a kit.
	bRetreating = false;
	if (const UHCJHealthComponent* Health = Ship.GetHealthComponent(); Health != nullptr && Health->GetHealthFraction() < Skill.RetreatHullFraction)
	{
		AHCJPickup* Repair = nullptr;
		double BestDistSq = TNumericLimits<double>::Max();
		for (const TWeakObjectPtr<AHCJPickup>& Entry : AHCJPickup::GetAll())
		{
			AHCJPickup* Pickup = Entry.Get();
			if (Pickup == nullptr || Pickup->GetWorld() != GetWorld() || !Pickup->IsActive() || Pickup->GetItem() != EHCJPickupItem::Repair
				|| Universe.GetSectorAtLocation(Pickup->GetActorLocation()) != Sector)
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(ShipLocation, Pickup->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Repair = Pickup;
			}
		}
		if (Repair != nullptr)
		{
			PickupGoal = Repair;
			bRetreating = true;
		}
	}

	ThinkTorpedoEvasion(Ship);

	switch (BotState)
	{
	case EHCJBotState::Attack:
	{
		AHCJShipCharacter* Target = TargetShip.Get();
		if (!IsValid(Target) || Target->IsDead() || Target->GetCurrentSector() != Sector)
		{
			SetBotState(EHCJBotState::Wander);
			break;
		}

		LastHostileSeenTime = Now;
		float TargetDistance = FVector::Dist(ShipLocation, Target->GetActorLocation());
		if (Target->IsCloakHidden() && TargetDistance > UHCJGameplaySettings::Get().CloakDetectRange)
		{
			// Lost it: the target cloaked. A moment of fire at where it was.
			LostTargetPoint = Target->GetActorLocation();
			LostTargetFireUntil = Now + 1.2f;
			SetBotState(EHCJBotState::Wander);
			break;
		}

		// A hostile that is clearly closer (and visible) takes over.
		if (AHCJShipCharacter* Nearest = FindNearestHostile(Ship, Universe, EngageRange))
		{
			if (Nearest != Target)
			{
				const float NearestDistance = FVector::Dist(ShipLocation, Nearest->GetActorLocation());
				if (NearestDistance < TargetDistance * HCJBotRetargetRatio && HasLineOfSight(Ship, *Nearest))
				{
					Target = Nearest;
					TargetShip = Nearest;
					TargetAcquiredTime = Now;
					TargetDistance = NearestDistance;
					LastTargetVisibleTime = Now;
				}
			}
		}

		if (HasLineOfSight(Ship, *Target))
		{
			LastTargetVisibleTime = Now;
		}
		else if (Now - LastTargetVisibleTime > LoseTargetSeconds)
		{
			SetBotState(EHCJBotState::Wander);
			break;
		}

		// New aim error: a random offset at the target whose angular size is at most AimErrorDeg.
		const float MaxOffset = TargetDistance * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Skill.AimErrorDeg, 0.0f, 45.0f)));
		AimErrorOffset = BotRandom.VRand() * (MaxOffset * BotRandom.FRand());

		ThinkTorpedo(Ship, *Target, TargetDistance, LastTargetVisibleTime >= Now);
		break;
	}

	case EHCJBotState::Travel:
	{
		if (Sector != TravelFromSector)
		{
			LastArrivalDim = TravelDim;
			SetBotState(EHCJBotState::Wander);
			break;
		}

		if (Now - StateEnterTime > TravelTimeout || !Universe.HasHop(Sector, TravelDim))
		{
			SetBotState(EHCJBotState::Wander);
			break;
		}

		if (AHCJShipCharacter* Hostile = FindNearestHostile(Ship, Universe, EngageRange * 0.5f))
		{
			if (HasLineOfSight(Ship, *Hostile))
			{
				SetBotState(EHCJBotState::Attack);
				TargetShip = Hostile;
			}
		}
		break;
	}

	case EHCJBotState::Wander:
	default:
	{
		// Anyone in the sector keeps the bot here; whoever is in range and visible gets attacked.
		if (AHCJShipCharacter* Hostile = FindNearestHostile(Ship, Universe, 2.0f * HCJ::SectorKillRadius))
		{
			LastHostileSeenTime = Now;
			const FVector HostileLocation = Hostile->GetActorLocation();

			if (FVector::DistSquared(ShipLocation, HostileLocation) <= FMath::Square(EngageRange) && HasLineOfSight(Ship, *Hostile))
			{
				SetBotState(EHCJBotState::Attack);
				TargetShip = Hostile;
				break;
			}

			// Out of range or behind a rock: close in on it instead of wandering at random.
			WanderPoint = HostileLocation;
			bHasWanderPoint = true;
			WanderPointExpireTime = Now + HCJBotWanderPointLifetime;
			break;
		}

		// Nobody to fight: now and then a bot in good shape picks on a station turret; a hurt one never does.
		const UHCJHealthComponent* Health = Ship.GetHealthComponent();
		const bool bFit = Health != nullptr && Health->GetHealthFraction() > 0.6f;
		if (!bFit)
		{
			TurretGoal.Reset();
		}
		else if (Now >= NextTurretDecisionTime)
		{
			NextTurretDecisionTime = Now + 6.0f;
			TurretGoal.Reset();
			if (BotRandom.FRand() < TurretAttackChance)
			{
				double BestDistSq = FMath::Square(static_cast<double>(TurretAttackRange));
				for (const TWeakObjectPtr<AHCJTurret>& Entry : AHCJTurret::GetAll())
				{
					AHCJTurret* Turret = Entry.Get();
					if (Turret == nullptr || Turret->GetWorld() != GetWorld() || Turret->IsTurretDestroyed())
					{
						continue;
					}
					const double DistSq = FVector::DistSquared(ShipLocation, Turret->GetActorLocation());
					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						TurretGoal = Turret;
					}
				}
			}
		}
		if (const AHCJTurret* Turret = TurretGoal.Get(); Turret != nullptr && !Turret->IsTurretDestroyed())
		{
			// Busy here: no travelling on.
			LastHostileSeenTime = Now;
			break;
		}

		if (Now - FMath::Max(LastHostileSeenTime, StateEnterTime) > HuntDelay)
		{
			const int32 Dim = ChooseTravelDim(Sector, Universe);
			if (Dim != INDEX_NONE)
			{
				SetBotState(EHCJBotState::Travel);
				TravelDim = Dim;
				TravelFromSector = Sector;
			}
			else
			{
				// Nowhere to go (cannot happen in a connected graph): try again after another delay.
				LastHostileSeenTime = Now;
			}
		}
		break;
	}
	}

	if (!AHCJGameMode::IsClassicBots())
	{
		ThinkSystems(Ship, Universe);
	}
}

bool AHCJBotController::IsWayClear(const AHCJShipCharacter& Ship, const FVector& Point, float MaxDistance) const
{
	const UWorld* World = GetWorld();
	const FVector Start = Ship.GetActorLocation();
	const FVector To = Point - Start;
	const double Distance = To.Size();
	if (World == nullptr || Distance < 1.0)
	{
		return false;
	}
	const FVector End = Start + To * (FMath::Min(Distance, static_cast<double>(MaxDistance)) / Distance);
	return !World->SweepTestByObjectType(Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(AvoidanceProbeRadius * Ship.GetTypeDef().Scale), MakeBotQueryParams(Ship));
}

void AHCJBotController::ThinkSystems(AHCJShipCharacter& Ship, const AHCJUniverse& Universe)
{
	const UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	const FVector ShipLocation = Ship.GetActorLocation();
	const UHCJShipMovementComponent* Movement = Ship.GetShipMovement();
	const UHCJHealthComponent* Health = Ship.GetHealthComponent();
	if (Movement == nullptr || Health == nullptr)
	{
		return;
	}
	const FHCJWarpParams& Warp = Movement->GetWarpParams();
	const bool bWarpReady = Ship.GetWarpPhase() == EHCJWarpPhase::Idle && Ship.GetTractorHolder() == nullptr && !Ship.IsCloaked();
	// A way worth a jump for a bot: a second or so at warp, and nothing in it.
	const float MinWarpWay = 0.55f * Warp.Speed;

	// ---- caught by a tractor beam: boost free, by skill, decided once per catch ----
	if (Ship.GetTractorHolder() != nullptr)
	{
		if (!bCaughtDecided)
		{
			bCaughtDecided = true;
			if (BotRandom.FRand() < Skill.BreakFreeChance)
			{
				BoostFreeUntilTime = Now + 2.5f;
			}
		}
	}
	else
	{
		if (bCaughtDecided && Now < BoostFreeUntilTime)
		{
			if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
			{
				GameMode->NotifyBotBrokeFree();
			}
		}
		bCaughtDecided = false;
		BoostFreeUntilTime = 0.0f;
	}

	// ---- warp for the way to a portal: rolled once per trip ----
	if (BotState == EHCJBotState::Travel && TravelDim != INDEX_NONE && Now >= WarpUntilTime && bWarpReady && Universe.HasHop(Ship.GetCurrentSector(), TravelDim))
	{
		const FVector Portal = Universe.GetHopLocation(Ship.GetCurrentSector(), TravelDim);
		const float Distance = static_cast<float>(FVector::Dist(ShipLocation, Portal));
		if (Distance > MinWarpWay && WarpRolledForDim != TravelDim && GetAimErrorDegrees(Ship, Portal) < 25.0f && IsWayClear(Ship, Portal, Distance - 2500.0f))
		{
			WarpRolledForDim = TravelDim;
			if (BotRandom.FRand() < Skill.WarpTravelChance)
			{
				WarpGoal = Portal;
				bWarpFlee = false;
				WarpUntilTime = Now + Warp.ChargeSeconds + Warp.MaxSeconds + 1.0f;
				UE_LOG(LogHCJ, Verbose, TEXT("%s warps for the portal %.0f uu away"), *GetNameSafe(GetPawn()), Distance);
			}
		}
	}
	if (BotState != EHCJBotState::Travel)
	{
		WarpRolledForDim = INDEX_NONE;
	}

	// ---- warp to get away when the hull is low: rolled once per retreat ----
	const bool bHurt = Health->GetHealthFraction() < Skill.RetreatHullFraction;
	if (!bHurt)
	{
		bWarpFleeRolled = false;
	}
	FVector AttackerLocation;
	if (bHurt && !bWarpFleeRolled && Now >= WarpUntilTime && bWarpReady && Ship.GetRecentAttackerLocation(3.0f, AttackerLocation))
	{
		bWarpFleeRolled = true;
		if (BotRandom.FRand() < Skill.WarpFleeChance)
		{
			// To the repair pad when that is a jump away, else straight away from the attacker, bent towards the middle of the sector.
			const AHCJPickup* Pad = PickupGoal.Get();
			FVector Goal = (bRetreating && Pad != nullptr && FVector::Dist(Pad->GetActorLocation(), ShipLocation) > MinWarpWay) ? Pad->GetActorLocation() : FVector::ZeroVector;
			if (Goal.IsZero())
			{
				const FVector Middle = Universe.GetSectorOrigin(Ship.GetCurrentSector());
				const FVector Away = (ShipLocation - AttackerLocation).GetSafeNormal() + (Middle - ShipLocation).GetSafeNormal() * 0.6;
				Goal = ShipLocation + Away.GetSafeNormal() * (Warp.Speed * Warp.MaxSeconds * 0.6f);
			}
			if (IsWayClear(Ship, Goal, MinWarpWay))
			{
				WarpGoal = Goal;
				bWarpFlee = true;
				WarpUntilTime = Now + Warp.ChargeSeconds + Warp.MaxSeconds * 0.6f;
				UE_LOG(LogHCJ, Verbose, TEXT("%s flees by warp (hull %.0f %%)"), *GetNameSafe(GetPawn()), Health->GetHealthFraction() * 100.0f);
			}
		}
	}

	if (Now < NextSystemsTime)
	{
		return;
	}
	NextSystemsTime = Now + 1.0f;

	// ---- cloak: for an approach from afar, or for a retreat ----
	if (!Ship.IsCloaked() && !Ship.IsCloakLocked() && Ship.GetCloakEnergy() > 0.6f && Ship.GetWarpPhase() == EHCJWarpPhase::Idle && Now >= WarpUntilTime && BotRandom.FRand() < Skill.CloakChance)
	{
		const AHCJShipCharacter* Target = TargetShip.Get();
		const bool bApproach = BotState == EHCJBotState::Attack && Target != nullptr && FVector::DistSquared(Target->GetActorLocation(), ShipLocation) > FMath::Square(7000.0);
		const bool bClosingIn = BotState == EHCJBotState::Wander && bHasWanderPoint && FindNearestHostile(Ship, Universe, 30000.0f) != nullptr;
		if (bApproach || bClosingIn || bRetreating)
		{
			Ship.SetFiring(false);
			Ship.SetCloaking(true);
			CloakUntilTime = Now + (bRetreating ? 8.0f : 12.0f);
			UE_LOG(LogHCJ, Verbose, TEXT("%s cloaks (%s)"), *GetNameSafe(GetPawn()), bRetreating ? TEXT("retreat") : TEXT("approach"));
		}
	}

	// ---- tractor beam: on a target in reach that runs or is nearly done ----
	if (BotState == EHCJBotState::Attack && Now >= TractorUntilTime && !bRetreating && !Ship.IsCloaked() && Ship.GetPhaserCharge() > 0.5f && !Ship.IsPhaserLocked())
	{
		AHCJShipCharacter* Target = TargetShip.Get();
		if (Target != nullptr && !Target->IsDead() && Target->GetTractorHolder() == nullptr && Now - LastTargetVisibleTime < 0.5f)
		{
			const FVector ToTarget = Target->GetActorLocation() - ShipLocation;
			const float Distance = static_cast<float>(ToTarget.Size());
			const float AwaySpeed = static_cast<float>(FVector::DotProduct(Target->GetVelocity(), ToTarget.GetSafeNormal()));
			const UHCJHealthComponent* TargetHealth = Target->GetHealthComponent();
			const bool bWorthIt = AwaySpeed > 1200.0f || (TargetHealth != nullptr && TargetHealth->GetHealthFraction() < 0.45f);
			if (Distance < Ship.GetTypeDef().TractorRange * 0.9f && GetAimErrorDegrees(Ship, Target->GetActorLocation()) < 30.0f
				&& BotRandom.FRand() < Skill.TractorChance * (bWorthIt ? 3.0f : 1.0f))
			{
				Ship.SetLockTarget(Target);
				TractorUntilTime = Now + 2.5f;
				UE_LOG(LogHCJ, Verbose, TEXT("%s uses its tractor beam on %s at %.0f uu"), *GetNameSafe(GetPawn()), *Target->GetName(), Distance);
			}
		}
	}
}

void AHCJBotController::TickSystems(AHCJShipCharacter& Ship)
{
	if (AHCJGameMode::IsClassicBots())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector ShipLocation = Ship.GetActorLocation();

	// Warp: nose on the goal, key held; let go short of the goal (the ship drops out at boost speed and flies on).
	bool bWarpKey = false;
	if (Now < WarpUntilTime)
	{
		const float Distance = static_cast<float>(FVector::Dist(ShipLocation, WarpGoal));
		const EHCJWarpPhase Phase = Ship.GetWarpPhase();
		const bool bStillWanted = bWarpFlee || BotState == EHCJBotState::Travel;
		if (!bStillWanted || Distance < 3200.0f || Phase == EHCJWarpPhase::Cooldown || Ship.GetTractorHolder() != nullptr)
		{
			WarpUntilTime = 0.0f;
		}
		else
		{
			DesiredAimPoint = WarpGoal;
			bHasAimPoint = true;
			Ship.SetFiring(false);
			// The jump goes where the nose points when the charge is complete: only with the nose on the goal.
			bWarpKey = Phase == EHCJWarpPhase::Active || GetAimErrorDegrees(Ship, WarpGoal) < 6.0f;
		}
	}
	Ship.SetWarping(bWarpKey);

	// Tractor beam key.
	Ship.SetTractoring(Now < TractorUntilTime && BotState == EHCJBotState::Attack);

	// Cloak: no firing while it is up; it comes down in time for the fight, or when its time is over.
	if (Ship.IsCloaked())
	{
		const AHCJShipCharacter* Target = TargetShip.Get();
		const bool bClose = BotState == EHCJBotState::Attack && Target != nullptr
			&& FVector::DistSquared(Target->GetActorLocation(), ShipLocation) < FMath::Square(static_cast<double>(PreferredRange) * 1.3);
		if (Now >= CloakUntilTime || (bClose && !bRetreating) || Ship.IsCloakRevealed())
		{
			Ship.SetCloaking(false);
		}
		else
		{
			Ship.SetFiring(false);
		}
	}

	// Caught by a beam and decided to fight it: full boost, away from the holder.
	if (const AHCJShipCharacter* Holder = Ship.GetTractorHolder(); Holder != nullptr && Now < BoostFreeUntilTime)
	{
		Ship.SetBoosting(true);
		Ship.AddMovementInput((ShipLocation - Holder->GetActorLocation()).GetSafeNormal(), 1.0f);
	}
}

AHCJShipCharacter* AHCJBotController::FindNearestHostile(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe, float MaxRange) const
{
	UWorld* World = GetWorld();
	if (!World || !Universe.IsBuilt())
	{
		return nullptr;
	}

	const FVector ShipLocation = Ship.GetActorLocation();
	const int32 Sector = Universe.GetSectorAtLocation(ShipLocation);

	AHCJShipCharacter* Best = nullptr;
	double BestDistSq = FMath::Square(static_cast<double>(MaxRange));

	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		AHCJShipCharacter* Other = *It;
		if (Other == &Ship || !IsValid(Other) || Other->IsDead())
		{
			continue;
		}

		const FVector OtherLocation = Other->GetActorLocation();
		const double DistSq = FVector::DistSquared(ShipLocation, OtherLocation);
		if (Other->IsCloakHidden() && DistSq > FMath::Square(static_cast<double>(UHCJGameplaySettings::Get().CloakDetectRange)))
		{
			continue;
		}
		if (DistSq <= BestDistSq && Universe.GetSectorAtLocation(OtherLocation) == Sector)
		{
			BestDistSq = DistSq;
			Best = Other;
		}
	}

	return Best;
}

AHCJPickup* AHCJBotController::FindUsefulPickup(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe, float MaxRange) const
{
	const UWorld* World = GetWorld();
	const UHCJHealthComponent* Health = Ship.GetHealthComponent();
	if (World == nullptr || Health == nullptr || !Universe.IsBuilt())
	{
		return nullptr;
	}

	const FVector ShipLocation = Ship.GetActorLocation();
	const int32 Sector = Universe.GetSectorAtLocation(ShipLocation);
	AHCJPickup* Best = nullptr;
	double BestDistSq = FMath::Square(static_cast<double>(MaxRange));

	for (const TWeakObjectPtr<AHCJPickup>& Entry : AHCJPickup::GetAll())
	{
		AHCJPickup* Pickup = Entry.Get();
		if (Pickup == nullptr || Pickup->GetWorld() != World || !Pickup->IsActive())
		{
			continue;
		}
		const FVector Location = Pickup->GetActorLocation();
		const double DistSq = FVector::DistSquared(ShipLocation, Location);
		if (DistSq > BestDistSq || Universe.GetSectorAtLocation(Location) != Sector)
		{
			continue;
		}

		bool bUseful = false;
		switch (Pickup->GetItem())
		{
		case EHCJPickupItem::Repair: bUseful = Health->GetHealthFraction() < 0.75f; break;
		case EHCJPickupItem::ShieldCell: bUseful = Health->GetShield() < Health->GetMaxShield() - 1.0f; break;
		case EHCJPickupItem::Overdrive: bUseful = !Ship.HasPowerup(EHCJPowerup::Overdrive); break;
		case EHCJPickupItem::QuadDamage: bUseful = !Ship.HasPowerup(EHCJPowerup::Quad); break;
		case EHCJPickupItem::Cloak: bUseful = Ship.GetCloakEnergy() < 0.5f && !AHCJGameMode::IsClassicBots(); break;
		case EHCJPickupItem::Torpedoes: bUseful = Ship.GetTorpedoCount() + 2 <= UHCJGameplaySettings::Get().TorpedoMaxStock; break;
		default: bUseful = HCJ::IsBaseWeapon(Ship.GetWeapon()); break;
		}
		if (!bUseful)
		{
			continue;
		}

		// Not behind a rock.
		if (World->LineTraceTestByObjectType(ShipLocation, Location, FCollisionObjectQueryParams(ECC_WorldStatic), MakeBotQueryParams(Ship)))
		{
			continue;
		}
		BestDistSq = DistSq;
		Best = Pickup;
	}
	return Best;
}

int32 AHCJBotController::ChooseTravelDim(int32 FromSector, const AHCJUniverse& Universe)
{
	const FHCJHypercubeGraph& Graph = Universe.GetGraph();
	if (!Graph.IsValidSector(FromSector))
	{
		return INDEX_NONE;
	}

	// Hunt: the closest sector (in portal hops) that holds a human's live ship.
	int32 BestSector = INDEX_NONE;
	int32 BestDistance = MAX_int32;

	const UWorld* World = GetWorld();
	if (const AGameStateBase* GameState = World ? World->GetGameState() : nullptr)
	{
		for (const APlayerState* OtherPlayerState : GameState->PlayerArray)
		{
			const AHCJPlayerState* HCJPlayerState = Cast<AHCJPlayerState>(OtherPlayerState);
			if (!HCJPlayerState || HCJPlayerState->IsABot())
			{
				continue;
			}

			const int32 HumanSector = HCJPlayerState->GetCurrentSector();
			if (HumanSector == FromSector || !Graph.IsValidSector(HumanSector))
			{
				continue;
			}

			const int32 Distance = Universe.GetRoutes().Distance(FromSector, HumanSector);
			if (Distance > 0 && Distance < BestDistance)
			{
				BestDistance = Distance;
				BestSector = HumanSector;
			}
		}
	}

	if (BestSector != INDEX_NONE)
	{
		const int32 HopDim = Universe.GetRoutes().NextHop(FromSector, BestSector);
		if (HopDim != INDEX_NONE && Universe.HasHop(FromSector, HopDim))
		{
			return HopDim;
		}
	}

	// Roam: a random open portal, preferably not the one the bot just came through.
	FHCJDimArray OpenDims = Graph.GetOpenDims(FromSector);
	if (Universe.HasWormhole(FromSector))
	{
		// The wormhole is one more way out.
		OpenDims.Add(HCJ::WormholeHop);
	}
	if (OpenDims.Num() > 1)
	{
		OpenDims.Remove(LastArrivalDim);
	}
	if (OpenDims.Num() == 0)
	{
		return INDEX_NONE;
	}

	return OpenDims[BotRandom.RandRange(0, OpenDims.Num() - 1)];
}

void AHCJBotController::PickWanderPoint(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe)
{
	const int32 Sector = Ship.GetCurrentSector();
	if (Sector == INDEX_NONE)
	{
		bHasWanderPoint = false;
		return;
	}

	WanderPoint = Universe.GetRandomFreeLocation(Sector, BotRandom, WanderClearance);
	// Never wander into the gravity well of a black hole, nor under the guns of a station.
	const double StationDangerSq = FMath::Square(static_cast<double>(UHCJGameplaySettings::Get().TurretRange) + HCJ::StationBodyRadius);
	const FVector StationLocation = Universe.GetStationLocation(Sector);
	for (int32 Attempt = 0; Attempt < 8 && (Universe.GetGravityDrift(WanderPoint).SizeSquared() > 0.0
		|| (Universe.HasStation(Sector) && FVector::DistSquared(WanderPoint, StationLocation) < StationDangerSq)); ++Attempt)
	{
		WanderPoint = Universe.GetRandomFreeLocation(Sector, BotRandom, WanderClearance);
	}
	bHasWanderPoint = true;

	const UWorld* World = GetWorld();
	WanderPointExpireTime = (World ? World->GetTimeSeconds() : 0.0f) + HCJBotWanderPointLifetime;
}

bool AHCJBotController::HasLineOfSight(const AHCJShipCharacter& Ship, const AHCJShipCharacter& Other) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams Params = MakeBotQueryParams(Ship);
	Params.AddIgnoredActor(&Other);

	return !World->LineTraceTestByObjectType(
		Ship.GetActorLocation(), Other.GetActorLocation(), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
}

// ---------------------------------------------------------------------------------------------------------------
// Per-frame behaviour
// ---------------------------------------------------------------------------------------------------------------

void AHCJBotController::TickWander(float DeltaTime, AHCJShipCharacter& Ship, const AHCJUniverse& Universe)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector ShipLocation = Ship.GetActorLocation();

	Ship.SetFiring(false);

	if (Now < LostTargetFireUntil && !Ship.IsCloaked())
	{
		// The target cloaked a moment ago: a burst at where it was.
		DesiredAimPoint = LostTargetPoint;
		bHasAimPoint = true;
		Ship.SetFiring(GetAimErrorDegrees(Ship, LostTargetPoint) <= FireConeDeg);
		SteerTowards(Ship, Universe, LostTargetPoint, 0.5f, false);
		return;
	}

	if (const AHCJPickup* Goal = PickupGoal.Get(); Goal != nullptr && Goal->IsActive())
	{
		// Fetch it: straight over the pad, no braking.
		const FVector GoalLocation = Goal->GetActorLocation();
		Ship.SetBoosting(FVector::DistSquared(ShipLocation, GoalLocation) > FMath::Square(BoostDistance));
		SteerTowards(Ship, Universe, GoalLocation, 1.0f, false);
		DesiredAimPoint = GoalLocation;
		bHasAimPoint = true;
		return;
	}

	if (const AHCJTurret* Turret = TurretGoal.Get(); Turret != nullptr && !Turret->IsTurretDestroyed())
	{
		// Attack run on a station turret: circle it at the edge of the weapon's reach and fire when lined up.
		const FVector TurretLocation = Turret->GetActorLocation();
		const FVector ToTurret = TurretLocation - ShipLocation;
		const float Distance = static_cast<float>(ToTurret.Size());
		const FVector Direction = Distance > 1.0f ? ToTurret / Distance : Ship.GetActorForwardVector();
		const FHCJWeaponDef& Weapon = Ship.GetFireWeaponDef();
		const float FireRange = FMath::Min(7500.0f, Weapon.BotMaxRange);
		DesiredAimPoint = TurretLocation;
		bHasAimPoint = true;

		FCollisionQueryParams Params = MakeBotQueryParams(Ship);
		Params.AddIgnoredActor(Turret);
		const bool bClear = !GetWorld()->LineTraceTestByObjectType(ShipLocation, TurretLocation, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		Ship.SetFiring(bClear && Distance <= FireRange && GetAimErrorDegrees(Ship, TurretLocation) <= FireConeDeg);

		if (Now >= NextStrafeFlipTime)
		{
			StrafeSign = -StrafeSign;
			NextStrafeFlipTime = Now + BotRandom.FRandRange(1.5f, 4.0f);
		}
		FVector Right = FVector::CrossProduct(FVector::UpVector, Direction);
		if (!Right.Normalize())
		{
			Right = FVector::RightVector;
		}
		const float WantedRange = 0.75f * FireRange;
		const float Radial = FMath::Clamp((Distance - WantedRange) / FMath::Max(WantedRange * 0.5f, 1.0f), -1.0f, 1.0f);
		Ship.SetBoosting(Distance > BoostDistance);
		SteerTowards(Ship, Universe, Radial >= 0.0f ? TurretLocation : ShipLocation - Direction * WantedRange, FMath::Abs(Radial), false,
			Right * (StrafeSign * HCJBotStrafeStrength));
		return;
	}

	if (!bHasWanderPoint
		|| Now >= WanderPointExpireTime
		|| FVector::DistSquared(ShipLocation, WanderPoint) <= FMath::Square(ArrivalTolerance)
		|| Universe.GetSectorAtLocation(WanderPoint) != Ship.GetCurrentSector())
	{
		PickWanderPoint(Ship, Universe);
		if (!bHasWanderPoint)
		{
			Ship.SetBoosting(false);
			return;
		}
	}

	const float Distance = FVector::Dist(ShipLocation, WanderPoint);
	Ship.SetBoosting(Distance > BoostDistance);

	SteerTowards(Ship, Universe, WanderPoint, 1.0f, true);

	DesiredAimPoint = WanderPoint;
	bHasAimPoint = true;
}

void AHCJBotController::TickAttack(float DeltaTime, AHCJShipCharacter& Ship, const AHCJUniverse& Universe)
{
	AHCJShipCharacter* Target = TargetShip.Get();
	if (!IsValid(Target) || Target->IsDead())
	{
		SetBotState(EHCJBotState::Wander);
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const FVector ShipLocation = Ship.GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector ToTarget = TargetLocation - ShipLocation;
	const float Distance = static_cast<float>(ToTarget.Size());
	const FVector TargetDirection = Distance > UE_KINDA_SMALL_NUMBER ? ToTarget / Distance : Ship.GetActorForwardVector();

	// Aim and trigger.
	const FVector AimPoint = ComputeLeadAimPoint(Ship, *Target) + AimErrorOffset;
	DesiredAimPoint = AimPoint;
	bHasAimPoint = true;

	// Visibility is sampled in Think; it counts as current for a little more than one think interval.
	const bool bTargetVisible = (Now - LastTargetVisibleTime) <= ThinkInterval * 1.5f;
	// Each weapon has the range at which a bot bothers to fire it (a scatter cannon is useless from afar).
	// (What the trigger fires right now: with the phaser bank emptied that is the blaster, as the weapon panel shows a player.)
	const FHCJWeaponDef& Weapon = Ship.GetFireWeaponDef();
	// Difficulty: nothing before the reaction delay is over, then bursts with pauses, from no farther than the level allows.
	const bool bReacted = Now - TargetAcquiredTime >= Skill.ReactionDelay;
	const bool bInBurst = Skill.BurstPause <= 0.0f || FMath::Fmod(Now + BurstPhase, Skill.BurstSeconds + Skill.BurstPause) < Skill.BurstSeconds;
	bool bFire = bTargetVisible && bReacted && bInBurst && !bRetreating
		&& Distance <= FMath::Min(EngageRange * Skill.EngageRangeFactor, Weapon.BotMaxRange) && GetAimErrorDegrees(Ship, AimPoint) <= FireConeDeg;
	if (bFire && Weapon.bBeam)
	{
		// The beam is where the nose points, at once: held only while the aim is on the target, and a better pilot keeps
		// a reserve in the bank unless the target is about to go.
		const UHCJHealthComponent* TargetHealth = Target->GetHealthComponent();
		const bool bFinishing = TargetHealth != nullptr && TargetHealth->GetHealthFraction() < 0.25f;
		bFire = GetAimErrorDegrees(Ship, AimPoint) <= Skill.PhaserConeDeg && (bFinishing || Ship.GetPhaserCharge() > Skill.PhaserReserve);
	}
	Ship.SetFiring(bFire);

	// Circle the target at PreferredRange.
	if (Now >= NextStrafeFlipTime)
	{
		StrafeSign = -StrafeSign;
		NextStrafeFlipTime = Now + BotRandom.FRandRange(1.5f, 4.0f);
	}

	FVector Right = FVector::CrossProduct(FVector::UpVector, TargetDirection);
	if (!Right.Normalize())
	{
		Right = FVector::RightVector;
	}
	const FVector Up = FVector::CrossProduct(TargetDirection, Right);
	// Slow vertical weave, out of phase between bots, so the orbit is not a flat circle.
	const float Weave = FMath::Sin(Now * 0.7f + static_cast<float>(GetUniqueID() % 16));
	const FVector Strafe = (Right * StrafeSign + Up * (0.5f * Weave)).GetSafeNormal() * (HCJBotStrafeStrength * Skill.StrafeStrength / 0.8f);

	const float WantedRange = FMath::Min(PreferredRange, 0.6f * Weapon.BotMaxRange);
	const float RangeError = Distance - WantedRange;
	const float Radial = FMath::Clamp(RangeError / FMath::Max(WantedRange * 0.5f, 1.0f), -1.0f, 1.0f);

	Ship.SetBoosting(Distance > BoostDistance);

	if (const AHCJPickup* Goal = PickupGoal.Get(); Goal != nullptr && Goal->IsActive())
	{
		// Keep shooting, but fly over the pad on the way; a bot that is breaking off just runs for it.
		if (bRetreating)
		{
			Ship.SetBoosting(true);
		}
		SteerTowards(Ship, Universe, Goal->GetActorLocation(), 1.0f, false, bRetreating ? FVector::ZeroVector : Strafe * 0.4f);
		return;
	}

	if (Radial >= 0.0f)
	{
		SteerTowards(Ship, Universe, TargetLocation, Radial, false, Strafe);
	}
	else
	{
		// Too close: back off along the line away from the target.
		const FVector RetreatPoint = ShipLocation - TargetDirection * WantedRange;
		SteerTowards(Ship, Universe, RetreatPoint, -Radial, false, Strafe);
	}
}

void AHCJBotController::TickTravel(float DeltaTime, AHCJShipCharacter& Ship, const AHCJUniverse& Universe)
{
	const int32 Sector = Ship.GetCurrentSector();

	Ship.SetFiring(false);

	if (Sector != TravelFromSector)
	{
		// The portal took the ship: the trip is over.
		LastArrivalDim = TravelDim;
		SetBotState(EHCJBotState::Wander);
		return;
	}

	if (!Universe.HasHop(Sector, TravelDim))
	{
		SetBotState(EHCJBotState::Wander);
		return;
	}

	const FVector ShipLocation = Ship.GetActorLocation();
	const FVector PortalLocation = Universe.GetHopLocation(Sector, TravelDim);
	const FVector ToPortal = PortalLocation - ShipLocation;
	const float Distance = static_cast<float>(ToPortal.Size());

	FVector Target = PortalLocation;

	// Inside the trigger without having been teleported (the ship arrived through this very portal, or the
	// cooldown still runs): a new begin-overlap is needed, so fly straight on, out of the trigger, and come back.
	const float InsideRadius = HCJ::PortalTriggerRadius + HCJ::ShipCollisionRadius;
	if (Distance <= InsideRadius)
	{
		FVector ThroughDirection = Ship.GetVelocity().GetSafeNormal();
		if (ThroughDirection.IsNearlyZero())
		{
			ThroughDirection = Distance > UE_KINDA_SMALL_NUMBER ? ToPortal / Distance : Ship.GetActorForwardVector();
		}
		Target = ShipLocation + ThroughDirection * (2.0f * InsideRadius);
	}
	else if (Ship.GetPortalCooldownRemaining() > 0.0f && Distance <= InsideRadius * 2.0f)
	{
		// Still on cooldown right in front of the portal: wait here instead of wasting the overlap.
		DesiredAimPoint = PortalLocation;
		bHasAimPoint = true;
		Ship.SetBoosting(false);
		SteerTowards(Ship, Universe, ShipLocation, 0.0f, true);
		return;
	}

	Ship.SetBoosting(Distance > BoostDistance);
	SteerTowards(Ship, Universe, Target, 1.0f, false);

	DesiredAimPoint = PortalLocation;
	bHasAimPoint = true;
}

void AHCJBotController::SteerTowards(AHCJShipCharacter& Ship, const AHCJUniverse& Universe, const FVector& WorldTarget, float Throttle, bool bArrive, const FVector& ExtraWorldInput)
{
	const UHCJShipMovementComponent* Movement = Ship.GetShipMovement();
	if (!Movement)
	{
		return;
	}

	const FVector ShipLocation = Ship.GetActorLocation();
	// Dodging a torpedo comes before wherever the bot was going (but after rocks, the sector edge and black holes, below).
	const bool bEvading = GetWorld()->GetTimeSeconds() < EvadeUntilTime && !EvadeDirection.IsNearlyZero();
	const FVector ToTarget = bEvading ? EvadeDirection * 5000.0 : WorldTarget - ShipLocation;
	const float Distance = static_cast<float>(ToTarget.Size());
	const FVector TargetDirection = Distance > UE_KINDA_SMALL_NUMBER ? ToTarget / Distance : FVector::ZeroVector;
	if (bEvading)
	{
		Throttle = 1.0f;
		bArrive = false;
		Ship.SetBoosting(true);
	}

	const float MaxSpeed = Movement->GetMaxSpeed();
	const float MaxAcceleration = FMath::Max(Movement->GetMaxAcceleration(), 1.0f);

	// Wanted motion as a direction and a strength in [0, 1].
	FVector Wanted = TargetDirection * FMath::Clamp(Throttle, 0.0f, 1.0f) + (bEvading ? FVector::ZeroVector : ExtraWorldInput);
	float Strength = FMath::Min(static_cast<float>(Wanted.Size()), 1.0f);

	FVector DesiredVelocity = FVector::ZeroVector;
	if (Strength > UE_KINDA_SMALL_NUMBER)
	{
		const FVector WantedDirection = Wanted.GetSafeNormal();
		const FVector SteerDirection = ComputeAvoidance(Ship, Universe, WantedDirection);

		float DesiredSpeed = MaxSpeed * Strength;
		if (bArrive)
		{
			// Fastest speed from which the ship can still stop at the target.
			const float BrakeDistance = FMath::Max(Distance - ArrivalTolerance * 0.5f, 0.0f);
			DesiredSpeed = FMath::Min(DesiredSpeed, FMath::Sqrt(2.0f * MaxAcceleration * HCJBotArriveBrakeFraction * BrakeDistance));
		}

		DesiredVelocity = SteerDirection * DesiredSpeed;
	}

	// Black hole: a hazard. Inside its influence radius the bot steers away, unless what it is flying to lies
	// farther from the hole than the bot itself; near the point of no return it drops everything and boosts out.
	bool bEscapeBoost = false;
	if (const int32 Sector = Universe.GetSectorAtLocation(ShipLocation); Universe.HasBlackHole(Sector))
	{
		const FHCJBlackHoleSettings& Hole = Universe.GetBlackHoleSettings();
		const FVector HoleLocation = Universe.GetBlackHoleLocation(Sector);
		const FVector FromHole = ShipLocation - HoleLocation;
		const float HoleDistance = static_cast<float>(FromHole.Size());
		if (HoleDistance < Hole.InfluenceRadius)
		{
			const FVector Outward = HoleDistance > 1.0f ? FromHole / HoleDistance : FVector::UpVector;
			// Twice the distance from which a cruising ship cannot get out any more, and never less than three horizons.
			const float DangerRadius = FMath::Max(2.0f * FHCJBlackHole::ComputeNoEscapeRadius(Movement->GetCruiseSpeed(), Hole), 3.0f * Hole.HorizonRadius);
			const bool bTargetIsFarther = FVector::DistSquared(WorldTarget, HoleLocation) > FMath::Square(static_cast<double>(HoleDistance));
			if (HoleDistance < DangerRadius)
			{
				DesiredVelocity = Outward * (Movement->GetCruiseSpeed() * Movement->BoostSpeedMultiplier);
				bEscapeBoost = true;
			}
			else if (!bTargetIsFarther)
			{
				const float Alpha = FMath::Clamp((Hole.InfluenceRadius - HoleDistance) / FMath::Max(Hole.InfluenceRadius - DangerRadius, 1.0f), 0.0f, 1.0f);
				DesiredVelocity = FMath::Lerp(DesiredVelocity, Outward * MaxSpeed, Alpha);
			}
			// The pull is a drift on top of the ship's own velocity: fly against it.
			DesiredVelocity -= Universe.GetGravityDrift(ShipLocation);
		}
	}

	// The ship drifts (little friction in flight), so thrust towards the velocity error rather than at the target:
	// this cancels sideways drift and brakes actively.
	const FVector VelocityError = DesiredVelocity - Ship.GetVelocity();
	FVector Input = VelocityError / (MaxAcceleration * HCJBotVelocityResponseTime);
	const float InputSize = static_cast<float>(Input.Size());
	if (InputSize <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	Ship.AddMovementInput(Input / InputSize, FMath::Min(InputSize, 1.0f));
	if (bEscapeBoost)
	{
		Ship.SetBoosting(true);
	}
}

FVector AHCJBotController::ComputeAvoidance(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe, const FVector& DesiredDirection) const
{
	FVector Result = DesiredDirection.GetSafeNormal();
	if (Result.IsNearlyZero())
	{
		return DesiredDirection;
	}

	const UWorld* World = GetWorld();
	const FVector ShipLocation = Ship.GetActorLocation();

	if (World && AvoidanceLookAhead > 0.0f)
	{
		const FCollisionQueryParams Params = MakeBotQueryParams(Ship);
		const FCollisionObjectQueryParams ObjectParams(ECC_WorldStatic);
		const FCollisionShape Probe = FCollisionShape::MakeSphere(FMath::Max(AvoidanceProbeRadius, HCJ::ShipCollisionRadius));

		// Probe 1: along the wanted direction.
		FHitResult Hit;
		if (World->SweepSingleByObjectType(Hit, ShipLocation, ShipLocation + Result * AvoidanceLookAhead, FQuat::Identity, ObjectParams, Probe, Params))
		{
			FVector Normal = Hit.bStartPenetrating ? Hit.Normal : Hit.ImpactNormal;
			if (!Normal.Normalize())
			{
				Normal = -Result;
			}

			// 0 far away .. 1 touching.
			const float Proximity = Hit.bStartPenetrating ? 1.0f : 1.0f - FMath::Clamp(Hit.Time, 0.0f, 1.0f);

			FVector Slide = FVector::VectorPlaneProject(Result, Normal);
			if (!Slide.Normalize())
			{
				// Head-on: any direction in the surface plane will do; pick a stable one.
				Slide = FVector::CrossProduct(Normal, Ship.GetActorUpVector());
				if (!Slide.Normalize())
				{
					Slide = FVector::CrossProduct(Normal, Ship.GetActorRightVector()).GetSafeNormal();
				}
			}

			const FVector Avoid = (Slide + Normal * Proximity).GetSafeNormal();
			const float Blend = FMath::Clamp(0.35f + Proximity, 0.0f, 1.0f);
			const FVector Blended = FMath::Lerp(Result, Avoid, Blend).GetSafeNormal();
			Result = Blended.IsNearlyZero() ? Avoid : Blended;
		}

		// Probe 2: along the drift, when the ship is not moving where it wants to go.
		const FVector Velocity = Ship.GetVelocity();
		const float Speed = static_cast<float>(Velocity.Size());
		if (Speed > 300.0f)
		{
			const FVector VelocityDirection = Velocity / Speed;
			if (FVector::DotProduct(VelocityDirection, Result) < 0.9)
			{
				const float DriftLookAhead = FMath::Min(Speed, AvoidanceLookAhead);
				FHitResult DriftHit;
				if (World->SweepSingleByObjectType(DriftHit, ShipLocation, ShipLocation + VelocityDirection * DriftLookAhead, FQuat::Identity, ObjectParams, Probe, Params))
				{
					FVector Normal = DriftHit.bStartPenetrating ? DriftHit.Normal : DriftHit.ImpactNormal;
					if (Normal.Normalize())
					{
						const float Proximity = DriftHit.bStartPenetrating ? 1.0f : 1.0f - FMath::Clamp(DriftHit.Time, 0.0f, 1.0f);
						const FVector Pushed = (Result + Normal * Proximity).GetSafeNormal();
						if (!Pushed.IsNearlyZero())
						{
							Result = Pushed;
						}
					}
				}
			}
		}
	}

	// Sector boundary: beyond the asteroid field never head further out; pull back harder the farther out.
	const int32 Sector = Universe.GetSectorAtLocation(ShipLocation);
	if (Sector != INDEX_NONE)
	{
		const FVector FromOrigin = ShipLocation - Universe.GetSectorOrigin(Sector);
		const float OriginDistance = static_cast<float>(FromOrigin.Size());
		if (OriginDistance > HCJ::SectorFieldRadius)
		{
			const FVector Outward = FromOrigin / OriginDistance;
			const float Overshoot = FMath::Clamp(
				(OriginDistance - HCJ::SectorFieldRadius) / (HCJ::SectorSafeRadius - HCJ::SectorFieldRadius), 0.0f, 1.0f);

			const double OutwardShare = FVector::DotProduct(Result, Outward);
			if (OutwardShare > 0.0)
			{
				Result -= Outward * OutwardShare;
			}
			Result = (Result - Outward * (0.25f + 0.75f * Overshoot)).GetSafeNormal();
			if (Result.IsNearlyZero())
			{
				Result = -Outward;
			}
		}
	}

	return Result;
}

FVector AHCJBotController::ComputeLeadAimPoint(const AHCJShipCharacter& Ship, const AHCJShipCharacter& Target) const
{
	const FVector TargetLocation = Target.GetActorLocation();
	const double ProjectileSpeed = Ship.GetProjectileSpeed();
	if (ProjectileSpeed <= UE_KINDA_SMALL_NUMBER)
	{
		return TargetLocation;
	}

	// Solve |R + V t| = s t for the smallest positive t.
	const FVector R = TargetLocation - Ship.GetMuzzleLocation();
	const FVector V = Target.GetVelocity();

	const double A = V.SizeSquared() - ProjectileSpeed * ProjectileSpeed;
	const double B = 2.0 * FVector::DotProduct(R, V);
	const double C = R.SizeSquared();

	double InterceptTime = -1.0;
	if (FMath::Abs(A) < UE_KINDA_SMALL_NUMBER)
	{
		// Target as fast as the projectile: linear equation.
		if (B < -UE_KINDA_SMALL_NUMBER)
		{
			InterceptTime = -C / B;
		}
	}
	else
	{
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant >= 0.0)
		{
			const double Root = FMath::Sqrt(Discriminant);
			const double T1 = (-B - Root) / (2.0 * A);
			const double T2 = (-B + Root) / (2.0 * A);
			const double Lo = FMath::Min(T1, T2);
			const double Hi = FMath::Max(T1, T2);
			InterceptTime = Lo > 0.0 ? Lo : Hi;
		}
	}

	if (InterceptTime <= 0.0)
	{
		return TargetLocation;
	}

	return TargetLocation + V * FMath::Min(InterceptTime, static_cast<double>(HCJBotMaxLeadSeconds));
}

void AHCJBotController::ThinkTorpedo(AHCJShipCharacter& Ship, AHCJShipCharacter& Target, float TargetDistance, bool bTargetVisible)
{
	if (!HCJTorpedo::IsEnabled())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	// The lock a player's HUD holds: torpedoes (and homing missiles) of this ship steer for it.
	Ship.SetLockTarget(&Target);

	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const FHCJWeaponDef& Torpedo = Gameplay.GetWeapon(EHCJWeapon::Torpedo);
	const UHCJHealthComponent* TargetHealth = Target.GetHealthComponent();
	const AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>();
	if (TargetHealth == nullptr || GameMode == nullptr)
	{
		return;
	}

	const FHCJTorpedoBotSkill TorpedoSkill = HCJTorpedo::GetBotSkill(GameMode->GetBotDifficulty());
	FHCJTorpedoBotInput Input;
	Input.bReady = Ship.IsTorpedoReady() && !bRetreating && Now - LastTorpedoTime > TorpedoSkill.MinSecondsBetween && Now - TargetAcquiredTime >= Skill.ReactionDelay;
	// Roughly ahead is enough: the torpedo steers a little by itself.
	Input.bLockedAndAimed = bTargetVisible && !Target.IsSpawnShielded() && GetAimErrorDegrees(Ship, Target.GetActorLocation()) <= 10.0f;
	Input.Distance = TargetDistance;
	// What the target panel shows: the facing of the target that is turned towards us, and its hull.
	const FVector LocalToUs = Target.GetActorTransform().InverseTransformVectorNoScale(Ship.GetActorLocation() - Target.GetActorLocation());
	Input.FacingFraction = TargetHealth->GetNumFacings() == HCJShield::NumFacings
		? TargetHealth->GetFacingFraction(static_cast<int32>(HCJShield::SelectFacing(LocalToUs))) : TargetHealth->GetShieldFraction();
	Input.HullFraction = TargetHealth->GetHealthFraction();
	Input.Roll = BotRandom.FRand();
	Input.SecondsOnTarget = Now - TargetAcquiredTime;

	// Never from so close that the blast or the arming distance spoils it.
	const float MinRange = Gameplay.TorpedoArmingDistance + Gameplay.TorpedoBlastRadius + 400.0f;
	const float MaxRange = FMath::Min(Torpedo.BotMaxRange, EngageRange * Skill.EngageRangeFactor);
	if (HCJTorpedo::ShouldBotFire(Input, TorpedoSkill, MinRange, MaxRange))
	{
		LastTorpedoTime = Now;
		UE_LOG(LogHCJ, Verbose, TEXT("%s fires a torpedo at %s from %.0f uu: facing shown %.0f %%, hull %.0f %%"), *GetNameSafe(GetPawn()), *Target.GetName(),
			TargetDistance, Input.FacingFraction * 100.0f, Input.HullFraction * 100.0f);
		Ship.FireTorpedo();
	}
}

void AHCJBotController::ThinkTorpedoEvasion(const AHCJShipCharacter& Ship)
{
	const UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	if (Now < NextEvadeDecisionTime)
	{
		return;
	}
	const FVector ShipLocation = Ship.GetActorLocation();
	const AHCJProjectile* Threat = nullptr;
	double BestDistSq = FMath::Square(7000.0);
	for (const TWeakObjectPtr<AHCJProjectile>& Entry : AHCJProjectile::GetLiveTorpedoes())
	{
		const AHCJProjectile* Torpedo = Entry.Get();
		if (Torpedo == nullptr || Torpedo->GetWorld() != World || Torpedo->GetHomingTarget() != &Ship || Torpedo->IsSpent())
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Torpedo->GetActorLocation(), ShipLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Threat = Torpedo;
		}
	}
	if (Threat == nullptr)
	{
		return;
	}
	// One decision per torpedo, more or less: the next one a second and a half later.
	NextEvadeDecisionTime = Now + 1.5f;
	const AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>();
	if (GameMode == nullptr || BotRandom.FRand() >= HCJTorpedo::GetBotSkill(GameMode->GetBotDifficulty()).EvadeChance)
	{
		return;
	}
	// Straight away from the line the torpedo flies along; when the ship sits on that line, to one side.
	const FVector Flight = Threat->GetVelocity().GetSafeNormal();
	const FVector Offset = ShipLocation - Threat->GetActorLocation();
	FVector Away = Offset - Flight * FVector::DotProduct(Offset, Flight);
	if (!Away.Normalize())
	{
		Away = FVector::CrossProduct(Flight, FVector::UpVector).GetSafeNormal() * StrafeSign;
	}
	if (Away.IsNearlyZero())
	{
		return;
	}
	EvadeDirection = Away;
	EvadeUntilTime = Now + 1.4f;
	UE_LOG(LogHCJ, Verbose, TEXT("%s breaks away from a torpedo %.0f uu out"), *GetNameSafe(GetPawn()), FMath::Sqrt(BestDistSq));
	if (AHCJGameMode* MutableGameMode = World->GetAuthGameMode<AHCJGameMode>())
	{
		MutableGameMode->NotifyBotTorpedoEvade();
	}
}

void AHCJBotController::TickShieldFacing(const AHCJShipCharacter& Ship)
{
	const UHCJHealthComponent* Health = Ship.GetHealthComponent();
	if (!(bRetreating || BotState == EHCJBotState::Travel) || Ship.IsFiring() || Health == nullptr || Health->GetNumFacings() != HCJShield::NumFacings)
	{
		return;
	}
	FVector Threat;
	if (!Ship.GetRecentAttackerLocation(3.0f, Threat))
	{
		return;
	}

	float Fractions[HCJShield::NumFacings];
	bool bAnyWeak = false;
	for (int32 Facing = 0; Facing < HCJShield::NumFacings; ++Facing)
	{
		Fractions[Facing] = Health->GetFacingFraction(Facing);
		bAnyWeak |= Fractions[Facing] <= HCJShield::WeakFraction;
	}
	if (!bAnyWeak)
	{
		return;
	}

	const FVector ToThreat = Threat - Ship.GetActorLocation();
	const EHCJShieldFacing Current = HCJShield::SelectFacing(Ship.GetActorTransform().InverseTransformVectorNoScale(ToThreat));
	const EHCJShieldFacing Wanted = HCJShield::PickFacingToPresent(Fractions, Current);
	if (Wanted != Current && GetWorld()->GetTimeSeconds() >= NextShieldTurnLogTime)
	{
		// At most one line a second per bot, and one count for the match statistics.
		NextShieldTurnLogTime = static_cast<float>(GetWorld()->GetTimeSeconds()) + 1.0f;
		if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
		{
			GameMode->NotifyBotShieldTurn();
		}
		UE_LOG(LogHCJ, Verbose, TEXT("%s turns its %s facing (%.0f %%) to the attacker: %s is at %.0f %%"), *GetNameSafe(GetPawn()),
			HCJShield::FacingName(Wanted), Fractions[static_cast<int32>(Wanted)] * 100.0f, HCJShield::FacingName(Current), Fractions[static_cast<int32>(Current)] * 100.0f);
	}
	// Yaw at which the wanted facing looks straight at the attacker.
	const double Yaw = ToThreat.Rotation().Yaw - HCJShield::FacingYawDegrees(Wanted);
	DesiredAimPoint = Ship.GetActorLocation() + FRotator(0.0, Yaw, 0.0).Vector() * 10000.0;
	bHasAimPoint = true;
}

void AHCJBotController::TickAim(float DeltaTime, const AHCJShipCharacter& Ship)
{
	if (!bHasAimPoint)
	{
		return;
	}

	const FVector WantedDirection = (DesiredAimPoint - Ship.GetActorLocation()).GetSafeNormal();
	if (WantedDirection.IsNearlyZero())
	{
		return;
	}

	const FVector CurrentDirection = GetControlRotation().Vector();
	// (A heavy hull turns slower than a light one.)
	const FVector NewDirection = FMath::VInterpNormalRotationTo(CurrentDirection, WantedDirection, DeltaTime,
		(BotState == EHCJBotState::Attack ? Skill.TurnRateDeg : AimTurnRateDeg) * Ship.GetTypeDef().BotTurnScale);

	FRotator NewRotation = NewDirection.Rotation();
	NewRotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(NewRotation.Pitch), -HCJBotMaxPitchDeg, HCJBotMaxPitchDeg);
	NewRotation.Roll = 0.0;
	SetControlRotation(NewRotation);
}

float AHCJBotController::GetAimErrorDegrees(const AHCJShipCharacter& Ship, const FVector& WorldPoint) const
{
	const FVector ToPoint = (WorldPoint - Ship.GetActorLocation()).GetSafeNormal();
	if (ToPoint.IsNearlyZero())
	{
		return 0.0f;
	}

	const double Cosine = FVector::DotProduct(Ship.GetAimRotation().Vector(), ToPoint);
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cosine, -1.0, 1.0))));
}
