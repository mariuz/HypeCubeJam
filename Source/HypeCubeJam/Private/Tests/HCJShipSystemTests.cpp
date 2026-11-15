#include "Tests/HCJTestHelpers.h"

#include "Game/HCJBalance.h"
#include "Ship/HCJShipSystems.h"
#include "Ship/HCJShipTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJSystemTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Steps a drive for Seconds in steps of Step; returns the number of steps flown at warp. */
	int32 RunDrive(FHCJWarpDrive& Drive, bool bWants, bool bBlocked, float Seconds, const FHCJWarpParams& Params, float Step = 1.0f / 60.0f)
	{
		int32 NumActive = 0;
		for (float Time = 0.0f; Time < Seconds; Time += Step)
		{
			NumActive += Drive.Step(bWants, bBlocked, Step, Params) ? 1 : 0;
		}
		return NumActive;
	}
}

/** Warp drive: charge, jump, longest jump, cooldown, the key must be let go, what cancels it. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJWarpStateMachineTest, "HypeCubeJam.Systems.WarpStateMachine", HCJSystemTestFlags)

bool FHCJWarpStateMachineTest::RunTest(const FString& Parameters)
{
	FHCJWarpParams Params;
	Params.ChargeSeconds = 1.6f;
	Params.MaxSeconds = 2.5f;
	Params.Cooldown = 12.0f;
	Params.AbortCooldown = 2.0f;
	const float Step = 1.0f / 60.0f;

	{
		FHCJWarpDrive Drive;
		TestEqual(TEXT("nothing without the key"), RunDrive(Drive, false, false, 1.0f, Params), 0);
		TestTrue(TEXT("idle"), Drive.Phase == EHCJWarpPhase::Idle);

		// The key held: charging, no warp before the charge is complete.
		TestEqual(TEXT("no warp while charging"), RunDrive(Drive, true, false, 1.5f, Params), 0);
		TestTrue(TEXT("charging"), Drive.Phase == EHCJWarpPhase::Charging && Drive.IsBusy());
		TestTrue(TEXT("charge fraction grows"), Drive.GetChargeFraction(Params) > 0.85f && Drive.GetChargeFraction(Params) <= 1.0f);

		// Then the jump: it starts once, lasts the longest time and no longer.
		int32 NumEntered = 0;
		int32 NumActive = 0;
		for (int32 Index = 0; Index < 600 && Drive.Phase != EHCJWarpPhase::Cooldown; ++Index)
		{
			NumActive += Drive.Step(true, false, Step, Params) ? 1 : 0;
			NumEntered += Drive.bJustEntered ? 1 : 0;
		}
		TestEqual(TEXT("the jump starts once"), NumEntered, 1);
		TestTrue(TEXT("the jump lasts about the longest time"), FMath::Abs(NumActive * Step - Params.MaxSeconds) < 0.06f);
		TestTrue(TEXT("the step that ended it says so"), Drive.bJustExited && Drive.Phase == EHCJWarpPhase::Cooldown);
		TestTrue(TEXT("full cooldown"), FMath::IsNearlyEqual(Drive.Time, Params.Cooldown, 0.01f) && FMath::IsNearlyEqual(Drive.GetCooldownFraction(Params), 1.0f, 0.01f));

		// Cooling down with the key still held: nothing, and afterwards the key must be let go first.
		TestEqual(TEXT("no warp in the cooldown"), RunDrive(Drive, true, false, Params.Cooldown + 0.5f, Params), 0);
		TestTrue(TEXT("idle after the cooldown"), Drive.Phase == EHCJWarpPhase::Idle);
		TestEqual(TEXT("a held key does not chain warps"), RunDrive(Drive, true, false, 3.0f, Params), 0);
		TestTrue(TEXT("still idle"), Drive.Phase == EHCJWarpPhase::Idle);
		RunDrive(Drive, false, false, 0.1f, Params);
		RunDrive(Drive, true, false, 0.5f, Params);
		TestTrue(TEXT("let go and pressed again: charging"), Drive.Phase == EHCJWarpPhase::Charging);
	}
	{
		// Letting go during the charge: the short cooldown, no jump.
		FHCJWarpDrive Drive;
		RunDrive(Drive, true, false, 1.0f, Params);
		TestFalse(TEXT("let go: no warp"), Drive.Step(false, false, Step, Params));
		TestTrue(TEXT("short cooldown"), Drive.Phase == EHCJWarpPhase::Cooldown && Drive.Time <= Params.AbortCooldown + 0.01f && !Drive.bJustExited);
	}
	{
		// Letting go at warp drops out at once.
		FHCJWarpDrive Drive;
		RunDrive(Drive, true, false, Params.ChargeSeconds + 0.5f, Params);
		TestTrue(TEXT("at warp"), Drive.Phase == EHCJWarpPhase::Active);
		TestTrue(TEXT("what is left of the jump"), Drive.GetEnergyFraction(Params) > 0.7f && Drive.GetEnergyFraction(Params) < 0.9f);
		TestFalse(TEXT("let go: out"), Drive.Step(false, false, Step, Params));
		TestTrue(TEXT("cooling down, exit reported"), Drive.Phase == EHCJWarpPhase::Cooldown && Drive.bJustExited);
	}
	{
		// Blocked (held by a tractor beam, cloaked): no charge; a charge or a jump under way stops.
		FHCJWarpDrive Drive;
		TestEqual(TEXT("blocked: nothing"), RunDrive(Drive, true, true, 3.0f, Params), 0);
		TestTrue(TEXT("blocked: idle"), Drive.Phase == EHCJWarpPhase::Idle);
		RunDrive(Drive, true, false, 1.0f, Params);
		Drive.Step(true, true, Step, Params);
		TestTrue(TEXT("blocked while charging: stopped"), Drive.Phase == EHCJWarpPhase::Cooldown);

		FHCJWarpDrive Second;
		RunDrive(Second, true, false, Params.ChargeSeconds + 0.3f, Params);
		TestFalse(TEXT("blocked at warp: out"), Second.Step(true, true, Step, Params));
		TestTrue(TEXT("blocked at warp: cooling down"), Second.Phase == EHCJWarpPhase::Cooldown && Second.bJustExited);
	}
	{
		// Cancel (a hit while charging, a collision): cooldown of the given length; nothing to cancel when idle.
		FHCJWarpDrive Drive;
		Drive.Cancel(5.0f);
		TestTrue(TEXT("cancel when idle does nothing"), Drive.Phase == EHCJWarpPhase::Idle);
		RunDrive(Drive, true, false, 1.0f, Params);
		Drive.Cancel(2.0f);
		TestTrue(TEXT("cancelled charge"), Drive.Phase == EHCJWarpPhase::Cooldown && FMath::IsNearlyEqual(Drive.Time, 2.0f) && !Drive.bJustExited);
		FHCJWarpDrive Second;
		RunDrive(Second, true, false, Params.ChargeSeconds + 0.3f, Params);
		Second.Cancel(12.0f);
		TestTrue(TEXT("cancelled jump reports the exit"), Second.Phase == EHCJWarpPhase::Cooldown && Second.bJustExited);
	}

	// Dropping out at the edge of the playable sector: only beyond the radius and only heading outwards.
	TestFalse(TEXT("inside: no drop"), HCJWarp::ShouldDropOutAtEdge(FVector(10000.0, 0.0, 0.0), FVector::ForwardVector, 22500.0f));
	TestTrue(TEXT("outside, outwards: drop"), HCJWarp::ShouldDropOutAtEdge(FVector(23000.0, 0.0, 0.0), FVector::ForwardVector, 22500.0f));
	TestFalse(TEXT("outside, inwards: no drop"), HCJWarp::ShouldDropOutAtEdge(FVector(23000.0, 0.0, 0.0), -FVector::ForwardVector, 22500.0f));
	TestTrue(TEXT("a long way is worth a warp"), HCJWarp::IsWorthWarping(30000.0f, Params) && !HCJWarp::IsWorthWarping(5000.0f, Params));
	return true;
}

/** Warp over the network: the server's copy of the drive agrees with the client's about every move, and refuses a warp without charge. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJWarpFollowTest, "HypeCubeJam.Systems.WarpFollow", HCJSystemTestFlags)

bool FHCJWarpFollowTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	FHCJWarpParams Params;
	FRandomStream Random(4711);

	// An honest client at an uneven frame rate, the key held for three jumps with pauses. The server gets the same
	// moves with its own rounding of every delta time.
	{
		FHCJWarpDrive Client;
		FHCJWarpDrive Server;
		int32 NumClientActive = 0;
		int32 NumJumps = 0;
		float Time = 0.0f;
		while (Time < 60.0f)
		{
			const float Delta = Random.FRandRange(0.006f, 0.04f);
			Time += Delta;
			const bool bKey = FMath::Fmod(Time, 20.0f) < 6.0f;
			const bool bClientActive = Client.Step(bKey, false, Delta, Params);
			NumJumps += Client.bJustEntered ? 1 : 0;
			const bool bServerActive = Server.StepFollow(bKey, bClientActive, false, Delta * Random.FRandRange(0.997f, 1.003f), Params);
			NumClientActive += bClientActive ? 1 : 0;
			HCJ_TEST_CHECK(bServerActive == bClientActive, TEXT("t=%.2f: client %d, server %d (client phase %d, server phase %d)"), Time, bClientActive ? 1 : 0,
				bServerActive ? 1 : 0, static_cast<int32>(Client.Phase), static_cast<int32>(Server.Phase));
			HCJ_TEST_CHECK(!Client.bJustEntered || Server.bJustEntered, TEXT("t=%.2f: the jump started on the client but not on the server"), Time);
		}
		TestEqual(TEXT("three jumps in a minute"), NumJumps, 3);
		TestTrue(TEXT("the client did warp"), NumClientActive > 100);
	}
	{
		// A client that claims warp without charge, or during the cooldown, is refused.
		FHCJWarpDrive Server;
		int32 NumGranted = 0;
		for (int32 Index = 0; Index < 30; ++Index)
		{
			NumGranted += Server.StepFollow(true, true, false, 1.0f / 60.0f, Params) ? 1 : 0;
		}
		TestEqual(TEXT("no warp without charge"), NumGranted, 0);

		// With charge it may run a little over the longest jump, then it is cut off and cools down.
		for (int32 Index = 0; Index < 90; ++Index)
		{
			Server.StepFollow(true, false, false, 1.0f / 60.0f, Params);
		}
		NumGranted = 0;
		for (int32 Index = 0; Index < 600; ++Index)
		{
			NumGranted += Server.StepFollow(true, true, false, 1.0f / 60.0f, Params) ? 1 : 0;
		}
		TestTrue(TEXT("a claimed jump is cut at the longest time plus the tolerance"), NumGranted / 60.0f <= Params.MaxSeconds + 0.3f && NumGranted / 60.0f >= Params.MaxSeconds);
		TestTrue(TEXT("cooling down afterwards"), Server.Phase == EHCJWarpPhase::Cooldown);
	}
	{
		// Blocked on the server (a tractor beam the client has not heard of yet): refused although the charge is complete.
		FHCJWarpDrive Server;
		for (int32 Index = 0; Index < 120; ++Index)
		{
			Server.StepFollow(true, false, false, 1.0f / 60.0f, Params);
		}
		TestFalse(TEXT("blocked: refused"), Server.StepFollow(true, true, true, 1.0f / 60.0f, Params));
		TestTrue(TEXT("blocked: the charge is gone"), Server.Phase == EHCJWarpPhase::Cooldown);
	}
	return NumFailures == 0;
}

/** Boost over the network: the server follows the client's meter move by move, and still limits a client that claims boost without end. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJBoostFollowTest, "HypeCubeJam.Systems.BoostFollow", HCJSystemTestFlags)

bool FHCJBoostFollowTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const float Drain = 3.5f;
	const float Recharge = 7.0f;
	const float Relock = 0.25f;
	FRandomStream Random(99);

	// Boost held for two minutes at an uneven frame rate: the meter runs dry and unlocks about eleven times.
	// The server's delta times differ a little from the client's; it must still call every move as the client did.
	{
		float ClientEnergy = 1.0f;
		bool bClientLocked = false;
		float ServerEnergy = 1.0f;
		bool bServerLocked = false;
		int32 NumBoosted = 0;
		int32 NumMoves = 0;
		int32 NumFlips = 0;
		bool bWasActive = false;
		for (float Time = 0.0f; Time < 120.0f;)
		{
			const float Delta = Random.FRandRange(0.006f, 0.035f);
			Time += Delta;
			const bool bClientActive = HCJBalance::StepBoost(ClientEnergy, bClientLocked, true, Delta, Drain, Recharge, Relock);
			const bool bServerActive = HCJBalance::StepBoostFollow(ServerEnergy, bServerLocked, bClientActive, Delta * Random.FRandRange(0.995f, 1.005f), Drain, Recharge, Relock);
			HCJ_TEST_CHECK(bServerActive == bClientActive, TEXT("t=%.2f: client %d (meter %.3f), server %d (meter %.3f)"), Time, bClientActive ? 1 : 0, ClientEnergy,
				bServerActive ? 1 : 0, ServerEnergy);
			NumFlips += bClientActive != bWasActive ? 1 : 0;
			bWasActive = bClientActive;
			NumBoosted += bClientActive ? 1 : 0;
			++NumMoves;
		}
		TestTrue(TEXT("the meter ran dry and unlocked many times"), NumFlips >= 20);
		TestTrue(TEXT("boosted about a third of the time"), NumBoosted > NumMoves / 4 && NumBoosted < NumMoves / 2);
	}
	{
		// A client that claims boost on every move gets the meter's worth and no more.
		float Energy = 1.0f;
		bool bLocked = false;
		float Boosted = 0.0f;
		const float Delta = 1.0f / 60.0f;
		for (int32 Index = 0; Index < 60 * 60; ++Index)
		{
			Boosted += HCJBalance::StepBoostFollow(Energy, bLocked, true, Delta, Drain, Recharge, Relock) ? Delta : 0.0f;
		}
		// An honest pilot holding the key gets a full meter, then a quarter of one every refill to the relock level.
		float HonestEnergy = 1.0f;
		bool bHonestLocked = false;
		float Honest = 0.0f;
		for (int32 Index = 0; Index < 60 * 60; ++Index)
		{
			Honest += HCJBalance::StepBoost(HonestEnergy, bHonestLocked, true, Delta, Drain, Recharge, Relock) ? Delta : 0.0f;
		}
		TestTrue(TEXT("a cheat gains little over an honest pilot"), Boosted <= Honest * 1.6f + 0.5f);
		TestTrue(TEXT("and far less than the full minute"), Boosted < 30.0f);
	}
	return NumFailures == 0;
}

/** Tractor beam: what it can hold, how it pulls, when it breaks. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJTractorPullTest, "HypeCubeJam.Systems.TractorPull", HCJSystemTestFlags)

bool FHCJTractorPullTest::RunTest(const FString& Parameters)
{
	const FVector Holder(1000.0, 2000.0, -500.0);
	const FVector Forward = FVector::ForwardVector;
	const float Range = 6500.0f;

	// Range and cone; a ship already held may stretch the range and leave the narrow cone.
	TestTrue(TEXT("ahead, in range"), HCJTractor::CanHold(Holder, Forward, Holder + FVector(5000.0, 0.0, 0.0), Range, false));
	TestFalse(TEXT("ahead, too far"), HCJTractor::CanHold(Holder, Forward, Holder + FVector(7000.0, 0.0, 0.0), Range, false));
	TestTrue(TEXT("held: stretched range"), HCJTractor::CanHold(Holder, Forward, Holder + FVector(7000.0, 0.0, 0.0), Range, true));
	TestFalse(TEXT("held: beyond the slack"), HCJTractor::CanHold(Holder, Forward, Holder + FVector(Range * HCJTractor::RangeSlack + 100.0, 0.0, 0.0), Range, true));
	TestFalse(TEXT("to the side: outside the cone"), HCJTractor::CanHold(Holder, Forward, Holder + FVector(1000.0, 4000.0, 0.0), Range, false));
	TestTrue(TEXT("to the side, held: inside the wide cone"), HCJTractor::CanHold(Holder, Forward, Holder + FVector(1000.0, 4000.0, 0.0), Range, true));
	TestFalse(TEXT("behind: never"), HCJTractor::CanHold(Holder, Forward, Holder - FVector(3000.0, 0.0, 0.0), Range, true));

	// The pull: towards the holder, at the pull speed, never closer than the hold distance, nothing inside it.
	const FVector Victim = Holder + FVector(4000.0, 3000.0, 0.0);
	const FVector Pull = HCJTractor::ComputePull(Victim, Holder, 1200.0f, 0.05f);
	TestTrue(TEXT("pull length = speed x time"), FMath::IsNearlyEqual(static_cast<float>(Pull.Size()), 60.0f, 0.01f));
	TestTrue(TEXT("pull towards the holder"), FVector::DotProduct(Pull.GetSafeNormal(), (Holder - Victim).GetSafeNormal()) > 0.9999);
	TestTrue(TEXT("no pull without time or speed"), HCJTractor::ComputePull(Victim, Holder, 1200.0f, 0.0f).IsZero() && HCJTractor::ComputePull(Victim, Holder, 0.0f, 0.05f).IsZero());
	const FVector Near = Holder + FVector(HCJTractor::HoldDistance + 20.0, 0.0, 0.0);
	TestTrue(TEXT("never past the hold distance"), FMath::IsNearlyEqual(static_cast<float>(HCJTractor::ComputePull(Near, Holder, 1200.0f, 0.5f).Size()), 20.0f, 0.01f));
	TestTrue(TEXT("nothing inside the hold distance"), HCJTractor::ComputePull(Holder + FVector(500.0, 0.0, 0.0), Holder, 1200.0f, 0.05f).IsZero());

	// Held for two seconds from 50 m: it ends up at 50 m - 2 s x pull, on the line to the holder.
	FVector Position = Holder + FVector(5000.0, 0.0, 0.0);
	for (int32 Index = 0; Index < 120; ++Index)
	{
		Position += HCJTractor::ComputePull(Position, Holder, 1200.0f, 1.0f / 60.0f);
	}
	TestTrue(TEXT("two seconds of pull"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Position, Holder)), 5000.0f - 2400.0f, 1.0f));

	// Breaking it.
	TestFalse(TEXT("holds"), HCJTractor::ShouldBreak(0.5f, 10.0f));
	TestTrue(TEXT("boosted free"), HCJTractor::ShouldBreak(HCJTractor::BreakBoostSeconds, 0.0f));
	TestTrue(TEXT("the holder was hit"), HCJTractor::ShouldBreak(0.0f, HCJTractor::BreakDamage));
	TestTrue(TEXT("a held ship is slowed"), HCJTractor::SpeedScale > 0.2f && HCJTractor::SpeedScale < 1.0f);
	return true;
}

/** Cloaking device: the meter, running dry, the reveals, weapons after a decloak, the owner's byte. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJCloakDeviceTest, "HypeCubeJam.Systems.CloakDevice", HCJSystemTestFlags)

bool FHCJCloakDeviceTest::RunTest(const FString& Parameters)
{
	const float Drain = 12.0f;
	const float Recharge = 20.0f;
	const float Step = 0.05f;

	FHCJCloakDevice Cloak;
	TestFalse(TEXT("not cloaked without the wish"), Cloak.Step(false, Step, Drain, Recharge, HCJCloak::RelockLevel));
	TestTrue(TEXT("raised"), Cloak.Step(true, Step, Drain, Recharge, HCJCloak::RelockLevel));
	TestTrue(TEXT("hidden"), Cloak.IsHidden() && !Cloak.IsRevealed());

	// Half the meter in half the time.
	for (int32 Index = 0; Index < 119; ++Index)
	{
		Cloak.Step(true, Step, Drain, Recharge, HCJCloak::RelockLevel);
	}
	TestTrue(TEXT("half drained after six seconds"), FMath::IsNearlyEqual(Cloak.Energy, 0.5f, 0.01f) && Cloak.bCloaked);

	// A reveal: still cloaked, seen for that long, then hidden again.
	Cloak.Reveal(HCJCloak::HitRevealSeconds);
	TestTrue(TEXT("revealed"), Cloak.IsRevealed() && !Cloak.IsHidden() && Cloak.bCloaked);
	for (int32 Index = 0; Index < 30; ++Index)
	{
		Cloak.Step(true, Step, Drain, Recharge, HCJCloak::RelockLevel);
	}
	TestTrue(TEXT("hidden again after the reveal"), Cloak.IsHidden());

	// Runs dry: drops by itself, locked until the relock level, then usable again.
	bool bCloaked = true;
	float Seconds = 0.0f;
	while (bCloaked && Seconds < 20.0f)
	{
		bCloaked = Cloak.Step(true, Step, Drain, Recharge, HCJCloak::RelockLevel);
		Seconds += Step;
	}
	TestTrue(TEXT("ran dry after the rest of the meter"), !bCloaked && Cloak.bLocked && Seconds > 4.0f && Seconds < 5.0f && !Cloak.CanEngage());
	TestFalse(TEXT("locked: cannot be raised"), Cloak.Step(true, Step, Drain, Recharge, HCJCloak::RelockLevel));
	for (int32 Index = 0; Index < 90; ++Index)
	{
		Cloak.Step(false, Step, Drain, Recharge, HCJCloak::RelockLevel);
	}
	TestTrue(TEXT("unlocked at the relock level"), !Cloak.bLocked && Cloak.Energy >= HCJCloak::RelockLevel && Cloak.Energy < 0.3f);
	TestTrue(TEXT("usable again"), Cloak.Step(true, Step, Drain, Recharge, HCJCloak::RelockLevel));

	// Dropping it: the meter refills over the recharge time; a reveal does not outlive the cloak.
	Cloak.Reveal(5.0f);
	Cloak.Step(false, Step, Drain, Recharge, HCJCloak::RelockLevel);
	TestTrue(TEXT("dropped: not revealed"), !Cloak.bCloaked && !Cloak.IsRevealed() && Cloak.RevealSeconds <= 0.0f);
	for (int32 Index = 0; Index < 420; ++Index)
	{
		Cloak.Step(false, Step, Drain, Recharge, HCJCloak::RelockLevel);
	}
	TestTrue(TEXT("full after the recharge time"), FMath::IsNearlyEqual(Cloak.Energy, 1.0f));

	// A cloak pad: a full meter (wasted on a full one), and with the free time it does not drain.
	TestFalse(TEXT("pad on a full meter: wasted"), Cloak.Refill());
	Cloak.Energy = 0.1f;
	Cloak.bLocked = true;
	TestTrue(TEXT("pad on an empty meter"), Cloak.Refill() && Cloak.Energy == 1.0f && !Cloak.bLocked);
	for (int32 Index = 0; Index < 100; ++Index)
	{
		Cloak.Step(true, Step, Drain, Recharge, HCJCloak::RelockLevel, /*bFreeDrain*/ true);
	}
	TestTrue(TEXT("free time: no drain"), Cloak.bCloaked && Cloak.Energy == 1.0f);

	// Weapons: never while cloaked, and only a moment after the cloak dropped.
	TestFalse(TEXT("no weapons while cloaked"), HCJCloak::CanFire(true, 100.0f));
	TestFalse(TEXT("no weapons right after the decloak"), HCJCloak::CanFire(false, HCJCloak::WeaponDelay * 0.5f));
	TestTrue(TEXT("weapons after the delay"), HCJCloak::CanFire(false, HCJCloak::WeaponDelay));

	// The owner's byte.
	for (const float Energy : { 0.0f, 0.2f, 0.5f, 1.0f })
	{
		for (const bool bLocked : { false, true })
		{
			float Out = -1.0f;
			bool bOutLocked = !bLocked;
			HCJCloak::UnpackEnergy(HCJCloak::PackEnergy(Energy, bLocked), Out, bOutLocked);
			TestTrue(TEXT("meter byte round trip"), FMath::IsNearlyEqual(Out, Energy, 0.005f) && bOutLocked == bLocked);
		}
	}
	return true;
}

/** Ship types: the table is usable and says what the hangar says about each type. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShipTypeTableTest, "HypeCubeJam.ShipTypes.Table", HCJSystemTestFlags)

bool FHCJShipTypeTableTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	FString Why;
	TestTrue(*FString::Printf(TEXT("table valid (%s)"), *Why), HCJShipTypes::IsTableValid(&Why));
	TestEqual(TEXT("five types"), HCJShipTypes::NumTypes, 5);

	const FHCJShipTypeDef& Interceptor = HCJShipTypes::Get(EHCJShipType::Interceptor);
	const FHCJShipTypeDef& Fighter = HCJShipTypes::Get(EHCJShipType::Fighter);
	const FHCJShipTypeDef& Gunship = HCJShipTypes::Get(EHCJShipType::Gunship);
	const FHCJShipTypeDef& Scout = HCJShipTypes::Get(EHCJShipType::Scout);
	const FHCJShipTypeDef& Cruiser = HCJShipTypes::Get(EHCJShipType::Cruiser);

	// The fighter is the ship the game had before the types: its numbers must not move.
	TestTrue(TEXT("fighter flight = the old ship"), Fighter.CruiseSpeed == 3000.0f && Fighter.Acceleration == 6000.0f && Fighter.Braking == 1900.0f
		&& Fighter.BoostDrainSeconds == 3.5f && Fighter.BoostRechargeSeconds == 7.0f && Fighter.Scale == 1.0f);
	TestTrue(TEXT("fighter hull and weapons = the old ship"), Fighter.MaxHull == 100.0f && Fighter.ShieldFacing == 26.0f && Fighter.ShieldRegenRate == 8.0f
		&& Fighter.ForeDamageScale == 1.0f && Fighter.PhaserDamageScale == 1.0f && Fighter.PhaserRechargeSeconds == 5.0f && Fighter.BoltDamageScale == 1.0f && Fighter.Torpedoes == 4);

	// What the hangar promises.
	for (int32 Type = 0; Type < HCJShipTypes::NumTypes; ++Type)
	{
		const FHCJShipTypeDef& Def = HCJShipTypes::Get(Type);
		HCJ_TEST_CHECK(Def.CruiseSpeed <= Interceptor.CruiseSpeed && Def.Acceleration <= Interceptor.Acceleration, TEXT("%s is faster or more agile than the interceptor"), Def.Name);
		HCJ_TEST_CHECK(Def.CruiseSpeed >= Cruiser.CruiseSpeed && Def.Scale <= Cruiser.Scale, TEXT("%s is slower or bigger than the cruiser"), Def.Name);
		HCJ_TEST_CHECK(Def.ShieldFacing <= Cruiser.ShieldFacing && Def.TractorPullSpeed <= Cruiser.TractorPullSpeed && Def.TractorRange <= Cruiser.TractorRange,
			TEXT("%s has stronger shields or a stronger tractor beam than the cruiser"), Def.Name);
		HCJ_TEST_CHECK(Def.Torpedoes <= Gunship.Torpedoes && Def.PhaserDamageScale <= Gunship.PhaserDamageScale, TEXT("%s out-guns the gunship"), Def.Name);
		HCJ_TEST_CHECK(Def.BoostDrainSeconds <= Scout.BoostDrainSeconds && Def.CloakSeconds <= Scout.CloakSeconds && Def.WarpCooldown >= Scout.WarpCooldown,
			TEXT("%s out-runs the scout's boost, cloak or warp drive"), Def.Name);
		HCJ_TEST_CHECK(Def.PhaserRechargeSeconds >= Interceptor.PhaserRechargeSeconds, TEXT("%s recharges its phasers faster than the interceptor"), Def.Name);
		for (int32 Stat = 0; Stat < static_cast<int32>(HCJShipTypes::EStat::Count); ++Stat)
		{
			const float Value = HCJShipTypes::GetStat(Type, static_cast<HCJShipTypes::EStat>(Stat));
			HCJ_TEST_CHECK(Value > 0.1f && Value <= 1.0001f, TEXT("%s: stat %d is %.2f"), Def.Name, Stat, Value);
		}
		// Client prediction and the collision: nothing extreme.
		HCJ_TEST_CHECK(Def.CruiseSpeed >= 2000.0f && Def.CruiseSpeed <= 4000.0f && Def.Scale >= 0.8f && Def.Scale <= 1.35f, TEXT("%s: speed or size out of the tested range"), Def.Name);
	}
	TestTrue(TEXT("the gunship has fore armour and a heavy hull"), Gunship.ForeDamageScale < 1.0f && Gunship.MaxHull > Fighter.MaxHull);
	TestTrue(TEXT("the light hulls are light"), Interceptor.MaxHull < Fighter.MaxHull && Scout.MaxHull < Fighter.MaxHull && Interceptor.Torpedoes == 2);

	// Hull meshes: the chosen hull, or a hash of the player id, always inside the type's range and the same every time.
	for (int32 Type = 0; Type < HCJShipTypes::NumTypes; ++Type)
	{
		const FHCJShipTypeDef& Def = HCJShipTypes::Get(Type);
		TSet<int32> Seen;
		for (int32 PlayerId = 0; PlayerId < 400; ++PlayerId)
		{
			const int32 Mesh = HCJShipTypes::PickHullMesh(Type, HCJShipTypes::Auto, PlayerId, HCJShipTypes::NumHullMeshes);
			HCJ_TEST_CHECK(Mesh >= Def.FirstHull && Mesh < Def.FirstHull + Def.NumHulls, TEXT("%s: player %d got mesh %d"), Def.Name, PlayerId, Mesh);
			HCJ_TEST_CHECK(Mesh == HCJShipTypes::PickHullMesh(Type, HCJShipTypes::Auto, PlayerId, HCJShipTypes::NumHullMeshes), TEXT("%s: not deterministic"), Def.Name);
			Seen.Add(Mesh);
		}
		HCJ_TEST_CHECK(Seen.Num() == Def.NumHulls, TEXT("%s: %d of %d hulls ever picked"), Def.Name, Seen.Num(), Def.NumHulls);
		for (int32 Variant = 0; Variant < Def.NumHulls; ++Variant)
		{
			HCJ_TEST_CHECK(HCJShipTypes::PickHullMesh(Type, static_cast<uint8>(Variant), 123, HCJShipTypes::NumHullMeshes) == Def.FirstHull + Variant, TEXT("%s: hull %d"), Def.Name, Variant);
		}
		HCJ_TEST_CHECK(HCJShipTypes::PickHullMesh(Type, 0, 5, 3) >= 0 && HCJShipTypes::PickHullMesh(Type, 0, 5, 3) < 3, TEXT("%s: a short mesh list"), Def.Name);
	}
	TestEqual(TEXT("no meshes"), HCJShipTypes::PickHullMesh(1, 0, 5, 0), static_cast<int32>(INDEX_NONE));

	// Bots: five bots in a row fly five different types.
	TSet<uint8> BotTypes;
	for (int32 Bot = 0; Bot < HCJShipTypes::NumTypes; ++Bot)
	{
		BotTypes.Add(HCJShipTypes::PickBotType(Bot + 7));
	}
	TestEqual(TEXT("bots get every type"), BotTypes.Num(), HCJShipTypes::NumTypes);
	return NumFailures == 0;
}

/** What a client sends as its ship choice is validated on the server: anything unknown becomes the fighter / "pick for me". */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShipTypeValidationTest, "HypeCubeJam.ShipTypes.ServerValidation", HCJSystemTestFlags)

bool FHCJShipTypeValidationTest::RunTest(const FString& Parameters)
{
	const uint8 Fighter = static_cast<uint8>(EHCJShipType::Fighter);
	for (int32 Type = 0; Type < HCJShipTypes::NumTypes; ++Type)
	{
		TestEqual(TEXT("a valid type is kept"), static_cast<int32>(HCJShipTypes::SanitizeType(Type)), Type);
	}
	for (const int32 Bad : { -1, 5, 6, 127, 200, 255, 100000, MIN_int32 })
	{
		TestEqual(TEXT("an unknown type is the fighter"), static_cast<int32>(HCJShipTypes::SanitizeType(Bad)), static_cast<int32>(Fighter));
		// And its row is the fighter's, whoever asks with the raw number.
		TestTrue(TEXT("the row of an unknown type is the fighter's"), &HCJShipTypes::Get(Bad) == &HCJShipTypes::Get(EHCJShipType::Fighter));
	}

	const int32 NumHulls = HCJShipTypes::Get(EHCJShipType::Cruiser).NumHulls;
	TestEqual(TEXT("a valid hull is kept"), static_cast<int32>(HCJShipTypes::SanitizeVariant(4, NumHulls - 1)), NumHulls - 1);
	for (const int32 Bad : { -1, NumHulls, 40, 255, 1000 })
	{
		TestEqual(TEXT("an unknown hull is 'pick for me'"), static_cast<int32>(HCJShipTypes::SanitizeVariant(4, Bad)), static_cast<int32>(HCJShipTypes::Auto));
	}
	TestEqual(TEXT("a valid paint is kept"), static_cast<int32>(HCJShipTypes::SanitizeSkin(9, 10)), 9);
	for (const int32 Bad : { -1, 10, 255, 4000 })
	{
		TestEqual(TEXT("an unknown paint is 'pick for me'"), static_cast<int32>(HCJShipTypes::SanitizeSkin(Bad, 10)), static_cast<int32>(HCJShipTypes::Auto));
	}
	TestEqual(TEXT("no paints configured"), static_cast<int32>(HCJShipTypes::SanitizeSkin(0, 0)), static_cast<int32>(HCJShipTypes::Auto));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
