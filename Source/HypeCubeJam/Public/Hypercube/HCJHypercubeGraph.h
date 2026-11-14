#pragma once

#include "CoreMinimal.h"

/**
 * Shared world-scale constants and seed helpers. Plain C++ (no UObject, no UWorld) so that
 * everything in this header is usable from automation tests.
 */
namespace HCJ
{
	/** Distance in uu between neighbouring cells of the world grid on which sector origins are placed. */
	inline constexpr double SectorSpacing = 500000.0;

	/**
	 * Deterministic, build-independent seed mixer. Every derived seed in the project (per sector, per edge,
	 * per match) MUST come from this function, never from GetTypeHash/HashCombine or FMath::Rand.
	 */
	inline constexpr int32 MixSeed(int32 A, int32 B)
	{
		uint32 H = static_cast<uint32>(A) * 0x9E3779B1u;
		H ^= static_cast<uint32>(B) + 0x85EBCA6Bu + (H << 6) + (H >> 2);
		H ^= H >> 16;
		H *= 0x7FEB352Du;
		H ^= H >> 15;
		H *= 0x846CA68Bu;
		H ^= H >> 16;
		return static_cast<int32>(H);
	}
}

/** Inline list of dimension indices (a sector has at most MaxDimension of them). */
using FHCJDimArray = TArray<int32, TInlineAllocator<8>>;

/**
 * The match topology: an N-dimensional hypercube whose 2^N vertices are Sectors and whose edges are
 * portals that are either open or closed.
 *
 *  - Sector id = bitmask in [0, 2^N). The neighbour of S along dimension d is S ^ (1 << d).
 *  - An edge is identified by (Sector, Dim); (S, d) and (S ^ (1 << d), d) are the SAME edge, so every
 *    query below is symmetric by construction.
 *  - The open-edge subgraph is always connected: Build() opens a random spanning tree first and then
 *    each remaining hypercube edge with probability EdgeProbability.
 *  - Fully deterministic from (Dimension, Seed, EdgeProbability): the only randomness source is one
 *    FRandomStream(Seed). Server and every client build bit-identical graphs.
 *
 * Plain value type: copyable, no UObject references, usable without a UWorld. All queries are O(1)
 * table lookups after Build(); none of them allocates except GetOpenDims.
 * Any query with an out-of-range sector or dimension returns the documented failure value, never asserts.
 */
struct HYPECUBEJAM_API FHCJHypercubeGraph
{
	static constexpr int32 MinDimension = 2;
	static constexpr int32 MaxDimension = 6;
	static constexpr int32 DefaultDimension = 4;
	static constexpr float DefaultEdgeProbability = 0.5f;

	/**
	 * (Re)builds the graph. InDimension is clamped to [MinDimension, MaxDimension], InEdgeProbability to [0, 1].
	 * Algorithm (fixed, because the result is part of the network contract):
	 *  1. FRandomStream Stream(InSeed).
	 *  2. Random spanning tree by randomized frontier growth starting at sector 0: keep the list of
	 *     closed edges joining a visited to an unvisited sector, pick one with Stream.RandRange, open it.
	 *  3. Visit every still-closed edge in ascending (Sector, Dim) order with bit Dim of Sector clear and
	 *     open it when Stream.FRand() < EdgeProbability.
	 *  4. Compute the all-pairs distance and next-hop tables by BFS.
	 */
	void Build(int32 InDimension, int32 InSeed, float InEdgeProbability);

	/** Back to the unbuilt state (IsBuilt() == false, NumSectors() == 0). */
	void Reset();

	bool IsBuilt() const { return Dimension >= MinDimension; }
	int32 GetDimension() const { return Dimension; }
	int32 GetSeed() const { return Seed; }
	float GetEdgeProbability() const { return EdgeProbability; }

	/** 2^Dimension, or 0 when not built. */
	int32 NumSectors() const { return IsBuilt() ? (1 << Dimension) : 0; }

	bool IsValidSector(int32 Sector) const { return Sector >= 0 && Sector < NumSectors(); }
	bool IsValidDim(int32 Dim) const { return Dim >= 0 && Dim < Dimension; }

	/** True when the portal between Sector and GetNeighbor(Sector, Dim) exists. False for invalid arguments. */
	bool IsEdgeOpen(int32 Sector, int32 Dim) const;

	/** Sector ^ (1 << Dim) regardless of whether the edge is open; INDEX_NONE for invalid arguments. */
	int32 GetNeighbor(int32 Sector, int32 Dim) const;

	/** Bit d set <=> IsEdgeOpen(Sector, d). 0 for an invalid sector. Never 0 for a valid one (connected graph). */
	uint8 GetOpenDimMask(int32 Sector) const;

	/** Open dimensions of Sector in ascending order (1..Dimension entries for a valid sector). */
	FHCJDimArray GetOpenDims(int32 Sector) const;

	/** Number of open (undirected) edges; >= NumSectors() - 1. */
	int32 NumOpenEdges() const;

	/**
	 * First step of a shortest open-edge path From -> To: the LOWEST dimension d such that (From, d) is open
	 * and ShortestDistance(GetNeighbor(From, d), To) == ShortestDistance(From, To) - 1.
	 * INDEX_NONE when From == To or either sector is invalid.
	 */
	int32 NextHopDim(int32 From, int32 To) const;

	/** Number of portals on a shortest path From -> To (0 when equal); INDEX_NONE for invalid sectors. */
	int32 ShortestDistance(int32 From, int32 To) const;

	/** True when every sector is reachable from sector 0 over open edges. Always true after Build(); exists for tests. */
	bool IsConnected() const;

	// ---- static helpers (valid without a built graph) ----

	/** Sector id as exactly InDimension binary digits, most significant bit first: (5, 4) -> "0101". */
	static FString SectorToBinaryString(int32 Sector, int32 InDimension);

	/**
	 * World-space origin of a sector. Sectors sit on a 4x4x4 grid of HCJ::SectorSpacing cells:
	 * grid X = bits 0-1 of the id, grid Y = bits 2-3, grid Z = bits 4-5, origin = (X, Y, Z) * SectorSpacing.
	 * Sector 0 is at the world origin; all coordinates are >= 0. Independent of the match dimension.
	 */
	static FVector GetSectorWorldOrigin(int32 Sector);

	/**
	 * Inverse of GetSectorWorldOrigin: the sector whose origin is nearest to WorldLocation, restricted to
	 * the sectors that exist for InDimension (each grid axis is clamped to the range its bits allow).
	 * Always returns a valid id in [0, 2^InDimension).
	 */
	static int32 FindSectorAtLocation(const FVector& WorldLocation, int32 InDimension);

	/**
	 * Unit direction associated with a dimension, used to place portals (see FHCJSectorLayout).
	 * Normalised: 0 (1,0,0)  1 (0,1,0)  2 (0,0,1)  3 (1,1,1)  4 (-1,1,1)  5 (1,-1,1).
	 * The 12 signed directions are pairwise at least 54.7 degrees apart. ZeroVector for an invalid Dim.
	 */
	static FVector GetDimensionDirection(int32 Dim);

	/**
	 * Two constant unit vectors that form an orthonormal basis with GetDimensionDirection(Dim); portals are
	 * jittered in their plane. Hard-coded (no trigonometry) so they are bit-identical on every machine.
	 * Both are ZeroVector, and the function returns false, for an invalid Dim.
	 */
	static bool GetDimensionJitterBasis(int32 Dim, FVector& OutU, FVector& OutV);

private:
	int32 TableIndex(int32 From, int32 To) const { return From * NumSectors() + To; }

	int32 Dimension = 0;
	int32 Seed = 0;
	float EdgeProbability = 0.0f;

	/** Per sector: bitmask of open dimensions. */
	TArray<uint8> OpenMask;
	/** NumSectors^2 table, [From * NumSectors + To] = hop count. */
	TArray<uint8> DistanceTable;
	/** NumSectors^2 table, [From * NumSectors + To] = next-hop dimension, 0xFF on the diagonal. */
	TArray<uint8> NextHopTable;
};
