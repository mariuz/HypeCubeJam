#include "Tests/HCJTestHelpers.h"

#include "Game/HCJGameplaySettings.h"
#include "Game/HCJUiLogic.h"
#include "Ship/HCJShieldFacings.h"
#include "Ship/HCJTorpedo.h"
#include "Ship/HCJWeapons.h"
#include "UI/HCJHints.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJTorpedoTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	float AngleBetweenDeg(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal())), -1.0f, 1.0f)));
	}
}

/** Stock and reload: a fresh bay, one shot at a time with the reload between, empty stays empty, a restock tops up to the maximum. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJTorpedoBayTest, "HypeCubeJam.Torpedo.StockAndReload", HCJTorpedoTestFlags)

bool FHCJTorpedoBayTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	constexpr float Reload = 1.8f;

	FHCJTorpedoBay Bay;
	TestFalse(TEXT("an empty bay cannot fire"), Bay.CanFire(100.0f));
	TestFalse(TEXT("and does not"), Bay.TryFire(100.0f, Reload));
	TestEqual(TEXT("an empty bay has nothing to wait for"), Bay.GetReloadFraction(100.0f, Reload), 1.0f);

	Bay.Reset(4, 6);
	TestEqual(TEXT("a fresh ship has the starting stock"), Bay.Count, 4);
	TestTrue(TEXT("loaded from the start"), Bay.CanFire(0.0f));
	Bay.Reset(9, 6);
	TestEqual(TEXT("never more than the maximum"), Bay.Count, 6);
	Bay.Reset(4, 6);

	// Fire all four as fast as the bay allows: one every Reload seconds, and nothing in between.
	float Time = 10.0f;
	for (int32 Shot = 0; Shot < 4; ++Shot)
	{
		HCJ_TEST_CHECK(Bay.TryFire(Time, Reload), TEXT("shot %d did not fire at %.2f"), Shot, Time);
		HCJ_TEST_CHECK(Bay.Count == 3 - Shot, TEXT("after shot %d the count is %d"), Shot, Bay.Count);
		HCJ_TEST_CHECK(!Bay.TryFire(Time + 0.01f, Reload) && !Bay.TryFire(Time + Reload - 0.05f, Reload), TEXT("shot %d: a second one went off during the reload"), Shot);
		HCJ_TEST_CHECK(Bay.Count == 3 - Shot, TEXT("a refused shot took a torpedo (count %d)"), Bay.Count);
		if (Bay.Count > 0)
		{
			const float Half = Bay.GetReloadFraction(Time + Reload * 0.5f, Reload);
			HCJ_TEST_CHECK(FMath::IsNearlyEqual(Half, 0.5f, 0.01f), TEXT("half way through the reload the indicator shows %.2f"), Half);
			HCJ_TEST_CHECK(Bay.GetReloadFraction(Time, Reload) < 0.01f && Bay.GetReloadFraction(Time + Reload, Reload) >= 1.0f, TEXT("reload indicator ends at %.2f"), Bay.GetReloadFraction(Time + Reload, Reload));
		}
		Time += Reload;
	}
	TestEqual(TEXT("four shots empty the bay"), Bay.Count, 0);
	TestFalse(TEXT("empty: no fifth shot, however long the wait"), Bay.TryFire(Time + 100.0f, Reload));
	TestEqual(TEXT("the count never goes below zero"), Bay.Count, 0);

	TestEqual(TEXT("a restock gives its amount"), Bay.Restock(3, 6), 3);
	TestEqual(TEXT("a second one"), Bay.Restock(3, 6), 3);
	TestEqual(TEXT("a third is refused: the bay is full"), Bay.Restock(3, 6), 0);
	TestEqual(TEXT("full is the maximum"), Bay.Count, 6);
	Bay.Count = 5;
	TestEqual(TEXT("one short of full takes one"), Bay.Restock(3, 6), 1);
	TestEqual(TEXT("a negative amount does nothing"), Bay.Restock(-2, 6), 0);
	TestTrue(TEXT("a restocked bay fires when the old reload is over"), Bay.TryFire(Time + 100.0f, Reload));

	// The configured torpedo is a secondary weapon with a stock and a reload in the wanted range.
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	for (const FHCJWeaponDef& Def : { FHCJWeaponDef::MakeDefault(EHCJWeapon::Torpedo), Gameplay.GetWeapon(EHCJWeapon::Torpedo) })
	{
		HCJ_TEST_CHECK(Def.Ammo >= 2 && Def.Ammo <= Gameplay.TorpedoMaxStock, TEXT("a fresh ship has %d torpedoes of at most %d"), Def.Ammo, Gameplay.TorpedoMaxStock);
		HCJ_TEST_CHECK(Def.FireInterval >= 1.0f && Def.FireInterval <= 3.0f, TEXT("reload is %.2f s"), Def.FireInterval);
		const FHCJWeaponDef Blaster = Gameplay.GetWeapon(EHCJWeapon::Blaster);
		const FHCJWeaponDef Missile = Gameplay.GetWeapon(EHCJWeapon::Missile);
		HCJ_TEST_CHECK(Def.ProjectileSpeed < 0.6f * Blaster.ProjectileSpeed, TEXT("a torpedo flies %.0f, a bolt %.0f"), Def.ProjectileSpeed, Blaster.ProjectileSpeed);
		HCJ_TEST_CHECK(Def.HomingTurnRate > 0.0f && Def.HomingTurnRate <= 0.5f * Missile.HomingTurnRate, TEXT("a torpedo turns %.0f deg/s, a missile %.0f"), Def.HomingTurnRate, Missile.HomingTurnRate);
	}
	TestFalse(TEXT("the torpedo is not a weapon a ship can hold"), HCJ::IsPrimaryWeapon(EHCJWeapon::Torpedo));
	TestTrue(TEXT("the blaster is"), HCJ::IsPrimaryWeapon(EHCJWeapon::Blaster));
	FHCJWeaponState State;
	State.Give(EHCJWeapon::Torpedo, Gameplay.GetWeapon(EHCJWeapon::Torpedo));
	TestTrue(TEXT("giving the torpedo as a weapon leaves the blaster"), State.Weapon == EHCJWeapon::Blaster);
	TestTrue(TEXT("the restock item is not a weapon pickup"), HCJ::ItemToWeapon(EHCJPickupItem::Torpedoes) == EHCJWeapon::Count);

	HCJ_TEST_FINISH();
}

/** Damage against a facing that is up, weak, down or overcharged, with the configured numbers: the design targets of the weapon. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJTorpedoDamageTest, "HypeCubeJam.Torpedo.DamageVsFacing", HCJTorpedoTestFlags)

bool FHCJTorpedoDamageTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	float ShieldLoss = 0.0f;
	float ToHull = 0.0f;

	// The rule itself, with round numbers: a torpedo that strips 30 from a shield and does 80 to an open hull.
	HCJTorpedo::SplitDamage(0.0f, 30.0f, 80.0f, ShieldLoss, ToHull);
	TestEqual(TEXT("facing down: nothing for the shield"), ShieldLoss, 0.0f);
	TestEqual(TEXT("facing down: everything to the hull"), ToHull, 80.0f);
	HCJTorpedo::SplitDamage(15.0f, 30.0f, 80.0f, ShieldLoss, ToHull);
	TestEqual(TEXT("half a torpedo's worth of shield: the facing loses all it has"), ShieldLoss, 15.0f);
	TestEqual(TEXT("and stops half the torpedo"), ToHull, 40.0f);
	HCJTorpedo::SplitDamage(30.0f, 30.0f, 80.0f, ShieldLoss, ToHull);
	TestEqual(TEXT("exactly enough shield: it is gone"), ShieldLoss, 30.0f);
	TestEqual(TEXT("and the hull untouched"), ToHull, 0.0f);
	HCJTorpedo::SplitDamage(52.0f, 30.0f, 80.0f, ShieldLoss, ToHull);
	TestEqual(TEXT("an overcharged facing loses only what the torpedo can strip"), ShieldLoss, 30.0f);
	TestEqual(TEXT("and lets nothing through"), ToHull, 0.0f);
	HCJTorpedo::SplitDamage(10.0f, 0.0f, 80.0f, ShieldLoss, ToHull);
	TestEqual(TEXT("a torpedo that cannot hurt shields is not stopped by one"), ToHull, 80.0f);
	TestEqual(TEXT("and leaves it alone"), ShieldLoss, 0.0f);
	HCJTorpedo::SplitDamage(-5.0f, 30.0f, 80.0f, ShieldLoss, ToHull);
	TestEqual(TEXT("a negative strength counts as down"), ToHull, 80.0f);
	// More shield never means more hull damage.
	float Previous = TNumericLimits<float>::Max();
	for (float Strength = 0.0f; Strength <= 60.0f; Strength += 0.5f)
	{
		HCJTorpedo::SplitDamage(Strength, 30.0f, 80.0f, ShieldLoss, ToHull);
		HCJ_TEST_CHECK(ToHull <= Previous + 1.0e-4f && ToHull >= 0.0f && ShieldLoss <= Strength + 1.0e-4f, TEXT("strength %.1f: %.2f to the hull (before %.2f), shield loss %.2f"), Strength, ToHull, Previous, ShieldLoss);
		Previous = ToHull;
	}

	// The configured weapon against a ship of this game (100 hull, facings of ShieldFacingCapacity).
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const float Facing = Gameplay.ShieldFacingCapacity;
	const float Shield = Gameplay.TorpedoShieldDamage;
	const float Hull = Gameplay.GetWeapon(EHCJWeapon::Torpedo).Damage;
	constexpr float ShipHull = 100.0f;

	// One on a full facing: strips most or all of it, and costs the hull little.
	HCJTorpedo::SplitDamage(Facing, Shield, Hull, ShieldLoss, ToHull);
	HCJ_TEST_CHECK(ShieldLoss >= 0.7f * Facing, TEXT("a torpedo strips %.1f of a full facing of %.1f"), ShieldLoss, Facing);
	HCJ_TEST_CHECK(ToHull <= 0.3f * ShipHull, TEXT("a torpedo on a full facing does %.1f to the hull"), ToHull);
	const float FirstHull = ToHull;
	const float FacingLeft = Facing - ShieldLoss;

	// One through a facing that is down: serious, but a fresh hull survives it.
	HCJTorpedo::SplitDamage(0.0f, Shield, Hull, ShieldLoss, ToHull);
	HCJ_TEST_CHECK(ToHull >= 0.6f * ShipHull && ToHull < ShipHull, TEXT("a torpedo through an open facing does %.1f to a hull of %.0f"), ToHull, ShipHull);

	// Two on the same facing: nearly a kill (at least 70 % of the hull), and never less than one on an open facing.
	HCJTorpedo::SplitDamage(FacingLeft, Shield, Hull, ShieldLoss, ToHull);
	const float TwoOnOne = FirstHull + ToHull;
	HCJ_TEST_CHECK(TwoOnOne >= 0.7f * ShipHull, TEXT("two torpedoes on one facing do %.1f to the hull"), TwoOnOne);

	// The blast scales both parts alike: half a torpedo on an open facing is half the hull damage.
	HCJTorpedo::SplitDamage(0.0f, Shield * 0.5f, Hull * 0.5f, ShieldLoss, ToHull);
	TestEqual(TEXT("half a blast on an open hull"), ToHull, Hull * 0.5f);
	// And a full facing still stops the same share of a weaker blast or more.
	HCJTorpedo::SplitDamage(Facing, Shield * 0.5f, Hull * 0.5f, ShieldLoss, ToHull);
	HCJ_TEST_CHECK(ToHull <= 0.5f * FirstHull + 1.0e-3f, TEXT("half a blast on a full facing does %.2f to the hull, a whole one %.2f"), ToHull, FirstHull);

	// A torpedo is the heavy single hit: more than any bolt through an open facing.
	for (const EHCJWeapon Weapon : { EHCJWeapon::Blaster, EHCJWeapon::Scatter, EHCJWeapon::Missile, EHCJWeapon::Pulse })
	{
		const FHCJWeaponDef& Def = Gameplay.GetWeapon(Weapon);
		HCJ_TEST_CHECK(Hull > Def.Damage, TEXT("a torpedo (%.0f) is no heavier than one projectile of %s (%.0f)"), Hull, *Def.Name, Def.Damage);
	}

	HCJ_TEST_FINISH();
}

/** Arming distance and blast falloff, and that the two keep a firer out of his own blast. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJTorpedoArmingTest, "HypeCubeJam.Torpedo.ArmingAndBlast", HCJTorpedoTestFlags)

bool FHCJTorpedoArmingTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	TestFalse(TEXT("not armed at the muzzle"), HCJTorpedo::IsArmed(0.0f, 900.0f));
	TestFalse(TEXT("not armed just short of the distance"), HCJTorpedo::IsArmed(899.0f, 900.0f));
	TestTrue(TEXT("armed at the distance"), HCJTorpedo::IsArmed(900.0f, 900.0f));
	TestTrue(TEXT("armed beyond it"), HCJTorpedo::IsArmed(5000.0f, 900.0f));
	TestTrue(TEXT("no arming distance: armed at once"), HCJTorpedo::IsArmed(0.0f, 0.0f));

	TestEqual(TEXT("full damage at the centre"), HCJTorpedo::BlastScale(0.0f, 100.0f, 400.0f, 0.25f), 1.0f);
	TestEqual(TEXT("full damage inside the inner radius"), HCJTorpedo::BlastScale(100.0f, 100.0f, 400.0f, 0.25f), 1.0f);
	TestEqual(TEXT("half way out"), HCJTorpedo::BlastScale(250.0f, 100.0f, 400.0f, 0.25f), 0.625f);
	TestEqual(TEXT("the edge value at the outer radius"), HCJTorpedo::BlastScale(400.0f, 100.0f, 400.0f, 0.25f), 0.25f);
	TestEqual(TEXT("nothing beyond"), HCJTorpedo::BlastScale(400.5f, 100.0f, 400.0f, 0.25f), 0.0f);
	TestEqual(TEXT("no blast radius: contact only"), HCJTorpedo::BlastScale(10.0f, 0.0f, 0.0f, 0.25f), 0.0f);
	float Previous = 1.0f;
	for (float Distance = 0.0f; Distance <= 600.0f; Distance += 5.0f)
	{
		const float Scale = HCJTorpedo::BlastScale(Distance, 100.0f, 400.0f, 0.25f);
		HCJ_TEST_CHECK(Scale <= Previous + 1.0e-5f && Scale >= 0.0f && Scale <= 1.0f, TEXT("blast scale %.3f at %.0f after %.3f"), Scale, Distance, Previous);
		Previous = Scale;
	}

	// The configured numbers: a torpedo that detonates the moment it is armed must not reach the ship that fired it,
	// even one that flies after it under boost for that long (the torpedo is faster than any ship).
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const FHCJWeaponDef& Def = Gameplay.GetWeapon(EHCJWeapon::Torpedo);
	const float HullRadius = FMath::Max(Gameplay.AimAssistRadius, 60.0f);
	HCJ_TEST_CHECK(Gameplay.TorpedoArmingDistance > Gameplay.TorpedoBlastRadius + HullRadius, TEXT("arming distance %.0f against a blast of %.0f + hull %.0f"),
		Gameplay.TorpedoArmingDistance, Gameplay.TorpedoBlastRadius, HullRadius);
	// 160 uu: the muzzle is that far ahead of the ship's centre. 6000 uu/s: boost speed of a ship.
	const float ArmSeconds = Gameplay.TorpedoArmingDistance / Def.ProjectileSpeed;
	const float Gap = 160.0f + Gameplay.TorpedoArmingDistance - 6000.0f * ArmSeconds - HullRadius;
	HCJ_TEST_CHECK(HCJTorpedo::BlastScale(Gap, Gameplay.TorpedoBlastInnerRadius, Gameplay.TorpedoBlastRadius, Gameplay.TorpedoBlastEdgeScale) <= Gameplay.TorpedoBlastEdgeScale + 0.3f,
		TEXT("a boosting firer is %.0f uu from his torpedo when it arms and would take %.2f of its blast"), Gap,
		HCJTorpedo::BlastScale(Gap, Gameplay.TorpedoBlastInnerRadius, Gameplay.TorpedoBlastRadius, Gameplay.TorpedoBlastEdgeScale));
	HCJ_TEST_CHECK(Gameplay.TorpedoBlastRadius <= 800.0f && Gameplay.TorpedoBlastInnerRadius <= Gameplay.TorpedoBlastRadius, TEXT("blast radii %.0f / %.0f"), Gameplay.TorpedoBlastInnerRadius, Gameplay.TorpedoBlastRadius);
	HCJ_TEST_CHECK(Gameplay.TorpedoDudDamageFraction < 0.5f, TEXT("a dud does %.2f of the damage"), Gameplay.TorpedoDudDamageFraction);
	HCJ_TEST_CHECK(Def.ProjectileSpeed * Def.Lifetime > 4.0f * Gameplay.TorpedoArmingDistance, TEXT("a torpedo flies %.0f uu and arms after %.0f"), Def.ProjectileSpeed * Def.Lifetime, Gameplay.TorpedoArmingDistance);

	HCJ_TEST_FINISH();
}

/** Homing: never more than the turn limit per step, reaches the wanted direction, and the lock is lost outside the cone or out of reach. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJTorpedoHomingTest, "HypeCubeJam.Torpedo.HomingTurnLimit", HCJTorpedoTestFlags)

bool FHCJTorpedoHomingTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	FRandomStream Random(7361);

	for (int32 Case = 0; Case < 2000; ++Case)
	{
		const FVector Current = Random.VRand() * Random.FRandRange(0.5f, 9000.0f);
		const FVector Wanted = Random.VRand() * Random.FRandRange(0.5f, 20000.0f);
		const float MaxDegrees = Random.FRandRange(0.05f, 30.0f);
		const FVector Turned = HCJTorpedo::TurnTowards(Current, Wanted, MaxDegrees);
		const float Before = AngleBetweenDeg(Current, Wanted);
		const float Step = AngleBetweenDeg(Current, Turned);
		const float After = AngleBetweenDeg(Turned, Wanted);
		HCJ_TEST_CHECK(FMath::IsNearlyEqual(static_cast<float>(Turned.Size()), 1.0f, 1.0e-3f), TEXT("case %d: result of length %.4f"), Case, Turned.Size());
		HCJ_TEST_CHECK(Step <= MaxDegrees + 0.05f, TEXT("case %d: turned %.3f degrees with a limit of %.3f"), Case, Step, MaxDegrees);
		HCJ_TEST_CHECK(Before <= MaxDegrees ? After < 0.05f : FMath::IsNearlyEqual(After, Before - MaxDegrees, 0.06f), TEXT("case %d: %.3f degrees off before, %.3f after a step of at most %.3f"), Case, Before, After, MaxDegrees);
	}
	TestTrue(TEXT("no turn rate: straight on"), HCJTorpedo::TurnTowards(FVector(3, 0, 0), FVector(0, 1, 0), 0.0f).Equals(FVector(1, 0, 0)));
	TestTrue(TEXT("no target direction: straight on"), HCJTorpedo::TurnTowards(FVector(0, 2, 0), FVector::ZeroVector, 10.0f).Equals(FVector(0, 1, 0)));
	const FVector Reversed = HCJTorpedo::TurnTowards(FVector(1, 0, 0), FVector(-1, 0, 0), 10.0f);
	TestTrue(TEXT("a target dead astern still gets a turn of the limit"), FMath::IsNearlyEqual(AngleBetweenDeg(FVector(1, 0, 0), Reversed), 10.0f, 0.05f));

	// A torpedo of the game against a target that crosses its path: it turns at most its rate, so a ship that boosts
	// across gets out of the way where a missile would follow.
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const FHCJWeaponDef& Torpedo = Gameplay.GetWeapon(EHCJWeapon::Torpedo);
	FVector Direction(1, 0, 0);
	constexpr float Dt = 1.0f / 60.0f;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Direction = HCJTorpedo::TurnTowards(Direction, FVector(0, 1, 0), Torpedo.HomingTurnRate * Dt);
	}
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(AngleBetweenDeg(FVector(1, 0, 0), Direction), FMath::Min(Torpedo.HomingTurnRate, 90.0f), 0.5f), TEXT("in one second the torpedo turned %.2f degrees (rate %.0f)"),
		AngleBetweenDeg(FVector(1, 0, 0), Direction), Torpedo.HomingTurnRate);

	// Keeping the lock.
	TestTrue(TEXT("dead ahead and near"), HCJTorpedo::KeepsLock(FVector(1, 0, 0), FVector(3000, 0, 0), 60.0f, 18000.0f));
	TestTrue(TEXT("50 degrees off inside a 60 degree cone"), HCJTorpedo::KeepsLock(FVector(1, 0, 0), FRotator(0, 50, 0).Vector() * 3000.0, 60.0f, 18000.0f));
	TestFalse(TEXT("70 degrees off: lost"), HCJTorpedo::KeepsLock(FVector(1, 0, 0), FRotator(0, 70, 0).Vector() * 3000.0, 60.0f, 18000.0f));
	TestFalse(TEXT("behind: lost"), HCJTorpedo::KeepsLock(FVector(1, 0, 0), FVector(-500, 0, 0), 60.0f, 18000.0f));
	TestFalse(TEXT("out of reach: lost"), HCJTorpedo::KeepsLock(FVector(1, 0, 0), FVector(18500, 0, 0), 60.0f, 18000.0f));
	TestTrue(TEXT("up and ahead counts like sideways"), HCJTorpedo::KeepsLock(FVector(1, 0, 0), FVector(1000, 0, 1000), 60.0f, 18000.0f));
	TestTrue(TEXT("on top of the target"), HCJTorpedo::KeepsLock(FVector(1, 0, 0), FVector::ZeroVector, 60.0f, 18000.0f));

	HCJ_TEST_FINISH();
}

/** The facing a blast lands on is the one towards the blast, and the bots' rule for firing a torpedo. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJTorpedoFacingAndBotsTest, "HypeCubeJam.Torpedo.BlastFacingAndBots", HCJTorpedoTestFlags)

bool FHCJTorpedoFacingAndBotsTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	// The ship takes a blast as a hit whose impact point is the blast: the facing follows from where the blast is in
	// the ship's own frame, whatever way the ship is turned in the world and wherever the torpedo came from.
	struct FCase
	{
		float ShipYaw;
		FVector BlastOffsetWorld;
		EHCJShieldFacing Expected;
	};
	const FCase Cases[] = {
		{ 0.0f, FVector(300, 0, 0), EHCJShieldFacing::Fore },
		{ 0.0f, FVector(-300, 20, 0), EHCJShieldFacing::Aft },
		{ 0.0f, FVector(0, 300, 40), EHCJShieldFacing::Starboard },
		{ 0.0f, FVector(10, -300, -40), EHCJShieldFacing::Port },
		{ 90.0f, FVector(0, 300, 0), EHCJShieldFacing::Fore },
		{ 90.0f, FVector(300, 0, 0), EHCJShieldFacing::Port },
		{ 180.0f, FVector(300, 0, 0), EHCJShieldFacing::Aft },
		{ -90.0f, FVector(300, 0, 0), EHCJShieldFacing::Starboard },
		{ 45.0f, FVector(0, -400, 0), EHCJShieldFacing::Port },
		{ 150.0f, FVector(400, 0, 100), EHCJShieldFacing::Aft },
	};
	for (const FCase& Case : Cases)
	{
		const FTransform Ship(FRotator(0.0f, Case.ShipYaw, 0.0f), FVector(123456.0, -7890.0, 250.0));
		const FVector Blast = Ship.GetLocation() + Case.BlastOffsetWorld;
		const EHCJShieldFacing Facing = HCJShield::SelectFacing(Ship.InverseTransformVectorNoScale(Blast - Ship.GetLocation()));
		HCJ_TEST_CHECK(Facing == Case.Expected, TEXT("ship at yaw %.0f, blast at offset %s: facing %s, expected %s"), Case.ShipYaw, *Case.BlastOffsetWorld.ToCompactString(),
			HCJShield::FacingName(Facing), HCJShield::FacingName(Case.Expected));
	}
	// A blast between two ships lands on the sides they turn to each other.
	{
		const FTransform Left(FRotator(0.0f, 0.0f, 0.0f), FVector(0, 0, 0));
		const FTransform Right(FRotator(0.0f, 180.0f, 0.0f), FVector(600, 0, 0));
		const FVector Blast(300, 0, 0);
		TestTrue(TEXT("the ship looking at the blast takes it on the fore facing"), HCJShield::SelectFacing(Left.InverseTransformVectorNoScale(Blast - Left.GetLocation())) == EHCJShieldFacing::Fore);
		TestTrue(TEXT("so does the one opposite"), HCJShield::SelectFacing(Right.InverseTransformVectorNoScale(Blast - Right.GetLocation())) == EHCJShieldFacing::Fore);
	}

	// Bots.
	constexpr float MinRange = 1750.0f;
	constexpr float MaxRange = 9000.0f;
	FHCJTorpedoBotSkill Always;
	Always.WeakFacingChance = 1.0f;
	Always.OpenerChance = 1.0f;
	FHCJTorpedoBotInput Input;
	Input.bReady = true;
	Input.bLockedAndAimed = true;
	Input.Distance = 4000.0f;
	Input.FacingFraction = 0.0f;
	Input.HullFraction = 1.0f;
	Input.Roll = 0.5f;
	Input.SecondsOnTarget = 10.0f;
	TestTrue(TEXT("locked, in range, the facing shown is down: fire"), HCJTorpedo::ShouldBotFire(Input, Always, MinRange, MaxRange));
	{
		FHCJTorpedoBotInput Other = Input;
		Other.bReady = false;
		TestFalse(TEXT("nothing loaded"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
		Other = Input;
		Other.bLockedAndAimed = false;
		TestFalse(TEXT("no lock"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
		Other = Input;
		Other.Distance = MinRange - 1.0f;
		TestFalse(TEXT("too close: it would not arm, or the blast would reach the bot"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
		Other = Input;
		Other.Distance = MaxRange + 1.0f;
		TestFalse(TEXT("too far"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
		Other = Input;
		Other.HullFraction = 0.1f;
		TestFalse(TEXT("the target is nearly dead: the guns do it"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
		Other = Input;
		Other.FacingFraction = 0.2f;
		TestTrue(TEXT("a weak facing"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
		Other.FacingFraction = 1.0f;
		TestFalse(TEXT("a full facing late in a fight: keep it"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
		Other.SecondsOnTarget = 1.0f;
		Other.Distance = 6000.0f;
		TestTrue(TEXT("a full facing at medium range, early: the opener"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
		Other.Distance = 3000.0f;
		TestFalse(TEXT("no opener from close up"), HCJTorpedo::ShouldBotFire(Other, Always, MinRange, MaxRange));
	}
	// Difficulty: harder bots fire and dodge more readily, none of them always; a facing that is down is fired at
	// twice as readily as a weak one.
	for (int32 Difficulty = 0; Difficulty <= 2; ++Difficulty)
	{
		const FHCJTorpedoBotSkill Skill = HCJTorpedo::GetBotSkill(Difficulty);
		HCJ_TEST_CHECK(Skill.WeakFacingChance > 0.0f && Skill.WeakFacingChance <= 0.5f && Skill.OpenerChance > 0.0f && Skill.OpenerChance < Skill.WeakFacingChance
			&& Skill.EvadeChance > 0.0f && Skill.EvadeChance <= 1.0f, TEXT("difficulty %d: chances %.3f / %.3f / %.3f"), Difficulty, Skill.WeakFacingChance, Skill.OpenerChance, Skill.EvadeChance);
		if (Difficulty > 0)
		{
			const FHCJTorpedoBotSkill Easier = HCJTorpedo::GetBotSkill(Difficulty - 1);
			HCJ_TEST_CHECK(Skill.WeakFacingChance > Easier.WeakFacingChance && Skill.OpenerChance > Easier.OpenerChance && Skill.EvadeChance > Easier.EvadeChance
				&& Skill.MinSecondsBetween < Easier.MinSecondsBetween,
				TEXT("difficulty %d is not more eager than %d"), Difficulty, Difficulty - 1);
		}
		int32 FiredDown = 0;
		int32 FiredWeak = 0;
		for (int32 Step = 0; Step < 1000; ++Step)
		{
			FHCJTorpedoBotInput Rolled = Input;
			Rolled.Roll = Step / 1000.0f;
			FiredDown += HCJTorpedo::ShouldBotFire(Rolled, Skill, MinRange, MaxRange) ? 1 : 0;
			Rolled.FacingFraction = 0.2f;
			FiredWeak += HCJTorpedo::ShouldBotFire(Rolled, Skill, MinRange, MaxRange) ? 1 : 0;
		}
		HCJ_TEST_CHECK(FiredWeak > 0 && FiredWeak < 1000 && FMath::Abs(FiredDown - FMath::Min(2 * FiredWeak, 1000)) <= 2, TEXT("difficulty %d: fired %d times of 1000 at a weak facing, %d at one that is down"), Difficulty, FiredWeak, FiredDown);
	}
	TestTrue(TEXT("difficulty is clamped"), HCJTorpedo::GetBotSkill(7).EvadeChance == HCJTorpedo::GetBotSkill(2).EvadeChance && HCJTorpedo::GetBotSkill(-3).EvadeChance == HCJTorpedo::GetBotSkill(0).EvadeChance);

	HCJ_TEST_FINISH();
}

/** HUD: the hint about firing a torpedo, the restock item, and markers that keep clear of the HUD panels. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJTorpedoHudTest, "HypeCubeJam.Torpedo.HudRules", HCJTorpedoTestFlags)

bool FHCJTorpedoHudTest::RunTest(const FString& Parameters)
{
	// The hint: once per session, after the onboarding, not urgent, but it replaces a lesser contextual hint.
	const FHCJHintRule& Rule = FHCJHintDirector::GetRule(EHCJHint::TorpedoChance);
	TestEqual(TEXT("shown once"), Rule.MaxShows, 1);
	TestTrue(TEXT("not urgent"), Rule.Priority < FHCJHintDirector::UrgentPriority);
	TestTrue(TEXT("but it interrupts lesser hints"), Rule.Priority >= FHCJHintDirector::InterruptPriority);
	{
		const uint32 AllDone = FHCJHintDirector::Bit(EHCJHint::Move) | FHCJHintDirector::Bit(EHCJHint::Fire) | FHCJHintDirector::Bit(EHCJHint::Boost) | FHCJHintDirector::Bit(EHCJHint::Target);
		FHCJHintDirector Director;
		FHCJHintInput Input;
		Input.bHasShip = true;
		Input.DoneMask = AllDone;
		Input.ActiveMask = FHCJHintDirector::Bit(EHCJHint::Portal);
		float Time = 0.0f;
		for (; Time < FHCJHintDirector::GapSeconds + 1.0f; Time += 0.1f)
		{
			Director.Update(Time, Input);
		}
		TestTrue(TEXT("a portal hint is up"), Director.GetCurrent() == EHCJHint::Portal);
		Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::TorpedoChance);
		TestTrue(TEXT("the target's facing goes down: the torpedo hint takes over"), Director.Update(Time, Input) == EHCJHint::TorpedoChance);
		Input.ActiveMask = 0;
		for (float End = Time + 30.0f; Time < End; Time += 0.1f)
		{
			Director.Update(Time, Input);
		}
		TestTrue(TEXT("it ends"), Director.GetCurrent() == EHCJHint::None);
		Input.ActiveMask = FHCJHintDirector::Bit(EHCJHint::TorpedoChance);
		for (float End = Time + 5.0f; Time < End; Time += 0.1f)
		{
			Director.Update(Time, Input);
		}
		TestTrue(TEXT("and is not shown a second time"), Director.GetCurrent() != EHCJHint::TorpedoChance);
		TestEqual(TEXT("one showing"), Director.GetShowCount(EHCJHint::TorpedoChance), 1);
	}
	TestTrue(TEXT("the restock item is explained"), FCString::Strlen(HCJHints::GetItemHint(static_cast<int32>(EHCJPickupItem::Torpedoes))) > 20);
	TestTrue(TEXT("and named"), FString(HCJ::GetItemName(EHCJPickupItem::Torpedoes)) == TEXT("TORPEDOES"));
	TestTrue(TEXT("the kill feed tag is the weapon name"), UHCJGameplaySettings::Get().GetWeapon(EHCJWeapon::Torpedo).Name == TEXT("TORPEDO"));

	// Markers and panels: the bottom-left block of a 1280x720 HUD and the weapon panel at the bottom right.
	const FBox2D Panels[] = { FBox2D(FVector2D(16, 625), FVector2D(310, 704)), FBox2D(FVector2D(1064, 600), FVector2D(1264, 704)) };
	TestTrue(TEXT("a marker in the middle of the screen is clear"), HCJUi::IsMarkerClear(FBox2D(FVector2D(600, 300), FVector2D(680, 380)), Panels));
	TestFalse(TEXT("a label over the shield / hull block is not"), HCJUi::IsMarkerClear(FBox2D(FVector2D(220, 600), FVector2D(330, 640)), Panels));
	TestFalse(TEXT("a marker inside a panel is not"), HCJUi::IsMarkerClear(FBox2D(FVector2D(100, 650), FVector2D(120, 670)), Panels));
	TestFalse(TEXT("a marker that covers a panel is not"), HCJUi::IsMarkerClear(FBox2D(FVector2D(0, 500), FVector2D(400, 720)), Panels));
	TestTrue(TEXT("just above the block is clear"), HCJUi::IsMarkerClear(FBox2D(FVector2D(100, 560), FVector2D(200, 624)), Panels));
	TestFalse(TEXT("but not with a margin"), HCJUi::IsMarkerClear(FBox2D(FVector2D(100, 560), FVector2D(200, 624)), Panels, 4.0f));
	TestFalse(TEXT("the second panel counts as well"), HCJUi::IsMarkerClear(FBox2D(FVector2D(1050, 590), FVector2D(1070, 610)), Panels));
	TestTrue(TEXT("no panels: always clear"), HCJUi::IsMarkerClear(FBox2D(FVector2D(0, 0), FVector2D(10, 10)), TConstArrayView<FBox2D>()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
