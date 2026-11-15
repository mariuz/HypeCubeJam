#pragma once

#include "CoreMinimal.h"

/**
 * Explosions: the plain rules behind the shockwave that pushes ships (server) and behind the local, cosmetic debris,
 * flares and camera shake (UHCJEffectsDirector). No world involved, so they are tested on their own.
 */
namespace HCJShockwave
{
	/**
	 * Speed (uu/s) a shockwave adds to something at Distance from its centre: PeakSpeed at the centre, falling to
	 * nothing at Radius ((1 - d / R) squared: most of the push is near the blast), 0 beyond.
	 */
	HYPECUBEJAM_API float ImpulseSpeed(float Distance, float Radius, float PeakSpeed);

	/** The change of velocity of something at Location: ImpulseSpeed straight away from Origin (along +X when it sits exactly on it). */
	HYPECUBEJAM_API FVector ComputeImpulse(const FVector& Origin, const FVector& Location, float Radius, float PeakSpeed);

	/** False with -HCJNoShockwaves (measurement aid: explosions push no ships). */
	HYPECUBEJAM_API bool IsEnabled();
}

/** One piece of debris: moved by plain integration, drawn as an instance of a small mesh, never replicated, no collision of its own. */
struct FHCJDebrisPiece
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	/** Tumble: axis (unit), angle (degrees) and degrees per second. */
	FVector Axis = FVector::UpVector;
	float Angle = 0.0f;
	float AngularSpeed = 0.0f;
	FVector Scale = FVector::OneVector;
	float Age = 0.0f;
	float Life = 1.0f;
	/** Which mesh it is drawn with (index into the director's debris meshes). */
	uint8 Kind = 0;
	bool bActive = false;
};

/**
 * The debris of one machine: a fixed number of slots. A new piece takes a free slot, or the slot of the oldest
 * piece when all are taken, so the count never exceeds the capacity whatever happens on the screen.
 */
struct HYPECUBEJAM_API FHCJDebrisPool
{
	/** Drops everything and sets the number of slots. */
	void SetCapacity(int32 InCapacity);
	int32 GetCapacity() const { return Pieces.Num(); }
	int32 NumActive() const { return ActiveCount; }
	const TArray<FHCJDebrisPiece>& GetPieces() const { return Pieces; }
	FHCJDebrisPiece& GetPiece(int32 Index) { return Pieces[Index]; }

	/** Adds a piece; returns its slot, or INDEX_NONE when the capacity is 0. */
	int32 Spawn(const FHCJDebrisPiece& Piece);

	/**
	 * One step for every piece: age, expiry, position from velocity, velocity damped by Drag (share lost per second),
	 * tumble. Returns the number of pieces still active.
	 */
	int32 Step(float DeltaSeconds, float Drag);

	/** A shockwave: every active piece inside Radius gets HCJShockwave::ComputeImpulse added to its velocity. */
	void ApplyShockwave(const FVector& Origin, float Radius, float PeakSpeed);

	/** Size factor of a piece of that age: 1 for the first two thirds of its life, then shrinking to 0 (the fade-out). */
	static float SizeFactor(float Age, float Life);

private:
	TArray<FHCJDebrisPiece> Pieces;
	int32 ActiveCount = 0;
};

namespace HCJEffects
{
	/** Camera shake (0..Strength) of an explosion at Distance from the view: full within a tenth of MaxDistance, falling in a straight line to 0 at MaxDistance. */
	HYPECUBEJAM_API float ShakeAt(float Distance, float MaxDistance, float Strength);

	/** A count for something of size Radius when BaseCount is right for ReferenceRadius: proportional, within [MinCount, MaxCount]. */
	HYPECUBEJAM_API int32 ScaleCount(int32 BaseCount, float Radius, float ReferenceRadius, int32 MinCount, int32 MaxCount);

	/** How much of a ship's staged explosion a machine draws. */
	enum EBlastDetail : int32
	{
		/** Every stage: pops, flash, fireball of four puffs (the first ends as the smoke), ring, sparks, plates, burning pieces. */
		BlastFull = 0,
		/** No burning pieces, one lobe, half the pieces. */
		BlastMedium = 1,
		/** Flash, one puff, a few pieces. */
		BlastLow = 2
	};

	/**
	 * The detail for a ship that blows up at Distance from the view while NumRunning other blasts are in their first
	 * second: full within FullDistance, medium within MediumDistance, low beyond; with two others running only a blast
	 * within 40 % of FullDistance stays full, with four or more nothing is better than medium and whatever is beyond
	 * FullDistance is low. The own ship is always full.
	 */
	HYPECUBEJAM_API int32 PickBlastDetail(float Distance, bool bOwnShip, int32 NumRunning, float FullDistance, float MediumDistance);

	/**
	 * The stages of a ship's death and when they run (seconds after the death; FailSeconds is how long the hull takes
	 * to fail): 0..2 three pops along the hull (at 0, 34 % and 68 % of FailSeconds), 3 the blast (at FailSeconds),
	 * 4..6 the lobes of the fireball (0.07, 0.15 and 0.27 s later).
	 */
	inline constexpr int32 NumBlastStages = 7;
	inline constexpr int32 BlastMainStage = 3;
	HYPECUBEJAM_API float BlastStageTime(int32 Stage, float FailSeconds);

	/** Heat (1 = glowing .. 0 = cold) of a hull plate of that age (seconds): it cools over about a second and a half, fast at first. */
	HYPECUBEJAM_API float PlateHeat(float Age);
}
