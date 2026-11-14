#include "Tests/HCJTestHelpers.h"

#include "Algo/Reverse.h"
#include "Audio/HCJMusicLogic.h"
#include "Effects/HCJEffectsLogic.h"
#include "Game/HCJBalance.h"
#include "Game/HCJGameplaySettings.h"
#include "Ship/HCJPhaser.h"
#include "Ship/HCJWeapons.h"
#include "UI/HCJHints.h"
#include "World/HCJAsteroidField.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJPhaserTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The rules a match uses with the built-in numbers. */
	FHCJAsteroidRules MakeRules()
	{
		FHCJAsteroidRules Rules;
		Rules.bEnabled = true;
		return Rules;
	}

	/** What AHCJUniverse does with the destroyed set, without a world: the rocks that exist, by key. */
	struct FFieldSim
	{
		TMap<uint32, FHCJAsteroid> Alive;
		FHCJDestroyedSet Destroyed;
		const FHCJSectorLayout* Layout = nullptr;
		FHCJAsteroidRules Rules;

		void Init(const FHCJSectorLayout& InLayout, const FHCJAsteroidRules& InRules)
		{
			Layout = &InLayout;
			Rules = InRules;
			for (int32 Index = 0; Index < InLayout.Asteroids.Num(); ++Index)
			{
				Alive.Add(HCJAsteroids::PackKey(InLayout.SectorId, HCJAsteroids::MakeId(Index)), InLayout.Asteroids[Index]);
			}
		}

		bool Apply(uint32 Key)
		{
			if (!Destroyed.Add(Key))
			{
				return false;
			}
			FHCJAsteroid Rock;
			if (!Alive.RemoveAndCopyValue(Key, Rock))
			{
				return true;
			}
			TArray<TPair<uint16, FHCJAsteroid>> Pieces;
			HCJAsteroids::CollectPieces(Layout->SectorId, Layout->Seed, HCJAsteroids::GetKeyId(Key), Rock, Rules, Destroyed, Pieces);
			for (const TPair<uint16, FHCJAsteroid>& Piece : Pieces)
			{
				Alive.Add(HCJAsteroids::PackKey(Layout->SectorId, Piece.Key), Piece.Value);
			}
			return true;
		}

		TArray<uint32> SortedAlive() const
		{
			TArray<uint32> Keys;
			Alive.GetKeys(Keys);
			Keys.Sort();
			return Keys;
		}
	};

	bool SameBits(const FHCJAsteroid& A, const FHCJAsteroid& B)
	{
		return FMemory::Memcmp(&A.LocalPosition, &B.LocalPosition, sizeof(FVector)) == 0 && FMemory::Memcmp(&A.Scale3D, &B.Scale3D, sizeof(FVector)) == 0
			&& FMemory::Memcmp(&A.Rotation, &B.Rotation, sizeof(FQuat)) == 0 && A.BoundingRadius == B.BoundingRadius && A.Shape == B.Shape;
	}
}

/** The phaser bank: drains in the configured time, locks when empty, waits, recharges, unlocks at the relock level. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPhaserBankTest, "HypeCubeJam.Phaser.Bank", HCJPhaserTestFlags)

bool FHCJPhaserBankTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	constexpr float Drain = 4.0f;
	constexpr float Delay = 1.0f;
	constexpr float Recharge = 5.0f;
	constexpr float Relock = 0.3f;
	constexpr float Step = 1.0f / 60.0f;

	FHCJPhaserBank Bank;
	TestEqual(TEXT("a new bank is full"), Bank.Charge, 1.0f);
	TestFalse(TEXT("and not locked"), Bank.bLocked);
	TestFalse(TEXT("no beam without the trigger"), Bank.Step(false, Step, Drain, Delay, Recharge, Relock));
	TestEqual(TEXT("a full bank stays full"), Bank.Charge, 1.0f);

	// Hold the trigger: the beam runs for the drain time, then the bank is empty and locked.
	float BeamSeconds = 0.0f;
	int32 Guard = 0;
	while (Bank.Step(true, Step, Drain, Delay, Recharge, Relock) && ++Guard < 100000)
	{
		BeamSeconds += Step;
	}
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(BeamSeconds, Drain, 3.0f * Step), TEXT("a full bank gave %.2f s of beam, configured %.2f"), BeamSeconds, Drain);
	TestTrue(TEXT("emptied: locked"), Bank.bLocked);
	TestEqual(TEXT("emptied: charge 0"), Bank.Charge, 0.0f);
	TestEqual(TEXT("no seconds left while locked"), Bank.GetSecondsLeft(Drain), 0.0f);

	// Still holding: no beam, and the recharge starts after the delay although the trigger is held (the blaster fires meanwhile).
	float Waited = 0.0f;
	while (Bank.bLocked && Waited < 60.0f)
	{
		HCJ_TEST_CHECK(!Bank.Step(true, Step, Drain, Delay, Recharge, Relock), TEXT("a locked bank gave a beam after %.2f s"), Waited);
		Waited += Step;
		if (Waited < Delay - 2.0f * Step)
		{
			HCJ_TEST_CHECK(Bank.Charge == 0.0f, TEXT("the bank recharged during the delay (%.3f after %.2f s)"), Bank.Charge, Waited);
		}
	}
	const float ExpectedUnlock = Delay + Relock * Recharge;
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(Waited, ExpectedUnlock, 0.1f), TEXT("unlocked after %.2f s, expected %.2f"), Waited, ExpectedUnlock);
	HCJ_TEST_CHECK(Bank.Charge >= Relock && Bank.Charge < Relock + 0.02f, TEXT("unlocked at %.3f"), Bank.Charge);
	TestTrue(TEXT("unlocked: the beam runs again"), Bank.Step(true, Step, Drain, Delay, Recharge, Relock));

	// A short burst does not lock, and a bank left alone is full again after delay + recharge.
	FHCJPhaserBank Burst;
	for (float Time = 0.0f; Time < 1.0f; Time += Step)
	{
		Burst.Step(true, Step, Drain, Delay, Recharge, Relock);
	}
	HCJ_TEST_CHECK(!Burst.bLocked && FMath::IsNearlyEqual(Burst.Charge, 0.75f, 0.02f), TEXT("after a second of beam: charge %.3f, locked %d"), Burst.Charge, Burst.bLocked ? 1 : 0);
	float Idle = 0.0f;
	while (Burst.Charge < 1.0f && Idle < 60.0f)
	{
		Burst.Step(false, Step, Drain, Delay, Recharge, Relock);
		Idle += Step;
	}
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(Idle, Delay + 0.25f * Recharge, 0.1f), TEXT("a quarter of the bank came back in %.2f s"), Idle);
	HCJ_TEST_CHECK(Burst.Charge == 1.0f, TEXT("the bank overfilled to %.4f"), Burst.Charge);

	// The same total comes out whatever the frame rate.
	for (const float Rate : { 20.0f, 144.0f })
	{
		FHCJPhaserBank Other;
		float Seconds = 0.0f;
		while (Other.Step(true, 1.0f / Rate, Drain, Delay, Recharge, Relock) && Seconds < 60.0f)
		{
			Seconds += 1.0f / Rate;
		}
		HCJ_TEST_CHECK(FMath::IsNearlyEqual(Seconds, Drain, 3.0f / Rate), TEXT("at %.0f fps the bank gave %.2f s"), Rate, Seconds);
	}

	// The owner's byte.
	for (const float Charge : { 0.0f, 0.3f, 0.5f, 1.0f })
	{
		for (const bool bLocked : { false, true })
		{
			float OutCharge = -1.0f;
			bool bOutLocked = !bLocked;
			HCJPhaser::UnpackCharge(HCJPhaser::PackCharge(Charge, bLocked), OutCharge, bOutLocked);
			HCJ_TEST_CHECK(FMath::IsNearlyEqual(OutCharge, Charge, 0.005f) && bOutLocked == bLocked, TEXT("charge %.2f locked %d came back as %.3f / %d"), Charge, bLocked ? 1 : 0, OutCharge, bOutLocked ? 1 : 0);
		}
	}
	HCJ_TEST_FINISH();
}

/** Damage with range, the beam byte, the aim assist, and the numbers the weapon was designed to. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPhaserFalloffTest, "HypeCubeJam.Phaser.FalloffAndAssist", HCJPhaserTestFlags)

bool FHCJPhaserFalloffTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	constexpr float Range = 9000.0f;

	TestEqual(TEXT("point blank: full damage"), HCJPhaser::RangeScale(0.0f, Range, 0.3f, 0.45f), 1.0f);
	TestEqual(TEXT("inside the full range: full damage"), HCJPhaser::RangeScale(2700.0f, Range, 0.3f, 0.45f), 1.0f);
	TestTrue(TEXT("at the end of the beam: the edge scale"), FMath::IsNearlyEqual(HCJPhaser::RangeScale(Range, Range, 0.3f, 0.45f), 0.45f, 0.001f));
	TestEqual(TEXT("beyond the beam: nothing"), HCJPhaser::RangeScale(Range + 1.0f, Range, 0.3f, 0.45f), 0.0f);
	TestTrue(TEXT("half way down the slope"), FMath::IsNearlyEqual(HCJPhaser::RangeScale(5850.0f, Range, 0.3f, 0.45f), 0.725f, 0.001f));
	float Last = 1.0f;
	for (float Distance = 0.0f; Distance <= Range; Distance += 150.0f)
	{
		const float Scale = HCJPhaser::RangeScale(Distance, Range, 0.3f, 0.45f);
		HCJ_TEST_CHECK(Scale <= Last + KINDA_SMALL_NUMBER && Scale >= 0.45f - KINDA_SMALL_NUMBER, TEXT("at %.0f uu the scale is %.3f after %.3f"), Distance, Scale, Last);
		Last = Scale;
	}

	// The beam byte: never 0 for a running beam, within one step of the length.
	TestTrue(TEXT("a beam of no length is still a beam"), HCJPhaser::PackLength(0.0f, Range) >= 1);
	TestEqual(TEXT("the full range is 255"), static_cast<int32>(HCJPhaser::PackLength(Range, Range)), 255);
	for (float Length = 10.0f; Length <= Range; Length += 173.0f)
	{
		const float Back = HCJPhaser::UnpackLength(HCJPhaser::PackLength(Length, Range), Range);
		HCJ_TEST_CHECK(Back >= Length - 0.01f && Back <= Length + Range / 255.0f + 0.01f, TEXT("length %.0f came back as %.0f"), Length, Back);
	}

	// Aim assist: straight ahead wins over off-axis, a little off the line still counts, far off does not, the locked target has more room.
	const FVector Origin(1000.0, -500.0, 200.0);
	const FVector Aim = FVector(1.0, 0.2, 0.1).GetSafeNormal();
	const FVector Side = FVector::CrossProduct(Aim, FVector::UpVector).GetSafeNormal();
	const auto At = [&](float Distance, float OffDegrees)
	{
		return Origin + (Aim * FMath::Cos(FMath::DegreesToRadians(OffDegrees)) + Side * FMath::Sin(FMath::DegreesToRadians(OffDegrees))) * Distance;
	};
	{
		const FVector Candidates[] = { At(4000.0f, 1.2f), At(6000.0f, 0.4f), At(3000.0f, 5.0f) };
		TestEqual(TEXT("the one nearest the aim line"), HCJPhaser::PickAssistTarget(Origin, Aim, Candidates, INDEX_NONE, Range, 1.6f, 3.0f), 1);
		TestEqual(TEXT("the locked target wins inside its own cone"), HCJPhaser::PickAssistTarget(Origin, Aim, Candidates, 0, Range, 1.6f, 3.0f), 0);
	}
	{
		const FVector Candidates[] = { At(4000.0f, 2.4f) };
		TestEqual(TEXT("2.4 degrees off: no assist for a stranger"), HCJPhaser::PickAssistTarget(Origin, Aim, Candidates, INDEX_NONE, Range, 1.6f, 3.0f), static_cast<int32>(INDEX_NONE));
		TestEqual(TEXT("2.4 degrees off: the locked target is held"), HCJPhaser::PickAssistTarget(Origin, Aim, Candidates, 0, Range, 1.6f, 3.0f), 0);
	}
	{
		const FVector Candidates[] = { At(9500.0f, 0.1f), At(4000.0f, 179.0f), At(4000.0f, 3.5f) };
		TestEqual(TEXT("out of range, behind, or too far off: a straight beam"), HCJPhaser::PickAssistTarget(Origin, Aim, Candidates, 2, Range, 1.6f, 3.0f), static_cast<int32>(INDEX_NONE));
	}
	TestEqual(TEXT("nobody there"), HCJPhaser::PickAssistTarget(Origin, Aim, TConstArrayView<FVector>(), INDEX_NONE, Range, 1.6f, 3.0f), static_cast<int32>(INDEX_NONE));

	// The configured weapon against the design: the main weapon, but not a better blaster at every range.
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const FHCJWeaponDef& Phaser = Gameplay.GetWeapon(EHCJWeapon::Phaser);
	const FHCJWeaponDef& Blaster = Gameplay.GetWeapon(EHCJWeapon::Blaster);
	TestTrue(TEXT("the phaser row is a beam"), Phaser.bBeam && Phaser.BeamRange > 0.0f && Phaser.Ammo == 0);
	TestFalse(TEXT("the blaster row is not"), Blaster.bBeam);
	const float BlasterDps = Blaster.Damage * Blaster.Pellets / Blaster.FireInterval;
	HCJ_TEST_CHECK(Phaser.Damage < BlasterDps, TEXT("the phaser does %.0f per second, the blaster %.0f at full hit rate: a beam that never misses must do less"), Phaser.Damage, BlasterDps);
	// One full bank, held on one side at close range, strips a facing and most of a hull but does not kill two ships.
	const float BankDamage = Phaser.Damage * Gameplay.PhaserDrainSeconds;
	const float OneSide = Gameplay.ShieldFacingCapacity + 100.0f;
	HCJ_TEST_CHECK(BankDamage >= 0.8f * OneSide && BankDamage < 2.0f * OneSide, TEXT("a full bank does %.0f; a ship from one side takes %.0f"), BankDamage, OneSide);
	HCJ_TEST_CHECK(Phaser.GetRange() < Blaster.GetRange(), TEXT("phaser reach %.0f, blaster reach %.0f: the beam is the short-range weapon"), Phaser.GetRange(), Blaster.GetRange());
	HCJ_TEST_CHECK(Gameplay.PhaserRelockLevel > 0.05f && Gameplay.PhaserRelockLevel < 0.8f, TEXT("relock level %.2f"), Gameplay.PhaserRelockLevel);
	HCJ_TEST_CHECK(Gameplay.PhaserLockAssistDegrees >= Gameplay.PhaserAssistDegrees && Gameplay.PhaserLockAssistDegrees <= 6.0f, TEXT("assist %.1f / %.1f degrees is not modest"),
		Gameplay.PhaserAssistDegrees, Gameplay.PhaserLockAssistDegrees);
	// A step of damage is small: a beam never does a bolt's worth in one event.
	HCJ_TEST_CHECK(Phaser.Damage * Phaser.FireInterval < Blaster.Damage, TEXT("one phaser step does %.1f"), Phaser.Damage * Phaser.FireInterval);
	HCJ_TEST_FINISH();
}

/** Weapon slots: the phaser is a base weapon like the blaster, a pickup takes its place and falls back to it, B switches the base. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPhaserSlotsTest, "HypeCubeJam.Phaser.WeaponSlots", HCJPhaserTestFlags)

bool FHCJPhaserSlotsTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();

	TestTrue(TEXT("the phaser is a base weapon"), HCJ::IsBaseWeapon(EHCJWeapon::Phaser));
	TestTrue(TEXT("the blaster is a base weapon"), HCJ::IsBaseWeapon(EHCJWeapon::Blaster));
	TestTrue(TEXT("the phaser is fired with the main trigger"), HCJ::IsPrimaryWeapon(EHCJWeapon::Phaser));
	for (const EHCJWeapon Weapon : { EHCJWeapon::Scatter, EHCJWeapon::Rail, EHCJWeapon::Missile, EHCJWeapon::Pulse, EHCJWeapon::Torpedo })
	{
		HCJ_TEST_CHECK(!HCJ::IsBaseWeapon(Weapon), TEXT("weapon %d counts as a base weapon"), static_cast<int32>(Weapon));
	}
	TestEqual(TEXT("the phaser has a name for the kill feed"), Gameplay.GetWeapon(EHCJWeapon::Phaser).Name, FString(TEXT("PHASER")));

	// A ship with the phaser as its base: a pickup replaces it, runs dry, and the phaser is back.
	FHCJWeaponState State;
	State.BaseWeapon = EHCJWeapon::Phaser;
	State.Drop();
	TestTrue(TEXT("a new ship holds its base weapon"), State.Weapon == EHCJWeapon::Phaser && State.Ammo == 0);
	TestFalse(TEXT("the phaser uses no ammunition"), State.ConsumeShot(Gameplay.GetWeapon(EHCJWeapon::Phaser)));
	TestTrue(TEXT("still the phaser"), State.Weapon == EHCJWeapon::Phaser);

	const FHCJWeaponDef& Rail = Gameplay.GetWeapon(EHCJWeapon::Rail);
	State.Give(EHCJWeapon::Rail, Rail);
	TestTrue(TEXT("a pickup weapon takes the place of the phaser"), State.Weapon == EHCJWeapon::Rail && State.Ammo == Rail.Ammo);
	bool bFellBack = false;
	for (int32 Shot = 0; Shot < Rail.Ammo; ++Shot)
	{
		bFellBack = State.ConsumeShot(Rail);
	}
	TestTrue(TEXT("the last shot reports the fall back"), bFellBack);
	TestTrue(TEXT("an emptied pickup weapon falls back to the phaser"), State.Weapon == EHCJWeapon::Phaser && State.Ammo == 0);

	State.Give(EHCJWeapon::Scatter, Gameplay.GetWeapon(EHCJWeapon::Scatter));
	State.Drop();
	TestTrue(TEXT("a dropped pickup weapon leaves the phaser"), State.Weapon == EHCJWeapon::Phaser);
	State.Give(EHCJWeapon::Phaser, Gameplay.GetWeapon(EHCJWeapon::Phaser));
	TestTrue(TEXT("the phaser cannot be picked up as ammunition"), State.Weapon == EHCJWeapon::Phaser && State.Ammo == 0);

	// B: phaser <-> blaster. Holding a base weapon it changes at once; holding a pickup only what it will fall back to.
	TestTrue(TEXT("switch to the blaster"), State.ToggleBase(true) == EHCJWeapon::Blaster && State.Weapon == EHCJWeapon::Blaster);
	TestTrue(TEXT("and back"), State.ToggleBase(true) == EHCJWeapon::Phaser && State.Weapon == EHCJWeapon::Phaser);
	State.Give(EHCJWeapon::Pulse, Gameplay.GetWeapon(EHCJWeapon::Pulse));
	const int32 PulseAmmo = State.Ammo;
	TestTrue(TEXT("with a pickup weapon the switch changes the fall back only"), State.ToggleBase(true) == EHCJWeapon::Blaster && State.Weapon == EHCJWeapon::Pulse && State.Ammo == PulseAmmo);
	State.Drop();
	TestTrue(TEXT("which is then the blaster"), State.Weapon == EHCJWeapon::Blaster);
	// Without phasers (-HCJNoPhasers) the switch never leaves the blaster.
	TestTrue(TEXT("no phaser allowed: stays the blaster"), State.ToggleBase(false) == EHCJWeapon::Blaster && State.Weapon == EHCJWeapon::Blaster);

	// The old default (a state nobody set up) still is the blaster.
	FHCJWeaponState Plain;
	Plain.Give(EHCJWeapon::Rail, Rail);
	Plain.Drop();
	TestTrue(TEXT("a plain state falls back to the blaster"), Plain.Weapon == EHCJWeapon::Blaster);

	// Bots: three levels of phaser discipline, better with the level.
	for (int32 Level = 0; Level < 2; ++Level)
	{
		const FHCJBotSkill Low = FHCJBotSkill::Get(Level);
		const FHCJBotSkill High = FHCJBotSkill::Get(Level + 1);
		HCJ_TEST_CHECK(High.PhaserConeDeg < Low.PhaserConeDeg && High.PhaserReserve >= Low.PhaserReserve, TEXT("level %d -> %d: cone %.1f -> %.1f, reserve %.2f -> %.2f"), Level, Level + 1,
			Low.PhaserConeDeg, High.PhaserConeDeg, Low.PhaserReserve, High.PhaserReserve);
	}
	// The two new hints have a rule and are not urgent.
	for (const EHCJHint Hint : { EHCJHint::PhaserEmpty, EHCJHint::RockBreak })
	{
		const FHCJHintRule& Rule = FHCJHintDirector::GetRule(Hint);
		HCJ_TEST_CHECK(Rule.MaxShows >= 1 && Rule.Duration >= 4.0f && Rule.Priority < FHCJHintDirector::UrgentPriority, TEXT("hint %d: %d shows, %.1f s, priority %d"),
			static_cast<int32>(Hint), Rule.MaxShows, Rule.Duration, Rule.Priority);
	}
	HCJ_TEST_FINISH();
}

/** Breaking rocks: which split, into what, the same on every machine; pieces lie inside the rock they come from; health by size. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJAsteroidSplitTest, "HypeCubeJam.Asteroids.SplitDeterminism", HCJPhaserTestFlags)

bool FHCJAsteroidSplitTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const FHCJAsteroidRules Rules = MakeRules();

	// Node ids.
	{
		const uint16 Base = HCJAsteroids::MakeId(321);
		const uint16 Child = HCJAsteroids::MakeChildId(Base, 2);
		const uint16 Grandchild = HCJAsteroids::MakeChildId(Child, 3);
		TestEqual(TEXT("generation of a layout rock"), HCJAsteroids::GetGeneration(Base), 0);
		TestEqual(TEXT("generation of a piece"), HCJAsteroids::GetGeneration(Child), 1);
		TestEqual(TEXT("generation of a piece of a piece"), HCJAsteroids::GetGeneration(Grandchild), 2);
		TestTrue(TEXT("all three keep the layout index"), HCJAsteroids::GetBaseIndex(Child) == 321 && HCJAsteroids::GetBaseIndex(Grandchild) == 321);
		TestTrue(TEXT("parents"), HCJAsteroids::GetParentId(Grandchild) == Child && HCJAsteroids::GetParentId(Child) == Base);
		TestTrue(TEXT("slots"), HCJAsteroids::GetSlot(Grandchild, 1) == 2 && HCJAsteroids::GetSlot(Grandchild, 2) == 3);
		TestFalse(TEXT("a second slot without a first is no id"), HCJAsteroids::IsValidId(static_cast<uint16>(321 | (1 << 14))));
		const uint32 Key = HCJAsteroids::PackKey(13, Grandchild);
		TestTrue(TEXT("key round trip"), HCJAsteroids::GetKeySector(Key) == 13 && HCJAsteroids::GetKeyId(Key) == Grandchild);
	}

	// Size classes and health.
	TestFalse(TEXT("a small rock is simply destroyed"), HCJAsteroids::CanSplit(Rules.SplitMinRadius - 1.0f, 0, Rules));
	TestTrue(TEXT("a rock of the split radius breaks"), HCJAsteroids::CanSplit(Rules.SplitMinRadius, 0, Rules));
	TestTrue(TEXT("a big piece breaks once more"), HCJAsteroids::CanSplit(1500.0f, 1, Rules));
	TestFalse(TEXT("but not a third time"), HCJAsteroids::CanSplit(1500.0f, HCJAsteroids::MaxGeneration, Rules));
	{
		const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
		FHCJAsteroidRules Configured = Rules;
		Configured.SplitMinRadius = Gameplay.AsteroidSplitMinRadius;
		Configured.HealthPerRadius = Gameplay.AsteroidHealthPerRadius;
		Configured.MinHealth = Gameplay.AsteroidMinHealth;
		Configured.MaxHealth = Gameplay.AsteroidMaxHealth;
		float LastHealth = 0.0f;
		for (float Radius = 50.0f; Radius <= 4000.0f; Radius += 50.0f)
		{
			const float Health = HCJAsteroids::ComputeHealth(Radius, Configured);
			HCJ_TEST_CHECK(Health >= LastHealth && Health >= Configured.MinHealth && Health <= Configured.MaxHealth, TEXT("radius %.0f: health %.1f after %.1f"), Radius, Health, LastHealth);
			LastHealth = Health;
		}
		// The design: a torpedo breaks a medium rock (4 m), the phaser needs a couple of seconds, a giant takes real effort.
		const float Medium = HCJAsteroids::ComputeHealth(400.0f, Configured);
		const float Torpedo = Gameplay.GetWeapon(EHCJWeapon::Torpedo).Damage * Gameplay.TorpedoRockDamageScale;
		const float PhaserDps = Gameplay.GetWeapon(EHCJWeapon::Phaser).Damage * Gameplay.PhaserRockDamageScale;
		HCJ_TEST_CHECK(Torpedo >= Medium, TEXT("a torpedo does %.0f to rock, a 4 m rock has %.0f"), Torpedo, Medium);
		HCJ_TEST_CHECK(Medium / PhaserDps >= 1.0f && Medium / PhaserDps <= 3.5f, TEXT("the phaser needs %.1f s for a 4 m rock"), Medium / PhaserDps);
		const float Giant = HCJAsteroids::ComputeHealth(3000.0f, Configured);
		HCJ_TEST_CHECK(Giant / PhaserDps > 2.0f * Gameplay.PhaserDrainSeconds, TEXT("a 30 m giant falls to %.1f s of phaser: less than two banks"), Giant / PhaserDps);
	}

	int32 NumSplit = 0;
	int32 NumNodes = 0;
	for (int32 SeedIndex = 0; SeedIndex < 6; ++SeedIndex)
	{
		const int32 Seed = HCJTest::MakeSeed(SeedIndex);
		FHCJHypercubeGraph Graph;
		Graph.Build(4, Seed, 0.5f);
		for (int32 Sector = 0; Sector < Graph.NumSectors(); Sector += 3)
		{
			const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);
			HCJ_TEST_CHECK(Layout.Asteroids.Num() <= HCJAsteroids::MaxBaseIndex, TEXT("sector %d has %d rocks: more than a node id can name"), Sector, Layout.Asteroids.Num());
			for (int32 Index = 0; Index < Layout.Asteroids.Num(); ++Index)
			{
				const FHCJAsteroid& Rock = Layout.Asteroids[Index];
				const uint16 Id = HCJAsteroids::MakeId(Index);
				TArray<FHCJAsteroid> Children;
				TArray<FHCJAsteroid> Again;
				HCJAsteroids::MakeChildren(Layout.Seed, Id, Rock, Rules, Children);
				HCJAsteroids::MakeChildren(Layout.Seed, Id, Rock, Rules, Again);
				++NumNodes;
				const bool bSplits = HCJAsteroids::CanSplit(Rock.BoundingRadius, 0, Rules);
				HCJ_TEST_CHECK((Children.Num() > 0) == bSplits, TEXT("rock %d (radius %.0f) has %d pieces"), Index, Rock.BoundingRadius, Children.Num());
				if (!bSplits)
				{
					continue;
				}
				++NumSplit;
				HCJ_TEST_CHECK(Children.Num() >= 2 && Children.Num() <= HCJAsteroids::MaxChildren && Children.Num() == Again.Num(), TEXT("rock %d: %d / %d pieces"), Index, Children.Num(), Again.Num());
				for (int32 Slot = 0; Slot < Children.Num() && Slot < Again.Num(); ++Slot)
				{
					const FHCJAsteroid& Child = Children[Slot];
					HCJ_TEST_CHECK(SameBits(Child, Again[Slot]), TEXT("rock %d piece %d differs between two calls"), Index, Slot + 1);
					HCJ_TEST_CHECK(Child.Shape == Rock.Shape, TEXT("rock %d piece %d changed shape"), Index, Slot + 1);
					HCJ_TEST_CHECK(Child.BoundingRadius >= Rules.ChildScaleMin * Rock.BoundingRadius - 0.5f && Child.BoundingRadius <= Rules.ChildScaleMax * Rock.BoundingRadius + 0.5f,
						TEXT("rock %d piece %d: radius %.1f of %.1f"), Index, Slot + 1, Child.BoundingRadius, Rock.BoundingRadius);
					// Inside the rock it comes from, so every clearance around the rock holds for the piece.
					const double Reach = FVector::Dist(Child.LocalPosition, Rock.LocalPosition) + Child.BoundingRadius;
					HCJ_TEST_CHECK(Reach <= Rock.BoundingRadius + HCJTest::DistanceTolerance, TEXT("rock %d piece %d reaches %.1f, the rock %.1f"), Index, Slot + 1, Reach, Rock.BoundingRadius);
					HCJ_TEST_CHECK(Child.LocalPosition.Size() + Child.BoundingRadius <= HCJ::SectorFieldRadius + HCJTest::DistanceTolerance, TEXT("rock %d piece %d leaves the field"), Index, Slot + 1);
					HCJ_TEST_CHECK(FMath::IsNearlyEqual(Child.BoundingRadius, FHCJAsteroid::ComputeBoundingRadius(Child.Shape, Child.Scale3D), 0.01f), TEXT("rock %d piece %d: stale radius"), Index, Slot + 1);

					// The piece by its id, and its own pieces.
					const uint16 ChildId = HCJAsteroids::MakeChildId(Id, Slot + 1);
					FHCJAsteroid Resolved;
					HCJ_TEST_CHECK(HCJAsteroids::ResolveNode(Layout, ChildId, Rules, Resolved) && SameBits(Resolved, Child), TEXT("rock %d piece %d does not resolve to itself"), Index, Slot + 1);
					TArray<FHCJAsteroid> Grandchildren;
					HCJAsteroids::MakeChildren(Layout.Seed, ChildId, Child, Rules, Grandchildren);
					HCJ_TEST_CHECK((Grandchildren.Num() > 0) == HCJAsteroids::CanSplit(Child.BoundingRadius, 1, Rules), TEXT("rock %d piece %d (radius %.0f) has %d pieces"), Index, Slot + 1,
						Child.BoundingRadius, Grandchildren.Num());
					for (int32 Sub = 0; Sub < Grandchildren.Num(); ++Sub)
					{
						const double SubReach = FVector::Dist(Grandchildren[Sub].LocalPosition, Rock.LocalPosition) + Grandchildren[Sub].BoundingRadius;
						HCJ_TEST_CHECK(SubReach <= Rock.BoundingRadius + HCJTest::DistanceTolerance, TEXT("rock %d piece %d.%d reaches %.1f, the rock %.1f"), Index, Slot + 1, Sub + 1, SubReach, Rock.BoundingRadius);
						TArray<FHCJAsteroid> None;
						HCJAsteroids::MakeChildren(Layout.Seed, HCJAsteroids::MakeChildId(ChildId, Sub + 1), Grandchildren[Sub], Rules, None);
						HCJ_TEST_CHECK(None.Num() == 0, TEXT("rock %d piece %d.%d breaks a third time"), Index, Slot + 1, Sub + 1);
					}
				}
				// A slot the rock does not have names nothing.
				FHCJAsteroid Nothing;
				HCJ_TEST_CHECK(Children.Num() == HCJAsteroids::MaxChildren || !HCJAsteroids::ResolveNode(Layout, HCJAsteroids::MakeChildId(Id, 3), Rules, Nothing), TEXT("rock %d: slot 3 resolves although it has %d pieces"), Index, Children.Num());
			}
			FHCJAsteroid Nothing;
			HCJ_TEST_CHECK(!HCJAsteroids::ResolveNode(Layout, HCJAsteroids::MakeId(Layout.Asteroids.Num()), Rules, Nothing), TEXT("sector %d: an index past the end resolves"), Sector);
		}
	}
	HCJ_TEST_CHECK(NumSplit > 100 && NumSplit < NumNodes, TEXT("%d of %d rocks break into pieces"), NumSplit, NumNodes);

	// Other rules, other pieces: the rules are part of the match config for a reason.
	{
		FHCJHypercubeGraph Graph;
		Graph.Build(4, 1234, 0.5f);
		const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(1234, 5, Graph);
		FHCJAsteroidRules Off = Rules;
		Off.SplitMinRadius = 100000.0f;
		int32 NumBroken = 0;
		for (int32 Index = 0; Index < Layout.Asteroids.Num(); ++Index)
		{
			TArray<FHCJAsteroid> Children;
			HCJAsteroids::MakeChildren(Layout.Seed, HCJAsteroids::MakeId(Index), Layout.Asteroids[Index], Off, Children);
			NumBroken += Children.Num();
		}
		TestEqual(TEXT("with a huge split radius nothing breaks"), NumBroken, 0);
	}
	HCJ_TEST_FINISH();
}

/**
 * The replicated destroyed set: a client that gets the keys in another order, twice, or all at once after joining
 * late ends with the same rocks as the server, and the checksum says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJAsteroidReplicationTest, "HypeCubeJam.Asteroids.ReplicationRoundTrip", HCJPhaserTestFlags)

bool FHCJAsteroidReplicationTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const FHCJAsteroidRules Rules = MakeRules();

	TestTrue(TEXT("nothing destroyed: checksum 0"), HCJAsteroids::ComputeDestroyedChecksum(TConstArrayView<uint32>()) == 0);

	for (int32 SeedIndex = 0; SeedIndex < 4; ++SeedIndex)
	{
		const int32 Seed = SeedIndex == 0 ? 1234 : HCJTest::MakeSeed(SeedIndex + 4);
		FHCJHypercubeGraph Graph;
		Graph.Build(4, Seed, 0.5f);
		const int32 Sector = 3 + SeedIndex;
		const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);

		// The server: a match's worth of destruction, always of a rock that exists at that moment (pieces included).
		FFieldSim Server;
		Server.Init(Layout, Rules);
		const int32 NumAtStart = Server.Alive.Num();
		FRandomStream Random(HCJ::MixSeed(Seed, 77));
		TArray<uint32> Replicated;
		for (int32 Step = 0; Step < 160 && Server.Alive.Num() > 0; ++Step)
		{
			const TArray<uint32> Keys = Server.SortedAlive();
			// Half the time a rock that is big enough to break, when there is one.
			uint32 Key = Keys[Random.RandRange(0, Keys.Num() - 1)];
			if (Random.FRand() < 0.5f)
			{
				for (int32 Try = 0; Try < 20; ++Try)
				{
					const uint32 Candidate = Keys[Random.RandRange(0, Keys.Num() - 1)];
					if (HCJAsteroids::CanSplit(Server.Alive[Candidate].BoundingRadius, HCJAsteroids::GetGeneration(HCJAsteroids::GetKeyId(Candidate)), Rules))
					{
						Key = Candidate;
						break;
					}
				}
			}
			HCJ_TEST_CHECK(Server.Destroyed.IsAlive(Key), TEXT("seed %d: the server picked a rock the set calls gone (%08X)"), Seed, Key);
			HCJ_TEST_CHECK(Server.Apply(Key), TEXT("seed %d: key %08X applied twice"), Seed, Key);
			HCJ_TEST_CHECK(!Server.Destroyed.IsAlive(Key), TEXT("seed %d: key %08X still alive"), Seed, Key);
			Replicated.Add(Key);
		}
		const TArray<uint32> ServerAlive = Server.SortedAlive();
		const uint32 ServerChecksum = Server.Destroyed.ComputeChecksum();
		HCJ_TEST_CHECK(ServerChecksum == HCJAsteroids::ComputeDestroyedChecksum(Replicated) && ServerChecksum != 0, TEXT("seed %d: checksum of the list %08X, of the set %08X"), Seed,
			HCJAsteroids::ComputeDestroyedChecksum(Replicated), ServerChecksum);
		HCJ_TEST_CHECK(ServerAlive.Num() != NumAtStart, TEXT("seed %d: the field did not change"), Seed);

		// What the set alone says exists must be what the server built step by step.
		{
			TArray<uint32> FromSet;
			TArray<FHCJAsteroid> Children;
			for (int32 Index = 0; Index < Layout.Asteroids.Num(); ++Index)
			{
				TArray<TPair<uint16, FHCJAsteroid>> Pending;
				Pending.Emplace(HCJAsteroids::MakeId(Index), Layout.Asteroids[Index]);
				while (Pending.Num() > 0)
				{
					const TPair<uint16, FHCJAsteroid> Node = Pending.Pop();
					const uint32 Key = HCJAsteroids::PackKey(Sector, Node.Key);
					FHCJAsteroid Resolved;
					HCJ_TEST_CHECK(HCJAsteroids::ResolveNode(Layout, Node.Key, Rules, Resolved) && SameBits(Resolved, Node.Value), TEXT("seed %d: node %04X does not resolve"), Seed, Node.Key);
					if (Server.Destroyed.IsAlive(Key))
					{
						FromSet.Add(Key);
					}
					HCJAsteroids::MakeChildren(Layout.Seed, Node.Key, Node.Value, Rules, Children);
					for (int32 Slot = 1; Slot <= Children.Num(); ++Slot)
					{
						Pending.Emplace(HCJAsteroids::MakeChildId(Node.Key, Slot), Children[Slot - 1]);
					}
				}
			}
			FromSet.Sort();
			HCJ_TEST_CHECK(FromSet == ServerAlive, TEXT("seed %d: the set says %d rocks exist, the server has %d"), Seed, FromSet.Num(), ServerAlive.Num());
		}

		// Clients: in order; all at once (late join); in reverse (a piece before its parent); shuffled with duplicates.
		for (int32 Variant = 0; Variant < 4; ++Variant)
		{
			TArray<uint32> Order = Replicated;
			if (Variant == 2)
			{
				Algo::Reverse(Order);
			}
			else if (Variant == 3)
			{
				for (int32 Index = Order.Num() - 1; Index > 0; --Index)
				{
					Order.Swap(Index, Random.RandRange(0, Index));
				}
				Order.Append(Replicated.GetData(), Replicated.Num() / 3);
			}
			FFieldSim Client;
			Client.Init(Layout, Rules);
			int32 NumApplied = 0;
			for (const uint32 Key : Order)
			{
				NumApplied += Client.Apply(Key) ? 1 : 0;
			}
			HCJ_TEST_CHECK(NumApplied == Replicated.Num(), TEXT("seed %d variant %d: %d keys applied of %d"), Seed, Variant, NumApplied, Replicated.Num());
			HCJ_TEST_CHECK(Client.Destroyed.ComputeChecksum() == ServerChecksum, TEXT("seed %d variant %d: checksum %08X, server %08X"), Seed, Variant, Client.Destroyed.ComputeChecksum(), ServerChecksum);
			const TArray<uint32> ClientAlive = Client.SortedAlive();
			HCJ_TEST_CHECK(ClientAlive == ServerAlive, TEXT("seed %d variant %d: the client has %d rocks, the server %d"), Seed, Variant, ClientAlive.Num(), ServerAlive.Num());
			for (const uint32 Key : ServerAlive)
			{
				const FHCJAsteroid* Mine = Client.Alive.Find(Key);
				HCJ_TEST_CHECK(Mine != nullptr && SameBits(*Mine, Server.Alive[Key]), TEXT("seed %d variant %d: rock %08X differs from the server's"), Seed, Variant, Key);
			}
		}

		// One key more or less is another checksum.
		TArray<uint32> Fewer = Replicated;
		Fewer.Pop();
		HCJ_TEST_CHECK(HCJAsteroids::ComputeDestroyedChecksum(Fewer) != ServerChecksum, TEXT("seed %d: the checksum does not notice a missing rock"), Seed);
	}

	// What goes over the wire: 4 bytes per rock, and a match stays far below the cap.
	TestEqual(TEXT("bytes per destroyed rock"), static_cast<int32>(sizeof(uint32)), 4);
	TestTrue(TEXT("the cap fits the replicated list"), HCJAsteroids::MaxDestroyedPerMatch <= 2000);
	HCJ_TEST_FINISH();
}

/** The shockwave of an explosion: strongest at the centre, nothing at the edge, always away from the blast. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShockwaveTest, "HypeCubeJam.Effects.ShockwaveFalloff", HCJPhaserTestFlags)

bool FHCJShockwaveTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	constexpr float Radius = 1600.0f;
	constexpr float Peak = 1300.0f;

	TestEqual(TEXT("at the centre: the peak"), HCJShockwave::ImpulseSpeed(0.0f, Radius, Peak), Peak);
	TestEqual(TEXT("at the edge: nothing"), HCJShockwave::ImpulseSpeed(Radius, Radius, Peak), 0.0f);
	TestEqual(TEXT("beyond: nothing"), HCJShockwave::ImpulseSpeed(Radius * 3.0f, Radius, Peak), 0.0f);
	TestTrue(TEXT("half way: a quarter"), FMath::IsNearlyEqual(HCJShockwave::ImpulseSpeed(Radius * 0.5f, Radius, Peak), Peak * 0.25f, 0.01f));
	TestEqual(TEXT("no radius: nothing"), HCJShockwave::ImpulseSpeed(10.0f, 0.0f, Peak), 0.0f);
	TestEqual(TEXT("no push: nothing"), HCJShockwave::ImpulseSpeed(10.0f, Radius, 0.0f), 0.0f);
	float Last = Peak;
	for (float Distance = 0.0f; Distance <= Radius; Distance += 40.0f)
	{
		const float Speed = HCJShockwave::ImpulseSpeed(Distance, Radius, Peak);
		HCJ_TEST_CHECK(Speed <= Last + KINDA_SMALL_NUMBER && Speed >= 0.0f, TEXT("at %.0f uu the push is %.1f after %.1f"), Distance, Speed, Last);
		Last = Speed;
	}

	const FVector Origin(500000.0, -1000.0, 250.0);
	FRandomStream Random(4242);
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const FVector Direction = Random.VRand();
		const float Distance = Random.FRandRange(5.0f, Radius * 1.2f);
		const FVector Impulse = HCJShockwave::ComputeImpulse(Origin, Origin + Direction * Distance, Radius, Peak);
		const float Expected = HCJShockwave::ImpulseSpeed(Distance, Radius, Peak);
		HCJ_TEST_CHECK(FMath::IsNearlyEqual(static_cast<float>(Impulse.Size()), Expected, 0.5f), TEXT("at %.0f uu the impulse is %.1f, expected %.1f"), Distance, Impulse.Size(), Expected);
		HCJ_TEST_CHECK(Expected <= 0.0f || FVector::DotProduct(Impulse.GetSafeNormal(), Direction) > 0.999, TEXT("at %.0f uu the impulse does not point away from the blast"), Distance);
	}
	TestTrue(TEXT("exactly on the blast: still a push of the peak"), FMath::IsNearlyEqual(static_cast<float>(HCJShockwave::ComputeImpulse(Origin, Origin, Radius, Peak).Size()), Peak, 0.01f));

	// The configured pushes: felt, but never enough to throw a ship out of its sector or to outrun a boost for long.
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	HCJ_TEST_CHECK(Gameplay.TorpedoShockwaveRadius > Gameplay.TorpedoBlastRadius && Gameplay.TorpedoShockwaveSpeed <= 3000.0f, TEXT("torpedo shockwave %.0f uu / %.0f uu/s"),
		Gameplay.TorpedoShockwaveRadius, Gameplay.TorpedoShockwaveSpeed);
	HCJ_TEST_CHECK(Gameplay.ShipShockwaveSpeed <= Gameplay.TorpedoShockwaveSpeed && Gameplay.ShipShockwaveRadius <= 3000.0f, TEXT("ship shockwave %.0f uu / %.0f uu/s"),
		Gameplay.ShipShockwaveRadius, Gameplay.ShipShockwaveSpeed);
	HCJ_TEST_FINISH();
}

/** Debris: never more pieces than slots, the oldest goes first, pieces move, fade and end; shake and counts scale within limits. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJDebrisCapsTest, "HypeCubeJam.Effects.DebrisCaps", HCJPhaserTestFlags)

bool FHCJDebrisCapsTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	FHCJDebrisPool Pool;
	FHCJDebrisPiece Piece;
	Piece.Life = 3.0f;
	Piece.Velocity = FVector(1000.0, 0.0, 0.0);
	TestEqual(TEXT("no slots: nothing is spawned"), Pool.Spawn(Piece), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and nothing is active"), Pool.NumActive(), 0);

	constexpr int32 Capacity = 24;
	Pool.SetCapacity(Capacity);
	for (int32 Index = 0; Index < Capacity; ++Index)
	{
		Piece.Life = 1.0f + 0.1f * Index;
		const int32 Slot = Pool.Spawn(Piece);
		HCJ_TEST_CHECK(Slot == Index, TEXT("piece %d went to slot %d"), Index, Slot);
	}
	TestEqual(TEXT("full"), Pool.NumActive(), Capacity);

	// Ten explosions' worth on top: the count stays at the cap, and each new piece takes the slot of the one nearest its end.
	Pool.Step(0.5f, 0.0f);
	for (int32 Index = 0; Index < 240; ++Index)
	{
		float LeastLeft = TNumericLimits<float>::Max();
		int32 Expected = INDEX_NONE;
		for (int32 Slot = 0; Slot < Capacity; ++Slot)
		{
			const FHCJDebrisPiece& Other = Pool.GetPieces()[Slot];
			if (Other.Life - Other.Age < LeastLeft)
			{
				LeastLeft = Other.Life - Other.Age;
				Expected = Slot;
			}
		}
		Piece.Life = 5.0f + 0.01f * Index;
		const int32 Slot = Pool.Spawn(Piece);
		HCJ_TEST_CHECK(Slot == Expected, TEXT("overflow piece %d replaced slot %d, the oldest is %d"), Index, Slot, Expected);
		HCJ_TEST_CHECK(Pool.NumActive() == Capacity, TEXT("after overflow piece %d: %d active of %d"), Index, Pool.NumActive(), Capacity);
	}

	// Motion: position from velocity, velocity damped, pieces end with their life and free their slot.
	FHCJDebrisPool Flight;
	Flight.SetCapacity(4);
	FHCJDebrisPiece Flyer;
	Flyer.Location = FVector(100.0, 0.0, 0.0);
	Flyer.Velocity = FVector(600.0, 0.0, 0.0);
	Flyer.AngularSpeed = 180.0f;
	Flyer.Life = 2.0f;
	Flight.Spawn(Flyer);
	Flight.Step(0.5f, 0.0f);
	TestTrue(TEXT("half a second at 600 uu/s"), FMath::IsNearlyEqual(Flight.GetPieces()[0].Location.X, 400.0, 0.01));
	TestTrue(TEXT("tumbled 90 degrees"), FMath::IsNearlyEqual(Flight.GetPieces()[0].Angle, 90.0f, 0.01f));
	Flight.Step(0.5f, 0.5f);
	TestTrue(TEXT("drag takes speed"), Flight.GetPieces()[0].Velocity.X < 600.0 && Flight.GetPieces()[0].Velocity.X > 0.0);
	Flight.ApplyShockwave(FVector(700.0, 0.0, 0.0) + FVector(400.0, 0.0, 0.0), 2000.0f, 1000.0f);
	TestTrue(TEXT("a blast ahead pushes it back"), Flight.GetPieces()[0].Velocity.X < 0.0);
	TestEqual(TEXT("one piece flying"), Flight.Step(0.5f, 0.0f), 1);
	TestEqual(TEXT("gone when its life is over"), Flight.Step(0.6f, 0.0f), 0);
	TestFalse(TEXT("its slot is free"), Flight.GetPieces()[0].bActive);
	TestEqual(TEXT("and is used again"), Flight.Spawn(Flyer), 0);

	// The fade: full size for two thirds of the life, then down to nothing.
	TestEqual(TEXT("young: full size"), FHCJDebrisPool::SizeFactor(0.5f, 3.0f), 1.0f);
	TestEqual(TEXT("at two thirds: still full"), FHCJDebrisPool::SizeFactor(2.0f, 3.0f), 1.0f);
	TestTrue(TEXT("five sixths: half"), FMath::IsNearlyEqual(FHCJDebrisPool::SizeFactor(2.5f, 3.0f), 0.5f, 0.001f));
	TestEqual(TEXT("at the end: nothing"), FHCJDebrisPool::SizeFactor(3.0f, 3.0f), 0.0f);

	// Camera shake by distance, piece counts by size.
	TestEqual(TEXT("next to the view: full shake"), HCJEffects::ShakeAt(300.0f, 7000.0f, 1.0f), 1.0f);
	TestEqual(TEXT("at the limit: none"), HCJEffects::ShakeAt(7000.0f, 7000.0f, 1.0f), 0.0f);
	TestEqual(TEXT("beyond: none"), HCJEffects::ShakeAt(20000.0f, 7000.0f, 1.0f), 0.0f);
	TestEqual(TEXT("switched off: none"), HCJEffects::ShakeAt(100.0f, 7000.0f, 0.0f), 0.0f);
	float LastShake = 1.0f;
	for (float Distance = 0.0f; Distance <= 7000.0f; Distance += 100.0f)
	{
		const float Shake = HCJEffects::ShakeAt(Distance, 7000.0f, 1.0f);
		HCJ_TEST_CHECK(Shake <= LastShake + KINDA_SMALL_NUMBER && Shake >= 0.0f, TEXT("shake %.3f at %.0f uu after %.3f"), Shake, Distance, LastShake);
		LastShake = Shake;
	}
	TestEqual(TEXT("a rock of the reference size"), HCJEffects::ScaleCount(9, 400.0f, 400.0f, 4, 18), 9);
	TestEqual(TEXT("a pebble: the minimum"), HCJEffects::ScaleCount(9, 50.0f, 400.0f, 4, 18), 4);
	TestEqual(TEXT("a giant: the maximum"), HCJEffects::ScaleCount(9, 3000.0f, 400.0f, 4, 18), 18);
	TestEqual(TEXT("switched off"), HCJEffects::ScaleCount(0, 400.0f, 400.0f, 4, 18), 0);
	HCJ_TEST_FINISH();
}

/** Music tracks: the menu wins, combat comes in with the fight and goes out only after its hold time, loops start before the outro. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJMusicTracksTest, "HypeCubeJam.Audio.MusicTracks", HCJPhaserTestFlags)

bool FHCJMusicTracksTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	constexpr float On = 0.75f;
	constexpr float Off = 0.3f;
	constexpr float Hold = 20.0f;
	constexpr float Step = 0.1f;

	FHCJMusicSelector Selector;
	TestTrue(TEXT("nothing chosen at first"), Selector.Current == EHCJMusicTrack::None);
	TestTrue(TEXT("title menu: the menu track"), Selector.Step(true, 0.0f, Step, On, Off, Hold) == EHCJMusicTrack::Menu);
	TestTrue(TEXT("the menu wins over a fight behind it"), Selector.Step(true, 1.0f, Step, On, Off, Hold) == EHCJMusicTrack::Menu);
	TestTrue(TEXT("into the match, calm: explore"), Selector.Step(false, 0.0f, Step, On, Off, Hold) == EHCJMusicTrack::Explore);
	TestTrue(TEXT("company nearby is not a fight"), Selector.Step(false, 0.45f, Step, On, Off, Hold) == EHCJMusicTrack::Explore);
	TestTrue(TEXT("a fight: combat"), Selector.Step(false, 1.0f, Step, On, Off, Hold) == EHCJMusicTrack::Combat);

	// The fight ends at once: the combat track still plays its hold time.
	float Seconds = 0.0f;
	while (Selector.Step(false, 0.0f, Step, On, Off, Hold) == EHCJMusicTrack::Combat && Seconds < 100.0f)
	{
		Seconds += Step;
	}
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(Seconds, Hold, 0.3f), TEXT("the combat track played %.1f s after the fight, hold time %.1f"), Seconds, Hold);

	// A fight that goes on keeps it; an intensity between the two thresholds changes nothing either way.
	Selector.Step(false, 1.0f, Step, On, Off, Hold);
	for (float Time = 0.0f; Time < 60.0f; Time += Step)
	{
		HCJ_TEST_CHECK(Selector.Step(false, 0.5f, Step, On, Off, Hold) == EHCJMusicTrack::Combat, TEXT("combat ended at intensity 0.5 after %.1f s"), Time);
	}
	TestTrue(TEXT("calm after a long fight: explore at once"), Selector.Step(false, 0.1f, Step, On, Off, Hold) == EHCJMusicTrack::Explore);
	for (float Time = 0.0f; Time < 5.0f; Time += Step)
	{
		HCJ_TEST_CHECK(Selector.Step(false, 0.5f, Step, On, Off, Hold) == EHCJMusicTrack::Explore, TEXT("combat started at intensity 0.5 after %.1f s"), Time);
	}
	TestTrue(TEXT("back to the menu from anywhere"), Selector.Step(true, 1.0f, Step, On, Off, Hold) == EHCJMusicTrack::Menu);

	// Never more than one switch in the hold time while the intensity jumps about.
	FHCJMusicSelector Jumpy;
	Jumpy.Step(false, 0.0f, Step, On, Off, Hold);
	FRandomStream Random(99);
	int32 Switches = 0;
	EHCJMusicTrack Last = Jumpy.Current;
	for (float Time = 0.0f; Time < 300.0f; Time += Step)
	{
		const EHCJMusicTrack Now = Jumpy.Step(false, Random.FRand(), Step, On, Off, Hold);
		Switches += Now != Last ? 1 : 0;
		Last = Now;
	}
	HCJ_TEST_CHECK(Switches <= 2 * FMath::CeilToInt32(300.0f / Hold) + 2, TEXT("%d switches in 300 s"), Switches);

	// Loop points: where the outro begins when given, else the end minus the crossfade; fades take their time.
	TestEqual(TEXT("given loop point"), HCJMusic::GetLoopStart(168.6f, 157.0f, 4.0f), 157.0f);
	TestTrue(TEXT("no loop point: the end minus the crossfade"), FMath::IsNearlyEqual(HCJMusic::GetLoopStart(120.0f, 0.0f, 3.0f), 117.0f, 0.001f));
	TestTrue(TEXT("a loop point past the end is ignored"), FMath::IsNearlyEqual(HCJMusic::GetLoopStart(120.0f, 500.0f, 3.0f), 117.0f, 0.001f));
	TestTrue(TEXT("a file shorter than its crossfade still plays"), HCJMusic::GetLoopStart(1.0f, 0.0f, 4.0f) >= 2.0f);
	float Weight = 0.0f;
	float FadeTime = 0.0f;
	while (Weight < 1.0f && FadeTime < 60.0f)
	{
		Weight = HCJMusic::StepWeight(Weight, 1.0f, Step, 2.5f);
		FadeTime += Step;
	}
	HCJ_TEST_CHECK(FMath::IsNearlyEqual(FadeTime, 2.5f, 0.15f), TEXT("a fade of 2.5 s took %.2f s"), FadeTime);
	TestEqual(TEXT("no fade time: at once"), HCJMusic::StepWeight(0.0f, 1.0f, Step, 0.0f), 1.0f);
	TestTrue(TEXT("gain 0.3 is about -10.5 dB"), FMath::IsNearlyEqual(HCJMusic::GainToDecibels(0.3f), -10.46f, 0.05f));
	TestEqual(TEXT("silence"), HCJMusic::GainToDecibels(0.0f), -120.0f);
	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
