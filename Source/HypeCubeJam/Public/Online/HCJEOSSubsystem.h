#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Online/HCJEosLogic.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HCJEOSSubsystem.generated.h"

struct FHCJEosImpl;

/** One leaderboard as the menu shows it. */
struct FHCJLeaderboardView
{
	FString Id;
	FString Title;
	/** Top rows, then the local row when it is ranked below them (HCJEos::MakeLeaderboardPage). */
	TArray<FHCJLeaderboardRow> Rows;
	bool bLoading = false;
	bool bLoaded = false;
	/** Why there are no rows; empty when the query worked. */
	FString Error;
};

/**
 * Epic Online Services for the local player: lobby advertisement / browser, stat ingest and leaderboards.
 *
 * Talks to the EOS C SDK directly (through the engine's EOSShared plugin, which loads the library, initialises
 * it and ticks the platform) because the only identity this product has is the device id of EOS Connect:
 * EOS_Connect_CreateDeviceId once per Windows user, then EOS_Connect_Login with EOS_ECT_DEVICEID_ACCESS_TOKEN,
 * creating the product user on EOS_InvalidUser. No Epic account, no overlay, no voice.
 *
 * Everything is asynchronous and fails soft: with the config section incomplete, the client secret empty, the
 * library missing or the network down the state says so (GetState / GetStatusLine), every request does
 * nothing, and the rest of the game runs exactly as without this class. Never active on a dedicated server or in
 * a commandlet; -HCJNoEOS switches it off.
 *
 * The lobby is an advertisement, not a transport: the host creates a public lobby whose attributes carry its
 * name, addresses, port and match (FHCJLobbyInfo); a joiner reads them and travels to the address by the
 * ordinary IP path. Nobody but the host is a member of the lobby.
 *
 * Game thread only. One per game instance; lives across the world changes of joining and leaving.
 */
UCLASS()
class HYPECUBEJAM_API UHCJEOSSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UHCJEOSSubsystem();
	virtual ~UHCJEOSSubsystem() override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** Sends what is pending (stats, lobby removal) and gives the service up to two seconds to take it. */
	virtual void Deinitialize() override;

	/** The subsystem of the game instance of WorldContext's world, or nullptr. */
	static UHCJEOSSubsystem* Get(const UObject* WorldContext);

	/** Reads [HypeCubeJam.EOS] from the engine ini. */
	static FHCJEosConfig LoadConfig();

	EHCJEosState GetState() const { return State; }
	bool IsReady() const { return State == EHCJEosState::Ready; }
	/** One line for the menu: the state, who is signed in, or why not. */
	FString GetStatusLine() const;
	/** Changes whenever something the menu shows changed (state, lobby list, leaderboards). */
	uint32 GetRevision() const { return Revision; }

	/** The pilot name. Used at sign-in; a change after sign-in signs in again so the leaderboards show it. */
	void SetDisplayName(const FString& Name);

	// ---- hosting ----

	/**
	 * Called by the local controller of a listen host about once a second: bHosting says whether a match is open
	 * to join, Info what to advertise. Creates the lobby, updates it when Info changed (at most every 2 s),
	 * destroys it when bHosting turns false.
	 */
	void UpdateHosting(bool bHosting, const FHCJLobbyInfo& Info);
	bool IsAdvertising() const;

	// ---- browser ----

	/** Starts a search for HypeCubeJam lobbies (no-op while one runs or when not ready). */
	void RefreshLobbies();
	bool IsSearchingLobbies() const { return bSearching; }
	const TArray<FHCJLobbyInfo>& GetLobbies() const { return Lobbies; }
	/** "3 games found", "No games found", the error of the last search, or empty before the first one. */
	const FString& GetLobbyStatus() const { return LobbyStatus; }
	/** IPv4 addresses of this machine's adapters (for FHCJLobbyInfo::LanAddresses and HCJEos::PickJoinAddress). */
	static TArray<FString> GetLocalAddresses();
	/** Network version string of this build, as advertised in FHCJLobbyInfo::Version. */
	static FString GetBuildVersion();
	/** The address typed by the host for players outside its network ([HypeCubeJam.EOS] PublicAddress or -HCJPublicAddress=). */
	static FString GetConfiguredPublicAddress();

	// ---- stats and leaderboards ----

	/** Every frame from the local controller: the local score line. Sends the stats when the match ends. */
	void ObserveLocalStats(const FHCJStatSample& Sample);
	/**
	 * Sends what is pending now. bScoreLineGone: the local score line is gone (leaving a server, world change), so
	 * the baselines start again; false when the same score line lives on (the host went back to the title menu).
	 */
	void FlushStats(bool bScoreLineGone = true);
	/** Text for the end-of-match board ("Stats sent to Epic Online Services"), empty when there is nothing to say. */
	FString GetStatsStatus() const { return StatsStatus; }
	/** True between a successful Initialize and Deinitialize (callbacks arriving later are dropped). */
	bool IsActive() const { return Impl != nullptr; }

	/** Queries both leaderboards again (no-op while loading or when not ready). */
	void RefreshLeaderboards();
	static constexpr int32 NumLeaderboards = 2;
	static constexpr int32 LeaderboardRows = 10;
	const FHCJLeaderboardView& GetLeaderboard(int32 Index) const { return Leaderboards[FMath::Clamp(Index, 0, NumLeaderboards - 1)]; }

private:
	bool Tick(float DeltaTime);
	void SetState(EHCJEosState NewState, const FString& Detail = FString());
	void Touch() { ++Revision; }

	void StartLogin();
	void OnLoggedIn();
	void OnLoginFailed(const FString& Why);

	void CreateLobby();
	void PushLobbyAttributes();
	void DestroyLobby();

	void IngestPending();
	/** Development (-HCJEosDemo): logs what IngestPending would send. */
	void DemoSendStats();
	void QueryLeaderboard(int32 Index);

	/** SDK handles and ids; defined in the .cpp so that no SDK header leaks into the game's headers. */
	FHCJEosImpl* Impl = nullptr;

	EHCJEosState State = EHCJEosState::Disabled;
	FString StateDetail;
	uint32 Revision = 0;

	FString DisplayName;
	/** The name the current sign-in was made with. */
	FString SignedInName;
	bool bLoginInProgress = false;
	int32 LoginFailures = 0;
	double NextLoginTime = 0.0;

	bool bWantHosting = false;
	FHCJLobbyInfo WantedInfo;
	FHCJLobbyInfo AdvertisedInfo;
	bool bLobbyHasAttributes = false;
	bool bLobbyBusy = false;
	double NextLobbyUpdateTime = 0.0;

	bool bSearching = false;
	TArray<FHCJLobbyInfo> Lobbies;
	FString LobbyStatus;

	FHCJStatTracker StatTracker;
	FString StatsStatus;
	bool bIngestWanted = false;
	bool bIngestInFlight = false;

	FHCJLeaderboardView Leaderboards[NumLeaderboards];

	FTSTicker::FDelegateHandle TickHandle;
};
