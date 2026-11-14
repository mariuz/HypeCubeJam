#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HCJPortal.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * One end of an open hypercube edge: a trigger sphere plus a visual disc.
 *
 * NETWORK MODEL: NOT replicated. AHCJUniverse spawns one portal per open (Sector, Dim) locally on every
 * machine during Rebuild and destroys them on the next rebuild, so portal positions never travel over
 * the network (they follow from the match seed). On clients the portal is purely visual; only the server's
 * copy reacts to overlaps. Because the actor is locally spawned it has ROLE_Authority everywhere: the
 * overlap handler must gate on GetNetMode() != NM_Client.
 *
 * TELEPORT PROTOCOL (server): when a live AHCJShipCharacter begins overlapping Trigger, the portal calls
 * Ship->RequestPortalTeleport(GetTeleportDelta(), GetDestSector()) and does nothing else. The ship applies
 * the pure translation on its next server tick and owns the re-entry cooldown (see AHCJShipCharacter).
 * The ship arrives inside the destination portal's trigger; that overlap is rejected by the cooldown and no
 * new begin-overlap fires until the ship has left the trigger, so there is no bounce. Projectiles and
 * everything else ignore portals.
 *
 * Collision: Trigger is query-only, object type WorldDynamic, ignores every channel except ECC_Pawn (overlap).
 * Visual components have no collision.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class HYPECUBEJAM_API AHCJPortal : public AActor
{
	GENERATED_BODY()

public:
	AHCJPortal();

	/**
	 * Called once by AHCJUniverse right after spawning the actor at the portal's world transform
	 * (AHCJUniverse::GetPortalWorldTransform: X axis = outward direction).
	 * @param InSectorId           sector this portal stands in
	 * @param InDim                dimension it flips
	 * @param InDestSector         InSectorId ^ (1 << InDim)
	 * @param InDestWorldLocation  world location of the matching portal in InDestSector
	 * @param InTint               tint of the DESTINATION sector; colours the disc so players can tell where it leads
	 */
	void InitPortal(int32 InSectorId, int32 InDim, int32 InDestSector, const FVector& InDestWorldLocation, const FLinearColor& InTint);

	/**
	 * Wormhole mode, called instead of InitPortal: a mouth in InSectorId that carries ships to the mouth at
	 * InDestWorldLocation in InDestSector (not a hypercube neighbour; GetDim() stays INDEX_NONE). Same server-side
	 * teleport path as a portal, smaller trigger (HCJ::WormholeTriggerRadius), its own look: a bright core inside
	 * a swirl (the black hole card material in another colour) that turns towards the local camera.
	 */
	void InitWormhole(int32 InSectorId, int32 InDestSector, const FVector& InDestWorldLocation, const FLinearColor& InTint);
	bool IsWormhole() const { return bWormhole; }

	/**
	 * Cosmetic (never does anything on a dedicated server, nothing while the local view is far away): turns the glyph
	 * ring, fades the flash of a passage, sizes the far glow by its distance from the view; wormhole mode: keeps the swirl
	 * card facing the view.
	 */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Cosmetic, any machine: a ship has just gone through the portal or wormhole mouth nearest to Location (within
	 * MaxDistance): it flashes and a ripple runs across it. Called by every machine for every ship it sees jump (both
	 * ends), from the ship's own effects tick - nothing is replicated for it.
	 */
	static void NotifyPassage(const UObject* WorldContextObject, const FVector& Location, float MaxDistance = 3000.0f);

	int32 GetSectorId() const { return SectorId; }
	int32 GetDim() const { return Dim; }
	int32 GetDestSector() const { return DestSector; }
	const FVector& GetDestWorldLocation() const { return DestWorldLocation; }

	/** World-space translation applied to a ship that enters this portal: DestWorldLocation - GetActorLocation(). */
	FVector GetTeleportDelta() const { return DestWorldLocation - GetActorLocation(); }

protected:
	/** Bound to Trigger->OnComponentBeginOverlap in the constructor. Server only (returns early on clients). */
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Root. Sphere of radius HCJ::PortalTriggerRadius. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USphereComponent> Trigger;

	/** Flat cylinder of radius HCJ::PortalVisualRadius whose axis is the actor X axis, tinted with InTint. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> DiscMesh;

	/** Ring of small cubes around the disc rim (neutral colour) that makes the portal readable from afar. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UInstancedStaticMeshComponent> FrameMesh;

	/** Wormhole mode: camera-facing card with the swirl; null otherwise or when its assets do not load. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CardMesh;

	/** Glowing glyph blocks and the inner lip of the ring (destination colour); turns slowly about the portal axis. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> GlyphMesh;

	/** A glint on a quad facing the view that keeps the portal visible as a landmark from across the sector. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FarGlowMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SwirlMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FarGlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CoreMaterial;

	/** Local world time of the last passage (see NotifyPassage); very negative when none. */
	float FlashTime = -1000.0f;
	float GlyphAngle = 0.0f;
	float CoreBrightness = 1.0f;
	float CardBaseScale = 1.0f;
	FLinearColor GlowColor = FLinearColor::White;

	/** Creates FarGlowMesh in Color (no-op without the glint material). */
	void SetupFarGlow(const FLinearColor& Color);

	bool bWormhole = false;
	int32 SectorId = INDEX_NONE;
	int32 Dim = INDEX_NONE;
	int32 DestSector = INDEX_NONE;
	FVector DestWorldLocation = FVector::ZeroVector;
};
