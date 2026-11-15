#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HCJHUD.generated.h"

class AHCJGameState;
class AHCJPlayerController;
class AHCJPlayerState;
class AHCJShipCharacter;
class UFont;
struct FHCJHypercubeGraph;

/**
 * Canvas-only HUD (no UMG, no assets; text is drawn with the engine's Slate font at the size it is shown, so it
 * stays sharp at every resolution). Exists only on the
 * machine of a local player and reads nothing but replicated/local state:
 *   local ship            AHCJPlayerController::GetShip -> health/shield (UHCJHealthComponent), speed, boost
 *   local player state    kills/deaths, respawn times
 *   game state            phase, time remaining, frag limit, winner, kill feed, sorted player states
 *   universe / graph      AHCJGameState::GetGraph, AHCJUniverse::GetSectorTint, player states' current sectors
 * Every Draw* helper must tolerate missing data (no game state yet, no ship, universe not built) by drawing nothing.
 *
 * Not drawn at all while the local player is in the title menu. Layout (scaled by HCJUi::ComputeUIScale):
 *   centre        crosshair at the projection of muzzle + aim * CrosshairDistance (so it matches where the server
 *                 fires even in third person); "RETURN TO SECTOR" warning beyond HCJ::SectorSafeRadius
 *   bottom left   health and shield bars with numbers
 *   bottom centre speed, BOOST indicator
 *   top left      "SECTOR 0101" (FHCJHypercubeGraph::SectorToBinaryString), kills / deaths, match clock, frag limit
 *   top right     hypercube map
 *   right, below the map   kill feed (entries younger than KillFeedSeconds)
 *   centre        respawn countdown while shipless; winner banner during Intermission
 *   full screen   scoreboard while AHCJPlayerController::IsScoreboardVisible() or during Intermission
 */
UCLASS()
class HYPECUBEJAM_API AHCJHUD : public AHUD
{
	GENERATED_BODY()

public:
	AHCJHUD();

	virtual void DrawHUD() override;

protected:
	void DrawCrosshair(const AHCJShipCharacter& Ship);
	void DrawVitals(const AHCJShipCharacter& Ship);
	void DrawFlightInfo(const AHCJShipCharacter& Ship);
	/** Warp drive, cloak and tractor beam: the lines above the crosshair (WARP CHARGING, CLOAKED, TRACTOR LOCK, CAUGHT IN TRACTOR BEAM). */
	void DrawSystems(const AHCJShipCharacter& Ship);
	/** Bottom right: weapon and ammunition, running power-ups with their seconds; centre: the last pickup for a moment. */
	void DrawLoadout(const AHCJShipCharacter& Ship, const AHCJPlayerController* Controller);
	/** Markers (diamond, item name, distance) over the pickup pads near the local ship that hold an item. */
	void DrawPickupMarkers(const AHCJShipCharacter& Ship);
	/** Out-of-bounds warning for the local ship. */
	void DrawBoundsWarning(const AHCJShipCharacter& Ship);
	/** Sector id in binary, score and match clock. LocalSector may be INDEX_NONE. */
	void DrawMatchInfo(const AHCJGameState& GameState, const AHCJPlayerState* LocalPlayerState, int32 LocalSector);
	/**
	 * Hypercube map inside the square (TopLeft, Size): one dot per sector at ProjectSector, a line per open edge,
	 * the local sector highlighted, sectors tinted with their sector tint, a small marker for every sector that
	 * holds at least one other combatant (from the replicated AHCJPlayerState::GetCurrentSector).
	 */
	void DrawHypercubeMap(const AHCJGameState& GameState, const FVector2D& TopLeft, float Size, int32 LocalSector);
	void DrawKillFeed(const AHCJGameState& GameState, const FVector2D& TopRight);
	/** Respawn countdown / "press FIRE to respawn" once the earliest respawn time has passed. */
	void DrawRespawnInfo(const AHCJGameState& GameState, const AHCJPlayerState& LocalPlayerState, bool bCompact);
	void DrawIntermission(const AHCJGameState& GameState);
	/** Table: name, kills, deaths, sector (binary); bots marked; local player's row highlighted. */
	void DrawScoreboard(const AHCJGameState& GameState, const AHCJPlayerState* LocalPlayerState);

	/**
	 * 2D position in [-1, 1]^2 of a sector on the map: sum over set bits d of a fixed per-dimension 2D axis
	 * (axes fan out at different angles and shrink with d), normalised by the sum of all axis lengths.
	 * Pure function of (Sector, Dimension), so the picture is stable for a given dimension.
	 */
	static FVector2D ProjectSector(int32 Sector, int32 Dimension);

	// ---- readability (HCJHUDReadability.cpp) ----

	/**
	 * Brackets with name, distance and a hull bar on the enemies in view (not behind a rock), arrows at the screen
	 * edge towards the nearest ones out of view, and the lead point of the current weapon for the enemy nearest to
	 * the crosshair. A cloaked ship gets none of them.
	 */
	void DrawTargets(const AHCJShipCharacter& Ship);
	/**
	 * Bottom centre: the sector around the ship seen from above (forward is up): enemies as red dots with a tick
	 * for above / below, portals, the wormhole, the station, the black hole and filled pads; over it the number of
	 * enemies in the sector or, when there is none, where the nearest one is and which way out leads there.
	 */
	void DrawRadar(const AHCJShipCharacter& Ship, const AHCJGameState& GameState);
	/**
	 * A small ship (nose up) inside four arcs, one per shield facing (fore at the top, starboard on the right): each
	 * filled from its middle by its strength and coloured by it (blue-white healthy, orange weak), white for a moment
	 * after a hit, red and blinking when down, a thin outer arc for an overcharge. The ship is drawn in the colour of
	 * its hull state. ViewerBearingDeg: where the viewer sits in that ship's frame (0 ahead, 90 to starboard), drawn
	 * as a mark outside the ring; below -360 for none. Reads replicated state only.
	 */
	void DrawShieldFacings(const AHCJShipCharacter& Ship, const FVector2D& Center, float Radius, float Alpha = 1.0f, float ViewerBearingDeg = -1000.0f);
	/** Right: name, hull, the four shield facings (and which one faces the player), distance, closing speed and "in range" of the locked target. */
	void DrawTargetPanel(const AHCJShipCharacter& Ship, const AHCJShipCharacter& Target, bool bManual);
	/**
	 * A photon torpedo is homing on the local ship (read from the replicated torpedoes this machine sees): the line
	 * "INCOMING TORPEDO", a marker on it - or a chevron at the screen edge towards it - with the distance, and a cue.
	 */
	void DrawTorpedoWarning(const AHCJShipCharacter& Ship);
	/** Remembers a panel of this frame (screen rectangle); world markers of the next frame keep clear of it. */
	void NotePanel(float X, float Y, float Width, float Height);
	/** Where the last damage came from (an arc around the centre), a flash when hit, a red vignette on a low hull. */
	void DrawDamageFeedback(const AHCJShipCharacter& Ship, const AHCJPlayerController* Controller);
	/** "PORTAL > 0101  320 m" over the portals of the current sector that are in range. */
	void DrawPortalLabels(const AHCJShipCharacter& Ship);
	/** What the marks of the map mean; returns the height used. */
	float DrawMapLegend(const FVector2D& TopLeft, float Width);
	/** The big centre line of AHCJGameState::GetCenterMessage while it is fresh; also announces a new match. */
	void DrawCenterMessage(AHCJGameState& GameState, bool bDraw);
	/** Controls and the goal of the game (F1, and by itself for the first seconds of the first ship). */
	void DrawHelp(const AHCJGameState& GameState);
	/** Outline of a circle from line segments. */
	void DrawCircle(float X, float Y, float Radius, const FLinearColor& Color, float Thickness, int32 Segments = 24);
	/** Size in pixels DrawLabel would use for Text. */
	FVector2D MeasureLabel(const FString& Text, float Scale = 1.0f, bool bLargeFont = false) const;

	/** Text helper: draws with a 1px shadow; returns the drawn text height in pixels. */
	float DrawLabel(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Scale = 1.0f, bool bCentered = false, bool bLargeFont = false);
	/** Filled bar with background; Fraction in [0, 1]. */
	void DrawBar(const FVector2D& Position, const FVector2D& Size, float Fraction, const FLinearColor& FillColor);

	// ---- hints, banner, panels (Private/UI/HCJHUDHints.cpp) ----

	/**
	 * Feeds the hint director (FHCJHintDirector, one per process) with what the local ship is doing and what is
	 * around it, and composes the text of the hint that is up. Called every frame, also while nothing is drawn.
	 */
	void UpdateHints(const AHCJShipCharacter* Ship, const AHCJGameState& GameState, const AHCJPlayerController* Controller);
	/** Bottom centre: the current hint (key caps + one line, a thin bar for the time left), the "hints on / off" notice, the way to a repair pad on a low hull. */
	void DrawHints(const AHCJShipCharacter* Ship, const AHCJPlayerController* Controller);
	/** Notices a change of sector of the local ship: starts the banner and, after a jump, the flash. */
	void UpdateSectorEvents(const AHCJShipCharacter* Ship);
	/** Top centre, for a few seconds after entering a sector: its id and what is in it. */
	void DrawSectorBanner(const AHCJGameState& GameState);
	/** A short full-screen flash in the colour of the sector just entered through a portal or wormhole. */
	void DrawJumpFlash();
	/** Text with key caps: "[W] thrust" draws W in a box. Returns the size used; with bDraw false only measures. */
	FVector2D DrawKeyMarkup(const FString& Markup, const FVector2D& Position, const FLinearColor& Color, float Scale, bool bCentered, bool bDraw = true, float Alpha = 1.0f);
	/** Dark panel; Accent with alpha > 0 adds a bar on its left edge. */
	void DrawPanel(float X, float Y, float Width, float Height, const FLinearColor& Accent, float Alpha = 1.0f);
	/** Size of the bottom-left block (shield, hull, boost); the radar sits on top of it. */
	FVector2D GetVitalsPanelSize() const;
	/** Top edge of the zone at the bottom centre that hints use. */
	float GetHintZoneTop() const;

public:
	/** Development aids (console command HCJMenuCmd): the onboarding steps count as done / every hint may be shown again. */
	static void DebugSkipOnboarding();
	static void DebugResetHints();

protected:
	/** Sector of the local player: from the ship's location when there is one, else from the player state. */
	int32 GetLocalSector(const AHCJPlayerController* Controller) const;
	/** HCJUi::ComputeUIScale of the canvas: 1 at 1920x1080; scales every size. */
	float GetUIScale() const;

	/** Enemies farther than this get no bracket or arrow (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|HUD")
	float TargetMarkerDistance = 60000.0f;

	/** Portals farther than this get no destination label (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|HUD")
	float PortalLabelDistance = 26000.0f;

	/** Radar: things farther than this sit on the rim (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|HUD")
	float RadarRange = 16000.0f;

	/** Local bookkeeping for hit feedback: last seen hull + shield of every ship, when it last dropped, floating numbers. */
	struct FShipWatch
	{
		float Vitals = 0.0f;
		float LastHitTime = -1000.0f;
	};
	TMap<TWeakObjectPtr<const AHCJShipCharacter>, FShipWatch> ShipWatches;
	struct FDamageNumber
	{
		FVector Location = FVector::ZeroVector;
		float Amount = 0.0f;
		float Time = 0.0f;
		/** Whose it is: the steps of a phaser beam on one ship add up in one number instead of ten a second. */
		TWeakObjectPtr<const AActor> Ship;
		float FirstTime = 0.0f;
	};
	TArray<FDamageNumber> DamageNumbers;

	/** HUD panels drawn this frame and in the frame before (screen rectangles): enemy markers and labels are not drawn on them. */
	TArray<FBox2D> PanelRects;
	TArray<FBox2D> PrevPanelRects;
	/** The torpedo the warning cue was last played for. */
	TWeakObjectPtr<const AActor> WarnedTorpedo;

	/** Set by DrawPortalLabels each frame: live enemies in the local sector, and the way out towards the nearest enemy (hop as FHCJRouteTable names it, INDEX_NONE when there is none or enemies are here). */
	int32 EnemiesInSector = 0;
	int32 GuideHop = INDEX_NONE;
	int32 GuideSector = INDEX_NONE;

	/** Sector banner / jump flash bookkeeping (local world time). */
	int32 BannerSector = INDEX_NONE;
	float BannerTime = -1000.0f;
	TWeakObjectPtr<const AHCJShipCharacter> BannerShip;
	float JumpFlashTime = -1000.0f;
	FLinearColor JumpFlashColor = FLinearColor::White;

	/** Hints: text of the current one (key-cap markup), a dim second line, and what it was composed for. */
	FString HintText;
	FString HintSubText;
	uint8 HintShown = 0;
	/** Local world time since which the sector has had no enemy; negative while there is one. */
	float SectorClearSince = -1.0f;
	/** Nearest active repair pad of the sector while the hull is low (world location; valid flag). */
	FVector RepairPadLocation = FVector::ZeroVector;
	bool bRepairPadKnown = false;
	/** Which of its two texts the low-hull hint on screen has. */
	bool bLowHullTextHasPad = false;
	/** What the respawn screen says about the last death of the local player. */
	FString DeathSummary;
	FString DeathTip;
	float DeathEntryTime = -1000.0f;

	/** Match number the "match start" line was last shown for. */
	int32 AnnouncedMatchNumber = 0;

	/** World distance in front of the muzzle at which the crosshair is projected. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|HUD")
	float CrosshairDistance = 15000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HCJ|HUD")
	float KillFeedSeconds = 6.0f;

	/** Pads farther than this from the local ship get no marker (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|HUD")
	float PickupMarkerDistance = 12000.0f;

	/** Map square edge at 1080p, in pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "HCJ|HUD")
	float MapSize = 260.0f;
};
