#include "Hypercube/HCJHypercubeGraph.h"

#include "Math/RandomStream.h"

namespace
{
	constexpr uint8 HCJUnreachable = 0xFF;

	/** Directed view of a hypercube edge: From is visited, From ^ (1 << Dim) is not. */
	struct FHCJFrontierEdge
	{
		uint8 From = 0;
		uint8 Dim = 0;

		int32 Target() const { return int32(From) ^ (1 << Dim); }
	};
}

void FHCJHypercubeGraph::Build(int32 InDimension, int32 InSeed, float InEdgeProbability)
{
	Dimension = FMath::Clamp(InDimension, MinDimension, MaxDimension);
	Seed = InSeed;
	EdgeProbability = FMath::IsFinite(InEdgeProbability) ? FMath::Clamp(InEdgeProbability, 0.0f, 1.0f) : DefaultEdgeProbability;

	const int32 N = 1 << Dimension;
	OpenMask.Init(0, N);

	const FRandomStream Stream(Seed);

	// ---- 2. random spanning tree by frontier growth from sector 0 ----
	TArray<bool, TInlineAllocator<64>> Visited;
	Visited.Init(false, N);
	TArray<FHCJFrontierEdge, TInlineAllocator<256>> Frontier;

	auto PushEdgesOf = [this, &Visited, &Frontier](int32 Sector)
	{
		for (int32 Dim = 0; Dim < Dimension; ++Dim)
		{
			if (!Visited[Sector ^ (1 << Dim)])
			{
				Frontier.Add(FHCJFrontierEdge{ uint8(Sector), uint8(Dim) });
			}
		}
	};

	Visited[0] = true;
	PushEdgesOf(0);

	for (int32 NumVisited = 1; NumVisited < N; ++NumVisited)
	{
		// The hypercube is connected, so the frontier cannot run dry before every sector is visited.
		check(Frontier.Num() > 0);
		const FHCJFrontierEdge Edge = Frontier[Stream.RandRange(0, Frontier.Num() - 1)];
		const int32 Target = Edge.Target();

		OpenMask[Edge.From] |= uint8(1 << Edge.Dim);
		OpenMask[Target] |= uint8(1 << Edge.Dim);
		Visited[Target] = true;

		// Order-preserving removal keeps the frontier (and therefore the result) well defined.
		Frontier.RemoveAll([Target](const FHCJFrontierEdge& Other) { return Other.Target() == Target; });
		PushEdgesOf(Target);
	}

	// ---- 3. extra edges ----
	for (int32 Sector = 0; Sector < N; ++Sector)
	{
		for (int32 Dim = 0; Dim < Dimension; ++Dim)
		{
			const uint8 Bit = uint8(1 << Dim);
			if ((Sector & Bit) != 0 || (OpenMask[Sector] & Bit) != 0)
			{
				continue;
			}
			if (Stream.FRand() < EdgeProbability)
			{
				OpenMask[Sector] |= Bit;
				OpenMask[Sector ^ Bit] |= Bit;
			}
		}
	}

	// ---- 4. all-pairs BFS ----
	DistanceTable.Init(HCJUnreachable, N * N);
	NextHopTable.Init(HCJUnreachable, N * N);

	TArray<int32, TInlineAllocator<64>> Queue;
	for (int32 From = 0; From < N; ++From)
	{
		Queue.Reset();
		Queue.Add(From);
		DistanceTable[TableIndex(From, From)] = 0;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 Current = Queue[Head];
			const uint8 NextDistance = uint8(DistanceTable[TableIndex(From, Current)] + 1);
			for (int32 Dim = 0; Dim < Dimension; ++Dim)
			{
				if ((OpenMask[Current] & (1 << Dim)) == 0)
				{
					continue;
				}
				const int32 Next = Current ^ (1 << Dim);
				uint8& Slot = DistanceTable[TableIndex(From, Next)];
				if (Slot == HCJUnreachable)
				{
					Slot = NextDistance;
					Queue.Add(Next);
				}
			}
		}
	}

	for (int32 From = 0; From < N; ++From)
	{
		for (int32 To = 0; To < N; ++To)
		{
			const uint8 Distance = DistanceTable[TableIndex(From, To)];
			if (From == To || Distance == HCJUnreachable)
			{
				continue;
			}
			for (int32 Dim = 0; Dim < Dimension; ++Dim)
			{
				if ((OpenMask[From] & (1 << Dim)) != 0
					&& DistanceTable[TableIndex(From ^ (1 << Dim), To)] == Distance - 1)
				{
					NextHopTable[TableIndex(From, To)] = uint8(Dim);
					break;
				}
			}
		}
	}
}

void FHCJHypercubeGraph::Reset()
{
	Dimension = 0;
	Seed = 0;
	EdgeProbability = 0.0f;
	OpenMask.Empty();
	DistanceTable.Empty();
	NextHopTable.Empty();
}

bool FHCJHypercubeGraph::IsEdgeOpen(int32 Sector, int32 Dim) const
{
	return IsValidSector(Sector) && IsValidDim(Dim) && (OpenMask[Sector] & (1 << Dim)) != 0;
}

int32 FHCJHypercubeGraph::GetNeighbor(int32 Sector, int32 Dim) const
{
	return (IsValidSector(Sector) && IsValidDim(Dim)) ? (Sector ^ (1 << Dim)) : INDEX_NONE;
}

uint8 FHCJHypercubeGraph::GetOpenDimMask(int32 Sector) const
{
	return IsValidSector(Sector) ? OpenMask[Sector] : uint8(0);
}

FHCJDimArray FHCJHypercubeGraph::GetOpenDims(int32 Sector) const
{
	FHCJDimArray Result;
	const uint8 Mask = GetOpenDimMask(Sector);
	for (int32 Dim = 0; Dim < Dimension; ++Dim)
	{
		if ((Mask & (1 << Dim)) != 0)
		{
			Result.Add(Dim);
		}
	}
	return Result;
}

int32 FHCJHypercubeGraph::NumOpenEdges() const
{
	int32 NumEnds = 0;
	for (const uint8 Mask : OpenMask)
	{
		NumEnds += FMath::CountBits(uint32(Mask));
	}
	return NumEnds / 2;
}

int32 FHCJHypercubeGraph::NextHopDim(int32 From, int32 To) const
{
	if (!IsValidSector(From) || !IsValidSector(To) || From == To)
	{
		return INDEX_NONE;
	}
	const uint8 Hop = NextHopTable[TableIndex(From, To)];
	return Hop == HCJUnreachable ? INDEX_NONE : int32(Hop);
}

int32 FHCJHypercubeGraph::ShortestDistance(int32 From, int32 To) const
{
	if (!IsValidSector(From) || !IsValidSector(To))
	{
		return INDEX_NONE;
	}
	const uint8 Distance = DistanceTable[TableIndex(From, To)];
	return Distance == HCJUnreachable ? INDEX_NONE : int32(Distance);
}

bool FHCJHypercubeGraph::IsConnected() const
{
	const int32 N = NumSectors();
	if (N == 0)
	{
		return false;
	}

	// Independent flood fill over OpenMask (does not use the BFS tables) so tests can cross-check them.
	uint64 VisitedBits = 1;
	TArray<int32, TInlineAllocator<64>> Stack;
	Stack.Add(0);
	int32 NumVisited = 1;
	while (Stack.Num() > 0)
	{
		const int32 Current = Stack.Pop(EAllowShrinking::No);
		for (int32 Dim = 0; Dim < Dimension; ++Dim)
		{
			if ((OpenMask[Current] & (1 << Dim)) == 0)
			{
				continue;
			}
			const int32 Next = Current ^ (1 << Dim);
			const uint64 NextBit = uint64(1) << Next;
			if ((VisitedBits & NextBit) == 0)
			{
				VisitedBits |= NextBit;
				++NumVisited;
				Stack.Add(Next);
			}
		}
	}
	return NumVisited == N;
}

FString FHCJHypercubeGraph::SectorToBinaryString(int32 Sector, int32 InDimension)
{
	const int32 NumDigits = FMath::Clamp(InDimension, 0, 31);
	FString Result;
	Result.Reserve(NumDigits);
	for (int32 Bit = NumDigits - 1; Bit >= 0; --Bit)
	{
		Result.AppendChar(((Sector >> Bit) & 1) != 0 ? TEXT('1') : TEXT('0'));
	}
	return Result;
}

FVector FHCJHypercubeGraph::GetSectorWorldOrigin(int32 Sector)
{
	if (Sector < 0)
	{
		return FVector::ZeroVector;
	}
	return FVector(
		double(Sector & 3),
		double((Sector >> 2) & 3),
		double((Sector >> 4) & 3)) * HCJ::SectorSpacing;
}

int32 FHCJHypercubeGraph::FindSectorAtLocation(const FVector& WorldLocation, int32 InDimension)
{
	const int32 Dim = FMath::Clamp(InDimension, MinDimension, MaxDimension);

	auto AxisIndex = [](double Coordinate, int32 NumBits) -> int32
	{
		const double MaxIndex = double((1 << NumBits) - 1);
		const double Cell = FMath::IsFinite(Coordinate) ? Coordinate / HCJ::SectorSpacing : 0.0;
		return int32(FMath::RoundToDouble(FMath::Clamp(Cell, 0.0, MaxIndex)));
	};

	const int32 X = AxisIndex(WorldLocation.X, FMath::Clamp(Dim, 0, 2));
	const int32 Y = AxisIndex(WorldLocation.Y, FMath::Clamp(Dim - 2, 0, 2));
	const int32 Z = AxisIndex(WorldLocation.Z, FMath::Clamp(Dim - 4, 0, 2));
	return X | (Y << 2) | (Z << 4);
}

FVector FHCJHypercubeGraph::GetDimensionDirection(int32 Dim)
{
	switch (Dim)
	{
	case 0: return FVector(1.0, 0.0, 0.0);
	case 1: return FVector(0.0, 1.0, 0.0);
	case 2: return FVector(0.0, 0.0, 1.0);
	case 3: return FVector(1.0, 1.0, 1.0) * UE_DOUBLE_INV_SQRT_3;
	case 4: return FVector(-1.0, 1.0, 1.0) * UE_DOUBLE_INV_SQRT_3;
	case 5: return FVector(1.0, -1.0, 1.0) * UE_DOUBLE_INV_SQRT_3;
	default: return FVector::ZeroVector;
	}
}

bool FHCJHypercubeGraph::GetDimensionJitterBasis(int32 Dim, FVector& OutU, FVector& OutV)
{
	constexpr double InvSqrt6 = 0.40824829046386301636621401245098;
	switch (Dim)
	{
	case 0: OutU = FVector(0.0, 1.0, 0.0); OutV = FVector(0.0, 0.0, 1.0); return true;
	case 1: OutU = FVector(0.0, 0.0, 1.0); OutV = FVector(1.0, 0.0, 0.0); return true;
	case 2: OutU = FVector(1.0, 0.0, 0.0); OutV = FVector(0.0, 1.0, 0.0); return true;
	case 3: OutU = FVector(1.0, -1.0, 0.0) * UE_DOUBLE_INV_SQRT_2; OutV = FVector(1.0, 1.0, -2.0) * InvSqrt6; return true;
	case 4: OutU = FVector(1.0, 1.0, 0.0) * UE_DOUBLE_INV_SQRT_2; OutV = FVector(1.0, -1.0, 2.0) * InvSqrt6; return true;
	case 5: OutU = FVector(1.0, 1.0, 0.0) * UE_DOUBLE_INV_SQRT_2; OutV = FVector(-1.0, 1.0, 2.0) * InvSqrt6; return true;
	default: OutU = FVector::ZeroVector; OutV = FVector::ZeroVector; return false;
	}
}
