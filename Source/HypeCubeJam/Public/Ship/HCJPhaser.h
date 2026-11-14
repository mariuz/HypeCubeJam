#pragma once

#include "CoreMinimal.h"

/**
 * Phasers: the beam weapon every ship starts with (EHCJWeapon::Phaser). Plain logic without a world, so it can be
 * tested on its own; AHCJShipCharacter runs it on the server.
 *
 * RULES: the beam is instant - a server line trace from the muzzle along the aim, bent a little towards a hostile
 * ship near the aim line (PickAssistTarget). While the trigger is held the beam drains the phaser bank; an emptied
 * bank is locked until it has recharged to a level, and until then the trigger fires the blaster instead. Damage is
 * steady (per second), strongest up close (RangeScale), and lands on the shield facing the beam touches.
 *
 * ON THE WIRE: one byte per ship for everybody (0 = no beam, else the beam length as a fraction of the range), the
 * actor the beam is locked on (changes rarely), and for the owner the charge as one byte. Every machine draws the
 * beam from the ship it sees, every frame, so it is smooth whatever the update rate.
 */
struct HYPECUBEJAM_API FHCJPhaserBank
{
	/** 0 empty .. 1 full. */
	float Charge = 1.0f;
	/** Emptied: no beam until the charge is back at the relock level. */
	bool bLocked = false;
	/** Seconds since the beam last ran. */
	float IdleSeconds = 1000.0f;

	/**
	 * One step. bWantsBeam: the trigger is held and the phaser is the weapon. Returns whether the beam runs during
	 * this step. The beam drains a full bank in DrainSeconds; a bank that is not firing starts to recharge after
	 * RechargeDelay and fills in RechargeSeconds; once emptied it is locked until RelockLevel.
	 */
	bool Step(bool bWantsBeam, float DeltaSeconds, float DrainSeconds, float RechargeDelay, float RechargeSeconds, float RelockLevel);

	/** Seconds of beam left in the bank. */
	float GetSecondsLeft(float DrainSeconds) const { return bLocked ? 0.0f : Charge * DrainSeconds; }
};

namespace HCJPhaser
{
	/**
	 * Share of the damage per second that arrives at Distance: 1 within FullFraction of Range, falling in a straight
	 * line to EdgeScale at Range, 0 beyond.
	 */
	HYPECUBEJAM_API float RangeScale(float Distance, float Range, float FullFraction, float EdgeScale);

	/** The replicated beam byte: 0 = off, 1..255 = length / Range (rounded up, so a running beam is never 0). */
	HYPECUBEJAM_API uint8 PackLength(float Length, float Range);
	HYPECUBEJAM_API float UnpackLength(uint8 Packed, float Range);

	/** The owner's charge byte: bit 7 = locked, bits 0..6 = charge in 127 steps. */
	HYPECUBEJAM_API uint8 PackCharge(float Charge, bool bLocked);
	HYPECUBEJAM_API void UnpackCharge(uint8 Packed, float& OutCharge, bool& bOutLocked);

	/**
	 * Aim assist of the beam: the index of the candidate (centre positions of hostile ships) the beam bends to, or
	 * INDEX_NONE for a straight beam. A candidate counts when it is within Range and within AssistDegrees of the aim
	 * line as seen from Origin - LockedAssistDegrees for LockedIndex, the firer's locked target, which also wins
	 * against every other candidate. Among the others the one nearest the aim line wins. Line of sight is the caller's job.
	 */
	HYPECUBEJAM_API int32 PickAssistTarget(const FVector& Origin, const FVector& AimDirection, TConstArrayView<FVector> Candidates, int32 LockedIndex,
		float Range, float AssistDegrees, float LockedAssistDegrees);

	/** False with -HCJNoPhasers (measurement aid: every ship starts with the blaster, as before the phasers). */
	HYPECUBEJAM_API bool IsEnabled();
}
