#include "World/HCJSectorLayout.h"

#include "Hypercube/HCJHypercubeGraph.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"

namespace
{
	/** Slack in uu kept inside every generation constraint so float rounding can never violate a guarantee. */
	constexpr float HCJGenMargin = 1.0f;

	/** Radius of the deterministic fallback spawn ring (inside [SpawnMinDistance, SpawnMaxDistance]). */
	constexpr float HCJFallbackSpawnRadius = 7500.0f;

	// NOTE: every value that feeds an accept/reject comparison (asteroid, portal and spawn positions) is built from
	// + - * / and sqrt only, which IEEE 754 rounds identically everywhere. No pow/acos/atan2/sin/cos there: their
	// last bit differs between C runtimes and would let two machines disagree on a candidate.
	// NOTE: every random draw below is its own statement. Never put two Stream calls into one expression or
	// argument list: their evaluation order would be unspecified and the layout would stop being deterministic.

	float RandFloat(const FRandomStream& Stream, float Min, float Max)
	{
		return float(Stream.FRandRange(Min, Max));
	}

	FQuat RandomRotation(const FRandomStream& Stream)
	{
		const FVector Axis = Stream.VRand();
		const float Angle = RandFloat(Stream,0.0f, UE_TWO_PI);
		return FQuat(Axis, Angle);
	}

	/** Random point in a ball of radius MaxDistance, biased a little towards the centre. */
	FVector RandomPointInBall(const FRandomStream& Stream, float MaxDistance)
	{
		const FVector Direction = Stream.VRand();
		// sqrt (exactly rounded) instead of the uniform cube root: biased towards the centre and portable.
		const double Fraction = FMath::Sqrt(double(Stream.FRand()));
		return Direction * (double(MaxDistance) * Fraction);
	}

	FVector RandomScale(const FRandomStream& Stream, EHCJAsteroidShape Shape, float MinScale, float MaxScale, float MaxElongation)
	{
		const float Base = RandFloat(Stream,MinScale, MaxScale);
		if (Shape == EHCJAsteroidShape::Sphere)
		{
			return FVector(Base);
		}
		const float X = Base * RandFloat(Stream,0.6f, 1.4f);
		const float Y = Base * RandFloat(Stream,0.6f, 1.4f);
		float Z = Base * RandFloat(Stream,0.6f, 1.4f);
		if (MaxElongation > 1.0f)
		{
			Z *= RandFloat(Stream,0.5f * (1.0f + MaxElongation), MaxElongation);
		}
		return FVector(X, Y, Z);
	}

	/** Picks a shape from weights (Sphere, Cube, Cone) that sum to 1. */
	EHCJAsteroidShape RandomShape(const FRandomStream& Stream, float SphereWeight, float CubeWeight)
	{
		const float Roll = Stream.FRand();
		if (Roll < SphereWeight)
		{
			return EHCJAsteroidShape::Sphere;
		}
		return Roll < SphereWeight + CubeWeight ? EHCJAsteroidShape::Cube : EHCJAsteroidShape::Cone;
	}

	/** Tries a few positions for one asteroid; drops it when all of them crowd a portal. */
	void PlaceAsteroid(const FRandomStream& Stream, FHCJSectorLayout& Layout, EHCJAsteroidShape Shape, const FVector& Scale3D)
	{
		FHCJAsteroid Asteroid;
		Asteroid.Shape = Shape;
		Asteroid.Scale3D = Scale3D;
		Asteroid.BoundingRadius = FHCJAsteroid::ComputeBoundingRadius(Shape, Scale3D);
		Asteroid.Rotation = RandomRotation(Stream);

		const float MaxDistance = HCJ::SectorFieldRadius - Asteroid.BoundingRadius - HCJGenMargin;
		if (MaxDistance <= 0.0f)
		{
			return;
		}

		const double PortalClearance = double(Asteroid.BoundingRadius) + HCJ::PortalClearRadius + HCJGenMargin;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			const FVector Candidate = RandomPointInBall(Stream, MaxDistance);
			bool bClearOfPortals = true;
			for (const FHCJPortalSpec& Portal : Layout.Portals)
			{
				if (FVector::DistSquared(Candidate, Portal.LocalPosition) < FMath::Square(PortalClearance))
				{
					bClearOfPortals = false;
					break;
				}
			}
			if (bClearOfPortals)
			{
				Asteroid.LocalPosition = Candidate;
				Layout.Asteroids.Add(Asteroid);
				return;
			}
		}
	}

	void GenerateAsteroids(const FRandomStream& Stream, FHCJSectorLayout& Layout)
	{
		switch (Layout.Theme)
		{
		case EHCJSectorTheme::Dense:
		{
			const int32 Count = Stream.RandRange(260, 340);
			Layout.Asteroids.Reserve(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const EHCJAsteroidShape Shape = RandomShape(Stream, 0.5f, 0.3f);
				const FVector Scale = RandomScale(Stream, Shape, 2.0f, 8.0f, 1.0f);
				PlaceAsteroid(Stream, Layout, Shape, Scale);
			}
			break;
		}
		case EHCJSectorTheme::Giants:
		{
			const int32 NumGiants = Stream.RandRange(6, 10);
			const int32 NumDebris = Stream.RandRange(120, 160);
			Layout.Asteroids.Reserve(NumGiants + NumDebris);
			for (int32 Index = 0; Index < NumGiants; ++Index)
			{
				const FVector Scale = RandomScale(Stream, EHCJAsteroidShape::Sphere, 30.0f, 60.0f, 1.0f);
				PlaceAsteroid(Stream, Layout, EHCJAsteroidShape::Sphere, Scale);
			}
			for (int32 Index = 0; Index < NumDebris; ++Index)
			{
				const EHCJAsteroidShape Shape = RandomShape(Stream, 0.4f, 0.4f);
				const FVector Scale = RandomScale(Stream, Shape, 2.0f, 7.0f, 1.0f);
				PlaceAsteroid(Stream, Layout, Shape, Scale);
			}
			break;
		}
		case EHCJSectorTheme::Shards:
		{
			const int32 Count = Stream.RandRange(160, 220);
			Layout.Asteroids.Reserve(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const EHCJAsteroidShape Shape = RandomShape(Stream, 0.0f, 0.6f);
				const FVector Scale = RandomScale(Stream, Shape, 2.0f, 6.0f, 6.0f);
				PlaceAsteroid(Stream, Layout, Shape, Scale);
			}
			break;
		}
		case EHCJSectorTheme::Sparse:
		default:
		{
			const int32 Count = Stream.RandRange(90, 130);
			Layout.Asteroids.Reserve(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const EHCJAsteroidShape Shape = RandomShape(Stream, 0.5f, 0.3f);
				const FVector Scale = RandomScale(Stream, Shape, 4.0f, 14.0f, 1.0f);
				PlaceAsteroid(Stream, Layout, Shape, Scale);
			}
			break;
		}
		}
	}

	FTransform MakeSpawnTransform(const FVector& LocalPosition)
	{
		// FVector::Rotation() yields pitch + yaw only (roll 0).
		return FTransform((-LocalPosition).Rotation(), LocalPosition);
	}

	void GenerateSpawnPoints(const FRandomStream& Stream, FHCJSectorLayout& Layout)
	{
		Layout.SpawnPoints.Reserve(HCJ::NumSpawnPoints);

		const double PortalClearanceSq = FMath::Square(double(HCJ::PortalClearRadius) + HCJGenMargin);
		const double SeparationSq = FMath::Square(double(HCJ::SpawnSeparation) + HCJGenMargin);

		for (int32 Attempt = 0; Attempt < 4000 && Layout.SpawnPoints.Num() < HCJ::NumSpawnPoints; ++Attempt)
		{
			const FVector Direction = Stream.VRand();
			const float Distance = RandFloat(Stream,HCJ::SpawnMinDistance + HCJGenMargin, HCJ::SpawnMaxDistance - HCJGenMargin);
			const FVector Candidate = Direction * double(Distance);

			if (!Layout.IsLocationClear(Candidate, HCJ::SpawnClearance + HCJGenMargin))
			{
				continue;
			}

			bool bAccept = true;
			for (const FHCJPortalSpec& Portal : Layout.Portals)
			{
				if (FVector::DistSquared(Candidate, Portal.LocalPosition) < PortalClearanceSq)
				{
					bAccept = false;
					break;
				}
			}
			for (int32 Index = 0; bAccept && Index < Layout.SpawnPoints.Num(); ++Index)
			{
				bAccept = FVector::DistSquared(Candidate, Layout.SpawnPoints[Index].GetLocation()) >= SeparationSq;
			}
			if (bAccept)
			{
				Layout.SpawnPoints.Add(MakeSpawnTransform(Candidate));
			}
		}

		if (Layout.SpawnPoints.Num() == HCJ::NumSpawnPoints)
		{
			return;
		}

		// Practically unreachable (the field is far too sparse), but the count is a hard guarantee: fall back to
		// an evenly spread ring of points and delete whatever rock is in their way. Portals are >= 6500 uu away.
		Layout.SpawnPoints.Reset();
		const double GoldenAngle = UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		for (int32 Index = 0; Index < HCJ::NumSpawnPoints; ++Index)
		{
			const double Z = 1.0 - (2.0 * Index + 1.0) / double(HCJ::NumSpawnPoints);
			const double Ring = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
			const double Phi = GoldenAngle * Index;
			const FVector Position = FVector(Ring * FMath::Cos(Phi), Ring * FMath::Sin(Phi), Z) * double(HCJFallbackSpawnRadius);

			Layout.Asteroids.RemoveAll([&Position](const FHCJAsteroid& Asteroid)
			{
				const double Reach = double(Asteroid.BoundingRadius) + HCJ::SpawnClearance + HCJGenMargin;
				return FVector::DistSquared(Position, Asteroid.LocalPosition) < FMath::Square(Reach);
			});
			Layout.SpawnPoints.Add(MakeSpawnTransform(Position));
		}
	}
}

int32 HCJ::PickAsteroidVariant(int32 SectorSeed, int32 AsteroidIndex, int32 NumVariants)
{
	if (NumVariants <= 1)
	{
		return 0;
	}
	const uint32 Hash = static_cast<uint32>(HCJ::MixSeed(HCJ::MixSeed(SectorSeed, 0x0A57E401), AsteroidIndex));
	return static_cast<int32>(Hash % static_cast<uint32>(NumVariants));
}

float FHCJAsteroid::ComputeBoundingRadius(EHCJAsteroidShape InShape, const FVector& InScale3D)
{
	const float HalfSize = 0.5f * HCJ::BasicShapeSize;
	return InShape == EHCJAsteroidShape::Sphere
		? HalfSize * float(FMath::Abs(InScale3D.X))
		: HalfSize * float(InScale3D.Size());
}

FTransform FHCJAsteroid::ComputeVisualTransform(const FBox& MeshBounds, float CornerFactor, float SphereFill) const
{
	const FVector Center = MeshBounds.GetCenter();
	const FVector Extent = MeshBounds.GetExtent().ComponentMax(FVector(0.01));
	const double HalfSize = 0.5 * HCJ::BasicShapeSize;

	// Half-size, along the asteroid's own axes, of the box the mesh bounds are fitted to.
	FVector Target;
	switch (Shape)
	{
	case EHCJAsteroidShape::Sphere:
	{
		const double Radius = FMath::Max(HalfSize * FMath::Abs(Scale3D.X), 1.0);
		const double Fill = FMath::Min(double(SphereFill), 1.0 + double(HCJ::MaxVisualOverhang) / Radius);
		Target = FVector(Radius * Fill / FMath::Max(double(CornerFactor), 1.0));
		break;
	}
	case EHCJAsteroidShape::Cone:
		Target = Scale3D.GetAbs() * FVector(HCJ::ConeVisualWidth, HCJ::ConeVisualWidth, 1.0) * HalfSize;
		break;
	case EHCJAsteroidShape::Cube:
	default:
		Target = Scale3D.GetAbs() * HalfSize;
		break;
	}

	const FVector VisualScale = Target / Extent;
	return FTransform(Rotation, LocalPosition - Rotation.RotateVector(VisualScale * Center), VisualScale);
}

FHCJSectorLayout FHCJSectorLayout::Generate(int32 MatchSeed, int32 SectorId, const FHCJHypercubeGraph& Graph)
{
	FHCJSectorLayout Layout;
	if (!Graph.IsBuilt() || !Graph.IsValidSector(SectorId))
	{
		return Layout;
	}

	Layout.SectorId = SectorId;
	Layout.Seed = MakeSectorSeed(MatchSeed, SectorId);

	const FRandomStream Stream(Layout.Seed);

	Layout.Theme = static_cast<EHCJSectorTheme>(Stream.RandRange(0, int32(EHCJSectorTheme::Count) - 1));

	const float Hue = RandFloat(Stream,0.0f, 360.0f);
	const float Saturation = RandFloat(Stream,0.7f, 1.0f);
	const float Value = RandFloat(Stream,0.85f, 1.0f);
	Layout.Tint = FLinearColor(Hue, Saturation, Value, 1.0f).HSVToLinearRGB();
	Layout.Tint.A = 1.0f;

	// Portals first: they consume nothing from Stream and the asteroids must keep clear of them.
	for (const int32 Dim : Graph.GetOpenDims(SectorId))
	{
		FHCJPortalSpec& Portal = Layout.Portals.AddDefaulted_GetRef();
		Portal.Dim = Dim;
		Portal.DestSector = Graph.GetNeighbor(SectorId, Dim);
		Portal.LocalPosition = ComputePortalLocalPosition(MatchSeed, SectorId, Dim);
	}

	GenerateAsteroids(Stream, Layout);
	GenerateSpawnPoints(Stream, Layout);
	return Layout;
}

int32 FHCJSectorLayout::MakeSectorSeed(int32 MatchSeed, int32 SectorId)
{
	return HCJ::MixSeed(MatchSeed, 0x1000 + SectorId);
}

FVector FHCJSectorLayout::ComputePortalLocalPosition(int32 MatchSeed, int32 SectorId, int32 Dim)
{
	const FVector DimensionDirection = FHCJHypercubeGraph::GetDimensionDirection(Dim);
	if (DimensionDirection.IsZero())
	{
		return FVector::ZeroVector;
	}

	const int32 Bit = 1 << Dim;
	const int32 Low = SectorId & ~Bit;
	const FRandomStream EdgeStream(HCJ::MixSeed(HCJ::MixSeed(MatchSeed, Low), 0x50000 + Dim));

	FVector BasisU;
	FVector BasisV;
	FHCJHypercubeGraph::GetDimensionJitterBasis(Dim, BasisU, BasisV);

	// Tilt the dimension direction by a random offset in its tangent plane (see the NOTE at the top of the file).
	const double JitterU = EdgeStream.FRandRange(-1.0, 1.0);
	const double JitterV = EdgeStream.FRandRange(-1.0, 1.0);
	const FVector Tilted = DimensionDirection + (BasisU * JitterU + BasisV * JitterV) * HCJ::PortalJitterTangent;
	const double TiltedLength = FMath::Sqrt(FVector::DotProduct(Tilted, Tilted));
	const double Distance = EdgeStream.FRandRange(double(HCJ::PortalMinDistance), double(HCJ::PortalMaxDistance));
	const FVector Position = Tilted * (Distance / TiltedLength);

	// Both ends compute the same Position and differ only by this exact negation: the mirror rule is bit-exact.
	return (SectorId & Bit) == 0 ? Position : -Position;
}

const FHCJPortalSpec* FHCJSectorLayout::FindPortal(int32 Dim) const
{
	return Portals.FindByPredicate([Dim](const FHCJPortalSpec& Portal) { return Portal.Dim == Dim; });
}

uint32 FHCJSectorLayout::ComputeChecksum(uint32 InCrc) const
{
	// Field by field: hashing whole structs would pull in their padding bytes.
	uint32 Crc = InCrc;
	const auto Mix = [&Crc](const auto& Value) { Crc = FCrc::MemCrc32(&Value, sizeof(Value), Crc); };
	const auto MixVector = [&Mix](const FVector& Value) { Mix(Value.X); Mix(Value.Y); Mix(Value.Z); };
	const auto MixQuat = [&Mix](const FQuat& Value) { Mix(Value.X); Mix(Value.Y); Mix(Value.Z); Mix(Value.W); };

	Mix(SectorId);
	Mix(Seed);
	Mix(Theme);

	Mix(Asteroids.Num());
	for (const FHCJAsteroid& Asteroid : Asteroids)
	{
		Mix(Asteroid.Shape);
		MixVector(Asteroid.LocalPosition);
		MixQuat(Asteroid.Rotation);
		MixVector(Asteroid.Scale3D);
		Mix(Asteroid.BoundingRadius);
	}

	Mix(Portals.Num());
	for (const FHCJPortalSpec& Portal : Portals)
	{
		Mix(Portal.Dim);
		Mix(Portal.DestSector);
		MixVector(Portal.LocalPosition);
	}

	Mix(SpawnPoints.Num());
	for (const FTransform& SpawnPoint : SpawnPoints)
	{
		MixVector(SpawnPoint.GetLocation());
		MixQuat(SpawnPoint.GetRotation());
	}
	return Crc;
}

bool FHCJSectorLayout::IsLocationClear(const FVector& LocalPosition, float Clearance) const
{
	for (const FHCJAsteroid& Asteroid : Asteroids)
	{
		const double Reach = double(Asteroid.BoundingRadius) + Clearance;
		if (Reach > 0.0 && FVector::DistSquared(LocalPosition, Asteroid.LocalPosition) < FMath::Square(Reach))
		{
			return false;
		}
	}
	return true;
}
