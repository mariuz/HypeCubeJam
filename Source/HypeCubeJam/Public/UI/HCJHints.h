#pragma once

#include "CoreMinimal.h"

/**
 * In-game hints of the local HUD. Everything here is plain logic without a world or a canvas (tested on its own):
 * which hint is on screen now (FHCJHintDirector), the key-cap markup of hint texts, the objective line and the
 * tip shown after a death. Hints are local and cosmetic: nothing is replicated, the server never knows about them.
 */
enum class EHCJHint : uint8
{
	None = 0,

	// ---- onboarding: shown one after the other on the first ship, each until the player does it (or a timeout) ----
	Move,
	Fire,
	Boost,
	Target,

	// ---- contextual: shown while (or right after) their condition holds, a limited number of times per session ----
	/** Inside the gravity well of a black hole. Urgent. */
	BlackHole,
	/** Hull low. Urgent. */
	LowHull,
	/** A pickup was just taken that has not been explained yet. */
	NewItem,
	/** Live station turrets within reach. */
	Turrets,
	/** The boost meter ran dry. */
	BoostEmpty,
	/** Near a wormhole mouth. */
	Wormhole,
	/** Near a portal. */
	Portal,
	/** Near a pad that holds an item. */
	Pickup,
	/** Nobody to fight in this sector, and a way to the nearest enemy is known. */
	EmptySector,
	/** What the match is about; once, after the onboarding. */
	Goal,
	/** One of the own shield facings just went down (the first time only): what facings are. */
	ShieldDown,
	/** The locked target shows a shield facing that is down and a torpedo is loaded (the first time only): fire one. */
	TorpedoChance,
	/** The phaser bank just ran empty (the first times only): the trigger fires the blaster until it has recharged. */
	PhaserEmpty,
	/** The own beam is on a rock (the first time only): rocks can be broken. */
	RockBreak,

	Count
};

/** How one hint behaves. */
struct FHCJHintRule
{
	/** Seconds on screen (onboarding: the timeout). */
	float Duration = 6.0f;
	/** Times it may be shown in one session. */
	int32 MaxShows = 1;
	/** Seconds between the end of one showing and the next. */
	float Cooldown = 30.0f;
	/** Higher wins when several are due. At or above FHCJHintDirector::UrgentPriority it may interrupt another hint. */
	int32 Priority = 0;
	/** Leaves early when its condition is no longer true (after MinShowSeconds). */
	bool bWhileTrue = false;
};

/** What the HUD knows this frame. */
struct FHCJHintInput
{
	/** The local player has a live ship (hints are only shown then). */
	bool bHasShip = false;
	/** Hints are switched on. When false nothing is shown, but actions are still remembered. */
	bool bEnabled = true;
	/** Bit (1 << EHCJHint) for every onboarding action the player is doing right now (Move, Fire, Boost, Target). */
	uint32 DoneMask = 0;
	/** Bit (1 << EHCJHint) for every contextual hint whose condition holds right now. */
	uint32 ActiveMask = 0;
};

/**
 * Decides which hint is on screen. One at a time; onboarding first, in order; contextual hints by priority, each
 * limited by its rule; an urgent hint interrupts (an interrupted onboarding step comes back afterwards), and a hint
 * about something that just happened (a new item) replaces a lower contextual one. Time is any
 * monotonic clock in seconds.
 */
class HYPECUBEJAM_API FHCJHintDirector
{
public:
	static constexpr int32 UrgentPriority = 90;
	/** A hint at or above this priority (something that just happened) may replace a lower contextual hint, never an onboarding step. */
	static constexpr int32 InterruptPriority = 60;
	/** A hint stays at least this long once shown, even if its reason went away. */
	static constexpr float MinShowSeconds = 1.5f;
	/** Pause between two hints. */
	static constexpr float GapSeconds = 1.2f;
	/** An onboarding step that was interrupted (death, an urgent hint) comes back, but is not shown more often than this. */
	static constexpr int32 MaxOnboardingShows = 2;
	/** An onboarding step whose action was done stays this long (so that the player sees it acknowledged). */
	static constexpr float DoneLingerSeconds = 0.7f;

	static const FHCJHintRule& GetRule(EHCJHint Hint);
	static bool IsOnboarding(EHCJHint Hint) { return Hint >= EHCJHint::Move && Hint <= EHCJHint::Target; }
	static uint32 Bit(EHCJHint Hint) { return 1u << static_cast<uint32>(Hint); }

	/** Forgets everything (new session). */
	void Reset();
	/** Advances to Now; returns the hint to draw (None for nothing). */
	EHCJHint Update(float Now, const FHCJHintInput& Input);

	EHCJHint GetCurrent() const { return Current; }
	/** Seconds the current hint has been up (0 without one). */
	float GetCurrentAge(float Now) const { return Current != EHCJHint::None ? Now - CurrentStart : 0.0f; }
	/** 1 at the start of the current hint, 0 when its time is up. */
	float GetCurrentRemainingFraction(float Now) const;
	/** The current onboarding step has been done and is about to leave. */
	bool IsCurrentDone() const { return CurrentDoneTime >= 0.0f; }
	int32 GetShowCount(EHCJHint Hint) const { return Shows[static_cast<int32>(Hint)]; }
	bool IsOnboardingFinished() const;

private:
	void Begin(EHCJHint Hint, float Now);
	void End(float Now, bool bFinished);
	bool IsDue(EHCJHint Hint, float Now, uint32 ActiveMask) const;
	EHCJHint PickContextual(float Now, uint32 ActiveMask, int32 MinPriority) const;

	EHCJHint Current = EHCJHint::None;
	float CurrentStart = 0.0f;
	float CurrentDoneTime = -1.0f;
	float LastEndTime = -1000.0f;
	/** Onboarding actions seen so far. */
	uint32 Done = 0;
	/** Onboarding steps that are over (done or timed out). */
	uint32 Finished = 0;
	int32 Shows[static_cast<int32>(EHCJHint::Count)] = {};
	float LastShownEnd[static_cast<int32>(EHCJHint::Count)] = {};
	bool bInitialised = false;
};

/** Why the local player died, as far as the kill feed tells. */
enum class EHCJDeathCause : uint8
{
	Player,
	Turret,
	BlackHole,
	Void
};

/** A piece of hint text: plain words or a key cap. */
struct FHCJHintRun
{
	FString Text;
	bool bKey = false;
};

namespace HCJHints
{
	/** Splits "[W][S] thrust" into key caps (the bracketed parts) and plain text, in order. An unclosed bracket is plain text. */
	HYPECUBEJAM_API void SplitKeyMarkup(const FString& Markup, TArray<FHCJHintRun>& OutRuns);

	/** From a kill feed entry of the local victim: KillerName as the feed has it (empty for the environment) and its cause string. */
	HYPECUBEJAM_API EHCJDeathCause ClassifyDeath(const FString& KillerName, const FString& Cause);
	/** A tip for the respawn screen; Index (e.g. the death count) rotates through the tips of that cause. Never empty. */
	HYPECUBEJAM_API const TCHAR* PickDeathTip(EHCJDeathCause Cause, int32 Index);

	/**
	 * The objective line under the score: how the local player stands against the best of the others.
	 * BestOtherKills < 0 means there is nobody else. FragLimit 0 = no limit.
	 */
	HYPECUBEJAM_API FString MakeObjectiveText(int32 MyKills, int32 BestOtherKills, const FString& BestOtherName, int32 FragLimit);

	/** One sentence about what an item does, with key caps in markup (EHCJPickupItem as an int; unknown = empty). */
	HYPECUBEJAM_API const TCHAR* GetItemHint(int32 Item);
}
