#include "Ship/HCJTorpedoBlast.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#include "Assets/HCJAssetSettings.h"
#include "Effects/HCJEffectsDirector.h"
#include "Game/HCJGameplaySettings.h"
#include "HypeCubeJam.h"

AHCJTorpedoBlast::AHCJTorpedoBlast()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	Quad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Quad"));
	Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Quad->SetGenerateOverlapEvents(false);
	Quad->SetCanEverAffectNavigation(false);
	Quad->SetCastShadow(false);
	Quad->SetReceivesDecals(false);
	SetRootComponent(Quad);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		Quad->SetStaticMesh(PlaneMesh.Object);
	}
}

void AHCJTorpedoBlast::FaceLocalView(USceneComponent* InQuad)
{
	const UWorld* World = InQuad != nullptr ? InQuad->GetWorld() : nullptr;
	const APlayerController* LocalController = (World != nullptr && GEngine != nullptr) ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	if (LocalController == nullptr)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	LocalController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ToView = ViewLocation - InQuad->GetComponentLocation();
	if (ToView.SizeSquared() > 1.0)
	{
		InQuad->SetWorldRotation(FRotationMatrix::MakeFromZX(ToView, FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y)).Rotator());
	}
}

void AHCJTorpedoBlast::Play(const UObject* WorldContextObject, const FVector& Location)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	UE_LOG(LogHCJ, Verbose, TEXT("Torpedo: detonation seen at %s"), *Location.ToCompactString());
	UHCJAssetSettings::PlaySoundAt(World, Settings.TorpedoExplosionSounds, TEXT("torpedo detonation sound"), Location, Settings.TorpedoExplosionVolume, 0.08f,
		EHCJSoundGroup::Explosion);
	UHCJAssetSettings::SpawnEffect(World, Settings.TorpedoExplosionEffect, TEXT("torpedo detonation"), Location, FRotator::ZeroRotator, Settings.TorpedoExplosionScale);
	if (UHCJEffectsDirector* Effects = UHCJEffectsDirector::Get(World))
	{
		// The flare, the camera shake and the push on the debris around.
		Effects->PlayTorpedoBlast(Location);
	}

	if (!UHCJAssetSettings::IsNearLocalView(World, Location, Settings.EffectMaxDistance))
	{
		return;
	}
	UMaterialInterface* BlastMaterial = UHCJAssetSettings::Load<UMaterialInterface>(Settings.TorpedoBlastMaterial, TEXT("torpedo detonation flash"));
	if (BlastMaterial == nullptr)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AHCJTorpedoBlast* Blast = World->SpawnActor<AHCJTorpedoBlast>(AHCJTorpedoBlast::StaticClass(), FTransform(Location), Params);
	if (Blast == nullptr)
	{
		return;
	}
	Blast->Duration = FMath::Max(Settings.TorpedoBlastSeconds, 0.1f);
	Blast->Material = UMaterialInstanceDynamic::Create(BlastMaterial, Blast);
	const FLinearColor Color = UHCJGameplaySettings::Get().GetWeapon(EHCJWeapon::Torpedo).Color;
	Blast->Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.R, Color.G, Color.B, 1.0f));
	// Right in front of the view (a hit on the own ship) the quad fills the screen: dimmer the nearer, so the pilot keeps seeing.
	float Nearness = 1.0f;
	if (const APlayerController* LocalController = GEngine->GetFirstLocalPlayerController(World))
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		LocalController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		Nearness = FMath::Clamp(static_cast<float>(FVector::Dist(ViewLocation, Location)) / 2500.0f, 0.25f, 1.0f);
	}
	Blast->Material->SetScalarParameterValue(TEXT("Brightness"), Settings.TorpedoBlastBrightness * Nearness);
	Blast->Material->SetScalarParameterValue(TEXT("Age"), 0.0f);
	Blast->Quad->SetMaterial(0, Blast->Material);
	// The basic plane is 100 uu across.
	Blast->Quad->SetWorldScale3D(FVector(Settings.TorpedoBlastSize / 100.0f));
	FaceLocalView(Blast->Quad);
	// A margin, so a hitch cannot leave the last frame of the ring standing.
	Blast->SetLifeSpan(Blast->Duration + 0.25f);
}

void AHCJTorpedoBlast::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	if (Age >= Duration)
	{
		Quad->SetVisibility(false);
		SetActorTickEnabled(false);
		return;
	}
	if (Material != nullptr)
	{
		Material->SetScalarParameterValue(TEXT("Age"), Age / Duration);
	}
	FaceLocalView(Quad);
}
