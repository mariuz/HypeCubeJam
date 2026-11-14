#include "Audio/HCJAudioDirector.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"

#include "Assets/HCJAssetSettings.h"
#include "Game/HCJGameState.h"
#include "Game/HCJPlayerController.h"
#include "Game/HCJPlayerState.h"
#include "Game/HCJUserSettings.h"
#include "HypeCubeJam.h"
#include "Ship/HCJHealthComponent.h"
#include "Ship/HCJShipCharacter.h"
#include "Ship/HCJShipMovementComponent.h"
#include "World/HCJPickup.h"
#include "World/HCJTurret.h"
#include "World/HCJUniverse.h"

namespace
{
	TAutoConsoleVariable<float> CVarMusicVolume(TEXT("hcj.Audio.MusicVolume"), -1.0f,
		TEXT("Music volume 0..2; negative = MusicVolume of [/Script/HypeCubeJam.HCJAssetSettings]."));
	TAutoConsoleVariable<float> CVarSfxVolume(TEXT("hcj.Audio.SfxVolume"), -1.0f,
		TEXT("Sound effect volume 0..2; negative = SfxVolume of [/Script/HypeCubeJam.HCJAssetSettings]."));
	TAutoConsoleVariable<float> CVarUiVolume(TEXT("hcj.Audio.UiVolume"), -1.0f,
		TEXT("Interface cue volume 0..2; negative = UiVolume of [/Script/HypeCubeJam.HCJAssetSettings]."));

	/** Another ship this close to the local view lifts the music out of its calm state. */
	constexpr float MusicTensionDistance = 9000.0f;
	constexpr float MusicTensionIntensity = 0.45f;
	/** Seconds the music stays intense after the last shot / hit. */
	constexpr float CombatHeatSeconds = 10.0f;

	float PickVolume(const TAutoConsoleVariable<float>& CVar, float ConfigValue)
	{
		const float Override = CVar.GetValueOnGameThread();
		return FMath::Clamp(Override >= 0.0f ? Override : ConfigValue, 0.0f, 2.0f);
	}
}

UHCJAudioDirector* UHCJAudioDirector::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World != nullptr ? World->GetSubsystem<UHCJAudioDirector>() : nullptr;
}

bool UHCJAudioDirector::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UHCJAudioDirector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHCJAudioDirector, STATGROUP_Tickables);
}

void UHCJAudioDirector::Deinitialize()
{
	if (BlackHoleLoop != nullptr)
	{
		BlackHoleLoop->Stop();
		BlackHoleLoop = nullptr;
	}
	StopMusicTracks();
	Super::Deinitialize();
}

void UHCJAudioDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer || GEngine == nullptr)
	{
		return;
	}
	APlayerController* Controller = GEngine->GetFirstLocalPlayerController(World);
	const AHCJGameState* GameState = World->GetGameState<AHCJGameState>();
	if (Controller == nullptr || GameState == nullptr)
	{
		return;
	}

	ApplyGroupVolumes();

	const float Now = static_cast<float>(World->GetTimeSeconds());
	AHCJShipCharacter* LocalShip = Cast<AHCJShipCharacter>(Controller->GetPawn());
	if (LocalShip != nullptr && LocalShip->IsDead())
	{
		LocalShip = nullptr;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const bool bIntermission = GameState->GetMatchPhase() == EHCJMatchPhase::Intermission;

	TickMatchCues();
	TickLocalPlayerCues(Now, LocalShip);
	TickBlackHoleRumble(LocalShip != nullptr ? LocalShip->GetActorLocation() : ViewLocation);
	// The title menu: the local host sits in it as a spectator while the bots play behind it.
	const AHCJPlayerController* HCJController = Cast<AHCJPlayerController>(Controller);
	const bool bTitleMenu = HCJController != nullptr && (HCJController->IsMenuOpen() || HCJController->IsInStartMenu()) && LocalShip == nullptr
		&& (Controller->PlayerState == nullptr || Controller->PlayerState->IsOnlyASpectator());
	if (!TickMusicTracks(DeltaTime, bTitleMenu, LocalShip, ViewLocation))
	{
		TickMusic(Now, DeltaTime, bIntermission, LocalShip, ViewLocation);
	}

	if (Now >= NextStatusTime)
	{
		NextStatusTime = Now + 15.0f;
		LogStatus(Now);
	}
}

void UHCJAudioDirector::ApplyGroupVolumes()
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	if (!bClassesLoaded)
	{
		bClassesLoaded = true;
		MusicClass = UHCJAssetSettings::Load<USoundClass>(Settings.MusicSoundClass, TEXT("music sound class"));
		SfxClass = UHCJAssetSettings::Load<USoundClass>(Settings.SfxSoundClass, TEXT("effects sound class"));
		UiClass = UHCJAssetSettings::Load<USoundClass>(Settings.UiSoundClass, TEXT("interface sound class"));
	}
	// The audio device reads the class properties every update, so this is all a volume slider needs.
	const UHCJUserSettings& User = UHCJUserSettings::Get();
	if (MusicClass != nullptr)
	{
		MusicClass->Properties.Volume = PickVolume(CVarMusicVolume, Settings.MusicVolume * User.MusicVolume);
	}
	if (SfxClass != nullptr)
	{
		SfxClass->Properties.Volume = PickVolume(CVarSfxVolume, Settings.SfxVolume * User.SfxVolume);
	}
	if (UiClass != nullptr)
	{
		UiClass->Properties.Volume = PickVolume(CVarUiVolume, Settings.UiVolume * User.UiVolume);
	}
}

void UHCJAudioDirector::NotifyHitConfirm(bool bKilled)
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	LastHitConfirmTime = static_cast<float>(World->GetTimeSeconds());
	if (bKilled)
	{
		LastKillConfirmTime = LastHitConfirmTime;
	}
	NotifyCombat();
	UHCJAssetSettings::PlaySound2D(this, Settings.HitConfirmSounds, TEXT("hit confirm"), Settings.UiCueVolume * 0.7f);
}

void UHCJAudioDirector::NotifyStreak(int32 Streak)
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	NotifyCombat();
	// The existing interface stingers: the kill cue again, plus the round stinger for a triple kill or a rampage.
	UHCJAssetSettings::PlaySound2D(this, Settings.KillSounds, TEXT("streak cue"), Settings.UiCueVolume);
	if (Streak >= 2)
	{
		UHCJAssetSettings::PlaySound2D(this, Settings.MatchStartSounds, TEXT("streak stinger"), Settings.UiCueVolume * 0.8f);
	}
}

void UHCJAudioDirector::NotifyKillEvent(const FString& KillerName, const FString& VictimName)
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer || GEngine == nullptr)
	{
		return;
	}
	const APlayerController* Controller = GEngine->GetFirstLocalPlayerController(World);
	const APlayerState* LocalState = Controller != nullptr ? Controller->PlayerState.Get() : nullptr;
	if (LocalState == nullptr)
	{
		return;
	}
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const FString LocalName = LocalState->GetPlayerName();
	if (VictimName == LocalName)
	{
		UHCJAssetSettings::PlaySound2D(this, Settings.DeathSounds, TEXT("death cue"), Settings.UiCueVolume);
	}
	else if (!KillerName.IsEmpty() && KillerName == LocalName)
	{
		NotifyCombat();
		UHCJAssetSettings::PlaySound2D(this, Settings.KillSounds, TEXT("kill cue"), Settings.UiCueVolume);
	}
}

void UHCJAudioDirector::TickMatchCues()
{
	const UWorld* World = GetWorld();
	const AHCJGameState* GameState = World->GetGameState<AHCJGameState>();
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const FHCJMatchConfig& Config = GameState->GetMatchConfig();
	const bool bIntermission = GameState->GetMatchPhase() == EHCJMatchPhase::Intermission;

	if (Config.IsValid() && (!bSeenMatch || Config.MatchNumber != LastMatchNumber) && !bIntermission)
	{
		bSeenMatch = true;
		LastMatchNumber = Config.MatchNumber;
		UHCJAssetSettings::PlaySound2D(this, Settings.MatchStartSounds, TEXT("match start cue"), Settings.UiCueVolume);
	}

	if (bIntermission && !bWasIntermission)
	{
		const APlayerController* Controller = GEngine->GetFirstLocalPlayerController(World);
		const bool bWon = Controller != nullptr && GameState->GetMatchWinner() != nullptr && GameState->GetMatchWinner() == Controller->PlayerState;
		UHCJAssetSettings::PlaySound2D(this, bWon ? Settings.MatchWonSounds : Settings.MatchLostSounds, TEXT("match end cue"), Settings.UiCueVolume);
	}
	bWasIntermission = bIntermission;
}

void UHCJAudioDirector::TickLocalPlayerCues(float Now, AHCJShipCharacter* LocalShip)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const UHCJHealthComponent* Health = LocalShip != nullptr ? LocalShip->GetHealthComponent() : nullptr;
	if (Health == nullptr)
	{
		LastShip = nullptr;
		return;
	}

	const float Vitals = Health->GetHealth() + Health->GetShield();
	if (LastShip.Get() != LocalShip)
	{
		// A new ship under the local player: the respawn cue (also the first spawn).
		LastShip = LocalShip;
		LastVitals = Vitals;
		NextLowHealthTime = Now + 1.0f;
		UHCJAssetSettings::PlaySound2D(this, Settings.RespawnSounds, TEXT("respawn cue"), Settings.UiCueVolume);
		return;
	}

	if (Vitals < LastVitals - 0.5f)
	{
		NotifyCombat();
		if (Now >= NextDamageCueTime)
		{
			NextDamageCueTime = Now + 0.12f;
			UHCJAssetSettings::PlaySound2D(this, Settings.DamageTakenSounds, TEXT("damage taken cue"), Settings.UiCueVolume);
		}
	}
	LastVitals = Vitals;

	if (LocalShip->IsFiring())
	{
		NotifyCombat();
	}

	if (Health->GetHealthFraction() < Settings.LowHealthFraction && Now >= NextLowHealthTime)
	{
		NextLowHealthTime = Now + FMath::Max(Settings.LowHealthInterval, 0.5f);
		UHCJAssetSettings::PlaySound2D(this, Settings.LowHealthSounds, TEXT("low hull cue"), Settings.UiCueVolume * 0.8f);
	}
}

void UHCJAudioDirector::TickBlackHoleRumble(const FVector& ViewLocation)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const AHCJUniverse* Universe = AHCJUniverse::Get(this);

	float Depth = 0.0f;
	if (Universe != nullptr && Universe->IsBuilt())
	{
		const double Distance = Universe->GetBlackHoleDistance(ViewLocation);
		const float Radius = Universe->GetBlackHoleSettings().InfluenceRadius;
		if (Distance >= 0.0 && Distance < Radius && Radius > 1.0f)
		{
			Depth = 1.0f - static_cast<float>(Distance) / Radius;
		}
	}

	if (Depth <= 0.0f)
	{
		if (BlackHoleLoop != nullptr && BlackHoleLoop->IsPlaying())
		{
			BlackHoleLoop->FadeOut(0.5f, 0.0f);
		}
		return;
	}

	if (BlackHoleLoop == nullptr)
	{
		USoundBase* Loop = UHCJAssetSettings::Load<USoundBase>(Settings.BlackHoleLoopSound, TEXT("black hole rumble"));
		if (Loop == nullptr)
		{
			return;
		}
		BlackHoleLoop = UGameplayStatics::SpawnSound2D(this, Loop, 1.0f, 1.0f, 0.0f, nullptr, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
		if (BlackHoleLoop == nullptr)
		{
			return;
		}
	}
	if (!BlackHoleLoop->IsPlaying())
	{
		BlackHoleLoop->Play();
	}
	// Quiet at the rim, loud and higher towards the horizon.
	BlackHoleLoop->SetVolumeMultiplier(FMath::Max(0.001f, Settings.BlackHoleLoopVolume * FMath::Pow(Depth, 1.5f)));
	BlackHoleLoop->SetPitchMultiplier(Settings.BlackHoleLoopPitch * (1.0f + 0.6f * Depth));
}

void UHCJAudioDirector::TickMusic(float Now, float DeltaTime, bool bIntermission, const AHCJShipCharacter* LocalShip, const FVector& ViewLocation)
{
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	CombatHeat = FMath::Max(0.0f, CombatHeat - DeltaTime / CombatHeatSeconds);
	if (Settings.MusicLayers.Num() == 0)
	{
		return;
	}

	if (bIntermission != bMusicIntermission)
	{
		// New state: start its first bar now.
		bMusicIntermission = bIntermission;
		BarIndex = 0;
		NextBarTime = Now;
	}
	if (Now < NextBarTime)
	{
		return;
	}
	const float BarSeconds = FMath::Max(Settings.MusicBarSeconds, 0.25f);
	// Keep the grid even when a frame was late; resynchronise after a long stall.
	NextBarTime = (Now - NextBarTime > BarSeconds) ? Now + BarSeconds : NextBarTime + BarSeconds;

	// Intensity for this bar.
	float Tension = 0.0f;
	if (LocalShip != nullptr)
	{
		for (TActorIterator<AHCJShipCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != LocalShip && !It->IsDead()
				&& FVector::DistSquared(It->GetActorLocation(), ViewLocation) < FMath::Square(static_cast<double>(MusicTensionDistance)))
			{
				Tension = MusicTensionIntensity;
				break;
			}
		}
	}
	// Full intensity while the heat of the last fight is above a third, then back down through the tension level.
	MusicIntensity = bIntermission ? 0.0f : FMath::Max(Tension, FMath::Clamp(CombatHeat * 3.0f, 0.0f, 1.0f));

	for (const FHCJMusicLayer& Layer : Settings.MusicLayers)
	{
		if (Layer.bIntermission != bIntermission || MusicIntensity + KINDA_SMALL_NUMBER < Layer.MinIntensity
			|| (BarIndex % FMath::Max(Layer.EveryBars, 1)) != 0)
		{
			continue;
		}
		UHCJAssetSettings::PlaySound2D(this, MakeArrayView(&Layer.Sound, 1), TEXT("music stem"), Layer.Volume, EHCJSoundGroup::Music);
		++NumStemsStarted;
	}
	++BarIndex;
}

void UHCJAudioDirector::StopMusicTracks()
{
	for (FTrackState& State : TrackStates)
	{
		for (TWeakObjectPtr<UAudioComponent>& Voice : State.Voices)
		{
			if (UAudioComponent* Component = Voice.Get())
			{
				Component->Stop();
			}
		}
		State = FTrackState();
	}
	TrackVoices.Reset();
	bTracksActive = false;
	LastTrackAudioTime = -1.0;
}

bool UHCJAudioDirector::TickMusicTracks(float DeltaTime, bool bMenu, const AHCJShipCharacter* LocalShip, const FVector& ViewLocation)
{
	UWorld* World = GetWorld();
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const FHCJMusicTrack* Tracks[static_cast<int32>(EHCJMusicTrack::Count)] = { nullptr, &Settings.MenuTrack, &Settings.ExploreTrack, &Settings.CombatTrack };
	const bool bWanted = UHCJUserSettings::Get().MusicSet == 0
		&& (!Settings.MenuTrack.Sound.IsNull() || !Settings.ExploreTrack.Sound.IsNull() || !Settings.CombatTrack.Sound.IsNull());
	if (!bWanted)
	{
		if (bTracksActive)
		{
			StopMusicTracks();
		}
		return false;
	}
	if (!bTracksActive)
	{
		bTracksActive = true;
		MusicSelector = FHCJMusicSelector();
		// The stems of set 1 ring out by themselves; their bar clock starts afresh when they come back.
		bMusicIntermission = false;
		BarIndex = 0;
	}

	// The intensity, every frame (the stems compute it once a bar): company nearby, the heat of the last fight.
	CombatHeat = FMath::Max(0.0f, CombatHeat - DeltaTime / CombatHeatSeconds);
	float Tension = 0.0f;
	if (LocalShip != nullptr)
	{
		for (TActorIterator<AHCJShipCharacter> It(World); It; ++It)
		{
			if (*It != LocalShip && !It->IsDead()
				&& FVector::DistSquared(It->GetActorLocation(), ViewLocation) < FMath::Square(static_cast<double>(MusicTensionDistance)))
			{
				Tension = MusicTensionIntensity;
				break;
			}
		}
	}
	MusicIntensity = bMenu ? 0.0f : FMath::Max(Tension, FMath::Clamp(CombatHeat * 3.0f, 0.0f, 1.0f));

	const EHCJMusicTrack Before = MusicSelector.Current;
	const EHCJMusicTrack Wanted = MusicSelector.Step(bMenu, MusicIntensity, DeltaTime, Settings.MusicCombatOnIntensity, Settings.MusicCombatOffIntensity, Settings.MusicCombatHoldSeconds);
	if (Wanted != Before)
	{
		++NumTrackSwitches;
		static const TCHAR* Names[] = { TEXT("none"), TEXT("menu"), TEXT("explore"), TEXT("combat") };
		UE_LOG(LogHCJ, Log, TEXT("Music: track %s -> %s (intensity %.2f)"), Names[static_cast<int32>(Before)], Names[static_cast<int32>(Wanted)], MusicIntensity);
	}

	// The clock of the audio device: what the voices actually played, whatever the game time does.
	const double AudioNow = World->GetAudioTimeSeconds();
	const float AudioDelta = LastTrackAudioTime >= 0.0 ? FMath::Clamp(static_cast<float>(AudioNow - LastTrackAudioTime), 0.0f, 1.0f) : 0.0f;
	LastTrackAudioTime = AudioNow;

	for (int32 Index = 1; Index < static_cast<int32>(EHCJMusicTrack::Count); ++Index)
	{
		FTrackState& State = TrackStates[Index];
		const FHCJMusicTrack& Track = *Tracks[Index];
		const float Target = static_cast<int32>(Wanted) == Index ? 1.0f : 0.0f;
		State.Weight = HCJMusic::StepWeight(State.Weight, Target, AudioDelta, Settings.MusicTrackFadeSeconds);
		if (State.Weight <= 0.0f && !State.bStarted)
		{
			continue;
		}

		if (!State.bStarted)
		{
			USoundBase* Sound = UHCJAssetSettings::Load<USoundBase>(Track.Sound, TEXT("music track"));
			if (Sound == nullptr)
			{
				continue;
			}
			State.Duration = Sound->GetDuration();
			for (int32 Voice = 0; Voice < 2; ++Voice)
			{
				UAudioComponent* Component = UGameplayStatics::CreateSound2D(World, Sound, 1.0f, 1.0f, 0.0f, nullptr, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
				if (Component != nullptr)
				{
					Component->bIsUISound = true;	// plays on while the game is paused
					TrackVoices.Add(Component);
				}
				State.Voices[Voice] = Component;
			}
			if (!State.Voices[0].IsValid() || !State.Voices[1].IsValid())
			{
				continue;
			}
			State.bStarted = true;
			State.ActiveVoice = 0;
			State.PassSeconds = 0.0f;
			State.Voices[0]->SetVolumeMultiplier(FMath::Max(Track.Volume * State.Weight, 0.0001f));
			State.Voices[0]->Play(0.0f);
			++NumStemsStarted;
		}

		UAudioComponent* Active = State.Voices[State.ActiveVoice].Get();
		UAudioComponent* Other = State.Voices[1 - State.ActiveVoice].Get();
		if (Active == nullptr || Other == nullptr)
		{
			State = FTrackState();
			continue;
		}

		// Faded out completely: it waits where it is and goes on from there when it is wanted again.
		const bool bSilent = State.Weight <= 0.0f;
		if (bSilent != State.bPaused)
		{
			State.bPaused = bSilent;
			Active->SetPaused(bSilent);
			Other->SetPaused(bSilent);
		}
		if (bSilent)
		{
			continue;
		}

		const float Volume = FMath::Max(Track.Volume * State.Weight, 0.0001f);
		Active->SetVolumeMultiplier(Volume);
		Other->SetVolumeMultiplier(Volume);

		// The loop: the next pass starts where the outro begins, and the two overlap.
		State.PassSeconds += AudioDelta;
		const float Crossfade = FMath::Max(Track.CrossfadeSeconds, 0.05f);
		if (State.Duration > 1.0f && State.PassSeconds >= HCJMusic::GetLoopStart(State.Duration, Track.LoopSeconds, Crossfade))
		{
			Other->FadeIn(Crossfade, 1.0f, 0.0f);
			Active->FadeOut(FMath::Max(Crossfade, State.Duration - State.PassSeconds), 0.0f);
			State.ActiveVoice = 1 - State.ActiveVoice;
			State.PassSeconds = 0.0f;
			++State.NumLoops;
			UE_LOG(LogHCJ, Log, TEXT("Music: track %d starts its next pass (loop %d), cross-fading %.1f s"), Index, State.NumLoops, Crossfade);
		}
		else if (!Active->IsPlaying() && !State.bPaused)
		{
			// The voice ended without us (a stall longer than the outro): start over.
			Active->Play(0.0f);
			State.PassSeconds = 0.0f;
		}
	}
	return true;
}

void UHCJAudioDirector::LogStatus(float Now)
{
	const UWorld* World = GetWorld();
	const FAudioDeviceHandle Device = World->GetAudioDevice();
	const FAudioDevice* RawDevice = Device.GetAudioDevice();
	UE_LOG(LogHCJ, Log, TEXT("Audio t=%.0fs: %d active sources of %d channels | started so far: fire %d, impact %d, explosion %d, world %d, interface %d, music stems %d | music bar %d, intensity %.2f%s | volumes music %.2f, effects %.2f, interface %.2f"),
		Now, RawDevice != nullptr ? RawDevice->GetNumActiveSources() : -1, RawDevice != nullptr ? RawDevice->GetMaxChannels() : 0,
		UHCJAssetSettings::GetNumSoundsPlayed(EHCJSoundGroup::Fire), UHCJAssetSettings::GetNumSoundsPlayed(EHCJSoundGroup::Impact),
		UHCJAssetSettings::GetNumSoundsPlayed(EHCJSoundGroup::Explosion), UHCJAssetSettings::GetNumSoundsPlayed(EHCJSoundGroup::World),
		UHCJAssetSettings::GetNumSoundsPlayed(EHCJSoundGroup::UI), NumStemsStarted, BarIndex, MusicIntensity,
		bMusicIntermission ? TEXT(" (intermission)") : TEXT(""),
		MusicClass != nullptr ? MusicClass->Properties.Volume : -1.0f, SfxClass != nullptr ? SfxClass->Properties.Volume : -1.0f,
		UiClass != nullptr ? UiClass->Properties.Volume : -1.0f);
	if (bTracksActive)
	{
		static const TCHAR* Names[] = { TEXT("none"), TEXT("menu"), TEXT("explore"), TEXT("combat") };
		const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
		const float Gains[] = { 0.0f, Settings.MenuTrack.Volume, Settings.ExploreTrack.Volume, Settings.CombatTrack.Volume };
		const int32 Current = static_cast<int32>(MusicSelector.Current);
		UE_LOG(LogHCJ, Log, TEXT("Music t=%.0fs: Lyria tracks, playing %s for %.0f s (pass at %.0f s of %.0f s, loops %d), intensity %.2f, switches so far %d | weights menu %.2f, explore %.2f, combat %.2f | gain of this track %.2f (%.1f dB), music class %.2f"),
			Now, Names[Current], MusicSelector.HeldSeconds, TrackStates[Current].PassSeconds, TrackStates[Current].Duration, TrackStates[Current].NumLoops, MusicIntensity, NumTrackSwitches,
			TrackStates[1].Weight, TrackStates[2].Weight, TrackStates[3].Weight, Gains[Current], HCJMusic::GainToDecibels(Gains[Current]),
			MusicClass != nullptr ? MusicClass->Properties.Volume : -1.0f);
	}

	// What this machine knows of the replicated world state (to compare a late joiner with the server).
	int32 KnownPads = 0;
	int32 ActivePads = 0;
	for (const TWeakObjectPtr<AHCJPickup>& Entry : AHCJPickup::GetAll())
	{
		const AHCJPickup* Pickup = Entry.Get();
		if (Pickup != nullptr && Pickup->GetWorld() == World)
		{
			++KnownPads;
			ActivePads += Pickup->IsActive() ? 1 : 0;
		}
	}
	int32 KnownTurrets = 0;
	int32 LiveTurrets = 0;
	for (const TWeakObjectPtr<AHCJTurret>& Entry : AHCJTurret::GetAll())
	{
		const AHCJTurret* Turret = Entry.Get();
		if (Turret != nullptr && Turret->GetWorld() == World)
		{
			++KnownTurrets;
			LiveTurrets += Turret->IsTurretDestroyed() ? 0 : 1;
		}
	}
	const APlayerController* Controller = GEngine->GetFirstLocalPlayerController(World);
	const AHCJShipCharacter* Ship = Controller != nullptr ? Cast<AHCJShipCharacter>(Controller->GetPawn()) : nullptr;
	UE_LOG(LogHCJ, Log, TEXT("Local t=%.0fs (%s): %d turrets known, %d alive | %d pickup pads known, %d hold an item | own ship: %s, ammo %d, power-ups %d, overdrive %s | movement corrections received so far: %d"),
		Now, World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("host"), KnownTurrets, LiveTurrets, KnownPads, ActivePads,
		Ship != nullptr ? *Ship->GetWeaponDef().Name : TEXT("none"), Ship != nullptr ? Ship->GetAmmo() : 0,
		Ship != nullptr ? (Ship->HasPowerup(EHCJPowerup::Overdrive) ? 1 : 0) + (Ship->HasPowerup(EHCJPowerup::Quad) ? 1 : 0) + (Ship->HasPowerup(EHCJPowerup::Cloak) ? 1 : 0) : 0,
		Ship != nullptr && Ship->GetShipMovement() != nullptr && Ship->GetShipMovement()->IsOverdriven() ? TEXT("on") : TEXT("off"),
		UHCJShipMovementComponent::GetNumClientCorrections());
}
