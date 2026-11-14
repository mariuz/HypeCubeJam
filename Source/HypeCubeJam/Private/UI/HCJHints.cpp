#include "UI/HCJHints.h"

namespace
{
	constexpr int32 NumHints = static_cast<int32>(EHCJHint::Count);

	/** Duration, max shows, cooldown, priority, leaves when no longer true. Indexed by EHCJHint. */
	const FHCJHintRule HintRules[NumHints] = {
		/* None        */ { 0.0f, 0, 0.0f, 0, false },
		/* Move        */ { 12.0f, 1, 0.0f, 0, false },
		/* Fire        */ { 12.0f, 1, 0.0f, 0, false },
		/* Boost       */ { 12.0f, 1, 0.0f, 0, false },
		/* Target      */ { 14.0f, 1, 0.0f, 0, false },
		/* BlackHole   */ { 6.0f, 2, 30.0f, 100, true },
		/* LowHull     */ { 7.0f, 3, 45.0f, 95, true },
		/* NewItem     */ { 6.0f, 9, 2.0f, 60, false },
		/* Turrets     */ { 7.0f, 2, 90.0f, 55, true },
		/* BoostEmpty  */ { 5.0f, 2, 60.0f, 50, false },
		/* Wormhole    */ { 6.0f, 2, 60.0f, 36, true },
		/* Portal      */ { 6.0f, 2, 60.0f, 35, true },
		/* Pickup      */ { 6.0f, 2, 60.0f, 30, true },
		/* EmptySector */ { 7.0f, 3, 45.0f, 20, true },
		/* Goal        */ { 7.0f, 1, 0.0f, 10, false },
		/* ShieldDown  */ { 9.0f, 1, 0.0f, 70, false },
		/* TorpedoChance */ { 8.0f, 1, 0.0f, 65, false },
		/* PhaserEmpty */ { 7.0f, 2, 90.0f, 52, false },
		/* RockBreak   */ { 7.0f, 1, 0.0f, 28, false },
	};

	const EHCJHint OnboardingSteps[] = { EHCJHint::Move, EHCJHint::Fire, EHCJHint::Boost, EHCJHint::Target };
}

const FHCJHintRule& FHCJHintDirector::GetRule(EHCJHint Hint)
{
	const int32 Index = static_cast<int32>(Hint);
	return HintRules[(Index >= 0 && Index < NumHints) ? Index : 0];
}

void FHCJHintDirector::Reset()
{
	*this = FHCJHintDirector();
}

bool FHCJHintDirector::IsOnboardingFinished() const
{
	for (const EHCJHint Step : OnboardingSteps)
	{
		if ((Finished & Bit(Step)) == 0)
		{
			return false;
		}
	}
	return true;
}

float FHCJHintDirector::GetCurrentRemainingFraction(float Now) const
{
	if (Current == EHCJHint::None)
	{
		return 0.0f;
	}
	const float Duration = FMath::Max(GetRule(Current).Duration, 0.01f);
	return FMath::Clamp(1.0f - (Now - CurrentStart) / Duration, 0.0f, 1.0f);
}

void FHCJHintDirector::Begin(EHCJHint Hint, float Now)
{
	Current = Hint;
	CurrentStart = Now;
	CurrentDoneTime = -1.0f;
	++Shows[static_cast<int32>(Hint)];
}

void FHCJHintDirector::End(float Now, bool bFinished)
{
	if (Current == EHCJHint::None)
	{
		return;
	}
	if (IsOnboarding(Current) && (bFinished || Shows[static_cast<int32>(Current)] >= MaxOnboardingShows))
	{
		Finished |= Bit(Current);
	}
	LastShownEnd[static_cast<int32>(Current)] = Now;
	LastEndTime = Now;
	Current = EHCJHint::None;
	CurrentDoneTime = -1.0f;
}

bool FHCJHintDirector::IsDue(EHCJHint Hint, float Now, uint32 ActiveMask) const
{
	const int32 Index = static_cast<int32>(Hint);
	const FHCJHintRule& Rule = HintRules[Index];
	return (ActiveMask & Bit(Hint)) != 0 && Shows[Index] < Rule.MaxShows && (Shows[Index] == 0 || Now - LastShownEnd[Index] >= Rule.Cooldown);
}

EHCJHint FHCJHintDirector::PickContextual(float Now, uint32 ActiveMask, int32 MinPriority) const
{
	EHCJHint Best = EHCJHint::None;
	int32 BestPriority = MinPriority - 1;
	for (int32 Index = static_cast<int32>(EHCJHint::Target) + 1; Index < NumHints; ++Index)
	{
		const EHCJHint Hint = static_cast<EHCJHint>(Index);
		if (HintRules[Index].Priority > BestPriority && IsDue(Hint, Now, ActiveMask))
		{
			Best = Hint;
			BestPriority = HintRules[Index].Priority;
		}
	}
	return Best;
}

EHCJHint FHCJHintDirector::Update(float Now, const FHCJHintInput& Input)
{
	if (!bInitialised)
	{
		bInitialised = true;
		for (float& Time : LastShownEnd)
		{
			Time = -1000000.0f;
		}
	}

	// What the player has done counts whenever it happens, also while no hint is up or hints are off.
	for (const EHCJHint Step : OnboardingSteps)
	{
		Done |= Input.DoneMask & Bit(Step);
	}

	if (!Input.bHasShip || !Input.bEnabled)
	{
		// Interrupted, not finished: an onboarding step comes back with the next ship.
		End(Now, false);
		return EHCJHint::None;
	}

	if (Current != EHCJHint::None)
	{
		const FHCJHintRule& Rule = GetRule(Current);
		const float Age = Now - CurrentStart;
		if (IsOnboarding(Current))
		{
			if (CurrentDoneTime < 0.0f && (Done & Bit(Current)) != 0)
			{
				CurrentDoneTime = Now;
			}
			if (Age >= Rule.Duration || (CurrentDoneTime >= 0.0f && Now - CurrentDoneTime >= DoneLingerSeconds && Age >= MinShowSeconds))
			{
				End(Now, true);
			}
		}
		else if (Age >= Rule.Duration || (Rule.bWhileTrue && (Input.ActiveMask & Bit(Current)) == 0 && Age >= MinShowSeconds))
		{
			End(Now, true);
		}
	}

	// Something urgent goes first and may interrupt whatever is up.
	const EHCJHint Urgent = PickContextual(Now, Input.ActiveMask, UrgentPriority);
	if (Urgent != EHCJHint::None && Urgent != Current
		&& (Current == EHCJHint::None || IsOnboarding(Current) || GetRule(Urgent).Priority > GetRule(Current).Priority))
	{
		End(Now, false);
		Begin(Urgent, Now);
		return Current;
	}

	// Something that just happened replaces a lesser contextual hint (it would be stale by the time that one is over).
	if (Current != EHCJHint::None && !IsOnboarding(Current))
	{
		const EHCJHint Event = PickContextual(Now, Input.ActiveMask, InterruptPriority);
		if (Event != EHCJHint::None && Event != Current && GetRule(Event).Priority > GetRule(Current).Priority)
		{
			End(Now, false);
			Begin(Event, Now);
			return Current;
		}
	}

	if (Current == EHCJHint::None && Now - LastEndTime >= GapSeconds)
	{
		for (const EHCJHint Step : OnboardingSteps)
		{
			if ((Finished & Bit(Step)) != 0)
			{
				continue;
			}
			if ((Done & Bit(Step)) != 0)
			{
				// Already did it without being told.
				Finished |= Bit(Step);
				continue;
			}
			Begin(Step, Now);
			return Current;
		}
		const EHCJHint Next = PickContextual(Now, Input.ActiveMask, MIN_int32 + 1);
		if (Next != EHCJHint::None)
		{
			Begin(Next, Now);
		}
	}
	return Current;
}

void HCJHints::SplitKeyMarkup(const FString& Markup, TArray<FHCJHintRun>& OutRuns)
{
	OutRuns.Reset();
	FString Plain;
	const auto FlushPlain = [&OutRuns, &Plain]()
	{
		if (!Plain.IsEmpty())
		{
			OutRuns.Add({ Plain, false });
			Plain.Reset();
		}
	};

	int32 Index = 0;
	while (Index < Markup.Len())
	{
		if (Markup[Index] == TEXT('['))
		{
			const int32 Close = Markup.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
			if (Close != INDEX_NONE && Close > Index + 1)
			{
				FlushPlain();
				OutRuns.Add({ Markup.Mid(Index + 1, Close - Index - 1), true });
				Index = Close + 1;
				continue;
			}
		}
		Plain.AppendChar(Markup[Index]);
		++Index;
	}
	FlushPlain();
}

EHCJDeathCause HCJHints::ClassifyDeath(const FString& KillerName, const FString& Cause)
{
	if (Cause == TEXT("TURRET") || (KillerName == TEXT("Station") && Cause.IsEmpty()))
	{
		return EHCJDeathCause::Turret;
	}
	if (!KillerName.IsEmpty())
	{
		return EHCJDeathCause::Player;
	}
	return Cause == TEXT("BLACK HOLE") ? EHCJDeathCause::BlackHole : EHCJDeathCause::Void;
}

const TCHAR* HCJHints::PickDeathTip(EHCJDeathCause Cause, int32 Index)
{
	static const TCHAR* PlayerTips[] = {
		TEXT("Keep strafing with [A] [D] while you shoot: a ship that flies straight is easy to lead."),
		TEXT("Aim at the yellow lead circle, not at the ship. It turns green when the shot will land."),
		TEXT("Low on hull? Break off with [Shift] and fly over a green REPAIR pad."),
		TEXT("A shield facing returns a few seconds after its last hit: turn a fresh side to them or break the line of fire."),
		TEXT("The target panel shows the four shield facings of your target. Hits on a red side go straight to the hull."),
		TEXT("Glowing pads hold weapons and power-ups. QUAD DAMAGE and the RAIL SHOT end fights fast."),
		TEXT("INCOMING TORPEDO: boost across its path, it turns slowly. Or take it on a full shield facing."),
		TEXT("Save torpedoes ([Right mouse]) for a shield facing that is down: through an open side one nearly kills."),
		TEXT("The phaser hits at once but weakens with range: close in, and keep the beam on one shield facing until it fails."),
		TEXT("A rock between you and a beam stops it. Big rocks break into pieces under phasers and torpedoes: cover does not last."),
	};
	static const TCHAR* TurretTips[] = {
		TEXT("Station turrets have a long reach. Keep your distance, or use rocks as cover on the way in."),
		TEXT("A destroyed turret is a point in the TURRETS column and stays down for a minute."),
	};
	static const TCHAR* HoleTips[] = {
		TEXT("In a gravity well turn away and hold [Shift]. Past the point of no return even boost is too slow."),
		TEXT("Black holes are the black dots on the map and the ringed square on the radar. Give them room."),
	};
	static const TCHAR* VoidTips[] = {
		TEXT("Stay inside the asteroid field: far outside it the hull fails. Portals are the way out of a sector."),
	};

	const int32 Safe = FMath::Max(Index, 0);
	switch (Cause)
	{
	case EHCJDeathCause::Turret: return TurretTips[Safe % UE_ARRAY_COUNT(TurretTips)];
	case EHCJDeathCause::BlackHole: return HoleTips[Safe % UE_ARRAY_COUNT(HoleTips)];
	case EHCJDeathCause::Void: return VoidTips[Safe % UE_ARRAY_COUNT(VoidTips)];
	default: return PlayerTips[Safe % UE_ARRAY_COUNT(PlayerTips)];
	}
}

FString HCJHints::MakeObjectiveText(int32 MyKills, int32 BestOtherKills, const FString& BestOtherName, int32 FragLimit)
{
	if (BestOtherKills < 0)
	{
		return FragLimit > 0 ? FString::Printf(TEXT("FIRST TO %d KILLS WINS"), FragLimit) : FString(TEXT("FREE-FOR-ALL"));
	}
	if (MyKills == 0 && BestOtherKills == 0)
	{
		return FragLimit > 0 ? FString::Printf(TEXT("FIRST TO %d KILLS WINS"), FragLimit) : FString(TEXT("MOST KILLS WINS"));
	}
	if (MyKills > BestOtherKills)
	{
		return FragLimit > 0 ? FString::Printf(TEXT("YOU LEAD by %d  -  %d to go"), MyKills - BestOtherKills, FMath::Max(FragLimit - MyKills, 0))
			: FString::Printf(TEXT("YOU LEAD by %d"), MyKills - BestOtherKills);
	}
	if (MyKills == BestOtherKills)
	{
		return FString::Printf(TEXT("TIED for the lead with %s"), *BestOtherName);
	}
	return FragLimit > 0 ? FString::Printf(TEXT("%s leads by %d  -  needs %d more"), *BestOtherName, BestOtherKills - MyKills, FMath::Max(FragLimit - BestOtherKills, 0))
		: FString::Printf(TEXT("%s leads by %d"), *BestOtherName, BestOtherKills - MyKills);
}

const TCHAR* HCJHints::GetItemHint(int32 Item)
{
	// Order of EHCJPickupItem.
	static const TCHAR* ItemHints[] = {
		TEXT("REPAIR: hull restored. Repair pads are the green diamonds on the radar."),
		TEXT("SHIELD CELL: recharges your weakest shield facings first, above their normal maximum."),
		TEXT("OVERDRIVE: faster flight and harder acceleration while the timer runs."),
		TEXT("QUAD DAMAGE: every shot hits four times as hard while the timer runs."),
		TEXT("CLOAK: enemies cannot see or lock you. Firing ends it."),
		TEXT("SCATTER CANNON: a cone of pellets. Deadly up close, weak at range.   [G] back to the phaser"),
		TEXT("RAIL SHOT: one very fast, heavy bolt. Hardly any lead needed.   [G] back to the phaser"),
		TEXT("HOMING MISSILES steer for your locked target: [T] next, [F] nearest.   [G] back to the phaser"),
		TEXT("RAPID PULSE: a stream of light bolts. Hold the trigger down.   [G] back to the phaser"),
		TEXT("TORPEDOES restocked. [Right mouse] fires one at your target: deadliest through a shield facing that is down."),
	};
	return (Item >= 0 && Item < static_cast<int32>(UE_ARRAY_COUNT(ItemHints))) ? ItemHints[Item] : TEXT("");
}
