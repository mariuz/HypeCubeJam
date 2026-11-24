#include "Game/HCJUiLogic.h"

#include "Game/HCJBalance.h"

#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Parse.h"

EHCJStreak FHCJStreakTracker::RegisterKill(float Time)
{
	MultiCount = (Time - LastKillTime <= MultiKillWindow) ? MultiCount + 1 : 1;
	LastKillTime = Time;
	++LifeStreak;

	if (LifeStreak % RampageEvery == 0)
	{
		return EHCJStreak::Rampage;
	}
	if (MultiCount >= 3)
	{
		return EHCJStreak::Triple;
	}
	return MultiCount == 2 ? EHCJStreak::Double : EHCJStreak::None;
}

void FHCJStreakTracker::RegisterDeath()
{
	LifeStreak = 0;
	MultiCount = 0;
	LastKillTime = -1000.0f;
}

float HCJUi::ComputeUIScale(float ClipX, float ClipY)
{
	if (ClipX <= 0.0f || ClipY <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(FMath::Min(ClipX / 1920.0f, ClipY / 1080.0f), 0.5f, 3.0f);
}

float HCJUi::ComputeTextScale(float UIScale)
{
	return FMath::Max(UIScale, 0.8f);
}

float HCJUi::ComputeSpreadRadius(float SpreadDegrees, float FovDegrees, float ClipX)
{
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(FovDegrees, 10.0f, 170.0f) * 0.5f);
	const float Spread = FMath::DegreesToRadians(FMath::Clamp(SpreadDegrees, 0.0f, 45.0f));
	return 0.5f * ClipX * FMath::Tan(Spread) / FMath::Tan(HalfFov);
}

FVector2D HCJUi::ComputeEdgeMarker(float RightComponent, float UpComponent, const FVector2D& Centre, const FVector2D& Radius, float& OutAngle)
{
	FVector2D Direction(RightComponent, -UpComponent);
	if (!Direction.Normalize())
	{
		Direction = FVector2D(0.0, -1.0);
	}
	OutAngle = FMath::Atan2(static_cast<float>(Direction.Y), static_cast<float>(Direction.X));
	return Centre + Direction * Radius;
}

bool HCJUi::IsMarkerClear(const FBox2D& Marker, TConstArrayView<FBox2D> Panels, float Margin)
{
	for (const FBox2D& Panel : Panels)
	{
		if (Marker.Max.X > Panel.Min.X - Margin && Marker.Min.X < Panel.Max.X + Margin
			&& Marker.Max.Y > Panel.Min.Y - Margin && Marker.Min.Y < Panel.Max.Y + Margin)
		{
			return false;
		}
	}
	return true;
}

namespace
{
	/** Hypercube map: how the cube of the first three bits is seen, and what each further bit adds. */
	constexpr double MapYaw = 0.4712;      // 27 degrees to the right
	constexpr double MapPitch = 0.4189;    // 24 degrees from above
	constexpr double MapPerspective = 0.22;
	constexpr int32 MapMaxDimension = 6;

	struct FMapShape
	{
		/** Size of the cube in the picture before it is fitted to the square. */
		double CubeScale = 1.0;
		/** Shift (x, y, depth) for the bits 3, 4 and 5 (plus when set, minus when clear). */
		FVector Offsets[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
	};

	FMapShape MakeMapShape(int32 Dimension)
	{
		FMapShape Shape;
		if (Dimension == 4)
		{
			Shape.CubeScale = 0.44;
			Shape.Offsets[0] = FVector(0.66, -0.12, 0.0);
		}
		else if (Dimension == 5)
		{
			Shape.CubeScale = 0.31;
			Shape.Offsets[0] = FVector(0.62, -0.07, 0.0);
			Shape.Offsets[1] = FVector(0.07, 0.62, 0.0);
		}
		else if (Dimension >= 6)
		{
			Shape.CubeScale = 0.26;
			Shape.Offsets[0] = FVector(0.62, -0.07, 0.0);
			Shape.Offsets[1] = FVector(0.07, 0.62, 0.0);
			Shape.Offsets[2] = FVector(0.20, 0.17, -0.35);
		}
		return Shape;
	}

	/** Not yet fitted to the square: x to the right, y down, z towards the viewer. */
	FVector ProjectMapRaw(int32 Sector, int32 Dimension, const FMapShape& Shape, double Yaw)
	{
		const double X = (Sector & 1) != 0 ? 1.0 : -1.0;
		const double Y = Dimension >= 2 ? ((Sector & 2) != 0 ? 1.0 : -1.0) : 0.0;
		const double Z = Dimension >= 3 ? ((Sector & 4) != 0 ? 1.0 : -1.0) : 0.0;
		const double CosYaw = FMath::Cos(Yaw);
		const double SinYaw = FMath::Sin(Yaw);
		const double CosPitch = FMath::Cos(MapPitch);
		const double SinPitch = FMath::Sin(MapPitch);
		const double X1 = X * CosYaw + Z * SinYaw;
		const double Z1 = -X * SinYaw + Z * CosYaw;
		const double Y2 = Y * CosPitch - Z1 * SinPitch;
		const double Z2 = (Y * SinPitch + Z1 * CosPitch) / UE_DOUBLE_SQRT_3;
		const double Zoom = Shape.CubeScale / (1.0 - MapPerspective * Z2);
		FVector Result(X1 * Zoom, -Y2 * Zoom, Z2);
		for (int32 Bit = 3; Bit < Dimension && Bit < MapMaxDimension; ++Bit)
		{
			Result += Shape.Offsets[Bit - 3] * ((Sector & (1 << Bit)) != 0 ? 1.0 : -1.0);
		}
		return Result;
	}

	/** What fits the picture of each dimension to the square and its depth to [0, 1]: worked out once, at rest. */
	struct FMapFit
	{
		FMapShape Shape;
		double ExtentX = 1.0;
		double ExtentY = 1.0;
		double MinDepth = -1.0;
		double MaxDepth = 1.0;
	};

	const FMapFit& GetMapFit(int32 Dimension)
	{
		struct FTable
		{
			FMapFit Fits[MapMaxDimension + 1];
			FTable()
			{
				for (int32 Dim = 1; Dim <= MapMaxDimension; ++Dim)
				{
					FMapFit& Fit = Fits[Dim];
					Fit.Shape = MakeMapShape(Dim);
					double MaxX = 0.0;
					double MaxY = 0.0;
					double MinZ = TNumericLimits<double>::Max();
					double MaxZ = -TNumericLimits<double>::Max();
					for (int32 Sector = 0; Sector < (1 << Dim); ++Sector)
					{
						const FVector Raw = ProjectMapRaw(Sector, Dim, Fit.Shape, MapYaw);
						MaxX = FMath::Max(MaxX, FMath::Abs(Raw.X));
						MaxY = FMath::Max(MaxY, FMath::Abs(Raw.Y));
						MinZ = FMath::Min(MinZ, Raw.Z);
						MaxZ = FMath::Max(MaxZ, Raw.Z);
					}
					Fit.ExtentX = FMath::Max(MaxX, 1.0e-6);
					// A line (one dimension) or a flat square has no height or depth to fit.
					Fit.ExtentY = MaxY > 1.0e-6 ? MaxY : 1.0;
					Fit.MinDepth = MinZ;
					Fit.MaxDepth = MaxZ > MinZ + 1.0e-6 ? MaxZ : MinZ + 1.0;
				}
			}
		};
		static const FTable Table;
		return Table.Fits[FMath::Clamp(Dimension, 1, MapMaxDimension)];
	}

	double OverlapArea(const FBox2D& A, const FBox2D& B)
	{
		const double Width = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
		const double Height = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
		return (Width > 0.0 && Height > 0.0) ? Width * Height : 0.0;
	}
}

FVector HCJUi::ProjectMapSector(int32 Sector, int32 Dimension, float SwayRad)
{
	if (Dimension <= 0 || Sector < 0)
	{
		return FVector(0.0, 0.0, 0.5);
	}
	const FMapFit& Fit = GetMapFit(Dimension);
	const FVector Raw = ProjectMapRaw(Sector, FMath::Min(Dimension, MapMaxDimension), Fit.Shape, MapYaw + SwayRad);
	return FVector(Raw.X / Fit.ExtentX, Raw.Y / Fit.ExtentY, FMath::Clamp((Raw.Z - Fit.MinDepth) / (Fit.MaxDepth - Fit.MinDepth), 0.0, 1.0));
}

float HCJUi::PlaceMapLabels(TArrayView<FHCJMapLabel> Labels, const FBox2D& Bounds, TConstArrayView<FVector2D> Dots, float DotHalfSize, const FVector2D& AwayFrom, TConstArrayView<FBox2D> Obstacles)
{
	// Below, above, right, left, then the four diagonals; each once close to the mark and once a line farther out.
	static const FVector2D Directions[8] = { FVector2D(0.0, 1.0), FVector2D(0.0, -1.0), FVector2D(1.0, 0.0), FVector2D(-1.0, 0.0),
		FVector2D(1.0, 1.0), FVector2D(-1.0, 1.0), FVector2D(1.0, -1.0), FVector2D(-1.0, -1.0) };
	constexpr int32 NumRounds = 3;
	const auto BoxOf = [](const FVector2D& Center, const FVector2D& Size) { return FBox2D(Center - Size * 0.5, Center + Size * 0.5); };
	const auto MarkOf = [](const FHCJMapLabel& Label) { return FBox2D(Label.Anchor - FVector2D(Label.AnchorRadius), Label.Anchor + FVector2D(Label.AnchorRadius)); };

	for (int32 Round = 0; Round < NumRounds; ++Round)
	{
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			FHCJMapLabel& Label = Labels[Index];
			FVector2D Away = Label.Anchor - AwayFrom;
			const bool bHasAway = Away.Normalize();
			double BestCost = TNumericLimits<double>::Max();
			FVector2D BestCenter = Label.Anchor;
			for (int32 Candidate = 0; Candidate < 16; ++Candidate)
			{
				const FVector2D& Direction = Directions[Candidate % 8];
				const int32 Ring = Candidate / 8;
				const double Gap = 1.0 + Ring * Label.Size.Y * 0.9;
				const bool bDiagonal = Direction.X != 0.0 && Direction.Y != 0.0;
				const FVector2D Center = bDiagonal
					? Label.Anchor + FVector2D(Direction.X * (Label.AnchorRadius * 0.7 + Label.Size.X * 0.5 + Gap * 0.7), Direction.Y * (Label.AnchorRadius * 0.7 + Label.Size.Y * 0.5 + Gap * 0.7))
					: Label.Anchor + FVector2D(Direction.X * (Label.AnchorRadius + Label.Size.X * 0.5 + Gap), Direction.Y * (Label.AnchorRadius + Label.Size.Y * 0.5 + Gap));
				const FBox2D Box = BoxOf(Center, Label.Size);
				// A small price for every step down the list, so that equal places are settled the same way every frame;
				// a larger one for the far places: a label away from its mark is only better than two square pixels of overlap.
				double Cost = 0.02 * Candidate + 2.0 * Ring;
				for (int32 Other = 0; Other < Labels.Num(); ++Other)
				{
					Cost += OverlapArea(Box, MarkOf(Labels[Other]));
					// In the first round the labels after this one have no place yet.
					if (Other != Index && (Round > 0 || Other < Index))
					{
						Cost += OverlapArea(Box, BoxOf(Labels[Other].Center, Labels[Other].Size));
					}
				}
				for (const FBox2D& Obstacle : Obstacles)
				{
					Cost += OverlapArea(Box, Obstacle);
				}
				for (const FVector2D& Dot : Dots)
				{
					Cost += 0.25 * OverlapArea(Box, FBox2D(Dot - FVector2D(DotHalfSize), Dot + FVector2D(DotHalfSize)));
				}
				Cost += 40.0 * (FMath::Max(0.0, Bounds.Min.X - Box.Min.X) + FMath::Max(0.0, Box.Max.X - Bounds.Max.X)
					+ FMath::Max(0.0, Bounds.Min.Y - Box.Min.Y) + FMath::Max(0.0, Box.Max.Y - Bounds.Max.Y));
				if (bHasAway)
				{
					// 0 on the far side of the mark, 3 on the side the line from the own sector arrives at.
					Cost += 1.5 * (1.0 - FVector2D::DotProduct(Direction.GetSafeNormal(), Away));
				}
				if (Cost < BestCost)
				{
					BestCost = Cost;
					BestCenter = Center;
				}
			}
			Label.Center = BestCenter;
		}
	}

	double Left = 0.0;
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		const FBox2D Box = BoxOf(Labels[Index].Center, Labels[Index].Size);
		for (int32 Other = 0; Other < Labels.Num(); ++Other)
		{
			Left += OverlapArea(Box, MarkOf(Labels[Other]));
			if (Other > Index)
			{
				Left += OverlapArea(Box, BoxOf(Labels[Other].Center, Labels[Other].Size));
			}
		}
		for (const FBox2D& Obstacle : Obstacles)
		{
			Left += OverlapArea(Box, Obstacle);
		}
	}
	return static_cast<float>(Left);
}

bool HCJUi::WantsStartMenu(const FString& CommandLine, const FString& ProjectName)
{
	const TCHAR* Stream = *CommandLine;
	FString Token;
	while (FParse::Token(Stream, Token, /*bUseEscape*/ false))
	{
		if (Token.StartsWith(TEXT("-")))
		{
			const FString Switch = Token.Mid(1);
			if (Switch.Equals(TEXT("AutoHost"), ESearchCase::IgnoreCase) || Switch.Equals(TEXT("server"), ESearchCase::IgnoreCase)
				|| Switch.Equals(TEXT("nullrhi"), ESearchCase::IgnoreCase) || Switch.StartsWith(TEXT("HCJCapture="), ESearchCase::IgnoreCase)
				|| Switch.StartsWith(TEXT("HCJFrameDump="), ESearchCase::IgnoreCase) || Switch.StartsWith(TEXT("ExecCmds="), ESearchCase::IgnoreCase))
			{
				return false;
			}
			continue;
		}
		if (Token.IsEmpty() || Token.Equals(ProjectName, ESearchCase::IgnoreCase) || Token.EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		// A map URL or a server address.
		return false;
	}
	return true;
}

float HCJUi::ScoreTarget(const FHCJTargetCandidate& Candidate, bool bIsCurrent)
{
	const float Sticky = bIsCurrent ? TargetStickinessDeg : 0.0f;
	if (Candidate.bVisible && Candidate.AngleDeg <= TargetConeDeg)
	{
		return Candidate.AngleDeg + Candidate.Distance / 6000.0f - Sticky;
	}
	// Behind everything in the cone: nearest first.
	return 1000.0f + Candidate.Distance / 100.0f - Sticky * 10.0f;
}

int32 HCJUi::PickTarget(TConstArrayView<FHCJTargetCandidate> Candidates, int32 CurrentIndex)
{
	int32 Best = INDEX_NONE;
	float BestScore = 0.0f;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const float Score = ScoreTarget(Candidates[Index], Index == CurrentIndex);
		if (Best == INDEX_NONE || Score < BestScore)
		{
			Best = Index;
			BestScore = Score;
		}
	}
	return Best;
}

bool HCJUi::IsOnLead(float AimErrorDeg, float Distance, float HitRadius)
{
	if (Distance <= HitRadius)
	{
		return true;
	}
	return FMath::Abs(AimErrorDeg) <= FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(HitRadius / Distance, 0.0f, 1.0f)));
}

FVector2D HCJUi::ProjectRadar(float Forward, float Right, float Up, float Range, float& OutHeight)
{
	const float SafeRange = FMath::Max(Range, 1.0f);
	FVector2D Point(Right / SafeRange, -Forward / SafeRange);
	const double Length = Point.Size();
	if (Length > 1.0)
	{
		Point /= Length;
	}
	OutHeight = FMath::Clamp(Up / SafeRange, -1.0f, 1.0f);
	return Point;
}

bool HCJUi::IsAimingAt(const FVector& OtherForward, const FVector& OtherToOwn, float MaxDistance, float ConeDeg)
{
	const double DistSq = OtherToOwn.SizeSquared();
	if (DistSq < 1.0 || DistSq > FMath::Square(static_cast<double>(MaxDistance)))
	{
		return false;
	}
	return FVector::DotProduct(OtherForward.GetSafeNormal(), OtherToOwn / FMath::Sqrt(DistSq)) >= FMath::Cos(FMath::DegreesToRadians(static_cast<double>(ConeDeg)));
}

EHCJHitSide HCJUi::ClassifyHitSide(float BearingDeg)
{
	const float Cosine = FMath::Cos(FMath::DegreesToRadians(BearingDeg));
	return Cosine < HCJBalance::RearArcDot ? EHCJHitSide::Behind : Cosine < HCJBalance::SideArcDot ? EHCJHitSide::Side : EHCJHitSide::Ahead;
}

namespace
{
	bool GMenuRequested = false;
	bool GPlayed = false;
	bool GFailure = false;
	FString GNotice;
}

void HCJMenuFlow::RequestMenu(const FString& Notice)
{
	GMenuRequested = true;
	if (!Notice.IsEmpty())
	{
		GNotice = Notice;
	}
}

bool HCJMenuFlow::ShouldOpenMenu()
{
	if (IsRunningDedicatedServer() || IsRunningCommandlet() || !FApp::CanEverRender())
	{
		return false;
	}
	if (GMenuRequested)
	{
		return true;
	}
	if (!GPlayed && FParse::Param(FCommandLine::Get(), TEXT("HCJMenu")))
	{
		// Development: the title menu although the command line is a capture run.
		return true;
	}
	return !GPlayed && HCJUi::WantsStartMenu(FCommandLine::Get(), FApp::GetProjectName());
}

void HCJMenuFlow::MarkPlayed()
{
	GPlayed = true;
	GMenuRequested = false;
}

FString HCJMenuFlow::TakeNotice()
{
	FString Result = MoveTemp(GNotice);
	GNotice.Reset();
	return Result;
}

bool HCJMenuFlow::TakeFailure()
{
	const bool bResult = GFailure;
	GFailure = false;
	return bResult;
}

void HCJMenuFlow::NotifyFailure(const FString& Notice)
{
	GFailure = true;
	RequestMenu(Notice);
}

int32 HCJUi::InsertNearest(TArrayView<FHCJNearSlot> Slots, int32 Num, double DistSq, int32 Index)
{
	const int32 Capacity = Slots.Num();
	Num = FMath::Clamp(Num, 0, Capacity);
	if (Capacity == 0 || (Num == Capacity && DistSq >= Slots[Num - 1].DistSq))
	{
		return Num;
	}
	// Where it goes: behind every entry that is as near or nearer.
	int32 At = Num < Capacity ? Num : Num - 1;
	while (At > 0 && Slots[At - 1].DistSq > DistSq)
	{
		Slots[At] = Slots[At - 1];
		--At;
	}
	Slots[At].DistSq = DistSq;
	Slots[At].Index = Index;
	return FMath::Min(Num + 1, Capacity);
}

float HCJUi::PickupMarkerStrength(float Distance, float NearDistance, float FarDistance, bool bEmphasis)
{
	if (bEmphasis || Distance <= NearDistance)
	{
		return 1.0f;
	}
	if (FarDistance <= NearDistance)
	{
		return PickupMarkerFarStrength;
	}
	return FMath::Lerp(1.0f, PickupMarkerFarStrength, FMath::Clamp((Distance - NearDistance) / (FarDistance - NearDistance), 0.0f, 1.0f));
}

int32 HCJUi::MarkerMetres(float Distance)
{
	const int32 Metres = FMath::Max(FMath::RoundToInt32(Distance / 100.0f), 0);
	return Metres <= 50 ? Metres : 5 * FMath::RoundToInt32(Metres / 5.0f);
}

int32 HCJUi::GuidePadRank(EHCJPickupItem Item, bool bLowHull, bool bWantWeapon)
{
	if (bLowHull)
	{
		return Item == EHCJPickupItem::Repair ? 2 : Item == EHCJPickupItem::ShieldCell ? 1 : 0;
	}
	return (bWantWeapon && HCJ::ItemToWeapon(Item) != EHCJWeapon::Count) ? 1 : 0;
}
