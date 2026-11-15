#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Hypercube/HCJMatchConfig.h"
#include "World/HCJAsteroidField.h"
#include "World/HCJBlackHole.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJStations.h"
#include "World/HCJWormholes.h"
#include "HCJUniverse.generated.h"

class AHCJPortal;
class AHCJSpaceBackdrop;
class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMesh;

/** Fired on the local machine at the end of every successful AHCJUniverse::Rebuild. */
DECLARE_MULTICAST_DELEGATE(FHCJOnUniverseRebuilt);

/**
 * The generated world of the current match: every sector's asteroids, portals, backdrop and the lighting.
 *
 * LIFETIME / NETWORK MODEL
 *  - NOT replicated (bReplicates = false). Exactly one instance exists per world on EVERY machine (dedicated
 *    server, listen server, each client); each instance is spawned locally by AHCJGameState::ApplyMatchConfig
 *    and owned by that game state (AHCJGameState::GetUniverse). Nobody else spawns it.
 *  - It is built from an FHCJMatchConfig only. Server: AHCJGameMode::InitGameState -> AHCJGameState::SetMatchConfig
 *    -> ApplyMatchConfig -> Rebuild, i.e. BEFORE any player logs in and before world BeginPlay. Clients (including
 *    late joiners): AHCJGameState::OnRep_MatchConfig -> ApplyMatchConfig -> Rebuild. A new match simply calls
 *    Rebuild again with the new config.
 *  - Because it is spawned locally it has ROLE_Authority on clients too: code in this class and in AHCJPortal
 *    must test GetNetMode() != NM_Client for server-only logic, never HasAuthority().
 *
 * CONTENT (/Engine/BasicShapes/{Sphere,Cube,Cone,Cylinder} + /Engine/BasicShapes/BasicShapeMaterial, vector
 * parameter "Color", scalar parameter "Roughness"; rock meshes from UHCJAssetSettings on top)
 *  - Per sector, one UInstancedStaticMeshComponent per EHCJAsteroidShape at the sector origin, instances in
 *    sector-local space, collision profile "BlockAll" (object type WorldStatic), one tinted dynamic material
 *    per sector. Built on every machine, dedicated server included (ships and projectiles collide with it).
 *    This is the ONLY asteroid collision, so it is identical everywhere whatever assets a machine has.
 *  - Cosmetic, not on a dedicated server: when UHCJAssetSettings::AsteroidMeshes load, the shape components are
 *    hidden and each asteroid is drawn by an instance of one rock mesh (one collision-free ISM per mesh and
 *    sector) fitted to its collision shape. Otherwise the shape components stay visible (the fallback).
 *  - One locally spawned AHCJPortal per open (Sector, Dim).
 *  - Cosmetic sky and lights, skipped on a dedicated server: an AHCJSpaceBackdrop (real stars, Milky Way, planets,
 *    black holes, nebulae) when UHCJAssetSettings enables it and its star catalogue loads, otherwise distant
 *    placeholder star instances per sector.
 *
 * The actor sits at the world origin and never moves. Rebuild must work before the actor has begun play.
 * All queries are const, cheap and valid on every machine once IsBuilt().
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class HYPECUBEJAM_API AHCJUniverse : public AActor
{
	GENERATED_BODY()

public:
	AHCJUniverse();

	/** The universe of WorldContextObject's world (through its AHCJGameState), or nullptr if none exists yet. */
	static AHCJUniverse* Get(const UObject* WorldContextObject);

	/**
	 * Destroys everything previously generated and builds the universe for InConfig: graph, all sector layouts,
	 * ISM components, portals, backdrop. Synchronous. No-op when InConfig equals the current config and IsBuilt().
	 * An invalid config (InConfig.IsValid() == false) only clears. Broadcasts OnRebuilt on success.
	 * Called only by AHCJGameState::ApplyMatchConfig.
	 */
	void Rebuild(const FHCJMatchConfig& InConfig);

	bool IsBuilt() const { return bBuilt; }
	const FHCJMatchConfig& GetConfig() const { return Config; }

	/** The match topology. Unbuilt graph (NumSectors() == 0) while !IsBuilt(). */
	const FHCJHypercubeGraph& GetGraph() const { return Graph; }
	int32 NumSectors() const { return Graph.NumSectors(); }

	/**
	 * CRC32 of the open-edge masks and every sector layout of the current build (0 while !IsBuilt()). Logged by
	 * Rebuild on every machine: equal values on server and client mean they generated the same universe.
	 */
	uint32 GetLayoutChecksum() const { return LayoutChecksum; }

	/** Local-space layout of a sector, or nullptr when !IsBuilt() or Sector is invalid. */
	const FHCJSectorLayout* GetSectorLayout(int32 Sector) const;

	/** FHCJHypercubeGraph::GetSectorWorldOrigin(Sector). */
	FVector GetSectorOrigin(int32 Sector) const;

	/** Sector whose origin is nearest to WorldLocation (always a valid id), or INDEX_NONE when !IsBuilt(). */
	int32 GetSectorAtLocation(const FVector& WorldLocation) const;

	/** Tint of a sector; white for an invalid sector. */
	FLinearColor GetSectorTint(int32 Sector) const;

	/**
	 * World transform (scale 1) of a spawn point of Sector picked uniformly with Stream; rotation faces the
	 * sector origin. Does not check for ships already standing there (the ship class spawns with
	 * AdjustIfPossibleButAlwaysSpawn). Identity when !IsBuilt() or Sector is invalid.
	 */
	FTransform GetRandomSpawnTransform(int32 Sector, FRandomStream& Stream) const;

	/** True when Sector has a portal along Dim, i.e. GetGraph().IsEdgeOpen(Sector, Dim). */
	bool HasPortal(int32 Sector, int32 Dim) const;

	/**
	 * World transform of the portal of Sector along Dim: location = portal centre, rotation X axis = outward
	 * direction (away from the sector origin), scale 1. Identity when HasPortal(Sector, Dim) is false.
	 */
	FTransform GetPortalWorldTransform(int32 Sector, int32 Dim) const;

	/** The local portal actor of (Sector, Dim), or nullptr when the edge is closed. */
	AHCJPortal* GetPortal(int32 Sector, int32 Dim) const;

	/** True when no asteroid bounding sphere of the sector containing WorldLocation comes within Clearance of it. */
	bool IsLocationClear(const FVector& WorldLocation, float Clearance) const;

	/**
	 * A world location inside Sector, within HCJ::SpawnMaxDistance of its origin, at least Clearance away from
	 * every asteroid bounding sphere (best effort: after 16 failed tries returns a spawn point location).
	 * Used by bots for wander targets. Sector origin when !IsBuilt() or Sector is invalid.
	 */
	FVector GetRandomFreeLocation(int32 Sector, FRandomStream& Stream, float Clearance) const;

	// ---- black holes (gameplay; identical on every machine, see FHCJBlackHole) ----

	/** Rules of the current match (from the replicated config). */
	const FHCJBlackHoleSettings& GetBlackHoleSettings() const { return Config.BlackHoles; }
	/** True when Sector has a black hole. */
	bool HasBlackHole(int32 Sector) const { return bBuilt && BlackHoles.IsValidIndex(Sector) && BlackHoles[Sector].bPresent; }
	/** World location of the hole of Sector; the sector origin when it has none. */
	FVector GetBlackHoleLocation(int32 Sector) const;
	/**
	 * Velocity (uu/s) with which the black hole of the sector containing WorldLocation carries things at that
	 * point towards itself; zero outside every influence radius. Pure function of the location and the match
	 * config: used by the movement simulation of ships on the owning client and the server alike.
	 */
	FVector GetGravityDrift(const FVector& WorldLocation) const;
	/** Distance from WorldLocation to the hole of its sector, or a negative number when that sector has none. */
	double GetBlackHoleDistance(const FVector& WorldLocation) const;
	/** True when WorldLocation is inside the event horizon of its sector's hole. */
	bool IsInsideEventHorizon(const FVector& WorldLocation) const;

	// ---- space stations (gameplay; identical on every machine, see FHCJStation) ----

	bool HasStation(int32 Sector) const { return bBuilt && Stations.IsValidIndex(Sector) && Stations[Sector].bPresent; }
	/** The station of Sector (bPresent false when it has none or Sector is invalid). */
	const FHCJStation& GetStation(int32 Sector) const;
	/** World location of the centre of Sector's station; the sector origin when it has none. */
	FVector GetStationLocation(int32 Sector) const;
	uint32 GetStationChecksum() const { return StationChecksum; }

	// ---- wormholes (gameplay; identical on every machine, see FHCJWormholes) ----

	/** The wormholes of the current build (both mouths placed), sorted by SectorA. */
	TConstArrayView<FHCJWormholeLink> GetWormholeLinks() const { return WormholeLinks; }
	/** Shortest routes over portals and wormholes (GetGraph() answers pure-portal questions). */
	const FHCJRouteTable& GetRoutes() const { return Routes; }
	bool HasWormhole(int32 Sector) const { return bBuilt && Routes.GetWormholeDest(Sector) != INDEX_NONE; }
	/** Sector at the other end of Sector's wormhole, or INDEX_NONE. */
	int32 GetWormholeDest(int32 Sector) const { return bBuilt ? Routes.GetWormholeDest(Sector) : INDEX_NONE; }
	/** World location of the wormhole mouth of Sector; the sector origin when it has none. */
	FVector GetWormholeLocation(int32 Sector) const;
	uint32 GetWormholeChecksum() const { return WormholeChecksum; }

	/** A way out of Sector as FHCJRouteTable::NextHop names it: a portal dimension, or HCJ::WormholeHop. */
	bool HasHop(int32 Sector, int32 Hop) const;
	/** World location to fly to for that hop (portal centre or wormhole mouth); the sector origin when HasHop is false. */
	FVector GetHopLocation(int32 Sector, int32 Hop) const;

	// ---- pickup pads (gameplay; identical on every machine, see FHCJPickupPads) ----

	/** Pad positions of Sector relative to its origin (pad index = array index); empty for an invalid sector or with pickups off. */
	TConstArrayView<FVector> GetPickupPads(int32 Sector) const;
	/** CRC32 of every pad position of the current build (0 without pads); logged next to the layout checksum. */
	uint32 GetPickupPadChecksum() const { return PickupPadChecksum; }

	// ---- destroyable asteroids (gameplay; World/HCJAsteroidField.h). The server owns health and decides; which rocks are
	// gone is replicated by AHCJGameState and applied here on every machine. ----

	/** Rules of the current match (from the replicated config). */
	const FHCJAsteroidRules& GetAsteroidRules() const { return Config.Asteroids; }
	/** Key (HCJAsteroids::PackKey) of the rock that instance Item of Component is, or MAX_uint32 when it is not a rock (a station box, ...). */
	uint32 FindAsteroidKey(const UPrimitiveComponent* Component, int32 Item) const;
	/**
	 * SERVER ONLY: Damage on the rock that instance Item of Component is (what a trace or a projectile hit reports).
	 * False when that is not a rock or rocks cannot be damaged in this match. A rock whose health is used up is
	 * destroyed through AHCJGameState::ServerDestroyAsteroid.
	 */
	bool ServerDamageAsteroid(const UPrimitiveComponent* Component, int32 Item, float Damage, AController* DamageInstigator);
	bool ServerDamageAsteroidKey(uint32 Key, float Damage, AController* DamageInstigator);
	/**
	 * SERVER ONLY: a blast at Origin. Every rock whose surface is within Radius takes Damage scaled like a torpedo
	 * blast (full within InnerRadius of the surface, EdgeScale at Radius); ExceptKey is left out. Returns the number damaged.
	 */
	int32 ServerDamageAsteroidsInRadius(const FVector& Origin, float InnerRadius, float Radius, float EdgeScale, float Damage, AController* DamageInstigator, uint32 ExceptKey);
	/**
	 * Every machine: makes the destroyed set of this universe equal to Keys (the replicated list; it only ever grows
	 * within a match). New entries remove their rock and put its pieces in place; with bEffects the break is shown
	 * and heard. Returns the number of rocks that changed.
	 */
	int32 SyncDestroyedAsteroids(TConstArrayView<uint32> Keys, bool bEffects);
	/** Every machine: destroys one rock (see SyncDestroyedAsteroids). False when it already was. */
	bool ApplyAsteroidDestroyed(uint32 Key, bool bEffects);
	int32 GetNumDestroyedAsteroids() const { return DestroyedAsteroids.Num(); }
	/** CRC32 of the destroyed set (0 when nothing is destroyed); equal on two machines = the same rocks are gone. */
	uint32 GetDestroyedChecksum() const { return DestroyedAsteroids.ComputeChecksum(); }
	/** Rocks that exist right now (layout rocks not removed for a station, minus destroyed, plus pieces). */
	int32 GetNumLiveAsteroids() const { return Rocks.Num(); }
	/** World location and bounding radius of an existing rock; false when Key names none. */
	bool GetAsteroidSphere(uint32 Key, FVector& OutLocation, float& OutRadius) const;
	/** Key of the existing rock nearest to WorldLocation in its sector (development commands), or MAX_uint32. MinRadius: only rocks at least that big. */
	uint32 FindNearestAsteroid(const FVector& WorldLocation, float MinRadius) const;
	/** GetWorld()->GetTimeSeconds() of the last Rebuild on this machine. */
	float GetBuildTime() const { return BuildTime; }

	/** The local sky (stars, planets, black holes, nebulae), or nullptr: dedicated server, or switched off in UHCJAssetSettings. */
	AHCJSpaceBackdrop* GetBackdrop() const { return Backdrop; }

	/**
	 * Cosmetic, local: the two lights of the scene for the sector the local view is in (called by the backdrop when the
	 * view changes sector). The sun keeps its direction; the fill light shines from where the backlit core of that
	 * sector's sky stands (AHCJSpaceBackdrop::MakeBacklight), in its colour. Intensities from UHCJAssetSettings.
	 */
	void ApplySectorLighting(int32 Sector);

	/** Local-machine notification that a (re)build finished; HUD and bots may cache per-match data on it. */
	FHCJOnUniverseRebuilt OnRebuilt;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Destroys all generated components and portal actors and resets Graph/Layouts/bBuilt. */
	void ClearGenerated();
	/**
	 * Creates the ISM components (and their instances) of one sector from Layouts[Sector]: one per EHCJAsteroidShape
	 * with collision (every machine; these ARE the asteroids as far as gameplay goes) and, when rock meshes are
	 * available, hides them and adds one collision-free ISM per rock mesh (BuildSectorRocks).
	 */
	void BuildSectorGeometry(int32 Sector);
	/** Cosmetic: draws every asteroid of the sector as a rock (variant by HCJ::PickAsteroidVariant, transform by FHCJAsteroid::ComputeVisualTransform). */
	void BuildSectorRocks(int32 Sector);
	/** Cosmetic: (re)loads the rock meshes named by UHCJAssetSettings. */
	void LoadRockMeshes();
	/** Collision boxes of the station of one sector (every machine) and what is drawn for it (not on a dedicated server). */
	void BuildStation(int32 Sector);
	/** One existing rock: its shape, where its collision instance and its drawn instance are, and (server) its health; negative = undamaged. */
	struct FRock
	{
		FHCJAsteroid Asteroid;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Collision;
		int32 CollisionIndex = INDEX_NONE;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Visual;
		int32 VisualIndex = INDEX_NONE;
		float Health = -1.0f;
	};
	/** New, unregistered component for the collision shapes / the rock mesh of a sector (material, collision, transform set up). */
	UInstancedStaticMeshComponent* CreateShapeComponent(int32 Sector, EHCJAsteroidShape Shape);
	UInstancedStaticMeshComponent* CreateRockComponent(int32 Sector, int32 Variant);
	/** The collision component of a shape / the drawn component of a rock mesh in a sector; created when the first rock needs it. */
	UInstancedStaticMeshComponent* GetShapeComponent(int32 Sector, EHCJAsteroidShape Shape);
	UInstancedStaticMeshComponent* GetRockComponent(int32 Sector, int32 Variant);
	/** Adds a rock (a piece of a destroyed one) to the world: collision instance, drawn instance, bookkeeping. */
	void AddRock(int32 Sector, uint16 Id, const FHCJAsteroid& Asteroid);

	/** Spawns and initialises the two mouth actors (AHCJPortal in wormhole mode) of one wormhole. */
	void BuildWormholeMouths(const FHCJWormholeLink& Link);
	/** Spawns and initialises the AHCJPortal actors of one sector. */
	void BuildSectorPortals(int32 Sector);
	/** Cosmetic star backdrop around one sector, seeded from the sector seed. Not called on a dedicated server. */
	void BuildSectorBackdrop(int32 Sector);
	/** New dynamic instance of BasicShapeMaterial with "Color" = Color and "Roughness" = Roughness. */
	UMaterialInstanceDynamic* CreateTintedMaterial(const FLinearColor& Color, float Roughness);
	UStaticMesh* GetShapeMesh(EHCJAsteroidShape Shape) const;
	/** Index into Portals for (Sector, Dim): Sector * FHCJHypercubeGraph::MaxDimension + Dim. */
	static int32 PortalIndex(int32 Sector, int32 Dim) { return Sector * FHCJHypercubeGraph::MaxDimension + Dim; }

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Key light; movable, casts shadows. Project exposure is fixed (auto exposure off), intensity is in lux. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UDirectionalLightComponent> SunLight;

	/** Weak shadowless light from the opposite side so that unlit faces are not pure black. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UDirectionalLightComponent> FillLight;

	/** Loaded in the constructor (ConstructorHelpers) from /Engine/BasicShapes; indexed by EHCJAsteroidShape. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> ShapeMeshes;

	/**
	 * Rock meshes of UHCJAssetSettings::AsteroidMeshes that loaded (never filled on a dedicated server), with their
	 * corner factors in the parallel array. Empty = asteroids are drawn as their collision shapes.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> RockMeshes;

	TArray<float> RockCornerFactors;

	/** /Engine/BasicShapes/BasicShapeMaterial. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	/** Every component created by Rebuild (asteroid ISMs and backdrop ISMs), for ClearGenerated. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> GeneratedComponents;

	/** Dynamic materials created by Rebuild (kept referenced for the lifetime of the build). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GeneratedMaterials;

	/** Cosmetic sky, spawned by the first Rebuild on machines that render (see AHCJSpaceBackdrop); lives as long as the universe. */
	UPROPERTY(Transient)
	TObjectPtr<AHCJSpaceBackdrop> Backdrop;

	/** NumSectors * MaxDimension entries, see PortalIndex; nullptr for closed edges. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AHCJPortal>> Portals;

	FHCJMatchConfig Config;
	FHCJHypercubeGraph Graph;
	/** Indexed by sector id. */
	TArray<FHCJSectorLayout> Layouts;
	/** Indexed by sector id; bPresent false for sectors without a hole. */
	TArray<FHCJBlackHole> BlackHoles;
	/** Indexed by sector id; bPresent false for sectors without a station. */
	TArray<FHCJStation> Stations;
	uint32 StationChecksum = 0;

	/** Collision boxes and meshes of the stations, for ClearGenerated. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> StationComponents;

	/** Wormholes whose two mouths could be placed; mouth positions (relative to the sector origin) by sector id. */
	TArray<FHCJWormholeLink> WormholeLinks;
	TArray<FVector> WormholeMouths;
	FHCJRouteTable Routes;
	uint32 WormholeChecksum = 0;

	/** The two local mouth actors of every wormhole. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AHCJPortal>> WormholePortals;

	/** Every rock that exists, by key. */
	TMap<uint32, FRock> Rocks;
	/** Per collision component: the key of each of its instances (MAX_uint32 for an instance whose rock is gone). */
	TMap<TObjectKey<UInstancedStaticMeshComponent>, TArray<uint32>> CollisionInstanceKeys;
	/** Per sector: the collision component of each shape and the drawn component of each rock mesh (null until needed). */
	TArray<TArray<TObjectPtr<UInstancedStaticMeshComponent>>> SectorShapeComponents;
	TArray<TArray<TObjectPtr<UInstancedStaticMeshComponent>>> SectorRockComponents;
	/** Tinted material of the collision shapes of each sector (they are drawn only without rock meshes); null on a dedicated server. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SectorShapeMaterials;
	FHCJDestroyedSet DestroyedAsteroids;
	float BuildTime = 0.0f;

	/** Indexed by sector id: pad positions relative to the sector origin. */
	TArray<TArray<FVector>> PickupPads;
	uint32 PickupPadChecksum = 0;
	uint32 LayoutChecksum = 0;
	bool bBuilt = false;
};
