#include "Game/HCJGameplaySettings.h"

#include "Ship/HCJTorpedo.h"

namespace
{
	/** Built-in weapon table and item weights, used wherever the config says nothing usable. */
	struct FHCJGameplayDefaults
	{
		FHCJWeaponDef Weapons[static_cast<int32>(EHCJWeapon::Count)];
		float ItemWeights[static_cast<int32>(EHCJPickupItem::Count)] = {
			/*Repair*/ 3.0f, /*ShieldCell*/ 2.5f, /*Overdrive*/ 1.5f, /*QuadDamage*/ 1.0f, /*Cloak*/ 1.0f,
			/*Scatter*/ 2.0f, /*Rail*/ 1.5f, /*Missile*/ 1.5f, /*Pulse*/ 2.0f, /*Torpedoes*/ 1.5f };

		FHCJGameplayDefaults()
		{
			for (int32 Index = 0; Index < static_cast<int32>(EHCJWeapon::Count); ++Index)
			{
				Weapons[Index] = FHCJWeaponDef::MakeDefault(static_cast<EHCJWeapon>(Index));
			}
		}
	};

	const FHCJGameplayDefaults& GetDefaults()
	{
		static const FHCJGameplayDefaults Defaults;
		return Defaults;
	}
}

const FHCJWeaponDef& UHCJGameplaySettings::GetWeapon(EHCJWeapon Weapon) const
{
	const int32 Index = Weapon < EHCJWeapon::Count ? static_cast<int32>(Weapon) : 0;
	if (Weapons.IsValidIndex(Index) && Weapons[Index].IsSane()
		// Blaster and phaser never run dry, the others always do, and only the phaser is a beam: a config that breaks this falls back.
		&& (Weapons[Index].Ammo == 0) == HCJ::IsBaseWeapon(static_cast<EHCJWeapon>(Index))
		&& Weapons[Index].bBeam == (Index == static_cast<int32>(EHCJWeapon::Phaser)))
	{
		return Weapons[Index];
	}
	return GetDefaults().Weapons[Index];
}

float UHCJGameplaySettings::GetItemWeight(EHCJPickupItem Item) const
{
	if (Item >= EHCJPickupItem::Count || (Item == EHCJPickupItem::Cloak && !bCloak) || (Item == EHCJPickupItem::Torpedoes && !HCJTorpedo::IsEnabled()))
	{
		return 0.0f;
	}
	const int32 Index = static_cast<int32>(Item);
	return FMath::Max(ItemWeights.IsValidIndex(Index) ? ItemWeights[Index] : GetDefaults().ItemWeights[Index], 0.0f);
}

EHCJPickupItem UHCJGameplaySettings::PickItem(float Roll) const
{
	float Total = 0.0f;
	for (int32 Index = 0; Index < static_cast<int32>(EHCJPickupItem::Count); ++Index)
	{
		Total += GetItemWeight(static_cast<EHCJPickupItem>(Index));
	}
	if (Total <= 0.0f)
	{
		return EHCJPickupItem::Repair;
	}

	float Remaining = FMath::Clamp(Roll, 0.0f, 0.999999f) * Total;
	EHCJPickupItem Last = EHCJPickupItem::Repair;
	for (int32 Index = 0; Index < static_cast<int32>(EHCJPickupItem::Count); ++Index)
	{
		const float Weight = GetItemWeight(static_cast<EHCJPickupItem>(Index));
		if (Weight <= 0.0f)
		{
			continue;
		}
		Last = static_cast<EHCJPickupItem>(Index);
		if (Remaining < Weight)
		{
			return Last;
		}
		Remaining -= Weight;
	}
	return Last;
}
