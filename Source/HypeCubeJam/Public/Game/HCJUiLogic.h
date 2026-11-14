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
