#include "Effects/HCJEffectsLogic.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

float HCJShockwave::ImpulseSpeed(float Distance, float Radius, float PeakSpeed)
{
	if (Radius <= 0.0f || PeakSpeed <= 0.0f || Distance >= Radius)
	{
		return 0.0f;
	}
	const float Share = 1.0f - FMath::Max(Distance, 0.0f) / Radius;
	return PeakSpeed * Share * Share;
}

FVector HCJShockwave::ComputeImpulse(const FVector& Origin, const FVector& Location, float Radius, float PeakSpeed)
{
	const FVector Away = Location - Origin;
	const double Distance = Away.Size();
	const float Speed = ImpulseSpeed(static_cast<float>(Distance), Radius, PeakSpeed);
	if (Speed <= 0.0f)
	{
		return FVector::ZeroVector;
	}
	return (Distance > 1.0 ? Away / Distance : FVector::ForwardVector) * Speed;
}

bool HCJShockwave::IsEnabled()
{
#if UE_BUILD_SHIPPING
	return true;
#else
	static const bool bDisabled = FParse::Param(FCommandLine::Get(), TEXT("HCJNoShockwaves"));
	return !bDisabled;
#endif
}

void FHCJDebrisPool::SetCapacity(int32 InCapacity)
{
	Pieces.Reset();
	Pieces.SetNum(FMath::Max(InCapacity, 0));
	ActiveCount = 0;
}

int32 FHCJDebrisPool::Spawn(const FHCJDebrisPiece& Piece)
{
	if (Pieces.Num() == 0)
	{
		return INDEX_NONE;
	}
	int32 Slot = INDEX_NONE;
	float OldestLeft = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		if (!Pieces[Index].bActive)
		{
			Slot = Index;
			break;
		}
		// All taken so far: the piece with the least life left goes.
		const float Left = Pieces[Index].Life - Pieces[Index].Age;
		if (Left < OldestLeft)
		{
			OldestLeft = Left;
			Slot = Index;
		}
	}
	if (!Pieces[Slot].bActive)
	{
		++ActiveCount;
	}
	Pieces[Slot] = Piece;
	Pieces[Slot].bActive = true;
	Pieces[Slot].Age = 0.0f;
	Pieces[Slot].Life = FMath::Max(Piece.Life, 0.05f);
	return Slot;
}

int32 FHCJDebrisPool::Step(float DeltaSeconds, float Drag)
{
	DeltaSeconds = FMath::Max(DeltaSeconds, 0.0f);
	const float Keep = FMath::Clamp(1.0f - Drag * DeltaSeconds, 0.0f, 1.0f);
	ActiveCount = 0;
	for (FHCJDebrisPiece& Piece : Pieces)
	{
		if (!Piece.bActive)
		{
			continue;
		}
		Piece.Age += DeltaSeconds;
		if (Piece.Age >= Piece.Life)
		{
			Piece.bActive = false;
			continue;
		}
		Piece.Location += Piece.Velocity * DeltaSeconds;
		Piece.Velocity *= Keep;
		Piece.Angle = FMath::Fmod(Piece.Angle + Piece.AngularSpeed * DeltaSeconds, 360.0f);
		++ActiveCount;
	}
	return ActiveCount;
}

void FHCJDebrisPool::ApplyShockwave(const FVector& Origin, float Radius, float PeakSpeed)
{
	for (FHCJDebrisPiece& Piece : Pieces)
	{
		if (Piece.bActive)
		{
			Piece.Velocity += HCJShockwave::ComputeImpulse(Origin, Piece.Location, Radius, PeakSpeed);
		}
	}
}

float FHCJDebrisPool::SizeFactor(float Age, float Life)
{
	if (Life <= 0.0f || Age >= Life)
	{
		return 0.0f;
	}
	const float FadeStart = Life * (2.0f / 3.0f);
	return Age <= FadeStart ? 1.0f : FMath::Clamp((Life - Age) / (Life - FadeStart), 0.0f, 1.0f);
}

float HCJEffects::ShakeAt(float Distance, float MaxDistance, float Strength)
{
	if (MaxDistance <= 0.0f || Strength <= 0.0f || Distance >= MaxDistance)
	{
		return 0.0f;
	}
	const float Near = 0.1f * MaxDistance;
	return Distance <= Near ? Strength : Strength * (MaxDistance - Distance) / (MaxDistance - Near);
}

int32 HCJEffects::ScaleCount(int32 BaseCount, float Radius, float ReferenceRadius, int32 MinCount, int32 MaxCount)
{
	if (BaseCount <= 0 || ReferenceRadius <= 0.0f)
	{
		return 0;
	}
	return FMath::Clamp(FMath::RoundToInt32(BaseCount * Radius / ReferenceRadius), MinCount, FMath::Max(MaxCount, MinCount));
}

int32 HCJEffects::PickBlastDetail(float Distance, bool bOwnShip, int32 NumRunning, float FullDistance, float MediumDistance)
{
	if (bOwnShip)
	{
		return BlastFull;
	}
	int32 Detail = Distance <= FullDistance ? BlastFull : Distance <= MediumDistance ? BlastMedium : BlastLow;
	if (NumRunning >= 4)
	{
		Detail = Distance <= FullDistance ? BlastMedium : BlastLow;
	}
	else if (NumRunning >= 2 && Distance > 0.4f * FullDistance)
	{
		Detail = FMath::Max(Detail, static_cast<int32>(BlastMedium));
	}
	return Detail;
}

float HCJEffects::BlastStageTime(int32 Stage, float FailSeconds)
{
	FailSeconds = FMath::Max(FailSeconds, 0.0f);
	switch (Stage)
	{
	case 0: return 0.0f;
	case 1: return 0.34f * FailSeconds;
	case 2: return 0.68f * FailSeconds;
	case 3: return FailSeconds;
	case 4: return FailSeconds + 0.07f;
	case 5: return FailSeconds + 0.15f;
	case 6: return FailSeconds + 0.27f;
	default: return TNumericLimits<float>::Max();
	}
}

float HCJEffects::PlateHeat(float Age)
{
	const float Left = FMath::Clamp(1.0f - Age / 1.6f, 0.0f, 1.0f);
	return Left * Left;
}
