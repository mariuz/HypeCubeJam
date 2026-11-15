#pragma once

#include "CoreMinimal.h"

/**
 * How well a bot fights at one difficulty level. Plain numbers (tested on their own); AHCJBotController reads the
 * row of AHCJGameMode::GetBotDifficulty every Think, so a change in the menu applies to the next match at once.
 */
struct HYPECUBEJAM_API FHCJBotSkill
{
	/** Random aim offset, degrees (re-rolled every think). */
	float AimErrorDeg = 2.0f;
	/** Seconds from getting a target (or a new one) to the first shot. */
	float ReactionDelay = 0.4f;
	/** Max turn rate of the aim, degrees per second. */
	float TurnRateDeg = 150.0f;
	/** The bot fires in bursts: seconds firing / seconds holding fire (0 pause = continuous). */
	float BurstSeconds = 2.0f;
	float BurstPause = 0.5f;
	/** Aggression: the share of EngageRange inside which it opens fire, and the hull fraction below which it breaks off for a repair pad. */
	float EngageRangeFactor = 1.0f;
	float RetreatHullFraction = 0.3f;
	/** Strength of the sideways strafe while attacking, 0..1 (a weak strafe is easy to track). */
	float StrafeStrength = 0.6f;
	/**
	 * Phaser discipline: the bot holds the beam only while its aim is within this many degrees of the target (a
	 * careless pilot sprays the bank into space), and lets go when the bank is down to this share unless the
	 * target's hull is nearly gone (so that it is not locked out in the middle of a fight).
	 */
	float PhaserConeDeg = 4.0f;
	float PhaserReserve = 0.0f;
	/**
	 * Ship systems, as chances: to warp on a long way to a portal / to flee by warp when the hull is low (rolled once
	 * per trip / per retreat), to raise the cloak for an approach or a retreat and to use the tractor beam on a target
	 * in reach (rolled once a second), and to boost free when a beam catches the bot (rolled once per catch).
	 */
	float WarpTravelChance = 0.5f;
	float WarpFleeChance = 0.3f;
	float CloakChance = 0.1f;
	float TractorChance = 0.08f;
	float BreakFreeChance = 0.7f;

	/** The row for Difficulty (0 easy, 1 normal, 2 hard; clamped). */
	static FHCJBotSkill Get(int32 Difficulty);
};

/** What a possible spawn point looks like to the rules; filled by the game mode from the world. */
struct FHCJSpawnCandidate
{
	/** Distance to the nearest live hostile ship (uu); very large when there is none in the sector. */
	float NearestEnemyDistance = 1.0e9f;
	/** Live hostile ships in the sector of the point. */
	int32 EnemiesInSector = 0;
	/** Inside the range of a live station turret. */
	bool bInTurretRange = false;
	/** Pull of a black hole at the point (uu/s); 0 outside every gravity well. */
	float GravityPull = 0.0f;
};

namespace HCJBalance
{
	/** A ship that spawns closer than this to an enemy is in a fight before its pilot has looked around (uu). */
	inline constexpr float SpawnSafeEnemyDistance = 5000.0f;

	/**
	 * Score of a spawn point, higher is better. Never next to an enemy, never under a station's guns, never in a
	 * gravity well; among the safe points, prefer a sector where there is somebody to fight (one to three enemies)
	 * over an empty one, and distance to the nearest enemy up to a comfortable 12 000 uu. Pure.
	 */
	HYPECUBEJAM_API float ScoreSpawn(const FHCJSpawnCandidate& Candidate);
	/** Index of the best candidate (first of equals); INDEX_NONE for an empty list. */
	HYPECUBEJAM_API int32 PickBestSpawn(TConstArrayView<FHCJSpawnCandidate> Candidates);

	/**
	 * One step of the shield regeneration rule: the new shield value. Nothing happens for RegenDelay seconds after
	 * the last damage (SecondsSinceDamage), on a destroyed ship, or to a shield at or above MaxShield (an overcharge
	 * from a shield cell stays); otherwise RegenRate points per second up to MaxShield. Pure; the server runs it.
	 */
	HYPECUBEJAM_API float StepShieldRegen(float Shield, float MaxShield, float SecondsSinceDamage, float DeltaSeconds, float RegenDelay, float RegenRate, bool bDepleted);

	/**
	 * One step of the boost meter (energy 0..1). Drains over DrainSeconds while boosting, refills over
	 * RechargeSeconds otherwise. Once empty, boost stays locked until the meter is back at RelockLevel, so holding
	 * the key does not make the engine stutter. Returns whether boost is in effect during this step. Pure, and run
	 * with the same inputs by the owning client's prediction and the server.
	 */
	HYPECUBEJAM_API bool StepBoost(float& Energy, bool& bLocked, bool bWantsBoost, float DeltaSeconds, float DrainSeconds, float RechargeSeconds, float RelockLevel);

	/**
	 * The server's side of the boost meter for a ship flown by a remote client. The client's own meter decides which
	 * moves are boosted and says so in every move (bClientBoosting), so the two never disagree about a move because
	 * their meters differ by a frame; the server's meter only limits it: boost is refused once the server's meter is
	 * Grace below empty, or while it is locked and more than Grace short of the relock level. Returns whether this
	 * move is boosted.
	 */
	HYPECUBEJAM_API bool StepBoostFollow(float& Energy, bool& bLocked, bool bClientBoosting, float DeltaSeconds, float DrainSeconds, float RechargeSeconds,
		float RelockLevel, float Grace = 0.08f);
}
