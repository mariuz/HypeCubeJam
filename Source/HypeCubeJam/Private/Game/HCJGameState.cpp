#include "Game/HCJGameState.h"

#include "Audio/HCJAudioDirector.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/HCJGameMode.h"
#include "Game/HCJPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "HypeCubeJam.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Net/UnrealNetwork.h"
#include "World/HCJUniverse.h"

AHCJGameState::AHCJGameState()
{
	PrimaryActorTick.bCanEverTick = false;
}

AHCJGameState* AHCJGameState::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetGameState<AHCJGameState>() : nullptr;
}

void AHCJGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHCJGameState, MatchConfig);
	DOREPLIFETIME(AHCJGameState, MatchPhase);
	DOREPLIFETIME(AHCJGameState, PhaseEndServerTime);
	DOREPLIFETIME(AHCJGameState, FragLimit);
	DOREPLIFETIME(AHCJGameState, bWeaponPadPointer);
	DOREPLIFETIME(AHCJGameState, MatchWinner);
	DOREPLIFETIME(AHCJGameState, DestroyedAsteroids);
	DOREPLIFETIME(AHCJGameState, DestroyedAsteroidsMatch);
}

void AHCJGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnMatchConfigApplied.Clear();
	if (IsValid(Universe))
	{
		Universe->Destroy();
	}
	Universe = nullptr;

	Super::EndPlay(EndPlayReason);
}

void AHCJGameState::SetMatchConfig(const FHCJMatchConfig& NewConfig)
{
	if (!HasAuthority())
	{
		return;
	}

	// A new match starts with every rock in place.
	DestroyedAsteroids.Reset();
	DestroyedAsteroidsMatch = NewConfig.MatchNumber;
	MatchConfig = NewConfig;
	UE_LOG(LogHCJ, Log, TEXT("Match %d: seed %d, dimension %d, edge probability %.2f"),
		MatchConfig.MatchNumber, MatchConfig.Seed, static_cast<int32>(MatchConfig.Dimension), MatchConfig.EdgeProbability);

	ApplyMatchConfig();
	ForceNetUpdate();
}

const FHCJHypercubeGraph* AHCJGameState::GetGraph() const
{
	return (Universe && Universe->IsBuilt()) ? &Universe->GetGraph() : nullptr;
}

void AHCJGameState::SetMatchPhase(EHCJMatchPhase NewPhase, float DurationSeconds, AHCJPlayerState* Winner)
{
	if (!HasAuthority())
	{
		return;
	}

	MatchPhase = NewPhase;
	PhaseEndServerTime = DurationSeconds > 0.0f ? static_cast<float>(GetServerWorldTimeSeconds()) + DurationSeconds : 0.0f;
	MatchWinner = Winner;
	ForceNetUpdate();
}

float AHCJGameState::GetPhaseTimeRemaining() const
{
	if (PhaseEndServerTime <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, PhaseEndServerTime - static_cast<float>(GetServerWorldTimeSeconds()));
}

void AHCJGameState::SetFragLimit(int32 NewFragLimit)
{
	if (HasAuthority())
	{
		FragLimit = FMath::Max(0, NewFragLimit);
	}
}

void AHCJGameState::SetWeaponPadPointer(bool bOn)
{
	if (HasAuthority() && bWeaponPadPointer != bOn)
	{
		bWeaponPadPointer = bOn;
		ForceNetUpdate();
	}
}

void AHCJGameState::GetSortedPlayerStates(TArray<AHCJPlayerState*>& OutPlayerStates) const
{
	OutPlayerStates.Reset();
	for (APlayerState* PlayerState : PlayerArray)
	{
		AHCJPlayerState* HCJPlayerState = Cast<AHCJPlayerState>(PlayerState);
		if (IsValid(HCJPlayerState) && !HCJPlayerState->IsOnlyASpectator())
		{
			OutPlayerStates.Add(HCJPlayerState);
		}
	}

	OutPlayerStates.Sort([](const AHCJPlayerState& A, const AHCJPlayerState& B)
	{
		if (A.GetKills() != B.GetKills())
		{
			return A.GetKills() > B.GetKills();
		}
		if (A.GetTurretKills() != B.GetTurretKills())
		{
			return A.GetTurretKills() > B.GetTurretKills();
		}
		if (A.GetDeaths() != B.GetDeaths())
		{
			return A.GetDeaths() < B.GetDeaths();
		}
		return A.GetPlayerName() < B.GetPlayerName();
	});
}

void AHCJGameState::ShowCenterMessage(const FString& Text, const FString& SubText, const FLinearColor& Color)
{
	const UWorld* World = GetWorld();
	CenterMessage.Text = Text;
	CenterMessage.SubText = SubText;
	CenterMessage.Color = Color;
	CenterMessage.LocalTime = World ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;
}

void AHCJGameState::MulticastKillEvent_Implementation(const FString& KillerName, const FString& VictimName, const FString& Cause, uint8 ShipTypes)
{
	const UWorld* World = GetWorld();
	const float Now = World ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;

	// Who the local player is (names are what the feed carries).
	const APlayerController* LocalController = (World != nullptr && GEngine != nullptr && World->GetNetMode() != NM_DedicatedServer)
		? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	const APlayerState* LocalState = LocalController != nullptr ? LocalController->PlayerState.Get() : nullptr;
	const FString LocalName = (LocalState != nullptr && !LocalState->IsOnlyASpectator()) ? LocalState->GetPlayerName() : FString();

	FHCJKillFeedEntry& Entry = KillFeed.AddDefaulted_GetRef();
	Entry.KillerName = KillerName;
	Entry.VictimName = VictimName;
	Entry.Cause = Cause;
	Entry.KillerType = ShipTypes & 0x0F;
	Entry.VictimType = ShipTypes >> 4;
	Entry.LocalTime = Now;
	Entry.bLocalKiller = !LocalName.IsEmpty() && KillerName == LocalName;
	Entry.bLocalVictim = !LocalName.IsEmpty() && VictimName == LocalName;

	if (StreakMatchNumber != MatchConfig.MatchNumber)
	{
		StreakMatchNumber = MatchConfig.MatchNumber;
		LocalStreak.Reset();
	}
	if (Entry.bLocalVictim)
	{
		LocalStreak.RegisterDeath();
		ShowCenterMessage(KillerName.IsEmpty() ? FString::Printf(TEXT("LOST TO %s"), Cause.IsEmpty() ? TEXT("THE VOID") : *Cause)
			: FString::Printf(TEXT("KILLED BY %s"), *KillerName.ToUpper()), KillerName.IsEmpty() ? FString() : Cause, FLinearColor(1.0f, 0.25f, 0.15f));
	}
	else if (Entry.bLocalKiller && Cause == TEXT("TURRET DOWN"))
	{
		ShowCenterMessage(TEXT("TURRET DESTROYED"), FString(), FLinearColor(0.85f, 0.95f, 1.0f));
	}
	else if (Entry.bLocalKiller)
	{
		const EHCJStreak Streak = LocalStreak.RegisterKill(Now);
		const TCHAR* Callout = Streak == EHCJStreak::Rampage ? TEXT("RAMPAGE!") : Streak == EHCJStreak::Triple ? TEXT("TRIPLE KILL!")
			: Streak == EHCJStreak::Double ? TEXT("DOUBLE KILL!") : nullptr;
		if (Callout != nullptr)
		{
			ShowCenterMessage(Callout, FString::Printf(TEXT("%s destroyed  -  %d in a row"), *VictimName, LocalStreak.GetLifeStreak()), FLinearColor(1.0f, 0.8f, 0.1f));
			if (UHCJAudioDirector* Audio = UHCJAudioDirector::Get(this))
			{
				Audio->NotifyStreak(static_cast<int32>(Streak));
			}
		}
		else
		{
			ShowCenterMessage(FString::Printf(TEXT("YOU DESTROYED %s"), *VictimName.ToUpper()), Cause, FLinearColor(0.2f, 1.0f, 0.9f));
		}
	}

	if (KillFeed.Num() > MaxKillFeedEntries)
	{
		KillFeed.RemoveAt(0, KillFeed.Num() - MaxKillFeedEntries);
	}

	// Kill / death cue of the local player (cosmetic).
	if (UHCJAudioDirector* Audio = UHCJAudioDirector::Get(this))
	{
		Audio->NotifyKillEvent(KillerName, VictimName);
	}
}

void AHCJGameState::OnRep_MatchConfig()
{
	ApplyMatchConfig();
}

void AHCJGameState::ApplyMatchConfig()
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}

	if (!IsValid(Universe))
	{
		if (!MatchConfig.IsValid())
		{
			return;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transient;
		Universe = World->SpawnActor<AHCJUniverse>(AHCJUniverse::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!Universe)
		{
			UE_LOG(LogHCJ, Error, TEXT("AHCJGameState: could not spawn the universe actor"));
			return;
		}
	}

	Universe->Rebuild(MatchConfig);
	// A late joiner (or a client whose list arrived before the config): the rocks that are already gone, without a show.
	SyncDestroyedAsteroids();
	OnMatchConfigApplied.Broadcast();
}

void AHCJGameState::ServerDestroyAsteroid(uint32 Key, AController* DamageInstigator)
{
	if (!HasAuthority() || !IsValid(Universe) || !Universe->IsBuilt() || DestroyedAsteroids.Contains(Key))
	{
		return;
	}
	FVector Location = FVector::ZeroVector;
	float Radius = 0.0f;
	Universe->GetAsteroidSphere(Key, Location, Radius);
	DestroyedAsteroids.Add(Key);
	DestroyedAsteroidsMatch = MatchConfig.MatchNumber;
	const int32 LiveBefore = Universe->GetNumLiveAsteroids();
	Universe->ApplyAsteroidDestroyed(Key, /*bEffects*/ true);
	ForceNetUpdate();
	UE_LOG(LogHCJ, Log, TEXT("Asteroids (server): %d destroyed, checksum %08X (rock %04X of sector %d, radius %.0f, %d piece(s))"), Universe->GetNumDestroyedAsteroids(),
		Universe->GetDestroyedChecksum(), HCJAsteroids::GetKeyId(Key), HCJAsteroids::GetKeySector(Key), Radius, Universe->GetNumLiveAsteroids() - LiveBefore + 1);
	if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
	{
		GameMode->NotifyAsteroidDestroyed(DamageInstigator, Radius, Universe->GetNumLiveAsteroids() - LiveBefore + 1);
	}
}

void AHCJGameState::OnRep_DestroyedAsteroids()
{
	SyncDestroyedAsteroids();
}

void AHCJGameState::SyncDestroyedAsteroids()
{
	const UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(Universe) || !Universe->IsBuilt() || Universe->GetConfig().MatchNumber != DestroyedAsteroidsMatch
		|| DestroyedAsteroids.Num() == Universe->GetNumDestroyedAsteroids())
	{
		return;
	}
	// What was already gone when this machine built its universe is put in place quietly; what happens later is shown.
	const bool bEffects = World->GetTimeSeconds() - Universe->GetBuildTime() > 1.0;
	const int32 NumChanged = Universe->SyncDestroyedAsteroids(DestroyedAsteroids, bEffects);
	if (NumChanged > 0 && !HasAuthority())
	{
		UE_LOG(LogHCJ, Log, TEXT("Asteroids (client): %d destroyed, checksum %08X (%d new%s)"), Universe->GetNumDestroyedAsteroids(), Universe->GetDestroyedChecksum(),
			NumChanged, bEffects ? TEXT("") : TEXT(", already gone when this machine joined"));
	}
}
