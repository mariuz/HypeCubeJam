#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Ship/HCJShieldFacings.h"
#include "HCJHealthComponent.generated.h"

class AController;
class UHCJHealthComponent;

/** Health or shield value changed. Fires on the server when it changes and on clients from the OnRep functions. */
DECLARE_MULTICAST_DELEGATE_OneParam(FHCJOnHealthChanged, UHCJHealthComponent* /*Component*/);

/** Health reached zero. SERVER ONLY, exactly once per life. Killer may be null (environment / self). */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FHCJOnHealthDepleted, UHCJHealthComponent* /*Component*/, AController* /*Killer*/, AActor* /*Causer*/);

/**
 * Replicated hit points with a regenerating shield in front of them.
 *
 * Authority: only the server mutates Health/Shield (ApplyDamage, ApplyDamageOnFacing, ResetToFull, shield
 * regeneration in TickComponent). Clients only read; values arrive by property replication (component is replicated
 * by default, owner must replicate). The component never looks at who its owner is and never destroys it:
 * the owner listens to OnDepleted and decides what death means.
 *
 * SHIELD FACINGS: the shield is one value (station turrets: the default) or, after SetFacingCount(4), four facings
 * (EHCJShieldFacing: fore, aft, port, starboard) of MaxShield / 4 each. A hit with a direction (ApplyDamageOnFacing)
 * drains the facing it lands on; what the facing cannot take, and every hit on a facing that is down, goes to the
 * hull. Damage without a direction (ApplyDamage) is shared by all facings. Each facing starts regenerating
 * ShieldRegenDelay seconds after its own last hit, at ShieldRegenRate points per second, and only while alive.
 * On the wire the strengths are one word of four quantised bytes (HCJShield::Pack); clients decode it, so their
 * values are exact to about 1 % of a facing and "down" is exact.
 */
UCLASS(ClassGroup = (HCJ))
class HYPECUBEJAM_API UHCJHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHCJHealthComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }
	/** Sum of all facings / their combined capacity. */
	float GetShield() const;
	float GetMaxShield() const { return MaxShield; }
	/** 0..1; 0 when MaxHealth <= 0. */
	float GetHealthFraction() const { return MaxHealth > 0.0f ? Health / MaxHealth : 0.0f; }
	/** 0..1 (above 1 when overcharged); 0 when MaxShield <= 0. */
	float GetShieldFraction() const { return MaxShield > 0.0f ? GetShield() / MaxShield : 0.0f; }
	/** Valid on every machine: Health <= 0. */
	bool IsDepleted() const { return Health <= 0.0f; }

	/** 1 (one shield all around) or 4 (EHCJShieldFacing). */
	int32 GetNumFacings() const { return NumFacings; }
	/** Strength of one facing (an EHCJShieldFacing index; out of range gives 0), the capacity of a facing and the fraction of it (above 1 when overcharged). */
	float GetFacingShield(int32 Facing) const { return (Facing >= 0 && Facing < NumFacings) ? FacingShield[Facing] : 0.0f; }
	float GetFacingMax() const { return MaxShield / NumFacings; }
	float GetFacingFraction(int32 Facing) const { return MaxShield > 0.0f ? GetFacingShield(Facing) / GetFacingMax() : 0.0f; }
	/** Nothing left on the facing (exact on every machine). */
	bool IsFacingDown(int32 Facing) const { return GetFacingShield(Facing) <= 0.0f; }

	/**
	 * SERVER ONLY (no-op returning 0 on clients, when already depleted, or when Amount <= 0).
	 * Damage without a direction: shared by all facings (HCJShield::AbsorbSpread), the rest goes to health; restarts
	 * the regen delay of every facing, broadcasts OnHealthChanged and, if health reached zero,
	 * OnDepleted(this, InstigatorController, Causer) exactly once.
	 * @return the damage actually absorbed by shield + health.
	 */
	float ApplyDamage(float Amount, AController* InstigatorController, AActor* Causer, FHCJShieldHit* OutHit = nullptr);

	/**
	 * SERVER ONLY, like ApplyDamage but for a hit that landed on one facing (HCJShield::SelectFacing): only that
	 * facing absorbs, overflow and a hit on a downed facing go to health, only its regen delay restarts. With a
	 * single shield (GetNumFacings() == 1) every facing means that shield.
	 */
	float ApplyDamageOnFacing(float Amount, EHCJShieldFacing Facing, AController* InstigatorController, AActor* Causer, FHCJShieldHit* OutHit = nullptr);

	/**
	 * SERVER ONLY: a photon torpedo on one facing (HCJTorpedo::SplitDamage). The facing loses up to ShieldDamage; the
	 * share of the torpedo it did not stop, of HullDamage, goes to health. Otherwise like ApplyDamageOnFacing.
	 */
	float ApplyTorpedoOnFacing(float ShieldDamage, float HullDamage, EHCJShieldFacing Facing, AController* InstigatorController, AActor* Causer, FHCJShieldHit* OutHit = nullptr);

	/** SERVER ONLY. Health = MaxHealth, every facing at its capacity, re-arms OnDepleted. */
	void ResetToFull();

	/** Every machine, before BeginPlay: other maxima than the class defaults (station turrets, ships). InMaxShield is the sum of all facings. Not replicated: call it with the same values everywhere. */
	void SetMaxValues(float InMaxHealth, float InMaxShield);
	/** Every machine, before BeginPlay: 4 for a ship with shield facings, 1 (the default) for one shield all around. Not replicated. */
	void SetFacingCount(int32 InNumFacings);
	/**
	 * Every machine, after BeginPlay (a ship whose type arrives or changes): new maxima. The server also refills hull
	 * and shields; a client decodes the replicated strengths again with the new capacity.
	 */
	void ChangeMaxValues(float InMaxHealth, float InMaxShield);
	/**
	 * SERVER ONLY (a cloaked ship): true drops every facing to nothing and stops the regeneration; false lets every
	 * facing regenerate at once, from nothing, without the usual delay.
	 */
	void SetShieldsSuppressed(bool bSuppressed);
	bool AreShieldsSuppressed() const { return bShieldsSuppressed; }
	/** Before BeginPlay (only the server uses them): seconds without a hit before a facing regenerates, and points per second per facing. */
	void SetShieldRegen(float InDelay, float InRatePerFacing);

	float GetShieldRegenDelay() const { return ShieldRegenDelay; }
	float GetShieldRegenRate() const { return ShieldRegenRate; }

	/** SERVER ONLY (pickups). Restores up to Amount hull points, never above MaxHealth, never on a depleted component. Returns the points restored. */
	float Heal(float Amount);

	/**
	 * SERVER ONLY (pickups). Adds up to Amount shield points, spread over the facings with the weakest first
	 * (HCJShield::AddSpread); a facing may then stand above its capacity, up to OverchargeFactor times it (at most
	 * HCJShield::QuantRange; regeneration still stops at the capacity). Returns the points added.
	 */
	float AddShield(float Amount, float OverchargeFactor);

	FHCJOnHealthChanged OnHealthChanged;
	FHCJOnHealthDepleted OnDepleted;

protected:
	UFUNCTION()
	void OnRep_Health();

	UFUNCTION()
	void OnRep_Shield();

	/** Server: the hull part and the delegates shared by both damage functions. */
	float FinishDamage(float ToShield, float ToHull, AController* InstigatorController, AActor* Causer);
	/** Server: every facing to Value. */
	void SetAllFacings(float Value);
	/** Server: PackedShield from the exact strengths. */
	void UpdatePackedShield();

	/** Same on every machine (class default); not replicated. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Health")
	float MaxHealth = 100.0f;

	/** Capacity of all facings together. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Health")
	float MaxShield = 50.0f;

	/** Seconds without damage on a facing before it starts to regenerate. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Health")
	float ShieldRegenDelay = 4.0f;

	/** Shield points per second, per facing. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Health")
	float ShieldRegenRate = 15.0f;

	/** 1 or HCJShield::NumFacings; see SetFacingCount. */
	int32 NumFacings = 1;
	/** Server: see SetShieldsSuppressed. */
	bool bShieldsSuppressed = false;

	/** Initialised to MaxHealth in BeginPlay on the server. */
	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 100.0f;

	/** The facings as four quantised bytes (HCJShield::Pack): the only shield state on the wire. */
	UPROPERTY(ReplicatedUsing = OnRep_Shield)
	uint32 PackedShield = 0;

	/** Server: the exact strengths (capacity in BeginPlay). Clients: decoded from PackedShield. Entries beyond NumFacings stay 0. */
	float FacingShield[HCJShield::NumFacings] = { 0.0f, 0.0f, 0.0f, 0.0f };

	/** Server world time (GetWorld()->GetTimeSeconds()) of the last damage on each facing. */
	float LastFacingDamageTime[HCJShield::NumFacings] = { -1000.0f, -1000.0f, -1000.0f, -1000.0f };
};
