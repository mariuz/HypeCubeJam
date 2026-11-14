#include "Game/HCJUiLogic.h"

#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Parse.h"

EHCJStreak FHCJStreakTracker::RegisterKill(float Time)
{
	MultiCount = (Time - LastKillTime <= MultiKillWindow) ? MultiCount + 1 : 1;
	LastKillTime = Time;
	++LifeStreak;

	if (LifeStreak % RampageEvery == 0)
	{
		return EHCJStreak::Rampage;
	}
	if (MultiCount >= 3)
	{
		return EHCJStreak::Triple;
	}
	return MultiCount == 2 ? EHCJStreak::Double : EHCJStreak::None;
}

void FHCJStreakTracker::RegisterDeath()
{
	LifeStreak = 0;
	MultiCount = 0;
	LastKillTime = -1000.0f;
}

float HCJUi::ComputeUIScale(float ClipX, float ClipY)
{
	if (ClipX <= 0.0f || ClipY <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(FMath::Min(ClipX / 1920.0f, ClipY / 1080.0f), 0.5f, 3.0f);
}

float HCJUi::ComputeTextScale(float UIScale)
{
	return FMath::Max(UIScale, 0.8f);
}

float HCJUi::ComputeSpreadRadius(float SpreadDegrees, float FovDegrees, float ClipX)
{
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(FovDegrees, 10.0f, 170.0f) * 0.5f);
	const float Spread = FMath::DegreesToRadians(FMath::Clamp(SpreadDegrees, 0.0f, 45.0f));
	return 0.5f * ClipX * FMath::Tan(Spread) / FMath::Tan(HalfFov);
}

FVector2D HCJUi::ComputeEdgeMarker(float RightComponent, float UpComponent, const FVector2D& Centre, const FVector2D& Radius, float& OutAngle)
{
	FVector2D Direction(RightComponent, -UpComponent);
	if (!Direction.Normalize())
	{
		Direction = FVector2D(0.0, -1.0);
	}
	OutAngle = FMath::Atan2(static_cast<float>(Direction.Y), static_cast<float>(Direction.X));
	return Centre + Direction * Radius;
}

bool HCJUi::IsMarkerClear(const FBox2D& Marker, TConstArrayView<FBox2D> Panels, float Margin)
{
	for (const FBox2D& Panel : Panels)
	{
		if (Marker.Max.X > Panel.Min.X - Margin && Marker.Min.X < Panel.Max.X + Margin
			&& Marker.Max.Y > Panel.Min.Y - Margin && Marker.Min.Y < Panel.Max.Y + Margin)
		{
			return false;
		}
	}
	return true;
}

bool HCJUi::WantsStartMenu(const FString& CommandLine, const FString& ProjectName)
{
	const TCHAR* Stream = *CommandLine;
	FString Token;
	while (FParse::Token(Stream, Token, /*bUseEscape*/ false))
	{
		if (Token.StartsWith(TEXT("-")))
		{
			const FString Switch = Token.Mid(1);
			if (Switch.Equals(TEXT("AutoHost"), ESearchCase::IgnoreCase) || Switch.Equals(TEXT("server"), ESearchCase::IgnoreCase)
				|| Switch.Equals(TEXT("nullrhi"), ESearchCase::IgnoreCase) || Switch.StartsWith(TEXT("HCJCapture="), ESearchCase::IgnoreCase)
				|| Switch.StartsWith(TEXT("HCJFrameDump="), ESearchCase::IgnoreCase) || Switch.StartsWith(TEXT("ExecCmds="), ESearchCase::IgnoreCase))
			{
				return false;
			}
			continue;
		}
		if (Token.IsEmpty() || Token.Equals(ProjectName, ESearchCase::IgnoreCase) || Token.EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		// A map URL or a server address.
		return false;
	}
	return true;
}

float HCJUi::ScoreTarget(const FHCJTargetCandidate& Candidate, bool bIsCurrent)
{
	const float Sticky = bIsCurrent ? TargetStickinessDeg : 0.0f;
	if (Candidate.bVisible && Candidate.AngleDeg <= TargetConeDeg)
	{
		return Candidate.AngleDeg + Candidate.Distance / 6000.0f - Sticky;
	}
	// Behind everything in the cone: nearest first.
	return 1000.0f + Candidate.Distance / 100.0f - Sticky * 10.0f;
}

int32 HCJUi::PickTarget(TConstArrayView<FHCJTargetCandidate> Candidates, int32 CurrentIndex)
{
	int32 Best = INDEX_NONE;
	float BestScore = 0.0f;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const float Score = ScoreTarget(Candidates[Index], Index == CurrentIndex);
		if (Best == INDEX_NONE || Score < BestScore)
		{
			Best = Index;
			BestScore = Score;
		}
	}
	return Best;
}

bool HCJUi::IsOnLead(float AimErrorDeg, float Distance, float HitRadius)
{
	if (Distance <= HitRadius)
	{
		return true;
	}
	return FMath::Abs(AimErrorDeg) <= FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(HitRadius / Distance, 0.0f, 1.0f)));
}

FVector2D HCJUi::ProjectRadar(float Forward, float Right, float Up, float Range, float& OutHeight)
{
	const float SafeRange = FMath::Max(Range, 1.0f);
	FVector2D Point(Right / SafeRange, -Forward / SafeRange);
	const double Length = Point.Size();
	if (Length > 1.0)
	{
		Point /= Length;
	}
	OutHeight = FMath::Clamp(Up / SafeRange, -1.0f, 1.0f);
	return Point;
}

namespace
{
	bool GMenuRequested = false;
	bool GPlayed = false;
	bool GFailure = false;
	FString GNotice;
}

void HCJMenuFlow::RequestMenu(const FString& Notice)
{
	GMenuRequested = true;
	if (!Notice.IsEmpty())
	{
		GNotice = Notice;
	}
}

bool HCJMenuFlow::ShouldOpenMenu()
{
	if (IsRunningDedicatedServer() || IsRunningCommandlet() || !FApp::CanEverRender())
	{
		return false;
	}
	if (GMenuRequested)
	{
		return true;
	}
	if (!GPlayed && FParse::Param(FCommandLine::Get(), TEXT("HCJMenu")))
	{
		// Development: the title menu although the command line is a capture run.
		return true;
	}
	return !GPlayed && HCJUi::WantsStartMenu(FCommandLine::Get(), FApp::GetProjectName());
}

void HCJMenuFlow::MarkPlayed()
{
	GPlayed = true;
	GMenuRequested = false;
}

FString HCJMenuFlow::TakeNotice()
{
	FString Result = MoveTemp(GNotice);
	GNotice.Reset();
	return Result;
}

bool HCJMenuFlow::TakeFailure()
{
	const bool bResult = GFailure;
	GFailure = false;
	return bResult;
}

void HCJMenuFlow::NotifyFailure(const FString& Notice)
{
	GFailure = true;
	RequestMenu(Notice);
}
