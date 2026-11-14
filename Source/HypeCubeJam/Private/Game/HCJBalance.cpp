#include "Game/HCJBalance.h"

FHCJBotSkill FHCJBotSkill::Get(int32 Difficulty)
{
	FHCJBotSkill Skill;
	switch (FMath::Clamp(Difficulty, 0, 2))
	{
	case 0:
		// Easy: slow to react, wide of the mark, short bursts, gives up a losing fight early.
		Skill.AimErrorDeg = 7.0f;
		Skill.ReactionDelay = 1.0f;
		Skill.TurnRateDeg = 95.0f;
		Skill.BurstSeconds = 0.9f;
		Skill.BurstPause = 1.1f;
		Skill.EngageRangeFactor = 0.6f;
		Skill.RetreatHullFraction = 0.45f;
		Skill.StrafeStrength = 0.35f;
		Skill.PhaserConeDeg = 6.0f;
		Skill.PhaserReserve = 0.0f;
		break;
	case 1:
		Skill.AimErrorDeg = 3.5f;
		Skill.ReactionDelay = 0.5f;
		Skill.TurnRateDeg = 140.0f;
		Skill.BurstSeconds = 1.8f;
		Skill.BurstPause = 0.6f;
		Skill.EngageRangeFactor = 0.85f;
		Skill.RetreatHullFraction = 0.3f;
		Skill.StrafeStrength = 0.6f;
		Skill.PhaserConeDeg = 4.0f;
		Skill.PhaserReserve = 0.05f;
		break;
	default:
		// Hard: what the bots were before difficulty levels, a little sharper.
		Skill.AimErrorDeg = 1.5f;
		Skill.ReactionDelay = 0.2f;
		Skill.TurnRateDeg = 185.0f;
		Skill.BurstSeconds = 4.0f;
		Skill.BurstPause = 0.0f;
		Skill.EngageRangeFactor = 1.0f;
		Skill.RetreatHullFraction = 0.2f;
		Skill.StrafeStrength = 0.8f;
		Skill.PhaserConeDeg = 3.0f;
		Skill.PhaserReserve = 0.1f;
		break;
	}
	return Skill;
}

float HCJBalance::ScoreSpawn(const FHCJSpawnCandidate& Candidate)
{
	float Score = 0.0f;
	if (Candidate.bInTurretRange)
	{
		Score -= 1000.0f;
	}
	if (Candidate.GravityPull > 0.0f)
	{
		Score -= 1000.0f + Candidate.GravityPull;
	}
	if (Candidate.NearestEnemyDistance < SpawnSafeEnemyDistance)
	{
		// The nearer, the worse; always worse than any point at a safe distance.
		Score -= 500.0f + 400.0f * (1.0f - FMath::Max(Candidate.NearestEnemyDistance, 0.0f) / SpawnSafeEnemyDistance);
	}
	else
	{
		Score += 100.0f * FMath::Clamp((Candidate.NearestEnemyDistance - SpawnSafeEnemyDistance) / 7000.0f, 0.0f, 1.0f);
	}
	// Somebody to fight nearby, but not a crowd.
	if (Candidate.EnemiesInSector >= 1 && Candidate.EnemiesInSector <= 3)
	{
		Score += 50.0f;
	}
	else if (Candidate.EnemiesInSector > 3)
	{
		Score += 10.0f;
	}
	return Score;
}

int32 HCJBalance::PickBestSpawn(TConstArrayView<FHCJSpawnCandidate> Candidates)
{
	int32 Best = INDEX_NONE;
	float BestScore = 0.0f;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const float Score = ScoreSpawn(Candidates[Index]);
		if (Best == INDEX_NONE || Score > BestScore)
		{
			Best = Index;
			BestScore = Score;
		}
	}
	return Best;
}

bool HCJBalance::StepBoost(float& Energy, bool& bLocked, bool bWantsBoost, float DeltaSeconds, float DrainSeconds, float RechargeSeconds, float RelockLevel)
{
	Energy = FMath::Clamp(Energy, 0.0f, 1.0f);
	if (bLocked && Energy >= RelockLevel)
	{
		bLocked = false;
	}
	const bool bActive = bWantsBoost && !bLocked && Energy > 0.0f;
	if (bActive)
	{
		Energy = FMath::Max(0.0f, Energy - DeltaSeconds / FMath::Max(DrainSeconds, 0.1f));
		if (Energy <= 0.0f)
		{
			bLocked = true;
		}
	}
	else
	{
		Energy = FMath::Min(1.0f, Energy + DeltaSeconds / FMath::Max(RechargeSeconds, 0.1f));
	}
	return bActive;
}

float HCJBalance::StepShieldRegen(float Shield, float MaxShield, float SecondsSinceDamage, float DeltaSeconds, float RegenDelay, float RegenRate, bool bDepleted)
{
	if (bDepleted || Shield >= MaxShield || RegenRate <= 0.0f || SecondsSinceDamage < RegenDelay)
	{
		return Shield;
	}
	return FMath::Min(MaxShield, Shield + RegenRate * DeltaSeconds);
}
