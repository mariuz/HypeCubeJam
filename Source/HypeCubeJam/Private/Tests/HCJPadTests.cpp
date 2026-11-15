#include "Tests/HCJTestHelpers.h"

#include "Game/HCJPadLogic.h"
#include "Game/HCJUserSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJPadTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/** The aim stick: radial dead zone, rescaled so that its edge is zero, squared response, a rate scaled by the frame time. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPadStickTest, "HypeCubeJam.Input.PadStickDeadZoneAndCurve", HCJPadTestFlags)

bool FHCJPadStickTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	// Inside the dead zone, and exactly on its edge: nothing, in every direction.
	for (int32 Step = 0; Step < 72; ++Step)
	{
		const double Angle = Step * UE_DOUBLE_PI / 36.0;
		const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
		for (const double Length : { 0.0, 0.05, 0.19, 0.2 })
		{
			const FVector2D Shaped = HCJPad::ShapeStick(Direction * Length, HCJPad::LookDeadZone, HCJPad::LookExponent);
			HCJ_TEST_CHECK(Shaped.IsZero(), TEXT("deflection %.2f at %.0f deg is inside the dead zone but gives (%f, %f)"), Length, FMath::RadiansToDegrees(Angle), Shaped.X, Shaped.Y);
		}
		// Outside: the direction is kept, the length is ((l - 0.2) / 0.8) ^ 2 and grows with the deflection.
		double Previous = 0.0;
		for (const double Length : { 0.25, 0.4, 0.6, 0.8, 1.0 })
		{
			const FVector2D Shaped = HCJPad::ShapeStick(Direction * Length, HCJPad::LookDeadZone, HCJPad::LookExponent);
			const double Expected = FMath::Square((Length - 0.2) / 0.8);
			HCJ_TEST_CHECK(FMath::IsNearlyEqual(Shaped.Size(), Expected, 1.e-6), TEXT("deflection %.2f gives %f, expected %f"), Length, Shaped.Size(), Expected);
			HCJ_TEST_CHECK(Shaped.Size() > Previous, TEXT("the response does not grow at deflection %.2f (%f after %f)"), Length, Shaped.Size(), Previous);
			HCJ_TEST_CHECK(FVector2D::DotProduct(Shaped.GetSafeNormal(), Direction) > 0.999999, TEXT("the direction changed at %.0f deg, deflection %.2f"), FMath::RadiansToDegrees(Angle), Length);
			Previous = Shaped.Size();
		}
	}

	// The corner of the square (both axes at 1) is full deflection, not more; half deflection is well under half the rate.
	TestTrue(TEXT("a corner of the stick's square reads as 1"), FMath::IsNearlyEqual(HCJPad::ShapeStick(FVector2D(1.0, 1.0), 0.2f, 2.0f).Size(), 1.0, 1.e-6));
	TestTrue(TEXT("half deflection gives fine aim (below 0.2 of the full rate)"), HCJPad::ShapeStick(FVector2D(0.5, 0.0), 0.2f, 2.0f).X < 0.2);
	TestTrue(TEXT("a linear curve is linear"), FMath::IsNearlyEqual(HCJPad::ShapeStick(FVector2D(0.6, 0.0), 0.2f, 1.0f).X, 0.5, 1.e-6));
	TestTrue(TEXT("left is negative"), HCJPad::ShapeStick(FVector2D(-1.0, 0.0), 0.2f, 2.0f).X < -0.999);
	TestTrue(TEXT("a NaN from a driver gives nothing"), HCJPad::ShapeStick(FVector2D(NAN, 0.0), 0.2f, 2.0f).IsZero());

	// Look: degrees a second at full deflection, times the frame time - the same turn in a second at any frame rate.
	const FVector2D Full(1.0, 0.0);
	const FVector2D At60 = HCJPad::LookDelta(Full, 1.0f, 1.0f / 60.0f);
	const FVector2D At30 = HCJPad::LookDelta(Full, 1.0f, 1.0f / 30.0f);
	TestTrue(TEXT("full deflection for one frame at 60 fps is the rate / 60"), FMath::IsNearlyEqual(At60.X, HCJPad::LookDegreesPerSecond / 60.0, 1.e-4));
	TestTrue(TEXT("60 frames at 60 fps turn as far as 30 frames at 30 fps"), FMath::IsNearlyEqual(At60.X * 60.0, At30.X * 30.0, 1.e-3));
	TestTrue(TEXT("no pitch from a sideways stick"), At60.Y == 0.0);
	TestTrue(TEXT("stick up is pitch up (positive, as the mouse)"), HCJPad::LookDelta(FVector2D(0.0, 1.0), 1.0f, 0.016f).Y > 0.0);
	TestTrue(TEXT("the dead zone gives no turn"), HCJPad::LookDelta(FVector2D(0.15, -0.1), 3.0f, 0.016f).IsZero());
	TestTrue(TEXT("sensitivity 2 turns twice as fast"), FMath::IsNearlyEqual(HCJPad::LookDelta(Full, 2.0f, 0.01f).X, 2.0 * HCJPad::LookDelta(Full, 1.0f, 0.01f).X, 1.e-4));
	TestTrue(TEXT("sensitivity is clamped at the top"), HCJPad::LookDelta(Full, 100.0f, 0.01f).X == HCJPad::LookDelta(Full, HCJPad::MaxSensitivity, 0.01f).X);
	TestTrue(TEXT("sensitivity is clamped at the bottom"), HCJPad::LookDelta(Full, 0.0f, 0.01f).X == HCJPad::LookDelta(Full, HCJPad::MinSensitivity, 0.01f).X);
	TestTrue(TEXT("a hitch of two seconds turns no further than the longest frame allows"), HCJPad::LookDelta(Full, 1.0f, 2.0f).X == HCJPad::LookDelta(Full, 1.0f, HCJPad::MaxLookFrameSeconds).X);
	TestTrue(TEXT("a negative frame time turns nothing"), HCJPad::LookDelta(Full, 1.0f, -1.0f).IsZero());

	HCJ_TEST_FINISH();
}

/** The SETTINGS switch, the sensitivity range, and which device counts as the one in use. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPadSettingTest, "HypeCubeJam.Input.PadSettingAndDevice", HCJPadTestFlags)

bool FHCJPadSettingTest::RunTest(const FString& Parameters)
{
	// The switch: only gamepad keys depend on it.
	TestTrue(TEXT("a pad key counts while the controller is on"), HCJPad::IsKeyAllowed(true, true));
	TestFalse(TEXT("a pad key does not count while it is off"), HCJPad::IsKeyAllowed(true, false));
	TestTrue(TEXT("the keyboard counts with the controller on"), HCJPad::IsKeyAllowed(false, true));
	TestTrue(TEXT("the keyboard counts with the controller off"), HCJPad::IsKeyAllowed(false, false));

	// A hand-edited settings file cannot break the slider.
	UHCJUserSettings* Settings = NewObject<UHCJUserSettings>();
	Settings->ControllerSensitivity = 99.0f;
	Settings->Sanitize();
	TestEqual(TEXT("sensitivity above the range"), Settings->ControllerSensitivity, HCJPad::MaxSensitivity);
	Settings->ControllerSensitivity = -4.0f;
	Settings->Sanitize();
	TestEqual(TEXT("sensitivity below the range"), Settings->ControllerSensitivity, HCJPad::MinSensitivity);
	Settings->ControllerSensitivity = 1.25f;
	Settings->Sanitize();
	TestEqual(TEXT("sensitivity inside the range is kept"), Settings->ControllerSensitivity, 1.25f);

	// The device in use: buttons and keys always; a pad axis only when it is really pushed; any mouse movement.
	TestTrue(TEXT("a pad button makes the pad the device"), HCJPad::UpdatePadActive(false, true, false, 1.0f));
	TestTrue(TEXT("a pad button's release as well"), HCJPad::UpdatePadActive(false, true, false, 0.0f));
	TestTrue(TEXT("a stick pushed far"), HCJPad::UpdatePadActive(false, true, true, -0.8f));
	TestFalse(TEXT("stick drift does not take over from the keyboard"), HCJPad::UpdatePadActive(false, true, true, 0.12f));
	TestTrue(TEXT("stick drift does not end the pad's turn either"), HCJPad::UpdatePadActive(true, true, true, 0.0f));
	TestFalse(TEXT("a key of the keyboard"), HCJPad::UpdatePadActive(true, false, false, 1.0f));
	TestFalse(TEXT("the mouse moved"), HCJPad::UpdatePadActive(true, false, true, 2.0f));
	TestTrue(TEXT("a mouse axis that did not move changes nothing"), HCJPad::UpdatePadActive(true, false, true, 0.0f));
	return true;
}

/** The focus of the menu: wrap-round steps, and the left stick as a D-pad with a repeat. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPadMenuFocusTest, "HypeCubeJam.Input.PadMenuFocus", HCJPadTestFlags)

bool FHCJPadMenuFocusTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	TestEqual(TEXT("down from the first"), HCJPad::StepFocus(0, 5, +1), 1);
	TestEqual(TEXT("down from the last wraps to the first"), HCJPad::StepFocus(4, 5, +1), 0);
	TestEqual(TEXT("up from the first wraps to the last"), HCJPad::StepFocus(0, 5, -1), 4);
	TestEqual(TEXT("a page with one entry stays on it"), HCJPad::StepFocus(0, 1, -1), 0);
	TestEqual(TEXT("a page with no entry"), HCJPad::StepFocus(3, 0, +1), 0);
	TestEqual(TEXT("an index beyond the list is clamped first"), HCJPad::StepFocus(99, 5, +1), 0);
	for (int32 Num = 1; Num <= 20; ++Num)
	{
		// Num steps in either direction come back to the start and visit every entry once.
		for (const int32 Delta : { +1, -1 })
		{
			int32 Index = 0;
			uint32 Seen = 0;
			for (int32 Step = 0; Step < Num; ++Step)
			{
				Seen |= 1u << Index;
				Index = HCJPad::StepFocus(Index, Num, Delta);
				HCJ_TEST_CHECK(Index >= 0 && Index < Num, TEXT("focus %d outside a list of %d"), Index, Num);
			}
			HCJ_TEST_CHECK(Index == 0 && Seen == (Num == 32 ? MAX_uint32 : (1u << Num) - 1u), TEXT("%d steps of %d over %d entries end at %d having seen %08X"), Num, Delta, Num, Index, Seen);
		}
	}

	using HCJPad::EDirection;
	HCJPad::FStickNav Nav;
	TestTrue(TEXT("a stick at rest steps nothing"), Nav.Update(0.0f, 0.0f, 0.0) == EDirection::None);
	TestTrue(TEXT("a stick inside the threshold steps nothing"), Nav.Update(0.0f, 0.4f, 0.1) == EDirection::None);
	TestTrue(TEXT("pushed up: one step up"), Nav.Update(0.1f, 0.9f, 0.2) == EDirection::Up);
	TestTrue(TEXT("still held: no second step at once"), Nav.Update(0.1f, 0.9f, 0.25) == EDirection::None);
	TestTrue(TEXT("held past the first repeat: a second step"), Nav.Update(0.1f, 0.9f, 0.2 + HCJPad::FStickNav::FirstRepeat + 0.01) == EDirection::Up);
	TestTrue(TEXT("no third step straight after"), Nav.Update(0.1f, 0.9f, 0.2 + HCJPad::FStickNav::FirstRepeat + 0.05) == EDirection::None);
	TestTrue(TEXT("then at the repeat rate"), Nav.Update(0.1f, 0.9f, 0.2 + HCJPad::FStickNav::FirstRepeat + 0.01 + HCJPad::FStickNav::Repeat + 0.01) == EDirection::Up);
	TestTrue(TEXT("let go"), Nav.Update(0.0f, 0.0f, 5.0) == EDirection::None);
	TestTrue(TEXT("pushed down after letting go: at once"), Nav.Update(0.0f, -1.0f, 5.01) == EDirection::Down);
	TestTrue(TEXT("rolled to the right without the centre: a step right"), Nav.Update(1.0f, -0.2f, 5.02) == EDirection::Right);
	TestTrue(TEXT("let go again"), Nav.Update(0.0f, 0.1f, 5.03) == EDirection::None);
	TestTrue(TEXT("left"), Nav.Update(-0.8f, 0.1f, 5.04) == EDirection::Left);

	// Held for two seconds: the first step, then about (2 - FirstRepeat) / Repeat more.
	HCJPad::FStickNav Held;
	int32 NumSteps = 0;
	for (int32 Frame = 0; Frame <= 120; ++Frame)
	{
		NumSteps += Held.Update(0.0f, 1.0f, Frame / 60.0) == EDirection::Up ? 1 : 0;
	}
	const int32 Expected = 1 + FMath::FloorToInt32((2.0 - HCJPad::FStickNav::FirstRepeat) / HCJPad::FStickNav::Repeat);
	HCJ_TEST_CHECK(FMath::Abs(NumSteps - Expected) <= 2, TEXT("a stick held for two seconds made %d steps, expected about %d"), NumSteps, Expected);

	HCJ_TEST_FINISH();
}

/** Key prompts with pad names: every key of the layout, repeated caps shown once, plain text untouched. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPadPromptTest, "HypeCubeJam.Input.PadPrompts", HCJPadTestFlags)

bool FHCJPadPromptTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("fire"), HCJPad::PadNameForKey(TEXT("Left mouse")), FString(TEXT("RT")));
	TestEqual(TEXT("torpedo"), HCJPad::PadNameForKey(TEXT("Right mouse")), FString(TEXT("LT")));
	TestEqual(TEXT("boost"), HCJPad::PadNameForKey(TEXT("Shift")), FString(TEXT("LB")));
	TestEqual(TEXT("warp"), HCJPad::PadNameForKey(TEXT("Z")), FString(TEXT("D-up")));
	TestEqual(TEXT("menu"), HCJPad::PadNameForKey(TEXT("Esc")), FString(TEXT("Start")));
	TestEqual(TEXT("a key the pad has nothing for stays"), HCJPad::PadNameForKey(TEXT("Middle mouse")), FString(TEXT("Middle mouse")));
	TestEqual(TEXT("hints stay on the keyboard"), HCJPad::PadNameForKey(TEXT("H")), FString(TEXT("H")));

	TestEqual(TEXT("the four thrust keys are one stick"), HCJPad::ToPadMarkup(TEXT("[W] [A] [S] [D] thrust     [Space] [Ctrl] up / down     [Mouse] steer")),
		FString(TEXT("[L stick] thrust     [A] [B] up / down     [R stick] steer")));
	TestEqual(TEXT("a key in the middle of a sentence"), HCJPad::ToPadMarkup(TEXT("Hold [Left mouse]: the PHASER beam hits at once")), FString(TEXT("Hold [RT]: the PHASER beam hits at once")));
	TestEqual(TEXT("the same pad button twice with words between is shown twice"), HCJPad::ToPadMarkup(TEXT("[Right mouse] fires, also [X]")), FString(TEXT("[LT] fires, also [LT]")));
	TestEqual(TEXT("the base weapon switch"), HCJPad::ToPadMarkup(TEXT("[B] switch to blaster")), FString(TEXT("[X] switch to blaster")));
	TestEqual(TEXT("system rows"), HCJPad::ToPadMarkup(TEXT("[R] TRACTOR")), FString(TEXT("[D-right] TRACTOR")));
	TestEqual(TEXT("text without keys is untouched"), HCJPad::ToPadMarkup(TEXT("First to 20 kills wins.")), FString(TEXT("First to 20 kills wins.")));
	TestEqual(TEXT("an unclosed bracket is plain text"), HCJPad::ToPadMarkup(TEXT("25 % [unclosed")), FString(TEXT("25 % [unclosed")));
	TestEqual(TEXT("empty"), HCJPad::ToPadMarkup(FString()), FString());
	// A pad name must not be translated again by accident when a prompt is already in pad names... it would be:
	// that is why the HUD draws such text with bKeyMarkupAsIs. This documents it.
	TestEqual(TEXT("pad names are keyboard letters too"), HCJPad::ToPadMarkup(TEXT("[A]")), FString(TEXT("[L stick]")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
