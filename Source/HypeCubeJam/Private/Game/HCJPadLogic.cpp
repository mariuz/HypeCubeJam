#include "Game/HCJPadLogic.h"

#include "UI/HCJHints.h"

FVector2D HCJPad::ShapeStick(const FVector2D& Raw, float DeadZone, float Exponent)
{
	const double Length = Raw.Size();
	const double Zone = FMath::Clamp(static_cast<double>(DeadZone), 0.0, 0.95);
	if (!FMath::IsFinite(Length) || Length <= Zone || Length <= UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	const double Linear = FMath::Clamp((Length - Zone) / (1.0 - Zone), 0.0, 1.0);
	const double Shaped = FMath::Pow(Linear, FMath::Max(static_cast<double>(Exponent), 0.1));
	return Raw * (Shaped / Length);
}

FVector2D HCJPad::LookDelta(const FVector2D& Raw, float Sensitivity, float DeltaSeconds)
{
	const float Rate = LookDegreesPerSecond * FMath::Clamp(Sensitivity, MinSensitivity, MaxSensitivity);
	const float Seconds = FMath::Clamp(DeltaSeconds, 0.0f, MaxLookFrameSeconds);
	return ShapeStick(Raw, LookDeadZone, LookExponent) * static_cast<double>(Rate * Seconds);
}

bool HCJPad::UpdatePadActive(bool bPadActive, bool bGamepadKey, bool bAxis, float AxisValue)
{
	if (bGamepadKey)
	{
		return (!bAxis || FMath::Abs(AxisValue) >= DeviceAxisThreshold) ? true : bPadActive;
	}
	return (!bAxis || AxisValue != 0.0f) ? false : bPadActive;
}

int32 HCJPad::StepFocus(int32 Index, int32 Num, int32 Delta)
{
	if (Num <= 0)
	{
		return 0;
	}
	const int32 Wrapped = (FMath::Clamp(Index, 0, Num - 1) + Delta) % Num;
	return Wrapped < 0 ? Wrapped + Num : Wrapped;
}

HCJPad::EDirection HCJPad::FStickNav::Update(float X, float Y, double Now)
{
	const float Largest = FMath::Max(FMath::Abs(X), FMath::Abs(Y));
	if (Held != EDirection::None)
	{
		if (Largest < Release)
		{
			Held = EDirection::None;
			return EDirection::None;
		}
		if (Largest >= Engage)
		{
			// A stick rolled from one side to another without passing the centre is a new push.
			const EDirection Wanted = FMath::Abs(Y) >= FMath::Abs(X) ? (Y > 0.0f ? EDirection::Up : EDirection::Down) : (X > 0.0f ? EDirection::Right : EDirection::Left);
			if (Wanted != Held)
			{
				Held = Wanted;
				NextRepeat = Now + FirstRepeat;
				return Held;
			}
		}
		if (Now >= NextRepeat)
		{
			NextRepeat = Now + Repeat;
			return Held;
		}
		return EDirection::None;
	}
	if (Largest < Engage)
	{
		return EDirection::None;
	}
	Held = FMath::Abs(Y) >= FMath::Abs(X) ? (Y > 0.0f ? EDirection::Up : EDirection::Down) : (X > 0.0f ? EDirection::Right : EDirection::Left);
	NextRepeat = Now + FirstRepeat;
	return Held;
}

FString HCJPad::PadNameForKey(const FString& Key)
{
	struct FName2
	{
		const TCHAR* Key;
		const TCHAR* Pad;
	};
	// The layout of AHCJPlayerController::BuildInputObjects, by the names the keyboard prompts use.
	static const FName2 Names[] = {
		{ TEXT("Mouse"), TEXT("R stick") },
		{ TEXT("W"), TEXT("L stick") }, { TEXT("A"), TEXT("L stick") }, { TEXT("S"), TEXT("L stick") }, { TEXT("D"), TEXT("L stick") },
		{ TEXT("Space"), TEXT("A") }, { TEXT("E"), TEXT("A") }, { TEXT("Ctrl"), TEXT("B") }, { TEXT("Q"), TEXT("B") },
		{ TEXT("Shift"), TEXT("LB") },
		{ TEXT("Left mouse"), TEXT("RT") },
		{ TEXT("Right mouse"), TEXT("LT") }, { TEXT("X"), TEXT("LT") },
		{ TEXT("B"), TEXT("X") },
		{ TEXT("T"), TEXT("RB") },
		{ TEXT("F"), TEXT("Y") },
		{ TEXT("G"), TEXT("D-down") },
		{ TEXT("Z"), TEXT("D-up") },
		{ TEXT("C"), TEXT("D-left") },
		{ TEXT("R"), TEXT("D-right") },
		{ TEXT("Tab"), TEXT("Back") },
		{ TEXT("V"), TEXT("R3") },
		{ TEXT("F1"), TEXT("L3") },
		{ TEXT("Esc"), TEXT("Start") },
		{ TEXT("Enter"), TEXT("A") },
	};
	for (const FName2& Name : Names)
	{
		if (Key.Equals(Name.Key, ESearchCase::CaseSensitive))
		{
			return Name.Pad;
		}
	}
	return Key;
}

FString HCJPad::ToPadMarkup(const FString& Markup)
{
	TArray<FHCJHintRun> Runs;
	HCJHints::SplitKeyMarkup(Markup, Runs);

	FString Result;
	FString LastKey;
	// Blanks between two key caps wait here: they are dropped when the second cap repeats the first.
	FString PendingBlank;
	for (const FHCJHintRun& Run : Runs)
	{
		if (Run.bKey)
		{
			const FString Name = PadNameForKey(Run.Text);
			if (!LastKey.IsEmpty() && Name == LastKey)
			{
				PendingBlank.Reset();
				continue;
			}
			Result += PendingBlank;
			PendingBlank.Reset();
			Result += TEXT("[") + Name + TEXT("]");
			LastKey = Name;
		}
		else if (!LastKey.IsEmpty() && Run.Text.TrimStartAndEnd().IsEmpty())
		{
			PendingBlank += Run.Text;
		}
		else
		{
			Result += PendingBlank;
			PendingBlank.Reset();
			Result += Run.Text;
			LastKey.Reset();
		}
	}
	return Result + PendingBlank;
}
