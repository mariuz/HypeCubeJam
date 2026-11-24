#include "Tests/HCJTestHelpers.h"

#include "Game/HCJGameState.h"
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

	// "It has its nose on us": close and inside the cone only.
	TestTrue(TEXT("nose on us"), HCJUi::IsAimingAt(FVector::ForwardVector, FVector(3000.0, 0.0, 0.0)));
	TestTrue(TEXT("ten degrees off still counts"), HCJUi::IsAimingAt(FVector::ForwardVector, FVector(3000.0, 3000.0 * FMath::Tan(FMath::DegreesToRadians(10.0)), 0.0)));
	TestFalse(TEXT("fifteen degrees off does not"), HCJUi::IsAimingAt(FVector::ForwardVector, FVector(3000.0, 3000.0 * FMath::Tan(FMath::DegreesToRadians(15.0)), 0.0)));
	TestFalse(TEXT("flying away from us"), HCJUi::IsAimingAt(FVector::ForwardVector, FVector(-3000.0, 0.0, 0.0)));
	TestFalse(TEXT("too far to matter"), HCJUi::IsAimingAt(FVector::ForwardVector, FVector(HCJUi::AimWarnDistance + 100.0, 0.0, 0.0)));
	TestFalse(TEXT("at the same place"), HCJUi::IsAimingAt(FVector::ForwardVector, FVector::ZeroVector));
	// The side a hit came from, for the death recap.
	TestTrue(TEXT("head-on"), HCJUi::ClassifyHitSide(0.0f) == EHCJHitSide::Ahead && HCJUi::ClassifyHitSide(55.0f) == EHCJHitSide::Ahead);
	TestTrue(TEXT("from the side"), HCJUi::ClassifyHitSide(65.0f) == EHCJHitSide::Side && HCJUi::ClassifyHitSide(105.0f) == EHCJHitSide::Side);
	TestTrue(TEXT("from behind"), HCJUi::ClassifyHitSide(110.0f) == EHCJHitSide::Behind && HCJUi::ClassifyHitSide(180.0f) == EHCJHitSide::Behind);

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

/**
 * Hypercube map: every sector inside the square and apart from every other one, a portal of one dimension always in
 * the same direction, depth in [0, 1]; the labels of the own sector and of everything one jump away find room without
 * lying on each other or on a mark - for every own sector of a 3-D, 4-D and 5-D universe with every portal open, at
 * the sizes of 1280x720 and 1920x1080.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJHypercubeMapTest, "HypeCubeJam.UI.HypercubeMap", HCJUiTestFlags)

bool FHCJHypercubeMapTest::RunTest(const FString& Parameters)
{
	for (int32 Dimension = 1; Dimension <= 6; ++Dimension)
	{
		const int32 NumSectors = 1 << Dimension;
		double MaxX = 0.0;
		double MaxY = 0.0;
		double MinDepth = 2.0;
		double MaxDepth = -1.0;
		double MinDistance = 10.0;
		for (int32 Sector = 0; Sector < NumSectors; ++Sector)
		{
			const FVector Point = HCJUi::ProjectMapSector(Sector, Dimension);
			TestTrue(TEXT("pure function"), Point.Equals(HCJUi::ProjectMapSector(Sector, Dimension), 0.0));
			MaxX = FMath::Max(MaxX, FMath::Abs(Point.X));
			MaxY = FMath::Max(MaxY, FMath::Abs(Point.Y));
			MinDepth = FMath::Min(MinDepth, Point.Z);
			MaxDepth = FMath::Max(MaxDepth, Point.Z);
			for (int32 Other = 0; Other < Sector; ++Other)
			{
				MinDistance = FMath::Min(MinDistance, FVector2D::Distance(FVector2D(Point), FVector2D(HCJUi::ProjectMapSector(Other, Dimension))));
			}
			// With the sway nothing leaves the panel (the marks have a margin of a seventh of the half width). From two
			// dimensions on: a single line of two sectors (no universe has it) is fitted to its own small height.
			const FVector Swayed = HCJUi::ProjectMapSector(Sector, Dimension, FMath::DegreesToRadians(6.0f));
			TestTrue(*FString::Printf(TEXT("%d-D: swayed mark of sector %d stays near the square (%.3f, %.3f)"), Dimension, Sector, Swayed.X, Swayed.Y),
				Dimension < 2 || (FMath::Abs(Swayed.X) < 1.12 && FMath::Abs(Swayed.Y) < 1.12));
		}
		TestEqual(*FString::Printf(TEXT("%d-D fills the width"), Dimension), MaxX, 1.0, 1.0e-6);
		TestTrue(*FString::Printf(TEXT("%d-D stays inside the height"), Dimension), MaxY <= 1.0 + 1.0e-6 && (Dimension < 2 || MaxY > 0.999));
		TestTrue(*FString::Printf(TEXT("%d-D depth in [0, 1]"), Dimension), MinDepth >= 0.0 && MaxDepth <= 1.0 && (Dimension < 3 || (MinDepth < 0.001 && MaxDepth > 0.999)));
		// Apart: a fifth of the half width for 8 and 16 sectors, a tenth for 32 (at 1280x720 that is 16 and 9 pixels).
		const double Wanted = Dimension <= 4 ? 0.2 : Dimension == 5 ? 0.1 : 0.02;
		TestTrue(*FString::Printf(TEXT("%d-D: sectors apart (nearest two %.3f, wanted %.3f)"), Dimension, MinDistance, Wanted), MinDistance >= Wanted);

		// One dimension, one direction: the step of a bit is the same wherever the bits above the cube are concerned.
		for (int32 Dim = 3; Dim < Dimension; ++Dim)
		{
			const FVector Step = HCJUi::ProjectMapSector(1 << Dim, Dimension) - HCJUi::ProjectMapSector(0, Dimension);
			for (int32 Sector = 0; Sector < NumSectors; ++Sector)
			{
				if ((Sector & (1 << Dim)) == 0)
				{
					const FVector Other = HCJUi::ProjectMapSector(Sector | (1 << Dim), Dimension) - HCJUi::ProjectMapSector(Sector, Dimension);
					TestTrue(TEXT("portals of one dimension are parallel"), FVector2D(Other).Equals(FVector2D(Step), 1.0e-6));
				}
			}
		}
	}
	TestTrue(TEXT("no dimension, no sector: the middle"), HCJUi::ProjectMapSector(0, 0).Equals(FVector(0.0, 0.0, 0.5)) && HCJUi::ProjectMapSector(-1, 4).Equals(FVector(0.0, 0.0, 0.5)));

	// Labels. Sizes as the HUD has them: map 200 / 260 px wide at 1280x720 (300 / 390 at 1920x1080), a digit about 6 (7.5) px.
	struct FScreen
	{
		const TCHAR* Name;
		float Unit;
		float UIScale;
		float Digit;
		float LineHeight;
	};
	const FScreen Screens[] = { { TEXT("1280x720"), 0.75f, 2.0f / 3.0f, 6.0f, 11.0f }, { TEXT("1920x1080"), 1.0f, 1.0f, 7.5f, 14.0f } };
	for (const FScreen& Screen : Screens)
	{
		for (int32 Dimension = 3; Dimension <= 5; ++Dimension)
		{
			const int32 NumSectors = 1 << Dimension;
			const float Width = (Dimension >= 5 ? 390.0f : 300.0f) * Screen.UIScale;
			const float AreaHeight = Width * 0.9f;
			const FVector2D Center(Width * 0.5f, AreaHeight * 0.5f);
			const FVector2D Radius(Width * 0.5f - 24.0f * Screen.Unit, AreaHeight * 0.5f - 22.0f * Screen.Unit);
			TArray<FVector2D> Dots;
			for (int32 Sector = 0; Sector < NumSectors; ++Sector)
			{
				const FVector Point = HCJUi::ProjectMapSector(Sector, Dimension);
				Dots.Add(FVector2D(Center.X + Point.X * Radius.X, Center.Y + Point.Y * Radius.Y));
			}
			const FBox2D Bounds(FVector2D(2.0f * Screen.UIScale, 0.0), FVector2D(Width - 2.0f * Screen.UIScale, AreaHeight));
			int32 NumBad = 0;
			float Worst = 0.0f;
			for (int32 Own = 0; Own < NumSectors; ++Own)
			{
				TArray<FHCJMapLabel> Labels;
				FHCJMapLabel& You = Labels.AddDefaulted_GetRef();
				You.Anchor = Dots[Own];
				You.AnchorRadius = 12.5f * Screen.Unit;
				You.Size = FVector2D(4.0f * Screen.Digit, Screen.LineHeight);
				for (int32 Dim = 0; Dim < Dimension; ++Dim)
				{
					FHCJMapLabel& Label = Labels.AddDefaulted_GetRef();
					Label.Anchor = Dots[Own ^ (1 << Dim)];
					Label.AnchorRadius = 10.5f * Screen.Unit;
					Label.Size = FVector2D(Dimension * Screen.Digit + 4.0f * Screen.Unit, Screen.LineHeight);
				}
				const float Left = HCJUi::PlaceMapLabels(Labels, Bounds, Dots, 3.0f * Screen.Unit, Dots[Own]);
				for (const FHCJMapLabel& Label : Labels)
				{
					const FBox2D Box(Label.Center - Label.Size * 0.5, Label.Center + Label.Size * 0.5);
					TestTrue(TEXT("label inside the map"), Box.Min.X >= Bounds.Min.X - 0.01 && Box.Max.X <= Bounds.Max.X + 0.01 && Box.Min.Y >= Bounds.Min.Y - 0.01 && Box.Max.Y <= Bounds.Max.Y + 0.01);
				}
				NumBad += Left > 0.5f ? 1 : 0;
				Worst = FMath::Max(Worst, Left);
			}
			// 8 and 16 sectors: never. 32 sectors with all five portals open: a corner of a label may touch (a tenth of a label at most).
			const float Allowed = Dimension <= 4 ? 0.5f : 0.1f * (Dimension * Screen.Digit) * Screen.LineHeight;
			TestTrue(*FString::Printf(TEXT("%s, %d-D: labels clear of each other and of the marks (%d of %d own sectors overlap, worst %.1f square px, allowed %.1f)"),
				Screen.Name, Dimension, NumBad, NumSectors, Worst, Allowed), Worst <= Allowed);
		}
	}
	return true;
}

/**
 * Pad markers of the HUD (item 38): the list of the nearest pads without an allocation, the strength of a marker by
 * its distance, the distance text in steps.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPickupMarkerTest, "HypeCubeJam.UI.PickupMarkers", HCJUiTestFlags)

bool FHCJPickupMarkerTest::RunTest(const FString& Parameters)
{
	// The nearest N of a stream, sorted, against a full sort.
	FRandomStream Random(4711);
	for (int32 Round = 0; Round < 200; ++Round)
	{
		const int32 Capacity = 1 + Round % HCJUi::MaxPickupMarkers;
		const int32 NumItems = Round % 23;
		FHCJNearSlot Slots[HCJUi::MaxPickupMarkers];
		int32 Num = 0;
		TArray<double> All;
		for (int32 Index = 0; Index < NumItems; ++Index)
		{
			// (Few distinct values: equal distances happen.)
			const double DistSq = static_cast<double>(Random.RandRange(0, 30)) * 1000.0;
			All.Add(DistSq);
			Num = HCJUi::InsertNearest(MakeArrayView(Slots, Capacity), Num, DistSq, Index);
		}
		TArray<double> Sorted = All;
		Sorted.Sort();
		bool bGood = Num == FMath::Min(NumItems, Capacity);
		TSet<int32> Seen;
		for (int32 Slot = 0; Slot < Num && bGood; ++Slot)
		{
			bGood = Slots[Slot].DistSq == Sorted[Slot] && All.IsValidIndex(Slots[Slot].Index) && All[Slots[Slot].Index] == Slots[Slot].DistSq && !Seen.Contains(Slots[Slot].Index);
			Seen.Add(Slots[Slot].Index);
		}
		TestTrue(*FString::Printf(TEXT("round %d: the %d nearest of %d, nearest first, each once"), Round, Capacity, NumItems), bGood);
	}
	{
		FHCJNearSlot Slots[3];
		int32 Num = HCJUi::InsertNearest(MakeArrayView(Slots), 0, 5.0, 10);
		Num = HCJUi::InsertNearest(MakeArrayView(Slots), Num, 5.0, 11);
		TestTrue(TEXT("an equal distance goes behind"), Num == 2 && Slots[0].Index == 10 && Slots[1].Index == 11);
		Num = HCJUi::InsertNearest(MakeArrayView(Slots), Num, 1.0, 12);
		Num = HCJUi::InsertNearest(MakeArrayView(Slots), Num, 9.0, 13);
		TestTrue(TEXT("a full list drops what is farther"), Num == 3 && Slots[0].Index == 12 && Slots[1].Index == 10 && Slots[2].Index == 11);
		TestEqual(TEXT("no room at all"), HCJUi::InsertNearest(TArrayView<FHCJNearSlot>(), 0, 1.0, 1), 0);
		TestEqual(TEXT("a wrong count is clamped"), HCJUi::InsertNearest(MakeArrayView(Slots), 99, 100.0, 14), 3);
	}

	// Strength.
	TestEqual(TEXT("full strength near"), HCJUi::PickupMarkerStrength(3000.0f, 7000.0f, 26000.0f, false), 1.0f);
	TestEqual(TEXT("the far strength far away"), HCJUi::PickupMarkerStrength(40000.0f, 7000.0f, 26000.0f, false), HCJUi::PickupMarkerFarStrength);
	TestEqual(TEXT("half way between"), HCJUi::PickupMarkerStrength(16500.0f, 7000.0f, 26000.0f, false), 0.5f * (1.0f + HCJUi::PickupMarkerFarStrength), 0.001f);
	TestEqual(TEXT("the pad to fly to is always at full strength"), HCJUi::PickupMarkerStrength(40000.0f, 7000.0f, 26000.0f, true), 1.0f);
	TestEqual(TEXT("no range between near and far"), HCJUi::PickupMarkerStrength(9000.0f, 7000.0f, 7000.0f, false), HCJUi::PickupMarkerFarStrength);
	float Last = 1.0f;
	for (float Distance = 0.0f; Distance < 50000.0f; Distance += 250.0f)
	{
		const float Strength = HCJUi::PickupMarkerStrength(Distance, 7000.0f, 26000.0f, false);
		TestTrue(TEXT("never stronger farther away, never too faint to read"), Strength <= Last && Strength >= 0.6f);
		Last = Strength;
	}

	// Distance text.
	TestEqual(TEXT("12.4 m"), HCJUi::MarkerMetres(1240.0f), 12);
	TestEqual(TEXT("50 m"), HCJUi::MarkerMetres(5000.0f), 50);
	TestEqual(TEXT("52 m is shown as 50"), HCJUi::MarkerMetres(5200.0f), 50);
	TestEqual(TEXT("53 m is shown as 55"), HCJUi::MarkerMetres(5300.0f), 55);
	TestEqual(TEXT("238 m is shown as 240"), HCJUi::MarkerMetres(23800.0f), 240);
	TestEqual(TEXT("a negative distance is 0"), HCJUi::MarkerMetres(-300.0f), 0);
	TestTrue(TEXT("low hull is a third of the hull"), HCJUi::LowHullFraction > 0.2f && HCJUi::LowHullFraction < 0.5f);
	return true;
}

/** The pad the HUD's pointer leads to (items 38 and 40): repair before shield on a low hull, a weapon pad on Easy without a weapon, nothing else. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJGuidePadTest, "HypeCubeJam.UI.GuidePad", HCJUiTestFlags)

bool FHCJGuidePadTest::RunTest(const FString& Parameters)
{
	using namespace HCJUi;
	TestEqual(TEXT("low hull: repair first"), GuidePadRank(EHCJPickupItem::Repair, true, false), 2);
	TestEqual(TEXT("low hull: then a shield cell"), GuidePadRank(EHCJPickupItem::ShieldCell, true, false), 1);
	TestEqual(TEXT("low hull: never a weapon, even without one"), GuidePadRank(EHCJPickupItem::Scatter, true, true), 0);
	TestEqual(TEXT("low hull: no power-up"), GuidePadRank(EHCJPickupItem::Overdrive, true, false), 0);
	for (const EHCJPickupItem Weapon : { EHCJPickupItem::Scatter, EHCJPickupItem::Rail, EHCJPickupItem::Missile, EHCJPickupItem::Pulse })
	{
		TestEqual(TEXT("without a weapon: every weapon pad"), GuidePadRank(Weapon, false, true), 1);
		TestEqual(TEXT("with a weapon (or not Easy): no pointer"), GuidePadRank(Weapon, false, false), 0);
	}
	TestEqual(TEXT("torpedoes are not a weapon to fly to"), GuidePadRank(EHCJPickupItem::Torpedoes, false, true), 0);
	TestEqual(TEXT("a repair pad on a healthy hull: no pointer"), GuidePadRank(EHCJPickupItem::Repair, false, true), 0);
	TestEqual(TEXT("nothing else"), GuidePadRank(EHCJPickupItem::Cloak, false, false), 0);
	return true;
}

/**
 * Item 42: the HUD wants a weapon pad only when the replicated flag is on, the hull is not low and the ship holds only its
 * base weapon; and the flag is a replicated property of the game state, so a remote client's HUD reads the server's value.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJWeaponPointerReplicatedTest, "HypeCubeJam.UI.WeaponPointerReplicated", HCJUiTestFlags)

bool FHCJWeaponPointerReplicatedTest::RunTest(const FString& Parameters)
{
	using namespace HCJUi;
	TestTrue(TEXT("on, healthy, phaser only: the pointer"), WantsWeaponPad(true, false, true));
	TestFalse(TEXT("flag off (Normal and up, a client before replication): none"), WantsWeaponPad(false, false, true));
	TestFalse(TEXT("low hull: the repair pad instead"), WantsWeaponPad(true, true, true));
	TestFalse(TEXT("holding a pickup weapon: none"), WantsWeaponPad(true, false, false));
	TestEqual(TEXT("the decision feeds the pad rank"), GuidePadRank(EHCJPickupItem::Rail, false, WantsWeaponPad(true, false, true)), 1);

	const AHCJGameState* Defaults = GetDefault<AHCJGameState>();
	TestFalse(TEXT("off until the server says otherwise"), Defaults->ShowsWeaponPadPointer());
	const FProperty* Flag = FindFProperty<FProperty>(AHCJGameState::StaticClass(), TEXT("bWeaponPadPointer"));
	if (!TestNotNull(TEXT("the flag is a property"), Flag))
	{
		return false;
	}
	// UHT sets CPF_Net for UPROPERTY(Replicated); DOREPLIFETIME in AHCJGameState::GetLifetimeReplicatedProps is checked by
	// the engine itself (a replicated property without it is reported when the class is first replicated).
	TestTrue(TEXT("marked replicated"), Flag->HasAnyPropertyFlags(CPF_Net));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
