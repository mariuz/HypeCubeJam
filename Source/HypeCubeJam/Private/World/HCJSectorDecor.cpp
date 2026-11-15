#include "World/HCJSectorDecor.h"

#include "Hypercube/HCJHypercubeGraph.h"
#include "World/HCJSectorLayout.h"

namespace
{
	/** Integer hash sequence in [0, 1); cosmetic, independent of every FRandomStream. */
	struct FHCJDecorHash
	{
		int32 State;
		int32 Counter = 0;

		explicit FHCJDecorHash(int32 Seed) : State(Seed) {}

		double Next()
		{
			State = HCJ::MixSeed(State, 0x0DEC0A01 + Counter++);
			return double(static_cast<uint32>(State) >> 8) * (1.0 / 16777216.0);
		}

		FVector NextDirection()
		{
			const double Z = 2.0 * Next() - 1.0;
			const double Phi = UE_DOUBLE_TWO_PI * Next();
			const double Ring = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
			return FVector(Ring * FMath::Cos(Phi), Ring * FMath::Sin(Phi), Z);
		}

		FQuat NextRotation()
		{
			const FVector Axis = NextDirection();
			return FQuat(Axis, UE_DOUBLE_TWO_PI * Next());
		}
	};
}

FHCJSectorDecor FHCJSectorDecor::Make(const FHCJSectorLayout& Layout, TConstArrayView<FHCJDecorKeepOut> KeepOut, int32 NumPebbles, int32 NumBelt, int32 NumVariants)
{
	FHCJSectorDecor Decor;
	if (Layout.SectorId == INDEX_NONE || NumVariants <= 0)
	{
		return Decor;
	}
	FHCJDecorHash Hash(HCJ::MixSeed(Layout.Seed, 0x0DEC0A00));

	const auto IsKeptOut = [&Layout, &KeepOut](const FVector& Position, float Radius)
	{
		for (const FHCJPortalSpec& Portal : Layout.Portals)
		{
			if (FVector::DistSquared(Position, Portal.LocalPosition) < FMath::Square(double(PortalKeepOut)))
			{
				return true;
			}
		}
		for (const FTransform& Spawn : Layout.SpawnPoints)
		{
			if (FVector::DistSquared(Position, Spawn.GetLocation()) < FMath::Square(double(SpawnKeepOut)))
			{
				return true;
			}
		}
		for (const FHCJDecorKeepOut& Sphere : KeepOut)
		{
			if (FVector::DistSquared(Position, Sphere.Center) < FMath::Square(double(Sphere.Radius)))
			{
				return true;
			}
		}
		// Not inside a real rock (it would not be seen).
		return !Layout.IsLocationClear(Position, Radius);
	};

	// ---- pebbles ----
	const int32 NumAsteroids = Layout.Asteroids.Num();
	Decor.Pebbles.Reserve(FMath::Max(NumPebbles, 0));
	for (int32 Index = 0; Index < NumPebbles; ++Index)
	{
		// Every draw is its own statement, and every pebble makes the same number of draws per try.
		const double KindRoll = Hash.Next();
		const double SizeRoll = Hash.Next();
		const int32 VariantRoll = FMath::Min(int32(Hash.Next() * NumVariants), NumVariants - 1);
		const FQuat Rotation = Hash.NextRotation();
		// Most pebbles are small: the cube of the roll.
		const float Radius = FMath::Lerp(PebbleMinRadius, PebbleMaxRadius, float(SizeRoll * SizeRoll * SizeRoll));
		for (int32 Attempt = 0; Attempt < 4; ++Attempt)
		{
			const FVector Direction = Hash.NextDirection();
			const double DistanceRoll = Hash.Next();
			const double PickRoll = Hash.Next();
			FVector Position;
			if (KindRoll < 0.7 && NumAsteroids > 0)
			{
				const FHCJAsteroid& Asteroid = Layout.Asteroids[FMath::Min(int32(PickRoll * NumAsteroids), NumAsteroids - 1)];
				Position = Asteroid.LocalPosition + Direction * (double(Asteroid.BoundingRadius) * (1.12 + 1.58 * DistanceRoll * DistanceRoll) + Radius + 40.0);
			}
			else
			{
				// Uniform in the ball of the field.
				Position = Direction * (double(HCJ::SectorFieldRadius) * FMath::Pow(DistanceRoll, 1.0 / 3.0));
			}
			if (Position.SizeSquared() <= FMath::Square(double(HCJ::SectorFieldRadius) * 1.1) && !IsKeptOut(Position, Radius))
			{
				FHCJDecorRock& Rock = Decor.Pebbles.AddDefaulted_GetRef();
				Rock.LocalPosition = Position;
				Rock.Rotation = Rotation;
				Rock.Radius = Radius;
				Rock.Variant = static_cast<uint8>(VariantRoll);
				break;
			}
		}
	}

	// ---- belt: a flattened ring around a per-sector axis, wholly outside the kill radius ----
	const FVector Axis = Hash.NextDirection();
	Decor.Belt.Reserve(FMath::Max(NumBelt, 0));
	for (int32 Index = 0; Index < NumBelt; ++Index)
	{
		const FVector Raw = Hash.NextDirection();
		const double DistanceRoll = Hash.Next();
		const double SizeRoll = Hash.Next();
		const int32 VariantRoll = FMath::Min(int32(Hash.Next() * NumVariants), NumVariants - 1);
		const FQuat Rotation = Hash.NextRotation();

		// Squash the direction towards the plane of the belt.
		const FVector Direction = (Raw - Axis * (0.72 * FVector::DotProduct(Raw, Axis))).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const float Radius = FMath::Lerp(BeltMinRadius, BeltMaxRadius, float(SizeRoll * SizeRoll * SizeRoll));
		const double Inner = double(BeltInnerLimit) + Radius;
		const double Distance = FMath::Lerp(Inner, FMath::Max(double(BeltOuterDistance), Inner), FMath::Pow(DistanceRoll, 1.4));
		FHCJDecorRock& Rock = Decor.Belt.AddDefaulted_GetRef();
		Rock.LocalPosition = Direction * Distance;
		Rock.Rotation = Rotation;
		Rock.Radius = Radius;
		Rock.Variant = static_cast<uint8>(VariantRoll);
	}
	return Decor;
}
