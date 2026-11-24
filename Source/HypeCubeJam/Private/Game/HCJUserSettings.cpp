#include "Game/HCJUserSettings.h"

#include "Game/HCJBalance.h"
#include "Game/HCJPadLogic.h"
#include "Ship/HCJShipTypes.h"

int32 UHCJUserSettings::GetBotDifficulty() const
{
	return (bSuperEasy && BotDifficulty == 0) ? HCJBalance::SuperEasyDifficulty : FMath::Clamp(BotDifficulty, 0, 3);
}

void UHCJUserSettings::SetBotDifficulty(int32 Difficulty)
{
	const int32 Level = HCJBalance::NormalizeDifficulty(Difficulty);
	bSuperEasy = Level == HCJBalance::SuperEasyDifficulty;
	BotDifficulty = bSuperEasy ? 0 : Level;
}

void UHCJUserSettings::Sanitize()
{
	MouseSensitivity = FMath::Clamp(MouseSensitivity, 0.2f, 3.0f);
	ControllerSensitivity = FMath::Clamp(ControllerSensitivity, HCJPad::MinSensitivity, HCJPad::MaxSensitivity);
	MusicVolume = FMath::Clamp(MusicVolume, 0.0f, 1.0f);
	SfxVolume = FMath::Clamp(SfxVolume, 0.0f, 1.0f);
	UiVolume = FMath::Clamp(UiVolume, 0.0f, 1.0f);
	BotCount = FMath::Clamp(BotCount, 0, 15);
	Dimension = FMath::Clamp(Dimension, 3, 5);
	BotDifficulty = FMath::Clamp(BotDifficulty, 0, 3);
	bSuperEasy = bSuperEasy && BotDifficulty == 0;
	MusicSet = FMath::Clamp(MusicSet, 0, 1);
	ShipType = HCJShipTypes::SanitizeType(ShipType);
	ShipVariant = (ShipVariant >= 0 && ShipVariant < HCJShipTypes::Get(ShipType).NumHulls) ? ShipVariant : -1;
	ShipSkin = (ShipSkin >= 0 && ShipSkin < 64) ? ShipSkin : -1;
	PlayerName = PlayerName.TrimStartAndEnd().Left(20);
	if (PlayerName.IsEmpty())
	{
		// Something friendlier than the machine name the engine would use: the name of the signed-in user.
		for (const TCHAR Char : FString(FPlatformProcess::UserName(false)).Left(16))
		{
			PlayerName.AppendChar((FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('-')) ? Char : TEXT('_'));
		}
	}
	LastJoinAddress = LastJoinAddress.TrimStartAndEnd().Left(64);
}

void UHCJUserSettings::Save()
{
	Sanitize();
	SaveConfig();
}
