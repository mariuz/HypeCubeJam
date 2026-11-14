#include "Tests/HCJTestHelpers.h"

#include "Assets/HCJAssetSettings.h"
#include "World/HCJSpaceBackdrop.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Ship hull and paint are derived on every machine from the replicated player id / colour. The derivation must be
 * a pure, platform-independent function, or two players would see different ships for the same pilot.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJAssetShipLookTest, "HypeCubeJam.Assets.ShipLook", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHCJAssetShipLookTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	// ---- hull variant: recorded values (integer arithmetic of HCJ::MixSeed) ----
	const int32 PlayerIds[] = { -1, 0, 1, 2, 3, 256, 257, 258, 259, 260 };
	const int32 Expected40[] = { 11, 9, 36, 25, 16, 14, 26, 32, 38, 2 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PlayerIds); ++Index)
	{
		const int32 Variant = UHCJAssetSettings::PickShipVariant(PlayerIds[Index], 40);
		HCJ_TEST_CHECK(Variant == Expected40[Index], TEXT("ship variant of player %d among 40 is %d, expected %d"), PlayerIds[Index], Variant, Expected40[Index]);
	}

	for (int32 NumVariants = -1; NumVariants <= 40; ++NumVariants)
	{
		TArray<int32> Counts;
		Counts.SetNumZeroed(FMath::Max(NumVariants, 1));
		for (int32 PlayerId = 256; PlayerId < 256 + 2000; ++PlayerId)
		{
			const int32 Variant = UHCJAssetSettings::PickShipVariant(PlayerId, NumVariants);
			HCJ_TEST_CHECK(Counts.IsValidIndex(Variant), TEXT("ship variant %d of player %d is outside [0, %d)"), Variant, PlayerId, NumVariants);
			HCJ_TEST_CHECK(Variant == UHCJAssetSettings::PickShipVariant(PlayerId, NumVariants), TEXT("player %d got two different ship variants of %d"), PlayerId, NumVariants);
			if (Counts.IsValidIndex(Variant))
			{
				++Counts[Variant];
			}
		}
		// Consecutive player ids (what a match really has) spread over all hulls.
		for (int32 Variant = 0; Variant < NumVariants; ++Variant)
		{
			HCJ_TEST_CHECK(Counts[Variant] >= 2000 / (3 * NumVariants), TEXT("ship variant %d of %d was picked only %d times for 2000 players"), Variant, NumVariants, Counts[Variant]);
		}
	}

	// ---- paint: nearest colour ----
	const FLinearColor Skins[] = {
		FLinearColor(1.0f, 0.0f, 0.0f), FLinearColor(1.0f, 0.5f, 0.0f), FLinearColor(1.0f, 1.0f, 0.0f), FLinearColor(0.0f, 1.0f, 0.0f),
		FLinearColor(0.0f, 1.0f, 1.0f), FLinearColor(0.0f, 0.0f, 1.0f), FLinearColor(0.6f, 0.0f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f),
		FLinearColor(0.35f, 0.35f, 0.38f), FLinearColor(0.02f, 0.02f, 0.02f)
	};
	const TConstArrayView<FLinearColor> SkinView(Skins);

	HCJ_TEST_CHECK(UHCJAssetSettings::PickNearestSkin(FLinearColor::Red, TConstArrayView<FLinearColor>()) == INDEX_NONE, TEXT("an empty skin list gave skin %d"), UHCJAssetSettings::PickNearestSkin(FLinearColor::Red, TConstArrayView<FLinearColor>()));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Skins); ++Index)
	{
		HCJ_TEST_CHECK(UHCJAssetSettings::PickNearestSkin(Skins[Index], SkinView) == Index, TEXT("the colour of skin %d picked skin %d"), Index, UHCJAssetSettings::PickNearestSkin(Skins[Index], SkinView));
	}
	HCJ_TEST_CHECK(UHCJAssetSettings::PickNearestSkin(FLinearColor(1.0f, 0.0f, 1.0f), SkinView) == 6, TEXT("magenta picked skin %d, not purple"), UHCJAssetSettings::PickNearestSkin(FLinearColor(1.0f, 0.0f, 1.0f), SkinView));
	HCJ_TEST_CHECK(UHCJAssetSettings::PickNearestSkin(FLinearColor(0.9f, 0.1f, 0.05f), SkinView) == 0, TEXT("a dull red picked skin %d"), UHCJAssetSettings::PickNearestSkin(FLinearColor(0.9f, 0.1f, 0.05f), SkinView));
	// A tie goes to the first entry.
	const FLinearColor Twins[] = { FLinearColor::Green, FLinearColor::Green };
	HCJ_TEST_CHECK(UHCJAssetSettings::PickNearestSkin(FLinearColor::Green, TConstArrayView<FLinearColor>(Twins)) == 0, TEXT("a tie picked skin %d"), UHCJAssetSettings::PickNearestSkin(FLinearColor::Green, TConstArrayView<FLinearColor>(Twins)));

	// Player colours are fully saturated hues: they must spread over the chromatic paints and never land on white / grey / black.
	int32 SkinCounts[UE_ARRAY_COUNT(Skins)] = {};
	for (int32 HueStep = 0; HueStep < 360; ++HueStep)
	{
		const FLinearColor PlayerColor = FLinearColor(float(HueStep), 1.0f, 1.0f).HSVToLinearRGB();
		const int32 Skin = UHCJAssetSettings::PickNearestSkin(PlayerColor, SkinView);
		HCJ_TEST_CHECK(Skin >= 0 && Skin <= 6, TEXT("hue %d picked the colourless skin %d"), HueStep, Skin);
		if (Skin >= 0 && Skin < UE_ARRAY_COUNT(Skins))
		{
			++SkinCounts[Skin];
		}
	}
	for (int32 Skin = 0; Skin <= 6; ++Skin)
	{
		HCJ_TEST_CHECK(SkinCounts[Skin] >= 15, TEXT("skin %d covers only %d of 360 hues"), Skin, SkinCounts[Skin]);
	}

	HCJ_TEST_FINISH();
}

/**
 * The sky of a sector (bodies, nebulae) is derived on every machine from (match seed, sector id):
 * it must be deterministic, well formed, and independent of the options that only switch parts of it off.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJAssetSectorSkyTest, "HypeCubeJam.Assets.SectorSky", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHCJAssetSectorSkyTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;

	const int32 NumCatalogBodies = 9;
	const double MinCosine = FMath::Cos(FMath::DegreesToRadians(double(FHCJSectorSky::MinSeparationDegrees) - 0.1));
	int32 NumSectors = 0;
	int32 NumTwoBodies = 0;
	int32 BodyCounts[9] = {};

	for (int32 SeedIndex = 0; SeedIndex < 200; ++SeedIndex)
	{
		const int32 Seed = HCJTest::MakeSeed(SeedIndex);
		for (int32 Sector = 0; Sector < 16; ++Sector)
		{
			const FHCJSectorSky Sky = FHCJSectorSky::Make(Seed, Sector, NumCatalogBodies, 2, 3);
			const FHCJSectorSky Again = FHCJSectorSky::Make(Seed, Sector, NumCatalogBodies, 2, 3);
			++NumSectors;
			NumTwoBodies += Sky.Bodies.Num() == 2 ? 1 : 0;

			HCJ_TEST_CHECK(Sky.Bodies.Num() >= 1 && Sky.Bodies.Num() <= 2 && Sky.Nebulae.Num() == 3, TEXT("seed %d sector %d: %d bodies, %d nebulae"), Seed, Sector, Sky.Bodies.Num(), Sky.Nebulae.Num());
			HCJ_TEST_CHECK(Again.Bodies.Num() == Sky.Bodies.Num() && Again.Nebulae.Num() == Sky.Nebulae.Num(), TEXT("seed %d sector %d: two skies differ"), Seed, Sector);

			TArray<FVector, TInlineAllocator<4>> Objects;
			for (int32 Index = 0; Index < Sky.Bodies.Num(); ++Index)
			{
				const FHCJSectorSky::FBody& Body = Sky.Bodies[Index];
				HCJ_TEST_CHECK(Body.BodyIndex >= 0 && Body.BodyIndex < NumCatalogBodies, TEXT("seed %d sector %d: body index %d"), Seed, Sector, Body.BodyIndex);
				HCJ_TEST_CHECK(Body.Direction.IsNormalized() && Body.AngularDiameterDegrees >= 6.0f && Body.AngularDiameterDegrees <= 12.0f && Body.Phase >= 0.0f && Body.Phase < 1.0f,
					TEXT("seed %d sector %d: body %d is malformed (direction %s, %.1f degrees)"), Seed, Sector, Index, *Body.Direction.ToString(), Body.AngularDiameterDegrees);
				HCJ_TEST_CHECK(Index >= Again.Bodies.Num() || (Again.Bodies[Index].BodyIndex == Body.BodyIndex && Again.Bodies[Index].Direction == Body.Direction),
					TEXT("seed %d sector %d: body %d differs between two skies"), Seed, Sector, Index);
				if (Body.BodyIndex >= 0 && Body.BodyIndex < NumCatalogBodies)
				{
					++BodyCounts[Body.BodyIndex];
				}
				Objects.Add(Body.Direction);
			}
			HCJ_TEST_CHECK(Sky.Bodies.Num() < 2 || Sky.Bodies[0].BodyIndex != Sky.Bodies[1].BodyIndex, TEXT("seed %d sector %d: the same body twice (%d)"), Seed, Sector, Sky.Bodies[0].BodyIndex);

			for (int32 A = 0; A < Objects.Num(); ++A)
			{
				for (int32 B = A + 1; B < Objects.Num(); ++B)
				{
					HCJ_TEST_CHECK(FVector::DotProduct(Objects[A], Objects[B]) <= MinCosine, TEXT("seed %d sector %d: sky objects %d and %d overlap (cos %.3f)"), Seed, Sector, A, B, FVector::DotProduct(Objects[A], Objects[B]));
				}
			}

			for (const FHCJSectorSky::FNebula& Nebula : Sky.Nebulae)
			{
				HCJ_TEST_CHECK(Nebula.Direction.IsNormalized() && Nebula.AngularSizeDegrees >= 60.0f && Nebula.AngularSizeDegrees <= 100.0f
					&& Nebula.Seed >= 0.0f && Nebula.Seed < 100.0f && Nebula.Strength >= 0.6f && Nebula.Strength <= 1.0f && FMath::Abs(Nebula.HueShiftDegrees) <= 50.0f,
					TEXT("seed %d sector %d: malformed nebula (%.1f degrees, seed %.1f)"), Seed, Sector, Nebula.AngularSizeDegrees, Nebula.Seed);
			}

			// Switching parts off must not move what stays.
			const FHCJSectorSky Bare = FHCJSectorSky::Make(Seed, Sector, NumCatalogBodies, 1, 0);
			HCJ_TEST_CHECK(Bare.Bodies.Num() == 1 && Bare.Nebulae.Num() == 0
				&& Bare.Bodies[0].BodyIndex == Sky.Bodies[0].BodyIndex && Bare.Bodies[0].Direction == Sky.Bodies[0].Direction,
				TEXT("seed %d sector %d: the first body changes when the other parts are switched off"), Seed, Sector);
			const FHCJSectorSky Empty = FHCJSectorSky::Make(Seed, Sector, 0, 2, 9);
			HCJ_TEST_CHECK(Empty.Bodies.Num() == 0 && Empty.Nebulae.Num() == 4, TEXT("seed %d sector %d: empty catalogue gave %d bodies, %d nebulae"), Seed, Sector, Empty.Bodies.Num(), Empty.Nebulae.Num());
			const FHCJSectorSky Single = FHCJSectorSky::Make(Seed, Sector, 1, 2, 3);
			HCJ_TEST_CHECK(Single.Bodies.Num() == 1 && Single.Bodies[0].BodyIndex == 0, TEXT("seed %d sector %d: a one-body catalogue gave %d bodies"), Seed, Sector, Single.Bodies.Num());
		}
	}

	// Another sector or another match is another sky.
	const FHCJSectorSky SkyA = FHCJSectorSky::Make(1234, 0, NumCatalogBodies, 2, 3);
	const FHCJSectorSky SkyB = FHCJSectorSky::Make(1234, 1, NumCatalogBodies, 2, 3);
	const FHCJSectorSky SkyC = FHCJSectorSky::Make(1235, 0, NumCatalogBodies, 2, 3);
	HCJ_TEST_CHECK(!(SkyA.Bodies[0].Direction == SkyB.Bodies[0].Direction) && !(SkyA.Bodies[0].Direction == SkyC.Bodies[0].Direction), TEXT("sectors 0 and 1 or seeds 1234 and 1235 share a sky (%d)"), 0);

	// About half of the sectors have two bodies, every body shows up.
	const double TwoBodyShare = double(NumTwoBodies) / NumSectors;
	HCJ_TEST_CHECK(TwoBodyShare > 0.45 && TwoBodyShare < 0.55, TEXT("%.3f of the sectors have two bodies, expected about 0.5"), TwoBodyShare);
	for (int32 Index = 0; Index < NumCatalogBodies; ++Index)
	{
		HCJ_TEST_CHECK(BodyCounts[Index] > NumSectors / (3 * NumCatalogBodies), TEXT("body %d appears in only %d of %d sectors"), Index, BodyCounts[Index], NumSectors);
	}

	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
