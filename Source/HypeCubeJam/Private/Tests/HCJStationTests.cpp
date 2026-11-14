#include "Tests/HCJTestHelpers.h"

#include "World/HCJBlackHole.h"
#include "World/HCJPickupPads.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJStations.h"
#include "World/HCJWormholes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJStationTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/**
 * Stations are placed on every machine from (match seed, layout, hole, pads, wormhole mouth, settings):
 * deterministic, in roughly the configured share of sectors, clear of portals, spawn points, pads, the wormhole
 * mouth and the influence of a black hole; no asteroid that is still built reaches into them; the layout is untouched.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJStationPlacementTest, "HypeCubeJam.Stations.Placement", HCJStationTestFlags)

bool FHCJStationPlacementTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const double Tolerance = HCJTest::DistanceTolerance;

	FHCJBlackHoleSettings Holes;
	Holes.bEnabled = true;
	Holes.SectorFraction = 0.5f;
	FHCJPickupSettings Pickups;
	Pickups.bEnabled = true;
	FHCJWormholeSettings Wormholes;
	Wormholes.bEnabled = true;

	const float Fractions[] = { 0.25f, 1.0f };
	for (const float Fraction : Fractions)
	{
		FHCJStationSettings Settings;
		Settings.bEnabled = true;
		Settings.SectorFraction = Fraction;

		int32 NumSectors = 0;
		int32 NumStations = 0;
		int32 NumSuppressed = 0;
		int32 NumAsteroids = 0;
		for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= 5; ++Dim)
		{
			for (int32 SeedIndex = 0; SeedIndex < 12; ++SeedIndex)
			{
				const int32 Seed = HCJTest::MakeSeed(SeedIndex);
				FHCJHypercubeGraph Graph;
				Graph.Build(Dim, Seed, 0.5f);
				const TArray<FHCJWormholeLink> Links = FHCJWormholes::MakeLinks(Seed, Dim, Wormholes);

				for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
				{
					const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);
					const FHCJBlackHole Hole = FHCJBlackHole::Make(Seed, Layout, Holes);
					const TArray<FVector> Pads = FHCJPickupPads::Make(Seed, Layout, Hole, Holes, Pickups);
					FVector Mouth = FVector::ZeroVector;
					bool bMouth = false;
					for (const FHCJWormholeLink& Link : Links)
					{
						if (Link.SectorA == Sector || Link.SectorB == Sector)
						{
							bMouth = FHCJWormholes::MakeMouth(Seed, Layout, Hole, Holes, Pads, Mouth);
						}
					}

					const FHCJStation Station = FHCJStation::Make(Seed, Layout, Hole, Holes, Pads, bMouth, Mouth, Settings);
					const FHCJStation Again = FHCJStation::Make(Seed, Layout, Hole, Holes, Pads, bMouth, Mouth, Settings);
					++NumSectors;
					NumAsteroids += Layout.Asteroids.Num();
					HCJ_TEST_CHECK(Station.bPresent == Again.bPresent && Station.LocalPosition == Again.LocalPosition && Station.ComputeChecksum(0) == Again.ComputeChecksum(0),
						TEXT("dim %d seed %d sector %d: two placements differ"), Dim, Seed, Sector);
					if (!Station.bPresent)
					{
						HCJ_TEST_CHECK(Station.Turrets.Num() == 0, TEXT("dim %d seed %d sector %d: %d turrets without a station"), Dim, Seed, Sector, Station.Turrets.Num());
						continue;
					}
					++NumStations;

					const double Distance = Station.LocalPosition.Size();
					HCJ_TEST_CHECK(Distance >= HCJ::StationMinDistance - Tolerance && Distance <= HCJ::StationMaxDistance + Tolerance, TEXT("dim %d seed %d sector %d: station %.1f uu from the origin"), Dim, Seed, Sector, Distance);
					for (const FHCJPortalSpec& Portal : Layout.Portals)
					{
						const double Gap = FVector::Dist(Station.LocalPosition, Portal.LocalPosition);
						HCJ_TEST_CHECK(Gap >= HCJ::StationPortalClearance - Tolerance, TEXT("dim %d seed %d sector %d: station %.1f uu from a portal"), Dim, Seed, Sector, Gap);
					}
					for (const FTransform& Spawn : Layout.SpawnPoints)
					{
						const double Gap = FVector::Dist(Station.LocalPosition, Spawn.GetLocation());
						HCJ_TEST_CHECK(Gap >= HCJ::StationSpawnClearance - Tolerance, TEXT("dim %d seed %d sector %d: station %.1f uu from a spawn point"), Dim, Seed, Sector, Gap);
					}
					for (const FVector& Pad : Pads)
					{
						const double Gap = FVector::Dist(Station.LocalPosition, Pad);
						HCJ_TEST_CHECK(Gap >= HCJ::StationPadClearance - Tolerance, TEXT("dim %d seed %d sector %d: station %.1f uu from a pad"), Dim, Seed, Sector, Gap);
					}
					if (bMouth)
					{
						const double Gap = FVector::Dist(Station.LocalPosition, Mouth);
						HCJ_TEST_CHECK(Gap >= HCJ::StationWormholeClearance - Tolerance, TEXT("dim %d seed %d sector %d: station %.1f uu from the wormhole"), Dim, Seed, Sector, Gap);
					}
					if (Hole.bPresent)
					{
						const double Gap = FVector::Dist(Station.LocalPosition, Hole.LocalPosition);
						HCJ_TEST_CHECK(Gap >= Holes.InfluenceRadius + HCJ::StationHoleMargin - Tolerance, TEXT("dim %d seed %d sector %d: station %.1f uu from the black hole"), Dim, Seed, Sector, Gap);
					}

					// Turrets: 3 to 6 different mounts of the shape, each outside the body and inside the body radius.
					HCJ_TEST_CHECK(Station.Turrets.Num() >= HCJ::StationMinTurrets && Station.Turrets.Num() <= HCJ::StationMaxTurrets, TEXT("dim %d seed %d sector %d: %d turrets"), Dim, Seed, Sector, Station.Turrets.Num());
					for (int32 Index = 0; Index < Station.Turrets.Num(); ++Index)
					{
						const FHCJTurretMount& Mount = Station.Turrets[Index];
						HCJ_TEST_CHECK(Mount.Location.Size() + HCJ::TurretMuzzleOffset <= HCJ::StationBodyRadius && FMath::IsNearlyEqual(Mount.Up.Size(), 1.0, 1.0e-6),
							TEXT("dim %d seed %d sector %d: turret %d sits %.1f uu out"), Dim, Seed, Sector, Index, Mount.Location.Size());
						for (int32 Other = 0; Other < Index; ++Other)
						{
							HCJ_TEST_CHECK(Mount.Location != Station.Turrets[Other].Location, TEXT("dim %d seed %d sector %d: turrets %d and %d share a mount"), Dim, Seed, Sector, Other, Index);
						}
						for (const FHCJStationBox& Box : FHCJStation::GetBodyBoxes())
						{
							const FVector Delta = (Mount.Location - Box.Center).GetAbs() - Box.HalfExtent;
							const double Outside = FVector(FMath::Max(Delta.X, 0.0), FMath::Max(Delta.Y, 0.0), FMath::Max(Delta.Z, 0.0)).Size();
							HCJ_TEST_CHECK(Outside >= HCJ::TurretRadius - Tolerance, TEXT("dim %d seed %d sector %d: turret %d is %.1f uu from the body"), Dim, Seed, Sector, Index, Outside);
						}
					}

					// What is left of the asteroid field stays clear of the station; what is removed is near it.
					for (const FHCJAsteroid& Asteroid : Layout.Asteroids)
					{
						const double Gap = FVector::Dist(Asteroid.LocalPosition, Station.LocalPosition) - Asteroid.BoundingRadius;
						const bool bSuppressed = Station.IsAsteroidSuppressed(Asteroid);
						NumSuppressed += bSuppressed ? 1 : 0;
						HCJ_TEST_CHECK(bSuppressed == (Gap < HCJ::StationRockFreeRadius), TEXT("dim %d seed %d sector %d: asteroid %.1f uu from the station, suppressed %d"), Dim, Seed, Sector, Gap, bSuppressed ? 1 : 0);
					}
					HCJ_TEST_CHECK(HCJ::StationRockFreeRadius >= HCJ::StationBodyRadius + 500.0f, TEXT("rock free radius %.0f does not cover the body radius %.0f"), HCJ::StationRockFreeRadius, HCJ::StationBodyRadius);

					HCJ_TEST_CHECK(Layout.ComputeChecksum() == FHCJSectorLayout::Generate(Seed, Sector, Graph).ComputeChecksum(), TEXT("dim %d seed %d sector %d: layout changed"), Dim, Seed, Sector);
				}
			}
		}

		const double Share = double(NumStations) / NumSectors;
		HCJ_TEST_CHECK(Share <= Fraction + 0.05 && Share >= 0.7 * Fraction, TEXT("%.3f of %d sectors have a station, configured %.2f"), Share, NumSectors, Fraction);
		// The filter takes a small part of the rocks only.
		HCJ_TEST_CHECK(NumSuppressed * 4 < NumAsteroids, TEXT("%d of %d asteroids suppressed"), NumSuppressed, NumAsteroids);
	}

	FHCJStationSettings Off;
	Off.SectorFraction = 1.0f;
	FHCJHypercubeGraph Graph;
	Graph.Build(4, 1234, 0.5f);
	const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(1234, 3, Graph);
	HCJ_TEST_CHECK(!FHCJStation::Make(1234, Layout, FHCJBlackHole(), Holes, TConstArrayView<FVector>(), false, FVector::ZeroVector, Off).bPresent, TEXT("a station although stations are off (%d)"), 0);
	HCJ_TEST_CHECK(!FHCJStation().IsAsteroidSuppressed(Layout.Asteroids[0]), TEXT("no station suppresses an asteroid (%d)"), 0);

	HCJ_TEST_FINISH();
}

/** Lead aim: a projectile fired at the returned point meets the target; unreachable targets are reported. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJLeadAimTest, "HypeCubeJam.Stations.LeadAim", HCJStationTestFlags)

bool FHCJLeadAimTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	FRandomStream Stream(20261114);

	for (int32 Case = 0; Case < 2000; ++Case)
	{
		const FVector Shooter = Stream.VRand() * Stream.FRandRange(0.0f, 5000.0f);
		const FVector TargetLocation = Shooter + Stream.VRand() * Stream.FRandRange(500.0f, 9000.0f);
		const float Speed = Stream.FRandRange(8000.0f, 30000.0f);
		// Ships fly at up to 9000 uu/s (boost with overdrive); sometimes faster than the projectile.
		const FVector Velocity = Stream.VRand() * Stream.FRandRange(0.0f, (Case % 10) == 0 ? 2.0f * Speed : 0.9f * Speed);

		FVector AimPoint;
		const bool bHit = HCJ::ComputeLeadAim(Shooter, TargetLocation, Velocity, Speed, AimPoint);
		if (bHit)
		{
			// Time for the projectile to reach the aim point == time for the target to get there.
			const double ProjectileTime = FVector::Dist(Shooter, AimPoint) / Speed;
			const FVector TargetThen = TargetLocation + Velocity * ProjectileTime;
			HCJ_TEST_CHECK(ProjectileTime > 0.0 && FVector::Dist(TargetThen, AimPoint) < 1.0, TEXT("case %d: the target is %.2f uu from the aim point when the shot arrives"), Case, FVector::Dist(TargetThen, AimPoint));
		}
		else
		{
			HCJ_TEST_CHECK(AimPoint == TargetLocation, TEXT("case %d: no intercept, but the aim point is not the target (%d)"), Case, 0);
			// Only a target at least as fast as the projectile can be out of reach.
			HCJ_TEST_CHECK(Velocity.Size() >= Speed * 0.999, TEXT("case %d: no intercept although the target does %.0f and the projectile %.0f"), Case, Velocity.Size(), Speed);
		}
	}

	// A target at rest is aimed at directly; one flying straight away faster than the projectile is never caught.
	FVector AimPoint;
	HCJ_TEST_CHECK(HCJ::ComputeLeadAim(FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), FVector::ZeroVector, 14000.0f, AimPoint) && AimPoint == FVector(1000.0, 0.0, 0.0), TEXT("target at rest: aim point x %.1f"), AimPoint.X);
	HCJ_TEST_CHECK(!HCJ::ComputeLeadAim(FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0), 14000.0f, AimPoint), TEXT("a fleeing faster target was caught (%d)"), 1);
	HCJ_TEST_CHECK(!HCJ::ComputeLeadAim(FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), FVector::ZeroVector, 0.0f, AimPoint), TEXT("a projectile without speed hit (%d)"), 1);
	// Crossing target: the lead is ahead of it, along its velocity.
	HCJ_TEST_CHECK(HCJ::ComputeLeadAim(FVector::ZeroVector, FVector(5000.0, 0.0, 0.0), FVector(0.0, 3000.0, 0.0), 14000.0f, AimPoint) && AimPoint.Y > 500.0 && FMath::IsNearlyEqual(AimPoint.X, 5000.0, 0.01), TEXT("crossing target: aim point y %.1f"), AimPoint.Y);

	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
