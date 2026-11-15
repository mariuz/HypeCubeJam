#include "Ship/HCJShipSystems.h"

// ---------------------------------------------------------------------------------------------------------------------
// Warp drive
// ---------------------------------------------------------------------------------------------------------------------

void FHCJWarpDrive::EnterCooldown(float Seconds, bool bFromActive)
{
	bJustExited = bFromActive;
	Phase = EHCJWarpPhase::Cooldown;
	Time = FMath::Max(Seconds, 0.0f);
	bNeedsRelease = true;
}

bool FHCJWarpDrive::Step(bool bWants, bool bBlocked, float DeltaSeconds, const FHCJWarpParams& Params)
{
	bJustEntered = false;
	bJustExited = false;
	if (!bWants)
	{
		bNeedsRelease = false;
	}

	switch (Phase)
	{
	case EHCJWarpPhase::Idle:
		if (bWants && !bBlocked && !bNeedsRelease)
		{
			Phase = EHCJWarpPhase::Charging;
			Time = DeltaSeconds;
		}
		return false;

	case EHCJWarpPhase::Charging:
		if (!bWants || bBlocked)
		{
			EnterCooldown(Params.AbortCooldown, false);
			return false;
		}
		if (Time >= Params.ChargeSeconds)
		{
			Phase = EHCJWarpPhase::Active;
			Time = DeltaSeconds;
			bJustEntered = true;
			return true;
		}
		Time += DeltaSeconds;
		return false;

	case EHCJWarpPhase::Active:
		if (!bWants || bBlocked || Time >= Params.MaxSeconds)
		{
			EnterCooldown(Params.Cooldown, true);
			return false;
		}
		Time += DeltaSeconds;
		return true;

	case EHCJWarpPhase::Cooldown:
	default:
		Time -= DeltaSeconds;
		if (Time <= 0.0f)
		{
			Phase = EHCJWarpPhase::Idle;
			Time = 0.0f;
		}
		return false;
	}
}

bool FHCJWarpDrive::StepFollow(bool bWants, bool bClientActive, bool bBlocked, float DeltaSeconds, const FHCJWarpParams& Params, float Tolerance)
{
	bJustEntered = false;
	bJustExited = false;
	if (!bWants)
	{
		bNeedsRelease = false;
	}

	if (bClientActive && !bBlocked)
	{
		if (Phase == EHCJWarpPhase::Charging && Time >= Params.ChargeSeconds - Tolerance)
		{
			// The client's charge is complete; the server's is too, give or take a frame.
			Phase = EHCJWarpPhase::Active;
			Time = 0.0f;
			bJustEntered = true;
		}
		if (Phase == EHCJWarpPhase::Active)
		{
			if (Time < Params.MaxSeconds + Tolerance)
			{
				Time += DeltaSeconds;
				return true;
			}
			EnterCooldown(Params.Cooldown, true);
			return false;
		}
		// No charge, or cooling down: refused. Falls through to the ordinary bookkeeping.
	}

	switch (Phase)
	{
	case EHCJWarpPhase::Idle:
		if (bWants && !bBlocked && !bNeedsRelease)
		{
			Phase = EHCJWarpPhase::Charging;
			Time = DeltaSeconds;
		}
		return false;

	case EHCJWarpPhase::Charging:
		if (!bWants || bBlocked)
		{
			EnterCooldown(Params.AbortCooldown, false);
			return false;
		}
		// Charged and waiting for the client's word: the time stands at "complete".
		Time = FMath::Min(Time + DeltaSeconds, Params.ChargeSeconds + Tolerance);
		return false;

	case EHCJWarpPhase::Active:
		// The client's jump is over (or blocked here).
		EnterCooldown(Params.Cooldown, true);
		return false;

	case EHCJWarpPhase::Cooldown:
	default:
		Time -= DeltaSeconds;
		if (Time <= 0.0f)
		{
			Phase = EHCJWarpPhase::Idle;
			Time = 0.0f;
		}
		return false;
	}
}

void FHCJWarpDrive::Cancel(float CooldownSeconds)
{
	if (IsBusy())
	{
		const bool bWasActive = Phase == EHCJWarpPhase::Active;
		EnterCooldown(CooldownSeconds, bWasActive);
	}
}

float FHCJWarpDrive::GetChargeFraction(const FHCJWarpParams& Params) const
{
	return Phase == EHCJWarpPhase::Charging ? FMath::Clamp(Time / FMath::Max(Params.ChargeSeconds, 0.01f), 0.0f, 1.0f) : 0.0f;
}

float FHCJWarpDrive::GetEnergyFraction(const FHCJWarpParams& Params) const
{
	return Phase == EHCJWarpPhase::Active ? FMath::Clamp(1.0f - Time / FMath::Max(Params.MaxSeconds, 0.01f), 0.0f, 1.0f) : 0.0f;
}

float FHCJWarpDrive::GetCooldownFraction(const FHCJWarpParams& Params) const
{
	return Phase == EHCJWarpPhase::Cooldown ? FMath::Clamp(Time / FMath::Max(Params.Cooldown, 0.01f), 0.0f, 1.0f) : 0.0f;
}

bool HCJWarp::ShouldDropOutAtEdge(const FVector& LocalPosition, const FVector& Direction, float EdgeRadius)
{
	return LocalPosition.SizeSquared() > FMath::Square(static_cast<double>(EdgeRadius)) && FVector::DotProduct(LocalPosition, Direction) > 0.0;
}

bool HCJWarp::IsWorthWarping(float Distance, const FHCJWarpParams& Params)
{
	return Distance >= Params.Speed * 1.5f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Cloaking device
// ---------------------------------------------------------------------------------------------------------------------

bool FHCJCloakDevice::Step(bool bWants, float DeltaSeconds, float DrainSeconds, float RechargeSeconds, float RelockLevel, bool bFreeDrain)
{
	Energy = FMath::Clamp(Energy, 0.0f, 1.0f);
	RevealSeconds = FMath::Max(RevealSeconds - DeltaSeconds, 0.0f);
	if (bLocked && Energy >= RelockLevel)
	{
		bLocked = false;
	}

	if (bCloaked && !bWants)
	{
		bCloaked = false;
	}
	else if (!bCloaked && bWants && CanEngage())
	{
		bCloaked = true;
	}

	if (bCloaked)
	{
		if (!bFreeDrain)
		{
			Energy = FMath::Max(0.0f, Energy - DeltaSeconds / FMath::Max(DrainSeconds, 0.1f));
		}
		if (Energy <= 0.0f)
		{
			bCloaked = false;
			bLocked = true;
		}
	}
	else
	{
		Energy = FMath::Min(1.0f, Energy + DeltaSeconds / FMath::Max(RechargeSeconds, 0.1f));
	}
	if (!bCloaked)
	{
		RevealSeconds = 0.0f;
	}
	return bCloaked;
}

bool FHCJCloakDevice::Refill()
{
	if (Energy >= 0.95f && !bLocked)
	{
		return false;
	}
	Energy = 1.0f;
	bLocked = false;
	return true;
}

bool HCJCloak::CanFire(bool bCloaked, float SecondsSinceDecloak)
{
	return !bCloaked && SecondsSinceDecloak >= WeaponDelay;
}

uint8 HCJCloak::PackEnergy(float Energy, bool bLocked)
{
	const int32 Steps = FMath::Clamp(FMath::RoundToInt32(FMath::Clamp(Energy, 0.0f, 1.0f) * 127.0f), 0, 127);
	return static_cast<uint8>(Steps | (bLocked ? 0x80 : 0));
}

void HCJCloak::UnpackEnergy(uint8 Packed, float& OutEnergy, bool& bOutLocked)
{
	OutEnergy = static_cast<float>(Packed & 0x7F) / 127.0f;
	bOutLocked = (Packed & 0x80) != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tractor beam
// ---------------------------------------------------------------------------------------------------------------------

bool HCJTractor::CanHold(const FVector& HolderLocation, const FVector& HolderForward, const FVector& TargetLocation, float Range, bool bAlreadyHeld)
{
	const FVector To = TargetLocation - HolderLocation;
	const double Distance = To.Size();
	if (Distance > static_cast<double>(Range) * (bAlreadyHeld ? RangeSlack : 1.0f))
	{
		return false;
	}
	if (Distance < 1.0)
	{
		return true;
	}
	const double Cosine = FVector::DotProduct(To / Distance, HolderForward.GetSafeNormal());
	return Cosine >= FMath::Cos(FMath::DegreesToRadians(static_cast<double>(ConeDegrees) * (bAlreadyHeld ? 2.0 : 1.0)));
}

FVector HCJTractor::ComputePull(const FVector& VictimLocation, const FVector& HolderLocation, float PullSpeed, float DeltaSeconds, float MinDistance)
{
	const FVector To = HolderLocation - VictimLocation;
	const double Distance = To.Size();
	if (Distance <= MinDistance || PullSpeed <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return FVector::ZeroVector;
	}
	const double Step = FMath::Min(static_cast<double>(PullSpeed) * DeltaSeconds, Distance - MinDistance);
	return To * (Step / Distance);
}

bool HCJTractor::ShouldBreak(float VictimBoostSeconds, float HolderDamageTaken)
{
	return VictimBoostSeconds >= BreakBoostSeconds || HolderDamageTaken >= BreakDamage;
}
