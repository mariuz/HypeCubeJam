#include "World/HCJPickupPads.h"

#include "World/HCJBlackHole.h"
#include "World/HCJFeatureHash.h"
#include "World/HCJSectorLayout.h"

namespace
{
	constexpr int32 PadCountSalt = 0x0BADC0DE;
	constexpr int32 PadPlaceSalt = 0x0BAD5EED;
}

int32 FHCJPickupPads::GetWantedPadCount(int32 MatchSeed, int32 SectorId, const FHCJPickupSettings& Settings)
{
	if (!Settings.bEnabled)
	{
		return 0;
	}
	const int32 Min = FMath::Min<int32>(Settings.MinPads, Settings.MaxPads);
	const int32 Max = FMath::Max<int32>(Settings.MinPads, Settings.MaxPads);
	HCJ::FFeatureHash Hash(MatchSeed, PadCountSalt, SectorId);
	return Min + Hash.NextInt(Max - Min + 1);
}

TArray<FVector> FHCJPickupPads::Make(int32 MatchSeed, const FHCJSectorLayout& Layout, const FHCJBlackHole& Hole,
	const FHCJBlackHoleSettings& HoleSettings, const FHCJPickupSettings& Settings)
{
	TArray<FVector> Pads;
	if (!Settings.bEnabled || Layout.SectorId == INDEX_NONE)
	{
		return Pads;
	}

	const int32 Wanted = GetWantedPadCount(MatchSeed, Layout.SectorId, Settings);
	const double PortalClearanceSq = FMath::Square(double(HCJ::PortalClearRadius));
	const double SpawnClearanceSq = FMath::Square(double(HCJ::PadSpawnClearance));
	const double SeparationSq = FMath::Square(double(HCJ::PadSeparation));
	const double HoleClearanceSq = FMath::Square(double(FHCJBlackHole::GetClearance(HoleSettings)));

	HCJ::FFeatureHash Hash(MatchSeed, PadPlaceSalt, Layout.SectorId);
	for (int32 Pad = 0; Pad < Wanted; ++Pad)
	{
		for (int32 Attempt = 0; Attempt < HCJ::PadPlacementAttempts; ++Attempt)
		{
			FVector Candidate;
			if (!Hash.NextPointInShell(HCJ::PadMinDistance, HCJ::PadMaxDistance, Candidate))
			{
				continue;
			}

			bool bAccept = Layout.IsLocationClear(Candidate, HCJ::PadRockClearance);
			for (int32 Index = 0; bAccept && Index < Layout.Portals.Num(); ++Index)
			{
				bAccept = FVector::DistSquared(Candidate, Layout.Portals[Index].LocalPosition) >= PortalClearanceSq;
			}
			for (int32 Index = 0; bAccept && Index < Layout.SpawnPoints.Num(); ++Index)
			{
				bAccept = FVector::DistSquared(Candidate, Layout.SpawnPoints[Index].GetLocation()) >= SpawnClearanceSq;
			}
			if (bAccept && Hole.bPresent)
			{
				bAccept = FVector::DistSquared(Candidate, Hole.LocalPosition) >= HoleClearanceSq;
			}
			for (int32 Index = 0; bAccept && Index < Pads.Num(); ++Index)
			{
				bAccept = FVector::DistSquared(Candidate, Pads[Index]) >= SeparationSq;
			}
			if (bAccept)
			{
				Pads.Add(Candidate);
				break;
			}
		}
	}
	return Pads;
}
