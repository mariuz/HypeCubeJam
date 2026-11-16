// AHCJHUD, third part: in-game hints, the sector banner, the jump flash and the small drawing helpers they share.
// Local and cosmetic: reads replicated / local state only, replicates nothing.

#include "Game/HCJHUD.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/HCJGameState.h"
#include "Game/HCJGameplaySettings.h"
#include "Game/HCJPlayerController.h"
#include "Game/HCJPlayerState.h"
#include "Game/HCJUiLogic.h"
#include "Game/HCJPadLogic.h"
#include "Game/HCJUserSettings.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "HypeCubeJam.h"
#include "Ship/HCJHealthComponent.h"
#include "Ship/HCJShipCharacter.h"
#include "Ship/HCJShipMovementComponent.h"
#include "UI/HCJHints.h"
#include "World/HCJBlackHole.h"
#include "World/HCJPickup.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJStations.h"
#include "World/HCJTurret.h"
#include "World/HCJUniverse.h"
#include "World/HCJWormholes.h"

namespace
{
	const FLinearColor HintTextColor(0.9f, 0.97f, 1.0f, 1.0f);
	const FLinearColor HintDimColor(0.55f, 0.65f, 0.75f, 1.0f);
	const FLinearColor HintAccentColor(0.2f, 1.0f, 0.9f, 1.0f);
	const FLinearColor HintWarningColor(1.0f, 0.25f, 0.15f, 1.0f);
	const FLinearColor HintDoneColor(0.3f, 1.0f, 0.35f, 1.0f);
	const FLinearColor HintGuideColor(1.0f, 0.9f, 0.15f, 1.0f);

	/**
	 * One per process, like the session it counts for: a HUD is created again with every world (joining, leaving),
	 * and "shown twice per session" must not start over then. Its clock is game time summed over those worlds.
	 */
	FHCJHintDirector GHintDirector;
	double GHintClock = 0.0;
	/** Bit per EHCJPickupItem that has been explained. */
	uint32 GExplainedItems = 0;

	constexpr float BannerSeconds = 4.5f;
	constexpr float JumpFlashSeconds = 0.35f;
	constexpr float LowHullFraction = 0.35f;

	const TCHAR* HintName(EHCJHint Hint)
	{
		static const TCHAR* Names[] = { TEXT("None"), TEXT("Move"), TEXT("Fire"), TEXT("Boost"), TEXT("Target"), TEXT("BlackHole"), TEXT("LowHull"), TEXT("NewItem"),
			TEXT("Turrets"), TEXT("BoostEmpty"), TEXT("Wormhole"), TEXT("Portal"), TEXT("Pickup"), TEXT("EmptySector"), TEXT("Goal"), TEXT("ShieldDown"), TEXT("TorpedoChance"),
			TEXT("PhaserEmpty"), TEXT("RockBreak"), TEXT("TractorCaught"), TEXT("Warp"), TEXT("Cloak"), TEXT("Tractor") };
		const int32 Index = static_cast<int32>(Hint);
		return Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(Names)) ? Names[Index] : TEXT("?");
	}
}

void AHCJHUD::DebugSkipOnboarding()
{
	// Every onboarding action "done": the director then skips the steps.
	FHCJHintInput Input;
	Input.bHasShip = false;
	Input.DoneMask = FHCJHintDirector::Bit(EHCJHint::Move) | FHCJHintDirector::Bit(EHCJHint::Fire) | FHCJHintDirector::Bit(EHCJHint::Boost) | FHCJHintDirector::Bit(EHCJHint::Target);
	GHintDirector.Update(static_cast<float>(GHintClock), Input);
}

void AHCJHUD::DebugResetHints()
{
	GHintDirector.Reset();
	GExplainedItems = 0;
}

void AHCJHUD::DrawPanel(float X, float Y, float Width, float Height, const FLinearColor& Accent, float Alpha)
{
	NotePanel(X, Y, Width, Height);
	DrawRect(FLinearColor(0.0f, 0.02f, 0.05f, FMath::Clamp(0.72f * Alpha, 0.0f, 0.95f)), X, Y, Width, Height);
	if (Accent.A > 0.0f)
	{
		DrawRect(FLinearColor(Accent.R, Accent.G, Accent.B, Accent.A * FMath::Min(Alpha, 1.0f)), X, Y, FMath::Max(2.0f, 3.0f * GetUIScale()), Height);
	}
}

FVector2D AHCJHUD::DrawKeyMarkup(const FString& Markup, const FVector2D& Position, const FLinearColor& Color, float Scale, bool bCentered, bool bDraw, float Alpha)
{
	TArray<FHCJHintRun> Runs;
	// With a game controller in the hand the key caps carry its names ("RT" for "Left mouse").
	const AHCJPlayerController* PadController = Cast<AHCJPlayerController>(GetOwningPlayerController());
	HCJHints::SplitKeyMarkup(!bKeyMarkupAsIs && PadController != nullptr && PadController->IsPadPrompts() ? HCJPad::ToPadMarkup(Markup) : Markup, Runs);
	if (Runs.Num() == 0)
	{
		return FVector2D::ZeroVector;
	}

	const float UIScale = GetUIScale();
	const float KeyScale = Scale * 0.8f;
	const float PadX = FMath::Max(4.0f, 6.0f * UIScale);
	const float PadY = FMath::Max(2.0f, 2.0f * UIScale);
	const float TextHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), Scale).Y);
	const float KeyTextHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), KeyScale, true).Y);
	const float KeyHeight = KeyTextHeight + 2.0f * PadY;
	const float LineHeight = FMath::Max(TextHeight, KeyHeight);

	TArray<float, TInlineAllocator<16>> Widths;
	float Width = 0.0f;
	for (const FHCJHintRun& Run : Runs)
	{
		const float RunWidth = Run.bKey ? FMath::Max(static_cast<float>(MeasureLabel(Run.Text, KeyScale, true).X) + 2.0f * PadX, KeyHeight)
			: static_cast<float>(MeasureLabel(Run.Text, Scale).X);
		Widths.Add(RunWidth);
		Width += RunWidth;
	}
	if (!bDraw)
	{
		return FVector2D(Width, LineHeight);
	}

	float X = FMath::RoundToFloat(bCentered ? static_cast<float>(Position.X) - Width * 0.5f : static_cast<float>(Position.X));
	const float Y = FMath::RoundToFloat(static_cast<float>(Position.Y));
	const float Border = FMath::Max(1.0f, FMath::RoundToFloat(1.5f * UIScale));
	for (int32 Index = 0; Index < Runs.Num(); ++Index)
	{
		const FHCJHintRun& Run = Runs[Index];
		if (Run.bKey)
		{
			// A key cap: outlined box, bold letter.
			const float Top = Y + (LineHeight - KeyHeight) * 0.5f;
			DrawRect(FLinearColor(HintAccentColor.R, HintAccentColor.G, HintAccentColor.B, 0.9f * Alpha), X, Top, Widths[Index], KeyHeight);
			DrawRect(FLinearColor(0.02f, 0.07f, 0.11f, Alpha), X + Border, Top + Border, Widths[Index] - 2.0f * Border, KeyHeight - 2.0f * Border);
			DrawLabel(Run.Text, FVector2D(X + Widths[Index] * 0.5f, Top + PadY - 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, Alpha), KeyScale, true, true);
		}
		else
		{
			DrawLabel(Run.Text, FVector2D(X, Y + (LineHeight - TextHeight) * 0.5f), FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha), Scale);
		}
		X += Widths[Index];
	}
	return FVector2D(Width, LineHeight);
}

void AHCJHUD::UpdateSectorEvents(const AHCJShipCharacter* Ship)
{
	const UWorld* World = GetWorld();
	if (Ship == nullptr || World == nullptr)
	{
		return;
	}
	const int32 Sector = Ship->GetCurrentSector();
	if (Sector == INDEX_NONE)
	{
		return;
	}
	const bool bSameShip = BannerShip.Get() == Ship;
	if (Sector == BannerSector && bSameShip)
	{
		return;
	}
	const float Now = static_cast<float>(World->GetTimeSeconds());
	if (bSameShip && BannerSector != INDEX_NONE)
	{
		// The same ship in another sector: it went through a portal or a wormhole.
		JumpFlashTime = Now;
		const AHCJUniverse* Universe = AHCJUniverse::Get(this);
		JumpFlashColor = (Universe != nullptr && Universe->IsBuilt()) ? FMath::Lerp(Universe->GetSectorTint(Sector), FLinearColor::White, 0.5f) : FLinearColor::White;
	}
	BannerSector = Sector;
	BannerShip = Ship;
	BannerTime = Now;
}

void AHCJHUD::DrawJumpFlash()
{
	const UWorld* World = GetWorld();
	const float Age = World != nullptr ? static_cast<float>(World->GetTimeSeconds()) - JumpFlashTime : 1000.0f;
	if (Age < 0.0f || Age >= JumpFlashSeconds)
	{
		return;
	}
	const float Strength = FMath::Square(1.0f - Age / JumpFlashSeconds);
	DrawRect(FLinearColor(JumpFlashColor.R, JumpFlashColor.G, JumpFlashColor.B, 0.22f * Strength), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
}

void AHCJHUD::DrawSectorBanner(const AHCJGameState& GameState)
{
	const UWorld* World = GetWorld();
	const AHCJUniverse* Universe = GameState.GetUniverse();
	const float Age = World != nullptr ? static_cast<float>(World->GetTimeSeconds()) - BannerTime : 1000.0f;
	if (Universe == nullptr || !Universe->IsBuilt() || BannerSector == INDEX_NONE || Age < 0.0f || Age >= BannerSeconds)
	{
		return;
	}
	const float UIScale = GetUIScale();
	const float Alpha = FMath::Clamp(Age / 0.2f, 0.0f, 1.0f) * FMath::Clamp((BannerSeconds - Age) / 0.8f, 0.0f, 1.0f);
	const int32 Dimension = Universe->GetGraph().GetDimension();
	const int32 Sector = BannerSector;

	// What is in this sector, most dangerous first.
	TArray<FString, TInlineAllocator<6>> Features;
	if (Universe->HasBlackHole(Sector))
	{
		Features.Add(TEXT("BLACK HOLE"));
	}
	if (Universe->HasStation(Sector))
	{
		const FVector Station = Universe->GetStationLocation(Sector);
		int32 LiveTurrets = 0;
		for (const TWeakObjectPtr<AHCJTurret>& Entry : AHCJTurret::GetAll())
		{
			const AHCJTurret* Turret = Entry.Get();
			if (Turret != nullptr && Turret->GetWorld() == World && !Turret->IsTurretDestroyed()
				&& FVector::DistSquared(Turret->GetActorLocation(), Station) < FMath::Square(double(HCJ::StationBodyRadius) + 500.0))
			{
				++LiveTurrets;
			}
		}
		Features.Add(LiveTurrets > 0 ? FString::Printf(TEXT("STATION  %d turrets"), LiveTurrets) : FString(TEXT("STATION  turrets down")));
	}
	if (Universe->HasWormhole(Sector))
	{
		Features.Add(FString::Printf(TEXT("WORMHOLE to %s"), *FHCJHypercubeGraph::SectorToBinaryString(Universe->GetWormholeDest(Sector), Dimension)));
	}
	int32 NumPortals = 0;
	for (int32 Dim = 0; Dim < Dimension; ++Dim)
	{
		NumPortals += Universe->HasPortal(Sector, Dim) ? 1 : 0;
	}
	Features.Add(FString::Printf(TEXT("%d %s"), NumPortals, NumPortals == 1 ? TEXT("PORTAL") : TEXT("PORTALS")));
	Features.Add(EnemiesInSector > 0 ? FString::Printf(TEXT("%d %s"), EnemiesInSector, EnemiesInSector == 1 ? TEXT("ENEMY") : TEXT("ENEMIES")) : FString(TEXT("no enemies")));
	const FString FeatureText = FString::Join(Features, TEXT("    |    "));

	const FString Title = TEXT("SECTOR ") + FHCJHypercubeGraph::SectorToBinaryString(Sector, Dimension);
	FLinearColor Tint = FMath::Lerp(Universe->GetSectorTint(Sector), FLinearColor::White, 0.3f);
	Tint.A = 1.0f;

	const FVector2D TitleSize = MeasureLabel(Title, 2.0f, true);
	const FVector2D FeatureSize = MeasureLabel(FeatureText, 1.05f);
	const float Pad = 10.0f * UIScale;
	const float Width = FMath::Max(static_cast<float>(TitleSize.X), static_cast<float>(FeatureSize.X)) + 6.0f * Pad;
	const float Height = 2.0f * Pad + static_cast<float>(TitleSize.Y) + static_cast<float>(FeatureSize.Y) + 4.0f * UIScale;
	const float CenterX = Canvas->ClipX * 0.5f;
	const float Top = 24.0f * UIScale;

	DrawRect(FLinearColor(0.0f, 0.02f, 0.05f, 0.6f * Alpha), CenterX - Width * 0.5f, Top, Width, Height);
	// A line in the sector colour under the block.
	DrawRect(FLinearColor(Tint.R, Tint.G, Tint.B, Alpha), CenterX - Width * 0.5f, Top + Height - FMath::Max(2.0f, 3.0f * UIScale), Width, FMath::Max(2.0f, 3.0f * UIScale));
	DrawLabel(Title, FVector2D(CenterX, Top + Pad - 2.0f * UIScale), FLinearColor(Tint.R, Tint.G, Tint.B, Alpha), 2.0f, true, true);
	DrawLabel(FeatureText, FVector2D(CenterX, Top + Pad + TitleSize.Y + 2.0f * UIScale), FLinearColor(HintTextColor.R, HintTextColor.G, HintTextColor.B, Alpha), 1.05f, true);
}

void AHCJHUD::UpdateHints(const AHCJShipCharacter* Ship, const AHCJGameState& GameState, const AHCJPlayerController* Controller)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	GHintClock += World->GetDeltaSeconds();
	const float Now = static_cast<float>(GHintClock);
	const float WorldNow = static_cast<float>(World->GetTimeSeconds());

	FHCJHintInput Input;
	Input.bHasShip = Ship != nullptr;
	Input.bEnabled = UHCJUserSettings::Get().bHints;
	bRepairPadKnown = false;

	// What the texts need.
	int32 PortalDest = INDEX_NONE;
	int32 WormholeDest = INDEX_NONE;
	int32 NearItem = INDEX_NONE;
	int32 Dimension = 0;
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	// A torpedo is loaded and the locked target shows us a facing that is down: which one, and whose.
	int32 OpenFacing = INDEX_NONE;
	FString OpenTargetName;

	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	if (Ship != nullptr)
	{
		const FVector ShipLocation = Ship->GetActorLocation();
		const UHCJShipMovementComponent* Movement = Ship->GetShipMovement();
		const UHCJHealthComponent* Health = Ship->GetHealthComponent();

		// What the player is doing.
		if (Ship->GetVelocity().SizeSquared() > FMath::Square(500.0))
		{
			Input.DoneMask |= FHCJHintDirector::Bit(EHCJHint::Move);
		}
		if (Ship->IsFiring())
		{
			Input.DoneMask |= FHCJHintDirector::Bit(EHCJHint::Fire);
		}
		if (Movement != nullptr && Movement->IsBoostActive())
		{
			Input.DoneMask |= FHCJHintDirector::Bit(EHCJHint::Boost);
		}
		if (Controller != nullptr && Controller->GetTargetKeyPresses() > 0)
		{
			Input.DoneMask |= FHCJHintDirector::Bit(EHCJHint::Target);
		}

		// What is going on around the ship.
		Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::Goal);
		if (Movement != nullptr && Movement->IsBoostLocked())
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::BoostEmpty);
		}
		// The phaser bank is empty; the own beam rests on a rock.
		if (Ship->GetWeapon() == EHCJWeapon::Phaser && Ship->IsPhaserLocked())
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::PhaserEmpty);
		}
		{
			FVector BeamStart;
			FVector BeamEnd;
			bool bBeamHit = false;
			if (Ship->GetPhaserBeamSegment(BeamStart, BeamEnd, bBeamHit) && bBeamHit && Ship->GetPhaserTarget() == nullptr)
			{
				Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::RockBreak);
			}
		}
		// A shield facing of ours went down a moment ago.
		if (WorldNow - Ship->GetLastFacingCollapseTime() < 4.0f)
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::ShieldDown);
		}
		if (const AHCJShipCharacter* Target = Controller != nullptr ? Controller->GetTarget() : nullptr; Target != nullptr && Ship->IsTorpedoReady() && !Target->IsSpawnShielded())
			{
				const UHCJHealthComponent* TargetHealth = Target->GetHealthComponent();
				const FVector ToUs = ShipLocation - Target->GetActorLocation();
				const double TargetDistance = ToUs.Size();
				if (TargetHealth != nullptr && TargetHealth->GetNumFacings() == HCJShield::NumFacings
					&& TargetDistance > Gameplay.TorpedoArmingDistance + Gameplay.TorpedoBlastRadius && TargetDistance < Gameplay.GetWeapon(EHCJWeapon::Torpedo).BotMaxRange)
				{
					const EHCJShieldFacing Towards = HCJShield::SelectFacing(Target->GetActorTransform().InverseTransformVectorNoScale(ToUs));
					if (TargetHealth->IsFacingDown(static_cast<int32>(Towards)))
					{
						OpenFacing = static_cast<int32>(Towards);
						const AHCJPlayerState* TargetState = Target->GetPlayerState<AHCJPlayerState>();
						OpenTargetName = TargetState != nullptr ? TargetState->GetPlayerName() : FString(TEXT("the target"));
						Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::TorpedoChance);
					}
				}
			}
		const bool bLowHull = Health != nullptr && Health->GetHealthFraction() < LowHullFraction;
		if (bLowHull)
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::LowHull);
		}
		if (Controller != nullptr && WorldNow - Controller->GetLastPickupTime() < 5.0f && (GExplainedItems & (1u << Controller->GetLastPickupItem())) == 0)
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::NewItem);
		}

		// Pads: the nearest one that holds something; on a low hull the nearest repair.
		double BestPadSq = FMath::Square(4500.0);
		double BestRepairSq = FMath::Square(double(HCJ::SectorKillRadius));
		for (const TWeakObjectPtr<AHCJPickup>& Entry : AHCJPickup::GetAll())
		{
			const AHCJPickup* Pickup = Entry.Get();
			if (Pickup == nullptr || Pickup->GetWorld() != World || !Pickup->IsActive())
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(Pickup->GetActorLocation(), ShipLocation);
			if (DistSq < BestPadSq)
			{
				BestPadSq = DistSq;
				NearItem = static_cast<int32>(Pickup->GetItem());
			}
			if (bLowHull && Pickup->GetItem() == EHCJPickupItem::Repair && DistSq < BestRepairSq)
			{
				BestRepairSq = DistSq;
				RepairPadLocation = Pickup->GetActorLocation();
				bRepairPadKnown = true;
			}
		}
		if (NearItem != INDEX_NONE)
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::Pickup);
		}

		if (Universe != nullptr && Universe->IsBuilt())
		{
			Dimension = Universe->GetGraph().GetDimension();
			const int32 Sector = Universe->GetSectorAtLocation(ShipLocation);
			const double HoleDistance = Universe->GetBlackHoleDistance(ShipLocation);
			if (HoleDistance >= 0.0 && HoleDistance < Universe->GetBlackHoleSettings().InfluenceRadius)
			{
				Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::BlackHole);
			}
			if (Sector != INDEX_NONE)
			{
				double BestPortalSq = FMath::Square(7000.0);
				for (int32 Dim = 0; Dim < Dimension; ++Dim)
				{
					if (Universe->HasPortal(Sector, Dim))
					{
						const double DistSq = FVector::DistSquared(Universe->GetPortalWorldTransform(Sector, Dim).GetLocation(), ShipLocation);
						if (DistSq < BestPortalSq)
						{
							BestPortalSq = DistSq;
							PortalDest = Sector ^ (1 << Dim);
						}
					}
				}
				if (PortalDest != INDEX_NONE)
				{
					Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::Portal);
				}
				if (Universe->HasWormhole(Sector) && FVector::DistSquared(Universe->GetWormholeLocation(Sector), ShipLocation) < FMath::Square(7000.0))
				{
					WormholeDest = Universe->GetWormholeDest(Sector);
					Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::Wormhole);
				}
				if (Universe->HasStation(Sector))
				{
					for (const TWeakObjectPtr<AHCJTurret>& Entry : AHCJTurret::GetAll())
					{
						const AHCJTurret* Turret = Entry.Get();
						if (Turret != nullptr && Turret->GetWorld() == World && !Turret->IsTurretDestroyed()
							&& FVector::DistSquared(Turret->GetActorLocation(), ShipLocation) < FMath::Square(double(Gameplay.TurretRange) * 1.25))
						{
							Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::Turrets);
							break;
						}
					}
				}
			}
		}

		// The ship systems: caught by a beam (the way out); once each, what warp, cloak and tractor beam are for.
		if (Ship->GetTractorHolder() != nullptr)
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::TractorCaught);
		}
		if (EnemiesInSector == 0 && SectorClearSince >= 0.0f && WorldNow - SectorClearSince > 6.0f && Ship->GetWarpPhase() == EHCJWarpPhase::Idle && !Ship->IsCloaked())
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::Warp);
		}
		if (EnemiesInSector > 0 && !Ship->IsCloaked() && !Ship->IsCloakLocked() && Ship->GetCloakEnergy() > 0.9f && !Ship->IsFiring())
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::Cloak);
		}
		if (const AHCJShipCharacter* Locked = Controller != nullptr ? Controller->GetTarget() : nullptr; Locked != nullptr && Ship->GetTractorTarget() == nullptr
			&& FVector::DistSquared(Locked->GetActorLocation(), ShipLocation) < FMath::Square(static_cast<double>(Ship->GetTypeDef().TractorRange) * 0.9))
		{
			Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::Tractor);
		}

		// Nobody here for a while, and the route table knows where they are (set by DrawPortalLabels this frame).
		if (EnemiesInSector == 0)
		{
			SectorClearSince = SectorClearSince < 0.0f ? WorldNow : SectorClearSince;
			if (WorldNow - SectorClearSince > 4.0f && GuideHop != INDEX_NONE && GuideSector != INDEX_NONE)
			{
				Input.ActiveMask |= FHCJHintDirector::Bit(EHCJHint::EmptySector);
			}
		}
		else
		{
			SectorClearSince = -1.0f;
		}
	}
	else
	{
		SectorClearSince = -1.0f;
	}

	const EHCJHint Before = GHintDirector.GetCurrent();
	const EHCJHint Hint = GHintDirector.Update(Now, Input);
	if (Hint == EHCJHint::None)
	{
		HintShown = 0;
		HintText.Reset();
		HintSubText.Reset();
		return;
	}
	const TCHAR* LowHullWithPad = TEXT("HULL LOW: a REPAIR pad is in this sector. Follow the green marker");
	const TCHAR* LowHullNoPad = TEXT("HULL LOW: break off with [Shift]. The shield recharges after a few seconds out of fire");
	if (Hint == Before && HintShown == static_cast<uint8>(Hint))
	{
		// Texts are composed once, except this one: the pad it points at can be taken, or the ship can change sector.
		if (Hint == EHCJHint::LowHull && bRepairPadKnown != bLowHullTextHasPad)
		{
			bLowHullTextHasPad = bRepairPadKnown;
			HintText = bRepairPadKnown ? LowHullWithPad : LowHullNoPad;
		}
		return;
	}

	// A hint begins: compose its text once.
	HintShown = static_cast<uint8>(Hint);
	HintSubText.Reset();
	const auto SectorName = [Dimension](int32 Sector) { return FHCJHypercubeGraph::SectorToBinaryString(Sector, FMath::Max(Dimension, 1)); };
	switch (Hint)
	{
	case EHCJHint::Move:
		HintText = TEXT("[W] [A] [S] [D] thrust     [Space] [Ctrl] up / down     [Mouse] steer");
		HintSubText = TEXT("[F1] all controls     [H] hints off");
		break;
	case EHCJHint::Fire:
		HintText = TEXT("Hold [Left mouse]: the PHASER beam hits at once. Keep it on an enemy; it is strongest up close");
		HintSubText = TEXT("It drains the phaser bank (WEAPON, bottom right).  [B] switches to the blaster, whose bolts need the lead circle");
		break;
	case EHCJHint::Boost:
		HintText = TEXT("Hold [Shift] to boost.  The BOOST bar (bottom left) drains and refills");
		break;
	case EHCJHint::Target:
		HintText = TEXT("[T] next target     [F] nearest enemy     [Middle mouse] hold the target");
		break;
	case EHCJHint::BlackHole:
		HintText = TEXT("GRAVITY WELL: turn away from the black hole and hold [Shift]");
		break;
	case EHCJHint::LowHull:
		bLowHullTextHasPad = bRepairPadKnown;
		HintText = bRepairPadKnown ? LowHullWithPad : LowHullNoPad;
		break;
	case EHCJHint::NewItem:
	{
		const int32 Item = Controller != nullptr ? Controller->GetLastPickupItem() : 0;
		GExplainedItems |= 1u << Item;
		HintText = HCJHints::GetItemHint(Item);
		break;
	}
	case EHCJHint::Turrets:
		HintText = FString::Printf(TEXT("STATION TURRETS fire inside %d m. Destroy them for points, or keep clear"), FMath::RoundToInt32(Gameplay.TurretRange / 100.0f));
		break;
	case EHCJHint::ShieldDown:
		if (Ship != nullptr)
	{
		// Which one: the facing that went down last.
		int32 Down = 0;
		for (int32 Facing = 1; Facing < HCJShield::NumFacings; ++Facing)
		{
			if (Ship->GetFacingCollapseTime(Facing) > Ship->GetFacingCollapseTime(Down))
			{
				Down = Facing;
			}
		}
		HintText = FString::Printf(TEXT("%s SHIELD DOWN: hits from that side now reach the hull. Turn another side to the enemy"), HCJShield::FacingName(static_cast<EHCJShieldFacing>(Down)));
		HintSubText = TEXT("Four facings: fore, aft, port, starboard. Each comes back a few seconds after its last hit");
		break;
	}
	case EHCJHint::TorpedoChance:
		HintText = FString::Printf(TEXT("%s shield of %s is DOWN: fire a torpedo  [Right mouse]  - through an open side it guts the hull"),
			OpenFacing != INDEX_NONE ? HCJShield::FacingName(static_cast<EHCJShieldFacing>(OpenFacing)) : TEXT("A"), *OpenTargetName);
		HintSubText = TEXT("Torpedoes steer a little for your target. A full shield facing stops most of one: pick the open side");
		break;
	case EHCJHint::PhaserEmpty:
		HintText = TEXT("PHASER BANK EMPTY: the trigger fires the blaster until the bank is back at the mark");
		HintSubText = TEXT("Short bursts keep a reserve: the bank starts to refill a second after the beam stops");
		break;
	case EHCJHint::RockBreak:
		HintText = TEXT("Phasers and torpedoes break rocks: big ones into pieces, small ones for good");
		HintSubText = TEXT("Hold the beam on it. Bolts do nothing to rock. The field is whole again when the next match starts");
		break;
	case EHCJHint::BoostEmpty:
		HintText = TEXT("BOOST EMPTY: it unlocks again at 25 %. Short bursts keep a reserve");
		break;
	case EHCJHint::TractorCaught:
		HintText = TEXT("CAUGHT IN A TRACTOR BEAM: hold [Shift] to boost free, or shoot the ship that holds you");
		break;
	case EHCJHint::Warp:
		HintText = TEXT("WARP DRIVE: point the nose where you want to go and hold [Z]. It charges, then jumps you far ahead");
		HintSubText = TEXT("Straight line only, no weapons, and it drops out before rock or when you let go. A hit while it charges stops it");
		break;
	case EHCJHint::Cloak:
		HintText = TEXT("CLOAK: [C] hides you from eyes, radar and locks while the CLOAK meter lasts (bottom right)");
		HintSubText = TEXT("Cloaked you have no shields and no weapons. A hit or an enemy within 15 m reveals you; firing drops the cloak");
		break;
	case EHCJHint::Tractor:
		HintText = TEXT("TRACTOR BEAM: hold [R] to hold your target in place - then shoot it. It feeds on the phaser bank");
		HintSubText = TEXT("It lets go when the target boosts free, when you are hit hard, or when rock gets in between");
		break;
	case EHCJHint::Wormhole:
		HintText = FString::Printf(TEXT("WORMHOLE: fly into it for a shortcut to sector %s"), *SectorName(WormholeDest));
		break;
	case EHCJHint::Portal:
		HintText = FString::Printf(TEXT("Fly through the PORTAL to jump to sector %s"), *SectorName(PortalDest));
		break;
	case EHCJHint::Pickup:
		HintText = FString::Printf(TEXT("%s pad ahead: fly through it to collect"), HCJ::GetItemName(static_cast<EHCJPickupItem>(FMath::Max(NearItem, 0))));
		break;
	case EHCJHint::EmptySector:
		HintText = FString::Printf(TEXT("No enemies here. The nearest are in sector %s: follow the yellow %s marker"), *SectorName(GuideSector),
			GuideHop == HCJ::WormholeHop ? TEXT("WORMHOLE") : TEXT("PORTAL"));
		break;
	case EHCJHint::Goal:
		HintText = GameState.GetFragLimit() > 0 ? FString::Printf(TEXT("First to %d kills wins.   [Tab] scores     top right: the map of the hypercube"), GameState.GetFragLimit())
			: FString(TEXT("Most kills wins.   [Tab] scores     top right: the map of the hypercube"));
		break;
	default:
		HintText.Reset();
		break;
	}
	UE_LOG(LogHCJ, Log, TEXT("Hint: %s (showing %d)"), HintName(Hint), GHintDirector.GetShowCount(Hint));
}

void AHCJHUD::DrawHints(const AHCJShipCharacter* Ship, const AHCJPlayerController* Controller)
{
	const UWorld* World = GetWorld();
	if (World == nullptr || !PlayerOwner)
	{
		return;
	}
	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	const float WorldNow = static_cast<float>(World->GetTimeSeconds());
	const float CenterX = Canvas->ClipX * 0.5f;
	const bool bEnabled = UHCJUserSettings::Get().bHints;
	// Bottom of the hint panel: above the speed line.
	const float Bottom = Canvas->ClipY - 24.0f * UIScale - 30.0f * TextScale;
	const float PadX = 14.0f * UIScale;
	const float PadY = 7.0f * UIScale;
	constexpr float Scale = 1.1f;

	// H was pressed: say what it did.
	const float ToggleAge = Controller != nullptr ? WorldNow - Controller->GetHintsToggleTime() : 1000.0f;
	if (ToggleAge >= 0.0f && ToggleAge < 3.0f)
	{
		const FString Notice = bEnabled ? TEXT("Hints ON     [H] switches them off") : TEXT("Hints OFF     [H] switches them back on");
		const float Alpha = FMath::Clamp((3.0f - ToggleAge) / 0.5f, 0.0f, 1.0f);
		const FVector2D Size = DrawKeyMarkup(Notice, FVector2D::ZeroVector, HintTextColor, Scale, true, false);
		const float Width = static_cast<float>(Size.X) + 2.0f * PadX;
		const float Height = static_cast<float>(Size.Y) + 2.0f * PadY;
		DrawPanel(CenterX - Width * 0.5f, Bottom - Height, Width, Height, bEnabled ? HintAccentColor : HintDimColor, Alpha);
		DrawKeyMarkup(Notice, FVector2D(CenterX, Bottom - Height + PadY), HintTextColor, Scale, true, true, Alpha);
		return;
	}
	if (!bEnabled || Ship == nullptr)
	{
		return;
	}

	// (Low hull: the way to the repair pad is drawn with the pad markers, AHCJHUD::DrawPickupMarkers - also with the hints off.)

	const EHCJHint Hint = GHintDirector.GetCurrent();
	if (Hint == EHCJHint::None || HintText.IsEmpty())
	{
		return;
	}
	const float Now = static_cast<float>(GHintClock);
	const float Age = GHintDirector.GetCurrentAge(Now);
	const float Alpha = FMath::Clamp(Age / 0.25f, 0.0f, 1.0f);
	const bool bUrgent = FHCJHintDirector::GetRule(Hint).Priority >= FHCJHintDirector::UrgentPriority;
	const bool bDone = GHintDirector.IsCurrentDone();
	const FLinearColor Accent = bDone ? HintDoneColor : bUrgent ? HintWarningColor : Hint == EHCJHint::EmptySector ? HintGuideColor : HintAccentColor;

	const FVector2D Size = DrawKeyMarkup(HintText, FVector2D::ZeroVector, HintTextColor, Scale, true, false);
	const FVector2D SubSize = HintSubText.IsEmpty() ? FVector2D::ZeroVector : DrawKeyMarkup(HintSubText, FVector2D::ZeroVector, HintDimColor, 0.9f, true, false);
	const float TimerHeight = FMath::Max(2.0f, 2.0f * UIScale);
	const float Width = FMath::Max(static_cast<float>(Size.X), static_cast<float>(SubSize.X)) + 2.0f * PadX;
	const float Height = static_cast<float>(Size.Y) + 2.0f * PadY + (HintSubText.IsEmpty() ? 0.0f : static_cast<float>(SubSize.Y) + 4.0f * UIScale) + TimerHeight;
	const float Left = CenterX - Width * 0.5f;
	const float Top = Bottom - Height;

	DrawPanel(Left, Top, Width, Height, Accent, Alpha * 1.15f);
	DrawKeyMarkup(HintText, FVector2D(CenterX, Top + PadY), bDone ? HintDoneColor : HintTextColor, Scale, true, true, Alpha);
	if (!HintSubText.IsEmpty())
	{
		DrawKeyMarkup(HintSubText, FVector2D(CenterX, Top + PadY + Size.Y + 4.0f * UIScale), HintDimColor, 0.9f, true, true, Alpha);
	}
	// The time it has left, as a line along the bottom edge.
	DrawRect(FLinearColor(Accent.R, Accent.G, Accent.B, 0.7f * Alpha), Left, Top + Height - TimerHeight, Width * GHintDirector.GetCurrentRemainingFraction(Now), TimerHeight);
}
