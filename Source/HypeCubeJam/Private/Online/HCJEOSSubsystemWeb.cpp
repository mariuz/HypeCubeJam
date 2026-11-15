#include "Online/HCJEOSSubsystem.h"

// The web build (Wasm): Epic Online Services do not run in a browser (no SDK library, no sockets), so the EOS SDK
// is not linked (HCJ_WITH_EOS=0 in HypeCubeJam.Build.cs) and this file is the whole class: always switched off,
// as a Windows run with -HCJNoEOS. Every request does nothing; the menu hides the online pages on the web.
// Nothing of [HypeCubeJam.EOS] is read here (the section is stripped from the staged web build as well).
#if !HCJ_WITH_EOS

#include "HypeCubeJam.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/NetworkVersion.h"

struct FHCJEosImpl
{
};

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
	return FHCJEosConfig();
}

FString UHCJEOSSubsystem::GetConfiguredPublicAddress()
{
	return FString();
}

TArray<FString> UHCJEOSSubsystem::GetLocalAddresses()
{
	return TArray<FString>();
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
	}
}

FString UHCJEOSSubsystem::GetStatusLine() const
{
	return TEXT("Online play is in the Windows build");
}

void UHCJEOSSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SetState(EHCJEosState::Disabled);
	UE_LOG(LogHCJ, Log, TEXT("EOS: off in the web build (no online services in a browser)"));
}

void UHCJEOSSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UHCJEOSSubsystem::SetDisplayName(const FString& Name)
{
	DisplayName = Name;
}

bool UHCJEOSSubsystem::Tick(float DeltaTime)
{
	return false;
}

void UHCJEOSSubsystem::StartLogin() {}
void UHCJEOSSubsystem::OnLoggedIn() {}
void UHCJEOSSubsystem::OnLoginFailed(const FString& Why) {}

bool UHCJEOSSubsystem::IsAdvertising() const
{
	return false;
}

void UHCJEOSSubsystem::UpdateHosting(bool bHosting, const FHCJLobbyInfo& Info) {}
void UHCJEOSSubsystem::CreateLobby() {}
void UHCJEOSSubsystem::PushLobbyAttributes() {}
void UHCJEOSSubsystem::DestroyLobby() {}
void UHCJEOSSubsystem::RefreshLobbies() {}
void UHCJEOSSubsystem::ObserveLocalStats(const FHCJStatSample& Sample) {}
void UHCJEOSSubsystem::DemoSendStats() {}
void UHCJEOSSubsystem::FlushStats(bool bScoreLineGone) {}
void UHCJEOSSubsystem::IngestPending() {}
void UHCJEOSSubsystem::RefreshLeaderboards() {}
void UHCJEOSSubsystem::RefreshPilotRecord() {}
void UHCJEOSSubsystem::CountOpenMatches(bool bFailed) {}

int32 UHCJEOSSubsystem::GetOpenMatches() const
{
	return -1;
}

FString UHCJEOSSubsystem::GetPilotRecordLine() const
{
	return FString();
}

FString UHCJEOSSubsystem::GetTitleStatusLine() const
{
	return GetStatusLine();
}
void UHCJEOSSubsystem::QueryLeaderboard(int32 Index) {}

#endif // !HCJ_WITH_EOS
