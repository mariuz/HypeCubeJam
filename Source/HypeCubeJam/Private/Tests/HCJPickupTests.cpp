#include "Tests/HCJTestHelpers.h"

#include "Game/HCJGameplaySettings.h"
#include "Ship/HCJWeapons.h"
#include "World/HCJBlackHole.h"
#include "World/HCJPickup.h"
#include "World/HCJPickupPads.h"
#include "World/HCJSectorLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJPickupTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/**
 * Pickup pads are gameplay objects placed on every machine from (match seed, sector layout, black hole, replicated
 * settings): deterministic, the configured number per sector, clear of rocks, portals, spawn points, the strong pull
 * of a black hole and of each other, and they leave the layout untouched.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPickupPadPlacementTest, "HypeCubeJam.Pickups.PadPlacement", HCJPickupTestFlags)

bool FHCJPickupPadPlacementTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const double Tolerance = HCJTest::DistanceTolerance;

	FHCJBlackHoleSettings Holes;
	Holes.bEnabled = true;
	Holes.SectorFraction = 0.5f;
	const double HoleClearance = FHCJBlackHole::GetClearance(Holes);

	FHCJPickupSettings Settings;
	Settings.bEnabled = true;
	Settings.MinPads = 3;
	Settings.MaxPads = 5;

	int32 NumSectors = 0;
	int32 NumPads = 0;
	int32 NumShort = 0;
	int32 CountSeen[9] = {};
	for (int32 Dim = FHCJHypercubeGraph::MinDimension; Dim <= FHCJHypercubeGraph::MaxDimension; ++Dim)
	{
		for (int32 SeedIndex = 0; SeedIndex < 20; ++SeedIndex)
		{
			const int32 Seed = HCJTest::MakeSeed(SeedIndex);
			FHCJHypercubeGraph Graph;
			Graph.Build(Dim, Seed, (SeedIndex % 2) == 0 ? 0.5f : 1.0f);

			for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
			{
				const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(Seed, Sector, Graph);
				const FHCJBlackHole Hole = FHCJBlackHole::Make(Seed, Layout, Holes);
				const TArray<FVector> Pads = FHCJPickupPads::Make(Seed, Layout, Hole, Holes, Settings);
				const TArray<FVector> Again = FHCJPickupPads::Make(Seed, Layout, Hole, Holes, Settings);
				const int32 Wanted = FHCJPickupPads::GetWantedPadCount(Seed, Sector, Settings);
				++NumSectors;
				NumPads += Pads.Num();
				NumShort += Pads.Num() < Wanted ? 1 : 0;
				++CountSeen[FMath::Clamp(Wanted, 0, 8)];

				HCJ_TEST_CHECK(Pads == Again, TEXT("dim %d seed %d sector %d: two placements differ"), Dim, Seed, Sector);
				HCJ_TEST_CHECK(Wanted >= Settings.MinPads && Wanted <= Settings.MaxPads && Pads.Num() <= Wanted,
					TEXT("dim %d seed %d sector %d: %d pads, wanted %d"), Dim, Seed, Sector, Pads.Num(), Wanted);

				for (int32 Index = 0; Index < Pads.Num(); ++Index)
				{
					const FVector& Pad = Pads[Index];
					const double Distance = Pad.Size();
					HCJ_TEST_CHECK(Distance >= HCJ::PadMinDistance - Tolerance && Distance <= HCJ::PadMaxDistance + Tolerance,
						TEXT("dim %d seed %d sector %d: pad %.1f uu from the sector origin"), Dim, Seed, Sector, Distance);
					HCJ_TEST_CHECK(Layout.IsLocationClear(Pad, HCJ::PadRockClearance - float(Tolerance)),
						TEXT("dim %d seed %d sector %d: pad %d is too close to a rock"), Dim, Seed, Sector, Index);
					for (const FHCJPortalSpec& Portal : Layout.Portals)
					{
						const double Gap = FVector::Dist(Pad, Portal.LocalPosition);
						HCJ_TEST_CHECK(Gap >= HCJ::PortalClearRadius - Tolerance, TEXT("dim %d seed %d sector %d: pad %.1f uu from the portal of dimension %d"), Dim, Seed, Sector, Gap, Portal.Dim);
					}
					for (const FTransform& Spawn : Layout.SpawnPoints)
					{
						const double Gap = FVector::Dist(Pad, Spawn.GetLocation());
						HCJ_TEST_CHECK(Gap >= HCJ::PadSpawnClearance - Tolerance, TEXT("dim %d seed %d sector %d: pad %.1f uu from a spawn point"), Dim, Seed, Sector, Gap);
					}
					if (Hole.bPresent)
					{
						const double Gap = FVector::Dist(Pad, Hole.LocalPosition);
						HCJ_TEST_CHECK(Gap >= HoleClearance - Tolerance, TEXT("dim %d seed %d sector %d: pad %.1f uu from the black hole"), Dim, Seed, Sector, Gap);
					}
					for (int32 Other = 0; Other < Index; ++Other)
					{
						const double Gap = FVector::Dist(Pad, Pads[Other]);
						HCJ_TEST_CHECK(Gap >= HCJ::PadSeparation - Tolerance, TEXT("dim %d seed %d sector %d: pads %d and %d are %.1f uu apart"), Dim, Seed, Sector, Other, Index, Gap);
					}
				}

				// The layout and the hole are untouched by all this.
				HCJ_TEST_CHECK(Layout.ComputeChecksum() == FHCJSectorLayout::Generate(Seed, Sector, Graph).ComputeChecksum(), TEXT("dim %d seed %d sector %d: layout changed"), Dim, Seed, Sector);
			}
		}
	}

	// Nearly every sector gets all the pads it asked for, and every count of the range occurs.
	HCJ_TEST_CHECK(NumShort * 50 <= NumSectors, TEXT("%d of %d sectors got fewer pads than wanted"), NumShort, NumSectors);
	HCJ_TEST_CHECK(CountSeen[3] > 0 && CountSeen[4] > 0 && CountSeen[5] > 0, TEXT("pad counts seen: 3 x%d, 4 x%d, 5 x%d"), CountSeen[3], CountSeen[4], CountSeen[5]);
	HCJ_TEST_CHECK(NumPads >= 3 * NumSectors * 95 / 100, TEXT("%d pads in %d sectors"), NumPads, NumSectors);

	// Off or an invalid layout: no pads. Another seed: other pads.
	FHCJHypercubeGraph Graph;
	Graph.Build(4, 1234, 0.5f);
	int32 NumDifferent = 0;
	for (int32 Sector = 0; Sector < Graph.NumSectors(); ++Sector)
	{
		const FHCJSectorLayout Layout = FHCJSectorLayout::Generate(1234, Sector, Graph);
		const FHCJBlackHole Hole = FHCJBlackHole::Make(1234, Layout, Holes);
		FHCJPickupSettings Off = Settings;
		Off.bEnabled = false;
		HCJ_TEST_CHECK(FHCJPickupPads::Make(1234, Layout, Hole, Holes, Off).Num() == 0, TEXT("sector %d: pads although pickups are off"), Sector);
		NumDifferent += FHCJPickupPads::Make(1234, Layout, Hole, Holes, Settings) != FHCJPickupPads::Make(4321, Layout, Hole, Holes, Settings) ? 1 : 0;
	}
	HCJ_TEST_CHECK(NumDifferent >= Graph.NumSectors() - 1, TEXT("only %d of %d sectors have other pads for another seed"), NumDifferent, Graph.NumSectors());
	HCJ_TEST_CHECK(FHCJPickupPads::Make(1234, FHCJSectorLayout(), FHCJBlackHole(), Holes, Settings).Num() == 0, TEXT("an invalid layout got %d pads"), 1);

	HCJ_TEST_FINISH();
}

/**
 * Weapon table and ammunition logic: every built-in and configured definition is usable, only the blaster is
 * unlimited, a pickup weapon counts down and falls back to the blaster, and the item roll covers the whole table.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJWeaponRulesTest, "HypeCubeJam.Pickups.Weapons", HCJPickupTestFlags)

bool FHCJWeaponRulesTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();

	for (int32 Index = 0; Index < static_cast<int32>(EHCJWeapon::Count); ++Index)
	{
		const EHCJWeapon Weapon = static_cast<EHCJWeapon>(Index);
		for (const FHCJWeaponDef& Def : { FHCJWeaponDef::MakeDefault(Weapon), Gameplay.GetWeapon(Weapon) })
		{
			HCJ_TEST_CHECK(Def.IsSane(), TEXT("weapon %d (%s) is not usable"), Index, *Def.Name);
			HCJ_TEST_CHECK((Def.Ammo == 0) == HCJ::IsBaseWeapon(Weapon), TEXT("weapon %d (%s) has ammo %d"), Index, *Def.Name, Def.Ammo);
			HCJ_TEST_CHECK(Def.bBeam == (Weapon == EHCJWeapon::Phaser), TEXT("weapon %d (%s): beam %d"), Index, *Def.Name, Def.bBeam ? 1 : 0);
			// Reaches a target at fighting distance, and one shot never kills a fresh ship (150 points) outright.
			HCJ_TEST_CHECK(Def.GetRange() >= 5000.0f, TEXT("weapon %d (%s) only reaches %.0f uu"), Index, *Def.Name, Def.GetRange());
			HCJ_TEST_CHECK(Def.Damage * Def.Pellets < 150.0f, TEXT("weapon %d (%s) does %.0f damage per shot"), Index, *Def.Name, Def.Damage * Def.Pellets);
			HCJ_TEST_CHECK((Def.HomingTurnRate > 0.0f) == (Def.HomingConeDegrees > 0.0f), TEXT("weapon %d (%s): homing rate %.0f with cone %.0f"), Index, *Def.Name, Def.HomingTurnRate, Def.HomingConeDegrees);
		}
	}

	// A broken definition is refused.
	FHCJWeaponDef Broken;
	Broken.FireInterval = 0.0f;
	HCJ_TEST_CHECK(!Broken.IsSane(), TEXT("a weapon with fire interval %.2f passed"), Broken.FireInterval);
	Broken = FHCJWeaponDef();
	Broken.Pellets = 0;
	HCJ_TEST_CHECK(!Broken.IsSane(), TEXT("a weapon with %d pellets passed"), Broken.Pellets);

	// Ammunition: give, count down, fall back; the same weapon again tops up to at most two pickups.
	const FHCJWeaponDef Blaster = FHCJWeaponDef::MakeDefault(EHCJWeapon::Blaster);
	const FHCJWeaponDef Rail = FHCJWeaponDef::MakeDefault(EHCJWeapon::Rail);
	const FHCJWeaponDef Pulse = FHCJWeaponDef::MakeDefault(EHCJWeapon::Pulse);
	FHCJWeaponState State;
	for (int32 Shot = 0; Shot < 100; ++Shot)
	{
		HCJ_TEST_CHECK(!State.ConsumeShot(Blaster) && State.Weapon == EHCJWeapon::Blaster, TEXT("the blaster ran dry after %d shots"), Shot);
	}
	State.Give(EHCJWeapon::Rail, Rail);
	HCJ_TEST_CHECK(State.Weapon == EHCJWeapon::Rail && State.Ammo == Rail.Ammo, TEXT("rail pickup gave weapon %d with %d shots"), static_cast<int32>(State.Weapon), State.Ammo);
	for (int32 Shot = 1; Shot <= Rail.Ammo; ++Shot)
	{
		const bool bReverted = State.ConsumeShot(Rail);
		HCJ_TEST_CHECK(bReverted == (Shot == Rail.Ammo), TEXT("rail shot %d of %d: reverted %d"), Shot, Rail.Ammo, bReverted ? 1 : 0);
	}
	HCJ_TEST_CHECK(State.Weapon == EHCJWeapon::Blaster && State.Ammo == 0, TEXT("after the last rail shot: weapon %d, ammo %d"), static_cast<int32>(State.Weapon), State.Ammo);

	State.Give(EHCJWeapon::Pulse, Pulse);
	State.Give(EHCJWeapon::Pulse, Pulse);
	State.Give(EHCJWeapon::Pulse, Pulse);
	HCJ_TEST_CHECK(State.Weapon == EHCJWeapon::Pulse && State.Ammo == 2 * Pulse.Ammo, TEXT("three pulse pickups gave %d shots"), State.Ammo);
	State.Give(EHCJWeapon::Rail, Rail);
	HCJ_TEST_CHECK(State.Weapon == EHCJWeapon::Rail && State.Ammo == Rail.Ammo, TEXT("switching to the rail kept %d shots"), State.Ammo);
	State.Drop();
	HCJ_TEST_CHECK(State.Weapon == EHCJWeapon::Blaster && State.Ammo == 0, TEXT("drop left weapon %d"), static_cast<int32>(State.Weapon));

	// Item roll: every item with a weight can come up, nothing else, and the ends of the range are safe.
	int32 Seen[static_cast<int32>(EHCJPickupItem::Count)] = {};
	for (int32 Step = 0; Step <= 1000; ++Step)
	{
		const EHCJPickupItem Item = Gameplay.PickItem(Step / 1000.0f);
		HCJ_TEST_CHECK(Item < EHCJPickupItem::Count && Gameplay.GetItemWeight(Item) > 0.0f, TEXT("roll %d gave item %d"), Step, static_cast<int32>(Item));
		if (Item < EHCJPickupItem::Count)
		{
			++Seen[static_cast<int32>(Item)];
		}
	}
	for (int32 Index = 0; Index < static_cast<int32>(EHCJPickupItem::Count); ++Index)
	{
		const EHCJPickupItem Item = static_cast<EHCJPickupItem>(Index);
		HCJ_TEST_CHECK((Seen[Index] > 0) == (Gameplay.GetItemWeight(Item) > 0.0f), TEXT("item %s came up %d times"), HCJ::GetItemName(Item), Seen[Index]);
		HCJ_TEST_CHECK((HCJ::ItemToWeapon(Item) != EHCJWeapon::Count) == (Index >= static_cast<int32>(EHCJPickupItem::Scatter) && Index <= static_cast<int32>(EHCJPickupItem::Pulse)), TEXT("item %s maps to weapon %d"), HCJ::GetItemName(Item), static_cast<int32>(HCJ::ItemToWeapon(Item)));
	}

	HCJ_TEST_FINISH();
}

/**
 * Pads that can be seen (item 38): every item has a kind - health, weapon, power-up - with a colour family of its own
 * and a short name for its marker; the light column and the halo keep a minimum size on the screen, between limits.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPickupVisibilityTest, "HypeCubeJam.Pickups.Visibility", HCJPickupTestFlags)

bool FHCJPickupVisibilityTest::RunTest(const FString& Parameters)
{
	// Kinds.
	TestTrue(TEXT("repair and shield cell are health"), HCJ::GetItemKind(EHCJPickupItem::Repair) == EHCJItemKind::Health && HCJ::GetItemKind(EHCJPickupItem::ShieldCell) == EHCJItemKind::Health);
	TestTrue(TEXT("overdrive, quad damage and cloak are power-ups"), HCJ::GetItemKind(EHCJPickupItem::Overdrive) == EHCJItemKind::PowerUp
		&& HCJ::GetItemKind(EHCJPickupItem::QuadDamage) == EHCJItemKind::PowerUp && HCJ::GetItemKind(EHCJPickupItem::Cloak) == EHCJItemKind::PowerUp);
	int32 NumByKind[static_cast<int32>(EHCJItemKind::Count)] = {};
	for (int32 Index = 0; Index < static_cast<int32>(EHCJPickupItem::Count); ++Index)
	{
		const EHCJPickupItem Item = static_cast<EHCJPickupItem>(Index);
		const EHCJItemKind Kind = HCJ::GetItemKind(Item);
		TestTrue(TEXT("a kind in range"), Kind < EHCJItemKind::Count);
		++NumByKind[FMath::Min(static_cast<int32>(Kind), static_cast<int32>(EHCJItemKind::Count) - 1)];
		// Whatever arms the ship is a weapon: the four pickup weapons and the torpedo restock.
		TestEqual(*FString::Printf(TEXT("%s: weapon kind"), HCJ::GetItemName(Item)), Kind == EHCJItemKind::Weapon,
			HCJ::ItemToWeapon(Item) != EHCJWeapon::Count || Item == EHCJPickupItem::Torpedoes);
		const FString Short = HCJ::GetItemShortName(Item);
		TestTrue(*FString::Printf(TEXT("%s: a short name of at most 9 characters ('%s')"), HCJ::GetItemName(Item), *Short), Short.Len() >= 4 && Short.Len() <= 9 && Short != TEXT("?"));
		for (int32 Other = 0; Other < Index; ++Other)
		{
			TestTrue(TEXT("short names differ"), Short != HCJ::GetItemShortName(static_cast<EHCJPickupItem>(Other)));
		}
	}
	TestTrue(TEXT("2 health, 5 weapon, 3 power-up items"), NumByKind[0] == 2 && NumByKind[1] == 5 && NumByKind[2] == 3);

	// Colour families: saturated, bright, far from each other and from the red of an enemy.
	const FLinearColor Enemy(1.0f, 0.2f, 0.06f);
	for (int32 A = 0; A < static_cast<int32>(EHCJItemKind::Count); ++A)
	{
		const FLinearColor ColorA = HCJ::GetItemKindColor(static_cast<EHCJItemKind>(A));
		TestTrue(TEXT("a bright colour"), ColorA.GetMax() >= 0.99f && ColorA.GetMin() <= 0.3f);
		TestTrue(TEXT("not the red of an enemy"), FVector3f(ColorA.R - Enemy.R, ColorA.G - Enemy.G, ColorA.B - Enemy.B).Size() > 0.4f);
		for (int32 B = 0; B < A; ++B)
		{
			const FLinearColor ColorB = HCJ::GetItemKindColor(static_cast<EHCJItemKind>(B));
			TestTrue(TEXT("kinds are far apart in colour"), FVector3f(ColorA.R - ColorB.R, ColorA.G - ColorB.G, ColorA.B - ColorB.B).Size() > 0.7f);
		}
	}
	TestTrue(TEXT("health is green"), HCJ::GetItemKindColor(EHCJItemKind::Health).G > 0.9f && HCJ::GetItemKindColor(EHCJItemKind::Health).R < 0.4f);

	// The light column and the halo.
	using namespace HCJPickupLook;
	TestEqual(TEXT("column next to the view"), BeamWidthAt(0.0f), BeamWidth);
	TestEqual(TEXT("column at 80 m: still its own width"), BeamWidthAt(8000.0f), BeamWidth);
	TestEqual(TEXT("column at 250 m: a share of the distance"), BeamWidthAt(25000.0f), 25000.0f * BeamAngularWidth, 0.01f);
	TestEqual(TEXT("column far away: the limit"), BeamWidthAt(500000.0f), BeamMaxWidth);
	TestEqual(TEXT("a negative distance is no distance"), BeamWidthAt(-5.0f), BeamWidth);
	TestEqual(TEXT("halo next to the view"), HaloEdgeAt(0.0f), HaloEdge);
	TestEqual(TEXT("halo at 250 m"), HaloEdgeAt(25000.0f), 25000.0f * HaloAngularEdge, 0.01f);
	TestEqual(TEXT("halo far away: the limit"), HaloEdgeAt(500000.0f), HaloMaxEdge);
	float Last = 0.0f;
	for (float Distance = 0.0f; Distance <= MaxViewDistance; Distance += 500.0f)
	{
		// From the far end of a sector (520 m across) the column is at least 0.5 % of the distance wide: 3 pixels at 1280x720, 90 degrees.
		TestTrue(TEXT("the column never gets thinner with the distance"), BeamWidthAt(Distance) >= Last);
		Last = BeamWidthAt(Distance);
		if (Distance >= 12000.0f && Distance <= 52000.0f)
		{
			TestTrue(TEXT("at least 0.5 % of the distance across a sector"), BeamWidthAt(Distance) >= 0.005f * Distance);
			TestTrue(TEXT("the halo at least 6 % of the distance across a sector"), HaloEdgeAt(Distance) >= 0.06f * Distance);
		}
	}
	TestTrue(TEXT("the column is taller than it is ever wide"), BeamHeight > 5.0f * BeamMaxWidth);
	for (int32 Step = 0; Step < 200; ++Step)
	{
		const float Pulse = HaloPulse(Step * 0.05f, Step * 0.3f);
		TestTrue(TEXT("the pulse stays between 0.8 and 1"), Pulse >= 0.8f && Pulse <= 1.0f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
