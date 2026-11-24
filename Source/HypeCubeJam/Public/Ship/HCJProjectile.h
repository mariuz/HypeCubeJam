#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ship/HCJTorpedo.h"
#include "Ship/HCJWeapons.h"
#include "HCJProjectile.generated.h"

class AController;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Projectile of every ship weapon (and of station turrets): what it does follows from its weapon type
 * (FHCJWeaponDef of UHCJGameplaySettings: speed, lifetime, damage, homing), set by the server before BeginPlay
 * (InitWeapon) and replicated once with the spawn. A quad-damage shot carries a flag for its look.
 * Homing projectiles turn towards HomingTarget (chosen and refreshed by the server, replicated) on every machine.
 *
 * NETWORK MODEL: replicated actor spawned ONLY by the server (AHCJShipCharacter::ServerFireProjectile) with
 * Owner = Instigator = the firing ship. bReplicates = true, replicated movement gives clients the initial
 * location/velocity, after which UProjectileMovementComponent simulates locally on every machine (no gravity,
 * constant velocity = forward * Speed; the ship's own velocity is NOT inherited, which bots rely on for
 * lead aiming). Net cull distance = HCJ::NetCullDistance, low net update frequency.
 *
 * Authority: only the server applies damage and destroys the actor. On clients a hit just stops/hides the
 * local simulation; destruction arrives through replication.
 *
 * Collision: CollisionComp is a sphere, object type WorldDynamic, QueryOnly, blocks WorldStatic (asteroids)
 * and Pawn (ships), ignores everything else (portal triggers, other projectiles). The instigator ship is
 * ignored in both directions (IgnoreActorWhenMoving on both sides).
 *
 * Server hit handling (OnCollisionHit): if the other actor is not the instigator, call
 * UGameplayStatics::ApplyPointDamage(Other, Damage, travel direction, Hit, InstigatorController, this, UDamageType)
 * using the controller cached at spawn (the shooter may have died since), then Destroy().
 *
 * LOOK (every machine except a dedicated server, nothing replicated beyond the actor itself): BeginPlay swaps the
 * stretched sphere for UHCJAssetSettings::ProjectileMesh, attaches the trail and plays the muzzle flash at the
 * shooter. The impact effect needs no RPC: a machine plays it when its own simulation of the bolt hits something
 * (OnCollisionHit) or, failing that, when the server's destruction of the bolt arrives well before the bolt's
 * lifetime is up (Destroyed) - a bolt only dies early by hitting something.
 *
 * PHOTON TORPEDO (weapon type EHCJWeapon::Torpedo; rules in Ship/HCJTorpedo.h, numbers in UHCJGameplaySettings): the
 * same actor with four differences. 1) Homing: HomingTarget is the ship its firer had locked at launch (none: it
 * flies straight); the server drops it for good when the target dies, cloaks or leaves the cone ahead. 2) Arming: it
 * counts the distance it has flown; a hit before TorpedoArmingDistance is a dud. 3) Detonation (server,
 * ServerDetonate): on contact or when its life is over it damages what it hit and every ship and turret within the
 * blast radius that no rock shields, the facing taken from where the blast is. 4) Look: a star-burst quad facing the
 * local view, and AHCJTorpedoBlast where it goes off - on clients when their copy hits something or, failing that,
 * when the server's destruction of the torpedo arrives (a torpedo always ends in a detonation, a black hole aside).
 * Every machine keeps a list of the live torpedoes (GetLiveTorpedoes) for the HUD warning and for bots.
 */
UCLASS()
class HYPECUBEJAM_API AHCJProjectile : public AActor
{
	GENERATED_BODY()

public:
	AHCJProjectile();

	virtual void BeginPlay() override;
	/**
	 * Only enabled for bolts fired in a sector with a black hole: every machine carries its copy of the bolt by
	 * AHCJUniverse::GetGravityDrift (swept), the same pure function of position as for ships, so the bent path is
	 * the same everywhere; the server destroys a bolt that reaches the event horizon.
	 */
	virtual void Tick(float DeltaSeconds) override;
	/** Clients: plays the impact effect for a server-side hit that the local simulation did not see. */
	virtual void Destroyed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Server: a torpedo whose time is up goes off where it is. */
	virtual void LifeSpanExpired() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * SERVER, between SpawnActorDeferred and FinishSpawning: which weapon fired this, whether the shooter had quad
	 * damage (look) and the damage multiplier that goes with it.
	 */
	void InitWeapon(EHCJWeapon InWeapon, bool bInQuad, float InDamageMultiplier);
	/** SERVER, like InitWeapon: a projectile with explicit values that hurts everything it hits (station turrets). */
	void InitCustom(float InSpeed, float InDamage, float InLifetime);

	EHCJWeapon GetWeapon() const { return static_cast<EHCJWeapon>(WeaponType); }
	/** Definition of the weapon that fired this projectile. */
	const FHCJWeaponDef& GetWeaponDef() const;
	float GetDamage() const { return Damage; }

	bool IsTorpedo() const { return CustomSpeed <= 0.0f && GetWeapon() == EHCJWeapon::Torpedo; }
	/** What this projectile steers for (replicated), or nullptr. */
	AActor* GetHomingTarget() const { return HomingTarget; }
	/** Torpedoes only, every machine: uu flown so far, and whether that is past the arming distance. */
	float GetDistanceTravelled() const { return DistanceTravelled; }
	bool IsArmed() const;
	/** The copy on this machine has stopped (hit something, swallowed): it is about to go away. */
	bool IsSpent() const { return bImpactPlayed; }
	/** Every machine: the torpedoes alive in any world of this process (check GetWorld()). */
	static const TArray<TWeakObjectPtr<AHCJProjectile>>& GetLiveTorpedoes();
	/** Fired by a ship flown by a person (server: its controller; client: its player state is not a bot's). Decides how a torpedo homes (HCJTorpedo::GetHoming). */
	bool IsFiredByHuman() const { return bHumanFirer; }
	/** SERVER, before FinishSpawning: the shot was turned by the Easy aim assist of item 40 (telemetry only). */
	void MarkAimAssisted() { bAimAssisted = true; }
	bool IsAimAssisted() const { return bAimAssisted; }

protected:
	/** Bound to CollisionComp->OnComponentHit in the constructor. Applies damage on the server only. */
	UFUNCTION()
	void OnCollisionHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	/** Not on a dedicated server, from BeginPlay: bolt mesh and colour, trail, muzzle flash. Missing assets keep the tinted sphere / no effect. */
	void SetupBoltVisuals(const FLinearColor& BoltColor);

	/** Not on a dedicated server, at most once per bolt: impact effect at Location (ship or rock flavour). */
	void PlayImpactEffects(const FVector& Location, const FVector& Normal, bool bHitShip);

	/** Root; radius 20 uu. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USphereComponent> CollisionComp;

	/**
	 * UHCJAssetSettings::ProjectileMesh, or a stretched /Engine/BasicShapes/Sphere; no collision; tinted in BeginPlay
	 * (dynamic material, parameter "Color" or UHCJAssetSettings::ProjectileColorParameter).
	 */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	/** ProjectileGravityScale = 0, bRotationFollowsVelocity = true, bShouldBounce = false, InitialSpeed = MaxSpeed = Speed. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UProjectileMovementComponent> Movement;

	/** Server: nearest live ship other than the shooter inside the homing cone of the weapon, or null. */
	AActor* FindHomingTarget() const;
	/**
	 * Server, torpedoes: the firer's locked ship when it is alive, uncloaked, ahead inside the lock cone and in reach;
	 * for a person's torpedo, failing that, the nearest such ship inside the acquisition cone (HCJTorpedo::PickTarget); else null.
	 */
	AActor* FindTorpedoTarget() const;
	/** Server, torpedoes: HomingTarget is still something to steer for (HCJTorpedo::KeepsLock). */
	bool TorpedoKeepsTarget() const;
	/**
	 * Server, torpedoes, once: the detonation at Location. DirectActor (may be null: end of life) is what it ran into,
	 * with Hit; it takes the full damage (a dud: a fraction, and nothing else happens). Armed: every other ship and
	 * turret within the blast radius, in line of sight, takes damage scaled by HCJTorpedo::BlastScale as a point
	 * damage event whose impact point is the blast, so the victim's facing is the one towards the blast. Then Destroy.
	 */
	void ServerDetonate(const FVector& Location, AActor* DirectActor, const FHitResult* Hit);
	/** Not on a dedicated server, at most once: AHCJTorpedoBlast (armed) or a small impact (dud). */
	void PlayDetonationEffects(const FVector& Location, const FVector& Normal, bool bArmed);
	/** Not on a dedicated server, from SetupBoltVisuals: the star-burst quad, launch sound and trail. */
	void SetupTorpedoVisuals(const FLinearColor& Color);

	/** Values of this projectile (from the weapon definition in BeginPlay, or from InitCustom). */
	float Speed = 20000.0f;
	float Damage = 20.0f;
	float Lifetime = 2.0f;

	/** EHCJWeapon; replicated with the spawn only. */
	UPROPERTY(Replicated)
	uint8 WeaponType = 0;

	/** Fired under quad damage (look only); replicated with the spawn only. */
	UPROPERTY(Replicated)
	bool bQuad = false;

	/** InitCustom was used: Speed is replicated instead of read from the weapon table. */
	UPROPERTY(Replicated)
	float CustomSpeed = 0.0f;

	/** What a homing projectile steers towards; null = straight on. */
	UPROPERTY(Replicated)
	TObjectPtr<AActor> HomingTarget;

	float DamageMultiplier = 1.0f;
	float CustomDamage = 0.0f;
	float CustomLifetime = 0.0f;
	/** Server: world time of the next target search of a homing projectile. */
	float NextHomingSearchTime = 0.0f;

	/** Server: GetInstigatorController() captured in BeginPlay, used for kill credit. */
	UPROPERTY(Transient)
	TObjectPtr<AController> CachedInstigatorController;

	/** Local: PlayImpactEffects already ran for this bolt. */
	bool bImpactPlayed = false;

	/** See IsFiredByHuman / IsAimAssisted (not replicated: each machine works the first out for itself). */
	bool bHumanFirer = false;
	bool bAimAssisted = false;
	/** Torpedoes, every machine: how this one homes (from bHumanFirer and the weapon row, in BeginPlay). */
	HCJTorpedo::FHoming TorpedoHoming;

	/** Torpedoes, every machine: uu flown (counted in Tick). */
	float DistanceTravelled = 0.0f;
	/** Server: ServerDetonate ran. */
	bool bDetonated = false;
	/** Local look of a torpedo: VisualMesh is a quad that Tick turns to the view. */
	bool bBillboard = false;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StarMaterial;
};
