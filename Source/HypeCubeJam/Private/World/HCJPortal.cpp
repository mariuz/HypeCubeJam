#include "World/HCJPortal.h"
#include "World/HCJWormholes.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"

#include "Assets/HCJAssetSettings.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HypeCubeJam.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Ship/HCJShipCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "World/HCJSectorLayout.h"

namespace
{
	const FName HCJPortalColorParameter(TEXT("Color"));
	const FName HCJPortalRoughnessParameter(TEXT("Roughness"));

	constexpr int32 HCJPortalFrameSegments = 28;
	constexpr float HCJPortalDiscThickness = 30.0f;
	/** The ring: how far its middle line lies outside the disc, how thick it is across and along the portal axis. */
	constexpr float HCJPortalRingOffset = 95.0f;
	constexpr float HCJPortalRingWidth = 170.0f;
	constexpr float HCJPortalRingDepth = 150.0f;
	constexpr int32 HCJPortalGlyphs = 14;
	/** Seconds the flash of a passage lasts, and the distance from the local view beyond which a portal does nothing per frame. */
	constexpr float HCJPortalFlashSeconds = 0.9f;
	constexpr double HCJPortalTickDistance = 70000.0;

	TArray<TWeakObjectPtr<AHCJPortal>>& AllPortals()
	{
		static TArray<TWeakObjectPtr<AHCJPortal>> Portals;
		return Portals;
	}
}

void AHCJPortal::NotifyPassage(const UObject* WorldContextObject, const FVector& Location, float MaxDistance)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	AHCJPortal* Best = nullptr;
	double BestDistSq = FMath::Square(double(MaxDistance));
	TArray<TWeakObjectPtr<AHCJPortal>>& Portals = AllPortals();
	for (int32 Index = Portals.Num() - 1; Index >= 0; --Index)
	{
		AHCJPortal* Portal = Portals[Index].Get();
		if (Portal == nullptr)
		{
			Portals.RemoveAtSwap(Index);
			continue;
		}
		const double DistSq = FVector::DistSquared(Portal->GetActorLocation(), Location);
		if (Portal->GetWorld() == World && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Portal;
		}
	}
	if (Best != nullptr)
	{
		Best->FlashTime = static_cast<float>(World->GetTimeSeconds());
		UE_LOG(LogHCJ, Verbose, TEXT("Portal: passage flash on the %s of sector %d (to %d)"), Best->bWormhole ? TEXT("wormhole mouth") : TEXT("portal"), Best->SectorId, Best->DestSector);
	}
}

AHCJPortal::AHCJPortal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// After the camera, so that what faces the view faces this frame's view.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;
	SetCanBeDamaged(false);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(HCJ::PortalTriggerRadius);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AHCJPortal::OnTriggerBeginOverlap);
	RootComponent = Trigger;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	// The cylinder's axis is its local Z; a 90 degree pitch lays it along the actor X axis (the outward direction).
	const float DiscScale = 2.0f * HCJ::PortalVisualRadius / HCJ::BasicShapeSize;
	DiscMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DiscMesh"));
	DiscMesh->SetupAttachment(Trigger);
	DiscMesh->SetStaticMesh(CylinderFinder.Object);
	DiscMesh->SetMaterial(0, MaterialFinder.Object);
	DiscMesh->SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
	DiscMesh->SetRelativeScale3D(FVector(DiscScale, DiscScale, HCJPortalDiscThickness / HCJ::BasicShapeSize));
	DiscMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DiscMesh->SetGenerateOverlapEvents(false);
	DiscMesh->SetCanEverAffectNavigation(false);
	DiscMesh->SetCastShadow(false);

	FrameMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(Trigger);
	FrameMesh->SetStaticMesh(CubeFinder.Object);
	FrameMesh->SetMaterial(0, MaterialFinder.Object);
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FrameMesh->SetGenerateOverlapEvents(false);
	FrameMesh->SetCanEverAffectNavigation(false);
	FrameMesh->SetCastShadow(false);
}

void AHCJPortal::InitPortal(int32 InSectorId, int32 InDim, int32 InDestSector, const FVector& InDestWorldLocation, const FLinearColor& InTint)
{
	SectorId = InSectorId;
	Dim = InDim;
	DestSector = InDestSector;
	DestWorldLocation = InDestWorldLocation;

	// Ring: blocks in the actor's YZ plane that touch each other. Roll turns each cube's Y axis into the radial
	// direction, which leaves its Z axis tangential, so the long Z scale follows the rim.
	const float RimRadius = HCJ::PortalVisualRadius + HCJPortalRingOffset;
	if (FrameMesh->GetInstanceCount() == 0)
	{
		const float SegmentLength = 1.03f * UE_TWO_PI * RimRadius / float(HCJPortalFrameSegments);

		TArray<FTransform> Segments;
		Segments.Reserve(HCJPortalFrameSegments);
		for (int32 Index = 0; Index < HCJPortalFrameSegments; ++Index)
		{
			const double AngleDegrees = 360.0 * Index / double(HCJPortalFrameSegments);
			const FRotator Rotation(0.0, 0.0, AngleDegrees);
			// Every other block stands a little proud, so the ring has a rhythm instead of being a plain band.
			const float Proud = (Index % 2) == 0 ? 1.0f : 0.82f;
			Segments.Add(FTransform(
				Rotation,
				Rotation.RotateVector(FVector(0.0, RimRadius, 0.0)),
				FVector(HCJPortalRingDepth * Proud / HCJ::BasicShapeSize, HCJPortalRingWidth * Proud / HCJ::BasicShapeSize, SegmentLength / HCJ::BasicShapeSize)));
		}
		FrameMesh->AddInstances(Segments, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false, /*bUpdateNavigation*/ false);
	}

	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	AllPortals().Add(this);

	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	UMaterialInterface* GlowBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PortalMaterial, TEXT("portal"));
	UMaterialInterface* SwirlBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PortalSwirlMaterial, TEXT("portal swirl"));
	GlowColor = FMath::Lerp(InTint, FLinearColor::White, 0.15f);
	GlowColor.A = 1.0f;

	// The ring itself: dark metal with a breath of the destination colour (lit, BasicShapeMaterial).
	if (UMaterialInstanceDynamic* FrameMaterial = FrameMesh->CreateDynamicMaterialInstance(0))
	{
		FrameMaterial->SetVectorParameterValue(HCJPortalColorParameter, FMath::Lerp(FLinearColor(0.10f, 0.11f, 0.13f), InTint, 0.12f));
		FrameMaterial->SetScalarParameterValue(HCJPortalRoughnessParameter, 0.35f);
	}

	// Glyph blocks on both faces of the ring and a thin lip inside it, glowing in the destination colour; they turn.
	if (GlowBase != nullptr)
	{
		GlyphMesh = NewObject<UInstancedStaticMeshComponent>(this, TEXT("GlyphMesh"));
		GlyphMesh->SetupAttachment(Trigger);
		GlyphMesh->SetStaticMesh(FrameMesh->GetStaticMesh());
		GlyphMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GlyphMesh->SetGenerateOverlapEvents(false);
		GlyphMesh->SetCanEverAffectNavigation(false);
		GlyphMesh->SetCastShadow(false);
		GlyphMesh->RegisterComponent();
		TArray<FTransform> Glyphs;
		for (int32 Index = 0; Index < HCJPortalGlyphs; ++Index)
		{
			const FRotator Rotation(0.0, 0.0, 360.0 * Index / double(HCJPortalGlyphs));
			// A bar across the ring, a little deeper than it, so it shows on both faces; every other one is a short dash.
			const bool bLong = (Index % 2) == 0;
			Glyphs.Add(FTransform(Rotation, Rotation.RotateVector(FVector(0.0, RimRadius, 0.0)),
				FVector((HCJPortalRingDepth + 24.0f) / HCJ::BasicShapeSize, (bLong ? 0.62f : 0.3f) * HCJPortalRingWidth / HCJ::BasicShapeSize, (bLong ? 46.0f : 110.0f) / HCJ::BasicShapeSize)));
		}
		const float LipRadius = HCJ::PortalVisualRadius + 6.0f;
		const float LipLength = 1.03f * UE_TWO_PI * LipRadius / float(HCJPortalFrameSegments);
		for (int32 Index = 0; Index < HCJPortalFrameSegments; ++Index)
		{
			const FRotator Rotation(0.0, 0.0, 360.0 * Index / double(HCJPortalFrameSegments));
			Glyphs.Add(FTransform(Rotation, Rotation.RotateVector(FVector(0.0, LipRadius, 0.0)),
				FVector((HCJPortalRingDepth + 10.0f) / HCJ::BasicShapeSize, 14.0f / HCJ::BasicShapeSize, LipLength / HCJ::BasicShapeSize)));
		}
		GlyphMesh->AddInstances(Glyphs, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false, /*bUpdateNavigation*/ false);
		GlyphMesh->SetMaterial(0, GlowBase);
		if (UMaterialInstanceDynamic* GlyphMaterial = GlyphMesh->CreateDynamicMaterialInstance(0))
		{
			if (!Settings.PortalColorParameter.IsNone())
			{
				GlyphMaterial->SetVectorParameterValue(Settings.PortalColorParameter, GlowColor);
			}
			if (!Settings.PortalBrightnessParameter.IsNone())
			{
				GlyphMaterial->SetScalarParameterValue(Settings.PortalBrightnessParameter, Settings.PortalFrameBrightness);
			}
		}
	}

	// The inside: the swirl on a quad that fills the ring (two-sided), or the old flat glowing disc without the material.
	UStaticMesh* Plane = SwirlBase != nullptr ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")) : nullptr;
	if (Plane != nullptr)
	{
		DiscMesh->SetStaticMesh(Plane);
		// The plane's normal (its Z) along the portal axis; uniform scale, so the material's view vector in the quad's space is not skewed.
		DiscMesh->SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
		DiscMesh->SetRelativeScale3D(FVector(2.0f * (HCJ::PortalVisualRadius + 20.0f) / HCJ::BasicShapeSize));
		SwirlMaterial = UMaterialInstanceDynamic::Create(SwirlBase, this);
		SwirlMaterial->SetVectorParameterValue(TEXT("Color"), GlowColor);
		SwirlMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.PortalSwirlBrightness);
		SwirlMaterial->SetScalarParameterValue(TEXT("Twist"), 1.0f);
		SwirlMaterial->SetScalarParameterValue(TEXT("Speed"), 1.0f);
		SwirlMaterial->SetScalarParameterValue(TEXT("Flash"), 0.0f);
		DiscMesh->SetMaterial(0, SwirlMaterial);
	}
	else
	{
		if (GlowBase != nullptr)
		{
			DiscMesh->SetMaterial(0, GlowBase);
		}
		if (UMaterialInstanceDynamic* DiscMaterial = DiscMesh->CreateDynamicMaterialInstance(0))
		{
			if (GlowBase == nullptr)
			{
				DiscMaterial->SetVectorParameterValue(HCJPortalColorParameter, InTint);
				DiscMaterial->SetScalarParameterValue(HCJPortalRoughnessParameter, 0.25f);
			}
			else
			{
				if (!Settings.PortalColorParameter.IsNone())
				{
					DiscMaterial->SetVectorParameterValue(Settings.PortalColorParameter, InTint);
				}
				if (!Settings.PortalBrightnessParameter.IsNone())
				{
					DiscMaterial->SetScalarParameterValue(Settings.PortalBrightnessParameter, Settings.PortalDiscBrightness);
				}
			}
		}
	}

	SetupFarGlow(GlowColor);
	SetActorTickEnabled(true);
}

void AHCJPortal::SetupFarGlow(const FLinearColor& Color)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	UMaterialInterface* GlintBase = Settings.PortalGlowAngularSize > 0.0f ? UHCJAssetSettings::Load<UMaterialInterface>(Settings.ShipGlintMaterial, TEXT("portal far glow")) : nullptr;
	UStaticMesh* Plane = GlintBase != nullptr ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")) : nullptr;
	if (Plane == nullptr)
	{
		return;
	}
	FarGlowMaterial = UMaterialInstanceDynamic::Create(GlintBase, this);
	FarGlowMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	FarGlowMaterial->SetScalarParameterValue(TEXT("Spikes"), 1.0f);
	FarGlowMaterial->SetScalarParameterValue(TEXT("Brightness"), 0.0f);
	FarGlowMesh = NewObject<UStaticMeshComponent>(this, TEXT("FarGlowMesh"));
	FarGlowMesh->SetupAttachment(Trigger);
	FarGlowMesh->SetUsingAbsoluteRotation(true);
	FarGlowMesh->SetUsingAbsoluteScale(true);
	FarGlowMesh->SetStaticMesh(Plane);
	FarGlowMesh->SetMaterial(0, FarGlowMaterial);
	FarGlowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FarGlowMesh->SetGenerateOverlapEvents(false);
	FarGlowMesh->SetCanEverAffectNavigation(false);
	FarGlowMesh->SetCastShadow(false);
	FarGlowMesh->SetVisibility(false);
	FarGlowMesh->RegisterComponent();
}

void AHCJPortal::InitWormhole(int32 InSectorId, int32 InDestSector, const FVector& InDestWorldLocation, const FLinearColor& InTint)
{
	bWormhole = true;
	SectorId = InSectorId;
	Dim = INDEX_NONE;
	DestSector = InDestSector;
	DestWorldLocation = InDestWorldLocation;
	Trigger->SetSphereRadius(HCJ::WormholeTriggerRadius);
	FrameMesh->SetVisibility(false);

	if (GetNetMode() == NM_DedicatedServer)
	{
		DiscMesh->SetVisibility(false);
		return;
	}
	AllPortals().Add(this);

	// Core: a glowing ball half the size of the trigger, in the colour of the sector at the other end.
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const float CoreRadius = 0.5f * HCJ::WormholeTriggerRadius;
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		DiscMesh->SetStaticMesh(Sphere);
	}
	DiscMesh->SetRelativeRotation(FRotator::ZeroRotator);
	DiscMesh->SetRelativeScale3D(FVector(2.0f * CoreRadius / HCJ::BasicShapeSize));
	const FLinearColor CoreColor = FMath::Lerp(InTint, FLinearColor::White, 0.4f);
	UMaterialInterface* Glow = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PortalMaterial, TEXT("portal"));
	if (Glow != nullptr)
	{
		DiscMesh->SetMaterial(0, Glow);
	}
	CoreMaterial = DiscMesh->CreateDynamicMaterialInstance(0);
	CoreBrightness = Settings.WormholeCoreBrightness;
	if (CoreMaterial != nullptr)
	{
		if (Glow == nullptr)
		{
			CoreMaterial->SetVectorParameterValue(HCJPortalColorParameter, CoreColor);
		}
		else
		{
			if (!Settings.PortalColorParameter.IsNone())
			{
				CoreMaterial->SetVectorParameterValue(Settings.PortalColorParameter, CoreColor);
			}
			if (!Settings.PortalBrightnessParameter.IsNone())
			{
				CoreMaterial->SetScalarParameterValue(Settings.PortalBrightnessParameter, Settings.WormholeCoreBrightness);
			}
		}
	}

	// Swirl: the black hole card in the wormhole colour; its dark middle (a fifth of the card) is hidden inside the core.
	UStaticMesh* Quad = UHCJAssetSettings::Load<UStaticMesh>(Settings.BackdropQuadMesh, TEXT("backdrop card"));
	UMaterialInterface* Swirl = UHCJAssetSettings::Load<UMaterialInterface>(Settings.BlackHoleMaterial, TEXT("black hole material"));
	if (Quad != nullptr && Swirl != nullptr)
	{
		CardMesh = NewObject<UStaticMeshComponent>(this, TEXT("CardMesh"));
		CardMesh->SetupAttachment(Trigger);
		CardMesh->SetStaticMesh(Quad);
		CardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CardMesh->SetGenerateOverlapEvents(false);
		CardMesh->SetCanEverAffectNavigation(false);
		CardMesh->SetCastShadow(false);
		CardMesh->SetUsingAbsoluteRotation(true);
		const double QuadSize = 2.0 * FMath::Max(Quad->GetBoundingBox().GetExtent().GetMax(), 1.0);
		const double Width = 2.0 * (0.9 * CoreRadius) / 0.20;
		CardBaseScale = static_cast<float>(Width / QuadSize);
		CardMesh->SetRelativeScale3D(FVector(1.0, CardBaseScale, CardBaseScale));
		CardMesh->RegisterComponent();
		if (UMaterialInstanceDynamic* CardMaterial = UMaterialInstanceDynamic::Create(Swirl, this))
		{
			CardMaterial->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(Settings.WormholeColor, InTint, 0.35f));
			CardMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.WormholeBrightness);
			CardMesh->SetMaterial(0, CardMaterial);
		}
	}
	GlowColor = FMath::Lerp(Settings.WormholeColor, FLinearColor::White, 0.2f);
	GlowColor.A = 1.0f;
	SetupFarGlow(GlowColor);
	SetActorTickEnabled(true);
}

void AHCJPortal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const APlayerController* Controller = (World != nullptr && GEngine != nullptr) ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	if (Controller == nullptr)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ToView = ViewLocation - GetActorLocation();
	const double Distance = ToView.Size();
	if (Distance > HCJPortalTickDistance)
	{
		// Another sector: nothing of this portal is seen.
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const float Flash = FMath::Clamp(1.0f - (Now - FlashTime) / HCJPortalFlashSeconds, 0.0f, 1.0f);

	if (GlyphMesh != nullptr)
	{
		GlyphAngle = FMath::Fmod(GlyphAngle + DeltaSeconds * (Settings.PortalRingTurnRate + 240.0f * Flash), 360.0f);
		GlyphMesh->SetRelativeRotation(FRotator(0.0, 0.0, GlyphAngle));
	}
	if (SwirlMaterial != nullptr)
	{
		SwirlMaterial->SetScalarParameterValue(TEXT("Flash"), Flash);
	}
	if (CoreMaterial != nullptr && bWormhole && !Settings.PortalBrightnessParameter.IsNone())
	{
		// The wormhole's answer to a passage: its core flares.
		CoreMaterial->SetScalarParameterValue(Settings.PortalBrightnessParameter, CoreBrightness * (1.0f + 3.0f * Flash));
	}
	if (CardMesh != nullptr && Distance > 1.0)
	{
		// The card's face is its +X side.
		CardMesh->SetWorldRotation(ToView.Rotation());
		const float Pulse = 1.0f + 0.45f * Flash;
		CardMesh->SetRelativeScale3D(FVector(1.0, CardBaseScale * Pulse, CardBaseScale * Pulse));
	}
	if (FarGlowMesh != nullptr && FarGlowMaterial != nullptr)
	{
		// A landmark: grows in beyond PortalGlowMinDistance and keeps its size on the screen from there on.
		const float Alpha = FMath::Clamp(static_cast<float>(Distance - Settings.PortalGlowMinDistance) / FMath::Max(Settings.PortalGlowMinDistance, 1.0f), 0.0f, 1.0f);
		const bool bShow = Alpha > 0.02f || Flash > 0.02f;
		if (bShow)
		{
			const float Edge = FMath::Max(static_cast<float>(Distance) * Settings.PortalGlowAngularSize, 2.4f * HCJ::PortalVisualRadius * Flash);
			FarGlowMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.PortalGlowBrightness * FMath::Max(Alpha, Flash));
			FarGlowMesh->SetWorldScale3D(FVector(Edge / 100.0f));
			FarGlowMesh->SetWorldRotation(FRotationMatrix::MakeFromZX(ToView, FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y)).Rotator());
		}
		if (FarGlowMesh->IsVisible() != bShow)
		{
			FarGlowMesh->SetVisibility(bShow);
		}
	}
}

void AHCJPortal::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Locally spawned actor: it has authority on clients too, so the net mode decides who may teleport.
	if (GetNetMode() == NM_Client || DestSector == INDEX_NONE)
	{
		return;
	}

	AHCJShipCharacter* Ship = Cast<AHCJShipCharacter>(OtherActor);
	if (!Ship || Ship->IsDead() || OtherComp != Ship->GetRootComponent())
	{
		return;
	}

	if (Ship->RequestPortalTeleport(GetTeleportDelta(), DestSector, bWormhole))
	{
		UE_LOG(LogHCJ, Verbose, TEXT("%s enters %s %d -> %d (dimension %d)"), *GetNameSafe(Ship), bWormhole ? TEXT("wormhole") : TEXT("portal"), SectorId, DestSector, Dim);
	}
}
