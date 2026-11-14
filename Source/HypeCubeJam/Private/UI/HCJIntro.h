#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

class UTexture;

/**
 * The intro movie on the whole viewport: a black background, the movie picture (a brush over the media texture, scaled to
 * fit), the title fading in over it near the end, "any key to skip" at the bottom. Any key, mouse button or gamepad
 * button calls OnSkip. The widget owns nothing of the media: AHCJPlayerController keeps the player and the texture alive
 * and feeds the two alphas.
 */
class SHCJIntro : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHCJIntro) {}
		SLATE_ARGUMENT(UTexture*, Texture)
		SLATE_ARGUMENT(FVector2D, PictureSize)
		SLATE_EVENT(FSimpleDelegate, OnSkip)
		/** 0..1, how visible the title is / how black the picture is. */
		SLATE_ATTRIBUTE(float, TitleAlpha)
		SLATE_ATTRIBUTE(float, BlackAlpha)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FSlateBrush PictureBrush;
	FSimpleDelegate OnSkip;
	TAttribute<float> TitleAlpha;
	TAttribute<float> BlackAlpha;
};
