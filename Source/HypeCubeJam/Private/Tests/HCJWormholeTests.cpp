#include "Tests/HCJTestHelpers.h"

#include "World/HCJBlackHole.h"
#include "World/HCJPickupPads.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJWormholes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJWormholeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FHCJWormholeSettings HCJMakeWormholeSettings(int32 Count = -1)
	{
		FHCJWormholeSettings Settings;
		Settings.bEnabled = true;
		Settings.Count = Count;
		return Settings;
	}
}

/**
 * Wormhole links are a pure function of (seed, dimension, count): deterministic, between sectors that no portal
 * could join, at most one mouth per sector, none in a square. Mouths sit in clear space.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJWormholeLinksTest, "HypeCubeJam.Wormholes.Links", HCJWormholeTestFlags)

bool FHCJWormholeLinksTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const double Tolerance = HCJTest::DistanceTolerance;

	FHCJBlackHoleSettings Holes;
	Holes.bEnabled = true;
	Holes.SectorFraction = 0.5f;
	FHCJPickupSettings Pickups;
	Pickups.bEnabled = true;

	int32 NumLinks = 0;
	int32 NumMouths = 0;
	int32 NumMouthFailures = 0;
	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		const int32 NumSectors = 1 << Dim;
		for (int32 SeedIndex = 0; SeedIndex < 20; ++SeedIndex)
		{
			const int32 Seed = HCJTest::MakeSeed(SeedIndex);
			const TArray<FHCJWormholeLink> Links = FHCJWormholes::MakeLinks(Seed, Dim, HCJMakeWormholeSettings());
			const TArray<FHCJWormholeLink> Again = FHCJWormholes::MakeLinks(Seed, Dim, HCJMakeWormholeSettings());
			HCJ_TEST_CHECK(Links == Again, TEXT("dim %d seed %d: two link lists differ"), Dim, Seed);

			const int32 Expected = Dim >= HCJ::WormholeMinDimension ? NumSectors / 4 : 0;
			HCJ_TEST_CHECK(Links.Num() == Expected && FHCJWormholes::GetAutoCount(Dim) == Expected, TEXT("dim %d seed %d: %d links, expected %d"), Dim, Seed, Links.Num(), Expected);
			NumLinks += Links.Num();

			uint64 Used = 0;
			int32 LastA = -1;
			for (const FHCJWormholeLink& Link : Links)
			{
				HCJ_TEST_CHECK(Link.SectorA >= 0 && Link.SectorA < Link.SectorB && Link.SectorB < NumSectors, TEXT("dim %d seed %d: link %d-%d out of range or unordered"), Dim, Seed, Link.SectorA, Link.SectorB);
				HCJ_TEST_CHECK(FMath::CountBits(static_cast<uint32>(Link.SectorA ^ Link.SectorB)) >= 2, TEXT("dim %d seed %d: link %d-%d joins hypercube neighbours"), Dim, Seed, Link.SectorA, Link.SectorB);
				HCJ_TEST_CHECK((Used & ((uint64(1) << Link.SectorA) | (uint64(1) << Link.SectorB))) == 0, TEXT("dim %d seed %d: a sector of link %d-%d has two mouths"), Dim, Seed, Link.SectorA, Link.SectorB);
				HCJ_TEST_CHECK(Link.SectorA > LastA, TEXT("dim %d seed %d: links not sorted at %d-%d"), Dim, Seed, Link.SectorA, Link.SectorB);
				Used |= (uint64(1) << Link.SectorA) | (uint64(1) << Link.SectorB);
				LastA = Link.SectorA;
			}

			// An explicit count is honoured up to one mouth per sector; zero and "off" give nothing.
			HCJ_TEST_CHECK(FHCJWormholes::MakeLinks(Seed, Dim, HCJMakeWormholeSettings(1)).Num() == (Dim >= HCJ::WormholeMinDimension ? 1 : 0), TEXT("dim %d seed %d: count 1 gave %d links"), Dim, Seed, FHCJWormholes::MakeLinks(Seed, Dim, HCJMakeWormholeSettings(1)).Num());
			HCJ_TEST_CHECK(FHCJWormholes::MakeLinks(Seed, Dim, HCJMakeWormholeSettings(1000)).Num() <= NumSectors / 2, TEXT("dim %d seed %d: more links than half the sectors (%d)"), Dim, Seed, NumSectors);
			HCJ_TEST_CHECK(FHCJWormholes::MakeLinks(Seed, Dim, HCJMakeWormholeSettings(0)).Num() == 0, TEXT("dim %d seed %d: count 0 gave links (%d)"), Dim, Seed, 0);
			FHCJWormholeSettings Off = HCJMakeWormholeSettings();
			Off.bEnabled = false;
			HCJ_TEST_CHECK(FHCJWormholes::MakeLinks(Seed, Dim, Off).Num() == 0, TEXT("dim %d seed %d: links although wormholes are off (%d)"), Dim, Seed, 0);

			// Mouths of the first seeds: clearances.
			if (SeedIndex >= 6 || Dim > 5)
			{
				continue;
			}
			FHCJHypercubeGraph Graph;
			Graph.Build(Dim, Seed, 0.5f);
			for (const FHCJWormholeLink& Link : Links)
			{
				for (const int32 Sector : { Link.SectorA, Link.SectorB })
				{
					const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);
					const FHCJBlackHole Hole = FHCJBlackHole::Make(Seed, Layout, Holes);
					const TArray<FVector> Pads = FHCJPickupPads::Make(Seed, Layout, Hole, Holes, Pickups);
					FVector Mouth = FVector::ZeroVector;
					FVector MouthAgain = FVector::ZeroVector;
					const bool bPlaced = FHCJWormholes::MakeMouth(Seed, Layout, Hole, Holes, Pads, Mouth);
					const bool bPlacedAgain = FHCJWormholes::MakeMouth(Seed, Layout, Hole, Holes, Pads, MouthAgain);
					HCJ_TEST_CHECK(bPlaced == bPlacedAgain && Mouth == MouthAgain, TEXT("dim %d seed %d sector %d: two mouth placements differ"), Dim, Seed, Sector);
					++NumMouths;
					if (!bPlaced)
					{
						++NumMouthFailures;
						continue;
					}
					const double Distance = Mouth.Size();
					HCJ_TEST_CHECK(Distance >= HCJ::WormholeMinDistance - Tolerance && Distance <= HCJ::WormholeMaxDistance + Tolerance, TEXT("dim %d seed %d sector %d: mouth %.1f uu from the origin"), Dim, Seed, Sector, Distance);
					HCJ_TEST_CHECK(Layout.IsLocationClear(Mouth, HCJ::WormholeRockClearance - float(Tolerance)), TEXT("dim %d seed %d sector %d: mouth too close to a rock (%d)"), Dim, Seed, Sector, 0);
					for (const FHCJPortalSpec& Portal : Layout.Portals)
					{
						const double Gap = FVector::Dist(Mouth, Portal.LocalPosition);
						HCJ_TEST_CHECK(Gap >= HCJ::WormholePortalClearance - Tolerance, TEXT("dim %d seed %d sector %d: mouth %.1f uu from a portal"), Dim, Seed, Sector, Gap);
					}
					for (const FTransform& Spawn : Layout.SpawnPoints)
					{
						const double Gap = FVector::Dist(Mouth, Spawn.GetLocation());
						HCJ_TEST_CHECK(Gap >= HCJ::WormholeSpawnClearance - Tolerance, TEXT("dim %d seed %d sector %d: mouth %.1f uu from a spawn point"), Dim, Seed, Sector, Gap);
					}
					for (const FVector& Pad : Pads)
					{
						const double Gap = FVector::Dist(Mouth, Pad);
						HCJ_TEST_CHECK(Gap >= HCJ::WormholePadClearance - Tolerance, TEXT("dim %d seed %d sector %d: mouth %.1f uu from a pad"), Dim, Seed, Sector, Gap);
					}
					if (Hole.bPresent)
					{
						const double Gap = FVector::Dist(Mouth, Hole.LocalPosition);
						HCJ_TEST_CHECK(Gap >= Holes.InfluenceRadius + HCJ::WormholeHoleMargin - Tolerance, TEXT("dim %d seed %d sector %d: mouth %.1f uu from the black hole"), Dim, Seed, Sector, Gap);
						HCJ_TEST_CHECK(FHCJBlackHole::ComputePullSpeed(Gap, Holes) == 0.0f, TEXT("dim %d seed %d sector %d: the mouth is pulled at %.0f uu/s"), Dim, Seed, Sector, FHCJBlackHole::ComputePullSpeed(Gap, Holes));
					}
				}
			}
		}
	}

	HCJ_TEST_CHECK(NumLinks > 0 && NumMouths > 0, TEXT("%d links, %d mouths tested"), NumLinks, NumMouths);
	// A mouth nearly always finds room.
	HCJ_TEST_CHECK(NumMouthFailures * 20 <= NumMouths, TEXT("%d of %d mouths found no room"), NumMouthFailures, NumMouths);

	HCJ_TEST_FINISH();
}

/** Routes over portals and wormholes: symmetric, consistent first hops, and never longer than over portals alone. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJWormholeRoutingTest, "HypeCubeJam.Wormholes.Routing", HCJWormholeTestFlags)

bool FHCJWormholeRoutingTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	int32 NumShorter = 0;
	int32 NumPairs = 0;

	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		for (int32 SeedIndex = 0; SeedIndex < 12; ++SeedIndex)
		{
			const int32 Seed = HCJTest::MakeSeed(SeedIndex);
			FHCJHypercubeGraph Graph;
			// A sparse graph (spanning tree only) is where shortcuts matter most.
			Graph.Build(Dim, Seed, (SeedIndex % 2) == 0 ? 0.0f : 0.5f);
			const TArray<FHCJWormholeLink> Links = FHCJWormholes::MakeLinks(Seed, Dim, HCJMakeWormholeSettings());

			FHCJRouteTable Routes;
			Routes.Build(Graph, Links);
			FHCJRouteTable Plain;
			Plain.Build(Graph, TConstArrayView<FHCJWormholeLink>());

			for (const FHCJWormholeLink& Link : Links)
			{
				HCJ_TEST_CHECK(Routes.GetWormholeDest(Link.SectorA) == Link.SectorB && Routes.GetWormholeDest(Link.SectorB) == Link.SectorA, TEXT("dim %d seed %d: link %d-%d is not two-way"), Dim, Seed, Link.SectorA, Link.SectorB);
				HCJ_TEST_CHECK(Routes.Distance(Link.SectorA, Link.SectorB) == 1, TEXT("dim %d seed %d: link %d-%d is %d hops"), Dim, Seed, Link.SectorA, Link.SectorB, Routes.Distance(Link.SectorA, Link.SectorB));
			}

			for (int32 From = 0; From < Graph.NumSectors(); ++From)
			{
				for (int32 To = 0; To < Graph.NumSectors(); ++To)
				{
					const int32 WithWormholes = Routes.Distance(From, To);
					const int32 PortalsOnly = Graph.ShortestDistance(From, To);
					++NumPairs;
					NumShorter += WithWormholes < PortalsOnly ? 1 : 0;

					HCJ_TEST_CHECK(Plain.Distance(From, To) == PortalsOnly, TEXT("dim %d seed %d: %d -> %d is %d hops without wormholes, the graph says %d"), Dim, Seed, From, To, Plain.Distance(From, To), PortalsOnly);
					HCJ_TEST_CHECK(WithWormholes >= 0 && WithWormholes <= PortalsOnly, TEXT("dim %d seed %d: %d -> %d is %d hops with wormholes, %d without"), Dim, Seed, From, To, WithWormholes, PortalsOnly);
					HCJ_TEST_CHECK(WithWormholes == Routes.Distance(To, From), TEXT("dim %d seed %d: %d -> %d is not symmetric (%d)"), Dim, Seed, From, To, WithWormholes);

					const int32 Hop = Routes.NextHop(From, To);
					if (From == To)
					{
						HCJ_TEST_CHECK(Hop == INDEX_NONE && WithWormholes == 0, TEXT("dim %d seed %d: sector %d to itself: hop %d"), Dim, Seed, From, Hop);
						continue;
					}
					const int32 Next = Hop == HCJ::WormholeHop ? Routes.GetWormholeDest(From) : (Hop >= 0 && Graph.IsEdgeOpen(From, Hop) ? Graph.GetNeighbor(From, Hop) : INDEX_NONE);
					HCJ_TEST_CHECK(Next != INDEX_NONE && Routes.Distance(Next, To) == WithWormholes - 1, TEXT("dim %d seed %d: %d -> %d: hop %d does not get closer"), Dim, Seed, From, To, Hop);
					// Without wormholes the table agrees with the graph's own next hop.
					HCJ_TEST_CHECK(Plain.NextHop(From, To) == Graph.NextHopDim(From, To), TEXT("dim %d seed %d: %d -> %d: plain hop %d, graph hop %d"), Dim, Seed, From, To, Plain.NextHop(From, To), Graph.NextHopDim(From, To));
				}
			}
		}
	}

	// Shortcuts do shorten something.
	HCJ_TEST_CHECK(NumShorter > 0, TEXT("no route got shorter in %d pairs (%d)"), NumPairs, NumShorter);

	FHCJRouteTable Empty;
	HCJ_TEST_CHECK(!Empty.IsBuilt() && Empty.Distance(0, 1) == INDEX_NONE && Empty.NextHop(0, 1) == INDEX_NONE && Empty.GetWormholeDest(0) == INDEX_NONE, TEXT("an unbuilt route table answered (%d)"), 0);

	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
