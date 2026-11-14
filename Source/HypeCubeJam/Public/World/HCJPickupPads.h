#pragma once

#include "CoreMinimal.h"
#include "Hypercube/HCJMatchConfig.h"

struct FHCJBlackHole;
struct FHCJSectorLayout;

namespace HCJ
{
	/** A pad's distance from the sector origin (uu): inside the asteroid field, inside the spawn shell. */
	inline constexpr float PadMinDistance = 2500.0f;
	inline constexpr float PadMaxDistance = 13000.0f;
	/** No asteroid bounding sphere within this distance of a pad. */
	inline constexpr float PadRockClearance = 500.0f;
	/** No spawn point within this distance of a pad: nobody spawns on top of an item. */
	inline constexpr float PadSpawnClearance = 800.0f;
	/** Pads of one sector are at least this far apart. */
	inline constexpr float PadSeparation = 3000.0f;
	/** Positions tried per pad before the sector goes with fewer pads. */
	inline constexpr int32 PadPlacementAttempts = 64;
}

/**
 * Where the pickup pads of one sector are: GAMEPLAY state like the asteroid layout and the black holes, a pure
 * function of (match seed, finished sector layout, the sector's black hole, settings), computed locally and
 * identically on every machine from the replicated match config. It reads the layout and draws from its own
 * hash stream (HCJ::FFeatureHash), so asteroids, portals, spawn points, the layout checksum and its golden values
 * are untouched. Only + - * / and sqrt feed the accept / reject decisions.
 *
 * What lies on a pad is NOT decided here: the server picks items at run time and replicates them (AHCJPickup).
 */
struct HYPECUBEJAM_API FHCJPickupPads
{
	/**
	 * Pad positions relative to the sector origin, in a fixed order (pad index = array index). Empty when pickups
	 * are off or the layout is invalid; fewer than asked for when no position met the guarantees:
	 *  - HCJ::PadMinDistance <= |pad| <= HCJ::PadMaxDistance;
	 *  - no asteroid bounding sphere within HCJ::PadRockClearance;
	 *  - every portal centre at least HCJ::PortalClearRadius away;
	 *  - every spawn point at least HCJ::PadSpawnClearance away;
	 *  - when the sector has a black hole: at least FHCJBlackHole::GetClearance from its centre (outside the
	 *    strong pull);
	 *  - pads at least HCJ::PadSeparation apart.
	 */
	static TArray<FVector> Make(int32 MatchSeed, const FHCJSectorLayout& Layout, const FHCJBlackHole& Hole,
		const FHCJBlackHoleSettings& HoleSettings, const FHCJPickupSettings& Settings);

	/** Number of pads Make aims for in a sector: Settings.MinPads..MaxPads, from the match seed. */
	static int32 GetWantedPadCount(int32 MatchSeed, int32 SectorId, const FHCJPickupSettings& Settings);
};
