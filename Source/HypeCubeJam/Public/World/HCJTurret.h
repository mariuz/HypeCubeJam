#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "HCJTurret.generated.h"

class AHCJShipCharacter;
class UHCJHealthComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A gun turret on a space station, hostile to every ship.
 *
 * NETWORK MODEL: a replicated actor spawned only by the server (AHCJGameMode::RebuildStations), one per turret
 * mount of FHCJStation, for the length of a match. Where the stations and their mounts are is known everywhere
 * from the match config; whether a turret is alive, how much it can still take and where it points is run-time
 * state and is replicated (bDestroyed, the health component, AimDirection), so a late joiner sees the right state.
 *
 * SERVER AI: every ThinkInterval the nearest living, uncloaked, not spawn-protected ship within range and in line
 * of sight (asteroids and the station itself block it) becomes the target. The gun turns towards the lead point
 * (HCJ::ComputeLeadAim) at a limited rate and fires AHCJProjectile::InitCustom shots with some spread once it
 * points close enough. It never turns more than a little below its mount plane.
 *
 * DAMAGE: a sphere of object type WorldStatic (ships and bolts collide with it, bots steer around it). Damage goes
 * to a UHCJHealthComponent without a shield; at zero the turret is destroyed (explosion, no collision, no fire),
 * AHCJGameMode::NotifyTurretDestroyed credits the killer, and it comes back after the configured time.
 * Ships killed by a turret die an environment death that the kill feed attributes to "Station".
 */
UCLASS(NotBlueprintable)
class HYPECUBEJAM_API AHCJTurret : public AActor
{
	GENERATED_BODY()

public:
	AHCJTurret();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** SERVER ONLY: into the health component; shots of other turrets do nothing. */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** SERVER, right after the spawn: which way is "up" for this mount (unit vector, away from the station body). */
	void ServerInit(const FVector& InMountUp);

	bool IsTurretDestroyed() const { return bDestroyed; }
	UHCJHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/** Every turret this process knows (server: all; client: the relevant ones); callers compare GetWorld(). Entries may be stale. */
	static const TArray<TWeakObjectPtr<AHCJTurret>>& GetAll();

protected:
	UFUNCTION()
	void OnRep_Destroyed();

	void HandleHealthDepleted(UHCJHealthComponent* Component, AController* Killer, AActor* Causer);
	/** Server timer: back to full health. */
	void ServerRespawn();
	/** Server: picks the target (see the class comment). */
	void ServerThink();
	/** Server: one shot along AimDirection. */
	void ServerFire();
	/** Not on a dedicated server: meshes from UHCJAssetSettings (basic shapes when they do not load). */
	void SetupVisuals();
	/** Every machine: collision and visibility from bDestroyed. */
	void ApplyDestroyedState(bool bWithEffects);

	/** Root: the turret's collision. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USphereComponent> Collision;

	/** Short column between the station and the head, along the mount's up direction. */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	/** Turns with the aim direction; the head and the gun hang on it (X = aim). */
	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<USceneComponent> AimPivot;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> HeadMesh;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UStaticMeshComponent> GunMesh;

	UPROPERTY(VisibleAnywhere, Category = "HCJ")
	TObjectPtr<UHCJHealthComponent> HealthComponent;

	UPROPERTY(ReplicatedUsing = OnRep_Destroyed)
	bool bDestroyed = false;

	/** Unit vector the gun points along (world space). */
	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal AimDirection = FVector::UpVector;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal MountUp = FVector::UpVector;

	/** Local: the direction the meshes currently show (eased towards AimDirection). */
	FVector ShownAim = FVector::UpVector;
	bool bShownDestroyed = false;
	bool bVisualsReady = false;

	// ---- server ----
	TWeakObjectPtr<AHCJShipCharacter> Target;
	float NextThinkTime = 0.0f;
	float NextFireTime = 0.0f;
	FRandomStream Random;
	FTimerHandle RespawnTimer;
};
