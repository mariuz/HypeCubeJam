#include "Tests/HCJTestHelpers.h"

#include "World/HCJSectorLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJSectorTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	constexpr int32 HCJSectorTestSeeds = 10;

	/** Bit-exact comparison: server and clients must generate the very same numbers. */
	bool HCJLayoutsIdentical(const FHCJSectorLayout& A, const FHCJSectorLayout& B)
	{
		if (A.SectorId != B.SectorId || A.Seed != B.Seed || A.Theme != B.Theme || !(A.Tint == B.Tint)
			|| A.Asteroids.Num() != B.Asteroids.Num() || A.Portals.Num() != B.Portals.Num() || A.SpawnPoints.Num() != B.SpawnPoints.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Asteroids.Num(); ++Index)
		{
			const FHCJAsteroid& X = A.Asteroids[Index];
			const FHCJAsteroid& Y = B.Asteroids[Index];
			if (X.Shape != Y.Shape || X.BoundingRadius != Y.BoundingRadius || !(X.LocalPosition == Y.LocalPosition)
				|| !(X.Scale3D == Y.Scale3D) || !(X.Rotation == Y.Rotation))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.Portals.Num(); ++Index)
		{
			const FHCJPortalSpec& X = A.Portals[Index];
			const FHCJPortalSpec& Y = B.Portals[Index];
			if (X.Dim != Y.Dim || X.DestSector != Y.DestSector || !(X.LocalPosition == Y.LocalPosition))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.SpawnPoints.Num(); ++Index)
		{
			const FTransform& X = A.SpawnPoints[Index];
			const FTransform& Y = B.SpawnPoints[Index];
			if (!(X.GetLocation() == Y.GetLocation()) || !(X.GetRotation() == Y.GetRotation()) || !(X.GetScale3D() == Y.GetScale3D()))
			{
				return false;
			}
		}
		return true;
	}

	void HCJGenerateAllLayouts(int32 Seed, const FHCJHypercubeGraph& Graph, TArray<FHCJSectorLayout>& OutLayouts)
	{
		OutLayouts.Reset(Graph.NumSectors());
		for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
		{
			OutLayouts.Add(FHCJSectorLayout::Generate(Seed, Sector, Graph));
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJSectorDeterminismTest, "HypeCubeJam.Sector.Determinism", HCJSectorTestFlags)

bool FHCJSectorDeterminismTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		for (int32 SeedIndex = 0; SeedIndex < HCJSectorTestSeeds; ++SeedIndex)
		{
			const int32 Seed = HCJTest::MakeSeed(SeedIndex);

			FHCJHypercubeGraph GraphA;
			GraphA.Build(Dim, Seed, FHCJHypercubeGraph::DefaultEdgeProbability);
			// A second, independently built graph: this is what another machine would have.
			FHCJHypercubeGraph GraphB;
			GraphB.Build(Dim, Seed, FHCJHypercubeGraph::DefaultEdgeProbability);

			for (int32 Sector = 0; Sector < GraphA.NumSectors(); ++Sector)
			{
				const FHCJSectorLayout First = FHCJSectorLayout::Generate(Seed, Sector, GraphA);
				const FHCJSectorLayout Second = FHCJSectorLayout::Generate(Seed, Sector, GraphB);

				HCJ_TEST_CHECK(First.SectorId == Sector && First.Seed == FHCJSectorLayout::MakeSectorSeed(Seed, Sector),
					TEXT("dim %d seed %d sector %d: layout carries id %d / seed %d"), Dim, Seed, Sector, First.SectorId, First.Seed);
				HCJ_TEST_CHECK(HCJLayoutsIdentical(First, Second), TEXT("dim %d seed %d sector %d: two generations differ"), Dim, Seed, Sector);

				// Another match seed gives another sector.
				const FHCJSectorLayout Other = FHCJSectorLayout::Generate(Seed ^ 0x13579BDF, Sector, GraphA);
				HCJ_TEST_CHECK(!HCJLayoutsIdentical(First, Other), TEXT("dim %d seed %d sector %d: a different match seed gave the same layout"), Dim, Seed, Sector);
			}

			// Sectors of one match are not copies of each other.
			const FHCJSectorLayout Sector0 = FHCJSectorLayout::Generate(Seed, 0, GraphA);
			FHCJSectorLayout Sector1 = FHCJSectorLayout::Generate(Seed, 1, GraphA);
			HCJ_TEST_CHECK(Sector0.Seed != Sector1.Seed && Sector0.Asteroids.Num() > 0 && Sector1.Asteroids.Num() > 0
				&& !(Sector0.Asteroids[0].LocalPosition == Sector1.Asteroids[0].LocalPosition),
				TEXT("dim %d seed %d: sectors 0 and 1 look identical (seeds %d, %d)"), Dim, Seed, Sector0.Seed, Sector1.Seed);

			// Invalid input gives the empty layout.
			const FHCJSectorLayout Invalid[] = {
				FHCJSectorLayout::Generate(Seed, -1, GraphA),
				FHCJSectorLayout::Generate(Seed, GraphA.NumSectors(), GraphA),
				FHCJSectorLayout::Generate(Seed, 0, FHCJHypercubeGraph())
			};
			for (const FHCJSectorLayout& Layout : Invalid)
			{
				HCJ_TEST_CHECK(Layout.SectorId == INDEX_NONE && Layout.Asteroids.Num() == 0 && Layout.Portals.Num() == 0 && Layout.SpawnPoints.Num() == 0,
					TEXT("dim %d seed %d: invalid input produced a non-empty layout (id %d)"), Dim, Seed, Layout.SectorId);
			}
		}
	}

	HCJ_TEST_FINISH();
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJSectorPortalsTest, "HypeCubeJam.Sector.Portals", HCJSectorTestFlags)

bool FHCJSectorPortalsTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	const double MinJitterCosine = FMath::Cos(FMath::DegreesToRadians(double(HCJ::PortalJitterDegrees) + 0.05));
	const float Probabilities[] = { 0.0f, 0.5f, 1.0f };
	TArray<FHCJSectorLayout> Layouts;

	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		for (const float Probability : Probabilities)
		{
			for (int32 SeedIndex = 0; SeedIndex < HCJSectorTestSeeds; ++SeedIndex)
			{
				const int32 Seed = HCJTest::MakeSeed(SeedIndex);
				FHCJHypercubeGraph Graph;
				Graph.Build(Dim, Seed, Probability);
				HCJGenerateAllLayouts(Seed, Graph, Layouts);

				for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
				{
					const FHCJSectorLayout& Layout = Layouts[Sector];
					const FHCJDimArray OpenDims = Graph.GetOpenDims(Sector);

					HCJ_TEST_CHECK(Layout.Portals.Num() == OpenDims.Num() && Layout.Portals.Num() >= 1,
						TEXT("dim %d seed %d sector %d: %d portals for %d open edges"), Dim, Seed, Sector, Layout.Portals.Num(), OpenDims.Num());

					for (int32 Index = 0; Index < Layout.Portals.Num(); ++Index)
					{
						HCJ_TEST_CHECK(Index >= OpenDims.Num() || Layout.Portals[Index].Dim == OpenDims[Index],
							TEXT("dim %d seed %d sector %d: portal %d is for dimension %d, not sorted like the open dimensions"), Dim, Seed, Sector, Index, Layout.Portals[Index].Dim);
						for (int32 Other = Index + 1; Other < Layout.Portals.Num(); ++Other)
						{
							const double Separation = FVector::Dist(Layout.Portals[Index].LocalPosition, Layout.Portals[Other].LocalPosition);
							HCJ_TEST_CHECK(Separation > 2.0 * HCJ::PortalClearRadius,
								TEXT("dim %d seed %d sector %d: portals %d and %d are only %.0f uu apart"), Dim, Seed, Sector, Index, Other, Separation);
						}
					}

					for (int32 EdgeDim = 0; EdgeDim < Dim; ++EdgeDim)
					{
						const int32 Neighbor = Sector ^ (1 << EdgeDim);
						const FVector Position = FHCJSectorLayout::ComputePortalLocalPosition(Seed, Sector, EdgeDim);
						const FVector Mirror = FHCJSectorLayout::ComputePortalLocalPosition(Seed, Neighbor, EdgeDim);

						// Mirror rule, open or closed, bit-exact.
						HCJ_TEST_CHECK(Position == -Mirror, TEXT("dim %d seed %d: portal positions of edge (%d, %d) are not point reflections: %s vs %s"),
							Dim, Seed, Sector, EdgeDim, *Position.ToString(), *Mirror.ToString());

						const double Distance = Position.Size();
						HCJ_TEST_CHECK(Distance >= HCJ::PortalMinDistance - HCJTest::DistanceTolerance && Distance <= HCJ::PortalMaxDistance + HCJTest::DistanceTolerance,
							TEXT("dim %d seed %d: portal (%d, %d) is %.1f uu from the sector origin"), Dim, Seed, Sector, EdgeDim, Distance);

						const double Sign = (Sector & (1 << EdgeDim)) == 0 ? 1.0 : -1.0;
						const double Cosine = FVector::DotProduct(Position.GetSafeNormal(), FHCJHypercubeGraph::GetDimensionDirection(EdgeDim) * Sign);
						HCJ_TEST_CHECK(Cosine >= MinJitterCosine, TEXT("dim %d seed %d: portal (%d, %d) strays from its dimension direction (cos %.4f)"), Dim, Seed, Sector, EdgeDim, Cosine);

						const FHCJPortalSpec* Spec = Layout.FindPortal(EdgeDim);
						const FHCJPortalSpec* NeighborSpec = Layouts[Neighbor].FindPortal(EdgeDim);
						const bool bOpen = Graph.IsEdgeOpen(Sector, EdgeDim);

						HCJ_TEST_CHECK((Spec != nullptr) == bOpen, TEXT("dim %d seed %d: edge (%d, %d) open=%d but portal present=%d"), Dim, Seed, Sector, EdgeDim, int32(bOpen), int32(Spec != nullptr));
						HCJ_TEST_CHECK((NeighborSpec != nullptr) == bOpen, TEXT("dim %d seed %d: edge (%d, %d) open=%d but far-end portal present=%d"), Dim, Seed, Sector, EdgeDim, int32(bOpen), int32(NeighborSpec != nullptr));
						if (!Spec || !NeighborSpec)
						{
							continue;
						}

						HCJ_TEST_CHECK(Spec->Dim == EdgeDim && Spec->DestSector == Neighbor && NeighborSpec->DestSector == Sector,
							TEXT("dim %d seed %d: portal (%d, %d) leads to %d and back to %d"), Dim, Seed, Sector, EdgeDim, Spec->DestSector, NeighborSpec->DestSector);
						HCJ_TEST_CHECK(Spec->LocalPosition == Position, TEXT("dim %d seed %d: portal (%d, %d) is not at ComputePortalLocalPosition"), Dim, Seed, Sector, EdgeDim);
						HCJ_TEST_CHECK(Spec->LocalPosition == -NeighborSpec->LocalPosition, TEXT("dim %d seed %d: the two portals of edge (%d, %d) do not mirror"), Dim, Seed, Sector, EdgeDim);

						// A ship leaving along the outward direction keeps that velocity and so flies INTO the destination sector.
						const double InwardCosine = FVector::DotProduct(Spec->GetOutwardDirection(), -NeighborSpec->GetOutwardDirection());
						HCJ_TEST_CHECK(InwardCosine > 0.9999, TEXT("dim %d seed %d: leaving through (%d, %d) does not head into sector %d (cos %.4f)"), Dim, Seed, Sector, EdgeDim, Neighbor, InwardCosine);

						// The teleport translation is the same vector seen from either end, negated.
						const FVector Delta = (FHCJHypercubeGraph::GetSectorWorldOrigin(Neighbor) + NeighborSpec->LocalPosition)
							- (FHCJHypercubeGraph::GetSectorWorldOrigin(Sector) + Spec->LocalPosition);
						const FVector BackDelta = (FHCJHypercubeGraph::GetSectorWorldOrigin(Sector) + Spec->LocalPosition)
							- (FHCJHypercubeGraph::GetSectorWorldOrigin(Neighbor) + NeighborSpec->LocalPosition);
						HCJ_TEST_CHECK((Delta + BackDelta).IsNearlyZero(1.0e-3), TEXT("dim %d seed %d: teleport deltas of edge (%d, %d) do not cancel (%s)"), Dim, Seed, Sector, EdgeDim, *(Delta + BackDelta).ToString());
					}
				}
			}
		}
	}

	HCJ_TEST_FINISH();
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJSectorClearancesTest, "HypeCubeJam.Sector.Clearances", HCJSectorTestFlags)

bool FHCJSectorClearancesTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	const double Tolerance = HCJTest::DistanceTolerance;
	int32 ThemeCounts[int32(EHCJSectorTheme::Count)] = {};
	int32 ShapeCounts[int32(EHCJAsteroidShape::Count)] = {};
	TArray<FHCJSectorLayout> Layouts;

	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		for (int32 SeedIndex = 0; SeedIndex < HCJSectorTestSeeds; ++SeedIndex)
		{
			const int32 Seed = HCJTest::MakeSeed(SeedIndex);
			FHCJHypercubeGraph Graph;
			// Alternate between sparse and full connectivity: more portals leave less room.
			Graph.Build(Dim, Seed, (SeedIndex % 2) == 0 ? 0.5f : 1.0f);
			HCJGenerateAllLayouts(Seed, Graph, Layouts);

			for (const FHCJSectorLayout& Layout : Layouts)
			{
				const int32 Sector = Layout.SectorId;

				// ---- theme and tint ----
				const int32 ThemeIndex = int32(Layout.Theme);
				HCJ_TEST_CHECK(ThemeIndex >= 0 && ThemeIndex < int32(EHCJSectorTheme::Count), TEXT("dim %d seed %d sector %d: invalid theme %d"), Dim, Seed, Sector, ThemeIndex);
				if (ThemeIndex >= 0 && ThemeIndex < int32(EHCJSectorTheme::Count))
				{
					++ThemeCounts[ThemeIndex];
				}
				const float MaxChannel = FMath::Max3(Layout.Tint.R, Layout.Tint.G, Layout.Tint.B);
				const float MinChannel = FMath::Min3(Layout.Tint.R, Layout.Tint.G, Layout.Tint.B);
				HCJ_TEST_CHECK(MaxChannel >= 0.8f && MaxChannel <= 1.0f && MinChannel >= 0.0f && (MaxChannel - MinChannel) >= 0.6f * MaxChannel && Layout.Tint.A == 1.0f,
					TEXT("dim %d seed %d sector %d: tint %s is not saturated and bright"), Dim, Seed, Sector, *Layout.Tint.ToString());

				// ---- asteroids ----
				HCJ_TEST_CHECK(Layout.Asteroids.Num() >= 50, TEXT("dim %d seed %d sector %d: only %d asteroids"), Dim, Seed, Sector, Layout.Asteroids.Num());
				for (int32 Index = 0; Index < Layout.Asteroids.Num(); ++Index)
				{
					const FHCJAsteroid& Asteroid = Layout.Asteroids[Index];
					const int32 ShapeIndex = int32(Asteroid.Shape);
					HCJ_TEST_CHECK(ShapeIndex >= 0 && ShapeIndex < int32(EHCJAsteroidShape::Count), TEXT("dim %d seed %d sector %d: asteroid %d has invalid shape %d"), Dim, Seed, Sector, Index, ShapeIndex);
					if (ShapeIndex >= 0 && ShapeIndex < int32(EHCJAsteroidShape::Count))
					{
						++ShapeCounts[ShapeIndex];
					}

					HCJ_TEST_CHECK(Asteroid.Scale3D.GetMin() > 0.0 && Asteroid.BoundingRadius > 0.0f && Asteroid.Rotation.IsNormalized(),
						TEXT("dim %d seed %d sector %d: asteroid %d has a degenerate transform (scale %s)"), Dim, Seed, Sector, Index, *Asteroid.Scale3D.ToString());
					HCJ_TEST_CHECK(Asteroid.BoundingRadius == FHCJAsteroid::ComputeBoundingRadius(Asteroid.Shape, Asteroid.Scale3D),
						TEXT("dim %d seed %d sector %d: asteroid %d bounding radius %.2f does not match its scale"), Dim, Seed, Sector, Index, Asteroid.BoundingRadius);
					if (Asteroid.Shape == EHCJAsteroidShape::Sphere)
					{
						HCJ_TEST_CHECK(Asteroid.Scale3D.X == Asteroid.Scale3D.Y && Asteroid.Scale3D.Y == Asteroid.Scale3D.Z,
							TEXT("dim %d seed %d sector %d: sphere asteroid %d is not uniformly scaled (%s)"), Dim, Seed, Sector, Index, *Asteroid.Scale3D.ToString());
					}

					const double Extent = Asteroid.LocalPosition.Size() + Asteroid.BoundingRadius;
					HCJ_TEST_CHECK(Extent <= HCJ::SectorFieldRadius + Tolerance, TEXT("dim %d seed %d sector %d: asteroid %d reaches %.1f uu, outside the field"), Dim, Seed, Sector, Index, Extent);

					for (const FHCJPortalSpec& Portal : Layout.Portals)
					{
						const double Gap = FVector::Dist(Asteroid.LocalPosition, Portal.LocalPosition) - Asteroid.BoundingRadius;
						HCJ_TEST_CHECK(Gap >= HCJ::PortalClearRadius - Tolerance, TEXT("dim %d seed %d sector %d: asteroid %d is %.1f uu from the portal of dimension %d"), Dim, Seed, Sector, Index, Gap, Portal.Dim);
					}

					// IsLocationClear agrees with the bounding sphere.
					HCJ_TEST_CHECK(!Layout.IsLocationClear(Asteroid.LocalPosition, 0.0f), TEXT("dim %d seed %d sector %d: the centre of asteroid %d counts as clear"), Dim, Seed, Sector, Index);
				}
				HCJ_TEST_CHECK(Layout.IsLocationClear(FVector(0.0, 0.0, 10.0 * HCJ::SectorFieldRadius), 1000.0f), TEXT("dim %d seed %d sector %d: a point far outside the field is not clear"), Dim, Seed, Sector);

				// ---- spawn points ----
				HCJ_TEST_CHECK(Layout.SpawnPoints.Num() == HCJ::NumSpawnPoints, TEXT("dim %d seed %d sector %d: %d spawn points"), Dim, Seed, Sector, Layout.SpawnPoints.Num());
				for (int32 Index = 0; Index < Layout.SpawnPoints.Num(); ++Index)
				{
					const FTransform& Spawn = Layout.SpawnPoints[Index];
					const FVector Location = Spawn.GetLocation();
					const double Distance = Location.Size();

					HCJ_TEST_CHECK(Distance >= HCJ::SpawnMinDistance - Tolerance && Distance <= HCJ::SpawnMaxDistance + Tolerance,
						TEXT("dim %d seed %d sector %d: spawn point %d is %.1f uu from the origin"), Dim, Seed, Sector, Index, Distance);
					HCJ_TEST_CHECK(Spawn.GetScale3D().Equals(FVector::OneVector, 1.0e-6), TEXT("dim %d seed %d sector %d: spawn point %d has scale %s"), Dim, Seed, Sector, Index, *Spawn.GetScale3D().ToString());

					// Not inside (or near) an asteroid: checked both through the API and by hand.
					HCJ_TEST_CHECK(Layout.IsLocationClear(Location, HCJ::SpawnClearance - float(Tolerance)), TEXT("dim %d seed %d sector %d: spawn point %d is not clear of asteroids"), Dim, Seed, Sector, Index);
					for (int32 AsteroidIndex = 0; AsteroidIndex < Layout.Asteroids.Num(); ++AsteroidIndex)
					{
						const FHCJAsteroid& Asteroid = Layout.Asteroids[AsteroidIndex];
						const double Gap = FVector::Dist(Location, Asteroid.LocalPosition) - Asteroid.BoundingRadius;
						HCJ_TEST_CHECK(Gap >= HCJ::SpawnClearance - Tolerance, TEXT("dim %d seed %d sector %d: spawn point %d is %.1f uu from asteroid %d"), Dim, Seed, Sector, Index, Gap, AsteroidIndex);
					}

					for (const FHCJPortalSpec& Portal : Layout.Portals)
					{
						const double Gap = FVector::Dist(Location, Portal.LocalPosition);
						HCJ_TEST_CHECK(Gap >= HCJ::PortalClearRadius - Tolerance, TEXT("dim %d seed %d sector %d: spawn point %d is %.1f uu from the portal of dimension %d"), Dim, Seed, Sector, Index, Gap, Portal.Dim);
					}

					for (int32 Other = Index + 1; Other < Layout.SpawnPoints.Num(); ++Other)
					{
						const double Gap = FVector::Dist(Location, Layout.SpawnPoints[Other].GetLocation());
						HCJ_TEST_CHECK(Gap >= HCJ::SpawnSeparation - Tolerance, TEXT("dim %d seed %d sector %d: spawn points %d and %d are %.1f uu apart"), Dim, Seed, Sector, Index, Other, Gap);
					}

					// Faces the sector origin without roll.
					const double FacingCosine = FVector::DotProduct(Spawn.GetRotation().GetForwardVector(), -Location.GetSafeNormal());
					// No roll <=> the right vector stays horizontal (robust near straight up/down, unlike reading back a rotator).
					const double RightZ = FMath::Abs(Spawn.GetRotation().GetRightVector().Z);
					HCJ_TEST_CHECK(FacingCosine > 0.9999 && RightZ < 1.0e-3, TEXT("dim %d seed %d sector %d: spawn point %d does not face the origin level (cos %.5f, right.z %.5f)"), Dim, Seed, Sector, Index, FacingCosine, RightZ);
				}
			}
		}
	}

	// Over ~1200 sectors every theme and every shape must have shown up.
	for (int32 ThemeIndex = 0; ThemeIndex < int32(EHCJSectorTheme::Count); ++ThemeIndex)
	{
		HCJ_TEST_CHECK(ThemeCounts[ThemeIndex] > 0, TEXT("theme %d was never generated"), ThemeIndex);
	}
	for (int32 ShapeIndex = 0; ShapeIndex < int32(EHCJAsteroidShape::Count); ++ShapeIndex)
	{
		HCJ_TEST_CHECK(ShapeCounts[ShapeIndex] > 0, TEXT("asteroid shape %d was never generated"), ShapeIndex);
	}

	HCJ_TEST_FINISH();
}

// ---------------------------------------------------------------------------------------------------------------------

namespace
{
	/** FNV-1a over 64-bit words: fixed integer arithmetic, so the digest is the same on every platform and compiler. */
	struct FHCJGoldenHash
	{
		uint64 State = 0xCBF29CE484222325ull;

		void Add(int64 Value)
		{
			uint64 Bits = static_cast<uint64>(Value);
			for (int32 Byte = 0; Byte < 8; ++Byte)
			{
				State ^= Bits & 0xFFull;
				State *= 0x100000001B3ull;
				Bits >>= 8;
			}
		}

		/** Positions are quantised to 1 uu: harmless last-bit noise passes, a diverging accept/reject decision does not. */
		void AddPosition(const FVector& Position)
		{
			Add(FMath::RoundToInt64(Position.X));
			Add(FMath::RoundToInt64(Position.Y));
			Add(FMath::RoundToInt64(Position.Z));
		}
	};

	/** Digest of everything the network contract covers: open edges and all generated positions of a match. */
	uint64 HCJHashUniverse(int32 Seed, int32 Dim)
	{
		FHCJHypercubeGraph Graph;
		Graph.Build(Dim, Seed, FHCJHypercubeGraph::DefaultEdgeProbability);

		FHCJGoldenHash Hash;
		Hash.Add(Graph.NumSectors());
		for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
		{
			const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);

			Hash.Add(Graph.GetOpenDimMask(Sector));
			Hash.Add(int64(Layout.Theme));

			Hash.Add(Layout.Portals.Num());
			for (const FHCJPortalSpec& Portal : Layout.Portals)
			{
				Hash.Add(Portal.Dim);
				Hash.Add(Portal.DestSector);
				Hash.AddPosition(Portal.LocalPosition);
			}

			Hash.Add(Layout.Asteroids.Num());
			for (const FHCJAsteroid& Asteroid : Layout.Asteroids)
			{
				Hash.Add(int64(Asteroid.Shape));
				Hash.AddPosition(Asteroid.LocalPosition);
			}

			Hash.Add(Layout.SpawnPoints.Num());
			for (const FTransform& Spawn : Layout.SpawnPoints)
			{
				Hash.AddPosition(Spawn.GetLocation());
			}
		}
		return Hash.State;
	}
}

/**
 * Golden values: the determinism test above generates twice in one process and so cannot see a platform, compiler
 * or C runtime that generates something else. These digests were recorded on Win64 / MSVC; any change to the
 * generation algorithm (which is part of the network contract) must update them deliberately.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJSectorGoldenTest, "HypeCubeJam.Sector.Golden", HCJSectorTestFlags)

bool FHCJSectorGoldenTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	struct FGoldenCase
	{
		int32 Seed;
		int32 Dim;
		uint64 Expected;
	};
	const FGoldenCase Cases[] = {
		{ 0, 2, 0x1643EBE3CAC28905ull },
		{ 1, 3, 0x3CC11B82976302EEull },
		{ 1234, 4, 0x19CDF8CC017E1107ull },
		{ -559038737, 4, 0x2EB494ABE1F4E0B4ull },
		{ 20261114, 5, 0xF1EEA3C47132A6A5ull },
		{ MAX_int32, 6, 0xC0801BF1F3945F83ull },
	};

	for (const FGoldenCase& Case : Cases)
	{
		const uint64 Actual = HCJHashUniverse(Case.Seed, Case.Dim);
		HCJ_TEST_CHECK(Actual == Case.Expected, TEXT("seed %d dim %d: universe digest is 0x%016llXull, expected 0x%016llXull"),
			Case.Seed, Case.Dim, Actual, Case.Expected);
	}

	HCJ_TEST_FINISH();
}

// ---------------------------------------------------------------------------------------------------------------------

/**
 * Rock meshes are drawn over the asteroids without touching generation: the variant comes from a hash, the
 * instance transform from the asteroid and the mesh bounds. Both are pure functions and must stay inside the
 * documented envelope (bounding sphere + HCJ::MaxVisualOverhang) so that spawn points stay clear of what is drawn.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJSectorRockVisualsTest, "HypeCubeJam.Sector.RockVisuals", HCJSectorTestFlags)

bool FHCJSectorRockVisualsTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	// ---- variant selection ----
	// Recorded values (the same integer arithmetic as HCJ::MixSeed on every platform).
	const int32 Expected1234[] = { 2, 0, 3, 2, 2, 2, 2, 0, 5, 5, 0, 0 };
	const int32 ExpectedDeadBeef[] = { 2, 2, 3, 2, 0, 1, 3, 1, 5, 5, 5, 2 };
	const int32 ExpectedZero[] = { 2, 0, 1, 0, 0, 2, 1, 3, 1, 2, 0, 3 };
	for (int32 Index = 0; Index < 12; ++Index)
	{
		HCJ_TEST_CHECK(HCJ::PickAsteroidVariant(1234, Index, 6) == Expected1234[Index], TEXT("variant of (1234, %d, 6) is %d"), Index, HCJ::PickAsteroidVariant(1234, Index, 6));
		HCJ_TEST_CHECK(HCJ::PickAsteroidVariant(-559038737, Index, 6) == ExpectedDeadBeef[Index], TEXT("variant of (-559038737, %d, 6) is %d"), Index, HCJ::PickAsteroidVariant(-559038737, Index, 6));
		HCJ_TEST_CHECK(HCJ::PickAsteroidVariant(0, Index, 4) == ExpectedZero[Index], TEXT("variant of (0, %d, 4) is %d"), Index, HCJ::PickAsteroidVariant(0, Index, 4));
	}

	for (int32 SeedIndex = 0; SeedIndex < HCJSectorTestSeeds; ++SeedIndex)
	{
		const int32 Seed = HCJTest::MakeSeed(SeedIndex);
		for (int32 NumVariants = -1; NumVariants <= 8; ++NumVariants)
		{
			int32 Counts[8] = {};
			for (int32 Index = 0; Index < 400; ++Index)
			{
				const int32 Variant = HCJ::PickAsteroidVariant(Seed, Index, NumVariants);
				HCJ_TEST_CHECK(Variant >= 0 && Variant < FMath::Max(NumVariants, 1), TEXT("seed %d: variant %d of asteroid %d is outside [0, %d)"), Seed, Variant, Index, NumVariants);
				HCJ_TEST_CHECK(Variant == HCJ::PickAsteroidVariant(Seed, Index, NumVariants), TEXT("seed %d: asteroid %d picked two different variants of %d"), Seed, Index, NumVariants);
				if (Variant >= 0 && Variant < 8)
				{
					++Counts[Variant];
				}
			}
			// 400 asteroids over at most 8 variants: every variant shows up with a healthy share.
			for (int32 Variant = 0; Variant < NumVariants; ++Variant)
			{
				HCJ_TEST_CHECK(Counts[Variant] >= 400 / (3 * NumVariants), TEXT("seed %d: variant %d of %d was picked only %d times in 400"), Seed, Variant, NumVariants, Counts[Variant]);
			}
		}
	}

	// ---- instance transforms ----
	struct FMeshCase
	{
		FBox Bounds;
		float CornerFactor;
	};
	const FMeshCase Meshes[] = {
		{ FBox(FVector(-6.74, -5.05, -5.35), FVector(6.74, 5.05, 5.35)), 1.37f },		// a centred rock
		{ FBox(FVector(-8.11, -5.76, -1.72), FVector(6.22, 5.75, 1.53)), 1.60f },		// a flat, off-centre slab
		{ FBox(FVector(0.0, -40.0, 10.0), FVector(1100.0, 40.0, 90.0)), 1.0f },			// pivot far outside the mesh
		{ FBox(FVector(-50.0), FVector(50.0)), 1.7320508f },								// a cube
	};
	const float Fills[] = { 0.8f, 1.0f, 1.12f, 1.5f };
	const double Tolerance = HCJTest::DistanceTolerance;

	for (int32 SeedIndex = 0; SeedIndex < HCJSectorTestSeeds; ++SeedIndex)
	{
		const int32 Seed = HCJTest::MakeSeed(SeedIndex);
		FHCJHypercubeGraph Graph;
		Graph.Build(4, Seed, FHCJHypercubeGraph::DefaultEdgeProbability);

		for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
		{
			const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);
			for (int32 Index = 0; Index < Layout.Asteroids.Num(); ++Index)
			{
				const FHCJAsteroid& Asteroid = Layout.Asteroids[Index];
				const FMeshCase& Mesh = Meshes[HCJ::PickAsteroidVariant(Layout.Seed, Index, UE_ARRAY_COUNT(Meshes))];
				const float Fill = Fills[(Index + Sector) % UE_ARRAY_COUNT(Fills)];
				const FTransform Visual = Asteroid.ComputeVisualTransform(Mesh.Bounds, Mesh.CornerFactor, Fill);

				// The centre of the mesh bounds lands on the asteroid, whatever the pivot of the mesh.
				const FVector Centre = Visual.TransformPosition(Mesh.Bounds.GetCenter());
				HCJ_TEST_CHECK(Centre.Equals(Asteroid.LocalPosition, 0.05), TEXT("seed %d sector %d asteroid %d: rock centre %s is not at the asteroid %s"),
					Seed, Sector, Index, *Centre.ToString(), *Asteroid.LocalPosition.ToString());
				HCJ_TEST_CHECK(Visual.GetScale3D().GetMin() > 0.0 && Visual.GetRotation().Equals(Asteroid.Rotation), TEXT("seed %d sector %d asteroid %d: rock has scale %s or another rotation"),
					Seed, Sector, Index, *Visual.GetScale3D().ToString());

				// Farthest possible vertex: a sphere-shaped asteroid knows it from the corner factor, the others from the box they are fitted to.
				const FVector HalfSize = Visual.GetScale3D() * Mesh.Bounds.GetExtent();
				const double Reach = Asteroid.Shape == EHCJAsteroidShape::Sphere ? HalfSize.GetMax() * Mesh.CornerFactor : HalfSize.Size();
				HCJ_TEST_CHECK(Reach <= Asteroid.BoundingRadius + HCJ::MaxVisualOverhang + Tolerance, TEXT("seed %d sector %d asteroid %d (shape %d): rock reaches %.1f uu, bounding radius %.1f"),
					Seed, Sector, Index, int32(Asteroid.Shape), Reach, Asteroid.BoundingRadius);
				if (Asteroid.Shape != EHCJAsteroidShape::Sphere || Fill <= 1.0f)
				{
					HCJ_TEST_CHECK(Reach <= Asteroid.BoundingRadius + Tolerance, TEXT("seed %d sector %d asteroid %d (shape %d, fill %.2f): rock reaches %.1f uu past its bounding radius %.1f"),
						Seed, Sector, Index, int32(Asteroid.Shape), Fill, Reach, Asteroid.BoundingRadius);
				}
				// Not a speck either: at least a third of the collision shape across.
				HCJ_TEST_CHECK(Reach >= 0.3 * Asteroid.BoundingRadius, TEXT("seed %d sector %d asteroid %d: rock reaches only %.1f uu of %.1f"), Seed, Sector, Index, Reach, Asteroid.BoundingRadius);

				// What the generator promises about the bounding spheres therefore holds for what is drawn.
				for (const FTransform& Spawn : Layout.SpawnPoints)
				{
					const double Gap = FVector::Dist(Spawn.GetLocation(), Asteroid.LocalPosition) - Reach;
					HCJ_TEST_CHECK(Gap >= HCJ::SpawnClearance - HCJ::MaxVisualOverhang - Tolerance, TEXT("seed %d sector %d: a spawn point is %.1f uu from the rock drawn for asteroid %d"), Seed, Sector, Gap, Index);
				}
			}
		}
	}

	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
