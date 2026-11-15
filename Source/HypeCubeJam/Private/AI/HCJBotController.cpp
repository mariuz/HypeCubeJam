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
#include "GameFramework/PlayerController.h"
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

	/**
	 * How a bot that flies "smart" uses its ship type (multipliers on what the skill row says; the fighter is the row of ones).
	 * The ship type table itself (Ship/HCJShipTypes.h) is untouched: this is behaviour, not numbers of the ship.
	 */
	struct FHCJBotStyle
	{
		/** Distance kept to the target, against PreferredRange. */
		float Range;
		/** Sideways share while circling. */
		float Strafe;
		/** Readiness with torpedoes, the tractor beam and - to get away - the cloak. */
		float Torpedo;
		float Tractor;
		float Cloak;
		/** Added to the hull fraction at which the bot breaks off. */
		float RetreatAdd;
		/** Makes boosted passes close around a hull that turns slower. */
		bool bKnife;
		/** Approaches under cloak and drops it behind the target. */
		bool bAmbush;
		/** Prefers damaged targets, avoids a heavy hull whose shield towards it is whole. */
		bool bVulture;
	};

	const FHCJBotStyle& GetBotStyle(const AHCJShipCharacter& Ship, bool bSmart)
	{
		static const FHCJBotStyle Plain = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, false, false, false };
		static const FHCJBotStyle Styles[HCJShipTypes::NumTypes] = {
			/* Interceptor: hit and run */ { 0.8f, 1.25f, 1.0f, 1.0f, 1.0f, 0.05f, true, false, false },
			/* Fighter: the all-rounder (passes around the two heavy hulls) */ { 1.0f, 1.0f, 1.2f, 1.0f, 1.0f, 0.0f, true, false, false },
			/* Gunship: torpedo boat, stands off */ { 1.3f, 0.7f, 2.0f, 1.0f, 0.5f, 0.0f, false, false, false },
			/* Scout: cloak to get away, ambush, flee */ { 0.85f, 1.2f, 1.0f, 1.0f, 3.0f, 0.12f, true, true, true },
			/* Cruiser: tractor and hold (the beam feeds on the phaser bank: not at every chance) */ { 0.95f, 0.6f, 1.6f, 1.8f, 0.5f, 0.0f, false, false, false },
		};
		const int32 Type = static_cast<int32>(Ship.GetShipType());
		return (bSmart && !AHCJGameMode::IsClassicBots() && Type >= 0 && Type < HCJShipTypes::NumTypes) ? Styles[Type] : Plain;
	}

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
	bSmart = false;
	bSmartRolled = false;
	DisengageUntilTime = 0.0f;
	NextDisengageTime = Now + 3.0f;
	bDisengageCover = false;
	bCoverCounted = false;
	bDisengageRolled = false;
	DisengageThreat.Reset();
	bWasRetreating = false;
	RockShotKey = MAX_uint32;
	RockShotUntil = 0.0f;
	NextRockShotTime = 0.0f;
	StationDangerRadius = 0.0f;
	bStationHoldEdge = false;
	bChasing = false;
	bWarpChase = false;
	NextWarpChaseTime = 0.0f;
	ClosePassUntil = 0.0f;
	NextClosePassTime = 0.0f;
	RearArcSince = -1.0f;
	bRearHold = false;
	ResightTime = -1000.0f;
	bHeldByCap = false;
	bHeldByGrace = false;
	bHeldByRespite = false;
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
	/** Development: idle bots keep the trigger as a development command set it ("HCJMenuCmd enemyfire"). */
	bool GHCJBotsIdleKeepFire = false;
}

void AHCJBotController::SetDebugIdleKeepsFire(bool bKeep)
{
	GHCJBotsIdleKeepFire = bKeep;
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
			if (!GHCJBotsIdleKeepFire)
			{
				Ship->SetFiring(false);
			}
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

		if (!TickDisengage(*Ship, *Universe))
		{
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

	// (A new state is a new target or none: the rear-arc clock and the "seen again" time start over.)
	RearArcSince = -1.0f;
	bRearHold = false;
	ResightTime = -1000.0f;

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

	if (const AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
	{
		Skill = FHCJBotSkill::Get(GameMode->GetBotDifficulty());
		if (AHCJGameMode::IsNoFair())
		{
			// The bots before the fairness rules: a human ship is a ship like any other.
			Skill.ClearFairness();
		}
	}
	bHeldByCap = false;
	bHeldByGrace = false;
	bHeldByRespite = false;
	if (!bSmartRolled)
	{
		// Once per ship: whether this one is flown with the tactics of its difficulty or plainly.
		bSmartRolled = true;
		bSmart = !AHCJGameMode::IsClassicBots() && BotRandom.FRand() < Skill.Tactics;
	}
	const FHCJBotStyle& Style = GetBotStyle(Ship, bSmart);

	// Something useful on a pad nearby? In a fight only a short detour, otherwise (and when breaking off) worth a trip.
	PickupGoal = FindUsefulPickup(Ship, Universe, (BotState == EHCJBotState::Attack && !IsDisengaging()) ? PickupDetourRange : PickupSeekRange);

	// The station of this sector, while it has guns and the bot is not after them: a place to keep out of.
	StationDangerRadius = 0.0f;
	if (bSmart && Universe.HasStation(Sector) && TurretGoal.Get() == nullptr)
	{
		for (const TWeakObjectPtr<AHCJTurret>& Entry : AHCJTurret::GetAll())
		{
			const AHCJTurret* Turret = Entry.Get();
			if (Turret != nullptr && Turret->GetWorld() == GetWorld() && !Turret->IsTurretDestroyed() && Universe.GetSectorAtLocation(Turret->GetActorLocation()) == Sector)
			{
				StationDangerRadius = UHCJGameplaySettings::Get().TurretRange + HCJ::StationBodyRadius + 600.0f;
				StationDangerLocation = Universe.GetStationLocation(Sector);
				break;
			}
		}
	}

	// Low on hull: break off and make for a repair pad anywhere in the sector, when one holds a kit.
	bRetreating = false;
	if (const UHCJHealthComponent* Health = Ship.GetHealthComponent(); Health != nullptr && Health->GetHealthFraction() < Skill.RetreatHullFraction + Style.RetreatAdd)
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
	if (bRetreating && !bWasRetreating)
	{
		NotifyTactic(EHCJBotTactic::Retreat);
	}
	bWasRetreating = bRetreating;

	ThinkDisengage(Ship, Universe);
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

		// Another target takes over: one that is clearly closer or, for a bot that flies smart, the better choice
		// (the ship that is shooting at it, a weaker one, one that fewer bots are on).
		bool bSpread = false;
		if (AHCJShipCharacter* Better = PickTarget(Ship, Universe, EngageRange, Target, bSpread); Better != nullptr && Better != Target)
		{
			if (bSmart)
			{
				if (Better == Ship.GetRecentAttackerShip(2.5f))
				{
					NotifyTactic(EHCJBotTactic::ThreatTurn);
				}
				else if (bSpread)
				{
					NotifyTactic(EHCJBotTactic::SpreadTarget);
				}
			}
			Target = Better;
			TargetShip = Better;
			TargetAcquiredTime = Now;
			TargetDistance = FVector::Dist(ShipLocation, Better->GetActorLocation());
			LastTargetVisibleTime = Now;
			RockShotUntil = 0.0f;
			RearArcSince = -1.0f;
			bRearHold = false;
			ResightTime = -1000.0f;
		}

		const bool bHumanTarget = IsHumanShip(*Target);
		if (bHumanTarget)
		{
			// Fairness: how many bots are on this person (the peak is reported), and the attack cap - a bot that is over
			// it and was not shot by that person lets go: first for a bot that person shot, then the junior one.
			const int32 Seniors = CountBotsAttacking(*Target, true);
			if (!bRetreating && !IsDisengaging())
			{
				// (The breather that person gets once nobody attacks it any more runs from the last of these.)
				Target->NoteBotAttack(Now);
				if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
				{
					GameMode->NotifyHumanAttackers(1 + CountBotsAttacking(*Target, false));
				}
			}
			if (Skill.MaxAttackersOnHuman > 0 && Seniors >= Skill.MaxAttackersOnHuman && Ship.GetRecentAttackerShip(HCJBalance::ProvokedSeconds) != Target)
			{
				bHeldByCap = true;
				UE_LOG(LogHCJ, Verbose, TEXT("%s lets go of %s: %d bots are on it already (cap %d)"), *GetNameSafe(GetPawn()), *Target->GetName(), Seniors, Skill.MaxAttackersOnHuman);
				SetBotState(EHCJBotState::Wander);
				break;
			}
		}

		if (HasLineOfSight(Ship, *Target))
		{
			if (bHumanTarget && Skill.RearFireDelay > 0.0f && Now - LastTargetVisibleTime > 1.0f)
			{
				// A person who got a rock between us shook this bot off: it needs its reaction time again.
				ResightTime = Now;
			}
			LastTargetVisibleTime = Now;
		}
		else if (Now - LastTargetVisibleTime > LoseTargetSeconds + (Now < RockShotUntil ? 2.5f : 0.0f))
		{
			SetBotState(EHCJBotState::Wander);
			break;
		}

		// New aim error: a random offset at the target whose angular size is at most AimErrorDeg.
		// (From behind a person the aim is worse still.)
		UpdateRearHold(Ship, *Target, Now);
		const float AimError = Skill.AimErrorDeg * (RearArcSince >= 0.0f ? Skill.RearAimErrorScale : 1.0f);
		const float MaxOffset = TargetDistance * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(AimError, 0.0f, 45.0f)));
		AimErrorOffset = BotRandom.VRand() * (MaxOffset * BotRandom.FRand());

		ThinkTorpedo(Ship, *Target, TargetDistance, LastTargetVisibleTime >= Now);
		ThinkRockShot(Ship, Universe, *Target, TargetDistance);
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

			bool bSpread = false;
			if (AHCJShipCharacter* Choice = bSmart ? PickTarget(Ship, Universe, EngageRange, nullptr, bSpread) : nullptr)
			{
				if (bSpread)
				{
					NotifyTactic(EHCJBotTactic::SpreadTarget);
				}
				SetBotState(EHCJBotState::Attack);
				TargetShip = Choice;
				break;
			}
			if (!bSmart && FVector::DistSquared(ShipLocation, HostileLocation) <= FMath::Square(EngageRange) && HasLineOfSight(Ship, *Hostile))
			{
				SetBotState(EHCJBotState::Attack);
				TargetShip = Hostile;
				break;
			}

			// Out of range or behind a rock: close in on it instead of wandering at random.
			WanderPoint = HostileLocation;
			bHasWanderPoint = true;
			bChasing = true;
			WanderPointExpireTime = Now + HCJBotWanderPointLifetime;
			break;
		}
		bChasing = false;

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

	// Fairness counters: what held this bot back during this Think, once each.
	const bool bHeldByRear = bRearHold && BotState == EHCJBotState::Attack;
	if (bHeldByCap || bHeldByGrace || bHeldByRespite || bHeldByRear)
	{
		if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
		{
			if (bHeldByCap) { GameMode->NotifyFairHeldCap(); }
			if (bHeldByGrace) { GameMode->NotifyFairHeldGrace(); }
			if (bHeldByRespite) { GameMode->NotifyFairHeldRespite(); }
			if (bHeldByRear) { GameMode->NotifyFairHeldRear(); }
		}
	}
}

bool AHCJBotController::IsHumanShip(const AHCJShipCharacter& Ship)
{
	return Cast<APlayerController>(Ship.GetController()) != nullptr;
}

int32 AHCJBotController::CountBotsAttacking(const AHCJShipCharacter& Other, bool bAheadOnly) const
{
	int32 Count = 0;
	for (TActorIterator<AHCJBotController> It(GetWorld()); It; ++It)
	{
		const AHCJBotController* Bot = *It;
		if (Bot == this || Bot->BotState != EHCJBotState::Attack || Bot->TargetShip.Get() != &Other || Bot->GetShip() == nullptr || Bot->bRetreating || Bot->IsDisengaging())
		{
			continue;
		}
		if (bAheadOnly)
		{
			// Who comes before this bot when the cap is full: a bot that person shot (it may always answer, so it is the
			// others that make room for it), then whoever took the target earlier.
			const bool bProvoked = Bot->GetShip()->GetRecentAttackerShip(HCJBalance::ProvokedSeconds) == &Other;
			const bool bSenior = Bot->TargetAcquiredTime < TargetAcquiredTime || (Bot->TargetAcquiredTime == TargetAcquiredTime && Bot->GetUniqueID() < GetUniqueID());
			if (!bProvoked && !bSenior)
			{
				continue;
			}
		}
		++Count;
	}
	return Count;
}

bool AHCJBotController::IsOffLimits(const AHCJShipCharacter& Ship, const AHCJShipCharacter& Other) const
{
	if (!IsHumanShip(Other) || &Other == TargetShip.Get() || Ship.GetRecentAttackerShip(HCJBalance::ProvokedSeconds) == &Other)
	{
		return false;
	}
	if (Skill.ArrivalGrace > 0.0f && (Other.IsSpawnShielded() || GetWorld()->GetTimeSeconds() - Other.GetSectorArrivalTime() < Skill.ArrivalGrace))
	{
		bHeldByGrace = true;
		return true;
	}
	const int32 Attackers = (Skill.MaxAttackersOnHuman > 0 || Skill.RespiteSeconds > 0.0f) ? CountBotsAttacking(Other, false) : 0;
	if (Skill.MaxAttackersOnHuman > 0 && Attackers >= Skill.MaxAttackersOnHuman)
	{
		bHeldByCap = true;
		return true;
	}
	if (Skill.RespiteSeconds > 0.0f && Attackers == 0 && GetWorld()->GetTimeSeconds() - Other.GetLastBotAttackTime() < Skill.RespiteSeconds)
	{
		// Its attacker has just gone: a breather before the next one.
		bHeldByRespite = true;
		return true;
	}
	return false;
}

void AHCJBotController::UpdateRearHold(const AHCJShipCharacter& Ship, const AHCJShipCharacter& Target, float Now)
{
	// "Behind" is measured from the nose of the person's ship: that is where its pilot looks.
	const FVector ToUs = (Ship.GetActorLocation() - Target.GetActorLocation()).GetSafeNormal();
	const bool bInRearArc = IsHumanShip(Target) && FVector::DotProduct(Target.GetActorForwardVector(), ToUs) < HCJBalance::RearArcDot;
	if (!bInRearArc)
	{
		RearArcSince = -1.0f;
		bRearHold = false;
		return;
	}
	if (RearArcSince < 0.0f)
	{
		RearArcSince = Now;
	}
	// Whoever that person shot may answer from wherever it is.
	bRearHold = Now - RearArcSince < Skill.RearFireDelay && Ship.GetRecentAttackerShip(HCJBalance::ProvokedSeconds) != &Target;
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
				bWarpChase = false;
				WarpUntilTime = Now + Warp.ChargeSeconds + Warp.MaxSeconds + 1.0f;
				UE_LOG(LogHCJ, Verbose, TEXT("%s warps for the portal %.0f uu away"), *GetNameSafe(GetPawn()), Distance);
			}
		}
	}
	if (BotState != EHCJBotState::Travel)
	{
		WarpRolledForDim = INDEX_NONE;
	}

	const FHCJBotStyle& Style = GetBotStyle(Ship, bSmart);

	// ---- warp to a fight far away: tried now and then while closing in on a hostile that is out of range ----
	if (bSmart && BotState == EHCJBotState::Wander && bChasing && bHasWanderPoint && !IsDisengaging() && Now >= WarpUntilTime && bWarpReady && Now >= NextWarpChaseTime)
	{
		const float Distance = static_cast<float>(FVector::Dist(ShipLocation, WanderPoint));
		if (Distance > 1.4f * MinWarpWay && GetAimErrorDegrees(Ship, WanderPoint) < 25.0f && IsWayClear(Ship, WanderPoint, Distance - 3500.0f))
		{
			NextWarpChaseTime = Now + 12.0f;
			if (BotRandom.FRand() < Skill.WarpTravelChance)
			{
				WarpGoal = WanderPoint;
				bWarpFlee = false;
				bWarpChase = true;
				WarpUntilTime = Now + Warp.ChargeSeconds + Warp.MaxSeconds + 1.0f;
				NotifyTactic(EHCJBotTactic::WarpChase);
				UE_LOG(LogHCJ, Verbose, TEXT("%s warps towards a fight %.0f uu away"), *GetNameSafe(GetPawn()), Distance);
			}
		}
	}

	// ---- warp to get away when the hull is low or the bot is breaking off: rolled once per retreat ----
	const bool bHurt = Health->GetHealthFraction() < Skill.RetreatHullFraction || IsDisengaging();
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
				bWarpChase = false;
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
	if (!Ship.IsCloaked() && !Ship.IsCloakLocked() && Ship.GetCloakEnergy() > 0.6f && Ship.GetWarpPhase() == EHCJWarpPhase::Idle && Now >= WarpUntilTime)
	{
		const AHCJShipCharacter* Target = TargetShip.Get();
		const bool bApproach = BotState == EHCJBotState::Attack && Target != nullptr && FVector::DistSquared(Target->GetActorLocation(), ShipLocation) > FMath::Square(7000.0);
		const bool bClosingIn = BotState == EHCJBotState::Wander && bHasWanderPoint && FindNearestHostile(Ship, Universe, 30000.0f) != nullptr;
		const bool bAway = bRetreating || IsDisengaging();
		// The cloak takes the shields down: for getting away (they are gone anyway) a type that lives by it uses it far
		// more readily than for an approach, which arrives without shields.
		if ((bApproach || bClosingIn || bAway) && BotRandom.FRand() < Skill.CloakChance * (bAway ? Style.Cloak : 1.0f))
		{
			Ship.SetFiring(false);
			Ship.SetCloaking(true);
			CloakUntilTime = Now + (bAway ? 8.0f : 12.0f);
			UE_LOG(LogHCJ, Verbose, TEXT("%s cloaks (%s)"), *GetNameSafe(GetPawn()), bAway ? TEXT("retreat") : TEXT("approach"));
		}
	}

	// ---- tractor beam: on a target in reach that runs or is nearly done ----
	if (BotState == EHCJBotState::Attack && Now >= TractorUntilTime && !bRetreating && !IsDisengaging() && !bRearHold && !Ship.IsCloaked() && Ship.GetPhaserCharge() > 0.5f && !Ship.IsPhaserLocked())
	{
		AHCJShipCharacter* Target = TargetShip.Get();
		if (Target != nullptr && !Target->IsDead() && Target->GetTractorHolder() == nullptr && Now - LastTargetVisibleTime < 0.5f)
		{
			const FVector ToTarget = Target->GetActorLocation() - ShipLocation;
			const float Distance = static_cast<float>(ToTarget.Size());
			const float AwaySpeed = static_cast<float>(FVector::DotProduct(Target->GetVelocity(), ToTarget.GetSafeNormal()));
			const UHCJHealthComponent* TargetHealth = Target->GetHealthComponent();
			// (Also a fast ship close by: held, it flies at half speed and cannot circle a heavy hull any more.)
			const bool bWorthIt = AwaySpeed > 1200.0f || (TargetHealth != nullptr && TargetHealth->GetHealthFraction() < 0.45f)
				|| (bSmart && Distance < 2600.0f && Target->GetVelocity().SizeSquared() > FMath::Square(3000.0));
			if (Distance < Ship.GetTypeDef().TractorRange * 0.9f && GetAimErrorDegrees(Ship, Target->GetActorLocation()) < 30.0f
				&& BotRandom.FRand() < Skill.TractorChance * Style.Tractor * (bWorthIt ? 3.0f : 1.0f))
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
		const bool bStillWanted = bWarpFlee || BotState == EHCJBotState::Travel || (bWarpChase && BotState == EHCJBotState::Wander);
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
	Ship.SetTractoring(Now < TractorUntilTime && BotState == EHCJBotState::Attack && !IsDisengaging());

	// Cloak: no firing while it is up; it comes down in time for the fight, or when its time is over. A scout that
	// flies smart keeps it up until it is close behind its target.
	if (Ship.IsCloaked())
	{
		const AHCJShipCharacter* Target = TargetShip.Get();
		// (No ambush from cloak on a person below Hard: the cloak then comes down at the ordinary distance.)
		const bool bAmbush = GetBotStyle(Ship, bSmart).bAmbush && (Skill.bFlankHumans || Target == nullptr || !IsHumanShip(*Target));
		const bool bClose = BotState == EHCJBotState::Attack && Target != nullptr
			&& FVector::DistSquared(Target->GetActorLocation(), ShipLocation) < FMath::Square(bAmbush ? 2600.0 : static_cast<double>(PreferredRange) * 1.3);
		const bool bAway = bRetreating || IsDisengaging();
		if (Now >= CloakUntilTime || (bClose && !bAway) || Ship.IsCloakRevealed())
		{
			if (bAmbush && bClose && !bAway)
			{
				NotifyTactic(EHCJBotTactic::Ambush);
			}
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
		if (DistSq <= BestDistSq && Universe.GetSectorAtLocation(OtherLocation) == Sector && !IsOffLimits(Ship, *Other))
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
	// Fairness: sectors of people that have their share of bots (MaxHuntersPerHuman).
	TArray<int32, TInlineAllocator<8>> FullSectors;
	bool bHeldByHunters = false;

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
			if (Skill.MaxHuntersPerHuman > 0)
			{
				// Fairness: enough bots are in that person's sector already, or on their way into it - this one goes
				// somewhere else (and does not roam into it either, below).
				int32 Hunters = 0;
				for (TActorIterator<AHCJShipCharacter> It(GetWorld()); It; ++It)
				{
					Hunters += (!It->IsDead() && !IsHumanShip(**It) && It->GetCurrentSector() == HumanSector) ? 1 : 0;
				}
				for (TActorIterator<AHCJBotController> It(GetWorld()); It; ++It)
				{
					const AHCJBotController* Bot = *It;
					if (Bot == this || Bot->BotState != EHCJBotState::Travel || Bot->GetShip() == nullptr || Bot->TravelFromSector == HumanSector)
					{
						continue;
					}
					const int32 Goal = Bot->TravelDim == HCJ::WormholeHop ? Universe.GetWormholeDest(Bot->TravelFromSector) : Graph.GetNeighbor(Bot->TravelFromSector, Bot->TravelDim);
					Hunters += Goal == HumanSector ? 1 : 0;
				}
				if (Hunters >= Skill.MaxHuntersPerHuman)
				{
					FullSectors.AddUnique(HumanSector);
					bHeldByHunters = true;
					continue;
				}
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
	// Not into a person's sector that is full, as long as there is another way out. (No such sector without a person
	// in the match, so the draw below is the one of before among bots.)
	for (int32 Index = OpenDims.Num() - 1; Index >= 0 && FullSectors.Num() > 0 && OpenDims.Num() > 1; --Index)
	{
		const int32 Goal = OpenDims[Index] == HCJ::WormholeHop ? Universe.GetWormholeDest(FromSector) : Graph.GetNeighbor(FromSector, OpenDims[Index]);
		if (FullSectors.Contains(Goal))
		{
			OpenDims.RemoveAt(Index);
		}
	}
	if (bHeldByHunters)
	{
		if (AHCJGameMode* GameMode = World != nullptr ? World->GetAuthGameMode<AHCJGameMode>() : nullptr)
		{
			GameMode->NotifyFairHeldHunters();
		}
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
	// (A person that was out of sight for a moment gets that delay again; a bot in a person's rear arc holds its fire first.)
	const bool bHumanTarget = IsHumanShip(*Target);
	UpdateRearHold(Ship, *Target, Now);
	const bool bReacted = Now - FMath::Max(TargetAcquiredTime, ResightTime) >= Skill.ReactionDelay;
	const bool bInBurst = Skill.BurstPause <= 0.0f || FMath::Fmod(Now + BurstPhase, Skill.BurstSeconds + Skill.BurstPause) < Skill.BurstSeconds;
	const float FireRange = FMath::Min(EngageRange * Skill.EngageRangeFactor, Weapon.BotMaxRange);
	// (SteerTowards: a bot that can hit its target from here does not follow it under the guns of a station.)
	bStationHoldEdge = bTargetVisible && Distance <= FireRange;
	bool bFire = bTargetVisible && bReacted && bInBurst && !bRetreating && !bRearHold
		&& Distance <= FireRange && GetAimErrorDegrees(Ship, AimPoint) <= FireConeDeg;
	if (bFire && Weapon.bBeam)
	{
		// The beam is where the nose points, at once: held only while the aim is on the target, and a better pilot keeps
		// a reserve in the bank unless the target is about to go.
		const UHCJHealthComponent* TargetHealth = Target->GetHealthComponent();
		const bool bFinishing = TargetHealth != nullptr && TargetHealth->GetHealthFraction() < 0.25f;
		bFire = GetAimErrorDegrees(Ship, AimPoint) <= Skill.PhaserConeDeg && (bFinishing || Ship.GetPhaserCharge() > Skill.PhaserReserve);
	}
	Ship.SetFiring(bFire);

	// A small rock in the line of fire is being shot away: the nose and the beam go on it until it breaks or the target shows.
	if (Now < RockShotUntil)
	{
		FVector RockLocation;
		float RockRadius = 0.0f;
		if (bTargetVisible || !Weapon.bBeam || !Universe.GetAsteroidSphere(RockShotKey, RockLocation, RockRadius))
		{
			RockShotUntil = 0.0f;
		}
		else
		{
			DesiredAimPoint = RockLocation;
			const float RockDistance = static_cast<float>(FVector::Dist(ShipLocation, RockLocation));
			Ship.SetFiring(GetAimErrorDegrees(Ship, RockLocation) <= FMath::Max(2.5f, FMath::RadiansToDegrees(FMath::Atan2(RockRadius * 0.6f, FMath::Max(RockDistance, 1.0f)))));
		}
	}

	const FHCJBotStyle& Style = GetBotStyle(Ship, bSmart);

	// A scout under cloak goes for the tail of its target (TickSystems drops the cloak there).
	// (Not the tail of a person below Hard.)
	if (Style.bAmbush && Ship.IsCloaked() && !bRetreating && (Skill.bFlankHumans || !bHumanTarget))
	{
		const FVector Behind = TargetLocation - Target->GetActorForwardVector() * 2000.0;
		Ship.SetBoosting(FVector::DistSquared(ShipLocation, Behind) > FMath::Square(3500.0));
		SteerTowards(Ship, Universe, Behind, 1.0f, false);
		return;
	}

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
	// A light, agile ship against a hull that turns slower: now and then a boosted pass close around it - the heavy
	// ship cannot keep its nose (and its fore shield) on something that crosses it that fast.
	const bool bOutTurns = Style.bKnife && (Skill.bFlankHumans || !bHumanTarget) && Target->GetTypeDef().BotTurnScale < Ship.GetTypeDef().BotTurnScale - 0.05f;
	if (bOutTurns && Now >= NextClosePassTime && Distance < 1.6f * PreferredRange && bTargetVisible && !bRetreating)
	{
		ClosePassUntil = Now + 1.8f;
		NextClosePassTime = Now + BotRandom.FRandRange(12.0f, 18.0f);
		NotifyTactic(EHCJBotTactic::ClosePass);
	}
	const bool bClosePass = bOutTurns && Now < ClosePassUntil;

	const float StrafeStrength = bClosePass ? 1.0f : FMath::Min(HCJBotStrafeStrength * Skill.StrafeStrength / 0.8f * Style.Strafe, 1.0f);
	FVector Strafe = (Right * StrafeSign + Up * (0.5f * Weave)).GetSafeNormal() * StrafeStrength;
	if (bHumanTarget && !bClosePass && Skill.FrontBias > 0.0f)
	{
		// Fairness: round to the front of a person's ship, where its pilot looks. The pull is the part of that ship's
		// forward direction that lies across our line to it: nothing dead ahead of it or dead astern, all of it on its flank.
		const FVector TargetForward = Target->GetActorForwardVector();
		const FVector FrontTangent = TargetForward - TargetDirection * FVector::DotProduct(TargetForward, TargetDirection);
		Strafe = (Strafe + FrontTangent * Skill.FrontBias).GetClampedToMaxSize(1.0f);
	}

	// (A ship that stands off may use more of its weapon's reach.)
	const float WantedRange = bClosePass ? 1500.0f : FMath::Min(PreferredRange * Style.Range, (Style.Range > 1.0f ? 0.8f : 0.6f) * Weapon.BotMaxRange);
	const float RangeError = Distance - WantedRange;
	const float Radial = FMath::Clamp(RangeError / FMath::Max(WantedRange * 0.5f, 1.0f), -1.0f, 1.0f);

	Ship.SetBoosting(Distance > BoostDistance || bClosePass);

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

	// Under the guns of a station (and not there to shoot at them): no further in, and out again - but never so that
	// the bot is left standing at the edge (the playthrough watch found exactly that: ships waiting for 20 s in front
	// of a portal, a pad or a wander point they were not allowed to reach):
	//  - what the bot flies to lies under the guns itself (a portal, a pad, a ship it has to get at): it goes in;
	//  - in a fight with a target it can already hit from where it is: it holds the edge;
	//  - otherwise the way leads past the station: around it, along the edge.
	if (StationDangerRadius > 0.0f && !bEvading)
	{
		const FVector FromStation = ShipLocation - StationDangerLocation;
		const float StationDistance = static_cast<float>(FromStation.Size());
		const bool bFighting = BotState == EHCJBotState::Attack && !IsDisengaging();
		const bool bGoalInside = FVector::DistSquared(WorldTarget, StationDangerLocation) < FMath::Square(static_cast<double>(StationDangerRadius));
		const bool bApply = bFighting ? bStationHoldEdge : !bGoalInside;
		if (bApply && StationDistance < StationDangerRadius && StationDistance > 1.0f)
		{
			const FVector Outward = FromStation / StationDistance;
			const double Inward = -FVector::DotProduct(DesiredVelocity, Outward);
			if (Inward > 0.0)
			{
				FVector Along = DesiredVelocity + Outward * Inward;
				if (!bFighting)
				{
					// The whole wanted speed along the edge; when the goal is straight behind the station, to one side of it.
					const double Speed = DesiredVelocity.Size();
					if (Along.SizeSquared() < FMath::Square(0.3 * Speed))
					{
						Along = FVector::CrossProduct(Outward, FMath::Abs(Outward.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector);
					}
					Along = Along.GetSafeNormal() * Speed;
				}
				DesiredVelocity = Along;
			}
			DesiredVelocity += Outward * (MaxSpeed * 0.5f * FMath::Clamp((StationDangerRadius - StationDistance) / 2000.0f, 0.0f, 1.0f));
			if (const float Now = static_cast<float>(GetWorld()->GetTimeSeconds()); Now >= NextStationSteerCountTime)
			{
				// One count per visit, more or less.
				NextStationSteerCountTime = Now + 8.0f;
				NotifyTactic(EHCJBotTactic::StationSteer);
			}
		}
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
	Input.bReady = Ship.IsTorpedoReady() && !bRetreating && !bRearHold && Now - LastTorpedoTime > TorpedoSkill.MinSecondsBetween
		&& Now - FMath::Max(TargetAcquiredTime, ResightTime) >= Skill.ReactionDelay;
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
	const FHCJBotStyle& Style = GetBotStyle(Ship, bSmart);
	bool bShoot = HCJTorpedo::ShouldBotFire(Input, TorpedoSkill, MinRange, MaxRange);
	// The phasers have opened a facing: a torpedo through it, at once rather than by a small chance.
	const bool bOpen = Input.FacingFraction <= 0.0f;
	if (!bShoot && bOpen && Input.bReady && Input.bLockedAndAimed && TargetDistance >= MinRange && TargetDistance <= MaxRange && Input.HullFraction >= 0.15f
		&& BotRandom.FRand() < Skill.ComboChance * Style.Torpedo)
	{
		bShoot = true;
	}
	if (bShoot && Skill.TorpedoOnHumanChance < 1.0f && IsHumanShip(Target) && BotRandom.FRand() >= Skill.TorpedoOnHumanChance)
	{
		// Fairness: this one is not fired at a person. (The reload clock runs as if it had been, so that the next
		// decision is not just a moment away.)
		bShoot = false;
		LastTorpedoTime = Now;
	}
	if (bShoot)
	{
		if (bOpen)
		{
			NotifyTactic(EHCJBotTactic::ComboTorpedo);
		}
		// The tractor beam first, when the target is in its reach: a ship that is held cannot run from the torpedo.
		if (bSmart && !AHCJGameMode::IsClassicBots() && Now >= TractorUntilTime && Target.GetTractorHolder() == nullptr && !Ship.IsCloaked() && Ship.GetPhaserCharge() > 0.3f
			&& !Ship.IsPhaserLocked() && TargetDistance < Ship.GetTypeDef().TractorRange * 0.9f && BotRandom.FRand() < Skill.Tactics * FMath::Min(Style.Tractor, 1.5f) * 0.7f)
		{
			TractorUntilTime = Now + 2.5f;
			NotifyTactic(EHCJBotTactic::TractorTorpedo);
		}
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
	if (!(bRetreating || IsDisengaging() || BotState == EHCJBotState::Travel) || Ship.IsFiring() || Health == nullptr || Health->GetNumFacings() != HCJShield::NumFacings)
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

// ---------------------------------------------------------------------------------------------------------------
// Tactics
// ---------------------------------------------------------------------------------------------------------------

void AHCJBotController::NotifyTactic(EHCJBotTactic Tactic) const
{
	if (AHCJGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHCJGameMode>() : nullptr)
	{
		GameMode->NotifyBotTactic(Tactic);
	}
	UE_LOG(LogHCJ, Verbose, TEXT("%s: tactic - %s"), *GetNameSafe(GetPawn()), HCJBalance::GetTacticName(Tactic));
}

FString AHCJBotController::DescribeForLog() const
{
	const AHCJShipCharacter* Ship = GetShip();
	if (Ship == nullptr)
	{
		return TEXT("no_ship");
	}
	const AHCJShipCharacter* Target = TargetShip.Get();
	const AHCJPickup* Pad = PickupGoal.Get();
	// Inside rock? (A sphere of the ship's size at its place, against asteroids only.)
	const bool bInRock = GetWorld()->OverlapAnyTestByObjectType(Ship->GetActorLocation(), FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(HCJ::ShipCollisionRadius * Ship->GetTypeDef().Scale), MakeBotQueryParams(*Ship));
	FString TargetName = Target != nullptr && Target->GetPlayerState() != nullptr ? Target->GetPlayerState()->GetPlayerName() : FString(TEXT("-"));
	TargetName.ReplaceInline(TEXT(" "), TEXT("_"));
	return FString::Printf(TEXT("type=%s smart=%d target=%s target_dist=%.0f retreating=%d disengaging=%d pad=%d turret_goal=%d wander_point=%d chasing=%d firing=%d boosting=%d warp=%d cloaked=%d tractor_target=%d held=%d in_rock=%d evading=%d portal_cooldown=%.1f travel_dim=%d"),
		*FString(Ship->GetTypeDef().ShortName).ToLower(), bSmart ? 1 : 0, *TargetName, Target != nullptr ? FVector::Dist(Target->GetActorLocation(), Ship->GetActorLocation()) : -1.0,
		bRetreating ? 1 : 0, IsDisengaging() ? 1 : 0, (Pad != nullptr && Pad->IsActive()) ? 1 : 0, TurretGoal.Get() != nullptr ? 1 : 0, bHasWanderPoint ? 1 : 0, bChasing ? 1 : 0,
		Ship->IsFiring() ? 1 : 0, Ship->IsBoosting() ? 1 : 0, static_cast<int32>(Ship->GetWarpPhase()), Ship->IsCloaked() ? 1 : 0, Ship->GetTractorTarget() != nullptr ? 1 : 0,
		Ship->GetTractorHolder() != nullptr ? 1 : 0, bInRock ? 1 : 0, GetWorld()->GetTimeSeconds() < EvadeUntilTime ? 1 : 0, Ship->GetPortalCooldownRemaining(), TravelDim);
}

bool AHCJBotController::IsDisengaging() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->GetTimeSeconds() < DisengageUntilTime;
}

AHCJShipCharacter* AHCJBotController::PickTarget(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe, float MaxRange, AHCJShipCharacter* Current, bool& bOutSpread) const
{
	bOutSpread = false;
	UWorld* World = GetWorld();
	if (World == nullptr || !Universe.IsBuilt())
	{
		return Current;
	}
	const FVector ShipLocation = Ship.GetActorLocation();

	if (!bSmart)
	{
		// The plain rule: the nearest visible hostile, and only when it is clearly nearer than the current target.
		AHCJShipCharacter* Nearest = FindNearestHostile(Ship, Universe, MaxRange);
		if (Nearest == nullptr || Nearest == Current || !HasLineOfSight(Ship, *Nearest))
		{
			return Current;
		}
		if (Current != nullptr && FVector::Dist(ShipLocation, Nearest->GetActorLocation()) >= FVector::Dist(ShipLocation, Current->GetActorLocation()) * HCJBotRetargetRatio)
		{
			return Current;
		}
		return Nearest;
	}

	const int32 Sector = Universe.GetSectorAtLocation(ShipLocation);
	const UHCJHealthComponent* OwnHealth = Ship.GetHealthComponent();
	const AHCJShipCharacter* Attacker = Ship.GetRecentAttackerShip(2.5f);
	const FHCJBotStyle& Style = GetBotStyle(Ship, true);

	// How many other bots are on each ship already.
	TMap<const AHCJShipCharacter*, int32, TInlineSetAllocator<16>> Crowd;
	for (TActorIterator<AHCJBotController> It(World); It; ++It)
	{
		const AHCJBotController* Other = *It;
		if (Other != this && Other->BotState == EHCJBotState::Attack && Other->GetShip() != nullptr)
		{
			if (const AHCJShipCharacter* Theirs = Other->TargetShip.Get())
			{
				++Crowd.FindOrAdd(Theirs);
			}
		}
	}

	AHCJShipCharacter* Best = nullptr;
	float BestScore = -1.0e9f;
	const AHCJShipCharacter* Nearest = nullptr;
	double NearestDistSq = TNumericLimits<double>::Max();
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		AHCJShipCharacter* Other = *It;
		if (Other == &Ship || !IsValid(Other) || Other->IsDead())
		{
			continue;
		}
		const FVector OtherLocation = Other->GetActorLocation();
		const double DistSq = FVector::DistSquared(ShipLocation, OtherLocation);
		if (DistSq > FMath::Square(static_cast<double>(MaxRange)) || Universe.GetSectorAtLocation(OtherLocation) != Sector
			|| (Other->IsCloakHidden() && DistSq > FMath::Square(static_cast<double>(UHCJGameplaySettings::Get().CloakDetectRange)))
			|| IsOffLimits(Ship, *Other))
		{
			continue;
		}
		if (Other != Current && !HasLineOfSight(Ship, *Other))
		{
			continue;
		}
		const UHCJHealthComponent* Health = Other->GetHealthComponent();
		if (Health == nullptr)
		{
			continue;
		}
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Other;
		}

		const float Distance = static_cast<float>(FMath::Sqrt(DistSq));
		const FVector ToUs = ShipLocation - OtherLocation;
		// What its target panel would show: the hull, and the facing it turns towards us.
		const float Shown = Health->GetNumFacings() == HCJShield::NumFacings
			? Health->GetFacingFraction(static_cast<int32>(HCJShield::SelectFacing(Other->GetActorTransform().InverseTransformVectorNoScale(ToUs)))) : Health->GetShieldFraction();
		const float HullGone = 1.0f - Health->GetHealthFraction();

		float Score = -Distance / 1000.0f;
		Score += (Style.bVulture ? 8.0f : 5.0f) * HullGone;
		Score += Shown <= 0.0f ? 4.0f : Shown <= HCJShield::WeakFraction ? 2.0f : 0.0f;
		if (Other == Attacker)
		{
			// Whoever is shooting at us, more so when it has found a side of our shield that is weak.
			Score += 5.0f;
			if (OwnHealth != nullptr && OwnHealth->GetNumFacings() == HCJShield::NumFacings)
			{
				const EHCJShieldFacing Side = HCJShield::SelectFacing(Ship.GetActorTransform().InverseTransformVectorNoScale(-ToUs));
				if (Side != EHCJShieldFacing::Fore && OwnHealth->GetFacingFraction(static_cast<int32>(Side)) <= HCJShield::WeakFraction)
				{
					Score += 2.5f;
				}
			}
		}
		// Its tail is towards us: it is busy with somebody else. (A person's back is no invitation: FHCJBotSkill::HumanTailScore.)
		if (FVector::DotProduct(Other->GetActorForwardVector(), ToUs.GetSafeNormal()) < HCJBalance::RearArcDot)
		{
			Score += IsHumanShip(*Other) ? Skill.HumanTailScore : 1.5f;
		}
		if (const int32* Others = Crowd.Find(Other))
		{
			Score -= 2.5f * *Others;
		}
		if (Other->IsSpawnShielded())
		{
			Score -= 8.0f;
		}
		if (Style.bVulture && Other->GetTypeDef().MaxHull > Ship.GetTypeDef().MaxHull * 1.1f && Shown > 0.6f && HullGone < 0.3f)
		{
			// A light ship does not open a fair fight with a heavy one.
			Score -= 3.0f;
		}
		if (Other == Current)
		{
			Score += 3.0f;
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}

	if (Best != nullptr && Nearest != nullptr && Best != Nearest)
	{
		const int32* OnNearest = Crowd.Find(Nearest);
		const int32* OnBest = Crowd.Find(Best);
		bOutSpread = (OnNearest != nullptr ? *OnNearest : 0) > (OnBest != nullptr ? *OnBest : 0);
	}
	return Best != nullptr ? Best : Current;
}

void AHCJBotController::ThinkDisengage(AHCJShipCharacter& Ship, const AHCJUniverse& Universe)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const UHCJHealthComponent* Health = Ship.GetHealthComponent();
	if (Health == nullptr || AHCJGameMode::IsClassicBots())
	{
		return;
	}
	const FVector ShipLocation = Ship.GetActorLocation();

	if (IsDisengaging())
	{
		FVector Unused;
		const bool bCalm = !Ship.GetRecentAttackerLocation(2.5f, Unused);
		const AHCJShipCharacter* Threat = DisengageThreat.Get();
		const bool bThreatGone = Threat == nullptr || Threat->IsDead() || Threat->GetCurrentSector() != Ship.GetCurrentSector();
		if (bDisengageCover && !bCoverCounted && !bThreatGone && !HasLineOfSight(Ship, *Threat))
		{
			// A rock is between the two.
			bCoverCounted = true;
			NotifyTactic(EHCJBotTactic::Cover);
		}
		if (bRetreating || bThreatGone || (bCalm && Health->GetShieldFraction() > 0.55f))
		{
			// Back into it (a repair pad that has a kit again takes over as the plain retreat).
			DisengageUntilTime = 0.0f;
		}
		if (!IsDisengaging())
		{
			NextDisengageTime = Now + 7.0f;
			bDisengageRolled = false;
			NotifyTactic(EHCJBotTactic::Reengage);
		}
		return;
	}
	if (DisengageUntilTime > 0.0f)
	{
		// Its time ran out between two Thinks.
		DisengageUntilTime = 0.0f;
		NextDisengageTime = Now + 7.0f;
		bDisengageRolled = false;
		NotifyTactic(EHCJBotTactic::Reengage);
	}

	AHCJShipCharacter* Attacker = Ship.GetRecentAttackerShip(2.0f);
	if (Attacker == nullptr || bRetreating || Now < NextDisengageTime || Health->GetNumFacings() != HCJShield::NumFacings)
	{
		if (Attacker == nullptr)
		{
			bDisengageRolled = false;
		}
		return;
	}

	// The occasion: the facing the attacker is on is down, and the hull is low - for a hit-and-run ship, touched.
	const FHCJBotStyle& Style = GetBotStyle(Ship, bSmart);
	const FVector AttackerLocation = Attacker->GetActorLocation();
	const EHCJShieldFacing Side = HCJShield::SelectFacing(Ship.GetActorTransform().InverseTransformVectorNoScale(AttackerLocation - ShipLocation));
	const bool bOpen = Health->GetFacingFraction(static_cast<int32>(Side)) <= 0.0f;
	const float Hull = Health->GetHealthFraction();
	const bool bLow = Hull < Skill.RetreatHullFraction + Style.RetreatAdd;
	if (!bOpen || !bLow)
	{
		bDisengageRolled = false;
		return;
	}
	if (bDisengageRolled)
	{
		return;
	}
	bDisengageRolled = true;
	if (BotRandom.FRand() >= Skill.Tactics)
	{
		return;
	}

	// Only with a way out - a rock to get behind, the cloak, or a pad that mends the ship: a ship that
	// just turns its tail to a pursuer of the same speed stops shooting and dies all the same.
	FVector CoverPoint = FVector::ZeroVector;
	const bool bCover = bSmart && Universe.FindCoverPoint(ShipLocation, AttackerLocation, 5000.0f, 350.0f, 500.0f * Ship.GetTypeDef().Scale, CoverPoint);
	const bool bCloakOut = !Ship.IsCloaked() && !Ship.IsCloakLocked() && Ship.GetCloakEnergy() > 0.5f && Ship.GetWarpPhase() == EHCJWarpPhase::Idle;
	const AHCJPickup* Pad = PickupGoal.Get();
	const bool bPadOut = Pad != nullptr && Pad->IsActive() && (Pad->GetItem() == EHCJPickupItem::Repair || Pad->GetItem() == EHCJPickupItem::ShieldCell);
	// (Not the warp drive: a hit while it charges stops it, so under fire it is a hope, not a way out. ThinkSystems still tries it.)
	if (!(bCover || bCloakOut || bPadOut))
	{
		return;
	}

	DisengageUntilTime = Now + 7.0f;
	DisengageThreat = Attacker;
	bCoverCounted = false;
	bDisengageCover = bCover;
	if (bCover)
	{
		DisengagePoint = CoverPoint;
	}
	else
	{
		// Straight away from the attacker, bent towards the middle of the sector.
		const FVector Middle = Universe.GetSectorOrigin(Ship.GetCurrentSector());
		const FVector Away = (ShipLocation - AttackerLocation).GetSafeNormal() + (Middle - ShipLocation).GetSafeNormal() * 0.6;
		DisengagePoint = ShipLocation + Away.GetSafeNormal() * 7000.0;
	}
	if (bCloakOut && !bCover && !bPadOut && bSmart && BotRandom.FRand() < FMath::Min(Skill.CloakChance * Style.Cloak * 4.0f, 1.0f))
	{
		// Nothing to hide behind: out of sight at once (the shields on that side are gone anyway).
		Ship.SetFiring(false);
		Ship.SetCloaking(true);
		CloakUntilTime = Now + 7.0f;
	}
	// (ThinkSystems may add the warp drive and the cloak: bWarpFleeRolled starts again for this retreat.)
	bWarpFleeRolled = false;
	NotifyTactic(EHCJBotTactic::Retreat);
	UE_LOG(LogHCJ, Verbose, TEXT("%s breaks off from %s (hull %.0f %%, %s facing down): %s"), *GetNameSafe(GetPawn()), *Attacker->GetName(), Hull * 100.0f, HCJShield::FacingName(Side),
		bDisengageCover ? TEXT("for a rock") : TEXT("away"));
}

bool AHCJBotController::TickDisengage(AHCJShipCharacter& Ship, const AHCJUniverse& Universe)
{
	if (!IsDisengaging())
	{
		return false;
	}
	Ship.SetFiring(false);
	const FVector ShipLocation = Ship.GetActorLocation();
	FVector Goal = DisengagePoint;
	bool bArrive = bDisengageCover;
	if (const AHCJPickup* Pad = PickupGoal.Get(); Pad != nullptr && Pad->IsActive() && (Pad->GetItem() == EHCJPickupItem::Repair || Pad->GetItem() == EHCJPickupItem::ShieldCell))
	{
		// Something that mends the ship is in reach: over it.
		Goal = Pad->GetActorLocation();
		bArrive = false;
	}
	const double DistSq = FVector::DistSquared(ShipLocation, Goal);
	Ship.SetBoosting(DistSq > FMath::Square(1500.0));
	SteerTowards(Ship, Universe, Goal, 1.0f, bArrive);
	// The nose along the way out; TickShieldFacing turns the best facing to the attacker instead while one is weak.
	const AHCJShipCharacter* Threat = DisengageThreat.Get();
	DesiredAimPoint = (DistSq > FMath::Square(900.0) || Threat == nullptr) ? Goal : Threat->GetActorLocation();
	bHasAimPoint = true;
	return true;
}

void AHCJBotController::ThinkRockShot(AHCJShipCharacter& Ship, const AHCJUniverse& Universe, const AHCJShipCharacter& Target, float TargetDistance)
{
	const UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	if (!bSmart || Now < NextRockShotTime || Now < RockShotUntil || LastTargetVisibleTime >= Now || bRetreating || IsDisengaging() || Ship.IsCloaked()
		|| !Universe.GetAsteroidRules().bEnabled)
	{
		return;
	}
	NextRockShotTime = Now + 1.0f;
	// Only the phaser breaks rock, and only a rock that costs little: small, near, and the target right behind it.
	const FHCJWeaponDef& Weapon = Ship.GetFireWeaponDef();
	if (!Weapon.bBeam || TargetDistance > Weapon.BotMaxRange || Ship.GetPhaserCharge() < 0.5f)
	{
		return;
	}
	FCollisionQueryParams Params = MakeBotQueryParams(Ship);
	Params.AddIgnoredActor(&Target);
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, Ship.GetActorLocation(), Target.GetActorLocation(), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return;
	}
	const uint32 Key = Universe.FindAsteroidKey(Hit.GetComponent(), Hit.Item);
	const float Health = Key != MAX_uint32 ? Universe.GetAsteroidHealth(Key) : -1.0f;
	if (Health <= 0.0f || Health > 140.0f || Hit.Distance > 4500.0f || BotRandom.FRand() >= Skill.Tactics)
	{
		return;
	}
	RockShotKey = Key;
	RockShotUntil = Now + 3.2f;
	NotifyTactic(EHCJBotTactic::RockShot);
	UE_LOG(LogHCJ, Verbose, TEXT("%s shoots at a rock (%.0f hit points, %.0f uu away) that hides %s"), *GetNameSafe(GetPawn()), Health, Hit.Distance, *Target.GetName());
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
