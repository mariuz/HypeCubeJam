#include "Audio/HCJMusicLogic.h"

EHCJMusicTrack FHCJMusicSelector::Step(bool bMenu, float Intensity, float DeltaSeconds, float CombatOn, float CombatOff, float MinHoldSeconds)
{
	EHCJMusicTrack Wanted = Current;
	if (bMenu)
	{
		Wanted = EHCJMusicTrack::Menu;
	}
	else if (Current == EHCJMusicTrack::Combat)
	{
		// Out of the fight: only after the track has had its time, and only when things have really calmed down.
		if (Intensity < CombatOff && HeldSeconds >= MinHoldSeconds)
		{
			Wanted = EHCJMusicTrack::Explore;
		}
	}
	else
	{
		Wanted = Intensity >= CombatOn ? EHCJMusicTrack::Combat : EHCJMusicTrack::Explore;
	}

	if (Wanted != Current)
	{
		Current = Wanted;
		HeldSeconds = 0.0f;
	}
	else
	{
		HeldSeconds += FMath::Max(DeltaSeconds, 0.0f);
	}
	return Current;
}

float HCJMusic::GetLoopStart(float Duration, float LoopSeconds, float CrossfadeSeconds)
{
	const float Crossfade = FMath::Max(CrossfadeSeconds, 0.0f);
	const float End = FMath::Max(Duration - Crossfade, 0.0f);
	const float Start = (LoopSeconds > 0.0f && LoopSeconds < Duration) ? LoopSeconds : End;
	return FMath::Max(Start, 0.5f * Crossfade);
}

float HCJMusic::StepWeight(float Weight, float Target, float DeltaSeconds, float FadeSeconds)
{
	if (FadeSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		return Target;
	}
	return FMath::FInterpConstantTo(Weight, Target, FMath::Max(DeltaSeconds, 0.0f), 1.0f / FadeSeconds);
}

float HCJMusic::GainToDecibels(float Gain)
{
	return Gain > 0.000001f ? 20.0f * FMath::LogX(10.0f, Gain) : -120.0f;
}
