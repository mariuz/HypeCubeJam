#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AHCJPlayerController;
class SVerticalBox;

/** Pages of the one menu widget. */
enum class EHCJMenuPage : uint8
{
	/** Title menu: play, host options, join by address, settings, quit. */
	Main,
	/** Mouse sensitivity, the three volumes, hints on / off. */
	Settings,
	/** In a match (Esc): resume, settings, leave to menu, quit. */
	Pause,
	/** A join is under way: address and a cancel button. */
	Connecting,
	/** The keys, in two columns, and what the marks of the HUD mean. */
	Controls,
	/** FIND GAMES: the lobbies advertised through Epic Online Services, refresh, join. */
	Lobbies,
	/** LEADERBOARDS: top kills and wins of Epic Online Services, with the local player's row. */
	Leaderboards,
	/** QUICK PLAY is at work: one live status line ("Searching for open matches...") and a cancel button. */
	QuickPlay,
	/** HANGAR: ship type, hull and paint, with the stats of the type; the hull turns in the world next to the page. */
	Hangar
};

/**
 * The game's only menu: a Slate widget built in C++ (no widget assets), added to the game viewport by the local
 * AHCJPlayerController, which also owns every action - the widget only reads / writes UHCJUserSettings and calls
 * the controller's Menu* functions. Sizes are Slate units: the engine's DPI scaling of viewport widgets makes
 * them follow the resolution (0.67 at 1280x720, 1 at 1920x1080).
 */
class SHCJMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHCJMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AHCJPlayerController>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuilds the content for Page. Notice is a line shown on the page (why the player is here, connection errors). */
	void SetPage(EHCJMenuPage NewPage, const FString& InNotice = FString());
	EHCJMenuPage GetPage() const { return Page; }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	/** The online pages are rebuilt when the online subsystem has something new (search done, scores arrived). */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<SWidget> BuildMain();
	TSharedRef<SWidget> BuildSettings();
	TSharedRef<SWidget> BuildPause();
	TSharedRef<SWidget> BuildConnecting();
	TSharedRef<SWidget> BuildQuickPlay();
	TSharedRef<SWidget> BuildControls();
	TSharedRef<SWidget> BuildLobbies();
	TSharedRef<SWidget> BuildLeaderboards();
	TSharedRef<SWidget> BuildHangar();
	/** Title, "Online services unavailable" and why, BACK: what the online pages show when there is no service. */
	TSharedRef<SWidget> BuildOnlineUnavailable(const FString& Title, const FString& InStatus);
	/** One leaderboard: title, column heads, rows (the local player's row in the accent colour) or why there are none. */
	TSharedRef<SWidget> MakeLeaderboardColumn(int32 Index);
	/** The online subsystem of the owner's game instance, or nullptr. */
	class UHCJEOSSubsystem* GetOnline() const;
	/** A dim line of key caps at the bottom of a page: what the keyboard does here. */
	TSharedRef<SWidget> MakeKeyLegend(const FString& Markup);
	/** "[W] [S]   thrust" row of the controls page. */
	TSharedRef<SWidget> MakeControlRow(const FString& Keys, const FString& What);
	/** Text with key caps from "[Key] words" markup (HCJHints::SplitKeyMarkup). */
	TSharedRef<SWidget> MakeKeyMarkup(const FString& Markup, float FontSize, const FLinearColor& Color);

	TSharedRef<SWidget> MakeButton(const FString& Label, TFunction<void()> OnClick, bool bPrimary = false, float FontSize = 20.0f);
	TSharedRef<SWidget> MakeLabel(const FString& Text, float FontSize, const FLinearColor& Color, bool bBold = false);
	/** "Label   <  value  >" row; Step gets -1 or +1. */
	TSharedRef<SWidget> MakeStepper(const FString& Label, TFunction<FString()> GetValue, TFunction<void(int32)> Step);
	TSharedRef<SWidget> MakeSlider(const FString& Label, float Min, float Max, TFunction<float()> GetValue, TFunction<void(float)> SetValue, TFunction<FString(float)> Format);
	/** "Label   [text box]" row. */
	TSharedRef<SWidget> MakeTextRow(const FString& Label, const FString& Hint, TFunction<FString()> GetValue, TFunction<void(const FString&)> SetValue, float Width = 260.0f);

	TWeakObjectPtr<AHCJPlayerController> Owner;
	EHCJMenuPage Page = EHCJMenuPage::Main;
	/** Where Back on the settings page leads. */
	EHCJMenuPage SettingsReturnPage = EHCJMenuPage::Main;
	FString Notice;
	/** UHCJEOSSubsystem::GetRevision the current page was built from. */
	uint32 OnlineRevision = 0;
};
