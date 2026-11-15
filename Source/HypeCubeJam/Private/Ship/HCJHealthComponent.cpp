#include "Ship/HCJHealthComponent.h"

#include "Engine/World.h"
#include "Game/HCJBalance.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Ship/HCJTorpedo.h"

UHCJHealthComponent::UHCJHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UHCJHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	const bool bAuthority = GetOwner() && GetOwner()->HasAuthority();
	if (bAuthority)
	{
		Health = MaxHealth;
		SetAllFacings(GetFacingMax());
	}
	else
	{
		// Whatever arrived before BeginPlay (the initial bunch) decoded with the right capacity.
		HCJShield::Unpack(PackedShield, GetFacingMax(), MakeArrayView(FacingShield, NumFacings));
	}
	// Only the server regenerates the shield; clients have nothing to tick.
	SetComponentTickEnabled(bAuthority);
}

float UHCJHealthComponent::GetShield() const
{
	float Sum = 0.0f;
	for (int32 Index = 0; Index < NumFacings; ++Index)
	{
		Sum += FacingShield[Index];
	}
	return Sum;
}

void UHCJHealthComponent::SetAllFacings(float Value)
{
	for (int32 Index = 0; Index < HCJShield::NumFacings; ++Index)
	{
		FacingShield[Index] = Index < NumFacings ? Value : 0.0f;
		LastFacingDamageTime[Index] = -1000.0f;
	}
	UpdatePackedShield();
}

void UHCJHealthComponent::UpdatePackedShield()
{
	PackedShield = HCJShield::Pack(MakeArrayView(FacingShield, NumFacings), GetFacingMax());
}

void UHCJHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	if (OwnerActor == nullptr || World == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	if (bShieldsSuppressed)
	{
		return;
	}
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const float FacingMax = GetFacingMax();
	const bool bDepleted = IsDepleted();
	bool bChanged = false;
	for (int32 Index = 0; Index < NumFacings; ++Index)
	{
		const float NewShield = HCJBalance::StepShieldRegen(FacingShield[Index], FacingMax, Now - LastFacingDamageTime[Index], DeltaTime,
			ShieldRegenDelay, ShieldRegenRate, bDepleted);
		if (NewShield != FacingShield[Index])
		{
			FacingShield[Index] = NewShield;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		UpdatePackedShield();
		OnHealthChanged.Broadcast(this);
	}
}

void UHCJHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHCJHealthComponent, Health);
	DOREPLIFETIME(UHCJHealthComponent, PackedShield);
}

float UHCJHealthComponent::ApplyDamage(float Amount, AController* InstigatorController, AActor* Causer, FHCJShieldHit* OutHit)
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority() || IsDepleted() || !(Amount > 0.0f))
	{
		return 0.0f;
	}

	uint8 CollapsedMask = 0;
	const float ToHull = HCJShield::AbsorbSpread(MakeArrayView(FacingShield, NumFacings), Amount, CollapsedMask);
	for (int32 Index = 0; Index < NumFacings && World != nullptr; ++Index)
	{
		LastFacingDamageTime[Index] = static_cast<float>(World->GetTimeSeconds());
	}
	if (OutHit != nullptr)
	{
		OutHit->ToShield = Amount - ToHull;
		OutHit->ToHull = FMath::Min(ToHull, Health);
		OutHit->CollapsedMask = CollapsedMask;
		OutHit->FractionAfter = 1.0f;
		for (int32 Index = 0; Index < NumFacings; ++Index)
		{
			OutHit->FractionAfter = FMath::Min(OutHit->FractionAfter, GetFacingFraction(Index));
		}
	}
	return FinishDamage(Amount - ToHull, ToHull, InstigatorController, Causer);
}

float UHCJHealthComponent::ApplyDamageOnFacing(float Amount, EHCJShieldFacing Facing, AController* InstigatorController, AActor* Causer, FHCJShieldHit* OutHit)
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority() || IsDepleted() || !(Amount > 0.0f))
	{
		return 0.0f;
	}

	const int32 Index = NumFacings > 1 ? FMath::Clamp(static_cast<int32>(Facing), 0, NumFacings - 1) : 0;
	bool bCollapsed = false;
	const float ToHull = HCJShield::AbsorbOnFacing(FacingShield[Index], Amount, bCollapsed);
	if (World != nullptr)
	{
		LastFacingDamageTime[Index] = static_cast<float>(World->GetTimeSeconds());
	}
	if (OutHit != nullptr)
	{
		OutHit->ToShield = Amount - ToHull;
		OutHit->ToHull = FMath::Min(ToHull, Health);
		OutHit->CollapsedMask = bCollapsed ? static_cast<uint8>(1 << Index) : 0;
		OutHit->FractionAfter = GetFacingFraction(Index);
	}
	return FinishDamage(Amount - ToHull, ToHull, InstigatorController, Causer);
}

float UHCJHealthComponent::ApplyTorpedoOnFacing(float ShieldDamage, float HullDamage, EHCJShieldFacing Facing, AController* InstigatorController, AActor* Causer, FHCJShieldHit* OutHit)
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority() || IsDepleted() || !(ShieldDamage > 0.0f || HullDamage > 0.0f))
	{
		return 0.0f;
	}

	const int32 Index = NumFacings > 1 ? FMath::Clamp(static_cast<int32>(Facing), 0, NumFacings - 1) : 0;
	const float Before = FacingShield[Index];
	float ShieldLoss = 0.0f;
	float ToHull = 0.0f;
	HCJTorpedo::SplitDamage(Before, ShieldDamage, HullDamage, ShieldLoss, ToHull);
	FacingShield[Index] = FMath::Max(Before - ShieldLoss, 0.0f);
	if (World != nullptr)
	{
		LastFacingDamageTime[Index] = static_cast<float>(World->GetTimeSeconds());
	}
	if (OutHit != nullptr)
	{
		OutHit->ToShield = ShieldLoss;
		OutHit->ToHull = FMath::Min(ToHull, Health);
		OutHit->CollapsedMask = (Before > 0.0f && FacingShield[Index] <= 0.0f) ? static_cast<uint8>(1 << Index) : 0;
		OutHit->FractionAfter = GetFacingFraction(Index);
	}
	return FinishDamage(ShieldLoss, ToHull, InstigatorController, Causer);
}

float UHCJHealthComponent::FinishDamage(float ToShield, float ToHull, AController* InstigatorController, AActor* Causer)
{
	const float HealthAbsorbed = FMath::Min(Health, ToHull);
	Health -= HealthAbsorbed;
	// Kill float residue so IsDepleted() is exact on every machine.
	if (Health < UE_KINDA_SMALL_NUMBER)
	{
		Health = 0.0f;
	}
	UpdatePackedShield();

	OnHealthChanged.Broadcast(this);

	// The early-out on IsDepleted() in the callers guarantees this fires once per life.
	if (IsDepleted())
	{
		OnDepleted.Broadcast(this, InstigatorController, Causer);
	}

	return ToShield + HealthAbsorbed;
}

void UHCJHealthComponent::ResetToFull()
{
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority())
	{
		return;
	}

	Health = MaxHealth;
	SetAllFacings(GetFacingMax());
	OnHealthChanged.Broadcast(this);
}

void UHCJHealthComponent::ChangeMaxValues(float InMaxHealth, float InMaxShield)
{
	SetMaxValues(InMaxHealth, InMaxShield);
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !HasBegunPlay())
	{
		return;
	}
	if (OwnerActor->HasAuthority())
	{
		if (!IsDepleted())
		{
			ResetToFull();
		}
	}
	else
	{
		HCJShield::Unpack(PackedShield, GetFacingMax(), MakeArrayView(FacingShield, NumFacings));
		OnHealthChanged.Broadcast(this);
	}
}

void UHCJHealthComponent::SetShieldsSuppressed(bool bSuppressed)
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	if (OwnerActor == nullptr || World == nullptr || !OwnerActor->HasAuthority() || bSuppressed == bShieldsSuppressed)
	{
		return;
	}
	bShieldsSuppressed = bSuppressed;
	if (bSuppressed)
	{
		SetAllFacings(0.0f);
		OnHealthChanged.Broadcast(this);
	}
	else
	{
		// Up again from nothing, at once: the usual delay after a hit does not apply.
		const float Now = static_cast<float>(World->GetTimeSeconds());
		for (float& Time : LastFacingDamageTime)
		{
			Time = Now - ShieldRegenDelay - 1.0f;
		}
	}
}

void UHCJHealthComponent::SetMaxValues(float InMaxHealth, float InMaxShield)
{
	MaxHealth = FMath::Max(InMaxHealth, 1.0f);
	MaxShield = FMath::Max(InMaxShield, 0.0f);
}

void UHCJHealthComponent::SetFacingCount(int32 InNumFacings)
{
	NumFacings = InNumFacings >= HCJShield::NumFacings ? HCJShield::NumFacings : 1;
}

void UHCJHealthComponent::SetShieldRegen(float InDelay, float InRatePerFacing)
{
	ShieldRegenDelay = FMath::Max(InDelay, 0.0f);
	ShieldRegenRate = FMath::Max(InRatePerFacing, 0.0f);
}

float UHCJHealthComponent::Heal(float Amount)
{
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority() || IsDepleted() || !(Amount > 0.0f))
	{
		return 0.0f;
	}
	const float Restored = FMath::Min(Amount, MaxHealth - Health);
	if (Restored <= 0.0f)
	{
		return 0.0f;
	}
	Health += Restored;
	OnHealthChanged.Broadcast(this);
	return Restored;
}

float UHCJHealthComponent::AddShield(float Amount, float OverchargeFactor)
{
	const AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || !OwnerActor->HasAuthority() || IsDepleted() || !(Amount > 0.0f))
	{
		return 0.0f;
	}
	if (bShieldsSuppressed)
	{
		return 0.0f;
	}
	const float Limit = GetFacingMax() * FMath::Clamp(OverchargeFactor, 1.0f, HCJShield::QuantRange);
	const float Added = HCJShield::AddSpread(MakeArrayView(FacingShield, NumFacings), Amount, Limit);
	if (Added <= 0.0f)
	{
		return 0.0f;
	}
	UpdatePackedShield();
	OnHealthChanged.Broadcast(this);
	return Added;
}

void UHCJHealthComponent::OnRep_Health()
{
	OnHealthChanged.Broadcast(this);
}

void UHCJHealthComponent::OnRep_Shield()
{
	HCJShield::Unpack(PackedShield, GetFacingMax(), MakeArrayView(FacingShield, NumFacings));
	OnHealthChanged.Broadcast(this);
}
