#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"
#include "HCJAssetSettings.generated.h"

class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class USoundAttenuation;
class USoundBase;
class USoundClass;
class USoundConcurrency;
class UStaticMesh;

/** Voice-limit groups of one-shot sounds (one USoundConcurrency each, see UHCJAssetSettings::GetConcurrency). */
enum class EHCJSoundGroup : uint8
{
	Fire,
	Impact,
	Explosion,
	World,
	UI,
	Music,
	Count
};

/**
 * One full-length music track (the Lyria tracks). UHCJAudioDirector loops it itself: the next pass starts at
 * LoopSeconds and the two are cross-faded, because the tracks have an intro at full level and an outro that fades.
 */
USTRUCT()
struct FHCJMusicTrack
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (AllowedClasses = "/Script/Engine.SoundBase"))
	FSoftObjectPath Sound;

	/** Linear gain of the track (measured, see docs/ASSETS.md: the tracks are mastered about 11 dB hotter than the stems mix). */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0"))
	float Volume = 0.3f;

	/** Seconds into the track at which the next pass starts (where its outro begins); 0 = at the end minus the crossfade. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0"))
	float LoopSeconds = 0.0f;

	/** Seconds the end of one pass and the start of the next overlap. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0"))
	float CrossfadeSeconds = 3.0f;
};

/**
 * One layer of the match music. The Lyra music is delivered as stems that start on a bar line and ring out past
 * it, so UHCJAudioDirector re-triggers each stem every EveryBars bars instead of looping the file.
 */
USTRUCT()
struct FHCJMusicLayer
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (AllowedClasses = "/Script/Engine.SoundBase"))
	FSoftObjectPath Sound;

	/** Length of the stem in bars: it is started again every this many bars. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "1"))
	int32 EveryBars = 4;

	/** The layer plays while the music intensity (0 = cruising alone, 1 = in a fight) is at least this. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinIntensity = 0.0f;

	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0"))
	float Volume = 1.0f;

	/** True: plays during the intermission instead of during the match. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ")
	bool bIntermission = false;
};

/** One rock mesh that asteroids may be drawn with. */
USTRUCT()
struct FHCJAsteroidMeshVariant
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath Mesh;

	/**
	 * Measured once per mesh: the largest vertex distance from the centre of the mesh's bounding box after every
	 * axis is divided by the box half-extent (a sphere is 1, a cube sqrt(3) ~ 1.73). Sphere-shaped asteroids
	 * are scaled by AsteroidSphereFill / CornerFactor so that the farthest vertex ends up at AsteroidSphereFill
	 * times the collision radius.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "1.0", ClampMax = "1.8"))
	float CornerFactor = 1.5f;
};

/** One paint job of the ship meshes and the colour it stands for. */
USTRUCT()
struct FHCJShipSkin
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath Material;

	/** Nominal colour of the paint; a ship gets the skin whose colour is nearest to its player colour. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ")
	FLinearColor Color = FLinearColor::White;
};

/**
 * The single place that names every authored asset the game uses (Fab packs under /Game). Values live in
 * Config/DefaultGame.ini, section [/Script/HypeCubeJam.HCJAssetSettings]; the C++ defaults are all empty.
 *
 * FALLBACK RULE: every consumer treats "path empty", "asset missing" and "asset of the wrong class" the same way
 * and falls back to what the game did before it had assets: /Engine/BasicShapes meshes with BasicShapeMaterial,
 * no effect, no sound. A missing asset is reported once in LogHCJ (Warning) and never again.
 *
 * NETWORK RULE: everything here is cosmetic. Gameplay collision (asteroid shapes, ship capsule, projectile sphere,
 * portal trigger) never depends on these assets, so a machine with different or missing assets still simulates
 * the same world. Dedicated servers do not load any of them.
 *
 * Loaded assets are cached and kept alive until engine exit, so repeated Load calls are cheap.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "HypeCubeJam Assets"))
class HYPECUBEJAM_API UHCJAssetSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHCJAssetSettings();

	static const UHCJAssetSettings& Get() { return *GetDefault<UHCJAssetSettings>(); }

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	/**
	 * Synchronously loads (or returns the cached) asset at Path as T. nullptr when the path is empty, the asset is
	 * missing or it is not a T; the last two log one warning naming Purpose. Game thread only.
	 */
	template <typename T>
	static T* Load(const FSoftObjectPath& Path, const TCHAR* Purpose)
	{
		return static_cast<T*>(LoadAsset(Path, T::StaticClass(), Purpose));
	}

	/** Loads every asset named below so that the first shot or explosion does not hitch. Not for dedicated servers. */
	static void PreloadAll();

	/** True when Location is within MaxDistance of the first local player's view point (false when there is none). */
	static bool IsNearLocalView(const UObject* WorldContextObject, const FVector& Location, float MaxDistance);

	/**
	 * Plays a one-shot Niagara system at a world location if it loads and Location is within EffectMaxDistance of
	 * the local view. Never call on a dedicated server. Returns the component (auto destroyed), or nullptr.
	 */
	static UNiagaraComponent* SpawnEffect(const UObject* WorldContextObject, const FSoftObjectPath& System, const TCHAR* Purpose,
		const FVector& Location, const FRotator& Rotation, float Scale);

	/** Plays a one-shot sound at a world location with the shared distance attenuation. Never call on a dedicated server. */
	static void PlaySound(const UObject* WorldContextObject, const FSoftObjectPath& Sound, const TCHAR* Purpose, const FVector& Location, float Volume);

	/** Sphere attenuation shared by all one-shot sounds: full volume inside SoundInnerRadius, silent beyond SoundInnerRadius + SoundFalloffDistance. */
	static USoundAttenuation* GetSoundAttenuation();

	/**
	 * Plays a random entry of Sounds at a world location (shared attenuation, the voice limit of Group, pitch
	 * 1 +- PitchVariation). Nothing happens when the list is empty, the pick does not load or Location is out of
	 * earshot of the local view. Never call on a dedicated server.
	 */
	static void PlaySoundAt(const UObject* WorldContextObject, TConstArrayView<FSoftObjectPath> Sounds, const TCHAR* Purpose,
		const FVector& Location, float Volume, float PitchVariation, EHCJSoundGroup Group, float PitchScale = 1.0f);

	/** Plays a random entry of Sounds without spatialisation (interface cues, music stems). Never call on a dedicated server. */
	static void PlaySound2D(const UObject* WorldContextObject, TConstArrayView<FSoftObjectPath> Sounds, const TCHAR* Purpose,
		float Volume, EHCJSoundGroup Group = EHCJSoundGroup::UI);

	/** Voice limit of a group (transient object built from the Max*Voices settings). */
	static USoundConcurrency* GetConcurrency(EHCJSoundGroup Group);

	/** One-shot sounds started per group since the process started (for the "Audio" log line). */
	static int32 GetNumSoundsPlayed(EHCJSoundGroup Group);

	/** Uniform random number in [0, 1) for cosmetic variety only (never gameplay, never world generation). */
	static float CosmeticRandom();

	// ---- ship ----

	/**
	 * Ship bodies. A ship uses entry PickShipVariant(PlayerId, Num): every machine knows the player id, so every
	 * machine shows the same hull for the same player without any extra replication. Empty, or the picked mesh
	 * fails to load = cone/cube/cylinder ship.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<FSoftObjectPath> ShipMeshes;

	/** Each mesh is scaled uniformly so that the longest side of its bounding box is this long (uu), and centred on the capsule. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "10.0"))
	float ShipMeshLength = 260.0f;

	/** Rotation applied to the meshes so that the nose points along the character's +X and the top along +Z. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FRotator ShipMeshRotation = FRotator::ZeroRotator;

	/**
	 * Paint jobs for ShipMeshes (applied to every material slot). A ship gets entry PickNearestSkin(player colour):
	 * the hull colour is the player's colour cue, again derived from replicated state only. Empty = the mesh's own materials.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	TArray<FHCJShipSkin> ShipSkins;

	/** Index of the ship mesh for a player id: a hash (HCJ::MixSeed), in [0, NumVariants), 0 when NumVariants <= 1. */
	static int32 PickShipVariant(int32 PlayerId, int32 NumVariants);

	/** Index of the entry of SkinColors nearest to Color (squared RGB distance, the first one wins a tie); INDEX_NONE when empty. */
	static int32 PickNearestSkin(const FLinearColor& Color, TConstArrayView<FLinearColor> SkinColors);

	/** Vector parameter of the ship material that receives PlayerColor * ShipTintStrength (ignored if the material has no such parameter). */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FName ShipTintParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ShipTintStrength = 0.25f;

	/** Two of these are attached to the ship body; their glow material shows the player colour. Empty = none. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath ThrusterMesh;

	/** Material slot of ThrusterMesh that glows, and its colour / brightness parameters. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FName ThrusterGlowSlot;

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FName ThrusterGlowColorParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FName ThrusterGlowBrightnessParameter;

	/** Brightness at rest and at full boost speed. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ThrusterGlowIdle = 2.0f;

	/** Ship beacon (a glowing point on distant ships): brightness of the glow material. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ShipBeaconBrightness = 12.0f;

	/** Radius of the beacon as a fraction of its distance from the view (0.004 is about 7 pixels across at 1280x720); 0 = no beacon. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ShipBeaconAngularSize = 0.004f;

	/** The beacon starts to grow in beyond this distance from the view (uu). */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ShipBeaconMinDistance = 2500.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ThrusterGlowBoost = 12.0f;

	// ---- seeing the enemy: rim glow, glint, engine glow and trail (materials built by docs/tools/make_ship_fx_materials.py) ----

	/**
	 * Overlay material of every hull (M_HCJ_ShipRim; the hull is drawn a second time with it): a fresnel rim in the enemy
	 * colour that widens to the whole silhouette with distance, plus ShipAmbientFloor so that the unlit side of a hull is
	 * never black. Parameters: vector Color, scalars Brightness, Far (0..1), Floor. The own ship gets the floor only.
	 * Empty = no rim, no floor.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath ShipRimMaterial;

	/**
	 * What a cloaked hull is drawn with (M_HCJ_Cloak, built by docs/tools/make_trek_materials.py): translucent, a faint
	 * rim shimmer that bends the light behind it. Parameters: scalars Opacity (0.1 for another ship, 0.4 for the own,
	 * flickering when revealed) and Phase. Empty = another ship's cloaked hull is not drawn at all.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath CloakMaterial;

	/**
	 * Star streaks of the warp drive (M_HCJ_WarpStreaks, same script): additive, on one quad in front of the own
	 * camera. Parameters: vector Color, scalar Strength (0..1). Empty = no streaks (the view still widens).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath WarpStreakMaterial;

	/** The colour enemies glow in (rim, glint, engine), and how much of the player's own colour is mixed into it (0..1). */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FLinearColor EnemyGlowColor = FLinearColor(1.0f, 0.2f, 0.06f);

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnemyColorMix = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float ShipRimBrightness = 3.0f;

	/** The rim is a thin outline up to the first distance from the view (uu) and the whole hull glows from the second. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float ShipRimNearDistance = 2000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "1.0"))
	float ShipRimFarDistance = 12000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float ShipAmbientFloor = 0.035f;

	/**
	 * Minimum apparent size: a point of light with a halo and fine spikes (M_HCJ_ShipGlint) on a quad facing the view at
	 * every other ship, whose edge is ShipGlintAngularSize of its distance from the view (0.09 is about 50 pixels at
	 * 1280x720), so that a ship never shrinks to nothing. It fades in between ShipGlintMinDistance and twice that.
	 * Parameters: vector Color, scalars Brightness, Spikes. Empty = the older glowing sphere (ShipBeacon*).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath ShipGlintMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float ShipGlintAngularSize = 0.09f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float ShipGlintMinDistance = 2500.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float ShipGlintBrightness = 3.0f;

	/** Glow at the tail of every ship (the glint material without spikes): edge of the quad at rest and at top speed (uu). */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float EngineGlowSize = 330.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float EngineGlowBoostSize = 620.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float EngineGlowBrightness = 2.5f;

	/**
	 * Engine trail (M_HCJ_EngineTrail) on a quad from the tail to where the tail was EngineTrailSeconds ago, rolled to the
	 * view: enemies in the enemy colour, the own ship in its own. Parameters: vector Color, scalar Brightness. Empty = none.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath EngineTrailMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.02"))
	float EngineTrailSeconds = 0.45f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "1.0"))
	float EngineTrailWidth = 170.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float EngineTrailBrightness = 3.0f;

	/**
	 * Shield bubble: an additive unlit material on one sphere per ship (M_HCJ_ShieldBubble, built by
	 * docs/tools/make_shield_material.py). Hidden except while a hit flares or a facing collapses. Parameters:
	 * vectors HitDirA / HitDirB / CollapseDir (unit, in the sphere's own space), HitColorA / HitColorB / CollapseColor,
	 * scalars HitAgeA / HitAgeB (0 = just hit, 1 = over), Collapse (0..1) and Brightness. Empty = no bubble.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath ShieldBubbleMaterial;

	/** Half axes of the bubble around the hull: fore-aft, port-starboard, up-down (uu). */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FVector ShieldBubbleRadii = FVector(200.0, 165.0, 100.0);

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ShieldBubbleBrightness = 0.8f;

	/** Seconds a hit flare and a collapse flicker last. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.1"))
	float ShieldFlareSeconds = 0.4f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.1"))
	float ShieldCollapseSeconds = 0.55f;

	/** A shield facing of a ship went down (positioned at the ship). */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> ShieldCollapseSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	float ShieldCollapseVolume = 0.9f;

	/** Position of the right thruster in the ship mesh's own (unscaled) space; the left one mirrors Y. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FVector ThrusterOffset = FVector(-300.0, 130.0, 150.0);

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FRotator ThrusterRotation = FRotator::ZeroRotator;

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ThrusterScale = 1.5f;

	/** Looping sound on every ship; volume and pitch follow its speed. Must loop by itself. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.SoundBase"))
	FSoftObjectPath EngineSound;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float EngineVolume = 0.5f;

	/** Engine sounds only play for ships within this distance of the local view. */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float EngineAudibleDistance = 6000.0f;

	/** Looping Niagara system behind the ship, active while it flies faster than cruise speed (boost). */
	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath BoostTrailEffect;

	/** Niagara user colour parameter of BoostTrailEffect that receives the player colour (optional). */
	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	FName BoostTrailColorParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float BoostTrailScale = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath ExplosionEffect;

	UPROPERTY(Config, EditAnywhere, Category = "Ship")
	float ExplosionScale = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (AllowedClasses = "/Script/Engine.SoundBase"))
	FSoftObjectPath ExplosionSound;

	UPROPERTY(Config, EditAnywhere, Category = "Ship", meta = (ClampMin = "0.0"))
	float ExplosionVolume = 1.0f;

	// ---- projectile ----

	/** Bolt mesh, long axis +X. Empty = stretched sphere. */
	UPROPERTY(Config, EditAnywhere, Category = "Projectile", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath ProjectileMesh;

	/** The mesh is scaled to this length (X) and thickness (Y and Z) in uu and centred on the projectile. */
	UPROPERTY(Config, EditAnywhere, Category = "Projectile", meta = (ClampMin = "1.0"))
	float ProjectileLength = 260.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile", meta = (ClampMin = "1.0"))
	float ProjectileThickness = 26.0f;

	/** Vector / scalar parameters of the bolt material that receive the shooter's colour and ProjectileBrightness. */
	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	FName ProjectileColorParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	FName ProjectileBrightnessParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	float ProjectileBrightness = 20.0f;

	/** Niagara system attached to every bolt that starts within EffectMaxDistance of the local view. */
	UPROPERTY(Config, EditAnywhere, Category = "Projectile", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath ProjectileTrailEffect;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	float ProjectileTrailScale = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath MuzzleFlashEffect;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	float MuzzleFlashScale = 1.0f;

	/** Niagara user colour parameter of MuzzleFlashEffect that receives the bolt colour (optional). */
	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	FName MuzzleFlashColorParameter;

	/** Niagara user bool parameters of MuzzleFlashEffect that are switched off (the expensive extras). */
	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	TArray<FName> MuzzleFlashDisabledFeatures;

	/** Impact on a ship / on anything else (asteroids). */
	UPROPERTY(Config, EditAnywhere, Category = "Projectile", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath ImpactShipEffect;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath ImpactRockEffect;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	float ImpactScale = 1.0f;

	/** Niagara user vector parameters of the impact effects that receive the surface normal and the travel direction of the bolt (optional). */
	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	FName ImpactNormalParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Projectile")
	FName ImpactDirectionParameter;

	// ---- asteroids ----

	/**
	 * Rock meshes. Each asteroid picks one with HCJ::PickAsteroidVariant (a hash of sector seed and asteroid
	 * index, no random draws). Entries that fail to load are skipped; none left = visible basic shapes.
	 * The rocks are DRAWN ONLY: collision stays the invisible basic shape of the layout on every machine.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Asteroids")
	TArray<FHCJAsteroidMeshVariant> AsteroidMeshes;

	/**
	 * Sphere asteroids: farthest rock vertex as a fraction of the collision radius. 1 = the rock is entirely
	 * inside its collision sphere; above 1 its corners poke out (never more than half of HCJ::SpawnClearance).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "0.5", ClampMax = "1.5"))
	float AsteroidSphereFill = 1.12f;

	/** Vector parameter of the rock materials multiplied towards the sector tint (optional). */
	UPROPERTY(Config, EditAnywhere, Category = "Asteroids")
	FName AsteroidTintParameter;

	/** 0 = untinted (white), 1 = the full sector tint. */
	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AsteroidTintStrength = 0.35f;

	/** Extra multiplier on AsteroidTintParameter: grey rock under the dim space lighting needs a lift to read at all. */
	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "0.0"))
	float AsteroidBrightness = 1.0f;

	// ---- portals ----

	/** Material for the portal disc and frame (replaces BasicShapeMaterial). */
	UPROPERTY(Config, EditAnywhere, Category = "Portals", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath PortalMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Portals")
	FName PortalColorParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Portals")
	FName PortalBrightnessParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Portals")
	float PortalDiscBrightness = 3.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Portals")
	float PortalFrameBrightness = 10.0f;

	/**
	 * The inside of a portal (M_HCJ_PortalSwirl, built by docs/tools/make_portal_materials.py) on one quad that fills the
	 * ring: a swirl with depth in the colour of the sector the portal leads to. Parameters: vector Color, scalars
	 * Brightness, Speed, Twist, Flash. Empty = the older flat glowing disc (PortalMaterial, PortalDiscBrightness).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Portals", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath PortalSwirlMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Portals", meta = (ClampMin = "0.0"))
	float PortalSwirlBrightness = 1.4f;

	/** Degrees per second the glyph ring of a portal turns. */
	UPROPERTY(Config, EditAnywhere, Category = "Portals")
	float PortalRingTurnRate = 7.0f;

	/**
	 * Landmark glow of portals and wormhole mouths (the ship glint material on a quad facing the view): its edge is this
	 * fraction of its distance from the view once that is beyond PortalGlowMinDistance (uu). 0 = none.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Portals", meta = (ClampMin = "0.0"))
	float PortalGlowAngularSize = 0.11f;

	UPROPERTY(Config, EditAnywhere, Category = "Portals", meta = (ClampMin = "0.0"))
	float PortalGlowMinDistance = 9000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Portals", meta = (ClampMin = "0.0"))
	float PortalGlowBrightness = 1.6f;

	// ---- pickups and special weapons (looks and sounds; the rules are in UHCJGameplaySettings) ----

	/** Unlit glow material for the pad and the item (replaces BasicShapeMaterial), with its colour / brightness parameters. */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath PickupMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	FName PickupColorParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	FName PickupBrightnessParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	float PickupItemBrightness = 6.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	float PickupPadBrightness = 1.0f;

	/** Somebody took a power-up / a weapon from a pad; a pad got a new item. Played at the pad. */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	TArray<FSoftObjectPath> PickupPowerupSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	TArray<FSoftObjectPath> PickupWeaponSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	TArray<FSoftObjectPath> PickupSpawnSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	float PickupVolume = 0.9f;

	/** Fire sounds of the pickup weapons (the blaster uses FireSounds; an empty list falls back to FireSounds). */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	TArray<FSoftObjectPath> ScatterFireSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	TArray<FSoftObjectPath> RailFireSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	TArray<FSoftObjectPath> MissileFireSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	TArray<FSoftObjectPath> PulseFireSounds;

	/** Missile body (long axis +X) and its trail; empty = the bolt mesh / bolt trail. */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath MissileMesh;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (ClampMin = "1.0"))
	float MissileLength = 220.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath MissileTrailEffect;

	// ---- photon torpedoes (looks and sounds; the rules are in UHCJGameplaySettings) ----

	/**
	 * The torpedo itself: an additive unlit star-burst (M_HCJ_TorpedoStar, built by docs/tools/make_torpedo_materials.py)
	 * on one quad that always faces the local view. Parameters: vector Color, scalar Brightness. Empty = a tinted sphere.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath TorpedoMaterial;

	/** Edge of the quad (uu); the bright core is about a tenth of it, the spikes reach the edge. */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "10.0"))
	float TorpedoStarSize = 520.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	float TorpedoBrightness = 6.0f;

	/** Trail behind the torpedo; its colour parameter gets the torpedo colour. Empty = none. */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath TorpedoTrailEffect;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	float TorpedoTrailScale = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	FName TorpedoTrailColorParameter;

	/**
	 * Detonation: a flash and a shockwave ring (M_HCJ_TorpedoBlast, same script) on one quad facing the local view that
	 * lives TorpedoBlastSeconds. Parameters: vector Color, scalars Age (0..1) and Brightness. Empty = only the explosion effect.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath TorpedoBlastMaterial;

	/** Edge of the detonation quad (uu): the ring ends at about 0.45 of it from the centre. */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "10.0"))
	float TorpedoBlastSize = 1400.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "0.1"))
	float TorpedoBlastSeconds = 0.6f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	float TorpedoBlastBrightness = 2.0f;

	/** Particles of the detonation (one of the explosion systems already in use). Empty = none. */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath TorpedoExplosionEffect;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	float TorpedoExplosionScale = 0.8f;

	/** Launch (played at TorpedoFirePitch, so the same wave as another cue sounds like something else), detonation, and the cue for "a torpedo is homing on you". */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	TArray<FSoftObjectPath> TorpedoFireSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	float TorpedoFireVolume = 0.9f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "0.2", ClampMax = "3.0"))
	float TorpedoFirePitch = 0.6f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	TArray<FSoftObjectPath> TorpedoExplosionSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	float TorpedoExplosionVolume = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes")
	TArray<FSoftObjectPath> TorpedoWarningSounds;

	// ---- phasers (looks and sounds; the rules are in UHCJGameplaySettings) ----

	/**
	 * The beam: an additive unlit material (M_HCJ_PhaserBeam, built by docs/tools/make_phaser_fx_materials.py) on one quad
	 * stretched from the muzzle to the end of the beam and rolled towards the local view. Parameters: vector Color,
	 * scalars Brightness, Length (uu) and Phase. Empty = no beam is drawn (the glows and sparks still are).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath PhaserBeamMaterial;

	/** Width of the beam quad (uu); the white core is about a tenth of it. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "1.0"))
	float PhaserBeamWidth = 130.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers")
	float PhaserBeamBrightness = 2.0f;

	/** Glow at the muzzle and where the beam ends on something: a flickering star on a quad facing the view (the torpedo's material). Empty = none. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath PhaserGlowMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "1.0"))
	float PhaserMuzzleGlowSize = 150.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "1.0"))
	float PhaserImpactGlowSize = 280.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers")
	float PhaserGlowBrightness = 1.2f;

	/** Seconds between two bursts of sparks (the bolt impact effects) where the beam ends on a ship or a rock. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.03"))
	float PhaserSparkInterval = 0.12f;

	/** The held beam: two looping sounds played together at their own pitch, at the ship, while its beam runs. Empty = silent. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (AllowedClasses = "/Script/Engine.SoundBase"))
	FSoftObjectPath PhaserLoopSound;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers")
	float PhaserLoopVolume = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.2", ClampMax = "4.0"))
	float PhaserLoopPitch = 2.4f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (AllowedClasses = "/Script/Engine.SoundBase"))
	FSoftObjectPath PhaserLayerSound;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers")
	float PhaserLayerVolume = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.2", ClampMax = "4.0"))
	float PhaserLayerPitch = 2.8f;

	/** One-shots when the beam starts and when it stops, at their own pitch. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers")
	TArray<FSoftObjectPath> PhaserStartSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.2", ClampMax = "4.0"))
	float PhaserStartPitch = 1.8f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers")
	TArray<FSoftObjectPath> PhaserStopSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.2", ClampMax = "4.0"))
	float PhaserStopPitch = 0.7f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers")
	float PhaserCueVolume = 0.6f;

	// ---- explosions: flares, debris, camera shake (UHCJEffectsDirector; local and cosmetic, with hard caps) ----

	/**
	 * Flare of an explosion: an additive unlit material (M_HCJ_ExplosionFlare, same script) on one quad facing the view
	 * with its long axis level on the screen: a bloom flash, a horizontal streak, a star. Parameters: vector Color,
	 * scalars Brightness, Age (0..1) and Seed. Empty = no flares.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath ExplosionFlareMaterial;

	/** Width of the flare quad (uu; it is half as high) of a ship, a torpedo and a rock of 400 uu radius (scaled with the rock, within limits). */
	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "10.0"))
	float ShipFlareSize = 2800.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "10.0"))
	float TorpedoFlareSize = 2400.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "10.0"))
	float RockFlareSize = 1700.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0.05"))
	float FlareSeconds = 0.55f;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions")
	float FlareBrightness = 1.5f;

	/** Most flares / debris pieces alive at one time; a new one replaces the oldest. */
	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxFlares = 6;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaxDebris = 72;

	/** Pieces thrown by a destroyed ship / by a rock of 400 uu radius (scaled with the rock, within limits). */
	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0", ClampMax = "64"))
	int32 ShipDebrisPieces = 14;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0", ClampMax = "64"))
	int32 RockDebrisPieces = 9;

	/** Small glowing bits thrown by a destroyed ship (half as many by a torpedo), and how bright they glow. */
	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0", ClampMax = "64"))
	int32 ShipDebrisEmbers = 10;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions")
	float DebrisEmberBrightness = 12.0f;

	/** Seconds a piece lives (it shrinks away over the last third) and how fast the pieces of a ship fly apart (uu/s). */
	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0.2"))
	float DebrisSeconds = 3.2f;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0.0"))
	float DebrisSpeed = 1100.0f;

	/** Camera shake of an explosion: full within a tenth of this distance (uu) from the view, nothing beyond it; strength 1 = the jolt of a heavy hit. */
	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0.0"))
	float ExplosionShakeDistance = 7000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float ExplosionShakeStrength = 1.0f;

	/** A rock breaking apart: particles (one of the explosion systems already in use) and sounds. Empty = none. */
	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath RockBreakEffect;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions")
	float RockBreakEffectScale = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions")
	TArray<FSoftObjectPath> RockBreakSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions")
	float RockBreakVolume = 0.9f;

	UPROPERTY(Config, EditAnywhere, Category = "Explosions", meta = (ClampMin = "0.2", ClampMax = "4.0"))
	float RockBreakPitch = 0.75f;

	// ---- space stations and turrets (looks; collision is always the boxes of FHCJStation and a sphere per turret) ----

	/**
	 * Station model drawn instead of the grey boxes: scaled uniformly so that it fits the bounding box of the
	 * station's collision shape, centred on it. Drawn only. Empty or not loading = the boxes are drawn.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath StationMesh;

	/** Rotation applied to StationMesh before it is fitted. */
	UPROPERTY(Config, EditAnywhere, Category = "Stations")
	FRotator StationMeshRotation = FRotator::ZeroRotator;

	/**
	 * Scale of StationMesh; the model is then centred on the hub of the collision shape. 0 = fitted into the box around
	 * the collision shape instead. With a model, the boxes of the shape that it does not envelop (the beam along Y, the
	 * mast and the panels on that beam) are still drawn, darker, so that nothing solid is invisible.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (ClampMin = "0.0"))
	float StationMeshScale = 0.0f;

	/** Scalar "EmissiveBoost" of the model's materials (its windows) and "Brightness" of its lamp material. */
	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (ClampMin = "0.0"))
	float StationWindowBrightness = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (ClampMin = "0.0"))
	float StationLampBrightness = 6.0f;

	/** Colour of the fallback boxes. */
	UPROPERTY(Config, EditAnywhere, Category = "Stations")
	FLinearColor StationColor = FLinearColor(0.32f, 0.35f, 0.42f);

	/** Brightness of the beacons at the ends of the station's beams (PortalMaterial in the sector tint); 0 = none. */
	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (ClampMin = "0.0"))
	float StationBeaconBrightness = 6.0f;

	/** Turret head and gun (any pivot; fitted by their bounds, the longest side of the gun along the aim). Empty = a ball and a barrel. */
	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath TurretHeadMesh;

	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath TurretGunMesh;

	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (ClampMin = "10.0"))
	float TurretHeadSize = 300.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Stations", meta = (ClampMin = "10.0"))
	float TurretGunLength = 420.0f;

	// ---- wormholes (look and sound; where they are is gameplay: see [/Script/HypeCubeJam.HCJGameMode]) ----

	/** Colour of the swirl (BlackHoleMaterial on a card), mixed with the tint of the sector at the other end. */
	UPROPERTY(Config, EditAnywhere, Category = "Wormholes")
	FLinearColor WormholeColor = FLinearColor(0.35f, 0.6f, 1.0f);

	UPROPERTY(Config, EditAnywhere, Category = "Wormholes", meta = (ClampMin = "0.0"))
	float WormholeBrightness = 3.0f;

	/** Brightness of the glowing core (PortalMaterial). */
	UPROPERTY(Config, EditAnywhere, Category = "Wormholes", meta = (ClampMin = "0.0"))
	float WormholeCoreBrightness = 4.0f;

	/** Where a ship comes out of a wormhole (instead of PortalSounds). */
	UPROPERTY(Config, EditAnywhere, Category = "Wormholes")
	TArray<FSoftObjectPath> WormholeSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Wormholes")
	float WormholeVolume = 1.0f;

	// ---- backdrop (AHCJSpaceBackdrop): stars, Milky Way, planets, black holes, nebulae ----

	/** Master switch. Off (or nothing loads) = the old placeholder stars of AHCJUniverse. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	bool bEnableBackdrop = false;

	/** Radius (uu) of the camera-centred sphere the sky is drawn on; must be far beyond a sector (40000 uu). */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "100000.0"))
	float BackdropRadius = 3000000.0f;

	/** Square card mesh facing +X used for stars, bodies, black holes and nebulae. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath BackdropQuadMesh;

	/** Inward-facing sphere and its material (the Milky Way). Empty = black sky behind the stars. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath SkyDomeMesh;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath SkyDomeMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	FName SkyDomeIntensityParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "0.0"))
	float SkyDomeIntensity = 1.0f;

	/**
	 * Star catalogue: a data table whose rows have numeric columns RA (hours), DEC (degrees), Magnitude and
	 * ColorIndex (B-V), e.g. the HYG catalogue of the engine's CelestialVault plugin. Read by column name.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	FSoftObjectPath StarCatalog;

	/** Instanced star material taking 4 custom floats per instance: R, G, B, magnitude. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath StarMaterial;

	/** Density: stars fainter than this magnitude are left out (6 = naked-eye limit, about 5000 stars). */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	float StarMaxMagnitude = 6.0f;

	/** Scalar parameter of StarMaterial added to every magnitude, and its value (negative = brighter). */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	FName StarMagnitudeOffsetParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	float StarMagnitudeOffset = 0.0f;

	/** Magnitude of the extra star drawn where the sun light comes from; 99 = no sun. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	float SunMagnitude = -8.0f;

	/** Planet table: rows with Name, Radius (km) and TextureColumnIndex (column of the atlas PlanetMaterial samples). */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.DataTable"))
	FSoftObjectPath PlanetCatalog;

	/** Sphere mesh for the planets (UV: u around, v pole to pole, poles on its Z axis). */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath PlanetMesh;

	/** Lit instanced planet material taking 1 custom float per instance: the atlas column (M_HCJ_Planet). Lit by the universe's sun. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath PlanetMaterial;

	/**
	 * Planets as their own spheres (M_HCJ_PlanetSurface, built by docs/tools/make_planet_materials.py): the planet's
	 * picture from the atlas projected along the line of sight, day and night from the sun, moving bands, an atmosphere
	 * rim; with PlanetRingMaterial (M_HCJ_PlanetRing) Saturn and Uranus get rings with the planet's shadow on them.
	 * Empty = the older lit instanced spheres with PlanetMaterial.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath PlanetSurfaceMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath PlanetRingMaterial;

	/** Brightness of the day side and the rings, and of the atmosphere rim. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "0.0"))
	float PlanetBrightness = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "0.0"))
	float PlanetRimBrightness = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath MoonMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	FName MoonPhaseParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	FName MoonBrightnessParameter;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop")
	float MoonBrightness = 1.0f;

	/** 0, 1 or 2 bodies in the sky of a sector. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "0", ClampMax = "2"))
	int32 MaxBodiesPerSector = 2;

	/** Multiplier on the apparent size of all bodies (1 = 4 to 18 degrees across). */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float BodySizeScale = 1.0f;

	/**
	 * Look of the black holes: translucent card material with a vector "Tint" and a scalar "Brightness"
	 * (M_HCJ_BlackHole). Where the holes are and how they pull is gameplay: see [/Script/HypeCubeJam.HCJGameMode].
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath BlackHoleMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "0.0"))
	float BlackHoleBrightness = 3.0f;

	/** Additive instanced card material taking 4 custom floats (R, G, B, seed) and a scalar "Intensity" (M_HCJ_Nebula). */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath NebulaMaterial;

	/** Quality / cost: each card is one large additive quad with 4 octaves of noise per pixel. */
	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "0", ClampMax = "4"))
	int32 NebulaCardsPerSector = 3;

	UPROPERTY(Config, EditAnywhere, Category = "Backdrop", meta = (ClampMin = "0.0"))
	float NebulaBrightness = 0.35f;

	// ---- audio: Lyra Starter Game sounds (Epic Games, Fab) under /Game/Audio; classes and submixes under /Game/HCJ/Audio ----

	/** Sound classes the waves are assigned to; their volume is driven by the *Volume settings / console variables below. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (AllowedClasses = "/Script/Engine.SoundClass"))
	FSoftObjectPath MusicSoundClass;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (AllowedClasses = "/Script/Engine.SoundClass"))
	FSoftObjectPath SfxSoundClass;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (AllowedClasses = "/Script/Engine.SoundClass"))
	FSoftObjectPath UiSoundClass;

	/** Group volumes; the console variables hcj.Audio.MusicVolume / SfxVolume / UiVolume override them when >= 0. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MusicVolume = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SfxVolume = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float UiVolume = 0.8f;

	/** Voice limits: the farthest, then oldest, sound of a group is stopped when a new one would exceed them. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "1"))
	int32 MaxFireVoices = 8;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "1"))
	int32 MaxImpactVoices = 6;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "1"))
	int32 MaxExplosionVoices = 4;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "1"))
	int32 MaxWorldVoices = 6;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "1"))
	int32 MaxUiVoices = 6;

	/** One of these per bolt, at the muzzle. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> FireSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	float FireVolume = 0.6f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float FirePitchVariation = 0.06f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> ImpactRockSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> ImpactShipSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	float ImpactVolume = 0.8f;

	/** Played together with ExplosionSound when a ship dies. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> ExplosionSounds;

	/** When a ship starts to boost. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> BoostSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	float BoostVolume = 0.6f;

	/** Where a ship comes out of a portal. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> PortalSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	float PortalVolume = 0.9f;

	/** Looping rumble whose volume rises as the local view sinks into a gravity well. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (AllowedClasses = "/Script/Engine.SoundBase"))
	FSoftObjectPath BlackHoleLoopSound;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	float BlackHoleLoopVolume = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	float BlackHoleLoopPitch = 0.6f;

	// Interface cues of the local player (not spatialised).

	/** A bolt of the local player hit a ship. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> HitConfirmSounds;

	/** The local ship lost shield or hull. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> DamageTakenSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> KillSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> DeathSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> RespawnSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> MatchStartSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> MatchWonSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> MatchLostSounds;

	/** Repeats every LowHealthInterval seconds while the local hull is below LowHealthFraction. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FSoftObjectPath> LowHealthSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthFraction = 0.3f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.5"))
	float LowHealthInterval = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	float UiCueVolume = 0.8f;

	/**
	 * Music, set 0 (the default; UHCJUserSettings::MusicSet): three full-length tracks generated with Google Lyria -
	 * the title menu, a match while calm, a match while the local ship fights - cross-faded by UHCJAudioDirector
	 * (Audio/HCJMusicLogic.h). A track that is empty or does not load is skipped; with none of them the stems play.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	FHCJMusicTrack MenuTrack;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	FHCJMusicTrack ExploreTrack;

	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	FHCJMusicTrack CombatTrack;

	/** Seconds one track fades out while the next fades in. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.1"))
	float MusicTrackFadeSeconds = 2.5f;

	/** The combat track comes in at this music intensity, and goes out below MusicCombatOffIntensity once it has played MusicCombatHoldSeconds. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MusicCombatOnIntensity = 0.75f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MusicCombatOffIntensity = 0.3f;

	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0"))
	float MusicCombatHoldSeconds = 20.0f;

	/** Music, set 1: stems re-triggered on a bar clock by UHCJAudioDirector. Empty = no music. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TArray<FHCJMusicLayer> MusicLayers;

	/** Length of one bar of the stems in seconds (4 beats at 90 bpm). */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "0.25"))
	float MusicBarSeconds = 2.6667f;

	// ---- limits ----

	/** One-shot effects, bolt trails and one-shot sounds farther than this from the local view are not spawned. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "0.0"))
	float EffectMaxDistance = 15000.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "0.0"))
	float SoundInnerRadius = 1500.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "1.0"))
	float SoundFalloffDistance = 13500.0f;

private:
	/** Cache lookup / synchronous load; failures are cached too (as null) so that they are reported only once. */
	static UObject* LoadAsset(const FSoftObjectPath& Path, const UClass* ExpectedClass, const TCHAR* Purpose);
};
