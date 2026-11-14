#pragma once

#include "CoreMinimal.h"
#include "HCJWeapons.generated.h"

/**
 * Weapons a ship can fire. A ship has a BASE weapon with unlimited ammunition - the phaser by default, the blaster
 * when its pilot switches to it - and can hold one pickup weapon in its place, which falls back to the base weapon
 * when it is empty or dropped. New values are appended: the numbers index the weapon table of the config.
 */
UENUM()
enum class EHCJWeapon : uint8
{
	Blaster = 0,
	/** Short range burst of pellets. */
	Scatter = 1,
	/** One very fast, heavy bolt. */
	Rail = 2,
	/** Slow missiles that steer towards the nearest hostile in a cone. */
	Missile = 3,
	/** Very high rate of fire, light bolts. */
	Pulse = 4,
	/**
	 * Photon torpedo: the secondary weapon of every ship (own stock and trigger, see Ship/HCJTorpedo.h), never the
	 * weapon a ship holds. It has a row in the weapon table so that its projectile is an ordinary AHCJProjectile:
	 * FireInterval is the reload time, Ammo the stock of a fresh ship, Damage the damage to an open hull.
	 */
	Torpedo = 5,
	/**
	 * Phaser: a continuous beam (Ship/HCJPhaser.h), the base weapon of every ship. No projectile: in its row Damage is
	 * the damage per second at close range, BeamRange the reach, FireInterval the step in which the damage is applied.
	 * It drains the phaser bank instead of ammunition; with the bank empty the trigger fires the blaster.
	 */
	Phaser = 6,
	Count UMETA(Hidden)
};

/** What a pickup pad can hold. */
UENUM()
enum class EHCJPickupItem : uint8
{
	Repair = 0,
	ShieldCell = 1,
	Overdrive = 2,
	QuadDamage = 3,
	Cloak = 4,
	Scatter = 5,
	Rail = 6,
	Missile = 7,
	Pulse = 8,
	/** Restocks the photon torpedoes (not a weapon pickup: the current weapon stays). */
	Torpedoes = 9,
	Count UMETA(Hidden)
};

/** Timed power-ups of a ship: bits of AHCJShipCharacter::PowerupFlags. */
namespace EHCJPowerup
{
	inline constexpr uint8 Overdrive = 1 << 0;
	inline constexpr uint8 Quad = 1 << 1;
	inline constexpr uint8 Cloak = 1 << 2;
}

/** How a weapon behaves. Server values decide; clients use them only for looks and to simulate their copy of a bolt. */
USTRUCT()
struct HYPECUBEJAM_API FHCJWeaponDef
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "HCJ")
	FString Name = TEXT("BLASTER");

	/** Seconds between two shots. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.02"))
	float FireInterval = 0.15f;

	/** Damage per projectile (per pellet). */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0"))
	float Damage = 20.0f;

	/** uu/s. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "100.0"))
	float ProjectileSpeed = 20000.0f;

	/** Seconds a projectile lives: range = speed * lifetime. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.1"))
	float Lifetime = 2.0f;

	/** Half angle of the cone the projectiles are scattered in, degrees. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float SpreadDegrees = 0.0f;

	/** Projectiles per shot. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "1", ClampMax = "16"))
	int32 Pellets = 1;

	/** Degrees per second a projectile turns towards its target; 0 = flies straight. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0"))
	float HomingTurnRate = 0.0f;

	/** Half angle of the cone in front of the projectile in which it looks for a target, degrees. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float HomingConeDegrees = 0.0f;

	/** Shots per pickup; 0 = unlimited (the blaster). */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0"))
	int32 Ammo = 0;

	/** Bolt / pickup colour; black = the colour of the shooter. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ")
	FLinearColor Color = FLinearColor::Black;

	/** Size of the bolt relative to the blaster bolt. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float VisualScale = 1.0f;

	/** Bots only fire this weapon at targets nearer than this (uu). */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0"))
	float BotMaxRange = 16000.0f;

	/** A beam (the phaser): no projectile is spawned; ProjectileSpeed is only what lead indicators are computed with. */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ")
	bool bBeam = false;

	/** Reach of a beam weapon (uu). */
	UPROPERTY(Config, EditAnywhere, Category = "HCJ", meta = (ClampMin = "0.0"))
	float BeamRange = 0.0f;

	/** How far the weapon reaches: the beam range, or speed * lifetime of its projectile. */
	float GetRange() const { return bBeam ? BeamRange : ProjectileSpeed * Lifetime; }

	/** Values a weapon can work with (positive rate, speed, lifetime, at least one pellet, sane angles). */
	bool IsSane() const;

	/** Built-in definition of a weapon, used when the config has no (sane) entry for it. */
	static FHCJWeaponDef MakeDefault(EHCJWeapon Weapon);
};

/**
 * Weapon and ammunition of one ship: plain logic without a world, so it can be tested on its own. The ship holds
 * one (server authoritative) and replicates its two fields.
 */
struct HYPECUBEJAM_API FHCJWeaponState
{
	EHCJWeapon Weapon = EHCJWeapon::Blaster;
	/** Shots left; meaningless for a weapon with unlimited ammunition. */
	int32 Ammo = 0;
	/** What the ship fires without a pickup weapon (phaser or blaster); Drop and an emptied weapon go back to it. */
	EHCJWeapon BaseWeapon = EHCJWeapon::Blaster;

	/** Picks a weapon up: a different weapon replaces the current one, the same one adds ammunition (at most two pickups' worth). */
	void Give(EHCJWeapon NewWeapon, const FHCJWeaponDef& Def);
	/** One shot fired with Def (the definition of the current weapon). Returns true when that emptied the weapon and it fell back to the blaster. */
	bool ConsumeShot(const FHCJWeaponDef& Def);
	/** Back to the base weapon. */
	void Drop();
	/**
	 * The pilot's switch between phaser and blaster: sets the base weapon to the other one (the phaser only when
	 * bPhaserAllowed) and, when no pickup weapon is held, the weapon with it. Returns the new base weapon.
	 */
	EHCJWeapon ToggleBase(bool bPhaserAllowed);
};

namespace HCJ
{
	/** Display name of an item ("REPAIR", "QUAD DAMAGE", ...). */
	HYPECUBEJAM_API const TCHAR* GetItemName(EHCJPickupItem Item);
	/** A weapon a ship can hold and fire with the main trigger (phaser, blaster and the pickup weapons; not the torpedo). */
	HYPECUBEJAM_API bool IsPrimaryWeapon(EHCJWeapon Weapon);
	/** Phaser or blaster: what a ship fires without a pickup weapon. */
	HYPECUBEJAM_API bool IsBaseWeapon(EHCJWeapon Weapon);
	/** The weapon an item gives, or EHCJWeapon::Count for a power-up. */
	HYPECUBEJAM_API EHCJWeapon ItemToWeapon(EHCJPickupItem Item);
	/** Glow colour of an item on its pad and on the HUD. */
	HYPECUBEJAM_API FLinearColor GetItemColor(EHCJPickupItem Item);
}
