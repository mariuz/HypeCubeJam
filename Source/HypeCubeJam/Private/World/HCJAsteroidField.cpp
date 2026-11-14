#include "World/HCJAsteroidField.h"

#include "Misc/Crc.h"
#include "World/HCJFeatureHash.h"

namespace
{
	/** Own hash stream of the pieces (see HCJ::FFeatureHash): nothing else draws from it. */
	constexpr int32 HCJAsteroidSplitSalt = 0x7A51D;

	/** A unit vector from the hash by rejection from the cube (only + * and sqrt); false after a rejected draw. */
	bool NextDirection(HCJ::FFeatureHash& Hash, FVector& OutDirection)
	{
		// Every draw is its own statement (unspecified evaluation order otherwise).
		const double X = 2.0 * Hash.Next() - 1.0;
		const double Y = 2.0 * Hash.Next() - 1.0;
		const double Z = 2.0 * Hash.Next() - 1.0;
		const double LengthSq = X * X + Y * Y + Z * Z;
		if (LengthSq > 1.0 || LengthSq < 0.01)
		{
			return false;
		}
		const double Inverse = 1.0 / FMath::Sqrt(LengthSq);
		OutDirection = FVector(X * Inverse, Y * Inverse, Z * Inverse);
		return true;
	}
}

bool HCJAsteroids::CanSplit(float BoundingRadius, int32 Generation, const FHCJAsteroidRules& Rules)
{
	return Generation >= 0 && Generation < MaxGeneration && BoundingRadius >= Rules.SplitMinRadius && Rules.SplitMinRadius > 0.0f;
}

float HCJAsteroids::ComputeHealth(float BoundingRadius, const FHCJAsteroidRules& Rules)
{
	return FMath::Clamp(BoundingRadius * Rules.HealthPerRadius, Rules.MinHealth, FMath::Max(Rules.MaxHealth, Rules.MinHealth));
}

void HCJAsteroids::MakeChildren(int32 SectorSeed, uint16 ParentId, const FHCJAsteroid& Parent, const FHCJAsteroidRules& Rules, TArray<FHCJAsteroid>& OutChildren)
{
	OutChildren.Reset();
	if (!CanSplit(Parent.BoundingRadius, GetGeneration(ParentId), Rules))
	{
		return;
	}

	HCJ::FFeatureHash Hash(SectorSeed, HCJAsteroidSplitSalt, static_cast<int32>(ParentId));
	const int32 Count = Hash.Next() < 0.5 ? 2 : 3;
	const double ScaleMin = FMath::Clamp(static_cast<double>(Rules.ChildScaleMin), 0.2, 0.7);
	const double ScaleMax = FMath::Clamp(static_cast<double>(Rules.ChildScaleMax), ScaleMin, 0.7);

	// Half extents of the parent's own shape in its own frame: a sphere of its radius, the box of a cube, a narrower box for a cone.
	const FVector Scale = Parent.Scale3D.GetAbs();
	const double HalfSize = 0.5 * HCJ::BasicShapeSize;
	const FVector Extent = Parent.Shape == EHCJAsteroidShape::Sphere ? FVector(HalfSize * Scale.X)
		: Parent.Shape == EHCJAsteroidShape::Cone ? FVector(0.5 * HalfSize * Scale.X, 0.5 * HalfSize * Scale.Y, 0.6 * HalfSize * Scale.Z)
		: Scale * HalfSize;

	FVector Directions[MaxChildren];
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		const double Fraction = ScaleMin + (ScaleMax - ScaleMin) * Hash.Next();

		// A direction away from the pieces before it, so that they do not sit in each other; a few tries, then the axis of the slot.
		FVector Direction = Slot == 0 ? FVector::ForwardVector : Slot == 1 ? FVector::BackwardVector : FVector::UpVector;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			FVector Candidate;
			if (!NextDirection(Hash, Candidate))
			{
				continue;
			}
			bool bApart = true;
			for (int32 Other = 0; Other < Slot; ++Other)
			{
				bApart = bApart && FVector::DotProduct(Candidate, Directions[Other]) < 0.25;
			}
			if (bApart)
			{
				Direction = Candidate;
				break;
			}
		}
		Directions[Slot] = Direction;
		const double Reach = 0.6 + 0.35 * Hash.Next();

		FHCJAsteroid& Child = OutChildren.AddDefaulted_GetRef();
		Child.Shape = Parent.Shape;
		Child.Rotation = Parent.Rotation;
		Child.Scale3D = Parent.Scale3D * Fraction;
		Child.BoundingRadius = FHCJAsteroid::ComputeBoundingRadius(Child.Shape, Child.Scale3D);
		// Inside the parent: a sphere of radius f R within (1 - f) R of the centre, a box of f E within (1 - f) E per axis.
		const FVector LocalOffset = Parent.Shape == EHCJAsteroidShape::Sphere
			? Direction * (Extent.X * (1.0 - Fraction) * Reach)
			: Direction * Extent * ((1.0 - Fraction) * Reach);
		Child.LocalPosition = Parent.LocalPosition + Parent.Rotation.RotateVector(LocalOffset);
	}
}

bool HCJAsteroids::ResolveNode(const FHCJSectorLayout& Layout, uint16 Id, const FHCJAsteroidRules& Rules, FHCJAsteroid& OutAsteroid)
{
	const int32 BaseIndex = GetBaseIndex(Id);
	if (!IsValidId(Id) || !Layout.Asteroids.IsValidIndex(BaseIndex))
	{
		return false;
	}
	FHCJAsteroid Node = Layout.Asteroids[BaseIndex];
	uint16 NodeId = MakeId(BaseIndex);
	TArray<FHCJAsteroid> Children;
	for (int32 Generation = 1; Generation <= GetGeneration(Id); ++Generation)
	{
		const int32 Slot = GetSlot(Id, Generation);
		MakeChildren(Layout.Seed, NodeId, Node, Rules, Children);
		if (!Children.IsValidIndex(Slot - 1))
		{
			return false;
		}
		Node = Children[Slot - 1];
		NodeId = MakeChildId(NodeId, Slot);
	}
	OutAsteroid = Node;
	return true;
}

void HCJAsteroids::CollectPieces(int32 Sector, int32 SectorSeed, uint16 ParentId, const FHCJAsteroid& Parent, const FHCJAsteroidRules& Rules,
	const FHCJDestroyedSet& Destroyed, TArray<TPair<uint16, FHCJAsteroid>>& OutPieces)
{
	TArray<FHCJAsteroid> Children;
	MakeChildren(SectorSeed, ParentId, Parent, Rules, Children);
	for (int32 Slot = 1; Slot <= Children.Num(); ++Slot)
	{
		const uint16 ChildId = MakeChildId(ParentId, Slot);
		if (Destroyed.Contains(PackKey(Sector, ChildId)))
		{
			// Already gone: what it left behind takes its place (two generations at most, so this ends).
			CollectPieces(Sector, SectorSeed, ChildId, Children[Slot - 1], Rules, Destroyed, OutPieces);
		}
		else
		{
			OutPieces.Emplace(ChildId, Children[Slot - 1]);
		}
	}
}

uint32 HCJAsteroids::ComputeDestroyedChecksum(TConstArrayView<uint32> Keys)
{
	if (Keys.Num() == 0)
	{
		return 0;
	}
	TArray<uint32> Sorted(Keys.GetData(), Keys.Num());
	Sorted.Sort();
	return FCrc::MemCrc32(Sorted.GetData(), Sorted.Num() * sizeof(uint32), 0);
}

bool FHCJDestroyedSet::Add(uint32 Key)
{
	bool bAlready = false;
	Keys.Add(Key, &bAlready);
	return !bAlready;
}

bool FHCJDestroyedSet::IsAlive(uint32 Key) const
{
	if (Keys.Contains(Key))
	{
		return false;
	}
	const int32 Sector = HCJAsteroids::GetKeySector(Key);
	uint16 Id = HCJAsteroids::GetKeyId(Key);
	while (HCJAsteroids::GetGeneration(Id) > 0)
	{
		Id = HCJAsteroids::GetParentId(Id);
		if (!Keys.Contains(HCJAsteroids::PackKey(Sector, Id)))
		{
			return false;
		}
	}
	return true;
}
