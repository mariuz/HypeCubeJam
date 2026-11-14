#include "Tests/HCJTestHelpers.h"

#include "Online/HCJEosLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJEosTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FHCJEosConfig MakeFullConfig()
	{
		FHCJEosConfig Config;
		Config.ProductId = TEXT("0123456789abcdef0123456789abcdef");
		Config.SandboxId = TEXT("fedcba9876543210fedcba9876543210");
		Config.DeploymentId = TEXT("00112233445566778899aabbccddeeff");
		Config.ClientId = TEXT("xyzClientId");
		Config.ClientSecret = TEXT("NotARealSecretJustATestValue");
		return Config;
	}

	FHCJLobbyInfo MakeLobby()
	{
		FHCJLobbyInfo Lobby;
		Lobby.HostName = TEXT("Ada");
		Lobby.PublicAddress = TEXT("203.0.113.7");
		Lobby.LanAddresses = { TEXT("192.168.1.20"), TEXT("10.0.0.5") };
		Lobby.Port = 7777;
		Lobby.Players = 2;
		Lobby.MaxPlayers = 16;
		Lobby.Bots = 6;
		Lobby.Dimension = 4;
		Lobby.Seed = -1234;
		Lobby.Version = TEXT("1A2B3C4D");
		return Lobby;
	}
}

/** Config validation and the "unavailable" fallback: an incomplete section never counts as usable and never leaks a value. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosConfigTest, "HypeCubeJam.Online.ConfigAndFallback", HCJEosTestFlags)

bool FHCJEosConfigTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("a full config is complete"), MakeFullConfig().IsComplete());
	TestFalse(TEXT("an empty config is not"), FHCJEosConfig().IsComplete());

	FHCJEosConfig NoSecret = MakeFullConfig();
	NoSecret.ClientSecret.Reset();
	TestFalse(TEXT("no secret, not complete"), NoSecret.IsComplete());
	TestEqual(TEXT("the problem names the key"), NoSecret.GetProblem(), FString(TEXT("ClientSecret is empty")));

	FHCJEosConfig Placeholder = MakeFullConfig();
	Placeholder.ClientSecret = TEXT("<paste here>");
	TestFalse(TEXT("a placeholder is not a secret"), Placeholder.IsComplete());
	TestFalse(TEXT("the problem never repeats the value"), Placeholder.GetProblem().Contains(TEXT("paste")));

	FHCJEosConfig NoDeployment = MakeFullConfig();
	NoDeployment.DeploymentId.Reset();
	TestTrue(TEXT("the first missing key is reported"), NoDeployment.GetProblem().StartsWith(TEXT("DeploymentId")));
	TestFalse(TEXT("no problem text contains the secret"), NoDeployment.GetProblem().Contains(NoDeployment.ClientSecret));

	// Which states hide the lobby list and the leaderboards.
	TestTrue(TEXT("disabled is unavailable"), HCJEos::IsUnavailable(EHCJEosState::Disabled));
	TestTrue(TEXT("not configured is unavailable"), HCJEos::IsUnavailable(EHCJEosState::NotConfigured));
	TestTrue(TEXT("no SDK is unavailable"), HCJEos::IsUnavailable(EHCJEosState::SdkUnavailable));
	TestTrue(TEXT("failed sign-in is unavailable"), HCJEos::IsUnavailable(EHCJEosState::Failed));
	TestFalse(TEXT("connecting shows the page"), HCJEos::IsUnavailable(EHCJEosState::Connecting));
	TestFalse(TEXT("ready shows the page"), HCJEos::IsUnavailable(EHCJEosState::Ready));
	for (const EHCJEosState State : { EHCJEosState::Disabled, EHCJEosState::NotConfigured, EHCJEosState::SdkUnavailable, EHCJEosState::Failed })
	{
		TestTrue(TEXT("every unavailable state says so"), HCJEos::DescribeState(State).StartsWith(TEXT("Online services unavailable")));
	}
	TestTrue(TEXT("the detail is appended"), HCJEos::DescribeState(EHCJEosState::NotConfigured, TEXT("ClientSecret is empty")).EndsWith(TEXT("(ClientSecret is empty)")));

	TestEqual(TEXT("plain names pass"), HCJEos::SanitizeDisplayName(TEXT("  Ada_1 ")), FString(TEXT("Ada_1")));
	TestEqual(TEXT("nothing left gives Pilot"), HCJEos::SanitizeDisplayName(TEXT("\t\n")), FString(TEXT("Pilot")));
	TestEqual(TEXT("names are cut to 32"), HCJEos::SanitizeDisplayName(FString::ChrN(50, TEXT('x'))).Len(), 32);
	return true;
}

/** A lobby survives packing into attributes and back; broken, foreign or hostile attribute sets are refused or cleaned. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosLobbyAttributesTest, "HypeCubeJam.Online.LobbyAttributes", HCJEosTestFlags)

bool FHCJEosLobbyAttributesTest::RunTest(const FString& Parameters)
{
	FHCJLobbyInfo Lobby = MakeLobby();
	Lobby.Sanitize();
	TArray<FHCJLobbyAttribute> Attributes = Lobby.ToAttributes();
	TestTrue(TEXT("the attribute count stays far below the service limit of 64"), Attributes.Num() <= 16);
	for (const FHCJLobbyAttribute& Attribute : Attributes)
	{
		TestTrue(TEXT("keys are upper case"), Attribute.Key == Attribute.Key.ToUpper());
		TestTrue(TEXT("no empty string values"), Attribute.bIsInt || !Attribute.StringValue.IsEmpty());
	}

	FHCJLobbyInfo Back;
	TestTrue(TEXT("round trip parses"), FHCJLobbyInfo::FromAttributes(Attributes, Back));
	TestTrue(TEXT("round trip keeps every value"), Back.AdvertisesSameAs(Lobby));
	TestEqual(TEXT("negative seeds survive"), Back.Seed, -1234);
	TestEqual(TEXT("map text"), Back.DescribeMap(), FString(TEXT("4D - 16 sectors")));

	// The service hands keys back in another case.
	TArray<FHCJLobbyAttribute> Lower = Attributes;
	for (FHCJLobbyAttribute& Attribute : Lower)
	{
		Attribute.Key = Attribute.Key.ToLower();
	}
	TestTrue(TEXT("keys are matched without case"), FHCJLobbyInfo::FromAttributes(Lower, Back));

	// No public address, no version: still a lobby, as long as there is a LAN address.
	FHCJLobbyInfo LanOnly = MakeLobby();
	LanOnly.PublicAddress.Reset();
	LanOnly.Version.Reset();
	LanOnly.Sanitize();
	TestTrue(TEXT("LAN-only lobby parses"), FHCJLobbyInfo::FromAttributes(LanOnly.ToAttributes(), Back));
	TestTrue(TEXT("empty public address stays empty"), Back.PublicAddress.IsEmpty());
	TestEqual(TEXT("both LAN addresses"), Back.LanAddresses.Num(), 2);

	// Every attribute is required.
	for (int32 Index = 0; Index < Attributes.Num(); ++Index)
	{
		TArray<FHCJLobbyAttribute> Missing = Attributes;
		Missing.RemoveAt(Index);
		TestFalse(*FString::Printf(TEXT("refused without %s"), *Attributes[Index].Key), FHCJLobbyInfo::FromAttributes(Missing, Back));
	}
	TestFalse(TEXT("an empty set is refused"), FHCJLobbyInfo::FromAttributes(TArray<FHCJLobbyAttribute>(), Back));

	// Wrong types and impossible values.
	const auto With = [&Attributes](const TCHAR* Key, const FHCJLobbyAttribute& Replacement)
	{
		TArray<FHCJLobbyAttribute> Result = Attributes;
		for (FHCJLobbyAttribute& Attribute : Result)
		{
			if (Attribute.Key == Key)
			{
				Attribute = Replacement;
			}
		}
		return Result;
	};
	TestFalse(TEXT("port as text is refused"), FHCJLobbyInfo::FromAttributes(With(TEXT("HCJPORT"), FHCJLobbyAttribute::MakeString(TEXT("HCJPORT"), TEXT("7777"))), Back));
	TestFalse(TEXT("port 0 is refused"), FHCJLobbyInfo::FromAttributes(With(TEXT("HCJPORT"), FHCJLobbyAttribute::MakeInt(TEXT("HCJPORT"), 0)), Back));
	TestFalse(TEXT("port 70000 is refused"), FHCJLobbyInfo::FromAttributes(With(TEXT("HCJPORT"), FHCJLobbyAttribute::MakeInt(TEXT("HCJPORT"), 70000)), Back));
	TestFalse(TEXT("dimension 9 is refused"), FHCJLobbyInfo::FromAttributes(With(TEXT("HCJDIM"), FHCJLobbyAttribute::MakeInt(TEXT("HCJDIM"), 9)), Back));
	TestFalse(TEXT("no room for anybody is refused"), FHCJLobbyInfo::FromAttributes(With(TEXT("HCJMAX"), FHCJLobbyAttribute::MakeInt(TEXT("HCJMAX"), 0)), Back));

	// A hostile host name or address must not get into a travel URL.
	TArray<FHCJLobbyAttribute> Hostile = With(TEXT("HCJPUB"), FHCJLobbyAttribute::MakeString(TEXT("HCJPUB"), TEXT("1.2.3.4?Name=x /Game/Evil")));
	Hostile = [&Hostile]()
	{
		for (FHCJLobbyAttribute& Attribute : Hostile)
		{
			if (Attribute.Key == TEXT("HCJLAN"))
			{
				Attribute.StringValue = TEXT("192.168.1.20,evil host?x,999.1.1.1,10.0.0.5");
			}
		}
		return Hostile;
	}();
	TestTrue(TEXT("a lobby with junk addresses still parses"), FHCJLobbyInfo::FromAttributes(Hostile, Back));
	TestTrue(TEXT("the junk public address is dropped"), Back.PublicAddress.IsEmpty());
	TestEqual(TEXT("only real IPv4 LAN addresses are kept"), Back.LanAddresses.Num(), 2);

	// Nothing usable to connect to: not listed.
	FHCJLobbyInfo NoAddress = MakeLobby();
	NoAddress.PublicAddress.Reset();
	NoAddress.LanAddresses.Reset();
	NoAddress.Sanitize();
	TestFalse(TEXT("a lobby without any address is refused"), FHCJLobbyInfo::FromAttributes(NoAddress.ToAttributes(), Back));

	// Sanitize clamps.
	FHCJLobbyInfo Wild = MakeLobby();
	Wild.Players = 99;
	Wild.MaxPlayers = 500;
	Wild.LanAddresses = { TEXT("10.0.0.1"), TEXT("10.0.0.2"), TEXT("10.0.0.3"), TEXT("10.0.0.4"), TEXT("10.0.0.5"), TEXT("10.0.0.6") };
	Wild.Sanitize();
	TestEqual(TEXT("max players is clamped to the lobby limit"), Wild.MaxPlayers, 64);
	TestEqual(TEXT("players never exceed max players"), Wild.Players, 64);
	TestEqual(TEXT("at most four LAN addresses"), Wild.LanAddresses.Num(), FHCJLobbyInfo::MaxLanAddresses);

	// What triggers an update.
	FHCJLobbyInfo Changed = Lobby;
	TestTrue(TEXT("same values, no update"), Changed.AdvertisesSameAs(Lobby));
	Changed.Players = 3;
	TestFalse(TEXT("a player count change is an update"), Changed.AdvertisesSameAs(Lobby));
	Changed = Lobby;
	Changed.LobbyId = TEXT("abc");
	TestTrue(TEXT("the lobby id is not advertised"), Changed.AdvertisesSameAs(Lobby));
	return true;
}

/** Which address a joiner travels to. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosJoinAddressTest, "HypeCubeJam.Online.JoinAddress", HCJEosTestFlags)

bool FHCJEosJoinAddressTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("IPv4"), HCJEos::IsIPv4(TEXT("192.168.1.20")));
	TestFalse(TEXT("three parts"), HCJEos::IsIPv4(TEXT("192.168.1")));
	TestFalse(TEXT("out of range"), HCJEos::IsIPv4(TEXT("192.168.1.256")));
	TestFalse(TEXT("letters"), HCJEos::IsIPv4(TEXT("a.b.c.d")));
	TestFalse(TEXT("empty part"), HCJEos::IsIPv4(TEXT("1..2.3")));
	TestFalse(TEXT("sign"), HCJEos::IsIPv4(TEXT("1.2.3.-4")));
	TestTrue(TEXT("host name"), HCJEos::IsSafeHost(TEXT("play.example.org")));
	TestFalse(TEXT("URL option"), HCJEos::IsSafeHost(TEXT("1.2.3.4?Name=x")));
	TestFalse(TEXT("blank"), HCJEos::IsSafeHost(TEXT("1.2.3.4 5")));
	TestFalse(TEXT("path"), HCJEos::IsSafeHost(TEXT("1.2.3.4/Game/Map")));
	TestFalse(TEXT("empty"), HCJEos::IsSafeHost(TEXT("")));

	const FHCJLobbyInfo Lobby = MakeLobby();
	TestEqual(TEXT("the host is this machine: loopback"), HCJEos::PickJoinAddress(Lobby, { TEXT("10.0.0.5") }), FString(TEXT("127.0.0.1:7777")));
	TestEqual(TEXT("same /24: the LAN address"), HCJEos::PickJoinAddress(Lobby, { TEXT("172.16.0.9"), TEXT("192.168.1.77") }), FString(TEXT("192.168.1.20:7777")));
	TestEqual(TEXT("another network: the public address"), HCJEos::PickJoinAddress(Lobby, { TEXT("192.168.50.3") }), FString(TEXT("203.0.113.7:7777")));
	TestEqual(TEXT("no local address known: the public address"), HCJEos::PickJoinAddress(Lobby, {}), FString(TEXT("203.0.113.7:7777")));

	FHCJLobbyInfo LanOnly = Lobby;
	LanOnly.PublicAddress.Reset();
	LanOnly.Port = 7778;
	TestEqual(TEXT("no public address: the first LAN address, with the port"), HCJEos::PickJoinAddress(LanOnly, { TEXT("192.168.50.3") }), FString(TEXT("192.168.1.20:7778")));

	FHCJLobbyInfo Nothing = Lobby;
	Nothing.PublicAddress.Reset();
	Nothing.LanAddresses.Reset();
	TestTrue(TEXT("nothing to connect to"), HCJEos::PickJoinAddress(Nothing, { TEXT("192.168.1.77") }).IsEmpty());

	FHCJLobbyInfo Junk = Lobby;
	Junk.PublicAddress = TEXT("evil?x");
	Junk.LanAddresses = { TEXT("not an address") };
	TestTrue(TEXT("junk gives no address"), HCJEos::PickJoinAddress(Junk, { TEXT("192.168.1.77") }).IsEmpty());
	return true;
}

/** Stat bookkeeping: deltas, resets between matches, when a match and a win count, what a bystander reports. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosStatTrackerTest, "HypeCubeJam.Online.StatTracker", HCJEosTestFlags)

bool FHCJEosStatTrackerTest::RunTest(const FString& Parameters)
{
	const auto Sample = [](int32 Kills, int32 Deaths, int32 Turrets, bool bParticipating, bool bIntermission, bool bWon)
	{
		FHCJStatSample Result;
		Result.Kills = Kills;
		Result.Deaths = Deaths;
		Result.Turrets = Turrets;
		Result.bParticipating = bParticipating;
		Result.bIntermission = bIntermission;
		Result.bLocalWon = bWon;
		return Result;
	};

	FHCJStatTracker Tracker;
	TestFalse(TEXT("nothing to send at the start"), Tracker.Observe(Sample(0, 0, 0, true, false, false)));
	Tracker.Observe(Sample(1, 0, 0, true, false, false));
	Tracker.Observe(Sample(3, 1, 1, true, false, false));
	TestFalse(TEXT("same values add nothing"), Tracker.Observe(Sample(3, 1, 1, true, false, false)));
	TestEqual(TEXT("kills so far"), Tracker.GetPending().Kills, 3);
	TestEqual(TEXT("no match before it ends"), Tracker.GetPending().Matches, 0);

	TestTrue(TEXT("the end of the match is the moment to send"), Tracker.Observe(Sample(4, 1, 1, true, true, true)));
	TestFalse(TEXT("only once per match"), Tracker.Observe(Sample(4, 1, 1, true, true, true)));
	FHCJStatDelta Delta = Tracker.Take();
	TestEqual(TEXT("kills"), Delta.Kills, 4);
	TestEqual(TEXT("deaths"), Delta.Deaths, 1);
	TestEqual(TEXT("turrets"), Delta.Turrets, 1);
	TestEqual(TEXT("one match"), Delta.Matches, 1);
	TestEqual(TEXT("one win"), Delta.Wins, 1);
	TestTrue(TEXT("taking empties"), Tracker.GetPending().IsEmpty());

	// Next match: the server resets the counters; nothing is counted twice, nothing goes negative.
	Tracker.Observe(Sample(0, 0, 0, true, false, false));
	Tracker.Observe(Sample(2, 3, 0, true, false, false));
	TestTrue(TEXT("second match ends"), Tracker.Observe(Sample(2, 3, 0, true, true, false)));
	Delta = Tracker.Take();
	TestEqual(TEXT("kills of the second match only"), Delta.Kills, 2);
	TestEqual(TEXT("deaths of the second match only"), Delta.Deaths, 3);
	TestEqual(TEXT("a lost match is a match"), Delta.Matches, 1);
	TestEqual(TEXT("but no win"), Delta.Wins, 0);

	// Somebody who only watched (title menu) reports no match, even when the board comes up.
	FHCJStatTracker Watcher;
	Watcher.Observe(Sample(0, 0, 0, false, false, false));
	TestFalse(TEXT("a bystander sends nothing"), Watcher.Observe(Sample(0, 0, 0, false, true, false)));
	TestTrue(TEXT("and has nothing"), Watcher.GetPending().IsEmpty());

	// The host played, went back to the title menu and watches the match end: no match, no win, but its kills.
	FHCJStatTracker Leaver;
	Leaver.Observe(Sample(0, 0, 0, true, false, false));
	Leaver.Observe(Sample(6, 1, 0, true, false, false));
	Leaver.Observe(Sample(6, 1, 0, false, false, false));
	TestFalse(TEXT("left to the menu before the end"), Leaver.Observe(Sample(6, 1, 0, false, true, true)));
	TestEqual(TEXT("kills stay"), Leaver.GetPending().Kills, 6);
	TestEqual(TEXT("no match"), Leaver.GetPending().Matches, 0);
	TestEqual(TEXT("no win"), Leaver.GetPending().Wins, 0);

	// Joining while the board is up: that match was not played.
	FHCJStatTracker Late;
	TestFalse(TEXT("joined during intermission"), Late.Observe(Sample(0, 0, 0, true, true, false)));
	Late.Observe(Sample(0, 0, 0, true, false, false));
	TestTrue(TEXT("the next match counts"), Late.Observe(Sample(1, 0, 0, true, true, false)));
	TestEqual(TEXT("one match"), Late.GetPending().Matches, 1);

	// Quitting mid-match: kills and deaths are there to send, no match, no win.
	FHCJStatTracker Quitter;
	Quitter.Observe(Sample(0, 0, 0, true, false, false));
	Quitter.Observe(Sample(5, 2, 0, true, false, false));
	Quitter.ResetBaseline();
	Delta = Quitter.Take();
	TestEqual(TEXT("kills before quitting"), Delta.Kills, 5);
	TestEqual(TEXT("deaths before quitting"), Delta.Deaths, 2);
	TestEqual(TEXT("no match for a quitter"), Delta.Matches, 0);
	// The next server starts from zero again.
	Quitter.Observe(Sample(1, 0, 0, true, false, false));
	TestEqual(TEXT("fresh baseline on the next server"), Quitter.GetPending().Kills, 1);

	// A failed ingest is put back and sent with the next one.
	FHCJStatTracker Retry;
	Retry.Observe(Sample(2, 0, 0, true, false, false));
	const FHCJStatDelta Failed = Retry.Take();
	Retry.Observe(Sample(3, 0, 0, true, false, false));
	Retry.Restore(Failed);
	TestEqual(TEXT("restored amounts add up"), Retry.GetPending().Kills, 3);
	return true;
}

/** Leaderboard page: top rows, the local row from further down, names cleaned. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosLeaderboardPageTest, "HypeCubeJam.Online.LeaderboardPage", HCJEosTestFlags)

bool FHCJEosLeaderboardPageTest::RunTest(const FString& Parameters)
{
	TArray<FHCJLeaderboardRow> Records;
	for (int32 Rank = 1; Rank <= 25; ++Rank)
	{
		FHCJLeaderboardRow& Row = Records.AddDefaulted_GetRef();
		Row.Rank = Rank;
		Row.Score = 1000 - Rank;
		Row.Name = Rank == 3 ? FString() : FString::Printf(TEXT("Pilot%d"), Rank);
		Row.bLocal = Rank == 17;
	}
	TArray<FHCJLeaderboardRow> Page = HCJEos::MakeLeaderboardPage(Records, 10);
	TestEqual(TEXT("ten rows and the local one"), Page.Num(), 11);
	TestEqual(TEXT("the last row is the local player"), Page.Last().Rank, 17);
	TestTrue(TEXT("marked local"), Page.Last().bLocal);
	TestEqual(TEXT("a nameless record gets a name"), Page[2].Name, FString(TEXT("Pilot")));

	Records[16].bLocal = false;
	Records[4].bLocal = true;
	Page = HCJEos::MakeLeaderboardPage(Records, 10);
	TestEqual(TEXT("a local player in the top rows is not repeated"), Page.Num(), 10);
	TestTrue(TEXT("and is marked where it stands"), Page[4].bLocal);

	Records.SetNum(3);
	TestEqual(TEXT("a short board is shown as it is"), HCJEos::MakeLeaderboardPage(Records, 10).Num(), 3);
	TestEqual(TEXT("an empty board gives no rows"), HCJEos::MakeLeaderboardPage(TArray<FHCJLeaderboardRow>(), 10).Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
