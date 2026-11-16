#include "Ship/HCJWeapons.h"

bool FHCJWeaponDef::IsSane() const
{
	return FireInterval >= 0.02f && Damage >= 0.0f && ProjectileSpeed >= 100.0f && Lifetime >= 0.1f
		&& SpreadDegrees >= 0.0f && SpreadDegrees <= 30.0f && Pellets >= 1 && Pellets <= 16
		&& HomingTurnRate >= 0.0f && HomingConeDegrees >= 0.0f && HomingConeDegrees <= 90.0f && Ammo >= 0
		&& VisualScale > 0.0f && !Name.IsEmpty() && (!bBeam || BeamRange > 0.0f);
}

FHCJWeaponDef FHCJWeaponDef::MakeDefault(EHCJWeapon Weapon)
{
	FHCJWeaponDef Def;
	switch (Weapon)
	{
	case EHCJWeapon::Scatter:
		Def.Name = TEXT("SCATTER CANNON");
		Def.FireInterval = 0.65f;
		Def.Damage = 9.0f;
		Def.ProjectileSpeed = 16000.0f;
		Def.Lifetime = 0.6f;
		Def.SpreadDegrees = 4.5f;
		Def.Pellets = 8;
		Def.Ammo = 14;
		Def.Color = FLinearColor(1.0f, 0.55f, 0.1f);
		Def.VisualScale = 0.6f;
		Def.BotMaxRange = 5000.0f;
		break;
	case EHCJWeapon::Rail:
		Def.Name = TEXT("RAIL SHOT");
		Def.FireInterval = 0.9f;
		Def.Damage = 70.0f;
		Def.ProjectileSpeed = 60000.0f;
		Def.Lifetime = 0.8f;
		Def.Ammo = 8;
		Def.Color = FLinearColor(0.3f, 0.8f, 1.0f);
		Def.VisualScale = 2.2f;
		Def.BotMaxRange = 16000.0f;
		break;
	case EHCJWeapon::Missile:
		Def.Name = TEXT("HOMING MISSILES");
		Def.FireInterval = 0.8f;
		Def.Damage = 55.0f;
		Def.ProjectileSpeed = 9000.0f;
		Def.Lifetime = 3.5f;
		Def.HomingTurnRate = 110.0f;
		Def.HomingConeDegrees = 40.0f;
		Def.Ammo = 8;
		Def.Color = FLinearColor(1.0f, 0.2f, 0.15f);
		Def.VisualScale = 1.0f;
		Def.BotMaxRange = 12000.0f;
		break;
	case EHCJWeapon::Pulse:
		Def.Name = TEXT("RAPID PULSE");
		Def.FireInterval = 0.07f;
		Def.Damage = 9.0f;
		Def.ProjectileSpeed = 22000.0f;
		Def.Lifetime = 1.5f;
		Def.SpreadDegrees = 1.0f;
		Def.Ammo = 120;
		Def.Color = FLinearColor(0.7f, 0.3f, 1.0f);
		Def.VisualScale = 0.7f;
		Def.BotMaxRange = 10000.0f;
		break;
	case EHCJWeapon::Torpedo:
		// Reload time, stock of a fresh ship, damage through a facing that is down; see Ship/HCJTorpedo.h.
		Def.Name = TEXT("TORPEDO");
		Def.FireInterval = 2.0f;
		Def.Damage = 70.0f;
		Def.ProjectileSpeed = 9000.0f;
		Def.Lifetime = 2.0f;
		Def.HomingTurnRate = 40.0f;
		Def.HomingConeDegrees = 60.0f;
		Def.Ammo = 4;
		Def.Color = FLinearColor(1.0f, 0.3f, 0.06f);
		Def.VisualScale = 1.0f;
		Def.BotMaxRange = 9000.0f;
		break;
	case EHCJWeapon::Phaser:
		// Damage per second at close range, applied every FireInterval; see Ship/HCJPhaser.h.
		Def.Name = TEXT("PHASER");
		Def.FireInterval = 0.1f;
		Def.Damage = 58.0f;
		Def.ProjectileSpeed = 1000000.0f;
		Def.Lifetime = 0.1f;
		Def.bBeam = true;
		Def.BeamRange = 9000.0f;
		Def.Color = FLinearColor(1.0f, 0.3f, 0.05f);
		Def.BotMaxRange = 8500.0f;
		break;
	case EHCJWeapon::Blaster:
	default:
		break;
	}
	return Def;
}

void FHCJWeaponState::Give(EHCJWeapon NewWeapon, const FHCJWeaponDef& Def)
{
	if (HCJ::IsBaseWeapon(NewWeapon) || !HCJ::IsPrimaryWeapon(NewWeapon) || Def.Ammo <= 0)
	{
		Drop();
		return;
	}
	if (NewWeapon == Weapon)
	{
		Ammo = FMath::Min(Ammo + Def.Ammo, 2 * Def.Ammo);
	}
	else
	{
		Weapon = NewWeapon;
		Ammo = Def.Ammo;
	}
}

bool FHCJWeaponState::ConsumeShot(const FHCJWeaponDef& Def)
{
	if (HCJ::IsBaseWeapon(Weapon) || Def.Ammo <= 0)
	{
		return false;
	}
	if (--Ammo <= 0)
	{
		Drop();
		return true;
	}
	return false;
}

void FHCJWeaponState::Drop()
{
	Weapon = HCJ::IsBaseWeapon(BaseWeapon) ? BaseWeapon : EHCJWeapon::Blaster;
	Ammo = 0;
}

EHCJWeapon FHCJWeaponState::ToggleBase(bool bPhaserAllowed)
{
	const bool bHeldBase = HCJ::IsBaseWeapon(Weapon);
	BaseWeapon = (BaseWeapon == EHCJWeapon::Phaser || !bPhaserAllowed) ? EHCJWeapon::Blaster : EHCJWeapon::Phaser;
	if (bHeldBase)
	{
		Weapon = BaseWeapon;
		Ammo = 0;
	}
	return BaseWeapon;
}

const TCHAR* HCJ::GetItemName(EHCJPickupItem Item)
{
	switch (Item)
	{
	case EHCJPickupItem::Repair: return TEXT("REPAIR");
	case EHCJPickupItem::ShieldCell: return TEXT("SHIELD CELL");
	case EHCJPickupItem::Overdrive: return TEXT("OVERDRIVE");
	case EHCJPickupItem::QuadDamage: return TEXT("QUAD DAMAGE");
	case EHCJPickupItem::Cloak: return TEXT("CLOAK");
	case EHCJPickupItem::Scatter: return TEXT("SCATTER CANNON");
	case EHCJPickupItem::Rail: return TEXT("RAIL SHOT");
	case EHCJPickupItem::Missile: return TEXT("HOMING MISSILES");
	case EHCJPickupItem::Pulse: return TEXT("RAPID PULSE");
	case EHCJPickupItem::Torpedoes: return TEXT("TORPEDOES");
	default: return TEXT("?");
	}
}

bool HCJ::IsBaseWeapon(EHCJWeapon Weapon)
{
	return Weapon == EHCJWeapon::Blaster || Weapon == EHCJWeapon::Phaser;
}

bool HCJ::IsPrimaryWeapon(EHCJWeapon Weapon)
{
	return Weapon < EHCJWeapon::Count && Weapon != EHCJWeapon::Torpedo;
}

EHCJWeapon HCJ::ItemToWeapon(EHCJPickupItem Item)
{
	switch (Item)
	{
	case EHCJPickupItem::Scatter: return EHCJWeapon::Scatter;
	case EHCJPickupItem::Rail: return EHCJWeapon::Rail;
	case EHCJPickupItem::Missile: return EHCJWeapon::Missile;
	case EHCJPickupItem::Pulse: return EHCJWeapon::Pulse;
	default: return EHCJWeapon::Count;
	}
}

FLinearColor HCJ::GetItemColor(EHCJPickupItem Item)
{
	switch (Item)
	{
	case EHCJPickupItem::Repair: return FLinearColor(0.2f, 1.0f, 0.3f);
	case EHCJPickupItem::ShieldCell: return FLinearColor(0.2f, 0.6f, 1.0f);
	case EHCJPickupItem::Overdrive: return FLinearColor(1.0f, 0.9f, 0.2f);
	case EHCJPickupItem::QuadDamage: return FLinearColor(1.0f, 0.1f, 0.9f);
	case EHCJPickupItem::Cloak: return FLinearColor(0.75f, 0.85f, 0.9f);
	case EHCJPickupItem::Scatter: return FLinearColor(1.0f, 0.55f, 0.1f);
	case EHCJPickupItem::Rail: return FLinearColor(0.3f, 0.8f, 1.0f);
	case EHCJPickupItem::Missile: return FLinearColor(1.0f, 0.2f, 0.15f);
	case EHCJPickupItem::Pulse: return FLinearColor(0.7f, 0.3f, 1.0f);
	case EHCJPickupItem::Torpedoes: return FLinearColor(1.0f, 0.3f, 0.06f);
	default: return FLinearColor::White;
	}
}

EHCJItemKind HCJ::GetItemKind(EHCJPickupItem Item)
{
	switch (Item)
	{
	case EHCJPickupItem::Repair:
	case EHCJPickupItem::ShieldCell:
		return EHCJItemKind::Health;
	case EHCJPickupItem::Overdrive:
	case EHCJPickupItem::QuadDamage:
	case EHCJPickupItem::Cloak:
		return EHCJItemKind::PowerUp;
	default:
		return EHCJItemKind::Weapon;
	}
}

FLinearColor HCJ::GetItemKindColor(EHCJItemKind Kind)
{
	switch (Kind)
	{
	case EHCJItemKind::Health: return FLinearColor(0.15f, 1.0f, 0.35f);
	case EHCJItemKind::PowerUp: return FLinearColor(1.0f, 0.25f, 0.9f);
	default: return FLinearColor(1.0f, 0.62f, 0.1f);
	}
}

const TCHAR* HCJ::GetItemShortName(EHCJPickupItem Item)
{
	switch (Item)
	{
	case EHCJPickupItem::Repair: return TEXT("REPAIR");
	case EHCJPickupItem::ShieldCell: return TEXT("SHIELD");
	case EHCJPickupItem::Overdrive: return TEXT("OVERDRIVE");
	case EHCJPickupItem::QuadDamage: return TEXT("QUAD");
	case EHCJPickupItem::Cloak: return TEXT("CLOAK");
	case EHCJPickupItem::Scatter: return TEXT("SCATTER");
	case EHCJPickupItem::Rail: return TEXT("RAIL");
	case EHCJPickupItem::Missile: return TEXT("MISSILES");
	case EHCJPickupItem::Pulse: return TEXT("PULSE");
	case EHCJPickupItem::Torpedoes: return TEXT("TORPEDOES");
	default: return TEXT("?");
	}
}
