#include "Ship/HCJShipTypes.h"

namespace
{
	// name, short, description, first hull, hulls, scale | cruise, accel, braking, boost drain, boost recharge, bot turn |
	// hull, facing, regen, fore scale | phaser dmg, phaser recharge, bolt dmg, torpedoes | cloak s, cloak recharge,
	// warp s, warp cooldown | tractor range, pull, drain
	const FHCJShipTypeDef ShipTypeTable[HCJShipTypes::NumTypes] = {
		{ TEXT("Interceptor"), TEXT("INT"), TEXT("Fastest and most agile. Light hull, phasers that recharge quickly, two torpedoes."),
			0, 8, 0.85f, 3500.0f, 8000.0f, 2500.0f, 3.5f, 6.0f, 1.2f,
			93.0f, 23.0f, 8.0f, 1.0f, 1.05f, 3.4f, 1.0f, 2,
			10.0f, 20.0f, 2.5f, 10.0f, 6000.0f, 1100.0f, 3.0f },
		{ TEXT("Fighter"), TEXT("FTR"), TEXT("The all-rounder: balanced speed, shields and weapons. Four torpedoes."),
			8, 8, 1.0f, 3000.0f, 6000.0f, 1900.0f, 3.5f, 7.0f, 1.0f,
			100.0f, 26.0f, 8.0f, 1.0f, 1.0f, 5.0f, 1.0f, 4,
			12.0f, 20.0f, 2.5f, 12.0f, 6500.0f, 1200.0f, 3.0f },
		{ TEXT("Gunship"), TEXT("GUN"), TEXT("Slow and heavy. Thick hull, fore armour, harder phasers and bolts, six torpedoes."),
			16, 8, 1.12f, 2600.0f, 5000.0f, 1600.0f, 3.0f, 8.0f, 0.85f,
			110.0f, 24.0f, 8.0f, 0.9f, 1.1f, 6.0f, 1.1f, 6,
			10.0f, 22.0f, 2.2f, 14.0f, 6500.0f, 1300.0f, 3.0f },
		{ TEXT("Scout"), TEXT("SCT"), TEXT("Fast, with the longest boost and cloak and the quickest warp drive. Light hull, weaker guns."),
			24, 8, 0.9f, 3300.0f, 7000.0f, 2200.0f, 5.5f, 6.0f, 1.1f,
			97.0f, 22.0f, 8.0f, 1.0f, 0.97f, 4.0f, 0.9f, 3,
			20.0f, 16.0f, 3.0f, 6.0f, 6000.0f, 1000.0f, 3.0f },
		{ TEXT("Cruiser"), TEXT("CRU"), TEXT("The biggest and slowest. Strongest shields all round and the strongest tractor beam."),
			32, 8, 1.3f, 2350.0f, 4200.0f, 1400.0f, 3.0f, 8.0f, 0.75f,
			144.0f, 34.0f, 10.0f, 1.0f, 1.0f, 5.0f, 1.0f, 4,
			8.0f, 22.0f, 2.2f, 16.0f, 8500.0f, 1900.0f, 5.0f },
	};

	float BestOf(float (*Value)(const FHCJShipTypeDef&))
	{
		float Best = 0.0f;
		for (const FHCJShipTypeDef& Def : ShipTypeTable)
		{
			Best = FMath::Max(Best, Value(Def));
		}
		return FMath::Max(Best, UE_KINDA_SMALL_NUMBER);
	}
}

const FHCJShipTypeDef& HCJShipTypes::Get(int32 Type)
{
	return ShipTypeTable[(Type >= 0 && Type < NumTypes) ? Type : static_cast<int32>(EHCJShipType::Fighter)];
}

uint8 HCJShipTypes::SanitizeType(int32 Type)
{
	return static_cast<uint8>((Type >= 0 && Type < NumTypes) ? Type : static_cast<int32>(EHCJShipType::Fighter));
}

uint8 HCJShipTypes::SanitizeVariant(int32 Type, int32 Variant)
{
	return (Variant >= 0 && Variant < Get(Type).NumHulls) ? static_cast<uint8>(Variant) : Auto;
}

uint8 HCJShipTypes::SanitizeSkin(int32 Skin, int32 NumSkins)
{
	return (Skin >= 0 && Skin < NumSkins && Skin < Auto) ? static_cast<uint8>(Skin) : Auto;
}

int32 HCJShipTypes::PickHullMesh(int32 Type, uint8 Variant, int32 PlayerId, int32 NumMeshes)
{
	if (NumMeshes <= 0)
	{
		return INDEX_NONE;
	}
	const FHCJShipTypeDef& Def = Get(Type);
	int32 Local = Variant;
	if (Variant == Auto || Local >= Def.NumHulls)
	{
		// The hash the game used for the whole list before there were types.
		const uint32 Hash = static_cast<uint32>(PlayerId) * 2654435761u;
		Local = static_cast<int32>((Hash >> 16) % static_cast<uint32>(FMath::Max(Def.NumHulls, 1)));
	}
	// A shorter mesh list than the table was written for still gives every type something.
	return (Def.FirstHull + Local) % NumMeshes;
}

uint8 HCJShipTypes::PickBotType(int32 BotIndex)
{
	static const EHCJShipType Order[NumTypes] = { EHCJShipType::Fighter, EHCJShipType::Interceptor, EHCJShipType::Cruiser, EHCJShipType::Scout, EHCJShipType::Gunship };
	return static_cast<uint8>(Order[((BotIndex % NumTypes) + NumTypes) % NumTypes]);
}

float HCJShipTypes::GetStat(int32 Type, EStat Stat)
{
	const FHCJShipTypeDef& Def = Get(Type);
	switch (Stat)
	{
	case EStat::Speed:
		return Def.CruiseSpeed / BestOf([](const FHCJShipTypeDef& D) { return D.CruiseSpeed; });
	case EStat::Agility:
		return Def.Acceleration / BestOf([](const FHCJShipTypeDef& D) { return D.Acceleration; });
	case EStat::Hull:
		return Def.MaxHull / BestOf([](const FHCJShipTypeDef& D) { return D.MaxHull; });
	case EStat::Shields:
		return Def.ShieldFacing / BestOf([](const FHCJShipTypeDef& D) { return D.ShieldFacing; });
	case EStat::Firepower:
	{
		const auto Power = [](const FHCJShipTypeDef& D) { return D.PhaserDamageScale * (1.0f + 1.5f / D.PhaserRechargeSeconds) + 0.12f * D.Torpedoes; };
		return Power(Def) / BestOf(Power);
	}
	case EStat::Systems:
	{
		const auto Systems = [](const FHCJShipTypeDef& D) { return D.CloakSeconds / 20.0f + 6.0f / D.WarpCooldown + D.TractorPullSpeed / 1900.0f; };
		return Systems(Def) / BestOf(Systems);
	}
	default:
		return 0.0f;
	}
}

const TCHAR* HCJShipTypes::GetStatName(EStat Stat)
{
	switch (Stat)
	{
	case EStat::Speed: return TEXT("SPEED");
	case EStat::Agility: return TEXT("AGILITY");
	case EStat::Hull: return TEXT("HULL");
	case EStat::Shields: return TEXT("SHIELDS");
	case EStat::Firepower: return TEXT("FIREPOWER");
	case EStat::Systems: return TEXT("CLOAK / WARP / TRACTOR");
	default: return TEXT("");
	}
}

bool HCJShipTypes::IsTableValid(FString* OutWhy)
{
	const auto Fail = [OutWhy](const FString& Why)
	{
		if (OutWhy != nullptr)
		{
			*OutWhy = Why;
		}
		return false;
	};
	uint64 Used = 0;
	for (int32 Type = 0; Type < NumTypes; ++Type)
	{
		const FHCJShipTypeDef& Def = ShipTypeTable[Type];
		if (Def.Name == nullptr || Def.ShortName == nullptr || Def.Description == nullptr || FCString::Strlen(Def.ShortName) != 3)
		{
			return Fail(FString::Printf(TEXT("type %d has no name / a short name that is not three letters"), Type));
		}
		if (Def.NumHulls <= 0 || Def.FirstHull < 0 || Def.FirstHull + Def.NumHulls > NumHullMeshes)
		{
			return Fail(FString::Printf(TEXT("%s: hull range outside the mesh list"), Def.Name));
		}
		for (int32 Hull = Def.FirstHull; Hull < Def.FirstHull + Def.NumHulls; ++Hull)
		{
			if ((Used & (uint64(1) << Hull)) != 0)
			{
				return Fail(FString::Printf(TEXT("%s: hull %d belongs to two types"), Def.Name, Hull));
			}
			Used |= uint64(1) << Hull;
		}
		const float Positive[] = { Def.Scale, Def.CruiseSpeed, Def.Acceleration, Def.Braking, Def.BoostDrainSeconds, Def.BoostRechargeSeconds, Def.BotTurnScale,
			Def.MaxHull, Def.ShieldFacing, Def.ShieldRegenRate, Def.ForeDamageScale, Def.PhaserDamageScale, Def.PhaserRechargeSeconds, Def.BoltDamageScale,
			Def.CloakSeconds, Def.CloakRechargeSeconds, Def.WarpSeconds, Def.WarpCooldown, Def.TractorRange, Def.TractorPullSpeed, Def.TractorDrainSeconds };
		for (const float Value : Positive)
		{
			if (!(Value > 0.0f) || !FMath::IsFinite(Value))
			{
				return Fail(FString::Printf(TEXT("%s: a number is not positive"), Def.Name));
			}
		}
		if (Def.Scale < 0.6f || Def.Scale > 1.6f || Def.Torpedoes < 0 || Def.Torpedoes > 12 || Def.ForeDamageScale > 1.0f)
		{
			return Fail(FString::Printf(TEXT("%s: scale, torpedoes or fore armour out of range"), Def.Name));
		}
	}
	return true;
}
