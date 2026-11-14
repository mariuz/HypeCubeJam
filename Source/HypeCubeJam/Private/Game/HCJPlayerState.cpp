#include "Game/HCJPlayerState.h"

#include "Net/UnrealNetwork.h"

AHCJPlayerState::AHCJPlayerState()
{
	// The engine default is 1 Hz; the current sector and the respawn times feed the HUD and the bots' hunting.
	SetNetUpdateFrequency(8.0f);
}

void AHCJPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHCJPlayerState, Kills);
	DOREPLIFETIME(AHCJPlayerState, Deaths);
	DOREPLIFETIME(AHCJPlayerState, TurretKills);
	DOREPLIFETIME(AHCJPlayerState, CurrentSector);
	DOREPLIFETIME(AHCJPlayerState, EarliestRespawnServerTime);
	DOREPLIFETIME(AHCJPlayerState, RespawnServerTime);
}

void AHCJPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (AHCJPlayerState* Other = Cast<AHCJPlayerState>(PlayerState))
	{
		Other->Kills = Kills;
		Other->Deaths = Deaths;
		Other->TurretKills = TurretKills;
	}
}

void AHCJPlayerState::Reset()
{
	Super::Reset();
	ResetMatchStats();
}

FLinearColor AHCJPlayerState::GetPlayerColor() const
{
	// Golden-ratio hue steps keep consecutive ids far apart on the colour wheel.
	const double Hue = FMath::Frac(static_cast<double>(GetPlayerId()) * 0.6180339887498949) * 360.0;
	return FLinearColor(static_cast<float>(Hue), 1.0f, 1.0f).HSVToLinearRGB();
}

void AHCJPlayerState::AddKill()
{
	if (!HasAuthority())
	{
		return;
	}
	++Kills;
	SetScore(static_cast<float>(Kills));
	ForceNetUpdate();
}

void AHCJPlayerState::AddDeath()
{
	if (!HasAuthority())
	{
		return;
	}
	++Deaths;
	ForceNetUpdate();
}

void AHCJPlayerState::ResetMatchStats()
{
	if (!HasAuthority())
	{
		return;
	}
	Kills = 0;
	Deaths = 0;
	TurretKills = 0;
	SetScore(0.0f);
	ForceNetUpdate();
}

void AHCJPlayerState::SetCurrentSector(int32 NewSector)
{
	if (HasAuthority() && CurrentSector != NewSector)
	{
		CurrentSector = NewSector;
	}
}

void AHCJPlayerState::SetRespawnTimes(float InEarliestRespawnServerTime, float InRespawnServerTime)
{
	if (!HasAuthority())
	{
		return;
	}
	if (EarliestRespawnServerTime != InEarliestRespawnServerTime || RespawnServerTime != InRespawnServerTime)
	{
		EarliestRespawnServerTime = InEarliestRespawnServerTime;
		RespawnServerTime = InRespawnServerTime;
		ForceNetUpdate();
	}
}

void AHCJPlayerState::AddTurretKill()
{
	if (HasAuthority())
	{
		++TurretKills;
		ForceNetUpdate();
	}
}
