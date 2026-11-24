#include "UI/HCJMenu.h"

#include "Game/HCJBalance.h"
#include "Game/HCJPlayerController.h"
#include "Game/HCJUserSettings.h"
#include "Online/HCJEOSSubsystem.h"
#include "UI/HCJHints.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Assets/HCJAssetSettings.h"
#include "Ship/HCJShipTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor MenuText(0.85f, 0.95f, 1.0f, 1.0f);
	const FLinearColor MenuDim(0.55f, 0.65f, 0.75f, 1.0f);
	const FLinearColor MenuAccent(0.2f, 1.0f, 0.9f, 1.0f);
	const FLinearColor MenuWarning(1.0f, 0.45f, 0.25f, 1.0f);
	const FLinearColor MenuPanel(0.0f, 0.02f, 0.05f, 0.85f);
	const FLinearColor MenuButton(0.25f, 0.6f, 0.85f, 1.0f);
	const FLinearColor MenuButtonPrimary(0.1f, 1.6f, 1.3f, 1.0f);
	/** The frame of the entry that has the focus of the pad / the arrow keys. */
	const FLinearColor MenuFocus(1.0f, 0.85f, 0.1f, 1.0f);

	const TCHAR* DifficultyName(int32 Difficulty)
	{
		return HCJBalance::GetDifficultyName(Difficulty);
	}

	const FLinearColor MenuKeyFill(0.02f, 0.07f, 0.11f, 1.0f);
	const FLinearColor MenuRowFill(0.03f, 0.09f, 0.14f, 0.9f);

	/** Title menu and in-match menu: the pages the others go back to. */
	bool IsRootPage(EHCJMenuPage Page)
	{
		return Page == EHCJMenuPage::Main || Page == EHCJMenuPage::Pause;
	}
}

void SHCJMenu::Construct(const FArguments& InArgs)
{
	UHCJUserSettings::Get().Sanitize();
	Owner = InArgs._Owner;
	SetPage(EHCJMenuPage::Main);
}

void SHCJMenu::SetPage(EHCJMenuPage NewPage, const FString& InNotice)
{
	if (!IsRootPage(NewPage) && NewPage != EHCJMenuPage::Connecting && NewPage != EHCJMenuPage::QuickPlay && IsRootPage(Page))
	{
		SettingsReturnPage = Page;
	}
	Notice = InNotice;

	UHCJEOSSubsystem* Online = GetOnline();
	if (Online != nullptr && NewPage != Page)
	{
		// Opening an online page asks for fresh data; the page is rebuilt when it arrives (Tick).
		if (NewPage == EHCJMenuPage::Lobbies) { Online->RefreshLobbies(); }
		if (NewPage == EHCJMenuPage::Leaderboards) { Online->RefreshLeaderboards(); Online->RefreshPilotRecord(); }
	}
	OnlineRevision = Online != nullptr ? Online->GetRevision() : 0;

	// A rebuild destroys the button that had the keyboard: keep Esc / Enter / F1 working afterwards.
	const bool bHadFocus = HasAnyUserFocusOrFocusedDescendants();

	const bool bSamePage = bBuilt && NewPage == Page;
	bBuilt = true;
	NavItems.Reset();
	if (const AHCJPlayerController* Controller = Owner.Get())
	{
		// A pad in the hand: the focus frame is there from the start.
		bNavVisible |= Controller->IsPadPrompts();
	}
	Page = NewPage;
	TSharedRef<SWidget> Content = Page == EHCJMenuPage::Settings ? BuildSettings()
		: Page == EHCJMenuPage::Controls ? BuildControls()
		: Page == EHCJMenuPage::Pause ? BuildPause()
		: Page == EHCJMenuPage::Lobbies ? BuildLobbies()
		: Page == EHCJMenuPage::Leaderboards ? BuildLeaderboards()
		: Page == EHCJMenuPage::Hangar ? BuildHangar()
		: Page == EHCJMenuPage::QuickPlay ? BuildQuickPlay()
		: Page == EHCJMenuPage::Connecting ? BuildConnecting() : BuildMain();
	SortNavItems(Content);
	// The hangar stands at the left and leaves the world clear: the hull turns there, next to the page.
	const bool bHangar = Page == EHCJMenuPage::Hangar;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, bHangar ? 0.12f : Page == EHCJMenuPage::Main ? 0.35f : 0.5f))
		.HAlign(bHangar ? HAlign_Left : HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(FMargin(bHangar ? 70.0f : 0.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(MenuPanel)
			.Padding(FMargin(36.0f, 26.0f))
			[
				SNew(SBox)
				.MinDesiredWidth(bHangar ? 560.0f : 620.0f)
				[
					Content
				]
			]
		]
	];

	// The focus: kept on a page that was only rebuilt; else PLAY on the title page, the first row of the pages that
	// are lists of settings, and the big button elsewhere.
	if (bSamePage)
	{
		NavIndex = FMath::Clamp(NavIndex, 0, FMath::Max(NavItems.Num() - 1, 0));
	}
	else
	{
		NavIndex = 0;
		const bool bList = Page == EHCJMenuPage::Settings || Page == EHCJMenuPage::Hangar;
		for (int32 Index = 0; Index < NavItems.Num() && !bList; ++Index)
		{
			if (Page == EHCJMenuPage::Main ? NavItems[Index].Label.StartsWith(TEXT("PLAY")) : NavItems[Index].bPrimary)
			{
				NavIndex = Index;
				break;
			}
		}
	}

	if (bHadFocus && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::SetDirectly);
	}
}

bool SHCJMenu::IsControllerEnabled() const
{
	return HCJPad::IsKeyAllowed(true, UHCJUserSettings::Get().bController);
}

FString SHCJMenu::GetNavLabel() const
{
	return NavItems.IsValidIndex(NavIndex) ? NavItems[NavIndex].Label : FString();
}

TSharedRef<SWidget> SHCJMenu::AddNavItem(const FString& Label, TSharedRef<SWidget> Content, TFunction<void()> Activate, TFunction<void(int32)> Step, bool bPrimary)
{
	const int32 Index = NavItems.Num();
	const TSharedRef<int32> Order = MakeShared<int32>(Index);
	const bool bButton = static_cast<bool>(Activate);
	// A frame of two units round the entry: clear until it has the focus. (The frame is a child of this widget and
	// dies with the page, so the plain pointer is safe.)
	TSharedRef<SWidget> Frame = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.Padding(FMargin(2.0f))
		// (A button covers the inside of its frame; a stepper or slider row does not, so its frame is a faint band
		// that the label and the value stay readable on.)
		.BorderBackgroundColor_Lambda([this, Order, bButton]()
		{
			return FSlateColor(bNavVisible && NavIndex == *Order ? (bButton ? MenuFocus : MenuFocus.CopyWithNewOpacity(0.3f)) : FLinearColor::Transparent);
		})
		[
			Content
		];
	FNavItem& Item = NavItems.AddDefaulted_GetRef();
	Item.Label = Label;
	Item.Activate = MoveTemp(Activate);
	Item.Step = MoveTemp(Step);
	Item.bPrimary = bPrimary;
	Item.Frame = Frame;
	Item.Order = Order;
	return Frame;
}

void SHCJMenu::SortNavItems(const TSharedRef<SWidget>& Root)
{
	// Depth first through the children, in slot order: where each entry's frame comes in the tree.
	TMap<const SWidget*, int32> Place;
	TArray<TSharedRef<SWidget>> Stack;
	Stack.Add(Root);
	while (Stack.Num() > 0)
	{
		const TSharedRef<SWidget> Widget = Stack.Pop(EAllowShrinking::No);
		Place.Add(&Widget.Get(), Place.Num());
		FChildren* Children = Widget->GetAllChildren();
		for (int32 Index = Children != nullptr ? Children->Num() - 1 : -1; Index >= 0; --Index)
		{
			Stack.Add(Children->GetChildAt(Index));
		}
	}
	const auto PlaceOf = [&Place](const FNavItem& Item)
	{
		const TSharedPtr<SWidget> Frame = Item.Frame.Pin();
		const int32* Found = Frame.IsValid() ? Place.Find(Frame.Get()) : nullptr;
		return Found != nullptr ? *Found : MAX_int32;
	};
	NavItems.StableSort([&PlaceOf](const FNavItem& A, const FNavItem& B) { return PlaceOf(A) < PlaceOf(B); });
	for (int32 Index = 0; Index < NavItems.Num(); ++Index)
	{
		if (NavItems[Index].Order.IsValid())
		{
			*NavItems[Index].Order = Index;
		}
	}
}

void SHCJMenu::GoBack()
{
	if (Page == EHCJMenuPage::Settings || Page == EHCJMenuPage::Controls || Page == EHCJMenuPage::Lobbies || Page == EHCJMenuPage::Leaderboards || Page == EHCJMenuPage::Hangar)
	{
		if (Page == EHCJMenuPage::Hangar)
		{
			if (AHCJPlayerController* Controller = Owner.Get()) { Controller->MenuApplyShip(); }
		}
		UHCJUserSettings::Get().Save();
		SetPage(SettingsReturnPage);
	}
	else if (Page == EHCJMenuPage::QuickPlay)
	{
		if (AHCJPlayerController* Controller = Owner.Get()) { Controller->MenuCancelQuickPlay(); }
	}
	else if (AHCJPlayerController* Controller = Owner.Get())
	{
		// (Also the CONNECTING page, as Esc always did there; its CANCEL button has the focus for A.)
		Controller->MenuEscape();
	}
}

void SHCJMenu::Navigate(EHCJMenuNav Action)
{
	bNavVisible = true;
	if (Action == EHCJMenuNav::Back)
	{
		GoBack();
		return;
	}
	if (NavItems.Num() == 0)
	{
		return;
	}
	NavIndex = FMath::Clamp(NavIndex, 0, NavItems.Num() - 1);
	switch (Action)
	{
	case EHCJMenuNav::Up:
		NavIndex = HCJPad::StepFocus(NavIndex, NavItems.Num(), -1);
		break;
	case EHCJMenuNav::Down:
		NavIndex = HCJPad::StepFocus(NavIndex, NavItems.Num(), +1);
		break;
	case EHCJMenuNav::Left:
	case EHCJMenuNav::Right:
	{
		const int32 Direction = Action == EHCJMenuNav::Left ? -1 : +1;
		if (NavItems[NavIndex].Step)
		{
			NavItems[NavIndex].Step(Direction);
		}
		else
		{
			NavIndex = HCJPad::StepFocus(NavIndex, NavItems.Num(), Direction);
		}
		break;
	}
	case EHCJMenuNav::Accept:
	{
		// Copies: a button may build another page (which empties NavItems) or close the menu while it runs.
		const TFunction<void()> Activate = NavItems[NavIndex].Activate;
		const TFunction<void(int32)> Step = NavItems[NavIndex].Step;
		if (Activate)
		{
			Activate();
		}
		else if (Step)
		{
			Step(+1);
		}
		break;
	}
	default:
		break;
	}
}

FReply SHCJMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	if (InAnalogInputEvent.GetKey() == EKeys::Gamepad_LeftX)
	{
		StickX = InAnalogInputEvent.GetAnalogValue();
	}
	else if (InAnalogInputEvent.GetKey() == EKeys::Gamepad_LeftY)
	{
		StickY = InAnalogInputEvent.GetAnalogValue();
	}
	// Not handled: the game below must go on seeing the sticks (their return to the centre above all).
	return SCompoundWidget::OnAnalogValueChanged(MyGeometry, InAnalogInputEvent);
}

FNavigationReply SHCJMenu::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	// (Explicit to nobody = the keyboard focus stays where it is.)
	return FNavigationReply::Explicit(nullptr);
}

FReply SHCJMenu::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!MouseEvent.GetCursorDelta().IsNearlyZero())
	{
		bNavVisible = false;
	}
	return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
}

UHCJEOSSubsystem* SHCJMenu::GetOnline() const
{
	return UHCJEOSSubsystem::Get(Owner.Get());
}

void SHCJMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Page == EHCJMenuPage::Lobbies || Page == EHCJMenuPage::Leaderboards)
	{
		const UHCJEOSSubsystem* Online = GetOnline();
		if (Online != nullptr && Online->GetRevision() != OnlineRevision)
		{
			SetPage(Page, Notice);
		}
	}

	// The left stick of a game controller: a step of the focus when it is pushed, and again while it is held.
	if (IsControllerEnabled())
	{
		const HCJPad::EDirection Direction = StickNav.Update(StickX, StickY, InCurrentTime);
		if (Direction != HCJPad::EDirection::None)
		{
			if (AHCJPlayerController* Controller = Owner.Get()) { Controller->NoteInputDevice(true, false, 1.0f); }
			Navigate(Direction == HCJPad::EDirection::Up ? EHCJMenuNav::Up : Direction == HCJPad::EDirection::Down ? EHCJMenuNav::Down
				: Direction == HCJPad::EDirection::Left ? EHCJMenuNav::Left : EHCJMenuNav::Right);
		}
	}
}

FReply SHCJMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key.IsGamepadKey())
	{
		// Every pad key ends here while the menu is open (the ship must not get it), also when the controller is
		// switched off in SETTINGS: then it does nothing at all.
		if (!IsControllerEnabled())
		{
			return FReply::Handled();
		}
		if (AHCJPlayerController* Controller = Owner.Get()) { Controller->NoteInputDevice(true, false, 1.0f); }
		// The D-pad repeats while it is held. A, B and Start do not: the Start that opened the menu is still down.
		if (Key == EKeys::Gamepad_DPad_Up) { Navigate(EHCJMenuNav::Up); }
		else if (Key == EKeys::Gamepad_DPad_Down) { Navigate(EHCJMenuNav::Down); }
		else if (Key == EKeys::Gamepad_DPad_Left) { Navigate(EHCJMenuNav::Left); }
		else if (Key == EKeys::Gamepad_DPad_Right) { Navigate(EHCJMenuNav::Right); }
		else if (InKeyEvent.IsRepeat()) { }
		else if (Key == EKeys::Gamepad_FaceButton_Bottom) { Navigate(EHCJMenuNav::Accept); }
		else if (Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right) { Navigate(EHCJMenuNav::Back); }
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		GoBack();
		return FReply::Handled();
	}
	// The arrow keys and Tab move the same focus as the pad (a text box that has the keyboard keeps left / right).
	if (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Tab)
	{
		Navigate(Key == EKeys::Up || (Key == EKeys::Tab && InKeyEvent.IsShiftDown()) ? EHCJMenuNav::Up : Key == EKeys::Down || Key == EKeys::Tab ? EHCJMenuNav::Down
			: Key == EKeys::Left ? EHCJMenuNav::Left : EHCJMenuNav::Right);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter && bNavVisible && NavItems.Num() > 0)
	{
		// After the arrow keys Enter presses what they chose.
		Navigate(EHCJMenuNav::Accept);
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Enter && (Page == EHCJMenuPage::Main || Page == EHCJMenuPage::Pause))
	{
		// Enter is the big button of the page (a focused text box or button takes the key before it gets here).
		if (AHCJPlayerController* Controller = Owner.Get())
		{
			if (Page == EHCJMenuPage::Main) { Controller->MenuPlay(); } else { Controller->MenuResume(); }
		}
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::F1 && (Page == EHCJMenuPage::Main || Page == EHCJMenuPage::Pause))
	{
		SetPage(EHCJMenuPage::Controls);
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

TSharedRef<SWidget> SHCJMenu::MakeLabel(const FString& Text, float FontSize, const FLinearColor& Color, bool bBold)
{
	return SNew(STextBlock)
		.Text(FText::FromString(Text))
		.Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::RoundToInt32(FontSize)))
		.ColorAndOpacity(FSlateColor(Color))
		.ShadowOffset(FVector2D(1.0, 1.0))
		.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
}

TSharedRef<SWidget> SHCJMenu::MakeKeyMarkup(const FString& Markup, float FontSize, const FLinearColor& Color)
{
	TArray<FHCJHintRun> Runs;
	HCJHints::SplitKeyMarkup(Markup, Runs);
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
	for (const FHCJHintRun& Run : Runs)
	{
		if (Run.bKey)
		{
			// A key cap: accent outline, dark fill, bold letter.
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(MenuAccent)
				.Padding(FMargin(1.5f))
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(MenuKeyFill)
					.Padding(FMargin(7.0f, 1.0f))
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Run.Text))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt32(FontSize)))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
					]
				]
			];
		}
		else
		{
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeLabel(Run.Text, FontSize, Color)
			];
		}
	}
	return Box;
}

TSharedRef<SWidget> SHCJMenu::MakeKeyLegend(const FString& Markup)
{
	return SNew(SBox).HAlign(HAlign_Center).Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))
		[
			MakeKeyMarkup(Markup, 12.0f, MenuDim)
		];
}

TSharedRef<SWidget> SHCJMenu::MakeControlRow(const FString& Keys, const FString& What)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(190.0f).HAlign(HAlign_Left)
			[
				MakeKeyMarkup(Keys, 14.0f, MenuAccent)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			MakeKeyMarkup(What, 15.0f, MenuText)
		];
}

TSharedRef<SWidget> SHCJMenu::MakeButton(const FString& Label, TFunction<void()> OnClick, bool bPrimary, float FontSize, bool bNavItem)
{
	TSharedRef<SWidget> Button = SNew(SButton)
		.ButtonColorAndOpacity(bPrimary ? MenuButtonPrimary : MenuButton)
		.ContentPadding(FMargin(18.0f, bPrimary ? 10.0f : 6.0f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		// (Never the keyboard focus: it stays with the menu, whose own focus the pad and the arrow keys move.)
		.IsFocusable(false)
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt32(FontSize)))
			.ColorAndOpacity(FSlateColor(FLinearColor::White))
		];
	return bNavItem ? AddNavItem(Label, Button, OnClick, nullptr, bPrimary) : Button;
}

TSharedRef<SWidget> SHCJMenu::MakeLegend(const FString& KeyboardMarkup, const FString& PadMarkup)
{
	const TWeakObjectPtr<AHCJPlayerController> WeakOwner = Owner;
	const auto IsPad = [WeakOwner]() { const AHCJPlayerController* C = WeakOwner.Get(); return C != nullptr && C->IsPadPrompts(); };
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([IsPad]() { return IsPad() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
			[
				MakeKeyLegend(KeyboardMarkup)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([IsPad]() { return IsPad() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				MakeKeyLegend(PadMarkup)
			]
		];
}

TSharedRef<SWidget> SHCJMenu::MakeStepper(const FString& Label, TFunction<FString()> GetValue, TFunction<void(int32)> Step)
{
	return AddNavItem(Label, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeLabel(Label, 16.0f, MenuText)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			MakeButton(TEXT("<"), [Step]() { Step(-1); }, false, 14.0f, false)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(230.0f).HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([GetValue]() { return FText::FromString(GetValue()); })
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FSlateColor(MenuAccent))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			MakeButton(TEXT(">"), [Step]() { Step(+1); }, false, 14.0f, false)
		], nullptr, Step, false);
}

TSharedRef<SWidget> SHCJMenu::MakeSlider(const FString& Label, float Min, float Max, TFunction<float()> GetValue, TFunction<void(float)> SetValue, TFunction<FString(float)> Format)
{
	// Left / right of the pad or the arrow keys: a twentieth of the range.
	const auto Step = [Min, Max, GetValue, SetValue](int32 Direction) { SetValue(FMath::Clamp(GetValue() + static_cast<float>(Direction) * (Max - Min) / 20.0f, Min, Max)); };
	return AddNavItem(Label, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeLabel(Label, 16.0f, MenuText)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(280.0f)
			[
				SNew(SSlider)
				.MinValue(Min)
				.MaxValue(Max)
				.SliderBarColor(MenuDim)
				.SliderHandleColor(MenuAccent)
				.IsFocusable(false)
				.Value_Lambda([GetValue]() { return GetValue(); })
				.OnValueChanged_Lambda([SetValue](float Value) { SetValue(Value); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(12.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(SBox).WidthOverride(70.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([GetValue, Format]() { return FText::FromString(Format(GetValue())); })
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FSlateColor(MenuAccent))
			]
		], nullptr, Step, false);
}

TSharedRef<SWidget> SHCJMenu::MakeTextRow(const FString& Label, const FString& Hint, TFunction<FString()> GetValue, TFunction<void(const FString&)> SetValue, float Width)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeLabel(Label, 16.0f, MenuText)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(Width)
			[
				SNew(SEditableTextBox)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
				.HintText(FText::FromString(Hint))
				.Text_Lambda([GetValue]() { return FText::FromString(GetValue()); })
				.OnTextChanged_Lambda([SetValue](const FText& Text) { SetValue(Text.ToString()); })
				.SelectAllTextWhenFocused(true)
			]
		];
}

TSharedRef<SWidget> SHCJMenu::BuildMain()
{
	const TWeakObjectPtr<AHCJPlayerController> WeakOwner = Owner;
	const FMargin Row(0.0f, 5.0f);

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeLabel(TEXT("HYPECUBE JAM"), 46.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
		[
			MakeLabel(TEXT("Free-for-all dogfights on the corners of a hypercube"), 15.0f, MenuDim)
		];

	if (!Notice.IsEmpty())
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
		[
			MakeLabel(Notice, 15.0f, MenuWarning, true)
		];
	}

	// The difficulty comes first, above PLAY, with a line that says what the level does to the player. (Outside the
	// platform switch below: the web build has it too.)
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeStepper(TEXT("Difficulty"), []() { return FString(DifficultyName(UHCJUserSettings::Get().GetBotDifficulty())); },
			[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.SetBotDifficulty(HCJBalance::StepDifficulty(S.GetBotDifficulty(), Step)); })
	];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
	[
		// (The page is not rebuilt when the stepper is pressed: the text follows the setting.)
		SNew(STextBlock)
		.Text_Lambda([]() { return FText::FromString(HCJBalance::GetDifficultyDescription(UHCJUserSettings::Get().GetBotDifficulty())); })
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
		.ColorAndOpacity(FSlateColor(MenuDim))
	];

#if PLATFORM_WASM
	// The web build is single-player: a browser cannot host or reach a host, and there is no online service.
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("PLAY   (a match against bots)"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuPlay(); } }, true, 24.0f)
	];
#else
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("QUICK PLAY   (join an open match, or host one)"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuQuickPlay(); } }, true, 24.0f)
	];
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("PLAY   (host a match with bots)"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuPlay(); } }, false, 20.0f)
	];
#endif
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([]()
			{
				const UHCJUserSettings& S = UHCJUserSettings::Get();
				return FText::FromString(FString::Printf(TEXT("Your ship:  %s"), HCJShipTypes::Get(S.ShipType).Name));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
			.ColorAndOpacity(FSlateColor(MenuAccent))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			MakeButton(TEXT("HANGAR  (choose your ship)"), [this]() { SetPage(EHCJMenuPage::Hangar); }, false, 17.0f)
		]
	];
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeStepper(TEXT("Bots"), []() { return FString::FromInt(UHCJUserSettings::Get().BotCount); },
			[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.BotCount = FMath::Clamp(S.BotCount + Step, 0, 15); })
	];
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeStepper(TEXT("Hypercube"), []() { const int32 D = UHCJUserSettings::Get().Dimension; return FString::Printf(TEXT("%dD  -  %d sectors"), D, 1 << D); },
			[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.Dimension = FMath::Clamp(S.Dimension + Step, 3, 5); })
	];
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeTextRow(TEXT("Seed (same seed = same universe)"), TEXT("random"),
			[]() { const int32 Seed = UHCJUserSettings::Get().Seed; return Seed != 0 ? FString::FromInt(Seed) : FString(); },
			[](const FString& Text) { UHCJUserSettings::Get().Seed = FCString::Atoi(*Text); }, 180.0f)
	];
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeTextRow(TEXT("Pilot name"), TEXT("default"),
			[]() { return UHCJUserSettings::Get().PlayerName; },
			[](const FString& Text) { UHCJUserSettings::Get().PlayerName = Text.Left(20); }, 260.0f)
	];

#if PLATFORM_WASM
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))
	[
		MakeLabel(TEXT("Web build: single-player against bots. Online play (quick play, find games,"), 13.0f, MenuDim)
	];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 0.0f))
	[
		MakeLabel(TEXT("join by address, leaderboards) is in the Windows build."), 13.0f, MenuDim)
	];
#else
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 16.0f, 0.0f, 5.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeLabel(TEXT("Join a host at address"), 16.0f, MenuText)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(220.0f)
			[
				SNew(SEditableTextBox)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
				.HintText(FText::FromString(TEXT("ip address")))
				.Text_Lambda([]() { return FText::FromString(UHCJUserSettings::Get().LastJoinAddress); })
				.OnTextChanged_Lambda([](const FText& Text) { UHCJUserSettings::Get().LastJoinAddress = Text.ToString(); })
				.OnTextCommitted_Lambda([WeakOwner](const FText& Text, ETextCommit::Type Commit)
				{
					if (Commit == ETextCommit::OnEnter)
					{
						if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuJoin(Text.ToString()); }
					}
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
		[
			MakeButton(TEXT("JOIN"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuJoin(UHCJUserSettings::Get().LastJoinAddress); } }, false, 18.0f)
		]
	];

	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
		[
			MakeButton(TEXT("FIND GAMES"), [this]() { UHCJUserSettings::Get().Save(); SetPage(EHCJMenuPage::Lobbies); }, false, 18.0f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
		[
			MakeButton(TEXT("LEADERBOARDS"), [this]() { SetPage(EHCJMenuPage::Leaderboards); }, false, 18.0f)
		]
	];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
	[
		// Live text, not a rebuild: the sign-in finishes while somebody may be typing a name.
		SNew(STextBlock)
		.Text_Lambda([WeakOwner]()
		{
			const UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(WeakOwner.Get());
			return FText::FromString(Online != nullptr ? Online->GetTitleStatusLine() : FString(TEXT("Online services unavailable")));
		})
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
		.ColorAndOpacity(FSlateColor(MenuDim))
	];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center)
	[
		// The pilot's lifetime record (live text as well); takes no room while it is not known.
		SNew(STextBlock)
		.Text_Lambda([WeakOwner]()
		{
			const UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(WeakOwner.Get());
			const FString Record = Online != nullptr ? Online->GetPilotRecordLine() : FString();
			return FText::FromString(Record.IsEmpty() ? FString() : FString::Printf(TEXT("Your record:  %s"), *Record));
		})
		.Visibility_Lambda([WeakOwner]()
		{
			const UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(WeakOwner.Get());
			return Online != nullptr && !Online->GetPilotRecordLine().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		})
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
		.ColorAndOpacity(FSlateColor(MenuDim))
	];
#endif

	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
		[
			MakeButton(TEXT("CONTROLS"), [this]() { SetPage(EHCJMenuPage::Controls); }, false, 18.0f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 6.0f, 0.0f))
		[
			MakeButton(TEXT("SETTINGS"), [this]() { SetPage(EHCJMenuPage::Settings); }, false, 18.0f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
		[
			MakeButton(TEXT("QUIT"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuQuit(); } }, false, 18.0f)
		]
	];

	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))
	[
		MakeLabel(TEXT("Portals lead to neighbouring corners of the hypercube. First to the frag limit wins."), 13.0f, MenuText)
	];
	Box->AddSlot().AutoHeight()
	[
		MakeLegend(TEXT("[Enter] play with bots     [F1] controls     in a match:  [Esc] menu   [F1] help   [H] hints"),
			TEXT("[D-pad] / [L stick] move     [A] select     left / right: change a value     in a match:  [Start] menu   [L3] help"))
	];
	return Box;
}

TSharedRef<SWidget> SHCJMenu::BuildSettings()
{
	// (5: two rows more than before - the controller's - in the same height.)
	const FMargin Row(0.0f, 5.0f);
	const auto Percent = [](float Value) { return FString::Printf(TEXT("%d %%"), FMath::RoundToInt32(Value * 100.0f)); };

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
		[
			MakeLabel(TEXT("SETTINGS"), 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeSlider(TEXT("Mouse sensitivity"), 0.2f, 3.0f, []() { return UHCJUserSettings::Get().MouseSensitivity; },
				[](float Value) { UHCJUserSettings::Get().MouseSensitivity = Value; }, [](float Value) { return FString::Printf(TEXT("%.2f x"), Value); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Controller (Xbox / Steam Controller)"), []() { return FString(UHCJUserSettings::Get().bController ? TEXT("On") : TEXT("Off")); },
				[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.bController = !S.bController; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeSlider(TEXT("Controller sensitivity"), HCJPad::MinSensitivity, HCJPad::MaxSensitivity, []() { return UHCJUserSettings::Get().ControllerSensitivity; },
				[](float Value) { UHCJUserSettings::Get().ControllerSensitivity = Value; }, [](float Value) { return FString::Printf(TEXT("%.2f x"), Value); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeSlider(TEXT("Music volume"), 0.0f, 1.0f, []() { return UHCJUserSettings::Get().MusicVolume; },
				[](float Value) { UHCJUserSettings::Get().MusicVolume = Value; }, Percent)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeSlider(TEXT("Effects volume"), 0.0f, 1.0f, []() { return UHCJUserSettings::Get().SfxVolume; },
				[](float Value) { UHCJUserSettings::Get().SfxVolume = Value; }, Percent)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeSlider(TEXT("Interface volume"), 0.0f, 1.0f, []() { return UHCJUserSettings::Get().UiVolume; },
				[](float Value) { UHCJUserSettings::Get().UiVolume = Value; }, Percent)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("In-game hints (key prompts, tips)"), []() { return FString(UHCJUserSettings::Get().bHints ? TEXT("On") : TEXT("Off")); },
				[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.bHints = !S.bHints; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Computer voice (spoken warnings)"), []() { return FString(UHCJUserSettings::Get().bComputerVoice ? TEXT("On") : TEXT("Off")); },
				[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.bComputerVoice = !S.bComputerVoice; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Camera shake (explosions)"), []() { return FString(UHCJUserSettings::Get().bCameraShake ? TEXT("On") : TEXT("Off")); },
				[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.bCameraShake = !S.bCameraShake; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Enemy highlight (glow on other ships)"), []() { return FString(UHCJUserSettings::Get().bEnemyHighlight ? TEXT("On") : TEXT("Off")); },
				[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.bEnemyHighlight = !S.bEnemyHighlight; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Cinematic look (bloom, colour grade, more debris)"), []() { return FString(UHCJUserSettings::Get().bCinematicLook ? TEXT("On") : TEXT("Off")); },
				[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.bCinematicLook = !S.bCinematicLook; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Music"), []() { return FString(UHCJUserSettings::Get().MusicSet == 0 ? TEXT("Lyria tracks") : TEXT("Lyra stems")); },
				[](int32 Step) { UHCJUserSettings& S = UHCJUserSettings::Get(); S.MusicSet = S.MusicSet == 0 ? 1 : 0; })
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 6.0f))
		[
			MakeLabel(TEXT("Saved for the next start. Volumes, hints, voice, shake, highlight, music and the controller change at once."), 13.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 2.0f))
		[
			MakeLabel(TEXT("Music generated with Google Lyria (Gemini). Sound effects and the older stems: Lyra Starter Game (Epic Games)."), 12.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 2.0f))
		[
			MakeLabel(TEXT("Space Station 4 by Gerardo Justel (CC-BY). Intro video generated with Google Veo (Gemini)."), 12.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 2.0f))
		[
			MakeLabel(TEXT("Voice: Flite speech synthesiser, Carnegie Mellon University"), 12.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
		[
			MakeButton(TEXT("BACK"), [this]() { UHCJUserSettings::Get().Save(); SetPage(SettingsReturnPage); }, true, 20.0f)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeLegend(TEXT("[Esc] back"), TEXT("[D-pad] / [L stick] move     left / right: change     [A] select     [B] back"))
		];
}

TSharedRef<SWidget> SHCJMenu::BuildPause()
{
	const TWeakObjectPtr<AHCJPlayerController> WeakOwner = Owner;
	const FMargin Row(0.0f, 6.0f);

	// (Built from top to bottom: the order of the calls is the order of the pad's focus.)
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
		[
			MakeLabel(TEXT("MENU"), 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 12.0f))
		[
			MakeLabel(TEXT("The match goes on while this is open."), 13.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeButton(TEXT("RESUME"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuResume(); } }, true, 22.0f)
		];

	// The host (and a single player) can make the bots easier or harder without leaving the match: under RESUME.
	if (WeakOwner.IsValid() && WeakOwner->HasAuthority())
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
		[
			MakeStepper(TEXT("Bot difficulty (applies now)"),
				[WeakOwner]() { const AHCJPlayerController* C = WeakOwner.Get(); return FString(DifficultyName(C != nullptr ? C->GetMenuDifficulty() : 0)); },
				[WeakOwner](int32 Step) { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuStepDifficulty(Step); } })
		];
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 8.0f))
		[
			SNew(STextBlock)
			.Text_Lambda([WeakOwner]() { const AHCJPlayerController* C = WeakOwner.Get(); return FText::FromString(HCJBalance::GetDifficultyDescription(C != nullptr ? C->GetMenuDifficulty() : 0)); })
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
			.ColorAndOpacity(FSlateColor(MenuDim))
		];
	}

	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("HANGAR  (your next ship)"), [this]() { SetPage(EHCJMenuPage::Hangar); }, false, 20.0f)
	];
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("SETTINGS"), [this]() { SetPage(EHCJMenuPage::Settings); }, false, 20.0f)
	];
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("CONTROLS"), [this]() { SetPage(EHCJMenuPage::Controls); }, false, 20.0f)
	];
#if !PLATFORM_WASM
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("LEADERBOARDS"), [this]() { SetPage(EHCJMenuPage::Leaderboards); }, false, 20.0f)
	];
#endif
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("LEAVE TO MENU"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuLeave(); } }, false, 20.0f)
	];
	Box->AddSlot().AutoHeight().Padding(Row)
	[
		MakeButton(TEXT("QUIT GAME"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuQuit(); } }, false, 20.0f)
	];
	Box->AddSlot().AutoHeight()
	[
		MakeLegend(TEXT("[Esc] or [Enter] resume     [F1] controls"), TEXT("[D-pad] / [L stick] move     [A] select     [B] or [Start] resume"))
	];
	return Box;
}

TSharedRef<SWidget> SHCJMenu::BuildControls()
{
	const FMargin Row(0.0f, 3.0f);
	const auto Head = [this](const TCHAR* Text)
	{
		return SNew(SBox).Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
			[
				MakeLabel(Text, 13.0f, MenuDim, true)
			];
	};

	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ Head(TEXT("FLIGHT AND COMBAT")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Mouse]"), TEXT("steer: the ship flies where you look")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[W] [S]"), TEXT("thrust forward / back")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[A] [D]"), TEXT("strafe left / right")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Space] [Ctrl]"), TEXT("up / down   (also [E] [Q])")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Shift]"), TEXT("boost: drains the BOOST bar")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Left mouse]"), TEXT("fire: hold for the phaser beam")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Right mouse]"), TEXT("photon torpedo   (also [X])")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[B]"), TEXT("switch phaser / blaster")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[T]"), TEXT("next target")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[F]"), TEXT("target the nearest enemy")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Middle mouse]"), TEXT("hold / release the target")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[G]"), TEXT("drop a pickup weapon")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Z] hold"), TEXT("warp drive: charge, then jump ahead")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[C]"), TEXT("cloak on / off (no shields, no weapons)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[R] hold"), TEXT("tractor beam on the target")) ];

	TSharedRef<SVerticalBox> Right = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ Head(TEXT("INTERFACE")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Tab]"), TEXT("scoreboard (hold)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[V]"), TEXT("first / third person")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[H]"), TEXT("hints on / off")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[F1]"), TEXT("controls overlay")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[Esc]"), TEXT("menu, settings, quit")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeControlRow(TEXT("[R] [Enter]"), TEXT("respawn now, when destroyed")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f)) [ Head(TEXT("ON THE HUD")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("Red brackets and arrows: enemies. Orange diamond: your target."), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("Yellow circle: aim here. It turns green when the shot will hit."), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("Yellow marker: the way to the nearest enemies."), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("Diamonds: pads with repairs, power-ups and weapons."), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("Four arcs around a ship: its shield facings (fore, aft, port,"), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("starboard). Red = down: hits from that side reach the hull."), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("Torpedoes: a full facing stops most of one; an open side does not."), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("Phaser: a beam, strongest up close; it drains the bank in the WEAPON"), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("panel. Phasers and torpedoes break rocks; bolts do not."), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("Warp: no weapons; a hit while charging stops it. Cloak: drains its"), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("meter; a hit or an enemy within 15 m reveals you. Tractor beam: uses"), 14.0f, MenuText) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ MakeLabel(TEXT("phaser energy; caught in one, boost to break free."), 14.0f, MenuText) ];

	// The game controller: the same things by their pad names (Xbox layout; a Steam Controller through Steam Input
	// reports the same buttons).
	const auto PadRow = [this](const TCHAR* Keys, const TCHAR* What)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(150.0f).HAlign(HAlign_Left)
				[
					MakeKeyMarkup(Keys, 14.0f, MenuAccent)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakeKeyMarkup(What, 15.0f, MenuText)
			];
	};
	TSharedRef<SVerticalBox> PadColumn = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ Head(UHCJUserSettings::Get().bController ? TEXT("CONTROLLER  (Xbox / Steam Controller)") : TEXT("CONTROLLER  (switched off in SETTINGS)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[R stick]"), TEXT("steer")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[L stick]"), TEXT("thrust and strafe")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[A] [B]"), TEXT("up / down")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[LB] hold"), TEXT("boost")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[RT]"), TEXT("fire: hold for the phaser")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[LT]"), TEXT("photon torpedo")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[X]"), TEXT("switch phaser / blaster")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[RB]"), TEXT("next target")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[Y]"), TEXT("target the nearest enemy")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[D-pad down]"), TEXT("drop a pickup weapon")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[D-pad up]"), TEXT("hold: warp drive")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[D-pad left]"), TEXT("cloak on / off")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[D-pad right]"), TEXT("hold: tractor beam")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[Back]"), TEXT("scoreboard (hold)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[R3]"), TEXT("first / third person  (stick click)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[L3]"), TEXT("controls overlay  (stick click)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[Start]"), TEXT("menu, settings, quit")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("[A] [RT]"), TEXT("respawn now, when destroyed")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row) [ PadRow(TEXT("menus"), TEXT("[D-pad] move   [A] select   [B] back")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f)) [ MakeLabel(TEXT("Steam Controller: add the game to Steam and pick"), 13.0f, MenuDim) ]
		+ SVerticalBox::Slot().AutoHeight() [ MakeLabel(TEXT("the Gamepad layout (see README). On / off: SETTINGS."), 13.0f, MenuDim) ];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
		[
			MakeLabel(TEXT("CONTROLS"), 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 16.0f))
		[
			MakeLabel(TEXT("Free-for-all: first to the frag limit wins. Portals lead to the neighbouring corners of the hypercube."), 14.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 40.0f, 0.0f))
			[
				Left
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 40.0f, 0.0f))
			[
				Right
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				PadColumn
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 18.0f, 0.0f, 0.0f))
		[
			SNew(SBox).MinDesiredWidth(360.0f)
			[
				MakeButton(TEXT("BACK"), [this]() { SetPage(SettingsReturnPage); }, true, 20.0f)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeLegend(TEXT("[Esc] back"), TEXT("[D-pad] / [L stick] move     left / right: change     [A] select     [B] back"))
		];
}

TSharedRef<SWidget> SHCJMenu::BuildHangar()
{
	const TWeakObjectPtr<AHCJPlayerController> WeakOwner = Owner;
	const FMargin Row(0.0f, 5.0f);
	const auto Apply = [WeakOwner]()
	{
		UHCJUserSettings::Get().Save();
		if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuApplyShip(); }
	};

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
		[
			MakeLabel(TEXT("HANGAR"), 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.0f, 0.0f, 0.0f, 12.0f))
		[
			MakeLabel(SettingsReturnPage == EHCJMenuPage::Pause ? TEXT("Your next ship: it is yours when you respawn.") : TEXT("Choose the ship you fly."), 14.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Ship type"), []() { return FString(HCJShipTypes::Get(UHCJUserSettings::Get().ShipType).Name); },
				[](int32 Step)
				{
					UHCJUserSettings& S = UHCJUserSettings::Get();
					S.ShipType = (HCJShipTypes::SanitizeType(S.ShipType) + Step + HCJShipTypes::NumTypes) % HCJShipTypes::NumTypes;
					S.ShipVariant = 0;
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Hull"), []()
				{
					const UHCJUserSettings& S = UHCJUserSettings::Get();
					const int32 Num = HCJShipTypes::Get(S.ShipType).NumHulls;
					return S.ShipVariant < 0 ? FString(TEXT("Surprise me")) : FString::Printf(TEXT("%d of %d"), S.ShipVariant + 1, Num);
				},
				[](int32 Step)
				{
					// -1 ("surprise me") sits between the last hull and the first.
					UHCJUserSettings& S = UHCJUserSettings::Get();
					const int32 Num = HCJShipTypes::Get(S.ShipType).NumHulls;
					S.ShipVariant = ((S.ShipVariant + 1 + Step + (Num + 1)) % (Num + 1)) - 1;
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(Row)
		[
			MakeStepper(TEXT("Paint"), []()
				{
					static const TCHAR* Names[] = { TEXT("Red"), TEXT("Orange"), TEXT("Yellow"), TEXT("Green"), TEXT("Cyan"), TEXT("Blue"), TEXT("Purple"), TEXT("White"), TEXT("Grey"), TEXT("Black") };
					const int32 Skin = UHCJUserSettings::Get().ShipSkin;
					return Skin < 0 ? FString(TEXT("By pilot colour")) : Skin < UE_ARRAY_COUNT(Names) ? FString(Names[Skin]) : FString::Printf(TEXT("Paint %d"), Skin + 1);
				},
				[](int32 Step)
				{
					UHCJUserSettings& S = UHCJUserSettings::Get();
					const int32 Num = FMath::Max(UHCJAssetSettings::Get().ShipSkins.Num(), 1);
					S.ShipSkin = ((S.ShipSkin + 1 + Step + (Num + 1)) % (Num + 1)) - 1;
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 8.0f))
		[
			SNew(SBox).WidthOverride(560.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([]() { return FText::FromString(HCJShipTypes::Get(UHCJUserSettings::Get().ShipType).Description); })
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
				.ColorAndOpacity(FSlateColor(MenuText))
				.AutoWrapText(true)
			]
		];

	for (int32 StatIndex = 0; StatIndex < static_cast<int32>(HCJShipTypes::EStat::Count); ++StatIndex)
	{
		const HCJShipTypes::EStat Stat = static_cast<HCJShipTypes::EStat>(StatIndex);
		Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(230.0f)
				[
					MakeLabel(HCJShipTypes::GetStatName(Stat), 13.0f, MenuDim, true)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SBox).HeightOverride(12.0f)
				[
					SNew(SProgressBar)
					.Percent_Lambda([Stat]() { return TOptional<float>(HCJShipTypes::GetStat(UHCJUserSettings::Get().ShipType, Stat)); })
					.FillColorAndOpacity(FSlateColor(MenuAccent))
				]
			]
		];
	}

	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
	[
		SNew(STextBlock)
		.Text_Lambda([]()
		{
			const FHCJShipTypeDef& Def = HCJShipTypes::Get(UHCJUserSettings::Get().ShipType);
			return FText::FromString(FString::Printf(TEXT("Hull %.0f   -   shields 4 x %.0f   -   %.0f m/s (boost %.0f)   -   %d torpedoes\nCloak %.0f s   -   warp every %.0f s   -   tractor beam %.0f m"),
				Def.MaxHull, Def.ShieldFacing, Def.CruiseSpeed / 100.0f, Def.CruiseSpeed / 50.0f, Def.Torpedoes, Def.CloakSeconds, Def.WarpCooldown, Def.TractorRange / 100.0f));
		})
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
		.ColorAndOpacity(FSlateColor(MenuDim))
	];
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 0.0f))
	[
		MakeButton(TEXT("DONE"), [this, Apply]() { Apply(); SetPage(SettingsReturnPage); }, true, 20.0f)
	];
	Box->AddSlot().AutoHeight()
	[
		MakeLegend(TEXT("[Esc] back"), TEXT("[D-pad] / [L stick] move     left / right: change     [A] select     [B] back"))
	];
	return Box;
}

TSharedRef<SWidget> SHCJMenu::BuildConnecting()
{
	const TWeakObjectPtr<AHCJPlayerController> WeakOwner = Owner;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
		[
			MakeLabel(TEXT("CONNECTING"), 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 16.0f))
		[
			MakeLabel(Notice, 16.0f, MenuText)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeButton(TEXT("CANCEL"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuCancelConnect(); } }, false, 20.0f)
		];
}

TSharedRef<SWidget> SHCJMenu::BuildQuickPlay()
{
	const TWeakObjectPtr<AHCJPlayerController> WeakOwner = Owner;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
		[
			MakeLabel(TEXT("QUICK PLAY"), 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
		[
			// Live text: the steps follow each other without a rebuild of the page.
			SNew(SBox).MinDesiredWidth(620.0f).HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([WeakOwner]()
				{
					const UHCJEOSSubsystem* Online = UHCJEOSSubsystem::Get(WeakOwner.Get());
					return FText::FromString(Online != nullptr ? Online->GetQuickPlay().GetStatus() : FString());
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
				.ColorAndOpacity(FSlateColor(MenuText))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 16.0f))
		[
			MakeLabel(TEXT("Finds an open match through Epic Online Services; hosts one when there is none."), 13.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeButton(TEXT("CANCEL"), [WeakOwner]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuCancelQuickPlay(); } }, false, 20.0f)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeLegend(TEXT("[Esc] cancel"), TEXT("[A] or [B] cancel"))
		];
}

TSharedRef<SWidget> SHCJMenu::BuildOnlineUnavailable(const FString& Title, const FString& InStatus)
{
	// The headline already says "unavailable": keep the reason only.
	FString Status = InStatus;
	Status.RemoveFromStart(TEXT("Online services unavailable: "));
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
		[
			MakeLabel(Title, 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 10.0f, 0.0f, 6.0f))
		[
			MakeLabel(TEXT("Online services unavailable"), 22.0f, MenuWarning, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
		[
			MakeLabel(Status, 14.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
		[
			MakeLabel(TEXT("Everything else works: PLAY and QUICK PLAY host a match, and JOIN on the title page"), 15.0f, MenuText)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 16.0f))
		[
			MakeLabel(TEXT("connects to a host by its network address."), 15.0f, MenuText)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeButton(TEXT("BACK"), [this]() { SetPage(SettingsReturnPage); }, true, 20.0f)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeLegend(TEXT("[Esc] back"), TEXT("[D-pad] / [L stick] move     left / right: change     [A] select     [B] back"))
		];
}

TSharedRef<SWidget> SHCJMenu::BuildLobbies()
{
	const TWeakObjectPtr<AHCJPlayerController> WeakOwner = Owner;
	UHCJEOSSubsystem* Online = GetOnline();
	if (Online == nullptr || HCJEos::IsUnavailable(Online->GetState()))
	{
		return BuildOnlineUnavailable(TEXT("FIND GAMES"), Online != nullptr ? Online->GetStatusLine() : FString(TEXT("No online subsystem in this run")));
	}

	const float PlayersWidth = 110.0f;
	const float BotsWidth = 70.0f;
	// (275: "4D - 16 sectors  -  Normal" at 16 pt needs about 250; at 230 the JOIN button cut it off.)
	const float MapWidth = 275.0f;
	const float JoinWidth = 120.0f;
	const auto Cell = [this](const FString& Text, float Width, const FLinearColor& Color, float Size, bool bBold)
	{
		return SNew(SBox).WidthOverride(Width).VAlign(VAlign_Center)
			[
				MakeLabel(Text, Size, Color, bBold)
			];
	};

	const bool bReady = Online->IsReady();
	const FString Status = !bReady ? Online->GetStatusLine()
		: !Notice.IsEmpty() ? Notice
		: Online->GetLobbyStatus().IsEmpty() ? FString(TEXT("Searching...")) : Online->GetLobbyStatus();

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
		[
			MakeLabel(TEXT("FIND GAMES"), 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 12.0f))
		[
			MakeLabel(Status, 15.0f, !Notice.IsEmpty() ? MenuWarning : MenuDim, !Notice.IsEmpty())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8.0f, 0.0f, 8.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center) [ MakeLabel(TEXT("HOST"), 13.0f, MenuDim, true) ]
			+ SHorizontalBox::Slot().AutoWidth() [ Cell(TEXT("PLAYERS"), PlayersWidth, MenuDim, 13.0f, true) ]
			+ SHorizontalBox::Slot().AutoWidth() [ Cell(TEXT("BOTS"), BotsWidth, MenuDim, 13.0f, true) ]
			+ SHorizontalBox::Slot().AutoWidth() [ Cell(TEXT("MAP"), MapWidth, MenuDim, 13.0f, true) ]
			+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(JoinWidth) ]
		];

	TSharedRef<SScrollBox> List = SNew(SScrollBox);
	const FString LocalVersion = UHCJEOSSubsystem::GetBuildVersion();
	const int64 NowUnix = FDateTime::UtcNow().ToUnixTimestamp();
	const TArray<FHCJLobbyInfo>& Lobbies = Online->GetLobbies();
	for (int32 Index = 0; Index < Lobbies.Num(); ++Index)
	{
		const FHCJLobbyInfo& Lobby = Lobbies[Index];
		const bool bSameVersion = Lobby.Version == LocalVersion;
		const bool bFull = Lobby.Players >= Lobby.MaxPlayers;
		const FLinearColor Color = bSameVersion ? MenuText : MenuDim;
		// Under the host's name, when the host says so (0.19 and later): frag limit, time left, who else is in.
		const FString Rules = Lobby.DescribeRules(NowUnix);
		TSharedRef<SWidget> Action = bSameVersion && !bFull
			? MakeButton(TEXT("JOIN"), [WeakOwner, Index]() { if (AHCJPlayerController* C = WeakOwner.Get()) { C->MenuJoinLobby(Index); } }, false, 16.0f)
			: MakeLabel(bFull ? TEXT("full") : TEXT("other version"), 13.0f, MenuDim);
		List->AddSlot().Padding(FMargin(0.0f, 2.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(MenuRowFill)
			.Padding(FMargin(8.0f, 4.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center) [ MakeLabel(Lobby.HostName, 16.0f, bSameVersion ? MenuAccent : MenuDim, true) ]
					+ SHorizontalBox::Slot().AutoWidth() [ Cell(FString::Printf(TEXT("%d / %d"), Lobby.Players, Lobby.MaxPlayers), PlayersWidth, Color, 16.0f, false) ]
					+ SHorizontalBox::Slot().AutoWidth() [ Cell(FString::FromInt(Lobby.Bots), BotsWidth, Color, 16.0f, false) ]
					+ SHorizontalBox::Slot().AutoWidth() [ Cell(Lobby.DescribeMapAndDifficulty(), MapWidth, Color, 16.0f, false) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center) [ SNew(SBox).WidthOverride(JoinWidth).MinDesiredHeight(34.0f).HAlign(bSameVersion && !bFull ? HAlign_Fill : HAlign_Center).VAlign(VAlign_Center) [ Action ] ]
				]
				// (A line of its own across the row: a list of pilots is wider than the host column.)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).Visibility(Rules.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
					[
						MakeLabel(Rules, 12.0f, MenuDim)
					]
				]
			]
		];
	}
	if (Lobbies.Num() == 0)
	{
		List->AddSlot().HAlign(HAlign_Center).Padding(FMargin(0.0f, 30.0f))
		[
			MakeLabel(Online->IsSearchingLobbies() || !bReady ? TEXT("...") : TEXT("- no games -"), 16.0f, MenuDim)
		];
	}

	Box->AddSlot().AutoHeight()
	[
		SNew(SBox).MinDesiredWidth(780.0f).MinDesiredHeight(120.0f).MaxDesiredHeight(330.0f)
		[
			List
		]
	];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
	[
		MakeLabel(TEXT("Games are found through Epic Online Services and joined by network address: a host outside"), 13.0f, MenuDim)
	];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center)
	[
		MakeLabel(TEXT("your network must have UDP port 7777 forwarded and its public address set (see README)."), 13.0f, MenuDim)
	];
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
		[
			MakeButton(Online->IsSearchingLobbies() ? TEXT("SEARCHING...") : TEXT("REFRESH"), [this]()
			{
				if (UHCJEOSSubsystem* O = GetOnline()) { O->RefreshLobbies(); }
				SetPage(EHCJMenuPage::Lobbies);
			}, true, 20.0f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
		[
			MakeButton(TEXT("BACK"), [this]() { SetPage(SettingsReturnPage); }, false, 20.0f)
		]
	];
	Box->AddSlot().AutoHeight()
	[
		MakeLegend(TEXT("[Esc] back"), TEXT("[D-pad] / [L stick] move     left / right: change     [A] select     [B] back"))
	];
	return Box;
}

TSharedRef<SWidget> SHCJMenu::MakeLeaderboardColumn(int32 Index)
{
	const UHCJEOSSubsystem* Online = GetOnline();
	const FHCJLeaderboardView& View = Online->GetLeaderboard(Index);
	const float RankWidth = 46.0f;
	const float NameWidth = 220.0f;
	const float ScoreWidth = 70.0f;
	const auto Row = [this, RankWidth, NameWidth, ScoreWidth](const FString& Rank, const FString& Name, const FString& Score, const FLinearColor& Color, float Size, bool bBold)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(RankWidth) [ MakeLabel(Rank, Size, Color, bBold) ] ]
			+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(NameWidth) [ MakeLabel(Name, Size, Color, bBold) ] ]
			+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(ScoreWidth).HAlign(HAlign_Right) [ MakeLabel(Score, Size, Color, bBold) ] ];
	};

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
		[
			MakeLabel(FString::Printf(TEXT("MOST %s"), *View.Title), 18.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
		[
			Row(TEXT("#"), TEXT("PILOT"), View.Title, MenuDim, 13.0f, true)
		];

	if (View.Rows.Num() == 0)
	{
		Column->AddSlot().AutoHeight().Padding(FMargin(0.0f, 20.0f))
		[
			MakeLabel(View.bLoading || !View.bLoaded ? FString(TEXT("Loading...")) : View.Error.IsEmpty() ? FString(TEXT("No scores yet")) : View.Error, 15.0f, MenuDim)
		];
	}
	int32 PreviousRank = 0;
	for (const FHCJLeaderboardRow& Entry : View.Rows)
	{
		if (PreviousRank > 0 && Entry.Rank > PreviousRank + 1)
		{
			// The local player's row from further down the board.
			Column->AddSlot().AutoHeight() [ MakeLabel(TEXT("..."), 13.0f, MenuDim) ];
		}
		PreviousRank = Entry.Rank;
		Column->AddSlot().AutoHeight().Padding(FMargin(0.0f, 2.0f))
		[
			Row(FString::FromInt(Entry.Rank), Entry.bLocal ? Entry.Name + TEXT("  (you)") : Entry.Name, FString::FromInt(Entry.Score),
				Entry.bLocal ? MenuAccent : MenuText, 15.0f, Entry.bLocal)
		];
	}
	return Column;
}

TSharedRef<SWidget> SHCJMenu::BuildLeaderboards()
{
	UHCJEOSSubsystem* Online = GetOnline();
	if (Online == nullptr || HCJEos::IsUnavailable(Online->GetState()))
	{
		return BuildOnlineUnavailable(TEXT("LEADERBOARDS"), Online != nullptr ? Online->GetStatusLine() : FString(TEXT("No online subsystem in this run")));
	}

	const bool bLoading = Online->GetLeaderboard(0).bLoading || Online->GetLeaderboard(1).bLoading;
	// The local pilot's lifetime numbers above the boards (nothing while they are not known).
	const FString Record = Online->GetPilotRecordLine();
	TSharedRef<SWidget> RecordStrip = Record.IsEmpty() ? StaticCastSharedRef<SWidget>(SNew(SBox))
		: StaticCastSharedRef<SWidget>(SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(MenuRowFill)
			.Padding(FMargin(14.0f, 6.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, 14.0f, 0.0f)) [ MakeLabel(TEXT("YOUR RECORD"), 13.0f, MenuAccent, true) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center) [ MakeLabel(Record, 15.0f, MenuText) ]
			]);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
		[
			MakeLabel(TEXT("LEADERBOARDS"), 32.0f, MenuAccent, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
		[
			MakeLabel(Online->GetStatusLine(), 14.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 0.0f, 0.0f, 14.0f))
		[
			RecordStrip
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MinDesiredHeight(300.0f).HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 44.0f, 0.0f))
				[
					MakeLeaderboardColumn(0)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeLeaderboardColumn(1)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
		[
			MakeLabel(TEXT("Your kills, deaths, wins, turrets and matches are sent at the end of every match and when you leave."), 13.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeLabel(TEXT("The boards follow a few minutes later. A pilot is the Windows user of a machine, not an account."), 13.0f, MenuDim)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				MakeButton(bLoading ? TEXT("LOADING...") : TEXT("REFRESH"), [this]()
				{
					if (UHCJEOSSubsystem* O = GetOnline()) { O->RefreshLeaderboards(); }
					SetPage(EHCJMenuPage::Leaderboards);
				}, true, 20.0f)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
			[
				MakeButton(TEXT("BACK"), [this]() { SetPage(SettingsReturnPage); }, false, 20.0f)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeLegend(TEXT("[Esc] back"), TEXT("[D-pad] / [L stick] move     left / right: change     [A] select     [B] back"))
		];
}
