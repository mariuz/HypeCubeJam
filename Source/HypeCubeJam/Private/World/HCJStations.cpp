#include "World/HCJStations.h"

#include "World/HCJBlackHole.h"
#include "World/HCJFeatureHash.h"
#include "World/HCJSectorLayout.h"

namespace
{
	constexpr int32 StationSalt = 0x057A7104;

	// The station: a hub, two crossing beams and a mast. Station space, uu.
	const FHCJStationBox GStationBoxes[] = {
		{ FVector(0.0, 0.0, 0.0), FVector(600.0, 600.0, 800.0) },		// hub
		{ FVector(0.0, 0.0, 0.0), FVector(1800.0, 200.0, 200.0) },		// beam along X
		{ FVector(0.0, 0.0, 0.0), FVector(200.0, 1800.0, 200.0) },		// beam along Y
		{ FVector(0.0, 0.0, 1300.0), FVector(150.0, 150.0, 500.0) },	// mast
		// solar panels: thin plates standing on the beams (index >= FirstPanelBox)
		{ FVector(1150.0, 0.0, 0.0), FVector(450.0, 25.0, 750.0) },
		{ FVector(-1150.0, 0.0, 0.0), FVector(450.0, 25.0, 750.0) },
		{ FVector(0.0, 1150.0, 0.0), FVector(25.0, 450.0, 750.0) },
		{ FVector(0.0, -1150.0, 0.0), FVector(25.0, 450.0, 750.0) },
	};

	// One mount at the end of every beam, one on the mast, one under the hub; each a little clear of the body.
	const FHCJTurretMount GStationMounts[HCJ::StationMaxTurrets] = {
		{ FVector(1800.0 + HCJ::TurretRadius, 0.0, 0.0), FVector(1.0, 0.0, 0.0) },
		{ FVector(-1800.0 - HCJ::TurretRadius, 0.0, 0.0), FVector(-1.0, 0.0, 0.0) },
		{ FVector(0.0, 1800.0 + HCJ::TurretRadius, 0.0), FVector(0.0, 1.0, 0.0) },
		{ FVector(0.0, -1800.0 - HCJ::TurretRadius, 0.0), FVector(0.0, -1.0, 0.0) },
		{ FVector(0.0, 0.0, 1800.0 + HCJ::TurretRadius), FVector(0.0, 0.0, 1.0) },
		{ FVector(0.0, 0.0, -800.0 - HCJ::TurretRadius), FVector(0.0, 0.0, -1.0) },
	};
}

bool HCJ::ComputeLeadAim(const FVector& ShooterLocation, const FVector& TargetLocation, const FVector& TargetVelocity,
	float ProjectileSpeed, FVector& OutAimPoint)
{
	OutAimPoint = TargetLocation;
	if (ProjectileSpeed <= 0.0f)
	{
		return false;
	}

	// |R + V t| = s t  ->  (V.V - s^2) t^2 + 2 R.V t + R.R = 0, smallest positive t.
	const FVector Relative = TargetLocation - ShooterLocation;
	const double A = TargetVelocity.SizeSquared() - double(ProjectileSpeed) * double(ProjectileSpeed);
	const double B = 2.0 * FVector::DotProduct(Relative, TargetVelocity);
	const double C = Relative.SizeSquared();

	double Time = -1.0;
	if (FMath::Abs(A) < 1.0e-6)
	{
		// Target exactly as fast as the projectile: linear equation.
		if (B < -1.0e-9)
		{
			Time = -C / B;
		}
	}
	else
	{
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant >= 0.0)
		{
			const double Root = FMath::Sqrt(Discriminant);
			const double T1 = (-B - Root) / (2.0 * A);
			const double T2 = (-B + Root) / (2.0 * A);
			const double Low = FMath::Min(T1, T2);
			const double High = FMath::Max(T1, T2);
			Time = Low > 0.0 ? Low : High;
		}
	}
	if (!(Time > 0.0))
	{
		return false;
	}
	OutAimPoint = TargetLocation + TargetVelocity * Time;
	return true;
}

TConstArrayView<FHCJStationBox> FHCJStation::GetBodyBoxes()
{
	return MakeArrayView(GStationBoxes);
}

TConstArrayView<FHCJTurretMount> FHCJStation::GetAllMounts()
{
	return MakeArrayView(GStationMounts);
}

FHCJStation FHCJStation::Make(int32 MatchSeed, const FHCJSectorLayout& Layout, const FHCJBlackHole& Hole, const FHCJBlackHoleSettings& HoleSettings,
	TConstArrayView<FVector> Pads, bool bHasWormhole, const FVector& WormholeMouth, const FHCJStationSettings& Settings)
{
	FHCJStation Station;
	if (!Settings.bEnabled || Layout.SectorId == INDEX_NONE)
	{
		return Station;
	}

	HCJ::FFeatureHash Hash(MatchSeed, StationSalt, Layout.SectorId);
	if (Hash.Next() >= double(Settings.SectorFraction))
	{
		return Station;
	}

	const double PortalClearanceSq = FMath::Square(double(HCJ::StationPortalClearance));
	const double SpawnClearanceSq = FMath::Square(double(HCJ::StationSpawnClearance));
	const double PadClearanceSq = FMath::Square(double(HCJ::StationPadClearance));
	const double WormholeClearanceSq = FMath::Square(double(HCJ::StationWormholeClearance));
	const double HoleClearanceSq = FMath::Square(double(HoleSettings.InfluenceRadius) + double(HCJ::StationHoleMargin));

	for (int32 Attempt = 0; Attempt < HCJ::StationPlacementAttempts; ++Attempt)
	{
		FVector Candidate;
		if (!Hash.NextPointInShell(HCJ::StationMinDistance, HCJ::StationMaxDistance, Candidate))
		{
			continue;
		}
		bool bAccept = true;
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
		if (bAccept && bHasWormhole)
		{
			bAccept = FVector::DistSquared(Candidate, WormholeMouth) >= WormholeClearanceSq;
		}
		if (bAccept && Hole.bPresent)
		{
			bAccept = FVector::DistSquared(Candidate, Hole.LocalPosition) >= HoleClearanceSq;
		}
		if (!bAccept)
		{
			continue;
		}

		Station.bPresent = true;
		Station.LocalPosition = Candidate;

		// Turrets: a seeded shuffle of the mounts, the first few are used.
		int32 Order[HCJ::StationMaxTurrets];
		for (int32 Index = 0; Index < HCJ::StationMaxTurrets; ++Index)
		{
			Order[Index] = Index;
		}
		for (int32 Index = HCJ::StationMaxTurrets - 1; Index > 0; --Index)
		{
			Swap(Order[Index], Order[Hash.NextInt(Index + 1)]);
		}
		const int32 NumTurrets = HCJ::StationMinTurrets + Hash.NextInt(HCJ::StationMaxTurrets - HCJ::StationMinTurrets + 1);
		for (int32 Index = 0; Index < NumTurrets; ++Index)
		{
			Station.Turrets.Add(GStationMounts[Order[Index]]);
		}
		return Station;
	}
	return Station;
}

bool FHCJStation::IsAsteroidSuppressed(const FHCJAsteroid& Asteroid) const
{
	if (!bPresent)
	{
		return false;
	}
	const double Reach = double(Asteroid.BoundingRadius) + double(HCJ::StationRockFreeRadius);
	return FVector::DistSquared(Asteroid.LocalPosition, LocalPosition) < Reach * Reach;
}

uint32 FHCJStation::ComputeChecksum(uint32 InCrc) const
{
	if (!bPresent)
	{
		return InCrc;
	}
	uint32 Crc = HCJ::CrcVector(LocalPosition, InCrc);
	for (const FHCJTurretMount& Mount : Turrets)
	{
		Crc = HCJ::CrcVector(Mount.Location, Crc);
	}
	return Crc;
}
