#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "Ship/HCJWeapons.h"
#include "HCJPickup.generated.h"

class AHCJShipCharacter;
class UMaterialInstanceDynamic;
class USphereComponent;
class UStaticMeshComponent;

/**
 * One pickup pad and the item on it.
 *
 * NETWORK MODEL: unlike the rest of the generated world this IS a replicated actor, spawned only by the server
 * (AHCJGameMode::RebuildPickups) at the pad positions of FHCJPickupPads, one per pad, for the length of a match.
 * Where the pads are is known everywhere from the match config; what a pad holds is run-time state and is
 * replicated: Item and bActive, so a late joiner sees the right item on every pad it can see (net cull distance
 * HCJ::NetCullDistance, like ships).
 *
 * SERVER: a living ship that touches the trigger of an active pad gets the item (AHCJShipCharacter::ServerGiveItem;
 * refused when it would be wasted, e.g. a repair kit at full hull - the item then stays). The pad goes inactive and
 * is refilled after RespawnMin..MaxSeconds with an item picked by UHCJGameplaySettings::PickItem.
 *
 * COSMETICS (not on a dedicated server): a flat pad and a spinning, glowing shape in the item's colour; pickup
 * and refill sounds where the replicated state changes.
 */
UCLASS(NotBlueprintable)
class HYPECUBEJAM_API AHCJPickup : public AActor
{
	GENERATED_BODY()

public:
	AHCJPickup();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** SERVER: puts the first item on the pad. Called once right after the spawn. */
	void ServerFill();

	EHCJPickupItem GetItem() const { return static_cast<EHCJPickupItem>(Item); }
	/** True while an item lies on the pad. */
	bool IsActive() const { return bActive; }

	/**
	 * Every pickup actor this process knows (server: all; client: the relevant ones). One list per process:
	 * callers that care compare GetWorld(). Entries may be stale (check IsValid).
	 */
	static const TArray<TWeakObjectPtr<AHCJPickup>>& GetAll();

protected:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_State();

	/** SERVER: gives the item to Ship when the pad is active and the ship takes it. */
	void ServerTryCollect(AHCJShipCharacter* Ship);
	/** Every machine but a dedicated server: shape, colour and visibility from Item / bActive. */
	void RefreshVisuals();

	/** Root; overlaps Pawn only (server). */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> PadMesh;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> ItemMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PadMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ItemMaterial;

	/** EHCJPickupItem. */
	UPROPERTY(ReplicatedUsing = OnRep_State)
	uint8 Item = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bActive = false;

	/** Local: the state the visuals / sounds were last refreshed for. */
	bool bShownActive = false;
	bool bVisualsReady = false;
	bool bGlowMaterial = false;

	FTimerHandle RefillTimer;
};
