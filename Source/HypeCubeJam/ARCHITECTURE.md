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
  the server is authoritative. Boost and warp ride in the saved-move flags as "this move WAS boosted / flown at warp"
  (the client's meter and drive decide; the server's copies only limit them), so the two ends never disagree about a
  move. See "Ship types, warp drive, cloak, tractor beam".
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
- Ship look: hull = `HCJShipTypes::PickHullMesh(type, chosen hull or a hash of the player id)`, skin = the chosen paint,
  else the nearest `ShipSkins` colour to `AHCJPlayerState::GetPlayerColor()`; size = `ShipMeshLength` x the type's scale;
  applied in `AHCJShipCharacter::ApplyShipLook` when the player state or the replicated type / hull / paint arrive.
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

## Cinematic look (cosmetic, every machine that draws; devlog item 26)
Nothing here is replicated, has collision or is read by gameplay, bots, aim assist or traces. Numbers: `UHCJAssetSettings`,
category "Cinematic" (`DefaultGame.ini`). Materials and textures: `docs/tools/make_cinematic_textures.py` (plain Python +
Pillow -> `SourceAssets/Cinematic/*.png`) and `docs/tools/make_cinematic_materials.py` (editor Python -> `/Game/HCJ/Cinematic`,
`M_HCJ_Nebula`, `M_Asteroid`, `M_HCJ_EngineJet`, `M_HCJ_ImpactStar`, `M_HCJ_PortalRunes`, `M_HCJ_PortalRays`).
- Sky (`AHCJSpaceBackdrop::ShowSector`): every cloud is an instance of `M_HCJ_Nebula` (6 custom floats: R, G, B, seed, core,
  soft; three samples of one noise texture, no per-pixel noise maths). Per sector: the seeded clouds of `FHCJSectorSky`
  (unchanged, tested), one backlit CORE cloud at `AHCJSpaceBackdrop::MakeBacklight(seed, sector, tint, sun)` (a pure static
  function with its own hash), `SkyHazeCards` soft cards at infinity, and `NearClouds` - soft cards that stand in the world
  600-1000 m from the sector origin (parallax against the sky at infinity). `MakeSkyColor` = sector tint, hue-shifted,
  pulled towards teal (`SkyTealMix`), the yellow band of hues folded to gold / green. `ExtraStars` made-up faint stars
  are added to the catalogue stars (same instanced draw).
- Lights (`AHCJUniverse::ApplySectorLighting(Sector)`, called by the backdrop when the view changes sector): the sun keeps
  its direction (`SunIntensity`); the fill light shines from the back light of that sector's sky in its colour
  (`FillIntensity`). The mobile renderer shades with one directional light: there the fill is missing and the rim light
  of the rock material stands in for it.
- Rocks: `M_Asteroid` (same parameters as before plus "Rim Color / Strength / Power / Dir" - a fresnel term in the
  emissive, stronger on the side of the back light - and "Crater Tiling / Strength" - a crater normal + cavity texture over
  the mesh UVs). `AHCJUniverse::CreateRockComponent` sets them per sector. Collision shapes, layout and checksums untouched.
- Decorative rocks (`World/HCJSectorDecor.h`, plain C++, tested: `HypeCubeJam.World.SectorDecor`): `FHCJSectorDecor::Make(layout,
  keep-out spheres, counts, variants)` -> pebbles (radius 7-45 uu, 70 % around the real rocks, none near portals, spawn
  points, pads, wormhole, station, black hole) and a belt of boulders wholly beyond `HCJ::SectorKillRadius + 2000`. Own hash
  of the sector seed. `AHCJSpaceBackdrop::BuildDecor` draws them for the sector the view is in only, as
  `UHierarchicalInstancedStaticMeshComponent`s without collision (forced to the last LOD; pebbles culled beyond
  `DecorPebbleCullDistance`), with `M_HCJ_DecorRock` (base colour + normal + rim; a vertex nearer than "Near Cull" to the
  camera collapses into it, so no pebble flashes across the view).
- Post process: an unbound `UPostProcessComponent` on the backdrop actor (`ApplyCinematicLook`): bloom intensity /
  threshold, vignette, contrast, saturation, shadow / highlight gain (teal and orange), optional lens flare
  (`CineLensFlare`, 0 = off). `UHCJUserSettings::bCinematicLook` (SETTINGS page "Cinematic look", on by default; read every
  frame by the backdrop's Tick): off disables the component (engine defaults, as before item 26) and keeps
  `DecorLowShare` of the decorative rocks. Sky, lights, rim light, jets and portal look are NOT switched by it.
- Engines: `EngineJetMesh` (`AHCJShipCharacter::TickShipEffects`): one quad with `M_HCJ_EngineJet` from the tail straight
  back, rolled to the view, length `EngineJetIdleLength..EngineJetBoostLength` hull lengths by speed; the own jet is
  `EngineJetOwnShare` of that (it points at the chase camera). Colours as the glow: enemies red, the own ship its own.
- Hits: `UHCJEffectsDirector::PlayImpactStar(location, scale, colour)` - a pool of `MaxImpactStars` quads with
  `M_HCJ_ImpactStar` (core, halo, radial streaks that fly out). Called from `AHCJProjectile::PlayImpactEffects`,
  `AHCJShipCharacter::TickPhaserEffects` (every `PhaserSparkInterval`) and `StartShieldFlare` (on the bubble's surface).
- Enemy glint: `M_HCJ_ShipGlint` is "alpha composite" now; its `Backing` parameter (`ShipGlintBacking`) darkens the sky
  behind an enemy's glint before the light is added, so the red point stays red against a bright nebula.
- Development: `HCJMenuCmd cinematic <0|1> | fly <0|1|2> | impact [uu] | enemyfire <0|1>[,<ship>[,<target ship>]]`,
  `scene <sector>,backlight`.

## Portals (cosmetic part)
- `AHCJPortal::InitPortal`: ring blocks (`FrameMesh`), turning glyphs and lip (`GlyphMesh`), the swirl quad (`DiscMesh`
  with `M_HCJ_PortalSwirl`, colour = tint of the destination sector), a far glow quad (`FarGlowMesh`, the ship glint
  material). `Tick` (TG_PostUpdateWork, returns at once when the local view is in another sector) turns the glyphs,
  fades the passage flash and sizes the far glow. `AHCJPortal::NotifyPassage(location)` (static, every machine) is called
  from `AHCJShipCharacter::TickShipEffects` when a ship's position jumps by a sector, for both ends. Wormhole mouths
  (`InitWormhole`) keep core + swirl card and share the far glow and the flash. Trigger and transform are untouched.
- Item 26: the ring is heavier and darker; `RuneMesh` (44 instanced quads with `M_HCJ_PortalRunes`, one of sixteen
  glyphs of a script-made atlas each, picked by a hash of the two sectors; they lie on both faces of the ring and turn
  against the lip), `RayMesh` (one quad in the portal's plane with `M_HCJ_PortalRays`: rays radiating from behind the
  ring) and five thin loops of light in `M_HCJ_PortalSwirl` (parameter `Filaments`). All in the destination colour; the
  passage flash drives runes and rays too.

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
  server never reads it from the client). Quad -> damage multiplier at spawn. The Cloak bit is the cloaking device's now (not a timed power-up; a CLOAK pad
  refills its meter): see "Ship types, warp drive, cloak, tractor beam".
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
- Tactics (devlog item 27; all in `AHCJBotController`, scaled by `FHCJBotSkill::Tactics` = 0.25 / 0.7 / 1 on easy /
  normal / hard: the chance, rolled once per ship, that it is flown "smart", and again per occasion). What a bot did
  is counted by `AHCJGameMode::NotifyBotTactic(EHCJBotTactic)` (the `Tactics t=` status line, the `bot_*` keys of the
  `Playthrough:` lines):
  - `PickTarget`: score of every hostile in sight (distance, hull lost, the facing it shows us weak or down, it is
    the ship shooting at us - more when it has a weak side of ours -, its tail towards us, minus 2.5 for every other
    bot already on it, a bonus for the current target). A bot that is not smart keeps the old rule (nearest).
  - `ThinkDisengage` / `TickDisengage`: the facing towards the attacker is down, the hull below the retreat line of
    the difficulty (+ 5 / 12 points for interceptor / scout) and there is a way out - a rock within 50 m to get
    behind (`AHCJUniverse::FindCoverPoint`), the cloak, a repair / shield pad: away under boost for at most 7 s, fire
    held, `TickShieldFacing` turns the best facing to the attacker; back when the shields are above 55 % and nobody
    hit it for 2.5 s, or the attacker is gone. Without a way out it fights on. The old retreat (hull low, a repair
    kit in the sector) is unchanged and counted as the same tactic.
  - `ThinkTorpedo`: besides `HCJTorpedo::ShouldBotFire`, a torpedo at once through a facing that is DOWN
    (`FHCJBotSkill::ComboChance` per decision); a smart bot holds the target with the tractor beam first when it is
    in reach. `AHCJGameMode::NotifyTorpedoThroughOpenFacing` + the kill in the same instant = "open-facing torpedo kill".
  - `FHCJBotStyle` (a table in the .cpp, one row per ship type, used by smart bots only; the ship type table of
    `Ship/HCJShipTypes.h` is untouched): range kept, strafe share, readiness with torpedoes / tractor / cloak-to-flee,
    extra retreat line, `bKnife` (boosted 1.8 s passes at 15 m, every 12-18 s, around a hull that turns slower: interceptor, fighter,
    scout), `bAmbush` (scout under cloak flies to the tail of its target and drops the cloak at 26 m), `bVulture`
    (scout: damaged targets first, no fair fight with a heavy hull).
  - Surroundings: `ThinkRockShot` (a rock of at most 140 hit points within 45 m on the line to a hidden target is
    shot with the phaser for up to 3.2 s), the station's reach (`StationDangerRadius`, applied in `SteerTowards`
    unless the bot attacks the turrets), warp towards a hostile far away (`bChasing` / `bWarpChase`).
  - `DescribeForLog`: the bot's state as key=value tokens, written by the game mode when its ship is stuck.

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

## Playthrough tests (devlog item 27)
Three layers, all reading the same server-side facts:
- **In the log** (`AHCJGameMode`, server): `Playthrough: event=<name> key=value ...` lines - `match_start` (seed,
  layout checksum, difficulty, limits), `match_end` (winner, kills, reason), `board` (one per row of the end-of-match
  board), `stats` (every counter of the running match: kills by weapon and ship type, jumps, pickups by item, turrets,
  rocks and the destroyed-set checksum, systems, bot tactics, server frame times; every 15 s and at the end), `human`
  (score line of every human), and warnings `stuck` (a bot's ship moved less than 2.5 m in 20 s; with
  `AHCJBotController::DescribeForLog`), `out_of_bounds` (alive beyond the kill radius for 3 s), `nan`. Clients write
  `event=client` every 10 s (layout, destroyed set, own score, movement corrections received).
  `AHCJGameMode::UpdatePlaythroughWatch` runs once a second; `Tick` measures the server's frames.
- **In the automation suite** (`Private/Tests/HCJPlaythroughTests.cpp`, `HypeCubeJam.Playthrough.*`): the engine's
  `FTestWorldWrapper` with the game's URL options - a real game world, game mode, universe and bot controllers, ticked
  by hand at 1/30 s (three minutes of match in about 20 s), asserted on through
  `AHCJGameMode::GetPlaythroughSnapshot`. No network, no local player. The online subsystem is off whenever
  `GIsAutomationTesting` is set.
- **As processes** (`docs/tools/playthrough.py`): starts headless matches (editor binary `-game -nullrhi`, or the
  packaged build), waits for them to quit by themselves (`-HCJQuitAfter`), parses the lines above and prints a
  pass / fail table; exit code 1 on a failure. Scenarios: a match to the kill limit and the next one, each
  difficulty, a crowded four-sector universe (turrets, rocks), host + client + late joiner with a leave and rejoin,
  the autopilot player through hangar and a whole match, and the sum over all matches (every weapon, turrets, rocks).

## Tests (group A, `Private/Tests/`, `EditorContext | ProductFilter`, names `HypeCubeJam.*`)
Graph connectivity for dims 2..6 over many seeds; edge symmetry; determinism of graph and layout; BFS tables
consistent; every open edge has a portal at both ends and they mirror; asteroid/portal/spawn clearances; golden
digests; shield facings (`HypeCubeJam.Shield.*`: facing selection, absorb and overflow, per-facing regeneration,
overcharge spread, replication round trip, facing to present, the hint); photon torpedoes (`HypeCubeJam.Torpedo.*`:
stock and reload, damage against a facing up / down, arming and blast falloff, homing turn limit and lock, blast
facing and the bots' rule, HUD rules incl. markers clear of panels); phasers (`HypeCubeJam.Phaser.*`: bank, falloff / beam byte / aim
assist / design targets, weapon slots); asteroids (`HypeCubeJam.Asteroids.*`: split determinism and pieces inside the rock, replication
round trip in any order); effects (`HypeCubeJam.Effects.*`: shockwave falloff, debris caps); music (`HypeCubeJam.Audio.MusicTracks`); online logic (config validation and the unavailable states, lobby attribute packing / parsing, join
address, stat tracker, leaderboard page, `HypeCubeJam.Online.LobbyFilter`: which lobbies are offered and in which order,
`HypeCubeJam.Online.QuickPlay`: every path of the quick-play state machine); playthroughs (`HypeCubeJam.Playthrough.*`: a match to the frag
limit and the next one on a new layout; three minutes of an open match - see "Playthrough tests"); ship systems (`HypeCubeJam.Systems.*`: warp state machine, warp and boost followed
by the server, tractor pull, cloak device) and ship types (`HypeCubeJam.ShipTypes.*`: table, server validation); the intro (`HypeCubeJam.UI.IntroFlow`: every way it ends, title and fade
timing; `HypeCubeJam.UI.IntroCommandLine`: which command lines play it); decorative rocks (`HypeCubeJam.World.SectorDecor`: determinism, keep-outs, belt beyond the kill radius, layout untouched); rock variant and visual transform envelope; ship hull/skin selection; sector sky; black hole placement
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
| `Public/World/HCJSectorDecor.h` | A |
| `Public/Assets/HCJAssetSettings.h` | shared |
| `Private/Tests/*.cpp` | A |
| `Public/Ship/HCJShipCharacter.h` | B |
| `Public/Ship/HCJShipMovementComponent.h` | B |
| `Public/Ship/HCJHealthComponent.h`, `Public/Ship/HCJShieldFacings.h` | B |
| `Public/Ship/HCJShipTypes.h`, `Public/Ship/HCJShipSystems.h` | B |
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
- Boost meter: simulated in `UHCJShipMovementComponent::UpdateCharacterStateBeforeMovement` on the owning client
  (`HCJBalance::StepBoost`); the server follows the client's "this move was boosted" flag and keeps its own meter as
  the limit (`StepBoostFollow`); carried in the saved moves; not replicated.

## Ship types, warp drive, cloak, tractor beam
- `Ship/HCJShipTypes.h` (plain C++, tested): `EHCJShipType` (interceptor, fighter, gunship, scout, cruiser) and the ONE
  table `FHCJShipTypeDef` (flight model, hull, shield facing, fore armour, phaser / bolt scales, torpedoes, cloak, warp,
  tractor numbers, hull mesh range, size). `HCJShipTypes::Sanitize*` validate a client's choice, `PickHullMesh`,
  `PickBotType`, `GetStat` (hangar bars). Code, not config: prediction needs identical numbers on client and server.
- Choice: `UHCJUserSettings::ShipType / ShipVariant / ShipSkin` (hangar page of `SHCJMenu`; preview actor in
  `AHCJPlayerController::UpdateHangarPreview`) -> `AHCJPlayerController::ServerSetShipChoice` (reliable; once per
  controller and from the hangar) -> `AHCJGameMode::SetShipChoice` (validates; a ship still under its spawn shield is
  changed in place) -> `AHCJPlayerState::ShipType / ShipVariant / ShipSkin` (replicated; scoreboard, player colour) ->
  `AHCJGameMode::RestartPlayer` -> `AHCJShipCharacter::ServerInitShipType` -> replicated `ShipType / ShipVariant /
  ShipSkin` on the ship -> `ApplyShipType` on every machine (movement component, capsule / hit sphere / bubble size,
  `UHCJHealthComponent::ChangeMaxValues`, look). Bots: `AHCJGameMode::SpawnBot` (`-HCJBotType=<n>`, `-HCJClassic`).
- `Ship/HCJShipSystems.h` (plain C++, tested): `FHCJWarpDrive` (`Step` for a ship that decides for itself, `StepFollow`
  for the server's copy of a remote client's drive, `Cancel`), `HCJWarp::ShouldDropOutAtEdge`; `FHCJCloakDevice`
  (meter, lock, reveal), `HCJCloak::*`; `HCJTractor::CanHold / ComputePull / ShouldBreak`. `HCJBalance::StepBoostFollow`
  is the same idea for the boost meter.
- Warp lives in `UHCJShipMovementComponent`: key = `FLAG_Custom_1`, "flown at warp" = `FLAG_Custom_2` (set in the saved
  move's `PostUpdate`), the drive's state is kept in every saved move; `UpdateCharacterStateBeforeMovement` steps it,
  `CalcVelocity` gives the fixed warp velocity, `ControlledCharacterMove` makes the thrust the nose around the jump,
  `ShouldWarpDropOut` (sector edge, sphere sweep against WorldStatic), `HandleImpact` -> `ConsumeWarpImpact` (server:
  damage). The ship: `WarpStateRep` (everybody, effects), `WarpCancelCount` (owner: `ServerCancelWarp` -> the
  prediction stops too), `ServerTickSystems`.
- Tractor beam: `AHCJShipCharacter::TractorTarget` on the holder and `TractorHolder` on the victim (both replicated to
  everybody; no RPC but the key, `ServerSetTractor`). The victim's `UHCJShipMovementComponent::SetTractor` (from
  `OnRep_TractorHolder`, server and clients) slows it (`GetMaxSpeed`) and pulls it in `PhysFlying`
  (`HCJTractor::ComputePull`); the owning client uses the holder's replicated position - small corrections while held.
  It drains the holder's `PhaserBank`. `ServerTickSystems` starts, keeps and breaks it; `ServerReleaseTractor`.
- Cloak: `FHCJCloakDevice` on the server (`ServerSetCloak`), `EHCJPowerup::Cloak` + `CloakRevealed` in `PowerupFlags`
  (everybody), `CloakEnergyRep` (owner). `IsCloakHidden()` is what every "can it be seen" test asks (HUD markers, radar,
  target lock, phaser assist, missiles, torpedoes, turrets, bots, the player state's sector). Shields:
  `UHCJHealthComponent::SetShieldsSuppressed`. Weapons: `ServerWeaponsBlocked` / `AreWeaponsOffline`. Look:
  `ApplyCloakLook` swaps every hull material for `UHCJAssetSettings::CloakMaterial` (`CloakSkinState`: another ship's
  shimmer, the own ship half transparent, revealed flicker); `bCloakLook` keeps glint, engine glow, trail and sounds off.
- Looks (`TickSystemEffects`, every machine that draws, nothing extra replicated): warp streak quad on the own camera
  (`WarpStreakMaterial`), FOV kick, engine flare while charging, long trail at warp, `UHCJEffectsDirector::PlayWarpFlash`
  where a ship jumps and drops out; the tractor beam quad (the phaser beam material in blue-white).
- HUD: `AHCJHUD::DrawSystems` (lines above the crosshair), the WARP / CLOAK / TRACTOR rows of `DrawLoadout`, the type in
  `DrawTargetPanel` / `DrawScoreboard` / `DrawKillFeed` (`AHCJGameState::MulticastKillEvent` carries both types in one
  byte), hints `EHCJHint::TractorCaught / Warp / Cloak / Tractor`.
- Bots: `AHCJBotController::ThinkSystems` / `TickSystems` (chances in `FHCJBotSkill`).
- Game mode statistics (`Systems t=` log line): warps, cloaks, reveals, tractor holds / breaks, kills / deaths by ship type.
- Development: `-HCJQuitAfter=<s>` (the process ends by itself), `-HCJBotsOnly` (the local player has no ship: a headless
  bot match as a plain standalone game, no network), `-HCJClassic`, `-HCJBotType=<n>`, `HCJMenuCmd warp | cloak | tractor |
  ship | enemyship | enemywarp | enemycloak | enemytractor | hangar`.

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
- Quick Play: `FHCJQuickPlay` (in `HCJEosLogic.h`: phases SigningIn -> Searching -> Joining -> Announcing, pure,
  tested) lives in the subsystem so that it survives the world change of a join. `AHCJPlayerController::MenuQuickPlay`
  starts it, `UpdateQuickPlay` (every frame of the local controller) feeds it the state of the service, the result of
  the search it asked for (`GetSearchSerial`) and whether the join failed (`HCJMenuFlow::TakeFailure`), and does what
  it returns: `RefreshLobbies`, `ClientTravel` to the address, or `MenuPlay`. A controller that finds itself on a
  client while Quick Play is active reports the join as done. Menu page `EHCJMenuPage::QuickPlay` shows the live status.
- Lobby hygiene: `HCJEos::ClassifyLobby` / `FilterJoinableLobbies` (version, free slot, address, age of the `HCJTIME`
  attribute) decide what `GetLobbies` returns - for FIND GAMES and Quick Play alike; the host rewrites its attributes
  every `HCJEos::LobbyHeartbeatSeconds`.
- Nothing of it is replicated and no gameplay depends on it. The game connection of a joiner is the engine's IP
  net driver to an address read from the lobby; EOS P2P is not used (devlog item 27 says what was looked at).
