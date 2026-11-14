#include "Ship/HCJPhaser.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool FHCJPhaserBank::Step(bool bWantsBeam, float DeltaSeconds, float DrainSeconds, float RechargeDelay, float RechargeSeconds, float RelockLevel)
{
	DeltaSeconds = FMath::Max(DeltaSeconds, 0.0f);
	if (bWantsBeam && !bLocked && Charge > 0.0f)
	{
		Charge -= DeltaSeconds / FMath::Max(DrainSeconds, 0.05f);
		IdleSeconds = 0.0f;
		if (Charge <= 0.0f)
		{
			Charge = 0.0f;
			bLocked = true;
		}
		return true;
	}

	IdleSeconds += DeltaSeconds;
	if (IdleSeconds >= RechargeDelay)
	{
		Charge = FMath::Min(Charge + DeltaSeconds / FMath::Max(RechargeSeconds, 0.05f), 1.0f);
	}
	if (bLocked && Charge >= FMath::Clamp(RelockLevel, 0.0f, 1.0f))
	{
		bLocked = false;
	}
	return false;
}

float HCJPhaser::RangeScale(float Distance, float Range, float FullFraction, float EdgeScale)
{
	if (Range <= 0.0f || Distance > Range)
	{
		return 0.0f;
	}
	const float FullRange = Range * FMath::Clamp(FullFraction, 0.0f, 1.0f);
	if (Distance <= FullRange || Range - FullRange <= UE_KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}
	return FMath::Lerp(1.0f, FMath::Clamp(EdgeScale, 0.0f, 1.0f), (Distance - FullRange) / (Range - FullRange));
}

uint8 HCJPhaser::PackLength(float Length, float Range)
{
	if (Range <= 0.0f)
	{
		return 1;
	}
	return static_cast<uint8>(FMath::Clamp(FMath::CeilToInt32(255.0f * Length / Range), 1, 255));
}

float HCJPhaser::UnpackLength(uint8 Packed, float Range)
{
	return Range * static_cast<float>(Packed) / 255.0f;
}

uint8 HCJPhaser::PackCharge(float Charge, bool bLocked)
{
	return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Charge * 127.0f), 0, 127) | (bLocked ? 0x80 : 0));
}

void HCJPhaser::UnpackCharge(uint8 Packed, float& OutCharge, bool& bOutLocked)
{
	OutCharge = static_cast<float>(Packed & 0x7F) / 127.0f;
	bOutLocked = (Packed & 0x80) != 0;
}

int32 HCJPhaser::PickAssistTarget(const FVector& Origin, const FVector& AimDirection, TConstArrayView<FVector> Candidates, int32 LockedIndex,
	float Range, float AssistDegrees, float LockedAssistDegrees)
{
	const FVector Aim = AimDirection.GetSafeNormal();
	if (Aim.IsNearlyZero() || Range <= 0.0f)
	{
		return INDEX_NONE;
	}
	const double MinCos = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(FMath::Clamp(AssistDegrees, 0.0f, 89.0f))));
	const double LockedMinCos = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(FMath::Clamp(LockedAssistDegrees, 0.0f, 89.0f))));

	int32 Best = INDEX_NONE;
	double BestCos = -2.0;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FVector To = Candidates[Index] - Origin;
		const double Distance = To.Size();
		if (Distance > Range || Distance < 1.0)
		{
			continue;
		}
		const double Cos = FVector::DotProduct(To, Aim) / Distance;
		if (Index == LockedIndex)
		{
			if (Cos >= LockedMinCos)
			{
				return Index;
			}
			continue;
		}
		if (Cos >= MinCos && Cos > BestCos)
		{
			BestCos = Cos;
			Best = Index;
		}
	}
	return Best;
}

bool HCJPhaser::IsEnabled()
{
#if UE_BUILD_SHIPPING
	return true;
#else
	static const bool bDisabled = FParse::Param(FCommandLine::Get(), TEXT("HCJNoPhasers"));
	return !bDisabled;
#endif
}
