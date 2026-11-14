// AHCJHUD, second part: what makes a fight and the world readable (targets, damage, portals, legend, messages, help).

#include "Game/HCJHUD.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/HCJGameState.h"
#include "Game/HCJGameplaySettings.h"
#include "Game/HCJPlayerController.h"
#include "Game/HCJPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Game/HCJUiLogic.h"
#include "Game/HCJUserSettings.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Assets/HCJAssetSettings.h"
#include "HypeCubeJam.h"
#include "Ship/HCJHealthComponent.h"
#include "Ship/HCJProjectile.h"
#include "Ship/HCJShipCharacter.h"
#include "Ship/HCJTorpedo.h"
#include "World/HCJPickup.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJStations.h"
#include "World/HCJWormholes.h"
#include "World/HCJUniverse.h"

namespace
{
	const FLinearColor ReadTextColor(0.85f, 0.95f, 1.0f, 1.0f);
	const FLinearColor ReadDimColor(0.55f, 0.65f, 0.75f, 1.0f);
	const FLinearColor ReadAccentColor(0.2f, 1.0f, 0.9f, 1.0f);
	const FLinearColor ReadWarningColor(1.0f, 0.25f, 0.15f, 1.0f);
	const FLinearColor ReadPanelColor(0.0f, 0.02f, 0.05f, 0.7f);

	FLinearColor Faded(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha);
	}

	/** Enemies are marked in one hostile colour, unlike anything else on the HUD; the locked target a little hotter. */
	const FLinearColor HostileColor(1.0f, 0.24f, 0.08f, 1.0f);
	const FLinearColor LockColor(1.0f, 0.62f, 0.12f, 1.0f);
	const FLinearColor LeadColor(1.0f, 0.9f, 0.15f, 1.0f);
	const FLinearColor FireColor(0.3f, 1.0f, 0.35f, 1.0f);

	/** Player colours can be dark (blue, purple): lift them so that thin lines read against space. */
	FLinearColor MarkerColor(const AHCJShipCharacter& Ship)
	{
		const AHCJPlayerState* State = Ship.GetPlayerState<AHCJPlayerState>();
		FLinearColor Color = State != nullptr ? FMath::Lerp(State->GetPlayerColor(), FLinearColor::White, 0.35f) : FLinearColor(1.0f, 0.4f, 0.3f);
		Color.A = 1.0f;
		return Color;
	}
}

void AHCJHUD::DrawCircle(float X, float Y, float Radius, const FLinearColor& Color, float Thickness, int32 Segments)
{
	Segments = FMath::Clamp(Segments, 6, 64);
	FVector2D Previous(X + Radius, Y);
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		const float Angle = 2.0f * UE_PI * Index / Segments;
		const FVector2D Next(X + Radius * FMath::Cos(Angle), Y + Radius * FMath::Sin(Angle));
		DrawLine(static_cast<float>(Previous.X), static_cast<float>(Previous.Y), static_cast<float>(Next.X), static_cast<float>(Next.Y), Color, Thickness);
		Previous = Next;
	}
}

void AHCJHUD::DrawShieldFacings(const AHCJShipCharacter& Ship, const FVector2D& Center, float Radius, float Alpha, float ViewerBearingDeg)
{
	const UHCJHealthComponent* Health = Ship.GetHealthComponent();
	const UWorld* World = GetWorld();
	if (Health == nullptr || World == nullptr || Radius < 4.0f)
	{
		return;
	}
	const float UIScale = GetUIScale();
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const float X = static_cast<float>(Center.X);
	const float Y = static_cast<float>(Center.Y);
	const float Thickness = FMath::Max(3.0f, Radius * 0.16f);
	const float RingRadius = Radius - Thickness * 0.5f;
	// Each facing owns a quarter of the ring; a gap separates them.
	constexpr float HalfSpanDeg = 37.0f;

	// From AngleA to AngleB (degrees, 0 = right, clockwise on screen) at ArcRadius.
	const auto DrawArc = [this, X, Y](float AngleA, float AngleB, float ArcRadius, const FLinearColor& Color, float Width)
	{
		const int32 Steps = FMath::Max(2, FMath::CeilToInt32(FMath::Abs(AngleB - AngleA) / 9.0f));
		FVector2D Previous(X + ArcRadius * FMath::Cos(FMath::DegreesToRadians(AngleA)), Y + ArcRadius * FMath::Sin(FMath::DegreesToRadians(AngleA)));
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			const float Angle = FMath::DegreesToRadians(FMath::Lerp(AngleA, AngleB, static_cast<float>(Step) / Steps));
			const FVector2D Next(X + ArcRadius * FMath::Cos(Angle), Y + ArcRadius * FMath::Sin(Angle));
			DrawLine(static_cast<float>(Previous.X), static_cast<float>(Previous.Y), static_cast<float>(Next.X), static_cast<float>(Next.Y), Color, Width);
			Previous = Next;
		}
	};

	for (int32 Facing = 0; Facing < HCJShield::NumFacings; ++Facing)
	{
		// Fore is up on the screen: the facing's yaw in the ship's frame, turned a quarter back.
		const float Middle = HCJShield::FacingYawDegrees(static_cast<EHCJShieldFacing>(Facing)) - 90.0f;
		const float Fraction = Health->GetFacingFraction(Facing);
		const float HitAge = Now - Ship.GetFacingHitTime(Facing);
		const float CollapseAge = Now - Ship.GetFacingCollapseTime(Facing);
		if (Fraction <= 0.0f)
		{
			// Down: the whole quarter in red, blinking; brighter and thicker right after it failed.
			const bool bFresh = CollapseAge >= 0.0f && CollapseAge < 1.2f;
			const float Blink = bFresh ? 0.55f + 0.45f * FMath::Sin(Now * 30.0f) : 0.45f + 0.3f * FMath::Sin(Now * 7.0f);
			DrawArc(Middle - HalfSpanDeg, Middle + HalfSpanDeg, RingRadius, Faded(ReadWarningColor, Alpha * Blink), bFresh ? Thickness * 1.4f : Thickness * 0.7f);
			continue;
		}
		FLinearColor Color = HCJShield::StrengthColor(Fraction);
		float Width = Thickness;
		if (HitAge >= 0.0f && HitAge < 0.35f)
		{
			// A hit: white, a little thicker, fading back.
			const float Flash = 1.0f - HitAge / 0.35f;
			Color = FMath::Lerp(Color, FLinearColor::White, Flash);
			Width *= 1.0f + 0.5f * Flash;
		}
		DrawArc(Middle - HalfSpanDeg, Middle + HalfSpanDeg, RingRadius, Faded(Color, Alpha * 0.22f), Thickness);
		const float Fill = HalfSpanDeg * FMath::Clamp(Fraction, 0.0f, 1.0f);
		DrawArc(Middle - Fill, Middle + Fill, RingRadius, Faded(Color, Alpha), Width);
		if (Fraction > 1.02f)
		{
			// Overcharge: a thin second arc outside.
			const float Extra = HalfSpanDeg * FMath::Clamp(Fraction - 1.0f, 0.0f, 1.0f);
			DrawArc(Middle - Extra, Middle + Extra, RingRadius + Thickness * 0.5f + FMath::Max(2.0f, 2.0f * UIScale), Faded(FLinearColor(0.8f, 1.0f, 1.0f), Alpha), FMath::Max(1.5f, Thickness * 0.4f));
		}
	}

	// The ship, nose up, in the colour of its hull state.
	const FLinearColor HullColor = Faded(FMath::Lerp(FLinearColor(1.0f, 0.15f, 0.1f), FLinearColor(0.2f, 1.0f, 0.3f), FMath::Clamp(Health->GetHealthFraction(), 0.0f, 1.0f)), Alpha);
	const float S = RingRadius - Thickness;
	const FVector2D Nose(X, Y - 0.62f * S);
	const FVector2D RightWing(X + 0.42f * S, Y + 0.5f * S);
	const FVector2D Tail(X, Y + 0.22f * S);
	const FVector2D LeftWing(X - 0.42f * S, Y + 0.5f * S);
	const float IconWidth = FMath::Max(1.5f, 2.0f * UIScale);
	const FVector2D Outline[] = { Nose, RightWing, Tail, LeftWing, Nose };
	for (int32 Index = 0; Index + 1 < UE_ARRAY_COUNT(Outline); ++Index)
	{
		DrawLine(static_cast<float>(Outline[Index].X), static_cast<float>(Outline[Index].Y), static_cast<float>(Outline[Index + 1].X), static_cast<float>(Outline[Index + 1].Y), HullColor, IconWidth);
	}

	if (ViewerBearingDeg > -360.0f)
	{
		// Where the viewer is around that ship: a wedge pointing in at the ring.
		const float Angle = FMath::DegreesToRadians(ViewerBearingDeg - 90.0f);
		const FVector2D Out(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2D Side(-Out.Y, Out.X);
		const FVector2D Tip = Center + Out * (Radius + 2.0f * UIScale);
		const FVector2D Base = Center + Out * (Radius + FMath::Max(7.0f, 9.0f * UIScale));
		const float Half = FMath::Max(3.5f, 4.5f * UIScale);
		const FLinearColor MarkColor = Faded(FLinearColor::White, Alpha);
		const FVector2D A = Base + Side * Half;
		const FVector2D B = Base - Side * Half;
		DrawLine(static_cast<float>(Tip.X), static_cast<float>(Tip.Y), static_cast<float>(A.X), static_cast<float>(A.Y), MarkColor, IconWidth);
		DrawLine(static_cast<float>(Tip.X), static_cast<float>(Tip.Y), static_cast<float>(B.X), static_cast<float>(B.Y), MarkColor, IconWidth);
		DrawLine(static_cast<float>(A.X), static_cast<float>(A.Y), static_cast<float>(B.X), static_cast<float>(B.Y), MarkColor, IconWidth);
	}
}

void AHCJHUD::DrawTargets(const AHCJShipCharacter& Ship)
{
	const UWorld* World = GetWorld();
	AHCJPlayerController* Controller = Cast<AHCJPlayerController>(PlayerOwner);
	if (!Controller || !World)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FRotationMatrix ViewAxes(ViewRotation);
	const FVector ViewForward = ViewAxes.GetUnitAxis(EAxis::X);
	const FVector ViewRight = ViewAxes.GetUnitAxis(EAxis::Y);
	const FVector ViewUp = ViewAxes.GetUnitAxis(EAxis::Z);

	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const float Thickness = FMath::Max(2.0f, 2.5f * UIScale);
	// Enemy markers are drawn heavier than anything else on the HUD, each line over a dark one so that it also reads
	// against a planet or a nebula.
	const float MarkerThickness = FMath::Max(3.0f, 4.0f * UIScale);
	const auto DrawMarkerLine = [this, MarkerThickness](float X1, float Y1, float X2, float Y2, const FLinearColor& LineColor, float Scale)
	{
		DrawLine(X1, Y1, X2, Y2, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f * LineColor.A), MarkerThickness * Scale + 2.0f);
		DrawLine(X1, Y1, X2, Y2, LineColor, MarkerThickness * Scale);
	};
	const FVector ShipLocation = Ship.GetActorLocation();
	const FVector Muzzle = Ship.GetMuzzleLocation();
	const FVector AimDirection = Ship.GetAimRotation().Vector();
	const FHCJWeaponDef& Weapon = Ship.GetFireWeaponDef();
	const double WeaponRange = static_cast<double>(Weapon.GetRange());

	struct FEnemy
	{
		AHCJShipCharacter* Ship = nullptr;
		FVector Location = FVector::ZeroVector;
		float Distance = 0.0f;
		float X = 0.0f;
		float Y = 0.0f;
		bool bOnScreen = false;
		bool bVisible = false;
		/** In view, but its marker would lie on a HUD panel: it gets an arrow like the ones out of view. */
		bool bCovered = false;
	};
	TArray<FEnemy, TInlineAllocator<16>> Enemies;
	TArray<FHCJTargetCandidate, TInlineAllocator<16>> Candidates;

	AHCJShipCharacter* CurrentTarget = Controller->GetTarget();
	int32 CurrentIndex = INDEX_NONE;

	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		AHCJShipCharacter* Other = *It;
		// A cloaked ship is not drawn on this machine and must not be given away by the HUD either.
		if (Other == &Ship || Other->IsDead() || Other->IsCloaked())
		{
			continue;
		}
		FEnemy Enemy;
		Enemy.Ship = Other;
		Enemy.Location = Other->GetActorLocation();
		Enemy.Distance = static_cast<float>(FVector::Dist(Enemy.Location, ShipLocation));
		if (Enemy.Distance > TargetMarkerDistance)
		{
			continue;
		}
		const FVector Screen = Canvas->Project(Enemy.Location, false);
		Enemy.X = static_cast<float>(Screen.X);
		Enemy.Y = static_cast<float>(Screen.Y);
		Enemy.bOnScreen = FVector::DotProduct(Enemy.Location - ViewLocation, ViewForward) > 0.0
			&& Enemy.X >= 0.0f && Enemy.Y >= 0.0f && Enemy.X <= Canvas->ClipX && Enemy.Y <= Canvas->ClipY;
		// Behind a rock or a station: still marked, but dimmer.
		Enemy.bVisible = Enemy.bOnScreen && !World->LineTraceTestByObjectType(ViewLocation, Enemy.Location, FCollisionObjectQueryParams(ECC_WorldStatic));

		FHCJTargetCandidate Candidate;
		Candidate.AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct((Enemy.Location - Muzzle).GetSafeNormal(), AimDirection)), -1.0f, 1.0f)));
		Candidate.Distance = Enemy.Distance;
		Candidate.bVisible = Enemy.bVisible;
		if (Other == CurrentTarget)
		{
			CurrentIndex = Enemies.Num();
		}
		Enemies.Add(Enemy);
		Candidates.Add(Candidate);

		// Hit feedback: a ship whose hull + shield dropped since the last frame flashes and sheds a number.
		if (const UHCJHealthComponent* Health = Other->GetHealthComponent())
		{
			const float Vitals = Health->GetHealth() + Health->GetShield();
			FShipWatch& Watch = ShipWatches.FindOrAdd(Other);
			if (Watch.Vitals > 0.0f && Vitals < Watch.Vitals - 0.5f)
			{
				Watch.LastHitTime = Now;
				// A stream of small hits (a phaser beam) grows the number that is already there, for up to a second.
				FDamageNumber* Running = DamageNumbers.FindByPredicate([Other, Now](const FDamageNumber& Number)
					{ return Number.Ship == Other && Now - Number.Time < 0.3f && Now - Number.FirstTime < 1.0f; });
				if (Running != nullptr)
				{
					Running->Amount += Watch.Vitals - Vitals;
					Running->Time = Now;
				}
				else if (Enemy.bOnScreen && DamageNumbers.Num() < 24)
				{
					DamageNumbers.Add({ Enemy.Location, Watch.Vitals - Vitals, Now, Other, Now });
				}
			}
			Watch.Vitals = Vitals;
		}
	}
	if (ShipWatches.Num() > 64)
	{
		for (auto It = ShipWatches.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	// Soft lock: the enemy nearest the crosshair, unless the player holds one.
	if (!Controller->IsTargetManual())
	{
		const int32 Best = HCJUi::PickTarget(Candidates, CurrentIndex);
		Controller->SetAutoTarget(Best != INDEX_NONE ? Enemies[Best].Ship : nullptr);
		CurrentTarget = Controller->GetTarget();
	}

	// Who is shooting at us: the ship nearest to where the last damage came from.
	const AHCJShipCharacter* Attacker = nullptr;
	if (Now - Controller->GetLastDamageTime() < 3.0f)
	{
		double BestDistSq = FMath::Square(1500.0);
		for (const FEnemy& Enemy : Enemies)
		{
			const double DistSq = FVector::DistSquared(Enemy.Location, Controller->GetLastDamageSource());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Attacker = Enemy.Ship;
			}
		}
	}

	const FEnemy* TargetEnemy = nullptr;
	for (FEnemy& Enemy : Enemies)
	{
		const bool bTarget = Enemy.Ship == CurrentTarget;
		if (bTarget)
		{
			TargetEnemy = &Enemy;
		}
		if (!Enemy.bOnScreen)
		{
			continue;
		}

		const FShipWatch* Watch = ShipWatches.Find(Enemy.Ship);
		const float HitAge = Watch != nullptr ? Now - Watch->LastHitTime : 1000.0f;
		const float Dim = Enemy.bVisible ? 1.0f : 0.45f;
		// Farther away the marker shrinks to a minimum and fades a little, it never disappears.
		const float Fade = Dim * FMath::Clamp(1.4f - Enemy.Distance / TargetMarkerDistance, 0.85f, 1.0f);
		FLinearColor Color = Faded(bTarget ? LockColor : HostileColor, Fade);
		if (HitAge < 0.2f)
		{
			Color = FMath::Lerp(FLinearColor::White, Color, HitAge / 0.2f);
		}
		const float MinHalf = (bTarget ? 28.0f : 22.0f) * TextScale;
		const float Half = FMath::Clamp(Canvas->ClipY * 440.0f / FMath::Max(Enemy.Distance, 1.0f), MinHalf, 110.0f * UIScale);
		const float Arm = Half * 0.5f;
		const float X = Enemy.X;
		const float Y = Enemy.Y;

		// The whole marker - bracket (diamond for the target), label above, bars below - must be clear of the HUD panels.
		const AHCJPlayerState* OtherState = Enemy.Ship->GetPlayerState<AHCJPlayerState>();
		const FString Label = FString::Printf(TEXT("%s  %d m"), OtherState != nullptr ? *OtherState->GetPlayerName() : TEXT("?"), FMath::RoundToInt32(Enemy.Distance / 100.0f));
		const float LabelScale = bTarget ? 1.15f : 1.0f;
		const FVector2D LabelSize = MeasureLabel(Label, LabelScale, bTarget);
		const float Extent = bTarget ? Half * 1.55f * 1.28f + Half * 0.35f : Half;
		const float LabelTop = Y - Extent - static_cast<float>(LabelSize.Y) - 3.0f * UIScale;
		const float HalfWidth = FMath::Max(Extent, static_cast<float>(LabelSize.X) * 0.5f + 3.0f);
		if (!HCJUi::IsMarkerClear(FBox2D(FVector2D(X - HalfWidth, LabelTop), FVector2D(X + HalfWidth, Y + Extent + 12.0f * UIScale)), PrevPanelRects, 2.0f * UIScale))
		{
			Enemy.bCovered = true;
			continue;
		}

		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float SX = (Corner & 1) != 0 ? 1.0f : -1.0f;
			const float SY = (Corner & 2) != 0 ? 1.0f : -1.0f;
			DrawMarkerLine(X + SX * Half, Y + SY * Half, X + SX * (Half - Arm), Y + SY * Half, Color, bTarget ? 1.3f : 1.0f);
			DrawMarkerLine(X + SX * Half, Y + SY * Half, X + SX * Half, Y + SY * (Half - Arm), Color, bTarget ? 1.3f : 1.0f);
		}
		if (bTarget)
		{
			// The locked target: a diamond around the bracket.
			const float D = Half * 1.55f;
			DrawMarkerLine(X - D, Y, X, Y - D, Color, 1.0f);
			DrawMarkerLine(X, Y - D, X + D, Y, Color, 1.0f);
			DrawMarkerLine(X + D, Y, X, Y + D, Color, 1.0f);
			DrawMarkerLine(X, Y + D, X - D, Y, Color, 1.0f);
			// ... and four ticks that breathe in and out, so the eye finds it among several brackets.
			const float Breath = D * (1.18f + 0.1f * FMath::Sin(Now * 5.0f));
			const float Tick = Half * 0.35f;
			DrawMarkerLine(X - Breath - Tick, Y, X - Breath, Y, Color, 1.0f);
			DrawMarkerLine(X + Breath, Y, X + Breath + Tick, Y, Color, 1.0f);
			DrawMarkerLine(X, Y - Breath - Tick, X, Y - Breath, Color, 1.0f);
			DrawMarkerLine(X, Y + Breath, X, Y + Breath + Tick, Color, 1.0f);
		}

		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f * Fade), X - static_cast<float>(LabelSize.X) * 0.5f - 3.0f, LabelTop, static_cast<float>(LabelSize.X) + 6.0f, static_cast<float>(LabelSize.Y));
		DrawLabel(Label, FVector2D(X, LabelTop), Color, LabelScale, true, bTarget);

		if (const UHCJHealthComponent* Health = Enemy.Ship->GetHealthComponent())
		{
			// Hull (and shield above it) as a thin bar under the bracket.
			const float BarWidth = 2.0f * Half;
			const float BarHeight = FMath::Max(3.0f, 4.0f * UIScale);
			const float BarY = Y + Extent + 4.0f * UIScale;
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f * Fade), X - Half - 1.0f, BarY - 1.0f, BarWidth + 2.0f, BarHeight + 2.0f);
			DrawRect(Faded(FMath::Lerp(FLinearColor(1.0f, 0.15f, 0.1f), FLinearColor(0.2f, 1.0f, 0.3f), FMath::Clamp(Health->GetHealthFraction(), 0.0f, 1.0f)), Fade),
				X - Half, BarY, BarWidth * FMath::Clamp(Health->GetHealthFraction(), 0.0f, 1.0f), BarHeight);
			if (Health->GetShield() > 0.5f)
			{
				DrawRect(Faded(FLinearColor(0.2f, 0.7f, 1.0f), Fade), X - Half, BarY - BarHeight, BarWidth * FMath::Clamp(Health->GetShieldFraction(), 0.0f, 1.0f), FMath::Max(2.0f, BarHeight * 0.6f));
			}
		}
	}

	// Lead marker for the locked target: shoot here to hit it if it keeps its course. Green when the guns are on it.
	if (TargetEnemy != nullptr && TargetEnemy->bOnScreen && !TargetEnemy->bCovered && TargetEnemy->Distance < WeaponRange * 1.2)
	{
		FVector AimPoint;
		if (HCJ::ComputeLeadAim(Muzzle, TargetEnemy->Location, TargetEnemy->Ship->GetVelocity(), Weapon.ProjectileSpeed, AimPoint)
			&& FVector::DotProduct(AimPoint - ViewLocation, ViewForward) > 0.0)
		{
			const FVector Screen = Canvas->Project(AimPoint, false);
			const float X = static_cast<float>(Screen.X);
			const float Y = static_cast<float>(Screen.Y);
			if (X >= 0.0f && Y >= 0.0f && X <= Canvas->ClipX && Y <= Canvas->ClipY)
			{
				const FVector ToLead = AimPoint - Muzzle;
				const float LeadDistance = static_cast<float>(ToLead.Size());
				const float AimError = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(ToLead.GetSafeNormal(), AimDirection)), -1.0f, 1.0f)));
				const float HitRadius = FMath::Max(UHCJGameplaySettings::Get().AimAssistRadius, HCJ::ShipCollisionRadius);
				const bool bInRange = TargetEnemy->Distance < WeaponRange;
				const bool bFire = bInRange && TargetEnemy->bVisible && HCJUi::IsOnLead(AimError, LeadDistance, HitRadius);
				const FLinearColor Color = bFire ? FireColor : Faded(LeadColor, bInRange ? 1.0f : 0.5f);
				const float Radius = (bFire ? 12.0f : 10.0f) * TextScale;
				DrawCircle(X, Y, Radius, Color, Thickness, 20);
				DrawLine(X - Radius * 0.45f, Y, X + Radius * 0.45f, Y, Color, Thickness);
				DrawLine(X, Y - Radius * 0.45f, X, Y + Radius * 0.45f, Color, Thickness);
				// Tied to its target, so it is clear whose lead this is.
				const FVector2D ToTarget(TargetEnemy->X - X, TargetEnemy->Y - Y);
				if (ToTarget.SizeSquared() > FMath::Square(1.5 * Radius))
				{
					const FVector2D Direction = ToTarget.GetSafeNormal();
					DrawLine(X + static_cast<float>(Direction.X) * Radius, Y + static_cast<float>(Direction.Y) * Radius, TargetEnemy->X, TargetEnemy->Y, Faded(Color, 0.6f), FMath::Max(1.0f, 1.5f * UIScale));
				}
				if (bFire)
				{
					DrawLabel(TEXT("FIRE"), FVector2D(X, Y + Radius + 2.0f * UIScale), FireColor, 1.0f, true, true);
				}
			}
		}
	}

	// Floating damage numbers.
	for (int32 Index = DamageNumbers.Num() - 1; Index >= 0; --Index)
	{
		const FDamageNumber& Number = DamageNumbers[Index];
		const float Age = Now - Number.Time;
		if (Age > 0.9f || Age < 0.0f)
		{
			DamageNumbers.RemoveAtSwap(Index);
			continue;
		}
		if (FVector::DotProduct(Number.Location - ViewLocation, ViewForward) <= 0.0)
		{
			continue;
		}
		const FVector Screen = Canvas->Project(Number.Location, false);
		DrawLabel(FString::FromInt(FMath::Max(1, FMath::RoundToInt32(Number.Amount))),
			FVector2D(Screen.X + 26.0f * UIScale, Screen.Y - (20.0f + 60.0f * Age) * UIScale), Faded(FLinearColor(1.0f, 0.95f, 0.6f), FMath::Clamp(1.8f - 2.0f * Age, 0.0f, 1.0f)), 1.3f, true, true);
	}

	// Arrows at the screen edge towards the enemies that are not in view, nearest first.
	TArray<const FEnemy*, TInlineAllocator<16>> OffScreen;
	for (const FEnemy& Enemy : Enemies)
	{
		if (!Enemy.bOnScreen || Enemy.bCovered)
		{
			OffScreen.Add(&Enemy);
		}
	}
	OffScreen.Sort([](const FEnemy& A, const FEnemy& B) { return A.Distance < B.Distance; });
	const FVector2D Centre(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	// Inside the panels on the right and above the hint zone at the bottom, so that an arrow never lies on them.
	const FVector2D Ring(Canvas->ClipX * 0.5f - 24.0f * UIScale - 285.0f * TextScale, Canvas->ClipY * 0.5f - 150.0f * TextScale);
	for (int32 Index = 0; Index < FMath::Min(OffScreen.Num(), 6); ++Index)
	{
		const FEnemy& Enemy = *OffScreen[Index];
		const bool bHot = Enemy.Ship == Attacker || Enemy.Ship == CurrentTarget;
		const FVector ToOther = Enemy.Location - ViewLocation;
		float Angle = 0.0f;
		const FVector2D Position = HCJUi::ComputeEdgeMarker(static_cast<float>(FVector::DotProduct(ToOther, ViewRight)),
			static_cast<float>(FVector::DotProduct(ToOther, ViewUp)), Centre, Ring, Angle);
		const float Pulse = Enemy.Ship == Attacker ? 0.7f + 0.3f * FMath::Sin(Now * 14.0f) : 1.0f;
		const FLinearColor Color = Faded(Enemy.Ship == CurrentTarget ? LockColor : HostileColor, (bHot ? 1.0f : 0.9f) * Pulse);
		const float Size = (bHot ? 36.0f : 28.0f) * TextScale;
		const FVector2D Forward(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2D Side(-Forward.Y, Forward.X);
		// A solid arrow head: lines fanned from the tip across the base.
		const FVector2D Tip = Position + Forward * Size;
		constexpr int32 Fan = 9;
		for (int32 Step = 0; Step <= Fan; ++Step)
		{
			const double T = -1.0 + 2.0 * Step / Fan;
			const FVector2D Base = Position - Forward * Size * 0.45 + Side * (Size * 0.62 * T);
			if (Step == 0 || Step == Fan)
			{
				// A dark edge, so the arrow keeps its shape over anything bright.
				DrawLine(static_cast<float>(Tip.X), static_cast<float>(Tip.Y), static_cast<float>(Base.X), static_cast<float>(Base.Y), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f * Color.A), MarkerThickness + 3.0f);
			}
			DrawLine(static_cast<float>(Tip.X), static_cast<float>(Tip.Y), static_cast<float>(Base.X), static_cast<float>(Base.Y), Color, MarkerThickness);
		}
		const FVector2D LabelPosition = Position - Forward * (Size * 0.45f + 16.0f * TextScale);
		const FString Label = Enemy.Ship == Attacker ? FString::Printf(TEXT("! %d m"), FMath::RoundToInt32(Enemy.Distance / 100.0f)) : FString::Printf(TEXT("%d m"), FMath::RoundToInt32(Enemy.Distance / 100.0f));
		DrawLabel(Label, FVector2D(LabelPosition.X, LabelPosition.Y - 7.0f * TextScale), Color, 1.0f, true, bHot);
	}

	if (CurrentTarget != nullptr)
	{
		DrawTargetPanel(Ship, *CurrentTarget, Controller->IsTargetManual());
	}

	// A homing missile is after us.
	const float WarningAge = Now - Controller->GetLastMissileWarningTime();
	if (WarningAge >= 0.0f && WarningAge < 2.5f)
	{
		DrawLabel(TEXT("MISSILE LOCK  -  BREAK AWAY"), FVector2D(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.11f),
			Faded(HostileColor, 0.6f + 0.4f * FMath::Sin(Now * 18.0f)), 1.7f, true, true);
	}
	DrawTorpedoWarning(Ship);
}

void AHCJHUD::DrawTorpedoWarning(const AHCJShipCharacter& Ship)
{
	const UWorld* World = GetWorld();
	if (World == nullptr || !PlayerOwner)
	{
		return;
	}
	// The torpedoes this machine sees whose (replicated) target is the own ship; the nearest one is marked.
	const FVector ShipLocation = Ship.GetActorLocation();
	const AHCJProjectile* Nearest = nullptr;
	double NearestDistSq = TNumericLimits<double>::Max();
	int32 NumIncoming = 0;
	for (const TWeakObjectPtr<AHCJProjectile>& Entry : AHCJProjectile::GetLiveTorpedoes())
	{
		const AHCJProjectile* Torpedo = Entry.Get();
		if (Torpedo == nullptr || Torpedo->GetWorld() != World || Torpedo->GetHomingTarget() != &Ship || Torpedo->IsSpent() || Torpedo->IsActorBeingDestroyed())
		{
			continue;
		}
		++NumIncoming;
		const double DistSq = FVector::DistSquared(Torpedo->GetActorLocation(), ShipLocation);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Torpedo;
		}
	}
	if (Nearest == nullptr)
	{
		return;
	}

	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const int32 Metres = FMath::RoundToInt32(FMath::Sqrt(NearestDistSq) / 100.0);
	const FHCJWeaponDef& Def = UHCJGameplaySettings::Get().GetWeapon(EHCJWeapon::Torpedo);
	// Brighter than the torpedo's own colour: this has to be read at once against any sky.
	const FLinearColor Base(1.0f, FMath::Max(Def.Color.G, 0.5f), FMath::Max(Def.Color.B, 0.2f), 1.0f);
	const float Pulse = 0.85f + 0.15f * FMath::Sin(Now * 20.0f);

	if (WarnedTorpedo.Get() != Nearest)
	{
		// One cue per torpedo.
		WarnedTorpedo = Nearest;
		const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
		UHCJAssetSettings::PlaySound2D(this, Settings.TorpedoWarningSounds.Num() > 0 ? Settings.TorpedoWarningSounds : Settings.LowHealthSounds, TEXT("torpedo warning"), Settings.UiCueVolume);
		UE_LOG(LogHCJ, Log, TEXT("Torpedo warning: %s is homing on the own ship, %d m out"), *Nearest->GetName(), Metres);
	}

	const FString Line = NumIncoming > 1 ? FString::Printf(TEXT("INCOMING TORPEDO x%d  -  BREAK AWAY"), NumIncoming) : FString(TEXT("INCOMING TORPEDO  -  BREAK AWAY"));
	const float LineY = Canvas->ClipY * 0.155f;
	const float LineHeight = DrawLabel(Line, FVector2D(Canvas->ClipX * 0.5f, LineY), Faded(Base, Pulse), 1.7f, true, true);
	DrawLabel(FString::Printf(TEXT("%d m"), Metres), FVector2D(Canvas->ClipX * 0.5f, LineY + LineHeight), Base, 1.2f, true, true);

	// Where it is: a marker on it, or a chevron at the edge of the view towards it.
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FRotationMatrix ViewAxes(ViewRotation);
	const FVector ToTorpedo = Nearest->GetActorLocation() - ViewLocation;
	const FVector Screen = Canvas->Project(Nearest->GetActorLocation(), false);
	const float X = static_cast<float>(Screen.X);
	const float Y = static_cast<float>(Screen.Y);
	const float Size = 18.0f * TextScale;
	const float Thickness = FMath::Max(2.0f, 3.0f * UIScale);
	const FString Tag = FString::Printf(TEXT("TORPEDO  %d m"), Metres);
	const FVector2D TagSize = MeasureLabel(Tag, 1.0f, true);
	const bool bOnScreen = FVector::DotProduct(ToTorpedo, ViewAxes.GetUnitAxis(EAxis::X)) > 0.0 && X >= 0.0f && Y >= 0.0f && X <= Canvas->ClipX && Y <= Canvas->ClipY
		&& HCJUi::IsMarkerClear(FBox2D(FVector2D(X - FMath::Max(Size * 1.5f, static_cast<float>(TagSize.X) * 0.5f), Y - Size * 1.5f),
			FVector2D(X + FMath::Max(Size * 1.5f, static_cast<float>(TagSize.X) * 0.5f), Y + Size * 1.5f + static_cast<float>(TagSize.Y) + 4.0f)), PrevPanelRects, 2.0f * UIScale);
	if (bOnScreen)
	{
		// A diamond that closes in on the torpedo, twice a second.
		const float Close = 1.0f + 0.6f * (1.0f - FMath::Frac(Now * 2.0f));
		const float D = Size * Close;
		const FLinearColor Color = Faded(Base, 0.75f + 0.25f * Pulse);
		DrawLine(X - D, Y, X, Y - D, Color, Thickness);
		DrawLine(X, Y - D, X + D, Y, Color, Thickness);
		DrawLine(X + D, Y, X, Y + D, Color, Thickness);
		DrawLine(X, Y + D, X - D, Y, Color, Thickness);
		DrawLabel(Tag, FVector2D(X, Y + Size * 1.6f + 2.0f), Color, 1.0f, true, true);
	}
	else
	{
		float Angle = 0.0f;
		// Inside the ring of the enemy arrows, so the two do not sit on each other.
		const FVector2D Position = HCJUi::ComputeEdgeMarker(static_cast<float>(FVector::DotProduct(ToTorpedo, ViewAxes.GetUnitAxis(EAxis::Y))),
			static_cast<float>(FVector::DotProduct(ToTorpedo, ViewAxes.GetUnitAxis(EAxis::Z))), FVector2D(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f),
			FVector2D(Canvas->ClipX * 0.5f - 24.0f * UIScale - 360.0f * TextScale, Canvas->ClipY * 0.5f - 205.0f * TextScale), Angle);
		const FVector2D Forward(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2D Side(-Forward.Y, Forward.X);
		const FLinearColor Color = Faded(Base, Pulse);
		for (int32 Chevron = 0; Chevron < 3; ++Chevron)
		{
			const FVector2D Tip = Position + Forward * (Size * (1.0 + 0.6 * Chevron));
			const FVector2D A = Tip - Forward * Size + Side * Size * 0.9;
			const FVector2D B = Tip - Forward * Size - Side * Size * 0.9;
			DrawLine(static_cast<float>(Tip.X), static_cast<float>(Tip.Y), static_cast<float>(A.X), static_cast<float>(A.Y), Color, Thickness);
			DrawLine(static_cast<float>(Tip.X), static_cast<float>(Tip.Y), static_cast<float>(B.X), static_cast<float>(B.Y), Color, Thickness);
		}
		const FVector2D TagPosition = Position - Forward * (Size + 14.0f * TextScale);
		DrawLabel(Tag, FVector2D(TagPosition.X, TagPosition.Y - static_cast<float>(TagSize.Y) * 0.5f), Base, 1.0f, true, true);
	}
}

void AHCJHUD::DrawTargetPanel(const AHCJShipCharacter& Ship, const AHCJShipCharacter& Target, bool bManual)
{
	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	const float Width = 250.0f * TextScale;
	const float Padding = 8.0f * UIScale;
	const float RowHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), 1.0f).Y) + 2.0f * UIScale;
	// Below the text rows: the target's shield facings with a line saying which one is towards the player.
	const float FacingRadius = FMath::Max(24.0f, 34.0f * UIScale);
	const float FacingBlock = 2.0f * FacingRadius + FMath::Max(22.0f, 26.0f * UIScale);
	const float Height = 2.0f * Padding + 4.6f * RowHeight + FacingBlock;
	const float Left = Canvas->ClipX - 24.0f * UIScale - Width;
	const float Top = Canvas->ClipY * 0.5f;

	NotePanel(Left, Top, Width, Height);
	DrawRect(ReadPanelColor, Left, Top, Width, Height);
	DrawRect(LockColor, Left, Top, FMath::Max(2.0f, 3.0f * UIScale), Height);

	const FHCJWeaponDef& Weapon = Ship.GetFireWeaponDef();
	const FVector ToTarget = Target.GetActorLocation() - Ship.GetActorLocation();
	const float Distance = static_cast<float>(ToTarget.Size());
	// Positive when the two ships get nearer.
	const float Closing = -static_cast<float>(FVector::DotProduct(Target.GetVelocity() - Ship.GetVelocity(), ToTarget.GetSafeNormal()));
	const bool bInRange = Distance < Weapon.GetRange();
	const AHCJPlayerState* TargetState = Target.GetPlayerState<AHCJPlayerState>();
	const UHCJHealthComponent* Health = Target.GetHealthComponent();

	float Y = Top + Padding;
	const float X = Left + Padding + 4.0f * UIScale;
	DrawLabel(bManual ? TEXT("TARGET  (held)") : TEXT("TARGET"), FVector2D(X, Y), ReadDimColor, 0.9f);
	Y += RowHeight * 0.9f;
	DrawLabel(TargetState != nullptr ? TargetState->GetPlayerName() : FString(TEXT("?")), FVector2D(X, Y), LockColor, 1.2f, false, true);
	Y += RowHeight * 1.3f;
	if (Health != nullptr)
	{
		const float BarWidth = Width - 2.0f * Padding - 8.0f * UIScale;
		const float BarHeight = FMath::Max(4.0f, 6.0f * UIScale);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), X, Y, BarWidth, 2.0f * BarHeight + 1.0f);
		DrawRect(FLinearColor(0.2f, 0.7f, 1.0f), X, Y, BarWidth * FMath::Clamp(Health->GetShieldFraction(), 0.0f, 1.0f), BarHeight);
		DrawRect(FMath::Lerp(FLinearColor(1.0f, 0.15f, 0.1f), FLinearColor(0.2f, 1.0f, 0.3f), FMath::Clamp(Health->GetHealthFraction(), 0.0f, 1.0f)),
			X, Y + BarHeight + 1.0f, BarWidth * FMath::Clamp(Health->GetHealthFraction(), 0.0f, 1.0f), BarHeight);
		Y += 2.0f * BarHeight + 5.0f * UIScale;
	}
	DrawLabel(FString::Printf(TEXT("%d m   %s %d m/s"), FMath::RoundToInt32(Distance / 100.0f), Closing >= 0.0f ? TEXT("closing") : TEXT("opening"), FMath::RoundToInt32(FMath::Abs(Closing) / 100.0f)),
		FVector2D(X, Y), ReadTextColor, 1.0f);
	Y += RowHeight;
	DrawLabel(bInRange ? FString::Printf(TEXT("%s IN RANGE"), *Weapon.Name) : FString::Printf(TEXT("%s out of range"), *Weapon.Name),
		FVector2D(X, Y), bInRange ? FireColor : ReadDimColor, 1.0f);
	Y += RowHeight;

	if (Health != nullptr && Health->GetNumFacings() == HCJShield::NumFacings)
	{
		// The target's four facings, its nose up, and where we are around it: the side our shots land on.
		const FVector LocalToUs = Target.GetActorTransform().InverseTransformVectorNoScale(-ToTarget);
		const EHCJShieldFacing Towards = HCJShield::SelectFacing(LocalToUs);
		const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(LocalToUs.Y), static_cast<float>(LocalToUs.X)));
		const FVector2D RingCenter(X + FacingRadius + FMath::Max(10.0f, 12.0f * UIScale), Y + FacingBlock * 0.5f);
		DrawShieldFacings(Target, RingCenter, FacingRadius, 1.0f, Bearing);

		const float TextX = static_cast<float>(RingCenter.X) + FacingRadius + FMath::Max(18.0f, 22.0f * UIScale);
		const float Fraction = Health->GetFacingFraction(static_cast<int32>(Towards));
		const bool bDown = Fraction <= 0.0f;
		float TextY = static_cast<float>(RingCenter.Y) - 1.6f * RowHeight;
		DrawLabel(TEXT("FACING YOU"), FVector2D(TextX, TextY), ReadDimColor, 0.9f);
		TextY += RowHeight * 0.95f;
		DrawLabel(HCJShield::FacingName(Towards), FVector2D(TextX, TextY), bDown ? ReadWarningColor : HCJShield::StrengthColor(Fraction), 1.1f, false, true);
		TextY += RowHeight * 1.15f;
		DrawLabel(bDown ? FString(TEXT("DOWN: hull open")) : FString::Printf(TEXT("%d %%"), FMath::RoundToInt32(FMath::Min(Fraction, 9.99f) * 100.0f)),
			FVector2D(TextX, TextY), bDown ? Faded(ReadWarningColor, 0.6f + 0.4f * FMath::Sin(static_cast<float>(GetWorld()->GetTimeSeconds()) * 9.0f)) : ReadTextColor, 1.0f, false, bDown);
	}
}

void AHCJHUD::DrawRadar(const AHCJShipCharacter& Ship, const AHCJGameState& GameState)
{
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	const UWorld* World = GetWorld();
	if (Universe == nullptr || !Universe->IsBuilt() || World == nullptr)
	{
		return;
	}
	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	const float Radius = 78.0f * TextScale;
	// Bottom left, on top of the shield / hull / boost block: the middle of the screen bottom is where the own ship
	// is in third person, and where the hints go.
	const float Margin = 24.0f * UIScale;
	const float CentreX = Margin + Radius;
	const float CentreY = Canvas->ClipY - Margin - static_cast<float>(GetVitalsPanelSize().Y) - 10.0f * UIScale - Radius;
	// The disc and the lines over it ("ENEMIES IN SECTOR", the way to the enemies) count as a panel for the world markers.
	NotePanel(Margin - 2.0f * UIScale, CentreY - Radius - 52.0f * TextScale, FMath::Max(2.0f * Radius, 250.0f * TextScale), 2.0f * Radius + 52.0f * TextScale + 10.0f * UIScale);

	// Disc from horizontal strips, rings and the forward line.
	const int32 NumStrips = FMath::Clamp(FMath::RoundToInt32(Radius / 2.0f), 8, 60);
	for (int32 Strip = 0; Strip < NumStrips; ++Strip)
	{
		const float Y0 = -Radius + 2.0f * Radius * Strip / NumStrips;
		const float Y1 = -Radius + 2.0f * Radius * (Strip + 1) / NumStrips;
		const float Mid = 0.5f * (Y0 + Y1);
		const float HalfWidth = FMath::Sqrt(FMath::Max(Radius * Radius - Mid * Mid, 0.0f));
		DrawRect(FLinearColor(0.0f, 0.03f, 0.06f, 0.55f), CentreX - HalfWidth, CentreY + Y0, 2.0f * HalfWidth, Y1 - Y0);
	}
	const FLinearColor GridColor(0.35f, 0.6f, 0.7f, 0.55f);
	DrawCircle(CentreX, CentreY, Radius, GridColor, FMath::Max(1.0f, 1.5f * UIScale), 40);
	DrawCircle(CentreX, CentreY, Radius * 0.5f, Faded(GridColor, 0.5f), 1.0f, 28);
	DrawLine(CentreX, CentreY - Radius, CentreX, CentreY + Radius, Faded(GridColor, 0.35f), 1.0f);
	DrawLine(CentreX - Radius, CentreY, CentreX + Radius, CentreY, Faded(GridColor, 0.35f), 1.0f);
	// The own ship: a small triangle pointing up.
	const float Me = 5.0f * TextScale;
	DrawLine(CentreX, CentreY - Me, CentreX - Me * 0.7f, CentreY + Me * 0.7f, ReadAccentColor, 2.0f);
	DrawLine(CentreX, CentreY - Me, CentreX + Me * 0.7f, CentreY + Me * 0.7f, ReadAccentColor, 2.0f);
	DrawLine(CentreX - Me * 0.7f, CentreY + Me * 0.7f, CentreX + Me * 0.7f, CentreY + Me * 0.7f, ReadAccentColor, 2.0f);

	const FRotationMatrix Axes(Ship.GetAimRotation());
	const FVector Forward = Axes.GetUnitAxis(EAxis::X);
	const FVector Right = Axes.GetUnitAxis(EAxis::Y);
	const FVector Up = Axes.GetUnitAxis(EAxis::Z);
	const FVector ShipLocation = Ship.GetActorLocation();
	const int32 Sector = Universe->GetSectorAtLocation(ShipLocation);

	const auto Place = [&](const FVector& Location, float& OutHeight)
	{
		const FVector Offset = Location - ShipLocation;
		const FVector2D Point = HCJUi::ProjectRadar(static_cast<float>(FVector::DotProduct(Offset, Forward)), static_cast<float>(FVector::DotProduct(Offset, Right)),
			static_cast<float>(FVector::DotProduct(Offset, Up)), RadarRange, OutHeight);
		return FVector2D(CentreX + Point.X * Radius, CentreY + Point.Y * Radius);
	};
	float Height = 0.0f;
	const float Mark = FMath::Max(3.0f, 4.0f * TextScale);

	// Ways out, the station and the hole of this sector.
	if (Sector != INDEX_NONE)
	{
		const int32 Dimension = Universe->GetGraph().GetDimension();
		for (int32 Dim = 0; Dim < Dimension; ++Dim)
		{
			if (Universe->HasPortal(Sector, Dim))
			{
				const FVector2D P = Place(Universe->GetPortalWorldTransform(Sector, Dim).GetLocation(), Height);
				const FLinearColor Color = Dim == GuideHop ? LeadColor : ReadAccentColor;
				const float S = Dim == GuideHop ? Mark * 1.5f : Mark;
				DrawLine(P.X - S, P.Y - S, P.X + S, P.Y - S, Color, 1.5f);
				DrawLine(P.X + S, P.Y - S, P.X + S, P.Y + S, Color, 1.5f);
				DrawLine(P.X + S, P.Y + S, P.X - S, P.Y + S, Color, 1.5f);
				DrawLine(P.X - S, P.Y + S, P.X - S, P.Y - S, Color, 1.5f);
			}
		}
		if (Universe->HasWormhole(Sector))
		{
			const FVector2D P = Place(Universe->GetWormholeLocation(Sector), Height);
			DrawCircle(P.X, P.Y, Mark * 1.2f, GuideHop == HCJ::WormholeHop ? LeadColor : FLinearColor(0.75f, 0.45f, 1.0f), 1.5f, 10);
		}
		if (Universe->HasStation(Sector))
		{
			const FVector2D P = Place(Universe->GetStationLocation(Sector), Height);
			DrawLine(P.X - Mark * 1.4f, P.Y, P.X + Mark * 1.4f, P.Y, FLinearColor::White, 2.0f);
			DrawLine(P.X, P.Y - Mark * 1.4f, P.X, P.Y + Mark * 1.4f, FLinearColor::White, 2.0f);
		}
		if (Universe->HasBlackHole(Sector))
		{
			const FVector2D P = Place(Universe->GetBlackHoleLocation(Sector), Height);
			DrawRect(FLinearColor::Black, P.X - Mark, P.Y - Mark, 2.0f * Mark, 2.0f * Mark);
			DrawCircle(P.X, P.Y, Mark * 1.6f, FLinearColor(0.9f, 0.6f, 1.0f), 1.5f, 12);
		}
	}

	// Pads that hold something: small diamonds in the colour of the item.
	for (const TWeakObjectPtr<AHCJPickup>& Entry : AHCJPickup::GetAll())
	{
		const AHCJPickup* Pickup = Entry.Get();
		if (Pickup == nullptr || Pickup->GetWorld() != World || !Pickup->IsActive() || FVector::DistSquared(Pickup->GetActorLocation(), ShipLocation) > FMath::Square(RadarRange))
		{
			continue;
		}
		const FVector2D P = Place(Pickup->GetActorLocation(), Height);
		const FLinearColor Color = Faded(HCJ::GetItemColor(Pickup->GetItem()), 0.9f);
		const float S = Mark * 0.8f;
		DrawLine(P.X - S, P.Y, P.X, P.Y - S, Color, 1.5f);
		DrawLine(P.X, P.Y - S, P.X + S, P.Y, Color, 1.5f);
		DrawLine(P.X + S, P.Y, P.X, P.Y + S, Color, 1.5f);
		DrawLine(P.X, P.Y + S, P.X - S, P.Y, Color, 1.5f);
	}

	// Enemies: red dots, a tick up or down for above / below the ship's plane. Never a cloaked one.
	const AHCJPlayerController* Controller = Cast<AHCJPlayerController>(PlayerOwner);
	const AHCJShipCharacter* Target = Controller != nullptr ? Controller->GetTarget() : nullptr;
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		const AHCJShipCharacter* Other = *It;
		if (Other == &Ship || Other->IsDead() || Other->IsCloaked() || FVector::DistSquared(Other->GetActorLocation(), ShipLocation) > FMath::Square(TargetMarkerDistance))
		{
			continue;
		}
		const FVector2D P = Place(Other->GetActorLocation(), Height);
		const FLinearColor Color = Other == Target ? LockColor : HostileColor;
		const float S = Other == Target ? Mark * 1.3f : Mark;
		if (FMath::Abs(Height) > 0.04f)
		{
			DrawLine(P.X, P.Y, P.X, P.Y - Height * Radius * 0.45f, Faded(Color, 0.8f), 1.5f);
		}
		DrawRect(Color, P.X - S, P.Y - S, 2.0f * S, 2.0f * S);
		if (Other == Target)
		{
			DrawCircle(P.X, P.Y, S * 2.1f, Color, 1.5f, 12);
		}
	}

	// Over the radar: how many enemies are here, or where the nearest one is.
	const float TextY = CentreY - Radius - static_cast<float>(MeasureLabel(TEXT("Ag"), 1.05f, true).Y) - 5.0f * UIScale;
	if (EnemiesInSector > 0)
	{
		DrawLabel(FString::Printf(TEXT("ENEMIES IN SECTOR: %d"), EnemiesInSector), FVector2D(Margin, TextY), HostileColor, 1.05f, false, true);
	}
	else if (GuideSector != INDEX_NONE && GuideHop != INDEX_NONE && Sector != INDEX_NONE)
	{
		const int32 Dimension = Universe->GetGraph().GetDimension();
		const FString Way = GuideHop == HCJ::WormholeHop ? FString(TEXT("the WORMHOLE"))
			: FString::Printf(TEXT("PORTAL > %s"), *FHCJHypercubeGraph::SectorToBinaryString(Sector ^ (1 << GuideHop), Dimension));
		// Two lines, left aligned: the radar sits in the corner now.
		const float LineHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), 1.05f, true).Y);
		DrawLabel(TEXT("SECTOR CLEAR"), FVector2D(Margin, TextY - LineHeight), ReadDimColor, 1.05f, false, true);
		DrawLabel(FString::Printf(TEXT("enemies in %s: take %s"), *FHCJHypercubeGraph::SectorToBinaryString(GuideSector, Dimension), *Way),
			FVector2D(Margin, TextY), LeadColor, 1.05f, false, true);
	}
	else
	{
		DrawLabel(TEXT("SECTOR CLEAR"), FVector2D(Margin, TextY), ReadDimColor, 1.05f, false, true);
	}
}

void AHCJHUD::DrawDamageFeedback(const AHCJShipCharacter& Ship, const AHCJPlayerController* Controller)
{
	const UWorld* World = GetWorld();
	if (!World || !PlayerOwner)
	{
		return;
	}
	const float UIScale = GetUIScale();
	const float Now = static_cast<float>(World->GetTimeSeconds());
	const float ClipX = Canvas->ClipX;
	const float ClipY = Canvas->ClipY;

	// Bands along the screen edges, strongest at the edge.
	const auto DrawVignette = [this, ClipX, ClipY, UIScale](const FLinearColor& Color, float Strength)
	{
		constexpr int32 NumBands = 6;
		const float Band = 16.0f * UIScale;
		for (int32 Index = 0; Index < NumBands; ++Index)
		{
			const float Alpha = Strength * FMath::Square(1.0f - static_cast<float>(Index) / NumBands);
			const float Inset = Index * Band;
			const FLinearColor BandColor(Color.R, Color.G, Color.B, Alpha);
			DrawRect(BandColor, Inset, Inset, ClipX - 2.0f * Inset, Band);
			DrawRect(BandColor, Inset, ClipY - Inset - Band, ClipX - 2.0f * Inset, Band);
			DrawRect(BandColor, Inset, Inset + Band, Band, ClipY - 2.0f * (Inset + Band));
			DrawRect(BandColor, ClipX - Inset - Band, Inset + Band, Band, ClipY - 2.0f * (Inset + Band));
		}
	};

	// Low hull: a slow red pulse that gets stronger as the hull goes.
	if (const UHCJHealthComponent* Health = Ship.GetHealthComponent())
	{
		const float Fraction = Health->GetHealthFraction();
		if (Fraction < 0.35f)
		{
			const float Pulse = 0.6f + 0.4f * FMath::Sin(Now * 5.0f);
			DrawVignette(FLinearColor(1.0f, 0.05f, 0.02f), 0.38f * Pulse * FMath::Clamp(1.0f - Fraction / 0.35f + 0.35f, 0.0f, 1.0f));
		}
	}

	if (Controller == nullptr)
	{
		return;
	}
	const float Age = Now - Controller->GetLastDamageTime();
	if (Age < 0.0f || Age > 1.4f)
	{
		return;
	}

	// The hit itself: a short flash at the edges.
	if (Age < 0.25f)
	{
		DrawVignette(FLinearColor(1.0f, 0.3f, 0.15f), 0.4f * (1.0f - Age / 0.25f) * FMath::Clamp(Controller->GetLastDamageAmount() / 20.0f, 0.4f, 1.0f));
	}

	// Where it came from: an arc on a ring around the crosshair, in view space.
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FRotationMatrix ViewAxes(ViewRotation);
	const FVector ToSource = Controller->GetLastDamageSource() - Ship.GetActorLocation();
	if (ToSource.SizeSquared() < FMath::Square(150.0))
	{
		return;
	}
	const float Right = static_cast<float>(FVector::DotProduct(ToSource, ViewAxes.GetUnitAxis(EAxis::Y)));
	const float Up = static_cast<float>(FVector::DotProduct(ToSource, ViewAxes.GetUnitAxis(EAxis::Z)));
	const float Forward = static_cast<float>(FVector::DotProduct(ToSource, ViewAxes.GetUnitAxis(EAxis::X)));
	// From dead ahead there is no screen direction: show it at the top; from behind, at the bottom.
	float Angle = 0.0f;
	const bool bAxial = FMath::Square(Right) + FMath::Square(Up) < FMath::Square(0.12f * FMath::Abs(Forward));
	HCJUi::ComputeEdgeMarker(bAxial ? 0.0f : Right, bAxial ? (Forward >= 0.0f ? 1.0f : -1.0f) : Up, FVector2D::ZeroVector, FVector2D(1.0, 1.0), Angle);

	const FLinearColor ArcColor = Faded(ReadWarningColor, FMath::Clamp(1.4f - Age, 0.0f, 1.0f));
	const float Radius = ClipY * 0.19f;
	const float HalfArc = 0.38f;
	constexpr int32 Segments = 10;
	const float CentreX = ClipX * 0.5f;
	const float CentreY = ClipY * 0.5f;
	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		const float LayerRadius = Radius + Layer * 5.0f * UIScale;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float A0 = Angle - HalfArc + 2.0f * HalfArc * Index / Segments;
			const float A1 = Angle - HalfArc + 2.0f * HalfArc * (Index + 1) / Segments;
			DrawLine(CentreX + LayerRadius * FMath::Cos(A0), CentreY + LayerRadius * FMath::Sin(A0),
				CentreX + LayerRadius * FMath::Cos(A1), CentreY + LayerRadius * FMath::Sin(A1), ArcColor, FMath::Max(2.0f, 3.0f * UIScale));
		}
	}
}

void AHCJHUD::DrawPortalLabels(const AHCJShipCharacter& Ship)
{
	EnemiesInSector = 0;
	GuideHop = INDEX_NONE;
	GuideSector = INDEX_NONE;

	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	const UWorld* World = GetWorld();
	if (!PlayerOwner || Universe == nullptr || !Universe->IsBuilt() || World == nullptr)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FRotationMatrix ViewAxes(ViewRotation);
	const FVector ViewDirection = ViewAxes.GetUnitAxis(EAxis::X);
	const FVector ShipLocation = Ship.GetActorLocation();
	const int32 Sector = Universe->GetSectorAtLocation(ShipLocation);
	const int32 Dimension = Universe->GetGraph().GetDimension();
	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	if (Sector == INDEX_NONE)
	{
		return;
	}

	// Enemies here (the ships this machine knows, never a cloaked one) ...
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		if (*It != &Ship && !It->IsDead() && !It->IsCloaked() && Universe->GetSectorAtLocation(It->GetActorLocation()) == Sector)
		{
			++EnemiesInSector;
		}
	}
	// ... or else the way to the nearest sector that holds one (from the replicated player states and the route table).
	if (EnemiesInSector == 0)
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			int32 BestDistance = MAX_int32;
			for (const APlayerState* PlayerState : GameState->PlayerArray)
			{
				const AHCJPlayerState* Other = Cast<AHCJPlayerState>(PlayerState);
				if (Other == nullptr || Other == PlayerOwner->PlayerState || Other->IsOnlyASpectator())
				{
					continue;
				}
				const int32 OtherSector = Other->GetCurrentSector();
				const int32 Distance = (OtherSector != INDEX_NONE && OtherSector != Sector) ? Universe->GetRoutes().Distance(Sector, OtherSector) : -1;
				if (Distance > 0 && Distance < BestDistance)
				{
					const int32 Hop = Universe->GetRoutes().NextHop(Sector, OtherSector);
					if (Hop != INDEX_NONE && Universe->HasHop(Sector, Hop))
					{
						BestDistance = Distance;
						GuideSector = OtherSector;
						GuideHop = Hop;
					}
				}
			}
		}
	}

	const auto DrawWay = [&](const FVector& Location, float LabelHeight, const FString& Name, const FLinearColor& BaseColor, bool bGuide)
	{
		const double Distance = FVector::Dist(Location, ShipLocation);
		if (!bGuide && Distance > PortalLabelDistance)
		{
			return;
		}
		const FVector Screen = Canvas->Project(Location + FVector(0.0, 0.0, LabelHeight), false);
		const bool bOnScreen = FVector::DotProduct(Location - ViewLocation, ViewDirection) > 0.0
			&& Screen.X >= 0.0 && Screen.Y >= 0.0 && Screen.X <= Canvas->ClipX && Screen.Y <= Canvas->ClipY;
		FLinearColor Color = bGuide ? LeadColor : BaseColor;
		Color.A = bGuide ? 1.0f : FMath::Clamp(1.3f - static_cast<float>(Distance) / PortalLabelDistance, 0.45f, 1.0f);
		const FString Label = bGuide ? FString::Printf(TEXT("%s   %d m   ENEMIES THIS WAY"), *Name, FMath::RoundToInt32(Distance / 100.0))
			: FString::Printf(TEXT("%s   %d m"), *Name, FMath::RoundToInt32(Distance / 100.0));
		if (bOnScreen)
		{
			const FVector2D Size = MeasureLabel(Label, 1.1f, bGuide);
			// Kept whole on the screen: a label centred on something at the edge would be cut in half.
			const double Half = Size.X * 0.5 + 6.0 * UIScale;
			const double LabelX = Canvas->ClipX > 2.0 * Half ? FMath::Clamp(Screen.X, Half, double(Canvas->ClipX) - Half) : Screen.X;
			DrawRect(FLinearColor(0.0f, 0.02f, 0.05f, 0.5f * Color.A), static_cast<float>(LabelX - Size.X * 0.5) - 4.0f * UIScale, static_cast<float>(Screen.Y) - 1.0f,
				static_cast<float>(Size.X) + 8.0f * UIScale, static_cast<float>(Size.Y) + 2.0f);
			DrawLabel(Label, FVector2D(LabelX, Screen.Y), Color, 1.1f, true, bGuide);
		}
		else if (bGuide)
		{
			// The way to the enemies is out of view: a chevron at the edge of the screen.
			const FVector ToWay = Location - ViewLocation;
			float Angle = 0.0f;
			const FVector2D Position = HCJUi::ComputeEdgeMarker(static_cast<float>(FVector::DotProduct(ToWay, ViewAxes.GetUnitAxis(EAxis::Y))),
				static_cast<float>(FVector::DotProduct(ToWay, ViewAxes.GetUnitAxis(EAxis::Z))), FVector2D(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f),
				FVector2D(Canvas->ClipX * 0.5f - 24.0f * UIScale - 330.0f * TextScale, Canvas->ClipY * 0.5f - 190.0f * TextScale), Angle);
			const FVector2D Forward(FMath::Cos(Angle), FMath::Sin(Angle));
			const FVector2D Side(-Forward.Y, Forward.X);
			const float Size = 16.0f * TextScale;
			for (int32 Chevron = 0; Chevron < 2; ++Chevron)
			{
				const FVector2D Tip = Position + Forward * (Size * (1.0 + 0.7 * Chevron));
				const FVector2D A = Tip - Forward * Size + Side * Size * 0.8;
				const FVector2D B = Tip - Forward * Size - Side * Size * 0.8;
				DrawLine(static_cast<float>(Tip.X), static_cast<float>(Tip.Y), static_cast<float>(A.X), static_cast<float>(A.Y), LeadColor, FMath::Max(2.0f, 3.0f * UIScale));
				DrawLine(static_cast<float>(Tip.X), static_cast<float>(Tip.Y), static_cast<float>(B.X), static_cast<float>(B.Y), LeadColor, FMath::Max(2.0f, 3.0f * UIScale));
			}
			const FVector2D LabelPosition = Position - Forward * (Size + 12.0f * TextScale);
			DrawLabel(Name, FVector2D(LabelPosition.X, LabelPosition.Y - 7.0f * TextScale), LeadColor, 1.0f, true, true);
		}
	};

	for (int32 Dim = 0; Dim < Dimension; ++Dim)
	{
		if (!Universe->HasPortal(Sector, Dim))
		{
			continue;
		}
		const int32 Dest = Sector ^ (1 << Dim);
		DrawWay(Universe->GetPortalWorldTransform(Sector, Dim).GetLocation(), 1100.0f,
			FString::Printf(TEXT("PORTAL > %s"), *FHCJHypercubeGraph::SectorToBinaryString(Dest, Dimension)),
			FMath::Lerp(Universe->GetSectorTint(Dest), FLinearColor::White, 0.6f), Dim == GuideHop);
	}
	if (GuideHop == HCJ::WormholeHop && Universe->HasWormhole(Sector))
	{
		DrawWay(Universe->GetWormholeLocation(Sector), 700.0f,
			FString::Printf(TEXT("WORMHOLE > %s"), *FHCJHypercubeGraph::SectorToBinaryString(Universe->GetWormholeDest(Sector), Dimension)), LeadColor, true);
	}
}

float AHCJHUD::DrawMapLegend(const FVector2D& TopLeft, float Width)
{
	const float UIScale = GetUIScale();
	const float RowHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), 0.85f).Y) + 3.0f * UIScale;
	const float Padding = 5.0f * UIScale;
	const float Height = 2.0f * Padding + 3.0f * RowHeight;
	DrawRect(FLinearColor(0.0f, 0.02f, 0.05f, 0.9f), static_cast<float>(TopLeft.X), static_cast<float>(TopLeft.Y), Width, Height);

	const float Icon = FMath::Max(5.0f, 7.0f * UIScale);
	const float Line = FMath::Max(1.0f, 1.5f * UIScale);
	const FLinearColor Violet(0.75f, 0.45f, 1.0f, 1.0f);
	// Two columns, three rows: the same marks the map draws.
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const float X = static_cast<float>(TopLeft.X) + Padding + (Index % 2) * Width * 0.5f;
		const float Y = static_cast<float>(TopLeft.Y) + Padding + (Index / 2) * RowHeight;
		const float CX = X + Icon;
		const float CY = Y + RowHeight * 0.5f - 1.0f * UIScale;
		const TCHAR* Text = TEXT("");
		switch (Index)
		{
		case 0:
			DrawRect(FLinearColor::White, CX - Icon * 0.8f, CY - Icon * 0.8f, Icon * 1.6f, Icon * 1.6f);
			DrawRect(FLinearColor::Black, CX - Icon * 0.55f, CY - Icon * 0.55f, Icon * 1.1f, Icon * 1.1f);
			DrawRect(ReadAccentColor, CX - Icon * 0.3f, CY - Icon * 0.3f, Icon * 0.6f, Icon * 0.6f);
			Text = TEXT("you are here");
			break;
		case 1:
			DrawLine(CX - Icon, CY, CX + Icon, CY, FLinearColor::White, Line);
			Text = TEXT("portal");
			break;
		case 2:
			DrawLine(CX - Icon, CY, CX - Icon * 0.3f, CY, Violet, Line * 1.5f);
			DrawLine(CX + Icon * 0.3f, CY, CX + Icon, CY, Violet, Line * 1.5f);
			Text = TEXT("wormhole");
			break;
		case 3:
			DrawRect(ReadDimColor, CX - Icon * 0.6f, CY - Icon * 0.6f, Icon * 1.2f, Icon * 1.2f);
			DrawRect(FLinearColor::Black, CX - Icon * 0.3f, CY - Icon * 0.3f, Icon * 0.6f, Icon * 0.6f);
			Text = TEXT("black hole");
			break;
		case 4:
			DrawLine(CX - Icon, CY, CX + Icon, CY, FLinearColor::White, Line);
			DrawLine(CX, CY - Icon, CX, CY + Icon, FLinearColor::White, Line);
			Text = TEXT("station (turrets)");
			break;
		default:
			DrawRect(ReadWarningColor, CX - Icon * 0.4f, CY - Icon * 0.4f, Icon * 0.8f, Icon * 0.8f);
			Text = TEXT("enemies");
			break;
		}
		DrawLabel(Text, FVector2D(X + 2.0f * Icon + 5.0f * UIScale, Y), ReadDimColor, 0.85f);
	}
	return Height;
}

void AHCJHUD::DrawCenterMessage(AHCJGameState& GameState, bool bDraw)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A new match: say what the game is about.
	const FHCJMatchConfig& Config = GameState.GetMatchConfig();
	if (Config.IsValid() && Config.MatchNumber != AnnouncedMatchNumber && GameState.GetMatchPhase() == EHCJMatchPhase::Playing)
	{
		AnnouncedMatchNumber = Config.MatchNumber;
		// A new universe: the sector of the last ship means nothing in it.
		BannerSector = INDEX_NONE;
		BannerShip.Reset();
		GameState.ShowCenterMessage(TEXT("MATCH START"), GameState.GetFragLimit() > 0
			? FString::Printf(TEXT("Free-for-all in a %dD hypercube  -  first to %d kills wins"), static_cast<int32>(Config.Dimension), GameState.GetFragLimit())
			: FString::Printf(TEXT("Free-for-all in a %dD hypercube"), static_cast<int32>(Config.Dimension)), ReadAccentColor);
	}

	const FHCJCenterMessage& Message = GameState.GetCenterMessage();
	const float Age = static_cast<float>(World->GetTimeSeconds()) - Message.LocalTime;
	constexpr float ShowSeconds = 3.0f;
	if (!bDraw || Age < 0.0f || Age >= ShowSeconds || Message.Text.IsEmpty() || GameState.GetMatchPhase() != EHCJMatchPhase::Playing)
	{
		return;
	}
	const float Alpha = FMath::Clamp((ShowSeconds - Age) / 0.6f, 0.0f, 1.0f);
	// Pops in a little larger.
	const float Scale = 2.1f + 0.5f * FMath::Clamp(1.0f - Age / 0.15f, 0.0f, 1.0f);
	const float CenterX = Canvas->ClipX * 0.5f;
	float Y = Canvas->ClipY * 0.30f;
	Y += DrawLabel(Message.Text, FVector2D(CenterX, Y), Faded(Message.Color, Alpha), Scale, true, true) + 4.0f * GetUIScale();
	if (!Message.SubText.IsEmpty())
	{
		DrawLabel(Message.SubText, FVector2D(CenterX, Y), Faded(ReadTextColor, Alpha), 1.3f, true);
	}
}

void AHCJHUD::DrawHelp(const AHCJGameState& GameState)
{
	const float UIScale = GetUIScale();
	const float TextScale = HCJUi::ComputeTextScale(UIScale);
	const float Padding = 16.0f * UIScale;
	const float Width = FMath::Min(1000.0f * TextScale, Canvas->ClipX - 48.0f * UIScale);
	const float ColumnWidth = (Width - 3.0f * Padding) * 0.5f;
	const float KeyColumn = 170.0f * TextScale;
	constexpr float RowScale = 1.0f;

	struct FRow
	{
		const TCHAR* Keys;
		const TCHAR* What;
	};
	static const FRow FlightRows[] = {
		{ TEXT("[Mouse]"), TEXT("steer: the ship flies where you look") },
		{ TEXT("[W] [S]"), TEXT("thrust forward / back") },
		{ TEXT("[A] [D]"), TEXT("strafe left / right") },
		{ TEXT("[Space] [Ctrl]"), TEXT("up / down   (also [E] [Q])") },
		{ TEXT("[Shift]"), TEXT("boost: drains the BOOST bar") },
		{ TEXT("[Left mouse]"), TEXT("fire: hold for the phaser beam") },
		{ TEXT("[Right mouse]"), TEXT("photon torpedo   (also [X])") },
		{ TEXT("[B]"), TEXT("phaser / blaster") },
		{ TEXT("[T]"), TEXT("next target") },
		{ TEXT("[F]"), TEXT("target the nearest enemy") },
		{ TEXT("[Middle mouse]"), TEXT("hold / release the target") },
		{ TEXT("[G]"), TEXT("drop a pickup weapon") },
	};
	static const FRow InterfaceRows[] = {
		{ TEXT("[Tab]"), TEXT("scoreboard (hold)") },
		{ TEXT("[V]"), TEXT("first / third person") },
		{ TEXT("[H]"), TEXT("hints on / off") },
		{ TEXT("[F1]"), TEXT("this page") },
		{ TEXT("[Esc]"), TEXT("menu, settings, quit") },
		{ TEXT("[R] [Enter]"), TEXT("respawn now, when destroyed") },
	};
	static const FRow LegendRows[] = {
		{ TEXT("red brackets"), TEXT("enemies; red arrows: out of view") },
		{ TEXT("orange diamond"), TEXT("your target") },
		{ TEXT("yellow circle"), TEXT("aim here; green: fire now") },
		{ TEXT("yellow marker"), TEXT("the way to the nearest enemies") },
	};

	const FString Goal1 = TEXT("Free-for-all. Every sector is a corner of the hypercube; portals lead to the neighbouring corners.");
	const FString Goal2 = GameState.GetFragLimit() > 0 ? FString::Printf(TEXT("First to %d kills wins."), GameState.GetFragLimit()) : FString(TEXT("Most kills when the time is up wins."));
	const FString Tip = TEXT("Glowing pads: repairs, power-ups, weapons. Stations shoot back. Keep away from black holes.");
	const FString ShieldTip = TEXT("Shields have four facings: fore, aft, port, starboard (the arcs around your ship, bottom left). A hit drains its side;");
	const FString ShieldTip2 = TEXT("a red side is down and lets hits through to the hull. Turn a fresh side to the enemy - and aim for his weak one.");

	const float RowHeight = static_cast<float>(DrawKeyMarkup(TEXT("[Ag]"), FVector2D::ZeroVector, ReadTextColor, RowScale, false, false).Y) + 4.0f * UIScale;
	const float LineHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), 1.05f).Y) + 3.0f * UIScale;
	const float TitleHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), 1.4f, true).Y) + 8.0f * UIScale;
	const float HeadHeight = static_cast<float>(MeasureLabel(TEXT("Ag"), 0.85f, true).Y) + 6.0f * UIScale;
	const int32 LeftRows = UE_ARRAY_COUNT(FlightRows);
	const float ColumnsHeight = HeadHeight + LeftRows * RowHeight;
	const FString TorpedoTip = TEXT("Photon torpedoes (TORPEDOES, bottom right): few, slow to reload, steering a little for your target. A full facing stops most of one;");
	const FString TorpedoTip2 = TEXT("through a facing that is down it nearly finishes a ship. INCOMING TORPEDO: boost across its path, or take it on a full side.");
	const FString PhaserTip = TEXT("Phaser (WEAPON, bottom right): a beam that hits at once and is strongest up close; it drains the phaser bank, and with the bank empty");
	const FString PhaserTip2 = TEXT("the trigger fires the blaster. Phasers and torpedoes also break rocks: big ones into pieces. A pickup weapon takes the place of the phaser.");
	const float Height = 2.0f * Padding + TitleHeight + 9.0f * LineHeight + 14.0f * UIScale + ColumnsHeight + HeadHeight + LineHeight;
	const float Left = (Canvas->ClipX - Width) * 0.5f;
	const float Top = FMath::Max((Canvas->ClipY - Height) * 0.5f, 8.0f * UIScale);

	DrawPanel(Left, Top, Width, Height, ReadAccentColor, 1.25f);

	float Y = Top + Padding;
	const float X = Left + Padding + 4.0f * UIScale;
	DrawLabel(TEXT("HOW TO PLAY"), FVector2D(X, Y), ReadAccentColor, 1.4f, false, true);
	const FVector2D CloseSize = DrawKeyMarkup(TEXT("[F1] closes this"), FVector2D::ZeroVector, ReadDimColor, 0.9f, false, false);
	DrawKeyMarkup(TEXT("[F1] closes this"), FVector2D(Left + Width - Padding - CloseSize.X, Y + 2.0f * UIScale), ReadDimColor, 0.9f, false);
	Y += TitleHeight;
	DrawLabel(Goal1, FVector2D(X, Y), ReadTextColor, 1.05f);
	Y += LineHeight;
	DrawLabel(Goal2, FVector2D(X, Y), FLinearColor(1.0f, 0.85f, 0.1f), 1.05f, false, true);
	Y += LineHeight + 4.0f * UIScale;
	DrawLabel(Tip, FVector2D(X, Y), ReadDimColor, 1.0f);
	Y += LineHeight;
	DrawLabel(ShieldTip, FVector2D(X, Y), ReadDimColor, 1.0f);
	Y += LineHeight;
	DrawLabel(ShieldTip2, FVector2D(X, Y), ReadDimColor, 1.0f);
	Y += LineHeight;
	DrawLabel(TorpedoTip, FVector2D(X, Y), ReadDimColor, 1.0f);
	Y += LineHeight;
	DrawLabel(TorpedoTip2, FVector2D(X, Y), ReadDimColor, 1.0f);
	Y += LineHeight;
	DrawLabel(PhaserTip, FVector2D(X, Y), ReadDimColor, 1.0f);
	Y += LineHeight;
	DrawLabel(PhaserTip2, FVector2D(X, Y), ReadDimColor, 1.0f);
	Y += LineHeight + 10.0f * UIScale;

	const auto DrawColumn = [&](float ColumnX, float ColumnY, const TCHAR* Title, TConstArrayView<FRow> Rows, bool bKeys)
	{
		DrawLabel(Title, FVector2D(ColumnX, ColumnY), ReadDimColor, 0.85f, false, true);
		DrawRect(Faded(ReadDimColor, 0.5f), ColumnX, ColumnY + HeadHeight - 4.0f * UIScale, ColumnWidth, FMath::Max(1.0f, UIScale));
		ColumnY += HeadHeight;
		for (const FRow& Row : Rows)
		{
			if (bKeys)
			{
				DrawKeyMarkup(Row.Keys, FVector2D(ColumnX, ColumnY), ReadAccentColor, RowScale, false);
			}
			else
			{
				DrawLabel(Row.Keys, FVector2D(ColumnX, ColumnY + 2.0f * UIScale), ReadAccentColor, RowScale);
			}
			DrawKeyMarkup(Row.What, FVector2D(ColumnX + KeyColumn, ColumnY), ReadTextColor, RowScale, false);
			ColumnY += RowHeight;
		}
		return ColumnY;
	};

	DrawColumn(X, Y, TEXT("FLIGHT AND COMBAT"), FlightRows, true);
	const float RightX = X + ColumnWidth + Padding;
	const float RightY = DrawColumn(RightX, Y, TEXT("INTERFACE"), InterfaceRows, true);
	// The legend fills the rest of the right column (it has four rows fewer than the left one).
	if (LeftRows - static_cast<int32>(UE_ARRAY_COUNT(InterfaceRows)) >= static_cast<int32>(UE_ARRAY_COUNT(LegendRows)))
	{
		// No separate head: the rows explain themselves.
		float LegendY = RightY;
		for (const FRow& Row : LegendRows)
		{
			DrawLabel(Row.Keys, FVector2D(RightX, LegendY + 2.0f * UIScale), FLinearColor(1.0f, 0.75f, 0.3f), RowScale);
			DrawLabel(Row.What, FVector2D(RightX + KeyColumn, LegendY + 2.0f * UIScale), ReadTextColor, RowScale);
			LegendY += RowHeight;
		}
	}
	Y += ColumnsHeight + 6.0f * UIScale;

	const bool bHints = UHCJUserSettings::Get().bHints;
	DrawKeyMarkup(bHints ? TEXT("In-game hints are ON.   [H] switches them off   (also under [Esc] > Settings)")
		: TEXT("In-game hints are OFF.   [H] switches them on   (also under [Esc] > Settings)"), FVector2D(X, Y), bHints ? ReadAccentColor : ReadDimColor, 0.95f, false);
}
