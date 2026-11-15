#include "World/HCJUniverse.h"

#include "Components/BoxComponent.h"
#include "World/HCJFeatureHash.h"
#include "World/HCJPickupPads.h"

#include "Assets/HCJAssetSettings.h"
#include "Effects/HCJEffectsDirector.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/HCJGameState.h"
#include "HypeCubeJam.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "UObject/ConstructorHelpers.h"
#include "World/HCJPortal.h"
#include "World/HCJSpaceBackdrop.h"

namespace
{
	const FName HCJColorParameter(TEXT("Color"));
	const FName HCJRoughnessParameter(TEXT("Roughness"));

	/** Backdrop stars sit in a shell well outside HCJ::SectorKillRadius and well inside half the sector spacing. */
	constexpr int32 HCJNumBackdropStars = 300;
	constexpr float HCJBackdropMinDistance = 60000.0f;
	constexpr float HCJBackdropMaxDistance = 130000.0f;

	/** Rock instances are culled between these distances (uu): far beyond a sector, far below the sector spacing. */
	constexpr int32 HCJRockCullStart = 180000;
	constexpr int32 HCJRockCullEnd = 200000;
}

AHCJUniverse::AHCJUniverse()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(SceneRoot);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetRelativeRotation(FRotator(-35.0, 40.0, 0.0));
	SunLight->SetIntensity(4.0f);
	SunLight->SetLightColor(FLinearColor(1.0f, 0.95f, 0.88f));
	SunLight->SetCastShadows(true);
	// Translucent effects (Niagara smoke, flashes) are forward shaded by exactly one directional light: the sun.
	SunLight->SetForwardShadingPriority(1);

	FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetMobility(EComponentMobility::Movable);
	FillLight->SetRelativeRotation(FRotator(30.0, -140.0, 0.0));
	FillLight->SetIntensity(1.0f);
	FillLight->SetLightColor(FLinearColor(0.55f, 0.7f, 1.0f));
	FillLight->SetCastShadows(false);
	// Only one directional light may drive forward shading / atmosphere; make it the sun.
	FillLight->SetAtmosphereSunLight(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	ShapeMeshes.SetNum(int32(EHCJAsteroidShape::Count));
	ShapeMeshes[int32(EHCJAsteroidShape::Sphere)] = SphereFinder.Object;
	ShapeMeshes[int32(EHCJAsteroidShape::Cube)] = CubeFinder.Object;
	ShapeMeshes[int32(EHCJAsteroidShape::Cone)] = ConeFinder.Object;
	BaseMaterial = MaterialFinder.Object;
}

void AHCJUniverse::ApplySectorLighting(int32 Sector)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	SunLight->SetIntensity(Settings.SunIntensity);
	FillLight->SetIntensity(Settings.FillIntensity);
	if (Layouts.IsValidIndex(Sector))
	{
		FVector BackDirection;
		FLinearColor BackColor;
		AHCJSpaceBackdrop::MakeBacklight(Config.Seed, Sector, Layouts[Sector].Tint, -SunLight->GetComponentRotation().Vector(), BackDirection, BackColor);
		// The light travels from the back light into the scene.
		FillLight->SetWorldRotation((-BackDirection).Rotation());
		FillLight->SetLightColor(FMath::Lerp(BackColor, FLinearColor::White, 0.25f));
	}
}

AHCJUniverse* AHCJUniverse::Get(const UObject* WorldContextObject)
{
	const AHCJGameState* GameState = AHCJGameState::Get(WorldContextObject);
	return GameState ? GameState->GetUniverse() : nullptr;
}

void AHCJUniverse::Rebuild(const FHCJMatchConfig& InConfig)
{
	if (bBuilt && InConfig == Config)
	{
		return;
	}

	ClearGenerated();
	Config = InConfig;
	if (!Config.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const bool bCosmetics = GetNetMode() != NM_DedicatedServer;
	if (bCosmetics)
	{
		// Before anything is built or anybody fires: pulls in meshes, effects and sounds in one go.
		UHCJAssetSettings::PreloadAll();
		LoadRockMeshes();
		ApplySectorLighting(INDEX_NONE);
	}

	Graph.Build(Config.Dimension, Config.Seed, Config.EdgeProbability);

	if (bCosmetics && UHCJAssetSettings::Get().bEnableBackdrop)
	{
		if (!IsValid(Backdrop))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Owner = this;
			SpawnParameters.ObjectFlags |= RF_Transient;
			Backdrop = World->SpawnActor<AHCJSpaceBackdrop>(AHCJSpaceBackdrop::StaticClass(), FTransform::Identity, SpawnParameters);
		}
		if (Backdrop != nullptr)
		{
			// The sun of the sky stands where the key light comes from.
			Backdrop->Rebuild(Config.Seed, -SunLight->GetComponentRotation().Vector());
		}
	}
	// Real stars replace the placeholder ones.
	const bool bPlaceholderStars = bCosmetics && !(Backdrop != nullptr && Backdrop->HasStars());

	const int32 Num = Graph.NumSectors();
	Layouts.Reserve(Num);
	for (int32 Sector = 0; Sector < Num; ++Sector)
	{
		Layouts.Add(FHCJSectorLayout::Generate(Config.Seed, Sector, Graph));
	}
	Portals.SetNum(Num * FHCJHypercubeGraph::MaxDimension);

	// Black holes: derived from the finished layouts, never part of them (layout checksum unchanged).
	BlackHoles.Reserve(Num);
	int32 NumBlackHoles = 0;
	uint32 BlackHoleChecksum = 0;
	for (int32 Sector = 0; Sector < Num; ++Sector)
	{
		const FHCJBlackHole& Hole = BlackHoles.Add_GetRef(FHCJBlackHole::Make(Config.Seed, Layouts[Sector], Config.BlackHoles));
		if (Hole.bPresent)
		{
			++NumBlackHoles;
			BlackHoleChecksum = FCrc::MemCrc32(&Sector, sizeof(Sector), BlackHoleChecksum);
			BlackHoleChecksum = FCrc::MemCrc32(&Hole.LocalPosition.X, sizeof(double), BlackHoleChecksum);
			BlackHoleChecksum = FCrc::MemCrc32(&Hole.LocalPosition.Y, sizeof(double), BlackHoleChecksum);
			BlackHoleChecksum = FCrc::MemCrc32(&Hole.LocalPosition.Z, sizeof(double), BlackHoleChecksum);
		}
	}

	// Pickup pads: like the holes, derived from the finished layouts with their own hash.
	PickupPads.SetNum(Num);
	PickupPadChecksum = 0;
	int32 NumPads = 0;
	for (int32 Sector = 0; Sector < Num; ++Sector)
	{
		PickupPads[Sector] = FHCJPickupPads::Make(Config.Seed, Layouts[Sector], BlackHoles[Sector], Config.BlackHoles, Config.Pickups);
		NumPads += PickupPads[Sector].Num();
		for (const FVector& Pad : PickupPads[Sector])
		{
			PickupPadChecksum = FCrc::MemCrc32(&Sector, sizeof(Sector), PickupPadChecksum);
			PickupPadChecksum = HCJ::CrcVector(Pad, PickupPadChecksum);
		}
	}

	// Wormholes: which sectors are linked follows from the seed alone; a link whose two mouths cannot both be
	// placed in clear space is dropped. Routes over portals + wormholes are rebuilt from what is left.
	WormholeLinks = FHCJWormholes::MakeLinks(Config.Seed, Config.Dimension, Config.Wormholes);
	WormholeMouths.Init(FVector::ZeroVector, Num);
	WormholeChecksum = 0;
	for (int32 Index = WormholeLinks.Num() - 1; Index >= 0; --Index)
	{
		const FHCJWormholeLink Link = WormholeLinks[Index];
		FVector MouthA = FVector::ZeroVector;
		FVector MouthB = FVector::ZeroVector;
		if (Layouts.IsValidIndex(Link.SectorA) && Layouts.IsValidIndex(Link.SectorB)
			&& FHCJWormholes::MakeMouth(Config.Seed, Layouts[Link.SectorA], BlackHoles[Link.SectorA], Config.BlackHoles, PickupPads[Link.SectorA], MouthA)
			&& FHCJWormholes::MakeMouth(Config.Seed, Layouts[Link.SectorB], BlackHoles[Link.SectorB], Config.BlackHoles, PickupPads[Link.SectorB], MouthB))
		{
			WormholeMouths[Link.SectorA] = MouthA;
			WormholeMouths[Link.SectorB] = MouthB;
		}
		else
		{
			WormholeLinks.RemoveAt(Index);
		}
	}
	Routes.Build(Graph, WormholeLinks);
	FString WormholeText;
	for (const FHCJWormholeLink& Link : WormholeLinks)
	{
		WormholeChecksum = FCrc::MemCrc32(&Link.SectorA, sizeof(int32), WormholeChecksum);
		WormholeChecksum = FCrc::MemCrc32(&Link.SectorB, sizeof(int32), WormholeChecksum);
		WormholeChecksum = HCJ::CrcVector(WormholeMouths[Link.SectorA], WormholeChecksum);
		WormholeChecksum = HCJ::CrcVector(WormholeMouths[Link.SectorB], WormholeChecksum);
		WormholeText += FString::Printf(TEXT("%s%s-%s"), WormholeText.IsEmpty() ? TEXT("") : TEXT(", "),
			*FHCJHypercubeGraph::SectorToBinaryString(Link.SectorA, Config.Dimension), *FHCJHypercubeGraph::SectorToBinaryString(Link.SectorB, Config.Dimension));
		BuildWormholeMouths(Link);
	}

	// Space stations: placed last, clear of everything above; the asteroids in their way are not built.
	Stations.SetNum(Num);
	StationChecksum = 0;
	int32 NumStations = 0;
	int32 NumStationTurrets = 0;
	int32 NumSuppressedAsteroids = 0;
	for (int32 Sector = 0; Sector < Num; ++Sector)
	{
		const bool bMouth = Routes.GetWormholeDest(Sector) != INDEX_NONE;
		Stations[Sector] = FHCJStation::Make(Config.Seed, Layouts[Sector], BlackHoles[Sector], Config.BlackHoles, PickupPads[Sector],
			bMouth, WormholeMouths[Sector], Config.Stations);
		if (Stations[Sector].bPresent)
		{
			++NumStations;
			NumStationTurrets += Stations[Sector].Turrets.Num();
			StationChecksum = FCrc::MemCrc32(&Sector, sizeof(Sector), StationChecksum);
			StationChecksum = Stations[Sector].ComputeChecksum(StationChecksum);
			for (const FHCJAsteroid& Asteroid : Layouts[Sector].Asteroids)
			{
				NumSuppressedAsteroids += Stations[Sector].IsAsteroidSuppressed(Asteroid) ? 1 : 0;
			}
			BuildStation(Sector);
		}
	}

	int32 NumAsteroids = 0;
	for (int32 Sector = 0; Sector < Num; ++Sector)
	{
		BuildSectorGeometry(Sector);
		BuildSectorPortals(Sector);
		if (bPlaceholderStars)
		{
			BuildSectorBackdrop(Sector);
		}
		NumAsteroids += Layouts[Sector].Asteroids.Num();
	}

	LayoutChecksum = 0;
	for (int32 Sector = 0; Sector < Num; ++Sector)
	{
		const uint8 OpenMask = Graph.GetOpenDimMask(Sector);
		LayoutChecksum = FCrc::MemCrc32(&OpenMask, sizeof(OpenMask), LayoutChecksum);
		LayoutChecksum = Layouts[Sector].ComputeChecksum(LayoutChecksum);
	}

	bBuilt = true;
	BuildTime = static_cast<float>(World->GetTimeSeconds());

	UE_LOG(LogHCJ, Log, TEXT("Asteroids (%s): %d rocks in the world, destroyable %s (break into pieces from radius %.0f, health %.2f per uu of radius within %.0f..%.0f)"),
		GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"), Rocks.Num(), Config.Asteroids.bEnabled ? TEXT("yes") : TEXT("no"), Config.Asteroids.SplitMinRadius,
		Config.Asteroids.HealthPerRadius, Config.Asteroids.MinHealth, Config.Asteroids.MaxHealth);
	UE_LOG(LogHCJ, Log, TEXT("Universe built (%s): match %d, seed %d, dimension %d, edge probability %.2f -> %d sectors, %d portal links, %d asteroids, layout checksum %08X"),
		GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
		Config.MatchNumber, Config.Seed, Graph.GetDimension(), Graph.GetEdgeProbability(), Num, Graph.NumOpenEdges(), NumAsteroids, LayoutChecksum);
	UE_LOG(LogHCJ, Log, TEXT("Black holes (%s): %d of %d sectors, checksum %08X (influence %.0f, horizon %.0f, pull %.0f, max %.0f)"),
		GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"), NumBlackHoles, Num, BlackHoleChecksum,
		Config.BlackHoles.InfluenceRadius, Config.BlackHoles.HorizonRadius, Config.BlackHoles.PullSpeed, Config.BlackHoles.MaxPullSpeed);
	UE_LOG(LogHCJ, Log, TEXT("Stations (%s): %d of %d sectors, %d turret mounts, %d asteroids removed around them, checksum %08X"),
		GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"), NumStations, Num, NumStationTurrets, NumSuppressedAsteroids, StationChecksum);
	UE_LOG(LogHCJ, Log, TEXT("Wormholes (%s): %d links (%s), checksum %08X"),
		GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"), WormholeLinks.Num(), *WormholeText, WormholeChecksum);
	UE_LOG(LogHCJ, Log, TEXT("Pickup pads (%s): %d pads in %d sectors, checksum %08X"),
		GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"), NumPads, Num, PickupPadChecksum);
	if (bCosmetics)
	{
		UE_LOG(LogHCJ, Log, TEXT("Asteroids are drawn with %d rock mesh variant(s)%s"), RockMeshes.Num(),
			RockMeshes.Num() > 0 ? TEXT("; their collision is the hidden basic shapes") : TEXT(": visible basic shapes"));
	}

	if (bCosmetics)
	{
		if (UHCJEffectsDirector* Effects = UHCJEffectsDirector::Get(this))
		{
			Effects->Prewarm();
		}
	}

	OnRebuilt.Broadcast();
}

const FHCJSectorLayout* AHCJUniverse::GetSectorLayout(int32 Sector) const
{
	return (bBuilt && Layouts.IsValidIndex(Sector)) ? &Layouts[Sector] : nullptr;
}

FVector AHCJUniverse::GetSectorOrigin(int32 Sector) const
{
	return FHCJHypercubeGraph::GetSectorWorldOrigin(Sector);
}

int32 AHCJUniverse::GetSectorAtLocation(const FVector& WorldLocation) const
{
	return bBuilt ? FHCJHypercubeGraph::FindSectorAtLocation(WorldLocation, Graph.GetDimension()) : INDEX_NONE;
}

FLinearColor AHCJUniverse::GetSectorTint(int32 Sector) const
{
	const FHCJSectorLayout* Layout = GetSectorLayout(Sector);
	return Layout ? Layout->Tint : FLinearColor::White;
}

FTransform AHCJUniverse::GetRandomSpawnTransform(int32 Sector, FRandomStream& Stream) const
{
	const FHCJSectorLayout* Layout = GetSectorLayout(Sector);
	if (!Layout || Layout->SpawnPoints.Num() == 0)
	{
		return FTransform::Identity;
	}

	FTransform Result = Layout->SpawnPoints[Stream.RandRange(0, Layout->SpawnPoints.Num() - 1)];
	Result.AddToTranslation(GetSectorOrigin(Sector));
	return Result;
}

const FHCJStation& AHCJUniverse::GetStation(int32 Sector) const
{
	static const FHCJStation None;
	return Stations.IsValidIndex(Sector) ? Stations[Sector] : None;
}

FVector AHCJUniverse::GetStationLocation(int32 Sector) const
{
	return GetSectorOrigin(Sector) + (HasStation(Sector) ? Stations[Sector].LocalPosition : FVector::ZeroVector);
}

void AHCJUniverse::BuildStation(int32 Sector)
{
	const FHCJStation& Station = Stations[Sector];
	const FVector Center = GetSectorOrigin(Sector) + Station.LocalPosition;
	const bool bCosmetics = GetNetMode() != NM_DedicatedServer;
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();

	// A model, when one is configured, is drawn instead of the boxes; the boxes stay the collision either way.
	UStaticMesh* Model = bCosmetics ? UHCJAssetSettings::Load<UStaticMesh>(Settings.StationMesh, TEXT("space station")) : nullptr;
	UStaticMesh* Cube = GetShapeMesh(EHCJAsteroidShape::Cube);
	// With a model the boxes it does not envelop (see FirstBoxOutsideModel) are still drawn: no solid thing is invisible.
	UMaterialInstanceDynamic* BodyMaterial = bCosmetics ? CreateTintedMaterial(Model != nullptr ? Settings.StationColor * 0.45f : Settings.StationColor, 0.45f) : nullptr;

	UMaterialInstanceDynamic* PanelMaterial = BodyMaterial != nullptr ? CreateTintedMaterial(FLinearColor(0.03f, 0.07f, 0.22f), 0.15f) : nullptr;

	FBox ShapeBounds(ForceInit);
	int32 BoxIndex = 0;
	for (const FHCJStationBox& Box : FHCJStation::GetBodyBoxes())
	{
		const int32 ThisBox = BoxIndex++;
		const bool bPanel = ThisBox >= FHCJStation::FirstPanelBox;
		// The model stands for the hub, the beam along X and the two panels on it; the rest of the shape stays visible.
		const bool bInsideModel = Model != nullptr && (ThisBox == 0 || ThisBox == 1 || ThisBox == 4 || ThisBox == 5);
		ShapeBounds += FBox(Box.Center - Box.HalfExtent, Box.Center + Box.HalfExtent);

		UBoxComponent* Solid = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
		Solid->SetMobility(EComponentMobility::Static);
		Solid->SetBoxExtent(Box.HalfExtent, false);
		Solid->SetCollisionProfileName(TEXT("BlockAll"));
		Solid->SetGenerateOverlapEvents(false);
		Solid->SetCanEverAffectNavigation(false);
		Solid->SetupAttachment(SceneRoot);
		Solid->SetRelativeLocation(Center + Box.Center);
		Solid->RegisterComponent();
		StationComponents.Add(Solid);

		if (bCosmetics && !bInsideModel && Cube != nullptr)
		{
			UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Visual->SetMobility(EComponentMobility::Static);
			Visual->SetStaticMesh(Cube);
			Visual->SetMaterial(0, bPanel ? PanelMaterial : BodyMaterial);
			Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Visual->SetGenerateOverlapEvents(false);
			Visual->SetCanEverAffectNavigation(false);
			Visual->SetupAttachment(SceneRoot);
			Visual->SetRelativeLocation(Center + Box.Center);
			Visual->SetRelativeScale3D(Box.HalfExtent * (2.0 / HCJ::BasicShapeSize));
			Visual->RegisterComponent();
			StationComponents.Add(Visual);
		}
	}
	if (!bCosmetics)
	{
		return;
	}

	if (Model != nullptr)
	{
		// Uniform scale so the rotated model just fills the box around the collision shape.
		const FBox ModelBounds = Model->GetBoundingBox().TransformBy(FTransform(Settings.StationMeshRotation));
		const FVector ModelSize = ModelBounds.GetSize().ComponentMax(FVector(1.0));
		const FVector ShapeSize = ShapeBounds.GetSize();
		// StationMeshScale > 0: that scale, the model centred on the hub. Else it is fitted into the box around the shape.
		const bool bFit = Settings.StationMeshScale <= 0.0f;
		const double Scale = bFit ? FMath::Min3(ShapeSize.X / ModelSize.X, ShapeSize.Y / ModelSize.Y, ShapeSize.Z / ModelSize.Z) : double(Settings.StationMeshScale);
		UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Visual->SetMobility(EComponentMobility::Static);
		Visual->SetStaticMesh(Model);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetGenerateOverlapEvents(false);
		Visual->SetCanEverAffectNavigation(false);
		Visual->SetupAttachment(SceneRoot);
		Visual->SetRelativeScale3D(FVector(Scale));
		Visual->SetRelativeLocationAndRotation(Center + (bFit ? ShapeBounds.GetCenter() : FVector::ZeroVector) - ModelBounds.GetCenter() * Scale, Settings.StationMeshRotation);
		Visual->SetCullDistance(float(HCJRockCullEnd));
		// The windows and lamps of the model in a warm white with a little of the sector's colour.
		for (int32 Slot = 0; Slot < Visual->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInstanceDynamic* SlotMaterial = Visual->CreateDynamicMaterialInstance(Slot))
			{
				GeneratedMaterials.Add(SlotMaterial);
				SlotMaterial->SetScalarParameterValue(TEXT("EmissiveBoost"), Settings.StationWindowBrightness);
				SlotMaterial->SetVectorParameterValue(TEXT("EmissiveTint"), FMath::Lerp(FLinearColor(1.0f, 0.9f, 0.7f), Layouts[Sector].Tint, 0.25f));
				SlotMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.StationLampBrightness);
			}
		}
		Visual->RegisterComponent();
		StationComponents.Add(Visual);
	}

	// Beacons at the ends of the beams, in the sector's colour: the station is found from far away.
	UMaterialInterface* Glow = Settings.StationBeaconBrightness > 0.0f ? UHCJAssetSettings::Load<UMaterialInterface>(Settings.PortalMaterial, TEXT("portal")) : nullptr;
	if (Glow != nullptr && Cube != nullptr)
	{
		UMaterialInstanceDynamic* BeaconMaterial = UMaterialInstanceDynamic::Create(Glow, this);
		GeneratedMaterials.Add(BeaconMaterial);
		if (!Settings.PortalColorParameter.IsNone())
		{
			BeaconMaterial->SetVectorParameterValue(Settings.PortalColorParameter, FMath::Lerp(Layouts[Sector].Tint, FLinearColor::White, 0.3f));
		}
		if (!Settings.PortalBrightnessParameter.IsNone())
		{
			BeaconMaterial->SetScalarParameterValue(Settings.PortalBrightnessParameter, Settings.StationBeaconBrightness);
		}
		for (const FVector& Offset : { FVector(1650.0, 0.0, 260.0), FVector(-1650.0, 0.0, 260.0), FVector(0.0, 1650.0, 260.0), FVector(0.0, -1650.0, 260.0), FVector(0.0, 0.0, -860.0) })
		{
			UStaticMeshComponent* Beacon = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Beacon->SetMobility(EComponentMobility::Static);
			Beacon->SetStaticMesh(Cube);
			Beacon->SetMaterial(0, BeaconMaterial);
			Beacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Beacon->SetGenerateOverlapEvents(false);
			Beacon->SetCanEverAffectNavigation(false);
			Beacon->SetCastShadow(false);
			Beacon->SetupAttachment(SceneRoot);
			Beacon->SetRelativeLocation(Center + Offset);
			Beacon->SetRelativeScale3D(FVector(1.2));
			Beacon->RegisterComponent();
			StationComponents.Add(Beacon);
		}
	}
}

FVector AHCJUniverse::GetWormholeLocation(int32 Sector) const
{
	return GetSectorOrigin(Sector) + (HasWormhole(Sector) && WormholeMouths.IsValidIndex(Sector) ? WormholeMouths[Sector] : FVector::ZeroVector);
}

bool AHCJUniverse::HasHop(int32 Sector, int32 Hop) const
{
	return Hop == HCJ::WormholeHop ? HasWormhole(Sector) : HasPortal(Sector, Hop);
}

FVector AHCJUniverse::GetHopLocation(int32 Sector, int32 Hop) const
{
	if (Hop == HCJ::WormholeHop)
	{
		return GetWormholeLocation(Sector);
	}
	return HasPortal(Sector, Hop) ? GetPortalWorldTransform(Sector, Hop).GetLocation() : GetSectorOrigin(Sector);
}

void AHCJUniverse::BuildWormholeMouths(const FHCJWormholeLink& Link)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const int32 Ends[2][2] = { { Link.SectorA, Link.SectorB }, { Link.SectorB, Link.SectorA } };
	for (const int32* End : Ends)
	{
		const int32 Sector = End[0];
		const int32 Dest = End[1];
		const FVector Location = GetSectorOrigin(Sector) + WormholeMouths[Sector];
		const FVector DestLocation = GetSectorOrigin(Dest) + WormholeMouths[Dest];

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transient;
		if (AHCJPortal* Mouth = World->SpawnActor<AHCJPortal>(AHCJPortal::StaticClass(), FTransform(Location), SpawnParameters))
		{
			Mouth->InitWormhole(Sector, Dest, DestLocation, Layouts[Dest].Tint);
			WormholePortals.Add(Mouth);
		}
	}
}

TConstArrayView<FVector> AHCJUniverse::GetPickupPads(int32 Sector) const
{
	return bBuilt && PickupPads.IsValidIndex(Sector) ? TConstArrayView<FVector>(PickupPads[Sector]) : TConstArrayView<FVector>();
}

FVector AHCJUniverse::GetBlackHoleLocation(int32 Sector) const
{
	return HasBlackHole(Sector) ? GetSectorOrigin(Sector) + BlackHoles[Sector].LocalPosition : GetSectorOrigin(Sector);
}

double AHCJUniverse::GetBlackHoleDistance(const FVector& WorldLocation) const
{
	const int32 Sector = GetSectorAtLocation(WorldLocation);
	return HasBlackHole(Sector) ? FVector::Dist(WorldLocation, GetBlackHoleLocation(Sector)) : -1.0;
}

FVector AHCJUniverse::GetGravityDrift(const FVector& WorldLocation) const
{
	const int32 Sector = GetSectorAtLocation(WorldLocation);
	if (!HasBlackHole(Sector))
	{
		return FVector::ZeroVector;
	}
	return FHCJBlackHole::ComputePull(GetBlackHoleLocation(Sector) - WorldLocation, Config.BlackHoles);
}

bool AHCJUniverse::IsInsideEventHorizon(const FVector& WorldLocation) const
{
	const double Distance = GetBlackHoleDistance(WorldLocation);
	return Distance >= 0.0 && Distance <= Config.BlackHoles.HorizonRadius;
}

bool AHCJUniverse::HasPortal(int32 Sector, int32 Dim) const
{
	return Graph.IsEdgeOpen(Sector, Dim);
}

FTransform AHCJUniverse::GetPortalWorldTransform(int32 Sector, int32 Dim) const
{
	if (!HasPortal(Sector, Dim) || !Layouts.IsValidIndex(Sector))
	{
		return FTransform::Identity;
	}
	const FHCJPortalSpec* Spec = Layouts[Sector].FindPortal(Dim);
	if (!Spec)
	{
		return FTransform::Identity;
	}
	return FTransform(Spec->GetOutwardDirection().Rotation(), GetSectorOrigin(Sector) + Spec->LocalPosition);
}

AHCJPortal* AHCJUniverse::GetPortal(int32 Sector, int32 Dim) const
{
	if (!bBuilt || !Graph.IsValidSector(Sector) || !Graph.IsValidDim(Dim))
	{
		return nullptr;
	}
	const int32 Index = PortalIndex(Sector, Dim);
	return Portals.IsValidIndex(Index) ? Portals[Index].Get() : nullptr;
}

bool AHCJUniverse::IsLocationClear(const FVector& WorldLocation, float Clearance) const
{
	const int32 Sector = GetSectorAtLocation(WorldLocation);
	const FHCJSectorLayout* Layout = GetSectorLayout(Sector);
	return !Layout || Layout->IsLocationClear(WorldLocation - GetSectorOrigin(Sector), Clearance);
}

FVector AHCJUniverse::GetRandomFreeLocation(int32 Sector, FRandomStream& Stream, float Clearance) const
{
	const FVector Origin = GetSectorOrigin(Sector);
	const FHCJSectorLayout* Layout = GetSectorLayout(Sector);
	if (!Layout)
	{
		return Origin;
	}

	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		const FVector Direction = Stream.VRand();
		const double Distance = Stream.FRandRange(0.0, HCJ::SpawnMaxDistance);
		const FVector Local = Direction * Distance;
		if (Layout->IsLocationClear(Local, Clearance))
		{
			return Origin + Local;
		}
	}

	if (Layout->SpawnPoints.Num() > 0)
	{
		return Origin + Layout->SpawnPoints[Stream.RandRange(0, Layout->SpawnPoints.Num() - 1)].GetLocation();
	}
	return Origin;
}

void AHCJUniverse::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Backdrop))
	{
		Backdrop->Destroy();
	}
	Backdrop = nullptr;
	ClearGenerated();
	Super::EndPlay(EndPlayReason);
}

void AHCJUniverse::ClearGenerated()
{
	for (UInstancedStaticMeshComponent* Component : GeneratedComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	GeneratedComponents.Reset();
	GeneratedMaterials.Reset();

	for (AHCJPortal* Portal : Portals)
	{
		if (IsValid(Portal))
		{
			Portal->Destroy();
		}
	}
	Portals.Reset();
	for (AHCJPortal* Mouth : WormholePortals)
	{
		if (IsValid(Mouth))
		{
			Mouth->Destroy();
		}
	}
	WormholePortals.Reset();
	for (UPrimitiveComponent* Component : StationComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	StationComponents.Reset();
	Stations.Reset();
	StationChecksum = 0;
	WormholeLinks.Reset();
	WormholeMouths.Reset();
	Routes.Reset();
	WormholeChecksum = 0;

	Rocks.Reset();
	CollisionInstanceKeys.Reset();
	SectorShapeComponents.Reset();
	SectorShapeMaterials.Reset();
	SectorRockComponents.Reset();
	DestroyedAsteroids.Reset();

	Layouts.Reset();
	BlackHoles.Reset();
	PickupPads.Reset();
	PickupPadChecksum = 0;
	Graph.Reset();
	LayoutChecksum = 0;
	bBuilt = false;
}

void AHCJUniverse::BuildSectorGeometry(int32 Sector)
{
	if (!Layouts.IsValidIndex(Sector))
	{
		return;
	}
	const FHCJSectorLayout& Layout = Layouts[Sector];

	// The layout is never changed; asteroids that would reach into this sector's station are simply not built.
	const FHCJStation& Station = GetStation(Sector);
	TArray<FTransform> Transforms[int32(EHCJAsteroidShape::Count)];
	TArray<uint32> Keys[int32(EHCJAsteroidShape::Count)];
	for (int32 Index = 0; Index < Layout.Asteroids.Num() && Index <= HCJAsteroids::MaxBaseIndex; ++Index)
	{
		const FHCJAsteroid& Asteroid = Layout.Asteroids[Index];
		if (!Station.IsAsteroidSuppressed(Asteroid))
		{
			Transforms[int32(Asteroid.Shape)].Add(Asteroid.GetLocalTransform());
			Keys[int32(Asteroid.Shape)].Add(HCJAsteroids::PackKey(Sector, HCJAsteroids::MakeId(Index)));
		}
	}

	for (int32 ShapeIndex = 0; ShapeIndex < int32(EHCJAsteroidShape::Count); ++ShapeIndex)
	{
		if (Transforms[ShapeIndex].Num() == 0)
		{
			continue;
		}
		UInstancedStaticMeshComponent* Component = CreateShapeComponent(Sector, static_cast<EHCJAsteroidShape>(ShapeIndex));
		if (Component == nullptr)
		{
			continue;
		}
		// Instances are added before registration so the physics bodies are created once, in bulk.
		Component->AddInstances(Transforms[ShapeIndex], /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false, /*bUpdateNavigation*/ false);
		Component->RegisterComponent();
		for (int32 Instance = 0; Instance < Keys[ShapeIndex].Num(); ++Instance)
		{
			const uint32 Key = Keys[ShapeIndex][Instance];
			FRock& Rock = Rocks.Add(Key);
			Rock.Asteroid = Layout.Asteroids[HCJAsteroids::GetBaseIndex(HCJAsteroids::GetKeyId(Key))];
			Rock.Collision = Component;
			Rock.CollisionIndex = Instance;
		}
		CollisionInstanceKeys.Add(Component, MoveTemp(Keys[ShapeIndex]));
	}

	if (RockMeshes.Num() > 0)
	{
		BuildSectorRocks(Sector);
	}
}

UInstancedStaticMeshComponent* AHCJUniverse::CreateShapeComponent(int32 Sector, EHCJAsteroidShape Shape)
{
	UStaticMesh* Mesh = GetShapeMesh(Shape);
	if (!Layouts.IsValidIndex(Sector) || Mesh == nullptr)
	{
		return nullptr;
	}
	if (SectorShapeComponents.Num() != Layouts.Num())
	{
		SectorShapeComponents.SetNum(Layouts.Num());
		SectorShapeMaterials.SetNum(Layouts.Num());
	}
	SectorShapeComponents[Sector].SetNum(int32(EHCJAsteroidShape::Count));

	// Rock colour: the sector tint pulled halfway towards dark grey, so the sectors read differently without glowing.
	if (GetNetMode() != NM_DedicatedServer && SectorShapeMaterials[Sector] == nullptr)
	{
		const FLinearColor RockColor = FMath::Lerp(FLinearColor(0.16f, 0.16f, 0.17f), Layouts[Sector].Tint, 0.5f);
		SectorShapeMaterials[Sector] = CreateTintedMaterial(RockColor, 0.85f);
	}
	// With rock meshes the shapes are collision only.
	const bool bDrawShapes = RockMeshes.Num() == 0;

	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	if (SectorShapeMaterials[Sector] != nullptr)
	{
		Component->SetMaterial(0, SectorShapeMaterials[Sector]);
	}
	Component->SetCollisionProfileName(TEXT("BlockAll"));
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(bDrawShapes);
	Component->SetVisibility(bDrawShapes);
	Component->SetupAttachment(SceneRoot);
	Component->SetRelativeLocation(GetSectorOrigin(Sector));
	GeneratedComponents.Add(Component);
	SectorShapeComponents[Sector][int32(Shape)] = Component;
	return Component;
}

UInstancedStaticMeshComponent* AHCJUniverse::GetShapeComponent(int32 Sector, EHCJAsteroidShape Shape)
{
	if (SectorShapeComponents.IsValidIndex(Sector) && SectorShapeComponents[Sector].IsValidIndex(int32(Shape)) && SectorShapeComponents[Sector][int32(Shape)] != nullptr)
	{
		return SectorShapeComponents[Sector][int32(Shape)];
	}
	// The first rock of this shape in the sector is a piece of a broken one.
	UInstancedStaticMeshComponent* Component = CreateShapeComponent(Sector, Shape);
	if (Component != nullptr)
	{
		Component->RegisterComponent();
		CollisionInstanceKeys.Add(Component, TArray<uint32>());
	}
	return Component;
}

void AHCJUniverse::LoadRockMeshes()
{
	RockMeshes.Reset();
	RockCornerFactors.Reset();
	for (const FHCJAsteroidMeshVariant& Variant : UHCJAssetSettings::Get().AsteroidMeshes)
	{
		UStaticMesh* Mesh = UHCJAssetSettings::Load<UStaticMesh>(Variant.Mesh, TEXT("asteroid rock"));
		if (Mesh != nullptr && Mesh->GetBoundingBox().IsValid)
		{
			RockMeshes.Add(Mesh);
			RockCornerFactors.Add(Variant.CornerFactor);
		}
	}
}

void AHCJUniverse::BuildSectorRocks(int32 Sector)
{
	const FHCJSectorLayout& Layout = Layouts[Sector];
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const int32 NumVariants = RockMeshes.Num();

	TArray<TArray<FTransform>> Transforms;
	TArray<TArray<uint32>> Keys;
	Transforms.SetNum(NumVariants);
	Keys.SetNum(NumVariants);
	const FHCJStation& Station = GetStation(Sector);
	for (int32 Index = 0; Index < Layout.Asteroids.Num() && Index <= HCJAsteroids::MaxBaseIndex; ++Index)
	{
		if (Station.IsAsteroidSuppressed(Layout.Asteroids[Index]))
		{
			continue;
		}
		const int32 Variant = HCJ::PickAsteroidVariant(Layout.Seed, Index, NumVariants);
		Transforms[Variant].Add(Layout.Asteroids[Index].ComputeVisualTransform(RockMeshes[Variant]->GetBoundingBox(), RockCornerFactors[Variant], Settings.AsteroidSphereFill));
		Keys[Variant].Add(HCJAsteroids::PackKey(Sector, HCJAsteroids::MakeId(Index)));
	}

	for (int32 Variant = 0; Variant < NumVariants; ++Variant)
	{
		if (Transforms[Variant].Num() == 0)
		{
			continue;
		}
		UInstancedStaticMeshComponent* Component = CreateRockComponent(Sector, Variant);
		Component->AddInstances(Transforms[Variant], /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false, /*bUpdateNavigation*/ false);
		Component->RegisterComponent();
		for (int32 Instance = 0; Instance < Keys[Variant].Num(); ++Instance)
		{
			if (FRock* Rock = Rocks.Find(Keys[Variant][Instance]))
			{
				Rock->Visual = Component;
				Rock->VisualIndex = Instance;
			}
		}
	}
}

UInstancedStaticMeshComponent* AHCJUniverse::CreateRockComponent(int32 Sector, int32 Variant)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (SectorRockComponents.Num() != Layouts.Num())
	{
		SectorRockComponents.SetNum(Layouts.Num());
	}
	SectorRockComponents[Sector].SetNum(RockMeshes.Num());

	// Textured rock, pulled a little towards the sector tint so that the sectors still read differently.
	const FLinearColor RockTint = FMath::Lerp(FLinearColor::White, Layouts[Sector].Tint, Settings.AsteroidTintStrength) * Settings.AsteroidBrightness;

	// (Item 26 tried hierarchical instances here: no faster on this card - 92 against 97 fps in the densest field - so not kept.)
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(RockMeshes[Variant]);
	if (!Settings.AsteroidTintParameter.IsNone())
	{
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* SlotMaterial = Component->GetMaterial(Slot);
			FLinearColor BaseValue;
			if (SlotMaterial != nullptr && SlotMaterial->GetVectorParameterValue(Settings.AsteroidTintParameter, BaseValue))
			{
				UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(SlotMaterial, this);
				Material->SetVectorParameterValue(Settings.AsteroidTintParameter, BaseValue * RockTint);
				// Rim light from the back light of this sector's sky, and craters (M_Asteroid; ignored by a material without them).
				FVector BackDirection;
				FLinearColor BackColor;
				AHCJSpaceBackdrop::MakeBacklight(Config.Seed, Sector, Layouts[Sector].Tint, -SunLight->GetComponentRotation().Vector(), BackDirection, BackColor);
				Material->SetVectorParameterValue(TEXT("Rim Color"), FMath::Lerp(BackColor, FLinearColor::White, 0.3f));
				Material->SetVectorParameterValue(TEXT("Rim Dir"), FLinearColor(BackDirection));
				Material->SetScalarParameterValue(TEXT("Rim Strength"), Settings.RockRimStrength);
				Material->SetScalarParameterValue(TEXT("Rim Power"), Settings.RockRimPower);
				Material->SetScalarParameterValue(TEXT("Crater Strength"), Settings.RockCraterStrength);
				GeneratedMaterials.Add(Material);
				Component->SetMaterial(Slot, Material);
			}
		}
	}
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(true);
	// Rocks of other sectors (500000 uu away) are specks of a few pixels: do not draw them at all.
	Component->SetCullDistances(HCJRockCullStart, HCJRockCullEnd);
	Component->SetupAttachment(SceneRoot);
	Component->SetRelativeLocation(GetSectorOrigin(Sector));
	GeneratedComponents.Add(Component);
	SectorRockComponents[Sector][Variant] = Component;
	return Component;
}

UInstancedStaticMeshComponent* AHCJUniverse::GetRockComponent(int32 Sector, int32 Variant)
{
	if (!Layouts.IsValidIndex(Sector) || !RockMeshes.IsValidIndex(Variant))
	{
		return nullptr;
	}
	if (SectorRockComponents.IsValidIndex(Sector) && SectorRockComponents[Sector].IsValidIndex(Variant) && SectorRockComponents[Sector][Variant] != nullptr)
	{
		return SectorRockComponents[Sector][Variant];
	}
	UInstancedStaticMeshComponent* Component = CreateRockComponent(Sector, Variant);
	Component->RegisterComponent();
	return Component;
}

// ---------------------------------------------------------------------------------------------------------------
// Destroyable asteroids
// ---------------------------------------------------------------------------------------------------------------

uint32 AHCJUniverse::FindAsteroidKey(const UPrimitiveComponent* Component, int32 Item) const
{
	const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
	const TArray<uint32>* Keys = Instanced != nullptr ? CollisionInstanceKeys.Find(Instanced) : nullptr;
	return (Keys != nullptr && Keys->IsValidIndex(Item)) ? (*Keys)[Item] : MAX_uint32;
}

bool AHCJUniverse::GetAsteroidSphere(uint32 Key, FVector& OutLocation, float& OutRadius) const
{
	const FRock* Rock = Rocks.Find(Key);
	if (Rock == nullptr)
	{
		return false;
	}
	OutLocation = GetSectorOrigin(HCJAsteroids::GetKeySector(Key)) + Rock->Asteroid.LocalPosition;
	OutRadius = Rock->Asteroid.BoundingRadius;
	return true;
}

uint32 AHCJUniverse::FindNearestAsteroid(const FVector& WorldLocation, float MinRadius) const
{
	const int32 Sector = GetSectorAtLocation(WorldLocation);
	const FVector Local = WorldLocation - GetSectorOrigin(Sector);
	uint32 Best = MAX_uint32;
	double BestDistSq = TNumericLimits<double>::Max();
	for (const TPair<uint32, FRock>& Pair : Rocks)
	{
		if (HCJAsteroids::GetKeySector(Pair.Key) != Sector || Pair.Value.Asteroid.BoundingRadius < MinRadius)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Pair.Value.Asteroid.LocalPosition, Local);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Pair.Key;
		}
	}
	return Best;
}

bool AHCJUniverse::FindCoverPoint(const FVector& From, const FVector& Threat, float MaxDistance, float MinRadius, float Clearance, FVector& OutPoint) const
{
	const int32 Sector = GetSectorAtLocation(From);
	if (!bBuilt || Sector == INDEX_NONE)
	{
		return false;
	}
	const FVector Origin = GetSectorOrigin(Sector);
	const FVector LocalFrom = From - Origin;
	const FVector LocalThreat = Threat - Origin;
	const FRock* Best = nullptr;
	double BestScore = TNumericLimits<double>::Max();
	for (const TPair<uint32, FRock>& Pair : Rocks)
	{
		const FHCJAsteroid& Asteroid = Pair.Value.Asteroid;
		if (HCJAsteroids::GetKeySector(Pair.Key) != Sector || Asteroid.BoundingRadius < MinRadius)
		{
			continue;
		}
		const double Distance = FVector::Dist(Asteroid.LocalPosition, LocalFrom);
		if (Distance > MaxDistance)
		{
			continue;
		}
		// Near, and not on the attacker's side of the ship: a rock between the two is the best there is.
		const FVector ToRock = (Asteroid.LocalPosition - LocalFrom).GetSafeNormal();
		const FVector ToThreat = (LocalThreat - LocalFrom).GetSafeNormal();
		const double Score = Distance * (1.0 + 0.5 * FMath::Max(FVector::DotProduct(ToRock, ToThreat), 0.0)) - 2.0 * Asteroid.BoundingRadius;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = &Pair.Value;
		}
	}
	if (Best == nullptr)
	{
		return false;
	}
	const FVector Away = (Best->Asteroid.LocalPosition - LocalThreat).GetSafeNormal();
	OutPoint = Origin + Best->Asteroid.LocalPosition + Away * (Best->Asteroid.BoundingRadius + Clearance);
	return true;
}

float AHCJUniverse::GetAsteroidHealth(uint32 Key) const
{
	const FRock* Rock = Rocks.Find(Key);
	if (Rock == nullptr || !bBuilt || !Config.Asteroids.bEnabled)
	{
		return -1.0f;
	}
	return Rock->Health >= 0.0f ? Rock->Health : HCJAsteroids::ComputeHealth(Rock->Asteroid.BoundingRadius, Config.Asteroids);
}

bool AHCJUniverse::ServerDamageAsteroid(const UPrimitiveComponent* Component, int32 Item, float Damage, AController* DamageInstigator)
{
	const uint32 Key = FindAsteroidKey(Component, Item);
	return Key != MAX_uint32 && ServerDamageAsteroidKey(Key, Damage, DamageInstigator);
}

bool AHCJUniverse::ServerDamageAsteroidKey(uint32 Key, float Damage, AController* DamageInstigator)
{
	if (GetNetMode() == NM_Client || !bBuilt || !Config.Asteroids.bEnabled || Damage <= 0.0f)
	{
		return false;
	}
	FRock* Rock = Rocks.Find(Key);
	if (Rock == nullptr)
	{
		return false;
	}
	if (Rock->Health < 0.0f)
	{
		Rock->Health = HCJAsteroids::ComputeHealth(Rock->Asteroid.BoundingRadius, Config.Asteroids);
	}
	Rock->Health -= Damage;
	if (Rock->Health > 0.0f)
	{
		return true;
	}
	if (DestroyedAsteroids.Num() >= HCJAsteroids::MaxDestroyedPerMatch)
	{
		// The replicated list is full (never reached in a real match): the rock stays, as good as new.
		Rock->Health = -1.0f;
		return true;
	}
	if (AHCJGameState* GameState = AHCJGameState::Get(this))
	{
		// Adds it to the replicated list and applies it here (the pieces appear, the break is shown on a listen host).
		GameState->ServerDestroyAsteroid(Key, DamageInstigator);
	}
	return true;
}

int32 AHCJUniverse::ServerDamageAsteroidsInRadius(const FVector& Origin, float InnerRadius, float Radius, float EdgeScale, float Damage, AController* DamageInstigator, uint32 ExceptKey)
{
	if (GetNetMode() == NM_Client || !bBuilt || !Config.Asteroids.bEnabled || Damage <= 0.0f || Radius <= 0.0f)
	{
		return 0;
	}
	const int32 Sector = GetSectorAtLocation(Origin);
	const FVector Local = Origin - GetSectorOrigin(Sector);
	// Collected first: destroying a rock changes the map.
	TArray<TPair<uint32, float>, TInlineAllocator<16>> Victims;
	for (const TPair<uint32, FRock>& Pair : Rocks)
	{
		if (Pair.Key == ExceptKey || HCJAsteroids::GetKeySector(Pair.Key) != Sector)
		{
			continue;
		}
		const float Distance = FMath::Max(static_cast<float>(FVector::Dist(Pair.Value.Asteroid.LocalPosition, Local)) - Pair.Value.Asteroid.BoundingRadius, 0.0f);
		if (Distance > Radius)
		{
			continue;
		}
		// The falloff of a torpedo blast (HCJTorpedo::BlastScale): full inside the inner radius, EdgeScale at the edge.
		const float Scale = Distance <= InnerRadius ? 1.0f : FMath::Lerp(1.0f, EdgeScale, (Distance - InnerRadius) / FMath::Max(Radius - InnerRadius, 1.0f));
		Victims.Emplace(Pair.Key, Damage * Scale);
	}
	for (const TPair<uint32, float>& Victim : Victims)
	{
		ServerDamageAsteroidKey(Victim.Key, Victim.Value, DamageInstigator);
	}
	return Victims.Num();
}

void AHCJUniverse::AddRock(int32 Sector, uint16 Id, const FHCJAsteroid& Asteroid)
{
	const uint32 Key = HCJAsteroids::PackKey(Sector, Id);
	if (Rocks.Contains(Key) || !Layouts.IsValidIndex(Sector))
	{
		return;
	}
	FRock& Rock = Rocks.Add(Key);
	Rock.Asteroid = Asteroid;
	if (UInstancedStaticMeshComponent* Collision = GetShapeComponent(Sector, Asteroid.Shape))
	{
		Rock.Collision = Collision;
		Rock.CollisionIndex = Collision->AddInstance(Asteroid.GetLocalTransform(), /*bWorldSpace*/ false);
		TArray<uint32>& Keys = CollisionInstanceKeys.FindOrAdd(Collision);
		if (Rock.CollisionIndex != INDEX_NONE)
		{
			if (Keys.Num() <= Rock.CollisionIndex)
			{
				Keys.SetNumUninitialized(Rock.CollisionIndex + 1);
			}
			Keys[Rock.CollisionIndex] = Key;
		}
	}
	if (RockMeshes.Num() > 0)
	{
		// The mesh of a piece: the same hash as for layout rocks, on a number no layout rock has.
		const int32 Variant = HCJ::PickAsteroidVariant(Layouts[Sector].Seed, 0x10000 + static_cast<int32>(Id), RockMeshes.Num());
		if (UInstancedStaticMeshComponent* Visual = GetRockComponent(Sector, Variant))
		{
			Rock.Visual = Visual;
			Rock.VisualIndex = Visual->AddInstance(Asteroid.ComputeVisualTransform(RockMeshes[Variant]->GetBoundingBox(), RockCornerFactors[Variant],
				UHCJAssetSettings::Get().AsteroidSphereFill), /*bWorldSpace*/ false);
		}
	}
}

bool AHCJUniverse::ApplyAsteroidDestroyed(uint32 Key, bool bEffects)
{
	const int32 Sector = HCJAsteroids::GetKeySector(Key);
	const uint16 Id = HCJAsteroids::GetKeyId(Key);
	if (!bBuilt || !Layouts.IsValidIndex(Sector) || !HCJAsteroids::IsValidId(Id) || !DestroyedAsteroids.Add(Key))
	{
		return false;
	}

	FRock Rock;
	if (!Rocks.RemoveAndCopyValue(Key, Rock))
	{
		// Its parent is not gone on this machine yet (entries can arrive in any order): remembered, so that the
		// piece is never put in place when the parent goes.
		return true;
	}
	// A zero scale ends the collision body of the instance and draws nothing; the instance indices of the others stay.
	const FTransform Gone(Rock.Asteroid.Rotation, Rock.Asteroid.LocalPosition, FVector::ZeroVector);
	if (UInstancedStaticMeshComponent* Collision = Rock.Collision.Get())
	{
		Collision->UpdateInstanceTransform(Rock.CollisionIndex, Gone, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		if (TArray<uint32>* Keys = CollisionInstanceKeys.Find(Collision); Keys != nullptr && Keys->IsValidIndex(Rock.CollisionIndex))
		{
			(*Keys)[Rock.CollisionIndex] = MAX_uint32;
		}
	}
	if (UInstancedStaticMeshComponent* Visual = Rock.Visual.Get())
	{
		Visual->UpdateInstanceTransform(Rock.VisualIndex, Gone, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
	}

	// Its pieces, the same on every machine; in place of one that is already known to be destroyed, what that one left behind.
	TArray<TPair<uint16, FHCJAsteroid>> Pieces;
	HCJAsteroids::CollectPieces(Sector, Layouts[Sector].Seed, Id, Rock.Asteroid, Config.Asteroids, DestroyedAsteroids, Pieces);
	for (const TPair<uint16, FHCJAsteroid>& Piece : Pieces)
	{
		AddRock(Sector, Piece.Key, Piece.Value);
	}

	UE_LOG(LogHCJ, Verbose, TEXT("Asteroid: rock %04X of sector %d (radius %.0f, generation %d) destroyed, %d piece(s) | %d destroyed so far, checksum %08X"), Id, Sector,
		Rock.Asteroid.BoundingRadius, HCJAsteroids::GetGeneration(Id), Pieces.Num(), DestroyedAsteroids.Num(), DestroyedAsteroids.ComputeChecksum());
	if (bEffects && GetNetMode() != NM_DedicatedServer)
	{
		if (UHCJEffectsDirector* Effects = UHCJEffectsDirector::Get(this))
		{
			Effects->PlayRockBreak(GetSectorOrigin(Sector) + Rock.Asteroid.LocalPosition, Rock.Asteroid.BoundingRadius, Pieces.Num() > 0);
		}
	}
	return true;
}

int32 AHCJUniverse::SyncDestroyedAsteroids(TConstArrayView<uint32> Keys, bool bEffects)
{
	int32 NumChanged = 0;
	for (const uint32 Key : Keys)
	{
		NumChanged += ApplyAsteroidDestroyed(Key, bEffects) ? 1 : 0;
	}
	return NumChanged;
}

void AHCJUniverse::BuildSectorPortals(int32 Sector)
{
	UWorld* World = GetWorld();
	if (!World || !Layouts.IsValidIndex(Sector))
	{
		return;
	}

	for (const FHCJPortalSpec& Spec : Layouts[Sector].Portals)
	{
		const int32 Index = PortalIndex(Sector, Spec.Dim);
		const FHCJPortalSpec* DestSpec = Layouts.IsValidIndex(Spec.DestSector) ? Layouts[Spec.DestSector].FindPortal(Spec.Dim) : nullptr;
		if (!DestSpec || !Portals.IsValidIndex(Index))
		{
			UE_LOG(LogHCJ, Error, TEXT("Sector %d portal along dimension %d has no matching portal in sector %d"), Sector, Spec.Dim, Spec.DestSector);
			continue;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transient;

		AHCJPortal* Portal = World->SpawnActor<AHCJPortal>(AHCJPortal::StaticClass(), GetPortalWorldTransform(Sector, Spec.Dim), SpawnParameters);
		if (!Portal)
		{
			UE_LOG(LogHCJ, Error, TEXT("Failed to spawn the portal of sector %d along dimension %d"), Sector, Spec.Dim);
			continue;
		}

		const FVector DestWorldLocation = GetSectorOrigin(Spec.DestSector) + DestSpec->LocalPosition;
		Portal->InitPortal(Sector, Spec.Dim, Spec.DestSector, DestWorldLocation, Layouts[Spec.DestSector].Tint);
		Portals[Index] = Portal;
	}
}

void AHCJUniverse::BuildSectorBackdrop(int32 Sector)
{
	UStaticMesh* Mesh = GetShapeMesh(EHCJAsteroidShape::Sphere);
	if (!Mesh || !Layouts.IsValidIndex(Sector))
	{
		return;
	}
	const FHCJSectorLayout& Layout = Layouts[Sector];

	// Own stream derived from the sector seed: the backdrop is cosmetic and must not disturb gameplay generation.
	const FRandomStream Stream(HCJ::MixSeed(Layout.Seed, 0x57A125));

	TArray<FTransform> Transforms;
	Transforms.Reserve(HCJNumBackdropStars);
	for (int32 Index = 0; Index < HCJNumBackdropStars; ++Index)
	{
		const FVector Direction = Stream.VRand();
		const double Distance = Stream.FRandRange(HCJBackdropMinDistance, HCJBackdropMaxDistance);
		const double Scale = Stream.FRandRange(1.5, 5.0);
		Transforms.Add(FTransform(FQuat::Identity, Direction * Distance, FVector(Scale)));
	}

	// Mostly white with a hint of the sector tint.
	UMaterialInstanceDynamic* Material = CreateTintedMaterial(FMath::Lerp(FLinearColor::White, Layout.Tint, 0.25f), 1.0f);

	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	if (Material)
	{
		Component->SetMaterial(0, Material);
	}
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(false);
	Component->SetupAttachment(SceneRoot);
	Component->SetRelativeLocation(GetSectorOrigin(Sector));
	Component->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false, /*bUpdateNavigation*/ false);
	Component->RegisterComponent();
	GeneratedComponents.Add(Component);
}

UMaterialInstanceDynamic* AHCJUniverse::CreateTintedMaterial(const FLinearColor& Color, float Roughness)
{
	if (!BaseMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (Material)
	{
		Material->SetVectorParameterValue(HCJColorParameter, Color);
		Material->SetScalarParameterValue(HCJRoughnessParameter, Roughness);
		GeneratedMaterials.Add(Material);
	}
	return Material;
}

UStaticMesh* AHCJUniverse::GetShapeMesh(EHCJAsteroidShape Shape) const
{
	const int32 Index = int32(Shape);
	return ShapeMeshes.IsValidIndex(Index) ? ShapeMeshes[Index].Get() : nullptr;
}
