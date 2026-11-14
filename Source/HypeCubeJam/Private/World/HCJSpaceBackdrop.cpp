#include "World/HCJSpaceBackdrop.h"

#include "Assets/HCJAssetSettings.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HypeCubeJam.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"
#include "World/HCJUniverse.h"

namespace
{
	/** Fractions of BackdropRadius at which the layers sit (nearer = drawn over the farther ones). */
	constexpr double HCJStarLayer = 0.99;
	constexpr double HCJNebulaLayer = 0.985;
	constexpr double HCJBodyLayer = 0.97;
	/** Planets drawn as their own spheres sit nearer, so that their rings (up to 2.3 planet radii) stay inside the dome. */
	constexpr double HCJPlanetBallLayer = 0.6;

	/** Radius of the dark disc in M_HCJ_BlackHole as a fraction of half the card (the "rs" of its shader). */
	constexpr double HCJBlackHoleShadowFraction = 0.20;

	/** Translucency order among the camera-centred layers (their sort distance is meaningless). */
	constexpr int32 HCJNebulaSortPriority = -20;
	constexpr int32 HCJStarSortPriority = -10;

	/** Deterministic [0, 1) sequence from integer hashing only; cosmetic, independent of every FRandomStream. */
	struct FHCJCosmeticHash
	{
		int32 State;
		int32 Counter = 0;

		explicit FHCJCosmeticHash(int32 Seed) : State(Seed) {}

		float Next()
		{
			State = HCJ::MixSeed(State, 0x51ED270B + Counter++);
			return float(static_cast<uint32>(State) >> 8) * (1.0f / 16777216.0f);
		}

		FVector NextDirection()
		{
			const double Z = 2.0 * Next() - 1.0;
			const double Phi = UE_DOUBLE_TWO_PI * Next();
			const double Ring = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
			return FVector(Ring * FMath::Cos(Phi), Ring * FMath::Sin(Phi), Z);
		}
	};

	/** Reads a numeric property (double, float or int) of a data table row by name; Default when the struct has none. */
	double ReadRowNumber(const UScriptStruct* RowStruct, const uint8* Row, const TCHAR* PropertyName, double Default)
	{
		const FProperty* Property = RowStruct != nullptr ? RowStruct->FindPropertyByName(PropertyName) : nullptr;
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			const void* Value = Numeric->ContainerPtrToValuePtr<void>(Row);
			return Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Value) : double(Numeric->GetSignedIntPropertyValue(Value));
		}
		return Default;
	}

	FString ReadRowString(const UScriptStruct* RowStruct, const uint8* Row, const TCHAR* PropertyName)
	{
		const FStrProperty* Property = RowStruct != nullptr ? CastField<FStrProperty>(RowStruct->FindPropertyByName(PropertyName)) : nullptr;
		return Property != nullptr ? Property->GetPropertyValue_InContainer(Row) : FString();
	}

	/** Star colour from the B-V colour index (Ballesteros' formula for the effective temperature). */
	FLinearColor ColorFromBV(double BV)
	{
		BV = FMath::Clamp(BV, -0.4, 2.0);
		const double Temperature = 4600.0 * (1.0 / (0.92 * BV + 1.7) + 1.0 / (0.92 * BV + 0.62));
		return FLinearColor::MakeFromColorTemperature(float(Temperature));
	}

	/** Unit vector for right ascension (hours) and declination (degrees); Y flipped for the left-handed world. */
	FVector DirectionFromRADec(double RAHours, double DecDegrees)
	{
		const double RA = FMath::DegreesToRadians(RAHours * 15.0);
		const double Dec = FMath::DegreesToRadians(DecDegrees);
		return FVector(FMath::Cos(Dec) * FMath::Cos(RA), -FMath::Cos(Dec) * FMath::Sin(RA), FMath::Sin(Dec));
	}

	void MakeCosmetic(UPrimitiveComponent* Component)
	{
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		Component->bAffectDynamicIndirectLighting = false;
		Component->bAffectDistanceFieldLighting = false;
		Component->bReceivesDecals = false;
	}

	/** A card at Direction * Distance, facing the origin, Width across; QuadSize is the side of the card mesh. */
	FTransform MakeCardTransform(const FVector& Direction, double Distance, double AngularSizeDegrees, double QuadSize, double RollDegrees = 0.0)
	{
		const double Width = 2.0 * Distance * FMath::Tan(FMath::DegreesToRadians(0.5 * FMath::Clamp(AngularSizeDegrees, 0.1, 150.0)));
		const FQuat Facing = FRotationMatrix::MakeFromX(-Direction).ToQuat();
		const FQuat Roll(FVector::ForwardVector, FMath::DegreesToRadians(RollDegrees));
		const double Scale = Width / FMath::Max(QuadSize, 1.0);
		return FTransform(Facing * Roll, Direction * Distance, FVector(1.0, Scale, Scale));
	}
}

FHCJSectorSky FHCJSectorSky::Make(int32 MatchSeed, int32 SectorId, int32 NumCatalogBodies, int32 MaxBodies, int32 NumNebulae)
{
	FHCJSectorSky Sky;
	FHCJCosmeticHash Hash(HCJ::MixSeed(HCJ::MixSeed(MatchSeed, 0x005C1E5), SectorId));

	// Every draw below happens whatever the arguments are, so that e.g. switching the nebulae off moves no planet.
	const float BodyCountRoll = Hash.Next();
	const float FirstBodyRoll = Hash.Next();
	const float SecondBodyRoll = Hash.Next();

	TArray<FVector, TInlineAllocator<4>> Placed;
	const auto PlaceApart = [&Hash, &Placed]()
	{
		const double MinCosine = FMath::Cos(FMath::DegreesToRadians(double(MinSeparationDegrees)));
		FVector Direction = Hash.NextDirection();
		for (int32 Attempt = 0; Attempt < 12; ++Attempt)
		{
			bool bApart = true;
			for (const FVector& Other : Placed)
			{
				bApart = bApart && FVector::DotProduct(Direction, Other) <= MinCosine;
			}
			if (bApart)
			{
				break;
			}
			Direction = Hash.NextDirection();
		}
		Placed.Add(Direction);
		return Direction;
	};

	const int32 NumBodies = FMath::Clamp(BodyCountRoll < 0.5f ? 2 : 1, 0, FMath::Min3(MaxBodies, NumCatalogBodies, 2));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FBody Body;
		Body.Direction = PlaceApart();
		Body.AngularDiameterDegrees = 6.0f + 6.0f * Hash.Next();
		Body.Phase = Hash.Next();
		if (Index >= NumBodies)
		{
			Placed.Pop();
			continue;
		}
		if (Index == 0)
		{
			Body.BodyIndex = FMath::Min(int32(FirstBodyRoll * NumCatalogBodies), NumCatalogBodies - 1);
		}
		else
		{
			// Another body than the first one.
			const int32 Step = 1 + FMath::Min(int32(SecondBodyRoll * (NumCatalogBodies - 1)), NumCatalogBodies - 2);
			Body.BodyIndex = (Sky.Bodies[0].BodyIndex + Step) % NumCatalogBodies;
		}
		Sky.Bodies.Add(Body);
	}

	for (int32 Index = 0; Index < FMath::Clamp(NumNebulae, 0, 4); ++Index)
	{
		FNebula Nebula;
		Nebula.Direction = Hash.NextDirection();
		Nebula.AngularSizeDegrees = 60.0f + 40.0f * Hash.Next();
		Nebula.RollDegrees = 360.0f * Hash.Next();
		Nebula.Seed = 100.0f * Hash.Next();
		Nebula.HueShiftDegrees = -50.0f + 100.0f * Hash.Next();
		Nebula.Strength = 0.6f + 0.4f * Hash.Next();
		Sky.Nebulae.Add(Nebula);
	}
	return Sky;
}

AHCJSpaceBackdrop::AHCJSpaceBackdrop()
{
	PrimaryActorTick.bCanEverTick = true;
	// After movement and the camera boom, so the sky sits on this frame's camera.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(SceneRoot);
	MakeCosmetic(SkyDome);

	Stars = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Stars"));
	Stars->SetupAttachment(SkyDome);
	MakeCosmetic(Stars);
	Stars->SetTranslucentSortPriority(HCJStarSortPriority);

	Planets = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Planets"));
	Planets->SetupAttachment(SceneRoot);
	MakeCosmetic(Planets);

	for (int32 Index = 0; Index < MaxPlanetBalls; ++Index)
	{
		PlanetBalls[Index] = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("PlanetBall%d"), Index));
		PlanetBalls[Index]->SetupAttachment(SceneRoot);
		MakeCosmetic(PlanetBalls[Index]);
		PlanetBalls[Index]->SetVisibility(false);
		PlanetRings[Index] = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("PlanetRing%d"), Index));
		PlanetRings[Index]->SetupAttachment(SceneRoot);
		MakeCosmetic(PlanetRings[Index]);
		PlanetRings[Index]->SetVisibility(false);
	}

	Moon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Moon"));
	Moon->SetupAttachment(SceneRoot);
	MakeCosmetic(Moon);
	Moon->SetVisibility(false);

	BlackHole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BlackHole"));
	BlackHole->SetupAttachment(SceneRoot);
	MakeCosmetic(BlackHole);
	// A thing in the world, not in the sky: it does not follow the camera.
	BlackHole->SetUsingAbsoluteLocation(true);
	BlackHole->SetUsingAbsoluteRotation(true);
	BlackHole->SetUsingAbsoluteScale(true);
	BlackHole->SetVisibility(false);

	Nebulae = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Nebulae"));
	Nebulae->SetupAttachment(SceneRoot);
	MakeCosmetic(Nebulae);
	Nebulae->SetTranslucentSortPriority(HCJNebulaSortPriority);
}

void AHCJSpaceBackdrop::Rebuild(int32 InMatchSeed, const FVector& SunDirection)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!bFixedSkyBuilt)
	{
		bFixedSkyBuilt = true;
		BuildFixedSky(SunDirection);
	}

	MatchSeed = InMatchSeed;
	ShownSector = INDEX_NONE;
	ShowSector(INDEX_NONE);
}

void AHCJSpaceBackdrop::BuildFixedSky(const FVector& SunDirection)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const double Radius = FMath::Max(Settings.BackdropRadius, 100000.0f);

	UStaticMesh* QuadMesh = UHCJAssetSettings::Load<UStaticMesh>(Settings.BackdropQuadMesh, TEXT("backdrop card"));

	// ---- Milky Way dome; also the frame the stars live in (the dome mesh is DomeExtent units in radius) ----
	UStaticMesh* DomeMesh = UHCJAssetSettings::Load<UStaticMesh>(Settings.SkyDomeMesh, TEXT("sky dome"));
	UMaterialInterface* DomeMaterial = UHCJAssetSettings::Load<UMaterialInterface>(Settings.SkyDomeMaterial, TEXT("sky dome material"));
	const double DomeExtent = DomeMesh != nullptr ? FMath::Max(DomeMesh->GetBoundingBox().GetExtent().GetMax(), 1.0) : 100.0;
	SkyDome->SetRelativeScale3D(FVector(Radius / DomeExtent));
	if (DomeMesh != nullptr && DomeMaterial != nullptr)
	{
		SkyDome->SetStaticMesh(DomeMesh);
		UMaterialInstanceDynamic* DomeInstance = UMaterialInstanceDynamic::Create(DomeMaterial, this);
		if (!Settings.SkyDomeIntensityParameter.IsNone())
		{
			DomeInstance->SetScalarParameterValue(Settings.SkyDomeIntensityParameter, Settings.SkyDomeIntensity);
		}
		SkyDome->SetMaterial(0, DomeInstance);
	}

	// ---- stars ----
	NumStars = 0;
	UDataTable* StarTable = UHCJAssetSettings::Load<UDataTable>(Settings.StarCatalog, TEXT("star catalogue"));
	UMaterialInterface* StarMaterial = UHCJAssetSettings::Load<UMaterialInterface>(Settings.StarMaterial, TEXT("star material"));
	const UScriptStruct* StarStruct = StarTable != nullptr ? StarTable->GetRowStruct() : nullptr;
	if (StarStruct != nullptr && StarMaterial != nullptr && QuadMesh != nullptr
		&& StarStruct->FindPropertyByName(TEXT("RA")) != nullptr && StarStruct->FindPropertyByName(TEXT("DEC")) != nullptr)
	{
		TArray<FTransform> Transforms;
		TArray<float> CustomData;
		const auto AddStar = [&Transforms, &CustomData, DomeExtent](const FVector& Direction, const FLinearColor& Color, float Magnitude)
		{
			Transforms.Add(FTransform(Direction * (HCJStarLayer * DomeExtent)));
			CustomData.Append({ Color.R, Color.G, Color.B, Magnitude });
		};

		for (const TPair<FName, uint8*>& Row : StarTable->GetRowMap())
		{
			const double Magnitude = ReadRowNumber(StarStruct, Row.Value, TEXT("Magnitude"), 99.0);
			if (Magnitude > Settings.StarMaxMagnitude)
			{
				continue;
			}
			const double RAHours = ReadRowNumber(StarStruct, Row.Value, TEXT("RA"), 0.0);
			const double DecDegrees = ReadRowNumber(StarStruct, Row.Value, TEXT("DEC"), 0.0);
			const double ColorIndex = ReadRowNumber(StarStruct, Row.Value, TEXT("ColorIndex"), 0.65);
			AddStar(DirectionFromRADec(RAHours, DecDegrees), ColorFromBV(ColorIndex), float(Magnitude));
		}
		const int32 NumCatalogStars = Transforms.Num();
		if (NumCatalogStars > 0 && Settings.SunMagnitude < 50.0f && !SunDirection.IsNearlyZero())
		{
			// The light source of the scene, where the sun light comes from.
			AddStar(SunDirection.GetSafeNormal(), FLinearColor(1.0f, 0.96f, 0.88f), Settings.SunMagnitude);
		}

		NumStars = Transforms.Num();
		if (NumStars > 0)
		{
			UMaterialInstanceDynamic* StarInstance = UMaterialInstanceDynamic::Create(StarMaterial, this);
			if (!Settings.StarMagnitudeOffsetParameter.IsNone())
			{
				StarInstance->SetScalarParameterValue(Settings.StarMagnitudeOffsetParameter, Settings.StarMagnitudeOffset);
			}
			Stars->SetStaticMesh(QuadMesh);
			Stars->SetMaterial(0, StarInstance);
			Stars->SetNumCustomDataFloats(4);
			// The instances are unit cards on the dome; the material sizes them on screen. Culling uses the dome's bounds.
			Stars->bUseAttachParentBound = SkyDome->GetStaticMesh() != nullptr;
			Stars->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false, /*bUpdateNavigation*/ false);
			for (int32 Index = 0; Index < NumStars; ++Index)
			{
				Stars->SetCustomData(Index, MakeArrayView(&CustomData[4 * Index], 4), /*bMarkRenderStateDirty*/ false);
			}
			Stars->MarkRenderStateDirty();
		}
		UE_LOG(LogHCJ, Log, TEXT("Backdrop: %d of %d catalogue stars up to magnitude %.1f"), NumCatalogStars, StarTable->GetRowMap().Num(), Settings.StarMaxMagnitude);
	}
	else if (!Settings.StarCatalog.IsNull())
	{
		UE_LOG(LogHCJ, Warning, TEXT("Backdrop: no stars (catalogue rows %d, row struct %s; is the CelestialVault plugin enabled?)"),
			StarTable != nullptr ? StarTable->GetRowMap().Num() : 0, *GetNameSafe(StarStruct));
	}

	// ---- bodies: the planet table plus the Moon ----
	Bodies.Reset();
	UDataTable* PlanetTable = UHCJAssetSettings::Load<UDataTable>(Settings.PlanetCatalog, TEXT("planet catalogue"));
	UMaterialInterface* PlanetMaterial = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PlanetMaterial, TEXT("planet material"));
	const UScriptStruct* PlanetStruct = PlanetTable != nullptr ? PlanetTable->GetRowStruct() : nullptr;
	UStaticMesh* PlanetMesh = UHCJAssetSettings::Load<UStaticMesh>(Settings.PlanetMesh, TEXT("planet sphere"));
	if (PlanetStruct != nullptr && PlanetMaterial != nullptr && PlanetMesh != nullptr)
	{
		Planets->SetStaticMesh(PlanetMesh);
		Planets->SetMaterial(0, PlanetMaterial);
		Planets->SetNumCustomDataFloats(1);
		for (const TPair<FName, uint8*>& Row : PlanetTable->GetRowMap())
		{
			FCatalogBody Body;
			Body.Name = ReadRowString(PlanetStruct, Row.Value, TEXT("Name"));
			if (Body.Name.IsEmpty())
			{
				Body.Name = Row.Key.ToString();
			}
			Body.AtlasColumn = int32(ReadRowNumber(PlanetStruct, Row.Value, TEXT("TextureColumnIndex"), 0.0));
			// Gas giants look bigger than rocky planets, but nowhere near to scale (Jupiter is 29 Mercuries across).
			const double RadiusKm = ReadRowNumber(PlanetStruct, Row.Value, TEXT("Radius"), 6000.0);
			Body.SizeFactor = float(FMath::Clamp(FMath::Pow(FMath::Max(RadiusKm, 1.0) / 20000.0, 0.3), 0.6, 1.5));
			// What the new planet material needs to know about each body (by name; anything else gets the defaults).
			if (Body.Name == TEXT("Mercury")) { Body.Atmosphere = FLinearColor(0.6f, 0.6f, 0.62f); Body.RimStrength = 0.25f; Body.Clouds = 0.0f; }
			else if (Body.Name == TEXT("Venus")) { Body.Atmosphere = FLinearColor(1.0f, 0.78f, 0.45f); Body.RimStrength = 1.3f; Body.Clouds = 0.9f; }
			else if (Body.Name == TEXT("Earth")) { Body.Atmosphere = FLinearColor(0.35f, 0.6f, 1.0f); Body.RimStrength = 1.3f; Body.Clouds = 0.6f; }
			else if (Body.Name == TEXT("Mars")) { Body.Atmosphere = FLinearColor(1.0f, 0.5f, 0.3f); Body.RimStrength = 0.6f; Body.Clouds = 0.15f; }
			else if (Body.Name == TEXT("Jupiter")) { Body.Atmosphere = FLinearColor(1.0f, 0.82f, 0.62f); Body.RimStrength = 0.9f; Body.Clouds = 1.0f; }
			else if (Body.Name == TEXT("Saturn"))
			{
				Body.Atmosphere = FLinearColor(1.0f, 0.88f, 0.62f); Body.RimStrength = 0.9f; Body.Clouds = 0.8f; Body.bBanded = true;
				Body.RingOuter = 2.27f; Body.RingInner = 1.24f; Body.RingColor = FLinearColor(0.92f, 0.82f, 0.64f); Body.RingOpacity = 0.95f;
			}
			else if (Body.Name == TEXT("Uranus"))
			{
				Body.Atmosphere = FLinearColor(0.5f, 0.95f, 1.0f); Body.RimStrength = 1.1f; Body.Clouds = 0.5f;
				// Thin, dark rings, seen as a faint line.
				Body.RingOuter = 2.0f; Body.RingInner = 1.64f; Body.RingColor = FLinearColor(0.55f, 0.7f, 0.75f); Body.RingOpacity = 0.35f;
			}
			else if (Body.Name == TEXT("Neptune")) { Body.Atmosphere = FLinearColor(0.3f, 0.5f, 1.0f); Body.RimStrength = 1.2f; Body.Clouds = 0.7f; }
			Bodies.Add(Body);
		}
	}

	// The planets as their own spheres with the new surface material (and rings), when it loads.
	SunDirectionWS = SunDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, 1.0));
	UMaterialInterface* SurfaceMaterial = PlanetMesh != nullptr ? UHCJAssetSettings::Load<UMaterialInterface>(Settings.PlanetSurfaceMaterial, TEXT("planet surface material")) : nullptr;
	UMaterialInterface* RingMaterial = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PlanetRingMaterial, TEXT("planet ring material"));
	UStaticMesh* RingMesh = RingMaterial != nullptr ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")) : nullptr;
	for (int32 Index = 0; Index < MaxPlanetBalls; ++Index)
	{
		PlanetBallMaterials[Index] = SurfaceMaterial != nullptr ? UMaterialInstanceDynamic::Create(SurfaceMaterial, this) : nullptr;
		if (PlanetBallMaterials[Index] != nullptr)
		{
			PlanetBalls[Index]->SetStaticMesh(PlanetMesh);
			PlanetBalls[Index]->SetMaterial(0, PlanetBallMaterials[Index]);
		}
		PlanetRingMaterials[Index] = RingMesh != nullptr ? UMaterialInstanceDynamic::Create(RingMaterial, this) : nullptr;
		if (PlanetRingMaterials[Index] != nullptr)
		{
			PlanetRings[Index]->SetStaticMesh(RingMesh);
			PlanetRings[Index]->SetMaterial(0, PlanetRingMaterials[Index]);
		}
	}
	if (UMaterialInterface* MoonBase = QuadMesh != nullptr ? UHCJAssetSettings::Load<UMaterialInterface>(Settings.MoonMaterial, TEXT("moon material")) : nullptr)
	{
		MoonMaterial = UMaterialInstanceDynamic::Create(MoonBase, this);
		if (!Settings.MoonBrightnessParameter.IsNone())
		{
			MoonMaterial->SetScalarParameterValue(Settings.MoonBrightnessParameter, Settings.MoonBrightness);
		}
		Moon->SetStaticMesh(QuadMesh);
		Moon->SetMaterial(0, MoonMaterial);

		FCatalogBody Body;
		Body.Name = TEXT("Moon");
		Body.SizeFactor = 0.8f;
		Bodies.Add(Body);
	}

	// ---- black hole and nebulae ----
	if (UMaterialInterface* BlackHoleBase = QuadMesh != nullptr ? UHCJAssetSettings::Load<UMaterialInterface>(Settings.BlackHoleMaterial, TEXT("black hole material")) : nullptr)
	{
		BlackHoleMaterial = UMaterialInstanceDynamic::Create(BlackHoleBase, this);
		BlackHole->SetStaticMesh(QuadMesh);
		BlackHole->SetMaterial(0, BlackHoleMaterial);
	}
	if (UMaterialInterface* NebulaMaterial = QuadMesh != nullptr ? UHCJAssetSettings::Load<UMaterialInterface>(Settings.NebulaMaterial, TEXT("nebula material")) : nullptr)
	{
		UMaterialInstanceDynamic* NebulaInstance = UMaterialInstanceDynamic::Create(NebulaMaterial, this);
		NebulaInstance->SetScalarParameterValue(TEXT("Intensity"), Settings.NebulaBrightness);
		Nebulae->SetStaticMesh(QuadMesh);
		Nebulae->SetMaterial(0, NebulaInstance);
		Nebulae->SetNumCustomDataFloats(4);
	}

	FString BodyNames;
	for (const FCatalogBody& Body : Bodies)
	{
		BodyNames += (BodyNames.IsEmpty() ? TEXT("") : TEXT(", ")) + Body.Name;
	}
	UE_LOG(LogHCJ, Log, TEXT("Backdrop: Milky Way %s, %d stars, bodies [%s], black holes %s, nebulae %s"),
		SkyDome->GetStaticMesh() != nullptr ? TEXT("on") : TEXT("off"), NumStars, *BodyNames,
		BlackHoleMaterial != nullptr ? TEXT("on") : TEXT("off"), Nebulae->GetStaticMesh() != nullptr ? TEXT("on") : TEXT("off"));
}

FHCJSectorSky AHCJSpaceBackdrop::MakeSky(int32 Sector) const
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	return FHCJSectorSky::Make(MatchSeed, Sector, Bodies.Num(), Settings.MaxBodiesPerSector,
		Nebulae->GetStaticMesh() != nullptr ? Settings.NebulaCardsPerSector : 0);
}

FString AHCJSpaceBackdrop::GetSkyLabel(int32 Sector) const
{
	if (Sector == INDEX_NONE || !bFixedSkyBuilt)
	{
		return FString();
	}

	const FHCJSectorSky Sky = MakeSky(Sector);
	FString Label;
	for (const FHCJSectorSky::FBody& Body : Sky.Bodies)
	{
		if (Bodies.IsValidIndex(Body.BodyIndex))
		{
			Label += (Label.IsEmpty() ? TEXT("") : TEXT(", ")) + Bodies[Body.BodyIndex].Name.ToUpper();
		}
	}
	return Label;
}

void AHCJSpaceBackdrop::ShowSector(int32 Sector)
{
	ShownSector = Sector;

	Planets->ClearInstances();
	Nebulae->ClearInstances();
	for (int32 Index = 0; Index < MaxPlanetBalls; ++Index)
	{
		PlanetBalls[Index]->SetVisibility(false);
		PlanetRings[Index]->SetVisibility(false);
	}
	int32 NumBalls = 0;
	Moon->SetVisibility(false);
	BlackHole->SetVisibility(false);
	if (Sector == INDEX_NONE)
	{
		return;
	}

	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const double Radius = FMath::Max(Settings.BackdropRadius, 100000.0f);
	const UStaticMesh* QuadMesh = Planets->GetStaticMesh() != nullptr ? Planets->GetStaticMesh().Get() : Nebulae->GetStaticMesh().Get();
	if (QuadMesh == nullptr)
	{
		QuadMesh = Moon->GetStaticMesh() != nullptr ? Moon->GetStaticMesh().Get() : BlackHole->GetStaticMesh().Get();
	}
	const double QuadSize = QuadMesh != nullptr ? 2.0 * QuadMesh->GetBoundingBox().GetExtent().GetMax() : 100.0;

	const AHCJUniverse* Universe = Cast<AHCJUniverse>(GetOwner());
	const FLinearColor SectorTint = Universe != nullptr ? Universe->GetSectorTint(Sector) : FLinearColor::White;
	const FHCJSectorSky Sky = MakeSky(Sector);

	for (const FHCJSectorSky::FBody& Body : Sky.Bodies)
	{
		if (!Bodies.IsValidIndex(Body.BodyIndex))
		{
			continue;
		}
		const FCatalogBody& Catalog = Bodies[Body.BodyIndex];
		const double AngularDiameter = Body.AngularDiameterDegrees * Catalog.SizeFactor * Settings.BodySizeScale;
		if (Catalog.AtlasColumn == INDEX_NONE)
		{
			Moon->SetRelativeTransform(MakeCardTransform(Body.Direction, HCJBodyLayer * Radius, AngularDiameter, QuadSize));
			Moon->SetVisibility(true);
			if (MoonMaterial != nullptr && !Settings.MoonPhaseParameter.IsNone())
			{
				// Never new moon: between a clear crescent and full.
				MoonMaterial->SetScalarParameterValue(Settings.MoonPhaseParameter, FMath::Lerp(0.15f, 0.5f, Body.Phase));
			}
		}
		else if (NumBalls < MaxPlanetBalls && PlanetBallMaterials[NumBalls] != nullptr)
		{
			// Its own sphere: the picture of the planet faces the viewer, its axis is tilted a little, the sun lights one side.
			const int32 Ball = NumBalls++;
			const double Distance = HCJPlanetBallLayer * Radius;
			const double BallRadius = Distance * FMath::Tan(FMath::DegreesToRadians(0.5 * FMath::Clamp(AngularDiameter, 0.1, 60.0)));
			const double MeshRadius = FMath::Max(PlanetBalls[Ball]->GetStaticMesh()->GetBoundingBox().GetExtent().GetMax(), 1.0);
			const FVector Facing = -Body.Direction;
			FVector Up = FVector::VectorPlaneProject(FMath::Abs(Facing.Z) < 0.95 ? FVector::UpVector : FVector::RightVector, Facing).GetSafeNormal();
			Up = Up.RotateAngleAxis(-28.0 + 56.0 * Body.Phase, Facing);
			const FVector Right = FVector::CrossProduct(Up, -Facing);
			PlanetBalls[Ball]->SetRelativeTransform(FTransform(FRotator::ZeroRotator, Body.Direction * Distance, FVector(BallRadius / MeshRadius)));
			PlanetBalls[Ball]->SetVisibility(true);
			UMaterialInstanceDynamic* Surface = PlanetBallMaterials[Ball];
			Surface->SetVectorParameterValue(TEXT("Facing"), FLinearColor(Facing));
			Surface->SetVectorParameterValue(TEXT("Right"), FLinearColor(Right));
			Surface->SetVectorParameterValue(TEXT("Up"), FLinearColor(Up));
			Surface->SetVectorParameterValue(TEXT("Sun"), FLinearColor(SunDirectionWS));
			Surface->SetVectorParameterValue(TEXT("Atmosphere"), Catalog.Atmosphere);
			Surface->SetScalarParameterValue(TEXT("Column"), float(Catalog.AtlasColumn));
			Surface->SetScalarParameterValue(TEXT("Kind"), Catalog.bBanded ? 1.0f : 0.0f);
			Surface->SetScalarParameterValue(TEXT("Brightness"), Settings.PlanetBrightness);
			Surface->SetScalarParameterValue(TEXT("Clouds"), Catalog.Clouds);
			Surface->SetScalarParameterValue(TEXT("Rim"), Catalog.RimStrength * Settings.PlanetRimBrightness);
			if (Catalog.RingOuter > 1.0f && PlanetRingMaterials[Ball] != nullptr)
			{
				// The rings lie in the planet's equator, opened 12 to 26 degrees towards the viewer.
				const double Open = FMath::DegreesToRadians(12.0 + 14.0 * Body.Phase);
				const FVector RingNormal = (Up * FMath::Cos(Open) + Facing * FMath::Sin(Open)).GetSafeNormal();
				const double Outer = BallRadius * Catalog.RingOuter;
				PlanetRings[Ball]->SetRelativeTransform(FTransform(FRotationMatrix::MakeFromZX(RingNormal, Right).Rotator(), Body.Direction * Distance, FVector(2.0 * Outer / 100.0)));
				PlanetRings[Ball]->SetVisibility(true);
				UMaterialInstanceDynamic* Ring = PlanetRingMaterials[Ball];
				Ring->SetVectorParameterValue(TEXT("Color"), Catalog.RingColor);
				Ring->SetVectorParameterValue(TEXT("Sun"), FLinearColor(SunDirectionWS));
				Ring->SetScalarParameterValue(TEXT("Inner"), Catalog.RingInner / Catalog.RingOuter);
				Ring->SetScalarParameterValue(TEXT("PlanetRadius"), float(BallRadius));
				Ring->SetScalarParameterValue(TEXT("Brightness"), Settings.PlanetBrightness);
				Ring->SetScalarParameterValue(TEXT("Opacity"), Catalog.RingOpacity);
				Ring->SetScalarParameterValue(TEXT("Seed"), 10.0f * Body.Phase);
			}
		}
		else
		{
			// A real, lit sphere: the sun light of the universe gives it its phase. Axis tilted a little, per body.
			const double Distance = HCJBodyLayer * Radius;
			const double Diameter = 2.0 * Distance * FMath::Tan(FMath::DegreesToRadians(0.5 * FMath::Clamp(AngularDiameter, 0.1, 60.0)));
			const double MeshDiameter = 2.0 * FMath::Max(Planets->GetStaticMesh()->GetBoundingBox().GetExtent().GetMax(), 1.0);
			const FRotator Tilt(-20.0 + 40.0 * Body.Phase, 360.0 * Body.Phase, 25.0 * (Body.Phase - 0.5));
			const int32 Instance = Planets->AddInstance(FTransform(Tilt, Body.Direction * Distance, FVector(Diameter / MeshDiameter)), /*bWorldSpace*/ false);
			const float CustomData[1] = { float(Catalog.AtlasColumn) };
			Planets->SetCustomData(Instance, MakeArrayView(CustomData, 1), /*bMarkRenderStateDirty*/ true);
		}
	}

	if (Universe != nullptr && Universe->HasBlackHole(Sector) && BlackHoleMaterial != nullptr)
	{
		BlackHole->SetVisibility(true);
		BlackHoleMaterial->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor(1.0f, 0.42f, 0.1f), SectorTint, 0.25f));
		BlackHoleMaterial->SetScalarParameterValue(TEXT("Brightness"), Settings.BlackHoleBrightness);
		UpdateBlackHoleCard(GetActorLocation());
	}

	for (const FHCJSectorSky::FNebula& Nebula : Sky.Nebulae)
	{
		// The sector's own colour, shifted a little per cloud.
		FLinearColor HSV = SectorTint.LinearRGBToHSV();
		HSV.R = FMath::Fmod(HSV.R + Nebula.HueShiftDegrees + 360.0f, 360.0f);
		HSV.G *= 0.85f;
		const FLinearColor Color = HSV.HSVToLinearRGB() * Nebula.Strength;

		const int32 Instance = Nebulae->AddInstance(MakeCardTransform(Nebula.Direction, HCJNebulaLayer * Radius, Nebula.AngularSizeDegrees, QuadSize, Nebula.RollDegrees), /*bWorldSpace*/ false);
		const float CustomData[4] = { Color.R, Color.G, Color.B, Nebula.Seed };
		Nebulae->SetCustomData(Instance, MakeArrayView(CustomData, 4), /*bMarkRenderStateDirty*/ true);
	}
}

void AHCJSpaceBackdrop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const APlayerController* LocalController = World != nullptr && GEngine != nullptr ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	if (LocalController == nullptr)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	LocalController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	SetActorLocation(ViewLocation);

	const AHCJUniverse* Universe = Cast<AHCJUniverse>(GetOwner());
	const int32 Sector = Universe != nullptr && Universe->IsBuilt() ? Universe->GetSectorAtLocation(ViewLocation) : INDEX_NONE;
	if (Sector != ShownSector)
	{
		ShowSector(Sector);
	}
	UpdateBlackHoleCard(ViewLocation);
}

void AHCJSpaceBackdrop::UpdateBlackHoleCard(const FVector& ViewLocation)
{
	const AHCJUniverse* Universe = Cast<AHCJUniverse>(GetOwner());
	if (Universe == nullptr || !BlackHole->IsVisible() || !Universe->HasBlackHole(ShownSector) || BlackHole->GetStaticMesh() == nullptr)
	{
		return;
	}

	// The dark disc of the material is the event horizon; the accretion disc reaches five times as far.
	const FVector HoleLocation = Universe->GetBlackHoleLocation(ShownSector);
	const double QuadSize = 2.0 * FMath::Max(BlackHole->GetStaticMesh()->GetBoundingBox().GetExtent().GetMax(), 1.0);
	const double Width = 2.0 * Universe->GetBlackHoleSettings().HorizonRadius / HCJBlackHoleShadowFraction;
	FVector ToView = ViewLocation - HoleLocation;
	if (!ToView.Normalize())
	{
		ToView = FVector::ForwardVector;
	}
	// Card normal (+X) at the camera, the disc tilted a little for looks.
	const FQuat Facing = FRotationMatrix::MakeFromX(ToView).ToQuat() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(20.0));
	BlackHole->SetWorldTransform(FTransform(Facing, HoleLocation, FVector(1.0, Width / QuadSize, Width / QuadSize)));
}
