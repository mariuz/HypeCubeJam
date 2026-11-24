#include "World/HCJPickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include "Assets/HCJAssetSettings.h"
#include "Game/HCJGameMode.h"
#include "Game/HCJGameplaySettings.h"
#include "HypeCubeJam.h"
#include "Ship/HCJShipCharacter.h"
#include "World/HCJSectorLayout.h"

namespace
{
	TArray<TWeakObjectPtr<AHCJPickup>> GAllPickups;

	/** Height of the item above the pad and its size (uu). (Item 38: 170 / 150 / 420 before - half as big again.) */
	constexpr float ItemHeight = 240.0f;
	constexpr float ItemSize = 230.0f;
	constexpr float PadDiameter = 620.0f;

	/**
	 * Where the local player looks from, asked once per frame for all pads. False when there is no local view (a
	 * dedicated server never gets here).
	 */
	bool GetLocalView(const UWorld* World, FVector& OutLocation, FRotator& OutRotation)
	{
		static uint64 CachedFrame = MAX_uint64;
		static TWeakObjectPtr<const UWorld> CachedWorld;
		static FVector CachedLocation = FVector::ZeroVector;
		static FRotator CachedRotation = FRotator::ZeroRotator;
		static bool bCachedValid = false;
		if (CachedFrame != GFrameCounter || CachedWorld.Get() != World)
		{
			CachedFrame = GFrameCounter;
			CachedWorld = World;
			const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			bCachedValid = Controller != nullptr && Controller->IsLocalController();
			if (bCachedValid)
			{
				Controller->GetPlayerViewPoint(CachedLocation, CachedRotation);
			}
		}
		OutLocation = CachedLocation;
		OutRotation = CachedRotation;
		return bCachedValid;
	}
}

float HCJPickupLook::BeamWidthAt(float ViewDistance)
{
	return FMath::Clamp(FMath::Max(ViewDistance, 0.0f) * BeamAngularWidth, BeamWidth, BeamMaxWidth);
}

float HCJPickupLook::HaloEdgeAt(float ViewDistance)
{
	return FMath::Clamp(FMath::Max(ViewDistance, 0.0f) * HaloAngularEdge, HaloEdge, HaloMaxEdge);
}

float HCJPickupLook::HaloPulse(float TimeSeconds, float Phase)
{
	return 0.9f + 0.1f * FMath::Sin(TimeSeconds * 3.0f + Phase);
}

AHCJPickup::AHCJPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(320.0f);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AHCJPickup::OnTriggerBeginOverlap);
	SetRootComponent(Trigger);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	const auto MakeMesh = [this](const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Trigger);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		if (Material != nullptr)
		{
			Component->SetMaterial(0, Material);
		}
		return Component;
	};

	PadMesh = MakeMesh(TEXT("PadMesh"), CylinderMesh.Object, ShapeMaterial.Object);
	PadMesh->SetRelativeScale3D(FVector(PadDiameter / HCJ::BasicShapeSize, PadDiameter / HCJ::BasicShapeSize, 0.12f));
	ItemMesh = MakeMesh(TEXT("ItemMesh"), CubeMesh.Object, ShapeMaterial.Object);
	ItemMesh->SetRelativeLocation(FVector(0.0f, 0.0f, ItemHeight));
	// The light column (a cylinder standing on the pad) and the halo (a quad that Tick turns to the view): hidden until a filled pad is in view.
	BeamMesh = MakeMesh(TEXT("BeamMesh"), CylinderMesh.Object, ShapeMaterial.Object);
	BeamMesh->SetUsingAbsoluteRotation(true);
	BeamMesh->SetUsingAbsoluteScale(true);
	BeamMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 0.5f * HCJPickupLook::BeamHeight));
	BeamMesh->SetRelativeScale3D(FVector(HCJPickupLook::BeamWidth / HCJ::BasicShapeSize, HCJPickupLook::BeamWidth / HCJ::BasicShapeSize, HCJPickupLook::BeamHeight / HCJ::BasicShapeSize));
	BeamMesh->SetVisibility(false);
	HaloMesh = MakeMesh(TEXT("HaloMesh"), PlaneMesh.Object, nullptr);
	HaloMesh->SetUsingAbsoluteRotation(true);
	HaloMesh->SetUsingAbsoluteScale(true);
	HaloMesh->SetRelativeLocation(FVector(0.0f, 0.0f, ItemHeight));
	HaloMesh->SetVisibility(false);

	bReplicates = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(2.0f);
	SetMinNetUpdateFrequency(1.0f);
	SetNetCullDistanceSquared(FMath::Square(HCJ::NetCullDistance));
}

const TArray<TWeakObjectPtr<AHCJPickup>>& AHCJPickup::GetAll()
{
	return GAllPickups;
}

void AHCJPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHCJPickup, Item);
	DOREPLIFETIME(AHCJPickup, bActive);
}

void AHCJPickup::BeginPlay()
{
	Super::BeginPlay();

	GAllPickups.RemoveAll([](const TWeakObjectPtr<AHCJPickup>& Entry) { return !Entry.IsValid(); });
	GAllPickups.Add(this);

	Trigger->SetSphereRadius(FMath::Max(UHCJGameplaySettings::Get().CollectRadius, 100.0f));

	if (GetNetMode() != NM_DedicatedServer)
	{
		const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
		UMaterialInterface* Glow = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PickupMaterial, TEXT("pickup glow material"));
		bGlowMaterial = Glow != nullptr;
		if (Glow != nullptr)
		{
			PadMesh->SetMaterial(0, Glow);
			ItemMesh->SetMaterial(0, Glow);
		}
		PadMaterial = PadMesh->CreateDynamicMaterialInstance(0);
		ItemMaterial = ItemMesh->CreateDynamicMaterialInstance(0);
		// The column glows like the item; the halo is the glint of the ships without its spikes. Without the glow
		// material a column would be a grey pole, without the glint material there is no halo: each is then left out.
		if (Glow != nullptr)
		{
			BeamMesh->SetMaterial(0, Glow);
			BeamMaterial = BeamMesh->CreateDynamicMaterialInstance(0);
		}
		if (UMaterialInterface* Glint = UHCJAssetSettings::Load<UMaterialInterface>(Settings.ShipGlintMaterial, TEXT("pickup halo material")))
		{
			HaloMaterial = UMaterialInstanceDynamic::Create(Glint, this);
			HaloMaterial->SetScalarParameterValue(TEXT("Spikes"), 0.35f);
			HaloMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.PickupHaloBrightness);
			HaloMaterial->SetScalarParameterValue(TEXT("Backing"), Settings.PickupHaloBacking);
			HaloMesh->SetMaterial(0, HaloMaterial);
		}
		bVisualsReady = true;
		// A pad that arrives (late join, coming into range) shows its state without a sound.
		bShownActive = bActive;
		RefreshVisuals();
		SetActorTickEnabled(true);
	}
	else
	{
		PadMesh->SetVisibility(false);
		ItemMesh->SetVisibility(false);
		BeamMesh->SetVisibility(false);
		HaloMesh->SetVisibility(false);
	}
}

void AHCJPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GAllPickups.Remove(this);
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefillTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void AHCJPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Cosmetic: the item turns and bobs.
	const float Time = static_cast<float>(GetWorld()->GetTimeSeconds()) + static_cast<float>(GetUniqueID() % 64);
	if (bActive && ItemMesh != nullptr)
	{
		ItemMesh->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, ItemHeight + 25.0f * FMath::Sin(Time * 2.0f)),
			FRotator(20.0f, FMath::Fmod(Time * 90.0f, 360.0f), 0.0f));
	}
	TickBeacon(Time);
}

void AHCJPickup::TickBeacon(float Now)
{
	if (BeamMesh == nullptr || HaloMesh == nullptr || (BeamMaterial == nullptr && HaloMaterial == nullptr))
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	const FVector ItemLocation = GetActorLocation() + FVector(0.0, 0.0, ItemHeight);
	const bool bView = bActive && GetLocalView(GetWorld(), ViewLocation, ViewRotation);
	const double DistanceSq = bView ? FVector::DistSquared(ViewLocation, ItemLocation) : 0.0;
	const bool bShow = bView && DistanceSq < FMath::Square(static_cast<double>(HCJPickupLook::MaxViewDistance));
	if (bShow != bBeaconShown)
	{
		bBeaconShown = bShow;
		BeamMesh->SetVisibility(bShow && BeamMaterial != nullptr);
		HaloMesh->SetVisibility(bShow && HaloMaterial != nullptr);
		ShownBeamWidth = 0.0f;
	}
	if (!bShow)
	{
		return;
	}
	const float Distance = static_cast<float>(FMath::Sqrt(DistanceSq));
	if (BeamMaterial != nullptr)
	{
		// (The column is round: it needs no turning, and its width only changes with the distance.)
		const float Width = HCJPickupLook::BeamWidthAt(Distance);
		if (FMath::Abs(Width - ShownBeamWidth) > 0.03f * ShownBeamWidth)
		{
			ShownBeamWidth = Width;
			BeamMesh->SetWorldScale3D(FVector(Width / HCJ::BasicShapeSize, Width / HCJ::BasicShapeSize, HCJPickupLook::BeamHeight / HCJ::BasicShapeSize));
		}
	}
	if (HaloMaterial != nullptr && Distance > 1.0f)
	{
		const float Edge = HCJPickupLook::HaloEdgeAt(Distance) * HCJPickupLook::HaloPulse(Now, 0.0f);
		const FVector ToView = (ViewLocation - ItemLocation) / Distance;
		HaloMesh->SetWorldScale3D(FVector(Edge / HCJ::BasicShapeSize));
		HaloMesh->SetWorldRotation(FRotationMatrix::MakeFromZX(ToView, FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y)).Rotator());
	}
}

void AHCJPickup::ServerFill()
{
	if (!HasAuthority())
	{
		return;
	}
	AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>();
	Item = static_cast<uint8>(GameMode != nullptr ? GameMode->ChoosePickupItem() : EHCJPickupItem::Repair);
	bActive = true;
	ForceNetUpdate();
	OnRep_State();

	// Somebody may be sitting on the pad already.
	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, AHCJShipCharacter::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		ServerTryCollect(Cast<AHCJShipCharacter>(Actor));
		if (!bActive)
		{
			break;
		}
	}
}

void AHCJPickup::ServerDebugSetItem(EHCJPickupItem NewItem)
{
#if !UE_BUILD_SHIPPING
	if (HasAuthority() && NewItem < EHCJPickupItem::Count)
	{
		GetWorld()->GetTimerManager().ClearTimer(RefillTimer);
		Item = static_cast<uint8>(NewItem);
		bActive = true;
		ForceNetUpdate();
		OnRep_State();
	}
#endif
}

void AHCJPickup::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (HasAuthority())
	{
		ServerTryCollect(Cast<AHCJShipCharacter>(OtherActor));
	}
}

bool AHCJPickup::ServerTryCollect(AHCJShipCharacter* Ship)
{
	if (!HasAuthority() || !bActive || !IsValid(Ship) || Ship->IsDead())
	{
		return false;
	}
	const EHCJPickupItem Collected = GetItem();
	if (!Ship->ServerGiveItem(Collected))
	{
		return false;
	}

	bActive = false;
	ForceNetUpdate();
	OnRep_State();

	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	UWorld* World = GetWorld();
	AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>();
	if (GameMode != nullptr)
	{
		GameMode->NotifyPickupCollected(Collected, Ship->GetController());
	}
	const float Delay = GameMode != nullptr
		? GameMode->RandomPickupDelay()
		: 0.5f * (Gameplay.RespawnMinSeconds + Gameplay.RespawnMaxSeconds);
	World->GetTimerManager().SetTimer(RefillTimer, this, &AHCJPickup::ServerFill, FMath::Max(Delay, 1.0f), false);
	return true;
}

void AHCJPickup::OnRep_State()
{
	if (!bVisualsReady)
	{
		return;
	}
	if (bActive != bShownActive)
	{
		const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
		const bool bWeapon = HCJ::ItemToWeapon(GetItem()) != EHCJWeapon::Count;
		if (bActive)
		{
			UHCJAssetSettings::PlaySoundAt(this, Settings.PickupSpawnSounds, TEXT("pickup refill sound"), GetActorLocation(), Settings.PickupVolume * 0.6f, 0.05f, EHCJSoundGroup::World);
		}
		else
		{
			UHCJAssetSettings::PlaySoundAt(this, bWeapon ? Settings.PickupWeaponSounds : Settings.PickupPowerupSounds, TEXT("pickup sound"),
				GetActorLocation(), Settings.PickupVolume, 0.05f, EHCJSoundGroup::World);
		}
		bShownActive = bActive;
	}
	RefreshVisuals();
}

void AHCJPickup::RefreshVisuals()
{
	if (!bVisualsReady)
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const EHCJPickupItem Shown = GetItem();
	const FLinearColor Color = HCJ::GetItemColor(Shown);
	// Pad, column and halo say what KIND of thing lies there (health, weapon, power-up); the item has its own colour.
	const FLinearColor KindColor = HCJ::GetItemKindColor(HCJ::GetItemKind(Shown));
	const bool bWeapon = HCJ::ItemToWeapon(Shown) != EHCJWeapon::Count;

	ItemMesh->SetVisibility(bActive);
	// Power-ups are cubes standing on a corner, weapons are long boxes: readable from far without any asset.
	ItemMesh->SetRelativeScale3D(bWeapon
		? FVector(2.2f, 0.45f, 0.45f) * (ItemSize / HCJ::BasicShapeSize)
		: FVector(1.0f) * (ItemSize / HCJ::BasicShapeSize));

	const auto Apply = [&Settings, this](UMaterialInstanceDynamic* Material, const FLinearColor& Tint, float Brightness)
	{
		if (Material == nullptr)
		{
			return;
		}
		if (bGlowMaterial)
		{
			if (!Settings.PickupColorParameter.IsNone())
			{
				Material->SetVectorParameterValue(Settings.PickupColorParameter, Tint);
			}
			if (!Settings.PickupBrightnessParameter.IsNone())
			{
				Material->SetScalarParameterValue(Settings.PickupBrightnessParameter, Brightness);
			}
		}
		else
		{
			Material->SetVectorParameterValue(TEXT("Color"), Tint * FMath::Min(Brightness, 1.0f));
		}
	};
	Apply(ItemMaterial, Color, Settings.PickupItemBrightness);
	// The pad shows the colour of what lies on it, and goes dark grey while it is empty.
	Apply(PadMaterial, bActive ? KindColor : FLinearColor(0.25f, 0.25f, 0.28f), bActive ? Settings.PickupPadBrightness : 0.3f * Settings.PickupPadBrightness);
	Apply(BeamMaterial, KindColor, Settings.PickupBeamBrightness);
	if (HaloMaterial != nullptr)
	{
		HaloMaterial->SetVectorParameterValue(TEXT("Color"), KindColor);
	}
	if (!bActive && bBeaconShown)
	{
		// (Tick shows them again when the pad is refilled.)
		bBeaconShown = false;
		BeamMesh->SetVisibility(false);
		HaloMesh->SetVisibility(false);
	}
}
