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

	// The fourth level, Ace, flies with Hard's numbers.
	{
		const FHCJBotSkill Hard = FHCJBotSkill::Get(2);
		const FHCJBotSkill Ace = FHCJBotSkill::Get(3);
		TestEqual(TEXT("four levels"), HCJBalance::NumBotDifficulties, 4);
		TestTrue(TEXT("ace has hard's seven numbers"), Ace.AimErrorDeg == Hard.AimErrorDeg && Ace.ReactionDelay == Hard.ReactionDelay && Ace.TurnRateDeg == Hard.TurnRateDeg
			&& Ace.BurstSeconds == Hard.BurstSeconds && Ace.BurstPause == Hard.BurstPause && Ace.EngageRangeFactor == Hard.EngageRangeFactor
			&& Ace.RetreatHullFraction == Hard.RetreatHullFraction && Ace.StrafeStrength == Hard.StrafeStrength);
		TestTrue(TEXT("ace has hard's tactics"), Ace.Tactics == Hard.Tactics && Ace.ComboChance == Hard.ComboChance);
		TestEqual(TEXT("above the range is ace"), FHCJBotSkill::Get(9).MaxAttackersOnHuman, Ace.MaxAttackersOnHuman);
	}

	// Fairness towards people: every step up allows more, Ace has no rule at all, and the "off" switch gives Ace's rules to any level.
	for (int32 Level = 0; Level < 2; ++Level)
	{
		const FHCJBotSkill Low = FHCJBotSkill::Get(Level);
		const FHCJBotSkill High = FHCJBotSkill::Get(Level + 1);
		TestTrue(TEXT("more attackers on a person"), High.MaxAttackersOnHuman > Low.MaxAttackersOnHuman && Low.MaxAttackersOnHuman >= 1);
		TestTrue(TEXT("more hunters in a person's sector"), High.MaxHuntersPerHuman > Low.MaxHuntersPerHuman && Low.MaxHuntersPerHuman >= Low.MaxAttackersOnHuman);
		TestTrue(TEXT("a shorter wait before fire from behind"), High.RearFireDelay < Low.RearFireDelay && High.RearFireDelay > 0.0f);
		TestTrue(TEXT("aim from behind no worse than before"), High.RearAimErrorScale <= Low.RearAimErrorScale && High.RearAimErrorScale >= 1.0f);
		TestTrue(TEXT("less pull to the front"), High.FrontBias < Low.FrontBias && High.FrontBias > 0.0f && Low.FrontBias <= 1.0f);
		TestTrue(TEXT("a person's tail counts for more"), High.HumanTailScore > Low.HumanTailScore && High.HumanTailScore <= 1.5f);
		TestTrue(TEXT("a shorter grace after arriving"), High.ArrivalGrace < Low.ArrivalGrace && High.ArrivalGrace > 0.0f);
		TestTrue(TEXT("a shorter breather between fights"), High.RespiteSeconds < Low.RespiteSeconds && High.RespiteSeconds >= 0.0f);
		TestTrue(TEXT("more damage to a person"), High.DamageToHumanScale > Low.DamageToHumanScale && Low.DamageToHumanScale > 0.0f && High.DamageToHumanScale <= 1.0f);
		TestTrue(TEXT("more torpedoes at a person"), High.TorpedoOnHumanChance > Low.TorpedoOnHumanChance && Low.TorpedoOnHumanChance > 0.0f && High.TorpedoOnHumanChance <= 1.0f);
	}
	TestFalse(TEXT("no flanking of people on easy and normal"), FHCJBotSkill::Get(0).bFlankHumans || FHCJBotSkill::Get(1).bFlankHumans);
	TestTrue(TEXT("flanking on hard"), FHCJBotSkill::Get(2).bFlankHumans);
	TestEqual(TEXT("easy: one attacker"), FHCJBotSkill::Get(0).MaxAttackersOnHuman, 1);
	TestEqual(TEXT("easy: bots do two fifths of their damage to a person"), FHCJBotSkill::Get(0).DamageToHumanScale, 0.4f);
	TestEqual(TEXT("normal: a little more than half"), FHCJBotSkill::Get(1).DamageToHumanScale, 0.55f);
	TestEqual(TEXT("hard: full damage"), FHCJBotSkill::Get(2).DamageToHumanScale, 1.0f);
	{
		const FHCJBotSkill Ace = FHCJBotSkill::Get(3);
		const FHCJBotSkill Off;
		TestTrue(TEXT("ace has no fairness rule"), Ace.MaxAttackersOnHuman == 0 && Ace.MaxHuntersPerHuman == 0 && Ace.RearFireDelay == 0.0f && Ace.RearAimErrorScale == 1.0f
			&& Ace.FrontBias == 0.0f && Ace.HumanTailScore == 1.5f && Ace.bFlankHumans && Ace.ArrivalGrace == 0.0f && Ace.RespiteSeconds == 0.0f && Ace.DamageToHumanScale == 1.0f
			&& Ace.TorpedoOnHumanChance == 1.0f);
		TestTrue(TEXT("the defaults are off"), Off.MaxAttackersOnHuman == 0 && Off.RearFireDelay == 0.0f && Off.DamageToHumanScale == 1.0f && Off.bFlankHumans);
		for (int32 Level = 0; Level < HCJBalance::NumBotDifficulties; ++Level)
		{
			FHCJBotSkill Cleared = FHCJBotSkill::Get(Level);
			const FHCJBotSkill Row = Cleared;
			Cleared.ClearFairness();
			TestTrue(TEXT("cleared: ace's rules"), Cleared.MaxAttackersOnHuman == 0 && Cleared.MaxHuntersPerHuman == 0 && Cleared.RearFireDelay == 0.0f && Cleared.RearAimErrorScale == 1.0f
				&& Cleared.FrontBias == 0.0f && Cleared.HumanTailScore == 1.5f && Cleared.bFlankHumans && Cleared.ArrivalGrace == 0.0f && Cleared.RespiteSeconds == 0.0f
				&& Cleared.DamageToHumanScale == 1.0f && Cleared.TorpedoOnHumanChance == 1.0f);
			TestTrue(TEXT("cleared: no beginner's help"), Cleared.DamageFromHumanScale == 1.0f && Cleared.HumanHullRegenRate == 0.0f && Cleared.HumanRepairScale == 1.0f);
			TestTrue(TEXT("cleared: the skill numbers stay"), Cleared.AimErrorDeg == Row.AimErrorDeg && Cleared.ReactionDelay == Row.ReactionDelay && Cleared.Tactics == Row.Tactics);
			// What the menu says about the level: a name and one line that fits the page (the 13 pt line of the web build is 74 characters).
			const FString Name = HCJBalance::GetDifficultyName(Level);
			const FString Description = HCJBalance::GetDifficultyDescription(Level);
			TestTrue(TEXT("a name"), !Name.IsEmpty());
			TestTrue(TEXT("a description that fits"), Description.Len() > 20 && Description.Len() < 74);
		}
		TestEqual(TEXT("names"), FString(HCJBalance::GetDifficultyName(0)), FString(TEXT("Easy")));
		TestEqual(TEXT("names"), FString(HCJBalance::GetDifficultyName(3)), FString(TEXT("Ace")));
		TestEqual(TEXT("names are clamped"), FString(HCJBalance::GetDifficultyName(12)), FString(TEXT("Ace")));
	}

	// Bearings: 0 dead ahead, 180 dead astern, and the rear arc begins a little behind the beam (about 107 degrees).
	TestEqual(TEXT("dead ahead"), HCJBalance::BearingDegrees(FVector::ForwardVector, FVector(500.0, 0.0, 0.0)), 0.0f, 0.01f);
	TestEqual(TEXT("abeam"), HCJBalance::BearingDegrees(FVector::ForwardVector, FVector(0.0, -300.0, 0.0)), 90.0f, 0.01f);
	TestEqual(TEXT("dead astern"), HCJBalance::BearingDegrees(FVector::ForwardVector, FVector(-2.0, 0.0, 0.0)), 180.0f, 0.01f);
	TestEqual(TEXT("above"), HCJBalance::BearingDegrees(FVector::ForwardVector, FVector(100.0, 0.0, 100.0)), 45.0f, 0.01f);
	TestEqual(TEXT("no direction"), HCJBalance::BearingDegrees(FVector::ForwardVector, FVector::ZeroVector), -1.0f);
	TestFalse(TEXT("abeam is not behind"), HCJBalance::IsRearBearing(90.0f));
	TestFalse(TEXT("105 degrees is not behind"), HCJBalance::IsRearBearing(105.0f));
	TestTrue(TEXT("110 degrees is behind"), HCJBalance::IsRearBearing(110.0f));
	TestTrue(TEXT("dead astern is behind"), HCJBalance::IsRearBearing(180.0f));
	TestFalse(TEXT("an unknown bearing is not behind"), HCJBalance::IsRearBearing(-1.0f));
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

/**
 * Beginner's help (item 38): Easy alone has it, the other levels are exactly "off", the switch that takes it away gives
 * back Easy's row of before and touches no other level; the hull mending rule.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJBeginnerHelpTest, "HypeCubeJam.Balance.BeginnerHelp", HCJBalanceTestFlags)

bool FHCJBeginnerHelpTest::RunTest(const FString& Parameters)
{
	using namespace HCJBalance;
	const FHCJBotSkill Easy = FHCJBotSkill::Get(0);
	TestTrue(TEXT("easy: a person's guns do more to a bot, but not double"), Easy.DamageFromHumanScale > 1.0f && Easy.DamageFromHumanScale <= 2.0f);
	TestTrue(TEXT("easy: the hull mends, slowly (a full hull is 100)"), Easy.HumanHullRegenRate > 0.0f && Easy.HumanHullRegenRate <= 10.0f);
	TestTrue(TEXT("easy: a repair pad restores more"), Easy.HumanRepairScale > 1.0f && Easy.HumanRepairScale <= 2.0f);
	TestTrue(TEXT("easy: bots still hurt"), Easy.DamageToHumanScale >= 0.25f && Easy.DamageToHumanScale < ClassicEasyDamageToHumanScale);
	for (int32 Level = 1; Level < NumBotDifficulties; ++Level)
	{
		const FHCJBotSkill Row = FHCJBotSkill::Get(Level);
		TestTrue(TEXT("no help above easy"), Row.DamageFromHumanScale == 1.0f && Row.HumanHullRegenRate == 0.0f && Row.HumanRepairScale == 1.0f);
		FHCJBotSkill Classic = Row;
		Classic.ClearBeginnerHelp();
		TestTrue(TEXT("the classic switch changes nothing above easy"), Classic.DamageToHumanScale == Row.DamageToHumanScale && Classic.MaxAttackersOnHuman == Row.MaxAttackersOnHuman
			&& Classic.ArrivalGrace == Row.ArrivalGrace && Classic.RespiteSeconds == Row.RespiteSeconds && Classic.TorpedoOnHumanChance == Row.TorpedoOnHumanChance);
	}
	TestEqual(TEXT("normal's damage scale is what it was"), FHCJBotSkill::Get(1).DamageToHumanScale, 0.55f);
	{
		FHCJBotSkill Classic = Easy;
		Classic.ClearBeginnerHelp();
		TestTrue(TEXT("classic easy: no help"), Classic.DamageFromHumanScale == 1.0f && Classic.HumanHullRegenRate == 0.0f && Classic.HumanRepairScale == 1.0f);
		TestEqual(TEXT("classic easy: bots at half damage"), Classic.DamageToHumanScale, 0.5f);
		TestTrue(TEXT("classic easy: the fairness rules stay"), Classic.MaxAttackersOnHuman == 1 && Classic.ArrivalGrace == Easy.ArrivalGrace && Classic.RespiteSeconds == Easy.RespiteSeconds
			&& Classic.RearFireDelay == Easy.RearFireDelay && Classic.TorpedoOnHumanChance == Easy.TorpedoOnHumanChance && Classic.AimErrorDeg == Easy.AimErrorDeg);
	}

	// Hull mending: nothing inside the delay, then at the rate up to the ceiling; a hull above the ceiling is left alone.
	constexpr float Max = 100.0f;
	constexpr float Rate = 4.0f;
	const float Ceiling = Max * HullRegenCeiling;
	TestTrue(TEXT("the ceiling leaves something for the repair pads"), HullRegenCeiling > 0.3f && HullRegenCeiling < 1.0f);
	TestEqual(TEXT("nothing inside the delay"), StepHullRegen(20.0f, Max, HullRegenDelay - 0.1f, 0.5f, Rate, false), 20.0f, 0.0001f);
	TestEqual(TEXT("mends after the delay"), StepHullRegen(20.0f, Max, HullRegenDelay, 0.5f, Rate, false), 22.0f, 0.0001f);
	TestEqual(TEXT("stops at the ceiling"), StepHullRegen(Ceiling - 0.5f, Max, 60.0f, 0.5f, Rate, false), Ceiling, 0.0001f);
	TestEqual(TEXT("a hull above the ceiling is left alone"), StepHullRegen(90.0f, Max, 60.0f, 0.5f, Rate, false), 90.0f, 0.0001f);
	TestEqual(TEXT("a full hull is left alone"), StepHullRegen(Max, Max, 60.0f, 0.5f, Rate, false), Max, 0.0001f);
	TestEqual(TEXT("a destroyed ship mends nothing"), StepHullRegen(0.0f, Max, 60.0f, 0.5f, Rate, false), 0.0f, 0.0001f);
	TestEqual(TEXT("... nor one that is flagged destroyed"), StepHullRegen(20.0f, Max, 60.0f, 0.5f, Rate, true), 20.0f, 0.0001f);
	TestEqual(TEXT("no rate, no mending"), StepHullRegen(20.0f, Max, 60.0f, 0.5f, 0.0f, false), 20.0f, 0.0001f);
	TestEqual(TEXT("a negative step mends nothing"), StepHullRegen(20.0f, Max, 60.0f, -1.0f, Rate, false), 20.0f, 0.0001f);
	float Hull = 10.0f;
	float Time = 0.0f;
	while (Hull < Ceiling && Time < 60.0f)
	{
		Time += 1.0f / 60.0f;
		Hull = StepHullRegen(Hull, Max, Time, 1.0f / 60.0f, Rate, false);
	}
	TestEqual(TEXT("time from 10 to the ceiling"), Time, HullRegenDelay + (Ceiling - 10.0f) / Rate, 0.05f);
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
