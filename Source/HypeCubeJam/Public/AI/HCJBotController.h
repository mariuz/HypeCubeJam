#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Math/RandomStream.h"
#include "Game/HCJBalance.h"
#include "HCJBotController.generated.h"

class AHCJGameState;
class AHCJPickup;
class AHCJTurret;
class AHCJShipCharacter;
class AHCJUniverse;

/** What a bot is currently doing. */
UENUM()
enum class EHCJBotState : uint8
{
	/** Fly between random free points of the current sector, looking for hostiles. */
	Wander,
	/** Chase, strafe around and shoot TargetShip. */
	Attack,
	/** Fly into the portal of TravelDim to reach another sector. */
	Travel
};

/**
 * Bot brain: an AAIController that flies the same AHCJShipCharacter as humans. SERVER ONLY (AI controllers are
 * never replicated). bWantsPlayerState = true, so each bot has an AHCJPlayerState (IsABot()) and shows on the
 * scoreboard. Spawned, named, respawned and removed exclusively by AHCJGameMode; the bot never spawns or
 * destroys ships and never respawns itself - while it has no live ship it simply idles.
 *
 * DIFFICULTY: FHCJBotSkill (Game/HCJBalance.h) for the game mode's level - aim error, reaction delay before the
 * first shot at a new target, turn rate, burst rhythm, the range from which it opens fire, how hard it strafes
 * and the hull fraction at which it breaks off a fight for a repair pad.
 *
 * NO NAVMESH. Steering is direct, in world space, every Tick:
 *  - thrust:  APawn::AddMovementInput(world direction, scale) on the ship (flying movement uses all three axes);
 *  - aim:     the bot turns its own control rotation towards DesiredAimPoint at no more than AimTurnRateDeg per
 *             second; the ship follows the control rotation and the server fires along it;
 *  - boost / fire: AHCJShipCharacter::SetBoosting / SetFiring.
 * Decisions (target choice, state changes, hunting) run in Think() every ThinkInterval seconds.
 *
 * BEHAVIOUR
 *  Wander   pick AHCJUniverse::GetRandomFreeLocation in the current sector, fly there, repeat. A hostile in the
 *           same sector within EngageRange with line of sight -> Attack. After HuntDelay seconds without a
 *           hostile -> Travel along ChooseTravelDim.
 *  Attack   target = nearest live hostile ship (any other ship: free-for-all) in the SAME sector. Aim at the lead
 *           point (ComputeLeadAimPoint) plus a small skill-based error, fire while the aim error is below
 *           FireConeDeg and line of sight is clear, keep PreferredRange by thrusting towards/away and strafe
 *           sideways, flipping the strafe direction every few seconds. Target dead, gone from the sector or out
 *           of sight for LoseTargetSeconds -> Wander.
 *  Travel   fly to AHCJUniverse::GetPortalWorldTransform(sector, TravelDim) and through it; boost when far.
 *           When GetCurrentSector() changes (the portal teleported the ship) or TravelTimeout elapses -> Wander.
 *           A hostile that comes within EngageRange/2 interrupts Travel -> Attack.
 *  ChooseTravelDim: let H = the sectors of human players' live ships (AHCJPlayerState: !IsABot() and
 *           GetCurrentSector() != INDEX_NONE), excluding the bot's own sector. If H is not empty take the one
 *           with the smallest FHCJHypercubeGraph::ShortestDistance and return NextHopDim(current, it); otherwise
 *           return a random open dimension of the current sector.
 *  TACTICS (difficulty-scaled: FHCJBotSkill::Tactics is the chance, rolled once per ship, that a bot flies "smart" at all, and
 *           again per occasion; EHCJBotTactic counts what was done):
 *           - target: PickTarget scores every hostile in sight - near, hull low, the facing it shows us weak or down, shooting
 *             at us (a flanker whose side of our shield is weak gets more), its tail towards us; minus one share for every
 *             other bot already on it, so that not everybody piles on one victim. The current target keeps a bonus.
 *           - breaking off (ThinkDisengage / TickDisengage): the facing towards the attacker is down and the hull is low (a
 *             light hull a little earlier), and there is a way out - a rock near enough to get behind
 *             (AHCJUniverse::FindCoverPoint), the cloak, a repair / shield pad -> away under boost (and at warp when the drive gets its charge), the
 *             best facing turned to the attacker (TickShieldFacing); back into the fight when the shields are back or
 *             after a few seconds. Without a way out the bot fights on.
 *           - torpedoes: at once through a facing that is down (FHCJBotSkill::ComboChance), the tractor beam first on a
 *             target in reach, so that it cannot dodge.
 *           - ship type (FHCJBotStyle in the .cpp): interceptor and scout fight close and make boosted passes around hulls
 *             that turn slower than they do; the scout cloaks readily to get away (the cloak costs the shields, so not
 *             for every approach) and, when it does approach under cloak, drops it behind its target; the gunship
 *             stands off and fires torpedoes twice as readily; the cruiser reaches for its tractor beam.
 *           - surroundings: a small rock in the line of fire is shot away with the phaser, the reach of a station's
 *             turrets is steered out of unless the bot attacks the turrets, a fight far away is reached at warp.
 *  Avoidance (always on): sphere sweep BY OBJECT TYPE ECC_WorldStatic (only asteroid ISMs have that type; ships are
 *           Pawn, portal triggers and projectiles WorldDynamic), radius AvoidanceProbeRadius, from the ship along the
 *           desired direction over AvoidanceLookAhead; on a hit, steer along the desired direction projected on
 *           the plane of the hit normal, blended with the normal. Also steer back towards the sector origin when
 *           farther than HCJ::SectorFieldRadius from it (never leave the safe radius voluntarily).
 */
UCLASS()
class HYPECUBEJAM_API AHCJBotController : public AAIController
{
	GENERATED_BODY()

public:
	AHCJBotController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void Tick(float DeltaTime) override;

	/** Development aid (HCJMenuCmd peace): every bot of this process holds still and holds its fire, for pictures. */
	static void SetDebugIdle(bool bIdle);
	/** Development: while idle, bots leave their trigger as "HCJMenuCmd enemyfire" set it (pictures of a beam seen from the side). */
	static void SetDebugIdleKeepsFire(bool bKeep);
	/**
	 * Replaces the engine's focus-based logic: the control rotation is owned by TickAim; this only makes the
	 * pawn face the control rotation (APawn::FaceRotation) when bUpdatePawn is set.
	 */
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

	EHCJBotState GetBotState() const { return BotState; }
	/** What the bot is doing, as key=value tokens for a "Playthrough:" line (the stuck-ship report of the game mode). */
	FString DescribeForLog() const;
	/** The possessed ship when it exists and is not dead, else nullptr. */
	AHCJShipCharacter* GetShip() const;
	AHCJShipCharacter* GetTargetShip() const { return TargetShip.Get(); }

protected:
	/** Seeds BotRandom (HCJ::MixSeed of FPlatformTime::Cycles and GetUniqueID). */
	virtual void PostInitializeComponents() override;
	/** Resets the brain for the new ship: state Wander, no target, timers cleared, control rotation = pawn rotation. */
	virtual void OnPossess(APawn* InPawn) override;
	/** Clears the target and any fire/boost intent. */
	virtual void OnUnPossess() override;

	// ---- decisions ----

	/** Runs every ThinkInterval: target acquisition and state transitions described in the class comment. */
	void Think(AHCJShipCharacter& Ship, const AHCJUniverse& Universe);
	void SetBotState(EHCJBotState NewState);
	/** Nearest live ship other than Ship in the same sector within MaxRange, or nullptr. */
	AHCJShipCharacter* FindNearestHostile(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe, float MaxRange) const;
	/**
	 * Nearest pad in the ship's sector within MaxRange that holds an item the ship can use right now (repair when
	 * hurt, shield cell when the shield is down, a weapon when on the blaster, a power-up it does not have) and is
	 * not behind a rock; nullptr when there is none. Only pads with an item count, so bots never wait at a pad.
	 */
	AHCJPickup* FindUsefulPickup(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe, float MaxRange) const;
	/** Dimension to travel along from FromSector (see class comment); INDEX_NONE when the sector has no portal. */
	int32 ChooseTravelDim(int32 FromSector, const AHCJUniverse& Universe);
	void PickWanderPoint(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe);
	/** No asteroid on the segment between the two ships (line trace by object type ECC_WorldStatic). */
	bool HasLineOfSight(const AHCJShipCharacter& Ship, const AHCJShipCharacter& Other) const;

	// ---- per-frame behaviour ----

	void TickWander(float DeltaTime, AHCJShipCharacter& Ship, const AHCJUniverse& Universe);
	void TickAttack(float DeltaTime, AHCJShipCharacter& Ship, const AHCJUniverse& Universe);
	void TickTravel(float DeltaTime, AHCJShipCharacter& Ship, const AHCJUniverse& Universe);

	/**
	 * Thrusts towards WorldTarget with obstacle and sector-boundary avoidance applied; Throttle in [0, 1].
	 * ExtraWorldInput (e.g. strafing) is added before avoidance. Slows down near the target when bArrive is set.
	 */
	void SteerTowards(AHCJShipCharacter& Ship, const AHCJUniverse& Universe, const FVector& WorldTarget, float Throttle, bool bArrive, const FVector& ExtraWorldInput = FVector::ZeroVector);
	/** Desired unit direction corrected by the avoidance rules; returns DesiredDirection when the way is clear. */
	FVector ComputeAvoidance(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe, const FVector& DesiredDirection) const;
	/**
	 * Point to aim at so that a projectile (constant speed AHCJShipCharacter::GetProjectileSpeed, no inherited
	 * velocity, fired from Ship's muzzle) meets Target assuming Target keeps its current velocity.
	 * Falls back to Target's location when there is no positive intercept time.
	 */
	FVector ComputeLeadAimPoint(const AHCJShipCharacter& Ship, const AHCJShipCharacter& Target) const;
	/**
	 * Shield facings: a bot that is breaking off (running for a repair pad, or travelling on) and was hit in the last
	 * few seconds turns a facing that still holds towards its attacker when one of its facings is weak or down
	 * (HCJShield::PickFacingToPresent) - it replaces DesiredAimPoint, flight is unaffected (thrust is world space).
	 * Never while the bot is shooting. It reads only its own facings, which its HUD would show a player.
	 */
	void TickShieldFacing(const AHCJShipCharacter& Ship);
	/**
	 * Photon torpedoes, every Think while attacking (HCJTorpedo::ShouldBotFire): the bot keeps its target locked (as a
	 * player's HUD does) and fires a torpedo when the target is in sight, roughly ahead and at a sensible range, and
	 * the facing the target shows it is down or weak - that is what the target panel tells a player, nothing more - or
	 * now and then as an opener. How readily depends on the difficulty.
	 */
	void ThinkTorpedo(AHCJShipCharacter& Ship, AHCJShipCharacter& Target, float TargetDistance, bool bTargetVisible);
	/**
	 * Every Think: a torpedo is homing on this ship (the HUD would say INCOMING TORPEDO) and is close: by chance
	 * (difficulty) the bot breaks away for a moment - boost, straight away from the torpedo's line of flight
	 * (SteerTowards does it while EvadeUntilTime runs).
	 */
	void ThinkTorpedoEvasion(const AHCJShipCharacter& Ship);
	/**
	 * Warp drive, cloak and tractor beam, decided once a second (and when something happens): warp on a long, clear
	 * way to a portal or a repair pad, or away from the attacker when the hull is low; the cloak for an approach from
	 * afar or a retreat; the tractor beam on a target in reach that is running or nearly finished. How readily is the
	 * difficulty (FHCJBotSkill). Not with -HCJClassic.
	 */
	void ThinkSystems(AHCJShipCharacter& Ship, const AHCJUniverse& Universe);
	/** Every tick, after the state's behaviour: holds or lets go of the warp and tractor keys, drops the cloak in time, boosts free of a beam. */
	void TickSystems(AHCJShipCharacter& Ship);
	/** Nothing solid between the ship and Point for the first MaxDistance uu (a sphere sweep the size of the ship). */
	bool IsWayClear(const AHCJShipCharacter& Ship, const FVector& Point, float MaxDistance) const;
	// ---- tactics (see the class comment) ----

	/**
	 * The ship to attack: for a bot that flies "smart" the best-scored hostile in sight within MaxRange (Current keeps a
	 * bonus and need not be in sight), else the nearest visible one - Current unless another is clearly nearer.
	 * bOutSpread: the choice is not the nearest because the nearest has more bots on it.
	 */
	AHCJShipCharacter* PickTarget(const AHCJShipCharacter& Ship, const AHCJUniverse& Universe, float MaxRange, AHCJShipCharacter* Current, bool& bOutSpread) const;
	/** Every Think: starts and ends breaking off (shields gone on the attacker's side and the hull low). */
	void ThinkDisengage(AHCJShipCharacter& Ship, const AHCJUniverse& Universe);
	/** Every tick while breaking off: flies to cover / a pad / away and holds fire. False when the bot is not breaking off. */
	bool TickDisengage(AHCJShipCharacter& Ship, const AHCJUniverse& Universe);
	bool IsDisengaging() const;
	/** Every Think while attacking a target behind a rock: a small rock in the way is shot at with the phaser for a moment. */
	void ThinkRockShot(AHCJShipCharacter& Ship, const AHCJUniverse& Universe, const AHCJShipCharacter& Target, float TargetDistance);
	void NotifyTactic(EHCJBotTactic Tactic) const;

	/** Turns the control rotation towards DesiredAimPoint, limited by AimTurnRateDeg. */
	void TickAim(float DeltaTime, const AHCJShipCharacter& Ship);
	/** Angle in degrees between the ship's aim direction and the direction to WorldPoint. */
	float GetAimErrorDegrees(const AHCJShipCharacter& Ship, const FVector& WorldPoint) const;

	// ---- tuning ----

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float ThinkInterval = 0.25f;

	/** Max control-rotation turn rate, degrees per second. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float AimTurnRateDeg = 150.0f;

	/** Random aim offset (degrees, re-rolled every think) that keeps bots beatable. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float AimErrorDeg = 2.0f;

	/** Fire only while the aim error is below this, degrees. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float FireConeDeg = 6.0f;

	/** Hostiles farther than this are ignored, uu. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float EngageRange = 16000.0f;

	/** Distance the bot tries to keep to its target, uu. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float PreferredRange = 3500.0f;

	/** Seconds a target may stay out of sight before it is dropped. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float LoseTargetSeconds = 3.0f;

	/** Seconds without any hostile in the sector before the bot travels on. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float HuntDelay = 5.0f;

	/** Give up a Travel that takes longer than this, seconds. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float TravelTimeout = 25.0f;

	/** Distance at which a wander point counts as reached, uu. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float ArrivalTolerance = 600.0f;

	/** Boost while the travel/wander target is farther than this, uu. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float BoostDistance = 6000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float AvoidanceLookAhead = 3000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float AvoidanceProbeRadius = 280.0f;

	/** Chance, every few seconds without a hostile ship, that a healthy bot goes for a live turret within TurretAttackRange. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float TurretAttackChance = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float TurretAttackRange = 11000.0f;

	/** A useful pickup this near is fetched when there is nothing to shoot at / in the middle of a fight, uu. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float PickupSeekRange = 10000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float PickupDetourRange = 4000.0f;

	/** Clearance from asteroids required for wander points, uu. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Bot")
	float WanderClearance = 800.0f;

	// ---- runtime state (server) ----

	EHCJBotState BotState = EHCJBotState::Wander;

	TWeakObjectPtr<AHCJShipCharacter> TargetShip;

	/** A station turret the bot has decided to attack (only while no hostile ship is around and it is in good shape). */
	TWeakObjectPtr<AHCJTurret> TurretGoal;
	float NextTurretDecisionTime = 0.0f;

	/** The pad the bot is flying to (re-chosen every Think; cleared when its item is gone). */
	TWeakObjectPtr<AHCJPickup> PickupGoal;

	/** World point the control rotation is turning towards. */
	FVector DesiredAimPoint = FVector::ZeroVector;
	bool bHasAimPoint = false;

	FVector WanderPoint = FVector::ZeroVector;
	bool bHasWanderPoint = false;

	/** Dimension of the portal being travelled to; INDEX_NONE outside Travel. */
	int32 TravelDim = INDEX_NONE;
	/** Sector the bot was in when Travel started (arrival = sector differs). */
	int32 TravelFromSector = INDEX_NONE;
	/** Dimension of the portal the bot last came through (not picked again for a random hop when avoidable). */
	int32 LastArrivalDim = INDEX_NONE;
	/** A wander point not reached by this time is replaced (the bot may be stuck behind a rock). */
	float WanderPointExpireTime = 0.0f;

	/** Current aim error offset in world space at the target (re-rolled in Think). */
	FVector AimErrorOffset = FVector::ZeroVector;
	/** +1 / -1 strafe direction and the time it flips next. */
	float StrafeSign = 1.0f;
	float NextStrafeFlipTime = 0.0f;

	// All times are GetWorld()->GetTimeSeconds().
	float NextThinkTime = 0.0f;
	float StateEnterTime = 0.0f;
	float LastHostileSeenTime = 0.0f;
	float LastTargetVisibleTime = 0.0f;

	/** Skill row of the match's difficulty (AHCJGameMode::GetBotDifficulty), refreshed every Think. */
	FHCJBotSkill Skill;
	/** When the current target was taken (the reaction delay runs from here) and this bot's offset in the burst rhythm. */
	float TargetAcquiredTime = 0.0f;
	float BurstPhase = 0.0f;
	/** Low on hull and a repair pad is in the sector: PickupGoal is that pad, the bot stops firing and boosts for it. */
	bool bRetreating = false;

	/** Torpedo evasion: flying EvadeDirection under boost until EvadeUntilTime; no new decision before NextEvadeDecisionTime. */
	float EvadeUntilTime = 0.0f;
	float NextEvadeDecisionTime = 0.0f;
	FVector EvadeDirection = FVector::ZeroVector;
	/** The last torpedo this bot fired (no second one at once, whatever the reload says). */
	float LastTorpedoTime = -1000.0f;

	/** TickShieldFacing: when the next "turns its facing" line may be logged. */
	float NextShieldTurnLogTime = 0.0f;

	/** Ship systems: the next decision; warp towards WarpGoal until WarpUntilTime (bWarpFlee: away from the attacker); the tractor key and the cloak are held until their times. */
	float NextSystemsTime = 0.0f;
	float WarpUntilTime = 0.0f;
	FVector WarpGoal = FVector::ZeroVector;
	bool bWarpFlee = false;
	/** The trip / the retreat the warp chance was last rolled for, so that it is rolled once. */
	int32 WarpRolledForDim = INDEX_NONE;
	bool bWarpFleeRolled = false;
	float TractorUntilTime = 0.0f;
	float CloakUntilTime = 0.0f;
	/** Caught by a beam: whether this bot boosts free was decided (once per catch), and until when it boosts. */
	bool bCaughtDecided = false;
	float BoostFreeUntilTime = 0.0f;
	/** A target that cloaked: the bot fires at where it last saw it for a moment. */
	FVector LostTargetPoint = FVector::ZeroVector;
	float LostTargetFireUntil = 0.0f;
	// ---- tactics ----

	/** This ship is flown "smart" (rolled once per ship against FHCJBotSkill::Tactics at its first Think). */
	bool bSmart = false;
	bool bSmartRolled = false;
	/** Breaking off until this time (0: not), towards DisengagePoint; the next one not before NextDisengageTime. */
	float DisengageUntilTime = 0.0f;
	float NextDisengageTime = 0.0f;
	FVector DisengagePoint = FVector::ZeroVector;
	bool bDisengageCover = false;
	bool bCoverCounted = false;
	/** The chance to break off was rolled for this occasion. */
	bool bDisengageRolled = false;
	TWeakObjectPtr<AHCJShipCharacter> DisengageThreat;
	/** bRetreating of the last Think (its rising edge is counted). */
	bool bWasRetreating = false;
	/** A rock in the line of fire that is being shot away. */
	uint32 RockShotKey = MAX_uint32;
	float RockShotUntil = 0.0f;
	float NextRockShotTime = 0.0f;
	/** The station of this sector has live turrets and the bot is not attacking them: keep this far from StationDangerLocation (0: nothing to avoid). */
	float StationDangerRadius = 0.0f;
	FVector StationDangerLocation = FVector::ZeroVector;
	mutable float NextStationSteerCountTime = 0.0f;
	/** In a fight: the target is in sight and in reach of the weapon, so the bot need not follow it into the station's reach (set by TickAttack). */
	bool bStationHoldEdge = false;
	/** The wander point is a hostile the bot is closing in on; warp towards it was started / may be tried again from this time. */
	bool bChasing = false;
	bool bWarpChase = false;
	float NextWarpChaseTime = 0.0f;
	/** A boosted pass close around the target runs until this time; the next one not before NextClosePassTime. */
	float ClosePassUntil = 0.0f;
	float NextClosePassTime = 0.0f;

	/** Per-bot randomness for behaviour (never used for world generation). */
	FRandomStream BotRandom;
};
