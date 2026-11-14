#include "Tests/HCJTestHelpers.h"

#include "Game/HCJBalance.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJBalanceTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/** Bot difficulty rows: every step up is strictly harder in every number, values stay in sane ranges, input is clamped. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJBotSkillTest, "HypeCubeJam.Balance.BotDifficulty", HCJBalanceTestFlags)

bool FHCJBotSkillTest::RunTest(const FString& Parameters)
{
	for (int32 Level = 0; Level < 2; ++Level)
	{
		const FHCJBotSkill Low = FHCJBotSkill::Get(Level);
		const FHCJBotSkill High = FHCJBotSkill::Get(Level + 1);
		TestTrue(TEXT("aim error shrinks"), High.AimErrorDeg < Low.AimErrorDeg);
		TestTrue(TEXT("reaction gets faster"), High.ReactionDelay < Low.ReactionDelay);
		TestTrue(TEXT("turn rate grows"), High.TurnRateDeg > Low.TurnRateDeg);
		TestTrue(TEXT("fires a larger share of the time"),
			High.BurstSeconds / (High.BurstSeconds + High.BurstPause) > Low.BurstSeconds / (Low.BurstSeconds + Low.BurstPause));
		TestTrue(TEXT("opens fire from farther"), High.EngageRangeFactor > Low.EngageRangeFactor);
		TestTrue(TEXT("stays in a fight longer"), High.RetreatHullFraction < Low.RetreatHullFraction);
		TestTrue(TEXT("strafes harder"), High.StrafeStrength > Low.StrafeStrength);
	}
	for (int32 Level = 0; Level <= 2; ++Level)
	{
		const FHCJBotSkill Skill = FHCJBotSkill::Get(Level);
		TestTrue(TEXT("aim error in range"), Skill.AimErrorDeg > 0.0f && Skill.AimErrorDeg < 10.0f);
		TestTrue(TEXT("reaction in range"), Skill.ReactionDelay >= 0.0f && Skill.ReactionDelay <= 1.5f);
		TestTrue(TEXT("burst positive"), Skill.BurstSeconds > 0.0f && Skill.BurstPause >= 0.0f);
		TestTrue(TEXT("factors in range"), Skill.EngageRangeFactor > 0.0f && Skill.EngageRangeFactor <= 1.0f
			&& Skill.RetreatHullFraction > 0.0f && Skill.RetreatHullFraction < 1.0f && Skill.StrafeStrength >= 0.0f && Skill.StrafeStrength <= 1.0f);
	}
	TestEqual(TEXT("below the range is easy"), FHCJBotSkill::Get(-5).AimErrorDeg, FHCJBotSkill::Get(0).AimErrorDeg);
	TestEqual(TEXT("above the range is hard"), FHCJBotSkill::Get(9).AimErrorDeg, FHCJBotSkill::Get(2).AimErrorDeg);
	return true;
}

/** Spawn scoring: hazards always lose against a safe point; among safe points company and distance decide. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJSpawnScoreTest, "HypeCubeJam.Balance.SpawnScoring", HCJBalanceTestFlags)

bool FHCJSpawnScoreTest::RunTest(const FString& Parameters)
{
	FHCJSpawnCandidate Empty;                       // empty sector, nothing around
	FHCJSpawnCandidate Company = Empty;             // two enemies in the sector, far away
	Company.EnemiesInSector = 2;
	Company.NearestEnemyDistance = 12000.0f;
	FHCJSpawnCandidate Close = Company;             // an enemy right there
	Close.NearestEnemyDistance = 800.0f;
	FHCJSpawnCandidate Closer = Close;
	Closer.NearestEnemyDistance = 200.0f;
	FHCJSpawnCandidate Turret = Company;
	Turret.bInTurretRange = true;
	FHCJSpawnCandidate Hole = Company;
	Hole.GravityPull = 300.0f;
	FHCJSpawnCandidate Crowd = Company;
	Crowd.EnemiesInSector = 6;

	using namespace HCJBalance;
	TestTrue(TEXT("company at a distance beats an empty sector"), ScoreSpawn(Company) > ScoreSpawn(Empty));
	TestTrue(TEXT("an empty sector beats spawning next to an enemy"), ScoreSpawn(Empty) > ScoreSpawn(Close));
	TestTrue(TEXT("the nearer the enemy, the worse"), ScoreSpawn(Close) > ScoreSpawn(Closer));
	TestTrue(TEXT("turret range loses against anything safe"), ScoreSpawn(Turret) < ScoreSpawn(Empty) && ScoreSpawn(Turret) < ScoreSpawn(Close));
	TestTrue(TEXT("a gravity well loses against anything safe"), ScoreSpawn(Hole) < ScoreSpawn(Empty) && ScoreSpawn(Hole) < ScoreSpawn(Close));
	TestTrue(TEXT("a few enemies beat a crowd"), ScoreSpawn(Company) > ScoreSpawn(Crowd));
	TestTrue(TEXT("just outside the safe distance beats just inside"),
		ScoreSpawn({ SpawnSafeEnemyDistance + 1.0f, 1, false, 0.0f }) > ScoreSpawn({ SpawnSafeEnemyDistance - 1.0f, 1, false, 0.0f }));

	const FHCJSpawnCandidate List[] = { Close, Turret, Empty, Company, Hole, Crowd };
	TestEqual(TEXT("best of a list"), PickBestSpawn(List), 3);
	TestEqual(TEXT("empty list"), PickBestSpawn(TConstArrayView<FHCJSpawnCandidate>()), static_cast<int32>(INDEX_NONE));
	const FHCJSpawnCandidate Equal[] = { Empty, Empty };
	TestEqual(TEXT("first of equals"), PickBestSpawn(Equal), 0);
	return true;
}

/** Boost meter: drains in the configured time, locks when empty until it has recharged to the relock level, refills fully. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJBoostMeterTest, "HypeCubeJam.Balance.BoostMeter", HCJBalanceTestFlags)

bool FHCJBoostMeterTest::RunTest(const FString& Parameters)
{
	constexpr float Step = 1.0f / 60.0f;
	constexpr float Drain = 3.0f;
	constexpr float Recharge = 6.0f;
	constexpr float Relock = 0.25f;

	float Energy = 1.0f;
	bool bLocked = false;
	float BoostTime = 0.0f;
	while (HCJBalance::StepBoost(Energy, bLocked, true, Step, Drain, Recharge, Relock) && BoostTime < 10.0f)
	{
		BoostTime += Step;
	}
	TestEqual(TEXT("a full meter lasts the drain time"), BoostTime, Drain, 2.5f * Step);
	TestTrue(TEXT("locked once empty"), bLocked);

	// Key still held: no boost until the relock level is reached, then boost again.
	float LockedTime = 0.0f;
	while (!HCJBalance::StepBoost(Energy, bLocked, true, Step, Drain, Recharge, Relock) && LockedTime < 10.0f)
	{
		LockedTime += Step;
	}
	TestEqual(TEXT("relocks after recharging to the relock level"), LockedTime, Relock * Recharge, 3.0f * Step);

	// Released: refills completely in at most the recharge time and never exceeds 1.
	Energy = 0.0f;
	bLocked = true;
	for (float Time = 0.0f; Time < Recharge + 0.1f; Time += Step)
	{
		TestFalse(TEXT("no boost without the key"), HCJBalance::StepBoost(Energy, bLocked, false, Step, Drain, Recharge, Relock));
	}
	TestEqual(TEXT("full again"), Energy, 1.0f, 0.001f);
	TestFalse(TEXT("unlocked again"), bLocked);

	// Same inputs, same result (what client prediction and the server rely on).
	float A = 0.7f, B = 0.7f;
	bool bLockA = false, bLockB = false;
	for (int32 Index = 0; Index < 500; ++Index)
	{
		const bool bWants = (Index / 37) % 2 == 0;
		const bool bA = HCJBalance::StepBoost(A, bLockA, bWants, Step, Drain, Recharge, Relock);
		const bool bB = HCJBalance::StepBoost(B, bLockB, bWants, Step, Drain, Recharge, Relock);
		if (bA != bB || A != B || bLockA != bLockB)
		{
			AddError(TEXT("boost meter is not deterministic"));
			break;
		}
	}
	return true;
}

/** Shield regeneration rule: nothing for the delay after damage, then at the configured rate up to the maximum, never beyond, never when destroyed. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShieldRegenTest, "HypeCubeJam.Balance.ShieldRegen", HCJBalanceTestFlags)

bool FHCJShieldRegenTest::RunTest(const FString& Parameters)
{
	using namespace HCJBalance;
	constexpr float Delay = 4.0f;
	constexpr float Rate = 15.0f;
	constexpr float Max = 50.0f;
	TestEqual(TEXT("no regeneration inside the delay"), StepShieldRegen(10.0f, Max, 3.9f, 0.5f, Delay, Rate, false), 10.0f, 0.0001f);
	TestEqual(TEXT("regenerates after the delay"), StepShieldRegen(10.0f, Max, 4.0f, 0.5f, Delay, Rate, false), 17.5f, 0.0001f);
	TestEqual(TEXT("stops at the maximum"), StepShieldRegen(49.0f, Max, 10.0f, 0.5f, Delay, Rate, false), Max, 0.0001f);
	TestEqual(TEXT("an overcharged shield is left alone"), StepShieldRegen(80.0f, Max, 10.0f, 0.5f, Delay, Rate, false), 80.0f, 0.0001f);
	TestEqual(TEXT("a destroyed ship regenerates nothing"), StepShieldRegen(10.0f, Max, 10.0f, 0.5f, Delay, Rate, true), 10.0f, 0.0001f);
	TestEqual(TEXT("no rate, no regeneration"), StepShieldRegen(10.0f, Max, 10.0f, 0.5f, Delay, 0.0f, false), 10.0f, 0.0001f);

	// From empty: full after delay + Max / Rate seconds of 60 Hz steps.
	float Shield = 0.0f;
	float Time = 0.0f;
	while (Shield < Max && Time < 20.0f)
	{
		Time += 1.0f / 60.0f;
		Shield = StepShieldRegen(Shield, Max, Time, 1.0f / 60.0f, Delay, Rate, false);
	}
	TestEqual(TEXT("time to a full shield"), Time, Delay + Max / Rate, 0.05f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
