#include "Misc/AutomationTest.h"
#include "UI/HCJIntroLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJIntroTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Runs Flow for Seconds in steps of 1/60 s with a player that shows its first frame after OpenDelay and is Duration long. */
	EHCJIntroEnd Run(FHCJIntroFlow& Flow, float Seconds, float OpenDelay, float Duration, bool bErrorAfterOpen = false)
	{
		const float Step = 1.0f / 60.0f;
		EHCJIntroEnd End = EHCJIntroEnd::None;
		for (float Time = 0.0f; Time < Seconds && End == EHCJIntroEnd::None; Time += Step)
		{
			const bool bOpen = OpenDelay >= 0.0f && Time >= OpenDelay;
			const float Position = bOpen ? FMath::Min(Time - OpenDelay, Duration) : 0.0f;
			End = Flow.Tick(Step, bOpen && Position < Duration, bOpen && bErrorAfterOpen, Position, bOpen ? Duration : 0.0f);
		}
		return End;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJIntroFlowTest, "HypeCubeJam.UI.IntroFlow", HCJIntroTestFlags)

bool FHCJIntroFlowTest::RunTest(const FString& Parameters)
{
	{
		// The ordinary case: opens after half a second, plays 10 s, ends by itself.
		FHCJIntroFlow Flow;
		TestTrue(TEXT("black before the first frame"), Flow.GetBlackAlpha() == 1.0f && Flow.GetTitleAlpha() == 0.0f);
		TestTrue(TEXT("still running in the middle"), Run(Flow, 5.0f, 0.5f, 10.0f) == EHCJIntroEnd::None);
		TestTrue(TEXT("started, picture clear, no title yet"), Flow.HasStarted() && Flow.GetBlackAlpha() == 0.0f && Flow.GetTitleAlpha() == 0.0f);
		TestEqual(TEXT("first frame delay"), Flow.GetStartDelay(), 0.5f, 0.1f);
		TestTrue(TEXT("still running near the end"), Run(Flow, 3.5f, -0.0f, 10.0f) == EHCJIntroEnd::None || Flow.IsOver());
	}
	{
		FHCJIntroFlow Flow;
		TestTrue(TEXT("plays to its end"), Run(Flow, 30.0f, 0.5f, 10.0f) == EHCJIntroEnd::Finished);
		TestEqual(TEXT("about as long as the movie plus the opening"), Flow.GetElapsed(), 10.5f, 0.3f);
		TestTrue(TEXT("title up and picture black at the end"), Flow.GetTitleAlpha() == 1.0f && Flow.GetBlackAlpha() > 0.8f);
		TestTrue(TEXT("the end is final"), Flow.Tick(1.0f, true, false, 3.0f, 10.0f) == EHCJIntroEnd::Finished);
	}
	{
		// Title timing: fades in TitleLead before the end.
		FHCJIntroFlow Flow;
		Run(Flow, 0.2f, 0.0f, 10.0f);
		Flow.Tick(0.016f, true, false, 10.0f - Flow.TitleLead - 0.1f, 10.0f);
		TestEqual(TEXT("no title before its time"), Flow.GetTitleAlpha(), 0.0f);
		Flow.Tick(0.016f, true, false, 10.0f - Flow.TitleLead + 0.5f * Flow.TitleFade, 10.0f);
		TestEqual(TEXT("half way in"), Flow.GetTitleAlpha(), 0.5f, 0.01f);
		TestEqual(TEXT("picture not fading yet"), Flow.GetBlackAlpha(), 0.0f);
	}
	{
		// It never blocks: no frame within the timeout.
		FHCJIntroFlow Flow;
		TestTrue(TEXT("a movie that never opens ends the intro"), Run(Flow, 60.0f, -1.0f, 10.0f) == EHCJIntroEnd::OpenTimeout);
		TestEqual(TEXT("after the open timeout"), Flow.GetElapsed(), Flow.OpenTimeout, 0.05f);
	}
	{
		FHCJIntroFlow Flow;
		TestTrue(TEXT("an error ends it at once"), Run(Flow, 60.0f, 1.0f, 10.0f, true) == EHCJIntroEnd::Failed);
		TestTrue(TEXT("within a frame of the error"), Flow.GetElapsed() < 1.1f);
	}
	{
		// A decoder that stalls: the position stops moving but the player keeps saying it plays.
		FHCJIntroFlow Flow;
		EHCJIntroEnd End = EHCJIntroEnd::None;
		for (int32 Frame = 0; Frame < 60 * 60 && End == EHCJIntroEnd::None; ++Frame)
		{
			End = Flow.Tick(1.0f / 60.0f, true, false, 2.0f, 10.0f);
		}
		TestTrue(TEXT("a stalled movie is cut off"), End == EHCJIntroEnd::TooLong);
		TestEqual(TEXT("at the time limit"), Flow.GetElapsed(), Flow.MaxSeconds, 0.05f);
	}
	{
		// A player that stops without reaching its duration (it rewinds to 0 at the end).
		FHCJIntroFlow Flow;
		Run(Flow, 3.0f, 0.2f, 10.0f);
		EHCJIntroEnd End = EHCJIntroEnd::None;
		for (int32 Frame = 0; Frame < 60 && End == EHCJIntroEnd::None; ++Frame)
		{
			End = Flow.Tick(1.0f / 60.0f, false, false, 0.0f, 10.0f);
		}
		TestTrue(TEXT("stopped by itself counts as finished"), End == EHCJIntroEnd::Finished);
	}
	{
		// Skip: ignored in the first moment, then immediate - also before the first frame.
		FHCJIntroFlow Flow;
		Flow.Tick(0.1f, false, false, 0.0f, 0.0f);
		Flow.RequestSkip();
		TestTrue(TEXT("a key in the grace time does nothing"), Flow.Tick(0.1f, false, false, 0.0f, 0.0f) == EHCJIntroEnd::None);
		Flow.Tick(Flow.SkipGrace, false, false, 0.0f, 0.0f);
		Flow.RequestSkip();
		TestTrue(TEXT("a key after it skips"), Flow.Tick(0.016f, false, false, 0.0f, 0.0f) == EHCJIntroEnd::Skipped);

		FHCJIntroFlow Playing;
		Run(Playing, 4.0f, 0.3f, 10.0f);
		Playing.RequestSkip();
		TestTrue(TEXT("a key while it plays skips"), Playing.Tick(0.016f, true, false, 3.7f, 10.0f) == EHCJIntroEnd::Skipped);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJIntroCommandLineTest, "HypeCubeJam.UI.IntroCommandLine", HCJIntroTestFlags)

bool FHCJIntroCommandLineTest::RunTest(const FString& Parameters)
{
	const FString Project = TEXT("HypeCubeJam");
	TestTrue(TEXT("double click"), HCJIntro::ShouldPlay(TEXT(""), Project, true, false, false));
	TestTrue(TEXT("launcher puts the project name in front"), HCJIntro::ShouldPlay(TEXT("HypeCubeJam -windowed -ResX=1280 -ResY=720 -HCJNoEOS"), Project, true, false, false));
	TestFalse(TEXT("-HCJNoIntro"), HCJIntro::ShouldPlay(TEXT("HypeCubeJam -HCJNoIntro"), Project, true, false, false));
	TestFalse(TEXT("only once per process"), HCJIntro::ShouldPlay(TEXT(""), Project, true, false, true));
	TestFalse(TEXT("no renderer"), HCJIntro::ShouldPlay(TEXT(""), Project, false, false, false));
	TestFalse(TEXT("-nullrhi"), HCJIntro::ShouldPlay(TEXT("-nullrhi"), Project, true, false, false));
	TestFalse(TEXT("dedicated server"), HCJIntro::ShouldPlay(TEXT("-server"), Project, true, true, false));
	TestFalse(TEXT("-server on the command line"), HCJIntro::ShouldPlay(TEXT("/Engine/Maps/Entry -server"), Project, true, false, false));
	TestFalse(TEXT("a map URL (host without the menu)"), HCJIntro::ShouldPlay(TEXT("/Engine/Maps/Entry?listen?Seed=1234 -game"), Project, true, false, false));
	TestFalse(TEXT("joining an address"), HCJIntro::ShouldPlay(TEXT("127.0.0.1"), Project, true, false, false));
	TestFalse(TEXT("-AutoHost"), HCJIntro::ShouldPlay(TEXT("-AutoHost"), Project, true, false, false));
	TestFalse(TEXT("a capture run"), HCJIntro::ShouldPlay(TEXT("-HCJCapture=3 -HCJAutoFly"), Project, true, false, false));
	TestFalse(TEXT("a capture run that opens the menu"), HCJIntro::ShouldPlay(TEXT("-HCJMenu -HCJCapture=3"), Project, true, false, false));
	TestFalse(TEXT("a frame dump"), HCJIntro::ShouldPlay(TEXT("-HCJFrameDump=100"), Project, true, false, false));
	TestFalse(TEXT("automation"), HCJIntro::ShouldPlay(TEXT("-ExecCmds=\"Automation RunTests HypeCubeJam; Quit\""), Project, true, false, false));
	TestTrue(TEXT("-HCJIntro forces it in a capture run"), HCJIntro::ShouldPlay(TEXT("-HCJMenu -HCJIntro -HCJCapture=3"), Project, true, false, false));
	TestFalse(TEXT("-HCJNoIntro wins over -HCJIntro"), HCJIntro::ShouldPlay(TEXT("-HCJIntro -HCJNoIntro"), Project, true, false, false));
	return true;
}

#endif
