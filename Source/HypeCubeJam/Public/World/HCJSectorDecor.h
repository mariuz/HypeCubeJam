#pragma once

#include "CoreMinimal.h"

struct FHCJSectorLayout;

/** One decorative rock, in sector-local space. Drawn only. */
struct FHCJDecorRock
{
	FVector LocalPosition = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	/** Half of its largest side, uu. */
	float Radius = 10.0f;
	/** Which rock mesh, in [0, NumVariants). */
	uint8 Variant = 0;
};

/** A sphere (sector-local) that decorative pebbles keep out of: a pad, a wormhole mouth, a station, a black hole. */
struct FHCJDecorKeepOut
{
	FVector Center = FVector::ZeroVector;
	float Radius = 0.0f;
};

/**
 * Decorative rocks of one sector (devlog item 26): the dense field of the intro movie without touching the game.
 *
 * COSMETIC ONLY. A pure function of the finished layout (its seed), the keep-out spheres and the counts, with its own
 * integer hash (HCJ::MixSeed): it consumes nothing from the layout's random stream, is not part of any checksum, is
 * never replicated and has no collision. Every machine makes its own; two machines with the same settings get the
 * same rocks, two with different counts differ - which changes nothing but the picture. Bots, aim assist, traces
 * and line-of-sight tests never see them (the components that draw them have no collision at all).
 *
 *  - Pebbles: radius PebbleMinRadius..PebbleMaxRadius (below half a metre: nothing a pilot would steer around). About
 *    70 % of them hang around the real rocks (between 1.12 and 2.7 radii from the centre, denser near the surface),
 *    the rest float free inside the field. None within PortalKeepOut of a portal, SpawnKeepOut of a spawn point, or
 *    inside a keep-out sphere; none inside the bounding sphere of a real rock.
 *  - Belt: boulders of BeltMinRadius..BeltMaxRadius in a flattened ring around the sector whose every surface lies
 *    beyond BeltInnerLimit = HCJ::SectorKillRadius + 2000: no ship can reach them alive, so they need no collision.
 */
struct HYPECUBEJAM_API FHCJSectorDecor
{
	TArray<FHCJDecorRock> Pebbles;
	TArray<FHCJDecorRock> Belt;

	static constexpr float PebbleMinRadius = 7.0f;
	static constexpr float PebbleMaxRadius = 45.0f;
	static constexpr float PortalKeepOut = 1500.0f;
	static constexpr float SpawnKeepOut = 600.0f;
	static constexpr float BeltMinRadius = 250.0f;
	static constexpr float BeltMaxRadius = 4500.0f;
	static constexpr float BeltInnerLimit = 42000.0f;
	static constexpr float BeltOuterDistance = 120000.0f;

	/**
	 * @param NumPebbles   wanted pebbles (fewer when the keep-outs reject too many tries)
	 * @param NumBelt      boulders of the belt
	 * @param NumVariants  number of rock meshes (each rock picks one); <= 0 gives an empty result
	 */
	static FHCJSectorDecor Make(const FHCJSectorLayout& Layout, TConstArrayView<FHCJDecorKeepOut> KeepOut, int32 NumPebbles, int32 NumBelt, int32 NumVariants);
};
