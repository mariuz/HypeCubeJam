#include "Tests/HCJTestHelpers.h"

#include "World/HCJSectorDecor.h"
#include "World/HCJSectorLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJDecorTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/**
 * Decorative rocks (item 26) are drawn only, but what the picture promises is checked: the same rocks for the same
 * sector, pebbles small and clear of portals, spawn points, keep-out spheres and real rocks, a belt that no living
 * ship can reach - and a layout that is bit for bit what it was before they were made.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJSectorDecorTest, "HypeCubeJam.World.SectorDecor", HCJDecorTestFlags)

bool FHCJSectorDecorTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	int32 NumPebbles = 0;
	int32 NumWanted = 0;

	for (int32 SeedIndex = 0; SeedIndex < 8; ++SeedIndex)
	{
		const int32 Seed = HCJTest::MakeSeed(SeedIndex);
		FHCJHypercubeGraph Graph;
		Graph.Build(4, Seed, 0.5f);
		for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
		{
			const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);
			const uint32 ChecksumBefore = Layout.ComputeChecksum();
			// A keep-out sphere in the middle of the field, as a pad or a station would be.
			const FHCJDecorKeepOut KeepOut[] = { { FVector(1500.0, -800.0, 400.0), 3000.0f } };

			const FHCJSectorDecor Decor = FHCJSectorDecor::Make(Layout, KeepOut, 600, 120, 4);
			const FHCJSectorDecor Again = FHCJSectorDecor::Make(Layout, KeepOut, 600, 120, 4);
			NumWanted += 600;
			NumPebbles += Decor.Pebbles.Num();

			HCJ_TEST_CHECK(Layout.ComputeChecksum() == ChecksumBefore, TEXT("seed %d sector %d: the layout changed"), Seed, Sector);
			HCJ_TEST_CHECK(Decor.Pebbles.Num() == Again.Pebbles.Num() && Decor.Belt.Num() == Again.Belt.Num(), TEXT("seed %d sector %d: two runs give %d and %d pebbles"), Seed, Sector,
				Decor.Pebbles.Num(), Again.Pebbles.Num());
			HCJ_TEST_CHECK(Decor.Pebbles.Num() <= 600 && Decor.Belt.Num() == 120, TEXT("seed %d sector %d: %d pebbles, %d belt rocks"), Seed, Sector, Decor.Pebbles.Num(), Decor.Belt.Num());

			for (int32 Index = 0; Index < Decor.Pebbles.Num(); ++Index)
			{
				const FHCJDecorRock& Rock = Decor.Pebbles[Index];
				HCJ_TEST_CHECK(Index >= Again.Pebbles.Num() || (Rock.LocalPosition == Again.Pebbles[Index].LocalPosition && Rock.Radius == Again.Pebbles[Index].Radius && Rock.Variant == Again.Pebbles[Index].Variant),
					TEXT("seed %d sector %d: pebble %d differs between two runs"), Seed, Sector, Index);
				HCJ_TEST_CHECK(Rock.Radius >= FHCJSectorDecor::PebbleMinRadius && Rock.Radius <= FHCJSectorDecor::PebbleMaxRadius && Rock.Radius < 50.0f && Rock.Variant < 4,
					TEXT("seed %d sector %d: pebble %d has radius %.1f, variant %d"), Seed, Sector, Index, Rock.Radius, int32(Rock.Variant));
				for (const FHCJPortalSpec& Portal : Layout.Portals)
				{
					HCJ_TEST_CHECK(FVector::Dist(Rock.LocalPosition, Portal.LocalPosition) >= FHCJSectorDecor::PortalKeepOut - HCJTest::DistanceTolerance,
						TEXT("seed %d sector %d: pebble %d is %.0f uu from a portal"), Seed, Sector, Index, FVector::Dist(Rock.LocalPosition, Portal.LocalPosition));
				}
				for (const FTransform& Spawn : Layout.SpawnPoints)
				{
					HCJ_TEST_CHECK(FVector::Dist(Rock.LocalPosition, Spawn.GetLocation()) >= FHCJSectorDecor::SpawnKeepOut - HCJTest::DistanceTolerance,
						TEXT("seed %d sector %d: pebble %d is %.0f uu from a spawn point"), Seed, Sector, Index, FVector::Dist(Rock.LocalPosition, Spawn.GetLocation()));
				}
				HCJ_TEST_CHECK(FVector::Dist(Rock.LocalPosition, KeepOut[0].Center) >= KeepOut[0].Radius - HCJTest::DistanceTolerance,
					TEXT("seed %d sector %d: pebble %d is inside the keep-out sphere"), Seed, Sector, Index);
				HCJ_TEST_CHECK(Layout.IsLocationClear(Rock.LocalPosition, 0.0f), TEXT("seed %d sector %d: pebble %d is inside a real rock"), Seed, Sector, Index);
			}
			for (int32 Index = 0; Index < Decor.Belt.Num(); ++Index)
			{
				const FHCJDecorRock& Rock = Decor.Belt[Index];
				const double Nearest = Rock.LocalPosition.Size() - Rock.Radius;
				HCJ_TEST_CHECK(Nearest >= FHCJSectorDecor::BeltInnerLimit - HCJTest::DistanceTolerance && Nearest > HCJ::SectorKillRadius,
					TEXT("seed %d sector %d: belt rock %d reaches to %.0f uu from the origin (kill radius %.0f)"), Seed, Sector, Index, Nearest, HCJ::SectorKillRadius);
				HCJ_TEST_CHECK(Rock.Radius >= FHCJSectorDecor::BeltMinRadius && Rock.Radius <= FHCJSectorDecor::BeltMaxRadius, TEXT("seed %d sector %d: belt rock %d has radius %.0f"), Seed, Sector,
					Index, Rock.Radius);
			}

			// Nothing to draw with, nothing made.
			HCJ_TEST_CHECK(FHCJSectorDecor::Make(Layout, KeepOut, 600, 120, 0).Pebbles.Num() == 0, TEXT("seed %d sector %d: pebbles without a mesh"), Seed, Sector);
		}
	}
	// The keep-outs must not eat the decoration: most of what was asked for is there.
	HCJ_TEST_CHECK(NumPebbles > NumWanted * 3 / 4, TEXT("only %d of %d pebbles were placed"), NumPebbles, NumWanted);

	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
