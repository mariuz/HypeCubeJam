#pragma once

#include "CoreMinimal.h"

/** The three full-length tracks UHCJAudioDirector switches between (generated with Google Lyria, see docs/ASSETS.md). */
enum class EHCJMusicTrack : uint8
{
	None = 0,
	/** Title menu. */
	Menu = 1,
	/** In a match while nothing much happens. */
	Explore = 2,
	/** In a match while the local ship is in a fight. */
	Combat = 3,
	Count
};

/**
 * Which track should play. Plain logic (tested on its own), stepped once a frame by UHCJAudioDirector with the
 * music intensity it already computes (0 cruising alone, about 0.45 with company, 1 while firing / hit).
 * The title menu always wins. In a match the combat track comes in when the intensity reaches CombatOn and goes
 * out again only when it has fallen below CombatOff AND the track has played for MinHoldSeconds, so that one stray
 * shot does not make the music flap.
 */
struct HYPECUBEJAM_API FHCJMusicSelector
{
	EHCJMusicTrack Current = EHCJMusicTrack::None;
	/** Seconds the current track has been the choice. */
	float HeldSeconds = 0.0f;

	EHCJMusicTrack Step(bool bMenu, float Intensity, float DeltaSeconds, float CombatOn, float CombatOff, float MinHoldSeconds);
};

namespace HCJMusic
{
	/**
	 * When, in seconds from the start of a pass, the next pass of a looping track must start so that the two
	 * overlap by the crossfade: LoopSeconds when it is set (the track has an outro that is faded under the new
	 * intro), else the end of the file minus the crossfade. Never less than half the crossfade into the track.
	 */
	HYPECUBEJAM_API float GetLoopStart(float Duration, float LoopSeconds, float CrossfadeSeconds);

	/** One step of a fade: Weight moved towards Target (0 or 1) at 1 / FadeSeconds per second. */
	HYPECUBEJAM_API float StepWeight(float Weight, float Target, float DeltaSeconds, float FadeSeconds);

	/** Decibels of a linear gain (-120 for silence). */
	HYPECUBEJAM_API float GainToDecibels(float Gain);
}
