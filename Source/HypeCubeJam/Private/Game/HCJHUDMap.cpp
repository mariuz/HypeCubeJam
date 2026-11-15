// AHCJHUD, third part: the hypercube map at the top right.

#include "Game/HCJHUD.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Game/HCJGameState.h"
#include "Game/HCJPlayerState.h"
#include "Game/HCJUiLogic.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Ship/HCJShipCharacter.h"
#include "World/HCJUniverse.h"
#include "World/HCJWormholes.h"

float AHCJHUD::DebugMapJumpAge = -1.0f;

namespace
{
	const FLinearColor MapPanelColor(0.0f, 0.02f, 0.05f, 0.9f);
	const FLinearColor MapDimColor(0.55f, 0.65f, 0.75f, 1.0f);
	const FLinearColor MapEdgeColor(0.45f, 0.6f, 0.75f, 1.0f);
	const FLinearColor MapAccentColor(0.2f, 1.0f, 0.9f, 1.0f);
	/** The way to the enemies, as on the in-world "ENEMIES THIS WAY" marker and the radar line. */
	const FLinearColor MapGuideColor(1.0f, 0.9f, 0.15f, 1.0f);
	const FLinearColor MapHostileColor(1.0f, 0.24f, 0.08f, 1.0f);
	const FLinearColor MapWormholeColor(0.78f, 0.5f, 1.0f, 1.0f);
	const FLinearColor MapHoleColor(1.0f, 0.55f, 0.1f, 1.0f);

	/** Seconds: the rings after a change of sector, the slide of the own mark, how long a lost ship marks a fight. */
	constexpr float MapPulseSeconds = 1.1f;
	constexpr float MapSlideSeconds = 0.3f;
	constexpr float MapFightSeconds = 8.0f;
	constexpr int32 MapMaxLabels = 12;
	/** Text sizes (HUD scale: 1 = 10 pt at 1080p; the bold style 13 pt). */
	constexpr float MapLabelTextSize = 0.9f;
	constexpr float MapYouTextSize = 0.72f;
	constexpr float MapBadgeTextSize = 0.62f;
	constexpr float MapTitleTextSize = 0.9f;
	constexpr float MapLegendTextSize = 0.85f;
	const FString MapYouText(TEXT("YOU"));
	const FString MapLegendTexts[3] = { FString(TEXT("station")), FString(TEXT("black hole")), FString(TEXT("wormhole")) };

	FLinearColor MapAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha);
	}

	FLinearColor MapShade(const FLinearColor& Color, float Brightness)
	{
		return FLinearColor(Color.R * Brightness, Color.G * Brightness, Color.B * Brightness, 1.0f);
	}
}

float AHCJHUD::DrawMapLegend(const FVector2D& TopLeft, float Width, bool bStations, bool bBlackHoles, bool bWormholes)
{
	const FString& StationText = MapLegendTexts[0];
	const FString& HoleText = MapLegendTexts[1];
	const FString& WormholeText = MapLegendTexts[2];
	const int32 NumEntries = (bStations ? 1 : 0) + (bBlackHoles ? 1 : 0) + (bWormholes ? 1 : 0);
	if (NumEntries == 0)
	{
		return 0.0f;
	}
	const float UIScale = GetUIScale();
	constexpr float TextSize = MapLegendTextSize;
	const float RowHeight = static_cast<float>(Map.LegendSizes[0].Y);
	const float Padding = 4.0f * UIScale;
	const float Icon = FMath::Max(4.0f, 5.5f * UIScale);
	const float Line = FMath::Max(1.0f, 1.5f * UIScale);
	const float Gap = 4.0f * UIScale;
	// The entries are spread over the row.
	const float Widths[3] = {
		bStations ? 2.0f * Icon + Gap + static_cast<float>(Map.LegendSizes[0].X) : 0.0f,
		bBlackHoles ? 2.0f * Icon + Gap + static_cast<float>(Map.LegendSizes[1].X) : 0.0f,
		bWormholes ? 3.0f * Icon + Gap + static_cast<float>(Map.LegendSizes[2].X) : 0.0f };
	const float Free = FMath::Max(0.0f, Width - Widths[0] - Widths[1] - Widths[2]);
	const float Between = Free / (NumEntries + 1);
	float X = static_cast<float>(TopLeft.X) + Between;
	const float Y = static_cast<float>(TopLeft.Y) + Padding;
	const float CY = Y + RowHeight * 0.5f;
	if (bStations)
	{
		const float CX = X + Icon;
		DrawRect(MapDimColor, CX - Icon * 0.35f, CY - Icon * 0.35f, Icon * 0.7f, Icon * 0.7f);
		DrawLine(CX - Icon, CY - Icon, CX + Icon, CY - Icon, FLinearColor::White, Line);
		DrawLine(CX + Icon, CY - Icon, CX + Icon, CY + Icon, FLinearColor::White, Line);
		DrawLine(CX + Icon, CY + Icon, CX - Icon, CY + Icon, FLinearColor::White, Line);
		DrawLine(CX - Icon, CY + Icon, CX - Icon, CY - Icon, FLinearColor::White, Line);
		DrawLabel(StationText, FVector2D(X + 2.0f * Icon + Gap, Y), MapDimColor, TextSize);
		X += Widths[0] + Between;
	}
	if (bBlackHoles)
	{
		const float CX = X + Icon;
		DrawRect(FLinearColor::Black, CX - Icon * 0.5f, CY - Icon * 0.5f, Icon, Icon);
		DrawCircle(CX, CY, Icon, MapHoleColor, Line, 10);
		DrawLabel(HoleText, FVector2D(X + 2.0f * Icon + Gap, Y), MapDimColor, TextSize);
		X += Widths[1] + Between;
	}
	if (bWormholes)
	{
		for (int32 Dash = 0; Dash < 3; ++Dash)
		{
			DrawLine(X + Dash * Icon * 1.2f, CY, X + Dash * Icon * 1.2f + Icon * 0.6f, CY, MapWormholeColor, Line * 1.3f);
		}
		DrawLabel(WormholeText, FVector2D(X + 3.0f * Icon + Gap, Y), MapDimColor, TextSize);
	}
	return RowHeight + 2.0f * Padding;
}

float AHCJHUD::DrawHypercubeMap(const AHCJGameState& GameState, const FVector2D& TopRight, int32 LocalSector, const AHCJShipCharacter* LiveShip)
{
	const FHCJHypercubeGraph* Graph = GameState.GetGraph();
	const UWorld* World = GetWorld();
	if (!Graph || !Graph->IsBuilt() || World == nullptr)
	{
		return 0.0f;
	}

	const AHCJUniverse* Universe = GameState.GetUniverse();
	const bool bUniverse = Universe != nullptr && Universe->IsBuilt();
	const float UIScale = GetUIScale();
	// Marks do not shrink as far as the layout does: at 1280x720 a dot would be three pixels.
	const float Unit = FMath::Max(UIScale, 0.75f);
	const int32 Dimension = Graph->GetDimension();
	const int32 NumSectors = Graph->NumSectors();
	const float Now = static_cast<float>(World->GetRealTimeSeconds());

	// ---- what is kept between frames ----
	const FHCJMatchConfig& Config = GameState.GetMatchConfig();
	if (Map.Dimension != Dimension || Map.MatchNumber != Config.MatchNumber || Map.Seed != Config.Seed || Map.SectorNames.Num() != NumSectors)
	{
		// Another universe: its names, and nothing that was true in the one before.
		Map.Dimension = Dimension;
		Map.MatchNumber = Config.MatchNumber;
		Map.Seed = Config.Seed;
		Map.Sector = INDEX_NONE;
		Map.PrevSector = INDEX_NONE;
		Map.SectorTime = -1000.0f;
		Map.Title = FString::Printf(TEXT("%dD HYPERCUBE  -  %d sectors"), Dimension, NumSectors);
		Map.SectorNames.Reset(NumSectors);
		for (int32 Sector = 0; Sector < NumSectors; ++Sector)
		{
			Map.SectorNames.Add(FHCJHypercubeGraph::SectorToBinaryString(Sector, Dimension));
		}
		if (Map.CountNames.Num() == 0)
		{
			for (int32 Count = 1; Count <= 9; ++Count)
			{
				Map.CountNames.Add(FString::FromInt(Count));
			}
			Map.CountNames.Add(TEXT("9+"));
		}
		Map.LossTimes.Init(-1000.0f, NumSectors);
		MapDeathCounts.Reset();
		Map.MeasuredScale = 0.0f;
	}
	if (Map.MeasuredScale != UIScale || Map.NameSizes.Num() != NumSectors)
	{
		// The text sizes of this universe at this canvas size, measured once (a measurement with nothing to measure
		// with - no font service yet - is tried again next frame).
		Map.NameSizes.Reset(NumSectors);
		for (const FString& Name : Map.SectorNames)
		{
			Map.NameSizes.Add(MeasureLabel(Name, MapLabelTextSize));
		}
		Map.YouSize = MeasureLabel(MapYouText, MapYouTextSize, true);
		Map.DigitSize = Map.CountNames.Num() > 7 ? MeasureLabel(Map.CountNames[7], MapBadgeTextSize, true) : FVector2D::ZeroVector;
		Map.TitleSize = MeasureLabel(Map.Title, MapTitleTextSize);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Map.LegendSizes[Index] = MeasureLabel(MapLegendTexts[Index], MapLegendTextSize);
		}
		Map.MeasuredScale = Map.TitleSize.Y > 0.0 ? UIScale : 0.0f;
	}
	if (LocalSector != Map.Sector && LocalSector >= 0 && LocalSector < NumSectors)
	{
		// The line travelled and the sliding mark are for a jump only: a new ship in a sector that is not joined to the
		// one before (a respawn) gets the rings, not a line across the map that no portal makes.
		const int32 Crossed = Map.Sector >= 0 ? (Map.Sector ^ LocalSector) : 0;
		const bool bPortal = Crossed != 0 && (Crossed & (Crossed - 1)) == 0 && Graph->IsEdgeOpen(LocalSector, FMath::FloorLog2(static_cast<uint32>(Crossed)));
		const bool bWormhole = bUniverse && Map.Sector >= 0 && Universe->GetWormholeDest(Map.Sector) == LocalSector;
		Map.PrevSector = (bPortal || bWormhole) ? Map.Sector : INDEX_NONE;
		Map.Sector = LocalSector;
		Map.SectorTime = Now;
	}
#if !UE_BUILD_SHIPPING
	if (DebugMapJumpAge >= 0.0f)
	{
		// Development (HCJMenuCmd mapjump <seconds>): the animation of the last change of sector stands still at that age.
		Map.SectorTime = Now - DebugMapJumpAge;
	}
#endif
	const int32 Own = (LocalSector >= 0 && LocalSector < NumSectors) ? LocalSector : INDEX_NONE;

	// ---- who is where (the always-relevant player states), and where a ship was just lost ----
	TArray<int32, TInlineAllocator<64>> Occupants;
	Occupants.SetNumZeroed(NumSectors);
	const APlayerState* LocalPlayerState = PlayerOwner ? PlayerOwner->PlayerState.Get() : nullptr;
	for (const APlayerState* PlayerState : GameState.PlayerArray)
	{
		const AHCJPlayerState* Other = Cast<AHCJPlayerState>(PlayerState);
		if (Other == nullptr || Other->IsOnlyASpectator())
		{
			continue;
		}
		const int32 Sector = Other->GetCurrentSector();
		const bool bValidSector = Occupants.IsValidIndex(Sector);
		int32& SeenDeaths = MapDeathCounts.FindOrAdd(Other, Other->GetDeaths());
		if (Other->GetDeaths() > SeenDeaths && bValidSector)
		{
			Map.LossTimes[Sector] = Now;
		}
		SeenDeaths = Other->GetDeaths();
		// A pilot waiting for a new ship is nobody to fight.
		if (Other != LocalPlayerState && bValidSector && !Other->IsWaitingToRespawn())
		{
			++Occupants[Sector];
		}
	}
	if (MapDeathCounts.Num() > GameState.PlayerArray.Num() + 16)
	{
		// Player states that left.
		for (auto It = MapDeathCounts.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
	if (Own != INDEX_NONE && LiveShip != nullptr)
	{
		// The own sector shows what the radar says: the ships this machine sees, never a cloaked one.
		Occupants[Own] = EnemiesInSector;
	}

	// ---- the target and the nearest fight ----
	int32 TargetSector = INDEX_NONE;
	bool bLockedTarget = false;
	if (LiveShip != nullptr)
	{
		if (const AHCJShipCharacter* Locked = LiveShip->GetLockTarget(); Locked != nullptr && !Locked->IsDead())
		{
			TargetSector = Locked->GetCurrentSector();
			bLockedTarget = true;
		}
		else if (GuideSector != INDEX_NONE && GuideHop != INDEX_NONE)
		{
			TargetSector = GuideSector;
		}
	}
	if (TargetSector == Own || TargetSector < 0 || TargetSector >= NumSectors)
	{
		TargetSector = INDEX_NONE;
	}
	int32 FightSector = INDEX_NONE;
	{
		int32 BestDistance = MAX_int32;
		for (int32 Sector = 0; Sector < NumSectors; ++Sector)
		{
			if (Sector == Own || (Occupants[Sector] < 2 && Now - Map.LossTimes[Sector] > MapFightSeconds))
			{
				continue;
			}
			// By the way there (portals and wormholes); without an own sector the fullest one.
			const int32 Way = (Own != INDEX_NONE && bUniverse) ? Universe->GetRoutes().Distance(Own, Sector) : -1;
			const int32 Distance = Way > 0 ? Way * 16 - Occupants[Sector] : 1000 - Occupants[Sector];
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				FightSector = Sector;
			}
		}
	}

	// ---- the panel ----
	const float Width = (NumSectors > 16 ? MapSizeLarge : MapSize) * UIScale;
	const float Left = static_cast<float>(TopRight.X) - Width;
	const float Top = static_cast<float>(TopRight.Y);
	const float TitleHeight = static_cast<float>(Map.TitleSize.Y) + 6.0f * UIScale;
	const float AreaHeight = Width * 0.9f;
	bool bAnyStation = false;
	bool bAnyHole = false;
	for (int32 Sector = 0; Sector < NumSectors && bUniverse; ++Sector)
	{
		bAnyStation |= Universe->HasStation(Sector);
		bAnyHole |= Universe->HasBlackHole(Sector);
	}
	const bool bAnyWormhole = bUniverse && Universe->GetWormholeLinks().Num() > 0;
	const float LegendTop = Top + TitleHeight + AreaHeight;
	// Nearly opaque: labels of things in the world behind the map must not read through it.
	const float LegendRow = (bAnyStation || bAnyHole || bAnyWormhole) ? static_cast<float>(Map.LegendSizes[0].Y) + 8.0f * UIScale : 0.0f;
	const float Height = TitleHeight + AreaHeight + LegendRow;
	DrawRect(MapPanelColor, Left, Top, Width, Height);
	DrawRect(MapAlpha(MapAccentColor, 0.55f), Left, Top, Width, FMath::Max(1.0f, 2.0f * UIScale));
	NotePanel(Left, Top, Width, Height);

	// ---- where every sector is drawn: at rest (labels are placed there) and with the slow sway ----
	const FVector2D Center(Left + Width * 0.5f, Top + TitleHeight + AreaHeight * 0.5f);
	const FVector2D Radius(Width * 0.5f - 24.0f * Unit, AreaHeight * 0.5f - 22.0f * Unit);
	const float Sway = FMath::DegreesToRadians(MapSwayDegrees) * FMath::Sin(Now * (2.0f * UE_PI / 18.0f));
	TArray<FVector, TInlineAllocator<64>> Points;
	TArray<FVector2D, TInlineAllocator<64>> RestPoints;
	Points.Reserve(NumSectors);
	RestPoints.Reserve(NumSectors);
	for (int32 Sector = 0; Sector < NumSectors; ++Sector)
	{
		const FVector Swayed = HCJUi::ProjectMapSector(Sector, Dimension, Sway);
		const FVector Rest = HCJUi::ProjectMapSector(Sector, Dimension, 0.0f);
		Points.Add(FVector(Center.X + Swayed.X * Radius.X, Center.Y + Swayed.Y * Radius.Y, Swayed.Z));
		RestPoints.Add(FVector2D(Center.X + Rest.X * Radius.X, Center.Y + Rest.Y * Radius.Y));
	}
	const auto LineBetween = [this](const FVector& A, const FVector& B, const FLinearColor& Color, float Thickness)
	{
		DrawLine(static_cast<float>(A.X), static_cast<float>(A.Y), static_cast<float>(B.X), static_cast<float>(B.Y), Color, Thickness);
	};
	const auto IsNeighbor = [Own, Graph](int32 Sector)
	{
		return Own != INDEX_NONE && Sector != Own && FMath::CountBits(static_cast<uint32>(Sector ^ Own)) == 1
			&& Graph->IsEdgeOpen(Own, FMath::FloorLog2(static_cast<uint32>(Sector ^ Own)));
	};
	const int32 OwnWormholeDest = (Own != INDEX_NONE && bUniverse) ? Universe->GetWormholeDest(Own) : INDEX_NONE;

	// ---- portals: each open edge once, from its end with the dimension bit clear; the far ones thin and dim ----
	for (int32 Sector = 0; Sector < NumSectors; ++Sector)
	{
		for (int32 Dim = 0; Dim < Dimension; ++Dim)
		{
			const int32 Neighbor = Sector ^ (1 << Dim);
			if ((Sector & (1 << Dim)) != 0 || !Graph->IsEdgeOpen(Sector, Dim) || Sector == Own || Neighbor == Own)
			{
				continue;
			}
			const float Depth = 0.5f * static_cast<float>(Points[Sector].Z + Points[Neighbor].Z);
			LineBetween(Points[Sector], Points[Neighbor], MapAlpha(MapEdgeColor, FMath::Lerp(0.22f, 0.7f, Depth)),
				Depth > 0.55f ? FMath::Max(1.0f, 1.6f * UIScale) : 1.0f);
		}
	}

	// ---- wormholes: violet dashed arcs, so that they are not taken for portals ----
	if (bUniverse)
	{
		for (const FHCJWormholeLink& Link : Universe->GetWormholeLinks())
		{
			if (!Points.IsValidIndex(Link.SectorA) || !Points.IsValidIndex(Link.SectorB))
			{
				continue;
			}
			const bool bOwnLink = Own == Link.SectorA || Own == Link.SectorB;
			const FVector2D A(Points[Link.SectorA].X, Points[Link.SectorA].Y);
			const FVector2D B(Points[Link.SectorB].X, Points[Link.SectorB].Y);
			// Bowed to one side of the straight line, where the portals run.
			const FVector2D Along = B - A;
			const FVector2D Control = (A + B) * 0.5 + FVector2D(-Along.Y, Along.X) * 0.16;
			constexpr int32 NumSteps = 20;
			FVector2D Previous = A;
			for (int32 Step = 1; Step <= NumSteps; ++Step)
			{
				const double T = static_cast<double>(Step) / NumSteps;
				const FVector2D Next = A * ((1.0 - T) * (1.0 - T)) + Control * (2.0 * T * (1.0 - T)) + B * (T * T);
				if ((Step & 1) != 0)
				{
					DrawLine(static_cast<float>(Previous.X), static_cast<float>(Previous.Y), static_cast<float>(Next.X), static_cast<float>(Next.Y),
						MapAlpha(MapWormholeColor, bOwnLink ? 1.0f : 0.55f), bOwnLink ? FMath::Max(2.0f, 2.6f * UIScale) : FMath::Max(1.0f, 1.5f * UIScale));
				}
				Previous = Next;
			}
		}
	}

	// ---- the way to the target: yellow, hop by hop ----
	int32 FirstHopSector = INDEX_NONE;
	if (TargetSector != INDEX_NONE && Own != INDEX_NONE && bUniverse)
	{
		int32 Current = Own;
		for (int32 Step = 0; Step < NumSectors && Current != TargetSector; ++Step)
		{
			const int32 Hop = Universe->GetRoutes().NextHop(Current, TargetSector);
			const int32 Next = Hop == HCJ::WormholeHop ? Universe->GetWormholeDest(Current) : (Hop >= 0 && Hop < Dimension) ? (Current ^ (1 << Hop)) : INDEX_NONE;
			if (!Points.IsValidIndex(Next))
			{
				break;
			}
			if (Current == Own)
			{
				FirstHopSector = Next;
			}
			else
			{
				// (A wormhole on the way: thinner, straight across - its violet arc is drawn anyway.)
				const bool bWormhole = Hop == HCJ::WormholeHop;
				LineBetween(Points[Current], Points[Next], MapAlpha(MapGuideColor, bWormhole ? 0.6f : 0.85f), bWormhole ? 1.0f : FMath::Max(1.5f, 2.0f * UIScale));
			}
			Current = Next;
		}
	}

	// ---- one jump away: the lines out of the own sector ----
	if (Own != INDEX_NONE)
	{
		for (int32 Dim = 0; Dim < Dimension; ++Dim)
		{
			if (Graph->IsEdgeOpen(Own, Dim))
			{
				const int32 Neighbor = Own ^ (1 << Dim);
				LineBetween(Points[Own], Points[Neighbor], Neighbor == FirstHopSector ? MapGuideColor : MapAlpha(MapAccentColor, 0.9f), FMath::Max(2.0f, 2.8f * UIScale));
			}
		}
		// The line just travelled lights up and fades.
		const float JumpAge = Now - Map.SectorTime;
		if (JumpAge >= 0.0f && JumpAge < MapPulseSeconds && Points.IsValidIndex(Map.PrevSector))
		{
			LineBetween(Points[Map.PrevSector], Points[Own], MapAlpha(FLinearColor::White, FMath::Square(1.0f - JumpAge / MapPulseSeconds)), FMath::Max(3.0f, 4.5f * UIScale));
		}
	}

	// ---- the sectors: the far ones first ----
	const auto DotHalf = [Unit](float Depth) { return FMath::Lerp(2.0f, 3.6f, Depth) * Unit; };
	constexpr float BadgeTextSize = MapBadgeTextSize;
	const FVector2D DigitSize = Map.DigitSize;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Sector = 0; Sector < NumSectors; ++Sector)
		{
			const float Depth = static_cast<float>(Points[Sector].Z);
			if ((Depth >= 0.5f) != (Pass == 1))
			{
				continue;
			}
			const float X = static_cast<float>(Points[Sector].X);
			const float Y = static_cast<float>(Points[Sector].Y);
			const bool bNear = IsNeighbor(Sector);
			const bool bWormholeEnd = Sector == OwnWormholeDest;
			const float Brightness = (bNear || bWormholeEnd || Sector == Own) ? 1.0f : FMath::Lerp(0.5f, 1.0f, Depth);
			const float Half = DotHalf(Depth) * ((bNear || bWormholeEnd) ? 1.35f : 1.0f);
			const float Thin = FMath::Max(1.0f, 1.5f * UIScale);

			const bool bHole = bUniverse && Universe->HasBlackHole(Sector);
			const FLinearColor Tint = bUniverse ? FMath::Lerp(Universe->GetSectorTint(Sector), FLinearColor::White, 0.2f) : FLinearColor::White;
			if (Sector != Own)
			{
				DrawRect(bHole ? FLinearColor::Black : MapShade(Tint, Brightness), X - Half, Y - Half, 2.0f * Half, 2.0f * Half);
			}
			if (bHole)
			{
				// A black core in a ring of fire.
				DrawCircle(X, Y, Half + 2.0f * Unit, MapShade(MapHoleColor, Brightness), Thin, 10);
			}
			if (bUniverse && Universe->HasStation(Sector))
			{
				// A white frame: the station with its turrets.
				const float Frame = Half + (bHole ? 4.5f : 2.5f) * Unit;
				const FLinearColor FrameColor = MapShade(FLinearColor::White, Brightness);
				DrawLine(X - Frame, Y - Frame, X + Frame, Y - Frame, FrameColor, Thin);
				DrawLine(X + Frame, Y - Frame, X + Frame, Y + Frame, FrameColor, Thin);
				DrawLine(X + Frame, Y + Frame, X - Frame, Y + Frame, FrameColor, Thin);
				DrawLine(X - Frame, Y + Frame, X - Frame, Y - Frame, FrameColor, Thin);
			}
			if (bNear || bWormholeEnd)
			{
				// One jump away: a diamond (a portal leads there) or a round ring (the wormhole does).
				const FLinearColor RingColor = Sector == FirstHopSector ? MapGuideColor : bNear ? MapAccentColor : MapWormholeColor;
				const float Ring = Half + 5.5f * Unit;
				const float Thick = FMath::Max(1.5f, 2.0f * UIScale);
				if (bNear)
				{
					DrawLine(X, Y - Ring, X + Ring, Y, RingColor, Thick);
					DrawLine(X + Ring, Y, X, Y + Ring, RingColor, Thick);
					DrawLine(X, Y + Ring, X - Ring, Y, RingColor, Thick);
					DrawLine(X - Ring, Y, X, Y - Ring, RingColor, Thick);
				}
				else
				{
					DrawCircle(X, Y, Ring * 0.85f, RingColor, Thick, 12);
				}
			}
			if (Sector == FightSector)
			{
				// A fight: a red ring that beats, four sparks flying off it.
				const float Beat = 0.5f + 0.5f * FMath::Sin(Now * 7.0f);
				const float Ring = Half + (7.0f + 2.5f * Beat) * Unit;
				const FLinearColor FightColor = MapAlpha(MapHostileColor, 0.65f + 0.35f * Beat);
				DrawCircle(X, Y, Ring, FightColor, FMath::Max(1.5f, 2.0f * UIScale), 12);
				for (int32 Spark = 0; Spark < 4; ++Spark)
				{
					const float SX = (Spark & 1) != 0 ? 1.0f : -1.0f;
					const float SY = (Spark & 2) != 0 ? 1.0f : -1.0f;
					DrawLine(X + SX * Ring * 0.85f, Y + SY * Ring * 0.85f, X + SX * (Ring * 0.85f + 4.0f * Unit), Y + SY * (Ring * 0.85f + 4.0f * Unit), FightColor, FMath::Max(1.5f, 2.0f * UIScale));
				}
			}
			if (Sector == TargetSector)
			{
				// The target: four corner brackets.
				const FLinearColor TargetColor = bLockedTarget ? MapHostileColor : MapGuideColor;
				const float Box = Half + 8.0f * Unit;
				const float Arm = 4.5f * Unit;
				const float Thick = FMath::Max(1.5f, 2.0f * UIScale);
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const float SX = (Corner & 1) != 0 ? 1.0f : -1.0f;
					const float SY = (Corner & 2) != 0 ? 1.0f : -1.0f;
					DrawLine(X + SX * Box, Y + SY * Box, X + SX * (Box - Arm), Y + SY * Box, TargetColor, Thick);
					DrawLine(X + SX * Box, Y + SY * Box, X + SX * Box, Y + SY * (Box - Arm), TargetColor, Thick);
				}
			}
		}
	}

	// ---- the own sector: found at a glance ----
	FVector2D OwnMark = FVector2D::ZeroVector;
	const float OwnRing = 8.5f * Unit;
	if (Own != INDEX_NONE)
	{
		OwnMark = FVector2D(Points[Own].X, Points[Own].Y);
		const float JumpAge = Now - Map.SectorTime;
		if (JumpAge >= 0.0f && JumpAge < MapSlideSeconds && Points.IsValidIndex(Map.PrevSector))
		{
			// The mark slides along the line it travelled.
			const float Alpha = 1.0f - FMath::Square(1.0f - JumpAge / MapSlideSeconds);
			OwnMark = FMath::Lerp(FVector2D(Points[Map.PrevSector].X, Points[Map.PrevSector].Y), OwnMark, static_cast<double>(Alpha));
		}
		const float X = static_cast<float>(OwnMark.X);
		const float Y = static_cast<float>(OwnMark.Y);
		const FLinearColor Tint = bUniverse ? FMath::Lerp(Universe->GetSectorTint(Own), FLinearColor::White, 0.2f) : FLinearColor::White;
		// A dark disc under it, so that the lines that meet here do not run through the mark.
		DrawRect(FLinearColor(0.0f, 0.02f, 0.05f, 1.0f), X - OwnRing * 0.75f, Y - OwnRing * 0.75f, OwnRing * 1.5f, OwnRing * 1.5f);
		const float Core = 3.8f * Unit;
		DrawRect(MapShade(Tint, 1.0f), X - Core, Y - Core, 2.0f * Core, 2.0f * Core);
		DrawCircle(X, Y, OwnRing, FLinearColor::White, FMath::Max(2.0f, 2.6f * UIScale), 16);
		// A slow halo; rings running out after a jump or a new ship.
		const float Breath = 0.5f + 0.5f * FMath::Sin(Now * 2.6f);
		DrawCircle(X, Y, OwnRing + (3.0f + 2.0f * Breath) * Unit, MapAlpha(MapAccentColor, 0.75f - 0.45f * Breath), FMath::Max(1.0f, 1.5f * UIScale), 16);
		for (int32 Ring = 0; Ring < 2; ++Ring)
		{
			const float RingAge = (JumpAge - 0.22f * Ring) / (MapPulseSeconds - 0.22f);
			if (RingAge >= 0.0f && RingAge < 1.0f)
			{
				DrawCircle(X, Y, OwnRing + (4.0f + 30.0f * RingAge) * Unit, MapAlpha(FLinearColor::White, FMath::Square(1.0f - RingAge)), FMath::Max(1.5f, 2.2f * UIScale), 20);
			}
		}
	}

	// ---- enemies: a red badge with their number at the upper right of the mark ----
	// (Where it lies at rest is kept: the labels stay off it.)
	TArray<FBox2D, TInlineAllocator<64>> BadgeBoxes;
	const auto BadgeBox = [&](int32 Sector, const FVector2D& At)
	{
		const float BadgeHeight = static_cast<float>(DigitSize.Y) - 1.0f;
		const float BadgeWidth = FMath::Max(static_cast<float>(DigitSize.X) * (Occupants[Sector] > 9 ? 2.0f : 1.0f) + 4.0f * Unit, BadgeHeight);
		const float Out = Sector == Own ? OwnRing * 0.75f : DotHalf(static_cast<float>(Points[Sector].Z));
		return FBox2D(FVector2D(At.X + Out, At.Y - Out - BadgeHeight), FVector2D(At.X + Out + BadgeWidth, At.Y - Out));
	};
	for (int32 Sector = 0; Sector < NumSectors; ++Sector)
	{
		if (Occupants[Sector] <= 0 || Map.CountNames.Num() < 10)
		{
			continue;
		}
		const FString& Count = Map.CountNames[FMath::Min(Occupants[Sector], 10) - 1];
		const FBox2D Box = BadgeBox(Sector, Sector == Own ? OwnMark : FVector2D(Points[Sector].X, Points[Sector].Y));
		BadgeBoxes.Add(BadgeBox(Sector, RestPoints[Sector]));
		const float X = static_cast<float>(Box.Min.X);
		const float Y = static_cast<float>(Box.Min.Y);
		const float BadgeWidth = static_cast<float>(Box.Max.X - Box.Min.X);
		const float BadgeHeight = static_cast<float>(Box.Max.Y - Box.Min.Y);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), X - 1.0f, Y - 1.0f, BadgeWidth + 2.0f, BadgeHeight + 2.0f);
		DrawRect(MapShade(MapHostileColor, 0.85f), X, Y, BadgeWidth, BadgeHeight);
		DrawLabel(Count, FVector2D(X + BadgeWidth * 0.5f, Y - 1.0f), FLinearColor::White, BadgeTextSize, true, true);
	}

	// ---- labels: the own sector, every sector one jump away, the target ----
	const FString& YouText = MapYouText;
	TArray<FHCJMapLabel, TInlineAllocator<MapMaxLabels>> Labels;
	const FString* LabelTexts[MapMaxLabels];
	FLinearColor LabelColors[MapMaxLabels];
	int32 LabelSectors[MapMaxLabels];
	bool bLabelBold[MapMaxLabels];
	constexpr float LabelTextSize = MapLabelTextSize;
	constexpr float YouTextSize = MapYouTextSize;
	const auto AddLabel = [&](int32 Sector, const FString& Text, const FLinearColor& Color, float MarkRadius, bool bBold)
	{
		if (Labels.Num() >= MapMaxLabels)
		{
			return;
		}
		const int32 Index = Labels.Num();
		FHCJMapLabel& Label = Labels.AddDefaulted_GetRef();
		Label.Anchor = RestPoints[Sector];
		Label.AnchorRadius = MarkRadius;
		Label.Size = (bBold ? Map.YouSize : Map.NameSizes[Sector]) + FVector2D(4.0f * Unit, 0.0);
		LabelTexts[Index] = &Text;
		LabelColors[Index] = Color;
		LabelSectors[Index] = Sector;
		bLabelBold[Index] = bBold;
	};
	if (Own != INDEX_NONE)
	{
		AddLabel(Own, YouText, FLinearColor::White, OwnRing + 4.0f * Unit, true);
		for (int32 Dim = 0; Dim < Dimension; ++Dim)
		{
			if (Graph->IsEdgeOpen(Own, Dim))
			{
				// The text and the colour of the "PORTAL > 0101" marker in the world.
				const int32 Neighbor = Own ^ (1 << Dim);
				const FLinearColor PortalColor = bUniverse ? FMath::Lerp(Universe->GetSectorTint(Neighbor), FLinearColor::White, 0.6f) : FLinearColor::White;
				AddLabel(Neighbor, Map.SectorNames[Neighbor], Neighbor == FirstHopSector ? MapGuideColor : FLinearColor(PortalColor.R, PortalColor.G, PortalColor.B, 1.0f),
					DotHalf(static_cast<float>(Points[Neighbor].Z)) * 1.35f + 5.5f * Unit, false);
			}
		}
		if (Points.IsValidIndex(OwnWormholeDest))
		{
			AddLabel(OwnWormholeDest, Map.SectorNames[OwnWormholeDest], OwnWormholeDest == FirstHopSector ? MapGuideColor : MapWormholeColor,
				DotHalf(static_cast<float>(Points[OwnWormholeDest].Z)) * 1.35f + 5.5f * Unit, false);
		}
	}
	if (TargetSector != INDEX_NONE && !IsNeighbor(TargetSector) && TargetSector != OwnWormholeDest)
	{
		AddLabel(TargetSector, Map.SectorNames[TargetSector], bLockedTarget ? MapHostileColor : MapGuideColor, DotHalf(static_cast<float>(Points[TargetSector].Z)) + 8.0f * Unit, false);
	}
	if (Labels.Num() > 0)
	{
		const FBox2D Bounds(FVector2D(Left + 2.0f * UIScale, Top + TitleHeight), FVector2D(Left + Width - 2.0f * UIScale, Top + TitleHeight + AreaHeight));
		HCJUi::PlaceMapLabels(Labels, Bounds, RestPoints, 3.0f * Unit, Own != INDEX_NONE ? RestPoints[Own] : FVector2D(Center), BadgeBoxes);
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			// Placed at rest; the label rides with its mark.
			const int32 Sector = LabelSectors[Index];
			const FVector2D Mark = (Sector == Own) ? OwnMark : FVector2D(Points[Sector].X, Points[Sector].Y);
			const FVector2D At = Labels[Index].Center - Labels[Index].Anchor + Mark;
			const FVector2D Size = Labels[Index].Size;
			// A label that had to go a line away from its mark is tied to it.
			const FVector2D Offset = At - Mark;
			const double Reach = FMath::Max(FMath::Abs(Offset.X) - Size.X * 0.5, FMath::Abs(Offset.Y) - Size.Y * 0.5) - Labels[Index].AnchorRadius;
			if (Reach > 5.0 * Unit)
			{
				const FVector2D Direction = Offset.GetSafeNormal();
				const FVector2D From = Mark + Direction * Labels[Index].AnchorRadius;
				const FVector2D To = At - Direction * (0.5 * FMath::Min(Size.X, Size.Y));
				DrawLine(static_cast<float>(From.X), static_cast<float>(From.Y), static_cast<float>(To.X), static_cast<float>(To.Y), MapAlpha(LabelColors[Index], 0.6f), 1.0f);
			}
			DrawRect(FLinearColor(0.0f, 0.02f, 0.05f, 0.82f), static_cast<float>(At.X - Size.X * 0.5), static_cast<float>(At.Y - Size.Y * 0.5), static_cast<float>(Size.X), static_cast<float>(Size.Y));
			DrawLabel(*LabelTexts[Index], FVector2D(At.X, At.Y - Size.Y * 0.5), LabelColors[Index], bLabelBold[Index] ? YouTextSize : LabelTextSize, true, bLabelBold[Index]);
		}
	}

	DrawLabel(Map.Title, FVector2D(Left + 6.0f * UIScale, Top + 4.0f * UIScale), MapDimColor, MapTitleTextSize);
	if (LegendRow > 0.0f)
	{
		DrawMapLegend(FVector2D(Left, LegendTop), Width, bAnyStation, bAnyHole, bAnyWormhole);
	}
	return Height;
}
