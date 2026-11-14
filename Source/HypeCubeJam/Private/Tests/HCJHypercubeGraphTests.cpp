#include "Tests/HCJTestHelpers.h"

#include "World/HCJSectorLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJGraphTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	int32 HCJGraphPopCount(int32 Value)
	{
		return FMath::CountBits(uint32(Value));
	}

	/** Reference BFS that only uses IsEdgeOpen, to cross-check the graph's own tables. */
	void HCJGraphReferenceDistances(const FHCJHypercubeGraph& Graph, int32 From, TArray<int32>& OutDistances)
	{
		OutDistances.Init(INDEX_NONE, Graph.NumSectors());
		TArray<int32> Queue;
		Queue.Add(From);
		OutDistances[From] = 0;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 Current = Queue[Head];
			for (int32 Dim = 0; Dim < Graph.GetDimension(); ++Dim)
			{
				const int32 Next = Current ^ (1 << Dim);
				if (Graph.IsEdgeOpen(Current, Dim) && OutDistances[Next] == INDEX_NONE)
				{
					OutDistances[Next] = OutDistances[Current] + 1;
					Queue.Add(Next);
				}
			}
		}
	}

	bool HCJGraphsIdentical(const FHCJHypercubeGraph& A, const FHCJHypercubeGraph& B)
	{
		if (A.GetDimension() != B.GetDimension() || A.GetSeed() != B.GetSeed()
			|| A.GetEdgeProbability() != B.GetEdgeProbability() || A.NumOpenEdges() != B.NumOpenEdges())
		{
			return false;
		}
		const int32 N = A.NumSectors();
		for (int32 From = 0; From < N; ++From)
		{
			if (A.GetOpenDimMask(From) != B.GetOpenDimMask(From))
			{
				return false;
			}
			for (int32 To = 0; To < N; ++To)
			{
				if (A.ShortestDistance(From, To) != B.ShortestDistance(From, To) || A.NextHopDim(From, To) != B.NextHopDim(From, To))
				{
					return false;
				}
			}
		}
		return true;
	}
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJGraphConnectivityTest, "HypeCubeJam.Hypercube.Connectivity", HCJGraphTestFlags)

bool FHCJGraphConnectivityTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	const float Probabilities[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
	const int32 SeedsPerCase = 120;

	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		const int32 N = 1 << Dim;
		const int32 NumHypercubeEdges = Dim * N / 2;

		for (const float Probability : Probabilities)
		{
			int64 TotalOpenEdges = 0;
			for (int32 SeedIndex = 0; SeedIndex < SeedsPerCase; ++SeedIndex)
			{
				const int32 Seed = HCJTest::MakeSeed(SeedIndex);
				FHCJHypercubeGraph Graph;
				Graph.Build(Dim, Seed, Probability);

				HCJ_TEST_CHECK(Graph.IsBuilt() && Graph.GetDimension() == Dim && Graph.NumSectors() == N,
					TEXT("dim %d seed %d p %.2f: graph not built with %d sectors"), Dim, Seed, Probability, N);
				HCJ_TEST_CHECK(Graph.IsConnected(), TEXT("dim %d seed %d p %.2f: graph is not connected"), Dim, Seed, Probability);

				const int32 NumOpen = Graph.NumOpenEdges();
				TotalOpenEdges += NumOpen;
				HCJ_TEST_CHECK(NumOpen >= N - 1 && NumOpen <= NumHypercubeEdges,
					TEXT("dim %d seed %d p %.2f: %d open edges, expected %d..%d"), Dim, Seed, Probability, NumOpen, N - 1, NumHypercubeEdges);
				if (Probability <= 0.0f)
				{
					HCJ_TEST_CHECK(NumOpen == N - 1, TEXT("dim %d seed %d p 0: %d open edges, a spanning tree has %d"), Dim, Seed, NumOpen, N - 1);
				}
				if (Probability >= 1.0f)
				{
					HCJ_TEST_CHECK(NumOpen == NumHypercubeEdges, TEXT("dim %d seed %d p 1: %d open edges, the full hypercube has %d"), Dim, Seed, NumOpen, NumHypercubeEdges);
				}

				for (int32 Sector = 0; Sector < N; ++Sector)
				{
					HCJ_TEST_CHECK(Graph.GetOpenDimMask(Sector) != 0, TEXT("dim %d seed %d p %.2f: sector %d has no open edge"), Dim, Seed, Probability, Sector);
					HCJ_TEST_CHECK(Graph.ShortestDistance(0, Sector) != INDEX_NONE && Graph.ShortestDistance(Sector, 0) != INDEX_NONE,
						TEXT("dim %d seed %d p %.2f: sector %d unreachable from sector 0"), Dim, Seed, Probability, Sector);
				}
			}

			// The extra edges really are drawn with the requested probability (loose statistical bound).
			const double Mean = double(TotalOpenEdges) / SeedsPerCase;
			const double Expected = (N - 1) + double(Probability) * (NumHypercubeEdges - (N - 1));
			const double Slack = FMath::Max(1.0, 0.15 * (NumHypercubeEdges - (N - 1)));
			HCJ_TEST_CHECK(FMath::Abs(Mean - Expected) <= Slack,
				TEXT("dim %d p %.2f: mean open edges %.2f, expected about %.2f"), Dim, Probability, Mean, Expected);
		}
	}

	// Arguments are clamped, never rejected.
	{
		FHCJHypercubeGraph Graph;
		Graph.Build(-3, 42, 0.5f);
		HCJ_TEST_CHECK(Graph.GetDimension() == FHCJHypercubeGraph::MinDimension && Graph.IsConnected(), TEXT("dimension %d not clamped up to the minimum"), -3);
		Graph.Build(40, 42, 7.0f);
		HCJ_TEST_CHECK(Graph.GetDimension() == FHCJHypercubeGraph::MaxDimension && Graph.GetEdgeProbability() == 1.0f && Graph.IsConnected(),
			TEXT("dimension %d / probability 7 not clamped down"), 40);
		Graph.Build(4, 42, -2.0f);
		HCJ_TEST_CHECK(Graph.GetEdgeProbability() == 0.0f && Graph.NumOpenEdges() == 15, TEXT("negative probability not clamped to 0 (%d open edges)"), Graph.NumOpenEdges());

		Graph.Reset();
		HCJ_TEST_CHECK(!Graph.IsBuilt() && Graph.NumSectors() == 0 && !Graph.IsConnected() && Graph.NumOpenEdges() == 0,
			TEXT("Reset left the graph built (%d sectors)"), Graph.NumSectors());
		HCJ_TEST_CHECK(!Graph.IsEdgeOpen(0, 0) && Graph.GetNeighbor(0, 0) == INDEX_NONE && Graph.GetOpenDimMask(0) == 0
			&& Graph.GetOpenDims(0).Num() == 0 && Graph.NextHopDim(0, 1) == INDEX_NONE && Graph.ShortestDistance(0, 0) == INDEX_NONE,
			TEXT("queries on an unbuilt graph must return their failure values (%d sectors)"), Graph.NumSectors());
	}

	HCJ_TEST_FINISH();
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJGraphEdgeSymmetryTest, "HypeCubeJam.Hypercube.EdgeSymmetry", HCJGraphTestFlags)

bool FHCJGraphEdgeSymmetryTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		const int32 N = 1 << Dim;
		for (int32 SeedIndex = 0; SeedIndex < 60; ++SeedIndex)
		{
			const int32 Seed = HCJTest::MakeSeed(SeedIndex);
			FHCJHypercubeGraph Graph;
			Graph.Build(Dim, Seed, FHCJHypercubeGraph::DefaultEdgeProbability);

			int32 NumOpenEnds = 0;
			for (int32 Sector = 0; Sector < N; ++Sector)
			{
				const uint8 Mask = Graph.GetOpenDimMask(Sector);
				HCJ_TEST_CHECK((Mask >> Dim) == 0, TEXT("dim %d seed %d: sector %d mask 0x%02x has bits beyond the dimension"), Dim, Seed, Sector, int32(Mask));

				const FHCJDimArray OpenDims = Graph.GetOpenDims(Sector);
				HCJ_TEST_CHECK(OpenDims.Num() == HCJGraphPopCount(Mask), TEXT("dim %d seed %d: sector %d GetOpenDims has %d entries for mask 0x%02x"), Dim, Seed, Sector, OpenDims.Num(), int32(Mask));
				for (int32 Index = 0; Index < OpenDims.Num(); ++Index)
				{
					HCJ_TEST_CHECK(Graph.IsEdgeOpen(Sector, OpenDims[Index]) && (Index == 0 || OpenDims[Index - 1] < OpenDims[Index]),
						TEXT("dim %d seed %d: sector %d GetOpenDims entry %d (%d) is closed or out of order"), Dim, Seed, Sector, Index, OpenDims[Index]);
				}

				for (int32 EdgeDim = 0; EdgeDim < Dim; ++EdgeDim)
				{
					const int32 Neighbor = Graph.GetNeighbor(Sector, EdgeDim);
					HCJ_TEST_CHECK(Neighbor == (Sector ^ (1 << EdgeDim)) && Graph.GetNeighbor(Neighbor, EdgeDim) == Sector,
						TEXT("dim %d seed %d: GetNeighbor(%d, %d) = %d is not an involution"), Dim, Seed, Sector, EdgeDim, Neighbor);

					const bool bOpen = Graph.IsEdgeOpen(Sector, EdgeDim);
					HCJ_TEST_CHECK(bOpen == Graph.IsEdgeOpen(Neighbor, EdgeDim),
						TEXT("dim %d seed %d: edge (%d, %d) is open from one end only"), Dim, Seed, Sector, EdgeDim);
					HCJ_TEST_CHECK(bOpen == ((Mask & (1 << EdgeDim)) != 0),
						TEXT("dim %d seed %d: IsEdgeOpen(%d, %d) disagrees with the open mask"), Dim, Seed, Sector, EdgeDim);
					NumOpenEnds += bOpen ? 1 : 0;
				}
			}
			HCJ_TEST_CHECK(NumOpenEnds == 2 * Graph.NumOpenEdges(), TEXT("dim %d seed %d: %d open edge ends but NumOpenEdges is %d"), Dim, Seed, NumOpenEnds, Graph.NumOpenEdges());

			// Out-of-range arguments return the documented failure values.
			HCJ_TEST_CHECK(!Graph.IsEdgeOpen(-1, 0) && !Graph.IsEdgeOpen(N, 0) && !Graph.IsEdgeOpen(0, -1) && !Graph.IsEdgeOpen(0, Dim),
				TEXT("dim %d seed %d: IsEdgeOpen accepted an invalid argument"), Dim, Seed);
			HCJ_TEST_CHECK(Graph.GetNeighbor(-1, 0) == INDEX_NONE && Graph.GetNeighbor(N, 0) == INDEX_NONE
				&& Graph.GetNeighbor(0, -1) == INDEX_NONE && Graph.GetNeighbor(0, Dim) == INDEX_NONE,
				TEXT("dim %d seed %d: GetNeighbor accepted an invalid argument"), Dim, Seed);
			HCJ_TEST_CHECK(Graph.GetOpenDimMask(-1) == 0 && Graph.GetOpenDimMask(N) == 0 && Graph.GetOpenDims(N).Num() == 0,
				TEXT("dim %d seed %d: open-dimension queries accepted an invalid sector"), Dim, Seed);
			HCJ_TEST_CHECK(Graph.NextHopDim(-1, 0) == INDEX_NONE && Graph.NextHopDim(0, N) == INDEX_NONE
				&& Graph.ShortestDistance(-1, 0) == INDEX_NONE && Graph.ShortestDistance(0, N) == INDEX_NONE,
				TEXT("dim %d seed %d: routing queries accepted an invalid sector"), Dim, Seed);
		}
	}

	HCJ_TEST_FINISH();
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJGraphDeterminismTest, "HypeCubeJam.Hypercube.Determinism", HCJGraphTestFlags)

bool FHCJGraphDeterminismTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		TSet<FString> Signatures;
		const int32 NumSeeds = 64;

		for (int32 SeedIndex = 0; SeedIndex < NumSeeds; ++SeedIndex)
		{
			const int32 Seed = HCJTest::MakeSeed(SeedIndex);

			FHCJHypercubeGraph First;
			First.Build(Dim, Seed, 0.5f);

			FHCJHypercubeGraph Second;
			Second.Build(Dim, Seed, 0.5f);
			HCJ_TEST_CHECK(HCJGraphsIdentical(First, Second), TEXT("dim %d seed %d: two builds from the same inputs differ"), Dim, Seed);

			// Rebuilding an instance that held another graph must not leak old state.
			FHCJHypercubeGraph Reused;
			Reused.Build(FHCJHypercubeGraph::MaxDimension, Seed ^ 0x5A5A5A5A, 1.0f);
			Reused.Build(Dim, Seed, 0.5f);
			HCJ_TEST_CHECK(HCJGraphsIdentical(First, Reused), TEXT("dim %d seed %d: a rebuilt instance differs from a fresh one"), Dim, Seed);

			const FHCJHypercubeGraph Copy = First;
			HCJ_TEST_CHECK(HCJGraphsIdentical(First, Copy), TEXT("dim %d seed %d: a copy differs from its source"), Dim, Seed);

			FString Signature;
			for (int32 Sector = 0; Sector < First.NumSectors(); ++Sector)
			{
				Signature += FString::Printf(TEXT("%02x"), int32(First.GetOpenDimMask(Sector)));
			}
			Signatures.Add(MoveTemp(Signature));
		}

		// Different seeds must actually give different topologies (a square only has 5 connected subgraphs
		// with >= 3 edges, so the bar is low for dim 2).
		const int32 MinDistinct = Dim == 2 ? 3 : NumSeeds / 2;
		HCJ_TEST_CHECK(Signatures.Num() >= MinDistinct, TEXT("dim %d: only %d distinct graphs from %d seeds"), Dim, Signatures.Num(), NumSeeds);
	}

	// The seed mixer is part of the network contract: it must be a pure function with good spread.
	{
		TSet<int32> SectorSeeds;
		for (int32 Sector = 0; Sector < 64; ++Sector)
		{
			const int32 SectorSeed = FHCJSectorLayout::MakeSectorSeed(1234, Sector);
			HCJ_TEST_CHECK(SectorSeed == HCJ::MixSeed(1234, 0x1000 + Sector), TEXT("MakeSectorSeed(1234, %d) does not match its definition"), Sector);
			SectorSeeds.Add(SectorSeed);
		}
		HCJ_TEST_CHECK(SectorSeeds.Num() == 64, TEXT("only %d distinct sector seeds for 64 sectors"), SectorSeeds.Num());
		HCJ_TEST_CHECK(HCJ::MixSeed(1, 2) != HCJ::MixSeed(2, 1), TEXT("MixSeed is symmetric (%d)"), HCJ::MixSeed(1, 2));
	}

	HCJ_TEST_FINISH();
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJGraphRoutingTest, "HypeCubeJam.Hypercube.Routing", HCJGraphTestFlags)

bool FHCJGraphRoutingTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	const float Probabilities[] = { 0.0f, 0.3f, 0.5f, 1.0f };
	TArray<int32> Reference;

	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		const int32 N = 1 << Dim;
		for (const float Probability : Probabilities)
		{
			for (int32 SeedIndex = 0; SeedIndex < 16; ++SeedIndex)
			{
				const int32 Seed = HCJTest::MakeSeed(SeedIndex);
				FHCJHypercubeGraph Graph;
				Graph.Build(Dim, Seed, Probability);

				for (int32 From = 0; From < N; ++From)
				{
					HCJGraphReferenceDistances(Graph, From, Reference);

					for (int32 To = 0; To < N; ++To)
					{
						const int32 Distance = Graph.ShortestDistance(From, To);
						HCJ_TEST_CHECK(Distance == Reference[To], TEXT("dim %d seed %d p %.2f: distance %d -> %d is %d, reference BFS says %d"), Dim, Seed, Probability, From, To, Distance, Reference[To]);
						HCJ_TEST_CHECK(Distance == Graph.ShortestDistance(To, From), TEXT("dim %d seed %d p %.2f: distance %d <-> %d is not symmetric"), Dim, Seed, Probability, From, To);
						HCJ_TEST_CHECK(Distance >= HCJGraphPopCount(From ^ To), TEXT("dim %d seed %d p %.2f: distance %d -> %d (%d) is below the Hamming distance"), Dim, Seed, Probability, From, To, Distance);

						const int32 Hop = Graph.NextHopDim(From, To);
						if (From == To)
						{
							HCJ_TEST_CHECK(Distance == 0 && Hop == INDEX_NONE, TEXT("dim %d seed %d p %.2f: sector %d to itself has distance %d, hop %d"), Dim, Seed, Probability, From, Distance, Hop);
							continue;
						}
						if (Probability >= 1.0f)
						{
							HCJ_TEST_CHECK(Distance == HCJGraphPopCount(From ^ To), TEXT("dim %d seed %d: full hypercube distance %d -> %d is %d"), Dim, Seed, From, To, Distance);
						}

						// The hop is the LOWEST open dimension that gets one step closer.
						int32 ExpectedHop = INDEX_NONE;
						for (int32 EdgeDim = 0; EdgeDim < Dim && ExpectedHop == INDEX_NONE; ++EdgeDim)
						{
							if (Graph.IsEdgeOpen(From, EdgeDim) && Graph.ShortestDistance(From ^ (1 << EdgeDim), To) == Distance - 1)
							{
								ExpectedHop = EdgeDim;
							}
						}
						HCJ_TEST_CHECK(Hop != INDEX_NONE && Hop == ExpectedHop, TEXT("dim %d seed %d p %.2f: next hop %d -> %d is %d, expected %d"), Dim, Seed, Probability, From, To, Hop, ExpectedHop);

						// Following the hops arrives in exactly Distance steps over open edges.
						int32 Current = From;
						int32 Steps = 0;
						while (Current != To && Steps <= N)
						{
							const int32 StepDim = Graph.NextHopDim(Current, To);
							if (!Graph.IsEdgeOpen(Current, StepDim))
							{
								break;
							}
							Current = Graph.GetNeighbor(Current, StepDim);
							++Steps;
						}
						HCJ_TEST_CHECK(Current == To && Steps == Distance, TEXT("dim %d seed %d p %.2f: walking %d -> %d ended in %d after %d steps (distance %d)"), Dim, Seed, Probability, From, To, Current, Steps, Distance);
					}
				}
			}
		}
	}

	HCJ_TEST_FINISH();
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJGraphHelpersTest, "HypeCubeJam.Hypercube.Helpers", HCJGraphTestFlags)

bool FHCJGraphHelpersTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	// ---- binary strings ----
	{
		struct FCase { int32 Sector; int32 Dim; const TCHAR* Expected; };
		const FCase Cases[] = {
			{ 5, 4, TEXT("0101") }, { 0, 2, TEXT("00") }, { 3, 2, TEXT("11") }, { 1, 6, TEXT("000001") },
			{ 63, 6, TEXT("111111") }, { 8, 4, TEXT("1000") }, { 6, 3, TEXT("110") }
		};
		for (const FCase& Case : Cases)
		{
			const FString Actual = FHCJHypercubeGraph::SectorToBinaryString(Case.Sector, Case.Dim);
			HCJ_TEST_CHECK(Actual == Case.Expected, TEXT("SectorToBinaryString(%d, %d) = \"%s\", expected \"%s\""), Case.Sector, Case.Dim, *Actual, Case.Expected);
		}
	}

	// ---- world grid ----
	{
		HCJ_TEST_CHECK(HCJ::SectorSpacing >= 400000.0, TEXT("sector spacing %.0f is below 400000 uu"), HCJ::SectorSpacing);
		HCJ_TEST_CHECK(2.0 * HCJ::SectorKillRadius < HCJ::SectorSpacing && HCJ::NetCullDistance + HCJ::SectorKillRadius < HCJ::SectorSpacing,
			TEXT("sectors of spacing %.0f can see or reach each other"), HCJ::SectorSpacing);
		HCJ_TEST_CHECK(FHCJHypercubeGraph::GetSectorWorldOrigin(0).IsZero(), TEXT("sector 0 is not at the world origin (%s)"), *FHCJHypercubeGraph::GetSectorWorldOrigin(0).ToString());

		const int32 MaxSectors = 1 << FHCJHypercubeGraph::MaxDimension;
		for (int32 A = 0; A < MaxSectors; ++A)
		{
			const FVector OriginA = FHCJHypercubeGraph::GetSectorWorldOrigin(A);
			HCJ_TEST_CHECK(OriginA.X >= 0.0 && OriginA.Y >= 0.0 && OriginA.Z >= 0.0, TEXT("sector %d origin %s has a negative coordinate"), A, *OriginA.ToString());
			for (int32 B = A + 1; B < MaxSectors; ++B)
			{
				const double Distance = FVector::Dist(OriginA, FHCJHypercubeGraph::GetSectorWorldOrigin(B));
				HCJ_TEST_CHECK(Distance >= HCJ::SectorSpacing - HCJTest::DistanceTolerance, TEXT("sectors %d and %d are only %.0f uu apart"), A, B, Distance);
			}
		}

		const FRandomStream Stream(20261114);
		for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
		{
			const int32 N = 1 << Dim;
			for (int32 Sector = 0; Sector < N; ++Sector)
			{
				const FVector Origin = FHCJHypercubeGraph::GetSectorWorldOrigin(Sector);
				HCJ_TEST_CHECK(FHCJHypercubeGraph::FindSectorAtLocation(Origin, Dim) == Sector,
					TEXT("dim %d: FindSectorAtLocation(origin of %d) = %d"), Dim, Sector, FHCJHypercubeGraph::FindSectorAtLocation(Origin, Dim));

				// Anywhere a living ship can be still maps to its own sector.
				for (int32 Sample = 0; Sample < 8; ++Sample)
				{
					const FVector Direction = Stream.VRand();
					const double Distance = Stream.FRandRange(0.0, HCJ::SectorKillRadius);
					const int32 Found = FHCJHypercubeGraph::FindSectorAtLocation(Origin + Direction * Distance, Dim);
					HCJ_TEST_CHECK(Found == Sector, TEXT("dim %d: a point %.0f uu from sector %d maps to sector %d"), Dim, Distance, Sector, Found);
				}
			}

			// Far outside the grid the result is clamped to a sector that exists for this dimension.
			for (int32 Sample = 0; Sample < 64; ++Sample)
			{
				const FVector Direction = Stream.VRand();
				const double Distance = Stream.FRandRange(0.0, 20.0 * HCJ::SectorSpacing);
				const int32 Found = FHCJHypercubeGraph::FindSectorAtLocation(Direction * Distance, Dim);
				HCJ_TEST_CHECK(Found >= 0 && Found < N, TEXT("dim %d: FindSectorAtLocation returned %d, outside [0, %d)"), Dim, Found, N);
			}
		}
	}

	// ---- dimension directions ----
	{
		const double MaxCosine = FMath::Cos(FMath::DegreesToRadians(54.7));
		for (int32 A = 0; A < FHCJHypercubeGraph::MaxDimension; ++A)
		{
			const FVector DirA = FHCJHypercubeGraph::GetDimensionDirection(A);
			HCJ_TEST_CHECK(FMath::IsNearlyEqual(DirA.Size(), 1.0, 1.0e-6), TEXT("dimension %d direction %s is not unit length"), A, *DirA.ToString());
			for (int32 B = A + 1; B < FHCJHypercubeGraph::MaxDimension; ++B)
			{
				// |dot| covers all four sign combinations of the two portal directions.
				const double Cosine = FMath::Abs(FVector::DotProduct(DirA, FHCJHypercubeGraph::GetDimensionDirection(B)));
				HCJ_TEST_CHECK(Cosine <= MaxCosine, TEXT("dimension directions %d and %d are closer than 54.7 degrees (cos %.4f)"), A, B, Cosine);
			}
		}
		HCJ_TEST_CHECK(FHCJHypercubeGraph::GetDimensionDirection(-1).IsZero() && FHCJHypercubeGraph::GetDimensionDirection(FHCJHypercubeGraph::MaxDimension).IsZero(),
			TEXT("an invalid dimension (%d) must give the zero vector"), FHCJHypercubeGraph::MaxDimension);
	}

	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
