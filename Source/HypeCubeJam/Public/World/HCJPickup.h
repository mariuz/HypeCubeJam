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

/** How a filled pad shows itself from far away: sizes in uu, pure functions of the distance to the view (tested). */
namespace HCJPickupLook
{
	/** The light column: its height above the pad, its width next to the view, and the share of the view distance it is at least wide (about 5 pixels at 1280x720), up to a limit. */
	inline constexpr float BeamHeight = 4200.0f;
	inline constexpr float BeamWidth = 70.0f;
	inline constexpr float BeamAngularWidth = 0.008f;
	inline constexpr float BeamMaxWidth = 520.0f;
	/** The halo around the item: edge of its quad next to the view, the share of the view distance it is at least, and its limit. */
	inline constexpr float HaloEdge = 900.0f;
	inline constexpr float HaloAngularEdge = 0.09f;
	inline constexpr float HaloMaxEdge = 4500.0f;
	/** Column and halo are not drawn for a view farther away than this (another sector). */
	inline constexpr float MaxViewDistance = 90000.0f;

	/** Width of the column for a view ViewDistance away. */
	HYPECUBEJAM_API float BeamWidthAt(float ViewDistance);
	/** Edge of the halo quad for a view ViewDistance away, before the pulse. */
	HYPECUBEJAM_API float HaloEdgeAt(float ViewDistance);
	/** The slow pulse of the halo, 0.8 .. 1.0; Phase shifts it per pad so that the pads of a sector do not beat together. */
	HYPECUBEJAM_API float HaloPulse(float TimeSeconds, float Phase);
}

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
 * and refill sounds where the replicated state changes. So that a pad can be found from the other end of a sector
 * (item 38): a column of light above a filled pad and a pulsing halo around the item, both in the colour of the
 * item's KIND (HCJ::GetItemKindColor: green health, amber weapons, magenta power-ups) and both kept at a minimum
 * size on the screen (HCJPickupLook); the pad has the same colour. Local looks only: nothing of it is replicated.
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
	/** SERVER, development (HCJMenuCmd padview): puts this item on the pad, filled, for a picture. */
	void ServerDebugSetItem(EHCJPickupItem NewItem);

	EHCJPickupItem GetItem() const { return static_cast<EHCJPickupItem>(Item); }
	/** True while an item lies on the pad. */
	bool IsActive() const { return bActive; }

	/**
	 * Every pickup actor this process knows (server: all; client: the relevant ones). One list per process:
	 * callers that care compare GetWorld(). Entries may be stale (check IsValid).
	 */
	static const TArray<TWeakObjectPtr<AHCJPickup>>& GetAll();

	/** SERVER: gives the item to Ship when the pad is active and the ship takes it (a touch, or the Easy reach of item 40). Returns whether it was taken. */
	bool ServerTryCollect(AHCJShipCharacter* Ship);

protected:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_State();


	/** Every machine but a dedicated server: shape, colour and visibility from Item / bActive. */
	void RefreshVisuals();
	/** Every machine but a dedicated server, every frame: column and halo sized and turned for the local view; hidden on an empty pad and from another sector. */
	void TickBeacon(float Now);

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

	/** The light column above a filled pad (a thin cylinder with the glow material) and the halo around the item (a quad facing the view with the glint material). */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> BeamMesh;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> HaloMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeamMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HaloMaterial;

	/** EHCJPickupItem. */
	UPROPERTY(ReplicatedUsing = OnRep_State)
	uint8 Item = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bActive = false;

	/** Local: the state the visuals / sounds were last refreshed for. */
	bool bShownActive = false;
	bool bVisualsReady = false;
	bool bGlowMaterial = false;
	/** Local: whether column and halo are drawn right now, and the column width last set (it is only set again when it changes by a few percent). */
	bool bBeaconShown = false;
	float ShownBeamWidth = 0.0f;

	FTimerHandle RefillTimer;
};
