#include "World/HCJWormholes.h"

#include "World/HCJBlackHole.h"
#include "World/HCJFeatureHash.h"
#include "World/HCJSectorLayout.h"

namespace
{
	constexpr int32 WormholeLinkSalt = 0x00770E11;
	constexpr int32 WormholeMouthSalt = 0x0077A0F1;
	constexpr uint8 NoRoute = 0xFF;
}

int32 FHCJWormholes::GetAutoCount(int32 Dimension)
{
	return Dimension >= HCJ::WormholeMinDimension && Dimension <= FHCJHypercubeGraph::MaxDimension ? (1 << Dimension) / 4 : 0;
}

TArray<FHCJWormholeLink> FHCJWormholes::MakeLinks(int32 MatchSeed, int32 Dimension, const FHCJWormholeSettings& Settings)
{
	TArray<FHCJWormholeLink> Links;
	if (!Settings.bEnabled || Dimension < HCJ::WormholeMinDimension || Dimension > FHCJHypercubeGraph::MaxDimension)
	{
		return Links;
	}
	const int32 NumSectors = 1 << Dimension;
	const int32 Wanted = FMath::Clamp(Settings.Count < 0 ? GetAutoCount(Dimension) : Settings.Count, 0, NumSectors / 2);
	if (Wanted == 0)
	{
		return Links;
	}

	// Seeded Fisher-Yates shuffle of the sector ids.
	TArray<int32, TInlineAllocator<64>> Order;
	for (int32 Sector = 0; Sector < NumSectors; ++Sector)
	{
		Order.Add(Sector);
	}
	HCJ::FFeatureHash Hash(MatchSeed, WormholeLinkSalt, Dimension);
	for (int32 Index = NumSectors - 1; Index > 0; --Index)
	{
		Order.Swap(Index, Hash.NextInt(Index + 1));
	}

	// Greedy: every still unpaired sector takes the next unpaired one that is at least two bits away.
	TArray<bool, TInlineAllocator<64>> Used;
	Used.Init(false, NumSectors);
	for (int32 First = 0; First < NumSectors && Links.Num() < Wanted; ++First)
	{
		if (Used[First])
		{
			continue;
		}
		for (int32 Second = First + 1; Second < NumSectors; ++Second)
		{
			if (!Used[Second] && FMath::CountBits(static_cast<uint32>(Order[First] ^ Order[Second])) >= 2)
			{
				Used[First] = true;
				Used[Second] = true;
				FHCJWormholeLink& Link = Links.AddDefaulted_GetRef();
				Link.SectorA = FMath::Min(Order[First], Order[Second]);
				Link.SectorB = FMath::Max(Order[First], Order[Second]);
				break;
			}
		}
	}
	Links.Sort([](const FHCJWormholeLink& A, const FHCJWormholeLink& B) { return A.SectorA < B.SectorA; });
	return Links;
}

bool FHCJWormholes::MakeMouth(int32 MatchSeed, const FHCJSectorLayout& Layout, const FHCJBlackHole& Hole, const FHCJBlackHoleSettings& HoleSettings,
	TConstArrayView<FVector> Pads, FVector& OutLocalPosition)
{
	if (Layout.SectorId == INDEX_NONE)
	{
		return false;
	}
	const double PortalClearanceSq = FMath::Square(double(HCJ::WormholePortalClearance));
	const double SpawnClearanceSq = FMath::Square(double(HCJ::WormholeSpawnClearance));
	const double PadClearanceSq = FMath::Square(double(HCJ::WormholePadClearance));
	const double HoleClearanceSq = FMath::Square(double(HoleSettings.InfluenceRadius) + double(HCJ::WormholeHoleMargin));

	HCJ::FFeatureHash Hash(MatchSeed, WormholeMouthSalt, Layout.SectorId);
	for (int32 Attempt = 0; Attempt < HCJ::WormholePlacementAttempts; ++Attempt)
	{
		FVector Candidate;
		if (!Hash.NextPointInShell(HCJ::WormholeMinDistance, HCJ::WormholeMaxDistance, Candidate))
		{
			continue;
		}
		bool bAccept = Layout.IsLocationClear(Candidate, HCJ::WormholeRockClearance);
		for (int32 Index = 0; bAccept && Index < Layout.Portals.Num(); ++Index)
		{
			bAccept = FVector::DistSquared(Candidate, Layout.Portals[Index].LocalPosition) >= PortalClearanceSq;
		}
		for (int32 Index = 0; bAccept && Index < Layout.SpawnPoints.Num(); ++Index)
		{
			bAccept = FVector::DistSquared(Candidate, Layout.SpawnPoints[Index].GetLocation()) >= SpawnClearanceSq;
		}
		for (int32 Index = 0; bAccept && Index < Pads.Num(); ++Index)
		{
			bAccept = FVector::DistSquared(Candidate, Pads[Index]) >= PadClearanceSq;
		}
		if (bAccept && Hole.bPresent)
		{
			bAccept = FVector::DistSquared(Candidate, Hole.LocalPosition) >= HoleClearanceSq;
		}
		if (bAccept)
		{
			OutLocalPosition = Candidate;
			return true;
		}
	}
	return false;
}

void FHCJRouteTable::Reset()
{
	NumSectors = 0;
	WormholeDest.Reset();
	DistanceTable.Reset();
	NextHopTable.Reset();
}

void FHCJRouteTable::Build(const FHCJHypercubeGraph& Graph, TConstArrayView<FHCJWormholeLink> Links)
{
	Reset();
	if (!Graph.IsBuilt())
	{
		return;
	}
	NumSectors = Graph.NumSectors();
	const int32 Dimension = Graph.GetDimension();

	WormholeDest.Init(INDEX_NONE, NumSectors);
	for (const FHCJWormholeLink& Link : Links)
	{
		if (Graph.IsValidSector(Link.SectorA) && Graph.IsValidSector(Link.SectorB) && Link.SectorA != Link.SectorB
			&& WormholeDest[Link.SectorA] == INDEX_NONE && WormholeDest[Link.SectorB] == INDEX_NONE)
		{
			WormholeDest[Link.SectorA] = Link.SectorB;
			WormholeDest[Link.SectorB] = Link.SectorA;
		}
	}

	// All-pairs distances by one BFS per source over portals and wormholes.
	DistanceTable.Init(NoRoute, NumSectors * NumSectors);
	TArray<int32, TInlineAllocator<64>> Queue;
	for (int32 Source = 0; Source < NumSectors; ++Source)
	{
		uint8* Row = &DistanceTable[Source * NumSectors];
		Row[Source] = 0;
		Queue.Reset();
		Queue.Add(Source);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 Sector = Queue[Head];
			const auto Visit = [&Queue, Row, Sector](int32 Next)
			{
				if (Row[Next] == NoRoute)
				{
					Row[Next] = Row[Sector] + 1;
					Queue.Add(Next);
				}
			};
			for (int32 Dim = 0; Dim < Dimension; ++Dim)
			{
				if (Graph.IsEdgeOpen(Sector, Dim))
				{
					Visit(Sector ^ (1 << Dim));
				}
			}
			if (WormholeDest[Sector] != INDEX_NONE)
			{
				Visit(WormholeDest[Sector]);
			}
		}
	}

	// First hop: the lowest portal dimension that gets one hop closer, else the wormhole.
	NextHopTable.Init(NoRoute, NumSectors * NumSectors);
	for (int32 From = 0; From < NumSectors; ++From)
	{
		for (int32 To = 0; To < NumSectors; ++To)
		{
			const uint8 Here = DistanceTable[From * NumSectors + To];
			if (From == To || Here == NoRoute)
			{
				continue;
			}
			for (int32 Dim = 0; Dim < Dimension; ++Dim)
			{
				if (Graph.IsEdgeOpen(From, Dim) && DistanceTable[(From ^ (1 << Dim)) * NumSectors + To] == Here - 1)
				{
					NextHopTable[From * NumSectors + To] = static_cast<uint8>(Dim);
					break;
				}
			}
			if (NextHopTable[From * NumSectors + To] == NoRoute && WormholeDest[From] != INDEX_NONE
				&& DistanceTable[WormholeDest[From] * NumSectors + To] == Here - 1)
			{
				NextHopTable[From * NumSectors + To] = static_cast<uint8>(HCJ::WormholeHop);
			}
		}
	}
}

int32 FHCJRouteTable::Distance(int32 From, int32 To) const
{
	if (From < 0 || To < 0 || From >= NumSectors || To >= NumSectors)
	{
		return INDEX_NONE;
	}
	const uint8 Value = DistanceTable[From * NumSectors + To];
	return Value == NoRoute ? INDEX_NONE : Value;
}

int32 FHCJRouteTable::NextHop(int32 From, int32 To) const
{
	if (From < 0 || To < 0 || From >= NumSectors || To >= NumSectors || From == To)
	{
		return INDEX_NONE;
	}
	const uint8 Value = NextHopTable[From * NumSectors + To];
	return Value == NoRoute ? INDEX_NONE : Value;
}
