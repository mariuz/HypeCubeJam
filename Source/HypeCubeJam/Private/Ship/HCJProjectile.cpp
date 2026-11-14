#include "Ship/HCJProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

#include "Assets/HCJAssetSettings.h"
#include "Effects/HCJEffectsDirector.h"
#include "Game/HCJGameMode.h"
#include "Game/HCJGameplaySettings.h"
#include "Game/HCJPlayerController.h"
#include "Game/HCJPlayerState.h"
#include "HypeCubeJam.h"
#include "Ship/HCJShipCharacter.h"
#include "Ship/HCJTorpedo.h"
#include "Ship/HCJTorpedoBlast.h"
#include "World/HCJTurret.h"
#include "World/HCJUniverse.h"
#include "World/HCJSectorLayout.h"

namespace
{
	TArray<TWeakObjectPtr<AHCJProjectile>> GLiveTorpedoes;
}

const TArray<TWeakObjectPtr<AHCJProjectile>>& AHCJProjectile::GetLiveTorpedoes()
{
	return GLiveTorpedoes;
}

bool AHCJProjectile::IsArmed() const
{
	return HCJTorpedo::IsArmed(DistanceTravelled, UHCJGameplaySettings::Get().TorpedoArmingDistance);
}

void AHCJProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GLiveTorpedoes.RemoveAllSwap([this](const TWeakObjectPtr<AHCJProjectile>& Entry) { return !Entry.IsValid() || Entry.Get() == this; });
	Super::EndPlay(EndPlayReason);
}

AHCJProjectile::AHCJProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
	CollisionComp->InitSphereRadius(20.0f);
	CollisionComp->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CollisionComp->SetGenerateOverlapEvents(false);
	CollisionComp->SetCanEverAffectNavigation(false);
	CollisionComp->CanCharacterStepUpOn = ECB_No;
	CollisionComp->OnComponentHit.AddDynamic(this, &AHCJProjectile::OnCollisionHit);
	SetRootComponent(CollisionComp);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(CollisionComp);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCanEverAffectNavigation(false);
	VisualMesh->SetCastShadow(false);
	// Bolt stretched along the flight direction (+X).
	VisualMesh->SetRelativeScale3D(FVector(1.6f, 0.3f, 0.3f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(SphereMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		VisualMesh->SetMaterial(0, ShapeMaterial.Object);
	}

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = CollisionComp;
	Movement->InitialSpeed = Speed;
	Movement->MaxSpeed = Speed;
	Movement->ProjectileGravityScale = 0.0f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->bInitialVelocityInLocalSpace = true;

	bReplicates = true;
	SetReplicatingMovement(true);
	// The flight is a straight line simulated locally; the initial heading must be precise.
	FRepMovement RepMovement = GetReplicatedMovement();
	RepMovement.RotationQuantizationLevel = ERotatorQuantization::ShortComponents;
	SetReplicatedMovement(RepMovement);

	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(2.0f);
	SetNetCullDistanceSquared(FMath::Square(HCJ::NetCullDistance));

	InitialLifeSpan = Lifetime;
}

void AHCJProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AHCJProjectile, WeaponType, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AHCJProjectile, bQuad, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AHCJProjectile, CustomSpeed, COND_InitialOnly);
	DOREPLIFETIME(AHCJProjectile, HomingTarget);
}

void AHCJProjectile::InitWeapon(EHCJWeapon InWeapon, bool bInQuad, float InDamageMultiplier)
{
	WeaponType = static_cast<uint8>(InWeapon < EHCJWeapon::Count ? InWeapon : EHCJWeapon::Blaster);
	bQuad = bInQuad;
	DamageMultiplier = FMath::Max(InDamageMultiplier, 0.0f);
}

void AHCJProjectile::InitCustom(float InSpeed, float InDamage, float InLifetime)
{
	CustomSpeed = FMath::Max(InSpeed, 100.0f);
	CustomDamage = FMath::Max(InDamage, 0.0f);
	CustomLifetime = FMath::Max(InLifetime, 0.1f);
}

const FHCJWeaponDef& AHCJProjectile::GetWeaponDef() const
{
	return UHCJGameplaySettings::Get().GetWeapon(GetWeapon());
}

void AHCJProjectile::BeginPlay()
{
	Super::BeginPlay();

	// What this projectile is: on the server from InitWeapon / InitCustom, on clients from the spawn replication.
	const FHCJWeaponDef& Def = GetWeaponDef();
	const bool bCustom = CustomSpeed > 0.0f;
	Speed = bCustom ? CustomSpeed : Def.ProjectileSpeed;
	Lifetime = bCustom ? FMath::Max(CustomLifetime, 0.1f) : Def.Lifetime;
	Damage = bCustom ? CustomDamage : Def.Damage * DamageMultiplier;
	Movement->MaxSpeed = Speed;
	FVector Direction = Movement->Velocity.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
	}
	Movement->Velocity = Direction * Speed;

	const bool bHoming = !bCustom && Def.HomingTurnRate > 0.0f;
	const bool bTorpedo = IsTorpedo();
	if (bTorpedo)
	{
		GLiveTorpedoes.AddUnique(this);
		if (!HasAuthority())
		{
			const APawn* Firer = GetInstigator();
			UE_LOG(LogHCJ, Verbose, TEXT("Torpedo: %s appeared, fired by %s%s, homing on %s"), *GetName(), *GetNameSafe(Firer),
				Firer != nullptr && Firer->IsLocallyControlled() ? TEXT(" (own ship)") : TEXT(""), *GetNameSafe(HomingTarget));
		}
	}
	if (HasAuthority())
	{
		CachedInstigatorController = GetInstigatorController();
		SetLifeSpan(Lifetime);
		if (bHoming)
		{
			// The path bends: clients need fresher positions than for a straight bolt.
			SetNetUpdateFrequency(20.0f);
			// A torpedo only ever steers for what its firer had locked; its victim is warned by its own HUD.
			HomingTarget = bTorpedo ? FindTorpedoTarget() : FindHomingTarget();
			if (bTorpedo)
			{
				UE_LOG(LogHCJ, Verbose, TEXT("Torpedo: %s fired by %s, homing on %s"), *GetName(), *GetNameSafe(GetInstigator()), *GetNameSafe(HomingTarget));
			}
			else if (const APawn* TargetPawn = Cast<APawn>(HomingTarget))
			{
				if (AHCJPlayerController* Victim = Cast<AHCJPlayerController>(TargetPawn->GetController()))
				{
					Victim->ClientMissileWarning();
				}
			}
		}
	}

	// Straight flight needs no tick; a black hole in this sector bends it, and so does homing.
	if (const AHCJUniverse* Universe = AHCJUniverse::Get(this))
	{
		SetActorTickEnabled(bHoming || bTorpedo || (Universe->IsBuilt() && Universe->HasBlackHole(Universe->GetSectorAtLocation(GetActorLocation()))));
	}

	if (GetInstigator() == nullptr && GetOwner() != nullptr)
	{
		// A turret's shot: it must not hit the turret it leaves.
		CollisionComp->IgnoreActorWhenMoving(GetOwner(), true);
	}
	const APawn* InstigatorPawn = GetInstigator();
	if (APawn* MutableInstigator = GetInstigator())
	{
		CollisionComp->IgnoreActorWhenMoving(MutableInstigator, true);
		if (UPrimitiveComponent* InstigatorRoot = Cast<UPrimitiveComponent>(MutableInstigator->GetRootComponent()))
		{
			// Prunes entries of bolts that are already gone, so the shooter's ignore list stays short.
			InstigatorRoot->CopyArrayOfMoveIgnoreActors();
			InstigatorRoot->IgnoreActorWhenMoving(this, true);
		}
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		FLinearColor BoltColor(1.0f, 0.55f, 0.1f);
		if (const AHCJPlayerState* ShooterState = InstigatorPawn ? InstigatorPawn->GetPlayerState<AHCJPlayerState>() : nullptr)
		{
			// Mostly the shooter's colour, pushed towards white so bolts read against the hulls.
			BoltColor = FMath::Lerp(ShooterState->GetPlayerColor(), FLinearColor::White, 0.35f);
		}
		if (!bCustom && (Def.Color.R > 0.0f || Def.Color.G > 0.0f || Def.Color.B > 0.0f))
		{
			// Pickup weapons have their own colour, so what is coming can be read from the bolt.
			BoltColor = Def.Color;
		}
		if (bQuad)
		{
			BoltColor = FMath::Lerp(BoltColor, FLinearColor(1.0f, 0.1f, 0.9f), 0.7f);
		}
		SetupBoltVisuals(BoltColor);
	}
}

void AHCJProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	if (Universe == nullptr || !Universe->IsBuilt() || IsActorBeingDestroyed())
	{
		return;
	}

	const bool bTorpedo = IsTorpedo();
	if (bTorpedo && !bImpactPlayed)
	{
		// Every machine counts for itself (a client's copy starts a few frames late: it arms that little later).
		DistanceTravelled += static_cast<float>(Movement->Velocity.Size()) * DeltaSeconds;
		if (bBillboard)
		{
			AHCJTorpedoBlast::FaceLocalView(VisualMesh);
		}
	}

	// Homing: the velocity turns towards the target at the weapon's turn rate, on every machine.
	const FHCJWeaponDef& Def = GetWeaponDef();
	if (CustomSpeed <= 0.0f && Def.HomingTurnRate > 0.0f)
	{
		const UWorld* World = GetWorld();
		if (HasAuthority() && World->GetTimeSeconds() >= NextHomingSearchTime)
		{
			NextHomingSearchTime = static_cast<float>(World->GetTimeSeconds()) + 0.2f;
			if (!bTorpedo)
			{
				HomingTarget = FindHomingTarget();
			}
			else if (HomingTarget != nullptr && !TorpedoKeepsTarget())
			{
				// Dodged: a torpedo that lost its target flies on straight and never finds another.
				UE_LOG(LogHCJ, Verbose, TEXT("Torpedo: %s lost %s after %.0f uu"), *GetName(), *GetNameSafe(HomingTarget), DistanceTravelled);
				if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
				{
					GameMode->NotifyTorpedoLostLock();
				}
				HomingTarget = nullptr;
			}
		}
		const AHCJShipCharacter* TargetShip = Cast<AHCJShipCharacter>(HomingTarget);
		const AActor* Target = HomingTarget;
		if (IsValid(Target) && (TargetShip == nullptr || !TargetShip->IsDead()))
		{
			const FVector Current = Movement->Velocity.GetSafeNormal();
			const FVector Wanted = Target->GetActorLocation() - GetActorLocation();
			if (!Current.IsNearlyZero() && !Wanted.IsNearlyZero())
			{
				Movement->Velocity = HCJTorpedo::TurnTowards(Current, Wanted, Def.HomingTurnRate * DeltaSeconds) * Speed;
			}
		}
	}

	const FVector Location = GetActorLocation();
	if (Universe->IsInsideEventHorizon(Location))
	{
		if (HasAuthority())
		{
			// Swallowed: gone without an impact.
			bImpactPlayed = true;
			Destroy();
		}
		else
		{
			bImpactPlayed = true;
			SetActorHiddenInGame(true);
		}
		return;
	}

	const FVector Drift = Universe->GetGravityDrift(Location);
	if (!Drift.IsNearlyZero())
	{
		// Swept: running into something on the way is an ordinary hit (OnCollisionHit).
		AddActorWorldOffset(Drift * DeltaSeconds, /*bSweep*/ true);
	}
}

AActor* AHCJProjectile::FindTorpedoTarget() const
{
	const AHCJShipCharacter* Shooter = Cast<AHCJShipCharacter>(GetInstigator());
	AHCJShipCharacter* Locked = Shooter != nullptr ? Shooter->GetLockTarget() : nullptr;
	if (!IsValid(Locked) || Locked == Shooter || Locked->IsDead() || Locked->IsCloaked())
	{
		return nullptr;
	}
	const FVector Forward = Movement->Velocity.IsNearlyZero() ? GetActorForwardVector() : Movement->Velocity.GetSafeNormal();
	return HCJTorpedo::KeepsLock(Forward, Locked->GetActorLocation() - GetActorLocation(), GetWeaponDef().HomingConeDegrees, Speed * Lifetime) ? Locked : nullptr;
}

bool AHCJProjectile::TorpedoKeepsTarget() const
{
	const AHCJShipCharacter* TargetShip = Cast<AHCJShipCharacter>(HomingTarget);
	if (!IsValid(TargetShip) || TargetShip->IsDead() || TargetShip->IsCloaked())
	{
		return false;
	}
	const FVector Forward = Movement->Velocity.IsNearlyZero() ? GetActorForwardVector() : Movement->Velocity.GetSafeNormal();
	return HCJTorpedo::KeepsLock(Forward, TargetShip->GetActorLocation() - GetActorLocation(), GetWeaponDef().HomingConeDegrees, Speed * Lifetime);
}

void AHCJProjectile::LifeSpanExpired()
{
	if (HasAuthority() && IsTorpedo() && !bDetonated && !bImpactPlayed)
	{
		ServerDetonate(GetActorLocation(), nullptr, nullptr);
		return;
	}
	Super::LifeSpanExpired();
}

void AHCJProjectile::ServerDetonate(const FVector& Location, AActor* DirectActor, const FHitResult* Hit)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || bDetonated || World == nullptr)
	{
		return;
	}
	bDetonated = true;

	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const bool bArmed = IsArmed();
	FVector TravelDirection = GetVelocity().GetSafeNormal();
	if (TravelDirection.IsNearlyZero())
	{
		TravelDirection = GetActorForwardVector();
	}
	// A little off the surface it hit, so that the line-of-sight checks do not start inside the rock.
	const FVector Origin = Hit != nullptr ? Location + FVector(Hit->ImpactNormal) * 15.0 : Location;

	int32 NumHit = 0;
	if (DirectActor != nullptr && Hit != nullptr)
	{
		UGameplayStatics::ApplyPointDamage(DirectActor, Damage * (bArmed ? 1.0f : Gameplay.TorpedoDudDamageFraction), TravelDirection, *Hit,
			CachedInstigatorController, this, UDamageType::StaticClass());
		NumHit += Cast<AHCJShipCharacter>(DirectActor) != nullptr ? 1 : 0;
	}
	AHCJUniverse* Universe = AHCJUniverse::Get(this);
	if (bArmed && Universe != nullptr && Universe->IsBuilt())
	{
		// Rock: the one it struck takes the torpedo, the others inside the blast their share of it.
		const float RockDamage = Damage * Gameplay.TorpedoRockDamageScale;
		const uint32 StruckKey = (DirectActor == Universe && Hit != nullptr) ? Universe->FindAsteroidKey(Hit->GetComponent(), Hit->Item) : MAX_uint32;
		// (The blast first: the pieces a struck rock breaks into are not there yet, so one torpedo does not take them as well.)
		Universe->ServerDamageAsteroidsInRadius(Origin, Gameplay.TorpedoBlastInnerRadius, Gameplay.TorpedoBlastRadius, Gameplay.TorpedoBlastEdgeScale, RockDamage,
			CachedInstigatorController, StruckKey);
		if (StruckKey != MAX_uint32)
		{
			Universe->ServerDamageAsteroidKey(StruckKey, RockDamage, CachedInstigatorController);
		}
	}

	if (bArmed && Gameplay.TorpedoBlastRadius > 0.0f)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HCJTorpedoBlast), false, this);
		const auto Blast = [&](AActor* Victim, float HullRadius)
		{
			const FVector VictimLocation = Victim->GetActorLocation();
			const float Distance = FMath::Max(static_cast<float>(FVector::Dist(Origin, VictimLocation)) - HullRadius, 0.0f);
			const float Scale = HCJTorpedo::BlastScale(Distance, Gameplay.TorpedoBlastInnerRadius, Gameplay.TorpedoBlastRadius, Gameplay.TorpedoBlastEdgeScale);
			if (Scale <= 0.0f)
			{
				return false;
			}
			FCollisionQueryParams VictimParams = Params;
			VictimParams.AddIgnoredActor(Victim);
			if (World->LineTraceTestByObjectType(Origin, VictimLocation, FCollisionObjectQueryParams(ECC_WorldStatic), VictimParams))
			{
				// A rock (or the station) is in the way.
				return false;
			}
			// The impact point is the blast itself: the victim takes it on the facing towards the blast.
			FHitResult BlastHit;
			BlastHit.Location = Origin;
			BlastHit.ImpactPoint = Origin;
			BlastHit.ImpactNormal = (Origin - VictimLocation).GetSafeNormal();
			BlastHit.Normal = BlastHit.ImpactNormal;
			UGameplayStatics::ApplyPointDamage(Victim, Damage * Scale, (VictimLocation - Origin).GetSafeNormal(), BlastHit,
				CachedInstigatorController, this, UDamageType::StaticClass());
			return true;
		};

		const float ShipHullRadius = FMath::Max(Gameplay.AimAssistRadius, HCJ::ShipCollisionRadius);
		// Collected first: damage can destroy ships, and that must not disturb the iteration.
		TArray<AHCJShipCharacter*, TInlineAllocator<8>> Ships;
		const double ReachSq = FMath::Square(static_cast<double>(Gameplay.TorpedoBlastRadius + ShipHullRadius));
		for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
		{
			if (*It != DirectActor && !It->IsDead() && FVector::DistSquared(It->GetActorLocation(), Origin) <= ReachSq)
			{
				Ships.Add(*It);
			}
		}
		for (AHCJShipCharacter* Ship : Ships)
		{
			NumHit += Blast(Ship, ShipHullRadius) ? 1 : 0;
		}
		for (const TWeakObjectPtr<AHCJTurret>& Entry : AHCJTurret::GetAll())
		{
			AHCJTurret* Turret = Entry.Get();
			if (Turret != nullptr && Turret != DirectActor && Turret->GetWorld() == World && !Turret->IsTurretDestroyed())
			{
				Blast(Turret, 150.0f);
			}
		}
	}

	UE_LOG(LogHCJ, Verbose, TEXT("Torpedo: %s of %s %s after %.0f uu on %s, %d ship(s) hit"), *GetName(), *GetNameSafe(GetInstigator()),
		bArmed ? TEXT("detonated") : TEXT("was a dud"), DistanceTravelled, DirectActor != nullptr ? *DirectActor->GetName() : TEXT("nothing (end of life)"), NumHit);
	if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
	{
		GameMode->NotifyTorpedoDetonated(bArmed, NumHit);
	}

	if (bArmed)
	{
		// The shockwave: every ship near the blast is pushed away from it, the firer's included.
		UHCJEffectsDirector::ServerPushShips(World, Origin, Gameplay.TorpedoShockwaveRadius, Gameplay.TorpedoShockwaveSpeed, nullptr);
	}

	// Listen-server host; clients play theirs from their own simulation or from Destroyed().
	PlayDetonationEffects(Location, Hit != nullptr ? FVector(Hit->ImpactNormal) : -TravelDirection, bArmed);
	Destroy();
}

void AHCJProjectile::PlayDetonationEffects(const FVector& Location, const FVector& Normal, bool bArmed)
{
	if (bImpactPlayed || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!bArmed)
	{
		// A dud: the knock of a bolt on a hull, nothing more.
		PlayImpactEffects(Location, Normal, /*bHitShip*/ true);
		return;
	}
	bImpactPlayed = true;
	AHCJTorpedoBlast::Play(this, Location);
}

void AHCJProjectile::SetupTorpedoVisuals(const FLinearColor& Color)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const float SizeFactor = GetWeaponDef().VisualScale;

	UMaterialInterface* Star = UHCJAssetSettings::Load<UMaterialInterface>(Settings.TorpedoMaterial, TEXT("torpedo star-burst"));
	UStaticMesh* Plane = Star != nullptr ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")) : nullptr;
	if (Star != nullptr && Plane != nullptr)
	{
		// One quad that faces the view: the engine's plane is 100 uu across.
		VisualMesh->SetStaticMesh(Plane);
		VisualMesh->EmptyOverrideMaterials();
		VisualMesh->SetUsingAbsoluteRotation(true);
		VisualMesh->SetRelativeLocation(FVector::ZeroVector);
		VisualMesh->SetRelativeScale3D(FVector(Settings.TorpedoStarSize * SizeFactor / 100.0f));
		StarMaterial = UMaterialInstanceDynamic::Create(Star, this);
		StarMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.R, Color.G, Color.B, 1.0f));
		StarMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.TorpedoBrightness);
		// Every torpedo flickers on its own beat.
		StarMaterial->SetScalarParameterValue(TEXT("Phase"), static_cast<float>(GetUniqueID() % 97));
		VisualMesh->SetMaterial(0, StarMaterial);
		bBillboard = true;
		AHCJTorpedoBlast::FaceLocalView(VisualMesh);
	}
	else
	{
		// Fallback: a glowing ball in the torpedo colour.
		VisualMesh->SetRelativeScale3D(FVector(0.9f) * SizeFactor);
		if (UMaterialInstanceDynamic* Ball = VisualMesh->CreateDynamicMaterialInstance(0))
		{
			Ball->SetVectorParameterValue(TEXT("Color"), Color);
			Ball->SetScalarParameterValue(TEXT("Roughness"), 0.2f);
		}
	}

	const FVector Start = GetActorLocation();
	UHCJAssetSettings::PlaySoundAt(this, Settings.TorpedoFireSounds.Num() > 0 ? Settings.TorpedoFireSounds : Settings.MissileFireSounds, TEXT("torpedo launch sound"),
		Start, Settings.TorpedoFireVolume, 0.04f, EHCJSoundGroup::Fire, Settings.TorpedoFirePitch);
	if (!UHCJAssetSettings::IsNearLocalView(this, Start, Settings.EffectMaxDistance))
	{
		return;
	}
	if (UNiagaraSystem* TrailSystem = UHCJAssetSettings::Load<UNiagaraSystem>(Settings.TorpedoTrailEffect, TEXT("torpedo trail")))
	{
		UNiagaraComponent* Trail = UNiagaraFunctionLibrary::SpawnSystemAttached(TrailSystem, CollisionComp, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			FVector(Settings.TorpedoTrailScale), EAttachLocation::KeepRelativeOffset, /*bAutoDestroy*/ true, ENCPoolMethod::None);
		if (Trail != nullptr && !Settings.TorpedoTrailColorParameter.IsNone())
		{
			Trail->SetVariableLinearColor(Settings.TorpedoTrailColorParameter, FLinearColor(Color.R, Color.G, Color.B, 1.0f));
		}
	}
}

AActor* AHCJProjectile::FindHomingTarget() const
{
	const FHCJWeaponDef& Def = GetWeaponDef();
	const UWorld* World = GetWorld();
	if (World == nullptr || Def.HomingTurnRate <= 0.0f)
	{
		return nullptr;
	}
	const FVector Location = GetActorLocation();
	const FVector Forward = Movement->Velocity.IsNearlyZero() ? GetActorForwardVector() : Movement->Velocity.GetSafeNormal();
	const double MinCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Def.HomingConeDegrees, 1.0f, 90.0f)));
	const double MaxDistanceSq = FMath::Square(double(Speed) * double(Lifetime));

	// The shooter's locked target, when it can be reached at all (anywhere in front of the missile).
	if (const AHCJShipCharacter* Shooter = Cast<AHCJShipCharacter>(GetInstigator()))
	{
		AHCJShipCharacter* Locked = Shooter->GetLockTarget();
		if (IsValid(Locked) && !Locked->IsDead() && !Locked->IsCloaked())
		{
			const FVector ToLocked = Locked->GetActorLocation() - Location;
			if (ToLocked.SizeSquared() <= MaxDistanceSq && FVector::DotProduct(ToLocked.GetSafeNormal(), Forward) > 0.0)
			{
				return Locked;
			}
		}
	}

	AActor* Best = nullptr;
	double BestScore = 0.0;
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		AHCJShipCharacter* Ship = *It;
		if (Ship == GetInstigator() || Ship->IsDead() || Ship->IsCloaked())
		{
			continue;
		}
		const FVector ToShip = Ship->GetActorLocation() - Location;
		const double DistanceSq = ToShip.SizeSquared();
		if (DistanceSq > MaxDistanceSq || DistanceSq < 1.0)
		{
			continue;
		}
		const double Cos = FVector::DotProduct(ToShip, Forward) / FMath::Sqrt(DistanceSq);
		if (Cos < MinCos)
		{
			continue;
		}
		// Prefer what is dead ahead and near.
		const double Score = Cos / (1.0 + FMath::Sqrt(DistanceSq) / 5000.0);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Ship;
		}
	}
	return Best;
}

void AHCJProjectile::SetupBoltVisuals(const FLinearColor& BoltColor)
{
	if (IsTorpedo())
	{
		SetupTorpedoVisuals(BoltColor);
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const bool bCustom = CustomSpeed > 0.0f;
	const EHCJWeapon Weapon = bCustom ? EHCJWeapon::Blaster : GetWeapon();
	const bool bMissile = Weapon == EHCJWeapon::Missile;
	const float SizeFactor = (bCustom ? 1.3f : GetWeaponDef().VisualScale) * (bQuad ? 1.4f : 1.0f);

	UStaticMesh* BoltMesh = bMissile ? UHCJAssetSettings::Load<UStaticMesh>(Settings.MissileMesh, TEXT("missile")) : nullptr;
	const bool bMissileMesh = BoltMesh != nullptr;
	if (BoltMesh == nullptr)
	{
		BoltMesh = UHCJAssetSettings::Load<UStaticMesh>(Settings.ProjectileMesh, TEXT("projectile"));
	}
	const FBox BoltBounds = BoltMesh != nullptr ? BoltMesh->GetBoundingBox() : FBox(ForceInit);
	const bool bUseBoltMesh = BoltMesh != nullptr && BoltBounds.IsValid && BoltBounds.GetSize().GetMax() > 0.01;
	if (bUseBoltMesh)
	{
		const FVector Size = BoltBounds.GetSize().ComponentMax(FVector(0.01));
		VisualMesh->SetStaticMesh(BoltMesh);
		VisualMesh->EmptyOverrideMaterials();
		if (bMissileMesh)
		{
			// Uniform scale to the configured length; the longest side of the mesh is laid along the flight direction.
			const double Scale = Settings.MissileLength * SizeFactor / Size.GetMax();
			const FRotator Fit = Size.X >= Size.Y && Size.X >= Size.Z ? FRotator::ZeroRotator
				: (Size.Y >= Size.Z ? FRotator(0.0, -90.0, 0.0) : FRotator(-90.0, 0.0, 0.0));
			VisualMesh->SetRelativeScale3D(FVector(Scale));
			VisualMesh->SetRelativeLocationAndRotation(-Fit.RotateVector(BoltBounds.GetCenter() * Scale), Fit);
		}
		else
		{
			// Scaled to the configured size from its measured bounds, whatever its pivot: centred on the collision sphere.
			const FVector Scale(Settings.ProjectileLength * SizeFactor / Size.X, Settings.ProjectileThickness * SizeFactor / Size.Y,
				Settings.ProjectileThickness * SizeFactor / Size.Z);
			VisualMesh->SetRelativeScale3D(Scale);
			VisualMesh->SetRelativeLocation(-(BoltBounds.GetCenter() * Scale));
		}
	}
	else
	{
		VisualMesh->SetRelativeScale3D(FVector(1.6f, 0.3f, 0.3f) * SizeFactor);
	}

	if (UMaterialInstanceDynamic* BoltMaterial = bMissileMesh ? nullptr : VisualMesh->CreateDynamicMaterialInstance(0))
	{
		if (!bUseBoltMesh)
		{
			BoltMaterial->SetVectorParameterValue(TEXT("Color"), BoltColor);
			BoltMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.2f);
		}
		else
		{
			if (!Settings.ProjectileColorParameter.IsNone())
			{
				BoltMaterial->SetVectorParameterValue(Settings.ProjectileColorParameter, BoltColor);
			}
			if (!Settings.ProjectileBrightnessParameter.IsNone())
			{
				BoltMaterial->SetScalarParameterValue(Settings.ProjectileBrightnessParameter, Settings.ProjectileBrightness);
			}
		}
	}

	// Trail and muzzle flash only for bolts that start near the local view: distant fire fights stay cheap.
	const FVector Start = GetActorLocation();
	// One sound per shot, not per pellet: the spread pellets of a shot are spawned in the same frame.
	static uint64 LastScatterSoundFrame = 0;
	TConstArrayView<FSoftObjectPath> Sounds = Settings.FireSounds;
	switch (Weapon)
	{
	case EHCJWeapon::Scatter: if (Settings.ScatterFireSounds.Num() > 0) { Sounds = Settings.ScatterFireSounds; } break;
	case EHCJWeapon::Rail: if (Settings.RailFireSounds.Num() > 0) { Sounds = Settings.RailFireSounds; } break;
	case EHCJWeapon::Missile: if (Settings.MissileFireSounds.Num() > 0) { Sounds = Settings.MissileFireSounds; } break;
	case EHCJWeapon::Pulse: if (Settings.PulseFireSounds.Num() > 0) { Sounds = Settings.PulseFireSounds; } break;
	default: break;
	}
	if (Weapon != EHCJWeapon::Scatter || LastScatterSoundFrame != GFrameCounter)
	{
		LastScatterSoundFrame = Weapon == EHCJWeapon::Scatter ? GFrameCounter : LastScatterSoundFrame;
		UHCJAssetSettings::PlaySoundAt(this, Sounds, TEXT("fire sound"), Start, Settings.FireVolume * (bQuad ? 1.25f : 1.0f),
			Settings.FirePitchVariation, EHCJSoundGroup::Fire);
	}
	if (!UHCJAssetSettings::IsNearLocalView(this, Start, Settings.EffectMaxDistance))
	{
		return;
	}

	UNiagaraSystem* TrailSystem = bMissile ? UHCJAssetSettings::Load<UNiagaraSystem>(Settings.MissileTrailEffect, TEXT("missile trail")) : nullptr;
	if (TrailSystem == nullptr)
	{
		TrailSystem = UHCJAssetSettings::Load<UNiagaraSystem>(Settings.ProjectileTrailEffect, TEXT("projectile trail"));
	}
	if (TrailSystem != nullptr)
	{
		// Not pooled: the component belongs to the bolt and dies with it.
		UNiagaraFunctionLibrary::SpawnSystemAttached(TrailSystem, CollisionComp, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			FVector(Settings.ProjectileTrailScale), EAttachLocation::KeepRelativeOffset, /*bAutoDestroy*/ true, ENCPoolMethod::None);
	}

	if (UNiagaraSystem* FlashSystem = UHCJAssetSettings::Load<UNiagaraSystem>(Settings.MuzzleFlashEffect, TEXT("muzzle flash")))
	{
		// On the shooter, so the flash stays at the muzzle of a moving ship.
		const APawn* Shooter = GetInstigator();
		USceneComponent* ShooterRoot = Shooter != nullptr ? Shooter->GetRootComponent() : nullptr;
		UNiagaraComponent* Flash = ShooterRoot != nullptr
			? UNiagaraFunctionLibrary::SpawnSystemAttached(FlashSystem, ShooterRoot, NAME_None, Start, GetActorRotation(),
				FVector(Settings.MuzzleFlashScale), EAttachLocation::KeepWorldPosition, /*bAutoDestroy*/ true, ENCPoolMethod::AutoRelease)
			: UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FlashSystem, Start, GetActorRotation(), FVector(Settings.MuzzleFlashScale),
				/*bAutoDestroy*/ true, /*bAutoActivate*/ true, ENCPoolMethod::AutoRelease);
		if (Flash != nullptr)
		{
			if (!Settings.MuzzleFlashColorParameter.IsNone())
			{
				Flash->SetVariableLinearColor(Settings.MuzzleFlashColorParameter, BoltColor);
			}
			for (const FName& Feature : Settings.MuzzleFlashDisabledFeatures)
			{
				Flash->SetVariableBool(Feature, false);
			}
		}
	}
}

void AHCJProjectile::PlayImpactEffects(const FVector& Location, const FVector& Normal, bool bHitShip)
{
	if (bImpactPlayed || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	bImpactPlayed = true;

	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	UHCJAssetSettings::PlaySoundAt(this, bHitShip ? Settings.ImpactShipSounds : Settings.ImpactRockSounds,
		bHitShip ? TEXT("impact sound on a ship") : TEXT("impact sound on a rock"), Location, Settings.ImpactVolume, 0.1f, EHCJSoundGroup::Impact);
	UNiagaraComponent* Impact = UHCJAssetSettings::SpawnEffect(this, bHitShip ? Settings.ImpactShipEffect : Settings.ImpactRockEffect,
		bHitShip ? TEXT("impact on a ship") : TEXT("impact on a rock"), Location, FRotator::ZeroRotator, Settings.ImpactScale);
	if (Impact != nullptr)
	{
		if (!Settings.ImpactNormalParameter.IsNone())
		{
			Impact->SetVariableVec3(Settings.ImpactNormalParameter, Normal);
		}
		if (!Settings.ImpactDirectionParameter.IsNone())
		{
			Impact->SetVariableVec3(Settings.ImpactDirectionParameter, GetActorForwardVector());
		}
	}
}

void AHCJProjectile::Destroyed()
{
	// The server destroyed the bolt long before it could expire, and the local simulation saw no hit (typically
	// the target was somewhere else on this machine): show the impact where the bolt is now.
	const UWorld* World = GetWorld();
	if (IsTorpedo())
	{
		// A torpedo always ends in a detonation; this machine has not shown it yet (its copy flew on, or its time was up).
		if (GetNetMode() == NM_Client && !bImpactPlayed && World != nullptr && !World->bIsTearingDown)
		{
			PlayDetonationEffects(GetActorLocation(), -GetActorForwardVector(), IsArmed());
		}
	}
	else if (GetNetMode() == NM_Client && !bImpactPlayed && World != nullptr && !World->bIsTearingDown
		&& GetGameTimeSinceCreation() < 0.8f * Lifetime)
	{
		PlayImpactEffects(GetActorLocation(), -GetActorForwardVector(), /*bHitShip*/ true);
	}

	Super::Destroyed();
}

void AHCJProjectile::OnCollisionHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor == this || (OtherActor != nullptr && OtherActor == GetInstigator()))
	{
		return;
	}

	const bool bHitShip = Cast<AHCJShipCharacter>(OtherActor) != nullptr;

	if (!HasAuthority())
	{
		// Cosmetic stop; the real outcome (damage, destruction) comes from the server.
		if (IsTorpedo())
		{
			PlayDetonationEffects(Hit.ImpactPoint, Hit.ImpactNormal, IsArmed());
		}
		else
		{
			PlayImpactEffects(Hit.ImpactPoint, Hit.ImpactNormal, bHitShip);
		}
		Movement->StopMovementImmediately();
		SetActorEnableCollision(false);
		SetActorHiddenInGame(true);
		return;
	}

	if (IsActorBeingDestroyed())
	{
		return;
	}

	if (IsTorpedo())
	{
		ServerDetonate(Hit.ImpactPoint, OtherActor, &Hit);
		return;
	}

	if (OtherActor != nullptr)
	{
		FVector TravelDirection = GetVelocity().GetSafeNormal();
		if (TravelDirection.IsNearlyZero())
		{
			TravelDirection = GetActorForwardVector();
		}
		UGameplayStatics::ApplyPointDamage(OtherActor, Damage, TravelDirection, Hit, CachedInstigatorController, this, UDamageType::StaticClass());
	}

	// Listen-server host; clients play theirs from their own simulation or from Destroyed().
	PlayImpactEffects(Hit.ImpactPoint, Hit.ImpactNormal, bHitShip);
	Destroy();
}
