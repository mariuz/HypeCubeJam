#pragma once

#include "CoreMinimal.h"
#include "Hypercube/HCJMatchConfig.h"

struct FHCJSectorLayout;

namespace HCJ
{
	/**
	 * No portal trigger and no spawn point lies within this fraction of the influence radius of a black hole:
	 * nobody spawns or leaves a portal where the pull is more than a gentle drift.
	 */
	inline constexpr float BlackHoleClearanceFraction = 0.6f;
	/** The hole's centre is this far from the sector origin (uu): inside the asteroid field, reachable. */
	inline constexpr float BlackHoleMinDistance = 4000.0f;
	inline constexpr float BlackHoleMaxDistance = 12000.0f;
	/** No asteroid bounding sphere within this many horizon radii of the centre. */
	inline constexpr float BlackHoleRockFreeHorizons = 2.0f;
	/** Positions tried per sector before the sector goes without a hole. */
	inline constexpr int32 BlackHolePlacementAttempts = 48;
}

/**
 * The black hole of one sector: GAMEPLAY state, like the asteroid layout. Plain data, a pure function of
 * (match seed, sector layout, settings), computed locally and identically on the server and every client; the
 * settings travel in the replicated FHCJMatchConfig, so all machines use the server's values.
 *
 * It does not touch FHCJSectorLayout: it reads the finished layout and draws from its own integer hash
 * (HCJ::MixSeed), never from the layout's FRandomStream, so asteroids, portals, spawn points, the layout checksum
 * and the golden values are exactly what they were without black holes. Only + - * / and sqrt feed the
 * accept / reject decisions (see the note in HCJSectorLayout.cpp), so two machines cannot disagree on a position.
 *
 * GRAVITY is a velocity field, not a force: a ship (or bolt) at distance d from the centre is carried towards it
 * with speed ComputePullSpeed(d) on top of its own motion. A pure function of position, which is what lets the
 * owning client's prediction and the server's simulation agree move for move (UHCJShipMovementComponent).
 *   PullSpeed(d) = min(MaxPullSpeed, PullSpeed * ((InfluenceRadius / d)^2 - 1))   for d < InfluenceRadius, else 0
 * i.e. inverse-square shaped, exactly zero at the edge (no jump), clamped near the centre. With the defaults
 * (6000 / 200 / 9000) the drift is 150 uu/s at three quarters of the radius, 600 at half, equals the cruise speed
 * (3000) at 1500 uu and the boost speed (6000) at about 1080 uu: from there on no ship gets out, and at
 * HorizonRadius (600) it is destroyed.
 */
struct HYPECUBEJAM_API FHCJBlackHole
{
	bool bPresent = false;
	/** Centre relative to the sector origin. */
	FVector LocalPosition = FVector::ZeroVector;

	/**
	 * The hole of Layout's sector, or bPresent == false: black holes off, the sector lost the SectorFraction
	 * roll, or none of HCJ::BlackHolePlacementAttempts positions met the guarantees below.
	 * Guarantees when present (covered by automation tests):
	 *  - HCJ::BlackHoleMinDistance <= |LocalPosition| <= HCJ::BlackHoleMaxDistance;
	 *  - every portal centre is at least GetClearance(Settings) + HCJ::PortalTriggerRadius away;
	 *  - every spawn point is at least GetClearance(Settings) away;
	 *  - no asteroid bounding sphere within HCJ::BlackHoleRockFreeHorizons * HorizonRadius of the centre.
	 */
	static FHCJBlackHole Make(int32 MatchSeed, const FHCJSectorLayout& Layout, const FHCJBlackHoleSettings& Settings);

	/** HCJ::BlackHoleClearanceFraction * InfluenceRadius, at least 2 * HorizonRadius. */
	static float GetClearance(const FHCJBlackHoleSettings& Settings);

	/** Speed (uu/s) at which something Distance away from the centre is carried towards it; see the class comment. */
	static float ComputePullSpeed(double Distance, const FHCJBlackHoleSettings& Settings);

	/** The drift velocity at a point: ToHole (centre minus point) normalised times ComputePullSpeed(|ToHole|). Zero at the centre. */
	static FVector ComputePull(const FVector& ToHole, const FHCJBlackHoleSettings& Settings);

	/** Distance from the centre below which a ship flying straight out at EscapeSpeed no longer gets away (0 if it always does). */
	static float ComputeNoEscapeRadius(float EscapeSpeed, const FHCJBlackHoleSettings& Settings);
};
