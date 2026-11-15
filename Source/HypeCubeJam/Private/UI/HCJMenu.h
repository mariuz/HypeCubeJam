#pragma once

#include "CoreMinimal.h"
#include "Game/HCJPadLogic.h"
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

/** What a game controller (or the arrow keys) can do in the menu. */
enum class EHCJMenuNav : uint8
{
	Up,
	Down,
	Left,
	Right,
	/** A / Enter: the entry that has the focus. */
	Accept,
	/** B / Start / Esc: one page back, or out of the in-match menu. */
	Back
};

/**
 * The game's only menu: a Slate widget built in C++ (no widget assets), added to the game viewport by the local
 * AHCJPlayerController, which also owns every action - the widget only reads / writes UHCJUserSettings and calls
 * the controller's Menu* functions. Sizes are Slate units: the engine's DPI scaling of viewport widgets makes
 * them follow the resolution (0.67 at 1280x720, 1 at 1920x1080).
 *
 * FOCUS (game controller, arrow keys): every button, stepper and slider of a page is an entry of NavItems, in the
 * order it was built; one of them has the focus (NavIndex) and is drawn with an accent frame once a pad or an arrow
 * key was used (bNavVisible; moving the mouse hides it again). Up / down (D-pad, left stick, arrows, Tab) move the
 * focus and wrap round; left / right change a stepper or slider, and move the focus on a button; A or Enter presses
 * the entry (a stepper steps forward); B, Start or Esc go back. The buttons themselves never take Slate's keyboard
 * focus: the menu widget keeps it, so a key always arrives here. Text boxes are left to the mouse and keyboard.
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
	/** The left stick of a game controller moves the focus (HCJPad::FStickNav in Tick). */
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	/** Slate's own focus navigation is switched off: this widget keeps the keyboard and moves its own focus. */
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	/** The mouse takes over: the focus frame goes away. */
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** One step of the focus, or the press of the entry that has it (pad, arrow keys, "HCJMenuCmd nav"). */
	void Navigate(EHCJMenuNav Action);
	int32 GetNavIndex() const { return NavIndex; }
	int32 GetNumNavItems() const { return NavItems.Num(); }
	/** The label of the entry that has the focus ("" when the page has none). */
	FString GetNavLabel() const;
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

	/** A button, and an entry of the focus list unless bNavItem is false (the arrows of a stepper are part of its row). */
	TSharedRef<SWidget> MakeButton(const FString& Label, TFunction<void()> OnClick, bool bPrimary = false, float FontSize = 20.0f, bool bNavItem = true);
	/** Esc / B / Start: what going back means on this page. */
	void GoBack();
	/** The legend of a page: the keyboard's, or the pad's while the pad is the device in use. */
	TSharedRef<SWidget> MakeLegend(const FString& KeyboardMarkup, const FString& PadMarkup);
	/** Adds an entry to the focus list and returns Content inside the frame that shows the focus. */
	TSharedRef<SWidget> AddNavItem(const FString& Label, TSharedRef<SWidget> Content, TFunction<void()> Activate, TFunction<void(int32)> Step, bool bPrimary);
	bool IsControllerEnabled() const;
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

	struct FNavItem
	{
		FString Label;
		/** A press: a button's click. Unset for a stepper or slider (a press steps it forward). */
		TFunction<void()> Activate;
		/** Left / right: -1 or +1. Unset for a button. */
		TFunction<void(int32)> Step;
		bool bPrimary = false;
		/** The frame drawn round the entry, and the entry's place in NavItems as that frame reads it. */
		TWeakPtr<SWidget> Frame;
		TSharedPtr<int32> Order;
	};
	/**
	 * Puts NavItems into the order of the widget tree under Root (top to bottom, left to right). The entries are
	 * registered while the page is built, and the order in which a compiler evaluates the operands of a chain of
	 * "+ Slot()[...]" is its own business.
	 */
	void SortNavItems(const TSharedRef<SWidget>& Root);
	TArray<FNavItem> NavItems;
	int32 NavIndex = 0;
	bool bNavVisible = false;
	/** A page was built before (the first one picks its own focus). */
	bool bBuilt = false;
	/** The left stick as the engine last reported it, and its steps. */
	float StickX = 0.0f;
	float StickY = 0.0f;
	HCJPad::FStickNav StickNav;
};
