#include "Online/HCJEOSSubsystem.h"

#include "HypeCubeJam.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/NetworkVersion.h"
#include "Misc/Parse.h"
#include "SocketSubsystem.h"

#include "EOSShared.h"
#include "IEOSSDKManager.h"
#include "eos_connect.h"
#include "eos_leaderboards.h"
#include "eos_lobby.h"
#include "eos_sdk.h"
#include "eos_stats.h"

/** SDK state of the subsystem. */
struct FHCJEosImpl
{
	IEOSPlatformHandlePtr Platform;
	EOS_HConnect Connect = nullptr;
	EOS_HLobby Lobby = nullptr;
	EOS_HStats Stats = nullptr;
	EOS_HLeaderboards Leaderboards = nullptr;
	EOS_ProductUserId LocalUser = nullptr;
	EOS_NotificationId AuthExpirationId = EOS_INVALID_NOTIFICATIONID;
	/** The lobby this process owns; empty when none. */
	FString LobbyId;
};

namespace
{
	const TCHAR* ConfigSection = TEXT("HypeCubeJam.EOS");
	const TCHAR* PlatformConfigName = TEXT("HypeCubeJam");

	/** Requests whose completion callback has not arrived yet (Deinitialize waits for them, briefly). */
	int32 GPendingCalls = 0;

	/**
	 * Client data of one request: the SDK calls Thunk on the game thread (inside the platform tick) when the
	 * request is complete; a subsystem that is gone or shut down by then is not touched.
	 */
	template <typename TInfo>
	struct THCJEosCall
	{
		TWeakObjectPtr<UHCJEOSSubsystem> Owner;
		TFunction<void(const TInfo&)> Fn;

		static void EOS_CALL Thunk(const TInfo* Data)
		{
			if (Data == nullptr || !EOS_EResult_IsOperationComplete(Data->ResultCode))
			{
				return;
			}
			THCJEosCall* Call = static_cast<THCJEosCall*>(Data->ClientData);
			--GPendingCalls;
			if (Call->Owner.IsValid() && Call->Owner->IsActive())
			{
				Call->Fn(*Data);
			}
			delete Call;
		}
	};

	template <typename TInfo>
	THCJEosCall<TInfo>* MakeCall(UHCJEOSSubsystem* Owner, TFunction<void(const TInfo&)> Fn)
	{
		++GPendingCalls;
		THCJEosCall<TInfo>* Call = new THCJEosCall<TInfo>();
		Call->Owner = Owner;
		Call->Fn = MoveTemp(Fn);
		return Call;
	}

	FString ResultText(EOS_EResult Result)
	{
		return LexToString(Result);
	}

	const TCHAR* LeaderboardId(int32 Index)
	{
		return Index == 0 ? HCJEos::LeaderboardKills() : HCJEos::LeaderboardWins();
	}
}

UHCJEOSSubsystem::UHCJEOSSubsystem()
{
	Leaderboards[0].Id = HCJEos::LeaderboardKills();
	Leaderboards[0].Title = TEXT("KILLS");
	Leaderboards[1].Id = HCJEos::LeaderboardWins();
	Leaderboards[1].Title = TEXT("WINS");
}

UHCJEOSSubsystem::~UHCJEOSSubsystem()
{
	delete Impl;
	Impl = nullptr;
}

UHCJEOSSubsystem* UHCJEOSSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext != nullptr ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UHCJEOSSubsystem>() : nullptr;
}

FHCJEosConfig UHCJEOSSubsystem::LoadConfig()
{
	FHCJEosConfig Config;
	if (GConfig != nullptr)
	{
		GConfig->GetString(ConfigSection, TEXT("ProductId"), Config.ProductId, GEngineIni);
		GConfig->GetString(ConfigSection, TEXT("SandboxId"), Config.SandboxId, GEngineIni);
		GConfig->GetString(ConfigSection, TEXT("DeploymentId"), Config.DeploymentId, GEngineIni);
		GConfig->GetString(ConfigSection, TEXT("ClientId"), Config.ClientId, GEngineIni);
		GConfig->GetString(ConfigSection, TEXT("ClientSecret"), Config.ClientSecret, GEngineIni);
	}
	Config.ProductId.TrimStartAndEndInline();
	Config.SandboxId.TrimStartAndEndInline();
	Config.DeploymentId.TrimStartAndEndInline();
	Config.ClientId.TrimStartAndEndInline();
	Config.ClientSecret.TrimStartAndEndInline();
	return Config;
}

FString UHCJEOSSubsystem::GetConfiguredPublicAddress()
{
	FString Address;
	if (!FParse::Value(FCommandLine::Get(), TEXT("HCJPublicAddress="), Address) && GConfig != nullptr)
	{
		GConfig->GetString(ConfigSection, TEXT("PublicAddress"), Address, GEngineIni);
	}
	Address.TrimStartAndEndInline();
	return HCJEos::IsSafeHost(Address) ? Address : FString();
}

TArray<FString> UHCJEOSSubsystem::GetLocalAddresses()
{
	// Asking the system for its adapters is not free and they rarely change: once every 30 s is enough.
	static TArray<FString> Cached;
	static double CachedTime = -1000.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - CachedTime < 30.0)
	{
		return Cached;
	}
	CachedTime = Now;
	TArray<FString> Result;
	if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		TArray<TSharedPtr<FInternetAddr>> Addresses;
		Sockets->GetLocalAdapterAddresses(Addresses);
		for (const TSharedPtr<FInternetAddr>& Address : Addresses)
		{
			const FString Text = Address.IsValid() ? Address->ToString(false) : FString();
			if (HCJEos::IsIPv4(Text) && !Text.StartsWith(TEXT("127.")) && !Text.StartsWith(TEXT("169.254.")) && Text != TEXT("0.0.0.0"))
			{
				Result.AddUnique(Text);
			}
		}
	}
	Cached = Result;
	return Result;
}

FString UHCJEOSSubsystem::GetBuildVersion()
{
	return FString::Printf(TEXT("%08X"), FNetworkVersion::GetLocalNetworkVersion());
}

void UHCJEOSSubsystem::SetState(EHCJEosState NewState, const FString& Detail)
{
	if (State != NewState || StateDetail != Detail)
	{
		State = NewState;
		StateDetail = Detail;
		Touch();
		UE_LOG(LogHCJ, Log, TEXT("EOS: %s"), *HCJEos::DescribeState(State, StateDetail));
	}
}

FString UHCJEOSSubsystem::GetStatusLine() const
{
	if (State == EHCJEosState::Ready)
	{
		return FString::Printf(TEXT("Online: signed in to Epic Online Services as %s"), *SignedInName);
	}
	return HCJEos::DescribeState(State, StateDetail);
}

void UHCJEOSSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (IsRunningDedicatedServer() || IsRunningCommandlet() || FParse::Param(FCommandLine::Get(), TEXT("HCJNoEOS")))
	{
		SetState(EHCJEosState::Disabled);
		UE_LOG(LogHCJ, Log, TEXT("EOS: off for this process (dedicated server, commandlet or -HCJNoEOS)"));
		return;
	}

#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("HCJEosDemo")))
	{
		// Development aid for pictures and for the join path: no service is contacted, the pages show made-up
		// rows. The first game advertises this machine's own addresses, so JOIN reaches a host running here.
		SignedInName = TEXT("DemoPilot");
		const TCHAR* Names[] = { TEXT("Ada"), TEXT("Grace_H"), TEXT("Linus"), TEXT("Margaret") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			FHCJLobbyInfo& Lobby = Lobbies.AddDefaulted_GetRef();
			Lobby.HostName = Names[Index];
			Lobby.LanAddresses = Index == 0 ? GetLocalAddresses() : TArray<FString>{ FString::Printf(TEXT("198.51.100.%d"), 10 + Index) };
			Lobby.Players = Index == 2 ? 16 : 1 + Index * 2;
			Lobby.Bots = 7 - Index * 2;
			Lobby.Dimension = 3 + Index % 3;
			Lobby.Version = Index == 3 ? FString(TEXT("00000000")) : GetBuildVersion();
			Lobby.LobbyId = FString::Printf(TEXT("demo%d"), Index);
			Lobby.Sanitize();
		}
		LobbyStatus = TEXT("4 games found  (made-up rows of -HCJEosDemo)");
		for (FHCJLeaderboardView& View : Leaderboards)
		{
			TArray<FHCJLeaderboardRow> Records;
			for (int32 Rank = 1; Rank <= 14; ++Rank)
			{
				FHCJLeaderboardRow& Row = Records.AddDefaulted_GetRef();
				Row.Rank = Rank;
				Row.Score = (&View == &Leaderboards[0] ? 40 : 6) * (16 - Rank);
				Row.bLocal = Rank == (&View == &Leaderboards[0] ? 13 : 4);
				Row.Name = Row.bLocal ? SignedInName : FString::Printf(TEXT("Pilot_%02d"), Rank * 7 % 31);
			}
			View.Rows = HCJEos::MakeLeaderboardPage(Records, LeaderboardRows);
			View.bLoaded = true;
		}
		UE_LOG(LogHCJ, Log, TEXT("EOS: -HCJEosDemo: made-up lobby and leaderboard rows, no service contacted"));
		State = EHCJEosState::Ready;
		Touch();
		return;
	}
#endif

	const FHCJEosConfig Config = LoadConfig();
	if (!Config.IsComplete())
	{
		// GetProblem names a key, never a value.
		SetState(EHCJEosState::NotConfigured, Config.GetProblem() + TEXT(" in [HypeCubeJam.EOS]"));
		return;
	}

	IEOSSDKManager* Manager = IEOSSDKManager::Get();
	if (Manager == nullptr || !Manager->IsInitialized())
	{
		SetState(EHCJEosState::SdkUnavailable, Manager == nullptr ? TEXT("EOSShared plugin not loaded") : TEXT("SDK library not loaded"));
		return;
	}

	FEOSSDKPlatformConfig PlatformConfig;
	PlatformConfig.Name = PlatformConfigName;
	PlatformConfig.ProductId = Config.ProductId;
	PlatformConfig.SandboxId = Config.SandboxId;
	PlatformConfig.DeploymentId = Config.DeploymentId;
	PlatformConfig.ClientId = Config.ClientId;
	PlatformConfig.ClientSecret = Config.ClientSecret;
	PlatformConfig.bDisableOverlay = true;
	PlatformConfig.bDisableSocialOverlay = true;
	PlatformConfig.bEnableRTC = false;
	PlatformConfig.bLoadingInEditor = GIsEditor && !IsRunningGame();
	const FString CacheBase = Manager->GetCacheDirBase();
	PlatformConfig.CacheDirectory = CacheBase.IsEmpty() ? FString() : CacheBase / TEXT("HypeCubeJam") / TEXT("EOSCache");
	Manager->AddPlatformConfig(PlatformConfig, /*bOverwriteExistingConfig*/ true);

	IEOSPlatformHandlePtr Platform = Manager->CreatePlatform(PlatformConfig.Name);
	if (!Platform.IsValid())
	{
		SetState(EHCJEosState::SdkUnavailable, TEXT("platform not created"));
		return;
	}

	Impl = new FHCJEosImpl();
	Impl->Platform = Platform;
	Impl->Connect = EOS_Platform_GetConnectInterface(*Platform);
	Impl->Lobby = EOS_Platform_GetLobbyInterface(*Platform);
	Impl->Stats = EOS_Platform_GetStatsInterface(*Platform);
	Impl->Leaderboards = EOS_Platform_GetLeaderboardsInterface(*Platform);

	// The token of a Connect login lasts about an hour; the service says when it is about to run out.
	EOS_Connect_AddNotifyAuthExpirationOptions ExpirationOptions = {};
	ExpirationOptions.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
	Impl->AuthExpirationId = EOS_Connect_AddNotifyAuthExpiration(Impl->Connect, &ExpirationOptions, this,
		[](const EOS_Connect_AuthExpirationCallbackInfo* Data)
		{
			UHCJEOSSubsystem* Self = Data != nullptr ? static_cast<UHCJEOSSubsystem*>(Data->ClientData) : nullptr;
			if (Self != nullptr && Self->IsActive() && !Self->bLoginInProgress)
			{
				UE_LOG(LogHCJ, Log, TEXT("EOS: sign-in about to expire, signing in again"));
				Self->StartLogin();
			}
		});

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UHCJEOSSubsystem::Tick), 0.25f);

	UE_LOG(LogHCJ, Log, TEXT("EOS: platform created (SDK %s), signing in with the device id"), *Manager->GetSDKVersion());
	SetState(EHCJEosState::Connecting);
	StartLogin();
}

void UHCJEOSSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	if (Impl != nullptr)
	{
		if (State == EHCJEosState::Ready)
		{
			// Last chance for what this session earned and for taking the advertisement down.
			bIngestWanted = true;
			IngestPending();
			bWantHosting = false;
			if (!Impl->LobbyId.IsEmpty())
			{
				DestroyLobby();
			}
			const double Start = FPlatformTime::Seconds();
			while (GPendingCalls > 0 && FPlatformTime::Seconds() - Start < 2.0)
			{
				Impl->Platform->Tick();
				FPlatformProcess::Sleep(0.01f);
			}
			UE_LOG(LogHCJ, Log, TEXT("EOS: shut down after %.2f s, %d request(s) unanswered"), FPlatformTime::Seconds() - Start, GPendingCalls);
		}
		if (Impl->AuthExpirationId != EOS_INVALID_NOTIFICATIONID)
		{
			EOS_Connect_RemoveNotifyAuthExpiration(Impl->Connect, Impl->AuthExpirationId);
		}
		delete Impl;
		Impl = nullptr;
	}
	else if (!StatTracker.GetPending().IsEmpty())
	{
		bIngestWanted = true;
		DemoSendStats();
	}
	State = EHCJEosState::Disabled;
	Super::Deinitialize();
}

void UHCJEOSSubsystem::SetDisplayName(const FString& Name)
{
	DisplayName = HCJEos::SanitizeDisplayName(Name);
}

bool UHCJEOSSubsystem::Tick(float DeltaTime)
{
	if (Impl == nullptr)
	{
		return true;
	}
	const double Now = FPlatformTime::Seconds();

	if (State != EHCJEosState::Ready)
	{
		if (!bLoginInProgress && Now >= NextLoginTime)
		{
			StartLogin();
		}
		return true;
	}

	if (!bLoginInProgress && !DisplayName.IsEmpty() && DisplayName != SignedInName)
	{
		// The pilot was renamed: a new sign-in carries the name to the leaderboards.
		StartLogin();
	}

	if (!bLobbyBusy)
	{
		if (bWantHosting)
		{
			if (Impl->LobbyId.IsEmpty())
			{
				if (Now >= NextLobbyUpdateTime)
				{
					CreateLobby();
				}
			}
			else if ((!bLobbyHasAttributes || !WantedInfo.AdvertisesSameAs(AdvertisedInfo)) && Now >= NextLobbyUpdateTime)
			{
				PushLobbyAttributes();
			}
		}
		else if (!Impl->LobbyId.IsEmpty())
		{
			DestroyLobby();
		}
	}

	if (bIngestWanted && !bIngestInFlight)
	{
		IngestPending();
	}
	return true;
}

// ---- sign-in ----

void UHCJEOSSubsystem::StartLogin()
{
	if (Impl == nullptr || bLoginInProgress)
	{
		return;
	}
	bLoginInProgress = true;
	if (DisplayName.IsEmpty())
	{
		DisplayName = HCJEos::SanitizeDisplayName(FString(FPlatformProcess::UserName(false)));
	}
	const FString LoginName = DisplayName;

	// Step 2: sign in with the device id; an unknown device id becomes a new product user.
	const auto Login = [this, LoginName]()
	{
		const FTCHARToUTF8 NameUtf8(*LoginName);
		EOS_Connect_Credentials Credentials = {};
		Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
		Credentials.Token = nullptr;
		Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
		EOS_Connect_UserLoginInfo UserInfo = {};
		UserInfo.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
		UserInfo.DisplayName = NameUtf8.Get();
		EOS_Connect_LoginOptions Options = {};
		Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		Options.Credentials = &Credentials;
		Options.UserLoginInfo = &UserInfo;

		EOS_Connect_Login(Impl->Connect, &Options, MakeCall<EOS_Connect_LoginCallbackInfo>(this,
			[this, LoginName](const EOS_Connect_LoginCallbackInfo& Info)
			{
				if (Info.ResultCode == EOS_EResult::EOS_Success)
				{
					Impl->LocalUser = Info.LocalUserId;
					SignedInName = LoginName;
					OnLoggedIn();
				}
				else if (Info.ResultCode == EOS_EResult::EOS_InvalidUser && Info.ContinuanceToken != nullptr)
				{
					UE_LOG(LogHCJ, Log, TEXT("EOS: first sign-in of this device id, creating the product user"));
					EOS_Connect_CreateUserOptions CreateOptions = {};
					CreateOptions.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
					CreateOptions.ContinuanceToken = Info.ContinuanceToken;
					EOS_Connect_CreateUser(Impl->Connect, &CreateOptions, MakeCall<EOS_Connect_CreateUserCallbackInfo>(this,
						[this, LoginName](const EOS_Connect_CreateUserCallbackInfo& CreateInfo)
						{
							if (CreateInfo.ResultCode == EOS_EResult::EOS_Success)
							{
								Impl->LocalUser = CreateInfo.LocalUserId;
								SignedInName = LoginName;
								OnLoggedIn();
							}
							else
							{
								OnLoginFailed(FString::Printf(TEXT("create user: %s"), *ResultText(CreateInfo.ResultCode)));
							}
						}), &THCJEosCall<EOS_Connect_CreateUserCallbackInfo>::Thunk);
				}
				else
				{
					OnLoginFailed(FString::Printf(TEXT("login: %s"), *ResultText(Info.ResultCode)));
				}
			}), &THCJEosCall<EOS_Connect_LoginCallbackInfo>::Thunk);
	};

	// Step 1: make sure this Windows user has a device id (EOS_DuplicateNotAllowed: it already has one).
	EOS_Connect_CreateDeviceIdOptions DeviceOptions = {};
	DeviceOptions.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
	DeviceOptions.DeviceModel = "PC Windows";
	EOS_Connect_CreateDeviceId(Impl->Connect, &DeviceOptions, MakeCall<EOS_Connect_CreateDeviceIdCallbackInfo>(this,
		[this, Login](const EOS_Connect_CreateDeviceIdCallbackInfo& Info)
		{
			if (Info.ResultCode == EOS_EResult::EOS_Success || Info.ResultCode == EOS_EResult::EOS_DuplicateNotAllowed)
			{
				if (Info.ResultCode == EOS_EResult::EOS_Success)
				{
					UE_LOG(LogHCJ, Log, TEXT("EOS: device id created for this Windows user"));
				}
				Login();
			}
			else
			{
				OnLoginFailed(FString::Printf(TEXT("device id: %s"), *ResultText(Info.ResultCode)));
			}
		}), &THCJEosCall<EOS_Connect_CreateDeviceIdCallbackInfo>::Thunk);
}

void UHCJEOSSubsystem::OnLoggedIn()
{
	bLoginInProgress = false;
	LoginFailures = 0;
	const bool bFirst = State != EHCJEosState::Ready;
	UE_LOG(LogHCJ, Log, TEXT("EOS: signed in as '%s', product user %s"), *SignedInName, *LexToString(Impl->LocalUser));
	SetState(EHCJEosState::Ready);
	Touch();
	if (bFirst)
	{
		RefreshLeaderboards();
	}
}

void UHCJEOSSubsystem::OnLoginFailed(const FString& Why)
{
	bLoginInProgress = false;
	++LoginFailures;
	// 20 s, 60 s, then every two minutes: a machine without a network must not be busy with this.
	const double Delay = LoginFailures == 1 ? 20.0 : LoginFailures == 2 ? 60.0 : 120.0;
	NextLoginTime = FPlatformTime::Seconds() + Delay;
	if (State == EHCJEosState::Ready)
	{
		// A renewal failed; the old sign-in may still be good. Remember the name so that this is not retried every tick.
		UE_LOG(LogHCJ, Log, TEXT("EOS: signing in again failed (%s)"), *Why);
		SignedInName = DisplayName;
		return;
	}
	UE_LOG(LogHCJ, Log, TEXT("EOS: sign-in failed (%s), next try in %.0f s"), *Why, Delay);
	SetState(EHCJEosState::Failed, Why);
}

// ---- hosting ----

bool UHCJEOSSubsystem::IsAdvertising() const
{
	return Impl != nullptr && !Impl->LobbyId.IsEmpty() && bLobbyHasAttributes;
}

void UHCJEOSSubsystem::UpdateHosting(bool bHosting, const FHCJLobbyInfo& Info)
{
	bWantHosting = bHosting;
	if (bHosting)
	{
		WantedInfo = Info;
		WantedInfo.Sanitize();
	}
#if !UE_BUILD_SHIPPING
	const bool bDemo = Impl == nullptr && State == EHCJEosState::Ready;
	if (bDemo && (bHosting != bLobbyHasAttributes || (bHosting && !WantedInfo.AdvertisesSameAs(AdvertisedInfo))))
	{
		// -HCJEosDemo: no lobby is made; the log shows what a host would advertise and when it would stop.
		bLobbyHasAttributes = bHosting;
		AdvertisedInfo = WantedInfo;
		if (bHosting)
		{
			UE_LOG(LogHCJ, Log, TEXT("EOS demo: would advertise '%s' %d/%d players, %d bots, %s, seed %d, port %d, %d LAN address(es), public address %s, version %s"),
				*WantedInfo.HostName, WantedInfo.Players, WantedInfo.MaxPlayers, WantedInfo.Bots, *WantedInfo.DescribeMap(), WantedInfo.Seed, WantedInfo.Port,
				WantedInfo.LanAddresses.Num(), WantedInfo.PublicAddress.IsEmpty() ? TEXT("not set") : TEXT("set"), *WantedInfo.Version);
		}
		else
		{
			UE_LOG(LogHCJ, Log, TEXT("EOS demo: would remove the lobby"));
		}
	}
#endif
}

void UHCJEOSSubsystem::CreateLobby()
{
	const FTCHARToUTF8 Bucket(HCJEos::LobbyBucket());
	EOS_Lobby_CreateLobbyOptions Options = {};
	Options.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
	Options.LocalUserId = Impl->LocalUser;
	Options.MaxLobbyMembers = static_cast<uint32_t>(FMath::Clamp(WantedInfo.MaxPlayers, 1, EOS_LOBBY_MAX_LOBBY_MEMBERS));
	Options.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
	Options.bPresenceEnabled = EOS_FALSE;
	Options.bAllowInvites = EOS_FALSE;
	Options.BucketId = Bucket.Get();
	Options.bDisableHostMigration = EOS_TRUE;
	Options.bEnableRTCRoom = EOS_FALSE;
	Options.LocalRTCOptions = nullptr;
	Options.LobbyId = nullptr;
	Options.bEnableJoinById = EOS_FALSE;
	Options.bRejoinAfterKickRequiresInvite = EOS_FALSE;
	Options.AllowedPlatformIds = nullptr;
	Options.AllowedPlatformIdsCount = 0;
	Options.bCrossplayOptOut = EOS_FALSE;

	bLobbyBusy = true;
	bLobbyHasAttributes = false;
	EOS_Lobby_CreateLobby(Impl->Lobby, &Options, MakeCall<EOS_Lobby_CreateLobbyCallbackInfo>(this,
		[this](const EOS_Lobby_CreateLobbyCallbackInfo& Info)
		{
			bLobbyBusy = false;
			if (Info.ResultCode == EOS_EResult::EOS_Success && Info.LobbyId != nullptr)
			{
				Impl->LobbyId = UTF8_TO_TCHAR(Info.LobbyId);
				NextLobbyUpdateTime = 0.0;
				UE_LOG(LogHCJ, Log, TEXT("EOS: lobby %s created"), *Impl->LobbyId);
			}
			else
			{
				NextLobbyUpdateTime = FPlatformTime::Seconds() + 30.0;
				UE_LOG(LogHCJ, Log, TEXT("EOS: lobby not created (%s), next try in 30 s"), *ResultText(Info.ResultCode));
			}
		}), &THCJEosCall<EOS_Lobby_CreateLobbyCallbackInfo>::Thunk);
}

void UHCJEOSSubsystem::PushLobbyAttributes()
{
	const FTCHARToUTF8 LobbyIdUtf8(*Impl->LobbyId);
	EOS_Lobby_UpdateLobbyModificationOptions ModOptions = {};
	ModOptions.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
	ModOptions.LocalUserId = Impl->LocalUser;
	ModOptions.LobbyId = LobbyIdUtf8.Get();
	EOS_HLobbyModification Modification = nullptr;
	EOS_EResult Result = EOS_Lobby_UpdateLobbyModification(Impl->Lobby, &ModOptions, &Modification);
	if (Result != EOS_EResult::EOS_Success || Modification == nullptr)
	{
		UE_LOG(LogHCJ, Log, TEXT("EOS: lobby update not started (%s)"), *ResultText(Result));
		NextLobbyUpdateTime = FPlatformTime::Seconds() + 10.0;
		return;
	}

	const FHCJLobbyInfo Sent = WantedInfo;
	for (const FHCJLobbyAttribute& Attribute : Sent.ToAttributes())
	{
		const FTCHARToUTF8 Key(*Attribute.Key);
		const FTCHARToUTF8 Value(*Attribute.StringValue);
		EOS_Lobby_AttributeData Data = {};
		Data.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
		Data.Key = Key.Get();
		if (Attribute.bIsInt)
		{
			Data.Value.AsInt64 = Attribute.IntValue;
			Data.ValueType = EOS_ELobbyAttributeType::EOS_AT_INT64;
		}
		else
		{
			Data.Value.AsUtf8 = Value.Get();
			Data.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
		}
		EOS_LobbyModification_AddAttributeOptions AddOptions = {};
		AddOptions.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
		AddOptions.Attribute = &Data;
		AddOptions.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
		Result = EOS_LobbyModification_AddAttribute(Modification, &AddOptions);
		if (Result != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogHCJ, Log, TEXT("EOS: lobby attribute %s refused (%s)"), *Attribute.Key, *ResultText(Result));
		}
	}

	EOS_Lobby_UpdateLobbyOptions UpdateOptions = {};
	UpdateOptions.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
	UpdateOptions.LobbyModificationHandle = Modification;
	bLobbyBusy = true;
	// Not more often than every two seconds, whatever the player count does.
	NextLobbyUpdateTime = FPlatformTime::Seconds() + 2.0;
	EOS_Lobby_UpdateLobby(Impl->Lobby, &UpdateOptions, MakeCall<EOS_Lobby_UpdateLobbyCallbackInfo>(this,
		[this, Sent](const EOS_Lobby_UpdateLobbyCallbackInfo& Info)
		{
			bLobbyBusy = false;
			if (Info.ResultCode == EOS_EResult::EOS_Success || Info.ResultCode == EOS_EResult::EOS_NoChange)
			{
				const bool bFirst = !bLobbyHasAttributes;
				AdvertisedInfo = Sent;
				bLobbyHasAttributes = true;
				UE_LOG(LogHCJ, Log, TEXT("EOS: lobby %s: '%s' %d/%d players, %d bots, %s, port %d, %d LAN address(es), public address %s"),
					bFirst ? TEXT("advertised") : TEXT("updated"), *Sent.HostName, Sent.Players, Sent.MaxPlayers, Sent.Bots, *Sent.DescribeMap(), Sent.Port,
					Sent.LanAddresses.Num(), Sent.PublicAddress.IsEmpty() ? TEXT("not set") : TEXT("set"));
			}
			else
			{
				UE_LOG(LogHCJ, Log, TEXT("EOS: lobby update failed (%s)"), *ResultText(Info.ResultCode));
				if (Info.ResultCode == EOS_EResult::EOS_NotFound)
				{
					// The service dropped the lobby (connection lost for a while): make a new one.
					Impl->LobbyId.Reset();
					bLobbyHasAttributes = false;
				}
				NextLobbyUpdateTime = FPlatformTime::Seconds() + 10.0;
			}
		}), &THCJEosCall<EOS_Lobby_UpdateLobbyCallbackInfo>::Thunk);
	EOS_LobbyModification_Release(Modification);
}

void UHCJEOSSubsystem::DestroyLobby()
{
	const FString LobbyId = Impl->LobbyId;
	const FTCHARToUTF8 LobbyIdUtf8(*LobbyId);
	EOS_Lobby_DestroyLobbyOptions Options = {};
	Options.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
	Options.LocalUserId = Impl->LocalUser;
	Options.LobbyId = LobbyIdUtf8.Get();
	Impl->LobbyId.Reset();
	bLobbyHasAttributes = false;
	bLobbyBusy = true;
	EOS_Lobby_DestroyLobby(Impl->Lobby, &Options, MakeCall<EOS_Lobby_DestroyLobbyCallbackInfo>(this,
		[this, LobbyId](const EOS_Lobby_DestroyLobbyCallbackInfo& Info)
		{
			bLobbyBusy = false;
			UE_LOG(LogHCJ, Log, TEXT("EOS: lobby %s removed (%s)"), *LobbyId, *ResultText(Info.ResultCode));
		}), &THCJEosCall<EOS_Lobby_DestroyLobbyCallbackInfo>::Thunk);
}

// ---- browser ----

void UHCJEOSSubsystem::RefreshLobbies()
{
	if (Impl == nullptr || State != EHCJEosState::Ready || bSearching)
	{
		return;
	}

	EOS_Lobby_CreateLobbySearchOptions CreateOptions = {};
	CreateOptions.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
	CreateOptions.MaxResults = 50;
	EOS_HLobbySearch Search = nullptr;
	EOS_EResult Result = EOS_Lobby_CreateLobbySearch(Impl->Lobby, &CreateOptions, &Search);
	if (Result != EOS_EResult::EOS_Success || Search == nullptr)
	{
		LobbyStatus = FString::Printf(TEXT("Search failed (%s)"), *ResultText(Result));
		Touch();
		return;
	}

	const FTCHARToUTF8 Bucket(HCJEos::LobbyBucket());
	EOS_Lobby_AttributeData Parameter = {};
	Parameter.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
	Parameter.Key = EOS_LOBBY_SEARCH_BUCKET_ID;
	Parameter.Value.AsUtf8 = Bucket.Get();
	Parameter.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
	EOS_LobbySearch_SetParameterOptions ParameterOptions = {};
	ParameterOptions.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
	ParameterOptions.Parameter = &Parameter;
	ParameterOptions.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
	Result = EOS_LobbySearch_SetParameter(Search, &ParameterOptions);
	if (Result != EOS_EResult::EOS_Success)
	{
		EOS_LobbySearch_Release(Search);
		LobbyStatus = FString::Printf(TEXT("Search failed (%s)"), *ResultText(Result));
		Touch();
		return;
	}

	EOS_LobbySearch_FindOptions FindOptions = {};
	FindOptions.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
	FindOptions.LocalUserId = Impl->LocalUser;
	bSearching = true;
	LobbyStatus = TEXT("Searching...");
	Touch();
	EOS_LobbySearch_Find(Search, &FindOptions, MakeCall<EOS_LobbySearch_FindCallbackInfo>(this,
		[this, Search](const EOS_LobbySearch_FindCallbackInfo& Info)
		{
			bSearching = false;
			Lobbies.Reset();
			int32 NumSkipped = 0;
			if (Info.ResultCode == EOS_EResult::EOS_Success)
			{
				EOS_LobbySearch_GetSearchResultCountOptions CountOptions = {};
				CountOptions.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
				const uint32_t Count = EOS_LobbySearch_GetSearchResultCount(Search, &CountOptions);
				for (uint32_t Index = 0; Index < Count; ++Index)
				{
					EOS_LobbySearch_CopySearchResultByIndexOptions CopyOptions = {};
					CopyOptions.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
					CopyOptions.LobbyIndex = Index;
					EOS_HLobbyDetails Details = nullptr;
					if (EOS_LobbySearch_CopySearchResultByIndex(Search, &CopyOptions, &Details) != EOS_EResult::EOS_Success || Details == nullptr)
					{
						continue;
					}

					TArray<FHCJLobbyAttribute> Attributes;
					EOS_LobbyDetails_GetAttributeCountOptions AttributeCountOptions = {};
					AttributeCountOptions.ApiVersion = EOS_LOBBYDETAILS_GETATTRIBUTECOUNT_API_LATEST;
					const uint32_t NumAttributes = EOS_LobbyDetails_GetAttributeCount(Details, &AttributeCountOptions);
					for (uint32_t AttributeIndex = 0; AttributeIndex < NumAttributes; ++AttributeIndex)
					{
						EOS_LobbyDetails_CopyAttributeByIndexOptions AttributeOptions = {};
						AttributeOptions.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYINDEX_API_LATEST;
						AttributeOptions.AttrIndex = AttributeIndex;
						EOS_Lobby_Attribute* Attribute = nullptr;
						if (EOS_LobbyDetails_CopyAttributeByIndex(Details, &AttributeOptions, &Attribute) == EOS_EResult::EOS_Success && Attribute != nullptr)
						{
							if (Attribute->Data != nullptr && Attribute->Data->Key != nullptr)
							{
								if (Attribute->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_INT64)
								{
									Attributes.Add(FHCJLobbyAttribute::MakeInt(UTF8_TO_TCHAR(Attribute->Data->Key), Attribute->Data->Value.AsInt64));
								}
								else if (Attribute->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING && Attribute->Data->Value.AsUtf8 != nullptr)
								{
									Attributes.Add(FHCJLobbyAttribute::MakeString(UTF8_TO_TCHAR(Attribute->Data->Key), FString(UTF8_TO_TCHAR(Attribute->Data->Value.AsUtf8))));
								}
							}
							EOS_Lobby_Attribute_Release(Attribute);
						}
					}

					FHCJLobbyInfo Lobby;
					if (FHCJLobbyInfo::FromAttributes(Attributes, Lobby))
					{
						EOS_LobbyDetails_CopyInfoOptions InfoOptions = {};
						InfoOptions.ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST;
						EOS_LobbyDetails_Info* DetailsInfo = nullptr;
						if (EOS_LobbyDetails_CopyInfo(Details, &InfoOptions, &DetailsInfo) == EOS_EResult::EOS_Success && DetailsInfo != nullptr)
						{
							Lobby.LobbyId = DetailsInfo->LobbyId != nullptr ? FString(UTF8_TO_TCHAR(DetailsInfo->LobbyId)) : FString();
							Lobby.OwnerId = LexToString(DetailsInfo->LobbyOwnerUserId);
							EOS_LobbyDetails_Info_Release(DetailsInfo);
						}
						// This process's own advertisement is not a game to join.
						if (Lobby.LobbyId.IsEmpty() || Lobby.LobbyId != Impl->LobbyId)
						{
							Lobbies.Add(MoveTemp(Lobby));
						}
					}
					else
					{
						++NumSkipped;
					}
					EOS_LobbyDetails_Release(Details);
				}

				const FString Version = GetBuildVersion();
				Lobbies.StableSort([&Version](const FHCJLobbyInfo& A, const FHCJLobbyInfo& B)
				{
					const bool bA = A.Version == Version;
					const bool bB = B.Version == Version;
					return bA != bB ? bA : A.Players > B.Players;
				});
				LobbyStatus = Lobbies.Num() == 0 ? FString(TEXT("No games found. Host one with PLAY and it will show up here for others."))
					: FString::Printf(TEXT("%d game%s found"), Lobbies.Num(), Lobbies.Num() == 1 ? TEXT("") : TEXT("s"));
			}
			else if (Info.ResultCode == EOS_EResult::EOS_NotFound)
			{
				LobbyStatus = TEXT("No games found. Host one with PLAY and it will show up here for others.");
			}
			else
			{
				LobbyStatus = FString::Printf(TEXT("Search failed (%s)"), *ResultText(Info.ResultCode));
			}
			UE_LOG(LogHCJ, Log, TEXT("EOS: lobby search: %s, %d listed, %d skipped"), *ResultText(Info.ResultCode), Lobbies.Num(), NumSkipped);
			EOS_LobbySearch_Release(Search);
			Touch();
		}), &THCJEosCall<EOS_LobbySearch_FindCallbackInfo>::Thunk);
}

// ---- stats ----

void UHCJEOSSubsystem::ObserveLocalStats(const FHCJStatSample& Sample)
{
	if (StatTracker.Observe(Sample))
	{
		bIngestWanted = true;
		DemoSendStats();
	}
}

void UHCJEOSSubsystem::DemoSendStats()
{
#if !UE_BUILD_SHIPPING
	if (Impl == nullptr && State == EHCJEosState::Ready && bIngestWanted)
	{
		// -HCJEosDemo: nothing is sent; the log shows what would be and when.
		bIngestWanted = false;
		const FHCJStatDelta Delta = StatTracker.Take();
		if (!Delta.IsEmpty())
		{
			UE_LOG(LogHCJ, Log, TEXT("EOS demo: would send stats: kills +%d, deaths +%d, wins +%d, turrets +%d, matches +%d"),
				Delta.Kills, Delta.Deaths, Delta.Wins, Delta.Turrets, Delta.Matches);
			StatsStatus = TEXT("Stats sent to Epic Online Services (leaderboards update after a few minutes)  [demo: nothing was sent]");
		}
	}
#endif
}

void UHCJEOSSubsystem::FlushStats(bool bScoreLineGone)
{
	if (bScoreLineGone)
	{
		StatTracker.ResetBaseline();
	}
	if (!StatTracker.GetPending().IsEmpty())
	{
		bIngestWanted = true;
		DemoSendStats();
	}
}

void UHCJEOSSubsystem::IngestPending()
{
	if (Impl == nullptr || State != EHCJEosState::Ready || bIngestInFlight)
	{
		return;
	}
	bIngestWanted = false;
	const FHCJStatDelta Delta = StatTracker.Take();
	if (Delta.IsEmpty())
	{
		return;
	}

	const FTCHARToUTF8 Kills(HCJEos::StatKills());
	const FTCHARToUTF8 Deaths(HCJEos::StatDeaths());
	const FTCHARToUTF8 Wins(HCJEos::StatWins());
	const FTCHARToUTF8 Turrets(HCJEos::StatTurrets());
	const FTCHARToUTF8 Matches(HCJEos::StatMatches());
	TArray<EOS_Stats_IngestData> Data;
	const auto Add = [&Data](const char* Name, int32 Amount)
	{
		if (Amount > 0)
		{
			EOS_Stats_IngestData& Entry = Data.AddZeroed_GetRef();
			Entry.ApiVersion = EOS_STATS_INGESTDATA_API_LATEST;
			Entry.StatName = Name;
			Entry.IngestAmount = Amount;
		}
	};
	Add(Kills.Get(), Delta.Kills);
	Add(Deaths.Get(), Delta.Deaths);
	Add(Wins.Get(), Delta.Wins);
	Add(Turrets.Get(), Delta.Turrets);
	Add(Matches.Get(), Delta.Matches);
	if (Data.Num() == 0)
	{
		return;
	}

	EOS_Stats_IngestStatOptions Options = {};
	Options.ApiVersion = EOS_STATS_INGESTSTAT_API_LATEST;
	Options.LocalUserId = Impl->LocalUser;
	Options.Stats = Data.GetData();
	Options.StatsCount = static_cast<uint32_t>(Data.Num());
	Options.TargetUserId = Impl->LocalUser;
	bIngestInFlight = true;
	UE_LOG(LogHCJ, Log, TEXT("EOS: sending stats: kills +%d, deaths +%d, wins +%d, turrets +%d, matches +%d"),
		Delta.Kills, Delta.Deaths, Delta.Wins, Delta.Turrets, Delta.Matches);
	EOS_Stats_IngestStat(Impl->Stats, &Options, MakeCall<EOS_Stats_IngestStatCompleteCallbackInfo>(this,
		[this, Delta](const EOS_Stats_IngestStatCompleteCallbackInfo& Info)
		{
			bIngestInFlight = false;
			if (Info.ResultCode == EOS_EResult::EOS_Success)
			{
				StatsStatus = TEXT("Stats sent to Epic Online Services (leaderboards update after a few minutes)");
				UE_LOG(LogHCJ, Log, TEXT("EOS: stats accepted"));
			}
			else
			{
				// Kept for the next occasion (next match end, leaving, quitting).
				StatTracker.Restore(Delta);
				StatsStatus = TEXT("Stats not sent: Epic Online Services did not answer");
				UE_LOG(LogHCJ, Log, TEXT("EOS: stats not accepted (%s), kept for the next try"), *ResultText(Info.ResultCode));
			}
			Touch();
		}), &THCJEosCall<EOS_Stats_IngestStatCompleteCallbackInfo>::Thunk);
}

// ---- leaderboards ----

void UHCJEOSSubsystem::RefreshLeaderboards()
{
	if (Impl == nullptr || State != EHCJEosState::Ready || Leaderboards[0].bLoading || Leaderboards[1].bLoading)
	{
		return;
	}
	// One after the other: the SDK keeps the records of the last query only.
	Leaderboards[0].bLoading = Leaderboards[1].bLoading = true;
	Touch();
	QueryLeaderboard(0);
}

void UHCJEOSSubsystem::QueryLeaderboard(int32 Index)
{
	const FTCHARToUTF8 Id(LeaderboardId(Index));
	EOS_Leaderboards_QueryLeaderboardRanksOptions Options = {};
	Options.ApiVersion = EOS_LEADERBOARDS_QUERYLEADERBOARDRANKS_API_LATEST;
	Options.LeaderboardId = Id.Get();
	Options.LocalUserId = Impl->LocalUser;
	EOS_Leaderboards_QueryLeaderboardRanks(Impl->Leaderboards, &Options, MakeCall<EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo>(this,
		[this, Index](const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo& Info)
		{
			FHCJLeaderboardView& View = Leaderboards[Index];
			View.bLoading = false;
			View.bLoaded = true;
			View.Rows.Reset();
			View.Error.Reset();
			if (Info.ResultCode == EOS_EResult::EOS_Success)
			{
				TArray<FHCJLeaderboardRow> Records;
				EOS_Leaderboards_GetLeaderboardRecordCountOptions CountOptions = {};
				CountOptions.ApiVersion = EOS_LEADERBOARDS_GETLEADERBOARDRECORDCOUNT_API_LATEST;
				const uint32_t Count = EOS_Leaderboards_GetLeaderboardRecordCount(Impl->Leaderboards, &CountOptions);
				for (uint32_t RecordIndex = 0; RecordIndex < Count; ++RecordIndex)
				{
					EOS_Leaderboards_CopyLeaderboardRecordByIndexOptions CopyOptions = {};
					CopyOptions.ApiVersion = EOS_LEADERBOARDS_COPYLEADERBOARDRECORDBYINDEX_API_LATEST;
					CopyOptions.LeaderboardRecordIndex = RecordIndex;
					EOS_Leaderboards_LeaderboardRecord* Record = nullptr;
					if (EOS_Leaderboards_CopyLeaderboardRecordByIndex(Impl->Leaderboards, &CopyOptions, &Record) == EOS_EResult::EOS_Success && Record != nullptr)
					{
						FHCJLeaderboardRow& Row = Records.AddDefaulted_GetRef();
						Row.Rank = static_cast<int32>(Record->Rank);
						Row.Score = Record->Score;
						Row.Name = Record->UserDisplayName != nullptr ? FString(UTF8_TO_TCHAR(Record->UserDisplayName)) : FString();
						Row.bLocal = Record->UserId == Impl->LocalUser;
						if (Row.bLocal && Row.Name.IsEmpty())
						{
							Row.Name = SignedInName;
						}
						EOS_Leaderboards_LeaderboardRecord_Release(Record);
					}
				}
				View.Rows = HCJEos::MakeLeaderboardPage(Records, LeaderboardRows);
				if (View.Rows.Num() == 0)
				{
					View.Error = TEXT("No scores yet");
				}
				UE_LOG(LogHCJ, Log, TEXT("EOS: leaderboard %s: %u record(s), %d shown, local player %s"), *View.Id, Count, View.Rows.Num(),
					View.Rows.ContainsByPredicate([](const FHCJLeaderboardRow& Row) { return Row.bLocal; }) ? TEXT("ranked") : TEXT("not ranked"));
			}
			else
			{
				View.Error = Info.ResultCode == EOS_EResult::EOS_NotFound ? FString(TEXT("No scores yet")) : FString::Printf(TEXT("Not available (%s)"), *ResultText(Info.ResultCode));
				UE_LOG(LogHCJ, Log, TEXT("EOS: leaderboard %s: %s"), *View.Id, *ResultText(Info.ResultCode));
			}
			Touch();
			if (Index + 1 < NumLeaderboards)
			{
				QueryLeaderboard(Index + 1);
			}
		}), &THCJEosCall<EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo>::Thunk);
}
