#include "World/HCJBlackHole.h"

#include "Hypercube/HCJHypercubeGraph.h"
#include "World/HCJSectorLayout.h"

namespace
{
	/** Integer hash sequence in [0, 1); nothing but integer arithmetic and one exact float conversion. */
	struct FHCJHoleHash
	{
		int32 State;
		int32 Counter = 0;

		explicit FHCJHoleHash(int32 Seed) : State(Seed) {}

		double Next()
		{
			State = HCJ::MixSeed(State, 0x0B1AC401 + Counter++);
			return double(static_cast<uint32>(State) >> 8) * (1.0 / 16777216.0);
		}
	};
}

float FHCJBlackHole::GetClearance(const FHCJBlackHoleSettings& Settings)
{
	return FMath::Max(HCJ::BlackHoleClearanceFraction * Settings.InfluenceRadius, 2.0f * Settings.HorizonRadius);
}

FHCJBlackHole FHCJBlackHole::Make(int32 MatchSeed, const FHCJSectorLayout& Layout, const FHCJBlackHoleSettings& Settings)
{
	FHCJBlackHole Hole;
	if (!Settings.bEnabled || Layout.SectorId == INDEX_NONE || Settings.InfluenceRadius <= 0.0f)
	{
		return Hole;
	}

	FHCJHoleHash Hash(HCJ::MixSeed(HCJ::MixSeed(MatchSeed, 0x00B1AC40), Layout.SectorId));
	if (Hash.Next() >= double(Settings.SectorFraction))
	{
		return Hole;
	}

	const double Clearance = GetClearance(Settings);
	const double PortalClearanceSq = FMath::Square(Clearance + HCJ::PortalTriggerRadius);
	const double SpawnClearanceSq = FMath::Square(Clearance);
	const float RockClearance = HCJ::BlackHoleRockFreeHorizons * Settings.HorizonRadius;

	for (int32 Attempt = 0; Attempt < HCJ::BlackHolePlacementAttempts; ++Attempt)
	{
		// Every draw is its own statement (unspecified evaluation order otherwise).
		const double X = 2.0 * Hash.Next() - 1.0;
		const double Y = 2.0 * Hash.Next() - 1.0;
		const double Z = 2.0 * Hash.Next() - 1.0;
		const double DistanceRoll = Hash.Next();

		// A direction from a point in the unit ball: only * + and sqrt, no trigonometry.
		const double LengthSq = X * X + Y * Y + Z * Z;
		if (LengthSq > 1.0 || LengthSq < 0.01)
		{
			continue;
		}
		const double Distance = HCJ::BlackHoleMinDistance + (HCJ::BlackHoleMaxDistance - HCJ::BlackHoleMinDistance) * DistanceRoll;
		const FVector Candidate = FVector(X, Y, Z) * (Distance / FMath::Sqrt(LengthSq));

		bool bAccept = Layout.IsLocationClear(Candidate, RockClearance);
		for (int32 Index = 0; bAccept && Index < Layout.Portals.Num(); ++Index)
		{
			bAccept = FVector::DistSquared(Candidate, Layout.Portals[Index].LocalPosition) >= PortalClearanceSq;
		}
		for (int32 Index = 0; bAccept && Index < Layout.SpawnPoints.Num(); ++Index)
		{
			bAccept = FVector::DistSquared(Candidate, Layout.SpawnPoints[Index].GetLocation()) >= SpawnClearanceSq;
		}
		if (bAccept)
		{
			Hole.bPresent = true;
			Hole.LocalPosition = Candidate;
			return Hole;
		}
	}
	return Hole;
}

float FHCJBlackHole::ComputePullSpeed(double Distance, const FHCJBlackHoleSettings& Settings)
{
	if (!Settings.bEnabled || Distance >= Settings.InfluenceRadius || Settings.InfluenceRadius <= 0.0f)
	{
		return 0.0f;
	}
	// Clamped near the centre: the ratio is capped before squaring so the division never blows up.
	const double Ratio = double(Settings.InfluenceRadius) / FMath::Max(Distance, 1.0);
	const double Speed = double(Settings.PullSpeed) * (Ratio * Ratio - 1.0);
	return float(FMath::Clamp(Speed, 0.0, double(Settings.MaxPullSpeed)));
}

FVector FHCJBlackHole::ComputePull(const FVector& ToHole, const FHCJBlackHoleSettings& Settings)
{
	const double DistanceSq = ToHole.SizeSquared();
	if (DistanceSq < 1.0e-4)
	{
		return FVector::ZeroVector;
	}
	const double Distance = FMath::Sqrt(DistanceSq);
	return ToHole * (double(ComputePullSpeed(Distance, Settings)) / Distance);
}

float FHCJBlackHole::ComputeNoEscapeRadius(float EscapeSpeed, const FHCJBlackHoleSettings& Settings)
{
	if (!Settings.bEnabled || Settings.PullSpeed <= 0.0f || EscapeSpeed <= 0.0f || Settings.MaxPullSpeed < EscapeSpeed)
	{
		return 0.0f;
	}
	// PullSpeed * ((R / d)^2 - 1) == EscapeSpeed  =>  d = R / sqrt(1 + EscapeSpeed / PullSpeed)
	return Settings.InfluenceRadius / FMath::Sqrt(1.0f + EscapeSpeed / Settings.PullSpeed);
}
