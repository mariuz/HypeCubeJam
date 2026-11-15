#pragma once

#include "CoreMinimal.h"

/**
 * Warp drive, cloaking device and tractor beam: the rules as plain logic without a world, so they can be tested on
 * their own. AHCJShipCharacter (server) and UHCJShipMovementComponent (server and owning client) run them.
 */

// ---------------------------------------------------------------------------------------------------------------------
// Warp drive
// ---------------------------------------------------------------------------------------------------------------------

enum class EHCJWarpPhase : uint8
{
	Idle,
	/** The key is held and the drive spools up; weapons are off; a hit or a tractor beam cancels it. */
	Charging,
	/** The ship flies in a straight line at warp speed. */
	Active,
	/** Nothing until the time is up. */
	Cooldown
};

struct FHCJWarpParams
{
	/** Seconds the key must be held before the jump. */
	float ChargeSeconds = 1.6f;
	/** Longest time at warp (the "energy" of one charge). */
	float MaxSeconds = 2.5f;
	/** Seconds after a warp before the next charge; after a cancelled charge only AbortCooldown. */
	float Cooldown = 12.0f;
	float AbortCooldown = 2.0f;
	/** uu/s at warp, and the speed the ship keeps when it drops out. */
	float Speed = 15000.0f;
	float ExitSpeed = 6000.0f;
};

/**
 * The state machine of one drive. It is part of the movement simulation: the owning client steps it for every move
 * it predicts (Step), keeps it in its saved moves and tells the server in the move flags whether the key is held and
 * whether the move was flown at warp; the server steps its own copy for every move it receives (StepFollow) and lets
 * the client's flag decide the exact move a warp starts and ends on, inside a tolerance - so the two never disagree
 * about a move because their clocks differ by a frame, and a client cannot warp without charge or cooldown.
 */
struct HYPECUBEJAM_API FHCJWarpDrive
{
	EHCJWarpPhase Phase = EHCJWarpPhase::Idle;
	/** Seconds spent in Charging / Active; seconds left in Cooldown. */
	float Time = 0.0f;
	/** The key must be let go once before the next charge (a held key does not chain warps). */
	bool bNeedsRelease = false;
	/** World direction of the jump, fixed when it starts. */
	FVector Direction = FVector::ForwardVector;
	/** Set by the step in which the jump started / ended (cleared by the next step). */
	bool bJustEntered = false;
	bool bJustExited = false;

	/**
	 * One move of a ship that decides for itself (owning client, bot, listen host). bWants: the key is held.
	 * bBlocked: something forbids warp right now (held by a tractor beam, cloaked). Returns whether this move is
	 * flown at warp.
	 */
	bool Step(bool bWants, bool bBlocked, float DeltaSeconds, const FHCJWarpParams& Params);

	/**
	 * One move on the server for a ship flown by a remote client. bClientActive: the client flew this move at warp.
	 * The jump starts on the move the client says, provided the server's own charge is complete to within
	 * Tolerance seconds, and ends when the client says so or Tolerance after the longest warp. A claim without
	 * charge, during the cooldown or while blocked is refused (the move is flown normally and the client corrected).
	 */
	bool StepFollow(bool bWants, bool bClientActive, bool bBlocked, float DeltaSeconds, const FHCJWarpParams& Params, float Tolerance = 0.25f);

	/** Ends whatever the drive is doing with a cooldown (a hit while charging, a tractor beam, a collision, a portal). No-op when idle or cooling. */
	void Cancel(float CooldownSeconds);

	bool IsBusy() const { return Phase == EHCJWarpPhase::Charging || Phase == EHCJWarpPhase::Active; }
	/** 0..1 while charging. */
	float GetChargeFraction(const FHCJWarpParams& Params) const;
	/** 1..0 while at warp: what is left of the jump. */
	float GetEnergyFraction(const FHCJWarpParams& Params) const;
	/** 0..1 while cooling down: how far the drive is from ready (1 = just dropped out, 0 = ready). */
	float GetCooldownFraction(const FHCJWarpParams& Params) const;

private:
	void EnterCooldown(float Seconds, bool bFromActive);
};

namespace HCJWarp
{
	/**
	 * A ship at warp drops out before it leaves the playable part of a sector: true when LocalPosition (relative to
	 * the sector origin) is beyond EdgeRadius and Direction points outwards.
	 */
	HYPECUBEJAM_API bool ShouldDropOutAtEdge(const FVector& LocalPosition, const FVector& Direction, float EdgeRadius);

	/** Bots: worth warping when the way is this long (uu) - at least a second and a half at warp. */
	HYPECUBEJAM_API bool IsWorthWarping(float Distance, const FHCJWarpParams& Params);
}

// ---------------------------------------------------------------------------------------------------------------------
// Cloaking device
// ---------------------------------------------------------------------------------------------------------------------

/**
 * The cloak of one ship (server). A meter that drains while cloaked and refills slowly otherwise; emptied, it is
 * locked until it is back at a level. A cloaked ship can be REVEALED for a moment (a hit, an enemy very close, a
 * tractor beam): it stays cloaked - shields down, weapons off - but everybody sees and can target it.
 */
struct HYPECUBEJAM_API FHCJCloakDevice
{
	/** 0 empty .. 1 full. */
	float Energy = 1.0f;
	bool bCloaked = false;
	bool bLocked = false;
	/** Seconds the ship is still revealed. */
	float RevealSeconds = 0.0f;

	/**
	 * One step. bWants: the pilot wants to be cloaked. bFreeDrain: the meter does not drain (a cloak pad). Returns
	 * whether the ship is cloaked after the step; a cloak that ran dry drops by itself (and locks).
	 */
	bool Step(bool bWants, float DeltaSeconds, float DrainSeconds, float RechargeSeconds, float RelockLevel, bool bFreeDrain = false);

	void Reveal(float Seconds) { RevealSeconds = FMath::Max(RevealSeconds, Seconds); }
	bool IsRevealed() const { return bCloaked && RevealSeconds > 0.0f; }
	/** Cloaked and not revealed: nobody else sees it. */
	bool IsHidden() const { return bCloaked && RevealSeconds <= 0.0f; }
	/** A cloak pad: full meter, unlocked. Returns false when there was nothing to gain. */
	bool Refill();
	/** Could the cloak be raised right now? */
	bool CanEngage() const { return !bLocked && Energy > 0.0f; }
};

namespace HCJCloak
{
	/** Seconds after a cloak drops before weapons work, the level a drained meter must reach, and the reveals (seconds). */
	inline constexpr float WeaponDelay = 1.0f;
	inline constexpr float RelockLevel = 0.2f;
	inline constexpr float HitRevealSeconds = 1.2f;
	inline constexpr float ProximityRevealSeconds = 0.5f;
	/** An enemy this close (uu) sees through the cloak. */
	inline constexpr float ProximityRange = 1500.0f;
	/** Seconds a cloak pad keeps the meter from draining. */
	inline constexpr float PadFreeSeconds = 8.0f;

	/** Weapons work again WeaponDelay after the cloak dropped (and never while cloaked). */
	HYPECUBEJAM_API bool CanFire(bool bCloaked, float SecondsSinceDecloak);
	/** The owner's meter in one byte: bit 7 = locked, bits 0..6 = energy in 127 steps. */
	HYPECUBEJAM_API uint8 PackEnergy(float Energy, bool bLocked);
	HYPECUBEJAM_API void UnpackEnergy(uint8 Packed, float& OutEnergy, bool& bOutLocked);
}

// ---------------------------------------------------------------------------------------------------------------------
// Tractor beam
// ---------------------------------------------------------------------------------------------------------------------

namespace HCJTractor
{
	/** Half angle (degrees) of the cone in front of the holder a target must be in, and how far past the range a held one may stretch. */
	inline constexpr float ConeDegrees = 50.0f;
	inline constexpr float RangeSlack = 1.15f;
	/** The beam does not pull a held ship closer than this (uu). */
	inline constexpr float HoldDistance = 1300.0f;
	/** What a held ship's top speed is multiplied with. */
	inline constexpr float SpeedScale = 0.55f;
	/** Seconds of boost that tear a held ship free, and the damage to the holder that makes it let go. */
	inline constexpr float BreakBoostSeconds = 0.9f;
	inline constexpr float BreakDamage = 22.0f;
	/** Seconds before a broken beam can be used again. */
	inline constexpr float BreakCooldown = 2.0f;

	/**
	 * Can a beam from HolderLocation (nose along HolderForward) catch or keep a ship at TargetLocation? Inside Range
	 * (times RangeSlack for a ship already held) and inside the cone (a held ship may drift to twice the half angle).
	 * Line of sight is the caller's job.
	 */
	HYPECUBEJAM_API bool CanHold(const FVector& HolderLocation, const FVector& HolderForward, const FVector& TargetLocation, float Range, bool bAlreadyHeld);

	/**
	 * How far the beam moves the held ship in one step: towards the holder at PullSpeed, never past HoldDistance
	 * from it. A pure function of the two positions - the victim's movement simulation applies it on the server and
	 * in the owning client's prediction, like the drift of a black hole.
	 */
	HYPECUBEJAM_API FVector ComputePull(const FVector& VictimLocation, const FVector& HolderLocation, float PullSpeed, float DeltaSeconds, float MinDistance = HoldDistance);

	/** The beam lets go: the victim boosted long enough, or the holder took enough damage while holding. */
	HYPECUBEJAM_API bool ShouldBreak(float VictimBoostSeconds, float HolderDamageTaken);
}
