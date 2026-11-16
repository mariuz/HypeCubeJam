#include "Game/HCJBalance.h"

FHCJBotSkill FHCJBotSkill::Get(int32 Difficulty)
{
	FHCJBotSkill Skill;
	const int32 Level = FMath::Clamp(Difficulty, 0, HCJBalance::NumBotDifficulties - 1);
	switch (Level)
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
		Skill.WarpTravelChance = 0.35f;
		Skill.WarpFleeChance = 0.15f;
		Skill.CloakChance = 0.03f;
		Skill.TractorChance = 0.03f;
		Skill.BreakFreeChance = 0.4f;
		Skill.Tactics = 0.25f;
		Skill.ComboChance = 0.08f;
		// Towards a person: one bot at a time, from the front, with a long warning, and its hits do less.
		Skill.MaxAttackersOnHuman = 1;
		Skill.MaxHuntersPerHuman = 3;
		Skill.RearFireDelay = 2.5f;
		Skill.RearAimErrorScale = 2.0f;
		Skill.FrontBias = 1.0f;
		Skill.HumanTailScore = -2.0f;
		Skill.bFlankHumans = false;
		Skill.ArrivalGrace = 4.0f;
		Skill.RespiteSeconds = 6.0f;
		Skill.DamageToHumanScale = 0.4f;
		Skill.TorpedoOnHumanChance = 0.35f;
		// Help for a beginner (item 38): its guns hit harder, its hull mends between fights, a repair pad makes it whole.
		Skill.DamageFromHumanScale = 1.5f;
		Skill.HumanHullRegenRate = 4.0f;
		Skill.HumanRepairScale = 100.0f / 60.0f;
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
		Skill.WarpTravelChance = 0.7f;
		Skill.WarpFleeChance = 0.5f;
		Skill.CloakChance = 0.1f;
		Skill.TractorChance = 0.1f;
		Skill.BreakFreeChance = 0.8f;
		Skill.Tactics = 0.7f;
		Skill.ComboChance = 0.35f;
		Skill.MaxAttackersOnHuman = 2;
		Skill.MaxHuntersPerHuman = 4;
		Skill.RearFireDelay = 1.2f;
		Skill.RearAimErrorScale = 1.5f;
		Skill.FrontBias = 0.6f;
		Skill.HumanTailScore = -1.0f;
		Skill.bFlankHumans = false;
		// (Tuned like Easy's, half-way to Hard: with 2 s / 3 s / 0.8 / 0.7 the autopilots still died every 15-20 s,
		// half of the time within 12 s of the spawn.)
		Skill.ArrivalGrace = 3.5f;
		Skill.RespiteSeconds = 5.0f;
		Skill.DamageToHumanScale = 0.55f;
		Skill.TorpedoOnHumanChance = 0.45f;
		break;
	default:
		// Hard and Ace: what the bots were before difficulty levels, a little sharper. Hard keeps a few rules towards a
		// person (below); Ace has none - every bot hunts the player, from any side.
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
		Skill.WarpTravelChance = 0.95f;
		Skill.WarpFleeChance = 0.8f;
		Skill.CloakChance = 0.2f;
		Skill.TractorChance = 0.2f;
		Skill.BreakFreeChance = 1.0f;
		Skill.Tactics = 1.0f;
		Skill.ComboChance = 0.7f;
		if (Level == 2)
		{
			Skill.MaxAttackersOnHuman = 3;
			Skill.MaxHuntersPerHuman = 6;
			Skill.RearFireDelay = 0.5f;
			Skill.RearAimErrorScale = 1.0f;
			Skill.FrontBias = 0.3f;
			Skill.HumanTailScore = 0.0f;
			Skill.bFlankHumans = true;
			Skill.ArrivalGrace = 1.0f;
			Skill.RespiteSeconds = 0.0f;
			Skill.DamageToHumanScale = 1.0f;
			Skill.TorpedoOnHumanChance = 1.0f;
		}
		break;
	}
	return Skill;
}

void FHCJBotSkill::ClearFairness()
{
	const FHCJBotSkill Off;
	MaxAttackersOnHuman = Off.MaxAttackersOnHuman;
	MaxHuntersPerHuman = Off.MaxHuntersPerHuman;
	RearFireDelay = Off.RearFireDelay;
	RearAimErrorScale = Off.RearAimErrorScale;
	FrontBias = Off.FrontBias;
	HumanTailScore = Off.HumanTailScore;
	bFlankHumans = Off.bFlankHumans;
	ArrivalGrace = Off.ArrivalGrace;
	RespiteSeconds = Off.RespiteSeconds;
	DamageToHumanScale = Off.DamageToHumanScale;
	TorpedoOnHumanChance = Off.TorpedoOnHumanChance;
	DamageFromHumanScale = Off.DamageFromHumanScale;
	HumanHullRegenRate = Off.HumanHullRegenRate;
	HumanRepairScale = Off.HumanRepairScale;
}

void FHCJBotSkill::ClearBeginnerHelp()
{
	const FHCJBotSkill Off;
	if (DamageFromHumanScale != Off.DamageFromHumanScale || HumanHullRegenRate != Off.HumanHullRegenRate || HumanRepairScale != Off.HumanRepairScale)
	{
		// (Only the row that has the help - Easy - had its damage scale lowered with it.)
		DamageToHumanScale = FMath::Max(DamageToHumanScale, HCJBalance::ClassicEasyDamageToHumanScale);
	}
	DamageFromHumanScale = Off.DamageFromHumanScale;
	HumanHullRegenRate = Off.HumanHullRegenRate;
	HumanRepairScale = Off.HumanRepairScale;
}

const TCHAR* HCJBalance::GetDifficultyName(int32 Difficulty)
{
	switch (FMath::Clamp(Difficulty, 0, NumBotDifficulties - 1))
	{
	case 0: return TEXT("Easy");
	case 1: return TEXT("Normal");
	case 2: return TEXT("Hard");
	default: return TEXT("Ace");
	}
}

const TCHAR* HCJBalance::GetDifficultyDescription(int32 Difficulty)
{
	switch (FMath::Clamp(Difficulty, 0, NumBotDifficulties - 1))
	{
	case 0: return TEXT("One bot at a time, from the front. Your guns hit harder, your hull mends.");
	case 1: return TEXT("Two on you at most. A moment's warning before any fire from behind.");
	case 2: return TEXT("Three on you. Sharp aim, torpedo combos, scouts ambush from cloak.");
	default: return TEXT("No rules: every bot hunts you, from any side, with everything it has.");
	}
}

float HCJBalance::BearingDegrees(const FVector& Forward, const FVector& ToOther)
{
	const FVector Direction = ToOther.GetSafeNormal();
	const FVector Nose = Forward.GetSafeNormal();
	if (Direction.IsNearlyZero() || Nose.IsNearlyZero())
	{
		return -1.0f;
	}
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Nose, Direction), -1.0, 1.0))));
}

bool HCJBalance::IsRearBearing(float BearingDeg)
{
	return BearingDeg >= 0.0f && FMath::Cos(FMath::DegreesToRadians(BearingDeg)) < RearArcDot;
}

const TCHAR* HCJBalance::GetTacticName(EHCJBotTactic Tactic)
{
	switch (Tactic)
	{
	case EHCJBotTactic::Retreat: return TEXT("retreats");
	case EHCJBotTactic::Reengage: return TEXT("re-engaged");
	case EHCJBotTactic::Cover: return TEXT("cover reached");
	case EHCJBotTactic::ComboTorpedo: return TEXT("torpedoes at an open facing");
	case EHCJBotTactic::TractorTorpedo: return TEXT("tractor then torpedo");
	case EHCJBotTactic::ThreatTurn: return TEXT("turned on an attacker");
	case EHCJBotTactic::RockShot: return TEXT("rocks shot out of the way");
	case EHCJBotTactic::SpreadTarget: return TEXT("chose a less crowded target");
	case EHCJBotTactic::WarpChase: return TEXT("warp chases");
	case EHCJBotTactic::StationSteer: return TEXT("steered off stations");
	case EHCJBotTactic::ClosePass: return TEXT("close passes");
	case EHCJBotTactic::Ambush: return TEXT("ambushes");
	default: return TEXT("?");
	}
}

const TCHAR* HCJBalance::GetTacticKey(EHCJBotTactic Tactic)
{
	switch (Tactic)
	{
	case EHCJBotTactic::Retreat: return TEXT("bot_retreats");
	case EHCJBotTactic::Reengage: return TEXT("bot_reengages");
	case EHCJBotTactic::Cover: return TEXT("bot_cover");
	case EHCJBotTactic::ComboTorpedo: return TEXT("bot_combo_torpedoes");
	case EHCJBotTactic::TractorTorpedo: return TEXT("bot_tractor_torpedoes");
	case EHCJBotTactic::ThreatTurn: return TEXT("bot_threat_turns");
	case EHCJBotTactic::RockShot: return TEXT("bot_rock_shots");
	case EHCJBotTactic::SpreadTarget: return TEXT("bot_spread_targets");
	case EHCJBotTactic::WarpChase: return TEXT("bot_warp_chases");
	case EHCJBotTactic::StationSteer: return TEXT("bot_station_steers");
	case EHCJBotTactic::ClosePass: return TEXT("bot_close_passes");
	case EHCJBotTactic::Ambush: return TEXT("bot_ambushes");
	default: return TEXT("bot_unknown");
	}
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

bool HCJBalance::StepBoostFollow(float& Energy, bool& bLocked, bool bClientBoosting, float DeltaSeconds, float DrainSeconds, float RechargeSeconds,
	float RelockLevel, float Grace)
{
	Energy = FMath::Clamp(Energy, -Grace, 1.0f);
	if (bClientBoosting && Energy > -Grace && !(bLocked && Energy < RelockLevel - Grace))
	{
		bLocked = false;
		Energy -= DeltaSeconds / FMath::Max(DrainSeconds, 0.1f);
		if (Energy <= -Grace)
		{
			bLocked = true;
		}
		return true;
	}
	if (Energy <= 0.02f)
	{
		// It ran (as good as) dry: from here the relock rule applies.
		bLocked = true;
	}
	Energy = FMath::Min(1.0f, FMath::Max(Energy, 0.0f) + DeltaSeconds / FMath::Max(RechargeSeconds, 0.1f));
	if (bLocked && Energy >= RelockLevel)
	{
		bLocked = false;
	}
	return false;
}

float HCJBalance::StepShieldRegen(float Shield, float MaxShield, float SecondsSinceDamage, float DeltaSeconds, float RegenDelay, float RegenRate, bool bDepleted)
{
	if (bDepleted || Shield >= MaxShield || RegenRate <= 0.0f || SecondsSinceDamage < RegenDelay)
	{
		return Shield;
	}
	return FMath::Min(MaxShield, Shield + RegenRate * DeltaSeconds);
}

float HCJBalance::StepHullRegen(float Hull, float MaxHull, float SecondsSinceDamage, float DeltaSeconds, float RegenRate, bool bDepleted)
{
	const float Ceiling = MaxHull * HullRegenCeiling;
	if (bDepleted || Hull <= 0.0f || Hull >= Ceiling || RegenRate <= 0.0f || SecondsSinceDamage < HullRegenDelay)
	{
		return Hull;
	}
	return FMath::Min(Ceiling, Hull + RegenRate * FMath::Max(DeltaSeconds, 0.0f));
}
