#pragma once

#include "CoreMinimal.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Hypercube/HCJMatchConfig.h"

struct FHCJBlackHole;
struct FHCJSectorLayout;

namespace HCJ
{
	/** A wormhole mouth's distance from the sector origin (uu). */
	inline constexpr float WormholeMinDistance = 4000.0f;
	inline constexpr float WormholeMaxDistance = 12000.0f;
	/** Radius of the mouth's trigger: a ship that touches it is carried to the other mouth. */
	inline constexpr float WormholeTriggerRadius = 500.0f;
	/** No asteroid bounding sphere within this distance of a mouth. */
	inline constexpr float WormholeRockClearance = 1100.0f;
	/** No portal centre, spawn point or pickup pad within these distances of a mouth. */
	inline constexpr float WormholePortalClearance = 4000.0f;
	inline constexpr float WormholeSpawnClearance = 1500.0f;
	inline constexpr float WormholePadClearance = 1500.0f;
	/** A mouth is this far OUTSIDE the influence radius of its sector's black hole. */
	inline constexpr float WormholeHoleMargin = 500.0f;
	/** Positions tried per mouth before the link is dropped. */
	inline constexpr int32 WormholePlacementAttempts = 96;
	/** Smallest hypercube dimension with wormholes: below it every pair of sectors is at most two portals apart. */
	inline constexpr int32 WormholeMinDimension = 3;

	/** "Hop" value of FHCJRouteTable::NextHop that means "take the wormhole of this sector" (portal hops are the dimension, 0..5). */
	inline constexpr int32 WormholeHop = 100;
}

/** One two-way wormhole: SectorA < SectorB, never hypercube neighbours. */
struct FHCJWormholeLink
{
	int32 SectorA = INDEX_NONE;
	int32 SectorB = INDEX_NONE;

	bool operator==(const FHCJWormholeLink& Other) const { return SectorA == Other.SectorA && SectorB == Other.SectorB; }
};

/**
 * Wormholes: two-way shortcuts between sectors that are NOT hypercube neighbours. GAMEPLAY state like the pads
 * and the black holes: pure functions of the match seed (and the finished layouts for the mouth positions) with
 * their own hash stream, computed identically on every machine; the graph, the layouts, their checksums and the
 * golden tests are untouched, and FHCJHypercubeGraph keeps answering pure-hypercube questions.
 */
struct HYPECUBEJAM_API FHCJWormholes
{
	/** Number of links for a dimension when the settings ask for "auto": a quarter of the sectors; 0 below HCJ::WormholeMinDimension. */
	static int32 GetAutoCount(int32 Dimension);

	/**
	 * Which sectors are linked: up to Count pairs (Count < 0 = GetAutoCount), each sector in at most one pair, the
	 * two sectors of a pair differ in at least two bits (no portal could ever join them). Sorted by SectorA.
	 * Empty when wormholes are off or the dimension is below HCJ::WormholeMinDimension.
	 * Pure function of (MatchSeed, Dimension, Count): a seeded shuffle of the sectors, then greedy pairing.
	 */
	static TArray<FHCJWormholeLink> MakeLinks(int32 MatchSeed, int32 Dimension, const FHCJWormholeSettings& Settings);

	/**
	 * Position of the mouth in Layout's sector, relative to the sector origin. False when no position met:
	 *  - HCJ::WormholeMinDistance <= |mouth| <= HCJ::WormholeMaxDistance;
	 *  - no asteroid bounding sphere within HCJ::WormholeRockClearance;
	 *  - portals / spawn points / pads at least HCJ::WormholePortalClearance / SpawnClearance / PadClearance away;
	 *  - with a black hole in the sector: at least InfluenceRadius + HCJ::WormholeHoleMargin from its centre.
	 */
	static bool MakeMouth(int32 MatchSeed, const FHCJSectorLayout& Layout, const FHCJBlackHole& Hole, const FHCJBlackHoleSettings& HoleSettings,
		TConstArrayView<FVector> Pads, FVector& OutLocalPosition);
};

/**
 * Shortest routes over portals AND wormholes (the hypercube graph alone answers pure-portal questions). Plain
 * value type built by BFS; a wormhole counts as one hop like a portal.
 */
struct HYPECUBEJAM_API FHCJRouteTable
{
	/** Graph must be built. Links whose sectors are invalid for the graph are ignored. */
	void Build(const FHCJHypercubeGraph& Graph, TConstArrayView<FHCJWormholeLink> Links);
	void Reset();

	bool IsBuilt() const { return NumSectors > 0; }
	/** Hops (portals + wormholes) on a shortest route; 0 when equal; INDEX_NONE for invalid sectors. Never more than Graph.ShortestDistance. */
	int32 Distance(int32 From, int32 To) const;
	/**
	 * First hop of a shortest route: a portal dimension (the lowest that works), or HCJ::WormholeHop when only the
	 * wormhole of From starts a shortest route. INDEX_NONE when From == To or a sector is invalid.
	 */
	int32 NextHop(int32 From, int32 To) const;
	/** The sector at the other end of From's wormhole, or INDEX_NONE. */
	int32 GetWormholeDest(int32 Sector) const { return WormholeDest.IsValidIndex(Sector) ? WormholeDest[Sector] : INDEX_NONE; }

private:
	int32 NumSectors = 0;
	TArray<int32> WormholeDest;
	TArray<uint8> DistanceTable;
	TArray<uint8> NextHopTable;
};
