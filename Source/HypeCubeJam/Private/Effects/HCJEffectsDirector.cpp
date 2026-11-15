#include "Effects/HCJEffectsDirector.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

#include "Assets/HCJAssetSettings.h"
#include "Game/HCJGameMode.h"
#include "Game/HCJGameplaySettings.h"
#include "Game/HCJUserSettings.h"
#include "HypeCubeJam.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Ship/HCJShipCharacter.h"
#include "World/HCJUniverse.h"

namespace
{
	/** Share of its speed a piece loses per second, how long the camera shakes, how bouncy a rock is. */
	constexpr float DebrisDrag = 0.45f;
	constexpr float ShakeSeconds = 0.45f;
	constexpr float DebrisRestitution = 0.45f;
	/** Rock chunks are drawn with at most this many of the rock meshes. */
	constexpr int32 MaxRockKinds = 2;
	/** Ships going up at the same time that are kept track of; one more replaces the one that is furthest along. */
	constexpr int32 MaxShipBlasts = 8;
	/** A spark is drawn as long as it flies in this many seconds (within limits, uu). */
	constexpr float SparkStreakSeconds = 0.05f;
	/** The base colour of the fire; a little of the ship's own colour is mixed in. */
	const FLinearColor FireColor(1.0f, 0.5f, 0.14f);
}

bool UHCJEffectsDirector::IsClassicBlast()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	static const bool bClassic = FParse::Param(FCommandLine::Get(), TEXT("HCJClassicBlast"));
	return bClassic;
#endif
}

float UHCJEffectsDirector::GetHullFailSeconds()
{
	return IsClassicBlast() ? 0.0f : FMath::Max(UHCJAssetSettings::Get().HullFailSeconds, 0.0f);
}

UHCJEffectsDirector* UHCJEffectsDirector::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World != nullptr ? World->GetSubsystem<UHCJEffectsDirector>() : nullptr;
}

bool UHCJEffectsDirector::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UHCJEffectsDirector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHCJEffectsDirector, STATGROUP_Tickables);
}

bool UHCJEffectsDirector::IsTickable() const
{
	// Nothing ticks while nothing flies, flares or shakes.
	return bReady && (Debris.NumActive() > 0 || GetNumFlares() > 0 || NumActiveStars > 0 || ShakeAmount > 0.0f
		|| NumActiveBlasts > 0 || NumActivePuffs > 0 || NumActiveRings > 0 || NumActiveTrails > 0);
}

void UHCJEffectsDirector::Deinitialize()
{
	if (IsValid(Host))
	{
		Host->Destroy();
	}
	Host = nullptr;
	DebrisMeshes.Reset();
	DebrisMaterials.Reset();
	FlareQuads.Reset();
	FlareMaterials.Reset();
	StarQuads.Reset();
	StarMaterials.Reset();
	Stars.Reset();
	NumActiveStars = 0;
	PuffQuads.Reset();
	PuffMaterials.Reset();
	RingQuads.Reset();
	RingMaterials.Reset();
	TrailComponents.Reset();
	Puffs.Reset();
	Rings.Reset();
	Trails.Reset();
	Blasts.Reset();
	NumActivePuffs = 0;
	NumActiveRings = 0;
	NumActiveTrails = 0;
	NumActiveBlasts = 0;
	bReady = false;
	Super::Deinitialize();
}

int32 UHCJEffectsDirector::GetNumFlares() const
{
	int32 Num = 0;
	for (const FFlare& Flare : Flares)
	{
		Num += Flare.bActive ? 1 : 0;
	}
	return Num;
}

bool UHCJEffectsDirector::GetLocalView(FVector& OutLocation, FRotator& OutRotation) const
{
	const UWorld* World = GetWorld();
	const APlayerController* LocalController = (World != nullptr && GEngine != nullptr) ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	if (LocalController == nullptr)
	{
		return false;
	}
	LocalController->GetPlayerViewPoint(OutLocation, OutRotation);
	return true;
}

bool UHCJEffectsDirector::EnsureReady()
{
	if (bReady || bFailed)
	{
		return bReady;
	}
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer || World->bIsTearingDown)
	{
		return false;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	Random.Initialize(HCJ::MixSeed(static_cast<int32>(FPlatformTime::Cycles64()), 0x0EFFEC75));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Host = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (Host == nullptr)
	{
		bFailed = true;
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	Host->SetRootComponent(Root);
	Root->RegisterComponent();

	// Debris meshes: a couple of the rock meshes for chunks of rock, the engine's cube (a dark plate) for pieces of a hull.
	const int32 Capacity = FMath::Clamp(Settings.MaxDebris, 0, 256);
	Debris.SetCapacity(Capacity);
	SlotKinds.Init(INDEX_NONE, Capacity);
	const auto AddDebrisMesh = [this, Root, Capacity](UStaticMesh* Mesh, UMaterialInterface* Material, int32 NumCustomFloats = 0)
	{
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Host, NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetNumCustomDataFloats(NumCustomFloats);
		if (Material != nullptr)
		{
			Component->SetMaterial(0, Material);
		}
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		Component->SetReceivesDecals(false);
		Component->SetupAttachment(Root);
		TArray<FTransform> Hidden;
		Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Capacity);
		Component->AddInstances(Hidden, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false, /*bUpdateNavigation*/ false);
		Component->RegisterComponent();
		DebrisMeshes.Add(Component);
		DebrisMeshSizes.Add(FMath::Max(static_cast<float>(Mesh->GetBoundingBox().GetSize().GetMax()), 1.0f));
	};
	if (Capacity > 0)
	{
		for (const FHCJAsteroidMeshVariant& Variant : Settings.AsteroidMeshes)
		{
			if (NumRockKinds >= MaxRockKinds)
			{
				break;
			}
			if (UStaticMesh* Rock = UHCJAssetSettings::Load<UStaticMesh>(Variant.Mesh, TEXT("rock debris")); Rock != nullptr && Rock->GetBoundingBox().IsValid)
			{
				AddDebrisMesh(Rock, nullptr);
				++NumRockKinds;
			}
		}
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (Cube != nullptr)
		{
			// Hull plates: dark metal whose torn edges glow while the piece is hot (heat: per-instance custom data 0);
			// the plain light grey of before with -HCJClassicBlast or when that material is missing.
			UMaterialInterface* HullMaterial = IsClassicBlast() ? nullptr : UHCJAssetSettings::Load<UMaterialInterface>(Settings.HullDebrisMaterial, TEXT("hull debris"));
			UMaterialInstanceDynamic* Plate = HullMaterial == nullptr && ShapeMaterial != nullptr ? UMaterialInstanceDynamic::Create(ShapeMaterial, Host) : nullptr;
			if (Plate != nullptr)
			{
				Plate->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.42f, 0.44f, 0.48f));
				Plate->SetScalarParameterValue(TEXT("Roughness"), 0.35f);
				DebrisMaterials.Add(Plate);
			}
			bPlatesHaveHeat = HullMaterial != nullptr;
			PlateKind = DebrisMeshes.Num();
			AddDebrisMesh(Cube, HullMaterial != nullptr ? HullMaterial : static_cast<UMaterialInterface*>(Plate), bPlatesHaveHeat ? 1 : 0);

			// Embers: the same cube with the unlit glow of the pads and beacons, so that a wreck reads against black space.
			UMaterialInterface* Glow = UHCJAssetSettings::Load<UMaterialInterface>(Settings.PickupMaterial, TEXT("debris ember glow"));
			UMaterialInstanceDynamic* Ember = Glow != nullptr ? UMaterialInstanceDynamic::Create(Glow, Host) : nullptr;
			if (Ember != nullptr)
			{
				if (!Settings.PickupColorParameter.IsNone())
				{
					Ember->SetVectorParameterValue(Settings.PickupColorParameter, FLinearColor(1.0f, 0.42f, 0.1f));
				}
				if (!Settings.PickupBrightnessParameter.IsNone())
				{
					Ember->SetScalarParameterValue(Settings.PickupBrightnessParameter, Settings.DebrisEmberBrightness);
				}
				DebrisMaterials.Add(Ember);
				EmberKind = DebrisMeshes.Num();
				AddDebrisMesh(Cube, Ember);
			}
			// Sparks: the same cube again, drawn out along its flight, hotter and brighter than an ember.
			UMaterialInstanceDynamic* Spark = Glow != nullptr ? UMaterialInstanceDynamic::Create(Glow, Host) : nullptr;
			if (Spark != nullptr)
			{
				if (!Settings.PickupColorParameter.IsNone())
				{
					Spark->SetVectorParameterValue(Settings.PickupColorParameter, FLinearColor(1.0f, 0.7f, 0.32f));
				}
				if (!Settings.PickupBrightnessParameter.IsNone())
				{
					Spark->SetScalarParameterValue(Settings.PickupBrightnessParameter, Settings.DebrisEmberBrightness * 2.5f);
				}
				DebrisMaterials.Add(Spark);
				SparkKind = DebrisMeshes.Num();
				AddDebrisMesh(Cube, Spark);
			}
		}
	}

	// Flares: quads on the engine's plane, hidden until used.
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* FlareBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.ExplosionFlareMaterial, TEXT("explosion flare"));
	const int32 NumFlares = (Plane != nullptr && FlareBase != nullptr) ? FMath::Clamp(Settings.MaxFlares, 0, 16) : 0;
	Flares.SetNum(NumFlares);
	for (int32 Index = 0; Index < NumFlares; ++Index)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(FlareBase, Host);
		UStaticMeshComponent* Quad = NewObject<UStaticMeshComponent>(Host, NAME_None, RF_Transient);
		Quad->SetMobility(EComponentMobility::Movable);
		Quad->SetStaticMesh(Plane);
		Quad->SetMaterial(0, Material);
		Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Quad->SetGenerateOverlapEvents(false);
		Quad->SetCanEverAffectNavigation(false);
		Quad->SetCastShadow(false);
		Quad->SetReceivesDecals(false);
		Quad->SetVisibility(false);
		Quad->SetupAttachment(Root);
		Quad->RegisterComponent();
		FlareQuads.Add(Quad);
		FlareMaterials.Add(Material);
	}

	// Impact stars: the same again with M_HCJ_ImpactStar.
	UMaterialInterface* StarBase = UHCJAssetSettings::Load<UMaterialInterface>(Settings.ImpactStarMaterial, TEXT("impact star"));
	const int32 NumStarQuads = (Plane != nullptr && StarBase != nullptr) ? FMath::Clamp(Settings.MaxImpactStars, 0, 24) : 0;
	Stars.SetNum(NumStarQuads);
	for (int32 Index = 0; Index < NumStarQuads; ++Index)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(StarBase, Host);
		UStaticMeshComponent* Quad = NewObject<UStaticMeshComponent>(Host, NAME_None, RF_Transient);
		Quad->SetMobility(EComponentMobility::Movable);
		Quad->SetStaticMesh(Plane);
		Quad->SetMaterial(0, Material);
		Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Quad->SetGenerateOverlapEvents(false);
		Quad->SetCanEverAffectNavigation(false);
		Quad->SetCastShadow(false);
		Quad->SetReceivesDecals(false);
		Quad->SetVisibility(false);
		Quad->SetupAttachment(Root);
		Quad->RegisterComponent();
		StarQuads.Add(Quad);
		StarMaterials.Add(Material);
	}

	// The staged explosion of a ship: fireball puffs, shockwave rings, trails of burning pieces.
	const auto MakeQuads = [this, Root, Plane](UMaterialInterface* Base, int32 Num, int32 SortPriority, TArray<TObjectPtr<UStaticMeshComponent>>& OutQuads, TArray<TObjectPtr<UMaterialInstanceDynamic>>& OutMaterials)
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Host);
			UStaticMeshComponent* Quad = NewObject<UStaticMeshComponent>(Host, NAME_None, RF_Transient);
			Quad->SetMobility(EComponentMobility::Movable);
			Quad->SetStaticMesh(Plane);
			Quad->SetMaterial(0, Material);
			Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Quad->SetGenerateOverlapEvents(false);
			Quad->SetCanEverAffectNavigation(false);
			Quad->SetCastShadow(false);
			Quad->SetReceivesDecals(false);
			Quad->SetTranslucentSortPriority(SortPriority);
			Quad->SetVisibility(false);
			Quad->SetupAttachment(Root);
			Quad->RegisterComponent();
			OutQuads.Add(Quad);
			OutMaterials.Add(Material);
		}
	};
	const bool bStaged = !IsClassicBlast();
	UMaterialInterface* FireballBase = bStaged ? UHCJAssetSettings::Load<UMaterialInterface>(Settings.FireballMaterial, TEXT("fireball")) : nullptr;
	const int32 NumPuffs = (Plane != nullptr && FireballBase != nullptr) ? FMath::Clamp(Settings.MaxFireballs, 0, 24) : 0;
	// Smoke covers what is behind it: drawn before the additive flares, rings and stars, so that a flash is never dulled by it.
	MakeQuads(FireballBase, NumPuffs, -1, PuffQuads, PuffMaterials);
	Puffs.SetNum(NumPuffs);
	UMaterialInterface* RingBase = bStaged ? UHCJAssetSettings::Load<UMaterialInterface>(Settings.ShockRingMaterial, TEXT("shockwave ring")) : nullptr;
	const int32 NumRings = (Plane != nullptr && RingBase != nullptr) ? FMath::Clamp(Settings.MaxShockRings, 0, 8) : 0;
	MakeQuads(RingBase, NumRings, 0, RingQuads, RingMaterials);
	Rings.SetNum(NumRings);
	UNiagaraSystem* TrailSystem = bStaged ? UHCJAssetSettings::Load<UNiagaraSystem>(Settings.DebrisTrailEffect, TEXT("debris trail")) : nullptr;
	const int32 NumTrails = (TrailSystem != nullptr && PlateKind != INDEX_NONE) ? FMath::Clamp(Settings.MaxDebrisTrails, 0, 8) : 0;
	for (int32 Index = 0; Index < NumTrails; ++Index)
	{
		UNiagaraComponent* Trail = NewObject<UNiagaraComponent>(Host, NAME_None, RF_Transient);
		Trail->SetAutoActivate(false);
		Trail->SetAsset(TrailSystem);
		Trail->SetWorldScale3D(FVector(Settings.DebrisTrailScale));
		Trail->SetupAttachment(Root);
		Trail->RegisterComponent();
		TrailComponents.Add(Trail);
	}
	Trails.SetNum(NumTrails);
	Blasts.SetNum(MaxShipBlasts);

	bReady = true;
	UE_LOG(LogHCJ, Log, TEXT("Effects: %d debris slots on %d meshes (%d rock, hull plate %s, ember %s, spark %s), %d flare quads, %d fireball puffs, %d shockwave rings, %d debris trails, ship blast %s, camera shake %s"),
		Capacity, DebrisMeshes.Num(), NumRockKinds, PlateKind != INDEX_NONE ? (bPlatesHaveHeat ? TEXT("with heat") : TEXT("plain")) : TEXT("no"), EmberKind != INDEX_NONE ? TEXT("yes") : TEXT("no"),
		SparkKind != INDEX_NONE ? TEXT("yes") : TEXT("no"), NumFlares, NumPuffs, NumRings, NumTrails, bStaged ? TEXT("staged") : TEXT("classic (-HCJClassicBlast)"),
		UHCJUserSettings::Get().bCameraShake ? TEXT("on") : TEXT("off"));
	return true;
}

void UHCJEffectsDirector::HideDebrisInstance(int32 Slot, int32 Kind)
{
	if (DebrisMeshes.IsValidIndex(Kind) && DebrisMeshes[Kind] != nullptr)
	{
		DebrisMeshes[Kind]->UpdateInstanceTransform(Slot, FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
	}
}

void UHCJEffectsDirector::SpawnFlare(const FVector& Location, float Size, const FLinearColor& Color)
{
	if (Flares.Num() == 0 || !UHCJAssetSettings::IsNearLocalView(this, Location, HCJ::NetCullDistance))
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	// A free quad, else the one that is nearly over.
	int32 Slot = 0;
	float BestLeft = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Flares.Num(); ++Index)
	{
		const float Left = Flares[Index].bActive ? Flares[Index].Duration - Flares[Index].Age : -1.0f;
		if (Left < BestLeft)
		{
			BestLeft = Left;
			Slot = Index;
		}
	}
	FVector ViewLocation = Location;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetLocalView(ViewLocation, ViewRotation);
	// Close to the view the quad covers the screen: dimmer the nearer, so the pilot keeps seeing.
	const float Nearness = FMath::Clamp(static_cast<float>(FVector::Dist(ViewLocation, Location)) / 4500.0f, 0.15f, 1.0f);

	FFlare& Flare = Flares[Slot];
	Flare.bActive = true;
	Flare.Age = 0.0f;
	Flare.Duration = FMath::Max(Settings.FlareSeconds, 0.05f);
	Flare.Brightness = Settings.FlareBrightness * Nearness;
	UMaterialInstanceDynamic* Material = FlareMaterials[Slot];
	Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.R, Color.G, Color.B, 1.0f));
	Material->SetScalarParameterValue(TEXT("Brightness"), Flare.Brightness);
	Material->SetScalarParameterValue(TEXT("Age"), 0.0f);
	Material->SetScalarParameterValue(TEXT("Seed"), Random.FRandRange(0.0f, 3.1f));
	UStaticMeshComponent* Quad = FlareQuads[Slot];
	Quad->SetWorldLocation(Location);
	// The plane is 100 uu across; the streak runs along its X, which is turned level with the screen.
	Quad->SetWorldScale3D(FVector(Size / 100.0f, Size / 100.0f, 1.0f));
	const FVector ToView = ViewLocation - Location;
	if (ToView.SizeSquared() > 1.0)
	{
		Quad->SetWorldRotation(FRotationMatrix::MakeFromZX(ToView, FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y)).Rotator());
	}
	Quad->SetVisibility(true);
	++NumFlaresSpawned;
}

void UHCJEffectsDirector::PlayImpactStar(const FVector& Location, float Scale, const FLinearColor& Color)
{
	if (!EnsureReady() || Stars.Num() == 0)
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (!UHCJAssetSettings::IsNearLocalView(this, Location, Settings.EffectMaxDistance * 2.0f))
	{
		return;
	}
	// A free quad, else the one that is nearly over.
	int32 Slot = 0;
	float BestLeft = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Stars.Num(); ++Index)
	{
		const float Left = Stars[Index].bActive ? Stars[Index].Duration - Stars[Index].Age : -1.0f;
		if (Left < BestLeft)
		{
			BestLeft = Left;
			Slot = Index;
		}
	}
	FVector ViewLocation = Location;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetLocalView(ViewLocation, ViewRotation);
	// Right in front of the view a star would cover it: dimmer and smaller the nearer.
	float Nearness = FMath::Clamp(static_cast<float>(FVector::Dist(ViewLocation, Location)) / 2500.0f, 0.25f, 1.0f);
	// A hit on the ship the view follows (the chase camera sits right behind it) must not hide that ship: smaller and dimmer still.
	const UWorld* World = GetWorld();
	const APlayerController* LocalController = (World != nullptr && GEngine != nullptr) ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	const AActor* ViewTarget = LocalController != nullptr ? LocalController->GetViewTarget() : nullptr;
	if (ViewTarget != nullptr && FVector::DistSquared(ViewTarget->GetActorLocation(), Location) < FMath::Square(500.0))
	{
		Scale *= 0.4f;
		Nearness *= 0.5f;
	}

	FFlare& Star = Stars[Slot];
	NumActiveStars += Star.bActive ? 0 : 1;
	Star.bActive = true;
	Star.Age = 0.0f;
	Star.Duration = FMath::Max(Settings.ImpactStarSeconds, 0.05f);
	Star.Brightness = Settings.ImpactStarBrightness * Nearness;
	UMaterialInstanceDynamic* Material = StarMaterials[Slot];
	Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.R, Color.G, Color.B, 1.0f));
	Material->SetScalarParameterValue(TEXT("Brightness"), Star.Brightness);
	Material->SetScalarParameterValue(TEXT("Age"), 0.0f);
	Material->SetScalarParameterValue(TEXT("Seed"), Random.FRandRange(0.0f, 10.0f));
	UStaticMeshComponent* Quad = StarQuads[Slot];
	const float Size = Settings.ImpactStarSize * Scale * FMath::Lerp(0.6f, 1.0f, Nearness);
	const FVector ToView = ViewLocation - Location;
	// A little towards the view, so that what was hit does not cut the quad in two.
	Quad->SetWorldLocation(Location + ToView.GetSafeNormal() * FMath::Min(60.0, 0.5 * ToView.Size()));
	Quad->SetWorldScale3D(FVector(Size / 100.0f, Size / 100.0f, 1.0f));
	if (ToView.SizeSquared() > 1.0)
	{
		Quad->SetWorldRotation(FRotationMatrix::MakeFromZX(ToView, FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y)).Rotator());
	}
	Quad->SetVisibility(true);
	++NumStarsSpawned;
}

int32 UHCJEffectsDirector::SpawnDebris(const FVector& Location, const FVector& BaseVelocity, int32 Count, int32 KindMin, int32 KindMax, float Spread, float Speed,
	const FVector& ScaleMin, const FVector& ScaleMax, bool bUniformScale, float LifeScale)
{
	if (Debris.GetCapacity() == 0 || DebrisMeshes.Num() == 0 || KindMax < KindMin)
	{
		return INDEX_NONE;
	}
	const float Life = FMath::Max(UHCJAssetSettings::Get().DebrisSeconds * LifeScale, 0.1f);
	int32 FirstSlot = INDEX_NONE;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Direction = Random.VRand();
		FHCJDebrisPiece Piece;
		Piece.Kind = static_cast<uint8>(Random.RandRange(KindMin, KindMax));
		Piece.Location = Location + Direction * Random.FRandRange(0.15f, 1.0f) * Spread;
		Piece.Velocity = BaseVelocity + Direction * Speed * Random.FRandRange(0.35f, 1.0f);
		Piece.Axis = Random.VRand();
		Piece.Angle = Random.FRandRange(0.0f, 360.0f);
		Piece.AngularSpeed = Random.FRandRange(90.0f, 420.0f) * (Random.FRand() < 0.5f ? -1.0f : 1.0f);
		const float Uniform = Random.FRand();
		Piece.Scale = bUniformScale ? FMath::Lerp(ScaleMin, ScaleMax, Uniform)
			: FVector(FMath::Lerp(ScaleMin.X, ScaleMax.X, Random.FRand()), FMath::Lerp(ScaleMin.Y, ScaleMax.Y, Random.FRand()), FMath::Lerp(ScaleMin.Z, ScaleMax.Z, Random.FRand()));
		Piece.Life = Life * Random.FRandRange(0.7f, 1.0f);
		const int32 Slot = Debris.Spawn(Piece);
		if (Slot == INDEX_NONE)
		{
			return FirstSlot;
		}
		if (SlotKinds[Slot] != INDEX_NONE && SlotKinds[Slot] != Piece.Kind)
		{
			HideDebrisInstance(Slot, SlotKinds[Slot]);
		}
		SlotKinds[Slot] = Piece.Kind;
		++NumDebrisSpawned;
		if (FirstSlot == INDEX_NONE)
		{
			FirstSlot = Slot;
		}
	}
	return FirstSlot;
}

void UHCJEffectsDirector::SpawnPuff(const FVector& Location, const FVector& Velocity, float Size, float Seconds, float FireSeconds, float Brightness, float Smoke, const FLinearColor& Color)
{
	if (Puffs.Num() == 0)
	{
		return;
	}
	// A free quad, else the one that is nearly over.
	int32 Slot = 0;
	float BestLeft = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Puffs.Num(); ++Index)
	{
		const float Left = Puffs[Index].bActive ? Puffs[Index].Duration - Puffs[Index].Age : -1.0f;
		if (Left < BestLeft)
		{
			BestLeft = Left;
			Slot = Index;
		}
	}
	FVector ViewLocation = Location;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetLocalView(ViewLocation, ViewRotation);
	// Right in front of the view a fireball would be all there is to see: smaller, dimmer and thinner the nearer.
	const float Nearness = FMath::Clamp(static_cast<float>(FVector::Dist(ViewLocation, Location)) / 2200.0f, 0.35f, 1.0f);

	FFlare& Puff = Puffs[Slot];
	NumActivePuffs += Puff.bActive ? 0 : 1;
	Puff.bActive = true;
	Puff.Age = 0.0f;
	Puff.Duration = FMath::Max(Seconds, 0.05f);
	Puff.Brightness = Brightness * Nearness;
	Puff.Velocity = Velocity;
	UMaterialInstanceDynamic* Material = PuffMaterials[Slot];
	Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.R, Color.G, Color.B, 1.0f));
	Material->SetScalarParameterValue(TEXT("Brightness"), Puff.Brightness);
	Material->SetScalarParameterValue(TEXT("Age"), 0.0f);
	Material->SetScalarParameterValue(TEXT("Seconds"), Puff.Duration);
	Material->SetScalarParameterValue(TEXT("Fire"), FMath::Clamp(FireSeconds, 0.05f, Puff.Duration));
	Material->SetScalarParameterValue(TEXT("Seed"), Random.FRandRange(0.0f, 1.0f));
	Material->SetScalarParameterValue(TEXT("Smoke"), Smoke * Nearness);
	UStaticMeshComponent* Quad = PuffQuads[Slot];
	const float Shown = Size * FMath::Lerp(0.65f, 1.0f, Nearness);
	Quad->SetWorldLocation(Location);
	Quad->SetWorldScale3D(FVector(Shown / 100.0f, Shown / 100.0f, 1.0f));
	const FVector ToView = ViewLocation - Location;
	if (ToView.SizeSquared() > 1.0)
	{
		Quad->SetWorldRotation(FRotationMatrix::MakeFromZX(ToView, FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y)).Rotator());
	}
	Quad->SetVisibility(true);
}

void UHCJEffectsDirector::SpawnRing(const FVector& Location, const FVector& Normal, float Size, float Seconds, const FLinearColor& Color)
{
	if (Rings.Num() == 0)
	{
		return;
	}
	int32 Slot = 0;
	float BestLeft = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Rings.Num(); ++Index)
	{
		const float Left = Rings[Index].bActive ? Rings[Index].Duration - Rings[Index].Age : -1.0f;
		if (Left < BestLeft)
		{
			BestLeft = Left;
			Slot = Index;
		}
	}
	FFlare& Ring = Rings[Slot];
	NumActiveRings += Ring.bActive ? 0 : 1;
	Ring.bActive = true;
	Ring.Age = 0.0f;
	Ring.Duration = FMath::Max(Seconds, 0.05f);
	Ring.Brightness = UHCJAssetSettings::Get().RingBrightness;
	UMaterialInstanceDynamic* Material = RingMaterials[Slot];
	Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.R, Color.G, Color.B, 1.0f));
	Material->SetScalarParameterValue(TEXT("Brightness"), Ring.Brightness);
	Material->SetScalarParameterValue(TEXT("Age"), 0.0f);
	Material->SetScalarParameterValue(TEXT("Seed"), Random.FRandRange(0.0f, 1.0f));
	UStaticMeshComponent* Quad = RingQuads[Slot];
	Quad->SetWorldLocation(Location);
	Quad->SetWorldScale3D(FVector(Size / 100.0f, Size / 100.0f, 1.0f));
	// The quad lies in the plane: its own Z is the normal.
	Quad->SetWorldRotation(FRotationMatrix::MakeFromZ(Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector)).Rotator());
	Quad->SetVisibility(true);
}

void UHCJEffectsDirector::StartDebrisTrail(int32 Slot, float Seconds)
{
	if (Slot == INDEX_NONE || Slot >= Debris.GetCapacity() || !Debris.GetPieces()[Slot].bActive)
	{
		return;
	}
	for (int32 Index = 0; Index < Trails.Num(); ++Index)
	{
		FDebrisTrail& Trail = Trails[Index];
		if (Trail.bActive || TrailComponents[Index] == nullptr)
		{
			continue;
		}
		// No free one: the piece simply does not burn (the cap).
		Trail.bActive = true;
		Trail.Slot = Slot;
		Trail.Left = Seconds;
		Trail.LastAge = 0.0f;
		++NumActiveTrails;
		TrailComponents[Index]->SetWorldLocation(Debris.GetPieces()[Slot].Location);
		TrailComponents[Index]->Activate(/*bReset*/ true);
		return;
	}
}

void UHCJEffectsDirector::AddShake(const FVector& Location, float Strength)
{
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!GetLocalView(ViewLocation, ViewRotation))
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const float Amount = HCJEffects::ShakeAt(static_cast<float>(FVector::Dist(ViewLocation, Location)), Settings.ExplosionShakeDistance, Strength * Settings.ExplosionShakeStrength);
	if (Amount > ShakeAmount)
	{
		ShakeAmount = Amount;
		ShakeTime = 0.0f;
	}
}

FVector UHCJEffectsDirector::GetCameraShake() const
{
	if (ShakeAmount <= 0.0f || !UHCJUserSettings::Get().bCameraShake)
	{
		return FVector::ZeroVector;
	}
	const UWorld* World = GetWorld();
	const float Now = World != nullptr ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;
	const float Left = FMath::Clamp(1.0f - ShakeTime / ShakeSeconds, 0.0f, 1.0f);
	const float Amount = ShakeAmount * Left * Left * 22.0f;
	return FVector(0.0, FMath::Sin(Now * 71.0f) * Amount, FMath::Cos(Now * 89.0f) * Amount);
}

void UHCJEffectsDirector::PushDebris(const FVector& Origin, float Radius, float PeakSpeed)
{
	if (bReady && Debris.NumActive() > 0)
	{
		Debris.ApplyShockwave(Origin, Radius, PeakSpeed);
	}
}

void UHCJEffectsDirector::PlayShipExplosion(const FVector& Location, const FVector& Velocity, const FLinearColor& Color)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (!EnsureReady() || !UHCJAssetSettings::IsNearLocalView(this, Location, Settings.EffectMaxDistance * 2.0f))
	{
		return;
	}
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	// The pieces already flying are pushed first; the new ones start with their own speed.
	PushDebris(Location, Gameplay.ShipShockwaveRadius, Gameplay.ShipShockwaveSpeed);
	SpawnFlare(Location, Settings.ShipFlareSize, FMath::Lerp(FLinearColor(1.0f, 0.55f, 0.2f), Color, 0.35f));
	if (PlateKind != INDEX_NONE)
	{
		// Hull plates: thin, half a metre to a metre and a half across.
		SpawnDebris(Location, Velocity * 0.6, Settings.ShipDebrisPieces, PlateKind, PlateKind, 90.0f, Settings.DebrisSpeed, FVector(45.0, 28.0, 5.0), FVector(150.0, 90.0, 14.0), false);
	}
	if (EmberKind != INDEX_NONE)
	{
		// Glowing bits, smaller and faster.
		SpawnDebris(Location, Velocity * 0.6, Settings.ShipDebrisEmbers, EmberKind, EmberKind, 60.0f, Settings.DebrisSpeed * 1.5f, FVector(10.0, 10.0, 10.0), FVector(26.0, 26.0, 26.0), true);
	}
	AddShake(Location, 1.0f);
	UE_LOG(LogHCJ, Verbose, TEXT("Effects: ship explosion at %s, %d debris pieces and %d flares alive"), *Location.ToCompactString(), Debris.NumActive(), GetNumFlares());
}

void UHCJEffectsDirector::PlayShipDeath(const FVector& Location, const FVector& Velocity, const FLinearColor& Color, const FVector& Forward, const FVector& Up, float HullLength, bool bOwnShip)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (!EnsureReady() || Blasts.Num() == 0 || !UHCJAssetSettings::IsNearLocalView(this, Location, Settings.EffectMaxDistance * 2.0f))
	{
		return;
	}
	// A free slot, else the blast that is furthest along; and how many are in their first second.
	int32 Slot = 0;
	float BestTime = -1.0f;
	int32 NumRunning = 0;
	for (int32 Index = 0; Index < Blasts.Num(); ++Index)
	{
		const FShipBlast& Other = Blasts[Index];
		if (!Other.bActive)
		{
			if (BestTime < TNumericLimits<float>::Max())
			{
				BestTime = TNumericLimits<float>::Max();
				Slot = Index;
			}
			continue;
		}
		NumRunning += Other.Time - Other.FailSeconds < 1.0f ? 1 : 0;
		if (Other.Time > BestTime)
		{
			BestTime = Other.Time;
			Slot = Index;
		}
	}
	FVector ViewLocation = Location;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetLocalView(ViewLocation, ViewRotation);

	FShipBlast& Blast = Blasts[Slot];
	NumActiveBlasts += Blast.bActive ? 0 : 1;
	Blast = FShipBlast();
	Blast.bActive = true;
	Blast.Location = Location;
	// What is left of the ship drifts on like its debris.
	Blast.Velocity = Velocity * 0.6;
	Blast.Forward = Forward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	Blast.Up = Up.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Blast.Color = Color;
	Blast.HullLength = FMath::Max(HullLength, 100.0f);
	Blast.FailSeconds = GetHullFailSeconds();
	Blast.bOwn = bOwnShip;
	Blast.Detail = static_cast<uint8>(HCJEffects::PickBlastDetail(static_cast<float>(FVector::Dist(ViewLocation, Location)), bOwnShip, NumRunning,
		Settings.BlastFullDetailDistance, Settings.BlastMediumDetailDistance));
	++NumBlastsPlayed;
	UE_LOG(LogHCJ, Verbose, TEXT("Effects: ship death at %s, %.0f uu from the view, detail %d (%d other blasts running)%s"), *Location.ToCompactString(),
		FVector::Dist(ViewLocation, Location), int32(Blast.Detail), NumRunning, bOwnShip ? TEXT(", the own ship") : TEXT(""));
	// The first pop shows in the frame the ship is lost.
	TickBlasts(0.0f);
}

void UHCJEffectsDirector::RunBlastStage(FShipBlast& Blast)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const bool bFull = Blast.Detail == HCJEffects::BlastFull;
	const bool bLow = Blast.Detail == HCJEffects::BlastLow;
	const FLinearColor Fire = FMath::Lerp(FireColor, Blast.Color, 0.2f);
	const int32 Stage = Blast.Stage;

	if (Stage < HCJEffects::BlastMainStage)
	{
		// The hull fails: a pop somewhere along it - a star, sparks, a crack.
		if (bLow || Blast.FailSeconds <= 0.0f)
		{
			return;
		}
		static const float Along[3] = { -0.3f, 0.28f, -0.02f };
		const FVector At = Blast.Location + Blast.Forward * (Blast.HullLength * Along[Stage]) + Random.VRand() * 35.0;
		PlayImpactStar(At, 0.9f, FLinearColor(1.0f, 0.62f, 0.25f));
		UHCJAssetSettings::SpawnEffect(this, Settings.ImpactShipEffect, TEXT("hull pop"), At, Blast.Forward.Rotation(), Settings.ImpactScale * 2.5f);
		if (SparkKind != INDEX_NONE)
		{
			SpawnDebris(At, Blast.Velocity, bFull ? 4 : 2, SparkKind, SparkKind, 20.0f, Settings.DebrisSpeed * 1.3f, FVector(4.0), FVector(6.0), true, 0.16f);
		}
		UHCJAssetSettings::PlaySoundAt(this, Settings.ImpactShipSounds, TEXT("hull pop sound"), At, Settings.ImpactVolume * 1.1f, 0.12f, EHCJSoundGroup::Impact, 0.7f);
		AddShake(At, Blast.bOwn ? 0.55f : 0.25f);
		return;
	}

	if (Stage == HCJEffects::BlastMainStage)
	{
		// The blast: the core flash, the fireball, the shockwave, everything that flies.
		const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
		PushDebris(Blast.Location, Gameplay.ShipShockwaveRadius, Gameplay.ShipShockwaveSpeed);
		SpawnFlare(Blast.Location, Settings.ShipFlareSize, FMath::Lerp(FLinearColor(1.0f, 0.55f, 0.2f), Blast.Color, 0.35f));
		// The first puff is the heart of the fireball and, when that has burnt out, the smoke that stays.
		SpawnPuff(Blast.Location, Blast.Velocity, Settings.ShipFireballSize * (bLow ? 0.8f : 1.0f), bLow ? Settings.ShipFireballSeconds : Settings.ShipSmokeSeconds,
			Settings.ShipFireballSeconds * 0.45f, Settings.FireballBrightness, bLow ? 0.6f : 1.0f, Fire);
		if (!bLow)
		{
			// In the plane of the wreck, tipped a little so that no two look alike - and tipped towards a view that is
			// close to that plane (the camera behind the own ship), which would otherwise see a line and be swept by it.
			FVector Normal = Blast.Up + Random.VRand() * 0.35;
			FVector ViewLocation = Blast.Location;
			FRotator ViewRotation = FRotator::ZeroRotator;
			if (GetLocalView(ViewLocation, ViewRotation))
			{
				const FVector ToView = (ViewLocation - Blast.Location).GetSafeNormal();
				const double Facing = FVector::DotProduct(Normal.GetSafeNormal(), ToView);
				if (FMath::Abs(Facing) < 0.5)
				{
					Normal = Normal.GetSafeNormal() + ToView * (Facing >= 0.0 ? 0.6 : -0.6);
				}
			}
			SpawnRing(Blast.Location, Normal, Settings.ShipRingSize, Settings.ShipRingSeconds, FMath::Lerp(FLinearColor(1.0f, 0.75f, 0.5f), Blast.Color, 0.4f));
		}
		UHCJAssetSettings::SpawnEffect(this, Settings.ExplosionEffect, TEXT("ship explosion"), Blast.Location, Blast.Forward.Rotation(), Settings.ExplosionScale);
		if (bFull)
		{
			// Chunks: two bursts of the rock impact around the blast.
			for (int32 Burst = 0; Burst < 2; ++Burst)
			{
				UHCJAssetSettings::SpawnEffect(this, Settings.ImpactRockEffect, TEXT("ship debris"), Blast.Location + Random.VRand() * 170.0, FRotator::ZeroRotator, Settings.ImpactScale * 3.0f);
			}
		}
		if (PlateKind != INDEX_NONE)
		{
			if (bFull)
			{
				// The largest pieces tumble away slowly, burning: fire and smoke behind them.
				for (int32 Chunk = 0; Chunk < 3; ++Chunk)
				{
					const int32 Slot = SpawnDebris(Blast.Location, Blast.Velocity, 1, PlateKind, PlateKind, 70.0f, Settings.DebrisSpeed * 0.6f, FVector(120.0, 70.0, 16.0), FVector(190.0, 110.0, 30.0), false);
					StartDebrisTrail(Slot, Settings.DebrisTrailSeconds);
				}
			}
			// Hull plates: thin, half a metre to a metre and a half across.
			const int32 NumPlates = bFull ? Settings.ShipDebrisPieces : bLow ? FMath::Min(Settings.ShipDebrisPieces, 4) : Settings.ShipDebrisPieces / 2;
			SpawnDebris(Blast.Location, Blast.Velocity, NumPlates, PlateKind, PlateKind, 90.0f, Settings.DebrisSpeed, FVector(45.0, 28.0, 5.0), FVector(150.0, 90.0, 14.0), false);
		}
		if (SparkKind != INDEX_NONE && !bLow)
		{
			// Sparks: fast, short-lived, drawn out along their flight.
			SpawnDebris(Blast.Location, Blast.Velocity, bFull ? Settings.ShipDebrisSparks : Settings.ShipDebrisSparks / 2, SparkKind, SparkKind, 50.0f, Settings.DebrisSpeed * 2.6f,
				FVector(4.0), FVector(7.5), true, 0.3f);
		}
		if (EmberKind != INDEX_NONE)
		{
			// Glowing bits, smaller than the plates and faster.
			SpawnDebris(Blast.Location, Blast.Velocity, bFull ? Settings.ShipDebrisEmbers : bLow ? FMath::Min(Settings.ShipDebrisEmbers, 3) : Settings.ShipDebrisEmbers / 2, EmberKind, EmberKind, 60.0f,
				Settings.DebrisSpeed * 1.5f, FVector(10.0, 10.0, 10.0), FVector(26.0, 26.0, 26.0), true);
		}
		AddShake(Blast.Location, Blast.bOwn ? Settings.OwnShipShake : 1.0f);
		UHCJAssetSettings::PlaySound(this, Settings.ExplosionSound, TEXT("ship explosion sound"), Blast.Location, Settings.ExplosionVolume);
		UHCJAssetSettings::PlaySoundAt(this, Settings.ExplosionSounds, TEXT("ship explosion sound"), Blast.Location, Settings.ExplosionVolume, 0.1f, EHCJSoundGroup::Explosion);
		if (Blast.bOwn)
		{
			// The own ship: a low boom under it.
			UHCJAssetSettings::PlaySoundAt(this, Settings.TorpedoExplosionSounds, TEXT("own ship blast sound"), Blast.Location, Settings.ExplosionVolume, 0.04f, EHCJSoundGroup::Explosion, 0.6f);
		}
		UE_LOG(LogHCJ, Verbose, TEXT("Effects: ship blast at %s (detail %d), %d debris pieces, %d flares, %d puffs, %d rings, %d trails alive"), *Blast.Location.ToCompactString(), int32(Blast.Detail),
			Debris.NumActive(), GetNumFlares(), NumActivePuffs, NumActiveRings, NumActiveTrails);
		return;
	}

	{
		// The lobes of the fireball: three at full detail, the first one at medium.
		const int32 Lobe = Stage - HCJEffects::BlastMainStage;
		if (bLow || (!bFull && Lobe > 1))
		{
			return;
		}
		const FVector Direction = Random.VRand();
		const FVector At = Blast.Location + Direction * (Settings.ShipFireballSize * Random.FRandRange(0.15f, 0.25f));
		const float Seconds = Settings.ShipFireballSeconds * Random.FRandRange(0.85f, 1.25f);
		SpawnPuff(At, Blast.Velocity + Direction * 260.0, Settings.ShipFireballSize * Random.FRandRange(0.5f, 0.7f), Seconds, Settings.ShipFireballSeconds * Random.FRandRange(0.3f, 0.4f),
			Settings.FireballBrightness * 0.9f, 0.7f, Fire);
		if (Lobe == 2)
		{
			// A second, smaller flash.
			SpawnFlare(At, Settings.ShipFlareSize * 0.4f, FMath::Lerp(FLinearColor(1.0f, 0.6f, 0.25f), Blast.Color, 0.25f));
		}
		if (Lobe == 3 && EmberKind != INDEX_NONE)
		{
			SpawnDebris(At, Blast.Velocity, 4, EmberKind, EmberKind, 60.0f, Settings.DebrisSpeed, FVector(8.0, 8.0, 8.0), FVector(20.0, 20.0, 20.0), true);
		}
	}
}

void UHCJEffectsDirector::TickBlasts(float DeltaTime)
{
	NumActiveBlasts = 0;
	for (FShipBlast& Blast : Blasts)
	{
		if (!Blast.bActive)
		{
			continue;
		}
		Blast.Time += DeltaTime;
		if (Blast.Stage > HCJEffects::BlastMainStage)
		{
			Blast.Location += Blast.Velocity * DeltaTime;
			Blast.Velocity *= FMath::Clamp(1.0f - DebrisDrag * DeltaTime, 0.0f, 1.0f);
		}
		while (Blast.Stage < HCJEffects::NumBlastStages && Blast.Time >= HCJEffects::BlastStageTime(Blast.Stage, Blast.FailSeconds))
		{
			RunBlastStage(Blast);
			++Blast.Stage;
		}
		// The slot stays taken for the first second after the blast although its last stage ran at 0.27 s: that second
		// is what PlayShipDeath counts when it decides how much detail the next blast gets.
		Blast.bActive = Blast.Stage < HCJEffects::NumBlastStages || Blast.Time - Blast.FailSeconds < 1.0f;
		NumActiveBlasts += Blast.bActive ? 1 : 0;
	}
}

void UHCJEffectsDirector::TickPuffsAndRings(float DeltaTime, bool bHasView, const FVector& ViewLocation, const FRotator& ViewRotation)
{
	const FVector ViewRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);
	const float Keep = FMath::Clamp(1.0f - DebrisDrag * DeltaTime, 0.0f, 1.0f);
	for (int32 Index = 0; Index < Puffs.Num() && NumActivePuffs > 0; ++Index)
	{
		FFlare& Puff = Puffs[Index];
		if (!Puff.bActive)
		{
			continue;
		}
		Puff.Age += DeltaTime;
		UStaticMeshComponent* Quad = PuffQuads[Index];
		if (Puff.Age >= Puff.Duration)
		{
			Puff.bActive = false;
			--NumActivePuffs;
			Quad->SetVisibility(false);
			continue;
		}
		PuffMaterials[Index]->SetScalarParameterValue(TEXT("Age"), Puff.Age / Puff.Duration);
		const FVector Location = Quad->GetComponentLocation() + Puff.Velocity * DeltaTime;
		Puff.Velocity *= Keep;
		const FVector ToView = ViewLocation - Location;
		if (bHasView && ToView.SizeSquared() > 1.0)
		{
			Quad->SetWorldLocationAndRotation(Location, FRotationMatrix::MakeFromZX(ToView, ViewRight).Rotator());
		}
		else
		{
			Quad->SetWorldLocation(Location);
		}
	}
	for (int32 Index = 0; Index < Rings.Num() && NumActiveRings > 0; ++Index)
	{
		FFlare& Ring = Rings[Index];
		if (!Ring.bActive)
		{
			continue;
		}
		Ring.Age += DeltaTime;
		if (Ring.Age >= Ring.Duration)
		{
			Ring.bActive = false;
			--NumActiveRings;
			RingQuads[Index]->SetVisibility(false);
			continue;
		}
		RingMaterials[Index]->SetScalarParameterValue(TEXT("Age"), Ring.Age / Ring.Duration);
	}
}

void UHCJEffectsDirector::TickTrails(float DeltaTime)
{
	for (int32 Index = 0; Index < Trails.Num() && NumActiveTrails > 0; ++Index)
	{
		FDebrisTrail& Trail = Trails[Index];
		if (!Trail.bActive)
		{
			continue;
		}
		Trail.Left -= DeltaTime;
		const FHCJDebrisPiece* Piece = Trail.Slot < Debris.GetCapacity() ? &Debris.GetPieces()[Trail.Slot] : nullptr;
		UNiagaraComponent* Component = TrailComponents[Index];
		// Burnt out, the piece is gone, or its slot went to a new piece (which is younger).
		if (Trail.Left <= 0.0f || Piece == nullptr || !Piece->bActive || Piece->Kind != PlateKind || Piece->Age + 0.001f < Trail.LastAge || Component == nullptr)
		{
			Trail.bActive = false;
			--NumActiveTrails;
			if (Component != nullptr)
			{
				// What it has emitted fades by itself.
				Component->Deactivate();
			}
			continue;
		}
		Trail.LastAge = Piece->Age;
		Component->SetWorldLocationAndRotation(Piece->Location, Piece->Velocity.SizeSquared() > 1.0 ? Piece->Velocity.Rotation() : FRotator::ZeroRotator);
	}
}

void UHCJEffectsDirector::PlayWarpFlash(const FVector& Location, bool bOwnShip)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (!EnsureReady() || !UHCJAssetSettings::IsNearLocalView(this, Location, Settings.EffectMaxDistance * 2.0f))
	{
		return;
	}
	SpawnFlare(Location, Settings.ShipFlareSize * (bOwnShip ? 0.5f : 1.1f), FLinearColor(0.5f, 0.75f, 1.0f));
	UE_LOG(LogHCJ, Verbose, TEXT("Effects: warp flash at %s, %d flares alive"), *Location.ToCompactString(), GetNumFlares());
}

void UHCJEffectsDirector::PlayTorpedoBlast(const FVector& Location)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (!EnsureReady() || !UHCJAssetSettings::IsNearLocalView(this, Location, Settings.EffectMaxDistance * 2.0f))
	{
		return;
	}
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	PushDebris(Location, Gameplay.TorpedoShockwaveRadius, Gameplay.TorpedoShockwaveSpeed);
	const FLinearColor Color = Gameplay.GetWeapon(EHCJWeapon::Torpedo).Color;
	SpawnFlare(Location, Settings.TorpedoFlareSize, FMath::Lerp(FLinearColor(1.0f, 0.6f, 0.3f), Color, 0.5f));
	if (EmberKind != INDEX_NONE)
	{
		SpawnDebris(Location, FVector::ZeroVector, Settings.ShipDebrisEmbers / 2, EmberKind, EmberKind, 40.0f, Settings.DebrisSpeed * 1.3f, FVector(8.0, 8.0, 8.0), FVector(20.0, 20.0, 20.0), true);
	}
	AddShake(Location, 0.8f);
}

void UHCJEffectsDirector::PlayRockBreak(const FVector& Location, float Radius, bool bSplit)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (!EnsureReady() || !UHCJAssetSettings::IsNearLocalView(this, Location, Settings.EffectMaxDistance * 2.0f))
	{
		return;
	}
	// Everything grows with the rock, within limits: a boulder of 4 m is the reference.
	const float Size = FMath::Clamp(Radius / 400.0f, 0.5f, 3.0f);
	UHCJAssetSettings::PlaySoundAt(this, Settings.RockBreakSounds, TEXT("rock break sound"), Location, Settings.RockBreakVolume * FMath::Min(Size, 1.5f), 0.1f,
		EHCJSoundGroup::Explosion, Settings.RockBreakPitch / FMath::Sqrt(Size));
	UHCJAssetSettings::SpawnEffect(this, Settings.RockBreakEffect, TEXT("rock break"), Location, FRotator::ZeroRotator, Settings.RockBreakEffectScale * Size);
	// Dust: a few bursts of the rock impact around the surface.
	for (int32 Burst = 0; Burst < 3; ++Burst)
	{
		UHCJAssetSettings::SpawnEffect(this, Settings.ImpactRockEffect, TEXT("rock dust"), Location + Random.VRand() * Radius * 0.6f, FRotator::ZeroRotator, Settings.ImpactScale * 2.5f * Size);
	}
	SpawnFlare(Location, Settings.RockFlareSize * Size, FLinearColor(1.0f, 0.72f, 0.42f));
	if (NumRockKinds > 0)
	{
		const int32 Count = HCJEffects::ScaleCount(Settings.RockDebrisPieces, Radius, 400.0f, 4, 2 * FMath::Max(Settings.RockDebrisPieces, 2));
		// Chunks of a tenth to a quarter of the rock's radius; a rock that leaves pieces behind throws smaller ones.
		const float Chunk = Radius * (bSplit ? 0.10f : 0.16f);
		const float Reference = DebrisMeshSizes.IsValidIndex(0) ? DebrisMeshSizes[0] : 100.0f;
		SpawnDebris(Location, FVector::ZeroVector, Count, 0, NumRockKinds - 1, Radius * 0.7f, 380.0f + 220.0f * Size, FVector(Chunk * 0.6f / Reference), FVector(Chunk * 1.6f / Reference), true);
	}
	AddShake(Location, 0.35f * Size);
	UE_LOG(LogHCJ, Verbose, TEXT("Effects: rock of radius %.0f broke at %s, %d debris pieces and %d flares alive"), Radius, *Location.ToCompactString(), Debris.NumActive(), GetNumFlares());
}

void UHCJEffectsDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!bReady || World == nullptr)
	{
		return;
	}

	if (ShakeAmount > 0.0f)
	{
		ShakeTime += DeltaTime;
		if (ShakeTime >= ShakeSeconds)
		{
			ShakeAmount = 0.0f;
		}
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	const bool bHasView = GetLocalView(ViewLocation, ViewRotation);

	// Flares.
	for (int32 Index = 0; Index < Flares.Num(); ++Index)
	{
		FFlare& Flare = Flares[Index];
		if (!Flare.bActive)
		{
			continue;
		}
		Flare.Age += DeltaTime;
		UStaticMeshComponent* Quad = FlareQuads[Index];
		if (Flare.Age >= Flare.Duration)
		{
			Flare.bActive = false;
			Quad->SetVisibility(false);
			continue;
		}
		FlareMaterials[Index]->SetScalarParameterValue(TEXT("Age"), Flare.Age / Flare.Duration);
		const FVector ToView = ViewLocation - Quad->GetComponentLocation();
		if (bHasView && ToView.SizeSquared() > 1.0)
		{
			Quad->SetWorldRotation(FRotationMatrix::MakeFromZX(ToView, FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y)).Rotator());
		}
	}

	// Impact stars.
	for (int32 Index = 0; Index < Stars.Num() && NumActiveStars > 0; ++Index)
	{
		FFlare& Star = Stars[Index];
		if (!Star.bActive)
		{
			continue;
		}
		Star.Age += DeltaTime;
		if (Star.Age >= Star.Duration)
		{
			Star.bActive = false;
			--NumActiveStars;
			StarQuads[Index]->SetVisibility(false);
			continue;
		}
		StarMaterials[Index]->SetScalarParameterValue(TEXT("Age"), Star.Age / Star.Duration);
	}

	// Ships going up, their fireballs and rings.
	if (NumActiveBlasts > 0)
	{
		TickBlasts(DeltaTime);
	}
	if (NumActivePuffs > 0 || NumActiveRings > 0)
	{
		TickPuffsAndRings(DeltaTime, bHasView, ViewLocation, ViewRotation);
	}

	// Debris.
	if (Debris.NumActive() == 0)
	{
		if (NumActiveTrails > 0)
		{
			TickTrails(DeltaTime);
		}
		return;
	}
	++FrameParity;
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	const bool bUniverse = Universe != nullptr && Universe->IsBuilt();
	const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(HCJDebris), false);
	const FCollisionObjectQueryParams Rocks(ECC_WorldStatic);
	for (int32 Slot = 0; Slot < Debris.GetCapacity(); ++Slot)
	{
		FHCJDebrisPiece& Piece = Debris.GetPiece(Slot);
		if (!Piece.bActive)
		{
			continue;
		}
		// Every other frame per piece: does the next two steps' worth of flight run into a rock? Then it bounces off.
		// (Not the sparks: they are gone in half a second.)
		if (((Slot + FrameParity) & 1) == 0 && Piece.Kind != SparkKind && Piece.Velocity.SizeSquared() > FMath::Square(60.0))
		{
			FHitResult Hit;
			if (World->LineTraceSingleByObjectType(Hit, Piece.Location, Piece.Location + Piece.Velocity * (2.0f * DeltaTime), Rocks, TraceParams) && !Hit.bStartPenetrating)
			{
				const FVector Normal = Hit.ImpactNormal;
				Piece.Velocity = (Piece.Velocity - 2.0 * FVector::DotProduct(Piece.Velocity, Normal) * Normal) * DebrisRestitution;
				Piece.AngularSpeed *= -0.7f;
			}
		}
		if (bUniverse)
		{
			// The pull of a black hole carries debris like everything else.
			Piece.Location += Universe->GetGravityDrift(Piece.Location) * DeltaTime;
		}
	}
	Debris.Step(DeltaTime, DebrisDrag);
	if (NumActiveTrails > 0)
	{
		TickTrails(DeltaTime);
	}

	bool bDirty[8] = {};
	for (int32 Slot = 0; Slot < Debris.GetCapacity(); ++Slot)
	{
		const FHCJDebrisPiece& Piece = Debris.GetPieces()[Slot];
		const int32 Kind = SlotKinds[Slot];
		if (Kind == INDEX_NONE || !DebrisMeshes.IsValidIndex(Kind))
		{
			continue;
		}
		if (!Piece.bActive)
		{
			HideDebrisInstance(Slot, Kind);
			SlotKinds[Slot] = INDEX_NONE;
			continue;
		}
		const float Size = FHCJDebrisPool::SizeFactor(Piece.Age, Piece.Life);
		if (Kind == SparkKind)
		{
			// A spark: a streak from where it was a moment ago to where it is, thinning out.
			const double Speed = Piece.Velocity.Size();
			const FVector Direction = Speed > 1.0 ? Piece.Velocity / Speed : FVector::ForwardVector;
			const double Length = FMath::Clamp(Speed * SparkStreakSeconds, 14.0, 260.0);
			const double Thickness = Piece.Scale.X * Size;
			DebrisMeshes[Kind]->UpdateInstanceTransform(Slot, FTransform(FRotationMatrix::MakeFromX(Direction).ToQuat(), Piece.Location - Direction * (0.5 * Length),
				FVector(Length / 100.0, Thickness / 100.0, Thickness / 100.0)), /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
			bDirty[Kind] = true;
			continue;
		}
		// Hull plates and embers are sized in uu (the cube is 100 across), rock chunks are already relative to their mesh.
		const FVector Scale = Kind >= NumRockKinds ? Piece.Scale * (Size / 100.0f) : Piece.Scale * Size;
		DebrisMeshes[Kind]->UpdateInstanceTransform(Slot, FTransform(FQuat(Piece.Axis, FMath::DegreesToRadians(Piece.Angle)), Piece.Location, Scale),
			/*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
		if (Kind == PlateKind && bPlatesHaveHeat)
		{
			// The torn edges glow while the plate is hot.
			DebrisMeshes[Kind]->SetCustomDataValue(Slot, 0, HCJEffects::PlateHeat(Piece.Age), /*bMarkRenderStateDirty*/ false);
		}
		if (Kind < UE_ARRAY_COUNT(bDirty))
		{
			bDirty[Kind] = true;
		}
	}
	for (int32 Kind = 0; Kind < DebrisMeshes.Num() && Kind < UE_ARRAY_COUNT(bDirty); ++Kind)
	{
		if (bDirty[Kind])
		{
			DebrisMeshes[Kind]->MarkRenderStateDirty();
		}
	}
}

int32 UHCJEffectsDirector::ServerPushShips(UWorld* World, const FVector& Origin, float Radius, float PeakSpeed, const AActor* Ignored)
{
	if (World == nullptr || World->GetNetMode() == NM_Client || Radius <= 0.0f || PeakSpeed <= 0.0f || !HCJShockwave::IsEnabled())
	{
		return 0;
	}
	int32 NumPushed = 0;
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		AHCJShipCharacter* Ship = *It;
		if (Ship == Ignored || Ship->IsDead())
		{
			continue;
		}
		const FVector Impulse = HCJShockwave::ComputeImpulse(Origin, Ship->GetActorLocation(), Radius, PeakSpeed);
		if (Impulse.SizeSquared() > FMath::Square(25.0))
		{
			Ship->ServerApplyImpulse(Impulse);
			++NumPushed;
		}
	}
	if (NumPushed > 0)
	{
		if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
		{
			GameMode->NotifyShockwave(NumPushed);
		}
	}
	return NumPushed;
}
