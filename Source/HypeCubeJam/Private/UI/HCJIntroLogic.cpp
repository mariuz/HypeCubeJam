#include "UI/HCJIntroLogic.h"

#include "Game/HCJUiLogic.h"
#include "Misc/Parse.h"

EHCJIntroEnd FHCJIntroFlow::Tick(float DeltaSeconds, bool bPlaying, bool bError, float Position, float Duration)
{
	if (End != EHCJIntroEnd::None)
	{
		return End;
	}
	Elapsed += FMath::Max(DeltaSeconds, 0.0f);
	LastPosition = Position;
	LastDuration = Duration;

	if (!bStarted && bPlaying && Position > 0.02f)
	{
		bStarted = true;
		StartDelay = Elapsed;
	}

	if (bSkipRequested)
	{
		End = EHCJIntroEnd::Skipped;
	}
	else if (bError)
	{
		End = EHCJIntroEnd::Failed;
	}
	else if (!bStarted && Elapsed >= OpenTimeout)
	{
		End = EHCJIntroEnd::OpenTimeout;
	}
	else if (Elapsed >= MaxSeconds)
	{
		End = EHCJIntroEnd::TooLong;
	}
	else if (bStarted)
	{
		// The end: the position reached the duration, or the player stopped by itself (it rewinds to 0 when it does).
		StoppedSeconds = bPlaying ? 0.0f : StoppedSeconds + DeltaSeconds;
		if ((Duration > 0.0f && Position >= Duration - 0.05f) || StoppedSeconds > 0.25f)
		{
			End = EHCJIntroEnd::Finished;
		}
	}
	return End;
}

void FHCJIntroFlow::RequestSkip()
{
	if (End == EHCJIntroEnd::None && Elapsed >= SkipGrace)
	{
		bSkipRequested = true;
	}
}

float FHCJIntroFlow::GetTitleAlpha() const
{
	if (!bStarted || LastDuration <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp((LastPosition - (LastDuration - TitleLead)) / FMath::Max(TitleFade, 0.01f), 0.0f, 1.0f);
}

float FHCJIntroFlow::GetBlackAlpha() const
{
	if (!bStarted)
	{
		return 1.0f;
	}
	if (LastDuration <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp((LastPosition - (LastDuration - FadeOut)) / FMath::Max(FadeOut, 0.01f), 0.0f, 1.0f);
}

const TCHAR* FHCJIntroFlow::DescribeEnd(EHCJIntroEnd InEnd)
{
	switch (InEnd)
	{
	case EHCJIntroEnd::Finished: return TEXT("played to its end");
	case EHCJIntroEnd::Skipped: return TEXT("skipped by a key or button");
	case EHCJIntroEnd::OpenTimeout: return TEXT("the movie showed no frame in time");
	case EHCJIntroEnd::Failed: return TEXT("the media player reported an error");
	case EHCJIntroEnd::TooLong: return TEXT("it ran past its time limit");
	default: return TEXT("still running");
	}
}

bool HCJIntro::ShouldPlay(const FString& CommandLine, const FString& ProjectName, bool bCanRender, bool bDedicatedServer, bool bAlreadyShown)
{
	if (bAlreadyShown || !bCanRender || bDedicatedServer || FParse::Param(*CommandLine, TEXT("HCJNoIntro")))
	{
		return false;
	}
	return FParse::Param(*CommandLine, TEXT("HCJIntro")) || HCJUi::WantsStartMenu(CommandLine, ProjectName);
}
