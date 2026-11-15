# HypeCubeJam - development notes

Facts only, in the order things happened. Hardware for all measurements: GTX 950 (2 GB), Windows 11, Unreal
Engine 5.9, 1280x720, seed 1234, listen server with 7 bots unless stated. Work was done by AI coding agents
(Claude Code) driven by a coordinating agent; the human supplied the Fab downloads and the direction.

## Starting point (before 14 Nov 2026)
- C++-only multiplayer space shooter: sectors are the vertices of a randomly connected hypercube (tesseract by
  default, 16 sectors), linked by portals; bots fill empty slots. Everything was drawn with engine basic shapes;
  no audio, no particle effects. 9 automation tests. About 80-100 fps on the GTX 950 after render defaults were
  lowered (no Lumen, no virtual shadow maps, TAA).

## 14 Nov 2026 - assets, sky, black holes
1. Asset plumbing. One settings class (`UHCJAssetSettings`, soft paths in `DefaultGame.ini`) names every asset;
   every consumer keeps a basic-shape / silent fallback. A headless editor Python pass measured the candidate
   assets first (bounds, pivots, material slots, Niagara user parameters, dependencies). All inspected pack assets
   loaded in 5.9 without errors.
2. First pass with two Fab packs already in the project: hover tank as the ship, Megascans debris rocks from the
   Niagara Examples pack as asteroids, bolt mesh, Niagara muzzle flash / trail / impact / explosion, engine and
   explosion sounds, glowing portals.
   - Problem: the rock material lacked the "used with instanced static meshes" flag (warning "Default Material
     will be used in game"). Fixed by setting the flag; later made moot (and reverted) when the rocks were replaced.
   - Problem: asteroid collision must be identical on server and clients and the layout generator is covered by
     golden-value tests. Decision: collision stays the invisible basic shapes of the layout; rocks are drawn only,
     fitted into the collision shape from their measured bounds; the variant is a hash of (sector seed, index).
   - The pack has no weapon fire sound (its "projectile" cue is the engine loop), so firing is silent.
3. Ship replaced by "Spaceship Modular Star Sparrow" (Ebal Studios): 40 FBX hulls, 21 textures imported by script,
   one master material + 10 colour instances created by script. Imported meshes are about 11 m long with the nose
   along +Y; the game scales them to 260 uu and yaws them -90 degrees. Hull = hash of the player id, skin = the
   colour nearest to the player colour, so every machine shows the same ship without new replicated state.
   - Problem: the script's material compile needs a renderer in a commandlet; the compile call was dropped (the
     material compiles on first load).
4. Asteroids replaced by four Quixel Megascans boulders (2K maps imported at 1K: base colour, normal, roughness,
   AO). Each file is one closed rock (checked by Euler characteristic). 3354-11252 triangles at LOD0; 4 LODs
   generated (100/50/25/12.5 %).
5. Sky: real stars (5070 of the 10 000-star HYG table up to magnitude 6), Milky Way, Solar System planets and
   Moon, procedural nebulae, from the engine's CelestialVault plugin content plus three small procedural
   materials built by script. Tuning from screenshots: star magnitude offset -3, nebula brightness 0.22, portal
   glow reduced (it was blown out to white), planets changed from the plugin's unlit discs (blurry at this size)
   to lit spheres.
6. Black holes became gameplay: placement from the seed (own hash, layout untouched), pull applied inside the
   ship movement simulation as a pure function of position, death at the event horizon, bots avoid them, HUD
   warning and minimap mark.
   - Measurement (headless server + client): a client ship placed 2700 uu from a hole drifted into the horizon
     twice; the client logged 2 movement corrections in the whole run, both of them the test teleports themselves.
     0 bot deaths by black hole in 76 s; 2 of 16 sectors had a hole for seed 1234.

7. First packaged build (Win64, Development, pak/IoStore) with `RunUAT BuildCookRun`.
   - Problem: the first cook failed (ExitCode 25): generated manifest assets of the editor-only ToolsetRegistry
     (MCP) plugin were pulled into the cook and their class does not exist in the game. Fixed with
     `+DirectoriesToNeverCook=(Path="/ToolsetRegistry")`.
   - Only folders holding used assets are always-cooked; the soft paths in the settings were also followed by the
     cooker (the bolt mesh, whose folder is not listed, was cooked). 975 packages cooked.
   - Sizes: 357 MB content (.ucas), 347 MB executable, 390 MB .pdb (not shipped). Zip
     `Packaged/mariuz_HypeCubeJam.zip`: 502.8 MB (788 MB unpacked).
   - Run of the packaged exe without arguments: listen server with 7 bots, 75 of 75 configured assets loaded,
     87-109 fps at 1280x720; a second instance started with `127.0.0.1` joined, layout and black hole checksums
     equal on both. Esc quits (added for the packaged game), `LocalMapOptions=?listen` makes double-click host.
   - Packaged log: about 8 D3D12 "Failed to create compute pipeline state" errors on the GTX 950 (not seen with
     the editor binary), one CelestialVault plugin CDO error about a missing redirector, one ToolsetRegistry
     warning. The match runs regardless.
   - Problem: the packaged `HypeCubeJam.exe` in the root is a launcher; stopping it by PID left the real game
     process running, which then starved the next run of video memory (GPU device removed at frame 5). The
     leftover process was found and stopped; later runs were clean.
8. Media. No ffmpeg on the machine, so the video was made without it: the game dumps one PNG per frame at a
   fixed 30 fps step (`-benchmark -fps=30 -HCJFrameDump=1200`, about 5 frames per second of wall time) and
   `docs/tools/frames_to_avi.py` (Pillow only) writes a Motion-JPEG AVI: `Media/HypeCubeJam_gameplay_40s.avi`,
   1280x720, 30 fps, 40 s, 82 MB, NO AUDIO, the local ship flown by the capture autopilot against 7 bots.
   Screenshots: `Media/01..06_*.png` (three at 1920x1080, three at 1280x720) and `Media/cover_630x500.png`.
   At 1920x1080 the editor binary showed the "video memory exhausted" message in some pictures; those were not used.

## 14 Nov 2026 (second session) - audio
9. Sound. Source: Epic's "Lyra Starter Game" in the launcher vault (945 audio assets). 47 sound waves were copied
   into `/Game/Audio` with their original paths (a script scanned the files for `/Game/` references: the only
   dependencies were Lyra's SFX and UI sound classes). `docs/tools/setup_audio.py` (headless editor Python)
   created three sound classes and three submixes (Music, SFX, UI) and re-homed every wave onto them; all 47 load
   in 5.9 with 0 errors and 0 warnings. No MetaSound of Lyra is used.
   - Problem: the Lyra music exists only as stems for its MetaSound music system (single bars of percussion, two-
     and four-bar pads, bass and lead phrases, each with a tail). Durations measured by script: 2.70 s percussion,
     5.47 s plips, 4.79 s bass, 11.6 s pad, 13.0 s lead, which fits one bar = 2.667 s (90 bpm, assumed).
     Decision: a small sequencer in C++ (`UHCJAudioDirector`, a world subsystem) starts each stem again every 1, 2
     or 4 bars. Layers switch on with an intensity value: pad and bass always, percussion and plips with other
     ships nearby, second percussion and lead while the local ship fires, hits or is hit. Intermission: the menu pad.
   - Wired: fire sound per bolt (4 variations, +-6 % pitch), impact on rock / on ship, hit confirm for the shooter
     (new unreliable client RPC, also draws a hit marker on the HUD), damage taken, low hull, kill, death, respawn,
     match start, match won / lost, boost, portal exit, black hole rumble (volume and pitch follow the depth in the
     well), a second explosion layer. Engine loop and first explosion layer are the earlier Hover Vehicles sounds.
   - One attenuation for all positioned one-shots (natural-sound falloff, -40 dB at 15 000 uu, not panned inside
     300-900 uu), voice limits per group (fire 8, impact 6, explosion 4, world 6, interface 6), volumes per sound
     class from config and from the console variables `hcj.Audio.MusicVolume / SfxVolume / UiVolume`.
   - Verified by log only: the game prints the active source count of the audio device every 15 s. Listen server
     with 7 bots (packaged build): 9-20 active sources of 32 channels, e.g. after 60 s 619 fire, 163 impact, 10
     explosion, 23 world and 26 interface sounds and 78 music stems had been started. NOBODY HAS LISTENED TO THE MIX:
     levels, the choice of each sound and the assumed tempo are untested by ear.
10. Second packaged build: 995 packages cooked, zip 504.6 MB (790 MB unpacked). Packaged exe without arguments:
    116-144 fps at 1280x720 alone; a second instance joined with `127.0.0.1` (same layout and black hole checksums);
    with both instances on the one GPU about 50 fps each.
    - Problem: the zip written with .NET `ZipFile.CreateFromDirectory` from Windows PowerShell 5.1 had backslashes in
      its entry names; `docs/tools/make_zip.ps1` now adds the entries one by one with forward slashes.

11. Video with sound. The engine can render audio offline: `-NonRealtimeAudio` makes the audio device render one
    fixed step per game tick, and the capture code records the master output (submix recording) for exactly the
    dumped frames. `docs/tools/frames_to_avi.py` now muxes that WAV into the AVI as PCM, can scale frames, drop
    frames and reduce the audio.
    - Problem: the first attempt recorded nothing ("Output recording is an audio mixer only feature"): `-benchmark`
      switches all audio off. `-UseFixedTimeStep -FPS=30` gives the same fixed step with audio.
    - Result: `Media/HypeCubeJam_gameplay.avi` (1280x720, 30 fps, 45 s, 48 kHz stereo, 87 MB) and
      `Media/HypeCubeJam_gameplay_small.avi` (512x288, 15 fps, 24 kHz mono, 7.7 MB). Windows reports 45 s and the
      audio bit rate for both. The recorded mix: peak 0 dBFS, RMS -16 dBFS, no clipped samples, no silent stretch
      (`docs/tools/wav_stats.py`); after that the effects and music volumes were lowered a little. Still not
      listened to.

## 14 Nov 2026 (second session) - pickups and weapons
12. Pickup pads: 3 to 5 per sector (64 in a 16-sector match), placed like the black holes by a pure function with
    its own hash stream from the finished layout, clear of rocks (500 uu), portals (2500), spawn points (800), the
    strong pull of a black hole (0.6 of its radius) and each other (3000). Layout checksum and golden tests
    unchanged; a pad checksum is logged on server and client (D547574B on both for seed 1234).
    - What lies on a pad is run-time state: one replicated actor per pad (item type + active flag), spawned by
      the server. Collected by server-side overlap, refilled after 20-30 s with a weighted random item.
    - Items: repair (+60 hull), shield cell (+50 shield, up to twice the normal maximum), overdrive (10 s, speed
      x1.5), quad damage (10 s, x4), cloak (8 s; other machines do not draw the ship, bots lose it beyond 2500 uu,
      firing ends it), and four limited-ammo weapons that fall back to the blaster: scatter cannon (8 pellets),
      rail shot (60 000 uu/s, 70 damage), homing missiles (turn 110 deg/s towards the nearest ship in a 40 degree
      cone, Hover Vehicles missile mesh), rapid pulse (14 shots/s). Weapons are a config table.
    - Overdrive and prediction: the speed multiplier is applied by the movement component on the server and on
      the owning client; the server never takes it from the client. The client stores the flag in each saved move
      so corrections replay correctly. Measured on a client of a dedicated server, overdrive given every 12 s:
      6 movement corrections in 45 s, against 5 in 45 s without overdrive (portal jumps and respawns).
    - Bots fly over a pad that holds something they can use (10 000 uu when idle, 4000 uu as a detour in a fight)
      and fire each weapon only inside its range. Dedicated server, 7 bots, 213 s: 68 items taken by bots; kills:
      blaster 24, scatter 2, rail 1, missiles 2, pulse 6.
    - Late join: a client that joined 50 s into the match logged 3-8 pads known in its sector with the same
      active count pattern as the server (pads are net-culled per sector like ships).
    - HUD: weapon and ammunition, power-up timers, a message for each pickup, markers over nearby pads.
    - Automation tests: 16 (new: pad placement, weapon table / ammunition logic).

## 14 Nov 2026 (second session) - wormholes
13. Wormholes: two-way shortcuts between sectors that are not hypercube neighbours (ids differ in two or more
    bits). Which sectors are linked is a pure function of (seed, dimension, count): a seeded shuffle of the sector
    ids, then greedy pairing; a quarter of the sectors' number by default (4 links in a tesseract), one mouth per
    sector at most, none below dimension 3. Each mouth is placed in clear space from the finished layout (rocks
    1100 uu, portals 4000, spawn points and pads 1500, outside the whole influence radius of a black hole); a
    link whose two mouths cannot both be placed is dropped. Graph and layout generators, their checksums and
    golden tests are unchanged; a wormhole checksum is logged on both sides (A385A205 for seed 1234, links
    0000-1110, 0001-1101, 0011-1001, 0110-1111).
    - Travel reuses the portal path: the mouth is an `AHCJPortal` in wormhole mode (destination sector and world
      location), the ship gets the same server-side pending teleport, a pure translation from mouth to mouth.
    - Routing: a separate route table (BFS over portals + wormholes) next to the hypercube graph, whose own
      queries still answer for portals only. Bots hunt and roam with the new table.
    - Look: a glowing core inside the black hole card material in another colour, turned towards the camera;
      dashed violet links on the minimap; "WORMHOLE > sector" label in the sector; its own exit sound.
    - Measured (dedicated server, 7 bots, 92 s): 33 portal jumps, 4 wormhole jumps. Automation tests: 18 (new:
      links / mouth clearances, routing never longer than portals only and symmetric).

## 14 Nov 2026 (second session) - space stations with turrets
14. Stations: in about a quarter of the sectors (7 of 16 for seed 1234), placed like the other features by a pure
    function with its own hash stream after pads and wormholes, 5000-10 000 uu from the sector origin, clear of
    portals (5500 uu), spawn points (3500), pads (3000), the wormhole mouth (4000) and 2500 uu outside the
    influence radius of a black hole. Asteroids are not a placement constraint: those within 3000 uu of the
    station centre are simply not built - a filter applied on every machine on top of the unchanged layout
    (21 asteroids for seed 1234). Layout generator, checksum and golden tests unchanged; a station checksum is
    logged on both sides (1A4ADC81).
    - Body: a fixed shape of 8 boxes (hub, two beams, mast, four solar panels), which are its collision on every
      machine, drawn as tinted boxes with glowing beacons.
    - The Fab model "Sci-Fi Space Station" (Helindu.Art) was imported in a headless test
      (`docs/tools/import_station.py`): one mesh of 2 114 738 triangles and 49 material slots, 3918 x 1498 x 1308
      units, generic material names with no link to the supplied texture sets. Decision: not used in this build.
      Its outline does not match a collision shape that could be written without looking at it, reducing it to a
      few percent and assigning 49 materials by eye was not possible in the time; nothing of it was saved to
      `Content/`. The game has a `StationMesh` setting that draws a model instead of the boxes.
    - Turrets: 3 to 6 per station on a seeded choice of 6 mounts, replicated actors spawned by the server (alive /
      destroyed, health and aim direction replicated). Server AI every 0.25 s: nearest living, uncloaked, not
      spawn-protected ship within 6500 uu and in line of sight (rocks and the station block it); the gun turns at
      70 deg/s towards the lead point (`HCJ::ComputeLeadAim`, tested) and fires every 0.5 s (9 damage, 14 000
      uu/s, 1.5 degrees of spread). 120 hit points, rebuilt 60 s after destruction. Head and gun are Hover
      Vehicles tank meshes fitted by their bounds.
    - Scoring: a destroyed turret gives its killer a "turret" point (own scoreboard column, tie-breaker, not a
      frag) and a kill feed line; a ship shot down by a turret is an environment death shown as "Station".
    - Bots keep wander points out of turret range, and a healthy bot with no ship to fight attacks a turret
      now and then (35 % chance every 6 s within 11 000 uu).
    - Measured, dedicated server, 8 bots, 257 s: 56 deaths, all by players, 0 by turrets (0 %), 153 turret shots,
      4 turrets destroyed by bots. A human ship parked 5000 uu from a station was destroyed by it within 8 s.
      No retuning was needed for the "at most a quarter of deaths" target; whether turrets are too harmless
      against bots is open.
    - Late join: with all 33 turrets destroyed on the server, a client that joined afterwards and went to a
      station logged "3 turrets known, 0 alive".
    - Automation tests: 20 (new: station placement / asteroid filter, lead aim).

## 14 Nov 2026 (third session) - menu and HUD
15. Title menu, in-match menu and a readable HUD, all in C++ (one Slate widget for the menus, canvas drawing for
    the HUD; no widget assets).
    - Title menu on a start without arguments: PLAY (hosts with bots, one click), bots 0-15, hypercube 3D-5D, bot
      difficulty, seed, pilot name, JOIN by address, SETTINGS (mouse sensitivity, three volumes), QUIT. Choices are
      stored in the user's GameUserSettings.ini. The world behind the menu is the listen server itself with the
      bots playing; the host has no ship and is not a combatant until PLAY, which starts a fresh match.
    - Decision: no separate menu map or game mode. A map URL, an address, `-AutoHost`, `-server`, `-nullrhi` or a
      capture flag on the command line skips the menu, so the documented server / client / capture command lines
      and the headless tests run unchanged (a test covers the command-line rule).
    - Esc no longer quits: it opens RESUME / SETTINGS / LEAVE TO MENU / QUIT GAME. A client that leaves, loses its
      host or cannot connect ends in the title menu with a line saying why.
    - HUD: target brackets (name, distance, hull, shield) on enemies in view and not behind a rock, arrows to the
      three nearest enemies out of view, lead circle for the current weapon, spread circle, damage direction arc
      (the server tells the victim where a hit came from - one unreliable client RPC), low-hull vignette,
      destination labels on portals, map legend, neighbour sectors ringed and labelled, kill feed with weapon or
      cause, centre messages (match start, kill, killed by, double / triple kill, rampage), respawn countdown,
      end-of-match board, scoreboard with rank / kills / deaths / turrets / ping, controls overlay (F1, and by
      itself for the first 8 s of the first ship).
    - Cloak: a cloaked ship gets no bracket, arrow or lead circle, and the server stops publishing its sector, so
      the map mark is gone as well (before, the map still showed it).
    - Problem: HUD text was unreadably small at 1280x720 (bitmap-scaled engine font at 0.67). The canvas text item
      accepts a Slate font, but drew nothing in the first try: it still takes its font cache type from a UFont, and
      with none set it silently skips the text. Fixed by passing the engine's runtime font alongside the Slate font
      info; text is now rendered at its final size (never below 0.8 of the 1080p size).
    - Problem: the name typed in the menu did not reach a server joined by address (the engine overrides
      `?Name=` with the platform nickname). Open at the time of this build: joiners show the machine name.
    - Streak detection, HUD scaling, the off-screen marker and the command-line rule are plain functions with tests
      (23 automation tests, all passing).
    - The 24 "Failed to create compute pipeline" lines of every packaged run were traced: the 24 shader hashes
      are in the cooked global shader archive and map to FTSRDecimateHistoryCS (16), FTSRUpdateHistoryCS (6) and
      FTSRResolveHistoryCS (2) - temporal super resolution, which the game does not use (it runs TAA). They were
      start-up precache requests, not Niagara effects. `r.PSOPrecache.GlobalShaders=0` removes them: the packaged
      log of this build has 0 such lines (1 error line left: the CelestialVault plugin CDO message).
      Five of the six Niagara systems in use do contain GPU-simulated emitters; the log shows no failure for them.
    - Verified by runs, not by a person at the keyboard: packaged exe without arguments logged the title menu
      state with 8 bots playing; a second packaged instance opened its own title menu, joined 127.0.0.1 through
      the menu's join action (layout checksum 8AB6F971 on both), opened the in-match menu and left to the title
      menu; menu -> Play -> pause -> resume -> scoreboard -> end of match -> next match ran in one packaged
      session at 109-137 fps (1280x720, alone on the GPU). Menu actions were triggered by a development console
      command, not by mouse clicks: nobody has clicked the buttons or typed in the text boxes.
    - Editor binary, dedicated server + client, 30 s: 6 movement corrections (spawn and portal jumps, as before).
    - Zip: see "Measurements".

## 14 Nov 2026 (third session) - seeing and hitting the enemy
16. Feedback on the first menu / HUD build: enemies were dark specks with thin markers; a player could not find or
    track a target. Changes:
    - Markers: every enemy within 600 m has a bracket in one hostile colour (red; nothing else on the HUD uses
      it) with a minimum size, name, distance, hull and shield bar; behind a rock it is dimmed instead of removed.
      Solid arrows at the screen edge for the six nearest enemies out of view; the attacker's arrow pulses.
    - Ships: hull length 260 -> 330 uu (looks only; collision unchanged), emission of the hull lights x3, bolts
      longer and brighter, and a "beacon": an unlit glowing sphere in the player colour, scaled with the distance
      from the local view so that a far ship is a bright point of constant size. Fill light 0.6 -> 1.0.
    - Targeting: soft lock on the enemy nearest the crosshair (5 degrees of stickiness), orange diamond, lead
      circle tied to the target that turns green ("FIRE") when the aim passes within the hit radius of the lead
      point; T / F / middle mouse to cycle, take the nearest, hold. Target panel with closing speed and range.
      The lock goes to the server (one reliable RPC when it changes) and homing missiles prefer it; the victim
      gets a "MISSILE LOCK" warning. There is no lock-on timer: the lock is immediate.
    - Aim assist: bolts now hit a ship within 100 uu of its centre (was the 60 uu movement sphere). Done with a
      second, query-only sphere per ship that blocks only projectiles and generates no overlap events, so portals,
      pads and ship movement are untouched and the server stays the judge.
    - Hit feedback: bracket flash and floating damage numbers, derived on each client from the replicated hull and
      shield of the ships it sees (no new replication).
    - Radar with enemy count and, in an empty sector, the way to the nearest enemy from the route table.
    - Measured, dedicated server, 8 bots, seed 1234: with the aim assist alone the bots' hit rate went from about
      21 % to 58 % (389 hits of 667 shots) and kills from about 13 to 17 per minute. Together with the weapon
      retune below: 26 kills in 122 s (12.8 per minute), 380 hits of 539 shots.
    - Weapon retune (config): blaster 20 -> 13 damage; scatter 10 x 10 pellets; rail 85; missiles 65, faster and
      tighter turning; pulse 160 rounds. Sustained damage per second at full hit rate: blaster 81, scatter 167,
      rail 106, missiles 108, pulse 129. Kills by weapon in that match: blaster 12, scatter 2, rail 6, missiles 3,
      pulse 3 (before the retune, 213 s: blaster 24, all pickup weapons 11).
    - Turrets (config): range 7500, damage 12, a shot every 0.4 s, 90 deg/s. Same match: 3 of 29 deaths by
      turrets (10 %); before: 0 of 56.
    - Distances: the HUD shows metres (100 uu). A sector is about 400 m across, so "long range" in this game is
      100-250 m. Pictures were taken with an enemy parked at 15, 36, 60, 101 and 253 m against dark sky, a nebula
      and the black hole disc (`HCJParkEnemy`): the marker is obvious in all of them; the hull itself is only
      readable up to about 40 m, beyond that the beacon is what is seen.
    - The turret gun mesh was measured headless (vertex extents along its long axis): the thin end (the barrel,
      radius 13 of 41) is at +X, which is the direction the game points it. The missile mesh is a plain cylinder
      with the same radius at both ends in that measurement; which end is the nose was not established.
    - 28 automation tests (new: target selection, fire-now rule, radar projection, bot difficulty table, spawn
      scoring, boost meter, shield regeneration rule - the last four are wired into the game in the next build).

## 14 Nov 2026 (third session) - flight feel, rules, bots
17. Gameplay pass.
    - Boost became a meter (3.5 s full, 7 s to refill, locked from empty until 25 %). Decision: no replication of
      the meter. It is stepped inside the character movement simulation on the owning client and on the server
      from the same inputs, the same way the boost flag already travelled in the saved moves; the saved moves now
      also carry the meter so that combined moves and correction replays use the values they were predicted
      with. Measured (dedicated server + client, autopilot boosting every other 6 s, 60 s): 14 movement
      corrections, against 11-12 before the meter - about one small correction each time the meter runs dry,
      because client and server cut their moves at different instants.
    - Braking drag 1200 -> 1900 uu/s2.
    - Spawn shield made visible (white shimmer of the hull lights, HUD line) through one replicated flag; shield
      regeneration (already 4 s / 15 per second) moved into a tested rule; spawn points are now scored (enemy
      distance, turret range, gravity, company) and the best of 12 is taken.
    - Problem: with the first scoring, which strongly preferred sectors with enemies, a bot match ran at 17 kills
      per minute on easy and 27 on hard (was 13): everybody was dropped into the same few fights. The preference
      was cut to a third. Easy bots after that: 30 kills in 121 s (14.9 per minute), 514 hits of 1080 shots, 2 of 32 deaths by turrets.
    - Bot difficulty: three rows of numbers (aim error, reaction delay, turn rate, bursts, opening range, strafe,
      retreat threshold). On every level a bot low on hull now stops firing and boosts for a repair pad in its
      sector. In a 45 s listen-server run the capture autopilot (flies circles, fires, never dodges) was not shot
      down once by easy bots; in the builds before it died about every 15 s. That is the only evidence that a
      newcomer can beat the default level - nobody has played against them.
    - Looks: hull banks into turns (first attempt banked the wrong way and far too much - a quaternion about +X
      rolls left in this engine; fixed after looking at the pictures), camera lag (first attempt pushed the ship
      170 px off centre at cruise speed; position lag cut to 45 uu), wider view under boost, camera shake on hits,
      hull lights follow the throttle, three debris bursts and a larger blast on death.
    - The homing missile was looked at in an enlarged crop of a capture: at its size (2.2 m) it is a grey dot with
      a trail; its two ends cannot be told apart in the game, and the mesh has the same radius at both ends.

18. New video from the packaged build (frame dump at a fixed 30 fps step with the sound rendered offline, as in
    item 11): title menu, PLAY, controls overlay, target lock and lead circle on a parked enemy, homing missiles,
    wormhole, quad damage, a station with turrets, the way-to-the-enemy chevron, scoreboard. 1500 frames, 50 s.
    The local ship is flown by the capture autopilot and the scenes are set up with development console commands
    (`-HCJDemoCmds`); it is not footage of a person playing. `Media/HypeCubeJam_gameplay.avi`: 1280x720, 30 fps,
    48 kHz stereo, 155 MB. `Media/HypeCubeJam_gameplay_small.avi`: 544x306, 15 fps, 12 kHz mono, JPEG quality 35, 9.7 MB.
    Recorded mix: peak 0 dBFS, RMS -17 dBFS, no clipped samples. Still not listened to.
    - Problem: in the first dump the renderer's red line "Video memory has been exhausted" was in about 150 frames
      (it also showed in the second of two instances sharing the 2 GB card). `r.DemotedLocalMemoryWarning=0` in
      `DefaultEngine.ini` removes the line from the screen; the capture log still reports the amount over budget.

## 14 Nov 2026 (fourth session) - hints and interface
19. In-game hints and a tidier HUD. Still C++ only: canvas HUD, one Slate menu widget.
    - Hints: one panel at the bottom centre, keys drawn as key caps, a thin line for the time left. Which hint is
      up is decided by a plain class (`FHCJHintDirector`, `Public/UI/HCJHints.h`), one per process, clocked by game
      time: first four onboarding prompts in order (move, fire, boost, target keys), each until the player does it
      or 12-14 s pass, an action already done is skipped; then contextual hints by priority, each at most 1-3
      times per session with a cooldown (gravity well, low hull, new item - once per item type, station turrets,
      boost empty, wormhole, portal, pad, empty sector, the goal). Gravity well and low hull interrupt anything; a
      new item replaces a lesser contextual hint. Local only, nothing replicated.
    - On a low hull a green ring / edge chevron with the distance leads to the nearest repair pad of the sector.
      After a death the respawn screen names who or what did it (from the kill feed) and gives one tip per cause.
    - H switches hints off / on (also "In-game hints" on the settings page; stored in GameUserSettings.ini). With
      hints off the old automatic showing of the controls page on the first ship is back.
    - HUD layout: the radar moved from the bottom centre to the bottom left, on top of one block of labelled
      SHIELD / HULL / BOOST bars with numbers (boost was a thin unlabelled bar under the speed); bottom right a
      WEAPON panel with ammunition number and bar, the drop key and power-up timers with draining bars; top left
      `KILLS n / limit` and a line saying who leads and by how much; a banner with the sector id and its contents
      for 4.5 s after entering a sector and a short flash in the sector colour after a jump; a larger red hit
      marker for a kill.
    - Problems found in the pictures and fixed: the scoreboard was drawn through the centre message (the big
      lines, the banner and the hints now give way to the scoreboard and the F1 page); off-screen enemy arrows lay
      on the target panel; a guide label at the screen edge was cut in half; the wormhole had two labels on top of
      each other when it was the way to the enemies; dark player colours (blue, purple) were unreadable on the
      scoreboard; "SECTOR ----" while waiting for a respawn; the first jump flash was far too strong (alpha 0.4
      for 0.45 s, now 0.22 for 0.35 s); pressing H in the first seconds brought up the controls page.
    - Menu: a CONTROLS page (two columns of key caps, what the HUD marks mean) from the title and the in-match
      menu, a line of key caps on every page, Enter = PLAY / RESUME, F1 = controls.
    - F1 overlay redrawn as a centred two-column page with the same content.
    - Missile mesh settled: a headless script listed the vertex rings of `SM_hovertank_missile_one` along its long
      axis. The +X end is a dome (radius 5.6 -> 5.3 -> 4.4 -> 3.1 -> 1.6 over the last 4 units), the -X end is flat
      with a narrower collar behind it (radius 4.2): +X is the nose, which is the way the game flies it. Nothing
      changed. (The earlier measurement compared only the largest radius of each end, which is equal.) The turret
      gun was measured again with the same result as before (thin end at +X).
    - Development commands: `HCJMenuCmd controls | hints | skiptutorial | resethints | die`.
    - 31 automation tests, all passing (new: hint onboarding, hint rules, hint texts).
    - Verified from pictures and logs of the packaged build only (1280x720 and 1920x1080, scenes set up with
      development console commands, ship idle or flown by the capture autopilot): every one of the 14
      hint types appeared in a log line `Hint: <name>` in at least one run (portal and fire only once each); pictures were looked at for the onboarding prompt, new item,
      gravity well, low hull with the repair marker, scoreboard, respawn tip, F1 page, pause / controls /
      settings / title pages. Nobody has pressed H, F1 or the menu buttons by hand.
    - Frame rate, packaged, 1280x720, listen server with 7 bots, seed 1234, capture autopilot, 8 s averages, one
      70 s run each of four builds of this session: 89-112, 110-134, 116-128 and (final build) 85-117 fps; the build
      before this session, same command line, same day: 93-110 fps. The spread between runs is larger than any
      difference between the builds. 1920x1080 (4-5 s averages): 62-78, 74-92, 68-88, 71-84 fps.
    - Problem: the low-hull hint named a repair pad "in this sector" although the ship had been moved to another
      sector in the meantime (its text was composed once). That one text now follows the pad.
    - Problem: an onboarding prompt that was interrupted (death, an urgent hint) came back every time; in one
      autopilot run "target keys" was shown three times. An onboarding step is now shown at most twice.
    - Pictures: `Media/16..23_*.png`. 16, 17, 20, 22, 23 are from the final build; 18, 19 and 21 from the two
      builds before it (the last changes were the two hint rules above, not drawing code). 19 and 21 were taken
      in the frame of a development teleport, so the ship sits in an odd place and the jump flash tints 19.
    - Zip `Packaged/mariuz_HypeCubeJam.zip`: 546.1 MB (832.3 MB unpacked, 46 files).

## 14 Nov 2026 (fifth session) - Epic Online Services: lobby browser, stats, leaderboards
20. "Use epic network services for lobby and leader boards". Done in the Developer Portal by the human beforehand:
    product, Live sandbox and deployment, a client on a Peer2Peer-type policy, five SUM stats (`HCJ_KILLS`,
    `HCJ_DEATHS`, `HCJ_WINS`, `HCJ_TURRETS`, `HCJ_MATCHES`), two leaderboards (`HCJ_LB_KILLS`, `HCJ_LB_WINS`). No
    identity provider and no Epic Account Services, so the only possible sign-in is EOS Connect with a device id.
    - What the engine ships (5.9, installed build): EOS SDK 1.20.0.3 headers / lib under
      `Engine/Source/ThirdParty/EOSSDK`, `EOSSDK-Win64-Shipping.dll` in `Engine/Binaries/Win64`, and the `EOSShared`
      plugin, whose `IEOSSDKManager` loads the library, calls `EOS_Initialize`, creates platforms from a config
      struct and ticks them. Decision: enable `EOSShared` only and call the C SDK from one game-instance subsystem
      (`UHCJEOSSubsystem`); `OnlineSubsystemEOS` is not used. API names and struct versions were taken from the
      headers (`*_API_LATEST` macros), not from memory.
    - Transport decision: **plain IP, not EOS P2P.** The engine's `SocketSubsystemEOS` / `NetDriverEOS` get their
      local user from an `ISocketSubsystemEOSUtils` object that `OnlineSubsystemEOS` supplies; using it with a raw
      SDK login means writing that glue, and it could not have been verified here anyway (two instances on one PC
      are the same EOS user, see below). So the lobby is an advertisement: its attributes carry the host's LAN
      addresses, an optional public address and the port, and JOIN travels there by the existing "join by address"
      path. Limit: over the internet the host needs UDP 7777 forwarded and must give its public address
      (`PublicAddress=` in `[HypeCubeJam.EOS]` or `-HCJPublicAddress=`); the game does not discover it. On one
      network or one PC it needs nothing. Nobody but the host is a member of the lobby.
    - Sign-in: `EOS_Connect_CreateDeviceId` (success or "duplicate" both mean: go on), `EOS_Connect_Login` with
      `EOS_ECT_DEVICEID_ACCESS_TOKEN` and the pilot name, `EOS_Connect_CreateUser` on `EOS_InvalidUser`, a new login
      when the service announces that the token expires or when the pilot is renamed; retries after 20 s, 60 s,
      then every 2 minutes. A device id belongs to the Windows user: the SDK has no supported way to give a second
      instance on the same PC another identity, so two local instances are one EOS user (documented in the README).
    - Lobby: created by a listen host once it is past the title menu, updated when players / bots / map change (at
      most every 2 s), destroyed on return to the title menu, on leaving the world and at exit (the subsystem pumps
      the platform for up to 2 s while shutting down). Matches of another network version are listed, not joinable.
      Everything read from somebody else's lobby is validated and cleaned before it reaches the screen or a travel
      URL (a test feeds it a host name / address with URL options in it).
    - Stats: each client reports its own score line (`FHCJStatTracker`): at the end of a match kills, deaths,
      turrets, one match and a win for the winner; on leaving / quitting mid-match kills, deaths and turrets. A
      host that went back to the title menu gets no match and no win for a match that ends behind the menu. A
      failed ingest is kept for the next one. Bots report nothing (they have no client).
    - Menu: FIND GAMES and LEADERBOARDS on the title page with a status line under them, LEADERBOARDS also in
      the Esc menu, one line on the end-of-match board saying whether the stats were sent. Without the service
      both pages say "Online services unavailable" and why.
    - Fails soft, checked: with the client secret empty the packaged game logs one line (`EOS: Online services
      unavailable: not configured (ClientSecret is empty in [HypeCubeJam.EOS])`), the menu shows the same, and the
      log has the same single error line as the build before (the CelestialVault plugin message). The secret is
      never written to a log, the screen or a document; a test checks that the "what is missing" text never
      contains a value.
    - Problem: in a test run the second instance timed out connecting. Cause: the host had been started with
      `-AutoHost "?Seed=1234"`; the loose `?Seed` argument replaced the start URL and the process was not
      listening at all. With the documented `"/Engine/Maps/Entry?listen?Seed=1234"` the join worked.
    - Problem: the main page must not be rebuilt when the sign-in finishes (somebody may be typing a name), so its
      status line is a live text; the two online pages are rebuilt when the subsystem's revision changes. A
      rebuild destroys the button that had the keyboard focus; the menu now takes the focus back so that Esc keeps
      working.
    - `-HCJEosDemo` (development builds): made-up lobby and leaderboard rows without contacting any service, the
      first row advertising this machine; the log says what a host would advertise and what a client would send.
    - **Verified against the live service: nothing.** The client secret was not in `DefaultEngine.ini` at any
      point of this session, so no sign-in, lobby, ingest or leaderboard query has ever reached Epic. What was
      verified offline, packaged build: (1) no secret -> unavailable state, game as before; (2) with
      `-HCJEosDemo`, host + second instance: the host logged `would advertise 'User' 1/16 players, 7 bots, 4D - 16
      sectors, seed 1234, port 7777, 3 LAN address(es)`, then `2/16 ... 6 bots` when the second instance joined and
      `1/16` again when it left; the second instance joined through JOIN of the first row (`joining 127.0.0.1:7777`,
      layout checksum 3976D716 on both) and on leaving logged `would send stats: kills +0, deaths +1`; (3) editor
      binary, `-HCJEosDemo`: `would send stats: ... matches +1` at the end of a match and `would remove the lobby`
      at quit. The SDK calls themselves (argument structs, callbacks, result handling) compile against the 1.20
      headers and have never run.
    - **Later the same day, with the client secret in `DefaultEngine.ini`: verified against the live service.**
      Repackaged (no code change was needed; 36 tests passing), packaged build, everything on one PC, menu actions
      by `HCJMenuCmd` console commands, evidence is the log:
      - Sign-in: `EOS: device id created for this Windows user` (first run only), `EOS: signed in as 'User',
        product user 0002...09e0`, 1.3 s after start (13 s on the very first run, most of it the SDK's first
        config download).
      - Hosting: `EOS: lobby <id> created`, `EOS: lobby advertised: 'User' 1/16 players, 7 bots, 4D - 16 sectors,
        port 7777, 3 LAN address(es), public address not set`; `lobby updated: ... 2/16 players, 6 bots` when the
        second instance joined and `1/16 ... 7 bots` when it left; `EOS: lobby <id> removed (EOS_Success)` at quit.
      - Browser: a second instance in its title menu logged `EOS: lobby search: EOS_Success, 1 listed, 0 skipped`
        (twice), then `joinlobby 0`: `Menu: joining the game of 'User' found through Epic Online Services`,
        `Menu: joining 127.0.0.1:7777`, `Welcomed by server`, layout checksum 3976D716 on both. The host's own
        search logged `0 listed` (its own lobby is left out, as intended).
      - Both instances are the same product user (same id in both logs). That did not get in the way: a search
        does return a lobby owned by the searching user in another process. Search and join between two different
        EOS users (two PCs / two Windows users) was not tested.
      - Stats: `EOS: sending stats: kills +0, deaths +5, wins +0, turrets +0, matches +1` -> `EOS: stats accepted`
        at the end of a match on the host and on the joined client, and again at quit (kills / deaths only).
      - Leaderboards: `EOS: leaderboard HCJ_LB_KILLS: 0 record(s)` while only deaths and matches had been sent;
        after an ingest with `kills +1` the query 30 s later gave `HCJ_LB_KILLS: 1 record(s), 1 shown, local player
        ranked`; after a won match (`?Bots=1`, `wins +1`) `HCJ_LB_WINS: 1 record(s), 1 shown, local player ranked`
        30 s later. So the delay was under 30 s here. The live boards now hold these test scores of user 'User'
        (1 kill made with development commands, 1 win of an empty match).
      - A host process that is killed instead of quitting leaves its lobby behind for a while; the SDK drops it
        from search results itself (`Lobby search result ... does not have a lobby owner; it will not be returned`).
      - No EOS result code other than `EOS_Success` appeared in any run; the client secret is in none of the logs
        (checked by a string search without printing it). Zip rebuilt: 554.3 MB (851.6 MB unpacked, 47 files).
      - Not verified live: the menu pages by eye or by mouse (only the log), a pilot rename, the sign-in renewal
        after about an hour, retries after a failure, more than one record on a board.
    - 36 automation tests, all passing (new, `HypeCubeJam.Online.*`: config validation and the unavailable states,
      lobby attribute packing / parsing, join address, stat tracker, leaderboard page).
    - Frame rate, packaged, 1280x720, listen server with 7 bots, seed 1234, capture autopilot, 8 s averages over
      70 s: 87-99 fps; same build with `-HCJNoEOS`: 91-98 fps; an earlier build of this session: 83-100 fps. The
      build before this session, as noted under 19: 85-117 fps.
    - Packaged: 1022 packages cooked; `Engine/Binaries/Win64/EOSSDK-Win64-Shipping.dll` (19.9 MB) is staged by
      the build without further rules. Zip `Packaged/mariuz_HypeCubeJam.zip`: 554.3 MB (851.6 MB unpacked, 47 files).
    - Pictures: `Media/24_title_menu_find_games_leaderboards.png`, `25_find_games_online_unavailable.png` (the real
      state of this build), `26_find_games_demo_rows.png` and `27_leaderboards_demo_rows.png` (made-up rows of
      `-HCJEosDemo`, for the layout only).

## 14 Nov 2026 (sixth session) - shield facings
21. "Add ship shield like in star trek". Until now the shield was one number in front of the hull (50 points, back
    at 15 per second after 4 s without a hit) and a bar on the HUD.
    - Rules: a ship has four shield facings - fore, aft, port, starboard. The server takes the facing from where a
      hit landed, relative to the ship's centre and in the ship's own frame (the impact point of the point-damage
      event; quarters of the horizontal circle, the vertical part ignored, straight above / below counts as fore).
      The hit drains that facing; what it cannot take, and every hit on a facing that is down, goes to the hull.
      Damage without a direction (leaving the sector, development commands) is shared by all facings. Each facing
      has its own 4 s clock and regenerates on its own; a shield cell fills the weakest facings first, each up to
      twice its capacity. Station turrets keep one shield all around (they have none configured). All of it is
      plain functions in `Ship/HCJShieldFacings.h` with tests; `UHCJHealthComponent` holds the values.
    - On the wire: the replicated float became one 32-bit word, a byte per facing (0 = exactly down, 255 = 2.5
      times the capacity; step 0.25 points at the final capacity), so the cost per update is what it was. The
      flare is one unreliable multicast per hit that a facing absorbed: 16 bits of direction, 8 bits of strength, at
      most one per ship every 0.08 s (a scatter blast is one flare). A facing going down is not sent at all: every
      machine sees it in the replicated strengths.
    - Bubble: one basic sphere per ship, stretched to an ellipsoid (200 x 165 x 100 uu half axes), with an additive
      unlit two-sided material built by `docs/tools/make_shield_material.py` (one Custom node in the sphere's own
      space: hot spot, two ripple rings, a rim-lit wash with a cell pattern, tint by the facing's strength; a block
      flicker over the half of a facing that fails). It is hidden and not ticked unless a flare (0.4 s) or a
      collapse (0.55 s) runs; a cloaked ship therefore shows it only while it is being hit. The collapse sound is
      Lyra's `SFX_Chime_03A`, already in `/Game/Audio` and used by nothing else. No asset was imported.
    - Problem: the first flare was a solid blob that hid the ship (brightness 6 on an additive two-sided sphere,
      and a wash term that lit half the bubble). Three rounds of looking at pictures: brightness 6 -> 2 -> 0.8,
      the wash narrowed and tied to the rim, the collapse blocks made smaller and dimmer.
    - HUD: the SHIELD bar became the own ship (drawn in the colour of its hull state) inside four arcs, fore at the
      top: filled from the middle by strength, blue-white to orange, white for a third of a second after a hit,
      red and blinking when down, a thin outer arc for an overcharge; next to it "SHIELDS 104" or "AFT SHIELD DOWN"
      and the four numbers. The target panel got the same four arcs for the target (its nose up), a wedge showing
      where the player is around it, and "FACING YOU: STARBOARD 17 %" / "DOWN: hull open". One new hint, once per
      session, the first time a facing of the own ship goes down; a paragraph on the F1 page and two lines on the
      CONTROLS page; two new death tips.
    - Bots: a bot that is breaking off (running for a repair pad, or travelling to another sector) and was hit in
      the last 3 s turns a facing that still holds towards its attacker when one of its own is weak (35 %) or down:
      a neighbouring facing (a quarter turn) rather than the opposite one unless that is clearly stronger. Thrust
      is world space, so the flight path does not change. A bot that is shooting keeps its guns on the target, and
      bots do not look at the facings of their target at all. Logged in a 150 s match: 7 such turns
      (`... turns its AFT facing (100 %) to the attacker: FORE is at 0 %`); 13 and 18 in two 300 s matches.
    - Balance, measured: headless dedicated server, 8 easy bots, seed 1234, no frag or time limit, 300 s per
      match, a new log line `Balance t=`: time to kill (first hit of the last engagement to death, credited kills
      only), kills per minute, hit rate, damage per kill. Runs differ a lot (kills per minute 11.5 to 16.6 with
      identical settings), so the two rule sets were run side by side: `-HCJSingleShield` gives ships the old
      single shield in the same binary.

      | | matches | time to kill | kills / min | hit rate | deaths by turrets |
      |---|---|---|---|---|---|
      | before, the build before this session + the log line | 2 | 4.24 / 4.57 s | 11.5 / 13.1 | 49 / 48 % | 8 of 132 (6 %) |
      | before, old rules in the final binary (`-HCJSingleShield`) | 9 | 4.97 s (4.11-5.61) | 14.9 (13.9-15.8) | 48 % | 35 of 713 (4.9 %) |
      | 4 x 20, 6 per second (the first try: same total as before) | 2 | 4.15 / 5.09 s | 12.7 / 16.2 | 52 / 45 % | 6 of 152 (3.9 %) |
      | 4 x 30, 8 per second | 3 | 4.29 s | 13.1 (12.9-13.5) | 52 % | 11 of 210 (5.2 %) |
      | **4 x 26, 8 per second (final)** | 6 | 4.19 s (3.60-4.96) | 14.7 (11.5-16.6) | 47 % | 19 of 465 (4.1 %) |

      Read: with four facings of 20 a ship died sooner (damage per kill 186 against 226: a fight comes mostly from
      one side, so only one facing counts); with 30 the same three-at-a-time runs gave 13.1 kills per minute
      against 15.3 for the old rules (-14 %); 26 gives 14.7 against 14.9 (-1 %) over 6 and 9 matches (6 + 6 of them
      run side by side, two and two), hit rate unchanged, share of turret deaths unchanged. Time to kill came out 0.8 s shorter than
      with the old rules in the same runs while the kill rate did not move; no explanation was looked for.
      The first row and the second differ (12.3 against 14.9 kills per minute for the same rules): the first two
      matches ran two at a time on the earlier binary, the others three or four at a time; which of the two made
      the difference was not established. Hits in the final matches: about 37 % stayed on a shield, 63 % reached
      the hull; about 35 facings went down per minute.
    - Netcode, editor binary, listen host + one client on the same PC, both flown by the capture autopilot,
      `-LogCmds="LogHCJ Verbose"`, evidence is the client's log: in 95 s it logged 16 strength changes of its own
      facings and 28 of other ships', 15 flares on its own bubble and 16 on others', 7 collapses of its own facings
      and 9 of others' (`Shield: PORT facing of HCJShipCharacter_0 (own ship) 26.0 -> 13.0 ...`, `Shield: bubble
      flare on ...`).
      Movement corrections: with the autopilot not boosting, 3 in about 60 s (154, 10 and 50 uu) while the
      client's own ship took 16 facing changes, 10 flares and 6 collapses and was destroyed and respawned at least
      once - the shield code moves nothing. The packaged build, same test, 80 s: layout checksum 3976D716 on both,
      13 facing changes, 13 flares and 6 collapses on the client's own ship, 0 corrections (no other ship was hit
      in the client's view in that run). With the autopilot holding boost for 15 s at a time the same test gave 53 and 36 corrections; a new
      verbose line per correction shows them in groups at the moment the boost meter unlocks at 25 % (5 to 8, mostly of
      20-60 uu, within half a second, every 2.7 s). That is the boost meter of item 17, not the shields, but it is
      many more than the "one per emptied meter" measured there against a dedicated server; here the server was a
      listen host sharing the GPU with the client at 50-60 fps. Not followed up.
    - Frame rate, packaged, 1280x720, listen server with 7 bots, seed 1234, capture autopilot, `-HCJNoEOS`, 8 s
      averages over 70 s, alone on the GPU, the build before this session
      (unpacked from the previous zip) and the new one, started alternately: before 134-150, 113-142, 117-140 fps;
      after 114-145, 122-136, 116-135 fps (means of the two alternated pairs: 129 and 127 before, 128 and 127
      after). No difference beyond the spread between runs. The 87-99 fps noted under item 20 were not reproduced
      today by either build (both are faster); why is not known.
    - 43 automation tests, all passing (new, `HypeCubeJam.Shield.*`: facing selection, absorb and overflow,
      per-facing regeneration, overcharge spread, replication round trip, facing to present, the hint).
    - Development aids: `HCJMenuCmd shieldhit <facing>,<damage>[,<degrees>]` and `enemyhit ...` (listen host),
      `-HCJSingleShield`, verbose `Shield:` and `Correction` log lines, the `Balance t=` status line.
    - Pictures (packaged build, 1280x720): `Media/28_shield_flare_on_hit.png` (a hit on the port
      facing, the flare at its real speed, a fraction of a second old), `29_shield_facing_down_hud_hint.png` (aft facing down:
      red arc, "AFT SHIELD DOWN", the hint), `30_target_shield_facings_readout.png` (target panel: starboard facing
      of the target down, "DOWN: hull open"; the collapse flicker on the target at 9 m),
      `31_shield_collapse_flicker_and_flare.png` (own ship: port facing collapsing, a hit on the starboard facing
      with its ripple). For 30 and 31 the flare and the collapse were slowed from 0.4 / 0.55 s to 3 s with an
      `-ini:Game:` override, because the frame after a screenshot request is often 0.4-0.5 s long and the effect was
      over before the picture (two of three pictures taken at real speed showed no bubble at all). The hits were made
      with the development command; the ship was idle.
    - Packaged: 1023 packages cooked. Zip `Packaged/mariuz_HypeCubeJam.zip`: 554.4 MB (851.7 MB unpacked, 47 files).
    - Judged from logs and pictures only. Nobody has flown with the facings: whether turning a fresh side to an
      enemy is practical with these controls, whether the arcs are read in a fight, whether the flare and the
      collapse flicker are too much or too little in motion, and what the collapse chime sounds like are untested.

## 14 Nov 2026 (seventh session) - photon torpedoes
22. "Add photon torpedoes like in star trek". A secondary weapon on every ship, built around the shield facings of
    item 21.
    - Rules: every ship carries torpedoes next to its weapon - 4 on a fresh ship, at most 6, a TORPEDOES pad gives 3
      (a tenth pad item; the pad positions and their checksum are unchanged, only what can lie on a pad). Right
      mouse button or X fires one (both were free; the project binds no gamepad), 2 s between shots. The server
      checks stock and reload; the count and the time the next one is ready are replicated to the owner only.
      The torpedo is the ordinary projectile actor with a row of its own in the weapon table: 9000 uu/s (bolts
      20 000, homing missiles 10 000), 2 s of life, turning 40 degrees per second (missiles: 140) towards the
      ship its firer had locked when it was launched. Without a lock it flies straight; once the target is more
      than 60 degrees off its nose, dead or cloaked, it loses it for good. It arms after 12 m: a hit before that
      is a dud (15 % of the damage, no blast). It goes off on contact or when its time is up, with a blast of
      4.5 m around the hit (full damage within 1.2 m of a hull, 25 % at the edge, nothing behind a rock); ships
      and station turrets in the blast are hit, the firer's own ship included.
    - Damage: one torpedo can strip 30 points from the facing it lands on (a facing has 26); the share of the
      torpedo the facing could not take is the share of 70 that goes on to the hull (100). So: on a full facing
      the facing is gone and the hull loses 9; through a facing that is down the hull loses 70; two on the same
      facing 79. The facing is found as for bolts (where the hit landed in the ship's own frame); for the blast
      the "impact point" of the damage event is the blast itself, so the existing rule picks the facing towards
      the blast. Quad damage does not apply to torpedoes. All of it is plain functions in `Ship/HCJTorpedo.h` with tests.
    - Problem (found by a test written before any match was run): at the first numbers (7500 uu/s, armed after
      9 m) a ship that follows its own torpedo under boost (6000 uu/s) is 2.4 m from it when it arms and would
      take 73 % of its own blast. A ship under boost is nearly as fast as that torpedo. Now 9000 uu/s and 12 m:
      the same ship is 4.6 m away, just outside the blast. A ship with overdrive and boost together (9000 uu/s)
      still keeps up with its own torpedo; left as it is.
    - Not done: a torpedo cannot be shot down. Projectiles ignore each other; a torpedo that bolts can hit would
      also be hit by the bolts of the ship that fired it (they are twice as fast and fly the same line), and
      keeping those apart needs ignore lists on both sides on every machine. Not cheap, so skipped.
    - Look and sound, from what is in the project: two additive unlit materials built by
      `docs/tools/make_torpedo_materials.py` (one Custom node each on the texture coordinates of a quad, no
      texture): the torpedo - a white-hot core in a red-orange ball, four long and four short spikes that turn and
      flicker - and the detonation - a flash, a shockwave ring running outwards, a star-burst in the first
      moments. Both on the engine's basic plane, turned to the local view every frame. Trail: the ribbon trail of
      the Niagara Examples pack; particles of the detonation: its small explosion (already used for ships). Launch:
      Lyra's jump-pad whoosh at 0.6 of its pitch (the missiles use the same wave at full pitch); detonation: the two
      explosion waves of the Hover Vehicles example content; warning cue: the tick of the low-hull warning. The
      victim's shield bubble flares through the existing path. Nothing was imported.
    - Problems found in the pictures and fixed: a detonation on the own ship turned the whole screen white (the
      quad was 18 m across with brightness 5, and the camera sits 7 m behind the ship): now 14 m, brightness 2, a
      weaker flash term, and dimmer the nearer the view is (a quarter at point-blank range). The warning line
      was a dull brown at the low point of its pulse: brighter colour, shallower pulse. The colour parameter of
      the ribbon trail could not be read by script (`exposed_parameters` is not scriptable), so the trail keeps
      the colour the pack gave it.
    - HUD: in the weapon panel a TORPEDOES block - a pip for each of the 6 places (filled = on board; the one
      being loaded fills from the bottom), READY / LOADING / EMPTY, a thin reload bar. "INCOMING TORPEDO - BREAK
      AWAY" with the distance, a diamond closing in on the torpedo when it is in view and a triple chevron at the
      edge of the view towards it when it is not; it is derived on each machine from the replicated torpedoes
      it sees (their target is already replicated for the homing missiles), so no new message. Kill feed
      `[TORPEDO]`. One hint, once per session: the first time the locked target shows a facing that is down while
      a torpedo is loaded. A row on the F1 page and the CONTROLS page, two lines of explanation on F1, two new
      death tips, a text for the new pad.
    - HUD defect fixed (seen in `Media/29_shield_facing_down_hud_hint.png`): the bracket and label of an enemy
      were drawn on top of the bottom-left shield / hull panel. Every panel now notes its screen rectangle
      (vitals, radar with the lines above it, weapon panel, target panel, map with legend, the block of lines at
      the top left, hint panel); an enemy whose whole marker (bracket or diamond, label, bars) would touch one of
      them gets an arrow on the ring of the off-screen arrows instead, and no lead circle. Pad markers that
      would lie on a panel are not drawn. Station, wormhole and portal labels are NOT checked.
    - Bots: a bot keeps its target locked (as a player's HUD does) and decides four times a second: with a
      torpedo loaded, the target in sight within 10 degrees of its nose and 20.5 to 90 m away, it fires when the
      facing the target turns towards it is down (twice the chance) or weak (below 35 %) - that is what the
      target panel shows a player, and all it looks at - or, in the first 4 s on a target beyond 45 m, now and
      then as an opener; never at a target with less than 15 % hull. Chance per decision / opener / pause after
      a torpedo: easy 0.04 / 0.012 / 10 s, normal 0.12 / 0.03 / 5 s, hard 0.3 / 0.05 / 3 s. A bot that a
      torpedo is homing on (what the warning tells a player) breaks away once it is within 70 m - 1.4 s of
      boost straight away from the torpedo's line of flight - with a chance of 0.3 / 0.6 / 0.9. Bots take a
      TORPEDOES pad when they are two or more short.
    - Balance, measured as in item 21: headless dedicated server, 8 easy bots, seed 1234, no frag or time limit,
      300 s per match, four matches at a time. "Before" is the same binary with `-HCJNoTorpedoes` (no stock, no
      pads, bots do not lock), run side by side with the others. A new log line `Torpedoes t=`.

      | | matches | time to kill | kills / min | hit rate (bolts) | kills by torpedo | deaths by turrets |
      |---|---|---|---|---|---|---|
      | before (`-HCJNoTorpedoes`) | 5 | 4.66 s (3.80-6.01) | 14.2 (12.3-15.9) | 50 % | - | 12 of 369 (3.3 %) |
      | first try: 85 to the hull, 1.8 s reload, easy bots 0.10 per decision, 3.6 s pause | 2 | 2.97 s | 17.4 / 17.4 | 49 % | 77 of 176 (44 %) | 5 of 181 (2.8 %) |
      | 70 to the hull, 2 s reload, easy bots 0.05, 8 s pause | 5 | 3.15 s (2.66-3.59) | 14.6 (13.1-15.3) | 49 % | 88 of 368 (24 %) | 21 of 389 (5.4 %) |
      | **final: the same, easy bots 0.04, 10 s pause** | 4 | 3.47 s (2.82-3.85) | 15.4 (14.9-16.3) | 42 % | 60 of 310 (19 %) | 10 of 320 (3.1 %) |

      Kills by weapon, before: blaster 46 %, scatter 12 %, rail 10 %, missiles 25 %, pulse 7 %. Final: blaster 39 %,
      scatter 4 %, rail 7 %, missiles 22 %, pulse 8 %, torpedo 19 %. In the four final matches 231 torpedoes were
      fired (about 7 per ship in 5 minutes), 93 lost their target (40 %), 138 damaged a ship (60 %), 124 of those
      hits landed on a facing that was already down, bots broke away from one 70 times.
      Read: with the first numbers the torpedo was the main weapon (44 % of kills, 23 % more kills per minute).
      With the final ones the kill rate is 8 % above "before" (15.4 against 14.2, inside the spread between
      matches: 12.3 to 15.9 before) and a fifth of the kills are torpedoes. Time to kill is a second shorter:
      a torpedo ends a fight that the guns had brought to an open facing. The hit rate of the bolts in the four
      final matches was 42 % against 49-50 % in all the others; these four ran while a person was playing another
      copy of the game on the same PC, and the bots also evade now - which of the two it is was not established.
      Only the easy level was measured; normal and hard bots fire torpedoes three and seven times as readily.
    - Netcode, editor binary, listen host + one client on the same PC (port 7790), both flown by the capture
      autopilot without boost, the client firing a torpedo whenever one is loaded (`-HCJAutoTorpedo`),
      `-LogCmds="LogHCJ Verbose"`, evidence is the client's log, 100 s: layout checksum 7C0CB60C on both; the
      client asked the server 25 times and saw 23 torpedoes of its own appear (the two others were refused by
      the server: the request had been sent in the last moment of a reload); 29 changes of its own stock
      (`own stock 4 -> 3, next one ready in 1.4 s`, back to 4 with every new ship); 3 torpedoes of other ships
      appeared in its view, 23 detonations were shown, 3 times `Torpedo warning: ... is homing on the own ship`.
      Movement corrections of the client: 4 in that run and 2 in the run before it; 2 in the same test with
      `-HCJNoTorpedoes` on both sides (item 21 measured 3 in 60 s). A torpedo moves no ship.
      - Problem: the autopilot's client asked 39 times for 14 torpedoes in the first run - it asked again every
        frame until the new stock had come back. The owning client now waits half a second after a request.
    - Frame rate, packaged, 1280x720, listen server with 7 bots, seed 1234, capture autopilot (it fires no
      torpedoes; the bots do), `-HCJNoEOS`, 8 s averages over 72 s, the build of item 21 (a copy of its packaged
      folder) and the new one started alternately, no other game process running at that time: before 115-128
      and 102-127 fps (means 120 and 120), after 106-130 and 112-121 fps (means 119 and 116). The same range.
    - 49 automation tests, all passing (new, `HypeCubeJam.Torpedo.*`: stock and reload, damage against a facing
      up / down / overcharged and the design targets, arming distance and blast falloff, homing turn limit and
      keeping the lock, the facing a blast lands on and the bots' rule, the hint / pad item / markers clear of
      panels). Two existing tests were changed for the new enum values (the pad item that is not a weapon, ten
      item texts).
    - Development aids: `-HCJNoTorpedoes`, `-HCJAutoTorpedo`, `HCJMenuCmd torpedo | enemytorpedo <uu> |
      torpedostock <n> | slomo <factor>`, `HCJGive 9`, verbose `Torpedo:` log lines, the `Torpedoes t=` line.
    - `docs/tools/make_zip.ps1` ended with exit code 1 although the zip was fine: robocopy reports "files were
      copied" as 1, the script only checked for 8 and above and left the 1 standing as its own exit code. It
      now clears it and ends with `exit 0` (checked: a run after packaging returned 0).
    - Pictures (packaged build, 1280x720, scenes set up with development commands, the ship idle):
      `Media/32_photon_torpedo_in_flight.png` (own torpedo on its way to the locked target, pips with one loading),
      `33_torpedo_detonation_on_shield.png` (a bot's torpedo going off on the fore facing of the own ship: flash,
      ring, the facing's arc red, "FORE SHIELD DOWN"), `34_torpedo_pips_incoming_warning.png` (INCOMING TORPEDO
      with the marker on it, an own torpedo just fired, TORPEDOES block), `35_enemy_marker_off_panel_torpedo_hint.png`
      (an enemy that would be drawn over the radar / shield block is an arrow instead; the torpedo hint, caught
      while fading). ALL FOUR WERE TAKEN WITH THE GAME SLOWED DOWN (`HCJMenuCmd slomo 0.08`, 0.05 for the last):
      a torpedo flight is a third of a second at these distances and the frame after a screenshot request is
      longer than that. Effects were not lengthened; what is seen is one moment of the real effect. The trail
      is hardly visible in them, and the shield bubble flare is not told apart from the detonation in 33.
    - Packaged: 1025 packages cooked. Zip `Packaged/mariuz_HypeCubeJam.zip`: 554.4 MB (851.8 MB unpacked, 47 files).
    - Judged from logs and pictures only. Nobody has fired a torpedo by hand, dodged one, or heard the launch,
      the detonation and the warning cue. Bots mostly meet head-on and at 35 m, so their matches say little
      about how a player uses four torpedoes, or about how hard one is to dodge with a mouse.

## 14 Nov 2026 (eighth session) - phasers, destroyable asteroids, shockwaves and flares, Lyria music
23. "Add phasers like in star trek, make the asteroids destroyable, add more physics effects and more flares on
    explosions" and "add a few music tracks generated with lyria". Four things in one session, in that order of priority.
    - **Phasers.** A beam weapon, the weapon every ship now starts with (`EHCJWeapon::Phaser`, appended to the enum so
      that the rows of the weapon table keep their numbers). Held trigger; the server traces a line from the muzzle
      along the aim every tick (the trace channel of a projectile, so it hits what bolts hit: rocks, stations, turrets,
      the 100 uu hit sphere of ships), 90 m reach. Damage per second, applied in steps of 0.1 s through the ordinary
      point damage with the hit point - so the shield facing is found by the existing rule and the shield bubble
      flares where the beam touches through the existing flare message (at most one per ship every 0.08 s). Full
      damage within 30 % of the range, 45 % at the end. Aim assist: the beam bends to a hostile ship within 1.6
      degrees of the aim line, 3 degrees for the locked target, when a second trace to it is clear.
      Quad damage applies; firing ends a cloak and the spawn shield.
    - Bank: 4 s of beam, refill starts 1 s after the beam stops and takes 5 s; emptied, it is locked until 25 %.
      Plain class `FHCJPhaserBank`, stepped by the server; the owner gets it in one byte.
    - Decision, weapon slots: the phaser is a second BASE weapon next to the blaster, not a pickup and not a third
      trigger. A ship has a base weapon (phaser; `B` switches it to the blaster and back; a new ship has the phaser
      again) and may hold one pickup weapon in its place - pickups replace what the trigger fires exactly as before
      and fall back to the base weapon. While the bank is locked the trigger fires the blaster (automatic fallback),
      so holding the button never does nothing; the HUD says "blaster until recharged". Torpedoes are untouched.
    - Replication: no RPC. One byte per ship to everybody (0 = off, 255 = full-length miss, else the length), the
      actor the beam holds (ship or turret; changes rarely), the bank and the base weapon to the owner. Every machine
      draws the beam each frame from the ship as it sees it (the smoothed mesh of a remote ship, the control rotation
      of the own one): to the hull of the held actor, or the replicated length along the nose.
    - Look: `M_HCJ_PhaserBeam` (built by `docs/tools/make_phaser_fx_materials.py`: one Custom node, no texture) on a
      quad from the muzzle to the end of the beam, rolled to the view: white core, orange-red body, a fast flicker and a
      ripple running along it. Glow at the muzzle and at the hit: the torpedo's star material. Sparks at the hit:
      the bolt impact effects every 0.12 s. Sound, all from what was in the project: Lyra's ring loop (the black hole
      rumble at 0.6 of its pitch) at 2.4 layered with the Hover Vehicles noise loop at 2.8, Lyra's dash at 1.8 for
      the start and its item-replenish cue at 0.7 for the stop. Nothing was imported for the phaser.
    - HUD: WEAPON panel with the bank in percent, a bar with the relock mark, RECHARGING, `[B]`; kill feed `[PHASER]`
      (the damage causer of a beam is the firing ship - that is how the kill is told from a bolt's); damage numbers
      of a beam add up in one number instead of ten a second; the shooter's hit cue comes at most every 0.3 s; the
      fire prompt of the onboarding now explains the beam; two new hints (bank empty, rocks can be broken); a row
      and two lines on F1 and CONTROLS; two death tips.
    - Bots: they aim straight at the target while the beam is what the trigger fires (the lead indicator of a beam
      is the target itself) and with lead for the blaster while the bank is locked; they hold the beam only inside
      6 / 4 / 3 degrees of aim error (easy / normal / hard) and keep 0 / 5 / 10 % of the bank unless the target's
      hull is below a quarter.
    - **Destroyable asteroids.** Decision: the layout is not touched (generator, checksum, golden tests as they
      were). Every rock is a node with a 16-bit id; a rock of 2.6 m bounding radius or more breaks into two or three
      pieces of 42-56 % of its size, a piece that is still that big breaks once more, anything smaller is simply
      destroyed. The pieces of a node are a pure function of (sector seed, node id) with their own hash stream; they
      lie inside the rock they come from (inside its sphere / its box, same rotation), so every clearance the
      generators guarantee around a rock holds for its pieces. The state of a match is the set of destroyed node
      ids: the server owns it and the health (radius x 0.22, 25 to 700; rock does not heal), `AHCJGameState`
      replicates it as a list of 4-byte keys that is only appended to (a late joiner gets all of it; emptied when
      a match starts, so the field regrows only then). Both sides log `Asteroids (...): N destroyed, checksum X`.
      Only phasers and torpedoes (direct hit and blast, 1.5 x damage) hurt rock; bolts do not.
    - How a rock goes: the instance of the collision shape and the instance of the drawn rock get a zero scale
      (the engine ends the collision body of an instance with zero scale, checked in its source, and the indices
      of the other instances stay); pieces are added instances. Bots avoid rocks by sweeping the collision that
      is really there, so they needed no change; their wander points come from the layout's spheres, which only
      errs on the safe side.
    - Problem (found by the test written for it): with the keys applied in reverse order - a piece before the rock
      it comes from - a client ended with 348 rocks where the server had 398: a piece that was already marked
      destroyed was left out when its parent went, and with it the pieces it should have left behind. Now
      `HCJAsteroids::CollectPieces` puts what a destroyed piece left behind in its place; the test applies the
      keys in order, all at once, reversed and shuffled with duplicates and gets the same rocks and checksum.
    - Problem: in the first headless match a torpedo removed a 4 m rock and both its pieces in one frame - the
      blast was applied after the direct hit, when the pieces already existed. The blast is applied first now.
    - **Shockwaves.** A torpedo detonation pushes every ship within 16 m (13 m/s at the centre, falling with the
      square of the distance to nothing at the edge), a ship's explosion every ship within 13 m (9 m/s). Server
      side, as a change of velocity inside the character movement; the owning client is brought along by a movement
      correction.
    - **Debris, flares, shake** (local, cosmetic, `UHCJEffectsDirector`): a destroyed ship throws 14 hull plates
      (the engine cube, flattened) and 10 glowing embers, a torpedo 5 embers, a rock chunks of the Megascans rocks
      (4 to 18 by its size); plain integration with drag, tumble, a bounce off rocks (one line trace per piece
      every other frame), the drift of a black hole, a push from later explosions; they shrink away over the last
      third of 3.2 s. 72 slots, a new piece replaces the one nearest its end. Flares: `M_HCJ_ExplosionFlare` (bloom
      flash, a horizontal streak, a star; same script) on at most 6 quads, 0.55 s. Camera shake by distance, with a
      switch on the settings page. The subsystem ticks only while something is alive; its meshes are created
      when the match is built (the first try created them in the frame of the first explosion: a hitch of 0.28 s).
    - Problems found in the pictures and fixed: the first flare whited out the middle of the screen at 18 m (3600
      uu wide, brightness 2.2: now 2800 / 1.5 and dimmer the nearer); the flare quad showed as a faint square
      (round falloff inside the quad now); hull plates were dark grey and 20-85 uu - not to be seen (now light
      grey, 45-150 uu, plus embers with the unlit glow material); the glow at the phaser muzzle hid the own ship
      (240 uu, brightness 3: now 150 / 1.6).
    - **Music.** Three tracks generated with Google Lyria (Gemini) by the user, as MP3 in `SourceAssets/Lyria`. The
      editor's sound factory imports MP3 itself (it lists mp3 next to wav / ogg / flac; checked in the engine
      source), so nothing was installed or converted: `docs/tools/import_lyria.py` imports them into
      `/Game/Audio/Lyria` on the music sound class and writes WAVs for measuring; `docs/tools/music_stats.py` (plain
      Python) measures them. Result: 44.1 kHz stereo, 167.7 / 175.6 / 168.6 s (combat / explore / menu), peak 0.0 /
      0.0 / -0.1 dBFS, RMS -14.3 dBFS each. The Lyra stems are -26.6 to -48.5 dBFS per file; mixed at their layer
      volumes about -30 dBFS (calm: pad + bass) to -25.5 dBFS (all six). The tracks play at a gain of 0.30 / 0.26 /
      0.32 (menu / explore / combat), i.e. about -25 dBFS, so the music slider means what it meant.
      Loop points: all three start at full level (-15 dBFS in the first quarter second) and end in an outro (menu
      fades from 157 s, explore drops to -24 dBFS at 157.6 s and fades to its end, combat stops at 164.7 s followed
      by 2.6 s of silence), so the ends do not match. The director loops them itself with a second voice: the next
      pass starts at 157 / 157 / 163 s, cross-faded over 4 / 4 / 1.5 s.
    - Integration: menu track on the title menu, explore track in a match, combat track when the music intensity
      (the number the stems already used) reaches 0.75, back when it is below 0.3 and the track has played 20 s;
      2.5 s crossfade; a track that fades out is paused and goes on from there. The stems are kept as "Lyra
      stems" under SETTINGS > Music and as the fallback. Compressed by the engine to 5.2 / 5.6 / 5.6 MB (Bink Audio).
    - Music verified by log only. Packaged build started without arguments: `Music: track explore -> menu` in the
      first second (the explore track is chosen for the few frames before the menu opens), at 15 s `Lyria tracks,
      playing menu for 15 s (pass at 15 s of 169 s, loops 0) ... weights menu 1.00, explore 0.00, combat 0.00 | gain
      of this track 0.30 (-10.5 dB), music class 0.40`. Hosting (editor binary): `track none -> explore`,
      `explore -> combat (intensity 1.00)` two seconds later when the autopilot opened fire, `combat -> explore
      (intensity 0.30)` 34 s after that. The loop: no ordinary run reaches 157 s, so the packaged game was started
      with the menu track's loop point overridden to 12 s (`-ini:Game:` on the command line): `track 1 starts its
      next pass (loop 1), cross-fading 4.0 s` at 12, 24 and 36 s, the music still playing at 45 s by the count of
      active sources. Whether the seam can be heard is not known.
    - Balance, measured as in items 21 and 22: headless dedicated server, 8 bots, seed 1234, no frag or time limit,
      300 s per match, four matches at a time (the last eight all at once). "Before" is the same binary with
      `-HCJNoPhasers -HCJNoRockDamage -HCJNoShockwaves`, run side by side with the others.

      | | matches | time to kill | kills / min | kills by phaser | deaths by turrets | rocks destroyed per match |
      |---|---|---|---|---|---|---|
      | before, easy (blaster as base weapon) | 5 | 3.87 s (3.60-4.18) | 14.7 (13.8-16.2) | - | 18 of 383 (4.7 %) | - |
      | first try, easy: 40 per second, relock at 30 %, rock 0.16 per uu | 2 | 5.16 / 4.11 s | 12.9 / 12.5 | 18 of 122 (15 %) | 5 of 127 | 0 / 1 |
      | **final, easy: 58 per second, relock at 25 %, rock 0.22 per uu** | 5 | 3.97 s (3.62-4.48) | 14.1 (10.1-16.6) | 89 of 357 (25 %) | 9 of 366 (2.5 %) | 1.2 (0-3) |
      | final, normal | 2 | 2.86 / 3.20 s | 16.2 / 19.0 | 74 of 178 (42 %) | 13 of 191 (6.8 %) | 0 / 1 |
      | final, hard | 2 | 2.19 / 2.60 s | 15.6 / 16.4 | 74 of 162 (46 %) | 5 of 167 (3.0 %) | 1 / 1 |

      Kills by weapon, easy, before: blaster 35 %, torpedo 22 %, missiles 19 %, rail 9 %, pulse 8 %, scatter 6 %.
      Final: phaser 25 %, missiles 25 %, torpedo 19 %, pulse 13 %, rail 8 %, scatter 6 %, blaster (the fallback) 4 %.
      Normal: phaser 42 %, missiles 20 %, torpedo 11 %, rail 10 %, scatter 7 %, pulse 6 %, blaster 4 %.
      Hard: phaser 46 %, rail 27 %, missiles 11 %, pulse 7 %, scatter 4 %, torpedo 3 %, blaster 1 %.
      Read: with 40 per second the phaser was a weaker base weapon than the blaster had been (15 % of the kills
      against 35 %, a tenth fewer kills per minute). With 58 the kill rate and the time to kill of easy bots are
      what they were (14.1 against 14.7 per minute, 3.97 against 3.87 s: inside the spread between matches) and the
      phaser is level with the homing missiles as the weapon with the most kills, a quarter of them; phaser and
      fallback blaster together make 29 % where the blaster made 35 %. Easy bots keep the beam on a ship only 55 %
      of the time it runs (normal 88 %, hard 97 %), which is why the phaser is clearly the main weapon on normal
      and hard. Torpedoes and pickup weapons keep their share on easy; on hard torpedoes hardly kill (3 %: hard
      bots break away from nine in ten). Normal and hard had never been measured: they kill faster (time to kill
      3.0 and 2.4 s) at about the same rate.
      The hit rate of the status line is not comparable any more: it divides hits of bolts by trigger pulls, and
      with fewer blaster shots the ten pellets of a scatter shot push it past 100 %.
      Rocks: bots almost never break one - 6 in the five easy matches, of 2834 in the world; they do not shoot at
      rock on purpose, a beam or a torpedo that misses does it. So the fields are far from being emptied by bots;
      what a player who sets out to clear a sector does to it in ten minutes was not measured (a full bank is
      232 points of damage: two and a half 4 m rocks).
      Shockwaves: 10 to 22 ships pushed per easy match.
    - Netcode, packaged build, one PC: a listen host (port 7791), a client that joined after 20 s and a second
      client that joined 55 s later (three windows of 800x450 on the one GPU), all flown by the capture autopilot
      (it holds the trigger), the host breaking the nearest rock of 2 m or more every 5 s with a development command
      (the server-side path a phaser or torpedo takes), `-LogCmds="LogHCJ Verbose"`. Layout checksum 3976D716 on all
      three. Rocks: the host logged 15 destructions (12 broke into pieces). The first client had three gone when it
      joined (`3 destroyed, checksum 4146F685 (..., already gone when this machine joined)`) and then logged every
      count from 4 to 15 with the checksum the host logged for that count (4: F9C60904 ... 15: 5978EC99). The late
      client got ten at once (`10 destroyed, checksum 0E4202DE (10 new, already gone when this machine joined)`,
      the host's value for 10) and then 11 to 15 like the host. Rocks in the world at the end: 2846 on the host and
      on both clients. Beams: the first client logged 55 beams going on in 100 s, 20 its own, 35 of other ships.
      - Not seen on a client: the break effect of a rock. The host broke rocks around itself, in other sectors
        than the clients were in, and a machine shows the effect only within 300 m. That a client has the same
        pieces is known from the counts and checksums, not from a picture taken on a client.
      - Movement corrections: with the client's autopilot not boosting (as in items 21 and 22), 100 s: 1 correction
        (232 uu off at 900 uu/s: the push of a ship exploding next to it - what a shockwave costs the owning client
        is one correction of a metre or two per push) against 0 in the same test with the packaged build of item 22
        (item 22 itself measured 2 to 4). With the autopilot boosting every other 5 s the client logged 30 in 100 s,
        at the marks where the boost meter empties or unlocks: the problem of item 21, unchanged.
      - The frame rate of the first client fell to 1 fps for about 20 s while the third instance started (three
        instances on a 2 GB card).
    - Frame rate, packaged, 1280x720, listen server with 7 bots, seed 1234, capture autopilot (it holds the trigger:
      phaser beam and blaster in turn), `-HCJNoEOS`, alone on the GPU, 8 s averages over 80 s, the build of item 22
      (a copy of its packaged folder) and the new one started alternately: before 100-116 and 106-126 fps (means
      107 and 115), after 101-118 and 88-108 (means 112 and 102); the final build once more after the last changes
      to the looks: 103-117 (mean 111). The same range; two runs of one build differ more than the builds do.
      - Worst case: the same scene with, every 2 s, a ship blown up 15 m ahead, a rock of 2.5 m or more broken and
        a torpedo fired while the own beam runs (development commands; 26 ship explosions and 28 rocks in 60 s,
        the debris pool at its cap of 72, two flares at a time): 2 s averages of 66-117 fps, mean 96. The same run
        without the explosions (a picture is taken every 2 s in both, and each costs one long frame): 91-123,
        mean 107. So about a tenth lower with something exploding all the time; the lowest 2 s average was 66 fps.
        This was measured one repackage before the final one (after it only the beam material and three sizes
        changed).
    - 57 automation tests, all passing (new: `HypeCubeJam.Phaser.Bank`, `.FalloffAndAssist`, `.WeaponSlots`,
      `HypeCubeJam.Asteroids.SplitDeterminism`, `.ReplicationRoundTrip`, `HypeCubeJam.Effects.ShockwaveFalloff`,
      `.DebrisCaps`, `HypeCubeJam.Audio.MusicTracks`). One existing test was changed for the new weapon (a base
      weapon has no ammunition; range instead of speed x lifetime).
    - Development aids: `-HCJNoPhasers`, `-HCJNoRockDamage`, `-HCJNoShockwaves`, `?Rocks=0|1`, `HCJMenuCmd fire <0|1> |
      baseweapon | god | killenemy | breakrock [min radius] | rockstatus`, verbose `Phaser:` / `Asteroid:` /
      `Effects:` lines, the `Phasers t=` status line, a `Music` line every 15 s.
    - Pictures (packaged build, 1280x720, scenes set up with development commands, the own ship idle and made
      undamageable): `Media/36_phaser_beam_on_shield.png` (the own beam on a bot parked 6 m ahead, seen from the side
      with the orbit camera: the beam, the glow at the muzzle, the bot's shield bubble flaring where it is touched,
      the damage number), `37_asteroid_breaking_apart.png` (a 16 m rock of a sector of shards a moment after it
      broke: the flare with its streak, chunks flying, its pieces), `38_explosion_flare_debris.png` (a bot blown up
      12 m ahead: the flare fading, hull plates and embers). ALL THREE WERE TAKEN WITH THE GAME SLOWED DOWN
      (`HCJMenuCmd slomo 0.05`, `0.06`, `0.1`): the frame after a screenshot request is longer than these effects.
      Effects were not lengthened. In 37 the flying chunks are the grey of the untinted rock mesh although the
      rocks of that sector are tinted green. A bot's beam on the own shield was seen in a picture that was not kept.
    - Packaged: 1059 packages cooked (1025 before: the three tracks, two materials and what the phaser / debris settings
      now reference). Zip `Packaged/mariuz_HypeCubeJam.zip`: 570.2 MB (867.6 MB unpacked, 47 files); 554.4 MB before,
      the difference is mostly the music (16.4 MB compressed in the pak).
    - Judged from logs and pictures only. Nobody has held the beam on a moving ship with a mouse, heard the beam
      loop or its start and stop, heard the three tracks or their crossfades and loop seams in the game, felt a
      shockwave, or seen debris and flares in motion.

## 15 Nov 2026 (ninth session) - seeing the enemy, bigger ships, station model, intro movie, portals, planets
24. "Enemies are hard to spot - make them more visible, or bigger if possible" (the player, after build 0.13.0; asked for
    the third time), plus a station model, an intro movie, portals that look like gateways and better planets.
    Visibility came first; the rest followed in that order. Everything is presentation except the ship size.
    - **Bigger ships.** Factor 1.7 on every machine: hull length 330 -> 560 uu (`ShipMeshLength`), movement collision
      radius 60 -> 100 (`HCJ::ShipCollisionRadius`, the capsule), hit sphere for bolts and beams 100 -> 135
      (`AimAssistRadius`; 1.35, not 1.7, see the balance below), shield bubble 200 x 165 x 100 -> 340 x 280 x 170, muzzle
      offset 160 -> 270, bots' avoidance probe 200 -> 280. Chase camera 700 -> 1150 uu behind and 140 -> 240 above, so the
      own ship is the size on the screen it was. The sector generator never reads the ship size: layout checksum
      3976D716 and the station / wormhole / pad / black hole checksums for seed 1234 are what the build before logged.
      Clearances were checked by their numbers, not by play: spawn points keep 600 uu from rock, a portal trigger is 700
      uu, a wormhole's 500, a pad's 320.
    - **Glow.** (1) Rim: the hull of every other ship is drawn a second time with `M_HCJ_ShipRim` as the component's
      overlay material - a fresnel rim in an enemy colour (red, 12 % of the player's colour mixed in) that widens to the
      whole silhouette between 20 and 120 m; the same overlay puts a small flat floor on every hull so the unlit side is
      not black. (2) Glint, the "minimum apparent size": from 40 m a point of light with a halo and fine spikes on a quad
      facing the view, 13 % of its distance across (full strength from 80 m), in place of the old 8-pixel beacon sphere.
      (3) Engine glow and a trail on every ship (a quad from the tail to a point that follows the tail with a 0.45 s lag,
      rolled to the view): enemies red, the own ship in its own colour. All built by
      `docs/tools/make_ship_fx_materials.py`; one Custom node each, no texture.
    - HUD: brackets and edge arrows drawn 3 px wide over a dark line (2 px before), larger (minimum half size 22 / 28 px
      instead of 17 / 22, arrows 28 / 36 instead of 20 / 26), fading to 85 % with distance instead of 60 %; four ticks
      that breathe around the locked target's diamond. Distance labels were already there.
    - Cloak: by the code - the overlay belongs to the hull component that a cloak hides, and glint, engine glow and trail
      are switched by the same flag. NOT looked at in a run (the development command cloaks only the own ship).
    - Setting "Enemy highlight" (SETTINGS page, on by default): off removes the rim and shrinks the glint to 40 %.
    - Problems found in the pictures and fixed: the first rim was pink, not red (additive red clips to white through
      the tone curve: darker, purer red and less of the player colour); the own engine glow was a ball that hid the ship
      (brightness of the own one cut to a seventh); **the own trail lay across the whole screen** in the first packaged
      run - it points from the tail at the chase camera, which sits right behind it (the own trail is now clamped to
      0.8 of the hull length, thinner and dimmer); at 37 m an enemy was a glowing blob (the glint now starts at 40 m
      instead of 25 m).
    - Before / after, same positions (`HCJMenuCmd scene` + `HCJParkEnemy`, bots held still with `HCJMenuCmd peace`), an
      enemy parked side on at 30, 100 and 250 m against the Milky Way band, a nebula and a planet; editor binary with
      `-game` for both (the "before" is the code before any of this, with only the new development commands), 1280x720,
      real time, nothing slowed: `Media/39..41`. Before: at 100 m and beyond the ship is a few dark pixels inside the HUD
      diamond. After: a red star about 60 px across with the hull's outline inside it at 100 m. `Media/42` is the 250 m
      case with the HUD switched off (`ShowHUD`): the enemy is the brightest thing in the frame.
      One thing these pictures show against the new look: a portal whose destination colour is red or pink has a
      landmark glint of nearly the same colour as an enemy (42: the portal on the right). The portal has its ring
      around it; no further separation was made.
    - Balance (method of items 21-23: headless dedicated server, 8 easy bots, seed 1234, no limits, 288 s, three matches
      at a time, editor binary). Before, from item 23: 14.1 kills per minute (10.1-16.6), time to kill 3.97 s.
      Hit sphere 160 (the full factor): 20.0 / 15.6 / 14.4 kills per minute (mean 16.7), time to kill 3.09 / 4.23 / 2.96 s
      (mean 3.43) - about a fifth more kills, missiles especially. Hit sphere 135 (final): 14.4 / 17.1 / 15.8 (mean
      15.8), time to kill 3.91 / 3.45 / 4.43 s (mean 3.93). So the final numbers are 12 % above the old mean in kill rate
      and level in time to kill, inside the spread between matches. Portal jumps 48-72 and wormhole jumps 10-12 per match:
      the bigger ships get through. No "before" run was made today on the same day's machine load.
    - **Station model.** "Space Station 4" by Gerardo Justel (CC-BY, Fab; glb of 155 MB: 20 meshes, 110 179 triangles,
      3 materials, nine 4096 textures). `docs/tools/prep_station4.py` (plain Python + Pillow) writes the eight maps the
      game uses at 2048 and a 6 MB glb with the 20 meshes merged into one; `import_station4.py` (editor commandlet)
      imports textures and mesh (Interchange glTF) and builds a plain lit material with two instances and an unlit lamp
      material; `station4_lods.py` (editor) makes four LODs. Result: one mesh, 98 914 triangles, three sections, LODs
      98 914 / 44 496 / 17 799 / 5 933, no Nanite (not tried: the game runs without Nanite, Lumen and virtual shadow maps
      on this card, so plain LODs were taken as the brief suggested), 57 MB of source assets in `Content/SpaceStation4`.
      In the game base colour and emission are limited to 2048, ORM and normal map to 1024.
      - Problem: with the importer's "combine static meshes" the result had 37 127 triangles and two material slots -
        a third of the model. The merge is now done in the prepare script (node transforms baked in, one primitive per
        material).
      - Problem: the model's "Emission" mesh is mostly a cloud of loose lights reaching 20 m from a hull that is 10 m
        long (found by plotting the vertices): in the game they would be points of light adrift in a fight, and they
        wrecked the fit by bounds. Only the 2 299 of 13 556 triangles within 0.6 m of the hull are kept.
      - Problem: LODs could not be made in the pythonscript commandlet (no static mesh editor subsystem there, and the
        older library returned -1); they are made by a third script run with `-ExecutePythonScript`.
      - **Fit: partial, and it is poor for half of the shape.** The model is two wheels on an axle (10.9 x 8.2 x 8.8 m);
        the collision is a cross: hub, a beam along X, a beam along Y, a mast, four panels. At scale 4 (the model 43 m
        long) the axle covers the hub, the beam along X and its two panels, and the turrets at the two ends of that beam
        sit on the wheel hubs (about a metre deep in them). The beam along Y, the mast and their two panels do not fit
        any part of the model: they are still drawn, as dark boxes sticking out of it, with their turrets at the ends -
        nothing solid is invisible, but it looks like a model with a cross of boxes through it. The corners of the hub
        box stand up to about 1.4 m out of the axle (not drawn). The collision was not touched.
      - The windows of the model (emission maps, boost 25) are hardly visible in the pictures; the lamps of LOD 0 are
        small. The station reads as a lit grey machine, not as a lit-up one. Not followed up.
    - **Intro.** `SourceAssets/Veo/intro_veo.mp4` (1280x720, 10 s, H.264 + one audio track, generated with Google Veo by
      the author) is `Content/Movies/intro_veo.mp4`, staged as a loose file. Decision: not the engine's startup movie
      player but the Media Framework from the game - a media player, media texture and file source created at run time
      by the local player controller, shown by a full-screen Slate widget in front of the title menu - because that can
      be proved from the game's own log and screenshots, be skipped by a development command, and carry the title.
      The movie's sound is NOT played (no sound component is attached; nobody has listened to it); the Lyria menu track
      starts with the first frame. The title "HYPECUBE JAM" fades in over the last three seconds, the picture fades to
      black in the last half second, then the menu. Any key, mouse button or gamepad button skips. The end is decided by
      a plain class (`FHCJIntroFlow`, tested): played to the end, skipped, no frame within 4 s, player error, or 20 s in
      any case. `-HCJNoIntro` switches it off; it plays only in a process that opens on the title menu from its command
      line (so never with a map URL, an address, `-AutoHost`, `-server`, `-nullrhi`, a capture run or automation), once.
      - Packaged build, started with `-HCJNoEOS -windowed` and nothing else: `Intro: opening Content/Movies/intro_veo.mp4`,
        `Intro: the movie plays (first frame after 0.62 s, 1280 x 720, 10.0 s long, 1 audio track(s) left silent)`,
        `Intro: over after 10.5 s at 10.0 of 10.0 s of the movie (played to its end); the title menu opens`, and
        `Music: track none -> menu` in the first frame. With `-HCJMenu -HCJIntro -HCJCapture=3`: pictures of the movie and
        of the menu after it (`Media/56..58`), 103-124 fps while it plays. Skip: `HCJMenuCmd skipintro` (what a key
        calls) at 5.2 s -> `Intro: over after 6.0 s at 5.2 of 10.0 s of the movie (skipped by a key or button)`.
        NOT tested: a real key press, a mouse click, a gamepad; full screen; a machine without the H.264 decoder
        (the timeout path is covered by the test of the plain class only).
      - In the very first run of a new package the 10 s of movie took 109 frames (about 11 fps; the log shows it), in
        every run after that 100 fps and more. Not investigated; the first run of a build compiles its shader pipelines.
    - **Portals.** Same trigger, same place. A ring of 28 dark blocks (every other one a little proud), 14 glowing glyph
      bars and an inner lip that turn at 7 degrees per second, and inside it one quad with `M_HCJ_PortalSwirl`
      (`docs/tools/make_portal_materials.py`): three layers of spiral arms and rings running inwards, the deeper ones
      smaller and shifted against the view by the camera vector in the quad's own space (parallax: a tunnel, not a
      disc; no refraction), a core, a rim, six sparks circling the rim - in the colour of the destination sector. A
      passage: every machine notices a ship whose position jumps by a sector (where the jump sound was already played)
      and flashes the portal nearest the place it left and the one nearest where it arrived - ripple from the centre to
      the rim, brighter, glyph ring spinning up, 0.9 s; nothing is replicated for it. Beyond 90 m a glint in the portal's
      colour keeps it a landmark. Wormholes keep their look (white-blue core in a flat swirl turned to the view) and
      get the landmark glint and a flare of the core on a passage: ring + coloured swirl against ball + flat disc.
      - Problem: the first swirl was a white-yellow disc again (emission of about 1.5 blooms out here): brightness 1.4
        -> 0.4 and the arms squared, so the dark between them stays dark.
      - The flash was not caught in a picture. Log: in the host + client run the host logged 22 passage flashes and the
        client 2 (`Portal: passage flash on the portal of sector ...`, verbose).
    - **Planets.** No new texture. Each planet of a sector is now its own sphere with `M_HCJ_PlanetSurface`
      (`docs/tools/make_planet_materials.py`): the planet's 256 px picture from the CelestialVault atlas projected along
      the line of sight (a body in the sky is always seen from the same side), a day side lit from the sector's sun with
      a soft terminator and a nearly black night side, a slowly drifting band / cloud pattern, an atmosphere rim in the
      planet's colour. Saturn is procedural bands (its atlas picture has the rings in it) and gets rings on one quad
      (`M_HCJ_PlanetRing`: banded, semi-transparent, a gap, the planet's shadow across them); Uranus a faint thin ring.
      Half as big again as before (`BodySizeScale` 1.5). The spheres moved from 0.97 to 0.6 of the backdrop radius so
      that the far side of the rings stays inside the sky dome. No city lights or aurora.
      - A planet whose sun is behind it is a dark disc with a bright rim (Neptune in sector 0010 for seed 1234): correct,
        and less of a show than the lit ones. The band drift is too slow to see in a still and was not watched.
      - The pictures are 256 px wide: at 300 px on the screen Jupiter and Venus are soft.
    - Development aids: `HCJMenuCmd scene <sector>,<look>[,<spawn point>[,<distance>]]`, `peace [0]`, `skipintro`;
      `-HCJNoIntro`, `-HCJIntro`.
    - 59 automation tests, all passing (new: `HypeCubeJam.UI.IntroFlow`, `HypeCubeJam.UI.IntroCommandLine`).
    - Frame rate, packaged, 1280x720, listen server with 7 bots, seed 1234, capture autopilot, `-HCJNoEOS`, 8 s averages
      over 80 s, the build before this session (a copy of its packaged folder) and the new one started alternately:
      before 115-131 and 110-132 fps (means 123 and 121), after 105-123 and 111-123 (means 115 and 117); the final
      package once more: 107-131 (mean 121). About 4 % lower in the alternated pairs, inside the spread of the last one.
      Close up (final package, 4 s averages, bots held still): station at 45 m 95-106 fps, at 110 m 115-128; portal at
      35 m 146-151, at 140 m 138-143; wormhole at 25 m 141; Saturn with rings 128; Neptune 131-138; Venus 119-127. The
      station is the most expensive thing to look at.
    - Packaged match (final package, autopilot, 91 s): 26 kills, 9 portal jumps, 1 wormhole jump, 1 error line (the
      known CelestialVault CDO message) and the same 19 warning lines, text for text, as the build before.
    - Host + client on one PC (final package, port 7795, two windows of 800x450, both on the autopilot, 80 s): layout
      3976D716, stations 41B00419 (6 of 16 sectors, 27 mounts), wormholes A385A205, pads D547574B, black holes 6ABCFA7F on
      both; 5 movement corrections on the client. That the client DRAWS stations and portals was not looked at in a
      picture of the client; it builds them from the same code path as the host.
    - Packaged: 1066 packages cooked (1059). Zip `Packaged/mariuz_HypeCubeJam.zip`: 583.5 MB (882.1 MB unpacked, 48
      files; 570.2 MB / 47 files before): the movie (9.5 MB, `HypeCubeJam/Content/Movies/intro_veo.mp4`) and the station.
    - Pictures, 1280x720, real time unless said: `Media/39_..41_enemy_visibility_*_before_after.png` (composites of six
      frames each, editor binary, see above), `42_enemy_250m_nebula_no_hud.png` (editor binary), and from the final
      package: `43_enemy_glow_phaser_fight.png`, `44_two_enemies_glints_from_death_camera.png` (autopilot match),
      `45_station_45m.png`, `46_station_110m_behind_rocks.png`, `47_station_65m_in_a_match.png`, `48_portal_close_35m.png`,
      `49_portal_close_30m_other_colour.png`, `50_portal_far_140m.png`, `51_wormhole_25m.png`, `52_saturn_rings.png`,
      `53_neptune_day_night_wormhole_glint.png`, `54_jupiter_behind_a_rock.png`, `55_venus.png`, `56_intro_frame.png`,
      `57_intro_title.png`, `58_title_menu_after_intro.png`. Scenes were set up with development commands; nothing was
      slowed down or lengthened.
    - Judged from logs and pictures only. Nobody has flown the bigger ships, looked for an enemy in a real fight with
      the new glow, watched the intro with a keyboard in hand, flown through a portal to see the flash, or seen the
      planets' bands move.

## 15 Nov 2026 (tenth session) - ship types, warp drive, cloaking device, tractor beam
25. "Add warp drive like in star trek", "add tractor beam like in star trek", "add cloaking device like in star trek", "add ship
    selection menu with different ship types". Four things in one session. Keys: hold Z warp, C cloak, hold R tractor beam
    (R still asks for a respawn when the ship is gone; T, F, V and Ctrl were taken).
    - **Ship types.** Five rows of one table in code (`Ship/HCJShipTypes.h`: interceptor, fighter, gunship, scout, cruiser);
      the fighter is the ship the game had. Per type: size (0.85 to 1.3: hull mesh, movement collision, hit sphere and
      shield bubble alike, on every machine), cruise speed, acceleration, braking, boost meter, hull, shield facing and its
      regeneration, fore armour, phaser and bolt scales, phaser refill, torpedoes, cloak meter, warp jump and cooldown,
      tractor reach / pull / drain. Decision: code, not config - the owning client predicts its movement with these
      numbers, and a table compiled into both ends cannot differ between them. Decision: the gunship's "strong fore shield"
      is fore armour (hits on the fore facing x 0.9), not a larger fore facing: the facings share one capacity in the
      health component and in the replicated word, and a multiplier in `TakeDamage` is three lines.
      Each type owns eight of the forty Star Sparrow hulls (meshes 1-8, 9-16, ...). **Nobody looked at the forty hulls to
      sort them by character**: the groups are the file order. A player picks type, hull (or "surprise me") and one of the
      ten paints (or "by pilot colour"); a chosen paint is also the player's colour on the HUD (black and grey lifted).
    - Path of a choice: `GameUserSettings.ini` -> `ServerSetShipChoice` (reliable; once per controller and from the hangar)
      -> `AHCJGameMode::SetShipChoice`, which validates it (unknown type = fighter, unknown hull / paint = pick for me;
      tested) -> player state (replicated: scoreboard) -> the next ship the server spawns (`ServerInitShipType`), or at once
      a ship that is still under its spawn shield -> type, hull and paint replicated on the ship -> `ApplyShipType` on every
      machine. `ProjectVersion` went from 0.1.0 to 0.2.0: replicated properties and one multicast changed, so builds
      before this one are a different network version (FIND GAMES lists them as not joinable; a join by address between an
      old and a new build was not tried).
    - Hangar: a page of the one Slate menu at the left of the screen (type, hull, paint, description, six bars, the
      numbers) and a LIVE preview: a local actor with the hull mesh and paint, placed in front of the camera every frame,
      turning 28 degrees a second. Reached from the title menu (the page shows "Your ship: <type>") and from the Esc menu
      ("your next ship").
    - **Warp drive.** Hold Z: 1.6 s charge, then a straight line at 150 m/s (boost is 47-70 m/s) for as long as the key
      is held, 2.2-3 s at most, then 6-16 s of cooldown by type (2 s after a stopped charge). No weapons while the drive
      is busy; a hit while it charges stops it; not while cloaked, not while held by a tractor beam. Decision on rock:
      the drive drops out BEFORE impact - each move at warp sweeps a sphere 0.3 s ahead against static geometry and ends
      the jump at boost speed; it also drops out 225 m from the sector origin when heading outwards (the safe radius is
      260 m). A ship that still hits something (another ship) loses 22 hull points. A portal entered at warp works: the
      server stops the drive in the tick of the teleport and the ship comes out at boost speed.
      - Netcode: the drive is part of the character movement simulation, like the boost meter. The key is a bit of every
        move, "this move was flown at warp" another, written after the client simulated the move; the client's saved moves
        keep the drive's state for combined moves and replays. The server steps its own copy and takes the START and the
        END of a jump from the client's bit, provided its own charge is complete to within 0.25 s and the jump is not more
        than 0.25 s over the longest. Direction: the thrust vector of the first move (the movement component makes it the
        nose), which the server receives with the move; from then on the velocity is fixed. When the server stops the
        drive, a counter replicated to the owner stops the prediction too.
      - The boost meter was changed to the same scheme (the bit now says "this move WAS boosted", the server's meter is
        the limit with a grace of 8 % of a meter). Reason: before, both ends ran the whole meter rule and could disagree
        about a move when it ran dry or unlocked - the burst of corrections of items 21 and 23. **That burst did not occur
        today with either build**, so the change is covered by a unit test (server and client call 120 s of moves alike
        with delta times 0.5 % apart) but not by a live before / after: packaged, host + client, boost held 15 s at a
        time: previous build 1 correction in 106 s, this build 3 in 110 s (none with boost on).
    - **Cloaking device.** C toggles. Meter 8-20 s by type, refill 16-22 s, locked from empty to 20 %. Decision (the
      Star Trek rule): shields are DOWN while cloaked - all four facings drop to nothing and regenerate at once, from
      nothing, after the decloak - and weapons are off: a fire attempt drops the cloak, weapons work 1 s later. No warp
      while cloaked. Revealed (still cloaked, but seen and targeted by all; the hull flickers): 1.2 s after a hit, while
      an enemy is within 15 m, while a tractor beam holds it. Hidden from: hull (a faint shimmer instead), glint, rim,
      engine glow and trail, engine sound, brackets and arrows, radar, map mark, target lock, lead circle, phaser aim
      assist, homing missiles and torpedoes, station turrets, bots beyond 25 m. The old pickup is now a CLOAK cell: full
      meter, no drain for 8 s. One test (`IsCloakHidden`) is asked everywhere; the Cloak bit of the power-up flags stays
      the replicated state, with a second bit for "revealed" and one byte of meter for the owner.
    - **Tractor beam.** Hold R: the locked target, else the nearest ship in the cone, within 60-85 m and 50 degrees of the
      nose, nothing solid in between. The victim flies at 0.55 of its speed and is pulled towards the holder (10-19 m/s,
      not closer than 13 m). Decision: it feeds on the holder's phaser bank (3 s from a full bank, 5 s on the cruiser),
      the holder may fire. It lets go when the victim boosted 0.9 s, the holder took 22 damage while holding, rock gets
      in between (checked every 0.15 s), range x 1.15 or the wide cone are left, or the bank is empty; then 2 s (torn
      free) before it can be used again. It reveals a cloaked ship and stops a warp drive.
      - Netcode: no RPC but the key. The holder's `TractorTarget` and the victim's `TractorHolder` are replicated to
        everybody; the victim's movement component slows it and applies the pull after the normal step, as it does the
        drift of a black hole - on the server and in the owning client's prediction. The client pulls towards the
        holder's REPLICATED position, which lags the server's: corrections while held, see below.
      - Cut: the beam does not pull pickups.
    - Bots: warp on a long clear way to a portal (rolled once per trip: 0.35 / 0.7 / 0.95 on easy / normal / hard) and to
      get away with a low hull (0.15 / 0.5 / 0.8), key held only with the nose within 6 degrees of the goal, let go 32 m
      short of it; cloak for an approach from afar or a retreat (0.03 / 0.1 / 0.2 per second), dropped 45 m from the
      target, when revealed or after 8-12 s; tractor beam on a target in reach, three times as readily on one that runs
      or is nearly done; caught in one, they boost away from the holder (0.4 / 0.8 / 1). A target that cloaks gets 1.2 s
      of fire at where it was. Bots are a mix of types in a fixed order.
    - Looks, all from what was in the project plus two materials built by `docs/tools/make_trek_materials.py` (Custom
      nodes, no texture): `M_HCJ_Cloak` (translucent: rim shimmer, crawling bands, a little refraction) and
      `M_HCJ_WarpStreaks` (additive radial streaks on a quad 0.8 m in front of the own camera). Warp: blue-white engine
      flare while charging, view widened from 95 to 128 degrees, long trail, a blue-white flare where another ship jumps
      and where it drops out (the explosion flare quad). Tractor beam: the phaser beam material in blue-white, 2.2-2.7
      times as wide. Sounds: existing waves at other pitches (phaser start at 0.45 for the charge, the portal sound at
      1.6 / 0.7 for jump / drop-out, the shield chime at 0.55 / 1.5 for cloak / decloak, phaser start / stop at 0.6 / 0.5
      for the beam). Nothing was imported.
    - HUD: WARP / CLOAK / TRACTOR rows (key, state, bar) in the WEAPON panel; under the own ship the lines WARP CHARGING
      (with a bar), WARP, CLOAKED, CLOAK REVEALED, DECLOAKING - weapons in n s, TRACTOR LOCK - name and distance, CAUGHT IN
      TRACTOR BEAM - [Shift] BOOST TO BREAK FREE; the type of the ship in the target panel (with "IN YOUR BEAM", "HOLDS
      YOU", "CLOAK REVEALED", "CHARGING WARP"), a SHIP column on the scoreboard, "(INT)" style tags in the kill feed
      (the kill event carries both types in one byte); four hints (caught in a beam; once each: warp, cloak, tractor);
      three rows and two lines on the F1 page and the CONTROLS page.
    - Problems found and fixed:
      - `float line = ...` in the streak material: `line` is a keyword of the shader language. The material failed to
        compile ("Default Material will be used in game") and the quad in front of the camera was drawn with the default
        material - **the whole view black with a grey grid for the length of a warp**. Seen in a picture; renamed.
      - The flare of the own jump whited out the lower half of the screen (the camera sits in it): no flare for the own
        ship, which has the streaks.
      - Raising the cloak made the shield bubble flicker in four places, played the collapse chime and triggered the
        "FORE SHIELD DOWN" hint: three or four facings gone in one update are now taken as "switched off".
      - The lines of the systems were first drawn above the crosshair, on top of the label of the target: moved under
        the own ship.
      - The first cloak picture showed "CLOAK REVEALED": the two parked ships were 12.5 m apart, inside the 15 m of the
        reveal rule. (The rule worked; the scene was wrong.)
      - The first bot match had the idle ship of the local player in it (9 ships; the fighter's deaths were its).
        `-HCJBotsOnly` keeps the local player without a ship.
    - **How the bot matches were run this time**: not as a dedicated server but as a standalone game without any network -
      `UnrealEditor-Cmd <project> "/Engine/Maps/Entry?Seed=1234?FragLimit=0?TimeLimit=0?Bots=8?Difficulty=n" -game -nullrhi
      -nosound -HCJBotsOnly -HCJNoEOS -HCJNoIntro -HCJQuitAfter=<s>` (the dedicated-server command line of the earlier
      sessions was not permitted to run in this session's sandbox). 8 bots, seed 1234, no limits; game time 288-410 s per
      match because the processes start slowly when four to eight run at once. "Classic" = the same binary with
      `-HCJClassic`: all bots fighters, none uses warp, cloak or tractor beam - the game before this session.
    - Balance. Kills / deaths by type, easy bots, pooled over the matches of a batch (K/D in brackets); the table was
      changed between batches:

      | batch (matches) | interceptor | fighter | gunship | scout | cruiser | kills / min | time to kill |
      |---|---|---|---|---|---|---|---|
      | 1: first table (2) | 36/46 (0.78) | 25/24 (1.04) | 23/9 (2.56) | 29/58 (0.50) | 40/24 (1.67) | 16.0, 15.8 | 3.61, 4.75 s |
      | 2 (3) | 44/71 (0.62) | 37/28 (1.32) | 36/22 (1.64) | 61/57 (1.07) | 60/69 (0.87) | 13.9-18.6 | 3.62-4.08 s |
      | 3 (5) | 120/93 (1.29) | 57/58 (0.98) | 43/54 (0.80) | 101/119 (0.85) | 93/106 (0.88) | 13.4-17.0 | 2.93-5.07 s |
      | 4 (4) | 90/91 (0.99) | 40/43 (0.93) | 50/38 (1.32) | 92/89 (1.03) | 64/90 (0.71) | 13.7-17.1 | 3.57-4.69 s |
      | 5 (5) | 94/121 (0.78) | 46/49 (0.94) | 59/44 (1.34) | 105/120 (0.88) | 106/98 (1.08) | 14.2-15.3 | 3.66-4.43 s |
      | 6 (5) | 104/118 (0.88) | 41/57 (0.72) | 49/57 (0.86) | 119/123 (0.97) | 114/87 (1.31) | 13.7-17.1 | 3.30-5.10 s |
      | **7: final table (5)** | 148/146 (1.01) | 55/70 (0.79) | 75/62 (1.21) | 140/153 (0.92) | 129/134 (0.96) | 14.4-18.2 (mean 16.2) | 3.86-4.56 s (mean 4.20) |
      | classic, same binary (3, one each in batches 1, 2, 6) | - | all | - | - | - | 18.8, 14.8, 18.0 | 3.62, 3.77, 3.87 s |

      Final table, normal bots (2 matches, 260 kills): interceptor 73/70 (1.04), fighter 43/32 (1.34), gunship 35/22
      (1.59), scout 54/84 (0.64), cruiser 55/59 (0.93); 18.5 and 20.4 kills per minute, time to kill 2.98 and 2.78 s.
      Hard (1 match): 1.10, 1.50, 1.47, 0.73, 0.81; 18.4 kills per minute, 2.50 s.
      Read: on EASY the final table is inside +-25 % of the mean of the five ratios (0.98): interceptor +3 %, fighter
      -19 %, gunship +24 %, scout -6 %, cruiser -2 %. That is the last of seven batches and the gunship (one bot of
      eight, 60-75 kills and deaths per batch) is at the edge; one batch cannot tell 1.2 from 1.0 for it - the fighter,
      whose numbers never changed, ran from 0.72 to 1.32 between batches. On NORMAL and HARD it is NOT inside: the scout
      is 30-40 % below the others in every batch that had normal bots (0.60, 0.65, 0.75-0.84, 0.75, 0.64), gunship and
      fighter above. Not retuned: what the scout is good at (cloak, warp, boost) a bot hardly uses to win a fight.
      Batch 7 ran while the package was being built (all eight cores busy), which lowers the bots' tick rate; its
      kill rates are not to be compared with the others.
      Kill rate and time to kill against the classic bots of the same batches: 16.2 (and 15.5 in batch 6) against
      14.8-18.8 per minute, 4.1-4.2 s against 3.6-3.9 s. Inside the spread between matches; the fights last a little
      longer (heavier hulls, ships that warp away).
    - What the bots do with the systems (final table, per match of 395-410 s): easy - 22-41 warps (2-7 of them stopped
      by the server: a hit while charging, a tractor beam, the portal the bot flew into), 4-10 cloaks (0-1 reveals), 28-38
      tractor holds (13-19 torn free, 7-16 bots boosted free); normal - 65-73 warps, 23-24 cloaks, 42-43 holds; hard - 78
      warps, 43 cloaks (9 reveals), 65 holds (37 torn free, 41 bots boosted free). Collisions at warp: 0 in all matches
      of the session (the look-ahead drops the drive first). Kills by weapon, easy, batch 6 (427 kills): phaser 23 %,
      torpedo 22 %, missiles 21 %, pulse 13 %, rail 8 %, scatter 7 %, blaster 7 %. No error line in any bot match log.
    - Netcode, packaged build, listen host + one client on one PC (loopback, ports 7791-7794, two windows of 800x450,
      `-LogCmds="LogHCJ Verbose"`, both end by themselves with `-HCJQuitAfter`); evidence is the client's log:
      - Checksums: layout 3976D716 on both in every run; pads D547574B on both.
      - Types: the host started with type 0 and the client with type 4, hull 2, paint 5 (`-ini:GameUserSettings:`
        overrides); host log `Ship choice: User asked for type 4, hull 2, paint 5 -> Cruiser, hull 2, paint 5`, client
        log for each of its five ships `Ship type (client): ... (own ship) is a Cruiser (hull 2, paint 5)`; its
        corrections show the cruiser's speeds (705 = 0.3 x 2350).
      - Seeing other ships (run 2: the host's ship is put behind the client's by a development command every 10 s and
        uses beam, cloak and warp in turn): the client logged 11 times `Tractor: beam of ... holds ...`, 8 times `the own
        ship is caught by the beam of ...`, 2 cloaks and 1 decloak of other ships, 1 other ship going to warp and 2 warp
        flashes shown. In a plain match with normal bots (run 1, 100 s) it saw 1 other ship at warp and 2 flashes, no
        cloak and no beam: ships are only known in the client's own sector.
      - Movement corrections of the client per run of 100-110 s, autopilot without boost: 3 (run 1; the previous build
        the same day: 2). While the client's own ship warps (run 3, `-HCJAutoWarp`: 3 jumps, 3 more charges stopped by
        hits): 0 corrections at warp, 0 while charging, 1 in the cooldown (33 uu), 6 in the run. While held by a beam
        (run 2: 8 holds of up to 5 s by a cruiser): 10 corrections in all while held - mostly 2 uu, some of 30 uu, one of
        120 uu - of 17 in that run (the host's ship is teleported next to the client's every 10 s). So: a jump costs
        nothing, a tractor beam costs one or two small corrections per hold.
    - Frame rate, packaged, 1280x720, listen host with 7 bots, seed 1234, capture autopilot, `-HCJNoEOS -HCJNoIntro`, 8 s
      averages over 80-88 s, the build before this session (a copy of its packaged folder) and the new one in turn:
      before 121-134 fps (mean 127; at the start of the session), after 74-101 (92; the first run of the new package),
      after 77-94 (85), before 80-101 (90), after 93-118 (104), before 86-115 (99). The machine was a fifth slower
      for BOTH builds after four hours of builds and bot matches (other programs took a fifth of the processor in the
      idle check); in the alternated runs the two builds are in the same range (before 90 and 99, after 85 and 104).
      A run with the first number's conditions was not repeated for the new build.
    - 66 automation tests, all passing (new: `HypeCubeJam.Systems.WarpStateMachine`, `.WarpFollow`, `.BoostFollow`,
      `.TractorPull`, `.CloakDevice`, `HypeCubeJam.ShipTypes.Table`, `.ServerValidation`).
    - Development aids: `-HCJQuitAfter=<s>`, `-HCJBotsOnly`, `-HCJClassic`, `-HCJBotType=<0..4>`, `-HCJAutoWarp`,
      `HCJParkEnemy <uu> <right> <up> <which ship>`, `HCJMenuCmd warp <0|1> | cloak | tractor <0|1> | ship <type>[,<hull>[,
      <paint>]] | hangar | enemyship <type>,<hull>,<paint>,<which> | enemywarp <0|1>[,<which>] | enemycloak [<which>] |
      enemytractor <0|1>[,<which>] | holdplayer [0]`, the `Systems t=` status line, verbose `Warp:` / `Cloak:` /
      `Tractor:` / `Ship type` lines, warp phase and "tractor held" in the `Correction` line.
    - Pictures (packaged build, 1280x720, standalone game, scenes set up with development commands, bots held still,
      the own ship idle): `Media/59_warp_charging.png` and `60_warp_streaks.png` (the own ship 0.05 s before and after
      the jump; **game slowed to 0.05**), `61_other_ship_warp_flash.png` (a parked cruiser jumping, **slowed to 0.05**),
      `62_tractor_beam_holder_view.png` and `63_caught_in_tractor_beam_warning.png` (**slowed to 0.4**),
      `64_cloaked_ship_shimmer_next_to_uncloaked.png` (a cloaked cruiser at 29 m left of an interceptor at 26 m; real
      time), `65_own_ship_cloaked.png` (real time), `66_hangar_interceptor.png`, `67_hangar_cruiser.png`,
      `68_hangar_gunship.png` (the hangar from the Esc menu), `69_hangar_title_menu_scout.png` (from the title menu),
      `70_two_ship_types_in_match.png` (a cruiser and an interceptor in front of the own fighter),
      `71_scoreboard_ship_types.png`, `72_help_page_new_keys.png`. Effects were not lengthened. In 64 the shimmer is
      faint: a dark blue outline against the Milky Way; against black sky it is harder to see still.
    - Packaged: 1068 packages in the cook (1039 cooked, 29 skipped for the platform; 1066 before: the two materials).
      Zip `Packaged/mariuz_HypeCubeJam.zip`: 583.6 MB (882.3 MB unpacked, 48 files; 583.5 MB / 48 files before).
    - Judged from logs, bot matches and still pictures only. Nobody has flown a warp jump, held a ship with the beam
      while shooting it, hunted a cloaked ship or clicked through the hangar; nobody has heard any of the new sounds.

## 15 Nov 2026 (eleventh session) - the look of the intro movie
26. "Make it close to the cinematic intro" (the player, after watching the Veo movie of item 24 in front of the game: black
    space, sparse grey angular rocks, small engine dots). A look pass: no rule, layout, collision or replicated state
    changed (`ProjectVersion` stays 0.2.0). Order of work as asked: sky, rocks, engines, weapons, portal, post process.
    - **How it is built.** Two new scripts, the pattern of the earlier ones: `docs/tools/make_cinematic_textures.py` (plain
      Python + Pillow, fixed seeds) writes three textures to `SourceAssets/Cinematic` - a tileable 1024 px cloud noise
      with four different fields in R, G, B, A, an atlas of sixteen rune-like glyphs (strokes on a 3 x 5 grid of points; no
      real script), a tileable crater normal / cavity map - and `docs/tools/make_cinematic_materials.py` (editor
      commandlet) imports them to `/Game/HCJ/Cinematic` and builds `M_HCJ_Nebula` (rebuilt), `M_Asteroid` (rebuilt),
      `M_HCJ_DecorRock` + four instances, `M_HCJ_EngineJet`, `M_HCJ_ImpactStar`, `M_HCJ_PortalRunes`, `M_HCJ_PortalRays`.
      `make_portal_materials.py`, `make_phaser_fx_materials.py` and `make_ship_fx_materials.py` were edited and run again
      (swirl filaments, thicker beam, glint backing). Decision for the web build that is planned: every new effect
      material is unlit + additive (or alpha composite) over texture coordinates, with at most a few texture samples -
      nothing reads scene colour, scene depth or custom depth, nothing refracts. The old nebula computed four octaves of
      noise per pixel; the new one samples one texture three times.
    - **Sky** (`AHCJSpaceBackdrop`). Per sector: the three seeded clouds as before (their place is tested and unchanged),
      one large BACKLIT CORE cloud (a white-hot point, the cloud around it lit from behind, dust lanes cutting into it), two soft haze
      cards at infinity so that no direction is black, and three soft clouds that stand IN the world 600-1000 m from the
      sector origin: they shift against the sky at infinity when a ship crosses the sector (the only real parallax; the
      rest of the sky is at infinity). Colours: the sector tint, hue-shifted per cloud and pulled 35 % towards teal; haze
      and near clouds mostly deep blue. Stars: the whole 10 000-star catalogue (it was cut at magnitude 6: 5070) plus 5000
      made-up faint ones in the same instanced draw - 15 001 with the sun.
      - Problem (first pictures): the whole screen was a milky fog and the core a white wall - haze and core far too
        bright, the cloud without dark gaps. Brightness of haze 0.5 -> 0.09, near clouds 0.5 -> 0.22, the cloud term
        squared, the white-hot core shrunk to about 6 degrees.
      - Problem: sectors with a yellow or yellow-green tint had an olive-mud sky (a dim yellow is brown). Hues between 45
        and 105 degrees are folded to gold or green for the SKY only (rocks, portals and the HUD keep the tint).
    - **Lights and rocks.** Sun 4 -> 6. The fill light (1 -> 2.2) now shines from where the core cloud of the sector's sky
      stands, in its colour: `AHCJUniverse::ApplySectorLighting`, called by the backdrop when the view changes sector (the
      core is always at least about 80 degrees away from the sun, so it lights the side the sun leaves dark). `M_Asteroid`
      got a fresnel rim light in the emissive (colour of the back light, stronger on the side that faces it) and craters
      (the crater map over the mesh UVs: bends the normal, darkens the bowls). The four Megascans boulders are the same
      meshes: they are still angular boulders, not the round cratered rocks of the movie.
    - **Decorative rocks** (`World/HCJSectorDecor.h`, plain C++; `AHCJSpaceBackdrop::BuildDecor`). Per sector 1600 pebbles
      (radius 7-45 cm; 70 % hang around the real rocks, the rest float free) and a belt of 180 boulders (2.5-45 m) in a
      flattened ring whose every surface is beyond the kill radius + 20 m (a ship is dead before it gets there, so
      they need no collision). A pure function of the sector seed with its own hash; only the sector the view is in
      is drawn, rebuilt on a jump; hierarchical instanced meshes without any collision, forced to the last LOD, pebbles
      culled beyond 90 m. Nothing is replicated; bots, aim assist, the phaser trace and line-of-sight tests cannot see
      them (they query collision; these have none). No pebble within 15 m of a portal, 6 m of a spawn point, 5 m of a
      pad, inside the station's or the black hole's zone, or inside a real rock; a pebble nearer than 7 m to the camera
      is not drawn (its vertices collapse into the camera in the vertex shader). Layout checksum for seed 1234 is
      3976D716 as before (the generator is not touched; the new test checks the layout checksum before and after).
      - Tried and not kept: hierarchical instances for the REAL rocks, expecting fewer triangles in a dense field. No
        faster on this card (92 against 97 fps, editor binary, densest sector), so they stay plain instances.
    - **Engines.** A jet behind every ship (`M_HCJ_EngineJet` on one quad from the tail straight back, rolled to the view:
      white-hot core, coloured body, running shock diamonds), 0.45 hull lengths at rest to 2.4 under full boost; enemies
      red, the own ship in its own colour. The own jet points at the chase camera, so it is 40 % of that length - it runs
      down the screen from the ship as in the title frame of the movie and ends before the lower edge. One jet from the
      middle of the tail, whatever the hull: the forty hulls have their nozzles in different places and nobody mapped them.
    - **Weapons.** Phaser beam: quad 130 -> 210 uu wide, a thicker white core and a wide halo. Impact stars
      (`UHCJEffectsDirector::PlayImpactStar`, a pool of ten quads with `M_HCJ_ImpactStar`: core, halo and two rings of thin
      radial streaks of unequal length that fly outwards for a third of a second) at every bolt hit, every 0.12 s where
      a beam burns, and on the shield bubble where a facing is hit. Bolts thicker (32 -> 44) and brighter (40 -> 70).
    - **Portal.** Ring heavier (blocks 240 x 190 instead of 170 x 150) and nearly black; 22 rune quads on each face
      (`M_HCJ_PortalRunes`, one of sixteen glyphs each by a hash of the two sectors, breathing one after the other, turning
      against the lip); five thin loops of light through the middle of the swirl; rays radiating from behind the ring
      on one quad in the portal's plane (`M_HCJ_PortalRays`). All in the destination colour; the passage flash drives
      runes and rays too. Trigger, position and size of the opening are unchanged.
      - Problem: the first ring was pale grey, not dark: albedo 0.07 under a sun of 6. Albedo 0.016.
      - Problem: close up the swirl was a white disc again (item 24 had the same): swirl 0.4 -> 0.22, filaments up.
    - **Post process and the setting.** An unbound post process component on the backdrop actor: bloom 0.8 above a
      threshold of 1.0 (the engine default is 0.675 on everything, which fogs a bright sky), vignette 0.45, contrast 1.08,
      saturation 1.1, shadows gained towards teal, highlights towards orange. Lens flare: the property is there
      (`CineLensFlare`), 0 by default. SETTINGS > "Cinematic look" (on by default, `UHCJUserSettings::bCinematicLook`):
      off disables the component - the picture the engine renders by default, as before this item - and keeps 35 % of
      the decorative rocks. It does not switch the sky, the lights, the rim light, the jets or the portal look back.
      The HUD is drawn after the post process and is not affected.
    - **Seeing the enemy against the new sky** (the third-time request of item 24 must not be undone). An additive red
      glint on a bright nebula turns pink-white. `M_HCJ_ShipGlint` is now "alpha composite": its `Backing` parameter
      (0.85 for an enemy's glint, 0 for engine glows and portal landmarks) darkens the sky behind the glint in a soft disc
      before the light is added; brightness 3.5 -> 4.2. Checked with two enemies parked in front of the brightest thing
      in the game (the core cloud of sector 0000) at 38 m, 100 m and 250 m: see "Verification".
    - **Problems found in the pictures and fixed** (besides those above): the star of a shield hit and of a turret bolt on
      the OWN ship covered the ship (the chase camera is 11 m behind it) - a hit within 5 m of the ship the view follows
      is now drawn at 40 % size and half brightness; the first enemy-to-enemy beam picture had no beam (the build had
      failed: a local variable hid a class member) - found because the picture was unchanged.
    - **Frame rate** (packaged, 1280x720, GTX 950, listen host with 7 bots, seed 1234, `-HCJNoEOS -HCJNoIntro`; the previous
      package - a copy made before the first change - and the new one started in turn; fixed scenes set up with
      development commands, bots held still, 2 s averages; "worst case" = the densest field with the phaser held while
      ships are blown up, rocks broken and torpedoes fired every 2 s for 24 s; "standard" = the capture autopilot in a
      bot match, 8 s averages over 88 s):

      | scene | before (3-4 runs) | first package | after soft cards + one pebble mesh | final (3 runs) | change |
      |---|---|---|---|---|---|
      | dense field, sector 0101 (337 rocks) | 136.5-137.0 | 111-114 | 119-121 | 121.2-122.3 | -11 % |
      | portal at 35 m | 158.3-159.2 | 137-138 | 140-141 | 138.6-140.0 | -12 % |
      | dense field, sector 0011 (264 rocks, station) | 148.7-149.5 | 117-119 | 128-129 | 131.6-132.1 | -12 % |
      | worst case, mean (lowest 2 s) | 120.1-120.2 (103-104) | 102-103 (91) | 109 (95) | 108.4-110.4 (97) | -9 % |
      | standard: autopilot match, mean | 127.4-136.6 (mean 133.6) | 112-116 | 121.5, 124.2 | 118.2, 122.5 | -8 to -10 % |

      So: about a tenth slower everywhere, 97 fps in the worst two seconds measured. The standard scene scatters by +-4 %
      between runs of the same build (different fights), so "-8 to -10 %" is as fine as it gets.
      - Where it goes (same scenes, the first package with settings overridden on the command line): without haze and
        near clouds +8 %; without decorative rocks +7 % (sector 0101) to +13 % (0011); post process off: nothing
        measurable (the gain of "Cinematic look off", +3 % to +8 % in the dense fields, is its smaller decoration);
        5000 fewer stars: nothing; lens flare at 0.5: -2 % for a ghost one has to look for - left at 0.
      - What was done about it: the soft cards (haze, near clouds) leave the shader after ONE texture sample; all pebbles
        are drawn with the rock mesh that has the fewest triangles in its last LOD (420 instead of 1054-1406; at half a
        metre the boulders cannot be told apart); pebbles are culled beyond 90 m; haze 3 -> 2 cards, near clouds 4 -> 3,
        belt 260 -> 180, ray quad 3.2 -> 2.9 portal radii.
      - A first "before" run (84 / 112 / 104 fps in the three fixed scenes) was thrown away: it ran right after an
        editor session and was a third slower than the four later runs of the same package. Why was not looked into.
        In one later "before" run the first scene read 120 instead of 137 for its first seconds; that value is left
        out of the range above, its other scenes are in.
      - The frame-rate table is from the package before the last two cosmetic changes (the small star on the own ship,
        glint backing 0.7 -> 0.85); the final package was not measured again.
    - **Verification** (final package unless said).
      - 67 automation tests, all passing (new: `HypeCubeJam.World.SectorDecor` - the same rocks for the same sector, pebble
        sizes, keep-outs, no pebble inside a real rock, the belt beyond the kill radius, the layout checksum untouched).
      - Autopilot match, 91 s, listen host with 7 bots: 25 kills, 16 portal jumps, 16.5 kills per minute, time to kill
        3.62 s; 4 warps, 1 cloak, 4 tractor holds by bots. 1 error line (the known CelestialVault CDO message) and 19
        warning lines - the same lines, text for text with the numbers blanked, as a match of the previous package run
        the same way. No "Default Material" line in any run (a material that fails to compile is drawn with it).
      - Host + client on one PC (port 7795, two windows of 800x450, both on the autopilot, 95 / 72 s): layout 3976D716,
        stations 41B00419, wormholes A385A205, pads D547574B, black holes 6ABCFA7F on both; 1 movement correction on the
        client; the client logged its own decoration (`Backdrop: sector 0 decorated with 1600 pebbles and 180 belt
        boulders`): each machine makes its own, nothing is sent.
      - Seeing the enemy against the brightest sky (`Media/83`, `84`; the backlit core of sector 0000): at 38 m the hulls
        with their red rim and engine glow are plain; at 100 m and 250 m with the HUD off each ship is a point with a
        red halo - but in front of the white-hot middle of the core the point itself is pink-white, not red, and it is
        one bright point among a few (the portal's landmark glint, pickups). With the HUD on, brackets, diamond and
        arrows are unchanged. Weaker than on black sky (`Media/42` of item 24), better than without the backing.
      - Mobile renderer: NOT run. One attempt with the editor binary and `-featureleveles31` did not get past the
        start-up compilation of the engine's mobile shaders in 150 s and was stopped. By construction: no scene colour,
        no custom depth, no refraction in anything new; `[branch]` + `Texture2DSampleLevel` in the nebula, per-instance
        custom data in the nebula and rune materials (both used the same way by materials that existed before), a
        second directional light (the fill) that the mobile forward renderer will not shade with, image-based lens
        flare (off) that it does not have, world position offset in `M_HCJ_DecorRock`.
    - Development aids: `HCJMenuCmd cinematic <0|1> | fly <0|1|2> | impact [uu] | enemyfire <0|1>[,<ship>[,<target>]]`,
      `scene <sector>,backlight`; the `scene` log line names the theme, the number of rocks and the tint of the sector.
    - Packaged: 1080 packages in the cook (1051 cooked, 29 skipped for the platform; 1068 before: five new materials, four
      material instances, three textures). Zip `Packaged/mariuz_HypeCubeJam.zip`: 584.8 MB (883.6 MB unpacked, 48 files;
      583.6 MB / 48 files before).
    - Pictures, 1280x720, final package, real time unless said, scenes set up with development commands, bots held still:
      `Media/73..82_*_before_after.png` (the previous package left, the new one right, same commands: sky in sectors 0000,
      0101, 0011 and 1010, the dense field of 0101, portal at 35 m and at 140 m, station, two enemies at 38 m, the own
      phaser beam - **game slowed to 0.2** for that one), `83_two_enemies_38m_against_the_brightest_nebula.png`,
      `84_enemies_100m_and_250m_against_the_brightest_nebula_no_hud.png`, `85_phaser_hit_seen_from_the_side_slowed.png`
      (one bot firing at another, **slowed to 0.15**), `86_own_engine_jet_cruise.png`, `87_own_engine_jet_boost.png`,
      `88` / `89_backlit_core_sector_*.png`, `90_portal_close_30m_runes_filaments_rays.png`,
      `91_settings_cinematic_look.png`, `92..94_no_hud_*.png`, `95_game_next_to_intro_frame_56.png`,
      `96_portal_next_to_intro_frame_57.png`, `97` / `98_match_*.png` (the autopilot match of the verification),
      `99_worst_case_scene_explosion_in_the_dense_field.png` (from a frame-rate run of the package before the last two
      cosmetic changes).
    - **What is still far from the movie.** The rocks are four angular scanned boulders with craters painted on, not
      round cratered bodies, and far fewer are near the camera (the movie's field would be solid rock to fly through;
      the pebbles are below half a metre on purpose). Enemy ships are the same bright Star Sparrow hulls in a red rim,
      not dark angular ships with red lights (not attempted: a paint is also the pilot's colour on the HUD). One jet
      per ship from the middle of the tail instead of one per nozzle. No volumetric nebula: cards, flat when a ship
      could fly near one (it cannot). No motion blur, no depth of field, no lens flare. The own phaser beam is seen
      from behind by the chase camera and reads as a short orange wedge, not as the long beam of the movie. The station
      is unchanged (item 24's half fit). Sectors with a gold or olive tint (1010) are the least like the movie's teal.
    - Judged from logs and still pictures only. Nobody has flown through the pebbles, watched the near clouds shift, seen
      the jets or the runes move, or looked at a whole fight with the stars of the hits going off.

## Measurements
- Automation tests: 9 at the start, 67 now, all passing (added: decorative rocks - determinism, keep-outs, belt beyond the kill radius, layout untouched; ship systems - warp state machine, warp and boost followed by the server, tractor pull, cloak device - and ship types - table, server validation; the intro flow and its command-line rule, phasers, destroyable asteroids, shockwave / debris rules, music tracks, photon torpedoes, shield facings, online logic, hints, rock visuals, ship look, sector sky, black hole
  placement, black hole gravity, pickup pad placement, weapon rules, wormhole links, wormhole routing, station
  placement, lead aim).
- Frame rate (editor binary, -game, 8 s averages): 101-128 fps with basic shapes (`-HCJNoAssets`), 98-129 fps
  with ships/rocks/effects, 80-112 fps with the sky as well. Client of a dedicated server: 93-115 fps.
- Server and client layout checksums match (7C0CB60C for seed 1234, 2853 asteroids, 24 portal links).
- Typical 90 s bot match: about 900 shots, 190 hits, 20 kills, 34 portal jumps.
- The editor binary on the 2 GB card intermittently reports "video memory exhausted" (120-590 MB over budget),
  also with basic shapes only.

## Not done / open
- Item 26 (the look of the intro movie) was judged from logs and still pictures.
  - About a tenth slower than the build before (table in item 26); 97 fps in the worst two seconds measured. "Cinematic
    look" off gives back 3-8 % in dense fields (fewer decorative rocks); the post process itself costs nothing measurable.
  - Not run with the mobile renderer (the web build that is planned): see item 26, "Verification", for what to expect.
  - Against the white-hot middle of a sector's core cloud the glint of a far enemy is pink-white rather than red.
  - Rocks are still the four angular boulders; enemy ships are not dark; one jet per ship, not one per nozzle; no lens
    flare (2 % for almost nothing); bots were not given a dark paint (optional target 7, not attempted).
  - The pebbles vanish within 7 m of the camera by a hard step in the vertex shader (a pebble may pop in or out for a
    frame); nobody has flown through them. A destroyed rock leaves its halo of pebbles behind.
  - The own phaser beam, 210 uu wide now, reads as an orange wedge from the chase camera.
  - The frame-rate table is from the package before the last two cosmetic changes; the final package was not re-measured.
  - `Content/Megascans` and `SourceAssets` are not tracked by git here: the rebuilt `M_Asteroid` and the three source
    textures of `SourceAssets/Cinematic` exist on this disk (both scripts recreate them).
- Item 25 (ship types, warp drive, cloak, tractor beam) was judged from logs, bot matches and still pictures.
  - Ship types are balanced for EASY bots only (kills / deaths inside +-25 % of the mean in the last batch, gunship at
    the edge). On normal and hard the scout is 30-40 % below the others; not retuned. How the types feel to a pilot
    (speed, the fore armour, a 30 % bigger cruiser) is untested.
  - The eight hulls of each type are the file order of the pack; nobody sorted the forty hulls by how they look.
  - The fix for the boost-meter corrections could not be shown in a run: the burst did not occur with the build before
    either (1 correction in 106 s; the new build 3 in 110 s).
  - A ship held by a tractor beam gets one or two small movement corrections per hold on its owner's machine.
  - The frame-rate comparison was made on a machine that had become a fifth slower during the session; the two builds
    were only compared in alternated runs (same range), not at the session's first number.
  - The tractor beam does not pull pickups (cut).
  - The cloak shimmer is faint by design; whether it is too faint or too easy to see in motion is not known. The own
    cloaked ship is a light blue ghost that covers the view ahead as much as the normal hull does.
  - Warp, cloak and tractor sounds are existing waves at other pitches; nobody has heard them.
  - A client that joins gets its type on its FIRST ship only if the choice arrives before the spawn shield ends (it did
    in every run here); otherwise from its second ship.
  - Bot matches of this session ran as a standalone game without network instead of a dedicated server.
- The "Sci-Fi Space Station" model in `SourceAssets/SciFiSpaceStation` is not used (see item 14).
- Item 24 (bigger ships, enemy glow, station model, intro, portals, planets) was judged from logs and still pictures.
  - The station model fits only half of the collision shape: the beam along Y, the mast and their panels are still
    drawn as boxes through it; its windows hardly glow.
  - Cloaked ships and the new glow were checked by reading the code, not in a run.
  - The intro was never skipped with a real key, mouse button or gamepad, and its sound track is not used.
  - A portal that leads to a red or pink sector has a landmark glint close to the colour of an enemy's.
  - The portal passage flash and the drift of the planets' bands were not caught in a picture.
  - Ship size and hit sphere were balanced with easy bots only (three matches each).
- The Moon disc was not confirmed in a screenshot (planets and the black hole were).
- Nobody has listened to the sound: levels, the choice of each effect and the tempo assumed for the music stems
  are unverified by ear.
- Nobody has used the menus with a mouse and keyboard; they were driven by console commands in unattended runs.
- Nobody has read the hints while playing: whether they come at the right moment, stay long enough or get in the
  way is untested. They were only checked on a listen host, not on a client that joined a server.
- Weapons and turrets are balanced from logged bot matches only, not by play testing.
- Shield facings (item 21) were tuned from bot matches and looked at in still pictures only. Nobody has flown with
  them, seen the bubble in motion or heard the collapse chime. Bots mostly meet head-on, so their matches say
  little about a player who manoeuvres for a weak side.
- Photon torpedoes (item 22) were tuned from bot matches on the easy level only and looked at in still pictures
  taken in slow motion. Nobody has fired or dodged one by hand or heard their sounds. Normal and hard bots were not
  measured. A torpedo cannot be shot down. Station, wormhole and portal labels can still be drawn over a HUD panel
  (enemy and pad markers no longer are). The torpedo trail keeps the pack's colour.
- Phasers, destroyable asteroids, shockwaves, debris, flares and the Lyria music (item 23) were tuned from bot matches
  and looked at in three still pictures taken in slow motion. Nobody has played with them or heard anything.
  - The choice of base weapon (B: phaser / blaster) is forgotten when the ship is destroyed: a new ship has the phaser.
  - Bots do not shoot at rocks on purpose; how fast a player can clear a field was not measured.
  - The push of a shockwave is not predicted on the owning client: it arrives as a movement correction of a metre or two.
  - The break effect of a rock was never seen on a client (state and checksums were compared; item 23, netcode).
  - The music loop seam ran only with an overridden loop point; no run reached the real one at 157 s. On the title
    menu the explore track is chosen for the first frames before the menu track.
  - The "hit rate" of the `Balance` status line is no longer comparable with earlier sessions (item 23, balance).
  - Flying rock chunks are not tinted like the rocks of their sector. Station, wormhole and portal labels can still
    be drawn over a HUD panel (as before).
  - The Lyria tracks are AI-generated; which terms of the Gemini / Lyria service apply to publishing them was not
    looked up (`docs/ASSETS.md`).
- A client that holds boost until the meter is empty gets a group of 5 to 8 small movement corrections each time
  the meter unlocks, against a listen host on the same PC (item 21, netcode); against a dedicated server item 17
  measured about one. Not investigated.
- The frame after a screenshot request is long enough to swallow a 0.4 s effect: pictures of short effects need
  the effect slowed down (item 21, pictures).
- No human played a multi-player session; all network checks are two instances on one machine or bots.
- Unused content of the two Fab packs is still in `Content/` (about 2.2 GB on disk; not cooked).
- Epic Online Services: sign-in, lobby create / update / search / destroy, stat ingest and both leaderboard
  queries ran against the live service from the packaged build (item 20), but only with two instances on one PC,
  which are one EOS user. Not tested: two different users, the online menu pages by eye, sign-in renewal.
- No EOS P2P / NAT traversal: internet play needs a forwarded port and a public address typed by the host.
- Stats are reported by each client for itself and are not checked by the server.
