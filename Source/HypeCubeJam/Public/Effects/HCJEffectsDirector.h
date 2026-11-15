#pragma once

#include "CoreMinimal.h"
#include "Effects/HCJEffectsLogic.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "HCJEffectsDirector.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
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
 *  - The death of a ship is staged (PlayShipDeath): small pops along the hull while it fails (HullFailSeconds), then
 *    the blast - the flare as the core flash, a fireball of several puffs that cools to smoke (M_HCJ_Fireball on quads
 *    facing the view), a shockwave ring in the plane of the wreck (M_HCJ_ShockRing), sparks (streaks along their
 *    flight), hull plates with glowing edges, the largest trailing fire and smoke (a small pool of Niagara trail
 *    components); the first puff lives on as the smoke that stays. Three levels of detail by the distance from the view and by how many
 *    blasts are running; every pool has a hard cap. -HCJClassicBlast plays the single pop of before instead.
 * The subsystem ticks only while a piece, a flare, a puff, a blast or a shake is alive.
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

	/** The explosion of before (-HCJClassicBlast): a ship blew up at Location while flying with Velocity: flare, hull debris, shake. Color: its player colour. */
	void PlayShipExplosion(const FVector& Location, const FVector& Velocity, const FLinearColor& Color);

	/**
	 * A ship was destroyed at Location while flying with Velocity: the staged explosion (class comment). Forward / Up:
	 * the axes of the hull (the pops run along it, the shockwave ring lies in its plane), HullLength its length in uu.
	 * bOwnShip: the ship of the local player (always full detail, a stronger shake). The blast itself comes
	 * GetHullFailSeconds() later; the caller hides the hull then.
	 */
	void PlayShipDeath(const FVector& Location, const FVector& Velocity, const FLinearColor& Color, const FVector& Forward, const FVector& Up, float HullLength, bool bOwnShip);

	/** Seconds between PlayShipDeath and the blast (0 with -HCJClassicBlast). */
	static float GetHullFailSeconds();
	/** True with -HCJClassicBlast (measurement aid: the explosion of before, in the same binary). */
	static bool IsClassicBlast();
	/** A ship jumped to warp or dropped out of it at Location: a blue-white flare (smaller and dimmer for the own ship, which sits in it). */
	void PlayWarpFlash(const FVector& Location, bool bOwnShip);
	/** A torpedo went off: flare, shake, a push for the debris around. */
	void PlayTorpedoBlast(const FVector& Location);
	/** A rock of that bounding radius was destroyed (bSplit: it broke into pieces that stay): flare, rock chunks, dust, sound, shake. */
	void PlayRockBreak(const FVector& Location, float Radius, bool bSplit);

	/**
	 * Something was hit at Location (a bolt, a phaser beam, a shield facing): a short star of radial streaks on a quad
	 * facing the view (M_HCJ_ImpactStar; a pool of UHCJAssetSettings::MaxImpactStars quads, the oldest is reused).
	 * Scale multiplies ImpactStarSize. Cosmetic, local; skipped far from the local view.
	 */
	void PlayImpactStar(const FVector& Location, float Scale, const FLinearColor& Color);

	/** Creates the meshes and quads now (when a match is built) instead of in the frame of the first explosion. */
	void Prewarm() { EnsureReady(); }

	/** Pushes the debris already flying (the cosmetic side of a shockwave). */
	void PushDebris(const FVector& Origin, float Radius, float PeakSpeed);

	/** Offset (uu, in the camera's own Y / Z) the local ship adds to its camera right now; zero when nothing shakes or shake is switched off. */
	FVector GetCameraShake() const;

	int32 GetNumDebris() const { return Debris.NumActive(); }
	int32 GetNumFlares() const;
	int32 GetNumFireballs() const { return NumActivePuffs; }
	int32 GetNumBlasts() const { return NumActiveBlasts; }
	/** Total since the world started (for the log). */
	int32 GetNumBlastsPlayed() const { return NumBlastsPlayed; }
	/** Totals since the world started (for the log). */
	int32 GetNumDebrisSpawned() const { return NumDebrisSpawned; }
	int32 GetNumFlaresSpawned() const { return NumFlaresSpawned; }
	int32 GetNumImpactStarsSpawned() const { return NumStarsSpawned; }

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
		/** Fireball puffs drift (uu/s, damped like the debris). */
		FVector Velocity = FVector::ZeroVector;
	};

	/** One ship going up: where, how, and which of its stages have run. */
	struct FShipBlast
	{
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Up = FVector::UpVector;
		FLinearColor Color = FLinearColor::White;
		float HullLength = 400.0f;
		/** Seconds since PlayShipDeath; the blast is at FailSeconds. */
		float Time = 0.0f;
		float FailSeconds = 0.0f;
		int32 Stage = 0;
		/** HCJEffects::EBlastDetail as a number: 0 everything, 1 simpler, 2 least. */
		uint8 Detail = 0;
		bool bOwn = false;
		bool bActive = false;
	};

	/** A burning piece: the debris slot a trail follows and how long it still burns. */
	struct FDebrisTrail
	{
		int32 Slot = INDEX_NONE;
		float Left = 0.0f;
		/** Age of the piece when last looked at: a slot that was given to a new piece is younger. */
		float LastAge = 0.0f;
		bool bActive = false;
	};

	/** Creates the host actor, the debris meshes and the flare quads on first use. False when this machine draws nothing. */
	bool EnsureReady();
	void SpawnFlare(const FVector& Location, float Size, const FLinearColor& Color);
	/** Returns the slot of the first piece (INDEX_NONE when none was made). LifeScale shortens or lengthens DebrisSeconds. */
	int32 SpawnDebris(const FVector& Location, const FVector& BaseVelocity, int32 Count, int32 KindMin, int32 KindMax, float Spread, float Speed, const FVector& ScaleMin, const FVector& ScaleMax, bool bUniformScale, float LifeScale = 1.0f);
	/** One puff of the fireball: Size uu across, living Seconds of which it burns FireSeconds, drifting with Velocity; Smoke 0 = fire only .. 1 = mostly smoke. */
	void SpawnPuff(const FVector& Location, const FVector& Velocity, float Size, float Seconds, float FireSeconds, float Brightness, float Smoke, const FLinearColor& Color);
	/** The shockwave ring in the plane with that normal. */
	void SpawnRing(const FVector& Location, const FVector& Normal, float Size, float Seconds, const FLinearColor& Color);
	/** A fire-and-smoke trail on the debris piece in Slot for Seconds. */
	void StartDebrisTrail(int32 Slot, float Seconds);
	/** Runs the stage Blast.Stage of a ship's death. */
	void RunBlastStage(FShipBlast& Blast);
	void TickBlasts(float DeltaTime);
	void TickPuffsAndRings(float DeltaTime, bool bHasView, const FVector& ViewLocation, const FRotator& ViewRotation);
	void TickTrails(float DeltaTime);
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

	/** Impact stars: the same kind of pool as the flares, with their own material. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> StarQuads;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> StarMaterials;

	TArray<FFlare> Stars;
	int32 NumStarsSpawned = 0;
	int32 NumActiveStars = 0;

	/** Fireball puffs and shockwave rings: the same kind of pool again. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PuffQuads;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PuffMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RingQuads;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RingMaterials;

	/** Trails of burning pieces: Niagara components that are moved with a debris piece. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> TrailComponents;

	TArray<FFlare> Puffs;
	TArray<FFlare> Rings;
	TArray<FDebrisTrail> Trails;
	TArray<FShipBlast> Blasts;
	int32 NumActivePuffs = 0;
	int32 NumActiveRings = 0;
	int32 NumActiveTrails = 0;
	int32 NumActiveBlasts = 0;
	int32 NumBlastsPlayed = 0;
	FHCJDebrisPool Debris;
	/** Size of each debris mesh (largest side of its bounds, uu) so that pieces can be given a size in uu. */
	TArray<float> DebrisMeshSizes;
	/** Kind drawn in each slot (INDEX_NONE: nothing), to hide the old instance when a slot is reused. */
	TArray<int32> SlotKinds;
	int32 NumRockKinds = 0;
	/** Index of the hull plate mesh, of the glowing ember mesh and of the spark mesh in DebrisMeshes; INDEX_NONE when it could not be made. */
	int32 PlateKind = INDEX_NONE;
	int32 EmberKind = INDEX_NONE;
	int32 SparkKind = INDEX_NONE;
	/** The hull plates take their heat as per-instance custom data. */
	bool bPlatesHaveHeat = false;

	FRandomStream Random;
	float ShakeAmount = 0.0f;
	float ShakeTime = 0.0f;
	bool bReady = false;
	bool bFailed = false;
	uint32 FrameParity = 0;
	int32 NumDebrisSpawned = 0;
	int32 NumFlaresSpawned = 0;
};
