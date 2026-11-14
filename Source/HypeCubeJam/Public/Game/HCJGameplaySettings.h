#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Ship/HCJWeapons.h"
#include "HCJGameplaySettings.generated.h"

/**
 * Gameplay tunables that are not world generation: weapons, pickups and power-ups. Values live in
 * Config/DefaultGame.ini, section [/Script/HypeCubeJam.HCJGameplaySettings]; every one has a working C++ default.
 *
 * AUTHORITY: the server's values decide (damage, ammunition, respawn times, what a pad holds). Clients read the
 * weapon table only for looks and to fly their copy of a projectile; the few numbers that client prediction needs
 * (overdrive multipliers) travel in the replicated match config instead (FHCJPickupSettings).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "HypeCubeJam Gameplay"))
class HYPECUBEJAM_API UHCJGameplaySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UHCJGameplaySettings& Get() { return *GetDefault<UHCJGameplaySettings>(); }

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	/** Definition of a weapon: the config entry at its index when there is one and it is sane, else FHCJWeaponDef::MakeDefault. */
	const FHCJWeaponDef& GetWeapon(EHCJWeapon Weapon) const;

	/** Relative chance of Item when a pad is (re)filled; 0 for an invalid item. */
	float GetItemWeight(EHCJPickupItem Item) const;

	/**
	 * Picks an item with the configured weights from Roll in [0, 1). Pure function (the server passes a number of
	 * its gameplay random stream). Cloak is left out when bCloak is off.
	 */
	EHCJPickupItem PickItem(float Roll) const;

	// ---- weapons ----

	/** Indexed by EHCJWeapon (0 blaster, 1 scatter, 2 rail, 3 missile, 4 pulse, 5 photon torpedo, 6 phaser). Missing or unusable entries fall back to the built-in table. */
	UPROPERTY(Config, EditAnywhere, Category = "Weapons")
	TArray<FHCJWeaponDef> Weapons;

	/**
	 * Aim assist: radius (uu) of the sphere around a ship that bolts hit, on the server and in every client's own
	 * simulation of a bolt. The ship's movement collision stays HCJ::ShipCollisionRadius (60). 0 or less than that
	 * switches the assist off.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Weapons", meta = (ClampMin = "0.0", ClampMax = "300.0"))
	float AimAssistRadius = 100.0f;

	// ---- photon torpedoes (Ship/HCJTorpedo.h). Reload time, starting stock, speed, lifetime, homing and the damage to an
	// open hull are the TORPEDO row of Weapons (FireInterval, Ammo, ProjectileSpeed, Lifetime, HomingTurnRate /
	// HomingConeDegrees, Damage). Same on every machine. ----

	/** Most torpedoes a ship can carry (a fresh ship has the Ammo of the weapon row). */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "1", ClampMax = "12"))
	int32 TorpedoMaxStock = 6;

	/** Torpedoes a restock pad gives. */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "1", ClampMax = "12"))
	int32 TorpedoRestockAmount = 3;

	/** What one torpedo can strip from the shield facing it lands on; the share of it the facing cannot take is the share of Damage that reaches the hull. */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "0.0"))
	float TorpedoShieldDamage = 30.0f;

	/**
	 * A torpedo is armed after this distance (uu); before it a hit is a dud. Long enough that the ship that fired it,
	 * even one following it under boost (6000 uu/s against the torpedo's 9000), is outside the blast when it arms.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "0.0"))
	float TorpedoArmingDistance = 1200.0f;

	/** Share of the damage a dud (a torpedo that hits before it is armed) still does to what it hit. */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TorpedoDudDamageFraction = 0.15f;

	/** Blast: full damage to hulls within the inner radius of the detonation, falling to TorpedoBlastEdgeScale at the outer radius (uu, measured to the hull). */
	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "0.0"))
	float TorpedoBlastInnerRadius = 120.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "0.0"))
	float TorpedoBlastRadius = 450.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Torpedoes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TorpedoBlastEdgeScale = 0.25f;

	// ---- phasers (Ship/HCJPhaser.h). Damage per second at close range, reach and the damage step are the PHASER row of
	// Weapons (Damage, BeamRange, FireInterval). Server values decide; clients use the range to draw the beam. ----

	/** Seconds of beam in a full phaser bank. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.5"))
	float PhaserDrainSeconds = 4.0f;

	/** Seconds after the beam stops before the bank starts to recharge. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.0"))
	float PhaserRechargeDelay = 1.0f;

	/** Seconds an empty bank needs to fill. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.5"))
	float PhaserRechargeSeconds = 5.0f;

	/** An emptied bank gives no beam (the trigger fires the blaster) until it is back at this level, 0..1. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float PhaserRelockLevel = 0.25f;

	/** Full damage within this share of the range; from there it falls in a straight line to PhaserEdgeScale at the end of the beam. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhaserFullDamageFraction = 0.3f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhaserEdgeScale = 0.45f;

	/** Aim assist: the beam bends to a hostile ship within this many degrees of the aim line; a little more for the locked target. */
	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float PhaserAssistDegrees = 1.6f;

	UPROPERTY(Config, EditAnywhere, Category = "Phasers", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float PhaserLockAssistDegrees = 3.0f;

	// ---- destroyable asteroids (World/HCJAsteroidField.h). Sent to the clients with the match config. Only phasers and
	// torpedoes damage rocks; bolts do not. URL option of the server: ?Rocks=0|1. ----

	UPROPERTY(Config, EditAnywhere, Category = "Asteroids")
	bool bDestroyableAsteroids = true;

	/** A rock with a bounding radius of at least this (uu) breaks into two or three pieces; smaller ones are simply destroyed. */
	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "50.0"))
	float AsteroidSplitMinRadius = 260.0f;

	/** Hit points of a rock: bounding radius (uu) times this, within [AsteroidMinHealth, AsteroidMaxHealth]. */
	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "0.001"))
	float AsteroidHealthPerRadius = 0.22f;

	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "1.0"))
	float AsteroidMinHealth = 25.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "1.0"))
	float AsteroidMaxHealth = 700.0f;

	/** What the phaser's damage per second / a torpedo's damage is multiplied with against rock. */
	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "0.0"))
	float PhaserRockDamageScale = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Asteroids", meta = (ClampMin = "0.0"))
	float TorpedoRockDamageScale = 1.5f;

	// ---- shockwaves (Effects/HCJEffectsLogic.h): an explosion pushes the ships around it. Server side, through the
	// ship movement; the owning client follows with a movement correction. ----

	/** Reach (uu) and push (uu/s at the centre, falling to nothing at the reach) of a torpedo detonation / a ship's explosion. 0 = off. */
	UPROPERTY(Config, EditAnywhere, Category = "Shockwaves", meta = (ClampMin = "0.0"))
	float TorpedoShockwaveRadius = 1600.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Shockwaves", meta = (ClampMin = "0.0"))
	float TorpedoShockwaveSpeed = 1300.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Shockwaves", meta = (ClampMin = "0.0"))
	float ShipShockwaveRadius = 1300.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Shockwaves", meta = (ClampMin = "0.0"))
	float ShipShockwaveSpeed = 900.0f;

	// ---- pickups ----

	/** Master switch (URL: ?Pickups=0|1). */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	bool bPickups = true;

	/** Pads per sector: a number in [Min, Max] picked per sector from the match seed. */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MinPadsPerSector = 3;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxPadsPerSector = 5;

	/** Seconds until an emptied pad holds a new item: uniform in [Min, Max]. */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (ClampMin = "1.0"))
	float RespawnMinSeconds = 20.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (ClampMin = "1.0"))
	float RespawnMaxSeconds = 30.0f;

	/** A ship whose centre comes this close to a pad collects its item (uu). */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups", meta = (ClampMin = "100.0"))
	float CollectRadius = 320.0f;

	/** Relative chances, indexed by EHCJPickupItem (repair, shield cell, overdrive, quad damage, cloak, scatter, rail, missile, pulse, torpedoes). */
	UPROPERTY(Config, EditAnywhere, Category = "Pickups")
	TArray<float> ItemWeights;

	// ---- station turrets (where the stations are: [/Script/HypeCubeJam.HCJGameMode]) ----

	/** A turret sees and shoots ships within this distance (uu). */
	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "1000.0"))
	float TurretRange = 6500.0f;

	/** Degrees per second the gun turns. */
	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "1.0"))
	float TurretTurnRate = 70.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "0.05"))
	float TurretFireInterval = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "0.0"))
	float TurretDamage = 9.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "1000.0"))
	float TurretProjectileSpeed = 14000.0f;

	/** Half angle of the cone the shots are scattered in, degrees. */
	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float TurretSpreadDegrees = 1.5f;

	/** The gun fires once it points within this many degrees of the lead point. */
	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "0.5", ClampMax = "45.0"))
	float TurretFireConeDegrees = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "1.0"))
	float TurretHealth = 120.0f;

	/** Seconds until a destroyed turret is rebuilt. */
	UPROPERTY(Config, EditAnywhere, Category = "Turrets", meta = (ClampMin = "1.0"))
	float TurretRespawnSeconds = 60.0f;

	// ---- shields: four facings (fore, aft, port, starboard), see Ship/HCJShieldFacings.h. Same on every machine. ----

	/** Capacity of one shield facing; a ship has four. */
	UPROPERTY(Config, EditAnywhere, Category = "Shields", meta = (ClampMin = "0.0"))
	float ShieldFacingCapacity = 26.0f;

	/** Seconds without a hit on a facing before it starts to regenerate. */
	UPROPERTY(Config, EditAnywhere, Category = "Shields", meta = (ClampMin = "0.0"))
	float ShieldRegenDelay = 4.0f;

	/** Points per second a facing regenerates. */
	UPROPERTY(Config, EditAnywhere, Category = "Shields", meta = (ClampMin = "0.0"))
	float ShieldRegenRate = 8.0f;

	// ---- power-ups ----

	/** Hull points restored by a repair kit. */
	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0"))
	float RepairAmount = 60.0f;

	/** Shield points added by a shield cell, the weakest facings first, each up to ShieldOverchargeFactor times its capacity. */
	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0"))
	float ShieldCellAmount = 50.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float ShieldOverchargeFactor = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0"))
	float OverdriveSeconds = 10.0f;

	/** Cruise / boost speed and acceleration multipliers while overdrive runs (sent to clients in the match config). */
	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float OverdriveSpeedMultiplier = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float OverdriveAccelerationMultiplier = 1.6f;

	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0"))
	float QuadSeconds = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float QuadDamageMultiplier = 4.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Powerups")
	bool bCloak = true;

	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "1.0"))
	float CloakSeconds = 8.0f;

	/** Bots do not see a cloaked ship farther away than this (uu). */
	UPROPERTY(Config, EditAnywhere, Category = "Powerups", meta = (ClampMin = "0.0"))
	float CloakDetectRange = 2500.0f;
};
