#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/HCJIntroLogic.h"
#include "HCJPlayerController.generated.h"

class AHCJShipCharacter;
class SHCJMenu;
class SHCJIntro;
class UFileMediaSource;
class UMediaPlayer;
class UMediaTexture;
enum class EHCJMenuPage : uint8;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
struct FInputKeyEventArgs;

/**
 * Human controller: owns ALL player input and the respawn request.
 *
 * INPUT (local controller only): the project has no assets, so SetupInputComponent creates the UInputAction and
 * UInputMappingContext objects with NewObject (outer = this), maps keys in code, registers the context with the
 * UEnhancedInputLocalPlayerSubsystem of the local player and binds the handlers on the UEnhancedInputComponent
 * (DefaultInput.ini already selects the Enhanced Input classes). Bindings:
 *   Move   (Axis3D)  W/S = +-X forward, D/A = +-Y right, Space/LeftCtrl (also E/Q) = +-Z up   -> AHCJShipCharacter::AddThrustInput
 *   Look   (Axis2D)  mouse XY (raw Y is positive when the mouse moves up) -> RotationInput.Yaw += X * LookSensitivity,
 *                    RotationInput.Pitch += Y * LookSensitivity. Written directly instead of AddYawInput/AddPitchInput
 *                    because those apply the legacy ini scales (InputYawScale 2.5 / InputPitchScale -2.5).
 *   Fire   (bool)    left mouse button                          -> AHCJShipCharacter::SetFiring; requests a respawn when shipless
 *   Boost  (bool)    left shift                                 -> AHCJShipCharacter::SetBoosting
 *   Scoreboard (bool) Tab, held                                 -> bScoreboardVisible
 *   Camera (bool)    V                                          -> AHCJShipCharacter::ToggleCameraMode
 *   Respawn (bool)   R / Enter                                  -> ServerRequestRespawn when shipless
 *   DropWeapon (bool) G                                         -> AHCJShipCharacter::DropWeapon (back to the blaster)
 *   Torpedo (bool)   right mouse button / X                     -> AHCJShipCharacter::FireTorpedo (one per press)
 *   Warp   (bool)    Z, held                                    -> AHCJShipCharacter::SetWarping (charge, then warp while held)
 *   Cloak  (bool)    C                                          -> AHCJShipCharacter::ToggleCloak
 *   Tractor (bool)   R, held (R also asks for a respawn when shipless) -> AHCJShipCharacter::SetTractoring
 * Handlers do nothing when there is no live ship (except Scoreboard and the respawn requests).
 *
 * GAME CONTROLLER (XInput: an Xbox pad, or a Steam Controller through Steam Input): a second mapping context with the
 * same actions and a few of its own, registered only while UHCJUserSettings::bController is on (SyncPadContext, every
 * frame). Keyboard and mouse stay as they are; both work at the same time.
 *   left stick = Move X / Y (dead zone 0.2), A / B = Move +-Z, right stick = PadLook (HCJPad::LookDelta: radial dead
 *   zone 0.2, squared response, degrees per second times the frame time), RT = Fire, LT = Torpedo, LB = Boost,
 *   RB = next target, Y = nearest enemy, X = base weapon, D-pad up = Warp (held), left = Cloak, right = Tractor (held),
 *   down = DropWeapon, right stick click = Camera, left stick click = controls overlay, Back = Scoreboard (held),
 *   Start = the in-match menu (as Esc), A = Respawn when shipless.
 * While the menu is open the pad drives the menu instead (SHCJMenu::OnKeyDown). The key prompts of the HUD show pad
 * names while the pad was the device used last (IsPadPrompts).
 *
 * MENU (local controller only): the title menu and the in-match menu are one Slate widget (SHCJMenu) that this
 * controller adds to the game viewport; while it is open the mouse cursor is shown and flight input is ignored.
 * A host started without arguments begins in the title menu (EnterStartMenu, decided by the game mode at login):
 * it has no ship, is a spectator as far as the rules go and watches a bot until it presses Play. Esc opens the
 * in-match menu (Resume, Settings, Controls, Leave to menu, Quit); F1 shows the controls overlay of the HUD; H
 * switches the in-game hints on / off.
 *
 * DEATH CAMERA: with no pawn the engine places the camera at GetSpawnLocation() with the control rotation;
 * the game mode sets the spawn location to the death position, so nothing else is needed here.
 */
UCLASS()
class HYPECUBEJAM_API AHCJPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHCJPlayerController();

	/** The possessed ship, or nullptr (dead / not spawned yet). */
	AHCJShipCharacter* GetShip() const;

	// ---- menu ----

	/** Server, for the local host, before it gets a ship: no ship, spectator, title menu (opened by the next PlayerTick). */
	void EnterStartMenu();
	bool IsInStartMenu() const { return bInStartMenu; }
	bool IsMenuOpen() const { return MenuWidget.IsValid(); }
	/** Local: the controls overlay should be drawn (F1, or the first seconds of the first ship of this process). */
	bool IsHelpVisible() const;

	/** Actions of the menu widget (local). */
	void MenuPlay();
	/**
	 * In-match menu of the host: the bot difficulty one step up or down, for the running match and for the next ones
	 * (the setting is saved). Does nothing on a client.
	 */
	void MenuStepDifficulty(int32 Step);
	/** The level the running match is played at when this machine is the host, else the level of the settings. */
	int32 GetMenuDifficulty() const;
	/**
	 * QUICK PLAY: searches the lobbies of Epic Online Services for an open match of this version and joins the best
	 * one; hosts a match (advertised as a lobby, bots filling in) when there is none or the join fails; plays a local
	 * match against bots when the service is not there. The steps are FHCJQuickPlay (Online/HCJEosLogic.h), kept by
	 * the online subsystem so that they survive the world change of a join; UpdateQuickPlay drives them.
	 */
	void MenuQuickPlay();
	void MenuCancelQuickPlay();
	void MenuJoin(const FString& Address);
	/** JOIN on the FIND GAMES page: the Index-th lobby of UHCJEOSSubsystem::GetLobbies, joined by its advertised address. */
	void MenuJoinLobby(int32 Index);
	void MenuResume();
	void MenuLeave();
	void MenuQuit();
	void MenuCancelConnect();
	/** Hangar: sends the ship chosen in UHCJUserSettings to the server (it applies to the next ship, or to one still under its spawn shield). */
	void MenuApplyShip();
	/** Client -> server: "I want this ship". Validated by AHCJGameMode::SetShipChoice; 255 = pick hull / paint for me. */
	UFUNCTION(Server, Reliable)
	void ServerSetShipChoice(uint8 Type, uint8 Variant, uint8 Skin);
	/** Esc: closes the in-match menu, or opens it. Does nothing in the title menu. */
	void MenuEscape();

	// ---- game controller ----

	/** Local: the pad was the device used last and the controller is enabled: prompts show pad names. */
	bool IsPadPrompts() const;
	/** Local: a key or axis of a device was used (the menu reports the pad keys it takes before the game sees them). */
	void NoteInputDevice(bool bGamepadKey, bool bAxis, float AxisValue);
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	/**
	 * Server -> owning client: this player's ship lost Amount points to something at SourceLocation. Cosmetic
	 * (damage direction indicator, hit flash, camera shake), so unreliable.
	 */
	UFUNCTION(Client, Unreliable)
	void ClientDamageTaken(FVector_NetQuantize SourceLocation, float Amount);

	/** Local: the last damage reported by ClientDamageTaken (local world time; very negative when none). */
	float GetLastDamageTime() const { return LastDamageTime; }
	FVector GetLastDamageSource() const { return LastDamageSource; }
	float GetLastDamageAmount() const { return LastDamageAmount; }
	/**
	 * Local: where that damage came from, seen from the own ship at that moment - the angle in degrees between its nose
	 * and the direction to the source (0 dead ahead, 180 dead astern); -1 when there was no ship to measure from.
	 */
	float GetLastDamageBearingDeg() const { return LastDamageBearingDeg; }

	// ---- target lock (local) ----

	/** The current target: a live, uncloaked ship, or nullptr. */
	AHCJShipCharacter* GetTarget() const;
	/** True while the player picked the target (T, F, middle mouse); the HUD's soft lock then leaves it alone. */
	bool IsTargetManual() const { return bManualTarget && GetTarget() != nullptr; }
	/** From the HUD every frame: the enemy nearest the crosshair. Ignored while a manual lock holds. */
	void SetAutoTarget(AHCJShipCharacter* NewTarget);
	/** T: the next enemy by distance. F: the nearest enemy. Middle mouse: holds the enemy at the crosshair (again: releases). */
	void TargetNext();
	void TargetNearest();
	void TargetUnderCrosshair();

	/** Local: how often T, F or middle mouse were used (the hint about them goes away once they were). */
	int32 GetTargetKeyPresses() const { return TargetKeyPresses; }

	/** B: switches the base weapon of the ship between phaser and blaster. */
	void ToggleBaseWeapon();

	/** H: switches the in-game hints on / off (kept in UHCJUserSettings). */
	void ToggleHints();
	/** Local world time of the last ToggleHints; very negative when never. */
	float GetHintsToggleTime() const { return HintsToggleTime; }

	/** Server -> owning client: a homing missile has picked this player's ship. Cosmetic warning (HUD line and a sound). */
	UFUNCTION(Client, Unreliable)
	void ClientMissileWarning();
	float GetLastMissileWarningTime() const { return LastMissileWarningTime; }

	/**
	 * Development aid, console command "HCJParkEnemy 5000 [right] [up]" (listen host, not in shipping builds): puts
	 * the nearest other ship (Index 1: the second nearest, ...) at rest that many uu in front of the own ship (offset to
	 * the right / up), for pictures.
	 */
	UFUNCTION(Exec)
	void HCJParkEnemy(float Distance = 5000.0f, float Right = 0.0f, float Up = 0.0f, int32 Index = 0);

	virtual void PlayerTick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---- the death camera (local, cosmetic) ----

	/**
	 * Local: the own ship was destroyed at Location while flying with Velocity; its blast comes BlastDelay seconds
	 * later. Until the next ship the view (CalcCamera: this controller is the view target while it has no pawn) starts
	 * where the chase camera was, pulls back UHCJAssetSettings::DeathCameraPullBack and turns slowly round the wreck,
	 * which it keeps in the middle - before, it sat at the place of the ship, inside its own explosion. It stays out
	 * of rocks (one line trace) and takes the shake of the blast. Nothing of this reaches the server.
	 */
	void NotifyOwnShipDestroyed(const FVector& Location, const FVector& Velocity, float BlastDelay);
	/** Local world seconds since the blast of the own ship: negative before it, very large when there was none. The HUD flashes on it. */
	float GetOwnBlastAge() const;
	/** A pawn that is not a destroyed ship. The wreck stays this controller's pawn for a moment on a client (two actors, two updates). */
	bool HasLivePawn() const;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	/** Local: true while the scoreboard key is held. The HUD also forces the scoreboard during Intermission. */
	bool IsScoreboardVisible() const { return bScoreboardVisible; }

	/**
	 * Client -> server: "respawn me now". The server forwards to AHCJGameMode::RequestRespawn, which validates
	 * everything (no ship, Playing phase, earliest respawn time reached); the client is never trusted.
	 */
	UFUNCTION(Server, Reliable)
	void ServerRequestRespawn();

	/**
	 * Development aid, console command "HCJGotoHole 0.5" (does nothing in shipping builds): asks the server to put
	 * this player's ship at InfluenceFraction of the influence radius from the nearest black hole, at rest, on
	 * the side of the sector origin. Used to test the gravity well without flying there.
	 */
	UFUNCTION(Exec)
	void HCJGotoHole(float InfluenceFraction = 0.6f);

	UFUNCTION(Server, Reliable)
	void ServerDebugGotoHole(float InfluenceFraction);

	/**
	 * Development aid, console command "HCJGive 2" (does nothing in shipping builds): gives this player's ship the
	 * pickup item with that number (EHCJPickupItem: 0 repair, 1 shield cell, 2 overdrive, 3 quad damage, 4 cloak,
	 * 5 scatter, 6 rail, 7 missiles, 8 pulse, 9 torpedoes) as if it had flown over a pad.
	 */
	UFUNCTION(Exec)
	void HCJGive(int32 Item = 2);

	/**
	 * Development aid, console command "HCJGotoWormhole" (does nothing in shipping builds): puts this player's ship
	 * at rest Distance uu in front of the wormhole mouth of its sector (or of the next sector that has one), facing it.
	 */
	UFUNCTION(Exec)
	void HCJGotoWormhole(float Distance = 2500.0f);

	UFUNCTION(Server, Reliable)
	void ServerDebugGotoWormhole(float Distance);

	/**
	 * Development aid, console command "HCJGotoStation 5000" (does nothing in shipping builds): puts this player's
	 * ship at rest Distance uu from the station of its sector (or of the next sector that has one), facing it.
	 */
	UFUNCTION(Exec)
	void HCJGotoStation(float Distance = 5000.0f);

	UFUNCTION(Server, Reliable)
	void ServerDebugGotoStation(float Distance);

	/**
	 * Development aid, console command "HCJMenuCmd <action> [argument]" (does nothing in shipping builds): what the
	 * menu buttons and keys do, for unattended runs. Actions: play, join <address>, pause (Esc), resume, leave,
	 * settings, controls, main, quit, help (F1), scores (toggles the scoreboard), endmatch (listen host: ends the match
	 * now), hurt <points> (listen host: damages the own ship from the right), hints (H), skiptutorial (the onboarding
	 * hints count as done), resethints (every hint may be shown again), die (listen host: destroys the own ship),
	 * lobbies / leaderboards (the online pages), refresh (the online page that is open), joinlobby <index>,
	 * torpedo (fires one of the own), enemytorpedo [distance] (listen host: the nearest other ship is put that far
	 * ahead, locks the own ship and fires one at it), torpedostock <n> (listen host: fills the own stock up to n),
	 * slomo <factor> (listen host: time dilation, for pictures), warp <0|1> / tractor <0|1> (the own key, held or let go),
	 * cloak (the own toggle), ship <type>[,<hull>[,<paint>]] (the hangar's choice; a listen host's ship becomes it at
	 * once), hangar (the hangar page), and for a listen host with "peace": enemyship <type>.., enemywarp <0|1>,
	 * enemycloak, enemytractor <0|1> (the nearest other ship). Game controller: nav <up|down|left|right|accept|back>
	 * (what the pad does in the open menu; logs the entry that has the focus), padtest (see TickPadTest),
	 * pad <0|1> (the SETTINGS switch).
	 */
	UFUNCTION(Exec)
	void HCJMenuCmd(const FString& Action, const FString& Argument = TEXT(""));

	/** Development aid, console command "HCJKillTurrets" (does nothing in shipping builds): destroys every station turret of the match. */
	UFUNCTION(Exec)
	void HCJKillTurrets();

	UFUNCTION(Server, Reliable)
	void ServerDebugKillTurrets();

	UFUNCTION(Server, Reliable)
	void ServerDebugGive(int32 Item);

	/**
	 * Server -> owning client: one of this player's bolts damaged another ship. Cosmetic only (hit sound and HUD
	 * hit marker through UHCJAudioDirector), so unreliable.
	 */
	UFUNCTION(Client, Unreliable)
	void ClientHitConfirm(bool bKilled);

	/** Server -> owning client: this player's ship took Item (EHCJPickupItem) from a pad. For the HUD message. */
	UFUNCTION(Client, Reliable)
	void ClientNotifyPickup(uint8 Item);

	/** Local: the last item picked up (EHCJPickupItem) and the local world time it happened; time very negative when none. */
	uint8 GetLastPickupItem() const { return LastPickupItem; }
	float GetLastPickupTime() const { return LastPickupTime; }

protected:
	/** Local: shows the menu on Page (creating the widget when needed), cursor on, flight input off. */
	void OpenMenu(EHCJMenuPage Page, const FString& Notice = FString());
	/** Local: removes the menu, cursor off, game input. */
	void CloseMenu();
	/** Bound to F1. */
	void ToggleHelp();
	/** Title menu: keeps the view on a live bot ship. */
	void UpdateMenuCamera();
	/**
	 * Local, every frame: feeds UHCJEOSSubsystem with the local score line (stats are sent at the end of a match)
	 * and, about once a second, with what a listen host advertises in its lobby (nothing while in the title menu).
	 */
	void UpdateOnline(float DeltaTime);

	/** Local controllers only (the engine creates the input component only for them). */
	virtual void SetupInputComponent() override;

	/** Creates the input actions and the mapping context (idempotent). */
	void BuildInputObjects();
	/** Adds MappingContext to the local player's Enhanced Input subsystem (no-op for non-local controllers). */
	void RegisterMappingContext();

	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleFireStarted(const FInputActionValue& Value);
	void HandleFireCompleted(const FInputActionValue& Value);
	void HandleBoostStarted(const FInputActionValue& Value);
	void HandleBoostCompleted(const FInputActionValue& Value);
	void HandleScoreboardStarted(const FInputActionValue& Value);
	void HandleScoreboardCompleted(const FInputActionValue& Value);
	void HandleToggleCamera(const FInputActionValue& Value);
	void HandleRespawn(const FInputActionValue& Value);
	void HandleDropWeapon(const FInputActionValue& Value);
	void HandleTorpedo(const FInputActionValue& Value);
	void HandleWarpStarted(const FInputActionValue& Value);
	void HandleWarpCompleted(const FInputActionValue& Value);
	void HandleCloak(const FInputActionValue& Value);
	void HandleTractorStarted(const FInputActionValue& Value);
	void HandleTractorCompleted(const FInputActionValue& Value);
	void HandlePadLook(const FInputActionValue& Value);
	void HandlePadMenu(const FInputActionValue& Value);
	void HandlePadTargetNext(const FInputActionValue& Value);
	void HandlePadTargetNearest(const FInputActionValue& Value);
	void HandlePadBaseWeapon(const FInputActionValue& Value);
	void HandlePadHelp(const FInputActionValue& Value);

	/** Local, every frame: the pad's mapping context is registered exactly while UHCJUserSettings::bController is on. */
	void SyncPadContext();
	/**
	 * Development (HCJMenuCmd padtest; not in shipping builds): presses every pad key through InputKey, one step every
	 * few frames, and logs what the ship did ("PadTest: step=... ok=1", at the end "PadTest: result=PASS|FAIL").
	 */
	void TickPadTest();

	/** Local, every frame: the turning hull in front of the camera while the hangar page is open (created / removed as needed). */
	void UpdateHangarPreview(float DeltaTime);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BoostAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ScoreboardAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RespawnAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DropWeaponAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TorpedoAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> WarpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CloakAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TractorAction;

	/** The game controller's context and the actions only it has. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> PadMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PadLookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PadMenuAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PadTargetNextAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PadTargetNearestAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PadBaseWeaponAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PadHelpAction;

	/** The pad's context is in the Enhanced Input subsystem. */
	bool bPadContextAdded = false;
	/** The pad was the device used last. */
	bool bPadActive = false;
	/** Which input handlers ran since this was last cleared (bits of EHCJInputHit in the .cpp), and the last thrust axes: read by the pad self-test. */
	uint32 InputHits = 0;
	FVector LastMoveAxes = FVector::ZeroVector;
	/** HCJMenuCmd padtest: the step being run (negative: off), frames left in it, what failed so far. */
	int32 PadTestStep = -1;
	int32 PadTestFrames = 0;
	int32 PadTestChecks = 0;
	int32 PadTestFailures = 0;
	FRotator PadTestRotation = FRotator::ZeroRotator;
	int32 PadTestCounter = 0;

	/** Hangar preview: a local actor with one mesh component, the hull material instance, what it shows and its turn. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> HangarPreview;

	UPROPERTY(Transient)
	TObjectPtr<class UStaticMeshComponent> HangarPreviewMesh;

	int32 HangarPreviewKey = -1;
	float HangarPreviewYaw = 0.0f;
	/** The ship choice of the settings was sent to the server (once per controller, and again from the hangar). */
	bool bShipChoiceSent = false;

	TWeakObjectPtr<AHCJShipCharacter> Target;
	bool bManualTarget = false;
	int32 TargetKeyPresses = 0;
	float HintsToggleTime = -1000.0f;
	float LastMissileWarningTime = -1000.0f;
	/** The pilot name of the settings was sent to the server (once per controller). */
	bool bNameSent = false;

	TSharedPtr<SHCJMenu> MenuWidget;
	bool bInStartMenu = false;

	/**
	 * Intro movie (local, once per process, in front of the title menu; see FHCJIntroFlow and HCJIntro::ShouldPlay):
	 * Content/Movies/intro_veo.mp4 through the Media Framework (WMF on Windows) into a media texture that a full-screen
	 * Slate widget shows. No sound component is attached: the movie is silent and the menu music plays under it.
	 * TickIntro returns true while the intro is on the screen; the title menu opens when it returns false.
	 */
	bool TickIntro(float DeltaTime);
	void StartIntro();
	void EndIntro();

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> IntroPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> IntroTexture;

	UPROPERTY(Transient)
	TObjectPtr<UFileMediaSource> IntroSource;

	TSharedPtr<SHCJIntro> IntroWidget;
	FHCJIntroFlow IntroFlow;
	/** Development ("HCJMenuCmd fly"): 0 off, 1 the own ship flies ahead at cruise, 2 under boost. */
	int32 DebugFlyMode = 0;
	bool bIntroChecked = false;
	bool bIntroFirstFrameLogged = false;
	/** Seconds until the next lobby update of UpdateOnline. */
	float OnlineHostTimer = 0.0f;
	/** Every frame while Quick Play runs: feeds FHCJQuickPlay and searches / joins / hosts as it says. */
	void UpdateQuickPlay();
	/** Seconds until the next "Playthrough: event=client" line (clients only; what the playthrough tests compare with the host). */
	float PlaythroughLogTimer = 5.0f;
	/** UHCJEOSSubsystem::GetSearchSerial when Quick Play asked for its search; negative: not asked yet. */
	int32 QuickPlaySearchSerial = -1;
	/** The status line of Quick Play that was last logged. */
	FString QuickPlayLoggedStatus;
	bool bHelpToggled = false;
	/** Local world time at which the first ship of this controller appeared; negative before. */
	float FirstShipTime = -1.0f;

	/** The death camera: where the wreck is and drifts, and the view at the moment the ship was lost. */
	struct FDeathCamera
	{
		bool bActive = false;
		FVector Wreck = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector StartLocation = FVector::ZeroVector;
		FRotator StartRotation = FRotator::ZeroRotator;
		float StartFOV = 95.0f;
		/** Local world time of the death and of the blast. */
		float StartTime = -1000.0f;
		float BlastTime = -1000.0f;
	};
	FDeathCamera DeathCamera;

	/**
	 * Development (-HCJBlastBench=<enemy|own|crowd>[,<count>[,<distance uu>[,<sector>]]]; listen host): measures what an
	 * explosion costs. Per round: the scene is set (HCJMenuCmd scene <sector>,dark; bots held still), the frame times
	 * of a second and a half without anything are taken, then a ship is blown up - a bot parked <distance> ahead, the
	 * own ship, or (crowd) four bots parked side by side from <distance> on, all in one frame - and the frame times of
	 * the next three and a half seconds. One "BlastBench:" log line per round (mean, median, 95th percentile and
	 * longest frame, the means of the first half second / the next second / the rest, the slowest half second) and
	 * one at the end, which leaves the first round out (it compiles shaders in the editor binary). No picture is
	 * taken while it runs (run it without -HCJCapture).
	 */
	struct FBlastBench
	{
		/** 0 off, 1 a bot ahead, 2 the own ship, 3 four bots at once. */
		int32 Mode = 0;
		int32 Total = 0;
		int32 Done = 0;
		float Distance = 1500.0f;
		int32 Sector = 5;
		/** 0 waiting for a ship, 1 settling, 2 reference, 3 blast. */
		int32 Phase = 0;
		double PhaseTime = 0.0;
		bool bParked = false;
		TArray<float> Reference;
		TArray<float> Blast;
		/** Sums over the rounds after the first. */
		int32 NumCounted = 0;
		double SumReference = 0.0;
		double SumReferenceMedian = 0.0;
		double SumBlast = 0.0;
		double SumBlastMedian = 0.0;
		double SumP95 = 0.0;
		double SumFirstHalf = 0.0;
		double SumSlowestHalf = 0.0;
		float WorstFrame = 0.0f;
		float WorstHalfSecond = 0.0f;
	};
	FBlastBench BlastBench;
	void TickBlastBench();

	float LastDamageTime = -1000.0f;
	FVector LastDamageSource = FVector::ZeroVector;
	float LastDamageAmount = 0.0f;
	float LastDamageBearingDeg = -1.0f;

	uint8 LastPickupItem = 0;
	float LastPickupTime = -1000.0f;

	/** Degrees of view rotation per unit of raw mouse delta. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|Input")
	float LookSensitivity = 0.12f;

	bool bScoreboardVisible = false;
};
