#pragma once

#include "CoreMinimal.h"
#include "Audio/HCJMusicLogic.h"
#include "Audio/HCJVoiceLogic.h"
#include "Subsystems/WorldSubsystem.h"
#include "HCJAudioDirector.generated.h"

class AHCJShipCharacter;
class UAudioComponent;
class USoundClass;

/**
 * Local, cosmetic sound of the match: music and the cues of the local player. One per game world; does nothing on
 * a dedicated server or while there is no local player. Reads replicated / local state only and replicates nothing.
 *
 *  - Group volumes: writes MusicVolume / SfxVolume / UiVolume (or the console variables hcj.Audio.*Volume when
 *    they are >= 0) into the three sound classes of UHCJAssetSettings every tick.
 *  - Music: a bar clock re-triggers the configured stems (UHCJAssetSettings::MusicLayers). Layers switch on with
 *    the "intensity": 0 while cruising alone, raised by other ships nearby, 1 while the local ship fires, hits
 *    or is hit. During the intermission the intermission layers play instead.
 *  - Music, set 0 (the default): three full-length tracks (UHCJAssetSettings::MenuTrack / ExploreTrack / CombatTrack):
 *    the title menu, a match while calm, a match while the intensity is high (FHCJMusicSelector), cross-faded over
 *    MusicTrackFadeSeconds. A track that fades out is paused and goes on from there when it comes back. Each
 *    track is looped here - a second voice starts at its loop point and the two are cross-faded - because the
 *    files end in an outro. The stems above are set 1 (UHCJUserSettings::MusicSet) and the fallback.
 *  - Cues of the local player: hit confirm (NotifyHitConfirm, from AHCJPlayerController::ClientHitConfirm),
 *    damage taken and low hull (from the replicated health of the local ship), kill / death (NotifyKillEvent, from
 *    AHCJGameState::MulticastKillEvent), respawn, match start, match won / lost.
 *  - The ship computer: twelve spoken warnings of the local player (UHCJAssetSettings::VoiceLines) on one voice of
 *    its own. TickVoice asks for a line on the edge of a state (a shield facing went down, hull critical, gravity
 *    well, tractor beam, sector emptied by the own kill, the first ship of the run); the HUD and the controller ask
 *    for "torpedo incoming", "enemy behind" and "missile locked on" where their cues are. FHCJVoiceLimiter
 *    (Audio/HCJVoiceLogic.h) decides whether a line is spoken; every answer is a LogHCJ "Voice:" line.
 *  - Black hole rumble: a looping sound whose volume follows the depth of the local view in a gravity well.
 *  - Every 15 s one LogHCJ "Audio" line: active sources of the audio device and sounds started per group.
 */
UCLASS()
class HYPECUBEJAM_API UHCJAudioDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UHCJAudioDirector* Get(const UObject* WorldContextObject);

	//~ UTickableWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** Local player's bolt hit a ship (bKilled: and destroyed it). */
	void NotifyHitConfirm(bool bKilled);
	/** A kill feed event arrived on this machine. */
	void NotifyKillEvent(const FString& KillerName, const FString& VictimName);
	/** Kill-streak callout of the local player (EHCJStreak as int): the match-start stinger, higher for a bigger streak. */
	void NotifyStreak(int32 Streak);
	/** Something exciting happened to the local player: keeps the music intense for a while. */
	void NotifyCombat() { CombatHeat = 1.0f; }
	/** Asks the ship computer to say Line now. It may decline (FHCJVoiceLimiter); true when the line is spoken. Local and cosmetic. */
	bool RequestVoice(EHCJVoiceLine Line);

	/** Local world time of the last hit confirm (for the HUD hit marker); very negative when none. */
	float GetLastHitConfirmTime() const { return LastHitConfirmTime; }
	/** Local world time of the last confirmed hit that destroyed its target (for the HUD's kill marker). */
	float GetLastKillConfirmTime() const { return LastKillConfirmTime; }
	float GetMusicIntensity() const { return MusicIntensity; }
	/** The track the selector wants right now (None while the stems play). */
	EHCJMusicTrack GetMusicTrack() const { return bTracksActive ? MusicSelector.Current : EHCJMusicTrack::None; }

private:
	void ApplyGroupVolumes();
	void TickLocalPlayerCues(float Now, AHCJShipCharacter* LocalShip);
	void TickMatchCues();
	void TickVoice(float DeltaTime, AHCJShipCharacter* LocalShip);
	void TickBlackHoleRumble(const FVector& ViewLocation);
	void TickMusic(float Now, float DeltaTime, bool bIntermission, const AHCJShipCharacter* LocalShip, const FVector& ViewLocation);
	void LogStatus(float Now);
	/** Set 0: picks the track, fades, loops. Returns false when the tracks are not in use (set 1, or none loads): the stems play. */
	bool TickMusicTracks(float DeltaTime, bool bMenu, const AHCJShipCharacter* LocalShip, const FVector& ViewLocation);
	void StopMusicTracks();

	/** One of the three tracks: two voices (for the loop seam), which one is the current pass, how far it is, its share of the mix. */
	struct FTrackState
	{
		TWeakObjectPtr<UAudioComponent> Voices[2];
		int32 ActiveVoice = 0;
		float PassSeconds = 0.0f;
		float Duration = 0.0f;
		float Weight = 0.0f;
		bool bStarted = false;
		bool bPaused = false;
		int32 NumLoops = 0;
	};
	FTrackState TrackStates[static_cast<int32>(EHCJMusicTrack::Count)];
	FHCJMusicSelector MusicSelector;
	bool bTracksActive = false;
	double LastTrackAudioTime = -1.0;
	int32 NumTrackSwitches = 0;

	/** The voices of the tracks (kept referenced; created on first use). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> TrackVoices;

	/** Kept alive while the world lives; null when the class does not load. */
	UPROPERTY(Transient)
	TObjectPtr<USoundClass> MusicClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> SfxClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> UiClass;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BlackHoleLoop;

	/** The one voice of the ship computer (created on first use, reused for every line). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> VoiceComponent;

	bool bClassesLoaded = false;

	// ship computer: what the voice saw last frame (edges, not states, are spoken)
	TWeakObjectPtr<AHCJShipCharacter> VoiceShip;
	float VoiceShipTime = 0.0f;
	float VoiceFacingCollapse[4] = {};
	bool bVoiceHullArmed = true;
	bool bVoiceInWell = false;
	bool bVoiceHeld = false;
	/** >= 0: at this time of the voice clock, look whether the local player's kill emptied the sector. */
	float VoiceSectorCheckTime = -1.0f;
	/** The sector the local ship was in at that kill: the line is about that sector only. */
	int32 VoiceSectorCheckSector = -1;
	/** The line being spoken was told to fade out (voice switched off, match over); cleared by the next line. */
	bool bVoiceFadingOut = false;

	// local player cues
	TWeakObjectPtr<AHCJShipCharacter> LastShip;
	float LastVitals = 0.0f;
	float NextDamageCueTime = 0.0f;
	float NextLowHealthTime = 0.0f;
	float LastHitConfirmTime = -1000.0f;
	float LastKillConfirmTime = -1000.0f;

	// match cues
	bool bSeenMatch = false;
	int32 LastMatchNumber = 0;
	bool bWasIntermission = false;

	// music
	float CombatHeat = 0.0f;
	float MusicIntensity = 0.0f;
	float NextBarTime = 0.0f;
	int32 BarIndex = 0;
	bool bMusicIntermission = false;
	int32 NumStemsStarted = 0;

	float NextStatusTime = 15.0f;
};
