#include "Tests/HCJTestHelpers.h"

#include "World/HCJBlackHole.h"
#include "World/HCJSectorLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJBlackHoleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FHCJBlackHoleSettings HCJMakeHoleSettings(float Fraction = 0.25f)
	{
		FHCJBlackHoleSettings Settings;
		Settings.bEnabled = true;
		Settings.SectorFraction = Fraction;
		return Settings;
	}
}

/**
 * Black holes are gameplay objects placed on every machine from (match seed, sector layout, replicated settings):
 * deterministic, clear of portals, spawn points and rocks, and present in roughly the configured share of sectors.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJBlackHolePlacementTest, "HypeCubeJam.BlackHole.Placement", HCJBlackHoleTestFlags)

bool FHCJBlackHolePlacementTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	const double Tolerance = HCJTest::DistanceTolerance;
	const float Fractions[] = { 0.25f, 1.0f };

	for (const float Fraction : Fractions)
	{
		const FHCJBlackHoleSettings Settings = HCJMakeHoleSettings(Fraction);
		const double Clearance = FHCJBlackHole::GetClearance(Settings);
		int32 NumSectors = 0;
		int32 NumHoles = 0;

		for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
		{
			for (int32 SeedIndex = 0; SeedIndex < 20; ++SeedIndex)
			{
				const int32 Seed = HCJTest::MakeSeed(SeedIndex);
				FHCJHypercubeGraph Graph;
				Graph.Build(Dim, Seed, (SeedIndex % 2) == 0 ? 0.5f : 1.0f);

				for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
				{
					const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);
					const FHCJBlackHole Hole = FHCJBlackHole::Make(Seed, Layout, Settings);
					const FHCJBlackHole Again = FHCJBlackHole::Make(Seed, Layout, Settings);
					++NumSectors;

					HCJ_TEST_CHECK(Hole.bPresent == Again.bPresent && Hole.LocalPosition == Again.LocalPosition, TEXT("dim %d seed %d sector %d: two placements differ"), Dim, Seed, Sector);
					if (!Hole.bPresent)
					{
						continue;
					}
					++NumHoles;

					const double Distance = Hole.LocalPosition.Size();
					HCJ_TEST_CHECK(Distance >= HCJ::BlackHoleMinDistance - Tolerance && Distance <= HCJ::BlackHoleMaxDistance + Tolerance,
						TEXT("dim %d seed %d sector %d: black hole %.1f uu from the sector origin"), Dim, Seed, Sector, Distance);

					for (const FHCJPortalSpec& Portal : Layout.Portals)
					{
						const double Gap = FVector::Dist(Hole.LocalPosition, Portal.LocalPosition);
						HCJ_TEST_CHECK(Gap >= Clearance + HCJ::PortalTriggerRadius - Tolerance, TEXT("dim %d seed %d sector %d: black hole %.1f uu from the portal of dimension %d"), Dim, Seed, Sector, Gap, Portal.Dim);
					}
					for (int32 Index = 0; Index < Layout.SpawnPoints.Num(); ++Index)
					{
						const double Gap = FVector::Dist(Hole.LocalPosition, Layout.SpawnPoints[Index].GetLocation());
						HCJ_TEST_CHECK(Gap >= Clearance - Tolerance, TEXT("dim %d seed %d sector %d: black hole %.1f uu from spawn point %d"), Dim, Seed, Sector, Gap, Index);
						// Nobody spawns where the pull is more than a drift: a fraction of the cruise speed.
						HCJ_TEST_CHECK(FHCJBlackHole::ComputePullSpeed(Gap, Settings) < 600.0f, TEXT("dim %d seed %d sector %d: spawn point %d is pulled at %.0f uu/s"), Dim, Seed, Sector, Index, FHCJBlackHole::ComputePullSpeed(Gap, Settings));
					}
					HCJ_TEST_CHECK(Layout.IsLocationClear(Hole.LocalPosition, HCJ::BlackHoleRockFreeHorizons * Settings.HorizonRadius - float(Tolerance)),
						TEXT("dim %d seed %d sector %d: a rock sits in the core of the black hole"), Dim, Seed, Sector);

					// The layout itself is untouched by all this (same object in, same checksum out).
					HCJ_TEST_CHECK(Layout.ComputeChecksum() == FHCJSectorLayout::Generate(Seed, Sector, Graph).ComputeChecksum(), TEXT("dim %d seed %d sector %d: layout changed"), Dim, Seed, Sector);
				}
			}
		}

		// Roughly the configured share: a sector only loses its hole when no position meets the clearances.
		const double Share = double(NumHoles) / NumSectors;
		HCJ_TEST_CHECK(Share <= Fraction + 0.03 && Share >= 0.75 * Fraction, TEXT("%.3f of %d sectors have a black hole, configured %.2f"), Share, NumSectors, Fraction);
	}

	// Off, zero fraction and invalid layouts give no hole; other seeds give other holes.
	FHCJHypercubeGraph Graph;
	Graph.Build(4, 1234, 0.5f);
	int32 NumDifferent = 0;
	for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
	{
		const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(1234, Sector, Graph);
		FHCJBlackHoleSettings Off = HCJMakeHoleSettings(1.0f);
		Off.bEnabled = false;
		HCJ_TEST_CHECK(!FHCJBlackHole::Make(1234, Layout, Off).bPresent && !FHCJBlackHole::Make(1234, Layout, HCJMakeHoleSettings(0.0f)).bPresent,
			TEXT("sector %d: a black hole although they are switched off"), Sector);
		const FHCJBlackHole A = FHCJBlackHole::Make(1234, Layout, HCJMakeHoleSettings(1.0f));
		const FHCJBlackHole B = FHCJBlackHole::Make(4321, Layout, HCJMakeHoleSettings(1.0f));
		NumDifferent += (A.bPresent != B.bPresent || !(A.LocalPosition == B.LocalPosition)) ? 1 : 0;
	}
	HCJ_TEST_CHECK(NumDifferent >= Graph.NumSectors() - 1, TEXT("only %d of 16 sectors place the hole differently for another seed"), NumDifferent);
	HCJ_TEST_CHECK(!FHCJBlackHole::Make(1234, FHCJSectorLayout(), HCJMakeHoleSettings(1.0f)).bPresent, TEXT("an empty layout got a black hole (%d)"), 0);

	HCJ_TEST_FINISH();
}

/** The pull: towards the centre, zero from the influence radius on, growing monotonically inwards, clamped. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJBlackHoleGravityTest, "HypeCubeJam.BlackHole.Gravity", HCJBlackHoleTestFlags)

bool FHCJBlackHoleGravityTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	const FHCJBlackHoleSettings Settings = HCJMakeHoleSettings();
	const float R = Settings.InfluenceRadius;

	// Zero outside, continuous at the edge.
	HCJ_TEST_CHECK(FHCJBlackHole::ComputePullSpeed(R, Settings) == 0.0f && FHCJBlackHole::ComputePullSpeed(2.0 * R, Settings) == 0.0f, TEXT("pull at the edge is %.3f"), FHCJBlackHole::ComputePullSpeed(R, Settings));
	HCJ_TEST_CHECK(FHCJBlackHole::ComputePullSpeed(R - 1.0, Settings) < 1.0f && FHCJBlackHole::ComputePullSpeed(R - 1.0, Settings) > 0.0f, TEXT("pull just inside the edge is %.3f"), FHCJBlackHole::ComputePullSpeed(R - 1.0, Settings));

	// Monotonic, never above the clamp, clamped at the centre.
	float Previous = 0.0f;
	for (double Distance = R; Distance >= 0.0; Distance -= 5.0)
	{
		const float Speed = FHCJBlackHole::ComputePullSpeed(Distance, Settings);
		HCJ_TEST_CHECK(Speed >= Previous && Speed <= Settings.MaxPullSpeed && FMath::IsFinite(Speed), TEXT("pull %.1f at %.0f uu after %.1f farther out"), Speed, Distance, Previous);
		Previous = Speed;
	}
	HCJ_TEST_CHECK(FHCJBlackHole::ComputePullSpeed(0.0, Settings) == Settings.MaxPullSpeed && FHCJBlackHole::ComputePullSpeed(Settings.HorizonRadius, Settings) == Settings.MaxPullSpeed,
		TEXT("pull at the centre is %.1f, at the horizon %.1f"), FHCJBlackHole::ComputePullSpeed(0.0, Settings), FHCJBlackHole::ComputePullSpeed(Settings.HorizonRadius, Settings));

	// Inverse-square shape: PullSpeed * ((R / d)^2 - 1).
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(FHCJBlackHole::ComputePullSpeed(R / 2.0, Settings), 3.0f * Settings.PullSpeed, 0.01f), TEXT("pull at half the radius is %.2f"), FHCJBlackHole::ComputePullSpeed(R / 2.0, Settings));
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(FHCJBlackHole::ComputePullSpeed(R / 4.0, Settings), 15.0f * Settings.PullSpeed, 0.01f), TEXT("pull at a quarter of the radius is %.2f"), FHCJBlackHole::ComputePullSpeed(R / 4.0, Settings));

	// The tuning the game relies on: a drift at the edge, escapable at cruise speed mid-range, boost needed
	// farther in, no escape before the horizon is reached.
	const float Cruise = 3000.0f;
	const float Boost = 6000.0f;
	const float NoCruiseEscape = FHCJBlackHole::ComputeNoEscapeRadius(Cruise, Settings);
	const float NoBoostEscape = FHCJBlackHole::ComputeNoEscapeRadius(Boost, Settings);
	HCJ_TEST_CHECK(FHCJBlackHole::ComputePullSpeed(0.75 * R, Settings) < 0.1f * Cruise && FHCJBlackHole::ComputePullSpeed(0.5 * R, Settings) < 0.5f * Cruise,
		TEXT("pull at 3/4 and 1/2 of the radius is %.0f / %.0f"), FHCJBlackHole::ComputePullSpeed(0.75 * R, Settings), FHCJBlackHole::ComputePullSpeed(0.5 * R, Settings));
	HCJ_TEST_CHECK(NoCruiseEscape > NoBoostEscape && NoBoostEscape > Settings.HorizonRadius && NoCruiseEscape < 0.5f * R,
		TEXT("no-escape radii: cruise %.0f, boost %.0f, horizon %.0f"), NoCruiseEscape, NoBoostEscape, Settings.HorizonRadius);
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(FHCJBlackHole::ComputePullSpeed(NoCruiseEscape, Settings), Cruise, 1.0f) && FMath::IsNearlyEqual(FHCJBlackHole::ComputePullSpeed(NoBoostEscape, Settings), Boost, 1.0f),
		TEXT("pull at the no-escape radii is %.1f / %.1f"), FHCJBlackHole::ComputePullSpeed(NoCruiseEscape, Settings), FHCJBlackHole::ComputePullSpeed(NoBoostEscape, Settings));

	// Direction: always straight at the centre, with the speed of that distance; nothing at the centre itself.
	FRandomStream Stream(77);
	for (int32 Index = 0; Index < 500; ++Index)
	{
		const FVector Direction = Stream.VRand();
		const double Distance = Stream.FRandRange(1.0, 1.2 * R);
		const FVector ToHole = -Direction * Distance;
		const FVector Pull = FHCJBlackHole::ComputePull(ToHole, Settings);
		const float Speed = FHCJBlackHole::ComputePullSpeed(Distance, Settings);
		HCJ_TEST_CHECK(FMath::IsNearlyEqual(Pull.Size(), double(Speed), 0.01), TEXT("pull vector %.2f long, speed %.2f at %.0f uu"), Pull.Size(), Speed, Distance);
		HCJ_TEST_CHECK(Speed <= 0.0f || FVector::DotProduct(Pull.GetSafeNormal(), -Direction) > 0.99999, TEXT("pull at %.0f uu does not point at the hole (%s)"), Distance, *Pull.ToString());
	}
	HCJ_TEST_CHECK(FHCJBlackHole::ComputePull(FVector::ZeroVector, Settings).IsZero(), TEXT("pull at the centre is not zero (%d)"), 0);

	// Switched off: nothing.
	FHCJBlackHoleSettings Off = Settings;
	Off.bEnabled = false;
	HCJ_TEST_CHECK(FHCJBlackHole::ComputePullSpeed(1000.0, Off) == 0.0f && FHCJBlackHole::ComputeNoEscapeRadius(Cruise, Off) == 0.0f, TEXT("disabled black holes still pull (%d)"), 0);

	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
