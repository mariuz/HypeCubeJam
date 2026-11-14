#pragma once

#include "CoreMinimal.h"

/**
 * The four shield facings of a ship, in its own frame (X forward, Y right). Plain data and pure functions (tested on
 * their own): which facing a hit lands on, how a facing absorbs damage, how damage without a direction and pickups
 * spread, how the four strengths travel over the network, which facing a ship under fire should show its attacker.
 * UHCJHealthComponent holds the values and the server runs the rules; clients only decode and draw.
 */
enum class EHCJShieldFacing : uint8
{
	Fore = 0,
	Aft,
	Port,
	Starboard,
	Count
};

/** What one hit did to a ship's shield (server; statistics and the bubble flare). */
struct FHCJShieldHit
{
	/** Points the shield took / points that went on to the hull. */
	float ToShield = 0.0f;
	float ToHull = 0.0f;
	/** Bit (1 << facing) for every facing this hit brought down. */
	uint8 CollapsedMask = 0;
	/** Strength of the facing after the hit as a fraction of its capacity (0..1; the weakest for spread damage). */
	float FractionAfter = 0.0f;
};

namespace HCJShield
{
	inline constexpr int32 NumFacings = static_cast<int32>(EHCJShieldFacing::Count);

	/** A replicated byte covers 0 .. QuantRange times the capacity of a facing (the room an overcharge needs). */
	inline constexpr float QuantRange = 2.5f;

	/** A facing at or below this fraction of its capacity counts as weak (HUD colour, bots). */
	inline constexpr float WeakFraction = 0.35f;

	/**
	 * The facing a hit lands on. LocalDirection points from the ship's centre to where the hit landed (or came from)
	 * in the ship's frame; it need not be normalised. Quarters of the horizontal circle: within 45 degrees of the
	 * nose is fore, of the tail aft, else port (left, -Y) or starboard (right, +Y); the vertical part is ignored.
	 * A hit from straight above or below (and a zero vector) counts as fore. Pure.
	 */
	HYPECUBEJAM_API EHCJShieldFacing SelectFacing(const FVector& LocalDirection);

	/** Unit vector of the facing in the ship's frame, its yaw there in degrees (fore 0, starboard 90, aft 180, port -90) and its name. */
	HYPECUBEJAM_API FVector FacingAxis(EHCJShieldFacing Facing);
	HYPECUBEJAM_API float FacingYawDegrees(EHCJShieldFacing Facing);
	HYPECUBEJAM_API const TCHAR* FacingName(EHCJShieldFacing Facing);

	/**
	 * Damage on one facing: the facing takes what it has, the rest is returned (it goes to the hull). A facing that is
	 * down takes nothing. bOutCollapsed: this hit brought it from above zero to zero. Pure.
	 */
	HYPECUBEJAM_API float AbsorbOnFacing(float& Strength, float Damage, bool& bOutCollapsed);

	/**
	 * Damage without a direction (leaving the sector, development commands): an equal share for every facing; what a
	 * facing cannot take is offered to the others; the returned rest goes to the hull. OutCollapsedMask gets a bit
	 * for every facing brought down. Pure.
	 */
	HYPECUBEJAM_API float AbsorbSpread(TArrayView<float> Strengths, float Damage, uint8& OutCollapsedMask);

	/**
	 * A shield cell: Amount spread over the facings, the weakest first (water filling: all facings below the level
	 * rise to it together), none above MaxPerFacing. Returns the points added. Pure.
	 */
	HYPECUBEJAM_API float AddSpread(TArrayView<float> Strengths, float Amount, float MaxPerFacing);

	/**
	 * One strength as a byte and back. 0 is exactly "down"; any strength above zero is at least 1, so a client never
	 * sees a live facing as down or the other way round. Step = FacingMax * QuantRange / 255.
	 */
	HYPECUBEJAM_API uint8 Quantise(float Strength, float FacingMax);
	HYPECUBEJAM_API float Dequantise(uint8 Value, float FacingMax);
	/** All facings in one replicated word, facing i in byte i. Unused facings are 0. */
	HYPECUBEJAM_API uint32 Pack(TConstArrayView<float> Strengths, float FacingMax);
	HYPECUBEJAM_API void Unpack(uint32 Packed, float FacingMax, TArrayView<float> OutStrengths);

	/** A unit direction in 16 bits (yaw and pitch, a byte each) for the cosmetic hit event; about 1.4 degrees of error at most. */
	HYPECUBEJAM_API uint16 PackDirection(const FVector& Direction);
	HYPECUBEJAM_API FVector UnpackDirection(uint16 Packed);

	/** Colour of a facing by its strength (fraction of capacity): orange when weak, blue-white when healthy. Bubble and HUD. */
	HYPECUBEJAM_API FLinearColor StrengthColor(float Fraction);

	/**
	 * Which facing a ship that is breaking off should turn towards its attacker. Fractions are the strengths of the
	 * four facings (0..1), Current the facing the attacker is on now. The current one is kept while it is not weak;
	 * otherwise the strongest facing - a neighbouring one (a quarter turn) before the opposite one (a half turn)
	 * unless that is clearly stronger - when it is better than the current one by MinGain, else the current one. Pure.
	 */
	HYPECUBEJAM_API EHCJShieldFacing PickFacingToPresent(TConstArrayView<float> Fractions, EHCJShieldFacing Current, float MinGain = 0.25f);
}
