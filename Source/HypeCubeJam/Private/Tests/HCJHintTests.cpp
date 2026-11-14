#include "Tests/HCJTestHelpers.h"

#include "UI/HCJHints.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJHintTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	uint32 Bit(EHCJHint Hint)
	{
		return FHCJHintDirector::Bit(Hint);
	}

	FHCJHintInput MakeInput(uint32 Active = 0, uint32 Done = 0, bool bHasShip = true, bool bEnabled = true)
	{
		FHCJHintInput Input;
		Input.bHasShip = bHasShip;
		Input.bEnabled = bEnabled;
		Input.ActiveMask = Active;
		Input.DoneMask = Done;
		return Input;
	}

	/** Runs the director from Time for Seconds in steps of 0.1 s with a constant input; returns the time reached. */
	float Run(FHCJHintDirector& Director, float Time, float Seconds, const FHCJHintInput& Input)
	{
		const int32 Steps = FMath::RoundToInt32(Seconds / 0.1f);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Time += 0.1f;
			Director.Update(Time, Input);
		}
		return Time;
	}
}

/** Onboarding: the four key prompts in order, each leaves when its action is done or after its timeout, done ones are skipped. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJHintOnboardingTest, "HypeCubeJam.UI.HintOnboarding", HCJHintTestFlags)

bool FHCJHintOnboardingTest::RunTest(const FString& Parameters)
{
	FHCJHintDirector Director;
	TestTrue(TEXT("nothing without a ship"), Director.Update(0.0f, MakeInput(0, 0, false)) == EHCJHint::None);
	TestTrue(TEXT("first prompt: move"), Director.Update(0.1f, MakeInput()) == EHCJHint::Move);
	TestEqual(TEXT("counted once"), Director.GetShowCount(EHCJHint::Move), 1);

	// Doing it does not remove the prompt at once (it is acknowledged first), but soon.
	float Time = 0.2f;
	TestTrue(TEXT("still up in the frame the action happens"), Director.Update(Time, MakeInput(0, Bit(EHCJHint::Move))) == EHCJHint::Move);
	TestTrue(TEXT("marked as done"), Director.IsCurrentDone());
	Time = Run(Director, Time, FHCJHintDirector::MinShowSeconds + FHCJHintDirector::DoneLingerSeconds, MakeInput());
	TestTrue(TEXT("gone after the action"), Director.GetCurrent() != EHCJHint::Move);

	// Next in order after the gap.
	Time = Run(Director, Time, FHCJHintDirector::GapSeconds + 0.2f, MakeInput());
	TestTrue(TEXT("second prompt: fire"), Director.GetCurrent() == EHCJHint::Fire);

	// Never fired: the prompt times out and the next one follows.
	Time = Run(Director, Time, FHCJHintDirector::GetRule(EHCJHint::Fire).Duration + FHCJHintDirector::GapSeconds + 0.3f, MakeInput());
	TestTrue(TEXT("third prompt after the timeout: boost"), Director.GetCurrent() == EHCJHint::Boost);
	TestEqual(TEXT("the fire prompt is not repeated"), Director.GetShowCount(EHCJHint::Fire), 1);

	// The target keys were used while the boost prompt was up: that step is skipped later.
	Time = Run(Director, Time, 0.5f, MakeInput(0, Bit(EHCJHint::Target)));
	Time = Run(Director, Time, 3.0f, MakeInput(0, Bit(EHCJHint::Boost)));
	Time = Run(Director, Time, 5.0f, MakeInput());
	TestTrue(TEXT("no prompt left"), Director.GetCurrent() == EHCJHint::None);
	TestEqual(TEXT("target prompt never shown"), Director.GetShowCount(EHCJHint::Target), 0);
	TestTrue(TEXT("onboarding finished"), Director.IsOnboardingFinished());

	// Losing the ship interrupts a prompt; it comes back with the next ship.
	FHCJHintDirector Second;
	Second.Update(0.1f, MakeInput());
	TestTrue(TEXT("interrupted by death"), Second.Update(1.0f, MakeInput(0, 0, false)) == EHCJHint::None);
	Run(Second, 1.0f, FHCJHintDirector::GapSeconds + 0.2f, MakeInput());
	TestTrue(TEXT("move prompt again"), Second.GetCurrent() == EHCJHint::Move);
	// ... but not a third time: a player who keeps dying is not shown the same prompt for ever.
	Second.Update(4.0f, MakeInput(0, 0, false));
	Run(Second, 4.0f, FHCJHintDirector::GapSeconds + 0.2f, MakeInput());
	TestTrue(TEXT("after two showings the next step follows"), Second.GetCurrent() == EHCJHint::Fire);
	TestEqual(TEXT("move prompt shown twice"), Second.GetShowCount(EHCJHint::Move), FHCJHintDirector::MaxOnboardingShows);

	// Hints off: nothing is shown, but what the player does is remembered.
	FHCJHintDirector Off;
	Time = Run(Off, 0.0f, 3.0f, MakeInput(0, Bit(EHCJHint::Move) | Bit(EHCJHint::Fire), true, false));
	TestTrue(TEXT("nothing while off"), Off.GetCurrent() == EHCJHint::None);
	TestEqual(TEXT("nothing counted while off"), Off.GetShowCount(EHCJHint::Move), 0);
	Run(Off, Time, FHCJHintDirector::GapSeconds + 0.2f, MakeInput());
	TestTrue(TEXT("switched on: starts at the first step not done yet"), Off.GetCurrent() == EHCJHint::Boost);
	return true;
}

/** Contextual hints: only after the onboarding, by priority, limited in number and by their cooldown; urgent ones interrupt. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJHintContextTest, "HypeCubeJam.UI.HintRules", HCJHintTestFlags)

bool FHCJHintContextTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const uint32 AllDone = Bit(EHCJHint::Move) | Bit(EHCJHint::Fire) | Bit(EHCJHint::Boost) | Bit(EHCJHint::Target);

	// Rules are sane: everything shown at least once and for a readable time; only the two dangers are urgent.
	for (int32 Index = 1; Index < static_cast<int32>(EHCJHint::Count); ++Index)
	{
		const EHCJHint Hint = static_cast<EHCJHint>(Index);
		const FHCJHintRule& Rule = FHCJHintDirector::GetRule(Hint);
		HCJ_TEST_CHECK(Rule.MaxShows >= 1 && Rule.Duration >= 4.0f, TEXT("hint %d: %d shows of %.1f s"), Index, Rule.MaxShows, Rule.Duration);
		HCJ_TEST_CHECK((Rule.Priority >= FHCJHintDirector::UrgentPriority) == (Hint == EHCJHint::BlackHole || Hint == EHCJHint::LowHull), TEXT("hint %d: priority %d"), Index, Rule.Priority);
	}

	// A non-urgent hint waits for the onboarding; an urgent one interrupts it and the step comes back afterwards.
	{
		FHCJHintDirector Director;
		Director.Update(0.1f, MakeInput(Bit(EHCJHint::Portal)));
		TestTrue(TEXT("onboarding before a portal hint"), Director.GetCurrent() == EHCJHint::Move);
		TestTrue(TEXT("black hole interrupts"), Director.Update(0.2f, MakeInput(Bit(EHCJHint::Portal) | Bit(EHCJHint::BlackHole))) == EHCJHint::BlackHole);
		float Time = Run(Director, 0.2f, 3.0f, MakeInput(Bit(EHCJHint::BlackHole)));
		TestTrue(TEXT("stays while in the well"), Director.GetCurrent() == EHCJHint::BlackHole);
		Time = Run(Director, Time, FHCJHintDirector::GapSeconds + 0.3f, MakeInput());
		TestTrue(TEXT("out of the well: the interrupted step is back"), Director.GetCurrent() == EHCJHint::Move);
		TestEqual(TEXT("which counts as a second showing"), Director.GetShowCount(EHCJHint::Move), 2);
	}

	// Something that just happened (a new item) replaces a lesser contextual hint, but never an onboarding step.
	{
		FHCJHintDirector Director;
		float Time = Run(Director, 0.0f, 0.2f, MakeInput(0, AllDone, false));
		Time = Run(Director, Time, FHCJHintDirector::GapSeconds + 0.2f, MakeInput(Bit(EHCJHint::Goal)));
		TestTrue(TEXT("the goal hint"), Director.GetCurrent() == EHCJHint::Goal);
		TestTrue(TEXT("turrets do not replace it"), Director.Update(Time + 0.1f, MakeInput(Bit(EHCJHint::Goal) | Bit(EHCJHint::Turrets))) == EHCJHint::Goal);
		TestTrue(TEXT("a new item does"), Director.Update(Time + 0.2f, MakeInput(Bit(EHCJHint::Goal) | Bit(EHCJHint::NewItem))) == EHCJHint::NewItem);

		FHCJHintDirector Fresh;
		Fresh.Update(0.1f, MakeInput());
		TestTrue(TEXT("a new item waits for the onboarding"), Fresh.Update(0.2f, MakeInput(Bit(EHCJHint::NewItem))) == EHCJHint::Move);
	}

	// Priority, show limit and cooldown.
	{
		FHCJHintDirector Director;
		float Time = Run(Director, 0.0f, 0.2f, MakeInput(0, AllDone, false));
		const uint32 Both = Bit(EHCJHint::Portal) | Bit(EHCJHint::Pickup);
		Time = Run(Director, Time, FHCJHintDirector::GapSeconds + 0.2f, MakeInput(Both));
		TestTrue(TEXT("the portal outranks the pad"), Director.GetCurrent() == EHCJHint::Portal);
		Time = Run(Director, Time, FHCJHintDirector::GetRule(EHCJHint::Portal).Duration + FHCJHintDirector::GapSeconds + 0.3f, MakeInput(Both));
		TestTrue(TEXT("then the pad (the portal hint is cooling down)"), Director.GetCurrent() == EHCJHint::Pickup);

		// A long stay next to both: each is shown at most MaxShows times, never more often than its cooldown allows.
		int32 LastPortalShows = Director.GetShowCount(EHCJHint::Portal);
		float LastPortalStart = 0.0f;
		bool bCooldownKept = true;
		for (int32 Step = 0; Step < 6000; ++Step)
		{
			Time += 0.1f;
			Director.Update(Time, MakeInput(Both));
			if (Director.GetShowCount(EHCJHint::Portal) != LastPortalShows)
			{
				bCooldownKept &= LastPortalStart == 0.0f || Time - LastPortalStart >= FHCJHintDirector::GetRule(EHCJHint::Portal).Cooldown;
				LastPortalShows = Director.GetShowCount(EHCJHint::Portal);
				LastPortalStart = Time;
			}
		}
		TestEqual(TEXT("portal hint limited"), Director.GetShowCount(EHCJHint::Portal), FHCJHintDirector::GetRule(EHCJHint::Portal).MaxShows);
		TestEqual(TEXT("pad hint limited"), Director.GetShowCount(EHCJHint::Pickup), FHCJHintDirector::GetRule(EHCJHint::Pickup).MaxShows);
		TestTrue(TEXT("cooldown between two showings"), bCooldownKept);
		TestTrue(TEXT("nothing left to say"), Director.GetCurrent() == EHCJHint::None);

		// A "while" hint leaves early when its reason is gone, but not before it could be read.
		Director.Reset();
		Time = Run(Director, 0.0f, 0.2f, MakeInput(0, AllDone, false));
		Time = Run(Director, Time, FHCJHintDirector::GapSeconds + 0.2f, MakeInput(Bit(EHCJHint::Wormhole)));
		TestTrue(TEXT("wormhole hint"), Director.GetCurrent() == EHCJHint::Wormhole);
		// How long it had been up when it left.
		float AgeAtEnd = 0.0f;
		for (int32 Step = 0; Step < 200 && Director.GetCurrent() == EHCJHint::Wormhole; ++Step)
		{
			AgeAtEnd = Director.GetCurrentAge(Time);
			Time += 0.1f;
			Director.Update(Time, MakeInput());
		}
		TestTrue(TEXT("left early"), Director.GetCurrent() == EHCJHint::None && AgeAtEnd < FHCJHintDirector::GetRule(EHCJHint::Wormhole).Duration - 1.0f);
		TestTrue(TEXT("but stayed readable"), AgeAtEnd >= FHCJHintDirector::MinShowSeconds - 0.15f);
		TestTrue(TEXT("remaining fraction is 0 without a hint"), Director.GetCurrentRemainingFraction(Time) == 0.0f);
	}
	HCJ_TEST_FINISH();
}

/** Hint texts: key-cap markup, the tip after a death, the objective line, one sentence per item. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJHintTextTest, "HypeCubeJam.UI.HintTexts", HCJHintTestFlags)

bool FHCJHintTextTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	TArray<FHCJHintRun> Runs;
	HCJHints::SplitKeyMarkup(TEXT("[W] [S] thrust, hold [Left mouse]"), Runs);
	TestEqual(TEXT("runs"), Runs.Num(), 5);
	if (Runs.Num() == 5)
	{
		TestTrue(TEXT("key W"), Runs[0].bKey && Runs[0].Text == TEXT("W"));
		TestTrue(TEXT("space between keys is text"), !Runs[1].bKey && Runs[1].Text == TEXT(" "));
		TestTrue(TEXT("key S"), Runs[2].bKey && Runs[2].Text == TEXT("S"));
		TestTrue(TEXT("plain text"), !Runs[3].bKey && Runs[3].Text == TEXT(" thrust, hold "));
		TestTrue(TEXT("key with a space in its name"), Runs[4].bKey && Runs[4].Text == TEXT("Left mouse"));
	}
	HCJHints::SplitKeyMarkup(TEXT("25 % [unclosed"), Runs);
	TestTrue(TEXT("an unclosed bracket is text"), Runs.Num() == 1 && !Runs[0].bKey && Runs[0].Text == TEXT("25 % [unclosed"));
	HCJHints::SplitKeyMarkup(TEXT("a [] b"), Runs);
	TestTrue(TEXT("an empty bracket is text"), Runs.Num() == 1 && !Runs[0].bKey);
	HCJHints::SplitKeyMarkup(FString(), Runs);
	TestEqual(TEXT("empty markup"), Runs.Num(), 0);

	// The feed's strings (AHCJGameMode::NotifyShipKilled) map to the right cause.
	TestTrue(TEXT("killed by a player"), HCJHints::ClassifyDeath(TEXT("Bot-03"), TEXT("RAIL SHOT")) == EHCJDeathCause::Player);
	TestTrue(TEXT("killed by a player, no weapon known"), HCJHints::ClassifyDeath(TEXT("Bot-03"), FString()) == EHCJDeathCause::Player);
	TestTrue(TEXT("killed by a turret"), HCJHints::ClassifyDeath(TEXT("Station"), TEXT("TURRET")) == EHCJDeathCause::Turret);
	TestTrue(TEXT("black hole"), HCJHints::ClassifyDeath(FString(), TEXT("BLACK HOLE")) == EHCJDeathCause::BlackHole);
	TestTrue(TEXT("the void"), HCJHints::ClassifyDeath(FString(), TEXT("THE VOID")) == EHCJDeathCause::Void);
	TestTrue(TEXT("unknown environment death"), HCJHints::ClassifyDeath(FString(), FString()) == EHCJDeathCause::Void);
	for (const EHCJDeathCause Cause : { EHCJDeathCause::Player, EHCJDeathCause::Turret, EHCJDeathCause::BlackHole, EHCJDeathCause::Void })
	{
		for (int32 Index = -2; Index < 12; ++Index)
		{
			const TCHAR* Tip = HCJHints::PickDeathTip(Cause, Index);
			HCJ_TEST_CHECK(Tip != nullptr && FCString::Strlen(Tip) > 20 && FCString::Strlen(Tip) < 120, TEXT("tip %d of cause %d"), Index, static_cast<int32>(Cause));
		}
	}
	TestTrue(TEXT("tips rotate"), HCJHints::PickDeathTip(EHCJDeathCause::Player, 0) != HCJHints::PickDeathTip(EHCJDeathCause::Player, 1));

	TestEqual(TEXT("alone"), HCJHints::MakeObjectiveText(3, -1, FString(), 25), FString(TEXT("FIRST TO 25 KILLS WINS")));
	TestEqual(TEXT("nobody scored yet"), HCJHints::MakeObjectiveText(0, 0, TEXT("Bot-01"), 25), FString(TEXT("FIRST TO 25 KILLS WINS")));
	TestEqual(TEXT("leading"), HCJHints::MakeObjectiveText(7, 4, TEXT("Bot-01"), 25), FString(TEXT("YOU LEAD by 3  -  18 to go")));
	TestEqual(TEXT("leading without a limit"), HCJHints::MakeObjectiveText(7, 4, TEXT("Bot-01"), 0), FString(TEXT("YOU LEAD by 3")));
	TestEqual(TEXT("tied"), HCJHints::MakeObjectiveText(4, 4, TEXT("Bot-01"), 25), FString(TEXT("TIED for the lead with Bot-01")));
	TestEqual(TEXT("behind"), HCJHints::MakeObjectiveText(2, 9, TEXT("Bot-01"), 25), FString(TEXT("Bot-01 leads by 7  -  needs 16 more")));

	for (int32 Item = 0; Item < 10; ++Item)
	{
		HCJ_TEST_CHECK(FCString::Strlen(HCJHints::GetItemHint(Item)) > 20, TEXT("item %d has a hint"), Item);
	}
	TestEqual(TEXT("unknown item"), FString(HCJHints::GetItemHint(99)), FString());
	HCJ_TEST_FINISH();
}

#endif
