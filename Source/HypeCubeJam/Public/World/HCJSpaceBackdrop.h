#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HCJSpaceBackdrop.generated.h"

class AHCJUniverse;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPostProcessComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;

/**
 * What is in the sky of one sector besides the fixed stars: a pure function of (match seed, sector id), so every
 * player in a sector sees the same sky without anything being replicated. Cosmetic only: it uses its own hash
 * (HCJ::MixSeed), never the FRandomStream of FHCJSectorLayout, and is not part of the layout checksum.
 * Directions are unit vectors in world axes (the sky does not rotate).
 */
struct HYPECUBEJAM_API FHCJSectorSky
{
	struct FBody
	{
		/** Index into the body catalogue passed to Make, in [0, NumCatalogBodies). */
		int32 BodyIndex = INDEX_NONE;
		FVector Direction = FVector::ForwardVector;
		/** Apparent diameter before the per-body size factor, degrees. */
		float AngularDiameterDegrees = 8.0f;
		/** 0..1, phase for bodies whose material supports one (the Moon). */
		float Phase = 0.5f;
	};

	struct FNebula
	{
		FVector Direction = FVector::ForwardVector;
		/** Angular width of the cloud card, degrees. */
		float AngularSizeDegrees = 70.0f;
		/** Roll of the card around its direction, degrees. */
		float RollDegrees = 0.0f;
		/** Noise seed of the cloud, [0, 100). */
		float Seed = 0.0f;
		/** Hue offset from the sector tint, degrees. */
		float HueShiftDegrees = 0.0f;
		/** Relative brightness, [0.6, 1]. */
		float Strength = 1.0f;
	};

	/** One or two distinct catalogue bodies (none when the catalogue is empty or MaxBodies <= 0). */
	TArray<FBody, TInlineAllocator<2>> Bodies;

	TArray<FNebula, TInlineAllocator<4>> Nebulae;

	/** Minimum angle kept between the two bodies, degrees (best effort after 12 tries). */
	static constexpr float MinSeparationDegrees = 40.0f;

	/**
	 * @param NumCatalogBodies   number of bodies that can be shown (planets + Moon)
	 * @param MaxBodies          0..2 bodies per sector
	 * @param NumNebulae         nebula cards per sector, 0..4
	 */
	static FHCJSectorSky Make(int32 MatchSeed, int32 SectorId, int32 NumCatalogBodies, int32 MaxBodies, int32 NumNebulae);
};

/**
 * The sky: real stars, the Milky Way, Solar System bodies and nebula clouds - and the look of the black holes.
 *
 * NETWORK MODEL: none. One instance per machine, spawned locally by AHCJUniverse::Rebuild next to the universe;
 * never on a dedicated server, nothing replicated, no collision, no shadows, no influence on gameplay, on the
 * layout generator or on its golden values. What a sector shows follows from FHCJSectorSky::Make(match seed,
 * sector id), so all players in a sector see the same sky.
 *
 * "AT INFINITY": the actor moves to the local camera every frame and everything is drawn on spheres of radius
 * UHCJAssetSettings::BackdropRadius (and a little less) around it, far beyond anything a ship can reach and
 * unaffected by the 500000 uu spacing of the sectors. The sky never rotates: its axes are the world axes.
 *
 * CONTENT (all paths in UHCJAssetSettings, "Backdrop"; every piece is skipped when its asset does not load):
 *  - Stars (REAL DATA): every star of the HYG catalogue data table up to StarMaxMagnitude as one instanced draw;
 *    right ascension / declination give the direction, the colour index (B-V) the colour, the magnitude the
 *    brightness (4 custom floats per instance: R, G, B, magnitude - the layout of the engine's CelestialVault
 *    star material, which also keeps every star at least a few pixels wide so that TAA cannot eat it).
 *    One extra, very bright instance stands where AHCJUniverse's sun light comes from.
 *  - Milky Way (REAL IMAGERY): the CelestialVault dome mesh and material; the stars are attached to it and use
 *    the same frame as in that plugin, so star positions and the band agree.
 *  - Bodies (REAL NAMES AND IMAGERY, FICTIONAL PLACEMENT): rows of the CelestialVault planet table, drawn as lit
 *    spheres (project material M_HCJ_Planet over the plugin's planet atlas; the universe's sun light gives them
 *    their phase), plus the Moon as a camera-facing disc with the plugin's Moon material. Which ones, where and
 *    how big: FHCJSectorSky.
 *  - Nebulae (PROCEDURAL): project material M_HCJ_Nebula on a few additive cards, tinted from the sector tint.
 *  - Black hole (PROCEDURAL LOOK of a GAMEPLAY object): where the hole is and what it does is AHCJUniverse /
 *    FHCJBlackHole; this actor only draws it: one translucent card with M_HCJ_BlackHole at the hole's world
 *    position, turned to the camera every frame, sized so that the dark disc is exactly the event horizon.
 *    It is the one part of the backdrop that is NOT at infinity.
 *
 * CINEMATIC LOOK (devlog item 26; all cosmetic, all local, numbers in UHCJAssetSettings "Cinematic"):
 *  - Sky layers per sector, all instances of M_HCJ_Nebula (6 custom floats: R, G, B, seed, core, soft): the seeded
 *    clouds of FHCJSectorSky, one BACKLIT CORE cloud in the direction MakeBacklight gives (the universe turns its
 *    fill light to shine from there and tints the rim light of the rocks with its colour), a few soft haze cards so
 *    that no direction is black, and NearClouds: soft cards that stand in the world 600-1000 m from the sector origin
 *    and therefore shift against the rest as a ship flies. Made-up faint stars thicken the catalogue stars.
 *  - Decorative rocks of the sector the view is in (FHCJSectorDecor: pebbles around the real rocks and a belt of
 *    boulders beyond the kill radius), as instances without collision; rebuilt when the view changes sector.
 *  - An unbound post process component (bloom, vignette, contrast, a teal-and-orange grade), enabled by
 *    UHCJUserSettings::bCinematicLook; switched off, the game renders with the engine defaults as before and keeps
 *    DecorLowShare of the decorative rocks.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class HYPECUBEJAM_API AHCJSpaceBackdrop : public AActor
{
	GENERATED_BODY()

public:
	AHCJSpaceBackdrop();

	/** Follows the local camera and switches to the sky of the sector the camera is in. */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * (Re)creates the match-independent parts on first use (dome, stars, catalogues) and resets the per-sector
	 * part for a new match. Called by AHCJUniverse::Rebuild (the owner) once the universe is built.
	 * @param SunDirection  unit vector from the scene towards the sun (opposite to the sun light's direction)
	 */
	void Rebuild(int32 InMatchSeed, const FVector& SunDirection);

	/** True when real stars are drawn (the universe then skips its placeholder stars). */
	bool HasStars() const { return NumStars > 0; }
	int32 GetNumStars() const { return NumStars; }

	/** The bodies in the sky of Sector, for the HUD: e.g. "JUPITER, MOON"; empty when none. */
	FString GetSkyLabel(int32 Sector) const;

	/** The sky of Sector for the current match (FHCJSectorSky::Make with the loaded catalogue and the settings). */
	FHCJSectorSky MakeSky(int32 Sector) const;

	/**
	 * Where the backlit core cloud of a sector's sky stands and its colour: a pure function of its arguments (own hash),
	 * at least about 80 degrees away from SunDirection so that the fill light from there lights the side the sun leaves
	 * dark. Used by this actor (the cloud), by AHCJUniverse (fill light, rim light of the rocks) and by the scene command.
	 */
	static void MakeBacklight(int32 InMatchSeed, int32 Sector, const FLinearColor& Tint, const FVector& SunDirection, FVector& OutDirection, FLinearColor& OutColor);

	/** A colour of the sky of a sector: the tint turned by HueShiftDegrees and pulled towards teal by UHCJAssetSettings::SkyTealMix. */
	static FLinearColor MakeSkyColor(const FLinearColor& Tint, float HueShiftDegrees);

	/** Decorative rocks drawn right now (pebbles + belt), for the log and the frame-rate notes. */
	int32 GetNumDecorRocks() const { return NumDecorRocks; }

protected:
	/** Loads the dome, the star catalogue and the body catalogue; creates the star instances. Once per actor. */
	void BuildFixedSky(const FVector& SunDirection);
	/** Replaces the body and nebula instances with those of Sector and shows or hides the black hole card. */
	void ShowSector(int32 Sector);
	/** Puts the black hole card on the hole of the shown sector, facing ViewLocation. */
	void UpdateBlackHoleCard(const FVector& ViewLocation);
	/** Replaces the decorative rocks and the near clouds with those of Sector (INDEX_NONE: none). */
	void BuildDecor(int32 Sector);
	/** Creates the decor components on first use; false when there is nothing to draw them with. */
	bool EnsureDecorComponents();
	/** Sets the post process component from the settings and switches it on or off. */
	void ApplyCinematicLook(bool bOn);

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Milky Way dome (radius BackdropRadius). */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	/** One instance per catalogue star, attached to SkyDome (positions in its 100-unit frame). */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UInstancedStaticMeshComponent> Stars;

	/** Planet spheres of the current sector (1 custom float per instance: atlas column). */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UInstancedStaticMeshComponent> Planets;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> Moon;

	/**
	 * The planets of the current sector when UHCJAssetSettings::PlanetSurfaceMaterial loads (else the Planets instances
	 * above are used): one sphere each with its own instance of M_HCJ_PlanetSurface (the planet's picture projected along
	 * the line of sight, day / night from the sun, moving bands, atmosphere rim), and for a ringed planet one quad with
	 * M_HCJ_PlanetRing around it. At most MaxPlanetBalls bodies per sector.
	 */
	static constexpr int32 MaxPlanetBalls = 2;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> PlanetBalls[MaxPlanetBalls];

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> PlanetRings[MaxPlanetBalls];

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PlanetBallMaterials[MaxPlanetBalls];

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PlanetRingMaterials[MaxPlanetBalls];

	/** Unit vector towards the sun of the scene (where the universe's key light comes from). */
	FVector SunDirectionWS = FVector(0.0, 0.0, 1.0);

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> BlackHole;

	/** Nebula cards of the current sector (4 custom floats per instance: R, G, B, seed). */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UInstancedStaticMeshComponent> Nebulae;

	/** Soft cloud cards of the current sector that stand in the world around the sector origin (not at infinity). */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UInstancedStaticMeshComponent> NearClouds;

	/** Decorative rocks of the current sector, one component per rock mesh: pebbles (culled by distance) and the far belt. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> DecorPebbles;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> DecorBelt;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DecorMaterials;

	/** Bloom, vignette and grade of the "Cinematic look" setting; unbound. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MoonMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BlackHoleMaterial;

	/** A body that can appear in a sector sky. */
	struct FCatalogBody
	{
		FString Name;
		/** Column of the planet atlas; INDEX_NONE for the Moon (own component). */
		int32 AtlasColumn = INDEX_NONE;
		/** Multiplier on the apparent diameter, from the real radius. */
		float SizeFactor = 1.0f;
		/** Colour of the atmosphere rim and how strong it is (an airless body has a faint one). */
		FLinearColor Atmosphere = FLinearColor(0.5f, 0.6f, 0.8f);
		float RimStrength = 1.0f;
		/** Strength of the slowly moving band / cloud pattern. */
		float Clouds = 0.5f;
		/** The surface is procedural bands instead of the atlas picture (Saturn: its picture has the rings in it). */
		bool bBanded = false;
		/** Outer radius of its rings in planet radii (0 = no rings), their inner radius, colour and opacity. */
		float RingOuter = 0.0f;
		float RingInner = 1.25f;
		FLinearColor RingColor = FLinearColor(0.85f, 0.75f, 0.6f);
		float RingOpacity = 0.9f;
	};
	TArray<FCatalogBody> Bodies;

	int32 MatchSeed = 0;
	int32 ShownSector = INDEX_NONE;
	int32 NumStars = 0;
	int32 NumDecorRocks = 0;
	bool bFixedSkyBuilt = false;
	bool bDecorTried = false;
	/** Which decor component draws every pebble: the rock mesh with the fewest triangles in its last LOD (and that count). */
	int32 DecorPebbleVariant = 0;
	int32 DecorPebbleTriangles = MAX_int32;
	/** What ApplyCinematicLook was last called with (-1: never). */
	int32 CinematicApplied = -1;
};
