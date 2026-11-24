#include "Ship/HCJTorpedo.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Ship/HCJShieldFacings.h"

void FHCJTorpedoBay::Reset(int32 StartCount, int32 MaxCount)
{
	Count = FMath::Clamp(StartCount, 0, FMath::Max(MaxCount, 0));
	ReadyTime = 0.0f;
}

bool FHCJTorpedoBay::TryFire(float Now, float ReloadSeconds)
{
	if (!CanFire(Now))
	{
		return false;
	}
	--Count;
	ReadyTime = Now + FMath::Max(ReloadSeconds, 0.0f);
	return true;
}

int32 FHCJTorpedoBay::Restock(int32 Amount, int32 MaxCount)
{
	const int32 Added = FMath::Clamp(Amount, 0, FMath::Max(MaxCount - Count, 0));
	Count += Added;
	return Added;
}

float FHCJTorpedoBay::GetReloadFraction(float Now, float ReloadSeconds) const
{
	if (Count <= 0 || ReloadSeconds <= 0.0f || Now >= ReadyTime)
	{
		return 1.0f;
	}
	return FMath::Clamp(1.0f - (ReadyTime - Now) / ReloadSeconds, 0.0f, 1.0f);
}

bool HCJTorpedo::IsEnabled()
{
#if !UE_BUILD_SHIPPING
	static const bool bDisabled = FParse::Param(FCommandLine::Get(), TEXT("HCJNoTorpedoes"));
	return !bDisabled;
#else
	return true;
#endif
}

bool HCJTorpedo::IsArmed(float DistanceTravelled, float ArmingDistance)
{
	return DistanceTravelled >= ArmingDistance;
}

void HCJTorpedo::SplitDamage(float FacingStrength, float ShieldDamage, float HullDamage, float& OutShieldLoss, float& OutToHull)
{
	const float Strength = FMath::Max(FacingStrength, 0.0f);
	const float Shield = FMath::Max(ShieldDamage, 0.0f);
	const float Hull = FMath::Max(HullDamage, 0.0f);
	OutShieldLoss = FMath::Min(Strength, Shield);
	// The share of the torpedo that the facing stopped; a torpedo that cannot hurt a shield at all is not stopped by one.
	const float Stopped = Shield > UE_KINDA_SMALL_NUMBER ? FMath::Min(Strength / Shield, 1.0f) : 0.0f;
	OutToHull = Hull * (1.0f - Stopped);
}

float HCJTorpedo::BlastScale(float Distance, float InnerRadius, float OuterRadius, float EdgeScale)
{
	const float Inner = FMath::Max(InnerRadius, 0.0f);
	const float Outer = FMath::Max(OuterRadius, Inner);
	if (Distance <= Inner)
	{
		return 1.0f;
	}
	if (Distance > Outer || Outer <= Inner)
	{
		return 0.0f;
	}
	return FMath::Lerp(1.0f, FMath::Clamp(EdgeScale, 0.0f, 1.0f), (Distance - Inner) / (Outer - Inner));
}

FVector HCJTorpedo::TurnTowards(const FVector& Current, const FVector& Wanted, float MaxDegrees)
{
	const FVector From = Current.GetSafeNormal();
	const FVector To = Wanted.GetSafeNormal();
	if (From.IsNearlyZero())
	{
		return To;
	}
	if (To.IsNearlyZero() || MaxDegrees <= 0.0f)
	{
		return From;
	}
	const float MaxStep = FMath::DegreesToRadians(MaxDegrees);
	const float Angle = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(From, To)), -1.0f, 1.0f));
	if (Angle <= MaxStep)
	{
		return To;
	}
	// Exactly opposite: any axis across the flight direction will do.
	FVector Axis = FVector::CrossProduct(From, To);
	if (!Axis.Normalize())
	{
		Axis = FVector::CrossProduct(From, FMath::Abs(From.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	}
	return FQuat(Axis, MaxStep).RotateVector(From).GetSafeNormal();
}

HCJTorpedo::FHoming HCJTorpedo::GetHoming(bool bHumanFirer, float RowTurnRate, float RowConeDeg)
{
	FHoming Homing;
	Homing.TurnRateDeg = RowTurnRate;
	Homing.LockConeDeg = RowConeDeg;
	Homing.KeepConeDeg = RowConeDeg;
	if (bHumanFirer && !IsClassicHoming())
	{
		Homing.TurnRateDeg = FMath::Max(RowTurnRate, HumanTurnRateDeg);
		Homing.AcquireConeDeg = HumanAcquireConeDeg;
		Homing.KeepConeDeg = FMath::Max(RowConeDeg, HumanKeepConeDeg);
		Homing.MaxLeadSeconds = HumanMaxLeadSeconds;
	}
	return Homing;
}

int32 HCJTorpedo::PickTarget(const FVector& Forward, TConstArrayView<FVector> ToCandidates, int32 LockedIndex, const FHoming& Homing, float MaxDistance)
{
	if (ToCandidates.IsValidIndex(LockedIndex) && KeepsLock(Forward, ToCandidates[LockedIndex], Homing.LockConeDeg, MaxDistance))
	{
		return LockedIndex;
	}
	if (Homing.AcquireConeDeg <= 0.0f)
	{
		return INDEX_NONE;
	}
	int32 Best = INDEX_NONE;
	double BestSq = 0.0;
	for (int32 Index = 0; Index < ToCandidates.Num(); ++Index)
	{
		const double DistanceSq = ToCandidates[Index].SizeSquared();
		if (DistanceSq >= 1.0 && KeepsLock(Forward, ToCandidates[Index], Homing.AcquireConeDeg, MaxDistance) && (Best == INDEX_NONE || DistanceSq < BestSq))
		{
			Best = Index;
			BestSq = DistanceSq;
		}
	}
	return Best;
}

FVector HCJTorpedo::SteerPoint(const FVector& TorpedoLocation, const FVector& TargetLocation, const FVector& TargetVelocity, float Speed, float MaxLeadSeconds)
{
	if (MaxLeadSeconds <= 0.0f || Speed <= 0.0f)
	{
		return TargetLocation;
	}
	const float Lead = FMath::Min(static_cast<float>(FVector::Dist(TorpedoLocation, TargetLocation)) / Speed, MaxLeadSeconds);
	return TargetLocation + TargetVelocity * Lead;
}

bool HCJTorpedo::IsClassicHoming()
{
#if !UE_BUILD_SHIPPING
	static const bool bClassic = FParse::Param(FCommandLine::Get(), TEXT("HCJClassicTorpedo"));
	return bClassic;
#else
	return false;
#endif
}

bool HCJTorpedo::KeepsLock(const FVector& Forward, const FVector& ToTarget, float ConeDegrees, float MaxDistance)
{
	const double DistanceSq = ToTarget.SizeSquared();
	if (DistanceSq > FMath::Square(static_cast<double>(MaxDistance)))
	{
		return false;
	}
	if (DistanceSq < 1.0)
	{
		return true;
	}
	const FVector Ahead = Forward.GetSafeNormal();
	const double Cos = FVector::DotProduct(Ahead, ToTarget) / FMath::Sqrt(DistanceSq);
	return Cos >= FMath::Cos(FMath::DegreesToRadians(static_cast<double>(FMath::Clamp(ConeDegrees, 0.0f, 180.0f))));
}

FHCJTorpedoBotSkill HCJTorpedo::GetBotSkill(int32 Difficulty)
{
	// Decisions are made every bot think (0.25 s): 0.04 is about one chance in six per second (twice that on a facing that is down).
	FHCJTorpedoBotSkill Skill;
	switch (FMath::Clamp(Difficulty, 0, 2))
	{
	case 0:
		Skill.WeakFacingChance = 0.04f;
		Skill.OpenerChance = 0.012f;
		Skill.MinSecondsBetween = 10.0f;
		Skill.EvadeChance = 0.3f;
		break;
	case 1:
		Skill.WeakFacingChance = 0.12f;
		Skill.OpenerChance = 0.03f;
		Skill.MinSecondsBetween = 5.0f;
		Skill.EvadeChance = 0.6f;
		break;
	default:
		Skill.WeakFacingChance = 0.3f;
		Skill.OpenerChance = 0.05f;
		Skill.MinSecondsBetween = 3.0f;
		Skill.EvadeChance = 0.9f;
		break;
	}
	return Skill;
}

bool HCJTorpedo::ShouldBotFire(const FHCJTorpedoBotInput& Input, const FHCJTorpedoBotSkill& Skill, float MinRange, float MaxRange)
{
	if (!Input.bReady || !Input.bLockedAndAimed || Input.Distance < MinRange || Input.Distance > MaxRange)
	{
		return false;
	}
	// Nearly dead already: the guns finish it, the torpedo is kept.
	if (Input.HullFraction < 0.15f)
	{
		return false;
	}
	if (Input.FacingFraction <= 0.0f)
	{
		return Input.Roll < 2.0f * Skill.WeakFacingChance;
	}
	if (Input.FacingFraction <= HCJShield::WeakFraction)
	{
		return Input.Roll < Skill.WeakFacingChance;
	}
	// The opener: early in a fight, from medium range, where it has room to arm and the target time to see it.
	return Input.SecondsOnTarget < 4.0f && Input.Distance >= 0.5f * MaxRange && Input.Roll < Skill.OpenerChance;
}
