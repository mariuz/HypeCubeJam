#include "Tests/HCJTestHelpers.h"

#include "AI/HCJBotController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/HCJGameMode.h"
#include "Game/HCJGameState.h"
#include "Game/HCJPlayerState.h"
#include "HAL/PlatformTime.h"
#include "HypeCubeJam.h"
#include "Ship/HCJShipCharacter.h"
#include "Tests/AutomationCommon.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJUniverse.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Playthrough tests inside the automation suite: a real game world with the real game mode, universe, ships and bot
 * controllers, ticked by hand at a fixed step (so a few minutes of match take a few seconds), and asserted on through
 * AHCJGameMode::GetPlaythroughSnapshot. No network, no local player, no rendering. The longer scenarios (every weapon,
 * host + clients, the packaged build) are docs/tools/playthrough.py, which runs whole processes and reads their logs.
 */
namespace
{
	constexpr EAutomationTestFlags HCJPlaythroughTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** The engine's test world with the game's URL options (bots, seed, frag limit, ...) passed to the game mode. */
	struct FHCJTestMatch : FTestWorldWrapper
	{
		FString Options;
		/** Game seconds and frames ticked so far. */
		float GameSeconds = 0.0f;
		int32 Frames = 0;

		virtual bool BeginPlayInTestWorld() override
		{
			if (TestWorld == nullptr || TestWorld->HasBegunPlay() || TestWorld->GetGameInstance() == nullptr)
			{
				ReportFailure(TEXT("the test world is not ready for BeginPlay"));
				return false;
			}
			CachedFrameCounter = GFrameCounter;
			FURL URL;
			TArray<FString> Parts;
			Options.ParseIntoArray(Parts, TEXT("?"), true);
			for (const FString& Part : Parts)
			{
				URL.AddOption(*Part);
			}
			TestWorld->SetGameMode(URL);
			if (TestWorld->GetAuthGameMode<AHCJGameMode>() == nullptr)
			{
				ReportFailure(TEXT("the test world did not get the game's game mode"));
				return false;
			}
			TestWorld->InitializeActorsForPlay(URL);
			TestWorld->BeginPlay();
			if (TestWorld->GetGameState<AHCJGameState>() == nullptr)
			{
				ReportFailure(TEXT("the test world did not get the game's game state"));
				return false;
			}
			return true;
		}

		bool Start(const FString& InOptions)
		{
			Options = InOptions;
			return CreateTestWorld(EWorldType::Game) && BeginPlayInTestWorld();
		}

		AHCJGameMode* GetGameMode() const { return TestWorld != nullptr ? TestWorld->GetAuthGameMode<AHCJGameMode>() : nullptr; }

		/** Ticks until Done returns true or MaxGameSeconds of game have passed; returns whether Done did. */
		bool RunUntil(float MaxGameSeconds, TFunctionRef<bool()> Done, float Step = 1.0f / 30.0f)
		{
			const float End = GameSeconds + MaxGameSeconds;
			while (GameSeconds < End)
			{
				TickTestWorld(Step);
				GameSeconds += Step;
				++Frames;
				// (Not every frame: the condition may walk the actors.)
				if (Frames % 15 == 0 && Done())
				{
					return true;
				}
			}
			return Done();
		}
	};

	int32 CountLiveShips(const UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
		{
			Count += (IsValid(*It) && !It->IsDead()) ? 1 : 0;
		}
		return Count;
	}
}

/**
 * A whole match and the start of the next: six hard bots to a frag limit. The match must end by the limit with a winner
 * and a full board, the next one must start by itself on another seed and another layout, and nothing may get stuck,
 * leave the sector alive or end up at a broken position on the way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPlaythroughMatchTest, "HypeCubeJam.Playthrough.MatchToFragLimitAndNext", HCJPlaythroughTestFlags)

bool FHCJPlaythroughMatchTest::RunTest(const FString& Parameters)
{
	const double WallStart = FPlatformTime::Seconds();
	constexpr int32 FragLimit = 4;
	FHCJTestMatch Match;
	if (!Match.Start(FString::Printf(TEXT("Seed=1234?Bots=6?Difficulty=2?FragLimit=%d?TimeLimit=0"), FragLimit)))
	{
		Match.ForwardErrorMessages(this);
		Match.DestroyTestWorld(true);
		return false;
	}
	AHCJGameMode* GameMode = Match.GetGameMode();
	const AHCJGameMode::FPlaythroughSnapshot First = GameMode->GetPlaythroughSnapshot();
	TestEqual(TEXT("the first match is match 1"), First.MatchNumber, 1);
	TestEqual(TEXT("with the seed of the URL"), First.Seed, 1234);
	TestTrue(TEXT("its universe is built"), First.LayoutChecksum != 0);

	// The bots get their ships a moment after the start.
	TestTrue(TEXT("six bots have ships within 10 s"), Match.RunUntil(10.0f, [&]() { return CountLiveShips(Match.GetTestWorld()) >= 6; }));

	// Somebody reaches the frag limit: 600 s of game at most (a match of this size takes a minute or two).
	AHCJGameMode::FPlaythroughSnapshot BeforeEnd = First;
	const bool bEnded = Match.RunUntil(600.0f, [&]()
	{
		const AHCJGameMode::FPlaythroughSnapshot Now = GameMode->GetPlaythroughSnapshot();
		if (Now.MatchesEnded == 0)
		{
			BeforeEnd = Now;
		}
		return Now.MatchesEnded >= 1;
	});
	TestTrue(TEXT("the match ends within 600 s of game"), bEnded);
	const float EndSeconds = Match.GameSeconds;
	const AHCJGameMode::FPlaythroughSnapshot AtEnd = GameMode->GetPlaythroughSnapshot();
	TestFalse(TEXT("it has a winner"), AtEnd.LastWinner.IsEmpty());
	TestTrue(TEXT("the winner reached the frag limit"), AtEnd.LastWinnerKills >= FragLimit);
	TestEqual(TEXT("the board lists every bot"), AtEnd.LastBoardRows, 6);
	TestTrue(TEXT("shots were fired"), BeforeEnd.Shots > 0 || BeforeEnd.Kills > 0);
	TestTrue(TEXT("kills were counted"), BeforeEnd.Kills >= FragLimit - 1);
	if (const AHCJGameState* GameState = Match.GetTestWorld()->GetGameState<AHCJGameState>())
	{
		TestTrue(TEXT("the game state is in the intermission"), GameState->GetMatchPhase() == EHCJMatchPhase::Intermission);
		const AHCJPlayerState* Winner = GameState->GetMatchWinner();
		TestTrue(TEXT("and publishes the winner"), Winner != nullptr && Winner->GetPlayerName() == AtEnd.LastWinner);
	}
	TestEqual(TEXT("no ship is left during the intermission"), CountLiveShips(Match.GetTestWorld()), 0);

	// The next match starts by itself: another seed, another layout, new ships.
	const bool bNext = Match.RunUntil(40.0f, [&]() { return GameMode->GetPlaythroughSnapshot().MatchNumber >= 2 && CountLiveShips(Match.GetTestWorld()) >= 6; });
	TestTrue(TEXT("the second match starts and the bots fly again within 40 s"), bNext);
	const AHCJGameMode::FPlaythroughSnapshot Second = GameMode->GetPlaythroughSnapshot();
	TestEqual(TEXT("it is match 2"), Second.MatchNumber, 2);
	TestTrue(TEXT("on a new seed"), Second.Seed != First.Seed);
	TestTrue(TEXT("with another layout checksum"), Second.LayoutChecksum != 0 && Second.LayoutChecksum != First.LayoutChecksum);
	TestTrue(TEXT("the score starts again"), Second.Kills <= 2);

	// A little of the second match, then the watch of the whole run.
	Match.RunUntil(20.0f, []() { return false; });
	const AHCJGameMode::FPlaythroughSnapshot Last = GameMode->GetPlaythroughSnapshot();
	TestEqual(TEXT("no bot sat still for 20 s (first match)"), BeforeEnd.StuckEvents, 0);
	TestEqual(TEXT("no bot sat still for 20 s (second match)"), Last.StuckEvents, 0);
	TestEqual(TEXT("no ship alive outside its sector"), BeforeEnd.OutOfBoundsEvents + Last.OutOfBoundsEvents, 0);
	TestEqual(TEXT("no broken position"), BeforeEnd.NaNEvents + Last.NaNEvents, 0);

	const double Wall = FPlatformTime::Seconds() - WallStart;
	const FString Summary = FString::Printf(TEXT("match 1: %.0f s of game, winner %s with %d kills, %d kills in all, %d portal jumps; match 2 seed %d layout %08X (match 1: %08X); %d frames in %.1f s of real time"),
		EndSeconds, *AtEnd.LastWinner, AtEnd.LastWinnerKills, BeforeEnd.Kills, BeforeEnd.PortalJumps, Second.Seed, Second.LayoutChecksum, First.LayoutChecksum, Match.Frames, Wall);
	AddInfo(Summary);
	UE_LOG(LogHCJ, Display, TEXT("Playthrough test (match to the frag limit and the next): %s"), *Summary);
	TestTrue(TEXT("the whole playthrough takes less than 180 s of real time"), Wall < 180.0);

	Match.DestroyTestWorld(true);
	return true;
}

/**
 * Three minutes of an open match (no limits) with eight normal bots of every ship type: fights, travel and items must
 * all happen - kills and deaths, portal jumps, pickups taken - every ship type must have flown, every ship must stay
 * inside its sector, and none may get stuck.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJPlaythroughOpenMatchTest, "HypeCubeJam.Playthrough.OpenMatchKeepsMoving", HCJPlaythroughTestFlags)

bool FHCJPlaythroughOpenMatchTest::RunTest(const FString& Parameters)
{
	const double WallStart = FPlatformTime::Seconds();
	FHCJTestMatch Match;
	if (!Match.Start(TEXT("Seed=4321?Bots=8?Difficulty=1?FragLimit=0?TimeLimit=0")))
	{
		Match.ForwardErrorMessages(this);
		Match.DestroyTestWorld(true);
		return false;
	}
	AHCJGameMode* GameMode = Match.GetGameMode();
	const AHCJUniverse* Universe = GameMode->GetUniverse();
	TestNotNull(TEXT("the universe exists"), Universe);

	bool TypeSeen[HCJShipTypes::NumTypes] = {};
	int32 NumOutside = 0;
	int32 NumSamples = 0;
	Match.RunUntil(180.0f, [&]()
	{
		// Twice a second: every live ship is inside the radius at which ships are destroyed.
		for (TActorIterator<AHCJShipCharacter> It(Match.GetTestWorld()); It; ++It)
		{
			const AHCJShipCharacter* Ship = *It;
			if (!IsValid(Ship) || Ship->IsDead() || Universe == nullptr || !Universe->IsBuilt())
			{
				continue;
			}
			++NumSamples;
			const int32 Type = static_cast<int32>(Ship->GetShipType());
			if (Type >= 0 && Type < HCJShipTypes::NumTypes)
			{
				TypeSeen[Type] = true;
			}
			const FVector Location = Ship->GetActorLocation();
			const int32 Sector = Universe->GetSectorAtLocation(Location);
			if (Location.ContainsNaN() || Sector == INDEX_NONE || FVector::Dist(Location, Universe->GetSectorOrigin(Sector)) > HCJ::SectorKillRadius + 1500.0)
			{
				++NumOutside;
			}
		}
		return false;
	});

	const AHCJGameMode::FPlaythroughSnapshot End = GameMode->GetPlaythroughSnapshot();
	TestEqual(TEXT("still the first match"), End.MatchNumber, 1);
	TestTrue(TEXT("ships were sampled"), NumSamples > 1000);
	TestTrue(TEXT("at least ten kills in three minutes"), End.Kills >= 10);
	TestTrue(TEXT("deaths are at least the kills"), End.Deaths >= End.Kills);
	TestTrue(TEXT("bots travelled through portals"), End.PortalJumps >= 5);
	TestTrue(TEXT("items were taken from pads"), End.PickupsTaken >= 5);
	TestTrue(TEXT("shots were fired"), End.Shots > 50);
	for (int32 Type = 0; Type < HCJShipTypes::NumTypes; ++Type)
	{
		TestTrue(*FString::Printf(TEXT("a %s flew"), HCJShipTypes::Get(Type).Name), TypeSeen[Type]);
	}
	TestEqual(TEXT("no sample of a live ship outside its sector"), NumOutside, 0);
	TestEqual(TEXT("no bot sat still for 20 s"), End.StuckEvents, 0);
	TestEqual(TEXT("no ship alive outside its sector for seconds"), End.OutOfBoundsEvents, 0);
	TestEqual(TEXT("no broken position"), End.NaNEvents, 0);

	const double Wall = FPlatformTime::Seconds() - WallStart;
	const FString Summary = FString::Printf(TEXT("180 s of game: %d kills, %d deaths, %d portal jumps, %d wormhole jumps, %d pickups, %d shots; %d frames in %.1f s of real time"),
		End.Kills, End.Deaths, End.PortalJumps, End.WormholeJumps, End.PickupsTaken, End.Shots, Match.Frames, Wall);
	AddInfo(Summary);
	UE_LOG(LogHCJ, Display, TEXT("Playthrough test (open match): %s"), *Summary);
	TestTrue(TEXT("three minutes of match take less than 180 s of real time"), Wall < 180.0);

	Match.DestroyTestWorld(true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
