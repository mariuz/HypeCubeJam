#pragma once

#include "CoreMinimal.h"

/**
 * The ship computer: a few spoken warnings of the local player (waves under /Game/HCJ/Audio/Voice, see docs/ASSETS.md).
 * Everything here is plain logic without a world or a sound (tested on its own, like FHCJHintDirector): which
 * request may be spoken now. UHCJAudioDirector owns one limiter, asks it, and plays the wave when it says so.
 */
enum class EHCJVoiceLine : uint8
{
	None = 0,
	/** "All systems online." Once per run of the game, on the first ship. */
	Welcome,
	/** "Fore / Aft / Port / Starboard shield down." In the order of EHCJShieldFacing. */
	ShieldFore,
	ShieldAft,
	ShieldPort,
	ShieldStarboard,
	/** "Hull critical." */
	HullCritical,
	/** "Torpedo incoming." */
	TorpedoIncoming,
	/** "Missile locked on." */
	MissileLock,
	/** "Enemy behind." */
	EnemyBehind,
	/** "Gravity well. Boost away." Past the point where only boost gets the ship out. */
	GravityWell,
	/** "Tractor beam. Boost free." */
	TractorCaught,
	/** "Sector clear." The local player's kill emptied the sector. */
	SectorClear,
	Count
};

/** How one line behaves. */
struct FHCJVoiceRule
{
	/** Higher wins. At or above FHCJVoiceLimiter::UrgentPriority a line may cut off a lower one and needs only the short gap. */
	int32 Priority = 0;
	/** Seconds from one start of the line (or of any line of its group) to the next. */
	float Cooldown = 30.0f;
	/** Lines with the same group (other than 0) share one cooldown: the four shield lines. */
	int32 Group = 0;
	/** Name of the wave under /Game/HCJ/Audio/Voice (for the log; the path that is loaded is UHCJAssetSettings::VoiceLines[line - 1]). */
	const TCHAR* Asset = TEXT("");
};

enum class EHCJVoiceDecision : uint8
{
	/** Not spoken, and not remembered: a warning that is late is worse than none. */
	Drop,
	/** Speak it. */
	Play,
	/** Stop the line that is being spoken, then speak this one. */
	Interrupt
};

/** Why the last request was refused (for the log line "Voice: ... declined"). */
enum class EHCJVoiceRefusal : uint8
{
	None,
	/** The setting is off (or the voice volume is 0). */
	Disabled,
	/** Inside the first SpawnGraceSeconds of a ship. */
	SpawnGrace,
	/** The line, or one of its group, was spoken within its cooldown. */
	Cooldown,
	/** Another line is being spoken and this one may not cut it off. */
	Busy,
	/** Inside the silence after a line. */
	Gap,
	/** MaxPerMinute lines in the last 60 s and this one is not urgent. */
	MinuteCap,
	/** HardMaxPerMinute lines in the last 60 s. */
	HardMinuteCap
};

/**
 * Keeps the voice from nagging. The rules, in the order they are checked:
 *  1. the "Computer voice" setting is off: nothing;
 *  2. nothing in the first SpawnGraceSeconds of a ship (the respawn cue and the spawn shield own that moment);
 *  3. a line (or its group) is not repeated within its cooldown;
 *  4. one voice at a time: while a line is spoken a request is dropped, unless it is urgent and of higher
 *     priority than the line being spoken - then it cuts that line off;
 *  5. after a line GapSeconds of silence (UrgentGapSeconds before an urgent line);
 *  6. a line that is not urgent is refused when MaxPerMinute lines (of any kind) started in the last 60 s;
 *  7. any line, urgent or not, is refused when HardMaxPerMinute lines started in the last 60 s. The sounds and the
 *     HUD texts that exist without the voice still warn, so the voice may stay silent.
 * A dropped request is forgotten, never queued. Callers therefore ask once, when the thing happens (an edge),
 * not every frame while it lasts. Time is any monotonic clock in seconds.
 */
class HYPECUBEJAM_API FHCJVoiceLimiter
{
public:
	static constexpr int32 UrgentPriority = 90;
	static constexpr float GapSeconds = 5.0f;
	static constexpr float UrgentGapSeconds = 1.5f;
	static constexpr float SpawnGraceSeconds = 2.5f;
	/** Lines of any kind in 60 s after which a line that is not urgent is refused. */
	static constexpr int32 MaxPerMinute = 3;
	/** Lines of any kind in 60 s after which every line is refused. */
	static constexpr int32 HardMaxPerMinute = 5;

	static const FHCJVoiceRule& GetRule(EHCJVoiceLine Line);
	/** The shield line of a facing (EHCJShieldFacing as an int); None when out of range. */
	static EHCJVoiceLine ShieldLine(int32 Facing);

	/** Forgets everything (new process; tests). */
	void Reset();
	/** A new ship under the local player: the grace period starts, whatever is being spoken is considered over. */
	void NotifySpawn(float Now);
	/**
	 * May Line be spoken at Now? DurationSeconds is the length of its wave. On Play / Interrupt the line counts
	 * as started at Now.
	 */
	EHCJVoiceDecision Request(EHCJVoiceLine Line, float Now, float DurationSeconds, bool bEnabled);

	/** The line being spoken at Now (None when silent). */
	EHCJVoiceLine GetCurrent(float Now) const { return Now < CurrentEnd ? Current : EHCJVoiceLine::None; }
	int32 GetNumPlayed(EHCJVoiceLine Line) const { return NumPlayed[static_cast<int32>(Line)]; }
	int32 GetNumPlayed() const { return NumPlayedTotal; }
	int32 GetNumDropped() const { return NumDropped; }
	/** Why the last Request() returned Drop (None after a Play / Interrupt). */
	EHCJVoiceRefusal GetLastRefusal() const { return LastRefusal; }
	static const TCHAR* DescribeRefusal(EHCJVoiceRefusal Refusal);

private:
	EHCJVoiceLine Current = EHCJVoiceLine::None;
	float CurrentEnd = -1000.0f;
	float SpawnTime = -1000.0f;
	float LastStart[static_cast<int32>(EHCJVoiceLine::Count)];
	int32 NumPlayed[static_cast<int32>(EHCJVoiceLine::Count)] = {};
	/** Start times of the last HardMaxPerMinute lines (a ring). */
	float Recent[HardMaxPerMinute];
	int32 RecentIndex = 0;
	int32 NumPlayedTotal = 0;
	int32 NumDropped = 0;
	EHCJVoiceRefusal LastRefusal = EHCJVoiceRefusal::None;

public:
	FHCJVoiceLimiter() { Reset(); }
};
