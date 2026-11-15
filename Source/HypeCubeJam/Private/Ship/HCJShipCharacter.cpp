#include "Ship/HCJShipCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "UObject/ConstructorHelpers.h"

#include "Assets/HCJAssetSettings.h"
#include "Effects/HCJEffectsDirector.h"
#include "Game/HCJGameMode.h"
#include "Game/HCJGameplaySettings.h"
#include "Game/HCJPlayerController.h"
#include "Game/HCJPlayerState.h"
#include "HypeCubeJam.h"
#include "Ship/HCJHealthComponent.h"
#include "Ship/HCJProjectile.h"
#include "Ship/HCJShipMovementComponent.h"
#include "Ship/HCJTorpedo.h"
#include "Game/HCJUserSettings.h"
#include "Ship/HCJTorpedoBlast.h"
#include "World/HCJPortal.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJTurret.h"
#include "World/HCJUniverse.h"

namespace
{
	/** Camera height above the ship in third person, so the hull does not cover the crosshair. */
	const FVector ThirdPersonSocketOffset(0.0f, 0.0f, 240.0f);
	const FLinearColor UnownedHullColor(0.35f, 0.35f, 0.38f);

	/** Degrees of bank per degree per second of turn, and the most a hull banks. */
	constexpr float BankPerYawRate = 0.3f;
	constexpr float MaxBankDegrees = 28.0f;

	/** A ship counts as boosting (for its look only) above this fraction of its cruise speed. */
	constexpr float BoostLookSpeedFraction = 1.15f;

	/** Seconds between two shield hit events of one ship (a scatter blast or a pulse stream is one flare, not ten). */
	constexpr float ShieldHitInterval = 0.08f;

	/** Hull points a ship loses when it runs into something at warp. */
	constexpr float WarpImpactDamage = 22.0f;
	/** The colour of warp and tractor effects. */
	const FLinearColor WarpColor(0.55f, 0.8f, 1.0f, 1.0f);
	const FLinearColor TractorColor(0.45f, 0.75f, 1.0f, 1.0f);

	/** "The own ship" for looks: the pawn of the first local player (a bot on a listen host is locally controlled too). */
	bool IsLocalPlayersShip(const AHCJShipCharacter& Ship)
	{
		const APlayerController* Pilot = Cast<APlayerController>(Ship.GetController());
		return Pilot != nullptr && Pilot->IsLocalController();
	}
}

AHCJShipCharacter::AHCJShipCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UHCJShipMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(HCJ::ShipCollisionRadius, HCJ::ShipCollisionRadius);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);

	bUseControllerRotationPitch = true;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;
	// Aim and view originate at the ship centre.
	BaseEyeHeight = 0.0f;

	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SetNetCullDistanceSquared(FMath::Square(HCJ::NetCullDistance));

	HealthComponent = CreateDefaultSubobject<UHCJHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->SetFacingCount(HCJShield::NumFacings);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 24.0f;
	CameraBoom->TargetArmLength = ThirdPersonArmLength;
	CameraBoom->SocketOffset = ThirdPersonSocketOffset;
	// A little give in the chase camera: the ship leads, the view follows.
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 30.0f;
	CameraBoom->CameraLagMaxDistance = 75.0f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 28.0f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(95.0f);

	HitSphere = CreateDefaultSubobject<USphereComponent>(TEXT("HitSphere"));
	HitSphere->SetupAttachment(GetCapsuleComponent());
	HitSphere->InitSphereRadius(HCJ::ShipCollisionRadius);
	HitSphere->SetCollisionObjectType(ECC_Pawn);
	HitSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	HitSphere->SetGenerateOverlapEvents(false);
	HitSphere->SetCanEverAffectNavigation(false);
	HitSphere->CanCharacterStepUpOn = ECB_No;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeaconSphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BeaconMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeaconMesh"));
	BeaconMesh->SetupAttachment(GetCapsuleComponent());
	BeaconMesh->SetStaticMesh(BeaconSphere.Object);
	BeaconMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeaconMesh->SetGenerateOverlapEvents(false);
	BeaconMesh->SetCanEverAffectNavigation(false);
	BeaconMesh->SetCastShadow(false);
	BeaconMesh->SetVisibility(false);

	// Attached to the visual root (not the hull, which banks), so network smoothing moves it with the ship.
	ShieldBubble = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldBubble"));
	ShieldBubble->SetupAttachment(GetMesh() != nullptr ? static_cast<USceneComponent*>(GetMesh()) : GetCapsuleComponent());
	ShieldBubble->SetStaticMesh(BeaconSphere.Object);
	ShieldBubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShieldBubble->SetGenerateOverlapEvents(false);
	ShieldBubble->SetCanEverAffectNavigation(false);
	ShieldBubble->SetCastShadow(false);
	ShieldBubble->SetReceivesDecals(false);
	ShieldBubble->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	USkeletalMeshComponent* VisualRoot = GetMesh();
	if (VisualRoot != nullptr)
	{
		VisualRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		VisualRoot->SetGenerateOverlapEvents(false);
		VisualRoot->SetCanEverAffectNavigation(false);
	}

	auto MakeBodyMesh = [this, VisualRoot](const TCHAR* Name, UStaticMesh* ShapeMesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMeshComponent* Body = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Body->SetupAttachment(VisualRoot != nullptr ? static_cast<USceneComponent*>(VisualRoot) : GetCapsuleComponent());
		Body->SetStaticMesh(ShapeMesh);
		Body->SetRelativeLocationAndRotation(Location, Rotation);
		Body->SetRelativeScale3D(Scale);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetGenerateOverlapEvents(false);
		Body->SetCanEverAffectNavigation(false);
		if (ShapeMaterial.Succeeded())
		{
			Body->SetMaterial(0, ShapeMaterial.Object);
		}
		return Body;
	};

	// The basic cone and cylinder have their axis along +Z; pitching by -90 degrees lays it along +X (forward).
	const FRotator AxisToForward(-90.0f, 0.0f, 0.0f);
	HullMesh = MakeBodyMesh(TEXT("HullMesh"), ConeMesh.Object, FVector(10.0f, 0.0f, 0.0f), AxisToForward, FVector(0.8f, 0.8f, 1.7f));
	WingMesh = MakeBodyMesh(TEXT("WingMesh"), CubeMesh.Object, FVector(-35.0f, 0.0f, 0.0f), FRotator::ZeroRotator, FVector(0.55f, 2.2f, 0.07f));
	EngineMesh = MakeBodyMesh(TEXT("EngineMesh"), CylinderMesh.Object, FVector(-85.0f, 0.0f, 0.0f), AxisToForward, FVector(0.45f, 0.45f, 0.4f));

	ProjectileClass = AHCJProjectile::StaticClass();
}

void AHCJShipCharacter::BeginPlay()
{
	if (HealthComponent != nullptr)
	{
		// Before the component begins play; config of the game, so the same numbers on every machine.
		// (A client already has the replicated type here; the server stamps it right after the spawn: ServerInitShipType.)
		const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
		const FHCJShipTypeDef& TypeDef = GetTypeDef();
		HealthComponent->SetMaxValues(TypeDef.MaxHull, TypeDef.ShieldFacing * HCJShield::NumFacings);
		HealthComponent->SetShieldRegen(Gameplay.ShieldRegenDelay, TypeDef.ShieldRegenRate);
#if !UE_BUILD_SHIPPING
		// Measurement aid for headless bot matches (see the development notes): the single shield of the builds
		// before the facings (50 points, 15 per second after 4 s), to compare kill rates with the same binary.
		static const bool bSingleShield = FParse::Param(FCommandLine::Get(), TEXT("HCJSingleShield"));
		if (bSingleShield)
		{
			HealthComponent->SetFacingCount(1);
			HealthComponent->SetMaxValues(HealthComponent->GetMaxHealth(), 50.0f);
			HealthComponent->SetShieldRegen(4.0f, 15.0f);
		}
#endif
		static bool bLoggedShieldRules = false;
		if (!bLoggedShieldRules)
		{
			bLoggedShieldRules = true;
			UE_LOG(LogHCJ, Log, TEXT("Shields: %d facing(s) of %.0f points, %.1f points per second after %.1f s without a hit on the facing"),
				HealthComponent->GetNumFacings(), HealthComponent->GetFacingMax(), HealthComponent->GetShieldRegenRate(), HealthComponent->GetShieldRegenDelay());
		}
	}

	Super::BeginPlay();

	if (HealthComponent != nullptr)
	{
		if (HasAuthority())
		{
			HealthComponent->OnDepleted.AddUObject(this, &AHCJShipCharacter::HandleHealthDepleted);
			// A new ship has a fresh stock of torpedoes.
			const UHCJGameplaySettings& Rules = UHCJGameplaySettings::Get();
			FHCJTorpedoBay Bay;
			Bay.Reset(HCJTorpedo::IsEnabled() ? Rules.GetWeapon(EHCJWeapon::Torpedo).Ammo : 0, Rules.TorpedoMaxStock);
			TorpedoCount = static_cast<uint8>(Bay.Count);
			TorpedoReadyTime = 0.0f;
			// A new ship holds the phaser (the blaster with -HCJNoPhasers) and a full bank.
			BaseWeaponType = static_cast<uint8>(HCJPhaser::IsEnabled() ? EHCJWeapon::Phaser : EHCJWeapon::Blaster);
			WeaponType = BaseWeaponType;
			PhaserBank = FHCJPhaserBank();
			PhaserChargeRep = HCJPhaser::PackCharge(PhaserBank.Charge, PhaserBank.bLocked);
		}
		for (int32 Facing = 0; Facing < HCJShield::NumFacings; ++Facing)
		{
			SeenFacingShield[Facing] = HealthComponent->GetFacingShield(Facing);
		}
		HealthComponent->OnHealthChanged.AddUObject(this, &AHCJShipCharacter::HandleHealthChanged);
	}
	// Aim assist: the same radius on every machine (it is a config value of the game, not of the match).
	const float AssistRadius = UHCJGameplaySettings::Get().AimAssistRadius;
	if (HitSphere != nullptr)
	{
		if (AssistRadius > HCJ::ShipCollisionRadius)
		{
			HitSphere->SetSphereRadius(AssistRadius);
		}
		else
		{
			HitSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	WeaponRandom.Initialize(HCJ::MixSeed(static_cast<int32>(GetUniqueID()), static_cast<int32>(FPlatformTime::Cycles64())));

	if (GetNetMode() != NM_DedicatedServer && HullMesh != nullptr)
	{
		SetupShipVisuals();
	}

	ApplyShipType();
	UpdateShipColor();
	UpdateCameraMode();
	// A ship that arrives with power-ups already running (late join, coming into range).
	ApplyPowerupState();
	OnRep_TractorHolder();
	WarpPhaseLook = WarpStateRep;

	// A late joiner can receive a ship that is already dead (it lingers for DeathLingerSeconds): no explosion for it.
	if (bIsDead)
	{
		bDeathEffectsPlayed = true;
		ApplyDeadState();
	}
}

void AHCJShipCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (GetNetMode() != NM_DedicatedServer)
	{
		TickShipEffects();
	}

	if (!HasAuthority() || bIsDead)
	{
		return;
	}

	bSpawnShield = IsSpawnProtected();
	ServerApplyPendingTeleport();
	ServerTickPowerups();
	ServerTickSystems(DeltaSeconds);
	if (bIsDead)
	{
		return;
	}
	ServerTickPhaser(DeltaSeconds);
	ServerTickWeapon();
	ServerTickBounds(DeltaSeconds);
}

void AHCJShipCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHCJShipCharacter, bIsDead);
	DOREPLIFETIME(AHCJShipCharacter, bSpawnShield);
	DOREPLIFETIME(AHCJShipCharacter, WeaponType);
	DOREPLIFETIME(AHCJShipCharacter, PowerupFlags);
	DOREPLIFETIME_CONDITION(AHCJShipCharacter, Ammo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AHCJShipCharacter, PowerupEndTime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AHCJShipCharacter, TorpedoCount, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AHCJShipCharacter, TorpedoReadyTime, COND_OwnerOnly);
	DOREPLIFETIME(AHCJShipCharacter, PhaserBeam);
	DOREPLIFETIME(AHCJShipCharacter, PhaserTarget);
	DOREPLIFETIME_CONDITION(AHCJShipCharacter, PhaserChargeRep, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AHCJShipCharacter, BaseWeaponType, COND_OwnerOnly);
	DOREPLIFETIME(AHCJShipCharacter, ShipType);
	DOREPLIFETIME(AHCJShipCharacter, ShipVariant);
	DOREPLIFETIME(AHCJShipCharacter, ShipSkin);
	DOREPLIFETIME(AHCJShipCharacter, WarpStateRep);
	DOREPLIFETIME_CONDITION(AHCJShipCharacter, WarpCancelCount, COND_OwnerOnly);
	DOREPLIFETIME(AHCJShipCharacter, TractorTarget);
	DOREPLIFETIME(AHCJShipCharacter, TractorHolder);
	DOREPLIFETIME_CONDITION(AHCJShipCharacter, CloakEnergyRep, COND_OwnerOnly);
}

EHCJWeapon AHCJShipCharacter::GetFireWeapon() const
{
	const EHCJWeapon Held = GetWeapon();
	return (Held == EHCJWeapon::Phaser && IsPhaserLocked()) ? EHCJWeapon::Blaster : Held;
}

const FHCJWeaponDef& AHCJShipCharacter::GetFireWeaponDef() const
{
	return UHCJGameplaySettings::Get().GetWeapon(GetFireWeapon());
}

float AHCJShipCharacter::GetPhaserCharge() const
{
	if (HasAuthority())
	{
		return PhaserBank.Charge;
	}
	float Charge = 0.0f;
	bool bLocked = false;
	HCJPhaser::UnpackCharge(PhaserChargeRep, Charge, bLocked);
	return Charge;
}

bool AHCJShipCharacter::IsPhaserLocked() const
{
	return HasAuthority() ? PhaserBank.bLocked : (PhaserChargeRep & 0x80) != 0;
}

void AHCJShipCharacter::ToggleBaseWeapon()
{
	if (HasAuthority())
	{
		ServerToggleBaseWeapon_Implementation();
	}
	else if (IsLocallyControlled())
	{
		ServerToggleBaseWeapon();
	}
}

void AHCJShipCharacter::ServerToggleBaseWeapon_Implementation()
{
	if (bIsDead)
	{
		return;
	}
	FHCJWeaponState State;
	State.Weapon = GetWeapon();
	State.Ammo = Ammo;
	State.BaseWeapon = GetBaseWeapon();
	State.ToggleBase(HCJPhaser::IsEnabled());
	BaseWeaponType = static_cast<uint8>(State.BaseWeapon);
	WeaponType = static_cast<uint8>(State.Weapon);
	Ammo = State.Ammo;
	NextFireTime = FMath::Min(NextFireTime, static_cast<float>(GetWorld()->GetTimeSeconds()) + 0.25f);
	ForceNetUpdate();
}

void AHCJShipCharacter::ServerApplyImpulse(const FVector& VelocityChange)
{
	UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	if (!HasAuthority() || bIsDead || ShipMovement == nullptr || VelocityChange.IsNearlyZero())
	{
		return;
	}
	// A plain change of velocity inside the movement simulation: the server's moves carry it on, the owner is corrected once.
	ShipMovement->AddImpulse(VelocityChange, /*bVelocityChange*/ true);
}

void AHCJShipCharacter::ServerTickPhaser(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const FHCJShipTypeDef& TypeDef = GetTypeDef();
	const bool bWantsBeam = bWantsToFire && GetWeapon() == EHCJWeapon::Phaser && GetController() != nullptr && !ServerWeaponsBlocked();
	// (The table's recharge time is for the configured 5 s; a changed config scales every type alike.)
	const bool bBeam = PhaserBank.Step(bWantsBeam, DeltaSeconds, Gameplay.PhaserDrainSeconds, Gameplay.PhaserRechargeDelay,
		Gameplay.PhaserRechargeSeconds * (TypeDef.PhaserRechargeSeconds / 5.0f), Gameplay.PhaserRelockLevel);
	PhaserChargeRep = HCJPhaser::PackCharge(PhaserBank.Charge, PhaserBank.bLocked);
	if (!bBeam)
	{
		if (PhaserBeam != 0)
		{
			PhaserBeam = 0;
			PhaserTarget = nullptr;
			PhaserDamageSeconds = 0.0f;
			OnRep_PhaserBeam(1);
		}
		return;
	}

	SpawnProtectionEndTime = 0.0f;

	const FHCJWeaponDef& Def = Gameplay.GetWeapon(EHCJWeapon::Phaser);
	const float Range = FMath::Max(Def.BeamRange, 100.0f);
	const FVector Start = GetMuzzleLocation();
	const FVector Aim = GetAimRotation().Vector();
	// What bolts hit: rocks, stations, turrets and the hit sphere of ships (the trace channel of a projectile).
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(HCJPhaserBeam), false, this);
	FHitResult Hit;
	bool bHit = World->LineTraceSingleByChannel(Hit, Start, Start + Aim * Range, ECC_WorldDynamic, Params);

	if (!bHit || Cast<AHCJShipCharacter>(Hit.GetActor()) == nullptr)
	{
		// Aim assist: the beam bends to a hostile ship close to the aim line, when nothing stands in between.
		TArray<AHCJShipCharacter*, TInlineAllocator<16>> Ships;
		TArray<FVector, TInlineAllocator<16>> Locations;
		int32 LockedIndex = INDEX_NONE;
		for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
		{
			AHCJShipCharacter* Other = *It;
			if (Other == this || Other->IsDead() || Other->IsCloakHidden() || FVector::DistSquared(Other->GetActorLocation(), Start) > FMath::Square(static_cast<double>(Range) + 200.0))
			{
				continue;
			}
			if (Other == LockTarget.Get())
			{
				LockedIndex = Ships.Num();
			}
			Ships.Add(Other);
			Locations.Add(Other->GetActorLocation());
		}
		const int32 Pick = HCJPhaser::PickAssistTarget(Start, Aim, Locations, LockedIndex, Range, Gameplay.PhaserAssistDegrees, Gameplay.PhaserLockAssistDegrees);
		if (Pick != INDEX_NONE)
		{
			FHitResult AssistHit;
			const FVector ToShip = (Locations[Pick] - Start).GetSafeNormal();
			if (World->LineTraceSingleByChannel(AssistHit, Start, Start + ToShip * Range, ECC_WorldDynamic, Params) && AssistHit.GetActor() == Ships[Pick])
			{
				Hit = AssistHit;
				bHit = true;
			}
		}
	}

	const FVector End = bHit ? FVector(Hit.ImpactPoint) : Start + Aim * Range;
	const float Length = FMath::Min(static_cast<float>(FVector::Dist(Start, End)), Range);
	AActor* HitActor = bHit ? Hit.GetActor() : nullptr;
	const uint8 NewBeam = bHit ? static_cast<uint8>(FMath::Min<int32>(HCJPhaser::PackLength(Length, Range), 254)) : 255;
	AActor* NewTarget = (Cast<AHCJShipCharacter>(HitActor) != nullptr || Cast<AHCJTurret>(HitActor) != nullptr) ? HitActor : nullptr;
	const bool bStarted = PhaserBeam == 0;
	PhaserBeam = NewBeam;
	PhaserTarget = NewTarget;
	if (bStarted)
	{
		PhaserDamageSeconds = 0.0f;
		OnRep_PhaserBeam(0);
		ForceNetUpdate();
	}

	AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>();
	if (GameMode != nullptr)
	{
		GameMode->NotifyPhaserBeam(DeltaSeconds, Cast<AHCJShipCharacter>(HitActor) != nullptr);
	}

	// Damage in steps, so that a held beam is ten damage events a second and not one per frame.
	PhaserDamageSeconds += DeltaSeconds;
	if (PhaserDamageSeconds < FMath::Max(Def.FireInterval, 0.02f))
	{
		return;
	}
	const float Seconds = FMath::Min(PhaserDamageSeconds, 0.5f);
	PhaserDamageSeconds = 0.0f;
	if (HitActor == nullptr)
	{
		return;
	}
	const float Damage = Def.Damage * Seconds * HCJPhaser::RangeScale(Length, Range, Gameplay.PhaserFullDamageFraction, Gameplay.PhaserEdgeScale)
		* (HasPowerup(EHCJPowerup::Quad) ? Gameplay.QuadDamageMultiplier : 1.0f) * TypeDef.PhaserDamageScale;
	if (AHCJUniverse* Universe = Cast<AHCJUniverse>(HitActor))
	{
		Universe->ServerDamageAsteroid(Hit.GetComponent(), Hit.Item, Damage * Gameplay.PhaserRockDamageScale, GetController());
		return;
	}
	const float Applied = UGameplayStatics::ApplyPointDamage(HitActor, Damage, Aim, Hit, GetController(), this, UDamageType::StaticClass());
	const float Now = static_cast<float>(World->GetTimeSeconds());
	if (Applied > 0.0f && Now >= NextPhaserHitConfirmTime)
	{
		// The shooter's hit cue: a few a second while the beam is on something, not one per damage step.
		NextPhaserHitConfirmTime = Now + 0.3f;
		if (AHCJPlayerController* Shooter = Cast<AHCJPlayerController>(GetController()))
		{
			Shooter->ClientHitConfirm(false);
		}
	}
}

void AHCJShipCharacter::OnRep_PhaserBeam(uint8 OldBeam)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const bool bOn = PhaserBeam != 0;
	if (bOn != (OldBeam != 0))
	{
		UE_LOG(LogHCJ, Verbose, TEXT("Phaser: beam of %s%s %s"), *GetName(), IsPlayerControlled() && IsLocallyControlled() ? TEXT(" (own ship)") : TEXT(""), bOn ? TEXT("on") : TEXT("off"));
		PlayPhaserCue(bOn);
	}
}

bool AHCJShipCharacter::GetPhaserBeamSegment(FVector& OutStart, FVector& OutEnd, bool& bOutHit) const
{
	if (PhaserBeam == 0 || bIsDead)
	{
		return false;
	}
	// The ship as this machine draws it (the smoothed visual root of a ship flown elsewhere), so that the beam does not jitter.
	const bool bOwnAim = GetController() != nullptr;
	const USceneComponent* VisualRoot = GetMesh();
	const FVector Origin = (bOwnAim || VisualRoot == nullptr) ? GetActorLocation() : VisualRoot->GetComponentLocation();
	const FVector Forward = bOwnAim ? GetAimRotation().Vector() : GetActorForwardVector();
	const float Range = FMath::Max(UHCJGameplaySettings::Get().GetWeapon(EHCJWeapon::Phaser).BeamRange, 100.0f);
	OutStart = Origin + Forward * MuzzleOffset;
	if (const AActor* Target = PhaserTarget; IsValid(Target))
	{
		const ACharacter* TargetCharacter = Cast<ACharacter>(Target);
		const FVector TargetLocation = (TargetCharacter != nullptr && TargetCharacter->GetMesh() != nullptr && TargetCharacter->GetController() == nullptr)
			? TargetCharacter->GetMesh()->GetComponentLocation() : Target->GetActorLocation();
		const FVector To = TargetLocation - OutStart;
		const double Distance = To.Size();
		// It ends on the hull's shield, not in the middle of the ship.
		const double Surface = TargetCharacter != nullptr ? FMath::Max(UHCJGameplaySettings::Get().AimAssistRadius, HCJ::ShipCollisionRadius) : 120.0f;
		OutEnd = Distance > Surface + 1.0 ? OutStart + To * ((Distance - Surface) / Distance) : TargetLocation;
		bOutHit = true;
		return true;
	}
	OutEnd = OutStart + Forward * HCJPhaser::UnpackLength(PhaserBeam, Range);
	bOutHit = PhaserBeam < 255;
	return true;
}


void AHCJShipCharacter::OnRep_TorpedoCount(uint8 OldCount)
{
	UE_LOG(LogHCJ, Verbose, TEXT("Torpedo: own stock %d -> %d, next one ready in %.1f s"), OldCount, TorpedoCount, FMath::Max(TorpedoReadyTime - GetServerTime(), 0.0f));
}

bool AHCJShipCharacter::IsTorpedoReady() const
{
	return !bIsDead && TorpedoCount > 0 && GetServerTime() >= TorpedoReadyTime;
}

float AHCJShipCharacter::GetTorpedoReloadFraction() const
{
	FHCJTorpedoBay Bay;
	Bay.Count = TorpedoCount;
	Bay.ReadyTime = TorpedoReadyTime;
	return Bay.GetReloadFraction(GetServerTime(), UHCJGameplaySettings::Get().GetWeapon(EHCJWeapon::Torpedo).FireInterval);
}

void AHCJShipCharacter::FireTorpedo()
{
	if (bIsDead)
	{
		return;
	}
	if (HasAuthority())
	{
		ServerFireTorpedo_Implementation();
	}
	else if (IsLocallyControlled() && IsTorpedoReady() && GetWorld()->GetTimeSeconds() - LastTorpedoFireTime > 0.5)
	{
		// (Half a second: the new stock and reload time need a moment to come back; no second request meanwhile.)
		// The owner's copy of the stock decides whether it is worth asking; the server's decides whether it fires.
		LastTorpedoFireTime = static_cast<float>(GetWorld()->GetTimeSeconds());
		UE_LOG(LogHCJ, Verbose, TEXT("Torpedo: asking the server to fire one (own stock %d, target %s)"), TorpedoCount, *GetNameSafe(LockTarget.Get()));
		ServerFireTorpedo();
	}
}

void AHCJShipCharacter::ServerFireTorpedo_Implementation()
{
	UWorld* World = GetWorld();
	if (bIsDead || World == nullptr || GetController() == nullptr || !ProjectileClass)
	{
		return;
	}
	if (ServerWeaponsBlocked())
	{
		// A fire attempt drops the cloak; the tubes work a moment later.
		bWantsCloak = false;
		return;
	}
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const FHCJWeaponDef& Def = Gameplay.GetWeapon(EHCJWeapon::Torpedo);
	FHCJTorpedoBay Bay;
	Bay.Count = TorpedoCount;
	Bay.ReadyTime = TorpedoReadyTime;
	if (!Bay.TryFire(GetServerTime(), Def.FireInterval))
	{
		return;
	}
	TorpedoCount = static_cast<uint8>(Bay.Count);
	TorpedoReadyTime = Bay.ReadyTime;
	LastTorpedoFireTime = static_cast<float>(World->GetTimeSeconds());

	SpawnProtectionEndTime = 0.0f;

	const FVector AimDirection = GetAimRotation().Vector();
	const FTransform SpawnTransform(AimDirection.Rotation(), GetMuzzleLocation());
	AHCJProjectile* Torpedo = World->SpawnActorDeferred<AHCJProjectile>(ProjectileClass, SpawnTransform, this, this, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Torpedo != nullptr)
	{
		// Quad damage does not touch torpedoes: one already is the heaviest hit in the game.
		Torpedo->InitWeapon(EHCJWeapon::Torpedo, false, 1.0f);
		Torpedo->FinishSpawning(SpawnTransform);
		if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
		{
			GameMode->NotifyTorpedoFired(Cast<AHCJShipCharacter>(Torpedo->GetHomingTarget()) != nullptr);
		}
	}
	ForceNetUpdate();
}

const FHCJWeaponDef& AHCJShipCharacter::GetWeaponDef() const
{
	return UHCJGameplaySettings::Get().GetWeapon(GetWeapon());
}

float AHCJShipCharacter::GetFireInterval() const
{
	return GetFireWeaponDef().FireInterval;
}

float AHCJShipCharacter::GetServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	return GameState != nullptr ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : 0.0f;
}

float AHCJShipCharacter::GetPowerupTimeRemaining(uint8 PowerupBit) const
{
	if (!HasPowerup(PowerupBit))
	{
		return 0.0f;
	}
	const int32 Index = FMath::FloorLog2(PowerupBit);
	return Index < UE_ARRAY_COUNT(PowerupEndTime) ? FMath::Max(0.0f, PowerupEndTime[Index] - GetServerTime()) : 0.0f;
}

bool AHCJShipCharacter::ServerGiveItem(EHCJPickupItem Item)
{
	if (!HasAuthority() || bIsDead || HealthComponent == nullptr)
	{
		return false;
	}
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();

	if (Item == EHCJPickupItem::Torpedoes)
	{
		FHCJTorpedoBay Bay;
		Bay.Count = TorpedoCount;
		const int32 Added = HCJTorpedo::IsEnabled() ? Bay.Restock(Gameplay.TorpedoRestockAmount, Gameplay.TorpedoMaxStock) : 0;
		TorpedoCount = static_cast<uint8>(Bay.Count);
		if (Added > 0)
		{
			ForceNetUpdate();
		}
		return Added > 0;
	}

	const EHCJWeapon NewWeapon = HCJ::ItemToWeapon(Item);
	if (NewWeapon != EHCJWeapon::Count)
	{
		FHCJWeaponState State;
		State.Weapon = GetWeapon();
		State.Ammo = Ammo;
		State.BaseWeapon = GetBaseWeapon();
		State.Give(NewWeapon, Gameplay.GetWeapon(NewWeapon));
		WeaponType = static_cast<uint8>(State.Weapon);
		Ammo = State.Ammo;
		// A slow weapon must not inherit a long wait from a fast one, nor the other way round.
		NextFireTime = FMath::Min(NextFireTime, static_cast<float>(GetWorld()->GetTimeSeconds()) + 0.25f);
		ForceNetUpdate();
		return true;
	}

	switch (Item)
	{
	case EHCJPickupItem::Repair:
		return HealthComponent->Heal(Gameplay.RepairAmount) > 0.0f;
	case EHCJPickupItem::ShieldCell:
		return HealthComponent->AddShield(Gameplay.ShieldCellAmount, Gameplay.ShieldOverchargeFactor) > 0.0f;
	case EHCJPickupItem::Overdrive:
		ServerSetPowerup(EHCJPowerup::Overdrive, Gameplay.OverdriveSeconds);
		return true;
	case EHCJPickupItem::QuadDamage:
		ServerSetPowerup(EHCJPowerup::Quad, Gameplay.QuadSeconds);
		return true;
	case EHCJPickupItem::Cloak:
	{
		// A full cloak meter, and for a while it does not drain. Wasted on a full meter.
		if (!Cloak.Refill())
		{
			return false;
		}
		CloakFreeEndTime = static_cast<float>(GetWorld()->GetTimeSeconds()) + HCJCloak::PadFreeSeconds;
		CloakEnergyRep = HCJCloak::PackEnergy(Cloak.Energy, Cloak.bLocked);
		return true;
	}
	default:
		return false;
	}
}

void AHCJShipCharacter::ServerSetPowerup(uint8 PowerupBit, float Seconds)
{
	const int32 Index = FMath::FloorLog2(PowerupBit);
	if (!HasAuthority() || Index >= UE_ARRAY_COUNT(PowerupEndTime))
	{
		return;
	}
	if (Seconds > 0.0f)
	{
		PowerupFlags |= PowerupBit;
		PowerupEndTime[Index] = GetServerTime() + Seconds;
	}
	else
	{
		PowerupFlags &= ~PowerupBit;
		PowerupEndTime[Index] = 0.0f;
	}
	ApplyPowerupState();
	ForceNetUpdate();
}

void AHCJShipCharacter::ServerTickPowerups()
{
	if (PowerupFlags == 0)
	{
		return;
	}
	const float Now = GetServerTime();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PowerupEndTime); ++Index)
	{
		const uint8 Bit = static_cast<uint8>(1 << Index);
		// (The cloak bit is the cloaking device's, not a timed power-up: ServerTickSystems.)
		if (Bit != EHCJPowerup::Cloak && (PowerupFlags & Bit) != 0 && Now >= PowerupEndTime[Index])
		{
			ServerSetPowerup(Bit, 0.0f);
		}
	}
}

void AHCJShipCharacter::OnRep_PowerupFlags()
{
	ApplyPowerupState();
}

void AHCJShipCharacter::ApplyPowerupState()
{
	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		// The multipliers are the server's (match config), so prediction and server use the same numbers.
		const AHCJUniverse* Universe = AHCJUniverse::Get(this);
		const FHCJPickupSettings Rules = Universe != nullptr ? Universe->GetConfig().Pickups : FHCJPickupSettings();
		ShipMovement->SetOverdrive(!bIsDead && HasPowerup(EHCJPowerup::Overdrive), Rules.OverdriveSpeedMultiplier, Rules.OverdriveAccelerationMultiplier);
	}

	// Cloak: other machines draw a faint shimmer (or nothing without the material); its pilot sees it half transparent.
	const bool bCloakedNow = IsCloaked() && !bIsDead;
	if (bWasCloakedLook && !bCloakedNow && GetWorld() != nullptr)
	{
		LocalDecloakTime = static_cast<float>(GetWorld()->GetTimeSeconds());
	}
	if (bCloakedNow != bWasCloakedLook && GetNetMode() != NM_DedicatedServer && bVisualsReady && !bIsDead)
	{
		const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
		UE_LOG(LogHCJ, Verbose, TEXT("Cloak: %s%s %s"), *GetName(), IsLocalPlayersShip(*this) ? TEXT(" (own ship)") : TEXT(""), bCloakedNow ? TEXT("cloaks") : TEXT("decloaks"));
		UHCJAssetSettings::PlaySoundAt(this, Settings.ShieldCollapseSounds, TEXT("cloak sound"), GetActorLocation(), Settings.ShieldCollapseVolume, 0.04f,
			EHCJSoundGroup::World, bCloakedNow ? 0.55f : 1.5f);
	}
	bWasCloakedLook = bCloakedNow;
	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		// No warp while cloaked (server and owning client alike).
		ShipMovement->SetWarpBlocked(bCloakedNow);
	}
	ApplyCloakLook();
}

void AHCJShipCharacter::ApplyCloakLook()
{
	if (GetNetMode() == NM_DedicatedServer || !bVisualsReady)
	{
		return;
	}
	const bool bCloakedNow = IsCloaked() && !bIsDead;
	const bool bOwn = IsLocalPlayersShip(*this);
	const int32 NewState = !bCloakedNow ? 0 : bOwn ? 2 : IsCloakRevealed() ? 3 : 1;
	// Hidden from this machine's markers, glints, trails and sounds.
	bCloakLook = NewState == 1;
	if (NewState == CloakSkinState)
	{
		return;
	}
	const bool bWasSkinned = CloakSkinState > 0;
	CloakSkinState = NewState;

	if (CloakMaterial == nullptr)
	{
		// No cloak material: another ship's cloaked hull is simply not drawn.
		for (UStaticMeshComponent* Body : { HullMesh.Get(), WingMesh.Get(), EngineMesh.Get() })
		{
			if (Body != nullptr)
			{
				Body->SetVisibility(NewState != 1 && !bIsDead, false);
			}
		}
		return;
	}

	const bool bSkinned = NewState > 0;
	if (bSkinned == bWasSkinned)
	{
		return;
	}
	for (UStaticMeshComponent* Body : { HullMesh.Get(), WingMesh.Get(), EngineMesh.Get() })
	{
		if (Body == nullptr)
		{
			continue;
		}
		Body->SetCastShadow(!bSkinned);
		if (bSkinned)
		{
			for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
			{
				Body->SetMaterial(Slot, CloakMaterial);
			}
		}
		else if (Body == HullMesh || !bUsingShipMesh)
		{
			for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
			{
				Body->SetMaterial(Slot, HullMaterial);
			}
		}
		else
		{
			// A thruster: the mesh's own materials, and the glow instance in its slot.
			Body->EmptyOverrideMaterials();
			if (ThrusterGlowSlot != INDEX_NONE && ThrusterGlowMaterial != nullptr)
			{
				Body->SetMaterial(ThrusterGlowSlot, ThrusterGlowMaterial);
			}
		}
	}
	if (HullMesh != nullptr)
	{
		HullMesh->SetOverlayMaterial(bSkinned ? nullptr : RimMaterial.Get());
	}
}

void AHCJShipCharacter::DropWeapon()
{
	if (HasAuthority())
	{
		ServerDropWeapon_Implementation();
	}
	else if (IsLocallyControlled())
	{
		ServerDropWeapon();
	}
}

void AHCJShipCharacter::SetLockTarget(AHCJShipCharacter* NewTarget)
{
	if (LockTarget.Get() == NewTarget)
	{
		return;
	}
	LockTarget = NewTarget;
	if (!HasAuthority() && IsLocallyControlled())
	{
		ServerSetLockTarget(NewTarget);
	}
}

void AHCJShipCharacter::ServerSetLockTarget_Implementation(AHCJShipCharacter* NewTarget)
{
	LockTarget = NewTarget != this ? NewTarget : nullptr;
}

void AHCJShipCharacter::ServerDropWeapon_Implementation()
{
	if (!bIsDead && !HCJ::IsBaseWeapon(GetWeapon()))
	{
		WeaponType = BaseWeaponType;
		Ammo = 0;
		ForceNetUpdate();
	}
}

float AHCJShipCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || bIsDead || IsSpawnProtected() || HealthComponent == nullptr)
	{
		return 0.0f;
	}

	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (ActualDamage <= 0.0f)
	{
		return 0.0f;
	}

	{
		const float Now = static_cast<float>(GetWorld()->GetTimeSeconds());
		if (EngagementStartTime < 0.0f || Now - LastHitTime > 4.0f)
		{
			EngagementStartTime = Now;
		}
		LastHitTime = Now;
	}

	if (EventInstigator != nullptr && EventInstigator != GetController())
	{
		LastDamager = EventInstigator;
		LastDamagerTime = static_cast<float>(GetWorld()->GetTimeSeconds());
	}

	// Which shield facing: where the hit landed (else where it came from) in the ship's own frame.
	FVector LocalHitDirection = FVector::ZeroVector;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
		FVector WorldDirection = PointDamage.HitInfo.ImpactPoint - GetActorLocation();
		if (PointDamage.HitInfo.ImpactPoint.IsNearlyZero() || WorldDirection.SizeSquared() < 1.0)
		{
			WorldDirection = -FVector(PointDamage.ShotDirection);
		}
		LocalHitDirection = GetActorTransform().InverseTransformVectorNoScale(WorldDirection).GetSafeNormal();
	}
	const bool bDirected = !LocalHitDirection.IsNearlyZero();

	// A phaser beam: its causer is the ship that fires it. Many small steps, so its cues are thinned out.
	const bool bPhaser = Cast<AHCJShipCharacter>(DamageCauser) != nullptr;
	const AController* VictimController = GetController();
	if (AHCJPlayerController* Victim = Cast<AHCJPlayerController>(GetController()))
	{
		// Where it came from, for the victim's HUD: the shooter, else the turret that owns the bolt, else the bolt.
		const AActor* Source = (EventInstigator != nullptr && EventInstigator->GetPawn() != nullptr) ? static_cast<const AActor*>(EventInstigator->GetPawn())
			: (DamageCauser != nullptr && DamageCauser->GetOwner() != nullptr) ? DamageCauser->GetOwner() : DamageCauser;
		Victim->ClientDamageTaken(Source != nullptr ? Source->GetActorLocation() : GetActorLocation(), ActualDamage);
	}

	// A photon torpedo (or its blast) has its own rule: ActualDamage is what it does to an open hull, and the facing
	// it lands on stops a share of it (HCJTorpedo::SplitDamage).
	const AHCJProjectile* CauserProjectile = Cast<AHCJProjectile>(DamageCauser);
	const bool bTorpedo = bDirected && CauserProjectile != nullptr && CauserProjectile->IsTorpedo();
	const EHCJShieldFacing HitFacing = HCJShield::SelectFacing(LocalHitDirection);
	// Fore armour of the heavy hulls: a hit on the fore facing does less.
	const float FacingScale = (bDirected && HitFacing == EHCJShieldFacing::Fore) ? GetTypeDef().ForeDamageScale : 1.0f;
	// A hit gives a cloaked ship away for a moment, and stops a warp drive that is spooling up.
	Cloak.Reveal(HCJCloak::HitRevealSeconds);
	if (const UHCJShipMovementComponent* ShipMovement = GetShipMovement(); ShipMovement != nullptr && ShipMovement->GetWarpDrive().Phase == EHCJWarpPhase::Charging)
	{
		ServerCancelWarp(TEXT("hit while charging"));
	}
	const float FacingBefore = bDirected ? HealthComponent->GetFacingFraction(HealthComponent->GetNumFacings() > 1 ? static_cast<int32>(HitFacing) : 0) : 0.0f;

	// May run Die() synchronously through OnDepleted.
	FHCJShieldHit ShieldHit;
	float Applied = 0.0f;
	if (bTorpedo)
	{
		if (FacingBefore <= 0.0f)
		{
			// Before the damage: the kill, if this is one, is reported from inside ApplyTorpedoOnFacing.
			if (AHCJGameMode* TorpedoGameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
			{
				TorpedoGameMode->NotifyTorpedoThroughOpenFacing(this);
			}
		}
		const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
		const float FullDamage = FMath::Max(Gameplay.GetWeapon(EHCJWeapon::Torpedo).Damage, 1.0f);
		Applied = HealthComponent->ApplyTorpedoOnFacing(Gameplay.TorpedoShieldDamage * ActualDamage / FullDamage, ActualDamage * FacingScale, HitFacing, EventInstigator, DamageCauser, &ShieldHit);
		UE_LOG(LogHCJ, Verbose, TEXT("Torpedo: %s took %.0f of %.0f on its %s facing (%.0f %% before): %.1f to the shield, %.1f to the hull, hull now %.0f"), *GetName(), ActualDamage, FullDamage,
			HCJShield::FacingName(HitFacing), FacingBefore * 100.0f, ShieldHit.ToShield, ShieldHit.ToHull, HealthComponent->GetHealth());
	}
	else
	{
		Applied = bDirected
			? HealthComponent->ApplyDamageOnFacing(ActualDamage * FacingScale, HitFacing, EventInstigator, DamageCauser, &ShieldHit)
			: HealthComponent->ApplyDamage(ActualDamage, EventInstigator, DamageCauser, &ShieldHit);
	}
	if (TractorTarget != nullptr)
	{
		// Enough of this and the beam lets go (ServerTickSystems).
		TractorDamageTaken += Applied;
	}

	if (AHCJPlayerController* Shooter = Cast<AHCJPlayerController>(EventInstigator); Shooter != nullptr && Shooter != VictimController
		&& (!bPhaser || HealthComponent->IsDepleted()))
	{
		// Hit sound and hit marker for the shooter; the larger marker when this hit destroyed the ship. (A beam sends its own, a few a second.)
		Shooter->ClientHitConfirm(HealthComponent->IsDepleted());
	}
	if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
	{
		if (bTorpedo)
		{
			// Counted apart from the guns, so that the hit rate stays the hit rate of bolts.
			GameMode->NotifyTorpedoHit(Applied, FacingBefore <= 0.0f);
		}
		else if (bPhaser)
		{
			GameMode->NotifyPhaserDamage(Applied, ShieldHit.ToHull > 0.0f);
		}
		else
		{
			GameMode->NotifyDamageDealt(Applied);
			GameMode->NotifyHitLanded(ShieldHit.ToHull > 0.0f);
		}
		for (uint8 Mask = ShieldHit.CollapsedMask; Mask != 0; Mask &= Mask - 1)
		{
			GameMode->NotifyFacingCollapsed();
		}
	}
	// The flare for everybody who sees the ship; a ship this hit destroyed explodes instead.
	if (bDirected && ShieldHit.ToShield > 0.0f && !bIsDead)
	{
		const float Now = static_cast<float>(GetWorld()->GetTimeSeconds());
		if (Now >= NextShieldHitSendTime)
		{
			NextShieldHitSendTime = Now + ShieldHitInterval;
			MulticastShieldHit(HCJShield::PackDirection(LocalHitDirection),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(ShieldHit.FractionAfter * 255.0f), 0, 255)));
		}
	}
	return Applied;
}

void AHCJShipCharacter::MulticastShieldHit_Implementation(uint16 PackedDirection, uint8 StrengthAfter)
{
	if (GetNetMode() != NM_DedicatedServer && !bIsDead)
	{
		StartShieldFlare(HCJShield::UnpackDirection(PackedDirection), HCJShield::StrengthColor(StrengthAfter / 255.0f));
	}
}

void AHCJShipCharacter::HandleHealthChanged(UHCJHealthComponent* Component)
{
	const UWorld* World = GetWorld();
	if (Component == nullptr || World == nullptr)
	{
		return;
	}
	const float Now = static_cast<float>(World->GetTimeSeconds());
	// A ship that just came into view (or was just spawned) is being filled in, not hit.
	const bool bSettled = GetGameTimeSinceCreation() > 0.5f;
	// Three or four facings gone in one update is a cloak going up (its shields are switched off), not a collapse under fire.
	int32 NumNewlyDown = 0;
	for (int32 Facing = 0; Facing < HCJShield::NumFacings; ++Facing)
	{
		NumNewlyDown += (Component->GetFacingShield(Facing) <= 0.0f && SeenFacingShield[Facing] > 0.0f) ? 1 : 0;
	}
	const bool bSwitchedOff = NumNewlyDown >= 3 || IsCloaked();
	for (int32 Facing = 0; Facing < HCJShield::NumFacings; ++Facing)
	{
		const float Strength = Component->GetFacingShield(Facing);
		const float Before = SeenFacingShield[Facing];
		SeenFacingShield[Facing] = Strength;
		if (!bSettled || Strength >= Before - 0.01f || bSwitchedOff)
		{
			continue;
		}
		FacingHitTime[Facing] = Now;
		UE_LOG(LogHCJ, Verbose, TEXT("Shield: %s facing of %s%s %.1f -> %.1f (facings %.1f %.1f %.1f %.1f, hull %.0f)"),
			HCJShield::FacingName(static_cast<EHCJShieldFacing>(Facing)), *GetName(), IsPlayerControlled() && IsLocallyControlled() ? TEXT(" (own ship)") : TEXT(""), Before, Strength,
			Component->GetFacingShield(0), Component->GetFacingShield(1), Component->GetFacingShield(2), Component->GetFacingShield(3), Component->GetHealth());
		if (Strength <= 0.0f && Before > 0.0f)
		{
			FacingCollapseTime[Facing] = Now;
			if (!bIsDead && !Component->IsDepleted() && GetNetMode() != NM_DedicatedServer)
			{
				PlayFacingCollapse(static_cast<EHCJShieldFacing>(Facing));
			}
		}
	}
}

float AHCJShipCharacter::GetLastFacingCollapseTime() const
{
	float Latest = -1000.0f;
	for (const float Time : FacingCollapseTime)
	{
		Latest = FMath::Max(Latest, Time);
	}
	return Latest;
}

AHCJShipCharacter* AHCJShipCharacter::GetRecentAttackerShip(float MaxAgeSeconds) const
{
	const UWorld* World = GetWorld();
	const AController* Attacker = LastDamager.Get();
	if (!HasAuthority() || World == nullptr || Attacker == nullptr || World->GetTimeSeconds() - LastDamagerTime > MaxAgeSeconds)
	{
		return nullptr;
	}
	AHCJShipCharacter* AttackerShip = Cast<AHCJShipCharacter>(Attacker->GetPawn());
	return (IsValid(AttackerShip) && !AttackerShip->IsDead() && AttackerShip != this) ? AttackerShip : nullptr;
}

bool AHCJShipCharacter::GetRecentAttackerLocation(float MaxAgeSeconds, FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	const AController* Attacker = LastDamager.Get();
	const APawn* AttackerPawn = Attacker != nullptr ? Attacker->GetPawn() : nullptr;
	if (!HasAuthority() || World == nullptr || AttackerPawn == nullptr || World->GetTimeSeconds() - LastDamagerTime > MaxAgeSeconds)
	{
		return false;
	}
	OutLocation = AttackerPawn->GetActorLocation();
	return true;
}

void AHCJShipCharacter::StartShieldFlare(const FVector& LocalDirection, const FLinearColor& Color)
{
	if (ShieldBubble == nullptr || ShieldBubbleMaterial == nullptr)
	{
		return;
	}
	// The bubble is a unit sphere stretched to an ellipsoid: the point of the ellipsoid in LocalDirection, in sphere space.
	const FVector Radii = UHCJAssetSettings::Get().ShieldBubbleRadii.ComponentMax(FVector(1.0));
	const FVector SphereDirection = (LocalDirection / Radii).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const int32 Slot = NextShieldFlareSlot;
	NextShieldFlareSlot = 1 - NextShieldFlareSlot;
	ShieldFlareTime[Slot] = static_cast<float>(GetWorld()->GetTimeSeconds());
	UE_LOG(LogHCJ, Verbose, TEXT("Shield: bubble flare on %s%s at local direction %s"), *GetName(), IsPlayerControlled() && IsLocallyControlled() ? TEXT(" (own ship)") : TEXT(""), *LocalDirection.ToCompactString());
	ShieldBubbleMaterial->SetVectorParameterValue(Slot == 0 ? TEXT("HitDirA") : TEXT("HitDirB"), FLinearColor(SphereDirection));
	ShieldBubbleMaterial->SetVectorParameterValue(Slot == 0 ? TEXT("HitColorA") : TEXT("HitColorB"), Color);
	bShieldBubbleActive = true;
	TickShieldBubble(ShieldFlareTime[Slot]);

	// A star of streaks where the shield was hit (item 26), on the surface of the bubble.
	if (UHCJEffectsDirector* Effects = UHCJEffectsDirector::Get(this))
	{
		const FVector Unit = LocalDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const double Reach = GetTypeDef().Scale / FMath::Sqrt(FMath::Square(Unit.X / Radii.X) + FMath::Square(Unit.Y / Radii.Y) + FMath::Square(Unit.Z / Radii.Z));
		const bool bOwnShip = IsPlayerControlled() && IsLocallyControlled();
		// (On the ship the view follows the effects director makes the star small and dim, so that it does not hide the ship.)
		Effects->PlayImpactStar(GetActorLocation() + GetActorTransform().TransformVectorNoScale(Unit) * Reach, bOwnShip ? 0.75f : 1.1f,
			FMath::Lerp(Color, FLinearColor::White, 0.35f));
	}
}

void AHCJShipCharacter::PlayFacingCollapse(EHCJShieldFacing Facing)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	UE_LOG(LogHCJ, Verbose, TEXT("Shield: %s facing of %s%s collapsed"), HCJShield::FacingName(Facing), *GetName(), IsPlayerControlled() && IsLocallyControlled() ? TEXT(" (own ship)") : TEXT(""));
	UHCJAssetSettings::PlaySoundAt(this, Settings.ShieldCollapseSounds, TEXT("shield collapse sound"), GetActorLocation(), Settings.ShieldCollapseVolume, 0.06f,
		EHCJSoundGroup::World);
	if (ShieldBubble == nullptr || ShieldBubbleMaterial == nullptr)
	{
		return;
	}
	ShieldCollapseTime = static_cast<float>(GetWorld()->GetTimeSeconds());
	ShieldBubbleMaterial->SetVectorParameterValue(TEXT("CollapseDir"), FLinearColor(HCJShield::FacingAxis(Facing)));
	bShieldBubbleActive = true;
	TickShieldBubble(ShieldCollapseTime);
}

void AHCJShipCharacter::TickShieldBubble(float Now)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const float AgeA = FMath::Clamp((Now - ShieldFlareTime[0]) / Settings.ShieldFlareSeconds, 0.0f, 1.0f);
	const float AgeB = FMath::Clamp((Now - ShieldFlareTime[1]) / Settings.ShieldFlareSeconds, 0.0f, 1.0f);
	const float CollapseAge = FMath::Clamp((Now - ShieldCollapseTime) / Settings.ShieldCollapseSeconds, 0.0f, 1.0f);
	const bool bActive = !bIsDead && (AgeA < 1.0f || AgeB < 1.0f || CollapseAge < 1.0f);
	if (ShieldBubble->IsVisible() != bActive)
	{
		ShieldBubble->SetVisibility(bActive);
	}
	bShieldBubbleActive = bActive;
	if (!bActive)
	{
		return;
	}
	ShieldBubbleMaterial->SetScalarParameterValue(TEXT("HitAgeA"), AgeA);
	ShieldBubbleMaterial->SetScalarParameterValue(TEXT("HitAgeB"), AgeB);
	// The flicker of a failing facing: on / nearly off about 22 times a second, dying away.
	const float Flicker = FMath::Frac(Now * 22.0f) < 0.55f ? 1.0f : 0.12f;
	ShieldBubbleMaterial->SetScalarParameterValue(TEXT("Collapse"), CollapseAge < 1.0f ? FMath::Square(1.0f - CollapseAge) * Flicker : 0.0f);
}

void AHCJShipCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (const UWorld* World = GetWorld())
	{
		SpawnProtectionEndTime = static_cast<float>(World->GetTimeSeconds()) + SpawnProtectionSeconds;
		bSpawnShield = true;
	}
	UpdateShipColor();
}

void AHCJShipCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	UpdateShipColor();
}

void AHCJShipCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	if (HasAuthority())
	{
		Die(nullptr, nullptr);
	}
}

void AHCJShipCharacter::AddThrustInput(const FVector& LocalAxes)
{
	if (bIsDead || LocalAxes.IsNearlyZero())
	{
		return;
	}

	const FRotationMatrix AimAxes(GetAimRotation());
	const FVector WorldInput =
		AimAxes.GetUnitAxis(EAxis::X) * FMath::Clamp(LocalAxes.X, -1.0, 1.0) +
		AimAxes.GetUnitAxis(EAxis::Y) * FMath::Clamp(LocalAxes.Y, -1.0, 1.0) +
		AimAxes.GetUnitAxis(EAxis::Z) * FMath::Clamp(LocalAxes.Z, -1.0, 1.0);

	AddMovementInput(WorldInput.GetClampedToMaxSize(1.0), 1.0f);
}

void AHCJShipCharacter::SetBoosting(bool bNewBoosting)
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}

	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		ShipMovement->SetWantsToBoost(bNewBoosting && !bIsDead);
	}
}

bool AHCJShipCharacter::IsBoosting() const
{
	const UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	return ShipMovement != nullptr && (ShipMovement->WantsToBoost() || ShipMovement->IsBoostActive());
}

void AHCJShipCharacter::SetFiring(bool bNewFiring)
{
	const bool bEffective = bNewFiring && !bIsDead;

	if (HasAuthority())
	{
		bWantsToFire = bEffective;
		if (bEffective)
		{
			SpawnProtectionEndTime = 0.0f;
		}
	}
	else if (IsLocallyControlled() && bWantsToFire != bEffective)
	{
		bWantsToFire = bEffective;
		ServerSetFiring(bEffective);
	}
}

void AHCJShipCharacter::ServerSetFiring_Implementation(bool bNewFiring)
{
	bWantsToFire = bNewFiring && !bIsDead;
	if (bWantsToFire)
	{
		SpawnProtectionEndTime = 0.0f;
	}
}

void AHCJShipCharacter::ToggleCameraMode()
{
	bFirstPerson = !bFirstPerson;
	UpdateCameraMode();
}

UHCJShipMovementComponent* AHCJShipCharacter::GetShipMovement() const
{
	return Cast<UHCJShipMovementComponent>(GetCharacterMovement());
}

int32 AHCJShipCharacter::GetCurrentSector() const
{
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	if (Universe == nullptr || !Universe->IsBuilt())
	{
		return INDEX_NONE;
	}
	return Universe->GetSectorAtLocation(GetActorLocation());
}

FVector AHCJShipCharacter::GetMuzzleLocation() const
{
	return GetActorLocation() + GetAimRotation().Vector() * MuzzleOffset;
}

FRotator AHCJShipCharacter::GetAimRotation() const
{
	if (const AController* AimController = GetController())
	{
		return AimController->GetControlRotation();
	}
	return GetActorRotation();
}

float AHCJShipCharacter::GetProjectileSpeed() const
{
	return GetFireWeaponDef().ProjectileSpeed;
}

bool AHCJShipCharacter::IsSpawnProtected() const
{
	const UWorld* World = GetWorld();
	return HasAuthority() && World != nullptr && World->GetTimeSeconds() < SpawnProtectionEndTime;
}

float AHCJShipCharacter::GetEngagementSeconds() const
{
	const UWorld* World = GetWorld();
	return (World != nullptr && EngagementStartTime >= 0.0f) ? static_cast<float>(World->GetTimeSeconds()) - EngagementStartTime : -1.0f;
}

bool AHCJShipCharacter::RequestPortalTeleport(const FVector& WorldDelta, int32 DestSector, bool bWormhole)
{
	const UWorld* World = GetWorld();
	if (!HasAuthority() || World == nullptr || bIsDead || bHasPendingTeleport)
	{
		return false;
	}

	const float Now = static_cast<float>(World->GetTimeSeconds());
	if (Now < PortalCooldownEndTime)
	{
		return false;
	}

	bHasPendingTeleport = true;
	bPendingWormhole = bWormhole;
	PendingTeleportDelta = WorldDelta;
	PortalCooldownEndTime = Now + HCJ::PortalReentryCooldown;

	UE_LOG(LogHCJ, Verbose, TEXT("%s enters a portal to sector %d"), *GetName(), DestSector);
	return true;
}

float AHCJShipCharacter::GetPortalCooldownRemaining() const
{
	const UWorld* World = GetWorld();
	if (!HasAuthority() || World == nullptr)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, PortalCooldownEndTime - static_cast<float>(World->GetTimeSeconds()));
}

void AHCJShipCharacter::Die(AController* Killer, AActor* Causer)
{
	if (!HasAuthority() || bIsDead)
	{
		return;
	}

	bIsDead = true;
	bWantsToFire = false;
	bHasPendingTeleport = false;
	// Power-ups and the pickup weapon die with the ship.
	PowerupFlags = 0;
	WeaponType = BaseWeaponType;
	Ammo = 0;
	TorpedoCount = 0;
	PhaserBeam = 0;
	PhaserTarget = nullptr;
	// Beams let go, the cloak drops, the drive stops.
	if (TractorTarget != nullptr)
	{
		ServerReleaseTractor(TEXT("the holder was destroyed"), false);
	}
	if (AHCJShipCharacter* Holder = GetTractorHolder())
	{
		Holder->ServerReleaseTractor(TEXT("the held ship was destroyed"), false);
	}
	TractorHolder = nullptr;
	Cloak = FHCJCloakDevice();
	bWantsCloak = false;
	bWantsTractor = false;
	WarpStateRep = 0;
	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		ShipMovement->SetWantsToWarp(false);
		ShipMovement->ResetWarp();
		ShipMovement->SetTractor(nullptr, 0.0f);
	}
	ApplyPowerupState();

	AController* VictimController = GetController();
	if (Killer == nullptr || Killer == VictimController)
	{
		AController* Credited = LastDamager.Get();
		const UWorld* World = GetWorld();
		if (Credited != nullptr && Credited != VictimController && World != nullptr
			&& World->GetTimeSeconds() - LastDamagerTime <= LastDamagerCreditSeconds)
		{
			Killer = Credited;
		}
	}

	UE_LOG(LogHCJ, Verbose, TEXT("%s destroyed (killer %s, causer %s)"), *GetName(), *GetNameSafe(Killer), *GetNameSafe(Causer));

	ApplyDeadState();
	PlayDeathEffects();
	ForceNetUpdate();

	if (UWorld* World = GetWorld())
	{
		if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
		{
			GameMode->NotifyShipKilled(VictimController, Killer, this, Causer);
		}
		// The blast pushes the ships around it.
		const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
		UHCJEffectsDirector::ServerPushShips(World, GetActorLocation(), Gameplay.ShipShockwaveRadius, Gameplay.ShipShockwaveSpeed, this);
	}

	DetachFromControllerPendingDestroy();
	SetLifeSpan(FMath::Max(DeathLingerSeconds, 0.05f));
}

void AHCJShipCharacter::OnRep_IsDead()
{
	if (bIsDead)
	{
		ApplyDeadState();
		PlayDeathEffects();
	}
}

void AHCJShipCharacter::HandleHealthDepleted(UHCJHealthComponent* Component, AController* Killer, AActor* Causer)
{
	Die(Killer, Causer);
}

void AHCJShipCharacter::ServerApplyPendingTeleport()
{
	if (!bHasPendingTeleport)
	{
		return;
	}
	bHasPendingTeleport = false;

	const int32 FromSector = GetCurrentSector();
	// A ship that flies into a portal at warp comes out of it at boost speed.
	ServerCancelWarp(TEXT("portal"));
	TeleportTo(GetActorLocation() + PendingTeleportDelta, GetActorRotation(), false, true);
	ForceNetUpdate();

	UE_LOG(LogHCJ, Verbose, TEXT("%s jumped from sector %d to sector %d%s"), *GetName(), FromSector, GetCurrentSector(), bPendingWormhole ? TEXT(" through a wormhole") : TEXT(""));
	if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
	{
		if (bPendingWormhole)
		{
			GameMode->NotifyWormholeJump();
		}
		else
		{
			GameMode->NotifyPortalJump();
		}
	}
}

void AHCJShipCharacter::ServerTickWeapon()
{
	if (!bWantsToFire || GetController() == nullptr || ServerWeaponsBlocked())
	{
		return;
	}

	const float Now = static_cast<float>(GetWorld()->GetTimeSeconds());
	if (Now < NextFireTime)
	{
		return;
	}

	if (GetFireWeapon() == EHCJWeapon::Phaser)
	{
		// The beam: ServerTickPhaser.
		return;
	}
	const FHCJWeaponDef& Def = GetFireWeaponDef();
	NextFireTime = Now + FMath::Max(Def.FireInterval, 0.02f);
	SpawnProtectionEndTime = 0.0f;
	ServerFireProjectile();

	FHCJWeaponState State;
	State.Weapon = GetWeapon();
	State.Ammo = Ammo;
	State.BaseWeapon = GetBaseWeapon();
	State.ConsumeShot(Def);
	WeaponType = static_cast<uint8>(State.Weapon);
	Ammo = State.Ammo;
}

void AHCJShipCharacter::ServerTickBounds(float DeltaSeconds)
{
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	if (Universe == nullptr || !Universe->IsBuilt())
	{
		return;
	}

	const FVector Location = GetActorLocation();
	const int32 Sector = Universe->GetSectorAtLocation(Location);
	if (Sector == INDEX_NONE)
	{
		return;
	}

	if (Universe->IsInsideEventHorizon(Location))
	{
		// Environment death like leaving the sector: no killer, unless somebody shot the ship in on the way.
		UE_LOG(LogHCJ, Log, TEXT("%s crossed the event horizon of the black hole in sector %d"), *GetName(), Sector);
		if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
		{
			GameMode->NotifyBlackHoleDeath(GetController());
		}
		Die(nullptr, nullptr);
		return;
	}

	const double DistanceSq = FVector::DistSquared(Location, Universe->GetSectorOrigin(Sector));
	if (DistanceSq > FMath::Square(static_cast<double>(HCJ::SectorKillRadius)))
	{
		Die(nullptr, nullptr);
	}
	else if (DistanceSq > FMath::Square(static_cast<double>(HCJ::SectorSafeRadius)) && HealthComponent != nullptr)
	{
		// Environment damage goes straight to the health component: spawn protection does not cover deserters.
		HealthComponent->ApplyDamage(HCJ::OutOfBoundsDamagePerSecond * DeltaSeconds, nullptr, nullptr);
	}
}

void AHCJShipCharacter::ServerFireProjectile()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !ProjectileClass)
	{
		return;
	}

	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const EHCJWeapon Weapon = GetFireWeapon();
	const FHCJWeaponDef& Def = Gameplay.GetWeapon(Weapon);
	const bool bQuad = HasPowerup(EHCJPowerup::Quad);
	const FVector Muzzle = GetMuzzleLocation();
	const FVector AimDirection = GetAimRotation().Vector();
	const float SpreadRadians = FMath::DegreesToRadians(Def.SpreadDegrees);

	bool bFired = false;
	for (int32 Pellet = 0; Pellet < FMath::Max(Def.Pellets, 1); ++Pellet)
	{
		const FVector Direction = SpreadRadians > 0.0f ? WeaponRandom.VRandCone(AimDirection, SpreadRadians) : AimDirection;
		const FTransform SpawnTransform(Direction.Rotation(), Muzzle);
		AHCJProjectile* Projectile = World->SpawnActorDeferred<AHCJProjectile>(ProjectileClass, SpawnTransform, this, this,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Projectile != nullptr)
		{
			Projectile->InitWeapon(Weapon, bQuad, (bQuad ? Gameplay.QuadDamageMultiplier : 1.0f) * GetTypeDef().BoltDamageScale);
			Projectile->FinishSpawning(SpawnTransform);
			bFired = true;
		}
	}

	if (bFired)
	{
		if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
		{
			GameMode->NotifyShotFired();
		}
	}
}

void AHCJShipCharacter::ApplyDeadState()
{
	// Component visibility instead of SetActorHiddenInGame: a hidden actor without collision stops being
	// net relevant, and bIsDead would then never reach the clients.
	if (USkeletalMeshComponent* VisualRoot = GetMesh())
	{
		VisualRoot->SetVisibility(false, true);
	}
	for (UStaticMeshComponent* Body : { HullMesh.Get(), WingMesh.Get(), EngineMesh.Get() })
	{
		if (Body != nullptr)
		{
			Body->SetVisibility(false, false);
		}
	}

	if (EngineAudio != nullptr)
	{
		EngineAudio->Stop();
	}
	if (BoostTrail != nullptr)
	{
		BoostTrail->Deactivate();
	}
	for (UStaticMeshComponent* Part : { PhaserBeamMesh.Get(), PhaserMuzzleGlow.Get(), PhaserImpactGlow.Get() })
	{
		if (Part != nullptr)
		{
			Part->SetVisibility(false);
		}
	}
	for (UAudioComponent* Loop : { PhaserAudio.Get(), PhaserAudioLayer.Get() })
	{
		if (Loop != nullptr)
		{
			Loop->Stop();
		}
	}
	bPhaserLook = false;
	for (UStaticMeshComponent* Part : { WarpStreakMesh.Get(), TractorBeamMesh.Get() })
	{
		if (Part != nullptr)
		{
			Part->SetVisibility(false);
		}
	}

	SetActorEnableCollision(false);
	for (UStaticMeshComponent* Part : { BeaconMesh.Get(), EngineGlowMesh.Get(), EngineTrailMesh.Get(), EngineJetMesh.Get() })
	{
		if (Part != nullptr)
		{
			Part->SetVisibility(false);
		}
	}

	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		ShipMovement->SetWantsToBoost(false);
		ShipMovement->StopMovementImmediately();
		ShipMovement->DisableMovement();
	}
}

void AHCJShipCharacter::UpdateCameraMode()
{
	if (CameraBoom != nullptr)
	{
		CameraBoom->TargetArmLength = bFirstPerson ? 0.0f : ThirdPersonArmLength;
		CameraBoom->bEnableCameraLag = !bFirstPerson;
		CameraBoom->bEnableCameraRotationLag = !bFirstPerson;
		CameraBoom->SocketOffset = bFirstPerson ? FVector::ZeroVector : ThirdPersonSocketOffset;
	}

	for (UStaticMeshComponent* Body : { HullMesh.Get(), WingMesh.Get(), EngineMesh.Get() })
	{
		if (Body != nullptr)
		{
			Body->SetOwnerNoSee(bFirstPerson);
			if (bCloakLook && CloakMaterial == nullptr)
			{
				// A new hull (ApplyShipLook) of a cloaked ship stays hidden.
				Body->SetVisibility(false, false);
			}
		}
	}
	if (BoostTrail != nullptr)
	{
		BoostTrail->SetOwnerNoSee(bFirstPerson);
	}
	if (ShieldBubble != nullptr)
	{
		// From the cockpit a flare would fill the screen; the HUD arcs say it instead.
		ShieldBubble->SetOwnerNoSee(bFirstPerson);
	}
}

void AHCJShipCharacter::UpdateShipColor()
{
	const AHCJPlayerState* ShipPlayerState = GetPlayerState<AHCJPlayerState>();
	const FLinearColor PlayerColor = ShipPlayerState != nullptr ? ShipPlayerState->GetPlayerColor() : UnownedHullColor;
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();

	// The player state may just have arrived or changed: hull and paint follow it.
	ApplyShipLook();

	if (HullMaterial != nullptr)
	{
		if (!bUsingShipMesh)
		{
			HullMaterial->SetVectorParameterValue(TEXT("Color"), PlayerColor);
		}
		else if (!Settings.ShipTintParameter.IsNone())
		{
			HullMaterial->SetVectorParameterValue(Settings.ShipTintParameter, PlayerColor * Settings.ShipTintStrength);
		}
	}
	if (ThrusterGlowMaterial != nullptr && !Settings.ThrusterGlowColorParameter.IsNone())
	{
		ThrusterGlowMaterial->SetVectorParameterValue(Settings.ThrusterGlowColorParameter, PlayerColor);
	}
	if (BeaconMaterial != nullptr && !bBeaconIsGlint)
	{
		if (!Settings.PickupColorParameter.IsNone())
		{
			BeaconMaterial->SetVectorParameterValue(Settings.PickupColorParameter, FMath::Lerp(PlayerColor, FLinearColor::White, 0.3f));
		}
		if (!Settings.PickupBrightnessParameter.IsNone())
		{
			BeaconMaterial->SetScalarParameterValue(Settings.PickupBrightnessParameter, Settings.ShipBeaconBrightness);
		}
	}
	if (BoostTrail != nullptr && !Settings.BoostTrailColorParameter.IsNone())
	{
		BoostTrail->SetVariableLinearColor(Settings.BoostTrailColorParameter, PlayerColor);
	}
}

void AHCJShipCharacter::ApplyShipLook()
{
	if (!bVisualsReady || HullMesh == nullptr)
	{
		return;
	}

	const AHCJPlayerState* ShipPlayerState = GetPlayerState<AHCJPlayerState>();
	const int32 PlayerId = ShipPlayerState != nullptr ? ShipPlayerState->GetPlayerId() : INDEX_NONE;
	const int32 TypeKey = (static_cast<int32>(ShipType) << 16) | (static_cast<int32>(ShipVariant) << 8) | ShipSkin;
	if (bUsingShipMesh && PlayerId == LookPlayerId && TypeKey == LookTypeKey)
	{
		return;
	}

	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (Settings.ShipMeshes.Num() == 0)
	{
		return;
	}

	// Hull: one of the hulls of the ship's type - the one its player chose, else a hash of the player id - so the
	// same ship is the same hull on every machine.
	const FHCJShipTypeDef& TypeDef = GetTypeDef();
	const int32 Variant = HCJShipTypes::PickHullMesh(ShipType, ShipVariant, PlayerId, Settings.ShipMeshes.Num());
	UStaticMesh* ShipMesh = UHCJAssetSettings::Load<UStaticMesh>(Settings.ShipMeshes[Variant], TEXT("ship body"));
	const FBox ShipBounds = ShipMesh != nullptr ? ShipMesh->GetBoundingBox() : FBox(ForceInit);
	if (ShipMesh == nullptr || !ShipBounds.IsValid || ShipBounds.GetSize().GetMax() < 1.0)
	{
		return;
	}
	bUsingShipMesh = true;
	LookPlayerId = PlayerId;
	LookTypeKey = TypeKey;

	// Uniform scale to the configured length (times the size of the type); the centre of the bounding box sits on the capsule centre.
	const double Scale = Settings.ShipMeshLength * TypeDef.Scale / ShipBounds.GetSize().GetMax();
	HullMesh->SetStaticMesh(ShipMesh);
	HullMesh->EmptyOverrideMaterials();
	HullMesh->SetRelativeScale3D(FVector(Scale));
	HullMesh->SetRelativeLocationAndRotation(-Settings.ShipMeshRotation.RotateVector(ShipBounds.GetCenter() * Scale), Settings.ShipMeshRotation);

	// Paint: the skin nearest to the player colour is the colour cue of the ship.
	TArray<FLinearColor> SkinColors;
	SkinColors.Reserve(Settings.ShipSkins.Num());
	for (const FHCJShipSkin& Skin : Settings.ShipSkins)
	{
		SkinColors.Add(Skin.Color);
	}
	const FLinearColor PlayerColor = ShipPlayerState != nullptr ? ShipPlayerState->GetPlayerColor() : UnownedHullColor;
	// The paint its player chose, else the one nearest the player colour.
	const int32 SkinIndex = Settings.ShipSkins.IsValidIndex(ShipSkin) ? static_cast<int32>(ShipSkin) : UHCJAssetSettings::PickNearestSkin(PlayerColor, SkinColors);
	UMaterialInterface* SkinMaterial = SkinIndex != INDEX_NONE
		? UHCJAssetSettings::Load<UMaterialInterface>(Settings.ShipSkins[SkinIndex].Material, TEXT("ship skin")) : nullptr;
	UMaterialInterface* BaseMaterial = SkinMaterial != nullptr ? SkinMaterial : HullMesh->GetMaterial(0);
	HullMaterial = BaseMaterial != nullptr ? UMaterialInstanceDynamic::Create(BaseMaterial, this) : nullptr;
	if (HullMaterial != nullptr)
	{
		for (int32 Slot = 0; Slot < HullMesh->GetNumMaterials(); ++Slot)
		{
			HullMesh->SetMaterial(Slot, HullMaterial);
		}
	}

	// The two spare body components become the thrusters (or disappear when there is no thruster mesh).
	UStaticMesh* ThrusterMesh = UHCJAssetSettings::Load<UStaticMesh>(Settings.ThrusterMesh, TEXT("ship thruster"));
	for (UStaticMeshComponent* Body : { WingMesh.Get(), EngineMesh.Get() })
	{
		Body->EmptyOverrideMaterials();
		Body->SetStaticMesh(ThrusterMesh);
		if (ThrusterMesh != nullptr)
		{
			const double Side = Body == WingMesh ? 1.0 : -1.0;
			Body->AttachToComponent(HullMesh, FAttachmentTransformRules::KeepRelativeTransform);
			Body->SetRelativeLocationAndRotation(Settings.ThrusterOffset * FVector(1.0, Side, 1.0), Settings.ThrusterRotation);
			Body->SetRelativeScale3D(FVector(Settings.ThrusterScale));
		}
	}
	ThrusterGlowMaterial = nullptr;
	const int32 GlowSlot = ThrusterMesh != nullptr ? ThrusterMesh->GetMaterialIndex(Settings.ThrusterGlowSlot) : INDEX_NONE;
	ThrusterGlowSlot = INDEX_NONE;
	if (GlowSlot != INDEX_NONE)
	{
		if (UMaterialInterface* GlowBase = WingMesh->GetMaterial(GlowSlot))
		{
			ThrusterGlowMaterial = UMaterialInstanceDynamic::Create(GlowBase, this);
			ThrusterGlowSlot = GlowSlot;
			WingMesh->SetMaterial(GlowSlot, ThrusterGlowMaterial);
			EngineMesh->SetMaterial(GlowSlot, ThrusterGlowMaterial);
		}
	}

	BaseHullRotation = HullMesh->GetRelativeRotation().Quaternion();
	BaseHullLocation = HullMesh->GetRelativeLocation();

	// The new components state must respect the death / camera / cloak state already applied.
	UpdateCameraMode();
	if (HullMesh != nullptr && RimMaterial != nullptr)
	{
		HullMesh->SetOverlayMaterial(RimMaterial);
	}
	CloakSkinState = 0;
	ApplyCloakLook();
}

void AHCJShipCharacter::SetupShipVisuals()
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();

	bVisualsReady = true;
	ApplyShipLook();
	if (!bUsingShipMesh)
	{
		if (UMaterialInterface* BaseMaterial = HullMesh->GetMaterial(0))
		{
			// Fallback: the cone / cube / cylinder ship in the player colour.
			HullMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			HullMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.45f);
			HullMesh->SetMaterial(0, HullMaterial);
			WingMesh->SetMaterial(0, HullMaterial);
			EngineMesh->SetMaterial(0, HullMaterial);
		}
	}

	if (ShieldBubble != nullptr)
	{
		// Without the material there is no bubble (the component stays hidden).
		if (UMaterialInterface* Bubble = UHCJAssetSettings::Load<UMaterialInterface>(Settings.ShieldBubbleMaterial, TEXT("shield bubble")))
		{
			ShieldBubbleMaterial = UMaterialInstanceDynamic::Create(Bubble, this);
			ShieldBubbleMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.ShieldBubbleBrightness);
			ShieldBubbleMaterial->SetScalarParameterValue(TEXT("HitAgeA"), 1.0f);
			ShieldBubbleMaterial->SetScalarParameterValue(TEXT("HitAgeB"), 1.0f);
			ShieldBubbleMaterial->SetScalarParameterValue(TEXT("Collapse"), 0.0f);
			ShieldBubble->SetMaterial(0, ShieldBubbleMaterial);
			// The basic sphere has a radius of 50.
			ShieldBubble->SetRelativeScale3D(Settings.ShieldBubbleRadii / 50.0);
		}
	}

	UStaticMesh* HighlightPlane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* GlintBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.ShipGlintMaterial, TEXT("ship glint"));
	if (BeaconMesh != nullptr)
	{
		if (GlintBase != nullptr && HighlightPlane != nullptr)
		{
			// Minimum apparent size: a point of light with a halo on a quad that faces the view.
			bBeaconIsGlint = true;
			BeaconMesh->SetStaticMesh(HighlightPlane);
			BeaconMaterial = UMaterialInstanceDynamic::Create(GlintBase, this);
			BeaconMaterial->SetScalarParameterValue(TEXT("Spikes"), 1.0f);
			BeaconMesh->SetMaterial(0, BeaconMaterial);
			BeaconMesh->SetUsingAbsoluteLocation(true);
			BeaconMesh->SetUsingAbsoluteRotation(true);
			BeaconMesh->SetUsingAbsoluteScale(true);
			BeaconMesh->SetReceivesDecals(false);
		}
		// Else the unlit glow material of the pads and portals on the sphere; without it the beacon stays off.
		else if (UMaterialInterface* Glow = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PickupMaterial, TEXT("ship beacon glow")))
		{
			BeaconMaterial = UMaterialInstanceDynamic::Create(Glow, this);
			BeaconMesh->SetMaterial(0, BeaconMaterial);
		}
	}

	// Rim glow: the hull drawn once more with an additive fresnel material.
	if (UMaterialInterface* RimBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.ShipRimMaterial, TEXT("ship rim glow")))
	{
		RimMaterial = UMaterialInstanceDynamic::Create(RimBase, this);
		RimMaterial->SetScalarParameterValue(TEXT("Brightness"), 0.0f);
		RimMaterial->SetScalarParameterValue(TEXT("Floor"), Settings.ShipAmbientFloor);
		HullMesh->SetOverlayMaterial(RimMaterial);
	}

	// Cloak: one translucent material instance that replaces every material of the hull while the ship is cloaked.
	if (UMaterialInterface* CloakBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.CloakMaterial, TEXT("cloak shimmer")))
	{
		CloakMaterial = UMaterialInstanceDynamic::Create(CloakBase, this);
		CloakMaterial->SetScalarParameterValue(TEXT("Phase"), static_cast<float>(GetUniqueID() % 61));
	}

	// Engine glow and engine trail: two free-standing quads (world space), driven by TickShipEffects.
	{
		const auto MakeHighlightQuad = [this, HighlightPlane](const TCHAR* Name, UMaterialInstanceDynamic* Material)
		{
			UStaticMeshComponent* Quad = NewObject<UStaticMeshComponent>(this, Name);
			Quad->SetupAttachment(GetCapsuleComponent());
			Quad->SetUsingAbsoluteLocation(true);
			Quad->SetUsingAbsoluteRotation(true);
			Quad->SetUsingAbsoluteScale(true);
			Quad->SetStaticMesh(HighlightPlane);
			Quad->SetMaterial(0, Material);
			Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Quad->SetGenerateOverlapEvents(false);
			Quad->SetCanEverAffectNavigation(false);
			Quad->SetCastShadow(false);
			Quad->SetReceivesDecals(false);
			Quad->SetVisibility(false);
			Quad->RegisterComponent();
			return Quad;
		};
		if (GlintBase != nullptr && HighlightPlane != nullptr && Settings.EngineGlowSize > 0.0f)
		{
			EngineGlowMaterial = UMaterialInstanceDynamic::Create(GlintBase, this);
			EngineGlowMaterial->SetScalarParameterValue(TEXT("Spikes"), 0.0f);
			EngineGlowMesh = MakeHighlightQuad(TEXT("EngineGlowMesh"), EngineGlowMaterial);
		}
		UMaterialInterface* TrailBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.EngineTrailMaterial, TEXT("engine trail"));
		if (TrailBase != nullptr && HighlightPlane != nullptr)
		{
			EngineTrailMaterial = UMaterialInstanceDynamic::Create(TrailBase, this);
			EngineTrailMesh = MakeHighlightQuad(TEXT("EngineTrailMesh"), EngineTrailMaterial);
		}
		UMaterialInterface* JetBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.EngineJetMaterial, TEXT("engine jet"));
		if (JetBase != nullptr && HighlightPlane != nullptr && Settings.EngineJetWidth > 0.0f)
		{
			EngineJetMaterial = UMaterialInstanceDynamic::Create(JetBase, this);
			EngineJetMaterial->SetScalarParameterValue(TEXT("Phase"), static_cast<float>(GetUniqueID() % 97));
			EngineJetMesh = MakeHighlightQuad(TEXT("EngineJetMesh"), EngineJetMaterial);
		}
	}

	// Phaser: the beam quad and a glow at each end. Free-standing (world space), hidden until the beam runs.
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* BeamBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PhaserBeamMaterial, TEXT("phaser beam"));
		UMaterialInterface* GlowBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PhaserGlowMaterial, TEXT("phaser glow"));
		const FLinearColor BeamColor = UHCJGameplaySettings::Get().GetWeapon(EHCJWeapon::Phaser).Color;
		const FLinearColor PhaserColor(BeamColor.R, BeamColor.G, BeamColor.B, 1.0f);
		const auto MakeQuad = [this, Plane](const TCHAR* Name, UMaterialInterface* Material, float Size)
		{
			UStaticMeshComponent* Quad = NewObject<UStaticMeshComponent>(this, Name);
			Quad->SetupAttachment(GetCapsuleComponent());
			Quad->SetUsingAbsoluteLocation(true);
			Quad->SetUsingAbsoluteRotation(true);
			Quad->SetUsingAbsoluteScale(true);
			Quad->SetStaticMesh(Plane);
			Quad->SetMaterial(0, Material);
			Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Quad->SetGenerateOverlapEvents(false);
			Quad->SetCanEverAffectNavigation(false);
			Quad->SetCastShadow(false);
			Quad->SetReceivesDecals(false);
			Quad->SetVisibility(false);
			Quad->SetWorldScale3D(FVector(Size / 100.0f));
			Quad->RegisterComponent();
			return Quad;
		};
		if (Plane != nullptr && BeamBase != nullptr)
		{
			PhaserBeamMaterial = UMaterialInstanceDynamic::Create(BeamBase, this);
			PhaserBeamMaterial->SetVectorParameterValue(TEXT("Color"), PhaserColor);
			PhaserBeamMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.PhaserBeamBrightness);
			PhaserBeamMaterial->SetScalarParameterValue(TEXT("Phase"), static_cast<float>(GetUniqueID() % 89));
			PhaserBeamMesh = MakeQuad(TEXT("PhaserBeamMesh"), PhaserBeamMaterial, Settings.PhaserBeamWidth);
		}
		if (Plane != nullptr && GlowBase != nullptr)
		{
			PhaserGlowMaterial = UMaterialInstanceDynamic::Create(GlowBase, this);
			PhaserGlowMaterial->SetVectorParameterValue(TEXT("Color"), PhaserColor);
			PhaserGlowMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.PhaserGlowBrightness);
			PhaserGlowMaterial->SetScalarParameterValue(TEXT("Phase"), static_cast<float>(GetUniqueID() % 97));
			PhaserMuzzleGlow = MakeQuad(TEXT("PhaserMuzzleGlow"), PhaserGlowMaterial, Settings.PhaserMuzzleGlowSize);
			PhaserImpactGlow = MakeQuad(TEXT("PhaserImpactGlow"), PhaserGlowMaterial, Settings.PhaserImpactGlowSize);
		}

		// The held beam: two loops at their own pitch, at the ship.
		const auto MakeLoop = [this, &Settings](const TCHAR* Name, const FSoftObjectPath& Path, float Volume, float Pitch) -> UAudioComponent*
		{
			USoundBase* Sound = UHCJAssetSettings::Load<USoundBase>(Path, TEXT("phaser beam loop"));
			if (Sound == nullptr)
			{
				return nullptr;
			}
			UAudioComponent* Loop = NewObject<UAudioComponent>(this, Name);
			Loop->SetupAttachment(GetCapsuleComponent());
			Loop->bAutoActivate = false;
			Loop->bAutoDestroy = false;
			Loop->bStopWhenOwnerDestroyed = true;
			Loop->AttenuationSettings = UHCJAssetSettings::GetSoundAttenuation();
			Loop->SoundClassOverride = UHCJAssetSettings::Load<USoundClass>(Settings.SfxSoundClass, TEXT("effects sound class"));
			Loop->SetSound(Sound);
			Loop->SetVolumeMultiplier(FMath::Max(Volume, 0.001f));
			Loop->SetPitchMultiplier(Pitch);
			Loop->RegisterComponent();
			return Loop;
		};
		PhaserAudio = MakeLoop(TEXT("PhaserAudio"), Settings.PhaserLoopSound, Settings.PhaserLoopVolume, Settings.PhaserLoopPitch);
		PhaserAudioLayer = MakeLoop(TEXT("PhaserAudioLayer"), Settings.PhaserLayerSound, Settings.PhaserLayerVolume, Settings.PhaserLayerPitch);
	}

	if (USoundBase* EngineSound = UHCJAssetSettings::Load<USoundBase>(Settings.EngineSound, TEXT("ship engine loop")))
	{
		FSoundAttenuationSettings Attenuation;
		Attenuation.bAttenuate = true;
		Attenuation.bSpatialize = true;
		Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
		Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		// Full volume out to the chase camera of the pilot, silent at EngineAudibleDistance.
		const float InnerRadius = FMath::Min(ThirdPersonArmLength + 300.0f, 0.5f * Settings.EngineAudibleDistance);
		Attenuation.AttenuationShapeExtents = FVector(InnerRadius, 0.0, 0.0);
		Attenuation.FalloffDistance = FMath::Max(Settings.EngineAudibleDistance - InnerRadius, 1.0f);

		EngineAudio = NewObject<UAudioComponent>(this, TEXT("EngineAudio"));
		EngineAudio->SetupAttachment(GetCapsuleComponent());
		EngineAudio->bAutoActivate = false;
		EngineAudio->bAutoDestroy = false;
		EngineAudio->bStopWhenOwnerDestroyed = true;
		EngineAudio->bOverrideAttenuation = true;
		EngineAudio->AttenuationOverrides = Attenuation;
		// The cue of the pack has no class of ours: put it under the effects volume.
		EngineAudio->SoundClassOverride = UHCJAssetSettings::Load<USoundClass>(Settings.SfxSoundClass, TEXT("effects sound class"));
		EngineAudio->SetSound(EngineSound);
		EngineAudio->RegisterComponent();
	}

	if (UNiagaraSystem* TrailSystem = UHCJAssetSettings::Load<UNiagaraSystem>(Settings.BoostTrailEffect, TEXT("ship boost trail")))
	{
		USceneComponent* VisualRoot = GetMesh() != nullptr ? static_cast<USceneComponent*>(GetMesh()) : GetCapsuleComponent();
		BoostTrail = NewObject<UNiagaraComponent>(this, TEXT("BoostTrail"));
		BoostTrail->SetupAttachment(VisualRoot);
		BoostTrail->SetAutoActivate(false);
		BoostTrail->SetAsset(TrailSystem);
		// At the tail of the ship.
		BoostTrail->SetRelativeLocation(FVector(-0.5 * (bUsingShipMesh ? Settings.ShipMeshLength : 200.0f), 0.0, 0.0));
		BoostTrail->SetRelativeScale3D(FVector(Settings.BoostTrailScale));
		BoostTrail->RegisterComponent();
	}
}

void AHCJShipCharacter::TickShipEffects()
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	const float CruiseSpeed = ShipMovement != nullptr ? ShipMovement->GetCruiseSpeed() : 3000.0f;
	const float TopSpeed = CruiseSpeed * (ShipMovement != nullptr ? ShipMovement->BoostSpeedMultiplier : 2.0f);

	// The speed is known on every machine (replicated movement); the boost intent is not.
	const float Speed = bIsDead ? 0.0f : static_cast<float>(GetVelocity().Size());
	const float Throttle = FMath::Clamp(Speed / FMath::Max(TopSpeed, 1.0f), 0.0f, 1.0f);

	// Sound cues that need no component: arriving through a portal (the position jumps by a sector), starting to boost.
	const FVector Location = GetActorLocation();
	if (!bIsDead && bHasLastEffectLocation && FVector::DistSquared(Location, LastEffectLocation) > FMath::Square(100000.0))
	{
		// Two sectors that no portal joins (two or more bits apart): that was a wormhole.
		const AHCJUniverse* Universe = AHCJUniverse::Get(this);
		const bool bThroughWormhole = Universe != nullptr && Universe->IsBuilt() && Settings.WormholeSounds.Num() > 0
			&& FMath::CountBits(static_cast<uint32>(Universe->GetSectorAtLocation(Location) ^ Universe->GetSectorAtLocation(LastEffectLocation))) >= 2;
		UHCJAssetSettings::PlaySoundAt(this, bThroughWormhole ? Settings.WormholeSounds : Settings.PortalSounds, TEXT("portal jump sound"), Location,
			bThroughWormhole ? Settings.WormholeVolume : Settings.PortalVolume, 0.05f, EHCJSoundGroup::World);
		// The gateway it left and the one it came out of flash (a respawn far from any portal finds none in reach).
		if (!bCloakLook)
		{
			AHCJPortal::NotifyPassage(this, LastEffectLocation);
			AHCJPortal::NotifyPassage(this, Location);
		}
	}
	LastEffectLocation = Location;
	bHasLastEffectLocation = true;
	if (!bIsDead)
	{
		// What the debris of this ship starts with (the movement is stopped the moment it dies).
		LastEffectVelocity = GetVelocity();
	}

	const bool bBoostLookNow = !bIsDead && Speed > CruiseSpeed * BoostLookSpeedFraction;
	if (bBoostLookNow && !bWasBoostLook)
	{
		UHCJAssetSettings::PlaySoundAt(this, Settings.BoostSounds, TEXT("boost sound"), Location, Settings.BoostVolume, 0.08f, EHCJSoundGroup::World);
	}
	bWasBoostLook = bBoostLookNow;

	// Seeing the enemy: rim glow, a glint that keeps a far ship at a minimum size on the screen, engine glow and trail.
	{
		const UWorld* World = GetWorld();
		const APlayerController* LocalController = (World != nullptr && GEngine != nullptr) ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
		FVector ViewLocation = Location;
		FRotator ViewRotation = FRotator::ZeroRotator;
		if (LocalController != nullptr)
		{
			LocalController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}
		const bool bOwn = LocalController != nullptr && LocalController->GetPawn() == this;
		const bool bShown = LocalController != nullptr && !bIsDead && !bCloakLook;
		const bool bHighlight = UHCJUserSettings::Get().bEnemyHighlight;
		const float Distance = static_cast<float>(FVector::Dist(ViewLocation, Location));
		const FVector ToView = Distance > 1.0f ? (ViewLocation - Location) / Distance : FVector::UpVector;
		const float HullLength = (bUsingShipMesh ? Settings.ShipMeshLength : 200.0f) * GetTypeDef().Scale;
		const float DeltaSeconds = World != nullptr ? World->GetDeltaSeconds() : 0.0f;
		// Warp: the engine flares up while the drive spools, and the trail stretches at warp.
		const EHCJWarpPhase WarpPhase = bIsDead ? EHCJWarpPhase::Idle : GetWarpPhase();
		const bool bWarpNow = WarpPhase == EHCJWarpPhase::Active;
		const float WarpGlow = bWarpNow ? 1.0f : WarpPhase == EHCJWarpPhase::Charging
			? FMath::Clamp((static_cast<float>(World != nullptr ? World->GetTimeSeconds() : 0.0) - WarpPhaseLookTime) / 1.6f, 0.0f, 1.0f) : 0.0f;

		const AHCJPlayerState* ShipPlayerState = GetPlayerState<AHCJPlayerState>();
		const FLinearColor PlayerColor = ShipPlayerState != nullptr ? ShipPlayerState->GetPlayerColor() : UnownedHullColor;
		FLinearColor EnemyColor = FMath::Lerp(Settings.EnemyGlowColor, PlayerColor, FMath::Clamp(Settings.EnemyColorMix, 0.0f, 1.0f));
		EnemyColor.A = 1.0f;
		// Everybody else is an enemy (free-for-all); the own ship shows its own colour.
		const FLinearColor GlowColor = FMath::Lerp(bOwn ? FLinearColor(PlayerColor.R, PlayerColor.G, PlayerColor.B, 1.0f) : EnemyColor, WarpColor, WarpGlow);

		if (RimMaterial != nullptr)
		{
			const float Far = FMath::Clamp((Distance - Settings.ShipRimNearDistance) / FMath::Max(Settings.ShipRimFarDistance - Settings.ShipRimNearDistance, 1.0f), 0.0f, 1.0f);
			RimMaterial->SetVectorParameterValue(TEXT("Color"), EnemyColor);
			RimMaterial->SetScalarParameterValue(TEXT("Far"), Far);
			RimMaterial->SetScalarParameterValue(TEXT("Brightness"), (bOwn || !bHighlight) ? 0.0f : Settings.ShipRimBrightness);
		}

		if (BeaconMesh != nullptr && BeaconMaterial != nullptr)
		{
			bool bShow = false;
			if (bShown && !bOwn && Distance < HCJ::NetCullDistance)
			{
				if (bBeaconIsGlint)
				{
					// Fades in where the hull itself gets small; with the highlight off it stays a small point.
					const float Alpha = FMath::Clamp((Distance - Settings.ShipGlintMinDistance) / FMath::Max(Settings.ShipGlintMinDistance, 1.0f), 0.0f, 1.0f);
					const float Edge = Distance * Settings.ShipGlintAngularSize * (bHighlight ? 1.0f : 0.4f);
					if (Alpha > 0.02f && Edge > 10.0f)
					{
						bShow = true;
						BeaconMaterial->SetVectorParameterValue(TEXT("Color"), EnemyColor);
						BeaconMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.ShipGlintBrightness * Alpha);
						BeaconMaterial->SetScalarParameterValue(TEXT("Backing"), bHighlight ? Settings.ShipGlintBacking * Alpha : 0.0f);
						// In front of the hull as the view sees it, so the hull does not cut the quad in two.
						BeaconMesh->SetWorldLocation(Location + ToView * (0.6f * HullLength));
						BeaconMesh->SetWorldScale3D(FVector(Edge / 100.0f));
						AHCJTorpedoBlast::FaceLocalView(BeaconMesh);
					}
				}
				else if (Settings.ShipBeaconAngularSize > 0.0f)
				{
					// The older beacon: a glowing sphere that grows in from ShipBeaconMinDistance.
					const float Radius = FMath::Min(Distance - Settings.ShipBeaconMinDistance, Distance) * Settings.ShipBeaconAngularSize;
					if (Radius > 5.0f)
					{
						bShow = true;
						BeaconMesh->SetWorldScale3D(FVector(FMath::Min(Radius, 600.0f) / 50.0f));
					}
				}
			}
			if (BeaconMesh->IsVisible() != bShow)
			{
				BeaconMesh->SetVisibility(bShow);
			}
		}

		// The tail as it is drawn (the smoothed mesh of a remote ship).
		const USceneComponent* VisualRoot = GetMesh() != nullptr ? static_cast<const USceneComponent*>(GetMesh()) : GetCapsuleComponent();
		const FVector Tail = VisualRoot->GetComponentLocation() - GetActorForwardVector() * (0.5f * HullLength);
		const bool bEngineShown = bShown && !(bOwn && bFirstPerson) && Distance < HCJ::NetCullDistance;

		if (EngineGlowMesh != nullptr)
		{
			if (bEngineShown)
			{
				const float Edge = FMath::Lerp(Settings.EngineGlowSize, Settings.EngineGlowBoostSize, Throttle) * (bOwn ? 0.4f : 1.0f) * (1.0f + 0.5f * WarpGlow);
				EngineGlowMaterial->SetVectorParameterValue(TEXT("Color"), GlowColor);
				EngineGlowMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.EngineGlowBrightness * FMath::Lerp(0.55f, 1.0f, Throttle) * (bOwn ? 0.14f : 1.0f) * (1.0f + (bOwn ? 1.5f : 0.8f) * WarpGlow));
				EngineGlowMesh->SetWorldLocation(Tail + ToView * 40.0f);
				EngineGlowMesh->SetWorldScale3D(FVector(Edge / 100.0f));
				AHCJTorpedoBlast::FaceLocalView(EngineGlowMesh);
			}
			if (EngineGlowMesh->IsVisible() != bEngineShown)
			{
				EngineGlowMesh->SetVisibility(bEngineShown);
			}
		}

		if (EngineTrailMesh != nullptr)
		{
			// The far end follows the tail with a lag: its distance is about speed x EngineTrailSeconds, also through turns.
			if (!bHasTrailEnd || FVector::DistSquared(Tail, TrailEnd) > FMath::Square(20000.0))
			{
				TrailEnd = Tail;
				bHasTrailEnd = true;
			}
			TrailEnd = FMath::Lerp(TrailEnd, Tail, 1.0 - FMath::Exp(-DeltaSeconds / FMath::Max(Settings.EngineTrailSeconds, 0.02f)));
			FVector Along = TrailEnd - Tail;
			// The own trail points at the chase camera right behind the ship: it is kept short, thin and dim, or it would
			// lie across the whole screen.
			Along = Along.GetClampedToMaxSize(bOwn ? 0.8 * HullLength * (bWarpNow ? 1.5 : 1.0) : (bWarpNow ? 14000.0 : 4000.0));
			const double Length = Along.Size();
			const bool bTrail = bEngineShown && Length > 60.0;
			if (bTrail)
			{
				const FVector Middle = Tail + Along * 0.5;
				EngineTrailMaterial->SetVectorParameterValue(TEXT("Color"), GlowColor);
				EngineTrailMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.EngineTrailBrightness * (bOwn ? 0.3f : 1.0f) * (bWarpNow ? 2.5f : 1.0f));
				EngineTrailMesh->SetWorldLocationAndRotation(Middle, FRotationMatrix::MakeFromXZ(Along, ViewLocation - Middle).Rotator());
				EngineTrailMesh->SetWorldScale3D(FVector(Length / 100.0, Settings.EngineTrailWidth * (bOwn ? 0.55 : (bWarpNow ? 1.6 : 1.0)) / 100.0, 1.0));
			}
			if (EngineTrailMesh->IsVisible() != bTrail)
			{
				EngineTrailMesh->SetVisibility(bTrail);
			}
		}

		if (EngineJetMesh != nullptr)
		{
			// The jet: straight back from the tail, its length follows the speed (at rest a short flame, under boost a long one).
			// The own jet points at the chase camera behind the ship: it is kept short enough to end on the screen.
			const float Lengths = FMath::Lerp(Settings.EngineJetIdleLength, Settings.EngineJetBoostLength, FMath::Pow(Throttle, 0.8f)) * (1.0f + 0.8f * WarpGlow);
			const double JetLength = double(HullLength) * Lengths * (bOwn ? Settings.EngineJetOwnShare : 1.0f);
			const bool bJet = bEngineShown && JetLength > 20.0;
			if (bJet)
			{
				const FVector Back = -GetActorForwardVector();
				const FVector Middle = Tail + Back * (0.5 * JetLength);
				const float TypeScale = GetTypeDef().Scale;
				EngineJetMaterial->SetVectorParameterValue(TEXT("Color"), GlowColor);
				EngineJetMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.EngineJetBrightness * FMath::Lerp(0.7f, 1.0f, Throttle) * (bOwn ? 0.55f : 1.0f));
				EngineJetMesh->SetWorldLocationAndRotation(Middle, FRotationMatrix::MakeFromXZ(Back, ViewLocation - Middle).Rotator());
				EngineJetMesh->SetWorldScale3D(FVector(JetLength / 100.0, Settings.EngineJetWidth * TypeScale * FMath::Lerp(0.8f, 1.15f, Throttle) * (bOwn ? 0.8f : 1.0f) / 100.0, 1.0));
			}
			if (EngineJetMesh->IsVisible() != bJet)
			{
				EngineJetMesh->SetVisibility(bJet);
			}
		}
	}

	const float EffectDelta = GetWorld()->GetDeltaSeconds();
	const float EffectNow = static_cast<float>(GetWorld()->GetTimeSeconds());

	// Shield bubble: nothing to do unless a flare or a collapse flicker is running.
	if (bShieldBubbleActive)
	{
		TickShieldBubble(EffectNow);
	}

	// Phaser beam: nothing to do unless this ship's beam runs (or has just stopped).
	if (PhaserBeam != 0 || bPhaserLook)
	{
		TickPhaserEffects(EffectNow, EffectDelta);
	}

	// Warp streaks and flash, tractor beam, cloak shimmer.
	TickSystemEffects(EffectNow, EffectDelta);

	// Bank into turns: looks only, on every machine, from how fast the ship's heading changes.
	if (HullMesh != nullptr && bUsingShipMesh && EffectDelta > UE_KINDA_SMALL_NUMBER)
	{
		const float Yaw = static_cast<float>(GetActorRotation().Yaw);
		const float YawRate = bHasLastBankYaw ? FMath::FindDeltaAngleDegrees(LastBankYaw, Yaw) / EffectDelta : 0.0f;
		LastBankYaw = Yaw;
		bHasLastBankYaw = true;
		const float WantedBank = bIsDead ? 0.0f : FMath::Clamp(YawRate * BankPerYawRate, -MaxBankDegrees, MaxBankDegrees);
		BankAngle = FMath::FInterpTo(BankAngle, WantedBank, EffectDelta, 3.5f);
		// Positive roll puts the right wing down: a turn to the right (yaw growing) banks right.
		const FQuat Bank = FRotator(0.0, 0.0, BankAngle).Quaternion();
		HullMesh->SetRelativeLocationAndRotation(Bank.RotateVector(BaseHullLocation), Bank * BaseHullRotation);
	}

	// The pilot's own view: wider under boost, and a short shake when the hull is hit.
	if (const AHCJPlayerController* Pilot = Cast<AHCJPlayerController>(GetController()); Pilot != nullptr && Pilot->IsLocalController() && Camera != nullptr && CameraBoom != nullptr)
	{
		const float BoostAlpha = FMath::Clamp((Speed - CruiseSpeed) / FMath::Max(CruiseSpeed * 0.8f, 1.0f), 0.0f, 1.0f);
		// Wider under boost, much wider at warp (it snaps back when the ship drops out).
		const bool bAtWarp = GetWarpPhase() == EHCJWarpPhase::Active;
		Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, bAtWarp ? 128.0f : 95.0f + 10.0f * BoostAlpha, EffectDelta, bAtWarp ? 7.0f : 4.0f));

		const float HitAge = EffectNow - Pilot->GetLastDamageTime();
		const float Shake = (HitAge >= 0.0f && HitAge < 0.35f) ? (1.0f - HitAge / 0.35f) * FMath::Clamp(Pilot->GetLastDamageAmount() / 25.0f, 0.3f, 1.0f) : 0.0f;
		FVector Jolt(0.0, FMath::Sin(EffectNow * 83.0f) * Shake * 16.0f, FMath::Cos(EffectNow * 97.0f) * Shake * 16.0f);
		if (const UHCJEffectsDirector* Effects = UHCJEffectsDirector::Get(this))
		{
			// Explosions nearby (off with the "Camera shake" setting).
			Jolt += Effects->GetCameraShake();
		}
		CameraBoom->SocketOffset = (bFirstPerson ? FVector::ZeroVector : ThirdPersonSocketOffset) + Jolt;
	}

	// The lights of the hull: brighter with thrust; white shimmer under the spawn shield; magenta pulse with quad damage.
	const bool bQuadNow = !bIsDead && HasPowerup(EHCJPowerup::Quad);
	if (HullMaterial != nullptr && bUsingShipMesh && !Settings.ShipTintParameter.IsNone())
	{
		if (bQuadNow)
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(EffectNow * 9.0f);
			HullMaterial->SetVectorParameterValue(Settings.ShipTintParameter, FLinearColor(1.0f, 0.1f, 0.9f) * (2.0f + 6.0f * Pulse));
		}
		else if (IsSpawnShielded())
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(EffectNow * 16.0f);
			HullMaterial->SetVectorParameterValue(Settings.ShipTintParameter, FLinearColor(0.8f, 0.95f, 1.0f) * (3.0f + 12.0f * Pulse));
		}
		else
		{
			const AHCJPlayerState* ShipPlayerState = GetPlayerState<AHCJPlayerState>();
			const FLinearColor PlayerColor = ShipPlayerState != nullptr ? ShipPlayerState->GetPlayerColor() : UnownedHullColor;
			HullMaterial->SetVectorParameterValue(Settings.ShipTintParameter, PlayerColor * (Settings.ShipTintStrength * FMath::Lerp(0.6f, 1.8f, Throttle)));
		}
	}
	bQuadLook = bQuadNow;

	if (EngineAudio == nullptr && BoostTrail == nullptr && ThrusterGlowMaterial == nullptr)
	{
		return;
	}

	if (EngineAudio != nullptr)
	{
		const bool bAudible = !bIsDead && !bCloakLook && UHCJAssetSettings::IsNearLocalView(this, GetActorLocation(), Settings.EngineAudibleDistance);
		if (bAudible)
		{
			EngineAudio->SetVolumeMultiplier(FMath::Max(Settings.EngineVolume, 0.001f) * FMath::Lerp(0.35f, 1.0f, Throttle));
			EngineAudio->SetPitchMultiplier(FMath::Lerp(0.7f, 1.5f, Throttle));
			if (!EngineAudio->IsPlaying())
			{
				EngineAudio->Play();
			}
		}
		else if (EngineAudio->IsPlaying())
		{
			EngineAudio->Stop();
		}
	}

	if (ThrusterGlowMaterial != nullptr && !Settings.ThrusterGlowBrightnessParameter.IsNone())
	{
		ThrusterGlowMaterial->SetScalarParameterValue(Settings.ThrusterGlowBrightnessParameter,
			FMath::Lerp(Settings.ThrusterGlowIdle, Settings.ThrusterGlowBoost, Throttle));
	}

	if (BoostTrail != nullptr)
	{
		const bool bBoostLook = !bIsDead && !bCloakLook && Speed > CruiseSpeed * BoostLookSpeedFraction;
		if (bBoostLook && !BoostTrail->IsActive())
		{
			BoostTrail->Activate();
		}
		else if (!bBoostLook && BoostTrail->IsActive())
		{
			BoostTrail->Deactivate();
		}
	}
}

void AHCJShipCharacter::PlayDeathEffects()
{
	if (bDeathEffectsPlayed || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	bDeathEffectsPlayed = true;

	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	UHCJAssetSettings::SpawnEffect(this, Settings.ExplosionEffect, TEXT("ship explosion"), GetActorLocation(), GetActorRotation(), Settings.ExplosionScale);
	// Debris: three bursts of the rock impact (it throws chunks) around the blast.
	for (int32 Burst = 0; Burst < 3; ++Burst)
	{
		const FVector Offset = FRotator(-30.0 + 30.0 * Burst, 40.0 + 120.0 * Burst, 0.0).Vector() * 170.0;
		UHCJAssetSettings::SpawnEffect(this, Settings.ImpactRockEffect, TEXT("ship debris"), GetActorLocation() + Offset, FRotator::ZeroRotator, Settings.ImpactScale * 3.0f);
	}
	if (UHCJEffectsDirector* Effects = UHCJEffectsDirector::Get(this))
	{
		// Flare, tumbling hull plates, camera shake.
		const AHCJPlayerState* ShipPlayerState = GetPlayerState<AHCJPlayerState>();
		Effects->PlayShipExplosion(GetActorLocation(), LastEffectVelocity, ShipPlayerState != nullptr ? ShipPlayerState->GetPlayerColor() : UnownedHullColor);
	}
	UHCJAssetSettings::PlaySound(this, Settings.ExplosionSound, TEXT("ship explosion sound"), GetActorLocation(), Settings.ExplosionVolume);
	UHCJAssetSettings::PlaySoundAt(this, Settings.ExplosionSounds, TEXT("ship explosion sound"), GetActorLocation(), Settings.ExplosionVolume, 0.1f, EHCJSoundGroup::Explosion);
}

void AHCJShipCharacter::PlayPhaserCue(bool bStart)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	UHCJAssetSettings::PlaySoundAt(this, bStart ? Settings.PhaserStartSounds : Settings.PhaserStopSounds, bStart ? TEXT("phaser start sound") : TEXT("phaser stop sound"),
		GetActorLocation(), Settings.PhaserCueVolume, 0.05f, EHCJSoundGroup::Fire, bStart ? Settings.PhaserStartPitch : Settings.PhaserStopPitch);
}

void AHCJShipCharacter::TickPhaserEffects(float Now, float DeltaSeconds)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	FVector Start;
	FVector End;
	bool bHit = false;
	const bool bBeam = GetPhaserBeamSegment(Start, End, bHit) && UHCJAssetSettings::IsNearLocalView(this, Start, HCJ::NetCullDistance);
	if (bBeam)
	{
		LastPhaserBeamTime = Now;
	}
	if (!bBeam && !bPhaserLook)
	{
		return;
	}

	if (bBeam != bPhaserLook)
	{
		bPhaserLook = bBeam;
		for (UStaticMeshComponent* Part : { PhaserBeamMesh.Get(), PhaserMuzzleGlow.Get(), PhaserImpactGlow.Get() })
		{
			if (Part != nullptr)
			{
				Part->SetVisibility(bBeam);
			}
		}
		const bool bAudible = bBeam && UHCJAssetSettings::IsNearLocalView(this, Start, Settings.SoundInnerRadius + Settings.SoundFalloffDistance);
		for (UAudioComponent* Loop : { PhaserAudio.Get(), PhaserAudioLayer.Get() })
		{
			if (Loop == nullptr)
			{
				continue;
			}
			if (bAudible && !Loop->IsPlaying())
			{
				Loop->Play();
			}
			else if (!bAudible && Loop->IsPlaying())
			{
				Loop->FadeOut(0.08f, 0.0f);
			}
		}
	}
	if (!bBeam)
	{
		return;
	}

	FVector ViewLocation = Start;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (const APlayerController* LocalController = GEngine != nullptr ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr)
	{
		LocalController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}

	const FVector Along = End - Start;
	const double Length = Along.Size();
	if (PhaserBeamMesh != nullptr && Length > 1.0)
	{
		// The plane lies in its XY: X along the beam, its normal turned as far towards the view as the beam allows.
		const FVector Middle = (Start + End) * 0.5;
		PhaserBeamMesh->SetWorldLocationAndRotation(Middle, FRotationMatrix::MakeFromXZ(Along, ViewLocation - Middle).Rotator());
		PhaserBeamMesh->SetWorldScale3D(FVector(Length / 100.0, Settings.PhaserBeamWidth / 100.0, 1.0));
		if (PhaserBeamMaterial != nullptr)
		{
			PhaserBeamMaterial->SetScalarParameterValue(TEXT("Length"), static_cast<float>(Length));
		}
	}
	if (PhaserMuzzleGlow != nullptr)
	{
		PhaserMuzzleGlow->SetWorldLocation(Start);
		AHCJTorpedoBlast::FaceLocalView(PhaserMuzzleGlow);
	}
	if (PhaserImpactGlow != nullptr)
	{
		if (PhaserImpactGlow->IsVisible() != bHit)
		{
			PhaserImpactGlow->SetVisibility(bHit);
		}
		if (bHit)
		{
			// A little towards the firer, so that the glow is not swallowed by what the beam hits.
			PhaserImpactGlow->SetWorldLocation(End - Along.GetSafeNormal() * 25.0);
			AHCJTorpedoBlast::FaceLocalView(PhaserImpactGlow);
		}
	}

	// Sparks where the beam ends on something: the bolt impact effects, a few bursts a second.
	if (bHit && Now >= NextPhaserSparkTime)
	{
		NextPhaserSparkTime = Now + FMath::Max(Settings.PhaserSparkInterval, 0.03f);
		const bool bOnShip = IsValid(PhaserTarget);
		if (UHCJEffectsDirector* Effects = UHCJEffectsDirector::Get(this))
		{
			// A star of streaks where the beam burns (item 26); on a ship in the beam's own colour, on rock paler.
			Effects->PlayImpactStar(End - Along.GetSafeNormal() * 30.0, bOnShip ? 1.0f : 0.75f, bOnShip ? FLinearColor(1.0f, 0.5f, 0.15f) : FLinearColor(1.0f, 0.7f, 0.4f));
		}
		UNiagaraComponent* Sparks = UHCJAssetSettings::SpawnEffect(this, bOnShip ? Settings.ImpactShipEffect : Settings.ImpactRockEffect, TEXT("phaser sparks"), End,
			FRotator::ZeroRotator, Settings.ImpactScale);
		if (Sparks != nullptr)
		{
			const FVector Normal = -Along.GetSafeNormal();
			if (!Settings.ImpactNormalParameter.IsNone())
			{
				Sparks->SetVariableVec3(Settings.ImpactNormalParameter, Normal);
			}
			if (!Settings.ImpactDirectionParameter.IsNone())
			{
				Sparks->SetVariableVec3(Settings.ImpactDirectionParameter, -Normal);
			}
		}
	}
}


// ---------------------------------------------------------------------------------------------------------------------
// Ship type
// ---------------------------------------------------------------------------------------------------------------------

void AHCJShipCharacter::ServerInitShipType(uint8 InType, uint8 InVariant, uint8 InSkin)
{
	if (!HasAuthority() || bIsDead)
	{
		return;
	}
	ShipType = HCJShipTypes::SanitizeType(InType);
	ShipVariant = HCJShipTypes::SanitizeVariant(ShipType, InVariant);
	ShipSkin = HCJShipTypes::SanitizeSkin(InSkin, UHCJAssetSettings::Get().ShipSkins.Num());
	ApplyShipType();

	// A fresh ship of that type: stock, bank, cloak, drive.
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	TorpedoCount = static_cast<uint8>(HCJTorpedo::IsEnabled() ? FMath::Clamp(GetTypeDef().Torpedoes, 0, Gameplay.TorpedoMaxStock) : 0);
	TorpedoReadyTime = 0.0f;
	PhaserBank = FHCJPhaserBank();
	PhaserChargeRep = HCJPhaser::PackCharge(PhaserBank.Charge, PhaserBank.bLocked);
	Cloak = FHCJCloakDevice();
	CloakEnergyRep = HCJCloak::PackEnergy(Cloak.Energy, Cloak.bLocked);
	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		ShipMovement->ResetWarp();
	}
	UE_LOG(LogHCJ, Verbose, TEXT("Ship type: %s is a %s (hull %d, paint %d)"), *GetName(), GetTypeDef().Name, ShipVariant, ShipSkin);
	ForceNetUpdate();
}

void AHCJShipCharacter::OnRep_ShipLook()
{
	UE_LOG(LogHCJ, Verbose, TEXT("Ship type (client): %s%s is a %s (hull %d, paint %d)"), *GetName(), IsLocalPlayersShip(*this) ? TEXT(" (own ship)") : TEXT(""), GetTypeDef().Name,
		ShipVariant, ShipSkin);
	ApplyShipType();
}

void AHCJShipCharacter::ApplyShipType()
{
	const FHCJShipTypeDef& Def = GetTypeDef();
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const int32 TypeKey = ShipType;
	if (TypeKey != AppliedTypeKey)
	{
		AppliedTypeKey = TypeKey;

		// Flight: the same numbers on the owning client and on the server, so prediction and server agree.
		if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
		{
			ShipMovement->SetFlightModel(Def.CruiseSpeed, Def.Acceleration, Def.Braking, Def.BoostDrainSeconds, Def.BoostRechargeSeconds);
			FHCJWarpParams WarpParams;
			WarpParams.MaxSeconds = Def.WarpSeconds;
			WarpParams.Cooldown = Def.WarpCooldown;
			WarpParams.ExitSpeed = Def.CruiseSpeed * ShipMovement->BoostSpeedMultiplier;
			ShipMovement->SetWarpParams(WarpParams);
		}

		// Size: movement collision, hit sphere and shield bubble alike, on every machine.
		const float Radius = HCJ::ShipCollisionRadius * Def.Scale;
		if (UCapsuleComponent* Capsule = GetCapsuleComponent(); Capsule != nullptr && !FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), Radius, 0.01f))
		{
			Capsule->SetCapsuleSize(Radius, Radius);
		}
		if (HitSphere != nullptr && Gameplay.AimAssistRadius > HCJ::ShipCollisionRadius)
		{
			HitSphere->SetSphereRadius(Gameplay.AimAssistRadius * Def.Scale);
		}
		if (ShieldBubble != nullptr)
		{
			ShieldBubble->SetRelativeScale3D(UHCJAssetSettings::Get().ShieldBubbleRadii * Def.Scale / 50.0);
		}

		// Hull and shields (not with the single-shield measurement aid, which set its own).
		if (HealthComponent != nullptr && HealthComponent->GetNumFacings() > 1)
		{
			HealthComponent->SetShieldRegen(Gameplay.ShieldRegenDelay, Def.ShieldRegenRate);
			const float MaxShield = Def.ShieldFacing * HCJShield::NumFacings;
			if (!FMath::IsNearlyEqual(HealthComponent->GetMaxHealth(), Def.MaxHull) || !FMath::IsNearlyEqual(HealthComponent->GetMaxShield(), MaxShield))
			{
				HealthComponent->ChangeMaxValues(Def.MaxHull, MaxShield);
				for (int32 Facing = 0; Facing < HCJShield::NumFacings; ++Facing)
				{
					SeenFacingShield[Facing] = HealthComponent->GetFacingShield(Facing);
				}
			}
		}
	}
	// The hull and the paint follow type, hull choice and paint choice.
	ApplyShipLook();
}

// ---------------------------------------------------------------------------------------------------------------------
// Warp drive
// ---------------------------------------------------------------------------------------------------------------------

void AHCJShipCharacter::SetWarping(bool bNewWarping)
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}
	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		ShipMovement->SetWantsToWarp(bNewWarping && !bIsDead);
	}
}

EHCJWarpPhase AHCJShipCharacter::GetWarpPhase() const
{
	const UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	if (ShipMovement != nullptr && !bIsDead && (HasAuthority() || GetLocalRole() == ROLE_AutonomousProxy))
	{
		return ShipMovement->GetWarpDrive().Phase;
	}
	return bIsDead ? EHCJWarpPhase::Idle : static_cast<EHCJWarpPhase>(WarpStateRep);
}

float AHCJShipCharacter::GetWarpMeter() const
{
	const UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	if (ShipMovement == nullptr)
	{
		return 0.0f;
	}
	const FHCJWarpDrive& Drive = ShipMovement->GetWarpDrive();
	const FHCJWarpParams& Params = ShipMovement->GetWarpParams();
	switch (Drive.Phase)
	{
	case EHCJWarpPhase::Charging: return Drive.GetChargeFraction(Params);
	case EHCJWarpPhase::Active: return Drive.GetEnergyFraction(Params);
	case EHCJWarpPhase::Cooldown: return Drive.GetCooldownFraction(Params);
	default: return 0.0f;
	}
}

float AHCJShipCharacter::GetWarpCooldownSeconds() const
{
	const UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	return (ShipMovement != nullptr && ShipMovement->GetWarpDrive().Phase == EHCJWarpPhase::Cooldown) ? FMath::Max(ShipMovement->GetWarpDrive().Time, 0.0f) : 0.0f;
}

void AHCJShipCharacter::ServerCancelWarp(const TCHAR* Why)
{
	UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	if (!HasAuthority() || ShipMovement == nullptr || !ShipMovement->GetWarpDrive().IsBusy())
	{
		return;
	}
	const bool bWasActive = ShipMovement->GetWarpDrive().Phase == EHCJWarpPhase::Active;
	ShipMovement->CancelWarp(bWasActive ? ShipMovement->GetWarpParams().Cooldown : ShipMovement->GetWarpParams().AbortCooldown);
	++WarpCancelCount;
	UE_LOG(LogHCJ, Verbose, TEXT("Warp: the drive of %s was stopped %s (%s)"), *GetName(), bWasActive ? TEXT("at warp") : TEXT("while charging"), Why);
	if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
	{
		GameMode->NotifyWarpCancelled();
	}
}

void AHCJShipCharacter::OnRep_WarpCancel()
{
	// The server stopped the drive: so does the prediction (it would otherwise jump and be pulled back).
	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		const bool bWasActive = ShipMovement->GetWarpDrive().Phase == EHCJWarpPhase::Active;
		ShipMovement->CancelWarp(bWasActive ? ShipMovement->GetWarpParams().Cooldown : ShipMovement->GetWarpParams().AbortCooldown);
	}
}

void AHCJShipCharacter::OnRep_WarpState(uint8 OldState)
{
	// Sounds and flash are driven by TickSystemEffects from GetWarpPhase(), which on the owning client is its own
	// prediction; this is the log evidence that the state of another ship arrived.
	if (GetNetMode() == NM_DedicatedServer || OldState == WarpStateRep)
	{
		return;
	}
	static const TCHAR* Names[] = { TEXT("idle"), TEXT("charging"), TEXT("at warp"), TEXT("cooling down") };
	UE_LOG(LogHCJ, Verbose, TEXT("Warp: drive of %s%s %s -> %s"), *GetName(), IsLocalPlayersShip(*this) ? TEXT(" (own ship)") : TEXT(""),
		Names[FMath::Min<int32>(OldState, 3)], Names[FMath::Min<int32>(WarpStateRep, 3)]);
}

// ---------------------------------------------------------------------------------------------------------------------
// Cloaking device
// ---------------------------------------------------------------------------------------------------------------------

void AHCJShipCharacter::ToggleCloak()
{
	SetCloaking(HasAuthority() ? !(bWantsCloak || Cloak.bCloaked) : !IsCloaked());
}

void AHCJShipCharacter::SetCloaking(bool bNewCloaking)
{
	if (bIsDead)
	{
		return;
	}
	if (HasAuthority())
	{
		ServerSetCloak_Implementation(bNewCloaking);
	}
	else if (IsLocallyControlled())
	{
		ServerSetCloak(bNewCloaking);
	}
}

void AHCJShipCharacter::ServerSetCloak_Implementation(bool bNewCloaking)
{
	// Raising it needs charge; the meter and the drop are ServerTickSystems.
	bWantsCloak = bNewCloaking && !bIsDead && (Cloak.bCloaked || Cloak.CanEngage());
}

float AHCJShipCharacter::GetCloakEnergy() const
{
	if (HasAuthority())
	{
		return Cloak.Energy;
	}
	float Energy = 0.0f;
	bool bLocked = false;
	HCJCloak::UnpackEnergy(CloakEnergyRep, Energy, bLocked);
	return Energy;
}

bool AHCJShipCharacter::IsCloakLocked() const
{
	return HasAuthority() ? Cloak.bLocked : (CloakEnergyRep & 0x80) != 0;
}

bool AHCJShipCharacter::ServerWeaponsBlocked() const
{
	const UWorld* World = GetWorld();
	const UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	return Cloak.bCloaked || (World != nullptr && !HCJCloak::CanFire(false, static_cast<float>(World->GetTimeSeconds()) - DecloakTime))
		|| (ShipMovement != nullptr && ShipMovement->GetWarpDrive().IsBusy());
}

bool AHCJShipCharacter::AreWeaponsOffline() const
{
	if (HasAuthority())
	{
		return ServerWeaponsBlocked();
	}
	const UWorld* World = GetWorld();
	const EHCJWarpPhase Phase = GetWarpPhase();
	return IsCloaked() || Phase == EHCJWarpPhase::Charging || Phase == EHCJWarpPhase::Active
		|| (World != nullptr && static_cast<float>(World->GetTimeSeconds()) - LocalDecloakTime < HCJCloak::WeaponDelay);
}

// ---------------------------------------------------------------------------------------------------------------------
// Tractor beam
// ---------------------------------------------------------------------------------------------------------------------

void AHCJShipCharacter::SetTractoring(bool bNewTractoring)
{
	const bool bEffective = bNewTractoring && !bIsDead;
	if (HasAuthority())
	{
		bWantsTractor = bEffective;
	}
	else if (IsLocallyControlled() && bLocalWantsTractor != bEffective)
	{
		bLocalWantsTractor = bEffective;
		ServerSetTractor(bEffective);
	}
}

void AHCJShipCharacter::ServerSetTractor_Implementation(bool bNewTractoring)
{
	bWantsTractor = bNewTractoring && !bIsDead;
}

bool AHCJShipCharacter::HasClearLineTo(const AHCJShipCharacter& Other) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HCJTractorSight), false, this);
	Params.AddIgnoredActor(&Other);
	return !World->LineTraceTestByObjectType(GetActorLocation(), Other.GetActorLocation(), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
}

void AHCJShipCharacter::ServerReleaseTractor(const TCHAR* Why, bool bBroken)
{
	AHCJShipCharacter* Held = TractorTarget;
	if (!HasAuthority() || Held == nullptr)
	{
		return;
	}
	TractorTarget = nullptr;
	if (IsValid(Held) && Held->TractorHolder == this)
	{
		Held->TractorHolder = nullptr;
		Held->OnRep_TractorHolder();
		Held->ForceNetUpdate();
	}
	const UWorld* World = GetWorld();
	TractorReadyTime = (World != nullptr ? static_cast<float>(World->GetTimeSeconds()) : 0.0f) + (bBroken ? HCJTractor::BreakCooldown : 0.5f);
	UE_LOG(LogHCJ, Verbose, TEXT("Tractor: %s lets go of %s (%s)"), *GetName(), *GetNameSafe(Held), Why);
	if (AHCJGameMode* GameMode = World != nullptr ? World->GetAuthGameMode<AHCJGameMode>() : nullptr)
	{
		GameMode->NotifyTractorRelease(bBroken);
	}
	OnRep_TractorTarget();
	ForceNetUpdate();
}

void AHCJShipCharacter::OnRep_TractorTarget()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const AHCJShipCharacter* Held = GetTractorTarget();
	UE_LOG(LogHCJ, Verbose, TEXT("Tractor: beam of %s%s %s%s"), *GetName(), IsLocalPlayersShip(*this) ? TEXT(" (own ship)") : TEXT(""),
		Held != nullptr ? TEXT("holds ") : TEXT("off"), Held != nullptr ? *Held->GetName() : TEXT(""));
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	UHCJAssetSettings::PlaySoundAt(this, Held != nullptr ? Settings.PhaserStartSounds : Settings.PhaserStopSounds, TEXT("tractor beam sound"), GetActorLocation(),
		Settings.PhaserCueVolume, 0.04f, EHCJSoundGroup::Fire, Held != nullptr ? 0.6f : 0.5f);
}

void AHCJShipCharacter::OnRep_TractorHolder()
{
	AHCJShipCharacter* Holder = GetTractorHolder();
	if (UHCJShipMovementComponent* ShipMovement = GetShipMovement())
	{
		// The pull is the holder's (its type); the movement simulation applies it on the server and in the owner's prediction.
		ShipMovement->SetTractor(Holder, Holder != nullptr ? Holder->GetTypeDef().TractorPullSpeed : 0.0f);
	}
	if (GetNetMode() != NM_DedicatedServer && IsLocalPlayersShip(*this))
	{
		UE_LOG(LogHCJ, Verbose, TEXT("Tractor: the own ship is %s%s"), Holder != nullptr ? TEXT("caught by the beam of ") : TEXT("free"), Holder != nullptr ? *Holder->GetName() : TEXT(""));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Server tick of the three systems
// ---------------------------------------------------------------------------------------------------------------------

void AHCJShipCharacter::ServerTickSystems(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	UHCJShipMovementComponent* ShipMovement = GetShipMovement();
	if (World == nullptr || ShipMovement == nullptr)
	{
		return;
	}
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const FHCJShipTypeDef& Def = GetTypeDef();
	AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>();
	const bool bBot = Cast<APlayerController>(GetController()) == nullptr;

	// ---- warp drive: the movement simulation runs it; here its consequences ----
	if (ShipMovement->ConsumeWarpImpact())
	{
		++WarpCancelCount;
		UE_LOG(LogHCJ, Verbose, TEXT("Warp: %s ran into something at warp"), *GetName());
		if (GameMode != nullptr)
		{
			GameMode->NotifyWarpImpact();
		}
		if (HealthComponent != nullptr)
		{
			HealthComponent->ApplyDamage(WarpImpactDamage, nullptr, nullptr);
		}
		if (bIsDead)
		{
			return;
		}
	}
	const uint8 NewWarpState = static_cast<uint8>(ShipMovement->GetWarpDrive().Phase);
	if (NewWarpState != WarpStateRep)
	{
		const uint8 OldState = WarpStateRep;
		WarpStateRep = NewWarpState;
		if (ShipMovement->GetWarpDrive().Phase == EHCJWarpPhase::Active)
		{
			SpawnProtectionEndTime = 0.0f;
			if (GameMode != nullptr)
			{
				GameMode->NotifyWarp(bBot);
			}
		}
		OnRep_WarpState(OldState);
		ForceNetUpdate();
	}

	// ---- held by a beam whose holder is gone ----
	if (TractorHolder != nullptr && (!IsValid(TractorHolder) || TractorHolder->IsDead() || TractorHolder->TractorTarget != this))
	{
		TractorHolder = nullptr;
		OnRep_TractorHolder();
		ForceNetUpdate();
	}

	// ---- cloaking device ----
	if (Cloak.bCloaked && (bWantsToFire || bWantsTractor))
	{
		// Firing is not possible while cloaked: the attempt drops the cloak, weapons work a moment later.
		bWantsCloak = false;
	}
	if (ShipMovement->GetWarpDrive().IsBusy() && !Cloak.bCloaked)
	{
		bWantsCloak = false;
	}
	const bool bWasCloaked = Cloak.bCloaked;
	const bool bNowCloaked = Cloak.Step(bWantsCloak, DeltaSeconds, Def.CloakSeconds, Def.CloakRechargeSeconds, HCJCloak::RelockLevel, Now < CloakFreeEndTime);
	if (!bNowCloaked)
	{
		// Dropped or ran dry: the pilot has to raise it again.
		bWantsCloak = false;
	}
	CloakEnergyRep = HCJCloak::PackEnergy(Cloak.Energy, Cloak.bLocked);
	if (bNowCloaked != bWasCloaked)
	{
		if (bNowCloaked)
		{
			PowerupFlags |= EHCJPowerup::Cloak;
			if (GameMode != nullptr)
			{
				GameMode->NotifyCloak(bBot);
			}
		}
		else
		{
			PowerupFlags &= ~(EHCJPowerup::Cloak | EHCJPowerup::CloakRevealed);
			DecloakTime = Now;
		}
		// Shields are down while cloaked and come up from nothing afterwards.
		if (HealthComponent != nullptr)
		{
			HealthComponent->SetShieldsSuppressed(bNowCloaked);
		}
		ApplyPowerupState();
		ForceNetUpdate();
	}
	if (bNowCloaked)
	{
		if (Now >= NextCloakProximityTime)
		{
			NextCloakProximityTime = Now + 0.2f;
			const FVector Location = GetActorLocation();
			for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
			{
				if (*It != this && !It->IsDead() && FVector::DistSquared(It->GetActorLocation(), Location) < FMath::Square(static_cast<double>(HCJCloak::ProximityRange)))
				{
					Cloak.Reveal(HCJCloak::ProximityRevealSeconds);
					break;
				}
			}
		}
		if (TractorHolder != nullptr)
		{
			Cloak.Reveal(HCJCloak::ProximityRevealSeconds);
		}
		const bool bRevealed = Cloak.IsRevealed();
		if (bRevealed != HasPowerup(EHCJPowerup::CloakRevealed))
		{
			PowerupFlags = bRevealed ? (PowerupFlags | EHCJPowerup::CloakRevealed) : (PowerupFlags & ~EHCJPowerup::CloakRevealed);
			if (bRevealed && GameMode != nullptr)
			{
				GameMode->NotifyCloakRevealed();
			}
			ApplyPowerupState();
			ForceNetUpdate();
		}
	}

	// ---- tractor beam (the holder's side) ----
	if (AHCJShipCharacter* Held = TractorTarget)
	{
		const TCHAR* Why = TEXT("released");
		bool bBroken = false;
		bool bKeep = bWantsTractor && IsValid(Held) && !Held->IsDead() && Held->TractorHolder == this && GetController() != nullptr;
		if (bKeep && (PhaserBank.bLocked || PhaserBank.Charge <= 0.0f))
		{
			bKeep = false;
			Why = TEXT("phaser bank empty");
		}
		if (bKeep && (ShipMovement->GetWarpDrive().IsBusy() || Cloak.bCloaked))
		{
			bKeep = false;
			Why = TEXT("warp or cloak");
		}
		if (bKeep && !HCJTractor::CanHold(GetActorLocation(), GetAimRotation().Vector(), Held->GetActorLocation(), Def.TractorRange, true))
		{
			bKeep = false;
			bBroken = true;
			Why = TEXT("out of range or out of the cone");
		}
		if (bKeep && Now >= NextTractorSightTime)
		{
			NextTractorSightTime = Now + 0.15f;
			if (!HasClearLineTo(*Held))
			{
				bKeep = false;
				bBroken = true;
				Why = TEXT("rock in between");
			}
		}
		if (bKeep)
		{
			if (const UHCJShipMovementComponent* HeldMovement = Held->GetShipMovement(); HeldMovement != nullptr && HeldMovement->IsBoostActive())
			{
				Held->TractorBoostSeconds += DeltaSeconds;
			}
			if (HCJTractor::ShouldBreak(Held->TractorBoostSeconds, TractorDamageTaken))
			{
				bKeep = false;
				bBroken = true;
				Why = Held->TractorBoostSeconds >= HCJTractor::BreakBoostSeconds ? TEXT("the held ship boosted free") : TEXT("the holder was hit");
			}
		}
		if (bKeep)
		{
			// The beam feeds on the phaser bank.
			PhaserBank.Charge = FMath::Max(0.0f, PhaserBank.Charge - DeltaSeconds / FMath::Max(Def.TractorDrainSeconds, 0.5f));
			PhaserBank.IdleSeconds = 0.0f;
			if (PhaserBank.Charge <= 0.0f)
			{
				PhaserBank.bLocked = true;
			}
			if (GameMode != nullptr)
			{
				GameMode->NotifyTractorSeconds(DeltaSeconds);
			}
		}
		else
		{
			ServerReleaseTractor(Why, bBroken);
		}
	}
	else if (bWantsTractor && Now >= TractorReadyTime && GetController() != nullptr && !ServerWeaponsBlocked() && !PhaserBank.bLocked && PhaserBank.Charge > 0.05f)
	{
		// The locked target when the beam can reach it, else the nearest ship in the cone.
		const FVector Location = GetActorLocation();
		const FVector Forward = GetAimRotation().Vector();
		const auto CanCatch = [this, &Location, &Forward, &Def](const AHCJShipCharacter* Other)
		{
			return IsValid(Other) && Other != this && !Other->IsDead() && Other->TractorHolder == nullptr && !Other->IsSpawnProtected()
				&& HCJTractor::CanHold(Location, Forward, Other->GetActorLocation(), Def.TractorRange, false) && HasClearLineTo(*Other);
		};
		AHCJShipCharacter* Candidate = LockTarget.Get();
		if (!CanCatch(Candidate))
		{
			Candidate = nullptr;
			double BestDistSq = TNumericLimits<double>::Max();
			for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
			{
				const double DistSq = FVector::DistSquared(It->GetActorLocation(), Location);
				if (DistSq < BestDistSq && !It->IsCloakHidden() && CanCatch(*It))
				{
					BestDistSq = DistSq;
					Candidate = *It;
				}
			}
		}
		if (Candidate != nullptr)
		{
			TractorTarget = Candidate;
			TractorDamageTaken = 0.0f;
			NextTractorSightTime = Now + 0.15f;
			SpawnProtectionEndTime = 0.0f;
			Candidate->TractorHolder = this;
			Candidate->TractorBoostSeconds = 0.0f;
			// A beam reveals a cloaked ship and stops a warp drive.
			Candidate->Cloak.Reveal(HCJCloak::ProximityRevealSeconds);
			Candidate->ServerCancelWarp(TEXT("caught by a tractor beam"));
			Candidate->OnRep_TractorHolder();
			Candidate->ForceNetUpdate();
			UE_LOG(LogHCJ, Verbose, TEXT("Tractor: %s catches %s at %.0f uu"), *GetName(), *Candidate->GetName(), FVector::Dist(Location, Candidate->GetActorLocation()));
			if (GameMode != nullptr)
			{
				GameMode->NotifyTractorHold(bBot);
			}
			OnRep_TractorTarget();
			ForceNetUpdate();
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Looks of the three systems (every machine that draws)
// ---------------------------------------------------------------------------------------------------------------------

void AHCJShipCharacter::TickSystemEffects(float Now, float DeltaSeconds)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const bool bOwn = IsLocalPlayersShip(*this);
	const FVector Location = GetActorLocation();

	// ---- warp: cues when the phase changes on this machine ----
	const EHCJWarpPhase Phase = bIsDead ? EHCJWarpPhase::Idle : GetWarpPhase();
	if (static_cast<uint8>(Phase) != WarpPhaseLook)
	{
		const EHCJWarpPhase Old = static_cast<EHCJWarpPhase>(WarpPhaseLook);
		WarpPhaseLook = static_cast<uint8>(Phase);
		WarpPhaseLookTime = Now;
		if (!bIsDead && !bCloakLook)
		{
			if (Phase == EHCJWarpPhase::Charging)
			{
				UHCJAssetSettings::PlaySoundAt(this, Settings.PhaserStartSounds, TEXT("warp charge sound"), Location, Settings.PhaserCueVolume, 0.0f, EHCJSoundGroup::World, 0.45f);
			}
			else if (Phase == EHCJWarpPhase::Active || Old == EHCJWarpPhase::Active)
			{
				// The flash where the ship jumps and where it drops out, for everybody who looks.
				const bool bEntry = Phase == EHCJWarpPhase::Active;
				// (Not for the own ship: its camera sits inside the flare, which would white out the view. Its pilot gets the streaks.)
				if (UHCJEffectsDirector* Effects = bOwn ? nullptr : UHCJEffectsDirector::Get(this))
				{
					Effects->PlayWarpFlash(Location, false);
				}
				UHCJAssetSettings::PlaySoundAt(this, Settings.PortalSounds, TEXT("warp sound"), Location, Settings.PortalVolume, 0.03f, EHCJSoundGroup::World, bEntry ? 1.6f : 0.7f);
				UE_LOG(LogHCJ, Verbose, TEXT("Warp: %s%s %s%s"), *GetName(), bOwn ? TEXT(" (own ship)") : TEXT(""), bEntry ? TEXT("jumps to warp") : TEXT("drops out of warp"), bOwn ? TEXT("") : TEXT(" (flash shown)"));
			}
		}
	}

	// ---- warp: streaks in front of the own camera ----
	if (bOwn && (Phase == EHCJWarpPhase::Charging || Phase == EHCJWarpPhase::Active) && WarpStreakMesh == nullptr && Camera != nullptr)
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* StreakBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.WarpStreakMaterial, TEXT("warp streaks"));
		if (Plane != nullptr && StreakBase != nullptr)
		{
			WarpStreakMaterial = UMaterialInstanceDynamic::Create(StreakBase, this);
			WarpStreakMaterial->SetVectorParameterValue(TEXT("Color"), WarpColor);
			WarpStreakMesh = NewObject<UStaticMeshComponent>(this, TEXT("WarpStreakMesh"));
			WarpStreakMesh->SetupAttachment(Camera);
			WarpStreakMesh->SetStaticMesh(Plane);
			WarpStreakMesh->SetMaterial(0, WarpStreakMaterial);
			WarpStreakMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			WarpStreakMesh->SetGenerateOverlapEvents(false);
			WarpStreakMesh->SetCastShadow(false);
			WarpStreakMesh->SetReceivesDecals(false);
			// 80 uu in front of the lens, turned to it, wide enough for the widest view.
			WarpStreakMesh->SetRelativeLocationAndRotation(FVector(80.0, 0.0, 0.0), FRotator(90.0, 0.0, 0.0));
			WarpStreakMesh->SetRelativeScale3D(FVector(2.4, 3.8, 1.0));
			WarpStreakMesh->SetVisibility(false);
			WarpStreakMesh->RegisterComponent();
		}
	}
	if (WarpStreakMesh != nullptr)
	{
		const float Strength = !bOwn ? 0.0f : Phase == EHCJWarpPhase::Active ? 1.0f
			: Phase == EHCJWarpPhase::Charging ? 0.3f * FMath::Clamp((Now - WarpPhaseLookTime) / 1.6f, 0.0f, 1.0f) : 0.0f;
		const bool bShow = Strength > 0.01f;
		if (bShow)
		{
			WarpStreakMaterial->SetScalarParameterValue(TEXT("Strength"), Strength);
		}
		if (WarpStreakMesh->IsVisible() != bShow)
		{
			WarpStreakMesh->SetVisibility(bShow);
		}
	}

	// ---- tractor beam: a quad from the holder to the ship it holds ----
	const AHCJShipCharacter* Held = bIsDead ? nullptr : GetTractorTarget();
	if (Held != nullptr && TractorBeamMesh == nullptr)
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* BeamBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PhaserBeamMaterial, TEXT("tractor beam"));
		if (Plane != nullptr && BeamBase != nullptr)
		{
			TractorBeamMaterial = UMaterialInstanceDynamic::Create(BeamBase, this);
			TractorBeamMaterial->SetVectorParameterValue(TEXT("Color"), TractorColor);
			TractorBeamMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.PhaserBeamBrightness * 0.8f);
			TractorBeamMaterial->SetScalarParameterValue(TEXT("Phase"), static_cast<float>(GetUniqueID() % 71));
			TractorBeamMesh = NewObject<UStaticMeshComponent>(this, TEXT("TractorBeamMesh"));
			TractorBeamMesh->SetupAttachment(GetCapsuleComponent());
			TractorBeamMesh->SetUsingAbsoluteLocation(true);
			TractorBeamMesh->SetUsingAbsoluteRotation(true);
			TractorBeamMesh->SetUsingAbsoluteScale(true);
			TractorBeamMesh->SetStaticMesh(Plane);
			TractorBeamMesh->SetMaterial(0, TractorBeamMaterial);
			TractorBeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			TractorBeamMesh->SetGenerateOverlapEvents(false);
			TractorBeamMesh->SetCastShadow(false);
			TractorBeamMesh->SetReceivesDecals(false);
			TractorBeamMesh->SetVisibility(false);
			TractorBeamMesh->RegisterComponent();
		}
	}
	if (TractorBeamMesh != nullptr)
	{
		const bool bShow = Held != nullptr;
		if (bShow)
		{
			// Both ends as this machine draws the ships (the smoothed mesh of a ship flown elsewhere).
			const auto DrawnLocation = [](const AHCJShipCharacter& Ship)
			{
				return (Ship.GetController() == nullptr && Ship.GetMesh() != nullptr) ? Ship.GetMesh()->GetComponentLocation() : Ship.GetActorLocation();
			};
			const FVector Start = DrawnLocation(*this) + GetActorForwardVector() * (0.3f * MuzzleOffset);
			const FVector End = DrawnLocation(*Held);
			const FVector Along = End - Start;
			const double Length = Along.Size();
			FVector ViewLocation = Start;
			FRotator ViewRotation = FRotator::ZeroRotator;
			if (const APlayerController* LocalController = GEngine != nullptr ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr)
			{
				LocalController->GetPlayerViewPoint(ViewLocation, ViewRotation);
			}
			if (Length > 1.0)
			{
				const FVector Middle = (Start + End) * 0.5;
				TractorBeamMesh->SetWorldLocationAndRotation(Middle, FRotationMatrix::MakeFromXZ(Along, ViewLocation - Middle).Rotator());
				// Wider than a phaser beam, and it breathes.
				const double Width = Settings.PhaserBeamWidth * (2.2 + 0.5 * FMath::Sin(Now * 9.0f));
				TractorBeamMesh->SetWorldScale3D(FVector(Length / 100.0, Width / 100.0, 1.0));
				TractorBeamMaterial->SetScalarParameterValue(TEXT("Length"), static_cast<float>(Length));
			}
		}
		if (TractorBeamMesh->IsVisible() != bShow)
		{
			TractorBeamMesh->SetVisibility(bShow);
		}
	}

	// ---- cloak: who sees what (the revealed bit and the local controller can change without a flag change) ----
	if (IsCloaked() || CloakSkinState != 0)
	{
		ApplyCloakLook();
		if (CloakMaterial != nullptr && CloakSkinState > 0)
		{
			// Another ship: a faint shimmer. The own ship: half transparent. Revealed: a strong flicker.
			const float Opacity = CloakSkinState == 2 ? 0.5f : CloakSkinState == 3 ? (FMath::Frac(Now * 13.0f) < 0.5f ? 0.85f : 0.3f) : 0.1f;
			CloakMaterial->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		}
	}
}
