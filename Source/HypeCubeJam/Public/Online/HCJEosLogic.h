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

	/** Filled in from a search result only. */
	FString LobbyId;
	FString OwnerId;

	static constexpr int32 MaxLanAddresses = 4;

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
}

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
