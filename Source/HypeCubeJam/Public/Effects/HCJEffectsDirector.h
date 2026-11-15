#pragma once

#include "CoreMinimal.h"
#include "Effects/HCJEffectsLogic.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "HCJEffectsDirector.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * Local, cosmetic effects of explosions: tumbling debris, flares, camera shake. One per game world; does nothing on
 * a dedicated server. Nothing here is replicated and nothing has gameplay collision: every machine makes its own
 * from the explosions it sees (a ship that dies, a torpedo that goes off, a rock that breaks).
 *
 *  - Debris: FHCJDebrisPool (a fixed number of slots, UHCJAssetSettings::MaxDebris) moved by plain integration -
 *    velocity, drag, tumble, the pull of a black hole, a bounce off rocks found by one line trace per piece every
 *    other frame - and drawn as instances of a few small meshes (rock chunks, hull plates, glowing embers). Pieces shrink away at
 *    the end of their life. A later explosion pushes the pieces already flying (PushDebris).
 *  - Flares: a small pool of quads (MaxFlares) with M_HCJ_ExplosionFlare, turned to the view with their long axis
 *    level on the screen, living about half a second.
 *  - Camera shake: an amount that decays, read by the local ship's camera (GetCameraShake); off with the
 *    "Camera shake" setting of the menu (UHCJUserSettings::bCameraShake).
 * The subsystem ticks only while a piece, a flare or a shake is alive.
 *
 * The push an explosion gives to SHIPS is gameplay and is not here: ServerPushShips, called by the server.
 */
UCLASS()
class HYPECUBEJAM_API UHCJEffectsDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UHCJEffectsDirector* Get(const UObject* WorldContextObject);

	//~ UTickableWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** A ship blew up at Location while flying with Velocity: flare, hull debris, shake. Color: its player colour. */
	void PlayShipExplosion(const FVector& Location, const FVector& Velocity, const FLinearColor& Color);
	/** A ship jumped to warp or dropped out of it at Location: a blue-white flare (smaller and dimmer for the own ship, which sits in it). */
	void PlayWarpFlash(const FVector& Location, bool bOwnShip);
	/** A torpedo went off: flare, shake, a push for the debris around. */
	void PlayTorpedoBlast(const FVector& Location);
	/** A rock of that bounding radius was destroyed (bSplit: it broke into pieces that stay): flare, rock chunks, dust, sound, shake. */
	void PlayRockBreak(const FVector& Location, float Radius, bool bSplit);

	/** Creates the meshes and quads now (when a match is built) instead of in the frame of the first explosion. */
	void Prewarm() { EnsureReady(); }

	/** Pushes the debris already flying (the cosmetic side of a shockwave). */
	void PushDebris(const FVector& Origin, float Radius, float PeakSpeed);

	/** Offset (uu, in the camera's own Y / Z) the local ship adds to its camera right now; zero when nothing shakes or shake is switched off. */
	FVector GetCameraShake() const;

	int32 GetNumDebris() const { return Debris.NumActive(); }
	int32 GetNumFlares() const;
	/** Totals since the world started (for the log). */
	int32 GetNumDebrisSpawned() const { return NumDebrisSpawned; }
	int32 GetNumFlaresSpawned() const { return NumFlaresSpawned; }

	/**
	 * SERVER ONLY (gameplay): the shockwave of an explosion pushes every living ship within Radius of Origin away from
	 * it (HCJShockwave::ComputeImpulse) through AHCJShipCharacter::ServerApplyImpulse. Ignored: a ship to leave out
	 * (the one that is exploding). Returns the number of ships pushed. Does nothing with -HCJNoShockwaves.
	 */
	static int32 ServerPushShips(UWorld* World, const FVector& Origin, float Radius, float PeakSpeed, const AActor* Ignored);

private:
	struct FFlare
	{
		float Age = 0.0f;
		float Duration = 0.0f;
		float Brightness = 1.0f;
		bool bActive = false;
	};

	/** Creates the host actor, the debris meshes and the flare quads on first use. False when this machine draws nothing. */
	bool EnsureReady();
	void SpawnFlare(const FVector& Location, float Size, const FLinearColor& Color);
	void SpawnDebris(const FVector& Location, const FVector& BaseVelocity, int32 Count, int32 KindMin, int32 KindMax, float Spread, float Speed, const FVector& ScaleMin, const FVector& ScaleMax, bool bUniformScale);
	void AddShake(const FVector& Location, float Strength);
	void HideDebrisInstance(int32 Slot, int32 Kind);
	bool GetLocalView(FVector& OutLocation, FRotator& OutRotation) const;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Host;

	/** One per debris mesh: rock chunks first, the hull plate last. Each has one instance per debris slot (zero scale while unused). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> DebrisMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FlareQuads;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FlareMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DebrisMaterials;

	TArray<FFlare> Flares;
	FHCJDebrisPool Debris;
	/** Size of each debris mesh (largest side of its bounds, uu) so that pieces can be given a size in uu. */
	TArray<float> DebrisMeshSizes;
	/** Kind drawn in each slot (INDEX_NONE: nothing), to hide the old instance when a slot is reused. */
	TArray<int32> SlotKinds;
	int32 NumRockKinds = 0;
	/** Index of the hull plate mesh and of the glowing ember mesh in DebrisMeshes; INDEX_NONE when it could not be made. */
	int32 PlateKind = INDEX_NONE;
	int32 EmberKind = INDEX_NONE;

	FRandomStream Random;
	float ShakeAmount = 0.0f;
	float ShakeTime = 0.0f;
	bool bReady = false;
	bool bFailed = false;
	uint32 FrameParity = 0;
	int32 NumDebrisSpawned = 0;
	int32 NumFlaresSpawned = 0;
};
