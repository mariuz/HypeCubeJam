#pragma once

#include "CoreMinimal.h"

struct FHCJHypercubeGraph;

/**
 * Sector geometry constants. All distances are in uu (cm), measured from the sector origin
 * (FHCJHypercubeGraph::GetSectorWorldOrigin). Shared by generation, ships, portals and bots.
 */
namespace HCJ
{
	/** Every asteroid (centre distance + bounding radius), spawn point and portal lies within this radius. */
	inline constexpr float SectorFieldRadius = 20000.0f;
	/** Ships farther than this from their sector origin take OutOfBoundsDamagePerSecond (server). */
	inline constexpr float SectorSafeRadius = 26000.0f;
	/** Ships farther than this from their sector origin die immediately (server). */
	inline constexpr float SectorKillRadius = 40000.0f;
	inline constexpr float OutOfBoundsDamagePerSecond = 25.0f;

	/** Portal centre distance from the sector origin is in [PortalMinDistance, PortalMaxDistance]. */
	inline constexpr float PortalMinDistance = 14000.0f;
	inline constexpr float PortalMaxDistance = 17000.0f;
	/** Max angle between a portal's direction and its dimension direction. */
	inline constexpr float PortalJitterDegrees = 10.0f;
	/**
	 * Per-axis tangent-plane jitter scale: just under tan(PortalJitterDegrees) / sqrt(2), so the corner of the
	 * jitter square (both axes at +-1) is tilted 9.994 degrees. A literal, never computed at runtime, because
	 * portal positions must be bit-identical on every machine.
	 */
	inline constexpr double PortalJitterTangent = 0.1246;
	/** Radius of the portal's overlap sphere; a ship whose capsule touches it is teleported. */
	inline constexpr float PortalTriggerRadius = 700.0f;
	/** Radius of the portal's visible disc. */
	inline constexpr float PortalVisualRadius = 900.0f;
	/** No asteroid surface (bounding sphere) and no spawn point within this distance of a portal centre. */
	inline constexpr float PortalClearRadius = 2500.0f;
	/** After a teleport a ship cannot be teleported again for this many seconds (server time). */
	inline constexpr float PortalReentryCooldown = 1.5f;

	/** Number of spawn points generated per sector. */
	inline constexpr int32 NumSpawnPoints = 12;
	/** Spawn point distance from the sector origin is in [SpawnMinDistance, SpawnMaxDistance]. */
	inline constexpr float SpawnMinDistance = 3000.0f;
	inline constexpr float SpawnMaxDistance = 12000.0f;
	/** Min distance from a spawn point to the surface of any asteroid bounding sphere. */
	inline constexpr float SpawnClearance = 600.0f;
	/** Min distance between two spawn points of the same sector. */
	inline constexpr float SpawnSeparation = 1500.0f;

	/** Ship collision is a sphere of this radius (capsule radius == half height). */
	inline constexpr float ShipCollisionRadius = 100.0f;
	/** Net cull distance for ships and projectiles: covers a whole sector, never reaches a neighbour sector. */
	inline constexpr float NetCullDistance = 120000.0f;

	/** Side / diameter of the /Engine/BasicShapes meshes at scale 1. */
	inline constexpr float BasicShapeSize = 100.0f;

	/** A rock drawn for an asteroid may stick out of the asteroid's bounding sphere by at most this (half of SpawnClearance). */
	inline constexpr float MaxVisualOverhang = 0.5f * SpawnClearance;
	/** Width of the rock drawn for a Cone asteroid relative to the cone's base diameter. */
	inline constexpr float ConeVisualWidth = 0.8f;

	/**
	 * Which of NumVariants rock meshes asteroid AsteroidIndex of the sector with seed SectorSeed is drawn with:
	 * a pure hash (HCJ::MixSeed), in [0, NumVariants), 0 when NumVariants <= 1. Deliberately NOT part of
	 * FHCJSectorLayout::Generate: it consumes nothing from the sector's random stream and is not gameplay state
	 * (rocks are drawn only, see AHCJUniverse), so machines with different variant lists still agree on the layout.
	 */
	HYPECUBEJAM_API int32 PickAsteroidVariant(int32 SectorSeed, int32 AsteroidIndex, int32 NumVariants);
}

/** Collision shape of an asteroid (and its look when no rock meshes are configured); index into the per-sector ISM components of AHCJUniverse. */
enum class EHCJAsteroidShape : uint8
{
	Sphere = 0,	// /Engine/BasicShapes/Sphere, always uniformly scaled (its simple collision is a sphere)
	Cube = 1,	// /Engine/BasicShapes/Cube, non-uniform scale allowed
	Cone = 2,	// /Engine/BasicShapes/Cone, non-uniform scale allowed
	Count = 3
};

/** Flavour of a sector: drives asteroid count and size distribution. Purely a generation input. */
enum class EHCJSectorTheme : uint8
{
	Sparse = 0,	// few, medium rocks
	Dense = 1,	// many small rocks
	Giants = 2,	// a handful of huge rocks plus debris
	Shards = 3,	// elongated cubes and cones
	Count = 4
};

/** One asteroid, in sector-local space. */
struct HYPECUBEJAM_API FHCJAsteroid
{
	FVector LocalPosition = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	/** Mesh scale (the mesh is HCJ::BasicShapeSize across at scale 1). Uniform for Sphere. */
	FVector Scale3D = FVector::OneVector;
	/** Conservative bounding-sphere radius around LocalPosition; == ComputeBoundingRadius(Shape, Scale3D). */
	float BoundingRadius = 0.0f;
	EHCJAsteroidShape Shape = EHCJAsteroidShape::Sphere;

	/** Sphere: 50 * Scale3D.X. Cube and Cone: 50 * Scale3D.Size() (half diagonal, conservative for the cone). */
	static float ComputeBoundingRadius(EHCJAsteroidShape InShape, const FVector& InScale3D);

	/** Instance transform of the collision shape (a basic shape mesh) relative to the sector origin. */
	FTransform GetLocalTransform() const { return FTransform(Rotation, LocalPosition, Scale3D); }

	/**
	 * Instance transform, relative to the sector origin, that draws a rock mesh in place of this asteroid.
	 * The mesh's bounding box MeshBounds (any size, any pivot) is centred on LocalPosition and scaled per axis to fit
	 * the collision shape:
	 *  - Cube: the box of the cube (the rock never leaves it).
	 *  - Cone: that box narrowed to HCJ::ConeVisualWidth across (the cone tapers, a rock does not).
	 *  - Sphere: a cube of half-size Radius * Fill / CornerFactor, Fill = min(SphereFill, 1 + HCJ::MaxVisualOverhang / Radius);
	 *    with CornerFactor = the mesh's farthest vertex in half-extent units, that vertex sits at Radius * Fill.
	 * So no rock vertex is farther than BoundingRadius + HCJ::MaxVisualOverhang from LocalPosition: a spawn point
	 * (HCJ::SpawnClearance from the bounding sphere) is never inside or touching a visible rock.
	 * Pure function of its arguments; cosmetic only.
	 */
	FTransform ComputeVisualTransform(const FBox& MeshBounds, float CornerFactor, float SphereFill) const;
};

/** One portal of a sector, in sector-local space. Exists only for open edges. */
struct HYPECUBEJAM_API FHCJPortalSpec
{
	/** Dimension flipped by this portal. */
	int32 Dim = INDEX_NONE;
	/** SectorId ^ (1 << Dim). */
	int32 DestSector = INDEX_NONE;
	/** Portal centre relative to the sector origin; == FHCJSectorLayout::ComputePortalLocalPosition. */
	FVector LocalPosition = FVector::ZeroVector;

	/** Unit vector from the sector origin towards the portal (the direction you fly to leave the sector). */
	FVector GetOutwardDirection() const { return LocalPosition.GetSafeNormal(); }
};

/**
 * Complete procedural description of one sector in LOCAL space (add the sector world origin to place it).
 * Plain data, deterministic from (MatchSeed, SectorId, Graph); no UWorld involved.
 *
 * Guarantees of Generate() (all covered by automation tests):
 *  - Asteroids: |LocalPosition| + BoundingRadius <= HCJ::SectorFieldRadius for every asteroid.
 *    Asteroids may overlap each other.
 *  - Portals: exactly one per open dimension of the sector, sorted by ascending Dim. For every asteroid,
 *    distance(asteroid, portal) >= BoundingRadius + HCJ::PortalClearRadius.
 *  - Mirror rule: the two ends of an edge are point reflections of each other,
 *    ComputePortalLocalPosition(Seed, S, d) == -ComputePortalLocalPosition(Seed, S ^ (1 << d), d),
 *    so a ship translated from one portal to the other while keeping its velocity flies INTO the
 *    destination sector.
 *  - SpawnPoints: exactly HCJ::NumSpawnPoints transforms (scale 1) whose distance to the origin is in
 *    [SpawnMinDistance, SpawnMaxDistance], at least SpawnClearance away from every asteroid bounding
 *    sphere, at least PortalClearRadius from every portal and SpawnSeparation from each other.
 *    Rotation (pitch + yaw, no roll) looks at the sector origin.
 */
struct HYPECUBEJAM_API FHCJSectorLayout
{
	int32 SectorId = INDEX_NONE;
	/** Sector seed == MakeSectorSeed(MatchSeed, SectorId). */
	int32 Seed = 0;
	EHCJSectorTheme Theme = EHCJSectorTheme::Sparse;
	/** Base colour of this sector's asteroids, and of the portals LEADING to this sector. Saturated, bright. */
	FLinearColor Tint = FLinearColor::White;

	TArray<FHCJAsteroid> Asteroids;
	TArray<FHCJPortalSpec> Portals;
	TArray<FTransform> SpawnPoints;

	/**
	 * Generates the layout. Graph must be built and SectorId valid for it; otherwise returns an empty layout
	 * (SectorId == INDEX_NONE). Randomness: FRandomStream(MakeSectorSeed(MatchSeed, SectorId)) for theme, tint,
	 * asteroids and spawn points; portals come from ComputePortalLocalPosition.
	 */
	static FHCJSectorLayout Generate(int32 MatchSeed, int32 SectorId, const FHCJHypercubeGraph& Graph);

	/** HCJ::MixSeed(MatchSeed, 0x1000 + SectorId). */
	static int32 MakeSectorSeed(int32 MatchSeed, int32 SectorId);

	/**
	 * Local position of the portal of SectorId along Dim (meaningful whether or not the edge is open).
	 * With Low = SectorId & ~(1 << Dim) and FRandomStream E(HCJ::MixSeed(HCJ::MixSeed(MatchSeed, Low), 0x50000 + Dim)):
	 *   A    = E.FRandRange(-1, 1); B = E.FRandRange(-1, 1)
	 *   Dir  = normalize(GetDimensionDirection(Dim) + (U * A + V * B) * HCJ::PortalJitterTangent),
	 *          (U, V) = FHCJHypercubeGraph::GetDimensionJitterBasis(Dim)
	 *   Dist = E.FRandRange(HCJ::PortalMinDistance, HCJ::PortalMaxDistance)
	 * (only + - * / and sqrt, so the result is bit-identical on every machine)
	 * and the result is +Dir * Dist when bit Dim of SectorId is clear, -Dir * Dist when it is set.
	 */
	static FVector ComputePortalLocalPosition(int32 MatchSeed, int32 SectorId, int32 Dim);

	/** Portal for Dim, or nullptr when that edge is closed. */
	const FHCJPortalSpec* FindPortal(int32 Dim) const;

	/** True when no asteroid bounding sphere comes within Clearance of LocalPosition. */
	bool IsLocationClear(const FVector& LocalPosition, float Clearance) const;

	/**
	 * CRC32 over the raw bits of everything gameplay relevant (asteroids, portals, spawn points), chained from
	 * InCrc. Two machines that log the same value built bit-identical sectors; the tint is cosmetic and left out.
	 */
	uint32 ComputeChecksum(uint32 InCrc = 0) const;
};
