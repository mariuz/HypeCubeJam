#pragma once

#include "CoreMinimal.h"

/**
 * Ship types: the one table of what differs between hulls. Plain data and functions without a world, so they can be
 * tested on their own. The numbers are code, not config: client prediction needs exactly the same movement values on
 * the owning client and on the server, and a table compiled into both cannot drift apart.
 *
 * WHO USES IT: the server validates what a player asks for (Sanitize*), stores it in AHCJPlayerState and stamps it on
 * every ship it spawns (AHCJShipCharacter::ShipType / ShipVariant / ShipSkin, replicated to everybody). Every machine
 * then applies the row (AHCJShipCharacter::ApplyShipType): movement, hull, shields, collision size, look.
 */
enum class EHCJShipType : uint8
{
	Interceptor,
	Fighter,
	Gunship,
	Scout,
	Cruiser,

	Count
};

struct HYPECUBEJAM_API FHCJShipTypeDef
{
	const TCHAR* Name;
	/** Three letters for the scoreboard, the kill feed and the target panel. */
	const TCHAR* ShortName;
	const TCHAR* Description;

	/** The hulls of this type: NumHulls consecutive entries of UHCJAssetSettings::ShipMeshes from FirstHull. */
	int32 FirstHull;
	int32 NumHulls;
	/** Size against the fighter: hull mesh, movement collision, hit sphere and shield bubble alike, on every machine. */
	float Scale;

	// ---- flight (client prediction: identical on client and server) ----
	float CruiseSpeed;
	float Acceleration;
	float Braking;
	float BoostDrainSeconds;
	float BoostRechargeSeconds;
	/** Bots turn their aim this much faster / slower than their skill row says. */
	float BotTurnScale;

	// ---- hull and shields ----
	float MaxHull;
	/** Capacity of one of the four shield facings, and points per second it regenerates. */
	float ShieldFacing;
	float ShieldRegenRate;
	/** What a hit on the FORE facing is multiplied with (below 1: fore armour). */
	float ForeDamageScale;

	// ---- weapons ----
	float PhaserDamageScale;
	/** Seconds an empty phaser bank needs to fill. */
	float PhaserRechargeSeconds;
	/** Damage of bolts and pickup weapons (not torpedoes). */
	float BoltDamageScale;
	/** Torpedoes on a fresh ship. */
	int32 Torpedoes;

	// ---- systems ----
	/** Seconds a full cloak meter lasts, and seconds an empty one needs to fill. */
	float CloakSeconds;
	float CloakRechargeSeconds;
	/** Seconds at warp in a full charge, and seconds before the drive can be used again. */
	float WarpSeconds;
	float WarpCooldown;
	/** Tractor beam: reach (uu), pull on the held ship (uu/s) and how long a full phaser bank feeds it (s). */
	float TractorRange;
	float TractorPullSpeed;
	float TractorDrainSeconds;
};

namespace HCJShipTypes
{
	inline constexpr int32 NumTypes = static_cast<int32>(EHCJShipType::Count);
	/** "Pick for me": the hull follows from the player id, the paint from the player colour (what bots use). */
	inline constexpr uint8 Auto = 255;
	/** Hull meshes the table was written for (Star Sparrow: 40). */
	inline constexpr int32 NumHullMeshes = 40;

	/** The row of a type; anything out of range is the fighter. */
	HYPECUBEJAM_API const FHCJShipTypeDef& Get(int32 Type);
	inline const FHCJShipTypeDef& Get(EHCJShipType Type) { return Get(static_cast<int32>(Type)); }

	/** Server-side validation of a client's choice: a valid type index, else the fighter. */
	HYPECUBEJAM_API uint8 SanitizeType(int32 Type);
	/** A valid hull of that type (0 .. NumHulls - 1), else Auto. */
	HYPECUBEJAM_API uint8 SanitizeVariant(int32 Type, int32 Variant);
	/** A valid paint (0 .. NumSkins - 1), else Auto. */
	HYPECUBEJAM_API uint8 SanitizeSkin(int32 Skin, int32 NumSkins);

	/**
	 * Index into the hull mesh list (NumMeshes entries) for a ship: the chosen hull of its type, or with Auto a hash
	 * of the player id inside the type's hulls. Same on every machine. INDEX_NONE for an empty list.
	 */
	HYPECUBEJAM_API int32 PickHullMesh(int32 Type, uint8 Variant, int32 PlayerId, int32 NumMeshes);

	/** The type of the Index-th bot of a match: every type in turn, in an order that mixes light and heavy hulls. */
	HYPECUBEJAM_API uint8 PickBotType(int32 BotIndex);

	/** Stat bars of the hangar, 0..1 against the best of all types: speed, agility, hull, shields, firepower, systems. */
	enum class EStat : uint8 { Speed, Agility, Hull, Shields, Firepower, Systems, Count };
	HYPECUBEJAM_API float GetStat(int32 Type, EStat Stat);
	HYPECUBEJAM_API const TCHAR* GetStatName(EStat Stat);

	/** Every row is usable: positive numbers, hull ranges inside the mesh list and not overlapping. For the tests. */
	HYPECUBEJAM_API bool IsTableValid(FString* OutWhy = nullptr);
}
