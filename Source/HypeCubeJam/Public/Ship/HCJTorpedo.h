#pragma once

#include "CoreMinimal.h"

/**
 * Photon torpedoes: the secondary weapon every ship carries (not a pickup weapon). Plain data and pure functions
 * (tested on their own); the server runs them, clients only draw.
 *
 * A ship has a small stock (FHCJTorpedoBay), fires one at a time and reloads between shots. The torpedo is an
 * AHCJProjectile of weapon type EHCJWeapon::Torpedo: slow, steering gently for the ship its firer had locked at
 * launch (it flies straight without a lock and gives up when the target gets out of its cone), armed only after a
 * short distance, detonating on contact or at the end of its life with a small blast. Its damage is built around
 * the shield facings: most of it is spent on the facing it lands on, and only what the facing does not stop
 * reaches the hull - so it strips a full facing and does little else, and guts a ship through a facing that is down.
 */
struct HYPECUBEJAM_API FHCJTorpedoBay
{
	/** Torpedoes on board. */
	int32 Count = 0;
	/** Time (any monotonic clock, seconds) from which the next one can be fired. */
	float ReadyTime = 0.0f;

	/** A fresh ship: StartCount torpedoes (at most MaxCount), loaded. */
	void Reset(int32 StartCount, int32 MaxCount);
	bool CanFire(float Now) const { return Count > 0 && Now >= ReadyTime; }
	/** Fires one when CanFire: one fewer on board, the next is ready ReloadSeconds from Now. Returns whether it fired. */
	bool TryFire(float Now, float ReloadSeconds);
	/** A restock: up to Amount more, never above MaxCount. Returns how many were added (0 = the bay was full). */
	int32 Restock(int32 Amount, int32 MaxCount);
	/** 0 right after a shot .. 1 loaded (also 1 for an empty bay: there is nothing to wait for). */
	float GetReloadFraction(float Now, float ReloadSeconds) const;
};

/** What a bot knows when it decides whether to fire a torpedo: nothing a player's HUD would not show. */
struct FHCJTorpedoBotInput
{
	/** Torpedoes on board and loaded. */
	bool bReady = false;
	/** The bot has its target locked, in sight, and its nose within a few degrees of it. */
	bool bLockedAndAimed = false;
	/** Distance to the target, uu. */
	float Distance = 0.0f;
	/** Strength (0..1 of its capacity) of the target's facing that is turned towards the bot - the target panel shows it. */
	float FacingFraction = 1.0f;
	/** Hull of the target (0..1) - the bracket shows it. */
	float HullFraction = 1.0f;
	/** A random number in [0, 1), rolled once per decision. */
	float Roll = 1.0f;
	/** Seconds since the bot took this target. */
	float SecondsOnTarget = 0.0f;
};

/** How eager a bot is with torpedoes at one difficulty. */
struct FHCJTorpedoBotSkill
{
	/** Chance per decision (a few times a second) to fire at a facing that is down or weak. */
	float WeakFacingChance = 0.25f;
	/** Chance per decision to open a fight at medium range with one, in the first seconds on a target. */
	float OpenerChance = 0.02f;
	/** Seconds a bot waits after a torpedo before it considers the next one (the reload is shorter). */
	float MinSecondsBetween = 10.0f;
	/** Chance, once per torpedo that comes close while homing on the bot, to break away from it. */
	float EvadeChance = 0.3f;
};

namespace HCJTorpedo
{
	/** False with -HCJNoTorpedoes on the command line of a development build (to measure a match without them): no stock, no restock pads. */
	HYPECUBEJAM_API bool IsEnabled();

	/** Armed once it has flown ArmingDistance from the muzzle. An unarmed torpedo that hits something is a dud: a knock, no blast. */
	HYPECUBEJAM_API bool IsArmed(float DistanceTravelled, float ArmingDistance);

	/**
	 * One torpedo hit on a shield facing of strength FacingStrength. ShieldDamage is what the torpedo can strip from a
	 * facing, HullDamage what it does to the hull through a facing that is down (both already scaled by the blast
	 * falloff). The facing loses min(strength, ShieldDamage); the share of the torpedo the facing did not stop -
	 * 1 - strength / ShieldDamage, zero when the facing held - of HullDamage goes on to the hull. Pure.
	 */
	HYPECUBEJAM_API void SplitDamage(float FacingStrength, float ShieldDamage, float HullDamage, float& OutShieldLoss, float& OutToHull);

	/**
	 * Share of the full damage a blast does at Distance from its centre to the hull of a ship: 1 inside InnerRadius,
	 * falling in a straight line to EdgeScale at OuterRadius, 0 beyond. Pure.
	 */
	HYPECUBEJAM_API float BlastScale(float Distance, float InnerRadius, float OuterRadius, float EdgeScale);

	/**
	 * Homing: the unit direction after turning Current towards Wanted by at most MaxDegrees (both need not be
	 * normalised; a zero vector leaves Current as it is). Shared by homing missiles and torpedoes. Pure.
	 */
	HYPECUBEJAM_API FVector TurnTowards(const FVector& Current, const FVector& Wanted, float MaxDegrees);

	/**
	 * Whether a torpedo flying along Forward keeps homing on something at ToTarget (relative position): it must be
	 * inside the cone of half angle ConeDegrees ahead and no farther than MaxDistance. A torpedo that lost its target
	 * never picks one up again. Pure.
	 */
	HYPECUBEJAM_API bool KeepsLock(const FVector& Forward, const FVector& ToTarget, float ConeDegrees, float MaxDistance);

	/**
	 * How a torpedo homes (item 40). A bot's torpedo flies as before: the weapon row's turn rate, only for the ship its
	 * firer had locked, inside the row's cone at launch and for as long as it stays inside it. A torpedo fired from a
	 * ship flown by a PERSON (every difficulty) also takes the nearest ship in a narrower cone ahead when there is no
	 * lock, keeps its target inside a wider cone, turns faster and leads it a little - so it closes instead of flying
	 * past, and a hard turn under boost still shakes it off.
	 */
	struct FHoming
	{
		/** Degrees per second. */
		float TurnRateDeg = 40.0f;
		/** Half angle in which the firer's lock is taken at launch. */
		float LockConeDeg = 60.0f;
		/** Half angle in which the nearest ship is taken when there is no lock (0 = never). */
		float AcquireConeDeg = 0.0f;
		/** Half angle the target must stay inside, or it is lost for good. */
		float KeepConeDeg = 60.0f;
		/** It steers for where the target will be in min(time to it, this) seconds (0 = where it is). */
		float MaxLeadSeconds = 0.0f;
	};
	/** A person's torpedo: turn rate, acquisition and keep cones, lead. */
	inline constexpr float HumanTurnRateDeg = 180.0f;
	inline constexpr float HumanAcquireConeDeg = 40.0f;
	inline constexpr float HumanKeepConeDeg = 80.0f;
	inline constexpr float HumanMaxLeadSeconds = 0.5f;
	/** The homing of a torpedo of the weapon row (RowTurnRate, RowCone) fired by a person or not. Pure. */
	HYPECUBEJAM_API FHoming GetHoming(bool bHumanFirer, float RowTurnRate, float RowConeDeg);
	/**
	 * The target of a torpedo at launch: ToCandidates are the positions of the ships it may home on relative to the
	 * torpedo (alive, not cloaked, not the firer), LockedIndex the firer's lock or INDEX_NONE. The lock when it is
	 * within LockConeDeg and MaxDistance; else, with an acquisition cone, the nearest candidate inside it and within
	 * MaxDistance; else INDEX_NONE (it flies straight). Pure.
	 */
	HYPECUBEJAM_API int32 PickTarget(const FVector& Forward, TConstArrayView<FVector> ToCandidates, int32 LockedIndex, const FHoming& Homing, float MaxDistance);
	/** The point a torpedo at TorpedoLocation flying at Speed steers for: the target, led by its velocity for min(distance / Speed, MaxLeadSeconds). Pure. */
	HYPECUBEJAM_API FVector SteerPoint(const FVector& TorpedoLocation, const FVector& TargetLocation, const FVector& TargetVelocity, float Speed, float MaxLeadSeconds);
	/** -HCJClassicTorpedo (development builds): every torpedo homes as a bot's does - the torpedo of build 0.21.0. */
	HYPECUBEJAM_API bool IsClassicHoming();

	/** The row for Difficulty (0 easy, 1 normal, 2 hard; clamped). */
	HYPECUBEJAM_API FHCJTorpedoBotSkill GetBotSkill(int32 Difficulty);

	/**
	 * A bot's decision to fire, made a few times a second: only with a loaded torpedo and a locked, aimed target
	 * between MinRange (outside the blast and the arming distance) and MaxRange; then either the facing shown to it
	 * is down or weak (HCJShield::WeakFraction) - by chance, more readily when it is down - or, in the first seconds
	 * on a fresh target at medium range (beyond half of MaxRange), as an opener now and then. Never on a target
	 * whose hull is nearly gone (a bolt will do). Pure.
	 */
	HYPECUBEJAM_API bool ShouldBotFire(const FHCJTorpedoBotInput& Input, const FHCJTorpedoBotSkill& Skill, float MinRange, float MaxRange);
}
