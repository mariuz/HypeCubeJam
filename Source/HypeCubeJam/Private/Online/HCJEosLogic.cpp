#include "Online/HCJEosLogic.h"

namespace
{
	const TCHAR* KeyName = TEXT("HCJNAME");
	const TCHAR* KeyPublic = TEXT("HCJPUB");
	const TCHAR* KeyLan = TEXT("HCJLAN");
	const TCHAR* KeyPort = TEXT("HCJPORT");
	const TCHAR* KeyPlayers = TEXT("HCJPLAYERS");
	const TCHAR* KeyMax = TEXT("HCJMAX");
	const TCHAR* KeyBots = TEXT("HCJBOTS");
	const TCHAR* KeyDim = TEXT("HCJDIM");
	const TCHAR* KeySeed = TEXT("HCJSEED");
	const TCHAR* KeyVersion = TEXT("HCJVER");
	/** Stands for "no value": the service does not take empty strings. */
	const TCHAR* NoValue = TEXT("-");

	bool LooksLikeValue(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Char : Value)
		{
			if (FChar::IsWhitespace(Char) || Char == TEXT('<') || Char == TEXT('>'))
			{
				return false;
			}
		}
		return true;
	}

	const FHCJLobbyAttribute* FindAttribute(const TArray<FHCJLobbyAttribute>& Attributes, const TCHAR* Key)
	{
		return Attributes.FindByPredicate([Key](const FHCJLobbyAttribute& A) { return A.Key.Equals(Key, ESearchCase::IgnoreCase); });
	}

	bool ReadInt(const TArray<FHCJLobbyAttribute>& Attributes, const TCHAR* Key, int64 Min, int64 Max, int32& Out)
	{
		const FHCJLobbyAttribute* Attribute = FindAttribute(Attributes, Key);
		if (Attribute == nullptr || !Attribute->bIsInt || Attribute->IntValue < Min || Attribute->IntValue > Max)
		{
			return false;
		}
		Out = static_cast<int32>(Attribute->IntValue);
		return true;
	}

	bool ReadString(const TArray<FHCJLobbyAttribute>& Attributes, const TCHAR* Key, FString& Out)
	{
		const FHCJLobbyAttribute* Attribute = FindAttribute(Attributes, Key);
		if (Attribute == nullptr || Attribute->bIsInt || Attribute->StringValue.IsEmpty())
		{
			return false;
		}
		Out = Attribute->StringValue == NoValue ? FString() : Attribute->StringValue;
		return true;
	}

	/** "a.b.c" of "a.b.c.d". */
	FString Prefix24(const FString& Address)
	{
		int32 LastDot = INDEX_NONE;
		return Address.FindLastChar(TEXT('.'), LastDot) ? Address.Left(LastDot) : FString();
	}
}

FString FHCJEosConfig::GetProblem() const
{
	const TPair<const TCHAR*, const FString*> Keys[] = {
		{ TEXT("ProductId"), &ProductId }, { TEXT("SandboxId"), &SandboxId }, { TEXT("DeploymentId"), &DeploymentId },
		{ TEXT("ClientId"), &ClientId }, { TEXT("ClientSecret"), &ClientSecret } };
	for (const TPair<const TCHAR*, const FString*>& Key : Keys)
	{
		if (Key.Value->IsEmpty())
		{
			return FString::Printf(TEXT("%s is empty"), Key.Key);
		}
		if (!LooksLikeValue(*Key.Value))
		{
			return FString::Printf(TEXT("%s is not a valid value"), Key.Key);
		}
	}
	return FString();
}

FString HCJEos::DescribeState(EHCJEosState State, const FString& Detail)
{
	FString Text;
	switch (State)
	{
	case EHCJEosState::Disabled: Text = TEXT("Online services unavailable: switched off for this run"); break;
	case EHCJEosState::NotConfigured: Text = TEXT("Online services unavailable: not configured"); break;
	case EHCJEosState::SdkUnavailable: Text = TEXT("Online services unavailable: Epic Online Services could not start"); break;
	case EHCJEosState::Connecting: Text = TEXT("Connecting to Epic Online Services..."); break;
	case EHCJEosState::Ready: Text = TEXT("Online: Epic Online Services"); break;
	default: Text = TEXT("Online services unavailable: sign-in failed, trying again"); break;
	}
	if (!Detail.IsEmpty())
	{
		Text += FString::Printf(TEXT(" (%s)"), *Detail);
	}
	return Text;
}

bool HCJEos::IsUnavailable(EHCJEosState State)
{
	return State != EHCJEosState::Ready && State != EHCJEosState::Connecting;
}

FString HCJEos::SanitizeDisplayName(const FString& Name)
{
	FString Result;
	for (const TCHAR Char : Name.TrimStartAndEnd())
	{
		if (Char >= 32 && Char < 127 && Result.Len() < 32)
		{
			Result.AppendChar(Char);
		}
	}
	Result.TrimStartAndEndInline();
	return Result.IsEmpty() ? FString(TEXT("Pilot")) : Result;
}

void FHCJLobbyInfo::Sanitize()
{
	HostName = HCJEos::SanitizeDisplayName(HostName);
	PublicAddress.TrimStartAndEndInline();
	if (!HCJEos::IsSafeHost(PublicAddress))
	{
		PublicAddress.Reset();
	}
	LanAddresses.RemoveAll([](const FString& Address) { return !HCJEos::IsIPv4(Address); });
	if (LanAddresses.Num() > MaxLanAddresses)
	{
		LanAddresses.SetNum(MaxLanAddresses);
	}
	Port = FMath::Clamp(Port, 1, 65535);
	MaxPlayers = FMath::Clamp(MaxPlayers, 1, 64);
	Players = FMath::Clamp(Players, 0, MaxPlayers);
	Bots = FMath::Clamp(Bots, 0, 64);
	Dimension = FMath::Clamp(Dimension, 2, 6);
	Version = Version.TrimStartAndEnd().Left(32);
}

TArray<FHCJLobbyAttribute> FHCJLobbyInfo::ToAttributes() const
{
	TArray<FHCJLobbyAttribute> Result;
	Result.Add(FHCJLobbyAttribute::MakeString(KeyName, HostName));
	Result.Add(FHCJLobbyAttribute::MakeString(KeyPublic, PublicAddress.IsEmpty() ? FString(NoValue) : PublicAddress));
	Result.Add(FHCJLobbyAttribute::MakeString(KeyLan, LanAddresses.Num() > 0 ? FString::Join(LanAddresses, TEXT(",")) : FString(NoValue)));
	Result.Add(FHCJLobbyAttribute::MakeInt(KeyPort, Port));
	Result.Add(FHCJLobbyAttribute::MakeInt(KeyPlayers, Players));
	Result.Add(FHCJLobbyAttribute::MakeInt(KeyMax, MaxPlayers));
	Result.Add(FHCJLobbyAttribute::MakeInt(KeyBots, Bots));
	Result.Add(FHCJLobbyAttribute::MakeInt(KeyDim, Dimension));
	Result.Add(FHCJLobbyAttribute::MakeInt(KeySeed, Seed));
	Result.Add(FHCJLobbyAttribute::MakeString(KeyVersion, Version.IsEmpty() ? FString(NoValue) : Version));
	return Result;
}

bool FHCJLobbyInfo::FromAttributes(const TArray<FHCJLobbyAttribute>& Attributes, FHCJLobbyInfo& OutInfo)
{
	FHCJLobbyInfo Info;
	FString Lan;
	int32 Seed = 0;
	if (!ReadString(Attributes, KeyName, Info.HostName) || Info.HostName.IsEmpty()
		|| !ReadString(Attributes, KeyPublic, Info.PublicAddress)
		|| !ReadString(Attributes, KeyLan, Lan)
		|| !ReadString(Attributes, KeyVersion, Info.Version)
		|| !ReadInt(Attributes, KeyPort, 1, 65535, Info.Port)
		|| !ReadInt(Attributes, KeyMax, 1, 64, Info.MaxPlayers)
		|| !ReadInt(Attributes, KeyPlayers, 0, 64, Info.Players)
		|| !ReadInt(Attributes, KeyBots, 0, 64, Info.Bots)
		|| !ReadInt(Attributes, KeyDim, 2, 6, Info.Dimension)
		|| !ReadInt(Attributes, KeySeed, MIN_int32, MAX_int32, Seed))
	{
		return false;
	}
	Info.Seed = Seed;
	Lan.ParseIntoArray(Info.LanAddresses, TEXT(","), true);
	// Whatever a stranger put into the attributes: only clean values get near a travel URL or the screen.
	Info.Sanitize();
	if (Info.PublicAddress.IsEmpty() && Info.LanAddresses.Num() == 0)
	{
		return false;
	}
	OutInfo = MoveTemp(Info);
	return true;
}

bool FHCJLobbyInfo::AdvertisesSameAs(const FHCJLobbyInfo& Other) const
{
	return HostName == Other.HostName && PublicAddress == Other.PublicAddress && LanAddresses == Other.LanAddresses
		&& Port == Other.Port && Players == Other.Players && MaxPlayers == Other.MaxPlayers && Bots == Other.Bots
		&& Dimension == Other.Dimension && Seed == Other.Seed && Version == Other.Version;
}

FString FHCJLobbyInfo::DescribeMap() const
{
	return FString::Printf(TEXT("%dD - %d sectors"), Dimension, 1 << Dimension);
}

bool HCJEos::IsIPv4(const FString& Address)
{
	TArray<FString> Parts;
	Address.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 4)
	{
		return false;
	}
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || Part.Len() > 3 || !Part.IsNumeric() || Part.Contains(TEXT("-")) || Part.Contains(TEXT("+")) || FCString::Atoi(*Part) > 255)
		{
			return false;
		}
	}
	return true;
}

bool HCJEos::IsSafeHost(const FString& Address)
{
	if (Address.IsEmpty() || Address.Len() > 64)
	{
		return false;
	}
	for (const TCHAR Char : Address)
	{
		const bool bOk = (Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('A') && Char <= TEXT('Z')) || (Char >= TEXT('0') && Char <= TEXT('9'))
			|| Char == TEXT('.') || Char == TEXT('-') || Char == TEXT(':') || Char == TEXT('[') || Char == TEXT(']');
		if (!bOk)
		{
			return false;
		}
	}
	return true;
}

FString HCJEos::PickJoinAddress(const FHCJLobbyInfo& Lobby, const TArray<FString>& LocalAddresses)
{
	FString Host;
	for (const FString& Address : Lobby.LanAddresses)
	{
		if (IsIPv4(Address) && LocalAddresses.Contains(Address))
		{
			Host = TEXT("127.0.0.1");
			break;
		}
	}
	if (Host.IsEmpty())
	{
		for (const FString& Address : Lobby.LanAddresses)
		{
			if (!IsIPv4(Address))
			{
				continue;
			}
			const FString Prefix = Prefix24(Address);
			if (LocalAddresses.ContainsByPredicate([&Prefix](const FString& Local) { return IsIPv4(Local) && Prefix24(Local) == Prefix; }))
			{
				Host = Address;
				break;
			}
		}
	}
	if (Host.IsEmpty() && IsSafeHost(Lobby.PublicAddress))
	{
		Host = Lobby.PublicAddress;
	}
	if (Host.IsEmpty())
	{
		for (const FString& Address : Lobby.LanAddresses)
		{
			if (IsIPv4(Address))
			{
				Host = Address;
				break;
			}
		}
	}
	if (Host.IsEmpty() || Lobby.Port < 1 || Lobby.Port > 65535)
	{
		return FString();
	}
	return FString::Printf(TEXT("%s:%d"), *Host, Lobby.Port);
}

void FHCJStatDelta::Add(const FHCJStatDelta& Other)
{
	Kills += Other.Kills;
	Deaths += Other.Deaths;
	Wins += Other.Wins;
	Turrets += Other.Turrets;
	Matches += Other.Matches;
}

bool FHCJStatTracker::Observe(const FHCJStatSample& Sample)
{
	const auto Step = [](int32 Now, int32& Last, int32& Sum)
	{
		if (Now > Last)
		{
			Sum += Now - Last;
		}
		// Lower than before: the counters were reset (new match, other server); count on from there.
		Last = Now;
	};
	Step(FMath::Max(Sample.Kills, 0), LastKills, Pending.Kills);
	Step(FMath::Max(Sample.Deaths, 0), LastDeaths, Pending.Deaths);
	Step(FMath::Max(Sample.Turrets, 0), LastTurrets, Pending.Turrets);

	bool bSendNow = false;
	if (Sample.bIntermission)
	{
		if (!bWasIntermission && bPlayedThisMatch && Sample.bParticipating)
		{
			++Pending.Matches;
			if (Sample.bLocalWon)
			{
				++Pending.Wins;
			}
			bSendNow = true;
		}
		bPlayedThisMatch = false;
	}
	else if (Sample.bParticipating)
	{
		bPlayedThisMatch = true;
	}
	bWasIntermission = Sample.bIntermission;
	return bSendNow;
}

void FHCJStatTracker::ResetBaseline()
{
	LastKills = LastDeaths = LastTurrets = 0;
	bPlayedThisMatch = false;
	bWasIntermission = false;
}

FHCJStatDelta FHCJStatTracker::Take()
{
	const FHCJStatDelta Result = Pending;
	Pending = FHCJStatDelta();
	return Result;
}

TArray<FHCJLeaderboardRow> HCJEos::MakeLeaderboardPage(const TArray<FHCJLeaderboardRow>& Records, int32 MaxRows)
{
	TArray<FHCJLeaderboardRow> Page;
	const FHCJLeaderboardRow* LocalBelow = nullptr;
	for (int32 Index = 0; Index < Records.Num(); ++Index)
	{
		if (Index < MaxRows)
		{
			Page.Add(Records[Index]);
		}
		else if (Records[Index].bLocal)
		{
			LocalBelow = &Records[Index];
			break;
		}
	}
	if (LocalBelow != nullptr)
	{
		Page.Add(*LocalBelow);
	}
	for (FHCJLeaderboardRow& Row : Page)
	{
		Row.Name = Row.Name.TrimStartAndEnd().Left(32);
		if (Row.Name.IsEmpty())
		{
			Row.Name = TEXT("Pilot");
		}
	}
	return Page;
}
