#include "Game/HCJGameMode.h"

#include "AI/HCJBotController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/HCJBalance.h"
#include "Game/HCJGameState.h"
#include "Game/HCJGameplaySettings.h"
#include "Game/HCJHUD.h"
#include "Game/HCJPlayerController.h"
#include "Game/HCJPlayerState.h"
#include "Game/HCJUiLogic.h"
#include "GameFramework/GameSession.h"
#include "HypeCubeJam.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Ship/HCJProjectile.h"
#include "Ship/HCJShipCharacter.h"
#include "World/HCJPickup.h"
#include "World/HCJTurret.h"
#include "TimerManager.h"
#include "World/HCJUniverse.h"

AHCJGameMode::AHCJGameMode()
{
	DefaultPawnClass = AHCJShipCharacter::StaticClass();
	PlayerControllerClass = AHCJPlayerController::StaticClass();
	PlayerStateClass = AHCJPlayerState::StaticClass();
	GameStateClass = AHCJGameState::StaticClass();
	HUDClass = AHCJHUD::StaticClass();
	BotControllerClass = AHCJBotController::StaticClass();

	bStartPlayersAsSpectators = false;

	// Only to measure the server's frames for the playthrough tests (Tick).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AHCJGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Frames of the running match, without its first seconds (the world is still being built then).
	const UWorld* World = GetWorld();
	if (World != nullptr && World->GetTimeSeconds() - MatchStats.StartTime > 5.0f)
	{
		++MatchStats.Ticks;
		MatchStats.TickSeconds += DeltaSeconds;
		MatchStats.MaxTickSeconds = FMath::Max(MatchStats.MaxTickSeconds, DeltaSeconds);
		MatchStats.SlowTicks += DeltaSeconds > 0.25f ? 1 : 0;
	}
}

void AHCJGameMode::NotifyTorpedoThroughOpenFacing(const AHCJShipCharacter* Victim)
{
	LastOpenTorpedoVictim = Victim;
	LastOpenTorpedoTime = GetWorld() != nullptr ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.0f;
}

AHCJGameMode::FPlaythroughSnapshot AHCJGameMode::GetPlaythroughSnapshot() const
{
	FPlaythroughSnapshot Snapshot;
	Snapshot.MatchNumber = CurrentConfig.MatchNumber;
	Snapshot.Seed = CurrentConfig.Seed;
	const AHCJUniverse* Universe = GetUniverse();
	Snapshot.LayoutChecksum = Universe != nullptr ? Universe->GetLayoutChecksum() : 0;
	Snapshot.Kills = MatchStats.Kills;
	Snapshot.Deaths = MatchStats.Kills + MatchStats.TurretDeaths + MatchStats.UncreditedDeaths;
	Snapshot.PortalJumps = MatchStats.PortalJumps;
	Snapshot.WormholeJumps = MatchStats.WormholeJumps;
	for (const int32 Taken : MatchStats.Pickups)
	{
		Snapshot.PickupsTaken += Taken;
	}
	Snapshot.Shots = MatchStats.Shots;
	Snapshot.StuckEvents = MatchStats.StuckEvents;
	Snapshot.OutOfBoundsEvents = MatchStats.OutOfBoundsEvents;
	Snapshot.NaNEvents = MatchStats.NaNEvents;
	Snapshot.MatchesEnded = NumMatchesEnded;
	Snapshot.LastWinner = LastWinnerName;
	Snapshot.LastWinnerKills = LastWinnerKills;
	Snapshot.LastBoardRows = LastBoardRows;
	return Snapshot;
}

namespace
{
	/** A name as one token of a "Playthrough:" line. */
	FString PlaythroughToken(const FString& Text)
	{
		FString Token = Text;
		Token.ReplaceInline(TEXT(" "), TEXT("_"));
		Token.ReplaceInline(TEXT("="), TEXT("_"));
		return Token.IsEmpty() ? FString(TEXT("-")) : Token;
	}
}

void AHCJGameMode::LogPlaythroughMatchStart() const
{
	const AHCJUniverse* Universe = GetUniverse();
	UE_LOG(LogHCJ, Log, TEXT("Playthrough: event=match_start match=%d seed=%d layout=%08X dim=%d difficulty=%d frag_limit=%d time_limit=%.0f bots=%d humans=%d rocks=%d"),
		CurrentConfig.MatchNumber, CurrentConfig.Seed, Universe != nullptr ? Universe->GetLayoutChecksum() : 0, CurrentConfig.Dimension, BotDifficulty, FragLimit, TimeLimitSeconds,
		GetDesiredBotCount(), GetNumHumanPlayers(), Universe != nullptr ? Universe->GetNumLiveAsteroids() : 0);
}

void AHCJGameMode::LogPlaythroughStats() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const AHCJUniverse* Universe = GetUniverse();
	FString Line = FString::Printf(TEXT("event=stats match=%d t=%.0f match_t=%.0f kills=%d deaths=%d shots=%d hits=%d portal_jumps=%d wormhole_jumps=%d turret_deaths=%d turrets_destroyed=%d turret_shots=%d black_hole_deaths=%d uncredited_deaths=%d"),
		CurrentConfig.MatchNumber, World->GetTimeSeconds(), World->GetTimeSeconds() - MatchStats.StartTime, MatchStats.Kills,
		MatchStats.Kills + MatchStats.TurretDeaths + MatchStats.UncreditedDeaths, MatchStats.Shots, MatchStats.Hits, MatchStats.PortalJumps, MatchStats.WormholeJumps,
		MatchStats.TurretDeaths, MatchStats.TurretsDestroyed, MatchStats.TurretShots, MatchStats.BlackHoleDeaths, MatchStats.UncreditedDeaths);
	const TCHAR* WeaponKeys[] = { TEXT("blaster"), TEXT("scatter"), TEXT("rail"), TEXT("missiles"), TEXT("pulse"), TEXT("torpedo"), TEXT("phaser") };
	for (int32 Index = 0; Index < static_cast<int32>(EHCJWeapon::Count); ++Index)
	{
		Line += FString::Printf(TEXT(" kills_%s=%d"), Index < UE_ARRAY_COUNT(WeaponKeys) ? WeaponKeys[Index] : *FString::Printf(TEXT("weapon%d"), Index), MatchStats.KillsByWeapon[Index]);
	}
	for (int32 Type = 0; Type < HCJShipTypes::NumTypes; ++Type)
	{
		const FString Short = FString(HCJShipTypes::Get(Type).ShortName).ToLower();
		Line += FString::Printf(TEXT(" kills_%s=%d deaths_%s=%d"), *Short, MatchStats.KillsByType[Type], *Short, MatchStats.DeathsByType[Type]);
	}
	int32 PickupsTaken = 0;
	for (int32 Index = 0; Index < static_cast<int32>(EHCJPickupItem::Count); ++Index)
	{
		PickupsTaken += MatchStats.Pickups[Index];
		Line += FString::Printf(TEXT(" pickup_%s=%d"), *PlaythroughToken(FString(HCJ::GetItemName(static_cast<EHCJPickupItem>(Index))).ToLower()), MatchStats.Pickups[Index]);
	}
	Line += FString::Printf(TEXT(" pickups=%d rocks_destroyed=%d rocks_by_bots=%d rocks_alive=%d destroyed_count=%d destroyed_checksum=%08X warps=%d cloaks=%d tractor_holds=%d torpedoes_fired=%d torpedo_open_hits=%d torpedo_open_kills=%d facing_turns=%d"),
		PickupsTaken, MatchStats.RocksDestroyed, MatchStats.RocksDestroyedByBots, Universe != nullptr ? Universe->GetNumLiveAsteroids() : 0,
		Universe != nullptr ? Universe->GetNumDestroyedAsteroids() : 0, Universe != nullptr ? Universe->GetDestroyedChecksum() : 0, MatchStats.Warps, MatchStats.Cloaks,
		MatchStats.TractorHolds, MatchStats.TorpedoesFired, MatchStats.TorpedoHitsOpen, MatchStats.TorpedoOpenFacingKills, MatchStats.BotShieldTurns);
	for (int32 Index = 0; Index < static_cast<int32>(EHCJBotTactic::Count); ++Index)
	{
		Line += FString::Printf(TEXT(" %s=%d"), HCJBalance::GetTacticKey(static_cast<EHCJBotTactic>(Index)), MatchStats.BotTactics[Index]);
	}
	Line += FString::Printf(TEXT(" stuck=%d out_of_bounds=%d nan=%d ticks=%d tick_mean_ms=%.1f tick_max_ms=%.0f slow_ticks=%d layout=%08X humans=%d bots=%d"),
		MatchStats.StuckEvents, MatchStats.OutOfBoundsEvents, MatchStats.NaNEvents, MatchStats.Ticks,
		MatchStats.Ticks > 0 ? 1000.0 * MatchStats.TickSeconds / MatchStats.Ticks : 0.0, MatchStats.MaxTickSeconds * 1000.0f, MatchStats.SlowTicks,
		Universe != nullptr ? Universe->GetLayoutChecksum() : 0, GetNumHumanPlayers(), Bots.Num());
	UE_LOG(LogHCJ, Log, TEXT("Playthrough: %s"), *Line);

	// The humans' score lines (the tests check that a client that joined gets kills and deaths).
	if (const AHCJGameState* HCJGameState = GetHCJGameState())
	{
		for (const APlayerState* PlayerState : HCJGameState->PlayerArray)
		{
			const AHCJPlayerState* HCJPlayerState = Cast<AHCJPlayerState>(PlayerState);
			if (HCJPlayerState != nullptr && !HCJPlayerState->IsABot())
			{
				UE_LOG(LogHCJ, Log, TEXT("Playthrough: event=human match=%d t=%.0f name=%s kills=%d deaths=%d type=%s spectator=%d"), CurrentConfig.MatchNumber, World->GetTimeSeconds(),
					*PlaythroughToken(HCJPlayerState->GetPlayerName()), HCJPlayerState->GetKills(), HCJPlayerState->GetDeaths(),
					*FString(HCJShipTypes::Get(HCJPlayerState->GetShipType()).ShortName).ToLower(), HCJPlayerState->IsOnlyASpectator() ? 1 : 0);
			}
		}
	}
}

void AHCJGameMode::UpdatePlaythroughWatch()
{
	UWorld* World = GetWorld();
	const AHCJUniverse* Universe = GetUniverse();
	const AHCJGameState* HCJGameState = GetHCJGameState();
	if (World == nullptr || Universe == nullptr || !Universe->IsBuilt() || HCJGameState == nullptr || World->GetTimeSeconds() < NextWatchTime)
	{
		return;
	}
	const float Now = static_cast<float>(World->GetTimeSeconds());
	NextWatchTime = Now + 1.0f;
	if (HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing)
	{
		ShipWatches.Reset();
		return;
	}
	// A ship that has not moved this far in this long is stuck (a bot waiting in front of a portal moves on within a few seconds).
	constexpr float StuckSeconds = 20.0f;
	constexpr double StuckDistance = 250.0;
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		AHCJShipCharacter* Ship = *It;
		AController* Controller = IsValid(Ship) ? Ship->GetController() : nullptr;
		if (Controller == nullptr || Ship->IsDead())
		{
			continue;
		}
		const FVector Location = Ship->GetActorLocation();
		FShipWatch& Watch = ShipWatches.FindOrAdd(Controller);
		if (Location.ContainsNaN() || Ship->GetVelocity().ContainsNaN())
		{
			++MatchStats.NaNEvents;
			UE_LOG(LogHCJ, Warning, TEXT("Playthrough: event=nan name=%s t=%.0f"), *PlaythroughToken(GetNameSafe(Ship)), Now);
			continue;
		}
		if (Watch.AnchorTime <= 0.0f || FVector::DistSquared(Location, Watch.Anchor) > FMath::Square(StuckDistance))
		{
			Watch.Anchor = Location;
			Watch.AnchorTime = Now;
			Watch.bStuckReported = false;
		}
		else if (!Watch.bStuckReported && Now - Watch.AnchorTime > StuckSeconds && Cast<AHCJBotController>(Controller) != nullptr)
		{
			// (Only bots: a human may leave the ship alone for as long as it likes.)
			Watch.bStuckReported = true;
			++MatchStats.StuckEvents;
			const AHCJBotController* Bot = CastChecked<AHCJBotController>(Controller);
			UE_LOG(LogHCJ, Warning, TEXT("Playthrough: event=stuck name=%s t=%.0f seconds=%.0f sector=%d x=%.0f y=%.0f z=%.0f state=%d speed=%.0f %s"),
				*PlaythroughToken(Ship->GetPlayerState() != nullptr ? Ship->GetPlayerState()->GetPlayerName() : GetNameSafe(Ship)), Now, Now - Watch.AnchorTime, Ship->GetCurrentSector(),
				Location.X, Location.Y, Location.Z, static_cast<int32>(Bot->GetBotState()), Ship->GetVelocity().Size(), *Bot->DescribeForLog());
		}
		// Alive outside every sector, or beyond the radius at which a ship is destroyed, for more than a moment.
		const int32 Sector = Universe->GetSectorAtLocation(Location);
		const bool bOutside = Sector == INDEX_NONE || FVector::DistSquared(Location, Universe->GetSectorOrigin(Sector)) > FMath::Square(static_cast<double>(HCJ::SectorKillRadius) + 1500.0);
		if (!bOutside)
		{
			Watch.OutsideSince = -1.0f;
		}
		else if (Watch.OutsideSince < 0.0f)
		{
			Watch.OutsideSince = Now;
		}
		else if (Now - Watch.OutsideSince > 3.0f)
		{
			Watch.OutsideSince = Now + 1.0e6f;
			++MatchStats.OutOfBoundsEvents;
			UE_LOG(LogHCJ, Warning, TEXT("Playthrough: event=out_of_bounds name=%s t=%.0f sector=%d x=%.0f y=%.0f z=%.0f"), *PlaythroughToken(GetNameSafe(Ship)), Now, Sector, Location.X, Location.Y, Location.Z);
		}
	}
	for (auto It = ShipWatches.CreateIterator(); It; ++It)
	{
		const AController* Controller = It.Key().Get();
		const AHCJShipCharacter* Ship = Controller != nullptr ? Cast<AHCJShipCharacter>(Controller->GetPawn()) : nullptr;
		if (Ship == nullptr || Ship->IsDead())
		{
			It.RemoveCurrent();
		}
	}
}

void AHCJGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	GameplayRandom.Initialize(MakeRandomSeed());

	bHasSeedOverride = UGameplayStatics::HasOption(Options, TEXT("Seed"));
	SeedOverride = bHasSeedOverride ? UGameplayStatics::GetIntOption(Options, TEXT("Seed"), 0) : 0;

	MatchDimension = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Dim"), DefaultDimension),
		FHCJHypercubeGraph::MinDimension, FHCJHypercubeGraph::MaxDimension);

	MatchEdgeProbability = DefaultEdgeProbability;
	if (UGameplayStatics::HasOption(Options, TEXT("EdgeP")))
	{
		MatchEdgeProbability = FCString::Atof(*UGameplayStatics::ParseOption(Options, TEXT("EdgeP")));
	}
	MatchEdgeProbability = FMath::Clamp(MatchEdgeProbability, 0.0f, 1.0f);

	MatchBlackHoles.bEnabled = UGameplayStatics::GetIntOption(Options, TEXT("BlackHoles"), bBlackHoles ? 1 : 0) != 0;
	MatchBlackHoles.SectorFraction = BlackHoleSectorFraction;
	if (UGameplayStatics::HasOption(Options, TEXT("BlackHoleP")))
	{
		MatchBlackHoles.SectorFraction = FCString::Atof(*UGameplayStatics::ParseOption(Options, TEXT("BlackHoleP")));
	}
	MatchBlackHoles.SectorFraction = FMath::Clamp(MatchBlackHoles.SectorFraction, 0.0f, 1.0f);
	MatchBlackHoles.InfluenceRadius = FMath::Clamp(BlackHoleInfluenceRadius, 1000.0f, 12000.0f);
	MatchBlackHoles.HorizonRadius = FMath::Clamp(BlackHoleHorizonRadius, 100.0f, 0.5f * MatchBlackHoles.InfluenceRadius);
	MatchBlackHoles.PullSpeed = FMath::Max(BlackHolePullSpeed, 0.0f);
	MatchBlackHoles.MaxPullSpeed = FMath::Max(BlackHoleMaxPullSpeed, 0.0f);

	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	MatchPickups.bEnabled = UGameplayStatics::GetIntOption(Options, TEXT("Pickups"), Gameplay.bPickups ? 1 : 0) != 0;
	MatchPickups.MinPads = static_cast<uint8>(FMath::Clamp(FMath::Min(Gameplay.MinPadsPerSector, Gameplay.MaxPadsPerSector), 0, 8));
	MatchPickups.MaxPads = static_cast<uint8>(FMath::Clamp(FMath::Max(Gameplay.MinPadsPerSector, Gameplay.MaxPadsPerSector), 0, 8));
	MatchPickups.OverdriveSpeedMultiplier = FMath::Clamp(Gameplay.OverdriveSpeedMultiplier, 1.0f, 3.0f);
	MatchPickups.OverdriveAccelerationMultiplier = FMath::Clamp(Gameplay.OverdriveAccelerationMultiplier, 1.0f, 3.0f);

	MatchStations.bEnabled = UGameplayStatics::GetIntOption(Options, TEXT("Stations"), bStations ? 1 : 0) != 0;
	MatchStations.SectorFraction = FMath::Clamp(StationSectorFraction, 0.0f, 1.0f);

	// Destroyable asteroids: the rules every machine breaks rocks by (URL: ?Rocks=0|1; -HCJNoRockDamage for measurements).
	MatchAsteroids.bEnabled = UGameplayStatics::GetIntOption(Options, TEXT("Rocks"), Gameplay.bDestroyableAsteroids ? 1 : 0) != 0
		&& !FParse::Param(FCommandLine::Get(), TEXT("HCJNoRockDamage"));
	MatchAsteroids.SplitMinRadius = FMath::Max(Gameplay.AsteroidSplitMinRadius, 50.0f);
	MatchAsteroids.HealthPerRadius = FMath::Max(Gameplay.AsteroidHealthPerRadius, 0.001f);
	MatchAsteroids.MinHealth = FMath::Max(Gameplay.AsteroidMinHealth, 1.0f);
	MatchAsteroids.MaxHealth = FMath::Max(Gameplay.AsteroidMaxHealth, MatchAsteroids.MinHealth);

	MatchWormholes.bEnabled = bWormholes;
	MatchWormholes.Count = WormholeCount;
	if (UGameplayStatics::HasOption(Options, TEXT("Wormholes")))
	{
		const int32 Requested = UGameplayStatics::GetIntOption(Options, TEXT("Wormholes"), 0);
		MatchWormholes.bEnabled = Requested > 0;
		MatchWormholes.Count = Requested;
	}

	MinCombatants = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("Bots"), MinCombatants));
	MinBots = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("MinBots"), MinBots));
	BotDifficulty = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Difficulty"), BotDifficulty), 0, 2);
	MaxBots = FMath::Max(0, MaxBots);
	FragLimit = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("FragLimit"), FragLimit));
	if (UGameplayStatics::HasOption(Options, TEXT("TimeLimit")))
	{
		TimeLimitSeconds = FCString::Atof(*UGameplayStatics::ParseOption(Options, TEXT("TimeLimit")));
	}
	TimeLimitSeconds = FMath::Max(0.0f, TimeLimitSeconds);

	NextBotNumber = 1;
	CurrentConfig = MakeMatchConfig(1);

	UE_LOG(LogHCJ, Log, TEXT("%s InitGame on %s, options '%s': min combatants %d, min bots %d, max bots %d, frag limit %d, time limit %.0f s"),
		*GetClass()->GetName(), *MapName, *Options, MinCombatants, MinBots, MaxBots, FragLimit, TimeLimitSeconds);
}

void AHCJGameMode::InitGameState()
{
	Super::InitGameState();

	if (AHCJGameState* HCJGameState = GetHCJGameState())
	{
		HCJGameState->SetMatchConfig(CurrentConfig);
		HCJGameState->SetFragLimit(FragLimit);
		HCJGameState->SetMatchPhase(EHCJMatchPhase::Playing, TimeLimitSeconds, nullptr);
		RebuildPickups();
		RebuildStations();
		LogPlaythroughMatchStart();
	}
	else
	{
		UE_LOG(LogHCJ, Error, TEXT("AHCJGameMode requires AHCJGameState (GameStateClass is %s)"), *GetNameSafe(GameStateClass.Get()));
	}
}

void AHCJGameMode::StartPlay()
{
	Super::StartPlay();

	GetWorldTimerManager().SetTimer(GameUpdateTimerHandle, this, &AHCJGameMode::GameUpdate, GameUpdateInterval, true);
	UpdateBotCount();
}

void AHCJGameMode::PostLogin(APlayerController* NewPlayer)
{
	// The host of a game started without arguments opens on the title menu: no ship until it presses Play.
	// (-HCJBotsOnly: the same state for a headless bot match without any network - the local player has no ship, the bots play.)
	static const bool bBotsOnly = FParse::Param(FCommandLine::Get(), TEXT("HCJBotsOnly"));
	if (AHCJPlayerController* Host = Cast<AHCJPlayerController>(NewPlayer); Host != nullptr && Host->IsLocalController() && (bBotsOnly || HCJMenuFlow::ShouldOpenMenu()))
	{
		Host->EnterStartMenu();
		UE_LOG(LogHCJ, Log, TEXT("Menu: the local player starts in the title menu (the bots play in the background)"));
	}

	Super::PostLogin(NewPlayer);
	UpdateBotCount();

	UE_LOG(LogHCJ, Log, TEXT("Player joined: %s (%s) -> %d humans, %d bots"),
		NewPlayer && NewPlayer->PlayerState ? *NewPlayer->PlayerState->GetPlayerName() : TEXT("?"),
		NewPlayer && NewPlayer->IsLocalController() ? TEXT("local") : TEXT("remote"), GetNumHumanPlayers(), Bots.Num());
}

void AHCJGameMode::ChangeName(AController* Controller, const FString& NewName, bool bNameChange)
{
	FString Name = NewName;
	if (Controller != nullptr && GameState != nullptr && !NewName.IsEmpty())
	{
		for (int32 Suffix = 2; Suffix < 100; ++Suffix)
		{
			const bool bTaken = GameState->PlayerArray.ContainsByPredicate([Controller, &Name](const APlayerState* Other)
			{
				return Other != nullptr && Other != Controller->PlayerState && Other->GetPlayerName().Equals(Name, ESearchCase::IgnoreCase);
			});
			if (!bTaken)
			{
				break;
			}
			Name = FString::Printf(TEXT("%s-%d"), *NewName.Left(17), Suffix);
		}
		if (Name != NewName)
		{
			UE_LOG(LogHCJ, Log, TEXT("Player name '%s' is taken: this one is '%s'"), *NewName, *Name);
		}
	}
	Super::ChangeName(Controller, Name, bNameChange);
}

void AHCJGameMode::Logout(AController* Exiting)
{
	CancelRespawn(Exiting);

	if (AHCJBotController* Bot = Cast<AHCJBotController>(Exiting))
	{
		Bots.Remove(Bot);
	}
	else if (const UWorld* World = GetWorld(); World && !World->bIsTearingDown)
	{
		UE_LOG(LogHCJ, Log, TEXT("Player left: %s"), Exiting && Exiting->PlayerState ? *Exiting->PlayerState->GetPlayerName() : TEXT("?"));
		// The leaving controller is still registered with the world here; refill once it is gone.
		GetWorldTimerManager().SetTimerForNextTick(this, &AHCJGameMode::UpdateBotCount);
	}

	Super::Logout(Exiting);
}

void AHCJGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!IsValid(NewPlayer) || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	const AHCJGameState* HCJGameState = GetHCJGameState();
	const AHCJUniverse* Universe = GetUniverse();
	if (!HCJGameState || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing || !Universe || !Universe->IsBuilt())
	{
		return;
	}
	if (HasLiveShip(NewPlayer) || MustSpectate(Cast<APlayerController>(NewPlayer)))
	{
		return;
	}

	// A dead ship that is still attached would be reused by RestartPlayerAtTransform.
	if (APawn* OldPawn = NewPlayer->GetPawn())
	{
		NewPlayer->UnPossess();
		OldPawn->Destroy();
	}

	FTransform SpawnTransform;
	const int32 Sector = ChooseSpawn(NewPlayer, SpawnTransform);
	if (Sector == INDEX_NONE)
	{
		return;
	}

	RestartPlayerAtTransform(NewPlayer, SpawnTransform);

	if (HasLiveShip(NewPlayer))
	{
		CancelRespawn(NewPlayer);
		if (AHCJPlayerState* HCJPlayerState = NewPlayer->GetPlayerState<AHCJPlayerState>())
		{
			// The ship its player chose (the hangar; bots: SpawnBot).
			if (AHCJShipCharacter* NewShip = Cast<AHCJShipCharacter>(NewPlayer->GetPawn()))
			{
				NewShip->ServerInitShipType(HCJPlayerState->GetShipType(), HCJPlayerState->GetShipVariant(), HCJPlayerState->GetShipSkin());
			}
			HCJPlayerState->SetRespawnTimes(0.0f, 0.0f);
			HCJPlayerState->SetCurrentSector(Sector);
		}
	}
}

bool AHCJGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	if (!Super::PlayerCanRestart_Implementation(Player))
	{
		return false;
	}

	const AHCJGameState* HCJGameState = GetHCJGameState();
	if (!HCJGameState || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing)
	{
		return false;
	}

	const AHCJPlayerState* HCJPlayerState = Player->GetPlayerState<AHCJPlayerState>();
	return !HCJPlayerState || HCJPlayerState->GetEarliestRespawnServerTime() <= static_cast<float>(HCJGameState->GetServerWorldTimeSeconds());
}

void AHCJGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FTimerManager& TimerManager = GetWorldTimerManager();
	TimerManager.ClearTimer(GameUpdateTimerHandle);
	TimerManager.ClearTimer(IntermissionTimerHandle);
	TimerManager.ClearAllTimersForObject(this);
	RespawnTimers.Reset();

	Super::EndPlay(EndPlayReason);
}

void AHCJGameMode::RebuildPickups()
{
	UWorld* World = GetWorld();
	const AHCJUniverse* Universe = GetUniverse();
	if (World == nullptr)
	{
		return;
	}

	TArray<AHCJPickup*> Old;
	for (TActorIterator<AHCJPickup> It(World); It; ++It)
	{
		Old.Add(*It);
	}
	for (AHCJPickup* Pickup : Old)
	{
		Pickup->Destroy();
	}
	if (Universe == nullptr || !Universe->IsBuilt())
	{
		return;
	}

	int32 NumSpawned = 0;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	for (int32 Sector = 0; Sector < Universe->NumSectors(); ++Sector)
	{
		const FVector Origin = Universe->GetSectorOrigin(Sector);
		for (const FVector& Pad : Universe->GetPickupPads(Sector))
		{
			if (AHCJPickup* Pickup = World->SpawnActor<AHCJPickup>(AHCJPickup::StaticClass(), Origin + Pad, FRotator::ZeroRotator, SpawnParameters))
			{
				Pickup->ServerFill();
				++NumSpawned;
			}
		}
	}
	UE_LOG(LogHCJ, Log, TEXT("Pickups: %d pads filled (pad checksum %08X)"), NumSpawned, Universe->GetPickupPadChecksum());
}

void AHCJGameMode::RebuildStations()
{
	UWorld* World = GetWorld();
	const AHCJUniverse* Universe = GetUniverse();
	if (World == nullptr)
	{
		return;
	}

	TArray<AHCJTurret*> Old;
	for (TActorIterator<AHCJTurret> It(World); It; ++It)
	{
		Old.Add(*It);
	}
	for (AHCJTurret* Turret : Old)
	{
		Turret->Destroy();
	}
	if (Universe == nullptr || !Universe->IsBuilt())
	{
		return;
	}

	int32 NumSpawned = 0;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	for (int32 Sector = 0; Sector < Universe->NumSectors(); ++Sector)
	{
		if (!Universe->HasStation(Sector))
		{
			continue;
		}
		const FVector Center = Universe->GetStationLocation(Sector);
		for (const FHCJTurretMount& Mount : Universe->GetStation(Sector).Turrets)
		{
			// Deferred: the mount direction must be there before BeginPlay (and before the first replication).
			AHCJTurret* Turret = World->SpawnActorDeferred<AHCJTurret>(AHCJTurret::StaticClass(), FTransform(Center + Mount.Location), nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Turret != nullptr)
			{
				Turret->ServerInit(Mount.Up);
				Turret->FinishSpawning(FTransform(Center + Mount.Location));
				++NumSpawned;
			}
		}
	}
	UE_LOG(LogHCJ, Log, TEXT("Stations: %d turrets placed (station checksum %08X)"), NumSpawned, Universe->GetStationChecksum());
}

void AHCJGameMode::NotifyTurretDestroyed(AController* Killer)
{
	AHCJGameState* HCJGameState = GetHCJGameState();
	if (HCJGameState == nullptr || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing)
	{
		return;
	}
	++MatchStats.TurretsDestroyed;
	FString KillerName;
	if (AHCJPlayerState* KillerState = Killer != nullptr ? Killer->GetPlayerState<AHCJPlayerState>() : nullptr)
	{
		KillerState->AddTurretKill();
		KillerName = KillerState->GetPlayerName();
	}
	UE_LOG(LogHCJ, Log, TEXT("Turret destroyed by %s"), KillerName.IsEmpty() ? TEXT("nobody") : *KillerName);
	if (!KillerName.IsEmpty())
	{
		const AHCJPlayerState* KillerState = Killer != nullptr ? Killer->GetPlayerState<AHCJPlayerState>() : nullptr;
		HCJGameState->MulticastKillEvent(KillerName, TEXT("a station turret"), TEXT("TURRET DOWN"), AHCJGameState::PackKillTypes(KillerState != nullptr ? KillerState->GetShipType() : -1, -1));
	}
}

EHCJPickupItem AHCJGameMode::ChoosePickupItem()
{
	return UHCJGameplaySettings::Get().PickItem(GameplayRandom.GetFraction());
}

float AHCJGameMode::RandomPickupDelay()
{
	const UHCJGameplaySettings& Gameplay = UHCJGameplaySettings::Get();
	const float Min = FMath::Min(Gameplay.RespawnMinSeconds, Gameplay.RespawnMaxSeconds);
	const float Max = FMath::Max(Gameplay.RespawnMinSeconds, Gameplay.RespawnMaxSeconds);
	return GameplayRandom.FRandRange(Min, Max);
}

void AHCJGameMode::NotifyPickupCollected(EHCJPickupItem Item, AController* Collector)
{
	if (Item < EHCJPickupItem::Count)
	{
		++MatchStats.Pickups[static_cast<int32>(Item)];
	}
	if (Cast<AHCJBotController>(Collector) != nullptr)
	{
		++MatchStats.BotPickups;
	}
	UE_LOG(LogHCJ, Verbose, TEXT("Pickup: %s took %s"), Collector && Collector->PlayerState ? *Collector->PlayerState->GetPlayerName() : TEXT("?"), HCJ::GetItemName(Item));
	if (AHCJPlayerController* Player = Cast<AHCJPlayerController>(Collector))
	{
		Player->ClientNotifyPickup(static_cast<uint8>(Item));
	}
}

void AHCJGameMode::NotifyShipKilled(AController* VictimController, AController* KillerController, AHCJShipCharacter* VictimShip, AActor* Causer)
{
	AHCJGameState* HCJGameState = GetHCJGameState();
	if (!VictimController || !HCJGameState || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing)
	{
		return;
	}

	FString VictimName;
	if (AHCJPlayerState* VictimState = VictimController->GetPlayerState<AHCJPlayerState>())
	{
		VictimState->AddDeath();
		VictimState->SetCurrentSector(INDEX_NONE);
		VictimName = VictimState->GetPlayerName();
	}

	// Ship types of the two, for the kill feed and the per-type balance numbers.
	const int32 VictimType = VictimShip != nullptr ? static_cast<int32>(VictimShip->GetShipType()) : -1;
	int32 KillerType = -1;
	if (VictimType >= 0 && VictimType < HCJShipTypes::NumTypes)
	{
		++MatchStats.DeathsByType[VictimType];
	}

	FString KillerName;
	if (KillerController && KillerController != VictimController)
	{
		if (AHCJPlayerState* KillerState = KillerController->GetPlayerState<AHCJPlayerState>())
		{
			KillerState->AddKill();
			KillerName = KillerState->GetPlayerName();
			const AHCJShipCharacter* KillerShip = Cast<AHCJShipCharacter>(KillerController->GetPawn());
			KillerType = KillerShip != nullptr ? static_cast<int32>(KillerShip->GetShipType()) : static_cast<int32>(KillerState->GetShipType());
			if (KillerType >= 0 && KillerType < HCJShipTypes::NumTypes)
			{
				++MatchStats.KillsByType[KillerType];
			}
		}
	}

	FString Cause;
	if (KillerName.IsEmpty() && Causer != nullptr && Cast<AHCJTurret>(Causer->GetOwner()) != nullptr)
	{
		// Shot down by a station: an environment death with a name on it.
		++MatchStats.TurretDeaths;
		KillerName = TEXT("Station");
		Cause = TEXT("TURRET");
		UE_LOG(LogHCJ, Log, TEXT("Death: %s (station turret)"), *VictimName);
	}
	else if (KillerName.IsEmpty())
	{
		++MatchStats.UncreditedDeaths;
		const AHCJUniverse* Universe = GetUniverse();
		const AHCJProjectile* OwnProjectile = Cast<AHCJProjectile>(Causer);
		Cause = (OwnProjectile != nullptr && OwnProjectile->IsTorpedo()) ? TEXT("OWN TORPEDO")
			: (VictimShip != nullptr && Universe != nullptr && Universe->IsInsideEventHorizon(VictimShip->GetActorLocation())) ? TEXT("BLACK HOLE") : TEXT("THE VOID");
		UE_LOG(LogHCJ, Log, TEXT("Death: %s (no killer)"), *VictimName);
	}
	else if (KillerName != TEXT("Station") || (KillerController && KillerController != VictimController))
	{
		++MatchStats.Kills;
		if (const float EngagedFor = VictimShip != nullptr ? VictimShip->GetEngagementSeconds() : -1.0f; EngagedFor >= 0.0f)
		{
			MatchStats.TimeToKillSum += EngagedFor;
			++MatchStats.TimeToKillCount;
		}
		const AHCJProjectile* Projectile = Cast<AHCJProjectile>(Causer);
		// The causer of a phaser kill is the ship that held the beam on the victim.
		const bool bPhaserKill = Projectile == nullptr && Cast<AHCJShipCharacter>(Causer) != nullptr && Causer != VictimShip;
		const EHCJWeapon KillWeapon = Projectile != nullptr ? Projectile->GetWeapon() : bPhaserKill ? EHCJWeapon::Phaser : EHCJWeapon::Blaster;
		++MatchStats.KillsByWeapon[static_cast<int32>(KillWeapon < EHCJWeapon::Count ? KillWeapon : EHCJWeapon::Blaster)];
		if (Projectile != nullptr && Projectile->IsTorpedo() && LastOpenTorpedoVictim.Get() == VictimShip && GetWorld()->GetTimeSeconds() - LastOpenTorpedoTime < 0.05)
		{
			// The torpedo went through a facing that was down and that was the end of the ship.
			++MatchStats.TorpedoOpenFacingKills;
		}
		// A kill credited without a bolt of the killer: the victim was shot and then finished off by the environment.
		Cause = (Projectile != nullptr || bPhaserKill) ? UHCJGameplaySettings::Get().GetWeapon(KillWeapon).Name : FString();
		UE_LOG(LogHCJ, Log, TEXT("Kill: %s destroyed %s (%s)"), *KillerName, *VictimName, *UHCJGameplaySettings::Get().GetWeapon(KillWeapon).Name);
	}

	HCJGameState->MulticastKillEvent(KillerName, VictimName, Cause, AHCJGameState::PackKillTypes(KillerType, VictimType));

	if (APlayerController* VictimPlayer = Cast<APlayerController>(VictimController); VictimPlayer && VictimShip)
	{
		// Death camera: APlayerController::SetSpawnLocation is protected; this public path sets it and
		// leaves the control rotation as it is.
		VictimPlayer->SetInitialLocationAndRotation(VictimShip->GetActorLocation(), VictimPlayer->GetControlRotation());
	}

	ScheduleRespawn(VictimController, RespawnDelay);

	// Ending the match destroys every ship, and the victim is still in the middle of Die(): decide next tick.
	GetWorldTimerManager().SetTimerForNextTick(this, &AHCJGameMode::CheckMatchEnd);
}

bool AHCJGameMode::IsClassicBots()
{
	static const bool bClassic = FParse::Param(FCommandLine::Get(), TEXT("HCJClassic"));
	return bClassic;
}

void AHCJGameMode::SetShipChoice(AController* Controller, int32 Type, int32 Variant, int32 Skin)
{
	AHCJPlayerState* HCJPlayerState = Controller != nullptr ? Controller->GetPlayerState<AHCJPlayerState>() : nullptr;
	if (HCJPlayerState == nullptr)
	{
		return;
	}
	// Whatever a client sends is validated here: an unknown type is the fighter, an unknown hull / paint "pick for me".
	HCJPlayerState->SetShipChoice(Type, Variant, Skin);
	AHCJShipCharacter* Ship = Cast<AHCJShipCharacter>(Controller->GetPawn());
	const bool bNow = Ship != nullptr && !Ship->IsDead() && Ship->IsSpawnProtected();
	if (bNow)
	{
		// The choice arrived a moment after the first ship: a ship that has not left its spawn shield is changed in place.
		Ship->ServerInitShipType(HCJPlayerState->GetShipType(), HCJPlayerState->GetShipVariant(), HCJPlayerState->GetShipSkin());
	}
	UE_LOG(LogHCJ, Log, TEXT("Ship choice: %s asked for type %d, hull %d, paint %d -> %s, hull %d, paint %d (%s)"), *HCJPlayerState->GetPlayerName(), Type, Variant, Skin,
		HCJShipTypes::Get(HCJPlayerState->GetShipType()).Name, HCJPlayerState->GetShipVariant(), HCJPlayerState->GetShipSkin(),
		bNow ? TEXT("applied to the ship it has") : TEXT("for its next ship"));
}

void AHCJGameMode::NotifyAsteroidDestroyed(const AController* DamageInstigator, float Radius, int32 NumPieces)
{
	++MatchStats.RocksDestroyed;
	MatchStats.RocksDestroyedByBots += Cast<AHCJBotController>(DamageInstigator) != nullptr ? 1 : 0;
	MatchStats.RocksSplit += NumPieces > 0 ? 1 : 0;
}

void AHCJGameMode::NotifyBlackHoleDeath(const AController* Victim)
{
	++MatchStats.BlackHoleDeaths;
	if (Cast<AHCJBotController>(Victim) != nullptr)
	{
		++MatchStats.BotBlackHoleDeaths;
	}
}

void AHCJGameMode::RequestRespawn(AController* Controller)
{
	const AHCJGameState* HCJGameState = GetHCJGameState();
	if (!IsValid(Controller) || !HCJGameState || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing || HasLiveShip(Controller))
	{
		return;
	}

	const AHCJPlayerState* HCJPlayerState = Controller->GetPlayerState<AHCJPlayerState>();
	if (HCJPlayerState && HCJPlayerState->GetEarliestRespawnServerTime() > static_cast<float>(HCJGameState->GetServerWorldTimeSeconds()))
	{
		return;
	}

	RestartPlayer(Controller);
}

int32 AHCJGameMode::GetNumHumanPlayers() const
{
	int32 Count = 0;
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Player = It->Get();
			if (IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->PlayerState && !Player->PlayerState->IsOnlyASpectator())
			{
				++Count;
			}
		}
	}
	return Count;
}

int32 AHCJGameMode::GetDesiredBotCount() const
{
	return FMath::Clamp(FMath::Max(MinBots, MinCombatants - GetNumHumanPlayers()), 0, FMath::Max(0, MaxBots));
}

AHCJGameState* AHCJGameMode::GetHCJGameState() const
{
	return GetGameState<AHCJGameState>();
}

AHCJUniverse* AHCJGameMode::GetUniverse() const
{
	const AHCJGameState* HCJGameState = GetHCJGameState();
	return HCJGameState ? HCJGameState->GetUniverse() : nullptr;
}

FHCJMatchConfig AHCJGameMode::MakeMatchConfig(int32 InMatchNumber) const
{
	FHCJMatchConfig Config;
	Config.MatchNumber = InMatchNumber;
	Config.Dimension = static_cast<uint8>(FMath::Clamp(MatchDimension, FHCJHypercubeGraph::MinDimension, FHCJHypercubeGraph::MaxDimension));
	Config.EdgeProbability = FMath::Clamp(MatchEdgeProbability, 0.0f, 1.0f);
	Config.BlackHoles = MatchBlackHoles;
	Config.Pickups = MatchPickups;
	Config.Wormholes = MatchWormholes;
	Config.Stations = MatchStations;
	Config.Asteroids = MatchAsteroids;
	if (bHasSeedOverride)
	{
		Config.Seed = InMatchNumber <= 1 ? SeedOverride : HCJ::MixSeed(SeedOverride, InMatchNumber);
	}
	else
	{
		Config.Seed = MakeRandomSeed();
	}
	return Config;
}

int32 AHCJGameMode::MakeRandomSeed()
{
	const uint64 Cycles = FPlatformTime::Cycles64();
	const uint64 Ticks = static_cast<uint64>(FDateTime::UtcNow().GetTicks());
	return HCJ::MixSeed(static_cast<int32>(Cycles ^ (Cycles >> 32)), static_cast<int32>(Ticks ^ (Ticks >> 32)));
}

void AHCJGameMode::GameUpdate()
{
	UpdatePlayerSectors();
	UpdateBotCount();
	EnsureRespawnsScheduled();
	UpdatePlaythroughWatch();
	CheckMatchEnd();

	if (const UWorld* World = GetWorld(); World && StatusLogInterval > 0.0f && World->GetTimeSeconds() >= NextStatusLogTime)
	{
		NextStatusLogTime = static_cast<float>(World->GetTimeSeconds()) + StatusLogInterval;
		LogStatus();
	}
}

void AHCJGameMode::LogStatus() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 LiveShips = 0;
	uint64 OccupiedSectors = 0;
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsDead())
		{
			++LiveShips;
			if (const int32 Sector = It->GetCurrentSector(); Sector >= 0 && Sector < 64)
			{
				OccupiedSectors |= uint64(1) << Sector;
			}
		}
	}

	UE_LOG(LogHCJ, Log, TEXT("Status t=%.0fs match %d: %d humans, %d bots, %d live ships in %d sectors | shots %d, hits %d, damage %.0f, kills %d, uncredited deaths %d, portal jumps %d, wormhole jumps %d, black hole deaths %d (%d bots)"),
		World->GetTimeSeconds(), CurrentConfig.MatchNumber, GetNumHumanPlayers(), Bots.Num(), LiveShips, FMath::CountBits64(OccupiedSectors),
		MatchStats.Shots, MatchStats.Hits, MatchStats.Damage, MatchStats.Kills, MatchStats.UncreditedDeaths, MatchStats.PortalJumps, MatchStats.WormholeJumps,
		MatchStats.BlackHoleDeaths, MatchStats.BotBlackHoleDeaths);

	UE_LOG(LogHCJ, Log, TEXT("Balance t=%.0fs: time to kill %.2f s (mean of %d), kills per minute %.1f, hit rate %.0f%%, damage per kill %.0f, hits on shield only %d, reaching hull %d, facings collapsed %d, bots turning a facing to the attacker %d"),
		World->GetTimeSeconds(), MatchStats.TimeToKillCount > 0 ? MatchStats.TimeToKillSum / MatchStats.TimeToKillCount : 0.0f, MatchStats.TimeToKillCount,
		World->GetTimeSeconds() > 1.0 ? 60.0 * MatchStats.Kills / World->GetTimeSeconds() : 0.0, MatchStats.Shots > 0 ? 100.0f * MatchStats.Hits / MatchStats.Shots : 0.0f,
		MatchStats.Kills > 0 ? MatchStats.Damage / MatchStats.Kills : 0.0f, MatchStats.HitsShieldOnly, MatchStats.HitsHull, MatchStats.FacingsCollapsed, MatchStats.BotShieldTurns);

	{
		FString Kills;
		for (int32 Index = 0; Index < static_cast<int32>(EHCJWeapon::Count); ++Index)
		{
			Kills += FString::Printf(TEXT("%s%s %d"), Index > 0 ? TEXT(", ") : TEXT(""), *UHCJGameplaySettings::Get().GetWeapon(static_cast<EHCJWeapon>(Index)).Name, MatchStats.KillsByWeapon[Index]);
		}
		const int32 TorpedoKills = MatchStats.KillsByWeapon[static_cast<int32>(EHCJWeapon::Torpedo)];
		UE_LOG(LogHCJ, Log, TEXT("Torpedoes t=%.0fs: fired %d (%d homing, %d lost their target), detonated %d, duds %d, damaged a ship %d (%.0f%% of fired), hits on ships %d (%d on a facing that was down), damage %.0f, kills %d of %d (%.0f%%), bots evading %d | kills by weapon: %s"),
			World->GetTimeSeconds(), MatchStats.TorpedoesFired, MatchStats.TorpedoesHoming, MatchStats.TorpedoesDodged, MatchStats.TorpedoDetonations, MatchStats.TorpedoDuds,
			MatchStats.TorpedoesConnected, MatchStats.TorpedoesFired > 0 ? 100.0f * MatchStats.TorpedoesConnected / MatchStats.TorpedoesFired : 0.0f,
			MatchStats.TorpedoHits, MatchStats.TorpedoHitsOpen, MatchStats.TorpedoDamage, TorpedoKills, MatchStats.Kills,
			MatchStats.Kills > 0 ? 100.0f * TorpedoKills / MatchStats.Kills : 0.0f, MatchStats.BotTorpedoEvades, *Kills);
	}

	{
		const AHCJUniverse* Universe = GetUniverse();
		UE_LOG(LogHCJ, Log, TEXT("Phasers t=%.0fs: beam %.0f s (%.0f%% of it on a ship), damage %.0f (%.1f per second on a ship), steps on shield only %d, reaching hull %d, kills %d of %d (%.0f%%) | rocks destroyed %d (%d by bots, %d broke into pieces), %d rocks in the world, destroyed-set checksum %08X | shockwaves %d, ships pushed %d"),
			World->GetTimeSeconds(), MatchStats.PhaserSeconds, MatchStats.PhaserSeconds > 0.0f ? 100.0f * MatchStats.PhaserOnShipSeconds / MatchStats.PhaserSeconds : 0.0f,
			MatchStats.PhaserDamage, MatchStats.PhaserOnShipSeconds > 0.0f ? MatchStats.PhaserDamage / MatchStats.PhaserOnShipSeconds : 0.0f,
			MatchStats.PhaserStepsShield, MatchStats.PhaserStepsHull, MatchStats.KillsByWeapon[static_cast<int32>(EHCJWeapon::Phaser)], MatchStats.Kills,
			MatchStats.Kills > 0 ? 100.0f * MatchStats.KillsByWeapon[static_cast<int32>(EHCJWeapon::Phaser)] / MatchStats.Kills : 0.0f,
			MatchStats.RocksDestroyed, MatchStats.RocksDestroyedByBots, MatchStats.RocksSplit, Universe != nullptr ? Universe->GetNumLiveAsteroids() : 0,
			Universe != nullptr ? Universe->GetDestroyedChecksum() : 0, MatchStats.Shockwaves, MatchStats.ShockwavePushes);
	}

	{
		FString ByType;
		for (int32 Type = 0; Type < HCJShipTypes::NumTypes; ++Type)
		{
			const int32 TypeKills = MatchStats.KillsByType[Type];
			const int32 TypeDeaths = MatchStats.DeathsByType[Type];
			ByType += FString::Printf(TEXT("%s%s %d/%d (%.2f)"), Type > 0 ? TEXT(", ") : TEXT(""), HCJShipTypes::Get(Type).ShortName, TypeKills, TypeDeaths,
				TypeDeaths > 0 ? static_cast<float>(TypeKills) / TypeDeaths : 0.0f);
		}
		UE_LOG(LogHCJ, Log, TEXT("Systems t=%.0fs: warps %d (%d by bots, %d stopped by the server, %d collisions), cloaks %d (%d by bots, %d reveals), tractor holds %d (%d by bots, %d torn free, %d bots boosted free, %.0f s held) | kills/deaths by ship type: %s"),
			World->GetTimeSeconds(), MatchStats.Warps, MatchStats.BotWarps, MatchStats.WarpCancels, MatchStats.WarpImpacts, MatchStats.Cloaks, MatchStats.BotCloaks,
			MatchStats.CloakReveals, MatchStats.TractorHolds, MatchStats.BotTractorHolds, MatchStats.TractorBreaks, MatchStats.BotTractorEscapes, MatchStats.TractorSeconds, *ByType);
	}

	{
		int32 NumTurrets = 0;
		int32 LiveTurrets = 0;
		for (TActorIterator<AHCJTurret> It(World); It; ++It)
		{
			++NumTurrets;
			LiveTurrets += It->IsTurretDestroyed() ? 0 : 1;
		}
		const int32 TotalDeaths = MatchStats.Kills + MatchStats.TurretDeaths + MatchStats.UncreditedDeaths;
		UE_LOG(LogHCJ, Log, TEXT("Deaths t=%.0fs: %d in all | by players %d, by station turrets %d (%.0f%%), by black holes %d, other %d | turrets: %d of %d alive, %d destroyed so far, %d shots"),
			World->GetTimeSeconds(), TotalDeaths, MatchStats.Kills, MatchStats.TurretDeaths,
			TotalDeaths > 0 ? 100.0f * MatchStats.TurretDeaths / TotalDeaths : 0.0f, MatchStats.BlackHoleDeaths,
			FMath::Max(MatchStats.UncreditedDeaths - MatchStats.BlackHoleDeaths, 0), LiveTurrets, NumTurrets, MatchStats.TurretsDestroyed, MatchStats.TurretShots);
	}

	{
		FString Tactics;
		for (int32 Index = 0; Index < static_cast<int32>(EHCJBotTactic::Count); ++Index)
		{
			Tactics += FString::Printf(TEXT("%s%s %d"), Index > 0 ? TEXT(", ") : TEXT(""), HCJBalance::GetTacticName(static_cast<EHCJBotTactic>(Index)), MatchStats.BotTactics[Index]);
		}
		UE_LOG(LogHCJ, Log, TEXT("Tactics t=%.0fs (bots): %s, facing turns %d, open-facing torpedo kills %d"), World->GetTimeSeconds(), *Tactics, MatchStats.BotShieldTurns,
			MatchStats.TorpedoOpenFacingKills);
	}
	LogPlaythroughStats();

	if (CurrentConfig.Pickups.bEnabled)
	{
		int32 ActivePads = 0;
		int32 NumPads = 0;
		for (TActorIterator<AHCJPickup> It(World); It; ++It)
		{
			++NumPads;
			ActivePads += It->IsActive() ? 1 : 0;
		}
		FString Taken;
		for (int32 Index = 0; Index < static_cast<int32>(EHCJPickupItem::Count); ++Index)
		{
			Taken += FString::Printf(TEXT("%s%s %d"), Index > 0 ? TEXT(", ") : TEXT(""), HCJ::GetItemName(static_cast<EHCJPickupItem>(Index)), MatchStats.Pickups[Index]);
		}
		FString Kills;
		for (int32 Index = 0; Index < static_cast<int32>(EHCJWeapon::Count); ++Index)
		{
			Kills += FString::Printf(TEXT("%s%s %d"), Index > 0 ? TEXT(", ") : TEXT(""), *UHCJGameplaySettings::Get().GetWeapon(static_cast<EHCJWeapon>(Index)).Name, MatchStats.KillsByWeapon[Index]);
		}
		UE_LOG(LogHCJ, Log, TEXT("Pickups t=%.0fs: %d of %d pads hold an item | taken: %s (%d by bots) | kills by weapon: %s"),
			World->GetTimeSeconds(), ActivePads, NumPads, *Taken, MatchStats.BotPickups, *Kills);
	}
}

void AHCJGameMode::CheckMatchEnd()
{
	const AHCJGameState* HCJGameState = GetHCJGameState();
	if (!HCJGameState || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing)
	{
		return;
	}

	bool bEnd = HCJGameState->HasPhaseTimeLimit() && HCJGameState->GetPhaseTimeRemaining() <= 0.0f;
	if (!bEnd && FragLimit > 0)
	{
		for (const APlayerState* PlayerState : HCJGameState->PlayerArray)
		{
			const AHCJPlayerState* HCJPlayerState = Cast<AHCJPlayerState>(PlayerState);
			if (HCJPlayerState && HCJPlayerState->GetKills() >= FragLimit)
			{
				bEnd = true;
				break;
			}
		}
	}

	if (bEnd)
	{
		EndMatch();
	}
}

void AHCJGameMode::EndMatch()
{
	AHCJGameState* HCJGameState = GetHCJGameState();
	if (!HCJGameState || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing)
	{
		return;
	}

	CancelAllRespawns();
	DestroyAllShips();

	for (APlayerState* PlayerState : HCJGameState->PlayerArray)
	{
		if (AHCJPlayerState* HCJPlayerState = Cast<AHCJPlayerState>(PlayerState))
		{
			HCJPlayerState->SetRespawnTimes(0.0f, 0.0f);
			HCJPlayerState->SetCurrentSector(INDEX_NONE);
		}
	}

	TArray<AHCJPlayerState*> Sorted;
	HCJGameState->GetSortedPlayerStates(Sorted);
	AHCJPlayerState* Winner = Sorted.Num() > 0 ? Sorted[0] : nullptr;

	const float Duration = FMath::Max(IntermissionSeconds, 0.1f);
	HCJGameState->SetMatchPhase(EHCJMatchPhase::Intermission, Duration, Winner);
	GetWorldTimerManager().SetTimer(IntermissionTimerHandle, this, &AHCJGameMode::StartNewMatch, Duration, false);

	UE_LOG(LogHCJ, Log, TEXT("Match %d over, winner: %s"), CurrentConfig.MatchNumber, Winner ? *Winner->GetPlayerName() : TEXT("nobody"));

	// For the playthrough tests: the result, the board every machine shows now, and the counters of the match.
	++NumMatchesEnded;
	LastWinnerName = Winner != nullptr ? Winner->GetPlayerName() : FString();
	LastWinnerKills = Winner != nullptr ? Winner->GetKills() : 0;
	LastBoardRows = Sorted.Num();
	const bool bFragEnd = FragLimit > 0 && Winner != nullptr && Winner->GetKills() >= FragLimit;
	UE_LOG(LogHCJ, Log, TEXT("Playthrough: event=match_end match=%d winner=%s winner_kills=%d frag_limit=%d reason=%s duration=%.0f board_rows=%d intermission=%.0f"),
		CurrentConfig.MatchNumber, *PlaythroughToken(LastWinnerName), LastWinnerKills, FragLimit, bFragEnd ? TEXT("frag_limit") : TEXT("time_or_command"),
		GetWorld()->GetTimeSeconds() - MatchStats.StartTime, Sorted.Num(), Duration);
	for (int32 Rank = 0; Rank < Sorted.Num(); ++Rank)
	{
		const AHCJPlayerState* Row = Sorted[Rank];
		UE_LOG(LogHCJ, Log, TEXT("Playthrough: event=board match=%d rank=%d name=%s kills=%d deaths=%d type=%s bot=%d"), CurrentConfig.MatchNumber, Rank + 1,
			*PlaythroughToken(Row->GetPlayerName()), Row->GetKills(), Row->GetDeaths(), *FString(HCJShipTypes::Get(Row->GetShipType()).ShortName).ToLower(), Row->IsABot() ? 1 : 0);
	}
	LogPlaythroughStats();
}

void AHCJGameMode::StartNewMatch()
{
	AHCJGameState* HCJGameState = GetHCJGameState();
	if (!HCJGameState || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Intermission)
	{
		return;
	}

	BeginMatch(CurrentConfig.MatchNumber + 1);
}

void AHCJGameMode::BeginMatch(int32 InMatchNumber)
{
	AHCJGameState* HCJGameState = GetHCJGameState();
	if (!HCJGameState)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(IntermissionTimerHandle);
	CancelAllRespawns();
	DestroyAllShips();

	for (APlayerState* PlayerState : HCJGameState->PlayerArray)
	{
		if (AHCJPlayerState* HCJPlayerState = Cast<AHCJPlayerState>(PlayerState))
		{
			HCJPlayerState->ResetMatchStats();
			HCJPlayerState->SetRespawnTimes(0.0f, 0.0f);
		}
	}

	MatchStats = FMatchStats();
	MatchStats.StartTime = GetWorld() != nullptr ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.0f;
	ShipWatches.Reset();
	// Not Playing while the world is rebuilt, so bots added now get no ship before the delayed respawn below.
	HCJGameState->SetMatchPhase(EHCJMatchPhase::Intermission, 0.0f, nullptr);
	CurrentConfig = MakeMatchConfig(InMatchNumber);
	HCJGameState->SetMatchConfig(CurrentConfig);
	RebuildPickups();
	RebuildStations();

	// Still Intermission here, so bots added now get no ship before the delayed respawn below.
	UpdateBotCount();

	HCJGameState->SetFragLimit(FragLimit);
	HCJGameState->SetMatchPhase(EHCJMatchPhase::Playing, TimeLimitSeconds, nullptr);
	LogPlaythroughMatchStart();

	ForEachCombatant([this](AController* Controller)
	{
		if (!MustSpectate(Cast<APlayerController>(Controller)))
		{
			ScheduleRespawn(Controller, MatchStartRespawnDelay);
		}
	});
}

void AHCJGameMode::StartMatchFromMenu(AHCJPlayerController* Host, int32 NumBots, int32 Dimension, int32 Seed, int32 Difficulty)
{
	if (!IsValid(Host))
	{
		return;
	}
	MinCombatants = FMath::Clamp(NumBots, 0, FMath::Max(0, MaxBots)) + 1;
	MatchDimension = FMath::Clamp(Dimension, FHCJHypercubeGraph::MinDimension, FHCJHypercubeGraph::MaxDimension);
	bHasSeedOverride = Seed != 0;
	SeedOverride = Seed;
	BotDifficulty = FMath::Clamp(Difficulty, 0, 2);
	if (Host->PlayerState != nullptr)
	{
		Host->PlayerState->SetIsOnlyASpectator(false);
	}
	UE_LOG(LogHCJ, Log, TEXT("Menu: Play - %d bots, dimension %d, seed %s, bot difficulty %d"), MinCombatants - 1, MatchDimension,
		bHasSeedOverride ? *FString::FromInt(SeedOverride) : TEXT("random"), BotDifficulty);
	BeginMatch(CurrentConfig.MatchNumber + 1);
}

void AHCJGameMode::ReturnToMenu(AHCJPlayerController* Host)
{
	if (!IsValid(Host))
	{
		return;
	}
	CancelRespawn(Host);
	if (APawn* Pawn = Host->GetPawn())
	{
		Host->SetInitialLocationAndRotation(Pawn->GetActorLocation(), Host->GetControlRotation());
		Host->UnPossess();
		Pawn->Destroy();
	}
	if (AHCJPlayerState* HostState = Host->GetPlayerState<AHCJPlayerState>())
	{
		HostState->SetIsOnlyASpectator(true);
		HostState->SetRespawnTimes(0.0f, 0.0f);
		HostState->SetCurrentSector(INDEX_NONE);
	}
	UE_LOG(LogHCJ, Log, TEXT("Menu: the host went back to the title menu"));
	UpdateBotCount();
}

void AHCJGameMode::DestroyAllShips()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AHCJShipCharacter*> Ships;
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		Ships.Add(*It);
	}

	for (AHCJShipCharacter* Ship : Ships)
	{
		if (!IsValid(Ship))
		{
			continue;
		}
		if (APlayerController* Player = Cast<APlayerController>(Ship->GetController()))
		{
			Player->SetInitialLocationAndRotation(Ship->GetActorLocation(), Player->GetControlRotation());
		}
		Ship->Destroy();
	}
}

int32 AHCJGameMode::ChooseSpawn(const AController* ForController, FTransform& OutTransform)
{
	const AHCJUniverse* Universe = GetUniverse();
	UWorld* World = GetWorld();
	if (Universe == nullptr || !Universe->IsBuilt() || World == nullptr)
	{
		return INDEX_NONE;
	}

	// The other live ships and the live turrets, once.
	struct FShipAt { FVector Location; int32 Sector; };
	TArray<FShipAt, TInlineAllocator<32>> Ships;
	for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
	{
		if (!It->IsDead() && It->GetController() != ForController)
		{
			Ships.Add({ It->GetActorLocation(), Universe->GetSectorAtLocation(It->GetActorLocation()) });
		}
	}
	TArray<FVector, TInlineAllocator<64>> Turrets;
	for (TActorIterator<AHCJTurret> It(World); It; ++It)
	{
		if (!It->IsTurretDestroyed())
		{
			Turrets.Add(It->GetActorLocation());
		}
	}
	const double TurretRangeSq = FMath::Square(static_cast<double>(UHCJGameplaySettings::Get().TurretRange) + 300.0);

	constexpr int32 NumCandidates = 12;
	TArray<FHCJSpawnCandidate, TInlineAllocator<NumCandidates>> Candidates;
	TArray<FTransform, TInlineAllocator<NumCandidates>> Transforms;
	TArray<int32, TInlineAllocator<NumCandidates>> Sectors;
	for (int32 Index = 0; Index < NumCandidates; ++Index)
	{
		const int32 Sector = ChooseSpawnSector(ForController);
		if (Sector == INDEX_NONE)
		{
			break;
		}
		const FTransform Transform = Universe->GetRandomSpawnTransform(Sector, GameplayRandom);
		const FVector Location = Transform.GetLocation();
		FHCJSpawnCandidate Candidate;
		for (const FShipAt& Ship : Ships)
		{
			if (Ship.Sector == Sector)
			{
				++Candidate.EnemiesInSector;
				Candidate.NearestEnemyDistance = FMath::Min(Candidate.NearestEnemyDistance, static_cast<float>(FVector::Dist(Ship.Location, Location)));
			}
		}
		for (const FVector& Turret : Turrets)
		{
			if (FVector::DistSquared(Turret, Location) < TurretRangeSq)
			{
				Candidate.bInTurretRange = true;
				break;
			}
		}
		Candidate.GravityPull = static_cast<float>(Universe->GetGravityDrift(Location).Size());
		Candidates.Add(Candidate);
		Transforms.Add(Transform);
		Sectors.Add(Sector);
	}

	const int32 Best = HCJBalance::PickBestSpawn(Candidates);
	if (Best == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	OutTransform = Transforms[Best];
	UE_LOG(LogHCJ, Verbose, TEXT("Spawn: sector %d, %d enemies there, nearest %.0f uu, turret range %d, pull %.0f (score %.0f)"), Sectors[Best],
		Candidates[Best].EnemiesInSector, Candidates[Best].NearestEnemyDistance, Candidates[Best].bInTurretRange ? 1 : 0, Candidates[Best].GravityPull,
		HCJBalance::ScoreSpawn(Candidates[Best]));
	return Sectors[Best];
}

int32 AHCJGameMode::ChooseSpawnSector(const AController* ForController)
{
	const AHCJUniverse* Universe = GetUniverse();
	const int32 NumSectors = (Universe && Universe->IsBuilt()) ? Universe->NumSectors() : 0;
	return NumSectors > 0 ? GameplayRandom.RandRange(0, NumSectors - 1) : INDEX_NONE;
}

void AHCJGameMode::ScheduleRespawn(AController* Controller, float Delay)
{
	if (!IsValid(Controller) || Controller->IsActorBeingDestroyed())
	{
		return;
	}

	const float SafeDelay = FMath::Max(Delay, 0.05f);
	FTimerHandle& Handle = RespawnTimers.FindOrAdd(Controller);
	GetWorldTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateUObject(this, &AHCJGameMode::RespawnController, TWeakObjectPtr<AController>(Controller)),
		SafeDelay, false);

	const AHCJGameState* HCJGameState = GetHCJGameState();
	if (AHCJPlayerState* HCJPlayerState = Controller->GetPlayerState<AHCJPlayerState>(); HCJPlayerState && HCJGameState)
	{
		const float Now = static_cast<float>(HCJGameState->GetServerWorldTimeSeconds());
		HCJPlayerState->SetRespawnTimes(Now + FMath::Min(MinRespawnDelay, SafeDelay), Now + SafeDelay);
	}
}

void AHCJGameMode::CancelRespawn(AController* Controller)
{
	FTimerHandle Handle;
	if (RespawnTimers.RemoveAndCopyValue(Controller, Handle))
	{
		GetWorldTimerManager().ClearTimer(Handle);
	}
}

void AHCJGameMode::CancelAllRespawns()
{
	FTimerManager& TimerManager = GetWorldTimerManager();
	for (TPair<TWeakObjectPtr<AController>, FTimerHandle>& Pair : RespawnTimers)
	{
		TimerManager.ClearTimer(Pair.Value);
	}
	RespawnTimers.Reset();
}

void AHCJGameMode::RespawnController(TWeakObjectPtr<AController> Controller)
{
	RespawnTimers.Remove(Controller);

	AController* Target = Controller.Get();
	if (!IsValid(Target))
	{
		return;
	}

	if (AHCJPlayerState* HCJPlayerState = Target->GetPlayerState<AHCJPlayerState>())
	{
		// The timer is gone; if the restart fails EnsureRespawnsScheduled starts a new one.
		HCJPlayerState->SetRespawnTimes(0.0f, 0.0f);
	}
	RestartPlayer(Target);
}

void AHCJGameMode::EnsureRespawnsScheduled()
{
	const AHCJGameState* HCJGameState = GetHCJGameState();
	if (!HCJGameState || HCJGameState->GetMatchPhase() != EHCJMatchPhase::Playing)
	{
		return;
	}

	for (auto It = RespawnTimers.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	FTimerManager& TimerManager = GetWorldTimerManager();
	ForEachCombatant([this, &TimerManager](AController* Controller)
	{
		if (HasLiveShip(Controller) || MustSpectate(Cast<APlayerController>(Controller)))
		{
			return;
		}
		const FTimerHandle* Handle = RespawnTimers.Find(Controller);
		if (!Handle || !TimerManager.IsTimerActive(*Handle))
		{
			ScheduleRespawn(Controller, MatchStartRespawnDelay);
		}
	});
}

bool AHCJGameMode::HasLiveShip(const AController* Controller)
{
	const AHCJShipCharacter* Ship = Controller ? Cast<AHCJShipCharacter>(Controller->GetPawn()) : nullptr;
	return IsValid(Ship) && !Ship->IsDead();
}

void AHCJGameMode::UpdateBotCount()
{
	const UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}

	Bots.RemoveAll([](const TObjectPtr<AHCJBotController>& Bot)
	{
		return !IsValid(Bot) || Bot->IsActorBeingDestroyed();
	});

	const int32 Desired = GetDesiredBotCount();

	while (Bots.Num() < Desired)
	{
		if (!SpawnBot())
		{
			break;
		}
	}

	while (Bots.Num() > Desired)
	{
		// Shipless bots go first, then the lowest score.
		AHCJBotController* Victim = nullptr;
		bool bVictimHasShip = true;
		int32 VictimKills = MAX_int32;
		for (AHCJBotController* Bot : Bots)
		{
			const bool bHasShip = HasLiveShip(Bot);
			const AHCJPlayerState* BotState = Bot->GetPlayerState<AHCJPlayerState>();
			const int32 Kills = BotState ? BotState->GetKills() : 0;
			if (!Victim || (bVictimHasShip && !bHasShip) || (bVictimHasShip == bHasShip && Kills < VictimKills))
			{
				Victim = Bot;
				bVictimHasShip = bHasShip;
				VictimKills = Kills;
			}
		}
		RemoveBot(Victim);
	}
}

AHCJBotController* AHCJGameMode::SpawnBot()
{
	UWorld* World = GetWorld();
	UClass* BotClass = BotControllerClass ? BotControllerClass.Get() : AHCJBotController::StaticClass();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	AHCJBotController* Bot = World->SpawnActor<AHCJBotController>(BotClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (!Bot)
	{
		UE_LOG(LogHCJ, Warning, TEXT("Could not spawn a bot controller of class %s"), *GetNameSafe(BotClass));
		return nullptr;
	}

	Bots.Add(Bot);

	if (APlayerState* BotState = Bot->PlayerState)
	{
		// Only human logins get an id from the session; the player colour is derived from it.
		if (GameSession)
		{
			BotState->SetPlayerId(GameSession->GetNextPlayerID());
		}
		// A mix of ship types (every bot a fighter with -HCJClassic; all of one type with -HCJBotType=<n>).
		if (AHCJPlayerState* HCJBotState = Cast<AHCJPlayerState>(BotState))
		{
			int32 ForcedType = -1;
			static const bool bForced = FParse::Value(FCommandLine::Get(), TEXT("HCJBotType="), ForcedType);
			static const int32 StaticForcedType = ForcedType;
			HCJBotState->SetShipChoice(IsClassicBots() ? static_cast<int32>(EHCJShipType::Fighter) : bForced ? StaticForcedType : HCJShipTypes::PickBotType(NextBotNumber),
				HCJShipTypes::Auto, HCJShipTypes::Auto);
		}
		BotState->SetPlayerName(FString::Printf(TEXT("Bot-%02d"), NextBotNumber++));
	}
	else
	{
		UE_LOG(LogHCJ, Warning, TEXT("Bot controller %s has no player state (bWantsPlayerState not set?)"), *Bot->GetName());
	}

	RestartPlayer(Bot);

	UE_LOG(LogHCJ, Log, TEXT("Bot added: %s (%s) -> %d bots, %d humans"),
		Bot->PlayerState ? *Bot->PlayerState->GetPlayerName() : *Bot->GetName(),
		HasLiveShip(Bot) ? TEXT("ship spawned") : TEXT("no ship yet"), Bots.Num(), GetNumHumanPlayers());
	return Bot;
}

void AHCJGameMode::RemoveBot(AHCJBotController* Bot)
{
	Bots.Remove(Bot);
	if (!IsValid(Bot))
	{
		return;
	}

	UE_LOG(LogHCJ, Log, TEXT("Bot removed: %s -> %d bots, %d humans"),
		Bot->PlayerState ? *Bot->PlayerState->GetPlayerName() : *Bot->GetName(), Bots.Num(), GetNumHumanPlayers());

	CancelRespawn(Bot);
	if (APawn* Pawn = Bot->GetPawn())
	{
		Bot->UnPossess();
		Pawn->Destroy();
	}
	Bot->Destroy();
}

void AHCJGameMode::UpdatePlayerSectors()
{
	ForEachCombatant([](AController* Controller)
	{
		if (AHCJPlayerState* HCJPlayerState = Controller->GetPlayerState<AHCJPlayerState>())
		{
			const AHCJShipCharacter* Ship = HasLiveShip(Controller) ? Cast<AHCJShipCharacter>(Controller->GetPawn()) : nullptr;
			// A cloaked ship is on nobody's map (and bots do not hunt it across sectors).
			HCJPlayerState->SetCurrentSector(Ship && !Ship->IsCloakHidden() ? Ship->GetCurrentSector() : INDEX_NONE);
		}
	});
}

void AHCJGameMode::ForEachCombatant(TFunctionRef<void(AController*)> Visitor) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Snapshot first: visitors spawn and destroy pawns.
	TArray<AController*, TInlineAllocator<32>> Combatants;
	for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (IsValid(Controller) && !Controller->IsActorBeingDestroyed()
			&& (Controller->IsA<APlayerController>() || Controller->IsA<AHCJBotController>()))
		{
			Combatants.Add(Controller);
		}
	}

	for (AController* Controller : Combatants)
	{
		if (IsValid(Controller))
		{
			Visitor(Controller);
		}
	}
}
