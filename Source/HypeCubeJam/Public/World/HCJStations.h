#pragma once

#include "CoreMinimal.h"
#include "Hypercube/HCJMatchConfig.h"

struct FHCJAsteroid;
struct FHCJBlackHole;
struct FHCJSectorLayout;

namespace HCJ
{
	/** A station's distance from the sector origin (uu). */
	inline constexpr float StationMinDistance = 5000.0f;
	inline constexpr float StationMaxDistance = 10000.0f;
	/** Everything solid of a station (body boxes, turrets) lies within this distance of its centre. */
	inline constexpr float StationBodyRadius = 2300.0f;
	/** Asteroids whose bounding sphere comes within this distance of the station centre are removed (see IsAsteroidSuppressed). */
	inline constexpr float StationRockFreeRadius = 3000.0f;
	/** No portal centre, spawn point, pickup pad or wormhole mouth within these distances of the station centre. */
	inline constexpr float StationPortalClearance = 5500.0f;
	inline constexpr float StationSpawnClearance = 3500.0f;
	inline constexpr float StationPadClearance = 3000.0f;
	inline constexpr float StationWormholeClearance = 4000.0f;
	/** The station centre is this far OUTSIDE the influence radius of its sector's black hole. */
	inline constexpr float StationHoleMargin = 2500.0f;
	inline constexpr int32 StationPlacementAttempts = 64;
	/** Turrets per station. */
	inline constexpr int32 StationMinTurrets = 3;
	inline constexpr int32 StationMaxTurrets = 6;
	/** Radius of a turret's collision sphere and how far in front of its centre its shots start. */
	inline constexpr float TurretRadius = 130.0f;
	inline constexpr float TurretMuzzleOffset = 300.0f;

	/**
	 * Lead aim: the point to shoot at so that a projectile of constant speed ProjectileSpeed leaving ShooterLocation
	 * now meets a target at TargetLocation moving with constant TargetVelocity. Returns false, and the target's
	 * current location, when the target cannot be caught (too fast, or no projectile speed).
	 * Pure; used by station turrets (and tested on its own).
	 */
	HYPECUBEJAM_API bool ComputeLeadAim(const FVector& ShooterLocation, const FVector& TargetLocation, const FVector& TargetVelocity,
		float ProjectileSpeed, FVector& OutAimPoint);
}

/** A box of the station's body in station space (the station is never rotated, so it is also axis aligned in the sector). */
struct FHCJStationBox
{
	FVector Center = FVector::ZeroVector;
	FVector HalfExtent = FVector::ZeroVector;
};

/** Where a turret sits on the station (station space) and which way is "up" for it (away from the body). */
struct FHCJTurretMount
{
	FVector Location = FVector::ZeroVector;
	FVector Up = FVector::UpVector;
};

/**
 * The space station of one sector: GAMEPLAY state like pads, wormholes and black holes - a pure function of
 * (match seed, finished sector layout, hole, pads, wormhole mouth, settings) with its own hash stream, computed
 * identically on every machine. The layout generator, its checksum and its golden tests are untouched.
 *
 * The station is a fixed, unrotated shape of a few boxes (GetBodyBoxes), which are its collision on every machine
 * whatever is drawn; turrets sit on a seeded choice of its mounts (GetAllMounts). Asteroids of the layout that
 * would overlap it are not built (IsAsteroidSuppressed): a deterministic filter applied by AHCJUniverse on every
 * machine alike, on top of the unchanged layout.
 *
 * The turrets themselves are run-time state (alive / destroyed, aim): replicated AHCJTurret actors.
 */
struct HYPECUBEJAM_API FHCJStation
{
	bool bPresent = false;
	/** Centre relative to the sector origin. */
	FVector LocalPosition = FVector::ZeroVector;
	/** Mounts in use (station space), HCJ::StationMinTurrets..StationMaxTurrets of them. */
	TArray<FHCJTurretMount> Turrets;

	/**
	 * The station of Layout's sector, or bPresent == false: stations off, the sector lost the SectorFraction roll,
	 * or no position met the guarantees:
	 *  - HCJ::StationMinDistance <= |LocalPosition| <= HCJ::StationMaxDistance;
	 *  - portals / spawn points / pads / the wormhole mouth at least HCJ::Station*Clearance away;
	 *  - with a black hole in the sector: at least InfluenceRadius + HCJ::StationHoleMargin from its centre.
	 * Asteroids do not block a position: those in the way are suppressed.
	 */
	static FHCJStation Make(int32 MatchSeed, const FHCJSectorLayout& Layout, const FHCJBlackHole& Hole, const FHCJBlackHoleSettings& HoleSettings,
		TConstArrayView<FVector> Pads, bool bHasWormhole, const FVector& WormholeMouth, const FHCJStationSettings& Settings);

	/** True when Asteroid (of this station's sector layout) must not be built because it would reach into the station. */
	bool IsAsteroidSuppressed(const FHCJAsteroid& Asteroid) const;

	/** Boxes of GetBodyBoxes from this index on are solar panels (drawn darker). */
	static constexpr int32 FirstPanelBox = 4;

	/** The solid boxes of every station, in station space. */
	static TConstArrayView<FHCJStationBox> GetBodyBoxes();
	/** All HCJ::StationMaxTurrets mounts of the station shape, in station space. */
	static TConstArrayView<FHCJTurretMount> GetAllMounts();

	/** CRC32 of the position and the mounts in use, chained from InCrc. */
	uint32 ComputeChecksum(uint32 InCrc) const;
};
