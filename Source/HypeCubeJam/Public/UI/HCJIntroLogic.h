#pragma once

#include "CoreMinimal.h"

/** How the intro movie ended. */
enum class EHCJIntroEnd : uint8
{
	/** Still on the screen. */
	None,
	/** It played to its end. */
	Finished,
	/** A key, a mouse button or a gamepad button. */
	Skipped,
	/** The movie did not show a frame within OpenTimeout. */
	OpenTimeout,
	/** The player reported an error. */
	Failed,
	/** It ran longer than MaxSeconds (a stalled decoder): never block the game. */
	TooLong
};

/**
 * The intro movie as plain logic (no engine media types, tested on its own): fed once a frame with what the media
 * player reports, it says when the movie is over and why, and what the overlay shows. The one rule above all others:
 * it always ends - after OpenTimeout without a frame, after MaxSeconds whatever happens, on any error, on any key.
 *
 * Local and cosmetic: AHCJPlayerController runs one for the local host in front of the title menu, once per process.
 */
struct HYPECUBEJAM_API FHCJIntroFlow
{
	/** Seconds the movie has to show its first frame. */
	float OpenTimeout = 4.0f;
	/** Hard limit on the whole intro, seconds (the movie is 10 s long). */
	float MaxSeconds = 20.0f;
	/** Input is ignored for this long after the start, so that the click that started the game does not skip it. */
	float SkipGrace = 0.4f;
	/** The title fades in over TitleFade seconds, TitleLead seconds before the end; the picture fades out over the last FadeOut seconds. */
	float TitleLead = 3.2f;
	float TitleFade = 0.8f;
	float FadeOut = 0.5f;

	/**
	 * One frame. Position / Duration: seconds as the player reports them (0 while it is still opening); bPlaying: it
	 * says it is playing; bError: it reports an error. Returns the end reason once the intro is over (and keeps
	 * returning it).
	 */
	EHCJIntroEnd Tick(float DeltaSeconds, bool bPlaying, bool bError, float Position, float Duration);

	/** Any key / button. Ignored during SkipGrace and once the intro is over. */
	void RequestSkip();

	bool IsOver() const { return End != EHCJIntroEnd::None; }
	EHCJIntroEnd GetEnd() const { return End; }
	/** The movie has shown a frame. */
	bool HasStarted() const { return bStarted; }
	/** Seconds since the intro began / seconds it took until the first frame (negative before). */
	float GetElapsed() const { return Elapsed; }
	float GetStartDelay() const { return StartDelay; }

	/** 0..1: how visible the title over the movie is. */
	float GetTitleAlpha() const;
	/** 0..1: how black the picture is (1 until the first frame, rising again over the last FadeOut seconds). */
	float GetBlackAlpha() const;

	static const TCHAR* DescribeEnd(EHCJIntroEnd InEnd);

private:
	EHCJIntroEnd End = EHCJIntroEnd::None;
	bool bStarted = false;
	bool bSkipRequested = false;
	float Elapsed = 0.0f;
	float StartDelay = -1.0f;
	float LastPosition = 0.0f;
	float LastDuration = 0.0f;
	/** Seconds the player has said "not playing" since it started (a movie that ends without reaching its duration). */
	float StoppedSeconds = 0.0f;
};

namespace HCJIntro
{
	/** Path of the movie below the project's Content folder (staged as a loose file: Content/Movies is not cooked into the pak). */
	inline const TCHAR* const MoviePath = TEXT("Movies/intro_veo.mp4");

	/**
	 * True when a process started with this command line shows the intro before its title menu: it would open on the
	 * title menu (HCJUi::WantsStartMenu: no map URL or address, no -AutoHost / -server / -nullrhi / capture run), it can
	 * render, it is not a dedicated server, and -HCJNoIntro is not given. -HCJIntro forces it in a run that opens the
	 * title menu for another reason (-HCJMenu in a capture run). Never more than once per process (bAlreadyShown).
	 */
	HYPECUBEJAM_API bool ShouldPlay(const FString& CommandLine, const FString& ProjectName, bool bCanRender, bool bDedicatedServer, bool bAlreadyShown);
}
