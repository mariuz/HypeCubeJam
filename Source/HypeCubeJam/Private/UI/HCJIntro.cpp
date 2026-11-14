#include "UI/HCJIntro.h"

#include "Engine/Texture.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SHCJIntro::Construct(const FArguments& InArgs)
{
	OnSkip = InArgs._OnSkip;
	TitleAlpha = InArgs._TitleAlpha;
	BlackAlpha = InArgs._BlackAlpha;

	PictureBrush.SetResourceObject(InArgs._Texture);
	PictureBrush.ImageSize = InArgs._PictureSize;
	PictureBrush.DrawAs = ESlateBrushDrawType::Image;
	PictureBrush.Tiling = ESlateBrushTileType::NoTile;

	const auto Shade = [](const TAttribute<float>& Alpha, const FLinearColor& Color)
	{
		return TAttribute<FSlateColor>::CreateLambda([Alpha, Color]() { return FSlateColor(FLinearColor(Color.R, Color.G, Color.B, Color.A * FMath::Clamp(Alpha.Get(0.0f), 0.0f, 1.0f))); });
	};
	const TAttribute<float> Title = TitleAlpha;
	const TAttribute<float> Black = BlackAlpha;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::Black)
			.Padding(0.0f)
			[
				SNew(SScaleBox)
				.Stretch(EStretch::ScaleToFit)
				[
					SNew(SImage).Image(&PictureBrush)
				]
			]
		]
		// Black over the picture: until the first frame, and again while it fades out.
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([Black]() { return FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, FMath::Clamp(Black.Get(1.0f), 0.0f, 1.0f))); }))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("HYPECUBE JAM")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 76))
				.ColorAndOpacity(Shade(Title, FLinearColor(0.2f, 1.0f, 0.9f, 1.0f)))
				.ShadowOffset(FVector2D(3.0, 3.0))
				.ShadowColorAndOpacity(TAttribute<FLinearColor>::CreateLambda([Title]() { return FLinearColor(0.0f, 0.0f, 0.0f, 0.85f * FMath::Clamp(Title.Get(0.0f), 0.0f, 1.0f)); }))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("dogfights on the corners of a hypercube")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 20))
				.ColorAndOpacity(Shade(Title, FLinearColor(0.85f, 0.95f, 1.0f, 1.0f)))
				.ShadowOffset(FVector2D(1.0, 1.0))
				.ShadowColorAndOpacity(TAttribute<FLinearColor>::CreateLambda([Title]() { return FLinearColor(0.0f, 0.0f, 0.0f, 0.85f * FMath::Clamp(Title.Get(0.0f), 0.0f, 1.0f)); }))
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0.0f, 0.0f, 24.0f, 16.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("any key to skip")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.8f, 0.85f, 0.6f)))
		]
	];
}

FReply SHCJIntro::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Keyboard keys and gamepad buttons arrive here alike.
	OnSkip.ExecuteIfBound();
	return FReply::Handled();
}

FReply SHCJIntro::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	OnSkip.ExecuteIfBound();
	return FReply::Handled();
}
