#pragma once

#include "CoreMinimal.h"

/**
 * Game controller rules (plain C++, tested): the shape of the aim stick, the SETTINGS switch, which device was used
 * last, the focus of the menu and the pad names of the key prompts. The bindings themselves are made in
 * AHCJPlayerController::BuildInputObjects; the menu side is SHCJMenu::Navigate.
 *
 * An Xbox pad arrives as XInput. A Steam Controller (2015) has no Windows driver of its own: Steam Input presents it
 * to the game as an XInput pad (or as mouse and keys), so it needs nothing here beyond the XInput bindings.
 */
namespace HCJPad
{
	/** Aim stick: nothing below this deflection; the rest is rescaled to 0..1 so that the edge of the dead zone is 0. */
	inline constexpr float LookDeadZone = 0.2f;
	/** Response curve of the aim stick: deflection to this power (2 = fine aim near the centre, full rate at the rim). */
	inline constexpr float LookExponent = 2.0f;
	/** Degrees of view rotation per second at full deflection and sensitivity 1. */
	inline constexpr float LookDegreesPerSecond = 150.0f;
	/** The frame time the look rate is scaled by is capped here: a hitch must not spin the ship. */
	inline constexpr float MaxLookFrameSeconds = 0.1f;
	/** Thrust stick: dead zone of each axis (Enhanced Input's dead zone modifier). */
	inline constexpr float MoveDeadZone = 0.2f;
	inline constexpr float MinSensitivity = 0.2f;
	inline constexpr float MaxSensitivity = 3.0f;
	/** A stick or trigger must be moved this far to count as "the pad was used" (drift must not flip the prompts). */
	inline constexpr float DeviceAxisThreshold = 0.5f;

	/**
	 * Radial dead zone and response curve: the direction of Raw, with a length of 0 inside the dead zone and
	 * ((|Raw| - DeadZone) / (1 - DeadZone)) ^ Exponent outside it (never above 1; a corner of the square reads as 1).
	 */
	HYPECUBEJAM_API FVector2D ShapeStick(const FVector2D& Raw, float DeadZone, float Exponent);

	/**
	 * Degrees to add to the view this frame (X yaw, Y pitch) for the aim stick at Raw: shaped as above, times the rate,
	 * the sensitivity (clamped to its range) and the frame time (clamped to 0 .. MaxLookFrameSeconds). The mouse adds a
	 * step per count; a stick is a rate, so it is scaled by time and the result does not depend on the frame rate.
	 */
	HYPECUBEJAM_API FVector2D LookDelta(const FVector2D& Raw, float Sensitivity, float DeltaSeconds);

	/** The SETTINGS switch: a gamepad key counts only while the controller is enabled; every other key always. */
	inline bool IsKeyAllowed(bool bGamepadKey, bool bControllerEnabled)
	{
		return !bGamepadKey || bControllerEnabled;
	}

	/**
	 * Which device was used last. bPadActive is the state before this event; the event is a key of a gamepad or not,
	 * an axis (with its value) or a button. Buttons and keys always count; an axis only beyond DeviceAxisThreshold when
	 * it is a pad's (drift), and whenever it moves at all when it is the mouse's.
	 */
	HYPECUBEJAM_API bool UpdatePadActive(bool bPadActive, bool bGamepadKey, bool bAxis, float AxisValue);

	/** Menu focus: Index moved by Delta among Num entries, wrapping round at both ends; 0 when there are none. */
	HYPECUBEJAM_API int32 StepFocus(int32 Index, int32 Num, int32 Delta);

	/** Directions of the menu. */
	enum class EDirection : uint8
	{
		None,
		Up,
		Down,
		Left,
		Right
	};

	/**
	 * The left stick as a D-pad for the menus: a direction once when the stick is pushed past Engage (the larger axis
	 * wins), again after FirstRepeat seconds and then every Repeat seconds while it is held; nothing until it has come
	 * back inside Release.
	 */
	struct HYPECUBEJAM_API FStickNav
	{
		static constexpr float Engage = 0.55f;
		static constexpr float Release = 0.35f;
		static constexpr double FirstRepeat = 0.45;
		static constexpr double Repeat = 0.16;

		/** X right, Y up (as the engine reports the stick), Now in seconds. Returns the step to make now, or None. */
		EDirection Update(float X, float Y, double Now);

	private:
		EDirection Held = EDirection::None;
		double NextRepeat = 0.0;
	};

	/**
	 * The pad name of one key cap of the prompts ("Left mouse" -> "RT", "Shift" -> "LB", "W" -> "L stick", ...), or the
	 * key itself when the pad has nothing for it (H, Middle mouse).
	 */
	HYPECUBEJAM_API FString PadNameForKey(const FString& Key);

	/**
	 * "[W] [A] [S] [D] thrust   [Left mouse] fire" with pad names: "[L stick] thrust   [RT] fire". Key caps that turn
	 * into the same name next to each other (only blanks between them) are shown once. Text outside brackets is kept.
	 */
	HYPECUBEJAM_API FString ToPadMarkup(const FString& Markup);
}
