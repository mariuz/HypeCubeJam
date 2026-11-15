#include "Game/HCJHUD.h"

#include "Online/HCJEOSSubsystem.h"
#include "Audio/HCJAudioDirector.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HypeCubeJam.h"
#include "Game/HCJBalance.h"
#include "Game/HCJUiLogic.h"
#include "Game/HCJUserSettings.h"
#include "UI/HCJHints.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "Game/HCJGameState.h"
#include "Game/HCJGameplaySettings.h"
#include "Game/HCJPlayerController.h"
#include "Game/HCJPlayerState.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Ship/HCJHealthComponent.h"
#include "Ship/HCJPhaser.h"
#include "Ship/HCJShipCharacter.h"
#include "Ship/HCJShipMovementComponent.h"
#include "Ship/HCJTorpedo.h"
#include "World/HCJPickup.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJSpaceBackdrop.h"
#include "World/HCJTurret.h"
#include "World/HCJUniverse.h"
#include "World/HCJWormholes.h"

namespace
{
	const FLinearColor HCJTextColor(0.85f, 0.95f, 1.0f, 1.0f);
	const FLinearColor HCJDimTextColor(0.55f, 0.65f, 0.75f, 1.0f);
	const FLinearColor HCJAccentColor(0.2f, 1.0f, 0.9f, 1.0f);
	const FLinearColor HCJWarningColor(1.0f, 0.25f, 0.15f, 1.0f);
	const FLinearColor HCJPanelColor(0.0f, 0.02f, 0.05f, 0.6f);

	FString FormatClock(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha);
	}
}

AHCJHUD::AHCJHUD()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AHCJHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	// World markers keep clear of the panels of the frame before (they are drawn before most panels).
	PrevPanelRects = MoveTemp(PanelRects);
	PanelRects.Reset();

	AHCJGameState* GameState = AHCJGameState::Get(this);
	if (!GameState)
	{
		return;
	}

	const AHCJPlayerController* Controller = Cast<AHCJPlayerController>(PlayerOwner);
	if (Controller != nullptr && Controller->IsInStartMenu())
	{
		// The title menu covers the screen; behind it the bots play.
		return;
	}
	const AHCJPlayerState* LocalPlayerState = Controller ? Controller->GetPlayerState<AHCJPlayerState>() : nullptr;
	const AHCJShipCharacter* Ship = Controller ? Controller->GetShip() : nullptr;
	const bool bIntermission = GameState->GetMatchPhase() == EHCJMatchPhase::Intermission;
	// While waiting for a respawn the view stays where the ship was: so does the sector line and the map mark.
	const int32 KnownSector = GetLocalSector(Controller);
	const int32 LocalSector = (KnownSector == INDEX_NONE && !bIntermission) ? BannerSector : KnownSector;
	const float UIScale = GetUIScale();
	const float Margin = 24.0f * UIScale;

	const bool bBoard = bIntermission || (Controller && Controller->IsScoreboardVisible());
	const bool bMenuOpen = Controller != nullptr && Controller->IsMenuOpen();
	const bool bHelp = !bBoard && !bMenuOpen && Controller != nullptr && Controller->IsHelpVisible();
	const AHCJShipCharacter* LiveShip = bIntermission ? nullptr : Ship;

	UpdateSectorEvents(LiveShip);

	if (LiveShip)
	{
		DrawJumpFlash();
		DrawDamageFeedback(*Ship, Controller);
		DrawPortalLabels(*Ship);
		DrawTargets(*Ship);
		DrawCrosshair(*Ship);
		DrawVitals(*Ship);
		DrawFlightInfo(*Ship);
		DrawSystems(*Ship);
		DrawBoundsWarning(*Ship);
		DrawPickupMarkers(*Ship);
		DrawLoadout(*Ship, Controller);
		DrawRadar(*Ship, *GameState);
	}

	else if (Controller != nullptr && !bIntermission)
	{
		DrawDeathFlash(*Controller);
	}

	// After the labels: the hints use what they found out about the sector.
	UpdateHints(LiveShip, *GameState, Controller);

	DrawMatchInfo(*GameState, LocalPlayerState, LocalSector);

	// The map notes its own panel; the kill feed hangs under it.
	float MapHeight = 0.0f;
#if UE_BUILD_SHIPPING
	MapHeight = DrawHypercubeMap(*GameState, FVector2D(Canvas->ClipX - Margin, Margin), LocalSector, LiveShip);
#else
	// Development: -HCJNoMap leaves the map out (what it costs is the difference between two runs); the time the game
	// thread spends in it is logged every 2000 frames (LogHCJ Verbose).
	static const bool bNoMap = FParse::Param(FCommandLine::Get(), TEXT("HCJNoMap"));
	if (!bNoMap)
	{
		const double MapStart = FPlatformTime::Seconds();
		MapHeight = DrawHypercubeMap(*GameState, FVector2D(Canvas->ClipX - Margin, Margin), LocalSector, LiveShip);
		MapDrawSeconds += FPlatformTime::Seconds() - MapStart;
		if (++MapDrawFrames >= 2000)
		{
			UE_LOG(LogHCJ, Verbose, TEXT("HUD: the hypercube map takes %.1f microseconds of the game thread per frame (%d frames, %d sectors, canvas %.0f x %.0f)"),
				1.0e6 * MapDrawSeconds / MapDrawFrames, MapDrawFrames, GameState->GetGraph() != nullptr ? GameState->GetGraph()->NumSectors() : 0, Canvas->ClipX, Canvas->ClipY);
			MapDrawSeconds = 0.0;
			MapDrawFrames = 0;
		}
	}
#endif
	DrawKillFeed(*GameState, FVector2D(Canvas->ClipX - Margin, Margin + MapHeight + 10.0f * UIScale));

	// The big transient lines give way to the boards: they would be drawn through each other.
	const bool bCentreFree = !bBoard && !bHelp;
	DrawCenterMessage(*GameState, bCentreFree);
	if (bCentreFree && LiveShip)
	{
		DrawSectorBanner(*GameState);
	}
	if (bCentreFree && !bMenuOpen)
	{
		DrawHints(LiveShip, Controller);
	}

	if (bIntermission)
	{
		DrawIntermission(*GameState);
	}
	else if (!Ship && LocalPlayerState && !bHelp)
	{
		DrawRespawnInfo(*GameState, *LocalPlayerState, bBoard);
	}

	if (bBoard)
	{
		DrawScoreboard(*GameState, LocalPlayerState);
	}
	else if (bHelp)
	{
		DrawHelp(*GameState);
	}
}

void AHCJHUD::DrawCrosshair(const AHCJShipCharacter& Ship)
{
	if (!PlayerOwner)
	{
		return;
	}

	const FVector AimPoint = Ship.GetMuzzleLocation() + Ship.GetAimRotation().Vector() * CrosshairDistance;

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
	if (FVector::DotProduct(AimPoint - ViewLocation, ViewRotation.Vector()) <= 0.0)
	{
		return;
	}

	const FVector Screen = Canvas->Project(AimPoint, false);
	const float X = static_cast<float>(Screen.X);
	const float Y = static_cast<float>(Screen.Y);
	if (X < 0.0f || Y < 0.0f || X > Canvas->ClipX || Y > Canvas->ClipY)
	{
		return;
	}

	const float UIScale = GetUIScale();
	const float Gap = 6.0f * UIScale;
	const float Length = 12.0f * UIScale;
	const float Thickness = FMath::Max(1.0f, 2.0f * UIScale);
	const FLinearColor Color = Ship.IsFiring() ? HCJWarningColor : HCJAccentColor;

	DrawLine(X - Gap - Length, Y, X - Gap, Y, Color, Thickness);
	DrawLine(X + Gap, Y, X + Gap + Length, Y, Color, Thickness);
	DrawLine(X, Y - Gap - Length, X, Y - Gap, Color, Thickness);
	DrawLine(X, Y + Gap, X, Y + Gap + Length, Color, Thickness);
	DrawRect(Color, X - Thickness * 0.5f, Y - Thickness * 0.5f, Thickness, Thickness);

	// The cone the current weapon scatters its shots in.
	const float SpreadDegrees = Ship.GetWeaponDef().SpreadDegrees;
	if (SpreadDegrees > 0.05f && PlayerOwner->PlayerCameraManager != nullptr)
	{
		const float Radius = HCJUi::ComputeSpreadRadius(SpreadDegrees, PlayerOwner->PlayerCameraManager->GetFOVAngle(), Canvas->ClipX);
		DrawCircle(X, Y, FMath::Max(Radius, Gap + 2.0f * UIScale), WithAlpha(Color, 0.7f), FMath::Max(1.0f, 1.5f * UIScale), 32);
	}

	// Hit marker: four diagonal ticks for a moment after the server confirmed a hit; larger, longer and red for a kill.
	const UHCJAudioDirector* Audio = UHCJAudioDirector::Get(this);
	const UWorld* World = GetWorld();
	const float Now = World != nullptr ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;
	const float HitAge = Audio != nullptr ? Now - Audio->GetLastHitConfirmTime() : 1000.0f;
	const float KillAge = Audio != nullptr ? Now - Audio->GetLastKillConfirmTime() : 1000.0f;
	const bool bKill = KillAge >= 0.0f && KillAge < 0.45f;
	const float MarkAge = bKill ? KillAge : HitAge;
	const float MarkSeconds = bKill ? 0.45f : 0.22f;
	if (MarkAge >= 0.0f && MarkAge < MarkSeconds)
	{
		const float Inner = Gap + (bKill ? 4.0f : 2.0f) * UIScale;
		const float Outer = Inner + (bKill ? 16.0f : 10.0f) * UIScale;
		const FLinearColor HitColor = WithAlpha(bKill ? HCJWarningColor : FLinearColor::White, 1.0f - MarkAge / MarkSeconds);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float SX = (Corner & 1) != 0 ? 1.0f : -1.0f;
			const float SY = (Corner & 2) != 0 ? 1.0f : -1.0f;
			DrawLine(X + SX * Inner, Y + SY * Inner, X + SX * Outer, Y + SY * Outer, HitColor, bKill ? Thickness * 1.6f : Thickness * 1.2f);
		}
	}
}

FVector2D AHCJHUD::GetVitalsPanelSize() const
{
	const float UIScale = GetUIScale();
	const float Pad = 8.0f * UIScale;
	const float RowHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), 0.8f, true).Y) + 6.0f * UIScale;
	const float LabelWidth = static_cast<float>(MeasureLabel(TEXT("SHIELD"), 0.8f, true).X) + 10.0f * UIScale;
	const float ValueWidth = static_cast<float>(MeasureLabel(TEXT("0000"), 0.9f, true).X) + 8.0f * UIScale;
	// Four rows (shields: status and numbers; hull; boost) and, on the left, a square for the shield facings.
	const float Height = 2.0f * Pad + 4.0f * RowHeight;
	return FVector2D(2.0f * Pad + Height + LabelWidth + 190.0f * UIScale + ValueWidth, Height);
}

void AHCJHUD::DrawVitals(const AHCJShipCharacter& Ship)
{
	const UHCJHealthComponent* Health = Ship.GetHealthComponent();
	const UWorld* World = GetWorld();
	if (!Health || !World)
	{
		return;
	}

	// One block at the bottom left: the ship inside its four shield facings, next to it the shield status and
	// numbers, then HULL and BOOST - a label, a bar and a number each.
	const float UIScale = GetUIScale();
	const float Margin = 24.0f * UIScale;
	const float Pad = 8.0f * UIScale;
	const FVector2D PanelSize = GetVitalsPanelSize();
	const float PanelLeft = Margin;
	const float Top = Canvas->ClipY - Margin - static_cast<float>(PanelSize.Y);
	const float RowHeight = (static_cast<float>(PanelSize.Y) - 2.0f * Pad) / 4.0f;
	// The rows start to the right of the square that holds the facings.
	const float Left = PanelLeft + static_cast<float>(PanelSize.Y);
	const float LabelWidth = static_cast<float>(MeasureLabel(TEXT("SHIELD"), 0.8f, true).X) + 10.0f * UIScale;
	const float BarWidth = 190.0f * UIScale;
	const float BarHeight = FMath::Max(6.0f, RowHeight - 7.0f * UIScale);
	const float Now = static_cast<float>(World->GetTimeSeconds());

	DrawPanel(PanelLeft, Top, static_cast<float>(PanelSize.X), static_cast<float>(PanelSize.Y), FLinearColor::Transparent);

	const auto DrawRow = [&](int32 Row, const TCHAR* Label, float Fraction, const FLinearColor& Color, const FString& Value, const FLinearColor& LabelColor)
	{
		const float Y = Top + Pad + Row * RowHeight;
		const float TextY = Y + (RowHeight - static_cast<float>(MeasureLabel(TEXT("Ag"), 0.8f, true).Y)) * 0.5f - 1.0f;
		DrawLabel(Label, FVector2D(Left + Pad, TextY), LabelColor, 0.8f, false, true);
		DrawBar(FVector2D(Left + Pad + LabelWidth, Y + (RowHeight - BarHeight) * 0.5f), FVector2D(BarWidth, BarHeight), Fraction, Color);
		DrawLabel(Value, FVector2D(Left + Pad + LabelWidth + BarWidth + 8.0f * UIScale, TextY - 1.0f), LabelColor, 0.9f, false, true);
	};

	const float HealthFraction = Health->GetHealthFraction();
	const bool bLowHull = HealthFraction < 0.35f;
	const FLinearColor HealthColor = FMath::Lerp(FLinearColor(1.0f, 0.15f, 0.1f), FLinearColor(0.2f, 1.0f, 0.3f), FMath::Clamp(HealthFraction, 0.0f, 1.0f));
	const FLinearColor ShieldColor(0.2f, 0.7f, 1.0f);

	if (Health->GetMaxShield() > 0.0f)
	{
		// The ship inside its four facings.
		const float Side = static_cast<float>(PanelSize.Y);
		DrawShieldFacings(Ship, FVector2D(PanelLeft + Side * 0.5f + 2.0f * UIScale, Top + Side * 0.5f), Side * 0.5f - Pad - 3.0f * UIScale);

		// First row: the sum, or which facing is down. Second row: the four strengths.
		int32 NumDown = 0;
		int32 DownFacing = INDEX_NONE;
		for (int32 Facing = 0; Facing < HCJShield::NumFacings; ++Facing)
		{
			if (Health->IsFacingDown(Facing))
			{
				++NumDown;
				DownFacing = Facing;
			}
		}
		const bool bOvercharged = Health->GetShield() > Health->GetMaxShield() + 0.5f;
		const float TextHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), 0.8f, true).Y);
		const float Row0Y = Top + Pad + (RowHeight - TextHeight) * 0.5f - 1.0f;
		const FLinearColor DownColor = FMath::Lerp(HCJWarningColor, FLinearColor(1.0f, 0.75f, 0.6f), 0.5f + 0.5f * FMath::Sin(Now * 9.0f));
		const FString Status = NumDown == 0 ? FString::Printf(TEXT("SHIELDS  %d%s"), FMath::CeilToInt(Health->GetShield()), bOvercharged ? TEXT("  +") : TEXT(""))
			: NumDown == HCJShield::NumFacings ? FString(TEXT("SHIELDS DOWN"))
			: NumDown == 1 ? FString::Printf(TEXT("%s SHIELD DOWN"), HCJShield::FacingName(static_cast<EHCJShieldFacing>(DownFacing)))
			: FString::Printf(TEXT("%d SHIELDS DOWN"), NumDown);
		DrawLabel(Status, FVector2D(Left + Pad, Row0Y), NumDown > 0 ? DownColor : bOvercharged ? FLinearColor(0.7f, 0.95f, 1.0f) : ShieldColor, 0.8f, false, true);

		static const TCHAR* Letters[HCJShield::NumFacings] = { TEXT("F"), TEXT("A"), TEXT("P"), TEXT("S") };
		float NumberX = Left + Pad;
		const float Row1Y = Top + Pad + RowHeight + (RowHeight - TextHeight) * 0.5f - 1.0f;
		const float Slot = (LabelWidth + BarWidth) / HCJShield::NumFacings;
		for (int32 Facing = 0; Facing < HCJShield::NumFacings; ++Facing)
		{
			const bool bDown = Health->IsFacingDown(Facing);
			const FLinearColor Color = bDown ? DownColor : HCJShield::StrengthColor(Health->GetFacingFraction(Facing));
			DrawLabel(Letters[Facing], FVector2D(NumberX, Row1Y), HCJDimTextColor, 0.8f, false, true);
			DrawLabel(bDown ? FString(TEXT("--")) : FString::FromInt(FMath::CeilToInt(Health->GetFacingShield(Facing))),
				FVector2D(NumberX + static_cast<float>(MeasureLabel(TEXT("F "), 0.8f, true).X), Row1Y), Color, 0.8f, false, true);
			NumberX += Slot;
		}
	}
	DrawRow(2, TEXT("HULL"), HealthFraction, HealthColor, FString::FromInt(FMath::CeilToInt(Health->GetHealth())),
		bLowHull ? WithAlpha(HCJWarningColor, 0.65f + 0.35f * FMath::Sin(Now * 8.0f)) : HCJTextColor);

	// Boost meter: drains while boosting, refills otherwise; red while it is locked after running dry.
	if (const UHCJShipMovementComponent* Movement = Ship.GetShipMovement())
	{
		const bool bLocked = Movement->IsBoostLocked();
		const FLinearColor BarColor = bLocked ? HCJWarningColor : Movement->IsBoostActive() ? FLinearColor(1.0f, 0.85f, 0.2f) : HCJAccentColor;
		DrawRow(3, TEXT("BOOST"), Movement->GetBoostEnergy(), BarColor,
			bLocked ? FString(TEXT("WAIT")) : FString::Printf(TEXT("%d"), FMath::RoundToInt32(Movement->GetBoostEnergy() * 100.0f)), bLocked ? HCJWarningColor : HCJTextColor);
	}
}

float AHCJHUD::GetHintZoneTop() const
{
	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	return Canvas->ClipY - 24.0f * UIScale - 96.0f * TextScale;
}

void AHCJHUD::DrawFlightInfo(const AHCJShipCharacter& Ship)
{
	const float UIScale = GetUIScale();
	const float CenterX = Canvas->ClipX * 0.5f;

	// Bottom centre: the speed, under the hint zone.
	const int32 Speed = FMath::RoundToInt(static_cast<float>(Ship.GetVelocity().Size()) / 100.0f);
	const UHCJShipMovementComponent* Movement = Ship.GetShipMovement();
	const bool bBoost = Movement != nullptr && Movement->IsBoostActive();
	const FString SpeedText = FString::Printf(TEXT("SPEED  %d m/s%s"), Speed, bBoost ? TEXT("  BOOST") : (Movement != nullptr && Movement->IsOverdriven()) ? TEXT("  OVERDRIVE") : TEXT(""));
	const float SpeedHeight = static_cast<float>(MeasureLabel(SpeedText, 1.1f, true).Y);
	DrawLabel(SpeedText, FVector2D(CenterX, Canvas->ClipY - 24.0f * UIScale - SpeedHeight), bBoost ? FLinearColor(1.0f, 0.85f, 0.2f) : HCJTextColor, 1.1f, true, true);

	if (Ship.IsSpawnShielded())
	{
		// Above the crosshair, where the other warnings are: the bottom centre belongs to the own ship.
		DrawLabel(TEXT("SPAWN SHIELD  -  no damage until you fire"), FVector2D(CenterX, Canvas->ClipY * 0.235f), FLinearColor(0.8f, 0.95f, 1.0f), 1.2f, true, true);
	}
}

void AHCJHUD::DrawSystems(const AHCJShipCharacter& Ship)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const float UIScale = GetUIScale();
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const float CenterX = Canvas->ClipX * 0.5f;
	// Under the own ship, above the hint zone (the space above the crosshair belongs to target labels and warnings);
	// one line per thing that is going on.
	float Y = Canvas->ClipY * 0.79f;
	const float Pulse = 0.6f + 0.4f * FMath::Sin(Now * 11.0f);
	const FLinearColor SystemColor(0.55f, 0.8f, 1.0f);
	const auto Line = [&](const FString& Markup, const FLinearColor& Color, float Scale)
	{
		Y += static_cast<float>(DrawKeyMarkup(Markup, FVector2D(CenterX, Y), Color, Scale, true).Y) + 4.0f * UIScale;
	};

	// Tractor beam: caught in one (the way out), or holding somebody.
	if (Ship.GetTractorHolder() != nullptr)
	{
		Line(TEXT("CAUGHT IN TRACTOR BEAM  -  [Shift] BOOST TO BREAK FREE"), WithAlpha(HCJWarningColor, Pulse), 1.3f);
	}
	if (const AHCJShipCharacter* Held = Ship.GetTractorTarget())
	{
		const AHCJPlayerState* HeldState = Held->GetPlayerState<AHCJPlayerState>();
		Line(FString::Printf(TEXT("TRACTOR LOCK  -  %s  %d m"), HeldState != nullptr ? *HeldState->GetPlayerName() : TEXT("?"),
			FMath::RoundToInt32(static_cast<float>(FVector::Dist(Held->GetActorLocation(), Ship.GetActorLocation())) / 100.0f)), SystemColor, 1.2f);
	}

	// Warp drive.
	const EHCJWarpPhase Phase = Ship.GetWarpPhase();
	if (Phase == EHCJWarpPhase::Charging)
	{
		Line(TEXT("WARP CHARGING  -  keep [Z] held"), WithAlpha(SystemColor, Pulse), 1.2f);
		const float BarWidth = 220.0f * UIScale;
		DrawBar(FVector2D(CenterX - BarWidth * 0.5f, Y), FVector2D(BarWidth, FMath::Max(4.0f, 6.0f * UIScale)), Ship.GetWarpMeter(), SystemColor);
		Y += FMath::Max(4.0f, 6.0f * UIScale) + 6.0f * UIScale;
	}
	else if (Phase == EHCJWarpPhase::Active)
	{
		Line(TEXT("WARP  -  let go of [Z] to drop out"), FLinearColor::White, 1.2f);
	}

	// Cloak.
	if (Ship.IsCloaked())
	{
		Line(Ship.IsCloakRevealed() ? TEXT("CLOAK REVEALED  -  they can see you") : TEXT("CLOAKED  -  shields down, weapons offline   [C] decloak"),
			Ship.IsCloakRevealed() ? WithAlpha(HCJWarningColor, Pulse) : FLinearColor(0.75f, 0.85f, 0.9f), 1.15f);
	}
	else if (const float SinceDecloak = Now - Ship.GetLastDecloakTime(); SinceDecloak >= 0.0f && SinceDecloak < HCJCloak::WeaponDelay)
	{
		Line(FString::Printf(TEXT("DECLOAKING  -  weapons in %.1f s"), HCJCloak::WeaponDelay - SinceDecloak), HCJWarningColor, 1.15f);
	}
}

void AHCJHUD::DrawLoadout(const AHCJShipCharacter& Ship, const AHCJPlayerController* Controller)
{
	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	const float Margin = 24.0f * UIScale;
	const float Pad = 8.0f * UIScale;
	const UWorld* World = GetWorld();
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();

	const FHCJWeaponDef& Def = Ship.GetWeaponDef();
	// A base weapon (phaser or blaster) has no ammunition; the phaser has its bank instead.
	const bool bBlaster = HCJ::IsBaseWeapon(Ship.GetWeapon()) || Def.Ammo <= 0;
	const bool bPhaser = Ship.GetWeapon() == EHCJWeapon::Phaser;
	const bool bPhaserLocked = bPhaser && Ship.IsPhaserLocked();
	const FLinearColor WeaponColor = (bBlaster && !bPhaser) ? HCJTextColor : FLinearColor(Def.Color.R, Def.Color.G, Def.Color.B, 1.0f);
	// What [B] switches to, shown under a base weapon when the phaser exists.
	const bool bBaseSwitch = bBlaster && HCJPhaser::IsEnabled();
	const TCHAR* BaseSwitchText = bPhaser ? TEXT("[B] switch to blaster") : TEXT("[B] switch to phaser");

	struct FPowerRow
	{
		EHCJPickupItem Item;
		float Remaining;
		float Duration;
	};
	TArray<FPowerRow, TInlineAllocator<3>> PowerRows;
	if (Ship.HasPowerup(EHCJPowerup::Overdrive)) { PowerRows.Add({ EHCJPickupItem::Overdrive, Ship.GetPowerupTimeRemaining(EHCJPowerup::Overdrive), Gameplay.OverdriveSeconds }); }
	if (Ship.HasPowerup(EHCJPowerup::Quad)) { PowerRows.Add({ EHCJPickupItem::QuadDamage, Ship.GetPowerupTimeRemaining(EHCJPowerup::Quad), Gameplay.QuadSeconds }); }

	// Bottom right panel: WEAPON, its name and ammunition (with a bar), how to drop it, then the running power-ups.
	const float Width = 250.0f * TextScale;
	const float SmallRow = static_cast<float>(MeasureLabel(TEXT("Ag"), 0.8f, true).Y) + 2.0f * UIScale;
	const float NameRow = static_cast<float>(MeasureLabel(TEXT("Ag"), 1.15f, true).Y) + 3.0f * UIScale;
	const float BarHeight = FMath::Max(4.0f, 6.0f * UIScale);
	const float BarRow = BarHeight + 6.0f * UIScale;
	const TCHAR* DropText = Ship.GetBaseWeapon() == EHCJWeapon::Phaser ? TEXT("[G] back to phaser") : TEXT("[G] back to blaster");
	const float DropRow = bBlaster ? (bBaseSwitch ? static_cast<float>(DrawKeyMarkup(BaseSwitchText, FVector2D::ZeroVector, HCJDimTextColor, 0.85f, false, false).Y) + 3.0f * UIScale : 0.0f)
		: static_cast<float>(DrawKeyMarkup(DropText, FVector2D::ZeroVector, HCJDimTextColor, 0.85f, false, false).Y) + 3.0f * UIScale;
	const float PowerRow = static_cast<float>(MeasureLabel(TEXT("Ag"), 1.0f, true).Y) + BarRow + 2.0f * UIScale;
	// Photon torpedoes: a label with the key, one pip per torpedo the ship can carry, a thin reload bar.
	const bool bTorpedoes = HCJTorpedo::IsEnabled();
	const float PipHeight = FMath::Max(9.0f, 13.0f * UIScale);
	const float TorpedoBlock = bTorpedoes ? 6.0f * UIScale + SmallRow + PipHeight + 4.0f * UIScale + FMath::Max(3.0f, 4.0f * UIScale) + 2.0f * UIScale : 0.0f;
	// Warp drive, cloak and tractor beam: a key, a state and a thin bar each.
	const float SystemBar = FMath::Max(3.0f, 4.0f * UIScale);
	const float SystemTextRow = static_cast<float>(DrawKeyMarkup(TEXT("[Z] WARP"), FVector2D::ZeroVector, HCJDimTextColor, 0.75f, false, false).Y) + 2.0f * UIScale;
	const float SystemsBlock = 6.0f * UIScale + 3.0f * (SystemTextRow + SystemBar + 4.0f * UIScale);
	const float Height = 2.0f * Pad + SmallRow + NameRow + ((bBlaster && !bPhaser) ? 0.0f : BarRow) + DropRow + TorpedoBlock + SystemsBlock + PowerRows.Num() * PowerRow + (PowerRows.Num() > 0 ? 4.0f * UIScale : 0.0f);
	const float Left = Canvas->ClipX - Margin - Width;
	const float Top = Canvas->ClipY - Margin - Height;
	const float InnerWidth = Width - 2.0f * Pad - 4.0f * UIScale;
	const float X = Left + Pad + 4.0f * UIScale;

	DrawPanel(Left, Top, Width, Height, WeaponColor);

	float Y = Top + Pad;
	DrawLabel(TEXT("WEAPON"), FVector2D(X, Y), HCJDimTextColor, 0.8f, false, true);
	Y += SmallRow;
	DrawLabel(Def.Name, FVector2D(X, Y), WeaponColor, 1.15f, false, true);
	const float PhaserCharge = bPhaser ? Ship.GetPhaserCharge() : 0.0f;
	const FString AmmoText = bPhaserLocked ? FString(TEXT("RECHARGING")) : bPhaser ? FString::Printf(TEXT("%d %%"), FMath::RoundToInt32(PhaserCharge * 100.0f))
		: bBlaster ? FString(TEXT("no limit")) : FString::FromInt(Ship.GetAmmo());
	const float AmmoScale = ((bBlaster && !bPhaser) || bPhaserLocked) ? 0.8f : 1.15f;
	const FVector2D AmmoSize = MeasureLabel(AmmoText, AmmoScale, true);
	const bool bLowAmmo = !bBlaster && Ship.GetAmmo() <= FMath::Max(1, Def.Ammo / 4);
	DrawLabel(AmmoText, FVector2D(X + InnerWidth - AmmoSize.X, Y + (NameRow - 3.0f * UIScale - AmmoSize.Y) * 0.5f),
		bPhaserLocked ? HCJWarningColor : bPhaser ? FLinearColor::White : bBlaster ? HCJDimTextColor : bLowAmmo ? HCJWarningColor : FLinearColor::White, AmmoScale, false, true);
	Y += NameRow;
	if (bPhaser)
	{
		// The phaser bank: drains while the beam runs; emptied, it is locked (the trigger fires the blaster) until the mark.
		const float Relock = Gameplay.PhaserRelockLevel;
		const FVector2D BarPosition(X, Y + 2.0f * UIScale);
		DrawBar(BarPosition, FVector2D(InnerWidth, BarHeight), PhaserCharge, bPhaserLocked ? HCJWarningColor : WeaponColor);
		DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, bPhaserLocked ? 0.9f : 0.35f), X + InnerWidth * Relock, BarPosition.Y - 1.0f, FMath::Max(1.0f, UIScale), BarHeight + 2.0f);
		Y += BarRow;
		if (bBaseSwitch)
		{
			DrawKeyMarkup(bPhaserLocked ? TEXT("blaster until recharged") : BaseSwitchText, FVector2D(X, Y), bPhaserLocked ? HCJWarningColor : HCJDimTextColor, 0.85f, false);
			Y += DropRow;
		}
	}
	else if (bBlaster && bBaseSwitch)
	{
		DrawKeyMarkup(BaseSwitchText, FVector2D(X, Y), HCJDimTextColor, 0.85f, false);
		Y += DropRow;
	}
	if (!bBlaster)
	{
		DrawBar(FVector2D(X, Y + 2.0f * UIScale), FVector2D(InnerWidth, BarHeight), static_cast<float>(Ship.GetAmmo()) / FMath::Max(Def.Ammo, 1), bLowAmmo ? HCJWarningColor : WeaponColor);
		Y += BarRow;
		DrawKeyMarkup(DropText, FVector2D(X, Y), HCJDimTextColor, 0.85f, false);
		Y += DropRow;
	}
	if (bTorpedoes)
	{
		const FHCJWeaponDef& Torpedo = Gameplay.GetWeapon(EHCJWeapon::Torpedo);
		const FLinearColor TorpedoColor(Torpedo.Color.R, Torpedo.Color.G, Torpedo.Color.B, 1.0f);
		const int32 Count = Ship.GetTorpedoCount();
		const int32 MaxCount = FMath::Max(Gameplay.TorpedoMaxStock, 1);
		const float Reload = Ship.GetTorpedoReloadFraction();
		const bool bReady = Count > 0 && Reload >= 1.0f;
		const float Now = World != nullptr ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;

		Y += 4.0f * UIScale;
		DrawRect(FLinearColor(HCJDimTextColor.R, HCJDimTextColor.G, HCJDimTextColor.B, 0.35f), X, Y, InnerWidth, FMath::Max(1.0f, UIScale));
		Y += 2.0f * UIScale;
		DrawLabel(TEXT("TORPEDOES"), FVector2D(X, Y), HCJDimTextColor, 0.8f, false, true);
		const FString State = Count <= 0 ? FString(TEXT("EMPTY")) : bReady ? FString(TEXT("READY")) : FString(TEXT("LOADING"));
		const FLinearColor StateColor = Count <= 0 ? HCJWarningColor : bReady ? FLinearColor(0.3f, 1.0f, 0.35f) : WithAlpha(TorpedoColor, 0.65f + 0.35f * FMath::Sin(Now * 10.0f));
		DrawLabel(State, FVector2D(X + InnerWidth - MeasureLabel(State, 0.8f, true).X, Y), StateColor, 0.8f, false, true);
		Y += SmallRow;

		// Pips: filled for every torpedo on board; the one being loaded fills up from the bottom.
		const float Gap = FMath::Max(3.0f, 4.0f * UIScale);
		const float KeyWidth = static_cast<float>(DrawKeyMarkup(TEXT("[Right mouse]"), FVector2D::ZeroVector, HCJDimTextColor, 0.75f, false, false).X);
		const float PipWidth = FMath::Clamp((InnerWidth - KeyWidth - 8.0f * UIScale - (MaxCount - 1) * Gap) / MaxCount, 5.0f, 22.0f * UIScale);
		for (int32 Pip = 0; Pip < MaxCount; ++Pip)
		{
			const float PipX = X + Pip * (PipWidth + Gap);
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), PipX, Y, PipWidth, PipHeight);
			if (Pip < Count)
			{
				const bool bLoading = !bReady && Pip == Count - 1;
				const float Fill = bLoading ? FMath::Clamp(Reload, 0.08f, 1.0f) : 1.0f;
				DrawRect(bLoading ? WithAlpha(TorpedoColor, 0.6f) : TorpedoColor, PipX + 1.0f, Y + 1.0f + (PipHeight - 2.0f) * (1.0f - Fill), PipWidth - 2.0f, (PipHeight - 2.0f) * Fill);
			}
			else
			{
				DrawRect(FLinearColor(TorpedoColor.R, TorpedoColor.G, TorpedoColor.B, 0.16f), PipX + 1.0f, Y + 1.0f, PipWidth - 2.0f, PipHeight - 2.0f);
			}
		}
		DrawKeyMarkup(TEXT("[Right mouse]"), FVector2D(X + InnerWidth - KeyWidth, Y - 2.0f * UIScale), HCJDimTextColor, 0.75f, false);
		Y += PipHeight + 4.0f * UIScale;
		// Reload: runs full after every shot.
		DrawBar(FVector2D(X, Y), FVector2D(InnerWidth, FMath::Max(3.0f, 4.0f * UIScale)), Count > 0 ? Reload : 0.0f, bReady ? FLinearColor(0.3f, 1.0f, 0.35f) : TorpedoColor);
		Y += FMath::Max(3.0f, 4.0f * UIScale) + 2.0f * UIScale;
	}
	{
		const float Now = World != nullptr ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;
		const FLinearColor ReadyColor(0.3f, 1.0f, 0.35f);
		const FLinearColor SystemColor(0.55f, 0.8f, 1.0f);
		Y += 4.0f * UIScale;
		DrawRect(FLinearColor(HCJDimTextColor.R, HCJDimTextColor.G, HCJDimTextColor.B, 0.35f), X, Y, InnerWidth, FMath::Max(1.0f, UIScale));
		Y += 2.0f * UIScale;
		const auto SystemRow = [&](const TCHAR* Markup, const FString& State, const FLinearColor& StateColor, float Fraction, const FLinearColor& BarColor)
		{
			DrawKeyMarkup(Markup, FVector2D(X, Y), HCJDimTextColor, 0.75f, false);
			DrawLabel(State, FVector2D(X + InnerWidth - MeasureLabel(State, 0.8f, true).X, Y + 1.0f * UIScale), StateColor, 0.8f, false, true);
			Y += SystemTextRow;
			DrawBar(FVector2D(X, Y), FVector2D(InnerWidth, SystemBar), Fraction, BarColor);
			Y += SystemBar + 4.0f * UIScale;
		};

		// Warp drive: charge while the key is held, what is left of the jump, then the cooldown.
		switch (Ship.GetWarpPhase())
		{
		case EHCJWarpPhase::Charging:
			SystemRow(TEXT("[Z] WARP"), TEXT("CHARGING"), WithAlpha(SystemColor, 0.65f + 0.35f * FMath::Sin(Now * 12.0f)), Ship.GetWarpMeter(), SystemColor);
			break;
		case EHCJWarpPhase::Active:
			SystemRow(TEXT("[Z] WARP"), TEXT("AT WARP"), FLinearColor::White, Ship.GetWarpMeter(), FLinearColor::White);
			break;
		case EHCJWarpPhase::Cooldown:
			SystemRow(TEXT("[Z] WARP"), FString::Printf(TEXT("%d s"), FMath::CeilToInt(Ship.GetWarpCooldownSeconds())), HCJDimTextColor, 1.0f - Ship.GetWarpMeter(), HCJDimTextColor);
			break;
		default:
			SystemRow(TEXT("[Z] WARP"), Ship.IsCloaked() ? TEXT("NOT CLOAKED") : Ship.GetTractorHolder() != nullptr ? TEXT("HELD") : TEXT("READY"),
				(Ship.IsCloaked() || Ship.GetTractorHolder() != nullptr) ? HCJWarningColor : ReadyColor, 1.0f, SystemColor);
			break;
		}

		// Cloak: the meter drains while cloaked.
		const float CloakEnergy = Ship.GetCloakEnergy();
		if (Ship.IsCloaked())
		{
			const bool bRevealed = Ship.IsCloakRevealed();
			SystemRow(TEXT("[C] CLOAK"), bRevealed ? TEXT("REVEALED") : TEXT("CLOAKED"), bRevealed ? HCJWarningColor : FLinearColor::White, CloakEnergy,
				CloakEnergy < 0.25f ? HCJWarningColor : FLinearColor(0.75f, 0.85f, 0.9f));
		}
		else if (Ship.IsCloakLocked())
		{
			SystemRow(TEXT("[C] CLOAK"), TEXT("RECHARGING"), HCJWarningColor, CloakEnergy, HCJWarningColor);
		}
		else
		{
			SystemRow(TEXT("[C] CLOAK"), FString::Printf(TEXT("%d %%"), FMath::RoundToInt32(CloakEnergy * 100.0f)), CloakEnergy >= 0.999f ? ReadyColor : HCJTextColor, CloakEnergy,
				FLinearColor(0.75f, 0.85f, 0.9f));
		}

		// Tractor beam: it feeds on the phaser bank.
		const float Bank = Ship.GetPhaserCharge();
		if (Ship.GetTractorTarget() != nullptr)
		{
			SystemRow(TEXT("[R] TRACTOR"), TEXT("LOCK"), FLinearColor::White, Bank, SystemColor);
		}
		else if (Ship.GetTractorHolder() != nullptr)
		{
			SystemRow(TEXT("[R] TRACTOR"), TEXT("CAUGHT"), WithAlpha(HCJWarningColor, 0.6f + 0.4f * FMath::Sin(Now * 12.0f)), Bank, HCJWarningColor);
		}
		else
		{
			const bool bNoPower = Ship.IsPhaserLocked();
			SystemRow(TEXT("[R] TRACTOR"), bNoPower ? TEXT("NO POWER") : TEXT("READY"), bNoPower ? HCJWarningColor : ReadyColor, Bank, bNoPower ? HCJWarningColor : SystemColor);
		}
	}
	if (PowerRows.Num() > 0)
	{
		Y += 4.0f * UIScale;
	}
	for (const FPowerRow& Row : PowerRows)
	{
		const FLinearColor Color = HCJ::GetItemColor(Row.Item);
		// The last three seconds blink.
		const float Blink = (Row.Remaining < 3.0f && World != nullptr) ? 0.6f + 0.4f * FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * 12.0f) : 1.0f;
		DrawLabel(HCJ::GetItemName(Row.Item), FVector2D(X, Y), WithAlpha(Color, Blink), 1.0f, false, true);
		const FString Seconds = FString::Printf(TEXT("%d s"), FMath::CeilToInt(Row.Remaining));
		DrawLabel(Seconds, FVector2D(X + InnerWidth - MeasureLabel(Seconds, 1.0f, true).X, Y), WithAlpha(FLinearColor::White, Blink), 1.0f, false, true);
		Y += PowerRow - BarRow;
		DrawBar(FVector2D(X, Y), FVector2D(InnerWidth, BarHeight), Row.Remaining / FMath::Max(Row.Duration, 0.1f), Color);
		Y += BarRow;
	}

	// What was just picked up, for a moment, above the hint zone.
	const float Age = (Controller != nullptr && World != nullptr) ? static_cast<float>(World->GetTimeSeconds()) - Controller->GetLastPickupTime() : 1000.0f;
	if (Age >= 0.0f && Age < 2.5f)
	{
		const EHCJPickupItem Item = static_cast<EHCJPickupItem>(Controller->GetLastPickupItem());
		const FString Text = FString::Printf(TEXT("+ %s"), HCJ::GetItemName(Item));
		DrawLabel(Text, FVector2D(Canvas->ClipX * 0.5f, GetHintZoneTop() - static_cast<float>(MeasureLabel(Text, 1.6f, true).Y) - 6.0f * UIScale),
			WithAlpha(HCJ::GetItemColor(Item), FMath::Clamp(2.5f - Age, 0.0f, 1.0f)), 1.6f, true, true);
	}
}

void AHCJHUD::DrawPickupMarkers(const AHCJShipCharacter& Ship)
{
	if (!PlayerOwner)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewDirection = ViewRotation.Vector();
	const FVector ShipLocation = Ship.GetActorLocation();
	const float UIScale = GetUIScale();

	// The wormhole of this sector: where it is and where it leads.
	if (const AHCJUniverse* Universe = AHCJUniverse::Get(this); Universe != nullptr && Universe->IsBuilt())
	{
		const int32 Sector = Universe->GetSectorAtLocation(ShipLocation);
		if (Universe->HasStation(Sector))
		{
			// The station of this sector and how many of its turrets are alive (from the replicated turret actors).
			const FVector Station = Universe->GetStationLocation(Sector);
			int32 NumTurrets = 0;
			int32 LiveTurrets = 0;
			for (const TWeakObjectPtr<AHCJTurret>& Entry : AHCJTurret::GetAll())
			{
				const AHCJTurret* Turret = Entry.Get();
				if (Turret != nullptr && Turret->GetWorld() == GetWorld() && FVector::DistSquared(Turret->GetActorLocation(), Station) < FMath::Square(double(HCJ::StationBodyRadius) + 500.0))
				{
					++NumTurrets;
					LiveTurrets += Turret->IsTurretDestroyed() ? 0 : 1;
				}
			}
			const FVector Screen = Canvas->Project(Station + FVector(0.0, 0.0, 2300.0), false);
			if (FVector::DotProduct(Station - ViewLocation, ViewDirection) > 0.0 && Screen.X >= 0.0 && Screen.Y >= 0.0 && Screen.X <= Canvas->ClipX && Screen.Y <= Canvas->ClipY)
			{
				DrawLabel(FString::Printf(TEXT("STATION  %d/%d turrets  %d m"), LiveTurrets, NumTurrets, FMath::RoundToInt32(FVector::Dist(Station, ShipLocation) / 100.0)),
					FVector2D(Screen.X, Screen.Y), LiveTurrets > 0 ? HCJWarningColor : HCJDimTextColor, 1.2f, true);
			}
		}
		// (When the wormhole is the way to the enemies, DrawPortalLabels has already labelled it.)
		if (Universe->HasWormhole(Sector) && GuideHop != HCJ::WormholeHop)
		{
			const FVector Mouth = Universe->GetWormholeLocation(Sector);
			const FVector Screen = Canvas->Project(Mouth + FVector(0.0, 0.0, 700.0), false);
			if (FVector::DotProduct(Mouth - ViewLocation, ViewDirection) > 0.0 && Screen.X >= 0.0 && Screen.Y >= 0.0 && Screen.X <= Canvas->ClipX && Screen.Y <= Canvas->ClipY)
			{
				const int32 Dest = Universe->GetWormholeDest(Sector);
				FLinearColor DestColor = Universe->GetSectorTint(Dest);
				DestColor.A = 1.0f;
				DrawLabel(FString::Printf(TEXT("WORMHOLE > %s  %d m"), *FHCJHypercubeGraph::SectorToBinaryString(Dest, Universe->GetGraph().GetDimension()),
					FMath::RoundToInt32(FVector::Dist(Mouth, ShipLocation) / 100.0)),
					FVector2D(Screen.X, Screen.Y), FMath::Lerp(DestColor, FLinearColor(0.75f, 0.45f, 1.0f), 0.5f), 1.2f, true);
			}
		}
	}

	for (const TWeakObjectPtr<AHCJPickup>& Entry : AHCJPickup::GetAll())
	{
		const AHCJPickup* Pickup = Entry.Get();
		if (Pickup == nullptr || Pickup->GetWorld() != GetWorld() || !Pickup->IsActive())
		{
			continue;
		}
		const FVector Location = Pickup->GetActorLocation();
		const double Distance = FVector::Dist(Location, ShipLocation);
		if (Distance > PickupMarkerDistance || FVector::DotProduct(Location - ViewLocation, ViewDirection) <= 0.0)
		{
			continue;
		}
		const FVector Screen = Canvas->Project(Location + FVector(0.0, 0.0, 420.0), false);
		const float X = static_cast<float>(Screen.X);
		const float Y = static_cast<float>(Screen.Y);
		if (X < 0.0f || Y < 0.0f || X > Canvas->ClipX || Y > Canvas->ClipY)
		{
			continue;
		}
		// Fades in with the distance; a diamond so it is not mistaken for the square map dots.
		const float Alpha = FMath::Clamp(1.2f - static_cast<float>(Distance) / PickupMarkerDistance, 0.25f, 1.0f);
		const FLinearColor Color = WithAlpha(HCJ::GetItemColor(Pickup->GetItem()), Alpha);
		const float R = 9.0f * UIScale;
		const float Thickness = FMath::Max(1.0f, 2.0f * UIScale);
		// Not on a HUD panel (the pad itself is still there to be seen).
		const FString PadLabel = FString::Printf(TEXT("%s  %d m"), HCJ::GetItemName(Pickup->GetItem()), FMath::RoundToInt32(Distance / 100.0));
		const float PadHalfWidth = FMath::Max(R, static_cast<float>(MeasureLabel(PadLabel, 1.0f).X) * 0.5f);
		if (!HCJUi::IsMarkerClear(FBox2D(FVector2D(X - PadHalfWidth, Y - R - 18.0f * UIScale), FVector2D(X + PadHalfWidth, Y + R)), PrevPanelRects, 2.0f * UIScale))
		{
			continue;
		}
		DrawLine(X - R, Y, X, Y - R, Color, Thickness);
		DrawLine(X, Y - R, X + R, Y, Color, Thickness);
		DrawLine(X + R, Y, X, Y + R, Color, Thickness);
		DrawLine(X, Y + R, X - R, Y, Color, Thickness);
		DrawLabel(PadLabel, FVector2D(X, Y - R - 18.0f * UIScale), Color, 1.0f, true);
	}
}

void AHCJHUD::DrawBoundsWarning(const AHCJShipCharacter& Ship)
{
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	const UWorld* World = GetWorld();
	if (!Universe || !Universe->IsBuilt() || !World)
	{
		return;
	}

	const int32 Sector = Universe->GetSectorAtLocation(Ship.GetActorLocation());
	if (Sector == INDEX_NONE)
	{
		return;
	}

	// Black hole proximity: from the edge of the gravity well, urgent once only boost gets the ship out.
	const FHCJBlackHoleSettings& Hole = Universe->GetBlackHoleSettings();
	const double HoleDistance = Universe->GetBlackHoleDistance(Ship.GetActorLocation());
	if (HoleDistance >= 0.0 && HoleDistance < Hole.InfluenceRadius)
	{
		const UHCJShipMovementComponent* Movement = Ship.GetShipMovement();
		const float CruiseSpeed = Movement != nullptr ? Movement->GetCruiseSpeed() : 3000.0f;
		const bool bUrgent = HoleDistance < FHCJBlackHole::ComputeNoEscapeRadius(CruiseSpeed, Hole);
		const float HolePulse = 0.55f + 0.45f * FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * (bUrgent ? 14.0f : 5.0f));
		const float HoleCenterX = Canvas->ClipX * 0.5f;
		float HoleY = Canvas->ClipY * 0.20f;
		HoleY += DrawLabel(bUrgent ? TEXT("BLACK HOLE - BOOST AWAY") : TEXT("GRAVITY WELL"), FVector2D(HoleCenterX, HoleY), WithAlpha(HCJWarningColor, HolePulse), bUrgent ? 1.6f : 1.3f, true, true);
		DrawLabel(FString::Printf(TEXT("event horizon in %d m, pull %d m/s"),
			int32(FMath::RoundToInt32(FMath::Max(0.0, HoleDistance - Hole.HorizonRadius) / 100.0)), int32(FMath::RoundToInt32(FHCJBlackHole::ComputePullSpeed(HoleDistance, Hole) / 100.0f))),
			FVector2D(HoleCenterX, HoleY), HCJWarningColor, 1.1f, true);
	}

	const float Distance = static_cast<float>(FVector::Dist(Ship.GetActorLocation(), Universe->GetSectorOrigin(Sector)));
	if (Distance <= HCJ::SectorSafeRadius)
	{
		return;
	}

	const float Pulse = 0.55f + 0.45f * FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * 8.0f);
	const float CenterX = Canvas->ClipX * 0.5f;
	float Y = Canvas->ClipY * 0.28f;
	Y += DrawLabel(TEXT("RETURN TO SECTOR"), FVector2D(CenterX, Y), WithAlpha(HCJWarningColor, Pulse), 1.6f, true, true);

	const float ToDeath = FMath::Max(0.0f, HCJ::SectorKillRadius - Distance) / 100.0f;
	DrawLabel(FString::Printf(TEXT("hull failure in %d m"), FMath::RoundToInt(ToDeath)), FVector2D(CenterX, Y), HCJWarningColor, 1.2f, true);
}

void AHCJHUD::DrawMatchInfo(const AHCJGameState& GameState, const AHCJPlayerState* LocalPlayerState, int32 LocalSector)
{
	const float UIScale = GetUIScale();
	const float Margin = 24.0f * UIScale;
	const float LineGap = 3.0f * UIScale;
	FVector2D Position(Margin, Margin);

	const FHCJMatchConfig& Config = GameState.GetMatchConfig();
	const int32 Dimension = Config.IsValid() ? Config.Dimension : 0;

	// Line 1: where you are. Line 2 (dim): what hangs in this sector's sky (same for everybody: derived from the match seed).
	FString SectorText;
	FString SkyLabel;
	if (Dimension > 0 && LocalSector != INDEX_NONE)
	{
		SectorText = TEXT("SECTOR ") + FHCJHypercubeGraph::SectorToBinaryString(LocalSector, Dimension);
		const AHCJUniverse* SkyUniverse = GameState.GetUniverse();
		const AHCJSpaceBackdrop* Backdrop = SkyUniverse != nullptr ? SkyUniverse->GetBackdrop() : nullptr;
		SkyLabel = Backdrop != nullptr ? Backdrop->GetSkyLabel(LocalSector) : FString();
		if (SkyUniverse != nullptr && SkyUniverse->HasBlackHole(LocalSector))
		{
			SkyLabel += SkyLabel.IsEmpty() ? TEXT("BLACK HOLE") : TEXT(", BLACK HOLE");
		}
	}
	else
	{
		SectorText = TEXT("SECTOR ") + FString::ChrN(FMath::Max(Dimension, 1), TEXT('-'));
	}

	FLinearColor SectorColor = HCJAccentColor;
	if (const AHCJUniverse* Universe = GameState.GetUniverse(); Universe && Universe->IsBuilt() && LocalSector != INDEX_NONE)
	{
		SectorColor = FMath::Lerp(Universe->GetSectorTint(LocalSector), FLinearColor::White, 0.25f);
		SectorColor.A = 1.0f;
	}
	Position.Y += DrawLabel(SectorText, Position, SectorColor, 1.4f, false, true) + LineGap;
	if (!SkyLabel.IsEmpty())
	{
		Position.Y += DrawLabel(SkyLabel, Position, HCJDimTextColor, 0.95f) + LineGap;
	}
	Position.Y += 3.0f * UIScale;

	if (LocalPlayerState)
	{
		// The score against the frag limit, then how that compares with the best of the others.
		const int32 FragLimit = GameState.GetFragLimit();
		const FString Kills = FragLimit > 0 ? FString::Printf(TEXT("KILLS  %d / %d"), LocalPlayerState->GetKills(), FragLimit) : FString::Printf(TEXT("KILLS  %d"), LocalPlayerState->GetKills());
		const float KillsWidth = static_cast<float>(MeasureLabel(Kills, 1.2f, true).X);
		DrawLabel(FString::Printf(TEXT("DEATHS  %d"), LocalPlayerState->GetDeaths()), FVector2D(Position.X + KillsWidth + 18.0f * UIScale, Position.Y + 2.0f * UIScale), HCJTextColor, 1.2f);
		Position.Y += DrawLabel(Kills, Position, FLinearColor::White, 1.2f, false, true) + LineGap;

		if (!LocalPlayerState->IsOnlyASpectator())
		{
			int32 BestOtherKills = -1;
			FString BestOtherName;
			for (const APlayerState* PlayerState : GameState.PlayerArray)
			{
				const AHCJPlayerState* Other = Cast<AHCJPlayerState>(PlayerState);
				if (Other != nullptr && Other != LocalPlayerState && !Other->IsOnlyASpectator() && Other->GetKills() > BestOtherKills)
				{
					BestOtherKills = Other->GetKills();
					BestOtherName = Other->GetPlayerName();
				}
			}
			const bool bLeading = LocalPlayerState->GetKills() > BestOtherKills && LocalPlayerState->GetKills() > 0;
			Position.Y += DrawLabel(HCJHints::MakeObjectiveText(LocalPlayerState->GetKills(), BestOtherKills, BestOtherName, FragLimit), Position,
				bLeading ? FLinearColor(1.0f, 0.85f, 0.1f) : HCJAccentColor, 1.05f) + LineGap;
		}
	}

	if (GameState.HasPhaseTimeLimit())
	{
		const bool bPlaying = GameState.GetMatchPhase() == EHCJMatchPhase::Playing;
		const float Remaining = GameState.GetPhaseTimeRemaining();
		Position.Y += DrawLabel(FString::Printf(TEXT("%s  %s"), bPlaying ? TEXT("TIME") : TEXT("NEXT MATCH"), *FormatClock(Remaining)),
			Position, (bPlaying && Remaining < 60.0f) ? HCJWarningColor : HCJTextColor, 1.2f) + LineGap;
	}

	if (Config.IsValid())
	{
		Position.Y += DrawLabel(FString::Printf(TEXT("MATCH %d   %dD   SEED %d"), Config.MatchNumber, Dimension, Config.Seed), Position, HCJDimTextColor, 0.9f);
	}
	// The block of lines at the top left counts as a panel for the world markers.
	NotePanel(Margin - 4.0f * UIScale, Margin - 4.0f * UIScale, 330.0f * HCJUi::ComputeTextScale(UIScale), static_cast<float>(Position.Y) - Margin + 8.0f * UIScale);
}

void AHCJHUD::DrawKillFeed(const AHCJGameState& GameState, const FVector2D& TopRight)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float UIScale = GetUIScale();
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const float TextScale = 1.1f;
	float Y = static_cast<float>(TopRight.Y);

	// The feed ends above the TARGET panel, which starts at half the height of the screen (the larger map of 32 and
	// more sectors leaves room for four rows there, not six): when there are more lines than fit, the oldest wait out.
	int32 NumLive = 0;
	for (const FHCJKillFeedEntry& Entry : GameState.GetKillFeed())
	{
		NumLive += (Now - Entry.LocalTime >= 0.0f && Now - Entry.LocalTime < KillFeedSeconds) ? 1 : 0;
	}
	const float RowStep = static_cast<float>(MeasureLabel(TEXT("Ag"), TextScale).Y) + 4.0f * UIScale;
	const int32 NumFit = FMath::Max(2, FMath::FloorToInt32((Canvas->ClipY * 0.5f - 6.0f * UIScale - Y) / FMath::Max(RowStep, 1.0f)));
	int32 NumToSkip = FMath::Max(NumLive - NumFit, 0);

	for (const FHCJKillFeedEntry& Entry : GameState.GetKillFeed())
	{
		const float Age = Now - Entry.LocalTime;
		if (Age < 0.0f || Age >= KillFeedSeconds)
		{
			continue;
		}
		if (NumToSkip > 0)
		{
			--NumToSkip;
			continue;
		}

		// "killer [WEAPON] victim", or "victim lost to CAUSE" for a death nobody caused.
		// (With the ship types: "Ann (INT)  [PHASER]  Bot-03 (CRU)".)
		const auto WithType = [](const FString& Name, uint8 Type)
		{
			return Type < HCJShipTypes::NumTypes ? FString::Printf(TEXT("%s (%s)"), *Name, HCJShipTypes::Get(Type).ShortName) : Name;
		};
		const FString Killer = WithType(Entry.KillerName, Entry.KillerType);
		const FString Victim = WithType(Entry.VictimName, Entry.VictimType);
		const FString Text = Entry.KillerName.IsEmpty()
			? FString::Printf(TEXT("%s  lost to  %s"), *Victim, Entry.Cause.IsEmpty() ? TEXT("THE VOID") : *Entry.Cause)
			: Entry.Cause.IsEmpty() ? FString::Printf(TEXT("%s  >  %s"), *Killer, *Victim)
			: FString::Printf(TEXT("%s  [%s]  %s"), *Killer, *Entry.Cause, *Victim);

		const FVector2D Size = MeasureLabel(Text, TextScale);
		// Fade out over the last second.
		const float Alpha = FMath::Clamp(KillFeedSeconds - Age, 0.0f, 1.0f);
		const bool bLocal = Entry.bLocalKiller || Entry.bLocalVictim;
		const float Pad = 4.0f * UIScale;
		const float RowLeft = static_cast<float>(TopRight.X - Size.X) - Pad;
		DrawRect(WithAlpha(FLinearColor(0.0f, 0.02f, 0.05f, bLocal ? 0.85f : 0.55f), Alpha), RowLeft, Y - 1.0f * UIScale,
			static_cast<float>(Size.X) + 2.0f * Pad, static_cast<float>(Size.Y) + 2.0f * UIScale);
		const FLinearColor Color = Entry.bLocalVictim ? FLinearColor(1.0f, 0.62f, 0.5f) : Entry.bLocalKiller ? HCJAccentColor : HCJTextColor;
		if (bLocal)
		{
			// The local player's lines carry a bar in front.
			DrawRect(WithAlpha(Color, Alpha), RowLeft - 3.0f * UIScale, Y - 1.0f * UIScale, FMath::Max(2.0f, 3.0f * UIScale), static_cast<float>(Size.Y) + 2.0f * UIScale);
		}
		Y += DrawLabel(Text, FVector2D(TopRight.X - Size.X, Y), WithAlpha(Color, Alpha), TextScale) + 4.0f * UIScale;
	}
}

void AHCJHUD::DrawRespawnInfo(const AHCJGameState& GameState, const AHCJPlayerState& LocalPlayerState, bool bCompact)
{
	if (!LocalPlayerState.IsWaitingToRespawn())
	{
		return;
	}

	const float UIScale = GetUIScale();
	const float Now = static_cast<float>(GameState.GetServerWorldTimeSeconds());
	const float Remaining = FMath::Max(0.0f, LocalPlayerState.GetRespawnServerTime() - Now);
	const float CenterX = Canvas->ClipX * 0.5f;
	const bool bMayRespawn = Now >= LocalPlayerState.GetEarliestRespawnServerTime();
	const UWorld* World = GetWorld();

	if (bCompact)
	{
		// The scoreboard is up: one line above it.
		DrawLabel(FString::Printf(TEXT("SHIP DESTROYED  -  respawn in %d"), FMath::CeilToInt(Remaining)), FVector2D(CenterX, Canvas->ClipY * 0.16f), HCJWarningColor, 1.4f, true, true);
		return;
	}

	// What the kill feed says about this death: the last entry with the local player as the victim.
	for (const FHCJKillFeedEntry& Entry : GameState.GetKillFeed())
	{
		if (Entry.bLocalVictim && Entry.LocalTime > DeathEntryTime)
		{
			DeathEntryTime = Entry.LocalTime;
			const EHCJDeathCause Cause = HCJHints::ClassifyDeath(Entry.KillerName, Entry.Cause);
			switch (Cause)
			{
			case EHCJDeathCause::Turret: DeathSummary = TEXT("Shot down by a station turret"); break;
			case EHCJDeathCause::BlackHole: DeathSummary = TEXT("Pulled into a black hole"); break;
			case EHCJDeathCause::Void: DeathSummary = TEXT("Lost outside the sector"); break;
			default: DeathSummary = Entry.Cause.IsEmpty() ? FString::Printf(TEXT("Destroyed by %s"), *Entry.KillerName)
				: FString::Printf(TEXT("Destroyed by %s with the %s"), *Entry.KillerName, *Entry.Cause); break;
			}
			DeathTip = HCJHints::PickDeathTip(Cause, LocalPlayerState.GetDeaths());
			// From which side: where the last damage came from, as the pilot saw it, when that was the hit that did it.
			const AHCJPlayerController* LocalController = Cast<AHCJPlayerController>(PlayerOwner);
			if (Cause == EHCJDeathCause::Player && LocalController != nullptr && World != nullptr
				&& World->GetTimeSeconds() - LocalController->GetLastDamageTime() < 1.5 && LocalController->GetLastDamageBearingDeg() >= 0.0f)
			{
				const EHCJHitSide Side = HCJUi::ClassifyHitSide(LocalController->GetLastDamageBearingDeg());
				DeathSummary += Side == EHCJHitSide::Behind ? TEXT("  -  from behind") : Side == EHCJHitSide::Side ? TEXT("  -  from the side") : TEXT("  -  head-on");
				if (Side == EHCJHitSide::Behind)
				{
					DeathTip = TEXT("Shot from behind: watch the lower half of the radar and the '!' arrows, then turn or boost away.");
				}
			}
			if (Entry.KillerName.IsEmpty() && Entry.Cause == TEXT("OWN TORPEDO"))
			{
				DeathSummary = TEXT("Caught in the blast of your own torpedo");
				DeathTip = TEXT("A torpedo arms a few metres out and its blast reaches about as far: never fire one at something right in front of you.");
			}
		}
	}

	const bool bMessageShown = World != nullptr && World->GetTimeSeconds() - GameState.GetCenterMessage().LocalTime < 3.0;
	if (!bMessageShown)
	{
		DrawLabel(TEXT("SHIP DESTROYED"), FVector2D(CenterX, Canvas->ClipY * 0.30f), HCJWarningColor, 1.8f, true, true);
	}
	// Below the middle of the screen: the death camera keeps the wreck there.
	float Y = Canvas->ClipY * 0.69f;
	Y += DrawLabel(FString::Printf(TEXT("RESPAWN IN  %d"), FMath::CeilToInt(Remaining)), FVector2D(CenterX, Y), HCJTextColor, 1.8f, true, true) + 6.0f * UIScale;
	if (bMayRespawn)
	{
		Y += static_cast<float>(DrawKeyMarkup(TEXT("[Left mouse] or [R] respawn now"), FVector2D(CenterX, Y), HCJAccentColor, 1.1f, true).Y);
	}

	// Who or what did it, and (with hints on) one thing to try next time.
	const bool bFresh = World != nullptr && World->GetTimeSeconds() - DeathEntryTime < 30.0;
	if (bFresh && !DeathSummary.IsEmpty())
	{
		const bool bTip = UHCJUserSettings::Get().bHints && !DeathTip.IsEmpty();
		const float Scale = 1.05f;
		const FVector2D SummarySize = MeasureLabel(DeathSummary, 1.15f, true);
		const FVector2D TipSize = bTip ? DrawKeyMarkup(DeathTip, FVector2D::ZeroVector, HCJTextColor, Scale, true, false) : FVector2D::ZeroVector;
		const float Pad = 10.0f * UIScale;
		const float Width = FMath::Max(static_cast<float>(SummarySize.X), static_cast<float>(TipSize.X)) + 4.0f * Pad;
		const float Height = 2.0f * Pad + static_cast<float>(SummarySize.Y) + (bTip ? static_cast<float>(TipSize.Y) + 6.0f * UIScale : 0.0f);
		const float Top = Canvas->ClipY * 0.81f;
		DrawPanel(CenterX - Width * 0.5f, Top, Width, Height, HCJWarningColor);
		DrawLabel(DeathSummary, FVector2D(CenterX, Top + Pad), FLinearColor(1.0f, 0.62f, 0.5f), 1.15f, true, true);
		if (bTip)
		{
			DrawKeyMarkup(DeathTip, FVector2D(CenterX, Top + Pad + SummarySize.Y + 6.0f * UIScale), HCJTextColor, Scale, true);
		}
	}
}

void AHCJHUD::DrawIntermission(const AHCJGameState& GameState)
{
	const float UIScale = GetUIScale();
	const float CenterX = Canvas->ClipX * 0.5f;
	float Y = Canvas->ClipY * 0.07f;

	Y += DrawLabel(TEXT("MATCH OVER"), FVector2D(CenterX, Y), HCJAccentColor, 2.4f, true, true) + 6.0f * UIScale;

	if (const AHCJPlayerState* Winner = GameState.GetMatchWinner())
	{
		const bool bLocalWinner = PlayerOwner != nullptr && Winner == PlayerOwner->PlayerState;
		FLinearColor WinnerColor = Winner->GetPlayerColor();
		WinnerColor.A = 1.0f;
		Y += DrawLabel(bLocalWinner ? FString::Printf(TEXT("YOU WIN  -  %d kills"), Winner->GetKills())
			: FString::Printf(TEXT("WINNER: %s  -  %d kills"), *Winner->GetPlayerName(), Winner->GetKills()),
			FVector2D(CenterX, Y), bLocalWinner ? FLinearColor(1.0f, 0.8f, 0.1f) : WinnerColor, 1.9f, true, true) + 6.0f * UIScale;
	}

	if (GameState.HasPhaseTimeLimit())
	{
		DrawLabel(FString::Printf(TEXT("next match in a new hypercube in  %d"), FMath::CeilToInt(GameState.GetPhaseTimeRemaining())),
			FVector2D(CenterX, Y), HCJTextColor, 1.4f, true);
	}

	// Whether this match reached the online leaderboards (nothing when there is no online service).
	if (const UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(this))
	{
		const FString Status = Online->IsReady() ? Online->GetStatsStatus() : FString();
		float LineY = Canvas->ClipY * 0.93f;
		if (!Status.IsEmpty())
		{
			LineY += DrawLabel(Status, FVector2D(CenterX, LineY), FLinearColor(0.55f, 0.65f, 0.75f, 1.0f), 1.0f, true) + 2.0f * UIScale;
		}
		// The pilot's lifetime record as the service holds it (nothing when it is not known).
		const FString Record = Online->GetPilotRecordLine();
		if (!Record.IsEmpty())
		{
			DrawLabel(FString::Printf(TEXT("Your record:  %s"), *Record), FVector2D(CenterX, LineY), FLinearColor(0.55f, 0.65f, 0.75f, 1.0f), 1.0f, true);
		}
	}
}

void AHCJHUD::DrawScoreboard(const AHCJGameState& GameState, const AHCJPlayerState* LocalPlayerState)
{
	TArray<AHCJPlayerState*> PlayerStates;
	GameState.GetSortedPlayerStates(PlayerStates);

	const float UIScale = GetUIScale();
	const float TextScale = 1.3f;
	const float RowHeight = 27.0f * FMath::Max(UIScale, 0.8f);
	const float Padding = 16.0f * UIScale;
	const float Width = FMath::Min(820.0f * FMath::Max(UIScale, 0.8f), Canvas->ClipX - 2.0f * Padding);
	const float Height = Padding * 2.0f + RowHeight * (PlayerStates.Num() + 2) + 8.0f * UIScale;
	const float Left = (Canvas->ClipX - Width) * 0.5f;
	const float Top = FMath::Max(FMath::Min(Canvas->ClipY * 0.24f, Canvas->ClipY - Height - Padding), 0.0f);

	DrawRect(FLinearColor(0.0f, 0.02f, 0.05f, 0.92f), Left, Top, Width, Height);
	DrawRect(HCJAccentColor, Left, Top, Width, FMath::Max(2.0f, 3.0f * UIScale));

	const float RankX = Left + Padding;
	const float NameX = Left + Padding + Width * 0.06f;
	const float ShipX = Left + Width * 0.36f;
	const float KillsX = Left + Width * 0.52f;
	const float DeathsX = Left + Width * 0.64f;
	const float TurretsX = Left + Width * 0.76f;
	const float PingX = Left + Width * 0.90f;
	float Y = Top + Padding;

	// Title row: what is being played for, and the clock.
	DrawLabel(TEXT("SCOREBOARD"), FVector2D(RankX, Y), HCJAccentColor, TextScale, false, true);
	{
		FString Goal = GameState.GetFragLimit() > 0 ? FString::Printf(TEXT("first to %d kills"), GameState.GetFragLimit()) : FString(TEXT("most kills wins"));
		if (GameState.HasPhaseTimeLimit() && GameState.GetMatchPhase() == EHCJMatchPhase::Playing)
		{
			Goal += FString::Printf(TEXT("   -   %s left"), *FormatClock(GameState.GetPhaseTimeRemaining()));
		}
		DrawLabel(Goal, FVector2D(Left + Width - Padding - MeasureLabel(Goal, 1.1f).X, Y + 2.0f * UIScale), HCJTextColor, 1.1f);
	}
	Y += RowHeight + 6.0f * UIScale;

	DrawLabel(TEXT("#"), FVector2D(RankX, Y), HCJDimTextColor, TextScale);
	DrawLabel(TEXT("PILOT"), FVector2D(NameX, Y), HCJDimTextColor, TextScale);
	DrawLabel(TEXT("SHIP"), FVector2D(ShipX, Y), HCJDimTextColor, TextScale);
	DrawLabel(TEXT("KILLS"), FVector2D(KillsX, Y), HCJDimTextColor, TextScale);
	DrawLabel(TEXT("DEATHS"), FVector2D(DeathsX, Y), HCJDimTextColor, TextScale);
	DrawLabel(TEXT("TURRETS"), FVector2D(TurretsX, Y), HCJDimTextColor, TextScale);
	DrawLabel(TEXT("PING"), FVector2D(PingX, Y), HCJDimTextColor, TextScale);
	Y += RowHeight;
	DrawRect(HCJDimTextColor, Left + Padding, Y - 4.0f * UIScale, Width - 2.0f * Padding, FMath::Max(1.0f, UIScale));

	int32 Rank = 0;
	for (const AHCJPlayerState* PlayerState : PlayerStates)
	{
		++Rank;
		const bool bLocal = PlayerState == LocalPlayerState;
		if (bLocal)
		{
			DrawRect(FLinearColor(0.2f, 1.0f, 0.9f, 0.18f), Left + Padding * 0.5f, Y - 2.0f * UIScale, Width - Padding, RowHeight);
		}

		// Player colours can be dark (blue, purple): lifted so that the names read on the dark board.
		FLinearColor RowColor = FMath::Lerp(PlayerState->GetPlayerColor(), FLinearColor::White, 0.3f);
		RowColor.A = 1.0f;
		const FLinearColor NumberColor = bLocal ? FLinearColor::White : HCJTextColor;

		const FString Name = PlayerState->IsABot() ? FString::Printf(TEXT("%s  (bot)"), *PlayerState->GetPlayerName())
			: bLocal ? FString::Printf(TEXT("%s  (you)"), *PlayerState->GetPlayerName()) : PlayerState->GetPlayerName();

		DrawLabel(FString::FromInt(Rank), FVector2D(RankX, Y), HCJDimTextColor, TextScale);
		// Bold for the local player, at the same height as the other rows (the bold style is 1.3 times larger).
		DrawLabel(Name, FVector2D(NameX, Y), RowColor, bLocal ? TextScale / 1.3f : TextScale, false, bLocal);
		DrawLabel(HCJShipTypes::Get(PlayerState->GetShipType()).Name, FVector2D(ShipX, Y), HCJTextColor, TextScale * 0.9f);
		DrawLabel(FString::FromInt(PlayerState->GetKills()), FVector2D(KillsX, Y), NumberColor, TextScale);
		DrawLabel(FString::FromInt(PlayerState->GetDeaths()), FVector2D(DeathsX, Y), NumberColor, TextScale);
		DrawLabel(FString::FromInt(PlayerState->GetTurretKills()), FVector2D(TurretsX, Y), NumberColor, TextScale);
		DrawLabel(PlayerState->IsABot() ? FString(TEXT("-")) : FString::FromInt(FMath::RoundToInt32(PlayerState->GetPingInMilliseconds())), FVector2D(PingX, Y), HCJDimTextColor, TextScale);
		Y += RowHeight;
	}
}

namespace
{
	/** The engine's Slate font at the size it will be shown: HUD "scale 1" is 10 pt (13 pt bold for the large style) at 1080p. */
	FSlateFontInfo MakeLabelFont(float Scale, bool bLargeFont, float TextScale)
	{
		const int32 Size = FMath::Clamp(FMath::RoundToInt32((bLargeFont ? 13.0f : 10.0f) * Scale * TextScale), 7, 96);
		return FCoreStyle::GetDefaultFontStyle(bLargeFont ? "Bold" : "Regular", Size);
	}
}

FVector2D AHCJHUD::MeasureLabel(const FString& Text, float Scale, bool bLargeFont) const
{
	if (Text.IsEmpty() || !FSlateApplication::IsInitialized() || FSlateApplication::Get().GetRenderer() == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	const FSlateFontInfo Font = MakeLabelFont(Scale, bLargeFont, HCJUi::ComputeTextScale(GetUIScale()));
	return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
}

float AHCJHUD::DrawLabel(const FString& Text, const FVector2D& Position, const FLinearColor& Color, float Scale, bool bCentered, bool bLargeFont)
{
	if (!Canvas || Text.IsEmpty() || !FSlateApplication::IsInitialized())
	{
		return 0.0f;
	}

	const FSlateFontInfo Font = MakeLabelFont(Scale, bLargeFont, HCJUi::ComputeTextScale(GetUIScale()));
	const FVector2D Size = MeasureLabel(Text, Scale, bLargeFont);

	// Whole pixels: text drawn between pixels is blurred.
	const FVector2D DrawPosition(FMath::RoundToDouble(bCentered ? Position.X - Size.X * 0.5 : Position.X), FMath::RoundToDouble(Position.Y));
	// The canvas text item takes its font cache from a UFont even when a Slate font is given: the engine's small font
	// (Roboto) is a runtime font, which is what lets the Slate font info pick the size. Should it ever be an offline
	// (bitmap) font, draw that scaled instead.
	const UFont* BaseFont = GEngine != nullptr ? GEngine->GetSmallFont() : nullptr;
	if (BaseFont == nullptr)
	{
		return 0.0f;
	}
	if (BaseFont->FontCacheType == EFontCacheType::Runtime)
	{
		FCanvasTextItem Item(DrawPosition, FText::FromString(Text), Font, Color);
		Item.Font = BaseFont;
		Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, Color.A));
		Canvas->DrawItem(Item);
	}
	else
	{
		FCanvasTextItem Item(DrawPosition, FText::FromString(Text), BaseFont, Color);
		const float LegacyScale = Font.Size / 10.0f;
		Item.Scale = FVector2D(LegacyScale, LegacyScale);
		Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, Color.A));
		Canvas->DrawItem(Item);
	}

	return static_cast<float>(Size.Y);
}

void AHCJHUD::DrawBar(const FVector2D& Position, const FVector2D& Size, float Fraction, const FLinearColor& FillColor)
{
	const float X = static_cast<float>(Position.X);
	const float Y = static_cast<float>(Position.Y);
	const float Width = static_cast<float>(Size.X);
	const float Height = static_cast<float>(Size.Y);
	const float Border = FMath::Max(1.0f, GetUIScale());

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f), X - Border, Y - Border, Width + 2.0f * Border, Height + 2.0f * Border);
	DrawRect(WithAlpha(FillColor, 0.2f), X, Y, Width, Height);

	const float Clamped = FMath::Clamp(Fraction, 0.0f, 1.0f);
	if (Clamped > 0.0f)
	{
		DrawRect(FillColor, X, Y, Width * Clamped, Height);
	}
}

void AHCJHUD::NotePanel(float X, float Y, float Width, float Height)
{
	if (Width > 0.0f && Height > 0.0f && PanelRects.Num() < 32)
	{
		PanelRects.Add(FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height)));
	}
}

int32 AHCJHUD::GetLocalSector(const AHCJPlayerController* Controller) const
{
	if (!Controller)
	{
		return INDEX_NONE;
	}
	if (const AHCJShipCharacter* Ship = Controller->GetShip())
	{
		return Ship->GetCurrentSector();
	}
	const AHCJPlayerState* LocalPlayerState = Controller->GetPlayerState<AHCJPlayerState>();
	return LocalPlayerState ? LocalPlayerState->GetCurrentSector() : INDEX_NONE;
}

float AHCJHUD::GetUIScale() const
{
	return Canvas ? HCJUi::ComputeUIScale(Canvas->ClipX, Canvas->ClipY) : 1.0f;
}
