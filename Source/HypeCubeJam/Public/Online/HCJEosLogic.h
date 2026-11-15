#pragma once

#include "CoreMinimal.h"

/**
 * Plain C++ behind the Epic Online Services integration (UHCJEOSSubsystem): no SDK types, no UWorld, so all of it
 * is covered by automation tests. Config validation, what a lobby advertises and how it is packed into lobby
 * attributes, which address a joiner uses, and the bookkeeping of the stats a client reports for itself.
 */

/** Credentials of [HypeCubeJam.EOS] in DefaultEngine.ini. The secret is never logged or shown. */
struct HYPECUBEJAM_API FHCJEosConfig
{
	FString ProductId;
	FString SandboxId;
	FString DeploymentId;
	FString ClientId;
	FString ClientSecret;

	/** True when every value is there and looks like a value (no blanks, no placeholder brackets). */
	bool IsComplete() const { return GetProblem().IsEmpty(); }
	/** Empty when complete, else the name of the first missing / malformed key ("ClientSecret is empty"). Never contains a value. */
	FString GetProblem() const;
};

/** Why the online features are or are not usable; drives the menu text. */
enum class EHCJEosState : uint8
{
	/** Switched off for this process: dedicated server, commandlet, -HCJNoEOS. */
	Disabled,
	/** [HypeCubeJam.EOS] is missing or incomplete (e.g. no client secret). */
	NotConfigured,
	/** The SDK library could not be loaded or the platform could not be created. */
	SdkUnavailable,
	/** Signing in (device id login under way, or waiting to retry). */
	Connecting,
	/** Signed in: lobbies, stats and leaderboards work. */
	Ready,
	/** Sign-in failed (network down, credentials rejected); retried in the background. */
	Failed
};

namespace HCJEos
{
	/** The line the menu shows for a state. Detail is appended in brackets when not empty. */
	HYPECUBEJAM_API FString DescribeState(EHCJEosState State, const FString& Detail = FString());
	/** True for every state in which the lobby list and the leaderboards cannot be shown. */
	HYPECUBEJAM_API bool IsUnavailable(EHCJEosState State);

	/** Display name for the EOS user: printable ASCII, 1..32 characters, "Pilot" when nothing is left. */
	HYPECUBEJAM_API FString SanitizeDisplayName(const FString& Name);

	/** Bucket every HypeCubeJam lobby is created in and searched by. */
	inline const TCHAR* LobbyBucket() { return TEXT("HCJ_FFA"); }

	/** Stat and leaderboard names of the Developer Portal. */
	inline const TCHAR* StatKills() { return TEXT("HCJ_KILLS"); }
	inline const TCHAR* StatDeaths() { return TEXT("HCJ_DEATHS"); }
	inline const TCHAR* StatWins() { return TEXT("HCJ_WINS"); }
	inline const TCHAR* StatTurrets() { return TEXT("HCJ_TURRETS"); }
	inline const TCHAR* StatMatches() { return TEXT("HCJ_MATCHES"); }
	inline const TCHAR* LeaderboardKills() { return TEXT("HCJ_LB_KILLS"); }
	inline const TCHAR* LeaderboardWins() { return TEXT("HCJ_LB_WINS"); }
}

/** One lobby attribute: a string or a 64-bit integer under an upper-case key. */
struct HYPECUBEJAM_API FHCJLobbyAttribute
{
	FString Key;
	bool bIsInt = false;
	int64 IntValue = 0;
	FString StringValue;

	static FHCJLobbyAttribute MakeInt(const TCHAR* InKey, int64 Value) { FHCJLobbyAttribute A; A.Key = InKey; A.bIsInt = true; A.IntValue = Value; return A; }
	static FHCJLobbyAttribute MakeString(const TCHAR* InKey, const FString& Value) { FHCJLobbyAttribute A; A.Key = InKey; A.StringValue = Value; return A; }
};

/**
 * What a host advertises. The lobby is an advertisement only: the game connection is the engine's ordinary IP
 * connection to one of the advertised addresses (see PickJoinAddress), not EOS P2P.
 */
struct HYPECUBEJAM_API FHCJLobbyInfo
{
	FString HostName;
	/** Address reachable from the internet, typed by the host (ini / command line); empty when unknown. */
	FString PublicAddress;
	/** IPv4 addresses of the host's network adapters, at most MaxLanAddresses. */
	TArray<FString> LanAddresses;
	int32 Port = 7777;
	/** Humans in the match including the host. */
	int32 Players = 1;
	int32 MaxPlayers = 16;
	int32 Bots = 0;
	int32 Dimension = 4;
	int32 Seed = 0;
	/** Network version of the build; a joiner with another one is not let in by the engine. */
	FString Version;

	/**
	 * When the host last wrote the attributes, seconds since 1970 UTC (the host writes them again every
	 * HCJEos::LobbyHeartbeatSeconds while nothing changes); 0 = not said (a build before this attribute).
	 */
	int64 UpdatedUnix = 0;

	// ---- optional: what FIND GAMES shows beyond the columns. A lobby without them (a build before 0.19) still parses. ----

	/** Bot difficulty of the match (HCJBalance: 0 Easy .. 3 Ace); -1 = not said. */
	int32 Difficulty = -1;
	/** Kills that win the match; 0 = no frag limit, or not said. */
	int32 FragLimit = 0;
	/** When the match runs out of time, seconds since 1970 UTC on the host's clock; 0 = no time limit, or not said. */
	int64 EndsUnix = 0;
	/** The other pilots in the match, "Ada, Linus" (whole names, at most MaxPilotsChars characters, "+2" for those that did not fit); empty = nobody else, or not said. */
	FString Pilots;

	/** Filled in from a search result only. */
	FString LobbyId;
	FString OwnerId;

	static constexpr int32 MaxLanAddresses = 4;
	/** No header of the SDK states a limit for a string value; the pilot list stays as short as a key may be. */
	static constexpr int32 MaxPilotsChars = 64;

	/** Clamps and trims everything into what ToAttributes may send. */
	void Sanitize();
	/** The attributes of this lobby (after Sanitize). */
	TArray<FHCJLobbyAttribute> ToAttributes() const;
	/**
	 * Reads a lobby back. False when an attribute is missing, has the wrong type or an impossible value - such a
	 * lobby (another game in the bucket, a lobby whose attributes are not written yet) is not listed. Keys are
	 * compared without case: the service returns them upper-cased.
	 */
	static bool FromAttributes(const TArray<FHCJLobbyAttribute>& Attributes, FHCJLobbyInfo& OutInfo);

	/** True when the advertised values (not LobbyId / OwnerId) are equal: no update needs to be sent. */
	bool AdvertisesSameAs(const FHCJLobbyInfo& Other) const;

	/** "4D - 16 sectors". */
	FString DescribeMap() const;
	/** "4D - 16 sectors  -  Hard" when the host says its bot difficulty and has bots, else DescribeMap(). */
	FString DescribeMapAndDifficulty() const;
	/**
	 * The second line of a FIND GAMES row: "first to 25  -  about 7 min left  -  with Ada, Linus", each part only
	 * when its attribute is there; empty when none is. NowUnix: seconds since 1970 UTC.
	 */
	FString DescribeRules(int64 NowUnix) const;
};

namespace HCJEos
{
	/** True for "a.b.c.d" with four numbers 0..255. */
	HYPECUBEJAM_API bool IsIPv4(const FString& Address);
	/** True for a host name or address that may go into a travel URL (letters, digits, '.', '-', ':' and brackets only). */
	HYPECUBEJAM_API bool IsSafeHost(const FString& Address);
	/**
	 * The address a joiner should travel to, "host:port", or empty when the lobby gives nothing usable:
	 *  1. one of the host's LAN addresses is one of ours: the host is this machine -> 127.0.0.1
	 *  2. one of them is in the same /24 as one of ours: same network -> that address
	 *  3. the public address, when the host gave one
	 *  4. the first LAN address (works only when it happens to be routable)
	 */
	HYPECUBEJAM_API FString PickJoinAddress(const FHCJLobbyInfo& Lobby, const TArray<FString>& LocalAddresses);

	/** Whatever a stranger wrote as a pilot list: printable ASCII only, trimmed, at most FHCJLobbyInfo::MaxPilotsChars characters. */
	HYPECUBEJAM_API FString SanitizePilots(const FString& Pilots);
	/** "Ada, Linus, +2": as many whole names as fit into MaxChars, then how many did not. Empty for no names. */
	HYPECUBEJAM_API FString JoinPilotNames(const TArray<FString>& Names, int32 MaxChars = FHCJLobbyInfo::MaxPilotsChars);
	/**
	 * The status line of the title menu once a lobby search has answered: "Online as Ada  -  2 open matches, 5 pilots
	 * flying" or "Online as Ada  -  no open match right now: QUICK PLAY hosts one". OpenMatches is the number of
	 * lobbies FIND GAMES would list (after FilterJoinableLobbies), Pilots the sum of their players.
	 */
	HYPECUBEJAM_API FString DescribeOpenMatches(const FString& Name, int32 OpenMatches, int32 Pilots);
}

/** Why a lobby of the search is or is not a game this build can join. */
enum class EHCJLobbyFit : uint8
{
	Joinable,
	/** Another network version: the engine would refuse the connection. */
	OtherVersion,
	/** No free slot. */
	Full,
	/** Its host has not written it for a long time (a host that crashed; the service drops such lobbies only after a while). */
	Stale,
	/** Nothing in it this machine could connect to. */
	NoAddress
};

namespace HCJEos
{
	/** A host writes its lobby at least this often, and a lobby not written for LobbyStaleSeconds is not offered. */
	inline constexpr int32 LobbyHeartbeatSeconds = 120;
	inline constexpr int32 LobbyStaleSeconds = 600;

	/** Whether this build can join Lobby now. NowUnix: seconds since 1970 UTC. */
	HYPECUBEJAM_API EHCJLobbyFit ClassifyLobby(const FHCJLobbyInfo& Lobby, const FString& LocalVersion, const TArray<FString>& LocalAddresses, int64 NowUnix);
	HYPECUBEJAM_API const TCHAR* DescribeLobbyFit(EHCJLobbyFit Fit);
	/**
	 * FIND GAMES and Quick Play: removes every lobby that is not joinable and sorts the rest best first - most
	 * players, then most free slots (the ping to a host is not known: no connection exists before the join).
	 * Returns how many were removed; OutWhy, when given, counts them by reason ("2 other version, 1 full").
	 */
	HYPECUBEJAM_API int32 FilterJoinableLobbies(TArray<FHCJLobbyInfo>& Lobbies, const FString& LocalVersion, const TArray<FString>& LocalAddresses, int64 NowUnix, FString* OutWhy = nullptr);
}

/** Where Quick Play is. */
enum class EHCJQuickPlayPhase : uint8
{
	Idle,
	/** Waiting for the sign-in to Epic Online Services. */
	SigningIn,
	/** A lobby search is running. */
	Searching,
	/** Connecting to the host of a lobby. */
	Joining,
	/** Showing what happens next ("No open match found - hosting one") for a moment before it happens. */
	Announcing
};

/** What the caller of FHCJQuickPlay::Step has to do now. */
enum class EHCJQuickPlayAction : uint8
{
	None,
	/** Start a lobby search. */
	Search,
	/** Travel to GetJoinAddress(). */
	Join,
	/** Host a match (advertised as a lobby when the service is there), bots filling in. */
	Host
};

/** What Quick Play sees in one step. */
struct HYPECUBEJAM_API FHCJQuickPlayInput
{
	/** Monotonic seconds. */
	double Now = 0.0;
	EHCJEosState State = EHCJEosState::Disabled;
	/** The reason of State as the menu shows it ("not configured (ClientSecret is empty ...)"); may be empty. */
	FString StateDetail;
	/** The search this Quick Play asked for is complete (successfully or not). */
	bool bSearchDone = false;
	/** ... and it failed, with this text. */
	FString SearchError;
	/** The joinable lobbies of that search, best first (HCJEos::FilterJoinableLobbies). */
	TArray<FHCJLobbyInfo> Lobbies;
	TArray<FString> LocalAddresses;
	/** The join that is under way failed (network or travel failure). */
	bool bJoinFailed = false;
};

/**
 * QUICK PLAY: find an open match through the lobby search and join the best one; when there is none, or the join
 * does not work, host one; without the online service, play against bots at once. A state machine without a world
 * or an SDK (tested on its own): the local player controller feeds it every frame and does what it returns.
 */
class HYPECUBEJAM_API FHCJQuickPlay
{
public:
	/** Starts (again). */
	void Start(double Now);
	/** Stops without doing anything further (the player pressed CANCEL). True when a join was under way (the caller cancels the connection). */
	bool Cancel();
	/** The connection to the host of the lobby stands: Quick Play is done. */
	void NotifyJoined();
	/** One step. */
	EHCJQuickPlayAction Step(const FHCJQuickPlayInput& Input);

	bool IsActive() const { return Phase != EHCJQuickPlayPhase::Idle; }
	EHCJQuickPlayPhase GetPhase() const { return Phase; }
	/** The line for the screen: "Searching for open matches...", "Joining Ada (3/16 players)...", ... */
	const FString& GetStatus() const { return Status; }
	/** "host:port" of the join that Step asked for, and whose match it is. */
	const FString& GetJoinAddress() const { return JoinAddress; }
	const FString& GetJoinHost() const { return JoinHost; }
	/** True once after a join ran out of time: the caller cancels the pending connection. */
	bool TakeCancelRequest() { const bool bResult = bCancelRequested; bCancelRequested = false; return bResult; }
	/** True when the match that Step last asked to host is a local one because the online service is not there. */
	bool IsLocalOnly() const { return bLocalOnly; }

	/** Seconds: waiting for a sign-in that is under way, for the search, for a connection; how long an announcement stays; hosts tried before hosting. */
	double SignInTimeout = 15.0;
	double SearchTimeout = 10.0;
	double JoinTimeout = 15.0;
	double AnnounceSeconds = 2.0;
	int32 MaxJoinAttempts = 2;
	/**
	 * The most one step may add to the time of a phase. A frame that takes many seconds (the first frames of a
	 * process compile shaders; a second instance on one graphics card stalled 16 s) must not run a timeout out: the
	 * answer that was waited for arrives with the very next frame.
	 */
	double MaxStepSeconds = 0.25;

private:
	void Announce(const FString& Text, bool bLocal, double Now);
	/** The next lobby that was not tried yet becomes the join; false when there is none (or enough were tried). */
	bool BeginNextJoin(const TArray<FString>& LocalAddresses, double Now, const FString& Prefix);

	/** Starts a phase: its time runs from zero. */
	void Enter(EHCJQuickPlayPhase NewPhase);

	EHCJQuickPlayPhase Phase = EHCJQuickPlayPhase::Idle;
	/** Seconds spent in the phase (sum of the steps, each at most MaxStepSeconds) and the time of the last step. */
	double PhaseSeconds = 0.0;
	double LastStepTime = 0.0;
	FString Status;
	FString JoinAddress;
	FString JoinHost;
	TArray<FHCJLobbyInfo> Candidates;
	int32 NextCandidate = 0;
	int32 NumAttempts = 0;
	bool bCancelRequested = false;
	bool bLocalOnly = false;
};

/** What a client adds to its own EOS stats (all of them are SUM stats). */
struct HYPECUBEJAM_API FHCJStatDelta
{
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 Wins = 0;
	int32 Turrets = 0;
	int32 Matches = 0;

	bool IsEmpty() const { return Kills == 0 && Deaths == 0 && Wins == 0 && Turrets == 0 && Matches == 0; }
	void Add(const FHCJStatDelta& Other);
};

/** The local player's lifetime numbers as the service holds them (the five SUM stats). */
struct HYPECUBEJAM_API FHCJPilotRecord
{
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 Wins = 0;
	int32 Turrets = 0;
	int32 Matches = 0;
	/** A query has answered (also when the player has no stats yet: all zero). */
	bool bLoaded = false;

	/** What an accepted ingest added: shown at once, without a second request. */
	void Add(const FHCJStatDelta& Delta);
	/** "12 kills  -  7 deaths  -  K/D 1.7  -  3 wins in 5 matches  -  4 turrets"; "no matches flown yet" when all zero. */
	FString Describe() const;
};

namespace HCJEos
{
	/**
	 * The record when the service does not let the client read its stats: what the two leaderboards say about the
	 * local player - "12 kills (rank 3)  -  2 wins (rank 5)". A rank of 0 = not on that board; empty when on neither.
	 */
	HYPECUBEJAM_API FString DescribeBoardRecord(int32 Kills, int32 KillsRank, int32 Wins, int32 WinsRank);
}

/** The local player's replicated score line as its own client sees it this frame. */
struct HYPECUBEJAM_API FHCJStatSample
{
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 Turrets = 0;
	/** The player is a combatant (not in the title menu, not a spectator). */
	bool bParticipating = false;
	/** The match is over and the board is up. */
	bool bIntermission = false;
	/** bIntermission and the published winner is the local player. */
	bool bLocalWon = false;
};

/**
 * Turns the samples of the local score line into the amounts to ingest. Counters only ever grow inside a match;
 * a counter that went down means a new match or another server, and the baseline starts again. A match counts
 * (and a win) at the change to intermission, when the player was a combatant during that match and still is.
 */
class HYPECUBEJAM_API FHCJStatTracker
{
public:
	/** Returns true when now is a moment to send (the match just ended). */
	bool Observe(const FHCJStatSample& Sample);
	/** The local score line is gone (left the server, new world): baselines start again; what is pending stays. */
	void ResetBaseline();
	/** What has accumulated since the last Take; clears it. */
	FHCJStatDelta Take();
	const FHCJStatDelta& GetPending() const { return Pending; }
	/** Puts back amounts whose ingest failed, to be sent with the next one. */
	void Restore(const FHCJStatDelta& Delta) { Pending.Add(Delta); }

private:
	FHCJStatDelta Pending;
	int32 LastKills = 0;
	int32 LastDeaths = 0;
	int32 LastTurrets = 0;
	bool bPlayedThisMatch = false;
	bool bWasIntermission = false;
};

/** One line of a leaderboard page. */
struct HYPECUBEJAM_API FHCJLeaderboardRow
{
	int32 Rank = 0;
	FString Name;
	int32 Score = 0;
	bool bLocal = false;
};

namespace HCJEos
{
	/**
	 * The rows a leaderboard page shows: the first MaxRows of Records (already in rank order) and, when the local
	 * player is ranked below them, the local row appended as the last line. A nameless record shows as "Pilot".
	 */
	HYPECUBEJAM_API TArray<FHCJLeaderboardRow> MakeLeaderboardPage(const TArray<FHCJLeaderboardRow>& Records, int32 MaxRows);
}
