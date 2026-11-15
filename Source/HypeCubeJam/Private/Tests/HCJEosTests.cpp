#include "Tests/HCJTestHelpers.h"

#include "Algo/AnyOf.h"
#include "Game/HCJBalance.h"
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

/** The richer FIND GAMES rows: four optional attributes that an old build neither sends nor needs, and what the row says. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosLobbyRowTest, "HypeCubeJam.Online.LobbyRowExtras", HCJEosTestFlags)

bool FHCJEosLobbyRowTest::RunTest(const FString& Parameters)
{
	const int64 Now = 1800000000;
	FHCJLobbyInfo Lobby = MakeLobby();
	Lobby.Difficulty = 2;
	Lobby.FragLimit = 25;
	Lobby.EndsUnix = Now + 400;
	Lobby.Pilots = TEXT("Hedy, Alan_T");
	Lobby.Sanitize();
	const TArray<FHCJLobbyAttribute> Attributes = Lobby.ToAttributes();
	TestTrue(TEXT("still far below the service limit of 64 attributes"), Attributes.Num() <= 16);
	for (const FHCJLobbyAttribute& Attribute : Attributes)
	{
		TestTrue(TEXT("keys are at most 64 characters"), Attribute.Key.Len() <= 64);
		TestTrue(TEXT("string values are at most 64 characters"), Attribute.bIsInt || Attribute.StringValue.Len() <= 64);
	}

	FHCJLobbyInfo Back;
	TestTrue(TEXT("round trip parses"), FHCJLobbyInfo::FromAttributes(Attributes, Back));
	TestTrue(TEXT("round trip keeps every value"), Back.AdvertisesSameAs(Lobby));
	TestEqual(TEXT("difficulty"), Back.Difficulty, 2);
	TestEqual(TEXT("frag limit"), Back.FragLimit, 25);
	TestEqual(TEXT("end of the match"), Back.EndsUnix, Now + 400);
	TestEqual(TEXT("pilots"), Back.Pilots, FString(TEXT("Hedy, Alan_T")));
	TestEqual(TEXT("map cell"), Back.DescribeMapAndDifficulty(), FString(TEXT("4D - 16 sectors  -  Hard")));
	TestEqual(TEXT("second line"), Back.DescribeRules(Now), FString(TEXT("first to 25  -  about 7 min left  -  with Hedy, Alan_T")));
	TestEqual(TEXT("a match that is over on our clock says no time"), Back.DescribeRules(Now + 500), FString(TEXT("first to 25  -  with Hedy, Alan_T")));
	TestEqual(TEXT("the last minute"), Back.DescribeRules(Now + 350), FString(TEXT("first to 25  -  about a minute left  -  with Hedy, Alan_T")));
	for (int32 Level = 0; Level < HCJBalance::NumBotDifficulties; ++Level)
	{
		FHCJLobbyInfo Named = Lobby;
		Named.Difficulty = Level;
		TestTrue(TEXT("the row uses the names of the balance table"), Named.DescribeMapAndDifficulty().EndsWith(HCJBalance::GetDifficultyName(Level)));
	}
	FHCJLobbyInfo NoBots = Lobby;
	NoBots.Bots = 0;
	TestEqual(TEXT("no bots, no difficulty"), NoBots.DescribeMapAndDifficulty(), NoBots.DescribeMap());

	// What a 0.18 host advertises: none of the four. It parses, joins as before and the row says nothing extra.
	TArray<FHCJLobbyAttribute> Old = Attributes;
	Old.RemoveAll([](const FHCJLobbyAttribute& A) { return A.Key == TEXT("HCJDIFF") || A.Key == TEXT("HCJFRAG") || A.Key == TEXT("HCJEND") || A.Key == TEXT("HCJPILOTS"); });
	TestEqual(TEXT("eleven attributes as in 0.18"), Old.Num(), 11);
	TestTrue(TEXT("a 0.18 lobby parses"), FHCJLobbyInfo::FromAttributes(Old, Back));
	TestEqual(TEXT("difficulty not said"), Back.Difficulty, -1);
	TestTrue(TEXT("no second line"), Back.DescribeRules(Now).IsEmpty());
	TestEqual(TEXT("plain map cell"), Back.DescribeMapAndDifficulty(), FString(TEXT("4D - 16 sectors")));
	TestTrue(TEXT("as joinable as before"), HCJEos::ClassifyLobby(Back, Lobby.Version, { TEXT("192.168.1.77") }, Now) == EHCJLobbyFit::Joinable);

	// A match without limits and without company writes "nothing" in a form the service takes.
	FHCJLobbyInfo Plain = MakeLobby();
	Plain.Sanitize();
	for (const FHCJLobbyAttribute& Attribute : Plain.ToAttributes())
	{
		TestTrue(TEXT("no empty string values"), Attribute.bIsInt || !Attribute.StringValue.IsEmpty());
	}
	TestTrue(TEXT("plain lobby parses"), FHCJLobbyInfo::FromAttributes(Plain.ToAttributes(), Back));
	TestTrue(TEXT("nobody else stays empty"), Back.Pilots.IsEmpty());
	TestTrue(TEXT("nothing to say"), Back.DescribeRules(Now).IsEmpty());

	// Wrong types and impossible values of the optional four are "not said", never a reason to drop the lobby.
	TArray<FHCJLobbyAttribute> Wrong = Old;
	Wrong.Add(FHCJLobbyAttribute::MakeInt(TEXT("HCJDIFF"), 9));
	Wrong.Add(FHCJLobbyAttribute::MakeString(TEXT("HCJFRAG"), TEXT("25")));
	Wrong.Add(FHCJLobbyAttribute::MakeInt(TEXT("HCJEND"), -5));
	Wrong.Add(FHCJLobbyAttribute::MakeInt(TEXT("HCJPILOTS"), 3));
	TestTrue(TEXT("a lobby with junk extras still parses"), FHCJLobbyInfo::FromAttributes(Wrong, Back));
	TestEqual(TEXT("junk difficulty is not said"), Back.Difficulty, -1);
	TestEqual(TEXT("junk frag limit is not said"), Back.FragLimit, 0);
	TestEqual(TEXT("junk end is not said"), Back.EndsUnix, static_cast<int64>(0));
	TestTrue(TEXT("junk pilots are not said"), Back.Pilots.IsEmpty());

	// A hostile pilot list: 400 characters with markup, a URL option and control characters.
	FString Hostile = TEXT("<b>?listen\t\n");
	while (Hostile.Len() < 400)
	{
		Hostile += TEXT("x?Name=y /Game/Evil ");
	}
	Hostile.AppendChar(static_cast<TCHAR>(0x263A));
	TArray<FHCJLobbyAttribute> WithHostile = Old;
	WithHostile.Add(FHCJLobbyAttribute::MakeString(TEXT("HCJPILOTS"), Hostile));
	TestTrue(TEXT("a lobby with a hostile pilot list still parses"), FHCJLobbyInfo::FromAttributes(WithHostile, Back));
	TestTrue(TEXT("the list is cut to 64 characters"), Back.Pilots.Len() <= FHCJLobbyInfo::MaxPilotsChars);
	for (const TCHAR Char : Back.Pilots)
	{
		TestTrue(TEXT("only printable ASCII is left"), Char >= 32 && Char < 127);
	}
	// (It is only ever drawn as plain text; it must not get near the join address.)
	TestEqual(TEXT("the join address does not see it"), HCJEos::PickJoinAddress(Back, { TEXT("192.168.1.77") }), FString(TEXT("192.168.1.20:7777")));

	// The host's side: whole names, then how many did not fit.
	TestTrue(TEXT("no names, nothing"), HCJEos::JoinPilotNames({}).IsEmpty());
	TestEqual(TEXT("two names"), HCJEos::JoinPilotNames({ TEXT("Ada"), TEXT("Linus") }), FString(TEXT("Ada, Linus")));
	TArray<FString> Many;
	for (int32 Index = 0; Index < 15; ++Index)
	{
		Many.Add(FString::Printf(TEXT("Pilot_Number_%02d"), Index));
	}
	const FString Joined = HCJEos::JoinPilotNames(Many);
	TestTrue(TEXT("fifteen long names fit into 64 characters"), Joined.Len() <= 64);
	TestEqual(TEXT("three whole names and the rest as a number"), Joined, FString(TEXT("Pilot_Number_00, Pilot_Number_01, Pilot_Number_02, +12")));
	TestTrue(TEXT("one name of 32 characters fits"), HCJEos::JoinPilotNames({ FString::ChrN(32, TEXT('w')) }).Len() == 32);
	TestTrue(TEXT("never longer than asked"), HCJEos::JoinPilotNames({ FString::ChrN(32, TEXT('w')), FString::ChrN(32, TEXT('v')), TEXT("z") }).Len() <= 64);

	// The end of the match moves by a second or two between two looks: no reason to write the lobby again.
	FHCJLobbyInfo Later = Lobby;
	Later.EndsUnix += 3;
	TestTrue(TEXT("3 s of jitter is the same advertisement"), Later.AdvertisesSameAs(Lobby));
	Later.EndsUnix += 30;
	TestFalse(TEXT("half a minute is not"), Later.AdvertisesSameAs(Lobby));
	Later = Lobby;
	Later.EndsUnix = 0;
	TestFalse(TEXT("a time limit that went away is news"), Later.AdvertisesSameAs(Lobby));
	Later = Lobby;
	Later.Pilots = TEXT("Hedy");
	TestFalse(TEXT("a pilot who left is news"), Later.AdvertisesSameAs(Lobby));

	// The counter of the title menu.
	TestEqual(TEXT("two matches"), HCJEos::DescribeOpenMatches(TEXT("Ada"), 2, 5), FString(TEXT("Online as Ada  -  2 open matches, 5 pilots flying")));
	TestEqual(TEXT("one match, one pilot"), HCJEos::DescribeOpenMatches(TEXT("Ada"), 1, 1), FString(TEXT("Online as Ada  -  1 open match, 1 pilot flying")));
	TestEqual(TEXT("none"), HCJEos::DescribeOpenMatches(TEXT("Ada"), 0, 0), FString(TEXT("Online as Ada  -  no open match right now: QUICK PLAY hosts one")));
	// ... counts what FIND GAMES lists: other versions, full and stale lobbies are not "open".
	TArray<FHCJLobbyInfo> Found;
	Found.Add(Lobby);
	Found.Add(Lobby);
	Found.Last().Version = TEXT("00000000");
	Found.Add(Lobby);
	Found.Last().Players = Found.Last().MaxPlayers;
	Found.Add(Lobby);
	Found.Last().UpdatedUnix = Now - 2 * HCJEos::LobbyStaleSeconds;
	Found[0].UpdatedUnix = Now;
	HCJEos::FilterJoinableLobbies(Found, Lobby.Version, { TEXT("192.168.1.77") }, Now);
	TestEqual(TEXT("one of four lobbies is an open match"), Found.Num(), 1);
	return true;
}

/** The pilot record: the five lifetime numbers as a line, growing with what was sent, and the fallback from the boards. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosPilotRecordTest, "HypeCubeJam.Online.PilotRecord", HCJEosTestFlags)

bool FHCJEosPilotRecordTest::RunTest(const FString& Parameters)
{
	FHCJPilotRecord Record;
	TestFalse(TEXT("not loaded at first"), Record.bLoaded);
	TestEqual(TEXT("nothing flown"), Record.Describe(), FString(TEXT("no matches flown yet")));

	Record.Kills = 12;
	Record.Deaths = 7;
	Record.Wins = 3;
	Record.Turrets = 4;
	Record.Matches = 5;
	TestEqual(TEXT("the line"), Record.Describe(), FString(TEXT("12 kills  -  7 deaths  -  K/D 1.7  -  3 wins in 5 matches  -  4 turrets")));

	FHCJPilotRecord One;
	One.Kills = 1;
	One.Wins = 1;
	One.Matches = 1;
	One.Turrets = 1;
	TestEqual(TEXT("singular forms, no deaths"), One.Describe(), FString(TEXT("1 kill  -  0 deaths  -  K/D -  -  1 win in 1 match  -  1 turret")));

	FHCJPilotRecord OnlyDeaths;
	OnlyDeaths.Deaths = 5;
	TestEqual(TEXT("deaths alone are a record"), OnlyDeaths.Describe(), FString(TEXT("0 kills  -  5 deaths  -  K/D 0.0  -  0 wins in 0 matches  -  0 turrets")));

	// An accepted ingest is added without asking the service again.
	FHCJStatDelta Delta;
	Delta.Kills = 8;
	Delta.Deaths = 3;
	Delta.Wins = 1;
	Delta.Turrets = 2;
	Delta.Matches = 1;
	Record.Add(Delta);
	TestEqual(TEXT("after a match"), Record.Describe(), FString(TEXT("20 kills  -  10 deaths  -  K/D 2.0  -  4 wins in 6 matches  -  6 turrets")));

	// The service refuses the stats query: what the two leaderboards say about the local player.
	TestEqual(TEXT("both boards"), HCJEos::DescribeBoardRecord(12, 3, 2, 5), FString(TEXT("12 kills (rank 3)  -  2 wins (rank 5)")));
	TestEqual(TEXT("kills only"), HCJEos::DescribeBoardRecord(1, 1, 0, 0), FString(TEXT("1 kill (rank 1)")));
	TestEqual(TEXT("wins only"), HCJEos::DescribeBoardRecord(0, 0, 1, 2), FString(TEXT("1 win (rank 2)")));
	TestTrue(TEXT("on neither board: nothing is drawn"), HCJEos::DescribeBoardRecord(0, 0, 0, 0).IsEmpty());
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

	// Every attribute is required - but the time of the last write and the four of the richer rows, which builds
	// before they existed do not send.
	const TCHAR* OptionalKeys[] = { TEXT("HCJTIME"), TEXT("HCJDIFF"), TEXT("HCJFRAG"), TEXT("HCJEND"), TEXT("HCJPILOTS") };
	for (int32 Index = 0; Index < Attributes.Num(); ++Index)
	{
		TArray<FHCJLobbyAttribute> Missing = Attributes;
		Missing.RemoveAt(Index);
		const bool bOptional = Algo::AnyOf(OptionalKeys, [&](const TCHAR* Key) { return Attributes[Index].Key == Key; });
		if (bOptional)
		{
			TestTrue(*FString::Printf(TEXT("accepted without %s"), *Attributes[Index].Key), FHCJLobbyInfo::FromAttributes(Missing, Back));
			continue;
		}
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

/** Lobby hygiene: only joinable games are offered (no other version, no full, stale or unreachable ones), the best first. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosLobbyFilterTest, "HypeCubeJam.Online.LobbyFilter", HCJEosTestFlags)

bool FHCJEosLobbyFilterTest::RunTest(const FString& Parameters)
{
	const FString Version = TEXT("1A2B3C4D");
	const TArray<FString> Local = { TEXT("192.168.1.50") };
	const int64 Now = 1800000000;

	FHCJLobbyInfo Good = MakeLobby();
	Good.UpdatedUnix = Now - 30;
	TestTrue(TEXT("a fresh lobby of this version with room is joinable"), HCJEos::ClassifyLobby(Good, Version, Local, Now) == EHCJLobbyFit::Joinable);

	FHCJLobbyInfo Other = Good;
	Other.Version = TEXT("00000000");
	TestTrue(TEXT("another network version"), HCJEos::ClassifyLobby(Other, Version, Local, Now) == EHCJLobbyFit::OtherVersion);

	FHCJLobbyInfo Full = Good;
	Full.Players = Full.MaxPlayers;
	TestTrue(TEXT("no free slot"), HCJEos::ClassifyLobby(Full, Version, Local, Now) == EHCJLobbyFit::Full);

	FHCJLobbyInfo Stale = Good;
	Stale.UpdatedUnix = Now - HCJEos::LobbyStaleSeconds - 1;
	TestTrue(TEXT("not written for too long"), HCJEos::ClassifyLobby(Stale, Version, Local, Now) == EHCJLobbyFit::Stale);
	Stale.UpdatedUnix = Now - HCJEos::LobbyStaleSeconds + 5;
	TestTrue(TEXT("just inside the limit"), HCJEos::ClassifyLobby(Stale, Version, Local, Now) == EHCJLobbyFit::Joinable);
	Stale.UpdatedUnix = 0;
	TestTrue(TEXT("a lobby that does not say when it was written is not called stale"), HCJEos::ClassifyLobby(Stale, Version, Local, Now) == EHCJLobbyFit::Joinable);
	Stale.UpdatedUnix = Now + 5000;
	TestTrue(TEXT("a host whose clock runs ahead is not called stale"), HCJEos::ClassifyLobby(Stale, Version, Local, Now) == EHCJLobbyFit::Joinable);
	TestTrue(TEXT("the heartbeat is well inside the stale limit"), HCJEos::LobbyHeartbeatSeconds * 3 <= HCJEos::LobbyStaleSeconds);

	FHCJLobbyInfo NoAddress = Good;
	NoAddress.PublicAddress.Reset();
	NoAddress.LanAddresses.Reset();
	TestTrue(TEXT("nothing to connect to"), HCJEos::ClassifyLobby(NoAddress, Version, Local, Now) == EHCJLobbyFit::NoAddress);

	// The time travels with the other attributes and does not count as a change of the advertisement.
	FHCJLobbyInfo Back;
	TestTrue(TEXT("attributes with the time parse"), FHCJLobbyInfo::FromAttributes(Good.ToAttributes(), Back));
	TestEqual(TEXT("the time survives"), Back.UpdatedUnix, Good.UpdatedUnix);
	FHCJLobbyInfo Later = Good;
	Later.UpdatedUnix += 100;
	TestTrue(TEXT("a newer time alone is the same advertisement"), Later.AdvertisesSameAs(Good));
	TArray<FHCJLobbyAttribute> Old = Good.ToAttributes();
	Old.RemoveAll([](const FHCJLobbyAttribute& A) { return A.Key == TEXT("HCJTIME"); });
	TestTrue(TEXT("a lobby of a build without the time attribute still parses"), FHCJLobbyInfo::FromAttributes(Old, Back));
	TestEqual(TEXT("and has no time"), Back.UpdatedUnix, static_cast<int64>(0));

	// The list: three joinable ones in the order most players, then most free slots; the rest counted by reason.
	FHCJLobbyInfo Busy = Good;
	Busy.HostName = TEXT("Busy");
	Busy.Players = 9;
	FHCJLobbyInfo Small = Good;
	Small.HostName = TEXT("Small");
	Small.Players = 2;
	Small.MaxPlayers = 4;
	TArray<FHCJLobbyInfo> List = { Other, Small, Full, Good, Stale, Busy, NoAddress };
	List[4].UpdatedUnix = Now - 100000;
	FString Why;
	const int32 Hidden = HCJEos::FilterJoinableLobbies(List, Version, Local, Now, &Why);
	TestEqual(TEXT("four are not offered"), Hidden, 4);
	TestEqual(TEXT("three are"), List.Num(), 3);
	if (List.Num() == 3)
	{
		TestEqual(TEXT("most players first"), List[0].HostName, FString(TEXT("Busy")));
		TestEqual(TEXT("then, with equal players, the one with more room"), List[1].HostName, FString(TEXT("Ada")));
		TestEqual(TEXT("then the small one"), List[2].HostName, FString(TEXT("Small")));
	}
	TestTrue(TEXT("the reasons are counted"), Why.Contains(TEXT("1 other version")) && Why.Contains(TEXT("1 full")) && Why.Contains(TEXT("1 stale")) && Why.Contains(TEXT("1 no address")));
	return true;
}

/** QUICK PLAY: search, join the best, fall back to hosting; a local match when the service is not there. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJEosQuickPlayTest, "HypeCubeJam.Online.QuickPlay", HCJEosTestFlags)

bool FHCJEosQuickPlayTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Local = { TEXT("192.168.1.50") };
	const auto MakeInput = [&Local](double Now, EHCJEosState State)
	{
		FHCJQuickPlayInput Input;
		Input.Now = Now;
		Input.State = State;
		Input.LocalAddresses = Local;
		return Input;
	};

	// (The cases below jump in time from step to step; the frame clamp has its own case at the end.)
	const auto MakeQuickPlay = []()
	{
		FHCJQuickPlay QuickPlay;
		QuickPlay.MaxStepSeconds = 1.0e9;
		return QuickPlay;
	};

	// 1. Service ready, one open match: search, then join it.
	{
		FHCJQuickPlay QuickPlay = MakeQuickPlay();
		TestFalse(TEXT("idle before the start"), QuickPlay.IsActive());
		QuickPlay.Start(100.0);
		TestTrue(TEXT("active"), QuickPlay.IsActive());
		TestTrue(TEXT("ready: asks for a search"), QuickPlay.Step(MakeInput(100.1, EHCJEosState::Ready)) == EHCJQuickPlayAction::Search);
		TestTrue(TEXT("says so"), QuickPlay.GetStatus().Contains(TEXT("Searching")));
		TestTrue(TEXT("waits for the search"), QuickPlay.Step(MakeInput(101.0, EHCJEosState::Ready)) == EHCJQuickPlayAction::None);
		FHCJQuickPlayInput Found = MakeInput(102.0, EHCJEosState::Ready);
		Found.bSearchDone = true;
		Found.Lobbies = { MakeLobby() };
		TestTrue(TEXT("a match: join"), QuickPlay.Step(Found) == EHCJQuickPlayAction::Join);
		TestEqual(TEXT("at the address of the same network"), QuickPlay.GetJoinAddress(), FString(TEXT("192.168.1.20:7777")));
		TestTrue(TEXT("names the host and the players"), QuickPlay.GetStatus().Contains(TEXT("Joining Ada (2/16 players)")));
		TestTrue(TEXT("waits for the connection"), QuickPlay.Step(MakeInput(104.0, EHCJEosState::Ready)) == EHCJQuickPlayAction::None);
		QuickPlay.NotifyJoined();
		TestFalse(TEXT("done once joined"), QuickPlay.IsActive());
	}

	// 2. Nothing found: says so, then hosts.
	{
		FHCJQuickPlay QuickPlay = MakeQuickPlay();
		QuickPlay.Start(0.0);
		QuickPlay.Step(MakeInput(0.1, EHCJEosState::Ready));
		FHCJQuickPlayInput Empty = MakeInput(1.0, EHCJEosState::Ready);
		Empty.bSearchDone = true;
		TestTrue(TEXT("nothing found: an announcement first"), QuickPlay.Step(Empty) == EHCJQuickPlayAction::None);
		TestEqual(TEXT("the text"), QuickPlay.GetStatus(), FString(TEXT("No open match found - hosting one")));
		TestTrue(TEXT("still showing it"), QuickPlay.Step(MakeInput(1.0 + QuickPlay.AnnounceSeconds * 0.5, EHCJEosState::Ready)) == EHCJQuickPlayAction::None);
		TestTrue(TEXT("then host"), QuickPlay.Step(MakeInput(1.1 + QuickPlay.AnnounceSeconds, EHCJEosState::Ready)) == EHCJQuickPlayAction::Host);
		TestFalse(TEXT("an online match"), QuickPlay.IsLocalOnly());
		TestFalse(TEXT("done"), QuickPlay.IsActive());
	}

	// 3. The join fails: the next match is tried, then a match is hosted. A join that times out asks for a cancel.
	{
		FHCJQuickPlay QuickPlay = MakeQuickPlay();
		QuickPlay.Start(0.0);
		QuickPlay.Step(MakeInput(0.1, EHCJEosState::Ready));
		FHCJLobbyInfo Second = MakeLobby();
		Second.HostName = TEXT("Grace");
		Second.LanAddresses = { TEXT("192.168.1.30") };
		FHCJLobbyInfo Third = MakeLobby();
		Third.HostName = TEXT("Linus");
		FHCJQuickPlayInput Found = MakeInput(1.0, EHCJEosState::Ready);
		Found.bSearchDone = true;
		Found.Lobbies = { MakeLobby(), Second, Third };
		TestTrue(TEXT("join the first"), QuickPlay.Step(Found) == EHCJQuickPlayAction::Join);
		FHCJQuickPlayInput Failed = MakeInput(3.0, EHCJEosState::Ready);
		Failed.bJoinFailed = true;
		TestTrue(TEXT("failed: join the second"), QuickPlay.Step(Failed) == EHCJQuickPlayAction::Join);
		TestEqual(TEXT("its address"), QuickPlay.GetJoinAddress(), FString(TEXT("192.168.1.30:7777")));
		TestTrue(TEXT("says what happened"), QuickPlay.GetStatus().Contains(TEXT("Could not join Ada")) && QuickPlay.GetStatus().Contains(TEXT("Joining Grace")));
		TestFalse(TEXT("a failure reported by the engine needs no cancel"), QuickPlay.TakeCancelRequest());
		TestTrue(TEXT("no connection in time: no third try, an announcement"), QuickPlay.Step(MakeInput(3.5 + QuickPlay.JoinTimeout, EHCJEosState::Ready)) == EHCJQuickPlayAction::None);
		TestTrue(TEXT("the pending connection is to be cancelled"), QuickPlay.TakeCancelRequest());
		TestFalse(TEXT("only once"), QuickPlay.TakeCancelRequest());
		TestTrue(TEXT("names the fallback"), QuickPlay.GetStatus().Contains(TEXT("Could not join Grace")) && QuickPlay.GetStatus().Contains(TEXT("hosting a match instead")));
		TestTrue(TEXT("then host"), QuickPlay.Step(MakeInput(100.0, EHCJEosState::Ready)) == EHCJQuickPlayAction::Host);
	}

	// 4. No service: a local match, and the text says why. The reason never carries more than the state's own words.
	{
		FHCJQuickPlay QuickPlay = MakeQuickPlay();
		QuickPlay.Start(0.0);
		FHCJQuickPlayInput Off = MakeInput(0.1, EHCJEosState::NotConfigured);
		Off.StateDetail = TEXT("ClientSecret is empty in [HypeCubeJam.EOS]");
		TestTrue(TEXT("not configured: an announcement"), QuickPlay.Step(Off) == EHCJQuickPlayAction::None);
		TestTrue(TEXT("says local and why"), QuickPlay.GetStatus().StartsWith(TEXT("Online services unavailable (not configured")) && QuickPlay.GetStatus().EndsWith(TEXT("starting a local match against bots")));
		TestTrue(TEXT("then host"), QuickPlay.Step(MakeInput(0.2 + QuickPlay.AnnounceSeconds, EHCJEosState::NotConfigured)) == EHCJQuickPlayAction::Host);
		TestTrue(TEXT("a local match"), QuickPlay.IsLocalOnly());
	}

	// 5. A sign-in that does not finish, a search that fails, a search without an answer: all end in a hosted match.
	{
		FHCJQuickPlay QuickPlay = MakeQuickPlay();
		QuickPlay.Start(0.0);
		TestTrue(TEXT("signing in: wait"), QuickPlay.Step(MakeInput(1.0, EHCJEosState::Connecting)) == EHCJQuickPlayAction::None);
		TestTrue(TEXT("still signing in"), QuickPlay.GetPhase() == EHCJQuickPlayPhase::SigningIn);
		QuickPlay.Step(MakeInput(1.0 + QuickPlay.SignInTimeout, EHCJEosState::Connecting));
		TestTrue(TEXT("gives up waiting"), QuickPlay.GetPhase() == EHCJQuickPlayPhase::Announcing && QuickPlay.GetStatus().Contains(TEXT("no answer")));
		TestTrue(TEXT("local match"), QuickPlay.Step(MakeInput(1000.0, EHCJEosState::Connecting)) == EHCJQuickPlayAction::Host && QuickPlay.IsLocalOnly());

		QuickPlay.Start(0.0);
		QuickPlay.Step(MakeInput(0.1, EHCJEosState::Ready));
		FHCJQuickPlayInput Broken = MakeInput(1.0, EHCJEosState::Ready);
		Broken.bSearchDone = true;
		Broken.SearchError = TEXT("Search failed (EOS_NoConnection)");
		QuickPlay.Step(Broken);
		TestEqual(TEXT("a failed search is named"), QuickPlay.GetStatus(), FString(TEXT("Search failed (EOS_NoConnection) - hosting a match instead")));
		TestTrue(TEXT("and a match is hosted"), QuickPlay.Step(MakeInput(100.0, EHCJEosState::Ready)) == EHCJQuickPlayAction::Host);

		QuickPlay.Start(0.0);
		QuickPlay.Step(MakeInput(0.1, EHCJEosState::Ready));
		QuickPlay.Step(MakeInput(0.2 + QuickPlay.SearchTimeout, EHCJEosState::Ready));
		TestTrue(TEXT("a search without an answer ends"), QuickPlay.GetPhase() == EHCJQuickPlayPhase::Announcing);

		QuickPlay.Start(0.0);
		QuickPlay.Step(MakeInput(0.1, EHCJEosState::Ready));
		TestFalse(TEXT("cancel while searching: nothing to cancel in the engine"), QuickPlay.Cancel());
		TestFalse(TEXT("and it is over"), QuickPlay.IsActive());
		TestTrue(TEXT("a step after that does nothing"), QuickPlay.Step(MakeInput(5.0, EHCJEosState::Ready)) == EHCJQuickPlayAction::None);
	}

	// 6. One frame of 20 s (a process compiling its first shaders) does not run the sign-in wait out: the next frame,
	// which brings the sign-in, still finds Quick Play waiting. Thirty seconds of ordinary frames do run it out.
	{
		FHCJQuickPlay QuickPlay;
		QuickPlay.Start(0.0);
		QuickPlay.Step(MakeInput(0.016, EHCJEosState::Connecting));
		QuickPlay.Step(MakeInput(20.0, EHCJEosState::Connecting));
		TestTrue(TEXT("still waiting for the sign-in after a 20 s frame"), QuickPlay.GetPhase() == EHCJQuickPlayPhase::SigningIn);
		TestTrue(TEXT("the sign-in of the next frame starts the search"), QuickPlay.Step(MakeInput(20.016, EHCJEosState::Ready)) == EHCJQuickPlayAction::Search);

		QuickPlay.Start(100.0);
		double Now = 100.0;
		for (int32 Frame = 0; Frame < 30 * 60 && QuickPlay.GetPhase() == EHCJQuickPlayPhase::SigningIn; ++Frame)
		{
			Now += 1.0 / 60.0;
			QuickPlay.Step(MakeInput(Now, EHCJEosState::Connecting));
		}
		TestTrue(TEXT("ordinary frames run the wait out"), QuickPlay.GetPhase() == EHCJQuickPlayPhase::Announcing);
		TestTrue(TEXT("after the sign-in timeout, not before"), Now - 100.0 > QuickPlay.SignInTimeout && Now - 100.0 < QuickPlay.SignInTimeout + 1.0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
