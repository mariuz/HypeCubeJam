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
#include "NiagaraComponent.h"

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
	return bReady && (Debris.NumActive() > 0 || GetNumFlares() > 0 || ShakeAmount > 0.0f);
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
	const auto AddDebrisMesh = [this, Root, Capacity](UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Host, NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
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
			UMaterialInstanceDynamic* Plate = ShapeMaterial != nullptr ? UMaterialInstanceDynamic::Create(ShapeMaterial, Host) : nullptr;
			if (Plate != nullptr)
			{
				Plate->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.42f, 0.44f, 0.48f));
				Plate->SetScalarParameterValue(TEXT("Roughness"), 0.35f);
				DebrisMaterials.Add(Plate);
			}
			PlateKind = DebrisMeshes.Num();
			AddDebrisMesh(Cube, Plate);

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

	bReady = true;
	UE_LOG(LogHCJ, Log, TEXT("Effects: %d debris slots on %d meshes (%d rock, hull plate %s, ember %s), %d flare quads, camera shake %s"), Capacity, DebrisMeshes.Num(), NumRockKinds,
		PlateKind != INDEX_NONE ? TEXT("yes") : TEXT("no"), EmberKind != INDEX_NONE ? TEXT("yes") : TEXT("no"), NumFlares, UHCJUserSettings::Get().bCameraShake ? TEXT("on") : TEXT("off"));
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

void UHCJEffectsDirector::SpawnDebris(const FVector& Location, const FVector& BaseVelocity, int32 Count, int32 KindMin, int32 KindMax, float Spread, float Speed,
	const FVector& ScaleMin, const FVector& ScaleMax, bool bUniformScale)
{
	if (Debris.GetCapacity() == 0 || DebrisMeshes.Num() == 0 || KindMax < KindMin)
	{
		return;
	}
	const float Life = FMath::Max(UHCJAssetSettings::Get().DebrisSeconds, 0.2f);
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
			return;
		}
		if (SlotKinds[Slot] != INDEX_NONE && SlotKinds[Slot] != Piece.Kind)
		{
			HideDebrisInstance(Slot, SlotKinds[Slot]);
		}
		SlotKinds[Slot] = Piece.Kind;
		++NumDebrisSpawned;
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

	// Debris.
	if (Debris.NumActive() == 0)
	{
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
		if (((Slot + FrameParity) & 1) == 0 && Piece.Velocity.SizeSquared() > FMath::Square(60.0))
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
		// Hull plates and embers are sized in uu (the cube is 100 across), rock chunks are already relative to their mesh.
		const FVector Scale = Kind >= NumRockKinds ? Piece.Scale * (Size / 100.0f) : Piece.Scale * Size;
		DebrisMeshes[Kind]->UpdateInstanceTransform(Slot, FTransform(FQuat(Piece.Axis, FMath::DegreesToRadians(Piece.Angle)), Piece.Location, Scale),
			/*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
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
