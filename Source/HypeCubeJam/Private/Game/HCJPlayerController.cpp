#include "Game/HCJPlayerController.h"

#include "World/HCJUniverse.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "World/HCJTurret.h"
#include "AI/HCJBotController.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJSpaceBackdrop.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Audio/HCJAudioDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Assets/HCJAssetSettings.h"
#include "Game/HCJGameMode.h"
#include "Game/HCJHUD.h"
#include "Game/HCJPlayerState.h"
#include "Game/HCJUiLogic.h"
#include "Game/HCJUserSettings.h"
#include "Game/HCJGameState.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/NetDriver.h"
#include "IPAddress.h"
#include "Online/HCJEOSSubsystem.h"
#include "UI/HCJMenu.h"
#include "UI/HCJIntro.h"
#include "FileMediaSource.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HypeCubeJam.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Ship/HCJHealthComponent.h"
#include "Ship/HCJShipCharacter.h"

namespace
{
	/** Maps Key to Action; a 1D key value lands on the X axis, so other axes need a swizzle (bToY: X->Y, bToZ: X->Z). */
	void MapAxisKey(UInputMappingContext& Context, const UInputAction* Action, const FKey& Key, bool bToY, bool bToZ, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = Context.MapKey(Action, Key);
		if (bToY || bToZ)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(&Context);
			Swizzle->Order = bToY ? EInputAxisSwizzle::YXZ : EInputAxisSwizzle::ZYX;
			Mapping.Modifiers.Add(Swizzle);
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(&Context));
		}
	}
}

AHCJPlayerController::AHCJPlayerController()
{
	bShowMouseCursor = false;
}

AHCJShipCharacter* AHCJPlayerController::GetShip() const
{
	AHCJShipCharacter* Ship = Cast<AHCJShipCharacter>(GetPawn());
	return (IsValid(Ship) && !Ship->IsDead()) ? Ship : nullptr;
}

void AHCJPlayerController::ServerRequestRespawn_Implementation()
{
	const UWorld* World = GetWorld();
	if (AHCJGameMode* GameMode = World ? World->GetAuthGameMode<AHCJGameMode>() : nullptr)
	{
		GameMode->RequestRespawn(this);
	}
}

void AHCJPlayerController::ClientHitConfirm_Implementation(bool bKilled)
{
	if (UHCJAudioDirector* Audio = UHCJAudioDirector::Get(this))
	{
		Audio->NotifyHitConfirm(bKilled);
	}
}

void AHCJPlayerController::ClientNotifyPickup_Implementation(uint8 Item)
{
	LastPickupItem = Item;
	LastPickupTime = GetWorld() != nullptr ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.0f;
}

namespace
{
	/** The controls overlay shows itself once per process, on the first ship. */
	bool GHelpAutoShown = false;
	constexpr float HelpAutoSeconds = 8.0f;

	/** A name that survives a travel URL. */
	FString SanitizeName(const FString& Name)
	{
		FString Result;
		for (const TCHAR Char : Name.TrimStartAndEnd().Left(20))
		{
			Result.AppendChar((FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('-')) ? Char : TEXT('_'));
		}
		return Result;
	}
}

void AHCJPlayerController::EnterStartMenu()
{
	bInStartMenu = true;
	if (PlayerState != nullptr)
	{
		PlayerState->SetIsOnlyASpectator(true);
	}
}

bool AHCJPlayerController::IsHelpVisible() const
{
	const UWorld* World = GetWorld();
	// With hints on, the onboarding prompts do the job of the automatic showing.
	return bHelpToggled || (!UHCJUserSettings::Get().bHints && FirstShipTime >= 0.0f && World != nullptr && World->GetTimeSeconds() - FirstShipTime < HelpAutoSeconds);
}

void AHCJPlayerController::ToggleHelp()
{
	if (IsHelpVisible())
	{
		bHelpToggled = false;
		FirstShipTime = -1000.0f;
	}
	else
	{
		bHelpToggled = true;
	}
}

void AHCJPlayerController::ToggleHints()
{
	if (IsMenuOpen())
	{
		return;
	}
	UHCJUserSettings& Settings = UHCJUserSettings::Get();
	Settings.bHints = !Settings.bHints;
	Settings.Save();
	HintsToggleTime = GetWorld() != nullptr ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.0f;
	if (!Settings.bHints && !bHelpToggled)
	{
		// Switching hints off must not bring up the controls page that stands in for them on a first ship.
		FirstShipTime = -1000.0f;
	}
	UE_LOG(LogHCJ, Log, TEXT("Hints %s"), Settings.bHints ? TEXT("on") : TEXT("off"));
}

void AHCJPlayerController::OpenMenu(EHCJMenuPage Page, const FString& Notice)
{
	if (!IsLocalController() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}
	if (!MenuWidget.IsValid())
	{
		SAssignNew(MenuWidget, SHCJMenu).Owner(this);
		GEngine->GameViewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(), 50);
	}
	MenuWidget->SetPage(Page, Notice);

	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(MenuWidget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
	bShowMouseCursor = true;

	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->SetFiring(false);
		Ship->SetBoosting(false);
	}
}

void AHCJPlayerController::CloseMenu()
{
	if (MenuWidget.IsValid())
	{
		if (GEngine != nullptr && GEngine->GameViewport != nullptr)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
		}
		MenuWidget.Reset();
	}
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

namespace
{
	/** The intro was started (or found missing) in this process: never again, whatever world comes next. */
	bool GIntroShown = false;
}

bool AHCJPlayerController::TickIntro(float DeltaTime)
{
	if (!bIntroChecked)
	{
		bIntroChecked = true;
		if (HCJIntro::ShouldPlay(FCommandLine::Get(), FApp::GetProjectName(), FApp::CanEverRender(), IsRunningDedicatedServer(), GIntroShown))
		{
			GIntroShown = true;
			StartIntro();
		}
	}
	if (!IntroWidget.IsValid() || IntroPlayer == nullptr)
	{
		return false;
	}

	const float Position = static_cast<float>(IntroPlayer->GetTime().GetTotalSeconds());
	const float Duration = static_cast<float>(IntroPlayer->GetDuration().GetTotalSeconds());
	const EHCJIntroEnd End = IntroFlow.Tick(DeltaTime, IntroPlayer->IsPlaying(), IntroPlayer->HasError(), Position, Duration);
	if (IntroFlow.HasStarted() && !bIntroFirstFrameLogged)
	{
		bIntroFirstFrameLogged = true;
		const FIntPoint Size = IntroPlayer->GetVideoTrackDimensions(INDEX_NONE, INDEX_NONE);
		UE_LOG(LogHCJ, Log, TEXT("Intro: the movie plays (first frame after %.2f s, %d x %d, %.1f s long, %d audio track(s) left silent)"),
			IntroFlow.GetStartDelay(), Size.X, Size.Y, Duration, IntroPlayer->GetNumTracks(EMediaPlayerTrack::Audio));
	}
	if (End == EHCJIntroEnd::None)
	{
		return true;
	}
	UE_LOG(LogHCJ, Log, TEXT("Intro: over after %.1f s at %.1f of %.1f s of the movie (%s); the title menu opens"), IntroFlow.GetElapsed(), Position, Duration, FHCJIntroFlow::DescribeEnd(End));
	EndIntro();
	return false;
}

void AHCJPlayerController::StartIntro()
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / HCJIntro::MoviePath);
	if (GEngine == nullptr || GEngine->GameViewport == nullptr || !IFileManager::Get().FileExists(*Path))
	{
		UE_LOG(LogHCJ, Warning, TEXT("Intro: no movie at Content/%s; straight to the title menu"), HCJIntro::MoviePath);
		return;
	}

	IntroPlayer = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
	IntroPlayer->SetLooping(false);
	IntroPlayer->PlayOnOpen = true;
	IntroTexture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);
	IntroTexture->AutoClear = true;
	IntroTexture->ClearColor = FLinearColor::Black;
	IntroTexture->SetMediaPlayer(IntroPlayer);
	IntroTexture->UpdateResource();
	IntroSource = NewObject<UFileMediaSource>(this, NAME_None, RF_Transient);
	IntroSource->SetFilePath(Path);

	IntroFlow = FHCJIntroFlow();
	bIntroFirstFrameLogged = false;
	if (!IntroPlayer->OpenSource(IntroSource))
	{
		UE_LOG(LogHCJ, Warning, TEXT("Intro: the media player could not open Content/%s; straight to the title menu"), HCJIntro::MoviePath);
		EndIntro();
		return;
	}

	const TWeakObjectPtr<AHCJPlayerController> WeakThis(this);
	SAssignNew(IntroWidget, SHCJIntro)
		.Texture(IntroTexture)
		.PictureSize(FVector2D(1280.0, 720.0))
		.OnSkip(FSimpleDelegate::CreateLambda([WeakThis]() { if (AHCJPlayerController* Self = WeakThis.Get()) { Self->IntroFlow.RequestSkip(); } }))
		.TitleAlpha(TAttribute<float>::CreateLambda([WeakThis]() { const AHCJPlayerController* Self = WeakThis.Get(); return Self != nullptr ? Self->IntroFlow.GetTitleAlpha() : 0.0f; }))
		.BlackAlpha(TAttribute<float>::CreateLambda([WeakThis]() { const AHCJPlayerController* Self = WeakThis.Get(); return Self != nullptr ? Self->IntroFlow.GetBlackAlpha() : 1.0f; }));
	// Above the menu (50) and everything the HUD draws.
	GEngine->GameViewport->AddViewportWidgetContent(IntroWidget.ToSharedRef(), 100);

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(IntroWidget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	bShowMouseCursor = false;
	UE_LOG(LogHCJ, Log, TEXT("Intro: opening Content/%s (any key skips; gives up after %.0f s without a picture)"), HCJIntro::MoviePath, IntroFlow.OpenTimeout);
}

void AHCJPlayerController::EndIntro()
{
	if (IntroWidget.IsValid())
	{
		if (GEngine != nullptr && GEngine->GameViewport != nullptr)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(IntroWidget.ToSharedRef());
		}
		IntroWidget.Reset();
	}
	if (IntroPlayer != nullptr)
	{
		IntroPlayer->Close();
	}
	IntroPlayer = nullptr;
	IntroTexture = nullptr;
	IntroSource = nullptr;
}

void AHCJPlayerController::UpdateOnline(float DeltaTime)
{
	UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(this);
	UWorld* World = GetWorld();
	if (Online == nullptr || World == nullptr)
	{
		return;
	}

	const AHCJPlayerState* Me = GetPlayerState<AHCJPlayerState>();
	const AHCJGameState* GameState = World->GetGameState<AHCJGameState>();
	if (Me != nullptr && GameState != nullptr)
	{
		FHCJStatSample Sample;
		Sample.Kills = Me->GetKills();
		Sample.Deaths = Me->GetDeaths();
		Sample.Turrets = Me->GetTurretKills();
		Sample.bParticipating = !bInStartMenu && !Me->IsOnlyASpectator();
		Sample.bIntermission = GameState->GetMatchPhase() == EHCJMatchPhase::Intermission;
		Sample.bLocalWon = Sample.bIntermission && GameState->GetMatchWinner() == Me;
		Online->ObserveLocalStats(Sample);
	}

	OnlineHostTimer -= DeltaTime;
	if (OnlineHostTimer > 0.0f)
	{
		return;
	}
	OnlineHostTimer = 1.0f;

	// The name of the settings, not the replicated one: a server may have changed that to keep names unique.
	const FString SettingsName = SanitizeName(UHCJUserSettings::Get().PlayerName);
	const FString PilotName = !SettingsName.IsEmpty() ? SettingsName : Me != nullptr ? Me->GetPlayerName() : FString();
	Online->SetDisplayName(PilotName);

	// A match is advertised by its listen host once it is past the title menu.
	const AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>();
	FHCJLobbyInfo Info;
	// (Nothing is gathered while the service is not there: this must cost nothing then.)
	const bool bHosting = Online->IsReady() && GameMode != nullptr && World->GetNetMode() == NM_ListenServer && !bInStartMenu && GameState != nullptr;
	if (bHosting)
	{
		Info.HostName = PilotName;
		Info.PublicAddress = UHCJEOSSubsystem::GetConfiguredPublicAddress();
		Info.LanAddresses = UHCJEOSSubsystem::GetLocalAddresses();
		const UNetDriver* NetDriver = World->GetNetDriver();
		const TSharedPtr<const FInternetAddr> LocalAddr = NetDriver != nullptr ? const_cast<UNetDriver*>(NetDriver)->GetLocalAddr() : nullptr;
		Info.Port = LocalAddr.IsValid() && LocalAddr->GetPort() > 0 ? LocalAddr->GetPort() : World->URL.Port;
		Info.Players = GameMode->GetNumHumanPlayers();
		Info.MaxPlayers = GameMode->GameSession != nullptr ? GameMode->GameSession->MaxPlayers : 16;
		Info.Bots = GameMode->GetNumBots();
		Info.Dimension = GameState->GetMatchConfig().Dimension;
		Info.Seed = GameState->GetMatchConfig().Seed;
		Info.Version = UHCJEOSSubsystem::GetBuildVersion();
	}
	Online->UpdateHosting(bHosting, Info);
}

void AHCJPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsLocalController())
	{
		// This world's match is over for the local player: report what it earned, take the advertisement down.
		if (UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(this))
		{
			Online->FlushStats();
			Online->UpdateHosting(false, FHCJLobbyInfo());
		}
	}
	if (MenuWidget.IsValid() && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
	}
	MenuWidget.Reset();
	EndIntro();
	Super::EndPlay(EndPlayReason);
}

void AHCJPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsLocalController())
	{
		return;
	}

	if (bInStartMenu)
	{
		// The intro movie first (once per process); the title menu when it is over.
		if (!TickIntro(DeltaTime) && !MenuWidget.IsValid())
		{
			OpenMenu(EHCJMenuPage::Main, HCJMenuFlow::TakeNotice());
		}
		UpdateMenuCamera();
	}
	if (MenuWidget.IsValid() && MenuWidget->GetPage() == EHCJMenuPage::Connecting && HCJMenuFlow::TakeFailure())
	{
		// The join did not work out and this world is still the one running.
		MenuWidget->SetPage(bInStartMenu ? EHCJMenuPage::Main : EHCJMenuPage::Pause, HCJMenuFlow::TakeNotice());
	}

	UpdateOnline(DeltaTime);

	// The pilot name of the settings, once: a join by address or a start without the menu does not carry it.
	if (!bNameSent && PlayerState != nullptr)
	{
		bNameSent = true;
		UHCJUserSettings& Settings = UHCJUserSettings::Get();
		Settings.Sanitize();
		const FString Name = SanitizeName(Settings.PlayerName);
		if (!Name.IsEmpty() && PlayerState->GetPlayerName() != Name)
		{
			SetName(Name);
		}
	}

	// The server needs the lock for this ship's homing missiles.
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->SetLockTarget(GetTarget());
	}

	if (FirstShipTime < 0.0f && FirstShipTime > -100.0f && !GHelpAutoShown && GetShip() != nullptr)
	{
		GHelpAutoShown = true;
		FirstShipTime = static_cast<float>(GetWorld()->GetTimeSeconds());
	}
}

AHCJShipCharacter* AHCJPlayerController::GetTarget() const
{
	AHCJShipCharacter* Ship = Target.Get();
	return (IsValid(Ship) && !Ship->IsDead() && !Ship->IsCloaked() && Ship != GetPawn()) ? Ship : nullptr;
}

void AHCJPlayerController::SetAutoTarget(AHCJShipCharacter* NewTarget)
{
	if (!IsTargetManual())
	{
		bManualTarget = false;
		Target = NewTarget;
	}
}

namespace
{
	/** Live, uncloaked ships other than Own within reach of the HUD markers, nearest first. */
	void CollectEnemies(const AHCJShipCharacter& Own, TArray<AHCJShipCharacter*>& OutEnemies)
	{
		const FVector Origin = Own.GetActorLocation();
		for (TActorIterator<AHCJShipCharacter> It(Own.GetWorld()); It; ++It)
		{
			if (*It != &Own && !It->IsDead() && !It->IsCloaked() && FVector::DistSquared(It->GetActorLocation(), Origin) < FMath::Square(60000.0))
			{
				OutEnemies.Add(*It);
			}
		}
		OutEnemies.Sort([Origin](const AHCJShipCharacter& A, const AHCJShipCharacter& B)
		{
			return FVector::DistSquared(A.GetActorLocation(), Origin) < FVector::DistSquared(B.GetActorLocation(), Origin);
		});
	}
}

void AHCJPlayerController::TargetNext()
{
	const AHCJShipCharacter* Ship = GetShip();
	if (Ship == nullptr || IsMenuOpen())
	{
		return;
	}
	TArray<AHCJShipCharacter*> Enemies;
	CollectEnemies(*Ship, Enemies);
	if (Enemies.Num() == 0)
	{
		return;
	}
	const int32 Index = Enemies.IndexOfByKey(GetTarget());
	Target = Enemies[(Index + 1) % Enemies.Num()];
	bManualTarget = true;
	++TargetKeyPresses;
}

void AHCJPlayerController::TargetNearest()
{
	const AHCJShipCharacter* Ship = GetShip();
	if (Ship == nullptr || IsMenuOpen())
	{
		return;
	}
	TArray<AHCJShipCharacter*> Enemies;
	CollectEnemies(*Ship, Enemies);
	if (Enemies.Num() > 0)
	{
		Target = Enemies[0];
		bManualTarget = true;
		++TargetKeyPresses;
	}
}

void AHCJPlayerController::TargetUnderCrosshair()
{
	if (IsMenuOpen())
	{
		return;
	}
	// The soft lock already holds the enemy nearest the crosshair: keep it, or let it go again.
	bManualTarget = !IsTargetManual() && GetTarget() != nullptr;
	++TargetKeyPresses;
}

void AHCJPlayerController::ClientMissileWarning_Implementation()
{
	const UWorld* World = GetWorld();
	const float Now = World != nullptr ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;
	if (Now - LastMissileWarningTime > 0.6f)
	{
		const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
		UHCJAssetSettings::PlaySound2D(this, Settings.LowHealthSounds, TEXT("missile warning"), Settings.UiCueVolume);
	}
	LastMissileWarningTime = Now;
}

void AHCJPlayerController::HCJParkEnemy(float Distance, float Right, float Up)
{
#if !UE_BUILD_SHIPPING
	AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr;
	if (Ship == nullptr)
	{
		return;
	}
	// Any other live ship, from whatever sector: the nearest.
	TArray<AHCJShipCharacter*> Enemies;
	for (TActorIterator<AHCJShipCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Ship && !It->IsDead())
		{
			Enemies.Add(*It);
		}
	}
	if (Enemies.Num() == 0)
	{
		UE_LOG(LogHCJ, Log, TEXT("Debug: no other ship to park"));
		return;
	}
	const FVector Origin = Ship->GetActorLocation();
	Enemies.Sort([Origin](const AHCJShipCharacter& A, const AHCJShipCharacter& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), Origin) < FVector::DistSquared(B.GetActorLocation(), Origin);
	});
	const FRotationMatrix Axes(GetControlRotation());
	const FVector Location = Ship->GetActorLocation() + Axes.GetUnitAxis(EAxis::X) * Distance + Axes.GetUnitAxis(EAxis::Y) * Right + Axes.GetUnitAxis(EAxis::Z) * Up;
	AHCJShipCharacter* Enemy = Enemies[0];
	Enemy->GetCharacterMovement()->StopMovementImmediately();
	// Side on, so the hull is seen along its length.
	Enemy->TeleportTo(Location, FRotator(0.0, GetControlRotation().Yaw + 90.0, 0.0), false, true);
	if (AController* EnemyController = Enemy->GetController())
	{
		EnemyController->SetControlRotation(Enemy->GetActorRotation());
	}
	UE_LOG(LogHCJ, Log, TEXT("Debug: %s parked %.0f uu ahead"), *GetNameSafe(Enemy), Distance);
#endif
}

void AHCJPlayerController::UpdateMenuCamera()
{
	const AHCJShipCharacter* Current = Cast<AHCJShipCharacter>(GetViewTarget());
	if (Current != nullptr && !Current->IsDead())
	{
		return;
	}
	for (TActorIterator<AHCJShipCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && It->GetController() != nullptr)
		{
			SetViewTarget(*It);
			return;
		}
	}
}

void AHCJPlayerController::MenuPlay()
{
	UHCJUserSettings& Settings = UHCJUserSettings::Get();
	Settings.Save();
	AHCJGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHCJGameMode>() : nullptr;
	if (GameMode == nullptr)
	{
		return;
	}
	HCJMenuFlow::MarkPlayed();
	bInStartMenu = false;
	CloseMenu();
	const FString Name = SanitizeName(Settings.PlayerName);
	if (!Name.IsEmpty())
	{
		SetName(Name);
	}
	GameMode->StartMatchFromMenu(this, Settings.BotCount, Settings.Dimension, Settings.Seed, Settings.BotDifficulty);
}

void AHCJPlayerController::MenuJoin(const FString& Address)
{
	UHCJUserSettings& Settings = UHCJUserSettings::Get();
	const FString Host = Address.TrimStartAndEnd();
	if (Host.IsEmpty() || Host.Contains(TEXT(" ")) || Host.Contains(TEXT("?")))
	{
		if (MenuWidget.IsValid())
		{
			MenuWidget->SetPage(MenuWidget->GetPage(), TEXT("Type the address of a host first, e.g. 192.168.1.20"));
		}
		return;
	}
	Settings.LastJoinAddress = Host;
	Settings.Save();

	FString URL = Host;
	const FString Name = SanitizeName(Settings.PlayerName);
	if (!Name.IsEmpty())
	{
		URL += TEXT("?Name=") + Name;
	}
	UE_LOG(LogHCJ, Log, TEXT("Menu: joining %s"), *Host);
	HCJMenuFlow::TakeFailure();
	OpenMenu(EHCJMenuPage::Connecting, Host);
	ClientTravel(URL, TRAVEL_Absolute);
}

void AHCJPlayerController::MenuJoinLobby(int32 Index)
{
	const UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(this);
	if (Online == nullptr || !Online->GetLobbies().IsValidIndex(Index))
	{
		return;
	}
	const FHCJLobbyInfo& Lobby = Online->GetLobbies()[Index];
	const FString Address = HCJEos::PickJoinAddress(Lobby, UHCJEOSSubsystem::GetLocalAddresses());
	if (Address.IsEmpty() || Lobby.Version != UHCJEOSSubsystem::GetBuildVersion())
	{
		if (MenuWidget.IsValid())
		{
			MenuWidget->SetPage(EHCJMenuPage::Lobbies, Address.IsEmpty() ? TEXT("That game gives no address to connect to") : TEXT("That game runs another version"));
		}
		return;
	}
	UE_LOG(LogHCJ, Log, TEXT("Menu: joining the game of '%s' found through Epic Online Services"), *Lobby.HostName);
	MenuJoin(Address);
}

void AHCJPlayerController::MenuCancelConnect()
{
	ConsoleCommand(TEXT("cancel"));
	HCJMenuFlow::TakeFailure();
	HCJMenuFlow::TakeNotice();
	if (MenuWidget.IsValid())
	{
		MenuWidget->SetPage(bInStartMenu ? EHCJMenuPage::Main : EHCJMenuPage::Pause);
	}
}

void AHCJPlayerController::MenuResume()
{
	if (!bInStartMenu)
	{
		CloseMenu();
	}
}

void AHCJPlayerController::MenuLeave()
{
	UHCJUserSettings::Get().Save();
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (AHCJGameMode* GameMode = World->GetAuthGameMode<AHCJGameMode>())
	{
		// The host: the match goes on with the bots (and whoever is connected) behind the title menu.
		GameMode->ReturnToMenu(this);
		EnterStartMenu();
		if (UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(this))
		{
			// What the abandoned match earned (the score line stays); the advertisement goes with the next UpdateOnline.
			Online->FlushStats(/*bScoreLineGone*/ false);
		}
		OpenMenu(EHCJMenuPage::Main);
	}
	else
	{
		UE_LOG(LogHCJ, Log, TEXT("Menu: leaving the server"));
		HCJMenuFlow::RequestMenu();
		ClientTravel(TEXT("/Engine/Maps/Entry?listen"), TRAVEL_Absolute);
	}
}

void AHCJPlayerController::MenuQuit()
{
	UHCJUserSettings::Get().Save();
	ConsoleCommand(TEXT("quit"));
}

void AHCJPlayerController::MenuEscape()
{
	if (bInStartMenu)
	{
		return;
	}
	if (MenuWidget.IsValid())
	{
		CloseMenu();
	}
	else
	{
		OpenMenu(EHCJMenuPage::Pause);
	}
}

void AHCJPlayerController::ClientDamageTaken_Implementation(FVector_NetQuantize SourceLocation, float Amount)
{
	LastDamageSource = SourceLocation;
	LastDamageAmount = Amount;
	LastDamageTime = GetWorld() != nullptr ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.0f;
}

void AHCJPlayerController::HCJGotoHole(float InfluenceFraction)
{
	ServerDebugGotoHole(InfluenceFraction);
}

void AHCJPlayerController::ServerDebugGotoHole_Implementation(float InfluenceFraction)
{
#if !UE_BUILD_SHIPPING
	AHCJShipCharacter* Ship = GetShip();
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	if (Ship == nullptr || Ship->IsDead() || Universe == nullptr || !Universe->IsBuilt())
	{
		return;
	}

	// The hole of the ship's own sector, else of the next sector that has one.
	const int32 NumSectors = Universe->NumSectors();
	const int32 StartSector = FMath::Max(Universe->GetSectorAtLocation(Ship->GetActorLocation()), 0);
	for (int32 Offset = 0; Offset < NumSectors; ++Offset)
	{
		const int32 Sector = (StartSector + Offset) % NumSectors;
		if (!Universe->HasBlackHole(Sector))
		{
			continue;
		}
		const FVector HoleLocation = Universe->GetBlackHoleLocation(Sector);
		const FVector Inward = (Universe->GetSectorOrigin(Sector) - HoleLocation).GetSafeNormal();
		const float Distance = FMath::Clamp(InfluenceFraction, 0.05f, 2.0f) * Universe->GetBlackHoleSettings().InfluenceRadius;
		const FVector Location = HoleLocation + Inward * Distance;
		const FRotator Rotation = (-Inward).Rotation();
		Ship->GetCharacterMovement()->StopMovementImmediately();
		Ship->TeleportTo(Location, Rotation, false, true);
		SetControlRotation(Rotation);
		ClientSetRotation(Rotation);
		UE_LOG(LogHCJ, Log, TEXT("Debug: %s moved to %.0f uu from the black hole of sector %d"), *GetNameSafe(Ship), Distance, Sector);
		return;
	}
	UE_LOG(LogHCJ, Log, TEXT("Debug: no black hole in this match"));
#endif
}

void AHCJPlayerController::HCJGive(int32 Item)
{
	ServerDebugGive(Item);
}

void AHCJPlayerController::ServerDebugGive_Implementation(int32 Item)
{
#if !UE_BUILD_SHIPPING
	AHCJShipCharacter* Ship = GetShip();
	if (Ship != nullptr && Item >= 0 && Item < static_cast<int32>(EHCJPickupItem::Count))
	{
		const bool bTaken = Ship->ServerGiveItem(static_cast<EHCJPickupItem>(Item));
		UE_LOG(LogHCJ, Log, TEXT("Debug: %s given %s (%s)"), *GetNameSafe(Ship), HCJ::GetItemName(static_cast<EHCJPickupItem>(Item)), bTaken ? TEXT("taken") : TEXT("refused"));
		if (bTaken)
		{
			ClientNotifyPickup(static_cast<uint8>(Item));
		}
	}
#endif
}

void AHCJPlayerController::HCJGotoWormhole(float Distance)
{
	ServerDebugGotoWormhole(Distance);
}

void AHCJPlayerController::ServerDebugGotoWormhole_Implementation(float Distance)
{
#if !UE_BUILD_SHIPPING
	AHCJShipCharacter* Ship = GetShip();
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	if (Ship == nullptr || Universe == nullptr || !Universe->IsBuilt())
	{
		return;
	}
	const int32 NumSectors = Universe->NumSectors();
	const int32 StartSector = FMath::Max(Universe->GetSectorAtLocation(Ship->GetActorLocation()), 0);
	for (int32 Offset = 0; Offset < NumSectors; ++Offset)
	{
		const int32 Sector = (StartSector + Offset) % NumSectors;
		if (!Universe->HasWormhole(Sector))
		{
			continue;
		}
		const FVector Mouth = Universe->GetWormholeLocation(Sector);
		const FVector Inward = (Universe->GetSectorOrigin(Sector) - Mouth).GetSafeNormal();
		const FVector Location = Mouth + Inward * FMath::Clamp(Distance, 800.0f, 10000.0f);
		const FRotator Rotation = (-Inward).Rotation();
		Ship->GetCharacterMovement()->StopMovementImmediately();
		Ship->TeleportTo(Location, Rotation, false, true);
		SetControlRotation(Rotation);
		ClientSetRotation(Rotation);
		UE_LOG(LogHCJ, Log, TEXT("Debug: %s moved in front of the wormhole of sector %d (to sector %d)"), *GetNameSafe(Ship), Sector, Universe->GetWormholeDest(Sector));
		return;
	}
	UE_LOG(LogHCJ, Log, TEXT("Debug: no wormhole in this match"));
#endif
}

void AHCJPlayerController::HCJGotoStation(float Distance)
{
	ServerDebugGotoStation(Distance);
}

void AHCJPlayerController::ServerDebugGotoStation_Implementation(float Distance)
{
#if !UE_BUILD_SHIPPING
	AHCJShipCharacter* Ship = GetShip();
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);
	if (Ship == nullptr || Universe == nullptr || !Universe->IsBuilt())
	{
		return;
	}
	const int32 NumSectors = Universe->NumSectors();
	const int32 StartSector = FMath::Max(Universe->GetSectorAtLocation(Ship->GetActorLocation()), 0);
	for (int32 Offset = 0; Offset < NumSectors; ++Offset)
	{
		const int32 Sector = (StartSector + Offset) % NumSectors;
		if (!Universe->HasStation(Sector))
		{
			continue;
		}
		const FVector Station = Universe->GetStationLocation(Sector);
		// From a little above one of its diagonals, so the whole shape is in view.
		const FVector From = FVector(1.0, 0.6, 0.45).GetSafeNormal();
		const FVector Location = Station + From * FMath::Clamp(Distance, 2600.0f, 20000.0f);
		const FRotator Rotation = (-From).Rotation();
		Ship->GetCharacterMovement()->StopMovementImmediately();
		Ship->TeleportTo(Location, Rotation, false, true);
		SetControlRotation(Rotation);
		ClientSetRotation(Rotation);
		UE_LOG(LogHCJ, Log, TEXT("Debug: %s moved to %.0f uu from the station of sector %d"), *GetNameSafe(Ship), Distance, Sector);
		return;
	}
	UE_LOG(LogHCJ, Log, TEXT("Debug: no station in this match"));
#endif
}

void AHCJPlayerController::HCJMenuCmd(const FString& Action, const FString& Argument)
{
#if !UE_BUILD_SHIPPING
	UE_LOG(LogHCJ, Log, TEXT("Debug: menu command '%s' '%s'"), *Action, *Argument);
	if (Action == TEXT("play")) { MenuPlay(); }
	else if (Action == TEXT("join")) { MenuJoin(Argument); }
	else if (Action == TEXT("pause")) { MenuEscape(); }
	else if (Action == TEXT("resume")) { MenuResume(); }
	else if (Action == TEXT("leave")) { MenuLeave(); }
	else if (Action == TEXT("quit")) { MenuQuit(); }
	else if (Action == TEXT("help")) { ToggleHelp(); }
	else if (Action == TEXT("scores")) { bScoreboardVisible = !bScoreboardVisible; }
	else if (Action == TEXT("hints")) { ToggleHints(); }
	else if (Action == TEXT("skiptutorial")) { AHCJHUD::DebugSkipOnboarding(); }
	else if (Action == TEXT("resethints")) { AHCJHUD::DebugResetHints(); }
	else if (Action == TEXT("settings") && MenuWidget.IsValid()) { MenuWidget->SetPage(EHCJMenuPage::Settings); }
	else if (Action == TEXT("controls") && MenuWidget.IsValid()) { MenuWidget->SetPage(EHCJMenuPage::Controls); }
	else if (Action == TEXT("die"))
	{
		if (AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr)
		{
			// Credited to another ship when there is one, so that the respawn screen shows what a real kill shows.
			AHCJShipCharacter* Other = nullptr;
			for (TActorIterator<AHCJShipCharacter> It(GetWorld()); It; ++It)
			{
				if (*It != Ship && !It->IsDead() && It->GetController() != nullptr)
				{
					Other = *It;
					break;
				}
			}
			Ship->TakeDamage(100000.0f, FDamageEvent(), Other != nullptr ? Other->GetController() : nullptr, Other);
		}
	}
	else if (Action == TEXT("main") && MenuWidget.IsValid()) { MenuWidget->SetPage(EHCJMenuPage::Main); }
	else if (Action == TEXT("lobbies") && MenuWidget.IsValid()) { MenuWidget->SetPage(EHCJMenuPage::Lobbies); }
	else if (Action == TEXT("leaderboards") && MenuWidget.IsValid()) { MenuWidget->SetPage(EHCJMenuPage::Leaderboards); }
	else if (Action == TEXT("refresh"))
	{
		if (UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(this))
		{
			Online->RefreshLobbies();
			Online->RefreshLeaderboards();
		}
	}
	else if (Action == TEXT("joinlobby")) { MenuJoinLobby(FCString::Atoi(*Argument)); }
	else if (Action == TEXT("endmatch"))
	{
		if (AHCJGameMode* GameMode = GetWorld()->GetAuthGameMode<AHCJGameMode>())
		{
			GameMode->DebugEndMatch();
		}
	}
	else if (Action == TEXT("shieldhit") || Action == TEXT("enemyhit"))
	{
		// "shieldhit 2,15": 15 damage on the port facing (0 fore, 1 aft, 2 port, 3 starboard) of the own ship;
		// "enemyhit ..." the same on the locked target, else the nearest other ship. A third number turns the
		// impact point by that many degrees. Listen host only.
		TArray<FString> Parts;
		Argument.ParseIntoArray(Parts, TEXT(","));
		const int32 Facing = Parts.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Parts[0]), 0, HCJShield::NumFacings - 1) : 0;
		const float Damage = Parts.Num() > 1 ? FMath::Max(FCString::Atof(*Parts[1]), 0.1f) : 10.0f;
		const float Turn = Parts.Num() > 2 ? FCString::Atof(*Parts[2]) : 0.0f;
		AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr;
		AHCJShipCharacter* Victim = Ship;
		if (Ship != nullptr && Action == TEXT("enemyhit"))
		{
			Victim = Ship->GetLockTarget();
			double BestDistSq = TNumericLimits<double>::Max();
			for (TActorIterator<AHCJShipCharacter> It(GetWorld()); It && Ship->GetLockTarget() == nullptr; ++It)
			{
				const double DistSq = FVector::DistSquared(It->GetActorLocation(), Ship->GetActorLocation());
				if (*It != Ship && !It->IsDead() && DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Victim = *It;
				}
			}
		}
		if (Victim != nullptr)
		{
			const FVector LocalDirection = FRotator(0.0, HCJShield::FacingYawDegrees(static_cast<EHCJShieldFacing>(Facing)) + Turn, 0.0).Vector();
			const FVector WorldDirection = Victim->GetActorTransform().TransformVectorNoScale(LocalDirection);
			FHitResult Hit;
			Hit.ImpactPoint = Victim->GetActorLocation() + WorldDirection * 100.0;
			Hit.Location = Hit.ImpactPoint;
			Hit.ImpactNormal = WorldDirection;
			const UHCJHealthComponent* Health = Victim->GetHealthComponent();
			Victim->TakeDamage(Damage, FPointDamageEvent(Damage, Hit, -WorldDirection, UDamageType::StaticClass()), Victim == Ship ? nullptr : this, nullptr);
			if (Victim == Ship)
			{
				ClientDamageTaken(Hit.ImpactPoint + WorldDirection * 3000.0, Damage);
			}
			UE_LOG(LogHCJ, Log, TEXT("Debug: %.0f damage on the %s facing of %s -> facings %.1f %.1f %.1f %.1f, hull %.0f"), Damage,
				HCJShield::FacingName(static_cast<EHCJShieldFacing>(Facing)), *GetNameSafe(Victim),
				Health != nullptr ? Health->GetFacingShield(0) : 0.0f, Health != nullptr ? Health->GetFacingShield(1) : 0.0f,
				Health != nullptr ? Health->GetFacingShield(2) : 0.0f, Health != nullptr ? Health->GetFacingShield(3) : 0.0f,
				Health != nullptr ? Health->GetHealth() : 0.0f);
		}
	}
	else if (Action == TEXT("torpedo"))
	{
		if (AHCJShipCharacter* Ship = GetShip())
		{
			Ship->FireTorpedo();
		}
	}
	else if (Action == TEXT("slomo"))
	{
		// Listen host: the whole game at a fraction of its speed, for pictures of things that are over in a moment.
		if (HasAuthority() && GetWorld()->GetWorldSettings() != nullptr)
		{
			GetWorld()->GetWorldSettings()->SetTimeDilation(FMath::Clamp(Argument.IsEmpty() ? 1.0f : FCString::Atof(*Argument), 0.02f, 1.0f));
		}
	}
	else if (Action == TEXT("torpedostock"))
	{
		// Listen host: as many restocks as it takes, or a volley into the void, is not needed - give or take directly.
		if (AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr)
		{
			for (int32 Step = 0; Step < 4 && Ship->GetTorpedoCount() < FCString::Atoi(*Argument); ++Step)
			{
				Ship->ServerGiveItem(EHCJPickupItem::Torpedoes);
			}
		}
	}
	else if (Action == TEXT("enemytorpedo"))
	{
		// Listen host: the nearest other ship, put Argument uu ahead (default 4000) and a little to the side, locks us and fires.
		AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr;
		AHCJShipCharacter* Enemy = nullptr;
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<AHCJShipCharacter> It(GetWorld()); It && Ship != nullptr; ++It)
		{
			const double DistSq = FVector::DistSquared(It->GetActorLocation(), Ship->GetActorLocation());
			if (*It != Ship && !It->IsDead() && It->GetController() != nullptr && DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Enemy = *It;
			}
		}
		if (Enemy != nullptr)
		{
			const float Distance = FMath::Clamp(Argument.IsEmpty() ? 4000.0f : FCString::Atof(*Argument), 1200.0f, 15000.0f);
			const FRotationMatrix Axes(GetControlRotation());
			const FVector Location = Ship->GetActorLocation() + Axes.GetUnitAxis(EAxis::X) * Distance + Axes.GetUnitAxis(EAxis::Y) * (0.25 * Distance) + Axes.GetUnitAxis(EAxis::Z) * (0.1 * Distance);
			const FRotator Facing = (Ship->GetActorLocation() - Location).Rotation();
			Enemy->GetCharacterMovement()->StopMovementImmediately();
			Enemy->TeleportTo(Location, Facing, false, true);
			Enemy->GetController()->SetControlRotation(Facing);
			Enemy->SetLockTarget(Ship);
			Enemy->FireTorpedo();
			UE_LOG(LogHCJ, Log, TEXT("Debug: %s fires a torpedo at the own ship from %.0f uu (%d left)"), *GetNameSafe(Enemy), Distance, Enemy->GetTorpedoCount());
		}
	}
	else if (Action == TEXT("fire"))
	{
		// The own trigger, held (1) or let go (0): for pictures of the phaser beam.
		if (AHCJShipCharacter* Ship = GetShip())
		{
			Ship->SetFiring(Argument.IsEmpty() || FCString::Atoi(*Argument) != 0);
		}
	}
	else if (Action == TEXT("baseweapon"))
	{
		ToggleBaseWeapon();
	}
	else if (Action == TEXT("god"))
	{
		// Listen host: the own ship takes no damage from now on (so that it lives through a series of pictures).
		if (AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr)
		{
			Ship->SetCanBeDamaged(Argument == TEXT("0"));
		}
	}
	else if (Action == TEXT("breakrock"))
	{
		// Listen host: destroys the rock nearest to the own ship (Argument: only rocks of at least that bounding radius), as a phaser or a torpedo would.
		AHCJUniverse* Universe = AHCJUniverse::Get(this);
		const AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr;
		if (Ship != nullptr && Universe != nullptr && Universe->IsBuilt())
		{
			const uint32 Key = Universe->FindNearestAsteroid(Ship->GetActorLocation() + GetControlRotation().Vector() * 1500.0, Argument.IsEmpty() ? 0.0f : FCString::Atof(*Argument));
			FVector Location = FVector::ZeroVector;
			float Radius = 0.0f;
			if (Key != MAX_uint32 && Universe->GetAsteroidSphere(Key, Location, Radius))
			{
				// Look at it, so that a picture taken now shows it.
				SetControlRotation((Location - Ship->GetActorLocation()).Rotation());
				UE_LOG(LogHCJ, Log, TEXT("Debug: breaking the rock %04X of sector %d (radius %.0f, %.0f uu away)"), HCJAsteroids::GetKeyId(Key), HCJAsteroids::GetKeySector(Key), Radius,
					FVector::Dist(Location, Ship->GetActorLocation()));
				Universe->ServerDamageAsteroidKey(Key, 100000.0f, this);
			}
		}
	}
	else if (Action == TEXT("rockstatus"))
	{
		// Any machine: what this machine believes about the rocks.
		if (const AHCJUniverse* Universe = AHCJUniverse::Get(this))
		{
			UE_LOG(LogHCJ, Log, TEXT("Asteroids (%s) status: %d destroyed, checksum %08X, %d rocks in the world, layout checksum %08X"), HasAuthority() ? TEXT("server") : TEXT("client"),
				Universe->GetNumDestroyedAsteroids(), Universe->GetDestroyedChecksum(), Universe->GetNumLiveAsteroids(), Universe->GetLayoutChecksum());
		}
	}
	else if (Action == TEXT("killenemy"))
	{
		// Listen host: the nearest other ship blows up, credited to the own ship (for pictures of the explosion).
		AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr;
		AHCJShipCharacter* Enemy = nullptr;
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<AHCJShipCharacter> It(GetWorld()); It && Ship != nullptr; ++It)
		{
			const double DistSq = FVector::DistSquared(It->GetActorLocation(), Ship->GetActorLocation());
			if (*It != Ship && !It->IsDead() && DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Enemy = *It;
			}
		}
		if (Enemy != nullptr)
		{
			Enemy->Die(this, Ship);
		}
	}
	else if (Action == TEXT("hurt"))
	{
		if (AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr)
		{
			Ship->TakeDamage(FMath::Max(FCString::Atof(*Argument), 1.0f), FDamageEvent(), nullptr, nullptr);
			ClientDamageTaken(Ship->GetActorLocation() + Ship->GetActorRightVector() * 3000.0, FMath::Max(FCString::Atof(*Argument), 1.0f));
		}
	}
	else if (Action == TEXT("skipintro"))
	{
		// What any key does while the intro movie runs.
		IntroFlow.RequestSkip();
	}
	else if (Action == TEXT("peace"))
	{
		// Listen host: bots hold still and hold their fire ("peace 0" ends it), so that a parked enemy stays where it is.
		AHCJBotController::SetDebugIdle(Argument != TEXT("0"));
	}
	else if (Action == TEXT("scene"))
	{
		// Listen host, for repeatable pictures: "scene <sector>,<look>[,<spawn point>]" puts the own ship (made undamageable)
		// at a spawn point of the sector and turns it towards <look>: body0 / body1 (the planets of that sector's sky),
		// nebula0..2, dark (the direction farthest from all of them and the sun), portal0.. (the n-th portal), wormhole,
		// station - for these three a fourth number is the distance to look from ("scene 2,portal0,0,3000").
		// Follow with HCJParkEnemy <uu> for an enemy at a known distance in that direction.
		TArray<FString> Parts;
		Argument.ParseIntoArray(Parts, TEXT(","));
		AHCJShipCharacter* Ship = HasAuthority() ? GetShip() : nullptr;
		const AHCJUniverse* Universe = AHCJUniverse::Get(this);
		const int32 Sector = Parts.Num() > 0 ? FCString::Atoi(*Parts[0]) : 0;
		const FHCJSectorLayout* Layout = Universe != nullptr ? Universe->GetSectorLayout(Sector) : nullptr;
		if (Ship != nullptr && Layout != nullptr && Layout->SpawnPoints.Num() > 0)
		{
			const FString Look = Parts.Num() > 1 ? Parts[1] : TEXT("dark");
			const int32 SpawnIndex = Parts.Num() > 2 ? FMath::Clamp(FCString::Atoi(*Parts[2]), 0, Layout->SpawnPoints.Num() - 1) : 0;
			FVector Location = Universe->GetSectorOrigin(Sector) + Layout->SpawnPoints[SpawnIndex].GetLocation();
			// A fourth number: for portal / wormhole / station, the distance (uu) to look at it from, on the line from the spawn point.
			const float ViewDistance = Parts.Num() > 3 ? FCString::Atof(*Parts[3]) : 0.0f;
			FVector LookTarget = FVector::ZeroVector;
			bool bHasLookTarget = false;
			const AHCJSpaceBackdrop* Backdrop = Universe->GetBackdrop();
			const FHCJSectorSky Sky = Backdrop != nullptr ? Backdrop->MakeSky(Sector) : FHCJSectorSky();
			FVector Direction = Layout->SpawnPoints[SpawnIndex].GetRotation().GetForwardVector();
			const int32 Index = Look.Len() > 0 && FChar::IsDigit(Look[Look.Len() - 1]) ? Look[Look.Len() - 1] - TEXT('0') : 0;
			if (Look.StartsWith(TEXT("body")) && Sky.Bodies.IsValidIndex(Index))
			{
				Direction = Sky.Bodies[Index].Direction;
			}
			else if (Look.StartsWith(TEXT("nebula")) && Sky.Nebulae.IsValidIndex(Index))
			{
				Direction = Sky.Nebulae[Index].Direction;
			}
			else if (Look.StartsWith(TEXT("portal")))
			{
				int32 Seen = 0;
				for (int32 Dim = 0; Dim < FHCJHypercubeGraph::MaxDimension; ++Dim)
				{
					if (Universe->HasPortal(Sector, Dim) && Seen++ == Index)
					{
						LookTarget = Universe->GetPortalWorldTransform(Sector, Dim).GetLocation();
						bHasLookTarget = true;
					}
				}
			}
			else if (Look == TEXT("wormhole") && Universe->HasWormhole(Sector))
			{
				LookTarget = Universe->GetWormholeLocation(Sector);
				bHasLookTarget = true;
			}
			else if (Look == TEXT("station") && Universe->HasStation(Sector))
			{
				LookTarget = Universe->GetStationLocation(Sector);
				bHasLookTarget = true;
			}
			else if (Look == TEXT("dark"))
			{
				// Of 300 evenly spread directions the one farthest (by angle) from every body, nebula centre and the sun.
				TArray<FVector> Avoid;
				for (const FHCJSectorSky::FBody& Body : Sky.Bodies) { Avoid.Add(Body.Direction); }
				for (const FHCJSectorSky::FNebula& Nebula : Sky.Nebulae) { Avoid.Add(Nebula.Direction); }
				Avoid.Add(FRotator(-35.0, 40.0, 0.0).Vector() * -1.0);
				double Best = -2.0;
				for (int32 Step = 0; Step < 300; ++Step)
				{
					const double Z = 0.6 * (1.0 - 2.0 * (Step + 0.5) / 300.0);
					const double Phi = Step * 2.399963;
					const double Ring = FMath::Sqrt(1.0 - Z * Z);
					const FVector Candidate(Ring * FMath::Cos(Phi), Ring * FMath::Sin(Phi), Z);
					double Worst = 2.0;
					for (const FVector& Other : Avoid) { Worst = FMath::Min(Worst, 1.0 - FVector::DotProduct(Candidate, Other)); }
					if (Worst > Best) { Best = Worst; Direction = Candidate; }
				}
			}
			if (bHasLookTarget)
			{
				Direction = (LookTarget - Location).GetSafeNormal();
				if (ViewDistance > 0.0f)
				{
					Location = LookTarget - Direction * ViewDistance;
				}
			}
			const FRotator Rotation = Direction.Rotation();
			Ship->SetCanBeDamaged(false);
			Ship->GetCharacterMovement()->StopMovementImmediately();
			Ship->TeleportTo(Location, Rotation, false, true);
			SetControlRotation(Rotation);
			ClientSetRotation(Rotation);
			UE_LOG(LogHCJ, Log, TEXT("Debug: scene in sector %d at spawn point %d, looking at %s (%s)"), Sector, SpawnIndex, *Look, *Direction.ToCompactString());
		}
	}
#endif
}

void AHCJPlayerController::HCJKillTurrets()
{
	ServerDebugKillTurrets();
}

void AHCJPlayerController::ServerDebugKillTurrets_Implementation()
{
#if !UE_BUILD_SHIPPING
	int32 NumDestroyed = 0;
	for (TActorIterator<AHCJTurret> It(GetWorld()); It; ++It)
	{
		if (!It->IsTurretDestroyed())
		{
			It->TakeDamage(100000.0f, FDamageEvent(), this, GetPawn());
			++NumDestroyed;
		}
	}
	UE_LOG(LogHCJ, Log, TEXT("Debug: %d turrets destroyed"), NumDestroyed);
#endif
}

void AHCJPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Esc opens / closes the in-match menu (which has Quit); F1 the controls overlay.
	if (InputComponent != nullptr)
	{
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AHCJPlayerController::MenuEscape);
		InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &AHCJPlayerController::ToggleHelp);
		InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AHCJPlayerController::ToggleHints);
		InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AHCJPlayerController::ToggleBaseWeapon);
		InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AHCJPlayerController::TargetNext);
		InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AHCJPlayerController::TargetNearest);
		InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &AHCJPlayerController::TargetUnderCrosshair);
	}

	BuildInputObjects();
	RegisterMappingContext();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogHCJ, Error, TEXT("AHCJPlayerController needs UEnhancedInputComponent (DefaultInput.ini DefaultInputComponentClass); got %s"),
			*GetNameSafe(InputComponent ? InputComponent->GetClass() : nullptr));
		return;
	}

	EnhancedInput->ClearBindingsForObject(this);
	EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AHCJPlayerController::HandleMove);
	EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHCJPlayerController::HandleLook);
	EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &AHCJPlayerController::HandleFireStarted);
	EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this, &AHCJPlayerController::HandleFireCompleted);
	EnhancedInput->BindAction(BoostAction, ETriggerEvent::Started, this, &AHCJPlayerController::HandleBoostStarted);
	EnhancedInput->BindAction(BoostAction, ETriggerEvent::Completed, this, &AHCJPlayerController::HandleBoostCompleted);
	EnhancedInput->BindAction(ScoreboardAction, ETriggerEvent::Started, this, &AHCJPlayerController::HandleScoreboardStarted);
	EnhancedInput->BindAction(ScoreboardAction, ETriggerEvent::Completed, this, &AHCJPlayerController::HandleScoreboardCompleted);
	EnhancedInput->BindAction(CameraAction, ETriggerEvent::Started, this, &AHCJPlayerController::HandleToggleCamera);
	EnhancedInput->BindAction(RespawnAction, ETriggerEvent::Started, this, &AHCJPlayerController::HandleRespawn);
	EnhancedInput->BindAction(DropWeaponAction, ETriggerEvent::Started, this, &AHCJPlayerController::HandleDropWeapon);
	EnhancedInput->BindAction(TorpedoAction, ETriggerEvent::Started, this, &AHCJPlayerController::HandleTorpedo);

	if (IsLocalController() && !MenuWidget.IsValid())
	{
		SetInputMode(FInputModeGameOnly());
	}
}

void AHCJPlayerController::BuildInputObjects()
{
	if (MappingContext)
	{
		return;
	}

	const auto MakeAction = [this](const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(this, FName(Name));
		Action->ValueType = ValueType;
		return Action;
	};

	MoveAction = MakeAction(TEXT("IA_HCJ_Move"), EInputActionValueType::Axis3D);
	LookAction = MakeAction(TEXT("IA_HCJ_Look"), EInputActionValueType::Axis2D);
	FireAction = MakeAction(TEXT("IA_HCJ_Fire"), EInputActionValueType::Boolean);
	BoostAction = MakeAction(TEXT("IA_HCJ_Boost"), EInputActionValueType::Boolean);
	ScoreboardAction = MakeAction(TEXT("IA_HCJ_Scoreboard"), EInputActionValueType::Boolean);
	CameraAction = MakeAction(TEXT("IA_HCJ_Camera"), EInputActionValueType::Boolean);
	RespawnAction = MakeAction(TEXT("IA_HCJ_Respawn"), EInputActionValueType::Boolean);
	DropWeaponAction = MakeAction(TEXT("IA_HCJ_DropWeapon"), EInputActionValueType::Boolean);
	TorpedoAction = MakeAction(TEXT("IA_HCJ_Torpedo"), EInputActionValueType::Boolean);

	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, FName(TEXT("IMC_HCJ_Default")));

	MapAxisKey(*Context, MoveAction, EKeys::W, false, false, false);
	MapAxisKey(*Context, MoveAction, EKeys::S, false, false, true);
	MapAxisKey(*Context, MoveAction, EKeys::D, true, false, false);
	MapAxisKey(*Context, MoveAction, EKeys::A, true, false, true);
	MapAxisKey(*Context, MoveAction, EKeys::SpaceBar, false, true, false);
	MapAxisKey(*Context, MoveAction, EKeys::E, false, true, false);
	MapAxisKey(*Context, MoveAction, EKeys::LeftControl, false, true, true);
	MapAxisKey(*Context, MoveAction, EKeys::Q, false, true, true);

	Context->MapKey(LookAction, EKeys::Mouse2D);
	Context->MapKey(FireAction, EKeys::LeftMouseButton);
	Context->MapKey(BoostAction, EKeys::LeftShift);
	Context->MapKey(ScoreboardAction, EKeys::Tab);
	Context->MapKey(CameraAction, EKeys::V);
	Context->MapKey(RespawnAction, EKeys::R);
	Context->MapKey(RespawnAction, EKeys::Enter);
	Context->MapKey(DropWeaponAction, EKeys::G);
	Context->MapKey(TorpedoAction, EKeys::RightMouseButton);
	Context->MapKey(TorpedoAction, EKeys::X);

	MappingContext = Context;
}

void AHCJPlayerController::RegisterMappingContext()
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer || !MappingContext)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		if (!Subsystem->HasMappingContext(MappingContext))
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}
}

void AHCJPlayerController::HandleMove(const FInputActionValue& Value)
{
	AHCJShipCharacter* Ship = IsMenuOpen() ? nullptr : GetShip();
	if (Ship != nullptr)
	{
		// Two keys of the same direction (Space + E) add up; keep every axis in [-1, 1].
		const FVector Axes = Value.Get<FVector>();
		Ship->AddThrustInput(FVector(FMath::Clamp(Axes.X, -1.0, 1.0), FMath::Clamp(Axes.Y, -1.0, 1.0), FMath::Clamp(Axes.Z, -1.0, 1.0)));
	}
}

void AHCJPlayerController::HandleLook(const FInputActionValue& Value)
{
	if (!GetShip() || IsLookInputIgnored() || IsMenuOpen())
	{
		return;
	}

	const FVector2D Delta = Value.Get<FVector2D>();
	const float Sensitivity = LookSensitivity * FMath::Clamp(UHCJUserSettings::Get().MouseSensitivity, 0.2f, 3.0f);
	RotationInput.Yaw += Delta.X * Sensitivity;
	RotationInput.Pitch += Delta.Y * Sensitivity;
}

void AHCJPlayerController::HandleFireStarted(const FInputActionValue& Value)
{
	if (IsMenuOpen())
	{
		return;
	}
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->SetFiring(true);
	}
	else
	{
		ServerRequestRespawn();
	}
}

void AHCJPlayerController::HandleFireCompleted(const FInputActionValue& Value)
{
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->SetFiring(false);
	}
}

void AHCJPlayerController::HandleBoostStarted(const FInputActionValue& Value)
{
	if (IsMenuOpen())
	{
		return;
	}
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->SetBoosting(true);
	}
}

void AHCJPlayerController::HandleBoostCompleted(const FInputActionValue& Value)
{
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->SetBoosting(false);
	}
}

void AHCJPlayerController::HandleScoreboardStarted(const FInputActionValue& Value)
{
	bScoreboardVisible = true;
}

void AHCJPlayerController::HandleScoreboardCompleted(const FInputActionValue& Value)
{
	bScoreboardVisible = false;
}

void AHCJPlayerController::HandleToggleCamera(const FInputActionValue& Value)
{
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->ToggleCameraMode();
	}
}

void AHCJPlayerController::ToggleBaseWeapon()
{
	if (IsMenuOpen())
	{
		return;
	}
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->ToggleBaseWeapon();
	}
}

void AHCJPlayerController::HandleDropWeapon(const FInputActionValue& Value)
{
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->DropWeapon();
	}
}

void AHCJPlayerController::HandleTorpedo(const FInputActionValue& Value)
{
	if (IsMenuOpen())
	{
		return;
	}
	if (AHCJShipCharacter* Ship = GetShip())
	{
		Ship->FireTorpedo();
	}
}

void AHCJPlayerController::HandleRespawn(const FInputActionValue& Value)
{
	if (!GetShip() && !IsMenuOpen())
	{
		ServerRequestRespawn();
	}
}
