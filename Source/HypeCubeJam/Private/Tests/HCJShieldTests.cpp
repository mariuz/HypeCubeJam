#include "Tests/HCJTestHelpers.h"

#include "Game/HCJBalance.h"
#include "Ship/HCJShieldFacings.h"
#include "UI/HCJHints.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJShieldTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	float Sum(TConstArrayView<float> Values)
	{
		float Total = 0.0f;
		for (const float Value : Values)
		{
			Total += Value;
		}
		return Total;
	}
}

/** Facing selection: quarters of the horizontal circle in the ship's frame, the vertical part ignored, total and scale free. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShieldFacingSelectTest, "HypeCubeJam.Shield.FacingSelection", HCJShieldTestFlags)

bool FHCJShieldFacingSelectTest::RunTest(const FString& Parameters)
{
	using namespace HCJShield;
	int32 NumFailures = 0;

	TestTrue(TEXT("ahead is fore"), SelectFacing(FVector(1, 0, 0)) == EHCJShieldFacing::Fore);
	TestTrue(TEXT("behind is aft"), SelectFacing(FVector(-1, 0, 0)) == EHCJShieldFacing::Aft);
	TestTrue(TEXT("right is starboard"), SelectFacing(FVector(0, 1, 0)) == EHCJShieldFacing::Starboard);
	TestTrue(TEXT("left is port"), SelectFacing(FVector(0, -1, 0)) == EHCJShieldFacing::Port);
	TestTrue(TEXT("40 degrees off the nose is still fore"), SelectFacing(FRotator(0, 40, 0).Vector()) == EHCJShieldFacing::Fore);
	TestTrue(TEXT("50 degrees off the nose to the right is starboard"), SelectFacing(FRotator(0, 50, 0).Vector()) == EHCJShieldFacing::Starboard);
	TestTrue(TEXT("50 degrees to the left is port"), SelectFacing(FRotator(0, -50, 0).Vector()) == EHCJShieldFacing::Port);
	TestTrue(TEXT("140 degrees is aft"), SelectFacing(FRotator(0, 140, 0).Vector()) == EHCJShieldFacing::Aft);
	TestTrue(TEXT("from above and ahead is fore"), SelectFacing(FVector(0.2, 0.05, 0.97)) == EHCJShieldFacing::Fore);
	TestTrue(TEXT("from below and left is port"), SelectFacing(FVector(0.05, -0.2, -0.97)) == EHCJShieldFacing::Port);
	TestTrue(TEXT("straight above counts as fore"), SelectFacing(FVector(0, 0, 1)) == EHCJShieldFacing::Fore);
	TestTrue(TEXT("no direction counts as fore"), SelectFacing(FVector::ZeroVector) == EHCJShieldFacing::Fore);
	TestTrue(TEXT("length does not matter"), SelectFacing(FVector(-3000, 100, 40)) == EHCJShieldFacing::Aft);

	// Every direction of a sweep: the chosen facing's axis is never more than 45 degrees (plus rounding) from the
	// horizontal direction, and each facing gets a quarter of the circle.
	int32 Counts[NumFacings] = {};
	for (int32 Step = 0; Step < 3600; ++Step)
	{
		const float Yaw = Step * 0.1f + 0.05f;
		for (const float Pitch : { -60.0f, 0.0f, 35.0f })
		{
			const FVector Direction = FRotator(Pitch, Yaw, 0).Vector();
			const EHCJShieldFacing Facing = SelectFacing(Direction);
			const double Cosine = FVector::DotProduct(FacingAxis(Facing), FVector(Direction.X, Direction.Y, 0.0).GetSafeNormal());
			HCJ_TEST_CHECK(Cosine >= 0.7071 - 1.0e-3, TEXT("yaw %.2f pitch %.0f: facing %d is %.3f off"), Yaw, Pitch, static_cast<int32>(Facing), Cosine);
			if (Pitch == 0.0f)
			{
				++Counts[static_cast<int32>(Facing)];
			}
		}
	}
	for (int32 Facing = 0; Facing < NumFacings; ++Facing)
	{
		HCJ_TEST_CHECK(Counts[Facing] == 900, TEXT("facing %d covers %d of 3600 steps"), Facing, Counts[Facing]);
		// Axis, yaw and selection agree with each other.
		const EHCJShieldFacing Value = static_cast<EHCJShieldFacing>(Facing);
		HCJ_TEST_CHECK(SelectFacing(FacingAxis(Value)) == Value, TEXT("axis of facing %d selects another facing"), Facing);
		HCJ_TEST_CHECK(FRotator(0, FacingYawDegrees(Value), 0).Vector().Equals(FacingAxis(Value), 1.0e-4), TEXT("yaw of facing %d does not match its axis"), Facing);
		HCJ_TEST_CHECK(FCString::Strlen(FacingName(Value)) > 2, TEXT("facing %d has no name"), Facing);
	}
	HCJ_TEST_FINISH();
}

/** One facing absorbs what it has; overflow and hits on a downed facing go to the hull; other facings are untouched. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShieldAbsorbTest, "HypeCubeJam.Shield.AbsorbAndOverflow", HCJShieldTestFlags)

bool FHCJShieldAbsorbTest::RunTest(const FString& Parameters)
{
	using namespace HCJShield;
	bool bCollapsed = true;

	float Strength = 20.0f;
	TestEqual(TEXT("a hit the facing can take leaves nothing for the hull"), AbsorbOnFacing(Strength, 13.0f, bCollapsed), 0.0f, 1.0e-4f);
	TestEqual(TEXT("and drains it"), Strength, 7.0f, 1.0e-4f);
	TestFalse(TEXT("still up"), bCollapsed);

	TestEqual(TEXT("the next hit overflows to the hull"), AbsorbOnFacing(Strength, 13.0f, bCollapsed), 6.0f, 1.0e-4f);
	TestEqual(TEXT("the facing is exactly down"), Strength, 0.0f);
	TestTrue(TEXT("this hit brought it down"), bCollapsed);

	TestEqual(TEXT("a hit on a downed facing goes to the hull whole"), AbsorbOnFacing(Strength, 13.0f, bCollapsed), 13.0f, 1.0e-4f);
	TestFalse(TEXT("a facing collapses only once"), bCollapsed);

	Strength = 13.0f;
	TestEqual(TEXT("a hit of exactly the strength leaves nothing for the hull"), AbsorbOnFacing(Strength, 13.0f, bCollapsed), 0.0f);
	TestTrue(TEXT("and brings the facing down"), bCollapsed && Strength == 0.0f);

	Strength = 5.0f;
	TestEqual(TEXT("no damage, no change"), AbsorbOnFacing(Strength, 0.0f, bCollapsed), 0.0f);
	TestEqual(TEXT("negative damage, no change"), AbsorbOnFacing(Strength, -4.0f, bCollapsed), 0.0f);
	TestEqual(TEXT("strength kept"), Strength, 5.0f);

	// Damage without a direction: an equal share each, facings that run dry pass the rest on, the remainder to the hull.
	float Facings[NumFacings] = { 20.0f, 20.0f, 20.0f, 20.0f };
	uint8 Mask = 0xFF;
	TestEqual(TEXT("spread damage inside the shield"), AbsorbSpread(Facings, 40.0f, Mask), 0.0f, 1.0e-4f);
	TestTrue(TEXT("ten off each"), FMath::IsNearlyEqual(Facings[0], 10.0f, 1.0e-4f) && FMath::IsNearlyEqual(Facings[3], 10.0f, 1.0e-4f));
	TestEqual(TEXT("nothing collapsed"), static_cast<int32>(Mask), 0);

	float Uneven[NumFacings] = { 2.0f, 20.0f, 0.0f, 20.0f };
	TestEqual(TEXT("uneven facings share the load"), AbsorbSpread(Uneven, 30.0f, Mask), 0.0f, 1.0e-3f);
	TestEqual(TEXT("the weak facing is down"), Uneven[0], 0.0f);
	TestEqual(TEXT("the downed facing stays down"), Uneven[2], 0.0f);
	TestEqual(TEXT("the others took the rest"), Sum(Uneven), 12.0f, 1.0e-3f);
	TestEqual(TEXT("the weak facing is reported"), static_cast<int32>(Mask), 1);

	float Small[NumFacings] = { 5.0f, 5.0f, 5.0f, 5.0f };
	TestEqual(TEXT("more than the whole shield: the rest goes to the hull"), AbsorbSpread(Small, 50.0f, Mask), 30.0f, 1.0e-3f);
	TestEqual(TEXT("all down"), Sum(Small), 0.0f);
	TestEqual(TEXT("all four reported"), static_cast<int32>(Mask), 0x0F);
	TestEqual(TEXT("no shield: everything goes to the hull"), AbsorbSpread(Small, 9.0f, Mask), 9.0f, 1.0e-4f);

	// A single shield (station turrets) behaves like the old one value.
	float Single[1] = { 50.0f };
	TestEqual(TEXT("single shield absorbs"), AbsorbSpread(Single, 20.0f, Mask), 0.0f, 1.0e-4f);
	TestEqual(TEXT("single shield value"), Single[0], 30.0f, 1.0e-4f);
	TestEqual(TEXT("single shield overflow"), AbsorbSpread(Single, 45.0f, Mask), 15.0f, 1.0e-4f);

	// Conservation over many random cases: shield lost + hull part == damage.
	FRandomStream Random(4711);
	int32 NumFailures = 0;
	for (int32 Case = 0; Case < 2000; ++Case)
	{
		float Values[NumFacings];
		for (float& Value : Values)
		{
			Value = Random.FRand() < 0.25f ? 0.0f : Random.FRandRange(0.0f, 45.0f);
		}
		const float Before = Sum(Values);
		const float Damage = Random.FRandRange(0.0f, 120.0f);
		const float ToHull = AbsorbSpread(Values, Damage, Mask);
		HCJ_TEST_CHECK(FMath::IsNearlyEqual(Before - Sum(Values) + ToHull, Damage, 0.01f), TEXT("case %d: %.3f lost + %.3f hull != %.3f"), Case, Before - Sum(Values), ToHull, Damage);
		HCJ_TEST_CHECK(ToHull <= 0.0f || Sum(Values) <= 0.0f, TEXT("case %d: hull damage %.3f while %.3f shield is left"), Case, ToHull, Sum(Values));
	}
	HCJ_TEST_FINISH();
}

/** Each facing regenerates by the existing rule on its own clock: a facing under fire stays down while the others recover. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShieldFacingRegenTest, "HypeCubeJam.Shield.FacingRegen", HCJShieldTestFlags)

bool FHCJShieldFacingRegenTest::RunTest(const FString& Parameters)
{
	constexpr float Capacity = 20.0f;
	constexpr float Delay = 4.0f;
	constexpr float Rate = 6.0f;
	constexpr float Step = 1.0f / 60.0f;

	// Fore is hit every second, aft was hit once at t = 0, port is overcharged, starboard untouched.
	float Facings[HCJShield::NumFacings] = { 0.0f, 0.0f, 35.0f, Capacity };
	float LastHit[HCJShield::NumFacings] = { 0.0f, 0.0f, -1000.0f, -1000.0f };
	float AftFullAt = -1.0f;
	for (float Time = Step; Time < 12.0f; Time += Step)
	{
		if (FMath::Fmod(Time, 1.0f) < Step)
		{
			LastHit[0] = Time;
		}
		for (int32 Index = 0; Index < HCJShield::NumFacings; ++Index)
		{
			Facings[Index] = HCJBalance::StepShieldRegen(Facings[Index], Capacity, Time - LastHit[Index], Step, Delay, Rate, false);
		}
		if (AftFullAt < 0.0f && Facings[1] >= Capacity)
		{
			AftFullAt = Time;
		}
		if (Time < Delay - Step)
		{
			TestEqual(TEXT("nothing regenerates inside the delay"), Facings[1], 0.0f);
		}
	}
	TestEqual(TEXT("the facing under fire stays down"), Facings[0], 0.0f);
	TestEqual(TEXT("the facing left alone is full after delay + capacity / rate"), AftFullAt, Delay + Capacity / Rate, 0.05f);
	TestEqual(TEXT("and stops at its capacity"), Facings[1], Capacity);
	TestEqual(TEXT("an overcharged facing is left alone"), Facings[2], 35.0f);
	TestEqual(TEXT("a full facing is left alone"), Facings[3], Capacity);
	TestEqual(TEXT("a destroyed ship regenerates nothing"), HCJBalance::StepShieldRegen(3.0f, Capacity, 100.0f, Step, Delay, Rate, true), 3.0f);
	return true;
}

/** A shield cell fills the weakest facings first, never above the overcharge limit, and adds exactly what it reports. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShieldOverchargeTest, "HypeCubeJam.Shield.OverchargeSpread", HCJShieldTestFlags)

bool FHCJShieldOverchargeTest::RunTest(const FString& Parameters)
{
	using namespace HCJShield;
	constexpr float Capacity = 20.0f;
	constexpr float Limit = 2.0f * Capacity;

	float Full[NumFacings] = { Capacity, Capacity, Capacity, Capacity };
	TestEqual(TEXT("a cell on a full shield overcharges"), AddSpread(Full, 40.0f, Limit), 40.0f, 1.0e-3f);
	TestTrue(TEXT("evenly"), FMath::IsNearlyEqual(Full[0], 30.0f, 1.0e-3f) && FMath::IsNearlyEqual(Full[1], 30.0f, 1.0e-3f)
		&& FMath::IsNearlyEqual(Full[2], 30.0f, 1.0e-3f) && FMath::IsNearlyEqual(Full[3], 30.0f, 1.0e-3f));
	TestEqual(TEXT("a second cell stops at the limit"), AddSpread(Full, 80.0f, Limit), 40.0f, 1.0e-3f);
	TestTrue(TEXT("all at the limit"), FMath::IsNearlyEqual(Sum(Full), 4.0f * Limit, 1.0e-3f));
	TestEqual(TEXT("a third cell is wasted"), AddSpread(Full, 50.0f, Limit), 0.0f);

	// The downed facing comes first: 0, 5, 20, 20 with 25 points -> 15, 15, 20, 20.
	float Holed[NumFacings] = { 0.0f, 5.0f, Capacity, Capacity };
	TestEqual(TEXT("everything is used"), AddSpread(Holed, 25.0f, Limit), 25.0f, 1.0e-3f);
	TestEqual(TEXT("the downed facing is back"), Holed[0], 15.0f, 1.0e-3f);
	TestEqual(TEXT("level with the weak one"), Holed[1], 15.0f, 1.0e-3f);
	TestEqual(TEXT("full facings are not touched before the weak ones caught up"), Holed[2], Capacity, 1.0e-3f);

	float One[1] = { 10.0f };
	TestEqual(TEXT("single shield"), AddSpread(One, 50.0f, 100.0f), 50.0f, 1.0e-3f);
	TestEqual(TEXT("single shield value"), One[0], 60.0f, 1.0e-3f);
	TestEqual(TEXT("nothing to add"), AddSpread(One, 0.0f, 100.0f), 0.0f);

	FRandomStream Random(99);
	int32 NumFailures = 0;
	for (int32 Case = 0; Case < 2000; ++Case)
	{
		float Values[NumFacings];
		for (float& Value : Values)
		{
			Value = Random.FRandRange(0.0f, Limit);
		}
		const float Before = Sum(Values);
		float Lowest = Limit;
		for (const float Value : Values)
		{
			Lowest = FMath::Min(Lowest, Value);
		}
		const float Amount = Random.FRandRange(0.0f, 120.0f);
		const float Added = AddSpread(Values, Amount, Limit);
		HCJ_TEST_CHECK(FMath::IsNearlyEqual(Sum(Values) - Before, Added, 0.01f), TEXT("case %d: sum grew by %.3f, reported %.3f"), Case, Sum(Values) - Before, Added);
		HCJ_TEST_CHECK(Added <= Amount + 0.01f && Added <= 4.0f * Limit - Before + 0.01f, TEXT("case %d: added %.3f of %.3f"), Case, Added, Amount);
		HCJ_TEST_CHECK(FMath::IsNearlyEqual(Added, FMath::Min(Amount, 4.0f * Limit - Before), 0.01f), TEXT("case %d: %.3f added, room for %.3f"), Case, Added, FMath::Min(Amount, 4.0f * Limit - Before));
		for (const float Value : Values)
		{
			HCJ_TEST_CHECK(Value <= Limit + 1.0e-3f && Value >= Lowest - 1.0e-3f, TEXT("case %d: facing at %.3f (limit %.1f, lowest before %.3f)"), Case, Value, Limit, Lowest);
		}
	}
	HCJ_TEST_FINISH();
}

/** Replication: four strengths in four bytes; round trip within half a step, "down" exact, stable when packed again; the hit direction in 16 bits. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShieldQuantiseTest, "HypeCubeJam.Shield.ReplicationRoundTrip", HCJShieldTestFlags)

bool FHCJShieldQuantiseTest::RunTest(const FString& Parameters)
{
	using namespace HCJShield;
	int32 NumFailures = 0;

	for (const float Capacity : { 12.5f, 20.0f, 50.0f })
	{
		const float Step = Capacity * QuantRange / 255.0f;
		TestEqual(TEXT("down is zero"), static_cast<int32>(Quantise(0.0f, Capacity)), 0);
		TestEqual(TEXT("zero is down"), Dequantise(0, Capacity), 0.0f);
		TestTrue(TEXT("a trace of shield is not down"), Quantise(0.001f, Capacity) == 1 && Dequantise(Quantise(0.001f, Capacity), Capacity) > 0.0f);
		TestEqual(TEXT("the capacity survives"), Dequantise(Quantise(Capacity, Capacity), Capacity), Capacity, Step * 0.5f + 1.0e-4f);
		TestEqual(TEXT("a double overcharge survives"), Dequantise(Quantise(2.0f * Capacity, Capacity), Capacity), 2.0f * Capacity, Step * 0.5f + 1.0e-4f);
		TestEqual(TEXT("values beyond the range are clamped"), static_cast<int32>(Quantise(10.0f * Capacity, Capacity)), 255);
		TestEqual(TEXT("negative is down"), static_cast<int32>(Quantise(-3.0f, Capacity)), 0);

		for (int32 Index = 0; Index <= 4000; ++Index)
		{
			const float Value = Capacity * QuantRange * Index / 4000.0f;
			const uint8 Byte = Quantise(Value, Capacity);
			const float Back = Dequantise(Byte, Capacity);
			// Below one step the value is rounded up to the first step so that it is not "down".
			const float Tolerance = Value < Step ? Step : Step * 0.5f + 1.0e-4f;
			HCJ_TEST_CHECK(FMath::Abs(Back - Value) <= Tolerance, TEXT("capacity %.1f: %.4f -> %d -> %.4f"), Capacity, Value, Byte, Back);
			HCJ_TEST_CHECK((Value > 0.0f) == (Back > 0.0f), TEXT("capacity %.1f: %.4f decodes as %.4f"), Capacity, Value, Back);
			HCJ_TEST_CHECK(Quantise(Back, Capacity) == Byte, TEXT("capacity %.1f: byte %d is not stable"), Capacity, Byte);
		}
	}
	TestEqual(TEXT("no capacity, no shield"), static_cast<int32>(Quantise(5.0f, 0.0f)), 0);

	// Four facings in one word, each in its own byte.
	const float Strengths[NumFacings] = { 20.0f, 0.0f, 7.3f, 41.0f };
	const uint32 Packed = Pack(Strengths, 20.0f);
	float Decoded[NumFacings] = { -1.0f, -1.0f, -1.0f, -1.0f };
	Unpack(Packed, 20.0f, Decoded);
	for (int32 Facing = 0; Facing < NumFacings; ++Facing)
	{
		TestEqual(*FString::Printf(TEXT("facing %d round trip"), Facing), Decoded[Facing], Strengths[Facing], 0.1f + 1.0e-4f);
		TestEqual(*FString::Printf(TEXT("facing %d byte"), Facing), static_cast<int32>((Packed >> (8 * Facing)) & 0xFF), static_cast<int32>(Quantise(Strengths[Facing], 20.0f)));
	}
	TestEqual(TEXT("the downed facing is exactly down"), Decoded[1], 0.0f);
	TestTrue(TEXT("packing the decoded values gives the same word"), Pack(Decoded, 20.0f) == Packed);
	const float Single[1] = { 33.0f };
	float SingleBack[1] = { 0.0f };
	Unpack(Pack(Single, 50.0f), 50.0f, SingleBack);
	TestEqual(TEXT("a single shield uses the first byte"), SingleBack[0], 33.0f, 0.25f);
	TestTrue(TEXT("and leaves the others empty"), (Pack(Single, 50.0f) >> 8) == 0u);

	// The hit direction of the flare.
	FRandomStream Random(31337);
	for (int32 Case = 0; Case < 3000; ++Case)
	{
		const FVector Direction = Random.VRand();
		const FVector Back = UnpackDirection(PackDirection(Direction));
		const double Degrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, Back), -1.0, 1.0)));
		HCJ_TEST_CHECK(Degrees <= 1.5 && FMath::IsNearlyEqual(Back.Size(), 1.0, 1.0e-3), TEXT("direction %s comes back %.2f degrees off"), *Direction.ToString(), Degrees);
	}
	for (int32 Facing = 0; Facing < NumFacings; ++Facing)
	{
		const EHCJShieldFacing Value = static_cast<EHCJShieldFacing>(Facing);
		HCJ_TEST_CHECK(SelectFacing(UnpackDirection(PackDirection(FacingAxis(Value)))) == Value, TEXT("axis of facing %d changes facing on the wire"), Facing);
	}
	HCJ_TEST_FINISH();
}

/** Bots breaking off: keep the facing towards the attacker while it holds, else show the strongest one if it is clearly better. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShieldPresentTest, "HypeCubeJam.Shield.FacingToPresent", HCJShieldTestFlags)

bool FHCJShieldPresentTest::RunTest(const FString& Parameters)
{
	using namespace HCJShield;
	const float Healthy[NumFacings] = { 1.0f, 1.0f, 1.0f, 1.0f };
	TestTrue(TEXT("all healthy: stay"), PickFacingToPresent(Healthy, EHCJShieldFacing::Aft) == EHCJShieldFacing::Aft);
	const float AftDown[NumFacings] = { 0.9f, 0.0f, 0.6f, 0.7f };
	TestTrue(TEXT("aft down under fire from behind: show the fore"), PickFacingToPresent(AftDown, EHCJShieldFacing::Aft) == EHCJShieldFacing::Fore);
	TestTrue(TEXT("attacker on a healthy side: stay"), PickFacingToPresent(AftDown, EHCJShieldFacing::Starboard) == EHCJShieldFacing::Starboard);
	const float AllWeak[NumFacings] = { 0.1f, 0.0f, 0.2f, 0.15f };
	TestTrue(TEXT("nothing clearly better: stay"), PickFacingToPresent(AllWeak, EHCJShieldFacing::Aft) == EHCJShieldFacing::Aft);
	const float WeakFore[NumFacings] = { 0.3f, 0.2f, 1.0f, 0.9f };
	TestTrue(TEXT("weak fore: the strongest side"), PickFacingToPresent(WeakFore, EHCJShieldFacing::Fore) == EHCJShieldFacing::Port);
	const float ForeDown[NumFacings] = { 0.0f, 1.0f, 1.0f, 1.0f };
	const EHCJShieldFacing Side = PickFacingToPresent(ForeDown, EHCJShieldFacing::Fore);
	TestTrue(TEXT("all others equal: a quarter turn, not a half turn"), Side == EHCJShieldFacing::Port || Side == EHCJShieldFacing::Starboard);
	const float SidesWeak[NumFacings] = { 0.0f, 1.0f, 0.5f, 0.6f };
	TestTrue(TEXT("the opposite facing when it is clearly the strongest"), PickFacingToPresent(SidesWeak, EHCJShieldFacing::Fore) == EHCJShieldFacing::Aft);
	const float PortDown[NumFacings] = { 0.95f, 1.0f, 0.0f, 1.0f };
	TestTrue(TEXT("from the port side the neighbours are fore and aft"), PickFacingToPresent(PortDown, EHCJShieldFacing::Port) == EHCJShieldFacing::Aft);
	const float JustAbove[NumFacings] = { WeakFraction + 0.01f, 1.0f, 1.0f, 1.0f };
	TestTrue(TEXT("not weak yet: stay"), PickFacingToPresent(JustAbove, EHCJShieldFacing::Fore) == EHCJShieldFacing::Fore);

	// Colour by strength: orange when weak, blue-white when healthy.
	const FLinearColor Weak = StrengthColor(0.05f);
	const FLinearColor Strong = StrengthColor(1.0f);
	TestTrue(TEXT("weak is orange"), Weak.R > Weak.B + 0.5f);
	TestTrue(TEXT("healthy is blue-white"), Strong.B > Strong.R && Strong.B > 0.9f && Strong.G > 0.7f);
	return true;
}

/** The hint about facings: once, the first time a facing goes down, after the onboarding; it replaces a lesser hint but not a danger. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJShieldHintTest, "HypeCubeJam.Shield.FacingDownHint", HCJShieldTestFlags)

bool FHCJShieldHintTest::RunTest(const FString& Parameters)
{
	const auto Bit = [](EHCJHint Hint) { return FHCJHintDirector::Bit(Hint); };
	const uint32 AllDone = Bit(EHCJHint::Move) | Bit(EHCJHint::Fire) | Bit(EHCJHint::Boost) | Bit(EHCJHint::Target);
	const auto Input = [AllDone](uint32 Active, bool bOnboardingDone = true)
	{
		FHCJHintInput Result;
		Result.bHasShip = true;
		Result.DoneMask = bOnboardingDone ? AllDone : 0;
		Result.ActiveMask = Active;
		return Result;
	};
	const auto Run = [](FHCJHintDirector& Director, float From, float Seconds, const FHCJHintInput& In)
	{
		float Time = From;
		for (; Time < From + Seconds; Time += 0.1f)
		{
			Director.Update(Time, In);
		}
		return Time;
	};

	const FHCJHintRule& Rule = FHCJHintDirector::GetRule(EHCJHint::ShieldDown);
	TestEqual(TEXT("shown once per session"), Rule.MaxShows, 1);
	TestTrue(TEXT("readable"), Rule.Duration >= 6.0f);
	TestTrue(TEXT("an event, not a danger"), Rule.Priority >= FHCJHintDirector::InterruptPriority && Rule.Priority < FHCJHintDirector::UrgentPriority);

	// A fresh player: the onboarding step stays, the facing hint waits (and is lost if the moment passes).
	FHCJHintDirector Fresh;
	Fresh.Update(0.0f, Input(0, false));
	TestTrue(TEXT("onboarding first"), Fresh.Update(0.1f, Input(Bit(EHCJHint::ShieldDown), false)) == EHCJHint::Move);

	// After the onboarding: the goal hint is up, a facing goes down, the facing hint takes over and stays its time.
	FHCJHintDirector Director;
	float Time = Run(Director, 0.0f, FHCJHintDirector::GapSeconds + 0.3f, Input(Bit(EHCJHint::Goal)));
	TestTrue(TEXT("the goal hint"), Director.GetCurrent() == EHCJHint::Goal);
	TestTrue(TEXT("a facing down replaces it"), Director.Update(Time + 0.1f, Input(Bit(EHCJHint::Goal) | Bit(EHCJHint::ShieldDown))) == EHCJHint::ShieldDown);
	Time = Run(Director, Time + 0.1f, 5.0f, Input(0));
	TestTrue(TEXT("it stays although the moment is over"), Director.GetCurrent() == EHCJHint::ShieldDown);
	TestTrue(TEXT("a low hull interrupts it"), Director.Update(Time, Input(Bit(EHCJHint::LowHull))) == EHCJHint::LowHull);

	// The next facing that fails says nothing new.
	Time = Run(Director, Time, 60.0f, Input(0));
	Time = Run(Director, Time, 20.0f, Input(Bit(EHCJHint::ShieldDown)));
	TestTrue(TEXT("not shown again"), Director.GetCurrent() != EHCJHint::ShieldDown);
	TestEqual(TEXT("shown exactly once"), Director.GetShowCount(EHCJHint::ShieldDown), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
