#include "World/HCJPickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
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

	/** Height of the item above the pad and its size (uu). */
	constexpr float ItemHeight = 170.0f;
	constexpr float ItemSize = 150.0f;
	constexpr float PadDiameter = 420.0f;
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
	if (bActive && ItemMesh != nullptr)
	{
		const float Time = static_cast<float>(GetWorld()->GetTimeSeconds()) + static_cast<float>(GetUniqueID() % 64);
		ItemMesh->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, ItemHeight + 25.0f * FMath::Sin(Time * 2.0f)),
			FRotator(20.0f, FMath::Fmod(Time * 90.0f, 360.0f), 0.0f));
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

void AHCJPickup::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (HasAuthority())
	{
		ServerTryCollect(Cast<AHCJShipCharacter>(OtherActor));
	}
}

void AHCJPickup::ServerTryCollect(AHCJShipCharacter* Ship)
{
	if (!HasAuthority() || !bActive || !IsValid(Ship) || Ship->IsDead())
	{
		return;
	}
	const EHCJPickupItem Collected = GetItem();
	if (!Ship->ServerGiveItem(Collected))
	{
		return;
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
	Apply(PadMaterial, bActive ? Color : FLinearColor(0.25f, 0.25f, 0.28f), bActive ? Settings.PickupPadBrightness : 0.3f * Settings.PickupPadBrightness);
}
