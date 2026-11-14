#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HCJTorpedoBlast.generated.h"

class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;

/**
 * The detonation of a photon torpedo: a flash and a shockwave ring on one quad that faces the local view, for
 * UHCJAssetSettings::TorpedoBlastSeconds, plus the explosion particles and sound. COSMETIC AND LOCAL: never
 * replicated, never spawned on a dedicated server; every machine that sees a torpedo go off spawns its own
 * (AHCJProjectile::PlayDetonationEffects). Nothing here touches gameplay - the blast damage is the server's
 * AHCJProjectile::ServerDetonate.
 */
UCLASS(NotPlaceable)
class HYPECUBEJAM_API AHCJTorpedoBlast : public AActor
{
	GENERATED_BODY()

public:
	AHCJTorpedoBlast();

	virtual void Tick(float DeltaSeconds) override;

	/** Not on a dedicated server: sound, particles and - when Location is near the local view and the material loads - the flash and ring. */
	static void Play(const UObject* WorldContextObject, const FVector& Location);

	/**
	 * Turns a quad (the engine's basic plane: normal +Z) so that it faces the first local player's view, keeping its
	 * own up along the view's up so that the picture on it does not spin. Does nothing without a local player.
	 */
	static void FaceLocalView(USceneComponent* Quad);

protected:
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> Quad;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	float Age = 0.0f;
	float Duration = 0.6f;
};
