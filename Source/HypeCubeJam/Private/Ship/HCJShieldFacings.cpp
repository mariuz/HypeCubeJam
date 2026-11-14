#include "Ship/HCJShieldFacings.h"

EHCJShieldFacing HCJShield::SelectFacing(const FVector& LocalDirection)
{
	const double X = LocalDirection.X;
	const double Y = LocalDirection.Y;
	if (FMath::Abs(X) < UE_KINDA_SMALL_NUMBER && FMath::Abs(Y) < UE_KINDA_SMALL_NUMBER)
	{
		return EHCJShieldFacing::Fore;
	}
	if (FMath::Abs(X) >= FMath::Abs(Y))
	{
		return X >= 0.0 ? EHCJShieldFacing::Fore : EHCJShieldFacing::Aft;
	}
	return Y >= 0.0 ? EHCJShieldFacing::Starboard : EHCJShieldFacing::Port;
}

FVector HCJShield::FacingAxis(EHCJShieldFacing Facing)
{
	switch (Facing)
	{
	case EHCJShieldFacing::Aft: return FVector(-1.0, 0.0, 0.0);
	case EHCJShieldFacing::Port: return FVector(0.0, -1.0, 0.0);
	case EHCJShieldFacing::Starboard: return FVector(0.0, 1.0, 0.0);
	default: return FVector(1.0, 0.0, 0.0);
	}
}

float HCJShield::FacingYawDegrees(EHCJShieldFacing Facing)
{
	switch (Facing)
	{
	case EHCJShieldFacing::Aft: return 180.0f;
	case EHCJShieldFacing::Port: return -90.0f;
	case EHCJShieldFacing::Starboard: return 90.0f;
	default: return 0.0f;
	}
}

const TCHAR* HCJShield::FacingName(EHCJShieldFacing Facing)
{
	switch (Facing)
	{
	case EHCJShieldFacing::Fore: return TEXT("FORE");
	case EHCJShieldFacing::Aft: return TEXT("AFT");
	case EHCJShieldFacing::Port: return TEXT("PORT");
	case EHCJShieldFacing::Starboard: return TEXT("STARBOARD");
	default: return TEXT("?");
	}
}

float HCJShield::AbsorbOnFacing(float& Strength, float Damage, bool& bOutCollapsed)
{
	bOutCollapsed = false;
	if (!(Damage > 0.0f))
	{
		return 0.0f;
	}
	const float Before = FMath::Max(Strength, 0.0f);
	const float Taken = FMath::Min(Before, Damage);
	Strength = Before - Taken;
	// No float residue: a facing is either up or exactly down.
	if (Strength < UE_KINDA_SMALL_NUMBER)
	{
		Strength = 0.0f;
	}
	bOutCollapsed = Before > 0.0f && Strength <= 0.0f;
	return Damage - Taken;
}

float HCJShield::AbsorbSpread(TArrayView<float> Strengths, float Damage, uint8& OutCollapsedMask)
{
	OutCollapsedMask = 0;
	float Remaining = FMath::Max(Damage, 0.0f);
	// Each round offers an equal share to every facing that is still up; at most one round per facing is needed.
	for (int32 Round = 0; Round < Strengths.Num() && Remaining > UE_KINDA_SMALL_NUMBER; ++Round)
	{
		int32 NumUp = 0;
		for (const float Strength : Strengths)
		{
			NumUp += Strength > 0.0f ? 1 : 0;
		}
		if (NumUp == 0)
		{
			break;
		}
		const float Share = Remaining / NumUp;
		for (int32 Index = 0; Index < Strengths.Num(); ++Index)
		{
			if (Strengths[Index] > 0.0f)
			{
				bool bCollapsed = false;
				const float Rest = AbsorbOnFacing(Strengths[Index], Share, bCollapsed);
				Remaining -= Share - Rest;
				if (bCollapsed && Index < 8)
				{
					OutCollapsedMask |= static_cast<uint8>(1 << Index);
				}
			}
		}
	}
	return Remaining > UE_KINDA_SMALL_NUMBER ? Remaining : 0.0f;
}

float HCJShield::AddSpread(TArrayView<float> Strengths, float Amount, float MaxPerFacing)
{
	float Remaining = FMath::Max(Amount, 0.0f);
	float Added = 0.0f;
	// Water filling: raise the lowest facings together until the amount is used up or all are at the limit.
	for (int32 Round = 0; Round <= Strengths.Num() && Remaining > UE_KINDA_SMALL_NUMBER; ++Round)
	{
		float Lowest = MaxPerFacing;
		for (const float Strength : Strengths)
		{
			Lowest = FMath::Min(Lowest, Strength);
		}
		if (Lowest >= MaxPerFacing)
		{
			break;
		}
		// The next level above the lowest one (or the limit), and how many facings sit at the lowest.
		float Next = MaxPerFacing;
		int32 NumLowest = 0;
		for (const float Strength : Strengths)
		{
			if (Strength <= Lowest + UE_KINDA_SMALL_NUMBER)
			{
				++NumLowest;
			}
			else if (Strength < Next)
			{
				Next = Strength;
			}
		}
		const float Rise = FMath::Min(Next - Lowest, Remaining / NumLowest);
		for (float& Strength : Strengths)
		{
			if (Strength <= Lowest + UE_KINDA_SMALL_NUMBER)
			{
				Strength = FMath::Min(Lowest + Rise, MaxPerFacing);
			}
		}
		Remaining -= Rise * NumLowest;
		Added += Rise * NumLowest;
	}
	return Added;
}

uint8 HCJShield::Quantise(float Strength, float FacingMax)
{
	if (!(Strength > 0.0f) || !(FacingMax > 0.0f))
	{
		return 0;
	}
	const float Step = FacingMax * QuantRange / 255.0f;
	return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Strength / Step), 1, 255));
}

float HCJShield::Dequantise(uint8 Value, float FacingMax)
{
	return Value * (FMath::Max(FacingMax, 0.0f) * QuantRange / 255.0f);
}

uint32 HCJShield::Pack(TConstArrayView<float> Strengths, float FacingMax)
{
	uint32 Packed = 0;
	for (int32 Index = 0; Index < Strengths.Num() && Index < 4; ++Index)
	{
		Packed |= static_cast<uint32>(Quantise(Strengths[Index], FacingMax)) << (8 * Index);
	}
	return Packed;
}

void HCJShield::Unpack(uint32 Packed, float FacingMax, TArrayView<float> OutStrengths)
{
	for (int32 Index = 0; Index < OutStrengths.Num(); ++Index)
	{
		OutStrengths[Index] = Index < 4 ? Dequantise(static_cast<uint8>((Packed >> (8 * Index)) & 0xFF), FacingMax) : 0.0f;
	}
}

uint16 HCJShield::PackDirection(const FVector& Direction)
{
	const FRotator Rotation = Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector).Rotation();
	const uint32 Yaw = static_cast<uint32>(FMath::RoundToInt32(FRotator::ClampAxis(Rotation.Yaw) / 360.0 * 256.0)) & 0xFF;
	const uint32 Pitch = static_cast<uint32>(FMath::Clamp(FMath::RoundToInt32((FRotator::NormalizeAxis(Rotation.Pitch) + 90.0) / 180.0 * 255.0), 0, 255));
	return static_cast<uint16>(Yaw | (Pitch << 8));
}

FVector HCJShield::UnpackDirection(uint16 Packed)
{
	const double Yaw = (Packed & 0xFF) * 360.0 / 256.0;
	const double Pitch = ((Packed >> 8) & 0xFF) * 180.0 / 255.0 - 90.0;
	return FRotator(Pitch, Yaw, 0.0).Vector();
}

FLinearColor HCJShield::StrengthColor(float Fraction)
{
	const FLinearColor Weak(1.0f, 0.42f, 0.08f);
	const FLinearColor Healthy(0.55f, 0.85f, 1.0f);
	return FMath::Lerp(Weak, Healthy, FMath::SmoothStep(0.15f, 0.75f, Fraction));
}

EHCJShieldFacing HCJShield::PickFacingToPresent(TConstArrayView<float> Fractions, EHCJShieldFacing Current, float MinGain)
{
	const int32 CurrentIndex = static_cast<int32>(Current);
	if (!Fractions.IsValidIndex(CurrentIndex) || Fractions[CurrentIndex] > WeakFraction)
	{
		return Current;
	}
	// Fore / aft and port / starboard are opposite pairs (0-1, 2-3): a neighbouring facing is a quarter turn away,
	// the opposite one a half turn, so a neighbour wins against an opposite facing that is only a little stronger.
	constexpr float NeighbourBonus = 0.1f;
	const int32 Opposite = CurrentIndex ^ 1;
	int32 Best = CurrentIndex;
	float BestScore = Fractions[CurrentIndex];
	for (int32 Index = 0; Index < Fractions.Num() && Index < NumFacings; ++Index)
	{
		const float Score = Fractions[Index] + ((Index != CurrentIndex && Index != Opposite) ? NeighbourBonus : 0.0f);
		if (Score > BestScore)
		{
			Best = Index;
			BestScore = Score;
		}
	}
	return Fractions[Best] >= Fractions[CurrentIndex] + MinGain ? static_cast<EHCJShieldFacing>(Best) : Current;
}
