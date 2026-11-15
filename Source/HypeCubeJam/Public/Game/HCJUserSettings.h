#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HCJUserSettings.generated.h"

/**
 * What the player chose in the menu, kept between runs in the user's GameUserSettings.ini (section
 * [/Script/HypeCubeJam.HCJUserSettings]). Local to this machine; the host's match options reach the game mode
 * through AHCJGameMode::StartMatchFromMenu, never through replication of this object.
 */
UCLASS(Config = GameUserSettings)
class HYPECUBEJAM_API UHCJUserSettings : public UObject
{
	GENERATED_BODY()

public:
	static UHCJUserSettings& Get() { return *GetMutableDefault<UHCJUserSettings>(); }

	/** Clamps every value into its range (a hand-edited file must not break the menu). */
	void Sanitize();
	/** Sanitize, then write the file. */
	void Save();

	/** Name shown to others; empty = the name the engine gives. */
	UPROPERTY(Config)
	FString PlayerName;

	/** Multiplier of the look speed, 0.2 .. 3. */
	UPROPERTY(Config)
	float MouseSensitivity = 1.0f;

	/** 0 .. 1, multiplied onto the configured group volumes. */
	UPROPERTY(Config)
	float MusicVolume = 1.0f;

	UPROPERTY(Config)
	float SfxVolume = 1.0f;

	UPROPERTY(Config)
	float UiVolume = 1.0f;

	/** Host options: bots that fill the match (0 .. 15), hypercube dimension (3 .. 5), seed (0 = random), bot difficulty (0 easy, 1 normal, 2 hard). */
	UPROPERTY(Config)
	int32 BotCount = 7;

	UPROPERTY(Config)
	int32 Dimension = 4;

	UPROPERTY(Config)
	int32 Seed = 0;

	UPROPERTY(Config)
	int32 BotDifficulty = 0;

	UPROPERTY(Config)
	FString LastJoinAddress = TEXT("127.0.0.1");

	/** In-game hints (onboarding prompts, contextual tips, tip after a death). H in a match or the settings page. */
	UPROPERTY(Config)
	bool bHints = true;

	/** The camera shakes with explosions nearby (the short jolt of a hit on the own hull stays). Settings page. */
	UPROPERTY(Config)
	bool bCameraShake = true;

	/** Music of a match: 0 = the three Lyria tracks (menu, exploring, combat), 1 = the older Lyra stems layered by the fight. Settings page. */
	UPROPERTY(Config)
	int32 MusicSet = 0;

	/** Enemy highlight: the rim glow on other ships and the large glint that marks them from afar. Settings page. */
	UPROPERTY(Config)
	bool bEnemyHighlight = true;

	/**
	 * Cinematic look: bloom, vignette, contrast and the teal-and-orange grade (AHCJSpaceBackdrop::ApplyCinematicLook), and
	 * the full number of decorative rocks. Off = the picture as the engine renders it by default and about a third of the
	 * decoration. Settings page.
	 */
	UPROPERTY(Config)
	bool bCinematicLook = true;

	/** Hangar: ship type (EHCJShipType), hull within the type and paint; -1 = let the game pick hull / paint. */
	UPROPERTY(Config)
	int32 ShipType = 1;
	UPROPERTY(Config)
	int32 ShipVariant = 0;
	UPROPERTY(Config)
	int32 ShipSkin = -1;

	/** The controls overlay was shown once on this machine. */
	UPROPERTY(Config)
	bool bHelpSeen = false;
};
