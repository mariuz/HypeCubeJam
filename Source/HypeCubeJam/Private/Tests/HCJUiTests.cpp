#include "Tests/HCJTestHelpers.h"

#include "Game/HCJUiLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJUiTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/** Kill streaks: double and triple kills need quick succession, a rampage every fifth kill of one life, death resets. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJStreakTest, "HypeCubeJam.UI.KillStreaks", HCJUiTestFlags)

bool FHCJStreakTest::RunTest(const FString& Parameters)
{
	FHCJStreakTracker Tracker;
	TestTrue(TEXT("first kill earns nothing"), Tracker.RegisterKill(10.0f) == EHCJStreak::None);
	TestTrue(TEXT("second kill inside the window is a double"), Tracker.RegisterKill(12.0f) == EHCJStreak::Double);
	TestTrue(TEXT("third in a row is a triple"), Tracker.RegisterKill(15.5f) == EHCJStreak::Triple);
	TestTrue(TEXT("a kill after the window starts again"), Tracker.RegisterKill(15.5f + FHCJStreakTracker::MultiKillWindow + 0.1f) == EHCJStreak::None);
	TestTrue(TEXT("fifth kill of the life is a rampage, whatever the timing"), Tracker.RegisterKill(100.0f) == EHCJStreak::Rampage);
	TestEqual(TEXT("life streak counts every kill"), Tracker.GetLifeStreak(), 5);

	Tracker.RegisterDeath();
	TestEqual(TEXT("death clears the life streak"), Tracker.GetLifeStreak(), 0);
	TestTrue(TEXT("no double across a death"), Tracker.RegisterKill(100.5f) == EHCJStreak::None);
	TestTrue(TEXT("double after the death"), Tracker.RegisterKill(101.0f) == EHCJStreak::Double);

	// Ten kills in one life, far apart: rampage exactly at 5 and 10.
	FHCJStreakTracker Slow;
	int32 NumRampages = 0;
	for (int32 Kill = 1; Kill <= 10; ++Kill)
	{
		const EHCJStreak Streak = Slow.RegisterKill(Kill * 30.0f);
		NumRampages += Streak == EHCJStreak::Rampage ? 1 : 0;
		TestTrue(TEXT("slow kills are never multi-kills"), Streak == EHCJStreak::None || Streak == EHCJStreak::Rampage);
	}
	TestEqual(TEXT("rampage every fifth kill"), NumRampages, 2);
	return true;
}

/** HUD scaling: 1 at 1080p, follows the smaller axis, clamped; text never below the readable minimum. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJHudScaleTest, "HypeCubeJam.UI.HudScale", HCJUiTestFlags)

bool FHCJHudScaleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("1920x1080"), HCJUi::ComputeUIScale(1920.0f, 1080.0f), 1.0f, 0.001f);
	TestEqual(TEXT("1280x720"), HCJUi::ComputeUIScale(1280.0f, 720.0f), 2.0f / 3.0f, 0.001f);
	TestEqual(TEXT("3840x2160"), HCJUi::ComputeUIScale(3840.0f, 2160.0f), 2.0f, 0.001f);
	TestEqual(TEXT("narrow window follows its width"), HCJUi::ComputeUIScale(960.0f, 1080.0f), 0.5f, 0.001f);
	TestEqual(TEXT("ultrawide follows its height"), HCJUi::ComputeUIScale(3440.0f, 1440.0f), 1440.0f / 1080.0f, 0.001f);
	TestEqual(TEXT("clamped below"), HCJUi::ComputeUIScale(320.0f, 200.0f), 0.5f, 0.001f);
	TestEqual(TEXT("clamped above"), HCJUi::ComputeUIScale(20000.0f, 10000.0f), 3.0f, 0.001f);
	TestEqual(TEXT("degenerate canvas"), HCJUi::ComputeUIScale(0.0f, 0.0f), 1.0f, 0.001f);

	TestEqual(TEXT("text at 720p is held at the minimum"), HCJUi::ComputeTextScale(HCJUi::ComputeUIScale(1280.0f, 720.0f)), 0.8f, 0.001f);
	TestEqual(TEXT("text at 1080p"), HCJUi::ComputeTextScale(1.0f), 1.0f, 0.001f);
	TestEqual(TEXT("text at 4K"), HCJUi::ComputeTextScale(2.0f), 2.0f, 0.001f);

	// Spread circle: a cone of half the field of view reaches the screen edge; no spread, no circle; monotonic.
	TestEqual(TEXT("spread of half the fov touches the edge"), HCJUi::ComputeSpreadRadius(45.0f, 90.0f, 1280.0f), 640.0f, 0.5f);
	TestEqual(TEXT("no spread"), HCJUi::ComputeSpreadRadius(0.0f, 95.0f, 1280.0f), 0.0f, 0.001f);
	TestTrue(TEXT("more spread, larger circle"), HCJUi::ComputeSpreadRadius(4.5f, 95.0f, 1280.0f) > HCJUi::ComputeSpreadRadius(1.0f, 95.0f, 1280.0f));

	// Off-screen marker: on the ring, in the direction of the thing (screen Y grows downwards).
	float Angle = 0.0f;
	const FVector2D Centre(640.0, 360.0);
	const FVector2D Ring(200.0, 200.0);
	FVector2D Marker = HCJUi::ComputeEdgeMarker(5.0f, 0.0f, Centre, Ring, Angle);
	TestTrue(TEXT("to the right"), Marker.Equals(FVector2D(840.0, 360.0), 0.01));
	Marker = HCJUi::ComputeEdgeMarker(0.0f, 3.0f, Centre, Ring, Angle);
	TestTrue(TEXT("above"), Marker.Equals(FVector2D(640.0, 160.0), 0.01));
	Marker = HCJUi::ComputeEdgeMarker(-2.0f, -2.0f, Centre, Ring, Angle);
	TestTrue(TEXT("below left, on the ring"), FMath::IsNearlyEqual(FVector2D::Distance(Marker, Centre), 200.0, 0.01) && Marker.X < Centre.X && Marker.Y > Centre.Y);
	Marker = HCJUi::ComputeEdgeMarker(0.0f, 0.0f, Centre, Ring, Angle);
	TestTrue(TEXT("no direction points up"), Marker.Equals(FVector2D(640.0, 160.0), 0.01));
	return true;
}

/** Which command lines open the title menu: only the bare start; every documented automated / join / host line bypasses it. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJStartMenuTest, "HypeCubeJam.UI.StartMenuCommandLine", HCJUiTestFlags)

bool FHCJStartMenuTest::RunTest(const FString& Parameters)
{
	const FString Project = TEXT("HypeCubeJam");
	TestTrue(TEXT("no arguments"), HCJUi::WantsStartMenu(TEXT(""), Project));
	TestTrue(TEXT("window switches only"), HCJUi::WantsStartMenu(TEXT("-windowed -ResX=1280 -ResY=720"), Project));
	TestTrue(TEXT("launcher puts the project name first"), HCJUi::WantsStartMenu(TEXT("HypeCubeJam -windowed"), Project));
	TestTrue(TEXT("editor binary with -game and no URL"), HCJUi::WantsStartMenu(TEXT("\"F:/work/HypeCubeJam/HypeCubeJam.uproject\" -game -log"), Project));

	TestFalse(TEXT("-AutoHost"), HCJUi::WantsStartMenu(TEXT("-AutoHost -windowed"), Project));
	TestFalse(TEXT("join by address"), HCJUi::WantsStartMenu(TEXT("127.0.0.1 -windowed"), Project));
	TestFalse(TEXT("join by address after the project name"), HCJUi::WantsStartMenu(TEXT("HypeCubeJam 192.168.1.20"), Project));
	TestFalse(TEXT("map URL with options"), HCJUi::WantsStartMenu(TEXT("\"/Engine/Maps/Entry?listen?Seed=1234?Dim=3\" -windowed"), Project));
	TestFalse(TEXT("editor binary with a URL"), HCJUi::WantsStartMenu(TEXT("\"F:/work/HypeCubeJam/HypeCubeJam.uproject\" \"/Engine/Maps/Entry?listen\" -game -log"), Project));
	TestFalse(TEXT("dedicated server"), HCJUi::WantsStartMenu(TEXT("\"F:/work/HypeCubeJam/HypeCubeJam.uproject\" -server -log"), Project));
	TestFalse(TEXT("capture run"), HCJUi::WantsStartMenu(TEXT("-HCJCapture=8 -HCJAutoFly -windowed"), Project));
	TestFalse(TEXT("frame dump"), HCJUi::WantsStartMenu(TEXT("-HCJFrameDump=1200 -UseFixedTimeStep"), Project));
	TestFalse(TEXT("headless"), HCJUi::WantsStartMenu(TEXT("-nullrhi -log"), Project));
	return true;
}

/** Soft lock: nearest the crosshair inside the cone, sticky, falls back to the nearest ship; "fire now" and the radar projection. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJTargetingTest, "HypeCubeJam.UI.Targeting", HCJUiTestFlags)

bool FHCJTargetingTest::RunTest(const FString& Parameters)
{
	// angle, distance, visible
	const FHCJTargetCandidate Centre{ 2.0f, 9000.0f, true };
	const FHCJTargetCandidate Side{ 15.0f, 1500.0f, true };
	const FHCJTargetCandidate Behind{ 150.0f, 800.0f, true };
	const FHCJTargetCandidate Hidden{ 1.0f, 3000.0f, false };

	const FHCJTargetCandidate A[] = { Side, Centre, Behind };
	TestEqual(TEXT("nearest the crosshair wins inside the cone, even when farther away"), HCJUi::PickTarget(A, INDEX_NONE), 1);
	const FHCJTargetCandidate B[] = { Behind, Hidden };
	TestEqual(TEXT("nobody in the cone: the nearest ship"), HCJUi::PickTarget(B, INDEX_NONE), 0);
	const FHCJTargetCandidate C[] = { Hidden, Side };
	TestEqual(TEXT("a ship behind a rock loses against a visible one in the cone"), HCJUi::PickTarget(C, INDEX_NONE), 1);
	TestEqual(TEXT("empty list"), HCJUi::PickTarget(TConstArrayView<FHCJTargetCandidate>(), INDEX_NONE), static_cast<int32>(INDEX_NONE));

	// Stickiness: a slightly better candidate does not steal the lock, a clearly better one does.
	const FHCJTargetCandidate Held{ 8.0f, 3000.0f, true };
	const FHCJTargetCandidate Slightly{ 5.0f, 3000.0f, true };
	const FHCJTargetCandidate Clearly{ 1.0f, 3000.0f, true };
	const FHCJTargetCandidate D[] = { Held, Slightly };
	TestEqual(TEXT("without a current target the better angle wins"), HCJUi::PickTarget(D, INDEX_NONE), 1);
	TestEqual(TEXT("the held target keeps the lock against a slightly better one"), HCJUi::PickTarget(D, 0), 0);
	const FHCJTargetCandidate E[] = { Held, Clearly };
	TestEqual(TEXT("a clearly better one takes over"), HCJUi::PickTarget(E, 0), 1);

	// Fire now: the angular size of the hit radius at that distance.
	TestTrue(TEXT("dead on"), HCJUi::IsOnLead(0.0f, 5000.0f, 95.0f));
	TestTrue(TEXT("inside the hit radius"), HCJUi::IsOnLead(1.0f, 5000.0f, 95.0f));   // asin(95/5000) = 1.09 deg
	TestFalse(TEXT("outside the hit radius"), HCJUi::IsOnLead(1.2f, 5000.0f, 95.0f));
	TestTrue(TEXT("point blank"), HCJUi::IsOnLead(40.0f, 50.0f, 95.0f));
	TestFalse(TEXT("the same error is a miss farther away"), HCJUi::IsOnLead(1.0f, 12000.0f, 95.0f));

	// Radar: forward is up, right is right, the rim clamps, height is separate.
	float Height = 0.0f;
	FVector2D Point = HCJUi::ProjectRadar(5000.0f, 0.0f, 0.0f, 10000.0f, Height);
	TestTrue(TEXT("ahead is up"), Point.Equals(FVector2D(0.0, -0.5), 0.001) && FMath::IsNearlyZero(Height));
	Point = HCJUi::ProjectRadar(0.0f, 2500.0f, 1000.0f, 10000.0f, Height);
	TestTrue(TEXT("right is right, above is positive"), Point.Equals(FVector2D(0.25, 0.0), 0.001) && FMath::IsNearlyEqual(Height, 0.1f, 0.001f));
	Point = HCJUi::ProjectRadar(-30000.0f, 40000.0f, -50000.0f, 10000.0f, Height);
	TestTrue(TEXT("far away sits on the rim in the right direction"), FMath::IsNearlyEqual(Point.Size(), 1.0, 0.001) && Point.X > 0.0 && Point.Y > 0.0);
	TestEqual(TEXT("height is clamped"), Height, -1.0f, 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
