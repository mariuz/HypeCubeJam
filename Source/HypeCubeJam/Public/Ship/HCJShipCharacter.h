#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Math/RandomStream.h"
#include "Ship/HCJPhaser.h"
#include "Ship/HCJShieldFacings.h"
#include "Ship/HCJShipSystems.h"
#include "Ship/HCJShipTypes.h"
#include "Ship/HCJWeapons.h"
#include "Templates/SubclassOf.h"
#include "HCJShipCharacter.generated.h"

class AHCJProjectile;
class UAudioComponent;
class UCameraComponent;
class UHCJHealthComponent;
class UHCJShipMovementComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class USphereComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * The one pawn of the game, flown by humans (AHCJPlayerController) and bots (AHCJBotController) alike.
 *
 * MOVEMENT: ACharacter + UHCJShipMovementComponent (MOVE_Flying, zero gravity) so client prediction, server
 * reconciliation and simulated-proxy smoothing come from the engine. The capsule is a sphere
 * (radius == half height == HCJ::ShipCollisionRadius). bUseControllerRotationPitch = bUseControllerRotationYaw = true,
 * roll unused: the ship always points where its controller looks (pitch limited to +-89.9 by the camera manager).
 * Visual meshes are attached to GetMesh() (an empty skeletal mesh component) so engine network smoothing moves them.
 * Constructor sets SpawnCollisionHandlingMethod = AdjustIfPossibleButAlwaysSpawn, net cull distance
 * HCJ::NetCullDistance, and AIControllerClass is left alone (the game mode spawns controllers explicitly).
 *
 * INPUT: this class binds no input. Controllers drive it through the "control API" below:
 *  - players: AHCJPlayerController calls AddThrustInput / SetBoosting / SetFiring / ToggleCameraMode on the
 *    owning client and turns the view by adding to its own RotationInput (control rotation);
 *  - bots (server): AHCJBotController calls APawn::AddMovementInput (world space) or AddThrustInput,
 *    SetControlRotation on itself, SetBoosting, SetFiring.
 *
 * COMBAT (server authoritative): SetFiring only forwards an intent; the server fires from ITS view of the ship
 * (GetMuzzleLocation / GetAimRotation) at most once per FireInterval. Damage arrives through TakeDamage on the
 * server only and goes to UHCJHealthComponent. When health is depleted the ship runs Die().
 *
 * DEATH (server, Die): 1) bIsDead = true (replicated; hides the ship and disables its collision everywhere),
 * 2) AHCJGameMode::NotifyShipKilled(GetController(), Killer, this) while still possessed, 3) DetachFromControllerPendingDestroy(),
 * 4) SetLifeSpan(DeathLingerSeconds). The ship never respawns itself and never touches scores: that is the game mode's job.
 * The game mode may also remove a ship without a kill by calling Destroy() directly.
 *
 * WEAPONS AND POWER-UPS (server authoritative, from pickups: ServerGiveItem): the weapon (FHCJWeaponState) decides
 * fire rate, pellets, spread, damage, speed and ammunition of what ServerTickWeapon fires; a pickup weapon falls
 * back to the blaster when it is empty or dropped (DropWeapon). Timed power-ups are bits of PowerupFlags with
 * their end times; overdrive goes through UHCJShipMovementComponent::SetOverdrive on the server and on the owning
 * client, quad damage multiplies the damage of the ship's projectiles, cloak hides the ship from other machines
 * and from bots beyond a short range and ends when the ship fires. Replicated: the weapon and the flags to
 * everybody (looks), ammunition and end times to the owner (HUD). All of it dies with the ship.
 *
 * SHIELDS (server authoritative; Ship/HCJShieldFacings.h): the health component of a ship has four shield facings.
 * TakeDamage takes the facing from where a hit landed in the ship's own frame (HCJShield::SelectFacing); damage
 * without a direction is shared by all facings. Cosmetic, on every machine that draws: a hit a facing absorbed
 * flares the shield bubble (one hidden sphere per ship) at the impact point - MulticastShieldHit, unreliable, three
 * bytes, at most one per ship every ShieldHitInterval - and a facing going down flickers the bubble and plays a
 * sound, derived from the replicated strengths (HandleHealthChanged), so nothing reliable is added to the wire.
 *
 * PHOTON TORPEDOES (server authoritative; Ship/HCJTorpedo.h): every ship carries a small stock next to its weapon.
 * FireTorpedo is a one-shot intent (owning client -> ServerFireTorpedo); the server checks stock and reload
 * (FHCJTorpedoBay), spawns one AHCJProjectile of type EHCJWeapon::Torpedo that homes on this ship's lock target, and
 * replicates the count and the time the next one is ready to the owner (HUD). A new ship starts with a fresh stock;
 * a TORPEDOES pad restocks. TakeDamage gives a torpedo hit its own rule: most of it is spent on the facing it lands
 * on (UHCJHealthComponent::ApplyTorpedoOnFacing).
 *
 * PHASERS (server authoritative; Ship/HCJPhaser.h): the weapon a ship starts with. While the trigger is held and the
 * phaser bank has charge, ServerTickPhaser traces the beam every server tick (bent a little to a hostile ship near
 * the aim line), applies its damage in small steps - to the shield facing it touches, to a turret, or to a rock
 * (AHCJUniverse::ServerDamageAsteroid) - and publishes the beam in one byte (PhaserBeam: off, or its length) plus the
 * actor it holds (PhaserTarget). Every machine draws the beam from that and from the ship it sees, every frame
 * (TickPhaserEffects). The bank is replicated to the owner in one byte. With the bank emptied the trigger fires the
 * blaster until it has recharged to a level; the pilot can also switch the base weapon to the blaster (ToggleBaseWeapon).
 *
 * SHIP TYPES (Ship/HCJShipTypes.h): the server stamps every ship with the type, hull and paint its player chose
 * (ServerInitShipType; replicated to everybody) and every machine applies the row of the type table (ApplyShipType):
 * flight model, hull and shield capacity, size of collision / hit sphere / bubble, torpedoes, the look.
 *
 * WARP DRIVE (Ship/HCJShipSystems.h): lives in the movement simulation (UHCJShipMovementComponent) - the key travels in
 * the move flags, the owning client predicts the jump. The ship publishes the phase in one byte for everybody's
 * effects (WarpStateRep), tells the owner when the server stopped the drive (WarpCancelCount: a hit while charging, a
 * tractor beam, a collision, a portal), and keeps weapons off while the drive is busy.
 *
 * CLOAKING DEVICE (server authoritative): a toggle (ToggleCloak -> ServerSetCloak) with a meter. Cloaked: the Cloak
 * bit of PowerupFlags (everybody), shields down (UHCJHealthComponent::SetShieldsSuppressed), no weapons, no warp; a
 * fire attempt drops the cloak and weapons work a second later. REVEALED (bit CloakRevealed) while hit, held by a
 * tractor beam or very close to an enemy: still cloaked, but seen and targeted by everybody. Other machines draw a
 * cloaked ship with the cloak material (a faint shimmer) and leave it off radar, brackets, glints and locks; its
 * pilot sees it half transparent.
 *
 * TRACTOR BEAM (server authoritative): held key (SetTractoring -> ServerSetTractor). The server picks the locked
 * target (else the nearest ship in the cone), checks range, cone and line of sight, and publishes who holds whom:
 * TractorTarget on the holder (the beam everybody draws), TractorHolder on the victim (its movement simulation pulls
 * and slows it, on the server and in its owner's prediction). It drains the holder's phaser bank and breaks when the
 * victim boosts long enough, the holder takes damage, a rock gets in between, or range / cone are left.
 *
 * PORTALS (server): see RequestPortalTeleport.
 *
 * LOOK AND SOUND (every machine except a dedicated server; nothing here is replicated or affects gameplay):
 * The basic-shape body is swapped for one of UHCJAssetSettings::ShipMeshes in one of ShipSkins (ApplyShipLook):
 * which hull and which paint follow from the player id and player colour, i.e. from the replicated player state
 * alone, so all machines show the same ship for the same player. The three basic-shape components are kept and
 * reused, so the fallback is "do nothing".
 * Tick drives the engine sound, the thruster glow and the boost trail from the ship's speed, which every machine
 * knows (replicated movement). The death explosion is played where bIsDead arrives: Die() on a listen server,
 * OnRep_IsDead on clients.
 */
UCLASS()
class HYPECUBEJAM_API AHCJShipCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/** Replaces the movement component class with UHCJShipMovementComponent. */
	AHCJShipCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ AActor / APawn
	virtual void BeginPlay() override;
	/** Server: pending portal teleport, weapon, sector bounds. Everyone but a dedicated server: engine sound, thruster glow, boost trail. */
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/**
	 * SERVER ONLY (returns 0 on clients, when dead, or while spawn protected). Routes the amount returned by
	 * Super::TakeDamage into the health component and remembers EventInstigator (when it is another controller)
	 * as the last damager for kill credit.
	 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	/** Server: starts spawn protection, refreshes the hull colour. */
	virtual void PossessedBy(AController* NewController) override;
	/** Clients: refreshes the hull colour from AHCJPlayerState::GetPlayerColor. */
	virtual void OnRep_PlayerState() override;
	/** Server: Die(nullptr, nullptr) instead of the engine's silent Destroy. */
	virtual void FellOutOfWorld(const class UDamageType& DmgType) override;

	// ---- control API (call on the machine that controls the pawn: owning client, or server for bots / listen host) ----

	/**
	 * Thrust for this frame in the ship's own frame: X forward, Y right, Z up, each in [-1, 1].
	 * Converted with the control rotation and fed to AddMovementInput. Must be called every frame while held.
	 */
	void AddThrustInput(const FVector& LocalAxes);

	/** Boost intent; forwards to UHCJShipMovementComponent::SetWantsToBoost (client predicted). */
	void SetBoosting(bool bNewBoosting);
	bool IsBoosting() const;

	/**
	 * Hold-to-fire intent. On the server sets the intent directly; on an owning client sends ServerSetFiring.
	 * Ignored on simulated proxies. Firing ends spawn protection.
	 */
	void SetFiring(bool bNewFiring);
	/** Server: current fire intent. Owning client: last value passed to SetFiring. */
	bool IsFiring() const { return bWantsToFire; }

	/** Local cosmetic: toggles between third person (spring arm) and first person (arm length 0, own hull hidden). */
	void ToggleCameraMode();
	bool IsFirstPerson() const { return bFirstPerson; }

	// ---- queries (valid on every machine unless stated) ----

	UHCJHealthComponent* GetHealthComponent() const { return HealthComponent; }
	/** The character movement component cast to its real class (never null after construction). */
	UHCJShipMovementComponent* GetShipMovement() const;
	bool IsDead() const { return bIsDead; }

	/** Sector containing the ship (AHCJUniverse::GetSectorAtLocation), INDEX_NONE while no universe is built. */
	int32 GetCurrentSector() const;

	/** Where projectiles spawn: GetActorLocation() + GetAimRotation().Vector() * MuzzleOffset. */
	FVector GetMuzzleLocation() const;
	/** The controller's control rotation when a controller exists on this machine (server, owning client), else the actor rotation. */
	FRotator GetAimRotation() const;
	/** Muzzle speed of the current weapon in uu/s; its projectiles leave at this constant speed. */
	float GetProjectileSpeed() const;
	/** Minimum seconds between two shots of the current weapon. */
	float GetFireInterval() const;

	// ---- phasers ----

	/**
	 * What the trigger fires right now: the held weapon, except that a phaser whose bank is emptied (locked) fires
	 * the blaster. Server and owning client (elsewhere: the held weapon).
	 */
	EHCJWeapon GetFireWeapon() const;
	const FHCJWeaponDef& GetFireWeaponDef() const;
	/** Phaser bank, 0..1, and whether it is locked after being emptied. Server and owning client only. */
	float GetPhaserCharge() const;
	bool IsPhaserLocked() const;
	/** Every machine: this ship's beam is running. */
	bool IsPhaserBeamOn() const { return PhaserBeam != 0 && !bIsDead; }
	/** Every machine: the ends of the running beam as this machine draws it; false when there is none. bOutHit: it ends on something. */
	bool GetPhaserBeamSegment(FVector& OutStart, FVector& OutEnd, bool& bOutHit) const;
	/** Every machine: the ship or turret the beam holds, or nullptr. */
	AActor* GetPhaserTarget() const { return PhaserTarget; }
	/** Control API: switches the base weapon between phaser and blaster (owning client -> ServerToggleBaseWeapon; server directly). */
	void ToggleBaseWeapon();
	/** Server and owning client: EHCJWeapon::Phaser or Blaster - what the ship fires without a pickup weapon. */
	EHCJWeapon GetBaseWeapon() const { return static_cast<EHCJWeapon>(BaseWeaponType); }
	/** Every machine, local time: when the beam of this ship was last seen running; -1000 when never. */
	float GetLastPhaserBeamTime() const { return LastPhaserBeamTime; }
	/** SERVER ONLY: pushes the ship (uu/s added to its velocity), e.g. by the shockwave of an explosion. The owning client follows by a movement correction. */
	void ServerApplyImpulse(const FVector& VelocityChange);

	// ---- weapons and power-ups ----

	EHCJWeapon GetWeapon() const { return static_cast<EHCJWeapon>(WeaponType); }
	/** Definition of the current weapon (UHCJGameplaySettings). */
	const FHCJWeaponDef& GetWeaponDef() const;
	/** Shots left in a pickup weapon. Server and owning client only (0 elsewhere and for the blaster). */
	int32 GetAmmo() const { return Ammo; }
	/** Bit of EHCJPowerup; valid on every machine. */
	bool HasPowerup(uint8 PowerupBit) const { return (PowerupFlags & PowerupBit) != 0; }
	bool IsCloaked() const { return HasPowerup(EHCJPowerup::Cloak); }
	/** Cloaked but given away for the moment (a hit, a tractor beam, an enemy very close). Every machine. */
	bool IsCloakRevealed() const { return IsCloaked() && HasPowerup(EHCJPowerup::CloakRevealed); }
	/** Cloaked and not revealed: other ships do not see it - no marker, radar blip, lock or aim assist. Every machine. */
	bool IsCloakHidden() const { return IsCloaked() && !HasPowerup(EHCJPowerup::CloakRevealed); }
	/** Seconds a power-up still runs. Server and owning client only (0 elsewhere or when it is not active). */
	float GetPowerupTimeRemaining(uint8 PowerupBit) const;

	/**
	 * SERVER ONLY, called by AHCJPickup: applies Item to this ship. Returns false, changing nothing, when the ship is
	 * dead or the item would be wasted (repair at full hull, shield cell at full overcharge).
	 */
	bool ServerGiveItem(EHCJPickupItem Item);

	// ---- photon torpedoes ----

	/**
	 * Control API: fires one torpedo when one is on board and loaded (owning client -> ServerFireTorpedo; server
	 * directly, for bots and a listen host). Ends spawn protection and a cloak like any shot.
	 */
	void FireTorpedo();
	/** Torpedoes on board. Server and owning client only (0 elsewhere). */
	int32 GetTorpedoCount() const { return TorpedoCount; }
	/** Server and owning client: one is on board and the reload is over. */
	bool IsTorpedoReady() const;
	/** Server and owning client: 0 right after a shot .. 1 loaded. */
	float GetTorpedoReloadFraction() const;
	/** Local (owning client / listen host): world time of the last FireTorpedo that this machine believes went off; -1000 when never. */
	float GetLastTorpedoFireTime() const { return LastTorpedoFireTime; }

	/** Control API: back to the blaster (owning client -> ServerDropWeapon; server directly). */
	void DropWeapon();
	/**
	 * Target lock (the HUD's current target). Owning client: tells the server when it changes; the server keeps it
	 * for this ship's homing missiles, which steer for the locked ship when it is alive, uncloaked and in reach.
	 */
	void SetLockTarget(AHCJShipCharacter* NewTarget);
	/** Server and owning client: the locked ship, or nullptr. */
	AHCJShipCharacter* GetLockTarget() const { return LockTarget.Get(); }

	// ---- shield facings (cosmetic bookkeeping, valid on every machine; times are GetWorld()->GetTimeSeconds()) ----

	/** When the facing last lost strength / last went down here; -1000 when never. Facing is an EHCJShieldFacing index. */
	float GetFacingHitTime(int32 Facing) const { return (Facing >= 0 && Facing < HCJShield::NumFacings) ? FacingHitTime[Facing] : -1000.0f; }
	float GetFacingCollapseTime(int32 Facing) const { return (Facing >= 0 && Facing < HCJShield::NumFacings) ? FacingCollapseTime[Facing] : -1000.0f; }
	/** The latest of the four collapse times. */
	float GetLastFacingCollapseTime() const;
	/**
	 * SERVER ONLY (bots): where the ship that last damaged this one is, when that was at most MaxAgeSeconds ago and
	 * it still has a ship. False for turrets and the environment.
	 */
	bool GetRecentAttackerLocation(float MaxAgeSeconds, FVector& OutLocation) const;

	/** Every machine: the ship is still under its spawn shield (takes no damage; firing drops it). */
	bool IsSpawnShielded() const { return bSpawnShield && !bIsDead; }
	/** SERVER ONLY: true while incoming damage is ignored after a spawn. */
	bool IsSpawnProtected() const;

	/** SERVER ONLY (balance statistics): seconds since the first hit of the current engagement (hits less than 4 s apart); -1 when never hit. */
	float GetEngagementSeconds() const;

	// ---- ship type ----

	/** SERVER ONLY: gives the ship its type, hull (HCJShipTypes::Auto = by player id) and paint (Auto = by player colour), with full hull, shields and stock. */
	void ServerInitShipType(uint8 InType, uint8 InVariant, uint8 InSkin);
	/** Every machine. */
	EHCJShipType GetShipType() const { return static_cast<EHCJShipType>(ShipType); }
	const FHCJShipTypeDef& GetTypeDef() const { return HCJShipTypes::Get(ShipType); }

	// ---- warp drive ----

	/** Control API: the warp key (owning client: predicted, sent in the move flags; server: bots, listen host). */
	void SetWarping(bool bNewWarping);
	/** Every machine: what the drive does (owning client and server: their own simulation; others: the replicated byte). */
	EHCJWarpPhase GetWarpPhase() const;
	/** Owning client and server: 0..1 of the current phase - charge while charging, what is left of the jump at warp, how far from ready while cooling down. */
	float GetWarpMeter() const;
	/** Owning client and server: seconds until the drive is ready (0 when idle). */
	float GetWarpCooldownSeconds() const;

	// ---- cloaking device ----

	/** Control API: raises / drops the cloak (owning client -> ServerSetCloak; server directly). */
	void ToggleCloak();
	void SetCloaking(bool bNewCloaking);
	/** Meter 0..1 and whether it is locked after running dry. Server and owning client. */
	float GetCloakEnergy() const;
	bool IsCloakLocked() const;
	/** Server and owning client: cloaked, just decloaked, or the warp drive is busy - the trigger does nothing. */
	bool AreWeaponsOffline() const;
	/** Every machine, local time: when this ship was last seen dropping its cloak; -1000 when never. */
	float GetLastDecloakTime() const { return LocalDecloakTime; }

	// ---- tractor beam ----

	/** Control API: hold the tractor beam key (owning client -> ServerSetTractor; server directly). */
	void SetTractoring(bool bNewTractoring);
	/** Every machine: the ship this one holds / the ship that holds this one; nullptr when none. */
	AHCJShipCharacter* GetTractorTarget() const { return IsValid(TractorTarget) ? TractorTarget.Get() : nullptr; }
	AHCJShipCharacter* GetTractorHolder() const { return IsValid(TractorHolder) ? TractorHolder.Get() : nullptr; }

	// ---- portals (server) ----

	/**
	 * Called by the server's AHCJPortal when this ship begins overlapping its trigger.
	 * Rejected (returns false) on clients, when dead, when a request is already pending, or while the re-entry
	 * cooldown runs. Otherwise stores the request, starts the cooldown (HCJ::PortalReentryCooldown) and returns true.
	 * The teleport itself is deferred to the next server Tick (never executed inside the overlap callback):
	 * TeleportTo(GetActorLocation() + WorldDelta, GetActorRotation(), false, true) - a pure translation, so
	 * velocity, rotation and control rotation are untouched - followed by ForceNetUpdate(). The owning client is
	 * brought over by the normal character-movement correction; simulated proxies snap through replication.
	 * @param WorldDelta  AHCJPortal::GetTeleportDelta()
	 * @param DestSector  informational (logging)
	 */
	bool RequestPortalTeleport(const FVector& WorldDelta, int32 DestSector, bool bWormhole = false);

	/** SERVER ONLY: seconds until the ship may use a portal again (0 when ready). */
	float GetPortalCooldownRemaining() const;

	// ---- death (server) ----

	/**
	 * SERVER ONLY, idempotent. Kills the ship (sequence in the class comment). Killer null or equal to the ship's
	 * own controller means environment/suicide; in that case a different controller that damaged the ship
	 * within LastDamagerCreditSeconds gets the credit instead.
	 */
	void Die(AController* Killer, AActor* Causer);

protected:
	/** Client -> server fire intent. The server never trusts aim or timing from the client. */
	UFUNCTION(Server, Reliable)
	void ServerSetFiring(bool bNewFiring);

	UFUNCTION(Server, Reliable)
	void ServerDropWeapon();

	UFUNCTION(Server, Reliable)
	void ServerToggleBaseWeapon();

	/** Clients: the beam of this ship went on or off (start / stop sound). */
	UFUNCTION()
	void OnRep_PhaserBeam(uint8 OldBeam);

	/** Server tick: phaser bank, beam trace, damage steps, the replicated beam state. */
	void ServerTickPhaser(float DeltaSeconds);
	/** Not on a dedicated server, every tick: the beam quad, the glow at both ends, sparks where it hits, the loop sound. */
	void TickPhaserEffects(float Now, float DeltaSeconds);
	/** Not on a dedicated server: start / stop cue of the beam. */
	void PlayPhaserCue(bool bStart);

	/** Client -> server: "fire a torpedo now". The server decides whether one goes off. */
	UFUNCTION(Server, Reliable)
	void ServerFireTorpedo();

	UFUNCTION(Server, Reliable)
	void ServerSetLockTarget(AHCJShipCharacter* NewTarget);

	UFUNCTION()
	void OnRep_PowerupFlags();

	/**
	 * Server -> everybody who sees the ship: a shield facing absorbed a hit. Cosmetic. PackedDirection is
	 * HCJShield::PackDirection of where it landed in the ship's frame, StrengthAfter the facing's strength
	 * afterwards (0..255 = 0..1 of its capacity).
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShieldHit(uint16 PackedDirection, uint8 StrengthAfter);

	/** Every machine, bound to HealthComponent->OnHealthChanged: notes facings that lost strength or went down (HUD flash, collapse flicker and sound). */
	void HandleHealthChanged(UHCJHealthComponent* Component);
	/** Not on a dedicated server: starts a flare on the bubble. LocalDirection is in the ship's frame. */
	void StartShieldFlare(const FVector& LocalDirection, const FLinearColor& Color);
	/** Not on a dedicated server: the flicker and the sound of a facing going down. */
	void PlayFacingCollapse(EHCJShieldFacing Facing);
	/** Not on a dedicated server, only while a flare or flicker runs: pushes the ages into the bubble material; hides the bubble when they are over. */
	void TickShieldBubble(float Now);

	/** Server: ends power-ups whose time is up. */
	void ServerTickPowerups();
	/** Server: sets or clears power-up bits and applies them. */
	void ServerSetPowerup(uint8 PowerupBit, float Seconds);
	/** Every machine: pushes PowerupFlags into the movement component (overdrive) and the look (cloak). */
	void ApplyPowerupState();
	/** GetGameState()->GetServerWorldTimeSeconds(): the clock power-up end times are in. */
	float GetServerTime() const;

	UFUNCTION()
	void OnRep_IsDead();

	UFUNCTION(Server, Reliable)
	void ServerSetCloak(bool bNewCloaking);

	UFUNCTION(Server, Reliable)
	void ServerSetTractor(bool bNewTractoring);

	/** Clients: type, hull or paint arrived or changed. */
	UFUNCTION()
	void OnRep_ShipLook();
	/** Clients (and the server by hand): the drive changed phase - sounds, the flash, a log line. */
	UFUNCTION()
	void OnRep_WarpState(uint8 OldState);
	/** Owning client: the server stopped the drive. */
	UFUNCTION()
	void OnRep_WarpCancel();
	UFUNCTION()
	void OnRep_TractorTarget();
	/** Every machine: who holds this ship changed - the movement simulation gets the pull. */
	UFUNCTION()
	void OnRep_TractorHolder();

	/** Every machine: applies the row of the type table (flight model, sizes, hull / shields, look). Idempotent. */
	void ApplyShipType();
	/** Server tick: warp bookkeeping, the cloak meter and its reveals, the tractor beam. */
	void ServerTickSystems(float DeltaSeconds);
	/** Server: lets go of the held ship. bBroken: torn away (a cooldown follows) rather than released. */
	void ServerReleaseTractor(const TCHAR* Why, bool bBroken);
	/** Server: stops the warp drive (cooldown; the owning client is told). */
	void ServerCancelWarp(const TCHAR* Why);
	/** Server: no rock between this ship and Other. */
	bool HasClearLineTo(const AHCJShipCharacter& Other) const;
	/** Server: the trigger does nothing right now (cloaked, just decloaked, warp drive busy). */
	bool ServerWeaponsBlocked() const;
	/** Not on a dedicated server: the cloak material on / off the hull, by who looks (see CloakSkinState). */
	void ApplyCloakLook();
	/** Not on a dedicated server, every tick: warp streaks and flash, the tractor beam, the cloak shimmer. */
	void TickSystemEffects(float Now, float DeltaSeconds);

	/** Bound to HealthComponent->OnDepleted in BeginPlay (server): calls Die(Killer, Causer). */
	void HandleHealthDepleted(UHCJHealthComponent* Component, AController* Killer, AActor* Causer);

	/** Server tick: applies a pending portal request. */
	void ServerApplyPendingTeleport();
	/** Server tick: fires when bWantsToFire and the fire interval elapsed. */
	void ServerTickWeapon();
	/** Server tick: Die inside a black hole's event horizon; out-of-bounds damage beyond HCJ::SectorSafeRadius, Die beyond HCJ::SectorKillRadius. */
	void ServerTickBounds(float DeltaSeconds);
	/** Server: one shot of the current weapon: Pellets projectiles from GetMuzzleLocation() along GetAimRotation() within the spread cone, Owner = Instigator = this. */
	void ServerFireProjectile();
	/** Every machine: hides the meshes, disables collision and movement once bIsDead is set. */
	void ApplyDeadState();
	/** Local: applies bFirstPerson to the spring arm and hull visibility. */
	void UpdateCameraMode();
	/** Every machine: tints the hull with the owning player's colour (grey while no player state). */
	void UpdateShipColor();

	/**
	 * Not on a dedicated server, from BeginPlay: applies the ship look, creates the fallback hull material when
	 * there is no ship mesh, the engine audio component and the boost trail.
	 * Whatever fails to load keeps its basic-shape (or silent) fallback.
	 */
	void SetupShipVisuals();
	/**
	 * Not on a dedicated server, from SetupShipVisuals and again whenever the player state changes: picks the hull
	 * mesh (UHCJAssetSettings::PickShipVariant of the player id) and the skin (PickNearestSkin of the player colour),
	 * fits the mesh to ShipMeshLength, creates the hull / thruster glow dynamic materials. No-op when the look for
	 * the current player id is already applied; leaves the basic shapes alone when the mesh does not load.
	 */
	void ApplyShipLook();
	/** Not on a dedicated server, every tick: engine volume/pitch, thruster glow and boost trail from the current speed. */
	void TickShipEffects();
	/** Not on a dedicated server, once per ship: explosion effect and sound at the ship. */
	void PlayDeathEffects();

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UHCJHealthComponent> HealthComponent;

	/** Attached to the capsule; follows the control rotation; third-person arm length ThirdPersonArmLength. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * The ship body: one of UHCJAssetSettings::ShipMeshes, or a cone pointing along +X. All three body meshes: no collision,
	 * attached to GetMesh(); /Engine/BasicShapes until SetupShipVisuals replaces them.
	 */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> HullMesh;

	/** Right thruster (attached to HullMesh), or a flattened cube. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> WingMesh;

	/** Left thruster (attached to HullMesh), or a cylinder at the back. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> EngineMesh;

	/**
	 * Dynamic instance of the hull material: BasicShapeMaterial shared by the body meshes (parameter "Color"), or
	 * the ship mesh's own material (parameter UHCJAssetSettings::ShipTintParameter).
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HullMaterial;

	/** Dynamic instance of the thruster glow material, shared by both thrusters; shows the player colour. Null in the fallback. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThrusterGlowMaterial;

	/** Looping engine sound; plays only while the ship is alive and near the local view. Null without EngineSound. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> EngineAudio;

	/** Active while the ship flies faster than cruise speed. Null without BoostTrailEffect. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> BoostTrail;

	/**
	 * Aim assist: what bolts hit. A sphere a little larger than the movement collision (UHCJGameplaySettings::
	 * AimAssistRadius), query only, blocking nothing but projectiles (object type WorldDynamic), no overlap events,
	 * so portals, pads and movement are unaffected. Same on every machine.
	 */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USphereComponent> HitSphere;

	/**
	 * Cosmetic: a small unlit glowing sphere in the player colour that is scaled with the distance from the local
	 * view, so a far ship still reads as a bright point. Hidden on the own ship, near the view, cloaked or dead.
	 */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> BeaconMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeaconMaterial;

	/**
	 * Cosmetic, "seeing the enemy": the overlay material of the hull (a fresnel rim in the enemy colour that widens with
	 * distance, and a small floor that keeps the unlit side from going black), the glow of the engine and the engine
	 * trail (two quads, world space). All hidden with the hull (dead, cloaked on other machines).
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RimMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> EngineGlowMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> EngineTrailMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EngineGlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EngineTrailMaterial;

	/** The beacon is the glint quad (UHCJAssetSettings::ShipGlintMaterial) and not the older glowing sphere. */
	bool bBeaconIsGlint = false;
	/** Where the far end of the engine trail is: it follows the tail of the ship with a lag. */
	FVector TrailEnd = FVector::ZeroVector;
	bool bHasTrailEnd = false;
	/** The colours last given to the highlight materials (they follow the player colour and who the local player is). */
	bool bHighlightEnemyLook = false;
	bool bHighlightColorsSet = false;

	/**
	 * Cosmetic: the shield bubble, an ellipsoid (basic sphere) around the hull with the additive material
	 * UHCJAssetSettings::ShieldBubbleMaterial. Not visible, and not touched by Tick, except while a hit flares on it
	 * or a facing collapses - also on a cloaked ship, which a hit gives away for that moment.
	 */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> ShieldBubble;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShieldBubbleMaterial;

	/** Cosmetic, created with the ship look when the beam material loads: the beam (a quad from the muzzle to its end, rolled to the view) and a glow at each end. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PhaserBeamMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PhaserMuzzleGlow;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PhaserImpactGlow;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PhaserBeamMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PhaserGlowMaterial;

	/** Held beam sound: two looping layers, playing only while the beam of this ship runs near the local view. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> PhaserAudio;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> PhaserAudioLayer;

	/** Local look of the beam: it is shown, when sparks were last thrown, when it was last seen running. */
	bool bPhaserLook = false;
	float NextPhaserSparkTime = 0.0f;
	float LastPhaserBeamTime = -1000.0f;

	/** Local look: the two flare slots of the bubble (start time, used in turn), the collapse flicker and whether TickShieldBubble has work. */
	float ShieldFlareTime[2] = { -1000.0f, -1000.0f };
	int32 NextShieldFlareSlot = 0;
	float ShieldCollapseTime = -1000.0f;
	bool bShieldBubbleActive = false;
	/** Local bookkeeping (see GetFacingHitTime): the strengths seen last and when each facing was hit / went down. */
	float SeenFacingShield[HCJShield::NumFacings] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float FacingHitTime[HCJShield::NumFacings] = { -1000.0f, -1000.0f, -1000.0f, -1000.0f };
	float FacingCollapseTime[HCJShield::NumFacings] = { -1000.0f, -1000.0f, -1000.0f, -1000.0f };
	/** Server: earliest time of the next MulticastShieldHit. */
	float NextShieldHitSendTime = 0.0f;

	TWeakObjectPtr<AHCJShipCharacter> LockTarget;

	/** Defaults to AHCJProjectile::StaticClass() in the constructor. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Weapon")
	TSubclassOf<AHCJProjectile> ProjectileClass;

	/** Distance in front of the ship centre at which projectiles spawn (outside the collision sphere). */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Weapon")
	float MuzzleOffset = 270.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Ship")
	float SpawnProtectionSeconds = 2.0f;

	/** How long the hidden, dead ship stays around so the death replicates before destruction. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Ship")
	float DeathLingerSeconds = 1.0f;

	/** Window in which the last damager gets credit for an environment death. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Ship")
	float LastDamagerCreditSeconds = 5.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Camera")
	float ThirdPersonArmLength = 1150.0f;

	UPROPERTY(ReplicatedUsing = OnRep_IsDead)
	bool bIsDead = false;

	/** The spawn shield is up (server: mirrors IsSpawnProtected); to everybody, for the shimmer and the HUD. */
	UPROPERTY(Replicated)
	bool bSpawnShield = false;

	/** Local look: where the hull mesh sits without the bank, the current bank angle and the yaw it was derived from. */
	FQuat BaseHullRotation = FQuat::Identity;
	FVector BaseHullLocation = FVector::ZeroVector;
	float BankAngle = 0.0f;
	float LastBankYaw = 0.0f;
	bool bHasLastBankYaw = false;

	/** EHCJWeapon; to everybody (bolt looks, HUD). */
	UPROPERTY(Replicated)
	uint8 WeaponType = 0;

	/** Shots left in a pickup weapon; owner only. */
	UPROPERTY(Replicated)
	int32 Ammo = 0;

	/** EHCJPowerup bits; to everybody (cloak, quad glow, overdrive prediction on the owner). */
	UPROPERTY(ReplicatedUsing = OnRep_PowerupFlags)
	uint8 PowerupFlags = 0;

	/** Server world time at which each power-up ends, indexed by bit number; owner only (HUD timers). */
	UPROPERTY(Replicated)
	float PowerupEndTime[3] = { 0.0f, 0.0f, 0.0f };

	/** The beam, to everybody: 0 = off, else HCJPhaser::PackLength of its length. */
	UPROPERTY(ReplicatedUsing = OnRep_PhaserBeam)
	uint8 PhaserBeam = 0;

	/** The ship or turret the beam holds (it ends there on every machine), or null: it ends PhaserBeam along the nose. To everybody. */
	UPROPERTY(Replicated)
	TObjectPtr<AActor> PhaserTarget;

	/** HCJPhaser::PackCharge of the phaser bank; owner only (HUD). */
	UPROPERTY(Replicated)
	uint8 PhaserChargeRep = 127;

	/** EHCJWeapon of the base weapon (phaser or blaster); owner only (HUD). */
	UPROPERTY(Replicated)
	uint8 BaseWeaponType = 0;

	/** Torpedoes on board; owner only. */
	UPROPERTY(ReplicatedUsing = OnRep_TorpedoCount)
	uint8 TorpedoCount = 0;

	/** Owning client: nothing but a verbose log line (the HUD reads the count every frame). */
	UFUNCTION()
	void OnRep_TorpedoCount(uint8 OldCount);

	/** Server world time (GetServerTime) from which the next torpedo can be fired; owner only (HUD reload indicator). */
	UPROPERTY(Replicated)
	float TorpedoReadyTime = 0.0f;

	float LastTorpedoFireTime = -1000.0f;

	/** EHCJShipType; hull within the type and paint (HCJShipTypes::Auto = derived from the player). To everybody. */
	UPROPERTY(ReplicatedUsing = OnRep_ShipLook)
	uint8 ShipType = static_cast<uint8>(EHCJShipType::Fighter);

	UPROPERTY(ReplicatedUsing = OnRep_ShipLook)
	uint8 ShipVariant = HCJShipTypes::Auto;

	UPROPERTY(ReplicatedUsing = OnRep_ShipLook)
	uint8 ShipSkin = HCJShipTypes::Auto;

	/** EHCJWarpPhase of the drive, to everybody (effects on other machines). */
	UPROPERTY(ReplicatedUsing = OnRep_WarpState)
	uint8 WarpStateRep = 0;

	/** Counts the times the server stopped the drive; owner only. */
	UPROPERTY(ReplicatedUsing = OnRep_WarpCancel)
	uint8 WarpCancelCount = 0;

	/** The ship this one holds with its tractor beam. To everybody (the beam). */
	UPROPERTY(ReplicatedUsing = OnRep_TractorTarget)
	TObjectPtr<AHCJShipCharacter> TractorTarget;

	/** The ship whose tractor beam holds this one. To everybody (the owner predicts the pull). */
	UPROPERTY(ReplicatedUsing = OnRep_TractorHolder)
	TObjectPtr<AHCJShipCharacter> TractorHolder;

	/** HCJCloak::PackEnergy of the cloak meter; owner only (HUD). */
	UPROPERTY(Replicated)
	uint8 CloakEnergyRep = 127;

	/** Cosmetic: the cloak material instance, the streak quad in front of the own camera, the tractor beam quad. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloakMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WarpStreakMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WarpStreakMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> TractorBeamMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TractorBeamMaterial;

	/** Local look: 0 not cloaked, 1 another ship's shimmer, 2 the own ship half transparent, 3 revealed (flicker); -1 = must be applied again. */
	int32 CloakSkinState = 0;
	/** Material slot of the thruster glow (INDEX_NONE: none), to put it back after a cloak. */
	int32 ThrusterGlowSlot = INDEX_NONE;
	/** The type / hull / paint the look and the numbers were last applied for. */
	int32 AppliedTypeKey = -1;
	int32 LookTypeKey = -1;
	/** Local: the warp phase last seen and when it changed; when the cloak was last seen dropping. */
	uint8 WarpPhaseLook = 0;
	float WarpPhaseLookTime = -1000.0f;
	float LocalDecloakTime = -1000.0f;
	bool bWasCloakedLook = false;
	/** Owning client: the tractor key as last sent. */
	bool bLocalWantsTractor = false;

	/** Local look: the cloak / quad state the meshes were last set up for. */
	bool bCloakLook = false;
	bool bQuadLook = false;

	/** Server: pellet spread (never world generation). */
	FRandomStream WeaponRandom;

	/** Fire intent (see IsFiring). Not replicated. */
	bool bWantsToFire = false;
	/** Local camera mode. */
	bool bFirstPerson = false;
	/** True once a configured ship mesh replaced the basic shapes. */
	bool bUsingShipMesh = false;
	/** SetupShipVisuals ran (never on a dedicated server). */
	bool bVisualsReady = false;
	/** Player id the current ship look was picked for (INDEX_NONE: no player state yet). */
	int32 LookPlayerId = INDEX_NONE;
	/** PlayDeathEffects ran (or must not run: the ship was already dead when it arrived on this machine). */
	bool bDeathEffectsPlayed = false;
	/** Local sound cues: where the ship was last tick (a jump of a sector's width is a portal jump) and whether it boosted. */
	FVector LastEffectLocation = FVector::ZeroVector;
	FVector LastEffectVelocity = FVector::ZeroVector;
	bool bHasLastEffectLocation = false;
	bool bWasBoostLook = false;

	// ---- server-only state, all times are GetWorld()->GetTimeSeconds() ----
	FHCJPhaserBank PhaserBank;
	/** Beam time not yet turned into damage, and the earliest time of the next hit cue for the shooter. */
	float PhaserDamageSeconds = 0.0f;
	float NextPhaserHitConfirmTime = 0.0f;
	float NextFireTime = 0.0f;
	float SpawnProtectionEndTime = 0.0f;
	float PortalCooldownEndTime = 0.0f;
	bool bHasPendingTeleport = false;
	/** The pending teleport is a wormhole jump (counted separately). */
	bool bPendingWormhole = false;
	FVector PendingTeleportDelta = FVector::ZeroVector;
	TWeakObjectPtr<AController> LastDamager;
	float LastDamagerTime = -1000.0f;
	// Warp, cloak, tractor (server).
	FHCJCloakDevice Cloak;
	bool bWantsCloak = false;
	bool bWantsTractor = false;
	/** World time the cloak last dropped (weapons work HCJCloak::WeaponDelay later), the end of a cloak pad's free time, the next proximity check. */
	float DecloakTime = -1000.0f;
	float CloakFreeEndTime = 0.0f;
	float NextCloakProximityTime = 0.0f;
	/** Tractor: when the beam may be used again, the next line-of-sight check, damage taken while holding, boost time while held. */
	float TractorReadyTime = 0.0f;
	float NextTractorSightTime = 0.0f;
	float TractorDamageTaken = 0.0f;
	float TractorBoostSeconds = 0.0f;
	/** Balance statistics: first and latest hit of the current engagement. */
	float EngagementStartTime = -1.0f;
	float LastHitTime = -1000.0f;
};
