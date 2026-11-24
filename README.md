# HypeCubeJam

Multiplayer free-for-all space shooter for Unreal Engine 5.9 (C++; looks and sounds from Fab packs, see "Assets").

**Play it:** https://mariuz.itch.io/hypecubejam (Windows build), or in the browser at
https://mariuz.github.io/HypeCubeJam/ (experimental: bots only, needs WebGPU, about 250 MB on the first visit;
served from the `gh-pages` branch). Trailer: https://youtu.be/iwO5n33EsUc. Made for the 2026 Epic AI MegaJam by one person
directing AI coding agents (Claude Code).

**About this repository:** it holds the C++ source, config, docs, build scripts and the project's own generated
materials. It does **not** hold the third-party asset packs (Fab licences do not allow redistributing their files)
or the Epic Online Services client secret, so a clone compiles but shows fallback shapes and has no sound until the
packs listed in `docs/ASSETS.md` are added to `Content/`, and online play needs your own credentials in
`Config/DefaultEngine.ini`, section `[HypeCubeJam.EOS]`.

Every level is a **Sector**: a vertex of an N-dimensional hypercube (default N = 4, a tesseract with
16 sectors). A sector id is a bitmask; neighbours differ in exactly one bit. Portals link sectors along
a random, always connected subset of the hypercube edges. The portal graph and the contents of every
sector (asteroid field, spawn points, portals, tint) are generated from one match seed, which is the only
thing the server replicates: each machine builds identical geometry locally. All sectors live in a single
persistent world at far-apart origins, so travelling through a portal is a teleport, not a map change.

Bots (`AHCJBotController`, flying the same `AHCJShipCharacter` as players) fill the match up to a minimum
number of combatants and are removed as humans join.

## Layout

```
Source/HypeCubeJam/Public|Private/
  Hypercube/   graph + sector layout generators (plain C++, no UWorld)
  World/       sector/portal actors built from the seed
  Ship/        ship character, projectile, health
  Game/        game mode/state, player state/controller, HUD, user settings, UI logic
  UI/          the Slate menu widget, the hint logic (Public/UI/HCJHints.h) and the hint / banner part of the HUD
  Online/      Epic Online Services: quick play, lobby browser, stats, leaderboards (UHCJEOSSubsystem, HCJEosLogic.h)
  Effects/     explosions: shockwave rule, debris pool, flares, camera shake (HCJEffectsLogic.h, UHCJEffectsDirector)
  Assets/      UHCJAssetSettings: the one place that names every authored asset
  AI/          bot controller
  Audio/       local music and cue director
  Private/Tests/   automation tests (HypeCubeJam.*), two of them whole matches (HCJPlaythroughTests.cpp)
docs/tools/playthrough.py   playthrough runner: whole matches as processes, judged from their logs (see "Tests")
```

## Build

Engine: `F:\EpicGames\UE_5.9` (the `EngineAssociation` in the `.uproject` is this install's build id).

```powershell
& "F:\EpicGames\UE_5.9\Engine\Build\BatchFiles\Build.bat" HypeCubeJamEditor Win64 Development -Project="F:\work\HypeCubeJam\HypeCubeJam.uproject" -WaitMutex
```

## Run

There is no project map; the game runs on `/Engine/Maps/Entry` with `HCJGameMode` as the global default.

```powershell
$Editor  = "F:\EpicGames\UE_5.9\Engine\Binaries\Win64\UnrealEditor.exe"
$Project = "F:\work\HypeCubeJam\HypeCubeJam.uproject"
# game/server runs of the editor binary would also start the MCP server; keep port 8000 for the editor itself
$NoMcp   = "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocol.ModelContextProtocolSettings]:bAutoStartServer=False"

# listen server (plays too); with nobody else connected the bots are your enemies
& $Editor $Project "/Engine/Maps/Entry?listen" -game -log -windowed -ResX=1280 -ResY=720 $NoMcp

# client
& $Editor $Project 127.0.0.1 -game -log -windowed -ResX=1280 -ResY=720 $NoMcp

# dedicated server (bots only until someone joins)
& $Editor $Project "/Engine/Maps/Entry" -server -log $NoMcp
```

Started without a map URL or address (`UnrealEditor.exe <project> -game`, or the packaged exe without arguments)
the game opens on the **title menu** and the bots play behind it; see "Menu and HUD". A map URL, an address,
`-AutoHost`, `-server`, `-nullrhi`, `-HCJCapture=` or `-HCJFrameDump=` on the command line skip the menu, so every
command above and every capture run behaves as before. `-HCJMenu` forces the menu in a capture run.

URL options (append to the map URL of the server):

| Option        | Meaning                                                        |
|---------------|----------------------------------------------------------------|
| `?Seed=<int>` | fixed match seed (default: random per match)                   |
| `?Dim=<2..6>` | hypercube dimension, i.e. 2^Dim sectors (default 4)            |
| `?EdgeP=<0..1>` | chance that a non-spanning-tree edge is open (default 0.5)   |
| `?Bots=<n>`   | minimum combatants; bots fill the rest (default 8)             |
| `?MinBots=<n>` | bots that stay however many humans join (default 0)           |
| `?Difficulty=<0..4>` | bot difficulty: 0 easy (default), 1 normal, 2 hard, 3 ace (hard without the fairness rules), 4 super easy |
| `?FragLimit=<n>` / `?TimeLimit=<s>` | match end (defaults 25 kills / 600 s; 0 disables) |
| `?BlackHoles=0|1` / `?BlackHoleP=<0..1>` | black holes on/off, share of sectors with one (default on, 0.25) |
| `?Pickups=0|1` | pickup pads on/off (default on) |
| `?Stations=0|1` | space stations with turrets on/off (default on) |
| `?Wormholes=<n>` | number of wormholes; 0 = none (default: a quarter of the number of sectors) |

Example: `"/Engine/Maps/Entry?listen?Seed=1234?Dim=3"`.

The defaults are config properties of `AHCJGameMode` (`Source/HypeCubeJam/Public/Game/HCJGameMode.h`,
`[/Script/HypeCubeJam.HCJGameMode]` in `DefaultGame.ini`).

The server log (`LogHCJ`) reports the match seed, every universe build with a `layout checksum` (a client
logs the same value when it generated the same universe), bot/player joins and leaves, kills, and a
`Status` line every 15 s (ships, shots, hits, kills, portal jumps, black hole deaths). With it a `Balance` line (time
to kill, kills per minute, hit rate, damage per kill, hits that stayed on a shield / reached the hull, facings
collapsed). `-LogCmds="LogHCJ Verbose"`
adds portal jumps and bot state changes. For a headless run use `-nullrhi`.

Controls: W/S/A/D thrust, Space/E up, Ctrl/Q down, mouse aim, left mouse fire, right mouse (or X) photon torpedo,
Shift boost, G drop pickup weapon, T next target, F nearest enemy, middle mouse holds the target, V camera, Tab scoreboard, F1 controls overlay,
H hints on / off, hold Z warp drive, C cloak on / off, hold R tractor beam (R/Enter/fire respawn when destroyed), Esc menu.
A game controller works as well: see "Game controller (Xbox pad, Steam Controller)".

## Game controller (Xbox pad, Steam Controller)

An Xbox-style (XInput) pad works next to keyboard and mouse - both at the same time, nothing to choose. SETTINGS has
**Controller (Xbox / Steam Controller) On / Off** (on by default; off = the game takes no gamepad key at all, in the
match or in the menus) and **Controller sensitivity** (0.2 x to 3 x: the turn rate of the aim stick). Both are saved in
`UHCJUserSettings` (`bController`, `ControllerSensitivity`).

| pad | does | keyboard / mouse |
|---|---|---|
| left stick | thrust forward / back, strafe left / right (dead zone 0.2 per axis) | W S A D |
| right stick | aim: the ship flies where you look | mouse |
| A / B | up / down | Space / Ctrl (E / Q) |
| RT | fire; hold for the phaser beam (also: respawn when destroyed) | left mouse |
| LT | photon torpedo | right mouse, X |
| LB (hold) | boost | Shift |
| RB | next target | T |
| Y | target the nearest enemy | F |
| X | switch phaser / blaster | B |
| D-pad up (hold) | warp drive | Z |
| D-pad left | cloak on / off | C |
| D-pad right (hold) | tractor beam on the target | R |
| D-pad down | drop a pickup weapon | G |
| right stick click (R3) | first / third person | V |
| left stick click (L3) | controls overlay | F1 |
| Back / View (hold) | scoreboard | Tab |
| Start / Menu | in-match menu | Esc |
| A | respawn now, when destroyed | R, Enter |

Not on the pad: hints on / off (H; also in SETTINGS), "hold the target" (middle mouse; the soft lock on the enemy
nearest the crosshair works as always), typing a seed, a pilot name or an address.

- Aim stick (`HCJPad::LookDelta`, `Game/HCJPadLogic.h`, tested): radial dead zone 0.2, the rest rescaled to 0..1 and
  squared (fine aim near the centre), 150 degrees a second at full deflection times the sensitivity, **times the frame
  time** (capped at 0.1 s) - a stick is a rate, the mouse a step per count, so the mouse path is untouched. Stick up is
  nose up (no invert option). No gameplay value changed; the aim assist is the one the mouse has.
- Bindings: a second Enhanced Input mapping context (`IMC_HCJ_Gamepad`, built in code in
  `AHCJPlayerController::BuildInputObjects` like the keyboard's) that maps pad keys to the same actions plus six of its
  own. `SyncPadContext` registers it exactly while the SETTINGS switch is on. The triggers are bound as the buttons the
  engine makes of them at its threshold, not as axes.
- Menus with a pad (and with the arrow keys): D-pad or left stick up / down moves a yellow focus frame through the
  buttons, steppers and sliders of the page (it wraps round), left / right changes a stepper or slider (or moves the
  focus on a button), **A** presses (a stepper steps forward), **B** or **Start** goes back (out of the in-match menu,
  one page back elsewhere; nothing on the title page). The title page opens with the focus on PLAY, so **A = PLAY**;
  SETTINGS and HANGAR open on their first row; the in-match menu on RESUME. The frame shows once a pad or an arrow
  key was used and goes away when the mouse moves. Text boxes (seed, pilot name, address) are mouse and keyboard only:
  there is no on-screen keyboard. `SHCJMenu::Navigate`; the menu widget keeps Slate's keyboard focus itself (its
  buttons are not focusable any more), so Tab now moves this focus instead of Slate's.
- Prompts: while the pad is the device used last (`AHCJPlayerController::IsPadPrompts`: any pad button, or a stick /
  trigger pushed past half; any key or mouse movement switches back) the key caps of the HUD - hints, the weapon and
  system rows, the respawn line, the F1 overlay - and the legend lines of the menu show pad names ("RT" for
  "Left mouse"; `HCJPad::ToPadMarkup`). The CONTROLS page always shows both, the pad in a third column.
- The respawn and end-of-match screens need no menu: A or RT respawns, the scoreboard of the intermission shows itself
  and the next match starts by itself. The intro is skipped by any pad button.

**Steam Controller (2015).** It has no Windows driver of its own; Steam Input turns it into something a game
understands. Two ways:

1. *As a gamepad (recommended).* In Steam: Games > "Add a Non-Steam Game to My Library..." > Browse >
   `HypeCubeJam.exe`. Start the game from Steam (Steam must be running, the controller switched on). In the
   controller settings of the game (the gamepad icon on its library page, or Steam button > Controller settings while
   it runs) choose the **Gamepad** template (or "Gamepad with Camera Controls" for the right pad as a stick with
   mouse-like feel). The game then sees an Xbox pad and the table above applies: left stick, right touch pad = right
   stick, the four face buttons, bumpers, triggers, the left touch pad = D-pad, Back / Start, stick click and
   right-pad click = L3 / R3. The grips can be given any button in Steam (boost or up / down sit well there).
2. *As mouse and keys.* Without adding the game, Steam's desktop configuration (Steam > Settings > Controller >
   Desktop Layout) makes the right pad the mouse and the buttons keys; bind them to the keyboard controls above.
   SETTINGS > Controller does not matter for this: the game sees a mouse and a keyboard.

The game uses no Steamworks code and needs no Steam to run. Other pads work when Windows or Steam presents them as
XInput (an Xbox One / Series pad directly; a DualShock / DualSense through Steam Input).

Development: `HCJMenuCmd padtest` presses every pad key through `APlayerController::InputKey` (no hardware) and logs
what happened (`PadTest: step=rt_fire ... ok=1`, at the end `PadTest: result=PASS steps=30 checks=36 failed=0`);
`HCJMenuCmd nav <up|down|left|right|accept|back>` does in the open menu what the pad does and logs the entry that has
the focus; `HCJMenuCmd pad <0|1>` is the SETTINGS switch. Not in shipping builds. What was and was not tried with a
real pad: docs/DEVLOG_NOTES.md, item 36.

## Menu and HUD

All of it is C++ (no widget assets): the menus are one Slate widget (`Private/UI/HCJMenu`), the HUD is drawn on the
canvas (`AHCJHUD`, in `HCJHUD.cpp` and `HCJHUDReadability.cpp`) with the engine's Slate font at the size shown.
Menu sizes follow the engine's DPI scaling of viewport widgets, HUD sizes `HCJUi::ComputeUIScale` (1 at 1920x1080,
0.67 at 1280x720; text never below 0.8).

- Title menu: the first row is **Difficulty** (Easy / Normal / Hard / Ace) with one line under it that says what the
  level does to the player ("One bot at a time, from the front. You start armed, your aim is helped." on Easy, "Bots go easy on you, your aim
  is helped a lot, you start armed." on Super Easy, the first entry; see
  "Bots and people: the fairness rules"). Then QUICK PLAY (first button: joins an open match found through Epic Online Services, hosts one when there is
  none - see "Online"), PLAY (host with bots - one click; Enter), HANGAR (ship type, hull and paint with a turning 3D preview and stat
  bars; see "Ship types, warp drive, cloak, tractor beam"), bots 0-15, hypercube 3D-5D, seed, pilot name, JOIN
  by address, FIND GAMES and LEADERBOARDS (see "Online"), SETTINGS (mouse sensitivity, controller on / off and its
  sensitivity, music / effects / interface volume), QUIT. The choices are kept in the
  user's `GameUserSettings.ini` (`UHCJUserSettings`). The host has no ship while in the menu and does not count as
  a combatant; PLAY starts a fresh match (`AHCJGameMode::StartMatchFromMenu`).
- Esc in a match: RESUME, "Bot difficulty (applies now)" with the level's one-line description (host and single
  player only: the running match changes level at once and the setting is saved), HANGAR (the next ship), SETTINGS,
  CONTROLS, LEAVE TO MENU, QUIT GAME. The match is not paused (it is a network
  game). A client that leaves, loses the host or fails to connect ends up in the title menu with a line saying why.
  Every page has a line of key caps (Enter = the big button, Esc = back, F1 = controls; with a pad in the hand the
  pad's: D-pad / left stick move, A select, B back); CONTROLS is a three-column
  page of all keys, of what the HUD marks mean and of the game controller's buttons; SETTINGS also has "In-game hints On / Off" and "Computer voice (spoken warnings) On / Off".
- F1: the same controls page as an overlay in the match (and by itself for the first 8 s of the first ship when
  hints are off).
- Hints (`UI/HCJHints.h`, `Private/UI/HCJHUDHints.cpp`; local, nothing replicated): one panel at the bottom centre,
  key caps drawn as boxes, a thin line for the time left. `FHCJHintDirector` (plain C++, tested, one per process)
  decides what is up: first the onboarding prompts in order (move, fire, boost, target keys), each until the
  player does it or 12-14 s pass; then contextual hints by priority, each at most 1-3 times per session with a
  cooldown: black hole and low hull (urgent: they interrupt), new item (once per item type), station turrets,
  boost empty, wormhole, portal, pad, empty sector, the goal. On a low hull a green marker / edge chevrons lead to
  the nearest repair pad (drawn with the pad markers, also with the hints off; see "Pickups, power-ups and weapons"). After a death the respawn screen says who or what did it and gives one tip. H or the
  settings page switches hints off (`bHints` in `UHCJUserSettings`).
- Layout: top left sector, sky, `KILLS n / limit`, the objective line (`HCJHints::MakeObjectiveText`: who leads, by
  how much), clock; top centre for 4.5 s after entering a sector a banner with its id and contents (black hole,
  station and live turrets, wormhole, portals, enemies) and a short flash in the sector colour after a jump; bottom
  left the radar on top of a block with the own ship inside four arcs (the shield facings: fore at the top, aft
  below, port left, starboard right; filled by strength, blue-white to orange, a white flash on a hit, red and
  blinking when down, a thin outer arc for an overcharge), the sum or which facing is down, the four numbers, and
  labelled HULL / BOOST bars with numbers; bottom centre speed and
  hints; bottom right a WEAPON panel (name, ammunition number and bar - for the phaser its bank with the relock
  mark - `[G]` / `[B]`, the TORPEDOES block - one pip per
  torpedo the ship can carry, READY / LOADING / EMPTY, a reload bar - and power-up timers with draining bars).
  Enemy brackets and labels are never drawn on a panel: every panel notes its rectangle, and an enemy whose marker
  would lie on one gets an edge arrow like the enemies out of view (`HCJUi::IsMarkerClear`).
  The scoreboard, the F1 page and the big centre lines are never drawn through each other.
- Enemies: every enemy within 600 m gets a bracket in the hostile colour (red) with name, distance, hull and
  shield; it has a minimum size, fades a little with distance and is dimmed, not removed, behind a rock. Solid red
  arrows at the screen edge point to the six nearest enemies out of view (the one shooting at you pulses). A ship
  beyond 15 m also carries a glowing point in its player colour whose size on screen is constant (the "beacon"),
  so a distant ship reads as a light. Cloaked ships get no bracket, arrow, beacon, radar dot or map mark.
- Targeting: the enemy nearest the crosshair is the target (soft lock with a little stickiness,
  `HCJUi::PickTarget`): orange diamond, lead circle joined to it by a line; the circle turns green with "FIRE"
  when the guns are on the lead point. T = next target, F = nearest enemy, middle mouse = hold / release the
  target. A panel on the right gives name, shield, hull, distance, closing speed, whether the weapon reaches, and
  the target's four shield facings (its nose up) with a mark for where you are around it and a line naming the
  facing towards you and its strength ("DOWN: hull open" when shots from here reach the hull).
  The lock is sent to the server and homing missiles steer for it; their victim sees "MISSILE LOCK" and hears a
  cue. Hits: the bracket flashes white and a damage number floats up (from the replicated hull + shield).
- Aim assist: bolts hit a ship within `AimAssistRadius` (100 uu, `[/Script/HypeCubeJam.HCJGameplaySettings]`) of its
  centre instead of the 60 uu of its movement collision - an extra query-only sphere on every ship, the same on
  server and clients, that only projectiles collide with. 0 switches it off.
- Radar (bottom left, above the bars): the sector from above with forward up; enemies as red squares with a tick
  for above / below, portals as squares, wormhole, station, black hole and filled pads (a green cross for health, an
  amber diamond for a weapon, a magenta triangle for a power-up); over it "ENEMIES IN
  SECTOR: n" or, when there is none, the sector of the nearest enemy and the portal or wormhole that leads there
  (route table), which is also marked "ENEMIES THIS WAY" in the world or by a chevron at the screen edge.
- Also: spread circle on the crosshair, an arc showing where damage came from, a red vignette on a low hull.
- Threats from behind (all from data every machine has; nothing new is replicated):
  - **ENEMY BEHIND - name - distance** (yellow, top of the screen, with a warning cue at most every 4 s): an enemy
    within 90 m behind the view has had its nose within 12 degrees of the own ship (`HCJUi::IsAimingAt`) for 0.3 s
    (`AimWarnDwellSeconds`: a nose that sweeps past is not a warning) - it is about to shoot, or shooting. The cue
    is the warning tick a fifth lower than the torpedo's and the missile's (`AimWarnCuePitch`). The same ship has a pulsing "!" edge arrow (it comes first among the arrows) and a yellow
    ring around its radar blip, also when it is not behind. Bots hold their fire for a moment when they get behind
    a player (below); this is the warning for that moment. "MISSILE LOCK" has the place on the screen when both apply.
  - **HIT FROM BEHIND**: a hit from the view's rear arc (more than about 107 degrees off the view direction) draws a
    wider, heavier, three-line arc at the bottom of the ring for 2 s with that label, so that it cannot be taken
    for a hit from below.
  - The respawn screen says where the last hit came from: "Destroyed by Bot-03 with the PHASER - from behind" (or
    "from the side", "head-on"), and for "from behind" the tip is about the radar and the "!" arrows.
- Development console command `HCJParkEnemy <distance> [right] [up]` (listen host) puts the nearest other ship in
  front of the own ship, for pictures.
- World: destination labels on portals and wormholes.
- **Hypercube map** (top right; `Private/Game/HCJHUDMap.cpp`). The sectors are the corners of cubes: one cube for 8
  sectors, two side by side for 16, four (two by two) for 32, seen a little from the right and from above; near
  corners are larger and brighter, lines at the back thin and dim, and the cubes sway slowly (4 degrees). A portal of
  one dimension always runs in the same direction on the map.
  - **YOU**: a white ring with a slow halo on the own sector; after a jump (or a new ship) rings run out from it,
    the mark slides along the line it travelled and that line lights up for a second.
  - **One jump away**: thick lines from the own sector to a diamond with the sector id - the same text as the
    in-world "PORTAL > 0101" marker, in the colour of that marker; violet with a round ring for the far end of the
    own sector's wormhole.
  - **Enemies**: a red badge with their number on every sector that holds some (the own sector counts what the radar
    counts).
  - **Target**: yellow corner brackets, the id and the way there hop by hop in yellow - the sector the HUD is
    guiding to ("enemies in 0011: take PORTAL > 0111"), or red brackets on the sector of the locked ship.
  - **Nearest fight**: a pulsing red ring with four sparks on the nearest other sector where a ship was lost in the
    last 8 s or that holds two or more ships.
  - **Station / black hole / wormhole**: a white frame / an orange ring round a black core / a violet dashed arc;
    one legend row under the map names them (only those the universe has). The three-row legend is gone.
  - 300 px wide at 1080p (200 at 1280x720), 390 (260) from 32 sectors on. Everything comes from data the client
    already has.
- Feedback: kill feed with the weapon or cause, centre messages (match start, kill, killed by, double / triple
  kill, rampage every fifth kill of a life - with the existing interface stingers), respawn countdown,
  end-of-match board with the winner and the countdown, scoreboard (rank, pilot, kills, deaths, turrets, ping).
- Development console command `HCJMenuCmd <play|join <ip>|pause|resume|leave|settings|controls|main|lobbies|leaderboards|refresh|joinlobby <n>|help|scores|endmatch|hurt <n>|shieldhit <f>,<n>|enemyhit <f>,<n>|torpedo|enemytorpedo <uu>|torpedostock <n>|slomo <f>|hints|skiptutorial|resethints|mapjump <s>|die>`
  does what the buttons and keys do; with `-HCJCaptureCmds=` a capture run can walk through the menus.

## Intro

Started without arguments the game plays `Content/Movies/intro_veo.mp4` once (1280 x 720, 10 s, **generated with
Google Veo (Gemini)**) before the title menu: full screen, the title fading in over the last three seconds, a short
fade to black, then the menu. Any key, mouse button or gamepad button skips it.

- How: a `UMediaPlayer`, a `UMediaTexture` and a `UFileMediaSource` created at run time by the local player controller
  (`AHCJPlayerController::StartIntro`; Media Framework with WMF on Windows, plugin `WmfMedia`), shown by a full-screen
  Slate widget (`Private/UI/HCJIntro`) above the menu. No sound component is attached, so the movie's own sound track is
  not played: the Lyria menu track starts under the picture.
- It never blocks: `FHCJIntroFlow` (`Public/UI/HCJIntroLogic.h`, plain C++, tested) ends it when the movie has played,
  on any key, when no frame has appeared after 4 s, on a player error and after 20 s whatever happens. A missing file
  is one warning in the log and the menu opens at once.
- When: `HCJIntro::ShouldPlay` - only in a process that opens on the title menu from its command line (so not with a
  map URL, an address, `-AutoHost`, `-server`, `-nullrhi`, `-HCJCapture=`, `-HCJFrameDump=`, `-ExecCmds=`), only once
  per process (not again after LEAVE TO MENU or a lost connection), never on a dedicated server or without a renderer.
  **`-HCJNoIntro`** switches it off; `-HCJIntro` forces it in a capture run that opens the menu (`-HCJMenu`).
- The file is staged as a loose file (`+DirectoriesToAlwaysStageAsNonUFS=(Path="Movies")`), not cooked into the pak.
- Log: `Intro: opening ...`, `Intro: the movie plays (first frame after ...)`, `Intro: over after ... (played to its
  end | skipped by a key or button | ...)`. Development: `HCJMenuCmd skipintro` does what a key does.

## Seeing the enemy

Other ships are made to stand out in the world itself, not only by HUD markers (`AHCJShipCharacter::TickShipEffects`,
materials built by `docs/tools/make_ship_fx_materials.py`, settings in the "seeing the enemy" block of the asset section):

- **Size**: ships are 1.7 times as big as before: hull length 560 uu (330), movement collision radius 100 (60;
  `HCJ::ShipCollisionRadius`), hit sphere for bolts and beams 135 (100; `AimAssistRadius`), shield bubble, muzzle
  offset and chase camera distance (1150 uu, 240 above) scaled with it. Sector layouts and their checksums do not
  depend on the ship size.
- **Rim glow**: every other hull is drawn a second time with `M_HCJ_ShipRim` as its overlay material - a fresnel rim in
  the enemy colour (red, with a little of the player's colour) that widens to the whole silhouette between 20 m and
  120 m. The same overlay adds a small flat floor to every hull, the own one included, so that the unlit side is never
  black.
- **Glint** (minimum apparent size): from 40 m (full strength from 80 m) a point of light with a halo and fine spikes (`M_HCJ_ShipGlint`) on a
  quad facing the view, 13 % of its distance across, so that a far ship never shrinks below about 75 pixels of glow
  at 1280 x 720. It replaces the older beacon sphere.
- **Engine glow and trail**: a glow at the tail and a trail (`M_HCJ_EngineTrail`, a quad from the tail to where the tail
  was 0.45 s ago, rolled to the view) on every ship: enemies in the enemy colour, the own ship in its own colour and
  dimmer.
- HUD: enemy brackets and edge arrows are drawn heavier (each line over a dark one), larger, and fade less with
  distance; the locked target has four ticks around its diamond that breathe.
- Cloak: a cloaked ship shows none of this on other machines (the overlay goes with the hidden hull; glint, glow and
  trail are switched off with it).
- SETTINGS > "Enemy highlight" (on by default, `UHCJUserSettings::bEnemyHighlight`): off removes the rim and shrinks
  the glint to 40 %.
- Development: `HCJMenuCmd scene <sector>,<look>[,<spawn point>[,<distance>]]` (own ship to a spawn point, looking at
  `body0|body1|nebula0..2|dark|portal<n>|wormhole|station`), `HCJMenuCmd peace [0]` (bots hold still and hold fire),
  `HCJParkEnemy <uu>`.

## Cinematic look

"Make it close to the cinematic intro" (item 26 of `docs/DEVLOG_NOTES.md`). Presentation only: no rule, layout,
collision or checksum changed. Details: `Source/HypeCubeJam/ARCHITECTURE.md`, "Cinematic look".

- **Sky**: every sector has layered nebula clouds in its own colour (pulled towards teal), a backlit core cloud with
  a white-hot centre, soft haze so that no direction is black, soft clouds that stand in the world 600-1000 m out and
  shift against the sky as you fly, and about 15 000 stars (the 10 000 of the catalogue plus 5 000 made-up faint ones).
- **Rocks**: a rim light on every rock (stronger towards the sector's back light), craters laid over the boulders, a
  stronger sun and a coloured fill light from the back light of the sky.
- **Decorative rocks**: about 1 600 pebbles (below half a metre; most hang around the real rocks) and a belt of 180
  boulders outside the kill radius, per sector, generated from the sector seed on each machine for the sector the
  view is in. Drawn only: no collision, never replicated, invisible to bots, aim assist and traces. A pebble closer
  than 7 m to the camera is not drawn.
- **Engines**: a jet behind every ship (white-hot core, shock diamonds), longer with speed; enemies red, the own ship
  in its own colour.
- **Weapons**: thicker phaser beam with a halo, a star of streaks at every hit (bolt, beam, shield), brighter bolts.
- **Portals**: a heavier, darker ring with glowing rune glyphs on both faces, loops of light in the swirl, rays
  radiating from behind the ring.
- **Post process**: more bloom (above a threshold), vignette, more contrast, a teal-and-orange grade.
- SETTINGS > "Cinematic look" (on by default, `UHCJUserSettings::bCinematicLook`): off switches the post process off
  (the picture as the engine renders it by default) and keeps about a third of the decorative rocks.
- Tunables: `[/Script/HypeCubeJam.HCJAssetSettings]` in `Config/DefaultGame.ini`, from `NebulaBrightness` to
  `PortalFilaments`. Materials: `python docs/tools/make_cinematic_textures.py` (Pillow), then the editor scripts
  `docs/tools/make_cinematic_materials.py`, `make_portal_materials.py`, `make_phaser_fx_materials.py`,
  `make_ship_fx_materials.py`.
- Development: `HCJMenuCmd cinematic <0|1>` (the setting, not saved), `fly <0|1|2>` (the own ship flies ahead at
  cruise / under boost), `impact [uu]` (an impact star ahead), `enemyfire <0|1>[,<ship>[,<target ship>]]` (with
  `peace`: that ship holds its trigger at another ship or, -1, at you), `scene <sector>,backlight`.

## Portals and wormhole mouths

`AHCJPortal` (trigger and position unchanged): a ring of 28 dark blocks, 14 glowing glyph bars and an inner lip that
turn slowly, and inside it one quad with `M_HCJ_PortalSwirl` (`docs/tools/make_portal_materials.py`): three layers of
spiral arms and rings running inwards with parallax (it reads as a tunnel), a core, a rim, six sparks circling the
rim - all in the colour of the sector the portal leads to. When a ship goes through, the portal it left and the one
it came out of flash, a ripple runs from the centre to the rim and the glyph ring spins up: every machine does that
for every ship it sees jump (`AHCJPortal::NotifyPassage`, called where the jump sound is played; nothing replicated).
Beyond 90 m a glint in the portal's colour keeps it visible as a landmark (the ship glint material). Wormhole mouths
keep their own look - a white-blue core in a flat swirl that faces the view - and get the same landmark glint (in the
wormhole colour) and a flare of the core on a passage. Settings: `PortalSwirl*`, `PortalRingTurnRate`, `PortalGlow*`.

## Online: quick play, lobby browser and leaderboards (Epic Online Services)

`UHCJEOSSubsystem` (`Public/Online/HCJEOSSubsystem.h`, a game-instance subsystem) talks to the EOS C SDK directly,
through the engine's `EOSShared` plugin (the only plugin added: it loads `EOSSDK-Win64-Shipping.dll`, initialises
the SDK and ticks the platform). The stock `OnlineSubsystemEOS` is not used. Everything that can be decided without
the SDK is plain C++ in `Public/Online/HCJEosLogic.h` and has tests.

- **Credentials**: `Config/DefaultEngine.ini`, section `[HypeCubeJam.EOS]`: `ProductId`, `SandboxId`,
  `DeploymentId`, `ClientId`, `ClientSecret` (from the Developer Portal; the secret is never logged or shown).
  They are read at start, so a packaged build must be packaged again after the secret is filled in - or started
  with `-ini:Engine:[HypeCubeJam.EOS]:ClientSecret=<secret>` (Development builds). Like every EOS game client, a
  packaged build carries its client credentials.
- **Identity**: EOS Connect with a device id (`EOS_Connect_CreateDeviceId`, then `EOS_Connect_Login` with
  `EOS_ECT_DEVICEID_ACCESS_TOKEN`; the product user is created at the first sign-in). No Epic account, no login
  screen, no overlay. The display name is the pilot name. **A device id belongs to the Windows user**: two
  instances started by the same Windows user are the same EOS user (the SDK has no switch for a second local
  identity), and a pilot's scores do not follow them to another PC or Windows user.
- **Fails soft**: with the section incomplete (e.g. no secret), the SDK library missing, the network down or
  `-HCJNoEOS`, the state says why (`LogHCJ: EOS: ...`, one line), the title menu shows "Online services
  unavailable: ..." and the two online pages show the same instead of their lists; everything else is unchanged.
  A failed sign-in is retried after 20 s, 60 s and then every 2 minutes. Never active on a dedicated server.
- **QUICK PLAY** (title menu, first button; `-HCJQuickPlay` on the command line does the same once the title menu is
  up; `HCJMenuCmd quickplay`): one click into a match with people when there is one.
  1. "Signing in to Epic Online Services..." - waits up to 15 s for a sign-in that is under way.
  2. "Searching for open matches..." - one lobby search (10 s at most). Offered are only lobbies this build can join:
     the same network version, a free slot, an address this machine can use, not stale (see FIND GAMES).
  3. "Joining <host> (<n>/<max> players)..." - the best one: most players, then most free slots. **The ping is not
     known** (there is no connection to a host before the join), so it does not rank. The join is the ordinary IP
     join to the advertised address, 15 s at most; when it fails or takes too long the next lobby is tried (two
     hosts at most): "Could not join <host> - Joining <next>...".
  4. "No open match found - hosting one" / "Could not join <host> - hosting a match instead" / "Search failed (...) -
     hosting a match instead": shown for 2 s, then the same as PLAY - a match with the bots of the menu, advertised
     as a lobby, so the next player who presses QUICK PLAY lands in it.
  5. Without the service (not configured, `-HCJNoEOS`, no network): "Online services unavailable (<why>) - starting a
     local match against bots", then PLAY.
  The ship chosen in the HANGAR applies either way. CANCEL / Esc stops it (and cancels a connection that is under
  way). The steps are `FHCJQuickPlay` in `Online/HCJEosLogic.h` (no SDK, no world; tested), kept by the subsystem so
  that they survive the world change of a join; `AHCJPlayerController::UpdateQuickPlay` feeds them every frame and
  logs each status line as `Quick play: ...`.
- **Title menu, the two lines under FIND GAMES / LEADERBOARDS** (item 33). The first says who is signed in and,
  once a search has answered, how many matches are open: "Online as Ada  -  2 open matches, 5 pilots flying" or
  "... -  no open match right now: QUICK PLAY hosts one". While the title page is up the lobby search of FIND GAMES
  runs every 45 s; the count is what FIND GAMES would list (after the filter below), the pilots are the players of
  those matches. A failed search, or an answer older than 135 s, gives the plain "signed in" text; so does every
  other page (the count is the title page's only), and a search of that timer is logged only when its answer
  differs from the last one. The second line
  is the pilot's lifetime record, "Your record:  12 kills  -  7 deaths  -  K/D 1.7  -  3 wins in 5 matches  -  4
  turrets" (`EOS_Stats_QueryStats` for the local user at sign-in and when LEADERBOARDS opens; an accepted ingest is
  added at once). The same line is a "YOUR RECORD" strip on the LEADERBOARDS page and a line on the end-of-match
  board. Not known = not drawn (also while the query has not answered, or failed for a reason that passes: it is
  asked again with the title page's next search and after the next sign-in). The query waits for a stat ingest that
  is on its way, so that a match is never counted twice. If the client policy of the product refused the query
  (`EOS_AccessDenied`, `EOS_MissingPermissions`), the log says so once (`EOS: pilot record not read (<result>): the
  client policy ...`) and the line shows kills and wins from the local rows of the two leaderboards instead.
- **FIND GAMES** (title menu): the list of advertised matches (host, players / maximum, bots, map and bot difficulty),
  REFRESH, JOIN. Under the host's name, when the host says so: "first to 25  -  about 7 min left  -  with Ada, Linus"
  (frag limit, time left, the other pilots). These come from four optional lobby attributes (`HCJDIFF`, `HCJFRAG`,
  `HCJEND`, `HCJPILOTS` - whole names, at most 64 characters, "+3" for those that did not fit; a joiner's name is
  listed 5 s after it joined). A 0.18 host does not send them and its row is as before; a 0.18 client ignores them.
  A listen host that is past the title menu (PLAY, `-AutoHost`, a `?listen` URL) creates one public EOS lobby in
  the bucket `HCJ_FFA`; its attributes carry the host name, the LAN addresses of the machine, an optional public
  address, the port, players, maximum, bots, hypercube dimension, seed and the network version of the build
  and the time of the last write (`FHCJLobbyInfo`; nothing about ship types: any ship may join any match). The lobby
  is updated when one of these changes (at most every 2 s; the player count follows joins and leaves), written again
  every 2 minutes as a heartbeat, and destroyed when the host returns to the title menu, leaves the world or quits.
  **Listed are only matches this build can join** (`HCJEos::FilterJoinableLobbies`): another network version, a full
  match, a lobby not written for 10 minutes (a host that crashed; the service also drops lobbies without an owner by
  itself) and one without a usable address are left out, and the status line says how many and why ("2 not shown: 1
  other version, 1 full"). The same list feeds QUICK PLAY.
- **Transport: plain IP, not EOS P2P.** The lobby is an advertisement only; nobody but the host is a member. JOIN
  travels to an advertised address by the same path as "Join a host at address" (`HCJEos::PickJoinAddress`):
  loopback when the host is this machine, the host's LAN address when it shares a /24 with one of ours, else the
  public address, else the first LAN address. **Limit (NAT):** over the internet this only works when the host
  has UDP port 7777 forwarded and has told the game its public address - `PublicAddress=<ip or name>` in
  `[HypeCubeJam.EOS]` or `-HCJPublicAddress=<ip or name>` on the command line; the game does not discover it.
  On one network (or one PC) it works without any setup. Direct join by address is unchanged.
- **Stats**: every human player's own client adds to its own SUM stats `HCJ_KILLS`, `HCJ_DEATHS`, `HCJ_WINS`,
  `HCJ_TURRETS`, `HCJ_MATCHES` (`EOS_Stats_IngestStat`): at the end of a match (kills, deaths, turrets, one match,
  one win for the winner) and, for kills / deaths / turrets, when leaving a server, returning to the title menu or
  quitting mid-match (the game waits up to 2 s at exit for the service to answer). `FHCJStatTracker` turns the
  replicated score line into the amounts; a failed ingest is kept and sent with the next one. Bots have no
  client and report nothing. The stats are reported by the client, as the policy of the product allows; the
  server does not vouch for them.
- **LEADERBOARDS** (title menu and Esc menu): the top 10 of `HCJ_LB_KILLS` and `HCJ_LB_WINS` (rank, pilot,
  score) and the local player's row when it is further down. EOS updates leaderboards some minutes after an ingest.
  The end-of-match board says whether the stats were sent.
- Development: `-HCJEosDemo` fills both pages with made-up rows without contacting any service (pictures, and
  JOIN / QUICK PLAY reach a host on this machine through the first row; `-HCJEosDemoPort=<port>` is the port that row
  advertises); `-HCJNoStats` sends no stats (test runs against the live service); `HCJMenuCmd lobbies | leaderboards |
  refresh | joinlobby <index> | quickplay | cancelquickplay`. No automation test and no playthrough run reaches the
  service: the subsystem is off while automation tests run, and the playthrough runner passes `-HCJNoEOS`.

## Flight, rules and bots

- Boost is a meter (`UHCJShipMovementComponent`): 3.5 s of boost when full, 7 s to refill, locked after running dry
  until it is back at 25 %. It is part of the movement simulation - the owning client and the server step it from
  the same inputs for every move (`HCJBalance::StepBoost`), nothing extra is sent, and the client's saved moves
  carry the meter for combined moves and correction replays. Measured on a client of a dedicated server with the
  autopilot boosting every other 6 s: about one extra movement correction per emptied meter.
- Shields (`Ship/HCJShieldFacings.h`, `UHCJHealthComponent`): four facings - fore, aft, port, starboard - of 26
  points each (`ShieldFacingCapacity` in `[/Script/HypeCubeJam.HCJGameplaySettings]`). The server takes the facing
  from where a hit landed in the ship's own frame (`HCJShield::SelectFacing`: quarters of the horizontal circle,
  the vertical part ignored). The hit drains that facing; what it cannot take, and every hit on a facing that is
  down, goes to the hull. Damage without a direction (leaving the sector) is shared by all facings. Each facing
  regenerates 8 points per second once it has not been hit for 4 s (`HCJBalance::StepShieldRegen`, one clock per
  facing). On the wire the four strengths are one 32-bit word (a byte each, 0 = exactly down).
  - Bubble: one hidden sphere per ship, stretched to an ellipsoid, with the additive unlit material
    `/Game/HCJ/Effects/M_HCJ_ShieldBubble` (built by `docs/tools/make_shield_material.py`). A hit that a facing
    absorbed flares it at the impact point for 0.4 s, tinted by what the facing has left (blue-white to orange); a
    facing going down flickers its half for 0.55 s and plays a chime. The flare is an unreliable multicast of three
    bytes, at most one per ship every 0.08 s; the collapse is derived on every machine from the replicated
    strengths. The bubble costs nothing while nothing happens (not visible, not ticked), and a cloaked ship shows
    it only while it is being hit.
  - Bots: a bot that is breaking off (running for a repair pad, or travelling on) and was hit in the last 3 s
    turns a facing that still holds towards its attacker when one of its own is weak or down; while it shoots it
    keeps its guns on the target. It uses its own facings only.
  - Development: `HCJMenuCmd shieldhit <facing>,<damage>[,<degrees>]` and `enemyhit ...` (listen host) hit one
    facing of the own ship / the target; `-LogCmds="LogHCJ Verbose"` logs every facing change, flare and collapse
    (`Shield: ...`); `-HCJSingleShield` gives ships the single 50-point shield of the earlier builds, to compare
    bot matches with the same binary.
- Spawn shield: 3 s without taking damage; firing drops it at once, so no damage is dealt from under it. The ship's
  lights shimmer white while it is up (replicated flag) and the HUD says so.
- Photon torpedoes (`Ship/HCJTorpedo.h`, plain C++ with tests; numbers in `[/Script/HypeCubeJam.HCJGameplaySettings]`):
  the secondary weapon of every ship, on the right mouse button (or X). A fresh ship has 4 (the `Ammo` of the TORPEDO
  row of `Weapons`), at most 6 (`TorpedoMaxStock`), 2 s between shots (`FireInterval` of the row); a TORPEDOES pad
  gives 3. The torpedo is an `AHCJProjectile`: 9000 uu/s (bolts 20 000, homing missiles 10 000), 2 s of life, turning
  40 degrees per second (missiles 140) towards the ship its firer had locked at launch - it flies straight without a
  lock and loses its target for good once that is more than 60 degrees off its nose. That is a BOT's torpedo. A torpedo
  fired by a PERSON (every difficulty; devlog item 40, `HCJTorpedo::GetHoming`, server-chosen target, replicated as
  before) takes the locked ship when it is within 60 degrees, else the nearest live, uncloaked ship within 40 degrees
  ahead and in reach (`HCJTorpedo::PickTarget`), turns 180 degrees a second, steers for where the target will be in
  up to 0.5 s (`SteerPoint`) and keeps it while it stays within 80 degrees of its nose - a hard turn under boost still
  shakes it off. `-HCJClassicTorpedo` gives a person the bot's torpedo again (the torpedo of build 0.21.0). Armed after 12 m
  (`TorpedoArmingDistance`; a hit before is a dud: 15 % of the damage, no blast); detonates on contact or when its
  time is up, with a blast of 4.5 m around the hit (full damage within 1.2 m of a hull, 25 % at the edge, nothing
  behind a rock). Damage (`HCJTorpedo::SplitDamage`): it strips up to 30 points (`TorpedoShieldDamage`) from the
  facing it lands on, and the share of it the facing could not take is the share of 70 (`Damage` of the row) that
  reaches the hull - a full facing of 26 is gone and the hull loses 9; through a facing that is down the hull loses
  70 of its 100; two on the same facing: 79. The facing is the one towards the impact, for a blast the one towards
  the blast. Quad damage does not apply. The firer is hit by his own blast like anybody else; the arming distance
  keeps him out of it, even when he follows the torpedo under boost.
  - Look and sound (existing content only): a star-burst and a detonation flash with a shockwave ring, two additive
    unlit materials built by `docs/tools/make_torpedo_materials.py` on a quad that faces the view, the ribbon trail
    and small explosion of the Niagara Examples pack; the launch is the jump-pad whoosh played at 0.6 of its pitch,
    the detonation the two explosion waves of the Hover Vehicles example content.
  - HUD: TORPEDOES block in the weapon panel; "INCOMING TORPEDO" with a marker on the torpedo, or a chevron at the
    edge of the view towards it, read from the replicated torpedoes this machine sees; kill feed `[TORPEDO]`; one
    hint the first time the locked target shows a facing that is down while a torpedo is loaded.
  - Bots (`HCJTorpedo::ShouldBotFire`) keep their target locked and fire a torpedo when the facing the target turns
    to them is down or weak - what the target panel shows a player - or now and then as an opener at medium range;
    how readily, and how often they break away from one that is homing on them, depends on the difficulty.
  - Development: `-HCJNoTorpedoes` (no stock, no pads: to measure a match without them), `-HCJAutoTorpedo` (capture
    autopilot fires them), `HCJMenuCmd torpedo | enemytorpedo <uu> | torpedostock <n> | slomo <f>`, the
    `Torpedoes t=` line of the server log.
- Spawn choice: the best of 12 random spawn points (`HCJBalance::ScoreSpawn`): never within 80 m of an enemy, in
  range of a live turret or in a gravity well when there is an alternative; a slight preference for a sector
  with one to three enemies. A new ship is turned to face the nearest ship of its sector.
- Bots have five difficulty levels - Super Easy, Easy, Normal, Hard, Ace in the menu's order (`FHCJBotSkill`; the
  Difficulty row of the menu, `BotDifficulty` in `[/Script/HypeCubeJam.HCJGameMode]`, URL `?Difficulty=0..4`: 0 easy,
  1 normal, 2 hard, 3 ace, **4 super easy** - devlog item 40; Super Easy got the next index, not 0, so that the numbers
  of the older builds keep their meaning; its bots fly Easy's numbers): aim error 7 / 3.5 / 1.5 degrees,
  reaction 1.0 / 0.5 / 0.2 s before the first shot at a new target, turn rate, burst rhythm, opening range, strafe
  strength, and the hull fraction (45 / 30 / 20 %) at which a bot breaks off, stops firing and boosts for a repair
  pad in its sector. Ace flies with Hard's numbers. The default is Easy.
- **Bots and people: the fairness rules** (devlog item 29; `AHCJBotController`, the fairness fields of
  `FHCJBotSkill`, server only, nothing replicated). They apply only when the other ship is flown by a person; bots
  among themselves fly the same on every level, so bot matches and their kill rates are unchanged.

  | | Super Easy | Easy | Normal | Hard | Ace |
  |---|---|---|---|---|---|
  | bots attacking one player at a time (`MaxAttackersOnHuman`) | 1 | 1 | 2 | 3 | no limit |
  | bots in a player's sector before travelling bots go elsewhere (`MaxHuntersPerHuman`) | 3 | 3 | 4 | 6 | no limit |
  | a bot in the player's rear arc holds its fire for (`RearFireDelay`) | 2.5 s | 2.5 s | 1.2 s | 0.5 s | - |
  | aim error from the rear arc (`RearAimErrorScale`) | x 2 | x 2 | x 1.5 | x 1 | x 1 |
  | pull round to the player's front (`FrontBias`) | 1.0 | 1.0 | 0.6 | 0.3 | 0 |
  | target score of a player's tail (`HumanTailScore`; bots among themselves +1.5) | -2 | -2 | -1 | 0 | +1.5 |
  | scout ambush from cloak, close passes around a player (`bFlankHumans`) | no | no | no | yes | yes |
  | left alone after appearing in a sector (`ArrivalGrace`; also under the spawn shield) | 4 s | 4 s | 3.5 s | 1 s | - |
  | left alone after the attacker is gone (`RespiteSeconds`) | 6 s | 6 s | 5 s | - | - |
  | damage of a bot's hit on a player (`DamageToHumanScale`) | 40 % | 45 % | 55 % | 100 % | 100 % |
  | torpedoes a bot would fire at a player that are fired (`TorpedoOnHumanChance`) | 35 % | 35 % | 45 % | 100 % | 100 % |
  | **beginner's help** (item 38): damage of a player's hit on a bot (`DamageFromHumanScale`) | 150 % | 135 % | 100 % | 100 % | 100 % |
  | the player's hull mends out of combat (`HumanHullRegenRate`) | 4 points a second | 4 points a second | - | - | - |
  | a repair pad restores, on a player's ship (`HumanRepairScale`) | 100 hull | 100 hull | 60 | 60 | 60 |
  | **more help** (item 40): a pad's item is taken within (`HumanPickupReach`) | 25 m | 25 m | touch | touch | touch |
  | a new ship of a player holds (`HumanSpawnWeapon`) | scatter cannon, full | scatter cannon, full | phaser | phaser | phaser |
  | a player's shots bend towards a ship within (`HumanAimAssistDeg`) | 5 degrees | 3 degrees | - | - | - |
  | the HUD points to a weapon pad while the ship has only its phaser (`bHumanWeaponPointer`) | yes | yes | no | no | no |

  - "Rear arc": more than about 107 degrees off the nose of the player's ship (`HCJBalance::RearArcDot`). The wait
    starts again every time the bot gets there; the HUD's ENEMY BEHIND is the warning for it.
  - A bot the player shot in the last 4 s (`HCJBalance::ProvokedSeconds`) may always answer: the cap, the grace, the
    respite and the rear wait do not hold it. When the cap is full it is the other bots that make room for it, so a
    player who fights one bot at a time has one bot at a time on Easy.
  - A bot that was shaken off for a second (a rock in between) needs its reaction time again.
  - Hunters: a bot that travels counts the bots in a player's sector and the ones on their way into it; when that is
    `MaxHuntersPerHuman` it neither hunts that player nor roams through a portal into that sector while it has
    another way out (`held_hunters` of the `Fair: event=status` line counts how often).
  - Ace (`?Difficulty=3`) is Hard's flying with none of these rules - what Hard was before. `-HCJNoFair` switches
    the rules off on every level (for comparisons with the same binary; the spawn changes - 3 s of spawn shield,
    80 m, facing the nearest ship - and the HUD stay).
  - **Beginner's help, Easy and Super Easy** (devlog item 38; server only, nothing replicated; the other levels and the bots
    among themselves are untouched): every hit of a player on a bot's ship does 1.5 times its damage (1.35 on Easy since
    item 40)
    (`AHCJShipCharacter::TakeDamage`, all weapons); the hull of a player's ship mends by 4 points a second once
    nothing has taken hull or shield from it for 5 s, up to 75 % of the full hull
    (`HCJBalance::StepHullRegen`, `HullRegenDelay`, `HullRegenCeiling` - the last quarter is what repair pads are
    for); a repair pad makes a player's ship whole (100 instead of 60); and a bot's hit does 40 % instead of 50 % (Easy 45 % since item 40; Super Easy 40 %).
    `-HCJClassicEasy20` takes these four away again with the help of item 40 (`FHCJBotSkill::ClearAllBeginnerHelp`:
    Easy as build 0.20.0 had it, for comparisons with the same binary); `-HCJNoFair` takes them away with everything
    else. The `Fair: event=rules` line has `classic_easy= human_damage_scale= hull_regen= repair_scale=`, the
    `Fair: event=status` line `human_kills= human_dmg= human_dmg_bonus= hull_regen= repairs= repair_points=`.
  - **Super Easy** (devlog item 40, `?Difficulty=4`, first in the menu): Easy's bots and every help of Easy, with
    the numbers the first 0.22.0 gave Easy - a player's hits 1.5 x, a bot's hits 40 %, the aim assist 5 degrees. After
    the player's first 0.22.0 match (16 kills, 1 death: "tone it down") Easy itself went back towards the middle:
    1.35 x, 45 %, 3 degrees. Stored in the settings as `BotDifficulty=0` + `bSuperEasy=True` (an older
    build reads that as Easy); in the lobby attribute as 4 (an older build shows the row without a level: it reads
    0..3 only). `-HCJClassicEasy` on Super Easy gives 0.21.0's Easy too. Development: `-HCJEasyAssist=<deg>
    -HCJEasyHumanDamage=<x> -HCJEasyBotDamage=<x>` override the three numbers on Easy and Super Easy (tuning).
  - **More help, Easy and Super Easy** (devlog item 40; server only, no replicated property or RPC added; the other levels and
    the bots' own aim are untouched): the item of a filled pad goes to a player's ship within 25 m of the pad
    (`AHCJShipCharacter::ServerTickPickupReach`, five times a second, through the pad's own `ServerTryCollect`: a
    refused item - a repair kit at full hull - stays on the pad); a player's new ship starts with a full scatter
    cannon instead of the phaser alone (`ServerGiveHumanStartWeapon` - its ten pellets in a 4 degree cone forgive the
    aim at the 10 to 25 m at which a beginner's duels are fought, two good shots break an Easy bot, and it has no
    range to waste ammunition beyond 100 m); a player's bolt leaves towards the lead point of the locked ship, else
    of the ship nearest the crosshair, when that point is within 3 degrees (Super Easy: 5) of the aim (`ServerFireProjectile`,
    `HCJBalance::PickAimAssist`; the pellets spread around it as before), and the phaser beam bends to a ship within
    3 (5) degrees instead of 1.6 / 3; while the ship has only its base weapon the HUD's pad pointer of item 38 leads to the
    nearest weapon pad (ringed, not pulsing; `HCJUi::GuidePadRank`) - on the host and in a single-player match only,
    because a remote client is not told the level. `-HCJClassicEasy` takes these four away and sets the damage scales back to 1.5 / 0.4
    (`ClearBeginnerHelp`: Easy as build 0.21.0 had it). The `Fair: event=rules` line has `classic_easy20= pickup_reach= spawn_weapon= aim_assist=
    weapon_pointer= classic_torpedo=`, the `Fair: event=status` line `human_pickups= reach_pickups= armed_spawns=
    human_bolts= assisted_bolts= human_bolt_hits= assisted_hits=` and, for people's torpedoes on every level,
    `torp_fired= torp_homing= torp_acquired= torp_lost= torp_hit=`.
  - The server logs what happened to the people in a match: `Fair: event=rules | spawn | hit | death | status`
    (key=value; see "Fairness telemetry" in docs/DEVLOG_NOTES.md, item 29, for the fields and how to read the rear
    share and the deaths per minute out of a log).
  - In a match the host changes the level with Esc > "Bot difficulty (applies now)" (`AHCJGameMode::SetBotDifficulty`;
    the bots read their row every 0.25 s).
  - Development: `HCJMenuCmd difficulty [n]` (the menu row), `matchdifficulty <+1|-1>` (the in-match stepper),
    `enemybehind [uu]` (listen host, with `peace`: the nearest ship is put behind the own one with its nose on it).
- Bot tactics (`AHCJBotController`, class comment; `FHCJBotSkill::Tactics` 0.25 / 0.7 / 1 is the chance that a ship is
  flown "smart" at all and that a clever move is made when there is one, so easy bots mostly fly as before):
  - **Target**: not simply the nearest - the ship that is shooting at the bot, a damaged one, one whose shield facing
    towards the bot is weak or down, one whose tail points at it; each other bot already on a ship counts against
    it, so they spread out.
  - **Breaking off**: when the facing towards the attacker is down and the hull is low, and there is a way out - a
    rock within 50 m to get behind, the cloak (it costs the shields, which are gone on that side anyway), a repair
    or shield pad - the bot goes, under boost, with its best facing turned to the attacker, and comes back when its
    shields are above 55 % or after 7 s. With no way out it fights on.
  - **Torpedoes**: at once through a facing that the phasers have brought down (35 % / 70 % per decision on normal /
    hard; easy as before); a smart bot holds a target in reach with the tractor beam first.
  - **By ship type** (smart bots only; the ship numbers are unchanged): interceptor, fighter and scout make boosted
    passes close around hulls that turn slower than they do; the scout cloaks readily to get away, goes for damaged
    ships and drops an approach cloak behind its target; the gunship stands off and is twice as ready with
    torpedoes; the cruiser uses its tractor beam more, above all on a fast ship close by.
  - **Surroundings**: a small rock on the line to a hidden target is shot away with the phaser; the reach of a
    station's turrets is steered out of unless the bot is attacking them; a fight far away is reached at warp.
  - The `Tactics t=` line of the server log counts all of it (retreats, cover reached, torpedoes at an open facing
    and kills by them, tractor then torpedo, turns on an attacker, rocks shot, ...).
- Looks only: the hull banks into turns, the chase camera lags a little and widens under boost, a short camera
  shake and an edge flash when hit, hull lights brighter with thrust, debris bursts in the death explosion.

## Ship types, warp drive, cloak, tractor beam

Four additions in the manner of Star Trek. The rules are plain C++ with tests (`Ship/HCJShipTypes.h`,
`Ship/HCJShipSystems.h`); `AHCJShipCharacter` and `UHCJShipMovementComponent` run them. Keys: hold **Z** warp, **C** cloak,
hold **R** tractor beam.

**Ship types.** One table (`HCJShipTypes`, code not config: client prediction needs the same flight numbers on both
ends). Each type owns eight of the forty Star Sparrow hulls; a pilot picks type, hull and one of ten paints in the
HANGAR (title menu, and under Esc for the next ship), stored in `GameUserSettings.ini`. The choice goes to the server
(`ServerSetShipChoice`), which validates it (an unknown type is the fighter), keeps it in the player state and stamps
it on every ship it spawns; type, hull and paint are replicated on the ship and applied on every machine - flight
model, hull and shield capacity, size of movement collision / hit sphere / shield bubble, stock of torpedoes, look.

| | Interceptor | Fighter | Gunship | Scout | Cruiser |
|---|---|---|---|---|---|
| size (fighter = 1) | 0.85 | 1 | 1.12 | 0.9 | 1.3 |
| cruise / boost, m/s | 35 / 70 | 30 / 60 | 26 / 52 | 33 / 66 | 23.5 / 47 |
| acceleration, m/s2 | 80 | 60 | 50 | 70 | 42 |
| boost meter, s (refill) | 3.5 (6) | 3.5 (7) | 3 (8) | 5.5 (6) | 3 (8) |
| hull | 93 | 100 | 110 | 97 | 144 |
| shield facing (x 4), regeneration per s | 23, 8 | 26, 8 | 24, 8 | 22, 8 | 34, 10 |
| hits on the fore facing | x 1 | x 1 | x 0.9 | x 1 | x 1 |
| phaser damage, bank refill s | x 1.05, 3.4 | x 1, 5 | x 1.1, 6 | x 0.97, 4 | x 1, 5 |
| bolts and pickup weapons | x 1 | x 1 | x 1.1 | x 0.9 | x 1 |
| torpedoes on a fresh ship | 2 | 4 | 6 | 3 | 4 |
| cloak meter, s (refill) | 10 (20) | 12 (20) | 10 (22) | 20 (16) | 8 (22) |
| warp: longest jump s, cooldown s | 2.5, 10 | 2.5, 12 | 2.2, 14 | 3, 6 | 2.2, 16 |
| tractor: reach m, pull m/s, s from a full bank | 60, 11, 3 | 65, 12, 3 | 65, 13, 3 | 60, 10, 3 | 85, 19, 5 |

The fighter is the ship the game had before. Bots fly a mix (fighter, interceptor, cruiser, scout, gunship in turn);
their aim turns 1.2 to 0.75 times as fast by type. Scoreboard, kill feed and target panel show the type.

**Warp drive.** Hold Z: 1.6 s of charge, then the ship flies in a straight line (the direction of its nose when the
jump starts) at 150 m/s for as long as the key is held, at most the jump time of its type; then the cooldown (2 s
after a charge that was stopped). No weapons while the drive charges or runs; a hit while it charges stops it; no
warp while cloaked or held by a tractor beam. Decision on rock: the drive DROPS OUT before impact - every move
at warp sweeps a sphere 0.3 s ahead against static geometry (rock, stations, turrets) and stops the jump with the
ship at boost speed - and it drops out 225 m from the sector origin when heading outwards. A ship that still runs into
something (another ship) loses 22 hull points. A portal entered at warp works: the ship comes out at boost speed.
- Netcode: the drive is part of the movement simulation. The key is one bit of every move (`FLAG_Custom_1`), "this
  move was flown at warp" another (`FLAG_Custom_2`); the owning client predicts the jump and keeps the drive's state
  in its saved moves; the server steps its own copy and lets the client's bit decide the exact move a jump starts and
  ends on, within 0.25 s of its own charge and of the longest jump (`FHCJWarpDrive::StepFollow`) - so the two do not
  disagree about a move, and a warp without charge or during the cooldown is refused. The direction is the thrust
  vector of the first move, which the server gets with the move. When the server stops a drive (hit, tractor beam,
  collision, portal) the owner is told through a counter and stops its prediction.
- The boost meter now works the same way (the bit says "this move WAS boosted"; the server's meter is only the limit):
  that removed the bursts of corrections the meter caused whenever it ran dry or unlocked (see the notes).
- Looks: the engine flares blue-white while charging; at warp the view widens to 128 degrees, star streaks run
  outwards on a quad in front of the own camera (`M_HCJ_WarpStreaks`), the trail stretches; every machine shows a
  blue-white flare where a ship jumps and where it drops out. Sounds are existing ones at other pitches.

**Cloaking device.** C toggles it. The server runs a meter (`FHCJCloakDevice`): it drains while cloaked, refills slowly,
and when it ran dry it is locked until 20 %. Cloaked: other machines draw the hull with `M_HCJ_Cloak` (translucent, a
faint rim shimmer with a little refraction) and nothing else of the ship - no glint, engine glow, trail, engine
sound - and the HUD has no bracket, arrow, radar blip, map mark, lock or lead circle for it; phaser aim assist,
homing missiles, torpedoes, station turrets and bots ignore it (bots still notice one within 25 m, and fire at
where it was for a moment). Its pilot sees the own ship half transparent. Star Trek rules: shields are DOWN while
cloaked (all four facings drop to nothing and regenerate at once, from nothing, after the decloak) and weapons are off
- pulling the trigger drops the cloak, and weapons work 1 s later. REVEALED for a moment (the hull flickers, everybody
sees and can target it): 1.2 s after a hit, while an enemy is within 15 m, while a tractor beam holds it. The CLOAK
pad fills the meter and stops the drain for 8 s.

**Tractor beam.** Hold R. The server takes the locked target (else the nearest ship in the cone) when it is within
the reach of the ship's type, within 50 degrees of the nose and not behind rock, and publishes who holds whom: the
holder's `TractorTarget` (the blue-white beam every machine draws) and the victim's `TractorHolder`. A held ship flies
at 0.55 of its speed and is pulled towards the holder, down to 13 m, by its own movement simulation - on the server
and in its owner's prediction (`HCJTractor::ComputePull`, a function of the two positions like the drift of a black
hole). The beam feeds on the holder's phaser bank and lets go when the victim has boosted for 0.9 s, when the holder
took 22 points of damage while holding, when rock gets in between, when the victim leaves 1.15 x the reach or the
wider cone, or when the bank is empty; torn free, it cannot be used for 2 s. It reveals a cloaked ship and stops a
warp drive. The holder can fire phasers and torpedoes while holding. Pads are NOT pulled (cut).
- The owning client of a held ship uses the holder's replicated position for the pull, which lags the server's:
  small corrections while held (measured in the notes).

- Bots (`AHCJBotController::ThinkSystems`, chances per difficulty in `FHCJBotSkill`): warp on a long clear way to a
  portal (0.35 / 0.7 / 0.95 of the trips) and away from the attacker, or to a repair pad, when the hull is low (0.15 /
  0.5 / 0.8); cloak for an approach from afar or a retreat (0.03 / 0.1 / 0.2 per second while the reason holds), dropped
  inside 45 m of the target or when revealed; tractor beam on a target in reach, three times as readily on one that
  runs or is nearly done (0.03 / 0.1 / 0.2 per second); caught in one, they boost away from the holder (0.4 / 0.8 / 1).
- HUD: WARP / CLOAK / TRACTOR rows with a bar each in the WEAPON panel; under the own ship the lines WARP CHARGING,
  WARP, CLOAKED, CLOAK REVEALED, DECLOAKING, TRACTOR LOCK and CAUGHT IN TRACTOR BEAM - [Shift] BOOST TO BREAK FREE;
  four hints (caught, and once each what warp, cloak and tractor beam do); rows on the F1 page and the CONTROLS page.
- Statistics: the `Systems t=` log line (warps, cloaks, reveals, tractor holds / torn free, kills / deaths by type).
- Development: `-HCJClassic` (bots fly fighters and use none of the systems: the game before, for comparisons),
  `-HCJBotType=<0..4>`, `-HCJBotsOnly` (the local player has no ship; with `-game -nullrhi` a headless bot match
  without any network), `-HCJQuitAfter=<s>`, `-HCJAutoWarp`, `HCJMenuCmd warp <0|1> | cloak | tractor <0|1> | ship
  <type>[,<hull>[,<paint>]] | hangar | enemyship .. | enemywarp .. | enemycloak .. | enemytractor .. | holdplayer`.
- Materials: `docs/tools/make_trek_materials.py` builds `M_HCJ_Cloak` and `M_HCJ_WarpStreaks`.

## Phasers

The weapon every ship starts with (`EHCJWeapon::Phaser`, rules in `Ship/HCJPhaser.h`, numbers in the `PHASER` row
and the `Phaser*` lines of `[/Script/HypeCubeJam.HCJGameplaySettings]`).

- **Beam.** While the left mouse button is held the server traces a line from the muzzle along the aim (9000 uu =
  90 m) and applies 58 damage per second in steps of 0.1 s: full within 30 % of the range, falling to 45 % at the
  end. It lands on the shield facing it touches (same rule as a bolt), then the hull; it also hurts turrets and
  rock. Aim assist, modest: the beam bends to a hostile ship within 1.6 degrees of the aim line (3 degrees for
  the locked target) when nothing stands in between. Quad damage applies. Firing ends a cloak.
- **Bank.** A full phaser bank is 4 s of beam. It starts to refill 1 s after the beam stops and is full after
  5 s. Emptied, it is locked until it is back at 25 %; **while it is locked the trigger fires the blaster**, so
  holding the button never does nothing.
- **Weapon slots.** A ship has a base weapon - the phaser, or the blaster after `B` (`B` again switches back; a
  new ship starts with the phaser again) - and can hold ONE pickup weapon in its place, exactly as before:
  a pickup replaces what the trigger fires and falls back to the base weapon when it is empty or dropped (`G`).
  Torpedoes stay the separate secondary weapon. So: left mouse = phaser / blaster / pickup, right mouse = torpedo.
- **On the wire.** Per ship one byte for everybody (beam off, or its length), the actor the beam holds, and for
  the owner one byte of bank. No RPC per frame: every machine draws the beam each frame from the ship it sees.
- **Look and sound.** An orange-red quad with a white core that flickers (`M_HCJ_PhaserBeam`, built by
  `docs/tools/make_phaser_fx_materials.py`), a glow at the muzzle and where it hits (the torpedo star), sparks at
  the hit (the bolt impact effects), the victim's shield bubble flaring where it is touched (the existing flare,
  at most every 0.08 s). Sound from what was in the project: the ring loop at 2.4 times its pitch layered with
  the engine noise loop at 2.8, a pitched-up dash for the start, a pitched-down item cue for the stop.
- **HUD.** WEAPON panel: `PHASER`, the bank in percent and as a bar with the relock mark, `RECHARGING` /
  "blaster until recharged" while locked, `[B] switch to blaster`. Kill feed `[PHASER]`. Damage numbers of a beam
  add up in one number. Hints: the fire prompt, "PHASER BANK EMPTY", "phasers and torpedoes break rocks". F1 and
  CONTROLS pages have the row and two lines.
- **Bots** see what a player sees: they hold the beam only while their aim is within 6 / 4 / 3 degrees of the
  target (easy / normal / hard), keep 0 / 5 / 10 % of the bank back unless the target is nearly dead, and aim
  the blaster with lead while the bank is locked.
- Development: `-HCJNoPhasers` (ships start with the blaster, as before), `HCJMenuCmd fire <0|1> | baseweapon |
  god | killenemy`, verbose `Phaser:` lines, the `Phasers t=` status line.

## Destroyable asteroids

Phasers and torpedoes damage rock (bolts do not). Rules in `World/HCJAsteroidField.h`, numbers in the `Asteroid*`
lines of the gameplay settings; the server sends them with the match config.

- Health = bounding radius x 0.22, between 25 and 700: a 4 m boulder has 88 (1.5 s of phaser up close, or one
  torpedo, which does 1.5 x its damage to rock), a 30 m giant 660. Rock does not heal.
- A rock with a radius of 2.6 m or more **breaks into two or three pieces** of 42-56 % of its size; a piece that
  is still that big breaks once more; smaller rocks are simply destroyed. Which pieces, and where, is a pure
  function of (sector seed, rock id): every machine makes the same ones. Pieces lie inside the rock they come
  from, so spawn points, pads, portals, wormholes and stations keep their clearances.
- The layout is never changed: the layout checksum (`7C0CB60C` for seed 1234) is what it was. The state of a
  match is the set of destroyed rock ids; the server owns it (and the health), `AHCJGameState` replicates it as a
  list of 4-byte keys that is only appended to, a late joiner gets the whole list, and both sides log
  `Asteroids (server|client): N destroyed, checksum X` - equal numbers mean the same rocks are gone.
- Everything is back when the next match starts. `?Rocks=0` on the host URL (or `-HCJNoRockDamage`) switches it off.
- Bots avoid rocks by sweeping the collision that is really there, so gaps and new pieces need nothing special.
- Development: `HCJMenuCmd breakrock [min radius] | rockstatus`.

## Explosions: shockwaves, debris, flares

- **Shockwaves (gameplay, server).** A torpedo detonation pushes every ship within 16 m away from it (13 m/s at
  the centre, falling to nothing at the edge), a ship's explosion every ship within 13 m (9 m/s). The push is a
  change of velocity inside the ship movement; the owning client follows by one movement correction.
  `-HCJNoShockwaves` switches it off.
- **Debris (local, cosmetic).** A destroyed ship throws hull plates and glowing embers, a breaking rock chunks of
  rock; they tumble, slow down, bounce off rocks, are carried by black holes, are pushed by later explosions and
  shrink away after about 3 s. Never more than 120 pieces (`MaxDebris`): a new one replaces the one nearest its end.
- **Flares.** Ship, torpedo and rock explosions get a flare (`M_HCJ_ExplosionFlare`: bloom flash, a horizontal
  streak, a star), at most 6 at a time, and a camera shake by distance (SETTINGS > "Camera shake" switches it off;
  the jolt of a hit on the own hull stays).
- **A ship's explosion is staged (local, cosmetic).** The hull fails for a third of a second - it rolls, its lights
  flare orange, three small pops run along it (a star, sparks, a crack) - then the blast: a core flash, a fireball of
  four puffs that billows out and cools from white-yellow through orange to dark red and smoke (`M_HCJ_Fireball`), a
  shockwave ring in the plane of the wreck (`M_HCJ_ShockRing`), sparks drawn out along their flight, hull plates
  whose torn edges glow while they are hot (`M_HCJ_HullDebris`), the three largest trailing fire and smoke (the
  rocket trail of the Niagara Examples); the heart of the fireball stays as a smoke cloud with a dull glow in it that
  drifts and thins out over three seconds. `docs/tools/make_explosion_materials.py` builds the three materials (the
  fireball takes its billows from the cloud texture of the sky; nothing was imported). Beyond 70 m, or with two other
  blasts running, a simpler version is played (one lobe, half the pieces, no trails); beyond 160 m only flare, one
  puff and a few pieces. Hard caps: 14 puffs, 3 rings, 5 trails, 120 debris pieces.
- **The own ship's death.** The view no longer sits inside the explosion: it starts where the chase camera was, pulls
  back 15 m and turns slowly round the wreck for the whole respawn countdown (it stops in front of a rock). The blast
  shakes it hard and briefly (SETTINGS > "Camera shake" switches that off), the screen flashes warm for a fifth of a
  second with an orange vignette for a second, and a low boom is played under the explosion sound. The respawn
  lines sit below the middle of the screen now.
- `UHCJEffectsDirector` ticks only while something is alive. Numbers: "explosions" block of the asset settings.
  Development: `-HCJClassicBlast` (the single pop and the view of before, same binary), `-HCJBlastBench=<enemy|own|crowd>[,<rounds>[,<uu>[,<sector>]]]`
  (frame times before and during blasts: `BlastBench:` lines), `HCJMenuCmd mapjump <s>` (the map's jump animation frozen at that age),
  `-HCJNoMap` (the HUD without the map, to measure what it costs; with `-LogCmds="LogHCJ Verbose"` the map logs its game-thread time every 2000 frames).

## Black holes

About a quarter of the sectors (`BlackHoleSectorFraction`) contain a black hole. It is gameplay state like the
asteroids: `FHCJBlackHole::Make` (plain C++, `World/HCJBlackHole.h`) places it from the match seed and the finished
sector layout with its own hash, at least 0.6 influence radii away from every portal and spawn point and clear of
rocks; the layout generator, its checksum and its golden tests are untouched. The tunables
(`[/Script/HypeCubeJam.HCJGameMode]` in `DefaultGame.ini`: influence radius 6000, horizon 600, pull 200, max pull
9000) travel to the clients inside the replicated match config.

The pull is a drift velocity towards the centre, `min(max, pull * ((R/d)^2 - 1))`, zero at the edge: 150 uu/s at
3/4 of the radius, 600 at half, equal to the cruise speed at 1500 uu and to the boost speed at about 1080 uu. It
is applied inside the ship's movement simulation (`UHCJShipMovementComponent::PhysFlying`) as a pure function of
position, so the owning client's prediction and the server agree (measured: no movement corrections while a
client ship drifted from 2700 uu into the horizon). Bolts are bent by the same function. A ship inside the horizon
dies (environment death, "lost to the void"; the `Status` line counts black hole deaths, bots separately). Bots
steer out of the well and boost away near the point of no return. The HUD warns inside the well and marks
black-hole sectors on the minimap with a black core. Development console command: `HCJGotoHole 0.5`.

## Space stations and turrets

About a quarter of the sectors (`StationSectorFraction`) hold a space station (`FHCJStation`, plain C++): placed
from the match seed and the finished layout with its own hash stream, after pads and wormholes and clear of them,
of portals, spawn points and the influence radius of a black hole. Asteroids within 3000 uu of the station centre
are not built: a deterministic filter in `AHCJUniverse` on top of the unchanged layout (generator output, checksum
and golden tests are untouched). `Stations (...)` in the log gives the count and a checksum for server and client.

- Body: 8 boxes (hub, beams, mast, solar panels). They are the collision on every machine whatever is drawn.
- Look: **"Space Station 4" by Gerardo Justel** (CC-BY; `/Game/SpaceStation4/Meshes/SM_Station4`, 98 914 triangles, three
  materials, four LODs, no Nanite), named by `StationMesh` and drawn at `StationMeshScale=4`, centred on the hub. The
  model is two wheels on an axle, 10.9 m long: the axle lies along the beam on X and covers it, the hub and the two
  panels on that beam; the turrets at the ends of that beam sit on the wheel hubs. The model does NOT cover the beam
  along Y, the mast and the two panels on that beam: those four boxes are still drawn (dark), so that nothing solid is
  invisible, and the corners of the hub box stand up to about 1.4 m out of the axle. Windows glow
  (`StationWindowBrightness`, a little of the sector's colour). Clear `StationMesh` for the plain boxes. Import:
  `docs/tools/prep_station4.py`, then `import_station4.py`, then `station4_lods.py` (see their headers).
- Turrets: 3 to 6 replicated `AHCJTurret` actors per station, spawned by the server on a seeded choice of the
  station's mounts. Server AI: nearest living, uncloaked ship in range with line of sight, limited turn rate, lead
  aim, spread. Destructible (120 points), rebuilt after 60 s. Tunables: "station turrets" block of
  `[/Script/HypeCubeJam.HCJGameplaySettings]`.
- Score: destroying a turret gives a point in the TURRETS column (breaks ties, is not a frag) and a kill feed
  line; a ship destroyed by a turret is reported as killed by "Station".
- Bots stay out of turret range when wandering and sometimes attack a turret when nobody else is around.
- HUD: white crosses on the minimap for station sectors, `STATION n/m turrets` label in the sector.
- Config: `bStations`, `StationSectorFraction` in `[/Script/HypeCubeJam.HCJGameMode]`; URL `?Stations=0|1`.
- Log: `Deaths t=...` every 15 s on the server (deaths by cause, turrets alive / destroyed / shots).
- Development console commands: `HCJGotoStation <distance>`, `HCJKillTurrets`.

## Wormholes

Two-way shortcuts between sectors that are NOT hypercube neighbours (`FHCJWormholes`, plain C++). The links are a
pure function of (match seed, dimension, count) with their own hash stream: by default a quarter as many links as
there are sectors, at most one mouth per sector, none below dimension 3. Each mouth is placed in clear space of
the finished layout (away from rocks, portals, spawn points, pads and the whole influence radius of a black hole);
a link is dropped when one of its mouths finds no room. The log line `Wormholes (...)` lists the links and a
checksum that must be equal on server and client.

- Travel: the mouth is an `AHCJPortal` in wormhole mode; a ship that touches it is translated to the other mouth by
  the same server-side teleport as for portals and keeps its velocity.
- Routing: `FHCJRouteTable` (`AHCJUniverse::GetRoutes`) is BFS over portals and wormholes; `FHCJHypercubeGraph`
  still answers pure-portal questions. Bots use the route table.
- HUD: dashed violet lines on the minimap, a `WORMHOLE > <sector>` label over the mouth of the current sector.
- Config: `bWormholes`, `WormholeCount` (negative = automatic) in `[/Script/HypeCubeJam.HCJGameMode]`; URL
  `?Wormholes=<n>` (0 = none). The `Status` line counts wormhole jumps separately.
- Development console command: `HCJGotoWormhole`.

## Pickups, power-ups and weapons

Every sector has 3 to 5 pickup pads (`FHCJPickupPads`, plain C++): positions follow from the match seed and the
finished sector layout with their own hash stream, so the layout generator, its checksum and its golden tests are
untouched; `Pickup pads (...)` in the log gives a checksum that must be equal on server and client. What lies on a
pad is run-time state: the server spawns one replicated `AHCJPickup` per pad (item type + active flag), a living
ship that touches an active pad gets the item, and the pad is refilled after 20 to 30 s.

| Item | Effect |
|---|---|
| Repair | +60 hull (refused at full hull); on Easy a player's ship is made whole (+100) |
| Shield cell | +50 shield, the weakest facings first, each up to twice its capacity |
| Overdrive | 10 s of 1.5 x speed and 1.6 x acceleration |
| Quad damage | 10 s of 4 x damage; the hull and the bolts glow magenta |
| Cloak | 8 s: not drawn on other machines, bots lose the ship beyond 2500 uu; firing ends it |
| Scatter cannon | 14 shots of 8 pellets, short range |
| Rail shot | 8 shots, 70 damage, 60 000 uu/s |
| Homing missiles | 8 missiles, 55 damage, steer towards the nearest ship in a 40 degree cone |
| Rapid pulse | 120 shots at 14 per second |
| Torpedoes | +3 photon torpedoes, up to 6 on board (refused when full); the current weapon stays |

- Rules: `[/Script/HypeCubeJam.HCJGameplaySettings]` in `DefaultGame.ini` (class `UHCJGameplaySettings`): the weapon
  table (`+Weapons=(...)`: fire interval, damage, speed, lifetime, spread, pellets, homing, ammunition, colour),
  item weights, pads per sector, respawn times, durations, on/off. URL: `?Pickups=0|1`. Looks and sounds are in
  the "pickups" block of the asset settings.
- Authority: firing, ammunition and power-up timers are the server's. Replicated on the ship: weapon and power-up
  flags to everybody, ammunition and end times to the owner. Everything is lost with the ship.
- Overdrive is prediction-safe: `UHCJShipMovementComponent` applies the multiplier on the server and, from the
  replicated flag, on the owning client; the multipliers travel in the match config.
- G drops a pickup weapon. Bots collect what they can use and keep each weapon to its range.
- Log: `Pickups t=...` every 15 s on the server (pads holding an item, items taken by type, kills by weapon);
  `Local t=...` on every client (pads known, own weapon, movement corrections received).
- Development console commands: `HCJGive <0..8>` gives the local ship an item. `HCJMenuCmd padview
  <uu>[,<item A>[,<item B>[,<yaw>[,<hull>]]]]` (listen host, with `peace`) puts the own ship that far from the nearest
  filled pad of its sector, looking at it; `<item A>` is put on that pad, `<item B>` on the pad next to it (the ship
  then looks at both from the side), `<yaw>` turns the ship away afterwards, `<hull>` sets the hull points - for
  pictures of the pads, the markers and the low-hull pointer.
- **Finding the pads** (devlog item 38; local looks, nothing replicated). Every item has a KIND with a colour of its
  own (`HCJ::GetItemKind`, `GetItemKindColor`): **health** (repair, shield cell) green, **weapons** (the four pickup
  weapons and the torpedoes) amber, **power-ups** (overdrive, quad damage, cloak) magenta.
  - In the world (`AHCJPickup`, `HCJPickupLook`): pad and item are half as big again (pad 6.2 m, item 2.3 m); above a
    filled pad stands a column of light 42 m high in the kind's colour (a cylinder with the glow material - opaque,
    so a bright nebula behind it does not wash it out) and around the item pulses a halo (the glint material of the
    ships, with its dark backing). Both keep a minimum size on the screen: the column is at least 0.8 % of the
    distance to the view wide (about 5 pixels at 1280x720), the halo at least 9 %. The pad shows the kind's colour,
    the item keeps its own. An empty pad has neither column nor halo. `PickupBeamBrightness`,
    `PickupHaloBrightness`, `PickupHaloBacking` in the asset settings.
  - On the HUD (`AHCJHUD::DrawPickupMarkers`): the six nearest filled pads of the sector (up to 450 m, i.e. all of
    them; 120 m before) have a marker - a cross for health, a diamond for a weapon, a triangle for a power-up, in the
    kind's colour - with a short name and the distance ("REPAIR  120 m"), smaller and fainter far away, never on a
    HUD panel. **On a low hull** (below 35 %) the nearest REPAIR pad - a SHIELD cell when there is no repair - is
    drawn larger and pulsing inside a ring, and when it is out of view three chevrons at the screen edge point to
    it with its name and distance. This no longer depends on the hints being on. The label strings are kept from
    frame to frame (`FPickupLabel`): none is built per frame.
  - On the radar: the same three icons in the kind's colours instead of small diamonds in the item's colour; the
    pad the HUD points at is larger, pulses inside a ring and is drawn on the rim when it is out of range.

## Audio

**Music: three tracks generated with Google Lyria (Gemini)** in `/Game/Audio/Lyria` (imported from
`SourceAssets/Lyria/*.mp3` by `docs/tools/import_lyria.py`; the editor imports MP3 directly): "Starward Bound" on the
title menu, "Gravity Well Hunt" in a match while calm, "Stellar Breach" while the local ship fights (music intensity
0.75 or more; back below 0.3 after at least 20 s), cross-faded over 2.5 s. A track that fades out is paused and
goes on from there. The game loops each track itself (the files end in an outro): the next pass starts at 157 /
157 / 163 s and is cross-faded. Gains 0.30 / 0.26 / 0.32 bring them from -14.3 dBFS RMS to about -25 dBFS, the level
of the older stems mix (measurements: `docs/ASSETS.md`). SETTINGS > Music switches to those stems ("Lyra stems").

Effects and the stems are sound waves of Epic's "Lyra Starter Game" (Fab), copied into `/Game/Audio`; the sound classes
and submixes (Music, SFX, UI) are under `/Game/HCJ/Audio` (`docs/tools/setup_audio.py` rebuilds them). Everything is
named in the "audio" block of `[/Script/HypeCubeJam.HCJAssetSettings]` and falls back to silence.

- `UHCJAudioDirector` (world subsystem, never active on a dedicated server): the music tracks (or the stems on a bar
  clock) with an intensity that follows the fight, the interface cues of the local player, the black hole rumble, group volumes.
- Positioned one-shots (`UHCJAssetSettings::PlaySoundAt`): one shared attenuation, one voice limit per group.
- Volumes: `MusicVolume`, `SfxVolume`, `UiVolume` in the config, or at run time the console variables
  `hcj.Audio.MusicVolume`, `hcj.Audio.SfxVolume`, `hcj.Audio.UiVolume` (0..2; negative = config value).
- The client log has an `Audio` line every 15 s: active sources of the device, sounds started per group, music state;
  with the tracks also a `Music` line (track, position, loops, switches, weights, gain) and one line per switch.
- The mix has not been checked by ear.

**Computer voice.** The ship computer speaks twelve short warnings to the local player: "All systems online." (once
per run, on the first ship), "Fore / Aft / Port / Starboard shield down.", "Hull critical.", "Torpedo incoming.",
"Missile locked on.", "Enemy behind." (the condition of the HUD's ENEMY BEHIND line, after its 0.3 s dwell), "Gravity
well. Boost away.", "Tractor beam. Boost free." and "Sector clear." (the own kill of a ship left nobody the HUD
counts and no live turret in the sector, and the player is still in it 1.5 s later). SETTINGS >
"Computer voice (spoken warnings)" switches it off (`UHCJUserSettings::bComputerVoice`, on by default; console
`hcj.Audio.Voice 0 / 1`); it follows the Interface volume, and `VoiceVolume` in the config sets its level on top.

- The waves (`/Game/HCJ/Audio/Voice/VO_Computer_*`, 44.1 kHz mono, 1.0-2.2 s) were rendered with the **Flite speech
  synthesiser (Carnegie Mellon University)**, voice cmu_us_rms, and processed (band-pass, a little ring modulation, a
  chirp in front); `docs/tools/import_voice.py` imports them from `SourceAssets/Voice`. No text-to-speech runs in the
  game, so the web build plays them like any other sound.
- It must not nag (`FHCJVoiceLimiter`, `Public/Audio/HCJVoiceLogic.h`, tested): nothing in the first 2.5 s of a ship;
  one line at a time (a line of priority 90 or more - torpedo, missile, gravity well, hull - may cut a lower one
  off); 5 s of silence after a line (1.5 s before an urgent one); a cooldown per line (30 s; hull and enemy behind
  60 s; sector clear 90 s; the four shield lines share one); a line that is not urgent is refused once 3 lines were
  spoken in the last 60 s, every line once 5 were. A refused line is dropped, not queued - the beeps and HUD texts
  that existed before still warn. Nothing is spoken on the title menu, after the own ship is destroyed or during the
  intermission; a line that is being spoken when the ship dies, the match ends or the voice is switched off fades
  out in 0.15 s (`Voice: the line being spoken is faded out (...)`).
- Log: `Voice: VO_Computer_<line> (n spoken, m declined so far)` for a spoken line, `Voice: VO_Computer_<line>
  declined, <reason> (...)` for a refused one.
- Nobody has heard it: wording, level against the music and whether "Port shield down" is understood are unverified.

## Assets

Third-party content (full list with authors and licences: `docs/ASSETS.md`):

| What | Pack | Where |
|---|---|---|
| Ships: 40 hulls, 10 colour skins | Spaceship Modular Star Sparrow (Ebal Studios, Fab) | `/Game/StarSparrow`, imported from `SourceAssets/StarSparrow` |
| Asteroids: 4 boulders | Quixel Megascans (Fab) | `/Game/Megascans/Asteroids`, imported from `SourceAssets/Megascans` |
| Space stations | **Space Station 4 by Gerardo Justel** (Fab, CC-BY) | `/Game/SpaceStation4`, imported from `SourceAssets/SpaceStation4` |
| Intro video | **generated with Google Veo (Gemini)** | `Content/Movies/intro_veo.mp4` (loose file), from `SourceAssets/Veo` |
| Music: three tracks | generated with Google Lyria (Gemini) | `/Game/Audio/Lyria`, imported from `SourceAssets/Lyria` |
| Computer voice: twelve spoken warnings | **Voice: Flite speech synthesiser, Carnegie Mellon University** (rendered offline, BSD-style licence in `docs/NOTICES.txt`) | `/Game/HCJ/Audio/Voice`, imported from `SourceAssets/Voice` |
| Sound effects and the alternative music stems (47 waves) | Lyra Starter Game (Epic Games, Fab) | `/Game/Audio` (copied from the launcher vault, original paths) |
| Bolt mesh, glow material, engine and explosion sounds | Sci-Fi Modular Hover Vehicles (Fab) | `/Game/HoverVehicles` (pack copied unmodified) |
| Muzzle flash, trails, impacts, explosion | Niagara Examples (Fab) | `/Game/NiagaraExamples` (pack copied unmodified) |
| Stars (HYG catalogue), Milky Way, planets, Moon | engine plugin CelestialVault | `/CelestialVault` |

- Every asset is named in ONE place: `Config/DefaultGame.ini`, section `[/Script/HypeCubeJam.HCJAssetSettings]`
  (class `UHCJAssetSettings`, `Public/Assets/HCJAssetSettings.h`, also under Project Settings > Game). To swap
  an asset, change its path there; mesh sizes, pivots and facing are measured from the mesh bounds at run time
  (plus `ShipMeshRotation` and the per-rock `CornerFactor`). Lists (`+ShipMeshes`, `+ShipSkins`, `+AsteroidMeshes`)
  take any number of entries.
- Fallback: a path that is empty, missing or of the wrong class is reported once in `LogHCJ` and that piece
  falls back to what the game did before it had assets: cone/cube/cylinder ship, sphere bolt, basic-shape
  asteroids and portals, placeholder stars, no effect, no sound. `-HCJNoAssets` forces this for everything.
- Assets are cosmetic. All collision is basic shapes (asteroid shapes of the layout, ship capsule, bolt sphere,
  portal trigger), identical on every machine whatever assets it has; a dedicated server loads none of them.
- Variety without replication: a ship's hull is `PickShipVariant(PlayerId)`, its skin the one nearest to the
  player colour, an asteroid's rock `HCJ::PickAsteroidVariant(sector seed, index)`, a sector's sky
  `FHCJSectorSky::Make(match seed, sector)`.
- Cooking: the settings are soft paths, so the folders holding used assets are listed under
  `+DirectoriesToAlwaysCook` in `DefaultGame.ini` (never a whole pack). Add the folder when you point a setting
  at a new place.
- `SourceAssets/` is raw source art (FBX, PNG/JPG as downloaded), not game content: nothing reads it at run time
  and it is not packaged. Keep it out of the engine's `Content/` folder; re-import from it when needed.
- Disk: `Content/` is about 2.3 GB (two complete Fab packs, mostly unused), `SourceAssets/` about 120 MB.

### Backdrop

`AHCJSpaceBackdrop` (one local, cosmetic actor per machine; never on a dedicated server) follows the camera and
draws the sky "at infinity": 5070 real stars of the HYG catalogue up to magnitude 6 (right ascension/declination,
magnitude, B-V colour) in one instanced draw, the Milky Way dome, one or two Solar System bodies per sector (the
Moon as a disc with a phase; planets as spheres, see below), and 3 procedural nebula cards tinted from the
sector colour. Planets (`M_HCJ_PlanetSurface`, `M_HCJ_PlanetRing`, built by `docs/tools/make_planet_materials.py`; no
new texture): each is its own sphere whose material projects the planet's picture from the CelestialVault atlas along
the line of sight (Saturn is procedural bands, because its picture has the rings in it), lights the day side from the
sector's sun with a soft terminator, lets a band / cloud pattern drift slowly, and adds an atmosphere rim in the
planet's colour; Saturn and (faintly) Uranus get rings on one quad - banded, semi-transparent, with the planet's
shadow across them. `BodySizeScale=1.5` makes them half as big again as before. Which bodies and where follows from (match seed, sector id), so everybody in a sector sees the
same sky; their names are on the HUD next to the sector id. Stars and imagery are real data; placement of the
bodies, the nebulae and the black hole look are made up. Settings: "Backdrop" block of the asset section
(`bEnableBackdrop`, `StarMaxMagnitude`, `StarMagnitudeOffset`, `MaxBodiesPerSector`, `NebulaCardsPerSector`,
`NebulaBrightness`, ...).

### Capture runs (development builds)

`-HCJCapture=<s>` saves `Saved/Screenshots/HCJCapture/Shot_NN.png` every `<s>` seconds and logs the frame rate of
each interval. Add `-HCJAutoFly` (the local ship flies and fires by itself; with `-HCJAutoTorpedo` also torpedoes, with
`-HCJAutoAim` its nose follows the nearest ship in sight - the autopilot of the playthrough tests; `-HCJAutoAimError=<deg>` makes it a poor shot whose nose wanders up to that many degrees around the target), `-HCJOrbit` (camera looks at the ship
from another side each picture), `-HCJTour` (listen server: hop to the next sector after each picture),
`-HCJSkyCam`, `-HCJHoleTest=<f>`, `-HCJCaptureCmds="a;b;c"` (one console command per picture, `_` for a space). See the
comment in `Private/HypeCubeJam.cpp`. For repeatable pictures: `HCJMenuCmd scene <sector>,<look>[,<spawn point>[,<distance>]]`
puts the own ship (undamageable) at a spawn point of the sector looking at `body0`, `body1`, `nebula0..2`, `dark`,
`portal<n>`, `wormhole` or `station` (for the last three from `<distance>` uu), `HCJMenuCmd peace` holds the bots still,
`HCJParkEnemy <uu>` parks one ahead. A capture run never plays the intro (`-HCJIntro` with `-HCJMenu` forces it).

Measured on a GTX 950, 1280x720, listen server with 7 bots, seed 1234 (editor binary, `-game`): about 100-125
fps with `-HCJNoAssets` (basic shapes), about 95-105 fps with the Fab assets, about 80-105 fps with the backdrop
as well.

## Packaged build

```powershell
& "F:\EpicGames\UE_5.9\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="F:\work\HypeCubeJam\HypeCubeJam.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="F:\work\HypeCubeJam\Packaged\Temp" -utf8output -unattended -nocompileeditor -skipbuildeditor
```

Since the engine update of 22 Nov 2026 (5.9.0 CL 60738873, with a new Fab plugin) the cook logs an engine ensure
("Registered the path /ToolsetRegistry/ for two different domains", `AssetReferencingDomains.cpp`) and counts its 29
lines as errors, so the command above ends with BUILD FAILED although every package cooked; append `-IgnoreCookErrors`
(devlog item 40). Builds of the engine before that update (0.21.0 and older) cannot join builds made after it and the
other way round: the engine's network changelist is part of the network version.

Double-clicking `HypeCubeJam.exe` opens the title menu; PLAY hosts a listen server with bots (the world behind the
menu already is one: `LocalMapOptions=?listen` in `DefaultEngine.ini`). `HypeCubeJam.exe -AutoHost` hosts without the
menu. To join from another instance or machine: JOIN in the menu, or `HypeCubeJam.exe <ip>` (e.g.
`HypeCubeJam.exe 127.0.0.1`). `-windowed -ResX=1280 -ResY=720` for a window.

Rendering defaults in `Config/DefaultEngine.ini` are direct lighting only (no Lumen, cascaded shadow maps,
TAA): on a GTX 950 at 1280x720 that is ~80-100 fps instead of ~26; the file says what to flip on stronger
hardware.

## Tests

```powershell
& "F:\EpicGames\UE_5.9\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "F:\work\HypeCubeJam\HypeCubeJam.uproject" -ExecCmds="Automation RunTests HypeCubeJam; Quit" -unattended -nullrhi -nosplash -log "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocol.ModelContextProtocolSettings]:bAutoStartServer=False"
```

87 automation tests (about a minute; three of them new with devlog item 40: `HypeCubeJam.Balance.BeginnerArmsAndAim` -
Easy alone has the reach, the start weapon, the aim assist and the weapon pointer, `-HCJClassicEasy` gives back Easy of
0.21.0 field by field, the assist's choice of target -, `HypeCubeJam.Torpedo.HumanHoming` - a person's torpedo against a
bot's: target choice, keep and lose, lead, closing on a crossing target -, `HypeCubeJam.UI.GuidePad` - which pad the
pointer leads to; three new with devlog item 38: `HypeCubeJam.Balance.BeginnerHelp` -
Easy alone has the help, the classic switch, the hull mending rule -, `HypeCubeJam.Pickups.Visibility` - item kinds,
their colours and short names, the size of the light column and the halo by distance -, `HypeCubeJam.UI.PickupMarkers`
- the nearest-pads list, marker strength, distance text; four of them `HypeCubeJam.Input.*`: the aim stick's dead zone and curve, the
controller switch and the device in use, the menu focus, the pad names of the prompts). Two of them are playthroughs (`HypeCubeJam.Playthrough.*`): a real game world
with the game mode, the universe and bots, ticked by hand at 30 frames per game second - a match of six hard bots to
a frag limit of 4 and the start of the next one on another layout, and three minutes of an open match of eight bots
(kills, portal jumps, pickups, every ship type, nobody stuck or outside its sector). About 15 and 20 s of real time.

### Playthrough runner

```powershell
python docs\tools\playthrough.py                      # all scenarios, editor binary, headless; about 7 minutes
python docs\tools\playthrough.py --binary packaged    # the packaged build (add --render for windows)
python docs\tools\playthrough.py --only net_host_client_late,autopilot_hangar_match
python docs\tools\playthrough.py --list
```

Starts whole matches as processes (`-nullrhi`, `-HCJNoEOS`, `-HCJNoStats`, ports 7791 upwards, each process quits by
itself with `-HCJQuitAfter`), reads the `Playthrough: event=... key=value` lines of their logs and prints one row per
scenario; exit code 1 when one failed. Logs go to `Saved/Playthrough/<time>/`. Scenarios:

| scenario | what runs | what must hold |
|---|---|---|
| `match_cycle` | 8 normal bots, kill limit 5 | the match ends by the limit, winner first on a board of 8 rows sorted by kills, match 2 starts on another seed and layout checksum |
| `difficulty_easy` / `_normal` / `_hard` | 8 bots, 230 s, no limits | at that difficulty: kills, every ship type kills and dies, portal and wormhole jumps, pickups, warp / cloak / tractor used |
| `small_universe` | 8 easy bots on 4 sectors, 260 s | a crowd: kills; its turret and rock numbers go into the sum below |
| `net_host_client_late` | listen host (`-HCJNoFair`: the score checks below need easy bots that treat a person like any ship) + client (25 s later) + late joiner (100 s later), all on the autopilot with aim; the client leaves after 48 s and joins again | same seed and layout checksum everywhere; every destroyed-rock set a client had is one the host had (count and checksum; the host breaks three rocks on purpose - `HCJMenuCmd breakrock` after 64, 128 and 168 s - so that there is always something to compare); client and late joiner get ships and have at least one kill and one death between them (an autopilot does not score every time); at most 30 movement corrections per 100 s; the host saw three remote joins and the client leave |
| `autopilot_hangar_match` | title menu, hangar (cruiser, hull 2, paint 5), PLAY, kill limit 4 | the choice reaches the server, the match ends by the limit, the player is on the board as a cruiser with a kill or a death, the next match starts |
| `all_matches_together` | the logs of the above | every weapon has a kill, every pad item was taken (cloak cells excepted), a turret shot a ship down, a turret was destroyed, a rock was destroyed |

Every process must also: quit by itself, have no `Error:` / ensure / fatal line (one known engine plugin message
excepted), no NaN, no `stuck` / `out_of_bounds` / `nan` event, a mean server frame below 60 ms and none above 3 s.
Scenarios run four at a time, so kill rates are lower than in a single match; the thresholds are loose on purpose
and say nothing about balance.

Structured lines (server): `match_start`, `match_end`, `board`, `stats` (all counters of the match, every 15 s),
`human`, and the warnings `stuck` (a bot's ship moved less than 2.5 m in 20 s; the line says what the bot was doing),
`out_of_bounds`, `nan`; clients: `client` every 10 s.

### Fairness matches

How the bots treat a human player (see "Bots and people: the fairness rules") is measured with one human on an
autopilot against seven bots, headless, several processes at a time, and read out of the server's `Fair:` lines:

```powershell
python docs\tools\fair_matches.py Saved\Fair easy_nofair:0:nofair:3 easy_fair:0:fair:3
python docs\tools\fair_matches.py Saved\Fair blind_nofair:0:nofair:3:blind blind_fair:0:fair:3:blind
python docs\tools\fair_matches.py Saved\Fair classic:0:classic20:3 help:0:fair:3   # Easy of 0.20.0 against Easy now
python docs\tools\fair_matches.py Saved\Fair before:0:classic:3:poor8 after:0:fair:3:poor8   # 0.21.0 against now, a poor shot
python docs\tools\fair_stats.py Saved\Fair\*.log
```

`<tag>:<difficulty 0..3>:<fair|nofair|classic|classic20>[:<count>[:blind|:poor<deg>]]` - `nofair` adds `-HCJNoFair` (the bots before the
rules), `classic` adds `-HCJClassicEasy -HCJClassicTorpedo` (the game of build 0.21.0: Easy without the help of item 40, a
person's torpedo as a bot's), `classic20` adds `-HCJClassicEasy20 -HCJClassicTorpedo` (Easy of build 0.20.0), `poor8` gives the
aiming autopilot `-HCJAutoAimError=8` (its nose wanders up to 8 degrees around the target: a poor shot, to show an aim
assist), `blind` takes the autopilot that only circles and fires straight ahead instead of the one that aims (the aiming one
faces its attacker, so only the blind one shows how much damage comes from behind). `fair_stats.py` prints per log
and per group: deaths per minute, the share of bot damage from the player's rear arc, deaths by a bot in the rear
arc, deaths within 12 s of the spawn, the most bots on the player at once, and how often the cap, the rear rule,
the arrival grace and the respite held a bot back, the player's own kills per minute and kills over deaths, and what
the beginner's help did (the player's damage on bots and the part the scale added, hull points mended, repair pads
taken), and item 40's numbers (pads people took and how many by the Easy reach, armed spawns, people's bolts, assisted
bolts, hit rates, people's torpedoes: fired, homing, found without a lock, lost, hit). It works on any game log - also the one of a real match (`Saved\Logs\HypeCubeJam.log` of the host).
Numbers: docs/DEVLOG_NOTES.md, items 29, 31, 38 and 40.

## MCP (Unreal MCP server)

The project enables the engine's `ModelContextProtocol` plugin ("Unreal MCP") plus a default set of editor
toolsets (Editor, Console, ConfigSettings, AutomationTest, LiveCoding, Input, Perf, SceneCapture), all
editor-only.

- The server starts with the editor (`bAutoStartServer=True` in `Config/DefaultEditorPerProjectUserSettings.ini`)
  and listens on `http://127.0.0.1:8000/mcp`. It does not start in commandlets; pass
  `-ModelContextProtocolStartServer` to force it, `-ModelContextProtocolPort=<n>` to change the port.
- `-game`, `-server` and automation runs of the editor binary are not commandlets, so they auto-start the
  server too and would compete for port 8000 (a later process only logs an error, but `.mcp.json` then
  reaches whichever process bound the port first). The editor must own port 8000 for `.mcp.json` to reach
  it, which is why the run and test commands above switch auto-start off with an `-ini:` override.
- `.mcp.json` in the project root registers it for Claude Code as the `unreal-mcp` HTTP server; start
  Claude Code in this directory with the editor running. Configs for other clients can be generated from
  Editor Preferences > Model Context Protocol.
- Project instructions sent to MCP clients live in `Config/DefaultGame.ini`
  (`[/Script/ModelContextProtocol.ModelContextProtocolProjectSettings]`).
