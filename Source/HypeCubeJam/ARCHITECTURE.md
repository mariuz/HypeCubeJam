# HypeCubeJam architecture

Multiplayer free-for-all space shooter, UE 5.9, one runtime module `HypeCubeJam`. Authored assets are cosmetic only.
The public headers are the contract; every function comment says which machine runs it and who calls it.

## Rules for implementers
- Do not change public signatures, replicated properties or RPCs. You may add private helpers in your own
  `.cpp` (anonymous namespace) and private members to headers of your own group only.
- Includes are relative to `Public/`: `#include "Ship/HCJShipCharacter.h"`. `.cpp` files mirror the layout under
  `Private/`. `LogHCJ` comes from `HypeCubeJam.h`.
- Randomness: `FRandomStream` only, never `FMath::Rand*`. Derived seeds only through `HCJ::MixSeed`.
- Units are uu (cm), seconds, degrees. Shared constants: `namespace HCJ` in `Hypercube/HCJHypercubeGraph.h`
  and `World/HCJSectorLayout.h`.
- `AHCJUniverse` and `AHCJPortal` are spawned locally, so they have `ROLE_Authority` on clients: server-only code in
  them tests `GetNetMode() != NM_Client`. Replicated actors (ship, projectile, states) use `HasAuthority()`.
- Assets: collision and the fallback look are `/Engine/BasicShapes/{Sphere,Cube,Cone,Cylinder}` (100 uu at scale 1)
  with `/Engine/BasicShapes/BasicShapeMaterial` (vector `Color`, scalar `Roughness`). Every other asset is named
  only in `UHCJAssetSettings` (`Assets/HCJAssetSettings.h`, soft paths from `DefaultGame.ini`) and loaded through
  `UHCJAssetSettings::Load`, which returns null (once logged) when it is missing - every caller must then keep
  its basic-shape / silent fallback. Never load them on a dedicated server, never let gameplay depend on them.

## Seed -> world
1. Server `AHCJGameMode::InitGame` parses `?Seed= ?Dim= ?EdgeP= ?Bots= ?MinBots= ?FragLimit= ?TimeLimit=` and builds
   an `FHCJMatchConfig {Seed, Dimension, EdgeProbability, MatchNumber}` (random clock-based seed unless `?Seed`).
2. `InitGameState` -> `AHCJGameState::SetMatchConfig`: the config is the ONLY replicated world-generation state
   (one `ReplicatedUsing` struct). The server applies it immediately, before any login.
3. Every machine runs `AHCJGameState::ApplyMatchConfig` (server: from SetMatchConfig; clients and late joiners:
   from `OnRep_MatchConfig`): spawn the local, non-replicated `AHCJUniverse` if missing, then `Rebuild(config)`.
4. `Rebuild`: `FHCJHypercubeGraph::Build` (random spanning tree + extra edges with probability p, always connected),
   `FHCJSectorLayout::Generate` per sector (asteroids, portals, spawn points, tint), `FHCJBlackHole::Make` per sector
   (from the finished layout and the replicated `FHCJBlackHoleSettings`, own hash - the layout is not touched),
   then per sector the asteroid ISM components with collision, one local `AHCJPortal` per open edge, lights.
   Machines that render additionally hide the collision shapes behind rock-mesh ISMs (one per rock mesh and sector,
   no collision; variant `HCJ::PickAsteroidVariant`, transform `FHCJAsteroid::ComputeVisualTransform`, which keeps
   every rock inside its bounding sphere + `HCJ::MaxVisualOverhang`) and spawn the local `AHCJSpaceBackdrop`
   (stars, Milky Way, planets, nebulae, the look of the black hole).
5. Sector `S` lives at `FHCJHypercubeGraph::GetSectorWorldOrigin(S)` on a 500000 uu grid in one persistent world
   (`/Engine/Maps/Entry`); nothing but the config crosses the network. A new match = new config = Rebuild everywhere.

## Authority
- Movement: `ACharacter` + `UHCJShipMovementComponent` (MOVE_Flying, zero gravity). The owning client predicts,
  the server is authoritative; boost rides in the saved-move flags so it never causes corrections.
- Combat: the client sends only a fire intent (`ServerSetFiring`). The server fires from its own view of the ship at
  most once per `FireInterval`, spawns replicated `AHCJProjectile`s, applies damage in `TakeDamage` (server only),
  owns health and the four shield facings (`UHCJHealthComponent`, replicated; see "Shield facings"), death, score
  and respawn. Clients never report hits.
- Death: `AHCJShipCharacter::Die` -> `AHCJGameMode::NotifyShipKilled` (score, kill feed, death camera, respawn timer)
  -> ship detaches and expires. Respawn: game-mode timer after `RespawnDelay`, or `ServerRequestRespawn` after
  `MinRespawnDelay`; always `RestartPlayer` at a generated spawn point of a random sector.
- `AHCJPlayerState` (always relevant): kills, deaths, current sector, respawn times. Ships and projectiles are
  net-culled at `HCJ::NetCullDistance`, so other sectors cost nothing and are known only through player states.
- HUD and input exist only on the owning client and read replicated/local state.

## Cosmetics (never on a dedicated server, nothing extra replicated)
- Ship look: hull = `UHCJAssetSettings::PickShipVariant(PlayerId)`, skin = nearest `ShipSkins` colour to
  `AHCJPlayerState::GetPlayerColor()`; applied in `AHCJShipCharacter::ApplyShipLook` when the player state arrives.
  Engine sound, boost trail and thruster glow follow the replicated velocity in `TickShipEffects`.
- Bolt: mesh, trail and muzzle flash in `AHCJProjectile::BeginPlay` (the replicated spawn is the event). Impact:
  where the local simulation of the bolt hits (`OnCollisionHit`), or in `Destroyed()` on clients when the server
  destroyed the bolt early and the local simulation saw no hit. No RPC.
- Death: explosion and sound where `bIsDead` arrives (`Die` on a listen host, `OnRep_IsDead` on clients); a ship
  that is already dead when it becomes relevant gets none.
- One-shot effects and sounds are skipped beyond `EffectMaxDistance` / the sound falloff from the local view.

## Seeing the enemy (cosmetic, every machine that draws)
- Ship size: `HCJ::ShipCollisionRadius` 100 (movement capsule, every machine), `UHCJGameplaySettings::AimAssistRadius` 135
  (the hit sphere), `UHCJAssetSettings::ShipMeshLength` 560. Layouts and checksums do not depend on them.
- `AHCJShipCharacter::SetupShipVisuals` creates, `TickShipEffects` drives: `RimMaterial` (the hull's OVERLAY material,
  `M_HCJ_ShipRim`: rim glow on other ships + a floor on all), the glint (`BeaconMesh` as a quad with `M_HCJ_ShipGlint`,
  sized by its distance from the local view), `EngineGlowMesh` and `EngineTrailMesh` (world-space quads). "Own ship" is
  the pawn of the first local controller; every other ship is an enemy. Hidden when dead or cloaked on this machine
  (`bCloakLook`). `UHCJUserSettings::bEnemyHighlight` switches the rim off and shrinks the glint.
- HUD: `AHCJHUD::DrawTargets` draws brackets and arrows with `DrawMarkerLine` (a line over a dark line).

## Portals (cosmetic part)
- `AHCJPortal::InitPortal`: ring blocks (`FrameMesh`), turning glyphs and lip (`GlyphMesh`), the swirl quad (`DiscMesh`
  with `M_HCJ_PortalSwirl`, colour = tint of the destination sector), a far glow quad (`FarGlowMesh`, the ship glint
  material). `Tick` (TG_PostUpdateWork, returns at once when the local view is in another sector) turns the glyphs,
  fades the passage flash and sizes the far glow. `AHCJPortal::NotifyPassage(location)` (static, every machine) is called
  from `AHCJShipCharacter::TickShipEffects` when a ship's position jumps by a sector, for both ends. Wormhole mouths
  (`InitWormhole`) keep core + swirl card and share the far glow and the flash. Trigger and transform are untouched.

## Intro movie (local, once per process)
- `UI/HCJIntroLogic.h` (plain C++, tested): `FHCJIntroFlow` (ends on: played, key, open timeout, error, time limit; title
  and fade alphas) and `HCJIntro::ShouldPlay(command line, ...)`.
- `AHCJPlayerController::TickIntro` (from `PlayerTick` while `bInStartMenu`, before the title menu is opened):
  `StartIntro` creates `UMediaPlayer` / `UMediaTexture` / `UFileMediaSource` (no assets; `Content/Movies/intro_veo.mp4`,
  a loose file) and the widget `SHCJIntro` (`Private/UI/HCJIntro.*`, viewport z-order 100); `EndIntro` removes them. No
  audio component: the movie is silent, `UHCJAudioDirector` plays the menu track from the first frame (`IsInStartMenu`).

## Planets (cosmetic)
- `AHCJSpaceBackdrop`: `PlanetBalls[2]` / `PlanetRings[2]` with dynamic instances of `M_HCJ_PlanetSurface` /
  `M_HCJ_PlanetRing`, set up per sector in `ShowSector` (frame of the picture, sun direction, atmosphere colour, ring
  size from `FCatalogBody`). The older instanced `Planets` remain as the fallback when the surface material is not set.

## Black holes (gameplay)
- Placement `FHCJBlackHole::Make(seed, layout, settings)`; settings in `FHCJMatchConfig::BlackHoles` (replicated).
- Pull = drift velocity `AHCJUniverse::GetGravityDrift(location)`, applied after the normal step in
  `UHCJShipMovementComponent::PhysFlying` (swept move, `Velocity` untouched). Pure function of position, executed
  by the owning client's prediction, the server's processing of its moves and correction replays alike.
- `AHCJProjectile::Tick` (enabled only in sectors with a hole) applies the same drift on every machine.
- Server: `AHCJShipCharacter::ServerTickBounds` kills a ship inside the horizon (`Die(nullptr, nullptr)` after
  `AHCJGameMode::NotifyBlackHoleDeath`). Bots: `AHCJBotController::SteerTowards` / `PickWanderPoint`.

## Space stations
- `FHCJStation::Make(seed, layout, hole, pads, wormhole mouth, settings)` per sector in `AHCJUniverse::Rebuild` (last of
  the features); settings in `FHCJMatchConfig::Stations`. `AHCJUniverse::BuildStation` creates the box collision
  (every machine) and the look; `BuildSectorGeometry` / `BuildSectorRocks` skip `IsAsteroidSuppressed` asteroids.
- Look: `UHCJAssetSettings::StationMesh` ("Space Station 4" by Gerardo Justel, `/Game/SpaceStation4`) at
  `StationMeshScale`, centred on the hub; the boxes the model does not envelop (beam along Y, mast, their panels) are
  still drawn. Collision is the boxes of `FHCJStation` whatever is drawn.
- Turrets: `AHCJTurret`, REPLICATED, spawned by `AHCJGameMode::RebuildStations` (after every `SetMatchConfig`).
  Collision sphere of type WorldStatic; `UHCJHealthComponent` (`SetMaxValues`); shots are `AHCJProjectile::InitCustom`
  with the turret as owner and no instigator, so a kill by them has no killer controller.
- `AHCJGameMode::NotifyTurretDestroyed` -> `AHCJPlayerState::AddTurretKill`; `NotifyShipKilled` names "Station".

## Wormholes
- `FHCJWormholes::MakeLinks(seed, dimension, settings)` -> pairs of non-adjacent sectors; `MakeMouth` per end from the
  finished layout, hole and pads; settings in `FHCJMatchConfig::Wormholes`. All in `AHCJUniverse::Rebuild`, which also
  builds `FHCJRouteTable` (portals + wormholes) and spawns two local `AHCJPortal` actors per link (`InitWormhole`).
- Teleport: `RequestPortalTeleport(delta, dest, bWormhole)`; delta = other mouth - this mouth.
- Bots: `AHCJUniverse::HasHop / GetHopLocation` with hop = portal dimension or `HCJ::WormholeHop`.

## Pickups, weapons, power-ups
- Pads: `FHCJPickupPads::Make(seed, layout, hole, settings)` per sector in `AHCJUniverse::Rebuild` (own hash stream
  `HCJ::FFeatureHash`, layout untouched); settings in `FHCJMatchConfig::Pickups` (replicated). Pad checksum logged.
- Items: `AHCJPickup`, a REPLICATED actor per pad spawned by `AHCJGameMode::RebuildPickups` (after every
  `SetMatchConfig`). Server: trigger overlap -> `AHCJShipCharacter::ServerGiveItem` -> inactive -> refill timer
  (`ChoosePickupItem`, `RandomPickupDelay` from the game mode's gameplay random stream).
- Weapons: `FHCJWeaponDef` table in `UHCJGameplaySettings`; `FHCJWeaponState` (plain, tested) is the give / consume /
  fall-back logic. `ServerTickWeapon` fires `Pellets` deferred-spawned `AHCJProjectile`s (`InitWeapon`); the
  projectile reads speed, lifetime, damage and homing from the table by its replicated weapon type.
- Power-ups: `PowerupFlags` (replicated to all) + end times (owner only). Overdrive ->
  `UHCJShipMovementComponent::SetOverdrive` on server and owning client (saved moves keep the flag for replays, the
  server never reads it from the client). Quad -> damage multiplier at spawn. Cloak -> hidden on other machines,
  ignored by bots and missiles beyond `CloakDetectRange`.
- Bots: `FindUsefulPickup` every Think; Wander flies to it, Attack detours over it while firing.

## Teleport protocol
1. Portals of an edge are point reflections: `LocalPos(S, d) == -LocalPos(S ^ (1 << d), d)`.
2. Server: a live ship begins overlapping the portal trigger -> `AHCJPortal` calls
   `Ship->RequestPortalTeleport(Portal->GetTeleportDelta(), DestSector)`.
3. The ship rejects the request while dead, pending or on cooldown; otherwise it starts
   `HCJ::PortalReentryCooldown` and, on its next server tick, does
   `TeleportTo(Location + Delta, Rotation, false, true)`: a pure translation, velocity and rotation unchanged,
   so it keeps flying into the destination sector.
4. It arrives inside the destination trigger; that begin-overlap is rejected by the cooldown and none fires again
   until the ship has left the trigger: no bounce. To go back, leave the trigger and re-enter.
5. The owning client follows through the normal movement correction, other clients through replication.
6. Leaving a sector the hard way: damage beyond `SectorSafeRadius`, death beyond `SectorKillRadius` (server).

## Bots
- `AHCJBotController` (server only, `bWantsPlayerState`) flies the same `AHCJShipCharacter`; no navmesh.
- Fill rule: `desired = clamp(max(MinBots, MinCombatants - humans), 0, MaxBots)` (defaults 0, 8, 16), evaluated on
  login, logout and every 0.25 s. Missing bots are spawned; surplus bots are removed (shipless first, then lowest
  score), so bots leave as humans join and return when they leave.
- Brain: Wander -> Attack (nearest hostile in the same sector, lead aim, strafe, fire in cone with line of sight)
  -> Travel (BFS `NextHopDim` towards the nearest sector holding a human ship, else a random open portal).
  Asteroid avoidance by sphere sweeps against object type WorldStatic. The game mode respawns bots like players.

## Collision
| Thing | Object type | Notes |
|---|---|---|
| Asteroid ISMs | WorldStatic (`BlockAll`) | simple collision of the basic shapes; hidden when rock meshes are drawn over them; a destroyed rock's instance has zero scale (no body), pieces are added instances |
| Phaser beam | line trace, channel WorldDynamic | what a projectile hits: rocks, stations, turrets, the hit sphere of ships |
| Rock mesh ISMs, backdrop | none | drawn only |
| Ship capsule | Pawn | sphere, radius 100 (`HCJ::ShipCollisionRadius`); bolts and beams hit the 135 uu `HitSphere` |
| Projectile | WorldDynamic, query only | blocks WorldStatic + Pawn, ignores its instigator |
| Portal trigger | WorldDynamic, query only | overlaps Pawn only |

## Match loop
Playing (frag limit 25 or 600 s) -> Intermission 8 s (ships destroyed, scoreboard, winner) -> new seed, universe
rebuilt on every machine, stats reset, everyone respawned after 1 s -> Playing.

## Tests (group A, `Private/Tests/`, `EditorContext | ProductFilter`, names `HypeCubeJam.*`)
Graph connectivity for dims 2..6 over many seeds; edge symmetry; determinism of graph and layout; BFS tables
consistent; every open edge has a portal at both ends and they mirror; asteroid/portal/spawn clearances; golden
digests; shield facings (`HypeCubeJam.Shield.*`: facing selection, absorb and overflow, per-facing regeneration,
overcharge spread, replication round trip, facing to present, the hint); photon torpedoes (`HypeCubeJam.Torpedo.*`:
stock and reload, damage against a facing up / down, arming and blast falloff, homing turn limit and lock, blast
facing and the bots' rule, HUD rules incl. markers clear of panels); phasers (`HypeCubeJam.Phaser.*`: bank, falloff / beam byte / aim
assist / design targets, weapon slots); asteroids (`HypeCubeJam.Asteroids.*`: split determinism and pieces inside the rock, replication
round trip in any order); effects (`HypeCubeJam.Effects.*`: shockwave falloff, debris caps); music (`HypeCubeJam.Audio.MusicTracks`); online logic (config validation and the unavailable states, lobby attribute packing / parsing, join
address, stat tracker, leaderboard page); the intro (`HypeCubeJam.UI.IntroFlow`: every way it ends, title and fade
timing; `HypeCubeJam.UI.IntroCommandLine`: which command lines play it); rock variant and visual transform envelope; ship hull/skin selection; sector sky; black hole placement
(determinism, clearances, share of sectors) and gravity curve (direction, monotonic, clamp, tuning).

## File -> owner group
A = Hypercube + World (+ Tests), B = Ship, C = Game, D = AI. Each header `X.h` has `Private/<same dir>/X.cpp`.

| File | Group |
|---|---|
| `Public/Hypercube/HCJHypercubeGraph.h` | A |
| `Public/Hypercube/HCJMatchConfig.h` (header only, no .cpp) | A |
| `Public/World/HCJSectorLayout.h` | A |
| `Public/World/HCJUniverse.h` | A |
| `Public/World/HCJPortal.h` | A |
| `Public/World/HCJBlackHole.h` | A |
| `Public/World/HCJPickupPads.h`, `HCJFeatureHash.h`, `HCJPickup.h`, `HCJWormholes.h`, `HCJStations.h`, `HCJTurret.h` | A |
| `Public/Ship/HCJWeapons.h` | B |
| `Public/Game/HCJGameplaySettings.h` | C |
| `Public/Audio/HCJAudioDirector.h` | C |
| `Public/World/HCJSpaceBackdrop.h` | A |
| `Public/Assets/HCJAssetSettings.h` | shared |
| `Private/Tests/*.cpp` | A |
| `Public/Ship/HCJShipCharacter.h` | B |
| `Public/Ship/HCJShipMovementComponent.h` | B |
| `Public/Ship/HCJHealthComponent.h`, `Public/Ship/HCJShieldFacings.h` | B |
| `Public/Ship/HCJProjectile.h`, `Public/Ship/HCJTorpedo.h`, `Public/Ship/HCJTorpedoBlast.h`, `Public/Ship/HCJPhaser.h` | B |
| `Public/World/HCJAsteroidField.h` | A |
| `Public/Effects/HCJEffectsLogic.h`, `Public/Effects/HCJEffectsDirector.h` | shared |
| `Public/Audio/HCJMusicLogic.h` | C |
| `Public/Game/HCJGameMode.h` | C |
| `Public/Game/HCJGameState.h` | C |
| `Public/Game/HCJPlayerState.h` | C |
| `Public/Game/HCJPlayerController.h` | C |
| `Public/Game/HCJHUD.h` | C |
| `Public/AI/HCJBotController.h` | D |

Cross-group calls: A -> B `RequestPortalTeleport`; B -> C `NotifyShipKilled`, `AHCJPlayerState::GetPlayerColor`;
B -> A `AHCJUniverse::GetSectorAtLocation`; C -> A `Rebuild`, spawn/sector queries; C -> B control API, `Destroy`;
C -> D spawn/destroy bot controllers; D -> A universe/graph queries; D -> B control API and ship queries;
D -> C `AHCJPlayerState::GetCurrentSector`.

## Menu, HUD and feel (added in the polish pass)
- `Game/HCJUiLogic.h` (plain C++, tested): kill streaks, HUD scale, off-screen marker, start-menu command-line rule,
  target selection, fire-now rule, radar projection, and `HCJMenuFlow` (process-wide "open the title menu" state).
- `Game/HCJBalance.h` (plain C++, tested): bot difficulty rows, spawn scoring, boost meter step, shield regeneration.
- `Game/HCJUserSettings.h`: what the menu stores in the user's GameUserSettings.ini.
- `Private/UI/HCJMenu.*`: the one Slate menu widget; every action goes through `AHCJPlayerController::Menu*`
  (the two online pages read `UHCJEOSSubsystem` directly).
- Title menu: `AHCJGameMode::PostLogin` puts the local host into the menu (spectator, no ship) when
  `HCJMenuFlow::ShouldOpenMenu`; `StartMatchFromMenu` / `ReturnToMenu` / `BeginMatch` are the transitions.
- HUD: `HCJHUD.cpp` (frame, map, feed, boards) and `HCJHUDReadability.cpp` (targets, radar, damage, portals, help).
  Target lock lives in `AHCJPlayerController` (local) and is mirrored to the server through
  `AHCJShipCharacter::SetLockTarget` for homing missiles.
- Hints: `UI/HCJHints.h` (plain C++, tested): `FHCJHintDirector` (which hint is up: onboarding order, priorities,
  show limits, cooldowns), key-cap markup, death tips, objective line. `Private/UI/HCJHUDHints.cpp` feeds it from the
  local ship and draws the hint panel, the sector banner, the jump flash and the shared panel / key-cap helpers.
  One director per process (file static), clocked by game time. Local only; `UHCJUserSettings::bHints`, key H.
- New RPCs / replicated state: `AHCJGameState::MulticastKillEvent` gained a cause string;
  `AHCJPlayerController::ClientDamageTaken`, `ClientMissileWarning` (unreliable, cosmetic);
  `AHCJShipCharacter::ServerSetLockTarget`, replicated `bSpawnShield`; torpedoes: `ServerFireTorpedo`, owner-only
  `TorpedoCount` / `TorpedoReadyTime`.
- Aim assist: `AHCJShipCharacter::HitSphere` (query only, blocks WorldDynamic = projectiles, no overlap events).
- Boost meter: simulated in `UHCJShipMovementComponent::UpdateCharacterStateBeforeMovement` on owning client and
  server; carried in the saved moves; not replicated.

## Shield facings
- `Ship/HCJShieldFacings.h` (plain C++, tested): `EHCJShieldFacing` (fore, aft, port, starboard) and `namespace
  HCJShield`: `SelectFacing` (hit direction in the ship's frame -> facing), `AbsorbOnFacing` / `AbsorbSpread` /
  `AddSpread` (a hit on one facing, damage without a direction, a shield cell), `Quantise` / `Pack` / `Unpack` (four
  strengths in one replicated word), `PackDirection` (the flare's direction in 16 bits), `StrengthColor`,
  `PickFacingToPresent` (bots).
- `UHCJHealthComponent`: `SetFacingCount(4)` (ships; turrets keep one shield), `ApplyDamageOnFacing` next to
  `ApplyDamage` (no direction: spread), one regeneration clock per facing, `GetFacingShield / GetFacingFraction /
  IsFacingDown`. Replicated: `Health` and `PackedShield` (uint32, replaces the float `Shield`); clients decode it in
  `OnRep_Shield`. Capacity and regeneration of ships come from `UHCJGameplaySettings` (set in
  `AHCJShipCharacter::BeginPlay` before the component begins play; same config on every machine).
- `AHCJShipCharacter::TakeDamage` (server): facing from the impact point of a `FPointDamageEvent` relative to the
  ship, in its frame; then `MulticastShieldHit` (unreliable, uint16 direction + uint8 strength, at most one per ship
  every 0.08 s) when the facing absorbed something. `HandleHealthChanged` (every machine, bound to
  `OnHealthChanged`) notes which facing lost strength or went down: HUD flash, collapse flicker and sound. The
  bubble is `ShieldBubble` (basic sphere, material `UHCJAssetSettings::ShieldBubbleMaterial`), hidden and untouched
  by Tick unless `bShieldBubbleActive`.
- HUD: `AHCJHUD::DrawShieldFacings` (own ship in `DrawVitals`, the target in `DrawTargetPanel`); hint
  `EHCJHint::ShieldDown`. Bots: `AHCJBotController::TickShieldFacing`.
- Game mode statistics (`Balance t=` log line): time to kill, kills per minute, hit rate, damage per kill, hits
  that stayed on the shield / reached the hull, facings collapsed.

## Photon torpedoes
- `Ship/HCJTorpedo.h` (plain C++, tested): `FHCJTorpedoBay` (stock, reload, restock), and `namespace HCJTorpedo`:
  `IsArmed`, `SplitDamage` (what a facing stops of one torpedo), `BlastScale`, `TurnTowards` (the homing step, also
  used by the homing missiles), `KeepsLock`, `GetBotSkill` / `ShouldBotFire`, `IsEnabled` (`-HCJNoTorpedoes`).
- A torpedo is an `AHCJProjectile` of weapon type `EHCJWeapon::Torpedo` (a row of the weapon table: reload, stock of a
  fresh ship, speed, lifetime, turn rate, damage to an open hull; the other numbers are `Torpedo*` in
  `UHCJGameplaySettings`). The weapon is NOT something a ship holds: `HCJ::IsPrimaryWeapon` is false for it.
- Ship (`AHCJShipCharacter`): replicated to the owner only `TorpedoCount` (uint8) and `TorpedoReadyTime` (server world
  time); `FireTorpedo` (control API) -> `ServerFireTorpedo` (reliable, one per key press) -> the server checks the
  bay and spawns the projectile. `ServerGiveItem(EHCJPickupItem::Torpedoes)` restocks. A new ship has a fresh stock.
- Projectile: `HomingTarget` (already replicated for missiles) is the firer's lock target at launch, dropped for good
  by the server when `KeepsLock` fails; every machine counts `DistanceTravelled` (arming); `ServerDetonate` (on
  contact, or from `LifeSpanExpired`) applies point damage to what it hit and, scaled by `BlastScale`, to every ship
  and turret in the blast radius with a clear line - the impact point of that damage event is the blast, so
  `AHCJShipCharacter::TakeDamage` picks the facing towards the blast with the existing rule. `TakeDamage` sees the
  causer is a torpedo and uses `UHCJHealthComponent::ApplyTorpedoOnFacing`. Nothing new is replicated for the
  detonation: a client shows `AHCJTorpedoBlast` (local actor: a quad facing the view, material age driven in Tick)
  when its copy of the torpedo hits something or when the torpedo is destroyed. `AHCJProjectile::GetLiveTorpedoes`
  (every machine) feeds the HUD warning and the bots.
- HUD: pips and reload in `DrawLoadout`; `DrawTorpedoWarning` (reads the replicated `HomingTarget` of the torpedoes
  this machine sees - no RPC); hint `EHCJHint::TorpedoChance`. World markers keep clear of the panels: every panel
  notes its rectangle (`NotePanel`, also from `DrawPanel`), `DrawTargets` tests markers against the rectangles of the
  frame before (`HCJUi::IsMarkerClear`) and gives a covered enemy an edge arrow instead.
- Bots: `AHCJBotController::ThinkTorpedo` (lock + `ShouldBotFire` from what the target panel shows a player) and
  `ThinkTorpedoEvasion` (a torpedo homing on the bot: `SteerTowards` flies away from its line under boost).
- Game mode statistics (`Torpedoes t=` log line): fired, homing, lost their target, detonated, duds, damaged a ship,
  hits (and on an open facing), damage, kills and their share, bots evading, kills by weapon.

## Phasers
- `Ship/HCJPhaser.h` (plain C++, tested): `FHCJPhaserBank` (charge, drain, recharge delay, lock when emptied, relock
  level) and `namespace HCJPhaser`: `RangeScale` (damage with range), `PackLength` / `UnpackLength` (the beam byte),
  `PackCharge` / `UnpackCharge` (the owner's byte), `PickAssistTarget` (the beam's aim assist), `IsEnabled` (`-HCJNoPhasers`).
- Weapon slots: `EHCJWeapon::Phaser` (appended, 6) is a BASE weapon like the blaster (`HCJ::IsBaseWeapon`): no
  ammunition. A ship has a base weapon (`BaseWeaponType`, phaser on a new ship) and may hold one pickup weapon in its
  place; `FHCJWeaponState::Drop` / an emptied pickup go back to the base weapon, `ToggleBase` (key B,
  `ServerToggleBaseWeapon`) switches it between phaser and blaster. `GetFireWeapon()` is what the trigger fires right
  now: a phaser whose bank is locked fires the blaster (HUD, bots and `ServerTickWeapon` all ask it).
- The row of the weapon table: `bBeam`, `BeamRange`, `Damage` = per second at close range, `FireInterval` = the damage
  step; `FHCJWeaponDef::GetRange()` replaces speed * lifetime wherever a range is needed. Tunables `Phaser*` in
  `UHCJGameplaySettings`.
- Server (`AHCJShipCharacter::ServerTickPhaser`, every tick while the trigger is held and the bank has charge): one
  line trace on the projectile channel from the muzzle along the aim; when that does not end on a ship,
  `PickAssistTarget` over the living, uncloaked ships and a second trace to the pick. Damage every `FireInterval`:
  `ApplyPointDamage` with the hit (so `TakeDamage` picks the shield facing from the impact point as for a bolt; the
  damage causer is the firing SHIP, which is how `TakeDamage`, the turret and `NotifyShipKilled` know a phaser), or
  `AHCJUniverse::ServerDamageAsteroid` for rock. Hit cues for the shooter are sent at most every 0.3 s.
- Replicated: `PhaserBeam` (uint8 to everybody: 0 off, 255 a full-length miss, else the length), `PhaserTarget` (the
  ship or turret the beam holds), `PhaserChargeRep` and `BaseWeaponType` (owner only). No RPC. Every machine draws the
  beam each frame from the ship it sees (`GetPhaserBeamSegment`: the smoothed mesh of a remote ship, the control
  rotation of the own one; the end is the target's hull or `PhaserBeam` along the nose) - `TickPhaserEffects`: one
  quad with `M_HCJ_PhaserBeam` rolled to the view, the torpedo star as a glow at both ends, impact sparks every
  0.12 s, two looping audio components, start / stop cues from `OnRep_PhaserBeam`.
- HUD: bank bar with the relock mark in `DrawLoadout`, `[B]` line, hints `PhaserEmpty` / `RockBreak`, damage numbers of
  a beam add up in one number (`FDamageNumber::Ship`). Bots: `TickAttack` holds the beam only inside
  `FHCJBotSkill::PhaserConeDeg` and keeps `PhaserReserve` in the bank.
- Game mode statistics (`Phasers t=` log line): seconds of beam, share on a ship, damage, kills, rocks destroyed,
  rocks in the world, destroyed-set checksum, shockwaves.

## Destroyable asteroids
- `World/HCJAsteroidField.h` (plain C++, tested). The layouts are never changed. Every rock is a NODE with a 16-bit id
  (layout index, plus a slot for each of at most two breaks); `HCJAsteroids::MakeChildren(sector seed, id, shape,
  rules)` gives the two or three pieces of a node big enough to break (own hash stream; pieces lie inside the rock,
  same rotation). The state of a match is the SET OF DESTROYED NODE KEYS (`FHCJDestroyedSet`; key = sector << 16 |
  id); `CollectPieces` says what appears when a node goes, whatever the order the keys arrive in. Rules:
  `FHCJAsteroidRules` in `FHCJMatchConfig::Asteroids` (replicated with the config; `?Rocks=0|1`, `-HCJNoRockDamage`).
- `AHCJUniverse`: `Rocks` (key -> shape, collision instance, drawn instance, server health), `CollisionInstanceKeys`
  (component + instance index -> key, for hits), per sector the collision component of each shape and the drawn
  component of each rock mesh (`GetShapeComponent` / `GetRockComponent` create one when a piece is the first of its
  kind). `ApplyAsteroidDestroyed(key)` gives the instance a zero scale (that ends its collision body; the indices
  of the others stay) and adds the pieces with `AddInstance`. Server: `ServerDamageAsteroid(Key)` /
  `ServerDamageAsteroidsInRadius` (health = radius * `HealthPerRadius`), called by the phaser and by
  `AHCJProjectile::ServerDetonate`; bolts do nothing to rock.
- `AHCJGameState`: replicated `DestroyedAsteroids` (TArray<uint32>, appended to) + `DestroyedAsteroidsMatch`;
  `ServerDestroyAsteroid` adds a key and applies it; clients `OnRep_DestroyedAsteroids` -> `SyncDestroyedAsteroids` ->
  `AHCJUniverse::SyncDestroyedAsteroids` (with effects only when the universe is older than a second: a late joiner
  gets the gaps quietly). `SetMatchConfig` empties the list: a new match has every rock again.
- Logged on both sides: `Asteroids (server|client): N destroyed, checksum X` (CRC32 of the sorted keys).

## Explosions: shockwaves, debris, flares
- `Effects/HCJEffectsLogic.h` (plain C++, tested): `HCJShockwave::ImpulseSpeed / ComputeImpulse` (push by distance),
  `FHCJDebrisPool` (fixed slots, the piece nearest its end is replaced, integration, fade), `HCJEffects::ShakeAt`,
  `ScaleCount`.
- Gameplay (server): `UHCJEffectsDirector::ServerPushShips` from `AHCJProjectile::ServerDetonate` and
  `AHCJShipCharacter::Die` -> `AHCJShipCharacter::ServerApplyImpulse` -> `UCharacterMovementComponent::AddImpulse`
  (velocity change inside the movement simulation; the owning client follows by one correction). `-HCJNoShockwaves`.
- Cosmetic (every machine that draws; nothing replicated): `UHCJEffectsDirector` (world subsystem, ticks only while
  something is alive): debris as instances of a few meshes on a host actor (rock chunks, hull plates, embers; a
  bounce off rocks by one line trace per piece every other frame; black hole drift), a pool of flare quads with
  `M_HCJ_ExplosionFlare`, camera shake read by `AHCJShipCharacter::TickShipEffects` (`UHCJUserSettings::bCameraShake`).
  Called from `PlayDeathEffects`, `AHCJTorpedoBlast::Play`, `AHCJUniverse::ApplyAsteroidDestroyed`.

## Music
- `Audio/HCJMusicLogic.h` (plain C++, tested): `FHCJMusicSelector` (menu / explore / combat with hysteresis and a hold
  time), `HCJMusic::GetLoopStart`, `StepWeight`.
- `UHCJAudioDirector::TickMusicTracks`: set 0 (`UHCJUserSettings::MusicSet`), the three Lyria tracks of
  `UHCJAssetSettings` (`FHCJMusicTrack`: sound, gain, loop point, crossfade). Two voices per track; a track that fades
  out is paused. Falls back to the stems (`TickMusic`, set 1) when no track is configured.

## Online (Epic Online Services)
- `Online/HCJEosLogic.h` (plain C++, tested): `FHCJEosConfig` (validation of `[HypeCubeJam.EOS]`), `EHCJEosState`
  and its menu text, `FHCJLobbyInfo` <-> lobby attributes (`ToAttributes` / `FromAttributes`, everything read
  from a stranger's lobby is validated and cleaned before it reaches the screen or a travel URL),
  `HCJEos::PickJoinAddress`, `FHCJStatTracker` (score line -> amounts to ingest), `HCJEos::MakeLeaderboardPage`.
- `Online/HCJEOSSubsystem.h`: `UHCJEOSSubsystem`, one per game instance, game thread only, never on a dedicated
  server. Owns the EOS platform (created through `IEOSSDKManager` of the engine's EOSShared plugin, which also
  ticks it), the device-id sign-in with retry, the host's lobby, the lobby search, the stat ingest and the two
  leaderboard queries. No SDK type appears in a game header (`FHCJEosImpl` is private to the .cpp). Every request
  is asynchronous; a completion callback reaches the subsystem through a heap-allocated call object that checks
  that the subsystem is still alive and active. `Deinitialize` sends what is pending and pumps the platform for
  at most 2 s.
- Who calls it: `AHCJPlayerController::UpdateOnline` (local controller, every frame: the local score line; once
  a second: what a listen host advertises, nothing while in the title menu), `EndPlay` (flush stats, stop
  advertising), `MenuJoinLobby`; `SHCJMenu` (pages `Lobbies` and `Leaderboards`, rebuilt when
  `GetRevision` changes); `AHCJHUD::DrawIntermission` (one status line).
- Nothing of it is replicated and no gameplay depends on it. The game connection of a joiner is the engine's IP
  net driver to an address read from the lobby; EOS P2P is not used.
