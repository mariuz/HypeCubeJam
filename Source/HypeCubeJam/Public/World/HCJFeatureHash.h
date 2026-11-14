#pragma once

#include "CoreMinimal.h"
#include "Hypercube/HCJHypercubeGraph.h"

namespace HCJ
{
	/**
	 * Deterministic number sequence for world features that are placed AFTER the sector layouts exist (pickup pads,
	 * wormholes, stations). Each feature seeds its own stream from (match seed, feature salt, sector), so adding a
	 * feature never shifts the numbers of the layout generator or of another feature. Integer arithmetic plus one
	 * exact conversion: bit-identical on every machine.
	 */
	struct FFeatureHash
	{
		int32 State;
		int32 Counter = 0;
		int32 Salt;

		FFeatureHash(int32 MatchSeed, int32 FeatureSalt, int32 Index)
			: State(HCJ::MixSeed(HCJ::MixSeed(MatchSeed, FeatureSalt), Index))
			, Salt(FeatureSalt)
		{
		}

		/** Next value in [0, 1). */
		double Next()
		{
			State = HCJ::MixSeed(State, Salt + 0x01000193 + Counter++);
			return double(static_cast<uint32>(State) >> 8) * (1.0 / 16777216.0);
		}

		/** Next integer in [0, Num). */
		int32 NextInt(int32 Num)
		{
			return Num > 1 ? FMath::Min(static_cast<int32>(Next() * Num), Num - 1) : 0;
		}

		/**
		 * A point at distance [MinDistance, MaxDistance] from the origin in a uniformly random direction (rejection
		 * from the unit ball: only + * and sqrt). Returns false when this draw was rejected (it still consumed four numbers).
		 */
		bool NextPointInShell(double MinDistance, double MaxDistance, FVector& OutPoint)
		{
			// Every draw is its own statement (unspecified evaluation order otherwise).
			const double X = 2.0 * Next() - 1.0;
			const double Y = 2.0 * Next() - 1.0;
			const double Z = 2.0 * Next() - 1.0;
			const double DistanceRoll = Next();
			const double LengthSq = X * X + Y * Y + Z * Z;
			if (LengthSq > 1.0 || LengthSq < 0.01)
			{
				return false;
			}
			const double Distance = MinDistance + (MaxDistance - MinDistance) * DistanceRoll;
			OutPoint = FVector(X, Y, Z) * (Distance / FMath::Sqrt(LengthSq));
			return true;
		}
	};

	/** Chains the raw bits of a vector into a CRC32. */
	inline uint32 CrcVector(const FVector& Vector, uint32 Crc)
	{
		Crc = FCrc::MemCrc32(&Vector.X, sizeof(double), Crc);
		Crc = FCrc::MemCrc32(&Vector.Y, sizeof(double), Crc);
		return FCrc::MemCrc32(&Vector.Z, sizeof(double), Crc);
	}
}
