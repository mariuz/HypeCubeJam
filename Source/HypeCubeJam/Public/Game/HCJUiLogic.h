#pragma once

#include "CoreMinimal.h"

/** Kill-streak callouts of the local player. */
enum class EHCJStreak : uint8
{
	None,
	/** Two kills within FHCJStreakTracker::MultiKillWindow of each other. */
	Double,
	/** Three or more in a row, each within the window of the one before. */
	Triple,
	/** Every RampageEvery-th kill without dying in between. */
	Rampage
};

/**
 * Detects kill streaks from the kill events of one player. Plain logic without a world (tested on its own); the
 * HUD side keeps one for the local player and feeds it from the kill feed, so nothing extra is replicated.
 */
struct HYPECUBEJAM_API FHCJStreakTracker
{
	static constexpr float MultiKillWindow = 4.0f;
	static constexpr int32 RampageEvery = 5;

	/** A kill at Time (seconds, any monotonic clock). Returns the callout it earns; a rampage outranks a multi-kill. */
	EHCJStreak RegisterKill(float Time);
	/** The player died: both counters start again. */
	void RegisterDeath();
	/** New match. */
	void Reset() { RegisterDeath(); }

	int32 GetLifeStreak() const { return LifeStreak; }

private:
	int32 LifeStreak = 0;
	int32 MultiCount = 0;
	float LastKillTime = -1000.0f;
};

/** Pure helpers of the HUD and the menu flow (no world, no canvas), so they can be tested. */
namespace HCJUi
{
	/** Scale of every HUD size: 1 at 1920x1080, following the smaller of the two axis ratios, clamped to [0.5, 3]. */
	HYPECUBEJAM_API float ComputeUIScale(float ClipX, float ClipY);
	/** Scale of HUD text: the UI scale, but never below 0.8 so that the smallest labels stay readable at 1280x720. */
	HYPECUBEJAM_API float ComputeTextScale(float UIScale);
	/** Radius in pixels of the circle a cone of half angle SpreadDegrees covers on a screen ClipX wide with a horizontal field of view FovDegrees. */
	HYPECUBEJAM_API float ComputeSpreadRadius(float SpreadDegrees, float FovDegrees, float ClipX);
	/**
	 * Where the marker of something off screen goes: on the ellipse with half axes Radius around Centre, in the
	 * screen direction (ViewRight, ViewUp components of the direction to the thing). OutAngle is the direction of
	 * the arrow in radians (0 = right, counter-clockwise with screen Y down negated). A zero direction points up.
	 */
	HYPECUBEJAM_API FVector2D ComputeEdgeMarker(float RightComponent, float UpComponent, const FVector2D& Centre, const FVector2D& Radius, float& OutAngle);
	/**
	 * True when Marker (the screen box of a world marker with its label) overlaps none of Panels, each grown by
	 * Margin on every side: such a marker may be drawn where it is; otherwise it would lie on a HUD panel.
	 */
	HYPECUBEJAM_API bool IsMarkerClear(const FBox2D& Marker, TConstArrayView<FBox2D> Panels, float Margin = 0.0f);
	/**
	 * True when a process started with this command line should open on the title menu: no map URL or address
	 * given, not a dedicated server, no rendering-less or capture run, no -AutoHost. ProjectName (and a .uproject
	 * path) are the tokens a launcher or the editor binary put in front; they are not a URL.
	 */
	HYPECUBEJAM_API bool WantsStartMenu(const FString& CommandLine, const FString& ProjectName);
}

/** An enemy as the target selection sees it. */
struct FHCJTargetCandidate
{
	/** Angle between the aim direction and the direction to the enemy, degrees. */
	float AngleDeg = 180.0f;
	/** Distance to the enemy, uu. */
	float Distance = 0.0f;
	/** In view and not behind a rock. */
	bool bVisible = false;
};

namespace HCJUi
{
	/** Half angle of the cone around the crosshair inside which the soft lock picks by angle, degrees. */
	inline constexpr float TargetConeDeg = 22.0f;
	/** A current target keeps the lock until another candidate is this many degrees better (no flicker between two ships). */
	inline constexpr float TargetStickinessDeg = 5.0f;

	/**
	 * Score of a candidate for the soft lock, lower is better: its angle from the crosshair, plus a little for
	 * distance (1 degree per 6000 uu), minus the stickiness when it is the current target. A candidate outside
	 * the cone or not visible is ranked behind every one inside it, by distance.
	 */
	HYPECUBEJAM_API float ScoreTarget(const FHCJTargetCandidate& Candidate, bool bIsCurrent);
	/** Index of the best candidate, INDEX_NONE for an empty list. CurrentIndex (or INDEX_NONE) is the target held so far. */
	HYPECUBEJAM_API int32 PickTarget(TConstArrayView<FHCJTargetCandidate> Candidates, int32 CurrentIndex);
	/**
	 * "Fire now": the aim direction passes within HitRadius of the lead point. AimErrorDeg is the angle between the
	 * aim direction and the direction to the lead point, Distance the distance to it.
	 */
	HYPECUBEJAM_API bool IsOnLead(float AimErrorDeg, float Distance, float HitRadius);
	/**
	 * Radar: where something at (Forward, Right, Up) relative to the ship (uu, in the ship's own frame) goes on a
	 * disc of radius 1 seen from above with forward at the top: X = right, Y = -forward, scaled by Range and
	 * clamped to the rim. OutHeight is the height above (+) or below (-) the ship's plane in [-1, 1] of Range.
	 */
	HYPECUBEJAM_API FVector2D ProjectRadar(float Forward, float Right, float Up, float Range, float& OutHeight);

	/** "It has its nose on us": an enemy this close (uu) whose forward direction is within this many degrees of the line to the own ship. */
	inline constexpr float AimWarnDistance = 9000.0f;
	inline constexpr float AimWarnConeDeg = 12.0f;
	/** "ENEMY BEHIND": the nose has to stay on the own ship this long before the line and the cue come (a bot on Hard holds its fire for 0.5 s). */
	inline constexpr float AimWarnDwellSeconds = 0.3f;
	/** ... and the cue is the warning tick at this pitch, so that it is not the torpedo's "break away". */
	inline constexpr float AimWarnCuePitch = 0.67f;
	/**
	 * Whether another ship is aiming at the own ship, from what every machine knows of it: OtherForward is its forward
	 * direction, OtherToOwn the vector from it to the own ship. Used for the "!" arrow, the ring on the radar and
	 * "ENEMY BEHIND" - the warning before a bot that got behind a player opens fire.
	 */
	HYPECUBEJAM_API bool IsAimingAt(const FVector& OtherForward, const FVector& OtherToOwn, float MaxDistance = AimWarnDistance, float ConeDeg = AimWarnConeDeg);
}

/** Where a hit came from, as the pilot saw it (the death recap). */
/** One entry of a "the N nearest" list (HCJUi::InsertNearest): what it is (an index the caller knows) and how far. */
struct FHCJNearSlot
{
	double DistSq = 0.0;
	int32 Index = INDEX_NONE;
};

namespace HCJUi
{
	/** Hull fraction below which the HUD treats the hull as low: the red vignette, the hint, the pointer to a repair pad. */
	inline constexpr float LowHullFraction = 0.35f;
	/** Pads of the sector that get a marker at most (the nearest ones). */
	inline constexpr int32 MaxPickupMarkers = 6;
	/** Strength of the marker of a pad at the far end of the sector (still readable). */
	inline constexpr float PickupMarkerFarStrength = 0.7f;

	/**
	 * Keeps the nearest entries seen so far in Slots, sorted nearest first, without allocating: adds (DistSq, Index)
	 * to the Num entries already there when there is room or when it is nearer than the farthest. Returns the new
	 * number of entries (at most Slots.Num()). An equal distance goes behind the entries already there.
	 */
	HYPECUBEJAM_API int32 InsertNearest(TArrayView<FHCJNearSlot> Slots, int32 Num, double DistSq, int32 Index);
	/**
	 * How strong the marker of a pad is drawn (alpha and size factor, PickupMarkerFarStrength .. 1): full up to
	 * NearDistance, then fading linearly to PickupMarkerFarStrength at FarDistance and beyond. bEmphasis (the repair
	 * pad on a low hull) is always 1.
	 */
	HYPECUBEJAM_API float PickupMarkerStrength(float Distance, float NearDistance, float FarDistance, bool bEmphasis);
	/** Distance in metres as a marker shows it: to the metre up to 50 m, in steps of 5 m beyond (the text changes less often). Distance in uu. */
	HYPECUBEJAM_API int32 MarkerMetres(float Distance);
}

enum class EHCJHitSide : uint8
{
	Ahead,
	Side,
	Behind
};

namespace HCJUi
{
	/**
	 * BearingDeg is the angle between the own nose and the direction to the shooter (0 dead ahead, 180 dead astern):
	 * behind beyond the rear arc of HCJBalance::RearArcDot (about 107 degrees), from the side beyond 60 degrees, else head-on.
	 */
	HYPECUBEJAM_API EHCJHitSide ClassifyHitSide(float BearingDeg);
}

/** One label of the hypercube map: the mark it belongs to, the room its text needs and (out) where it goes. */
struct FHCJMapLabel
{
	/** Centre of the mark of its sector, pixels. */
	FVector2D Anchor = FVector2D::ZeroVector;
	/** Half size of that mark: the label keeps clear of it. */
	float AnchorRadius = 0.0f;
	/** Size of the text. */
	FVector2D Size = FVector2D::ZeroVector;
	/** Out: centre of the text. */
	FVector2D Center = FVector2D::ZeroVector;
};

namespace HCJUi
{
	/**
	 * Hypercube map: where sector Sector of a Dimension-dimensional hypercube is drawn. X and Y are in [-1, 1] (Y
	 * down, the picture fills the square), Z is the depth in [0, 1] (1 = nearest to the viewer: drawn larger and
	 * brighter). The first three bits span a cube seen a little from the right and from above, with a mild
	 * perspective; every further bit shifts the whole cube: the fourth sideways (two cubes next to each other), the
	 * fifth downwards (four cubes, two by two), the sixth a little back along a diagonal. So a portal of one dimension
	 * always runs in the same direction on the map. Pure function of its arguments; SwayRad turns the cubes about
	 * their vertical axis (the slow sway of the HUD) without changing the scale.
	 */
	HYPECUBEJAM_API FVector ProjectMapSector(int32 Sector, int32 Dimension, float SwayRad = 0.0f);

	/**
	 * Puts every label next to its mark - below, above, beside or diagonally, near or one line farther out - so that
	 * it stays inside Bounds, off the marks of all labelled sectors, off the other labels, off Obstacles (the enemy
	 * badges) and, where there is a choice, off the plain dots (Dots, each DotHalfSize around its centre) and on the
	 * side of its mark that faces away from AwayFrom (the own sector: the lines to the neighbours start there). Three
	 * rounds, each label taking the best of sixteen places given where the others are. Returns the area (square
	 * pixels) by which labels still overlap marks, obstacles or each other.
	 */
	HYPECUBEJAM_API float PlaceMapLabels(TArrayView<FHCJMapLabel> Labels, const FBox2D& Bounds, TConstArrayView<FVector2D> Dots, float DotHalfSize, const FVector2D& AwayFrom,
		TConstArrayView<FBox2D> Obstacles = TConstArrayView<FBox2D>());
}

/** Process-wide menu flow state that has to survive a map change (leaving a server, a failed connection). */
namespace HCJMenuFlow
{
	/** The next local controller that hosts should open on the title menu whatever the command line says. */
	HYPECUBEJAM_API void RequestMenu(const FString& Notice = FString());
	/** True when a hosting local controller created now should start in the menu (and not get a ship). */
	HYPECUBEJAM_API bool ShouldOpenMenu();
	/** The Play button was used: later worlds of this process host straight away unless RequestMenu is called again. */
	HYPECUBEJAM_API void MarkPlayed();
	/** A line for the menu (why the player is back here); emptied by TakeNotice. */
	HYPECUBEJAM_API FString TakeNotice();
	/** A connection attempt failed since the last call (set by the network failure handler). */
	HYPECUBEJAM_API bool TakeFailure();
	HYPECUBEJAM_API void NotifyFailure(const FString& Notice);
}
