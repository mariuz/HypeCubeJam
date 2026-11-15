#include "World/HCJTurret.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include "Assets/HCJAssetSettings.h"
#include "Game/HCJGameMode.h"
#include "Game/HCJGameplaySettings.h"
#include "Game/HCJPlayerController.h"
#include "HypeCubeJam.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Ship/HCJHealthComponent.h"
#include "Ship/HCJProjectile.h"
#include "Ship/HCJShipCharacter.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJStations.h"

namespace
{
	TArray<TWeakObjectPtr<AHCJTurret>> GAllTurrets;

	/** The gun may point this far below its mount plane (cosine of 100 degrees from the mount's up direction). */
	constexpr float MinUpDot = -0.17f;

	/** Rotates the unit vector From towards the unit vector To by at most MaxRadians. */
	FVector TurnTowards(const FVector& From, const FVector& To, float MaxRadians)
	{
		const float Angle = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(From, To)), -1.0f, 1.0f));
		if (Angle <= MaxRadians || Angle < 1.0e-4f)
		{
			return To;
		}
		return FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(From, To), MaxRadians / Angle).RotateVector(From).GetSafeNormal();
	}

	/** Fits Mesh into Component: uniform scale so its longest side is Length, that side along +X, centred at Offset. */
	void FitMesh(UStaticMeshComponent* Component, UStaticMesh* Mesh, float Length, const FVector& Offset)
	{
		const FBox Bounds = Mesh->GetBoundingBox();
		const FVector Size = Bounds.GetSize().ComponentMax(FVector(0.01));
		const double Scale = Length / Size.GetMax();
		const FRotator Fit = Size.X >= Size.Y && Size.X >= Size.Z ? FRotator::ZeroRotator
			: (Size.Y >= Size.Z ? FRotator(0.0, -90.0, 0.0) : FRotator(-90.0, 0.0, 0.0));
		Component->SetStaticMesh(Mesh);
		Component->EmptyOverrideMaterials();
		Component->SetRelativeScale3D(FVector(Scale));
		Component->SetRelativeLocationAndRotation(Offset - Fit.RotateVector(Bounds.GetCenter() * Scale), Fit);
	}
}

AHCJTurret::AHCJTurret()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(HCJ::TurretRadius);
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->SetGenerateOverlapEvents(false);
	Collision->SetCanEverAffectNavigation(false);
	SetRootComponent(Collision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	const auto MakeMesh = [this](const TCHAR* Name, USceneComponent* Parent, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		if (Material != nullptr)
		{
			Component->SetMaterial(0, Material);
		}
		return Component;
	};

	BaseMesh = MakeMesh(TEXT("BaseMesh"), Collision, CylinderMesh.Object, ShapeMaterial.Object);
	BaseMesh->SetRelativeScale3D(FVector(1.6f, 1.6f, 1.2f));

	AimPivot = CreateDefaultSubobject<USceneComponent>(TEXT("AimPivot"));
	AimPivot->SetupAttachment(Collision);
	AimPivot->SetUsingAbsoluteRotation(true);

	// Fallback look: a ball with a barrel along the aim direction.
	HeadMesh = MakeMesh(TEXT("HeadMesh"), AimPivot, SphereMesh.Object, ShapeMaterial.Object);
	HeadMesh->SetRelativeScale3D(FVector(2.0f));
	GunMesh = MakeMesh(TEXT("GunMesh"), AimPivot, CylinderMesh.Object, ShapeMaterial.Object);
	GunMesh->SetRelativeLocationAndRotation(FVector(190.0f, 0.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f));
	GunMesh->SetRelativeScale3D(FVector(0.45f, 0.45f, 2.6f));

	HealthComponent = CreateDefaultSubobject<UHCJHealthComponent>(TEXT("HealthComponent"));

	bReplicates = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(2.0f);
	SetNetCullDistanceSquared(FMath::Square(HCJ::NetCullDistance));
}

const TArray<TWeakObjectPtr<AHCJTurret>>& AHCJTurret::GetAll()
{
	return GAllTurrets;
}

void AHCJTurret::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHCJTurret, bDestroyed);
	DOREPLIFETIME(AHCJTurret, AimDirection);
	DOREPLIFETIME(AHCJTurret, MountUp);
}

void AHCJTurret::ServerInit(const FVector& InMountUp)
{
	MountUp = InMountUp.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	AimDirection = MountUp;
}

void AHCJTurret::BeginPlay()
{
	// Same values on every machine, before the component initialises its health.
	HealthComponent->SetMaxValues(UHCJGameplaySettings::Get().TurretHealth, 0.0f);

	Super::BeginPlay();

	GAllTurrets.RemoveAll([](const TWeakObjectPtr<AHCJTurret>& Entry) { return !Entry.IsValid(); });
	GAllTurrets.Add(this);

	if (HasAuthority())
	{
		HealthComponent->OnDepleted.AddUObject(this, &AHCJTurret::HandleHealthDepleted);
		Random.Initialize(HCJ::MixSeed(static_cast<int32>(GetUniqueID()), static_cast<int32>(FPlatformTime::Cycles64())));
		// Spread the think ticks of the turrets.
		NextThinkTime = static_cast<float>(GetWorld()->GetTimeSeconds()) + Random.GetFraction() * 0.25f;
	}

	ShownAim = FVector(AimDirection).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	if (GetNetMode() != NM_DedicatedServer)
	{
		SetupVisuals();
	}
	else
	{
		BaseMesh->SetVisibility(false);
		HeadMesh->SetVisibility(false);
		GunMesh->SetVisibility(false);
	}
	// A turret that arrives destroyed (late join) shows that without an explosion.
	bShownDestroyed = bDestroyed;
	ApplyDestroyedState(/*bWithEffects*/ false);
}

void AHCJTurret::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GAllTurrets.Remove(this);
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void AHCJTurret::SetupVisuals()
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const FVector Up = FVector(MountUp).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

	// The column stands on the station side of the turret, along the mount direction.
	BaseMesh->SetWorldRotation(FRotationMatrix::MakeFromZ(Up).Rotator());
	BaseMesh->SetRelativeLocation(-Up * 0.5f * HCJ::TurretRadius);

	if (UStaticMesh* Head = UHCJAssetSettings::Load<UStaticMesh>(Settings.TurretHeadMesh, TEXT("turret head")))
	{
		FitMesh(HeadMesh, Head, Settings.TurretHeadSize, FVector::ZeroVector);
	}
	if (UStaticMesh* Gun = UHCJAssetSettings::Load<UStaticMesh>(Settings.TurretGunMesh, TEXT("turret gun")))
	{
		FitMesh(GunMesh, Gun, Settings.TurretGunLength, FVector(0.35f * Settings.TurretGunLength, 0.0f, 0.0f));
	}
	bVisualsReady = true;
}

void AHCJTurret::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const FVector Up = FVector(MountUp).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

	if (HasAuthority() && !bDestroyed)
	{
		const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
		const float Now = static_cast<float>(World->GetTimeSeconds());
		if (Now >= NextThinkTime)
		{
			NextThinkTime = Now + 0.25f;
			ServerThink();
		}

		// Turn towards the lead point of the target, or back to rest.
		FVector Wanted = Up;
		bool bOnTarget = false;
		const AHCJShipCharacter* Ship = Target.Get();
		const FVector Current = FVector(AimDirection).GetSafeNormal(UE_SMALL_NUMBER, Up);
		if (IsValid(Ship) && !Ship->IsDead())
		{
			FVector AimPoint;
			HCJ::ComputeLeadAim(GetActorLocation() + Current * HCJ::TurretMuzzleOffset, Ship->GetActorLocation(), Ship->GetVelocity(), Gameplay.TurretProjectileSpeed, AimPoint);
			const FVector ToAim = (AimPoint - GetActorLocation()).GetSafeNormal();
			if (!ToAim.IsNearlyZero() && FVector::DotProduct(ToAim, Up) >= MinUpDot)
			{
				Wanted = ToAim;
				bOnTarget = true;
			}
		}
		const FVector NewAim = TurnTowards(Current, Wanted, FMath::DegreesToRadians(Gameplay.TurretTurnRate) * DeltaSeconds);
		AimDirection = NewAim;

		if (bOnTarget && Now >= NextFireTime
			&& FVector::DotProduct(NewAim, Wanted) >= FMath::Cos(FMath::DegreesToRadians(Gameplay.TurretFireConeDegrees)))
		{
			NextFireTime = Now + FMath::Max(Gameplay.TurretFireInterval, 0.05f);
			ServerFire();
		}
	}

	if (bVisualsReady && !bDestroyed)
	{
		// Eased, so the 10 Hz replication of the aim does not show as steps.
		const FVector Goal = FVector(AimDirection).GetSafeNormal(UE_SMALL_NUMBER, Up);
		ShownAim = TurnTowards(ShownAim.GetSafeNormal(UE_SMALL_NUMBER, Up), Goal, FMath::DegreesToRadians(240.0f) * DeltaSeconds);
		AimPivot->SetWorldRotation(FRotationMatrix::MakeFromXZ(ShownAim, Up).Rotator());
	}
}

void AHCJTurret::ServerThink()
{
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	UWorld* World = GetWorld();
	const FVector Location = GetActorLocation();
	const FVector Up = FVector(MountUp).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HCJTurretSight), /*bTraceComplex*/ false, this);
	AHCJShipCharacter* Best = nullptr;
	double BestDistSq = FMath::Square(static_cast<double>(Gameplay.TurretRange));
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		AHCJShipCharacter* Ship = *It;
		if (Ship->IsDead() || Ship->IsCloakHidden() || Ship->IsSpawnProtected())
		{
			continue;
		}
		const FVector ToShip = Ship->GetActorLocation() - Location;
		const double DistSq = ToShip.SizeSquared();
		if (DistSq >= BestDistSq || FVector::DotProduct(ToShip.GetSafeNormal(), Up) < MinUpDot)
		{
			continue;
		}
		// Rocks and the station itself block the view (the trace starts just outside the turret, on its outer side).
		if (World->LineTraceTestByObjectType(Location + Up * (HCJ::TurretRadius + 20.0f), Ship->GetActorLocation(),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			continue;
		}
		BestDistSq = DistSq;
		Best = Ship;
	}
	Target = Best;
}

void AHCJTurret::ServerFire()
{
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	UWorld* World = GetWorld();
	const FVector Aim = FVector(AimDirection).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float Spread = FMath::DegreesToRadians(Gameplay.TurretSpreadDegrees);
	const FVector Direction = Spread > 0.0f ? Random.VRandCone(Aim, Spread) : Aim;
	const FTransform SpawnTransform(Direction.Rotation(), GetActorLocation() + Aim * HCJ::TurretMuzzleOffset);

	AHCJProjectile* Projectile = World->SpawnActorDeferred<AHCJProjectile>(AHCJProjectile::StaticClass(), SpawnTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile != nullptr)
	{
		const float Speed = FMath::Max(Gameplay.TurretProjectileSpeed, 1000.0f);
		Projectile->InitCustom(Speed, Gameplay.TurretDamage, 1.3f * Gameplay.TurretRange / Speed);
		Projectile->FinishSpawning(SpawnTransform);
		if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
		{
			GameMode->NotifyTurretShot();
		}
	}
}

float AHCJTurret::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || bDestroyed || (DamageCauser != nullptr && Cast<AHCJTurret>(DamageCauser->GetOwner()) != nullptr))
	{
		return 0.0f;
	}
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (ActualDamage <= 0.0f)
	{
		return 0.0f;
	}
	// (A phaser beam - its causer is the ship - is many small steps and sends its own cue a few times a second.)
	const bool bDestroys = ActualDamage >= HealthComponent->GetHealth();
	if (AHCJPlayerController* Shooter = Cast<AHCJPlayerController>(EventInstigator); Shooter != nullptr && (bDestroys || Cast<APawn>(DamageCauser) == nullptr))
	{
		Shooter->ClientHitConfirm(bDestroys);
	}
	return HealthComponent->ApplyDamage(ActualDamage, EventInstigator, DamageCauser);
}

void AHCJTurret::HandleHealthDepleted(UHCJHealthComponent* Component, AController* Killer, AActor* Causer)
{
	if (bDestroyed)
	{
		return;
	}
	bDestroyed = true;
	Target.Reset();
	ForceNetUpdate();
	OnRep_Destroyed();

	UWorld* World = GetWorld();
	if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
	{
		GameMode->NotifyTurretDestroyed(Killer);
	}
	World->GetTimerManager().SetTimer(RespawnTimer, this, &AHCJTurret::ServerRespawn, FMath::Max(UHCJGameplaySettings::Get().TurretRespawnSeconds, 1.0f), false);
}

void AHCJTurret::ServerRespawn()
{
	if (!HasAuthority())
	{
		return;
	}
	HealthComponent->ResetToFull();
	bDestroyed = false;
	AimDirection = MountUp;
	ForceNetUpdate();
	OnRep_Destroyed();
}

void AHCJTurret::OnRep_Destroyed()
{
	ApplyDestroyedState(/*bWithEffects*/ bDestroyed != bShownDestroyed);
	bShownDestroyed = bDestroyed;
}

void AHCJTurret::ApplyDestroyedState(bool bWithEffects)
{
	Collision->SetCollisionEnabled(bDestroyed ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// The column stays as a stump; head and gun are gone until the turret is rebuilt.
	HeadMesh->SetVisibility(!bDestroyed);
	GunMesh->SetVisibility(!bDestroyed);
	if (bWithEffects && bDestroyed)
	{
		const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
		UHCJAssetSettings::SpawnEffect(this, Settings.ExplosionEffect, TEXT("turret explosion"), GetActorLocation(), GetActorRotation(), Settings.ExplosionScale);
		UHCJAssetSettings::PlaySoundAt(this, Settings.ExplosionSounds, TEXT("turret explosion sound"), GetActorLocation(), Settings.ExplosionVolume, 0.1f, EHCJSoundGroup::Explosion);
	}
}
