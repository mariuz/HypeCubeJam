#include "Audio/HCJVoiceLogic.h"

namespace
{
	/** Times are floats that were added up by the caller: "exactly the gap later" must count as late enough. */
	constexpr float TimeTolerance = 0.001f;
}

const FHCJVoiceRule& FHCJVoiceLimiter::GetRule(EHCJVoiceLine Line)
{
	// Priority, cooldown (s), group, asset. In the order of EHCJVoiceLine.
	static const FHCJVoiceRule Rules[static_cast<int32>(EHCJVoiceLine::Count)] =
	{
		{ 0, 0.0f, 0, TEXT("") },                                   // None
		{ 20, 1.0e9f, 0, TEXT("VO_Computer_Welcome") },             // once
		{ 70, 30.0f, 1, TEXT("VO_Computer_ShieldFore") },
		{ 70, 30.0f, 1, TEXT("VO_Computer_ShieldAft") },
		{ 70, 30.0f, 1, TEXT("VO_Computer_ShieldPort") },
		{ 70, 30.0f, 1, TEXT("VO_Computer_ShieldStarboard") },
		{ 90, 60.0f, 0, TEXT("VO_Computer_HullCritical") },
		{ 100, 30.0f, 0, TEXT("VO_Computer_TorpedoIncoming") },
		{ 95, 30.0f, 0, TEXT("VO_Computer_MissileLock") },
		{ 60, 60.0f, 0, TEXT("VO_Computer_EnemyBehind") },
		{ 92, 30.0f, 0, TEXT("VO_Computer_GravityWell") },
		{ 80, 30.0f, 0, TEXT("VO_Computer_TractorCaught") },
		{ 30, 90.0f, 0, TEXT("VO_Computer_SectorClear") },
	};
	const int32 Index = static_cast<int32>(Line);
	return Rules[(Index >= 0 && Index < static_cast<int32>(EHCJVoiceLine::Count)) ? Index : 0];
}

EHCJVoiceLine FHCJVoiceLimiter::ShieldLine(int32 Facing)
{
	return (Facing >= 0 && Facing < 4) ? static_cast<EHCJVoiceLine>(static_cast<int32>(EHCJVoiceLine::ShieldFore) + Facing) : EHCJVoiceLine::None;
}

const TCHAR* FHCJVoiceLimiter::DescribeRefusal(EHCJVoiceRefusal Refusal)
{
	switch (Refusal)
	{
	case EHCJVoiceRefusal::Disabled: return TEXT("the voice is off");
	case EHCJVoiceRefusal::SpawnGrace: return TEXT("first seconds of a ship");
	case EHCJVoiceRefusal::Cooldown: return TEXT("cooldown of the line");
	case EHCJVoiceRefusal::Busy: return TEXT("another line is being spoken");
	case EHCJVoiceRefusal::Gap: return TEXT("silence after a line");
	case EHCJVoiceRefusal::MinuteCap: return TEXT("3 lines in the last minute");
	case EHCJVoiceRefusal::HardMinuteCap: return TEXT("5 lines in the last minute");
	default: return TEXT("");
	}
}

void FHCJVoiceLimiter::Reset()
{
	LastRefusal = EHCJVoiceRefusal::None;
	Current = EHCJVoiceLine::None;
	CurrentEnd = -1000.0f;
	SpawnTime = -1000.0f;
	for (float& Time : LastStart)
	{
		Time = -1.0e10f;
	}
	for (int32& Count : NumPlayed)
	{
		Count = 0;
	}
	for (float& Time : Recent)
	{
		Time = -1.0e9f;
	}
	RecentIndex = 0;
	NumPlayedTotal = 0;
	NumDropped = 0;
}

void FHCJVoiceLimiter::NotifySpawn(float Now)
{
	SpawnTime = Now;
	// The ship that was being talked to is gone; the gap is counted from here.
	Current = EHCJVoiceLine::None;
	CurrentEnd = FMath::Min(CurrentEnd, Now);
}

EHCJVoiceDecision FHCJVoiceLimiter::Request(EHCJVoiceLine Line, float Now, float DurationSeconds, bool bEnabled)
{
	const int32 Index = static_cast<int32>(Line);
	LastRefusal = EHCJVoiceRefusal::None;
	if (Line == EHCJVoiceLine::None || Index >= static_cast<int32>(EHCJVoiceLine::Count))
	{
		return EHCJVoiceDecision::Drop;
	}
	const FHCJVoiceRule& Rule = GetRule(Line);
	const bool bUrgent = Rule.Priority >= UrgentPriority;

	bool bAllowed = bEnabled && Now - SpawnTime + TimeTolerance >= SpawnGraceSeconds;
	if (!bAllowed)
	{
		LastRefusal = bEnabled ? EHCJVoiceRefusal::SpawnGrace : EHCJVoiceRefusal::Disabled;
	}

	// The line itself, or another line of its group, was spoken a moment ago.
	for (int32 Other = 1; bAllowed && Other < static_cast<int32>(EHCJVoiceLine::Count); ++Other)
	{
		const bool bSame = Other == Index || (Rule.Group != 0 && GetRule(static_cast<EHCJVoiceLine>(Other)).Group == Rule.Group);
		if (bSame && Now - LastStart[Other] + TimeTolerance < Rule.Cooldown)
		{
			bAllowed = false;
			LastRefusal = EHCJVoiceRefusal::Cooldown;
		}
	}

	bool bInterrupt = false;
	if (bAllowed)
	{
		if (Now < CurrentEnd)
		{
			// One voice at a time.
			bInterrupt = bUrgent && Rule.Priority > GetRule(Current).Priority;
			bAllowed = bInterrupt;
			LastRefusal = bAllowed ? EHCJVoiceRefusal::None : EHCJVoiceRefusal::Busy;
		}
		else if (Now - CurrentEnd + TimeTolerance < (bUrgent ? UrgentGapSeconds : GapSeconds))
		{
			bAllowed = false;
			LastRefusal = EHCJVoiceRefusal::Gap;
		}
	}

	// Recent holds the last HardMaxPerMinute starts: how many of them are inside the last minute?
	if (bAllowed)
	{
		int32 InLastMinute = 0;
		for (const float Start : Recent)
		{
			InLastMinute += Now - Start < 60.0f ? 1 : 0;
		}
		if (InLastMinute >= (bUrgent ? HardMaxPerMinute : MaxPerMinute))
		{
			bAllowed = false;
			LastRefusal = bUrgent ? EHCJVoiceRefusal::HardMinuteCap : EHCJVoiceRefusal::MinuteCap;
		}
	}

	if (!bAllowed)
	{
		++NumDropped;
		return EHCJVoiceDecision::Drop;
	}

	Current = Line;
	CurrentEnd = Now + FMath::Max(DurationSeconds, 0.1f);
	LastStart[Index] = Now;
	Recent[RecentIndex] = Now;
	RecentIndex = (RecentIndex + 1) % HardMaxPerMinute;
	++NumPlayed[Index];
	++NumPlayedTotal;
	return bInterrupt ? EHCJVoiceDecision::Interrupt : EHCJVoiceDecision::Play;
}
