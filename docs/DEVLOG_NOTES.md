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

## 15 Nov 2026 (twelfth session) - bot tactics, playthrough tests, quick play
27. "Add more playthrough tests, improve the bots' AI, add create server on the Epic games, also join to it if it is
    available", then "yes, quick play". Three parts in one session. No ship number, weapon number, layout rule or
    replicated property changed (`ProjectVersion` stays 0.2.0; layout checksum of seed 1234 still 7C0CB60C).
    - **A. Bot tactics** (`AHCJBotController`; counters: `AHCJGameMode::NotifyBotTactic`, the `Tactics t=` log line).
      One new number in the skill row carries the difficulty: `FHCJBotSkill::Tactics` = 0.25 / 0.7 / 1 (easy / normal /
      hard) - the chance, rolled once per ship, that it is flown "smart" at all, and rolled again for single moves - and
      `ComboChance` 0.08 / 0.35 / 0.7. So three of four easy ships fly as they did.
      - Target (`PickTarget`): every hostile in sight gets a score - nearer is better (1 per 10 m), hull lost (x 5), the
        facing it shows us weak (+2) or down (+4), it is the ship that hit us in the last 2.5 s (+5, and +2.5 more
        when it sits on a side of ours that is weak), its tail towards us (+1.5), every other bot already on it (-2.5),
        under its spawn shield (-8), the target we have (+3, so that it does not flip). A ship that is not smart keeps
        the old rule (nearest, switch at 60 % of the distance).
      - Breaking off (`ThinkDisengage`, `TickDisengage`). Decision after the first batch: a bot breaks off only when
        it has a way out. The first version let light hulls run as soon as the facing towards the attacker was down
        and the hull touched ("hit and run"); the pursuer has the same speed and the same range, so the runner just
        stopped shooting and died - interceptor and scout lost K/D on hard (scout 0.64). Now: the facing towards the
        attacker is down AND the hull is below the retreat line of the difficulty (45 / 30 / 20 %, +5 for the
        interceptor, +12 for the scout) AND one of: a rock of at least 3.5 m within 50 m to get behind
        (`AHCJUniverse::FindCoverPoint`), the cloak ready, a repair or shield pad in reach. Then up to 7 s away under
        boost, fire held, the best facing turned to the attacker; back when the shields are above 55 % and nothing
        hit for 2.5 s. The warp drive is tried on top (as before) but does not count as a way out: a hit stops its
        charge.
      - Shield facings in a duel: **not** turned while shooting, on purpose. A facing holds 22-34 points, a phaser
        does about 47 a second: turning the nose 50 degrees away and back costs about a second of the bot's own fire
        (about 45 damage not dealt) to save at most one facing (26). The facing turn therefore stays where the bot is
        not shooting anyway - retreating, breaking off, travelling - and those cases became more frequent (table).
        What a smart bot does instead when it is shot from the side or from behind: it turns on that ship (the +5).
      - Cloak: a cloak takes all four facings down (item 25). The first version sent the scout into every fight under
        cloak - it arrived without shields (scout K/D on hard 0.64). Now the type's readiness (scout x 3) applies to
        getting away only, where the shields on that side are gone anyway and the cloak breaks every lock; an
        approach under cloak is as rare as before, and a scout that makes one flies to the tail of its target and
        drops the cloak at 26 m.
      - Torpedoes: through a facing that is DOWN at once (`ComboChance` per decision, on top of the old rule); a
        smart bot holds a target in beam reach with the tractor first (a held ship flies at 0.55 of its speed).
      - Ship types (`FHCJBotStyle`, a table in the .cpp for smart bots; the ship table is untouched): interceptor,
        fighter and scout make boosted passes (1.8 s at 15 m, every 12-18 s) around a hull that turns slower than
        they do; gunship keeps 30 % more range and is twice as ready with torpedoes; cruiser 1.8 x with the tractor
        (3 x in the first version - the beam feeds on the phaser bank and the cruiser's K/D fell) and tractors a fast
        ship that is close; scout: damaged targets first, no fair fight with a heavy hull whose facing is whole.
      - Surroundings: a rock of at most 140 hit points within 45 m on the line to a hidden target is shot with the
        phaser (up to 3.2 s; the target is kept 2.5 s longer meanwhile); the reach of a station's live turrets (94 m
        from its centre) is kept out of unless the bot attacks the turrets; a hostile far out of range is reached
        at warp.
      - **Measured**, same method before and after (standalone, headless, 8 bots of mixed types, seed 1234, no limits,
        `-HCJBotsOnly -HCJNoEOS -HCJNoIntro`, 8 processes at once: 3 easy, 3 normal, 2 hard matches of about 410 s).
        "Before" is the build of item 26, run at the start of this session; "after" the final code (fifth batch).

        | | easy before | easy after | normal before | normal after | hard before | hard after |
        |---|---|---|---|---|---|---|
        | kills per minute | 14.5 | 14.1 | 18.2 | 17.6 | 19.3 | 18.4 |
        | time to kill, s | 4.31 | 4.14 | 2.68 | 2.53 | 2.44 | 2.32 |
        | kills by phaser / torpedo, % | 21 / 24 | 24 / 23 | 39 / 11 | 33 / 15 | 50 / 4 | 39 / 14 |
        | by missiles / rail / pulse / scatter / blaster, % | 21 / 13 / 9 / 6 / 7 | 14 / 8 / 8 / 15 / 8 | 16 / 12 / 7 / 10 / 5 | 11 / 15 / 8 / 9 / 9 | 6 / 13 / 17 / 4 / 6 | 11 / 11 / 10 / 7 / 7 |
        | K/D interceptor | 0.87 | 1.24 | 1.05 | 1.35 | 1.12 | 1.08 |
        | K/D fighter | 1.02 | 0.93 | 0.92 | 0.88 | 1.00 | 1.24 |
        | K/D gunship | 0.78 | 0.81 | 0.80 | 0.83 | 0.89 | 0.89 |
        | K/D scout | 0.80 | 0.91 | 0.92 | 0.95 | 0.93 | 1.22 |
        | K/D cruiser | 1.25 | 0.89 | 0.97 | 0.84 | 0.89 | 0.74 |
        | phaser beam on a ship, % | 56 | 56 | 85 | 83 | 89 | 90 |
        | per 10 min: warps / cloaks / tractor holds | 46 / 17 / 37 | 48 / 12 / 56 | 102 / 28 / 79 | 99 / 43 / 144 | 139 / 59 / 102 | 148 / 67 / 217 |
        | torpedoes fired / hit a ship / on an open facing | 109 / 76 / 56 | 137 / 94 / 75 | 192 / 69 / 42 | 218 / 83 / 58 | 269 / 29 / 19 | 279 / 78 / 57 |
        | facing turned to the attacker | 60 | 97 | 63 | 159 | 51 | 179 |
        | deaths by station turrets | 9 | 4 | 9 | 4 | 7 | 4 |
        | rocks destroyed by bots | 2 | 3 | 1 | 2 | 3 | 5 |
        | new: retreats / back in the fight / cover reached | - | 75 / 9 / 1 | - | 117 / 16 / 10 | - | 152 / 14 / 8 |
        | new: torpedoes at an open facing / kills by them | - | 118 / 30 | - | 180 / 24 | - | 212 / 24 |
        | new: tractor then torpedo | - | 7 | - | 67 | - | 143 |
        | new: turned on an attacker / less crowded target | - | 3 / 0 | - | 3 / 0 | - | 5 / 2 |
        | new: close passes / cloak ambushes / warp chases | - | 22 / 0 / 0 | - | 73 / 2 / 19 | - | 101 / 7 / 30 |
        | new: rocks shot out of the way / steered off a station | - | 1 / 20 | - | 2 / 53 | - | 8 / 89 |

        ("retreats" counts the old run for a repair pad as well; "back in the fight" only the new breaking off.)
        Five batches were run; the code changed a little between them (cloak rule, way-out rule, pass rhythm). Types
        pooled over the last four (12 easy, 12 normal, 8 hard matches; 3560 kills) - K/D against the mean of the five:

        | | interceptor | fighter | gunship | scout | cruiser |
        |---|---|---|---|---|---|
        | easy | 1.19 (+26 %) | 0.90 (-4 %) | 0.72 (-24 %) | 0.97 (+4 %) | 0.91 (-3 %) |
        | normal | 1.33 (+36 %) | 0.88 (-10 %) | 0.82 (-16 %) | 1.08 (+11 %) | 0.78 (-20 %) |
        | hard | 1.09 (+9 %) | 0.95 (-4 %) | 0.99 (0 %) | 1.04 (+4 %) | 0.90 (-9 %) |

      - Read. (1) Easy is as lethal as it was: 14.1 against 14.5 kills a minute, 4.14 against 4.31 s (matches of one
        batch: 3.5-4.7 s); beam on target 56 % as before, on purpose. (2) Torpedoes on hard: 4 % -> 14 % of the kills,
        29 -> 78 hits per ten minutes; the bots still dodge most of them (197 of 279 lost their target). (3) The scout,
        30-40 % below the others on normal and hard in item 25, is at +11 % / +4 % over twelve / eight matches. Today's
        "before" did not show that gap (0.92, 0.93) - one bot type has about 150 kills and deaths per batch, and its
        K/D moves by a quarter between batches of the same code (interceptor on normal in the four batches: 1.09,
        1.78, 1.23, 1.35). (4) Hard is inside 10 % for every type. On normal and easy the interceptor is now clearly
        the best and the two heavy hulls the worst: the fast ship gains most from choosing its fights. Halving the
        passes did not change it; not tuned further. (5) Kill rate on hard a little lower (bots that live longer).
      - Still weak: fights of two are still nose to nose (both aim at each other; the passes are the exception); rocks
        are shot at once or twice a match; "turned on an attacker" and "less crowded target" are rare because a
        sector seldom holds more than two or three ships; cover is reached about once a minute on normal and hard.
        Nobody has played against these bots.
    - **B. Playthrough tests.** Three layers on one set of facts.
      - The server writes structured lines: `Playthrough: event=match_start | match_end | board | stats | human`, and
        warnings `stuck` (a bot's ship moved less than 2.5 m in 20 s; with what the bot was doing), `out_of_bounds`
        (alive beyond the kill radius for 3 s) and `nan`. `stats` carries every counter of the match (kills by weapon and
        by ship type, jumps, pickups by item, turrets, rocks with the destroyed-set checksum, systems, tactics, server
        frame times) every 15 s and at the end. Clients write `event=client` every 10 s (layout, destroyed set, own
        score, movement corrections).
      - In the automation suite (`HypeCubeJam.Playthrough.*`, `Private/Tests/HCJPlaythroughTests.cpp`): the engine's
        `FTestWorldWrapper` with the game's URL options - the real game mode, universe, ships and bot controllers in a
        world ticked by hand at 30 frames per game second. `MatchToFragLimitAndNext`: six hard bots, limit 4 - ended
        after 74 s of game with a winner at 4 kills and a board of six, match 2 on seed -490612130 with layout
        5671F10D (match 1: 7C0CB60C), 3106 frames in 13.7 s. `OpenMatchKeepsMoving`: eight normal bots, 180 s - 53
        kills, 45 portal jumps, 53 pickups, all five types flew, no ship outside its sector or stuck, 5400 frames in
        19.1 s. The online subsystem is switched off while automation tests run (a test world has a game instance).
      - As processes: `docs/tools/playthrough.py` (see README, "Playthrough runner"): seven scenarios and one sum over
        all of them, a pass / fail table, exit code 1 on a failure. Headless by default (`-nullrhi`, `-HCJNoEOS`),
        ports 7791 upwards, four scenarios at a time.
      - **What the tests found** (all fixed unless said):
        1. *Bots standing still at the edge of a station's reach.* The new "keep out of a station's reach" removed the
           inward part of a bot's wanted velocity; a bot whose portal, wormhole mouth, pad or wander point lay inside or
           straight behind the reach stood at the edge - `stuck` events in three of eight matches of the fourth batch
           (an interceptor 20 s in front of a wormhole mouth, a cruiser in front of a portal, a scout going back and
           forth). Fixed in `SteerTowards`: a goal that lies under the guns is flown to; a fight holds the edge only
           while the target can be hit from there; otherwise the bot goes round along the edge. No `stuck` event in
           the 8 matches of the fifth batch and in the suite runs after it.
        2. *Three players called "User".* Host, client and late joiner all had the default pilot name: three rows
           "User" on the board, "User destroyed User" in the kill feed. `AHCJGameMode::ChangeName` now makes names
           unique in a match (`User-2`, `User-3`). The `?Name=` option of a join URL is not what names a player (the
           pilot name of the user settings is, sent after the join) - the runner sets it with an `-ini:` override.
        3. *A world that is not a client says it is one.* Between "leave" and the next join the client's own local
           world reported `NM_Client` for a moment (a connection was pending) and one `event=client` line carried the
           local universe's checksum. The line - and Quick Play's "the join worked" - now also require a controller
           without authority; otherwise a join that fails after that moment would have been taken for a success.
        4. *Two bots nose to nose for 21 s* (second batch, easy, 1.7 m apart, both attacking, speed about 10 uu/s).
           Seen once, before the stuck line said what a bot is doing; not seen again in 24 matches and the suite
           runs. Not explained. It may have been case 1 (both at a station's edge); the watch stays in.
        5. Of the runner itself: its NaN pattern matched the key `nan=0`; an easy match of 230 s need not contain a
           cloak (asked of normal and hard only); a join is logged before the pilot name arrives (remote joins are
           counted instead); headless there is no title menu (`FApp::CanEverRender`), so that check is made in
           rendered runs only.
      - **The suite**, last full run (editor binary, headless, four scenarios at a time, 518 s; exit code 0). The
        first run passed 1 of 8 (findings 1-3 and 5 above), the second 7 of 8.

        | scenario | result | checks | what it saw |
        |---|---|---|---|
        | `match_cycle` | pass | 21 | match 1 won with 5 kills after 33 s, board of 8; layouts 7C0CB60C -> 5671F10D |
        | `difficulty_easy` | pass | 29 | 52 kills, 59 portal / 11 wormhole jumps, 68 pickups; every type killed and died |
        | `difficulty_normal` | pass | 30 | 76 kills, 49 / 6 jumps, 54 pickups |
        | `difficulty_hard` | pass | 30 | 62 kills, 73 / 10 jumps, 61 pickups |
        | `small_universe` | pass | 13 | 92 kills on four sectors; 107 turret shots, 3 turrets destroyed |
        | `net_host_client_late` | pass | 49 | client 2 kills / 4 deaths, 8 corrections in 180 s (4.4 per 100 s); late joiner 1 / 1, 3 in 110 s (2.7 per 100 s) and one rock that was already gone; host saw 3 remote joins, the client leave and come back |
        | `autopilot_hangar_match` | pass | 17 | cruiser chosen, PLAY, three matches ended by the limit of 4; the player 3 kills / 6 deaths |
        | `all_matches_together` | pass | 20 | kills: phaser 134, torpedo 82, missiles 69, blaster 43, pulse 33, rail 31, scatter 29; turrets shot 8 ships down, 11 turrets destroyed; 8 rocks destroyed |

        Server frames in that run: mean 1.4-2.1 ms, longest 127 ms. No `stuck`, `out_of_bounds` or `nan` event, no
        error line.
      - With the packaged build: `autopilot_hangar_match` rendered (`--binary packaged --render`) passed 18 checks -
        there the player starts in the title menu, and won one of its matches. `net_host_client_late` headless (`--binary packaged`) passed 49 checks on its second run (client 1 kill / 2 deaths, 3.7 corrections per 100 s; late joiner 0 / 4, 8.2 per 100 s, two rocks already gone); on the first the autopilot client had 4 deaths and no kill, which the test then demanded - it now asks for one kill between client and late joiner.
        Rendered with three windows it does not run on this card: the client's device was removed (`DXGI_ERROR_DEVICE_REMOVED`,
        video memory budget 118 MB, 770 MB used) when the third instance came up; until then its checks held.
      - Movement corrections of the joined clients in all runs (autopilot with aim, boost every other 8 s): 1.8-8.9
        per 100 s headless, 15-27 per 100 s in the rendered run that ran out of video memory; the limit of the test
        is 30.
      - The runner puts the user's ship choice back after the hangar scenario (PLAY saves the settings).
    - **C. Quick play** (`FHCJQuickPlay` in `Online/HCJEosLogic.h`, `AHCJPlayerController::MenuQuickPlay` /
      `UpdateQuickPlay`, menu page `QuickPlay`; README, "Online").
      - QUICK PLAY is the first button of the title menu; PLAY (host with bots) stays the Enter key - Enter must not
        take a player into a stranger's match by surprise. `-HCJQuickPlay` presses it once the title menu is up.
      - Steps and texts: "Signing in to Epic Online Services..." (15 s at most) -> "Searching for open matches..."
        (10 s at most) -> "Joining <host> (<n>/<max> players)..." (the ordinary IP join, 15 s at most, a second host
        is tried) -> or "No open match found - hosting one" / "Could not join <host> - hosting a match instead" /
        "Search failed (...) - hosting a match instead" for 2 s, then PLAY with the bots of the menu, advertised as a
        lobby. No service: "Online services unavailable (<why>) - starting a local match against bots". The hangar
        choice applies as for PLAY and JOIN (it is sent by every controller at its start).
      - "Best" lobby: most players, then most free slots. Not by ping: there is no connection to a host before the
        join and the lobby carries no region; said so in the README.
      - Lobby hygiene: one more attribute, `HCJTIME` (seconds since 1970 when the host last wrote the lobby; written
        again every 2 minutes). `HCJEos::FilterJoinableLobbies` leaves out other network versions, full matches,
        lobbies older than 10 minutes and lobbies without a usable address - for FIND GAMES (which used to list other
        versions and full matches greyed out) and for Quick Play; the status line says how many and why. A lobby
        without the attribute (a 0.2.0 build of before today) is never called stale. Player count and bots follow
        joins and leaves as before; the lobby is destroyed on leaving to the menu and at quit.
      - Problem (second live run): the second instance started Quick Play, froze for 16 s in its first frames (two
        instances compiling shaders on one GTX 950: `GPU timeout: a payload ... has not completed`), and the sign-in
        wait - 8 s of wall clock then - was over when the next frame came, 0.6 s before the sign-in arrived: "no
        answer from Epic Online Services - starting a local match". Now the time of a phase is the sum of its frames,
        each counted as 0.25 s at most (tested), and the sign-in wait is 15 s.
      - Problem (first live run): the playthrough suite ran at the same time; the second instance needed 96 s to
        start and came up as the first one quit. Live tests are run alone.
      - **EOS P2P / relay: not built; what was looked at.** The engine ships `Plugins/Online/SocketSubsystemEOS`
        (`FSocketSubsystemEOS(IEOSPlatformHandlePtr, ISocketSubsystemEOSUtilsPtr)`, `UNetDriverEOS`, connect URL
        `EOS:<product user id>:<socket>`): the glue to the game's own SDK login is one small utils object, less than
        item 20 assumed. Two things stopped it. (1) A host listens on ONE game net driver: `UNetDriverEOS::InitListen`
        is either an EOS P2P listener or (`?bUseIPSockets`) the plain IP one, not both - a host reachable through the
        relay would no longer take "join by address" and LAN joins, i.e. the working path would change, not gain a
        fallback. (2) It cannot be verified here: both instances on this PC are the same product user. Probe (a
        temporary development switch, removed again): each of two running instances sent 20 packets to its own
        product user id on one socket name through `EOS_P2P_SendPacket` - every packet came back to the process that
        sent it (20 own, 0 from the other process, one connection request each, from itself). P2P between two
        processes of one user stays inside each process. The condition for keeping it - a packaged host and client
        on this PC that connect and play through it - cannot be met with one Windows user. Internet play still needs
        the forwarded port and `-HCJPublicAddress`.
      - **Live test** (two instances of the package on this PC = one EOS user, `-HCJNoStats`, ports 7801 / 7802, a
        test bucket `-HCJEosBucket=HCJ_TEST_<time>` so that the run neither finds real matches nor shows in the
        public list; third run, evidence is the two logs):
        - A, 05:38:33 UTC: `Quick play: Signing in...`, signed in after 0.9 s, `Searching for open matches...`,
          `EOS: lobby search: EOS_Success, 0 listed`, `Quick play: No open match found - hosting one`, 2 s later
          `hosting a match (advertised through Epic Online Services)`, `EOS: lobby <id> created`, `lobby advertised:
          'User' 1/16 players, 7 bots, 4D - 16 sectors, port 7801`.
        - B, started when A's lobby was advertised: signed in after 1.1 s, `lobby search: EOS_Success, 1 listed`,
          `lobby listed: 'User' 1/16 players, 7 bots, ..., port 7801, version FFB5210D`, `Quick play: Joining User
          (1/16 players)...`, `connecting to 127.0.0.1:7801`, `Welcomed by server` 70 ms later, `Universe built
          (client) ... layout checksum B3951D41` (A's match: B3951D41), `Quick play: joined the match of 'User'`.
          A: `Player joined ... (remote) -> 2 humans, 6 bots`, `EOS: lobby updated: 'User' 2/16 players, 6 bots`.
        - B left after 58 s (`Menu: leaving the server`). A: `Player left: User-2`, `lobby updated: 'User' 1/16
          players, 7 bots` 0.6 s later.
        - A quit at 05:41:03: `EOS: lobby <id> removed (EOS_Success)`, `shut down after 0.18 s, 0 request(s)
          unanswered`.
        - B, Quick Play again at 05:41:25: `lobby search: EOS_Success, 0 listed`, `No open match found - hosting one`,
          `lobby <id> created`, `lobby advertised: ... port 7802`; at its quit `lobby <id> removed (EOS_Success)`.
        - Each quit logged `EOS: stats not sent (-HCJNoStats): ...`; nothing was ingested. The first search of a
          process took 3.2 s, later ones 0.2-0.3 s. One error line per log (the known CelestialVault message). The
          client secret is in none of the nine logs of the three runs (compared in a script without printing it).
        - First run (suite running alongside): A as above; B came up as A quit, no answer to its sign-in for 8 s
          (frozen frames), local match; the sign-in arrived later and its match was advertised after all. Second run:
          the 16 s freeze described above. Both ended in a playable match - the fallbacks work - but not in the join.
        - Not tested: two different EOS users (two PCs); a join that fails with a second lobby to try and the
          cancel after 15 s against a real host (unit-tested only); the real bucket `HCJ_FFA`; the menu by mouse.
    - **Frame rate** (packaged, 1280x720, GTX 950, listen host with 7 bots, seed 1234, capture autopilot, `-HCJNoEOS
      -HCJNoIntro`, 8 s averages over 92 s; the package of item 26 - a copy made before the first change - and the new
      one in turn): before 108.1 and 104.7 fps (intervals 94-117), after 106.1 and 107.8 fps (96-121); the four runs had 20-26 kills.
      Nothing new is drawn in a match and the bot code runs on the server's game thread: the two packages are in the same range. (Both are below the 118-122 fps of item 26's table: the machine had run builds and bot matches for two hours; only the alternated runs compare.)
    - **Verification**: 71 automation tests, all passing (new: `HypeCubeJam.Playthrough.MatchToFragLimitAndNext`,
      `.OpenMatchKeepsMoving`, `HypeCubeJam.Online.LobbyFilter`, `.QuickPlay`); the playthrough suite 8 of 8 (above);
      layout checksum of seed 1234 unchanged (7C0CB60C; stations, wormholes, pads and black holes are derived from it and
      were not touched). No error line in any bot match or suite log but the known CelestialVault message of the
      packaged build.
    - Development aids added: `-HCJQuickPlay`, `-HCJNoStats`, `-HCJEosBucket=<name>`, `-HCJEosDemoPort=<port>`,
      `-HCJAutoAim`, `HCJMenuCmd quickplay | cancelquickplay`, the `Tactics t=` and `Playthrough:` log lines,
      `docs/tools/playthrough.py`.
    - Packaged: 1080 packages in the cook (1051 cooked, 29 skipped for the platform; as before - no content was added). Zip `Packaged/mariuz_HypeCubeJam.zip`: 584.9 MB (883.7 MB unpacked, 48 files; 584.8 MB / 48 files before).
    - Pictures (packaged build, 1280x720, taken by the capture switch, one a second):
      `Media/100_title_menu_quick_play.png` (signed in to the live service), `101_quick_play_searching.png`,
      `102_quick_play_no_match_hosting_one.png` (live service, an empty test bucket), `103_quick_play_no_online_service_local_match.png`
      (`-HCJNoEOS`), `104_find_games_live_lobby.png` (a second instance hosting in the same test bucket),
      `105_quick_play_joining_demo_row.png`, `106_quick_play_join_failed_next_host_demo_rows.png`,
      `107_quick_play_join_failed_hosting_instead_demo_rows.png` (**made-up rows of `-HCJEosDemo`** on a port where
      nobody listens: the join to the first row ran out after 15 s, the second likewise, then a match was hosted - the
      fallback chain in a real run). The "Joining ..." line of the live join lasted 0.4 s and is in no picture.
    - Judged from logs, bot matches, the test runs and still pictures. Nobody has played against the new bots, clicked
      QUICK PLAY with a mouse, or joined a match of another PC with it.

## 15 Nov 2026 (thirteenth session) - web build (WebAssembly + WebGPU)
28. "Build a web version". The engine's Wasm platform (`Engine/Platforms/Wasm`, UE 5.9 Preview 1; its `Docs/` and the
    `Skills/web-deploy` skill were followed) with Emscripten 6.0.10 from `F:\work\tools\emsdk`
    (`WASM_EMSDK_ROOT` set per command, nothing system-wide). The game runs in Chrome: title menu, hangar,
    settings, a match against bots, both renderer tiers. Nothing was uploaded.
    - **What the web build is**: single-player against bots. A browser cannot host and has no online subsystem, so
      on Wasm PLAY ("a match against bots") is the primary button and QUICK PLAY, the join-by-address row, FIND GAMES
      and LEADERBOARDS (title and pause menu) are not built into the menu; two lines say "Online play ... is in the
      Windows build". The match is a standalone match (the engine drops `?listen` on Wasm; `LocalMapOptions=` is empty
      there). The intro movie is skipped (no media player plugin on the web) and not staged.
    - **Changes, all behind the platform** (`PLATFORM_WASM` is the engine's define, `HAL/Platform.h`):
      - `HypeCubeJam.Build.cs`: `HCJ_WITH_EOS` = 0 on Wasm, 1 elsewhere; `EOSShared`, `EOSSDK`, `Sockets` only with it.
      - `Private/Online/HCJEOSSubsystem.cpp`: the whole file inside `#if HCJ_WITH_EOS` (not a line changed otherwise);
        `Private/Online/HCJEOSSubsystemWeb.cpp` (new, `#if !HCJ_WITH_EOS`): the same class as a stub that is always
        `Disabled` and reads no config.
      - `Private/UI/HCJMenu.cpp`: `#if PLATFORM_WASM` blocks (above). `Private/Game/HCJPlayerController.cpp`:
        `TickIntro` skips the intro on Wasm.
      - `HypeCubeJam.uproject`: `EOSShared` and `WmfMedia` get `"PlatformDenyList": ["Wasm"]`.
      - `Config/Wasm/WasmEngine.ini` (page title, `StartMode=Click` - the click a browser needs for sound and pointer
        lock -, `bInjectIsolationHeaders=True` for GitHub Pages, both mobile tiers, no `?listen`),
        `Config/Wasm/WasmGame.ini` (`IniSectionDenylist=HypeCubeJam.EOS` and the five key names, `bSkipMovies=True`
        plus `!DirectoriesToAlwaysStageAsNonUFS=ClearArray` - both are needed, UAT stages `Content/Movies` by itself),
        `Config/Wasm/WasmDeviceProfiles.ini` (texture caps, below). None of the three is read on Windows.
      - `.gitignore`: `PackagedWeb/`. Tools: `docs/tools/web_fixtures.json` (harness fixtures), `web_secret_scan.py`,
        `web_split_for_pages.py`, `web_plain_server.py`.
      - Windows after the changes: `HypeCubeJamEditor Win64 Development` built (up to date), 71 of 71 automation
        tests pass (exit code 0). The Windows package and `Packaged/` were not touched.
    - **Commands** (engine root `F:\EpicGames\UE_5.9`, `set WASM_EMSDK_ROOT=F:\work\tools\emsdk` first):
      - SDK: `RunUAT.bat Turnkey -command=VerifySdk -platform=Wasm` -> `Status=Valid, Current_Sdk=6.0.10`.
      - Package: `RunUAT.bat BuildCookRun -project=F:\work\HypeCubeJam\HypeCubeJam.uproject -noP4 -platform=Wasm
        -clientconfig=Shipping -build -cook -stage -pak -iostore -package -archive
        -archivedirectory=F:\work\HypeCubeJam\PackagedWeb\Ship -unattended -utf8output -nocompileeditor
        -AdditionalCookerOptions="-unattended -nopause -nosplash -nullrhi"` (`-clientconfig=Development` and
        `PackagedWeb\Dev` for the test stage: its page takes `?args=` and console commands). Delete the archive folder
        first: files of an earlier stage stay in it otherwise. (Item 43: no `-IgnoreCookErrors` needed with
        `AllToolsets` off; if it is used, run `docs\tools\cook_error_guard.py` on the log right after - see there.)
      - Check: `python Engine\Platforms\Wasm\Build\Tools\deploy\check_stage.py PackagedWeb\Ship\Wasm` (all passed;
        release scan 0 findings), `python docs\tools\web_secret_scan.py PackagedWeb\Ship\Wasm
        Engine\Binaries\Win64\UnrealPak.exe <scratch folder>`.
      - Run: `set PROBE_CHROME=C:\Program Files (x86)\Google\Chrome\Application\chrome.exe`, then
        `python Engine\Platforms\Wasm\Build\Tools\web\webrun.py run --stage PackagedWeb\Dev\Wasm --fixtures
        docs\tools\web_fixtures.json --fixture hcj_tour --port 8080 --out <dir>` (Development: `hcj_tour`,
        `hcj_scores`; Shipping: `hcj_click_play`, `hcj_click_soak`, which click instead of running console commands;
        `--query tier=forward` for the other tier). By hand: `python Engine\Platforms\Wasm\Build\Tools\web\serve.py
        --root PackagedWeb\Ship\Wasm --port 8080`, then `http://127.0.0.1:8080/index.html`.
      - Hosting copies: `python docs\tools\web_split_for_pages.py PackagedWeb\Ship\Wasm PackagedWeb\GitHubPages` and
        `... PackagedWeb\itch --limit-mb 200`, then the zip of the `itch` folder.
    - **Times** (8 cores, while two other jobs used the machine): first Development BuildCookRun 17 min 8 s (compile
      and link of the game module against the engine's prebuilt web libraries 211 s; the cook of 1080 packages 13 min -
      every material was compiled to WGSL for both tiers, 337 shader maps, no failure). Shipping BuildCookRun 28 min
      47 s: the link 19 min (wasm-opt alone about 15 min, 241 MB -> 142 MB), the cook 3 min (shaders from the cache),
      Brotli quality 11 of the staged files 6.5 min. A restage without cook 7 min.
    - **In the browser** (Chrome 157, GTX 950, 1920x1080 off-screen window, GPU shared with other running copies of
      the game - no conclusion about speed): compatibility check passed; READY after 4.4-5.7 s on the Shipping stages
      (local server, files from disk); the deferred tier (`SP_WEBGPU_ES3_1_DEFERRED`) is picked by default, the forward
      tier (`SP_WEBGPU_ES3_1`) runs with `?tier=forward`; 0 WebGPU errors, no `UE_RUN ERROR`, no missing package, no
      stub shader in any run; 62-66 pipelines created. Frame rate seen (frames presented per second, Development
      stage): 51 fps in a match on the deferred tier, 55 on the forward tier, 31-39 in the title menu. Sound: the
      WebAudio context runs at 48 kHz and frames are played all through (75 underruns at the start of the first run);
      waves are cooked as RAD Audio (the web default; the MP3-imported Lyria tracks and the Lyra stems alike, 7.5 MB
      together - both sets were kept).
      Every material was compiled for both tiers (no cook error, no stub shader at run time). The pictures of both
      tiers show ships and their paint, rocks of three themes, nebula and stars, portals, a wormhole, pickup pads, a
      phaser beam, explosions, the shield ring, the HUD and the menus - nothing black or missing in them. Not compared
      side by side with Windows: how much the second directional light adds on the forward tier, the decorative
      pebbles of `M_HCJ_DecorRock` (world position offset), torpedoes, the station and its turrets, black holes, the
      cloak and warp effects (none of them was looked for in a picture).
      Bots fight: after 80 s the scoreboard showed 26 kills among seven bots (`Media/115`).
      Stability: the Shipping stage (deferred tier) ran a match for 10 minutes without an error line - the first match
      ended at the frag limit and the next one started (`Media/119`, `120`); the wasm heap stayed at 512 MiB over 64
      samples, the JavaScript heap went from 17 to 25 MB. The ship of the player was not flown in it.
    - **Size**. First cook: container (`.ucas`) 208.7 MB, of which 104 MB the 4K maps of the hover-vehicle pack (the
      turret parts, and barrels / lamp poles that an old `DirectoriesToAlwaysCook` line still pulls in). With
      `WasmDeviceProfiles.ini` (every world / vehicle / weapon / character / effects / cinematic / HLOD texture group
      at most 1024, sky 2048; the cook drops the larger mips) 99.3 MB. The movie (9.5 MB) is not staged. Shipping
      wasm 142.3 MB (26.5 MB as Brotli).
      Final stage `PackagedWeb\Ship\Wasm`: 56 files, 286.5 MB with the `.br` copies; without them 33 files, 261.4 MB.
      Download of a first visit: **forward tier 252 MB, deferred tier 261 MB** on a host that sends the files as they
      are (GitHub Pages, itch.io); **134 MB / 138 MB** on a host that serves the `.br` copies (nginx / Apache / IIS
      with the engine's templates; measured by the harness: 133.8 MB before READY on the forward tier). Largest files:
      `HypeCubeJam-Wasm-Shipping.wasm` 142.3 MB, `HypeCubeJam-Wasm.ucas` 99.3 MB, `HypeCubeJam-Wasm.pak` 9.2 MB,
      the deferred tier's shader libraries 5.6 + 2.9 MB.
    - **Hosts**.
      - GitHub Pages (100 MB a file, about 1 GB a site, no headers): `PackagedWeb\GitHubPages` - 36 files, 261.4 MB,
        the wasm in three parts of at most 50 MB (the page's own `parts` format in `wasm-cache.json`, joined by its
        service worker), the container 99.3 MB as it is (it is read in byte ranges and cannot be cut; **0.7 MB under
        100 000 000 bytes** - more content needs another cut or a second pak chunk), no `.br`, a `.nojekyll` file.
        Tested with `docs/tools/web_plain_server.py` (no isolation headers, byte ranges): the page logged "PAGE
        ISOLATION reload under the service worker", passed its check after the reload, joined the wasm and ran a match.
      - itch.io HTML5 (500 MB extracted, 200 MB a file, 1000 files): `PackagedWeb\mariuz_HypeCubeJam_web.zip` -
        154.5 MB, 33 files, 261.4 MB extracted, largest file 142.3 MB, `index.html` at the root. The same folder
        (`PackagedWeb\itch`) ran from the engine's server. Not tested on itch.io itself.
    - **The EOS secret**: the web build has no EOS code and the section is not staged. `web_secret_scan.py` searched
      the 56 staged files and the 1077 files extracted from the staged `.pak` for the five values of
      `[HypeCubeJam.EOS]` and the section name (UTF-8 and UTF-16): 0 hits; the same script finds all of them in the
      Windows stage's `.pak`. The staged `DefaultEngine.ini` has 7 sections, none of them EOS.
    - **Problems and workarounds**.
      - The harness looks for Chrome in `C:\Program Files`; here it is in `Program Files (x86)`: `PROBE_CHROME`.
      - `-DirectoriesToAlwaysStageAsNonUFS=(Path="Movies")` in the Wasm ini did not keep the movie out, nor did
        `bSkipMovies=True` alone: both routes stage it (see the ini).
      - The container was over GitHub's 100 MB twice (208.7, then 105.7 MB: one 4K normal map sat in the
        `HierarchicalLOD` texture group) - texture caps. The Shipping wasm is over it by design - the split script.
      - The game's own log lines (`LogHCJ`, verbosity Log) do not reach the browser console (it shows Display and
        above; `-FullStdOutLogOutput` changed nothing) and a web run left no log file in the browser's storage that
        the harness could fetch: kills were read from the scoreboard and the kill feed in pictures, not from log
        lines. Portal jumps of bots were not counted.
      - A Shipping page drops `?args=` and console commands: its runs are driven by clicks at fixed places
        (1920x1080).
    - **Deploy** (not done - nothing was uploaded).
      - itch.io: new project, kind "HTML"; upload `mariuz_HypeCubeJam_web.zip`, tick "This file will be played in
        the browser"; Embed options: viewport 1280 x 720 (or larger), the fullscreen button, and
        **"SharedArrayBuffer support" on** (only that isolates the game's frame; check that it stayed on after
        saving). Chrome or Edge on a desktop.
      - GitHub Pages: publish the contents of `PackagedWeb\GitHubPages` as the site root (a `gh-pages` branch or a
        `docs` folder; `.nojekyll` included). No `_headers` rule is read there; the service worker adds the
        isolation headers and the page reloads once on the first visit. Each update: publish the whole folder again.
      - Any server with header control: `PackagedWeb\Ship\Wasm` with `PackagedWeb\deploy-plan-static` (nginx.conf /
        web.config from the engine; `.br` served for whole-file requests). Never upload `Ship\Wasm-Debug`.
    - Pictures (1920x1080, taken by the harness): `Media/110_web_title_menu.png`, `111_web_hangar.png`,
      `112_web_settings.png`, `113_web_match_deferred_tier.png`, `114_web_match_forward_tier.png`,
      `115_web_scoreboard_bots_fighting.png` (Development stage), `116_web_shipping_title.png`,
      `117_web_shipping_match.png`, `118_web_shipping_forward_tier.png`, `119_web_shipping_after_5_minutes.png`,
      `120_web_shipping_after_10_minutes.png` (Shipping stage).
    - **Not verified**: sound by ear (only that the audio device runs and plays frames); the game on itch.io or
      GitHub Pages themselves, in Edge, Firefox, Safari or on a phone; a download over a real network; whether
      settings survive a reload (browser storage); QUIT in a browser tab; playing with mouse and keyboard by a person
      (the harness clicked PLAY and held one key). No pipeline prewarm list was recorded (the engine's
      `-WebGPUPipelineRecord` / `pipeline_list.py`), so things flicker in for a few frames the first time they are
      drawn (about 1300 draws skipped in the first 40 s of a run).

## 15 Nov 2026 (fourteenth session) - difficulty in the menu, bots that are fair to a player
29. "Less bots that shoots you from the back" (the player, after one match of 9.4 minutes on build 0.17.0: Easy, 7 bots,
    4-D; earlier: "enemies are hard to spot"). What the game log of that match says: 2 kills, 30 deaths - one every
    19 s; 27 by bots (every one of the seven at least twice), 3 with no killer; eight deaths within 12 s of the one
    before; killed by phaser 8, torpedo 6, scatter 4, missiles 4, pulse 3, rail 1, blaster 1. The log could not say
    from which side. Built from a design spec (`fairness_spec.md`, written from reading the code; what it got wrong is
    at the end). No replicated property, no RPC, no lobby key changed: an old and a new build play together, and the
    rules are the server's. Layout checksum of seed 1234 unchanged (7C0CB60C).
    - **A. Difficulty: four levels, first row of the menu.** Easy / Normal / Hard / **Ace** (`?Difficulty=0..3`,
      `HCJBalance::NumBotDifficulties`). The skill rows of Easy, Normal and Hard are untouched; Ace flies with Hard's
      numbers and has none of the rules below - it is what Hard was. The stepper moved from the middle of the title
      page to the top, above QUICK PLAY and PLAY (outside the `PLATFORM_WASM` switch: the web build has it), with one
      line under it that follows the setting (`HCJBalance::GetDifficultyDescription`, under 74 characters, tested):
      "One bot at a time. Slow, wide aim, and they come at you from the front." / "Two on you at most. A moment's
      warning before any fire from behind." / "Three on you. Sharp aim, torpedo combos, scouts ambush from cloak." /
      "No rules: every bot hunts you, from any side, with everything it has." The in-match menu (Esc) has "Bot
      difficulty (applies now)" for the host and a single player (`AHCJPlayerController::MenuStepDifficulty` ->
      `AHCJGameMode::SetBotDifficulty`; bots read their row every Think, the setting is saved).
    - **B. The fairness rules** (`AHCJBotController`, class comment; numbers in `FHCJBotSkill`; `README.md`, "Bots and
      people", has the table). They apply only when the other ship's controller is a player controller. Bots among
      themselves are untouched, so the bot matches, their kill rates and the kill minimums of the playthrough tests
      do not move.

      | | Easy | Normal | Hard | Ace |
      |---|---|---|---|---|
      | `MaxAttackersOnHuman` | 1 | 2 | 3 | 0 (no limit) |
      | `MaxHuntersPerHuman` | 3 | 4 | 6 | 0 |
      | `RearFireDelay`, s | 2.5 | 1.2 | 0.5 | 0 |
      | `RearAimErrorScale` | 2 | 1.5 | 1 | 1 |
      | `FrontBias` | 1.0 | 0.6 | 0.3 | 0 |
      | `HumanTailScore` | -2 | -1 | 0 | +1.5 |
      | `bFlankHumans` | no | no | yes | yes |
      | `ArrivalGrace`, s | 4 | 2 | 1 | 0 |
      | `RespiteSeconds` | 6 | 3 | 0 | 0 |
      | `DamageToHumanScale` | 0.5 | 0.8 | 1 | 1 |
      | `TorpedoOnHumanChance` | 0.35 | 0.7 | 1 | 1 |

      - *Attack cap and grace* (`IsOffLimits`, asked by `FindNearestHostile` and by the smart loop of `PickTarget`,
        i.e. by every way a bot gets a target): a human ship is not picked while it is under its spawn shield, for
        `ArrivalGrace` seconds after it appeared in its sector (spawn, portal, wormhole:
        `AHCJShipCharacter::GetSectorArrivalTime`), while `MaxAttackersOnHuman` bots are attacking it, and for
        `RespiteSeconds` after its last attacker is gone. A bot the human hit in the last 4 s
        (`HCJBalance::ProvokedSeconds`) may always answer. A bot with nobody else to fight travels on after 5 s, as
        before. (Normal's last four rows of the table above were changed in item 31: 3.5 s, 5 s, 0.55, 0.45.)
      - *Rear arc* (`UpdateRearHold`): "behind" is more than about 107 degrees off the nose of the human's ship (dot
        product below `HCJBalance::RearArcDot` = -0.3, the threshold `PickTarget` already used). A bot that gets
        there holds guns, beam, torpedoes and tractor for `RearFireDelay`, aims `RearAimErrorScale` times worse for
        as long as it is there, and its strafe is pulled round to the human's front (`FrontBias`: the part of the
        human's forward vector that lies across the line between the two). A human's tail is no bonus in the target
        score; no cloaked approach to the tail and no close pass around a human below Hard; a human that was out of
        sight for more than a second gets the reaction delay again.
      - *Hunters*: `ChooseTravelDim` does not route to a human's sector that already holds `MaxHuntersPerHuman` bots.
      - *Damage*: `AHCJShipCharacter::TakeDamage` multiplies what a bot does to a human by `DamageToHumanScale` before
        anything else is computed from it - every weapon, the torpedo's share for the shield and each phaser step.
        A torpedo a bot would fire at a human is fired with `TorpedoOnHumanChance` only.
      - *Spawns* (bots too): spawn shield 2 -> 3 s (the first shot still ends it), no spawn within 80 m of an enemy
        instead of 50 (`SpawnSafeEnemyDistance`; 90 would break the scoring test), and a new ship is turned to face
        the nearest ship of its sector (no extra draw from `GameplayRandom`).
      - `-HCJNoFair` (`AHCJGameMode::IsNoFair`): `FHCJBotSkill::ClearFairness` on the row every Think and no damage
        scale - the bots of before on every level, in the same binary. The spawn changes and the HUD stay.
    - **C. What the player is told** (client side, from data every machine already has).
      - ENEMY BEHIND: an enemy within 90 m whose nose is within 12 degrees of the own ship (`HCJUi::IsAimingAt`) gets
        the pulsing "!" edge arrow (first among the arrows) and a yellow ring on the radar; when it is in the view's
        rear arc the screen says "ENEMY BEHIND - Bot-04 26 m" in yellow with a cue (the torpedo warning sound, at
        most every 4 s) and the log `Threat warning: ...`. This is what fills the 2.5 / 1.2 / 0.5 s in which a bot
        behind a player holds its fire. It also fires for a bot that is aiming at somebody else on the same line.
      - HIT FROM BEHIND: a hit from the view's rear arc draws a wider arc (0.60 instead of 0.38 rad each side), three
        heavier lines, for 2 s instead of 1.4, with the label under it. The arc is placed as on the radar - behind is
        down, left and right are kept. Before, a hit from behind and to the right was drawn at the right of the ring
        (where the shooter would be on the screen if the view were turned), a hit from dead astern like one from
        below. (The first picture of the real beam showed the new label at the bottom and the arc at the upper right;
        the placement was changed after that.)
      - Respawn screen: "Destroyed by Bot-04 with the RAPID PULSE - from behind" (or "from the side", "head-on":
        `HCJUi::ClassifyHitSide` of `AHCJPlayerController::GetLastDamageBearingDeg`, computed in `ClientDamageTaken`
        from the own ship's nose; only when the last damage is less than 1.5 s old), and for "from behind" the tip
        "Shot from behind: watch the lower half of the radar and the '!' arrows, then turn or boost away."
    - **D. Fairness telemetry** (server, `LogHCJ`, Log level, key=value like the `Playthrough:` lines; never the word
      "Error:"). `docs/tools/fair_stats.py <logs>` prints all of the numbers below per log and per group of logs.
      - `Fair: event=rules match= difficulty= level= nofair= max_attackers= max_hunters= rear_fire_delay= rear_aim_scale=
        front_bias= tail_score= flank= arrival_grace= damage_scale= respite= torpedo_chance=` - at every match start
        and when the level is changed in a match: what the match is played with.
      - `Fair: event=spawn t= name= nearest= enemies= sector=` - a human's new ship; `nearest` is the distance to the
        nearest ship of that sector (-1: nobody there).
      - `Fair: event=hit t= victim= attacker= attacker_bot= weapon= facing= bearing= dist= dmg= hull_after= fatal= rear=
        raw= scale=` - every hit on a human ship. `bearing` is the angle between the victim's nose and the direction
        to the attacker's ship (0 dead ahead, 180 dead astern; -1 unknown), `rear=1` when that is in the rear arc
        (above 107.5), `facing` the shield facing it landed on, `dmg` what shield and hull took, `raw` the damage
        before the difficulty's `scale`. The steps of a phaser beam are added up per attacker: one line every 0.5 s
        (and at once for the step that destroys the ship).
      - `Fair: event=death t= name= killer= cause= bearing= dist= killer_bot= rear= alive= attackers=` - a human's
        death: the weapon or `turret`, `own_torpedo`, `black_hole`, `out_of_the_sector`,
        `no_shooter_inside_the_sector`; `alive` seconds since the spawn, `attackers` bots attacking it at that moment.
        The plain line is now `Death: <name> (no killer: <cause>)`.
      - `Fair: event=status t= match= difficulty= nofair= humans= dmg_total= dmg_rear= dmg_aft= deaths= rear_deaths=
        peak_attackers= held_cap= held_rear= held_grace= bot_deaths= dmg_unscaled= human_ships= match_t= held_respite=`
        - every 15 s, at the end of a match and when the world ends; summed over all humans and reset with the match.
        `dmg_*` is damage by bots only, added up per damage step (exact); `peak_attackers` the most bots attacking
        one human at a time; `held_*` the number of bot Thinks (4 a second per bot) in which a bot left a human alone
        because of the cap / held its fire in the rear arc / because of the arrival grace / because of the respite.
      - **Rear share of a log**: `dmg_rear / dmg_total` of its last `event=status` line. **Deaths per minute**:
        `deaths / (match_t / 60)` of that line, or the `event=death` lines over the time since the first
        `event=spawn`. By hand:
        `Select-String 'Fair: event=status' HypeCubeJam.log | Select-Object -Last 1`. The same from the hit lines
        (sum of `dmg` with `attacker_bot=1 rear=1` over the sum with `attacker_bot=1`) differs by a point or two,
        because a phaser line carries the bearing of its last step.
    - **Measured.** `docs/tools/fair_matches.py`: one human on an autopilot against seven bots (`?Bots=8`), seed 99,
      no limits, headless editor binary, 245 s per process (4.1 minutes of match), six processes at a time; "before"
      is the same binary with `-HCJNoFair`. Two pilots, because the first one cannot show the rear share: the
      autopilot of the playthrough tests ("aiming": its nose follows the nearest ship in sight, it fires all the time
      and so provokes every bot near it) faces whoever attacks it - 2 % of its damage comes from behind with or
      without the rules. The "blind" pilot (`-HCJAutoFly` alone: circles, fires straight ahead, never hits) is the
      player who does not find the enemy: a third of its damage comes from behind without the rules (the spec had
      guessed 35 % for a uniform orbit).

      | Easy | aiming, before | aiming, fair | blind, before | blind, fair |
      |---|---|---|---|---|
      | matches / minutes | 6 / 24.4 | 6 / 24.4 | 3 / 12.2 | 3 / 12.2 |
      | human deaths per minute | 3.44 (84) | **1.31** (32; 29 by bots, 3 in a black hole) | 2.21 (27) | **0.74** (9) |
      | per match | 2.7-3.9 | 1.0-2.0 | 1.7-2.9 | 0.25-1.2 |
      | rear share of bot damage | 2.2 % | 0.1 % | **32.8 %** | **3.2 %** |
      | deaths by a bot in the rear arc | 2 of 82 | 0 of 29 | 7 of 26 (27 %) | 1 of 9 (11 %) |
      | deaths within 12 s of the spawn | 44 of 84 | 3 of 32 | 6 of 27 | 0 of 9 |
      | seconds alive at death, median | 12 | 35 | 19 | 53 |
      | spawn to the first bot hit, median s | 4.6 | 8.9 | 5.0 | 12.9 |
      | bots that hit it in its last 8 s: 1 / 2 / 3 | 54 / 26 / 2 | 29 / 1 / 0 | 25 / 2 / 0 | 9 / 0 / 0 |
      | `peak_attackers` per match | 2 3 3 2 2 3 | 2 1 1 2 2 2 | 3 2 2 | 1 2 1 |
      | `held_cap` / `held_rear` / `held_grace` / `held_respite` | 0 / 0 / 0 / 0 | 1306 / 20 / 348 / 1440 | 0 / 0 / 0 / 0 | 734 / 290 / 162 / 896 |
      | bot damage on the human, applied (before the scale) | 11768 | 5421 (10841) | 4657 | 2249 (4498) |
      | torpedo share of that damage | 19 % | 5 % | 12 % | 1 % |
      | the human's kills | 17 | 27 | 0 | 0 |
      | kills in the match, all ships | 397 | 360 | 193 | 170 |

      | Normal, Hard, Ace | deaths per minute | rear share | deaths in the rear arc | `peak_attackers` | held cap / rear / grace / respite |
      |---|---|---|---|---|---|
      | Normal aiming, before (2 matches) | 4.79 (39) | 0.1 % | 0 of 39 | 2 2 | 0 |
      | Normal aiming, fair (2) | 3.92 (32) | 1.5 % | 1 of 32 | 2 2 | 25 / 35 / 189 / 112 |
      | Normal blind, before (2) | 4.04 (33) | 30.6 % | 10 of 31 | 3 2 | 0 |
      | Normal blind, fair (2) | 2.96 (24) | 7.1 % | 3 of 22 | 2 2 | 19 / 157 / 170 / 158 |
      | Hard blind, before (2) | 3.93 (32) | 32.0 % | 10 of 32 | 3 2 | 0 |
      | Hard blind, fair (2) | 4.18 (34) | 19.8 % | 10 of 33 | 2 2 | 0 / 84 / 132 / 0 |
      | Hard aiming, before / fair (1 each) | 5.39 / 5.63 | 0.9 % / 2.8 % | 0 / 0 | 2 / 2 | 0 / 7 / 84 / 0 (fair) |
      | Ace aiming (1) | 4.16 (17) | 2.6 % | 1 of 17 | 2 | 0 / 0 / 0 / 0 |

      - Read. (1) Easy: a player who does not find the enemy dies a third as often (0.74 against 2.21 a minute) and
        takes a tenth of the share from behind (3 % against 33 %); a player who always fights dies 1.3 times a minute
        instead of 3.4, lives 35 s instead of 12, and is almost never killed in its first 12 s (3 of 32 against 44 of
        84) or with two or more bots on it (1 of 32 against 28 of 84). Targets of the orchestrator for the aiming pilot on
        Easy - at most 1.5 deaths a minute, at least 40 % below the baseline: 1.31 and -62 % over six matches; **one
        of the six matches was above 1.5** (2.0, two of its eight deaths in a black hole), and 32 deaths put the rate
        at 1.3 +- 0.25. (2) Normal: rear share 7 % (target 25), deaths 18 % (aiming) and 27 % (blind) below the
        baseline, at most two attackers - but still a death every 15-20 s for these pilots and half of them within
        12 s of the spawn: Normal's fairness numbers are the spec's and were not tuned. (3) Hard: the rear share
        falls from 32 to 20 % (target 35), deaths do not change (no damage scale, cap of three). **`held_cap` stays 0
        on Hard**: three bots on one human did not happen in these matches with or without the cap (7 bots on 16
        sectors; the peak was 2, once 3 in a baseline). (4) Ace: every counter 0. (5) The bots still fight each other:
        kills in the match 360 against 397 (Easy), and no `stuck`, `out_of_bounds` or `nan` event and no error line in
        any of the 49 match logs of this session.
      - How the Easy numbers were reached (aiming pilot, deaths per minute; three matches unless said):
        the spec's numbers with `DamageToHumanScale` 0.6: 1.56; plus the respite and 4 s of arrival grace: 1.63 (no
        gain for this pilot: it shoots at whatever is near, and a bot it shot may answer); plus "the others make room
        for the bot the player shot": 1.31 and 1.63 in two batches (1.48 over six) - before that, in 10 of 19 deaths
        two or more bots had hit it in its last 8 s, afterwards in 1 of 16; plus `DamageToHumanScale` 0.5 and
        `TorpedoOnHumanChance` 0.35: 1.31 over six. The steps are inside each other's noise (a match has 4-8 human
        deaths); what is solid is the distance to the baseline (3.3-3.6 in every batch).
    - **The three deaths with no killer.** Not a collision: rocks do no damage at flying speed - only a collision at
      warp does (22 points), and that match's log says "0 collisions". Not a black hole ("by black holes 0"). What is
      left in the code: beyond 260 m from the middle of a sector the hull loses 25 points a second and at 400 m the
      ship is gone ("THE VOID" in the kill feed, "RETURN TO SECTOR" on the HUD); the blast of the own torpedo; and no
      bot had hit the ship in the 5 s before, or that bot would have been credited. The three came within 100 s of
      each other near the end of the match (t = 435, 453 and 536 s), the first 1.3 s after the player's second kill.
      Most likely the player flew out of the sector, probably running from the bots; the 0.17.0 log cannot prove it.
      Nothing was changed for it. From now on the log says which: `Death: <name> (no killer: out of the sector | own
      torpedo | black hole | no shooter inside the sector)` and `cause=` of `Fair: event=death`.
    - **Verification.** `HypeCubeJamEditor` and `HypeCubeJam` (Win64 Development) built. 71 of 71 automation tests
      (the count is unchanged: the new checks are inside `HypeCubeJam.Balance.BotDifficulty` - four levels, Ace =
      Hard's numbers, every fairness field ordered across the levels, Ace and `ClearFairness` all off, names and
      descriptions, bearings and the rear arc - and inside `HypeCubeJam.UI.Targeting`: `IsAimingAt`,
      `ClassifyHitSide`). Playthrough suite (editor binary, headless, four scenarios at a
      time, 516 s, exit code 0): 8 of 8.

      | scenario | result | checks | what it saw |
      |---|---|---|---|
      | `match_cycle` | pass | 21 | match 1 won with 5 kills after 36 s; layouts 7C0CB60C -> 5671F10D |
      | `difficulty_easy` | pass | 29 | 52 kills (item 27: 52), 55 portal / 11 wormhole jumps, 48 pickups; every type killed and died |
      | `difficulty_normal` | pass | 31 | 76 kills (76), 44 / 10 jumps, 53 pickups |
      | `difficulty_hard` | pass | 31 | 71 kills (62), 55 / 11 jumps, 71 pickups |
      | `small_universe` | pass | 13 | 92 kills (92) on four sectors; 81 turret shots, no ship shot down and no rock broken in this run |
      | `net_host_client_late` | pass | 49 | client 3 kills / 2 deaths, 3.9 corrections per 100 s; late joiner 2 / 4, 2.7 per 100 s and one rock already gone; 3 rocks destroyed, 3 destroyed-set states compared by each |
      | `autopilot_hangar_match` | pass | 17 | cruiser chosen, PLAY, three matches to the limit of 4 (Normal); the player 5 kills / 2 deaths |
      | `all_matches_together` | pass | 20 | kills: phaser 139, torpedo 84, blaster 57, missiles 54, pulse 45, scatter 35, rail 25; turrets shot 5 ships down, 7 turrets destroyed; 8 rocks destroyed |

      Server frames 1.5-2.2 ms mean, longest 400 ms. The two scenarios with a human were also run by themselves
      (twice; see below for the first).
      - *Found by the suite:* `net_host_client_late` failed its first run with "no rock was destroyed in this run
        (nothing to compare)". Until now the torpedoes of autopilots that died every few seconds broke a rock or two
        in every run; pilots that live longer are handed fewer torpedoes (65 fired against 109 with `-HCJNoFair` in a
        pair of runs side by side: 2 and 4 rocks). The scenario no longer leaves that to chance: its host runs
        `HCJMenuCmd breakrock` after 64, 128 and 168 s (`docs/tools/playthrough.py`).
      - Not packaged (the next step of the session is somebody else's). The staged build in `Packaged\Temp` and the
        zip are still 0.17.0 without these changes.
    - Development aids added: `-HCJNoFair`, the `Fair:` lines, `docs/tools/fair_matches.py`, `docs/tools/fair_stats.py`,
      `HCJMenuCmd difficulty [n]` (the menu row; not saved), `matchdifficulty <+1|-1>` (the in-match stepper),
      `enemybehind [uu]` (listen host, with `peace`: the nearest ship is put behind the own one with its nose on it);
      `HCJMenuCmd die` now credits the nearest ship.
    - Pictures (editor binary with `-game`, 1280x720, the capture switch; the menu rows by `HCJMenuCmd difficulty`, the
      enemy placed by `enemybehind` while the bots were held still with `peace`, the kill by `enemyfire`):
      `Media/121_title_menu_difficulty_easy.png`, `122_..._normal.png`, `123_..._hard.png`, `124_..._ace.png` (the
      Difficulty row and its line; taken before the last tuning steps - the menu code did not change after them),
      `125_enemy_behind_warning.png` (the line, the "!" arrow, the yellow ring on the radar),
      `126_hit_from_behind_development_hit.png` (`HCJMenuCmd shieldhit 1,12`: a hit on the aft facing from dead
      astern), `127_hit_from_behind_phaser_of_a_bot_behind.png` (the beam of the bot behind, both warnings),
      `128_death_recap_from_behind.png` ("Destroyed by Bot-03 with the BLASTER - from behind" and the tip),
      `129_in_match_menu_bot_difficulty.png` (after one step: Normal). The cue of ENEMY BEHIND was not heard (the
      runs had no sound).
    - **What the spec had wrong, and what was done differently.**
      - Its measurement could not show its main number: the autopilot it names turns its nose to the nearest ship,
        so its rear share is 2 % before any rule. The blind pilot was added for that.
      - `?Bots=7` is seven combatants, i.e. six bots next to the human: the matches here use `?Bots=8`.
      - "A bot the human shot may always answer" plus a plain cap gave an aggressive player two bots at once on Easy
        most of the time. Now the other bots make room for the one the player shot (`CountBotsAttacking` with
        `bAheadOnly`).
      - The arrival time is also set at the spawn, not only after a jump: the spawn shield ends with the first shot,
        and a pilot who fires at once had no grace at all.
      - Bots that are breaking off or running for a repair pad are not counted as attackers (they hold their fire).
      - "playthrough.py needs no change": see the rock check above.
      - The ring on the radar is yellow, not `ReadWarningColor`: that red is the colour of the blip it surrounds.
      - Added to its numbers: `RespiteSeconds`, `TorpedoOnHumanChance`, `DamageToHumanScale` (asked for after the
        player's data: 0.6 on Easy, lowered to 0.5 while tuning), Easy `ArrivalGrace` 3 -> 4. Its optional parts were
        all built (ENEMY BEHIND, the recap, the in-match stepper).
      - Its line numbers were right to within a line or two, and its three open questions: the two playthrough
        automation tests have no human ship (they pass unchanged); no Blueprint overrides the ship class
        (`DefaultPawnClass` is the C++ class), so the 3 s apply; whether a player can look away from the ship's nose
        was not checked - the rules measure "behind" from the nose of the ship.
    - Judged from logs of autopilot matches, the test runs and nine still pictures. Nobody has played against the
      fair bots, heard the ENEMY BEHIND cue or used the two difficulty steppers with a mouse.

## 15 Nov 2026 (fifteenth session) - the hypercube map, the explosion of a ship
30. "Improve the hypercube visualization on the right , improve particles when my ship explodes" (the player). Both are
    client-side and cosmetic: no replicated property, no RPC, no gameplay value changed (an old and a new build play
    together); nothing platform-specific. Built on item 29 as it stood. Not packaged.
    - **A. The hypercube map** (`Private/Game/HCJHUDMap.cpp`, new; `AHCJHUD::DrawHypercubeMap`).
      - *What it was* (`Media/131`-`133`): the 2^n sectors on a fan of n axes in the plane - a tangle from 16 sectors
        on, unreadable at 32 (two labels on top of each other in the first picture taken) -, 4-pixel dots, the own
        sector a white square the size of the others, neighbours a thin ring with the id under it, one red speck for
        "somebody is there", and three rows of legend.
      - *Layout* (`HCJUi::ProjectMapSector`, plain C++, tested): the sectors are the corners of cubes. The first three
        bits span one cube seen 27 degrees from the right and 24 from above with a mild perspective; every further bit
        shifts the whole cube - two cubes side by side for 16 sectors, four (two by two) for 32, a second layer behind
        them for 64. So a portal of one dimension always runs in the same direction on the map. The depth of a corner
        sizes and shades its mark (2 to 3.6 px half width, 50 to 100 % brightness); a line whose ends are both at the
        back is one pixel and dim. The cubes sway 4 degrees to either side in 18 s (`MapSwayDegrees`, 0 = still).
        Nearest two sectors on the map at 1280x720 (worked out from the projection, not measured in a picture): about
        50 px (8 sectors), 26 px (16) and 27 px (32, in its wider panel); the old layout had 28, 19 and 6 px. Tried
        first and dropped: the cube-in-a-cube picture of a tesseract (18 px and 11 px: the two corners on the line of
        sight fall on each other).
      - *What it shows* (class comment of `AHCJHUD`): the own sector - a white ring, "YOU", a slow halo; for a second
        after a jump (or a new ship) two rings run out from it, the mark slides along the line it travelled and that
        line lights up. One jump away - thick lines from the own sector, a diamond and the sector id, the text and the
        colour of the in-world "PORTAL > 0101" marker; violet with a round ring for the far end of the own wormhole.
        Enemies - a red badge with their number (the replicated sector of every player state; a pilot waiting for a
        ship is not counted; the own sector shows what the radar counts, so a cloaked ship is not given away there).
        Target - yellow corner brackets, the id and the way there hop by hop in yellow: the sector the HUD is guiding
        to (`GuideSector`), or red brackets on the sector of the locked ship. Nearest fight - a pulsing red ring with
        four sparks on the nearest other sector (by the route table) where a ship was lost in the last 8 s (a death
        count that went up) or that holds two or more ships. Station - a white frame; black hole - an orange ring
        round a black core; wormhole - a violet dashed arc. The legend is one row (station, black hole, wormhole), and
        only for what the universe has.
      - *Labels* (`HCJUi::PlaceMapLabels`, tested): each label takes the best of 16 places next to its mark (below,
        above, beside, diagonal; near, or one line farther with a thin tie line) - off the marks, the badges, the other
        labels and the panel edge, preferably on the side away from the own sector - in three rounds. Run on the
        positions at rest, so the sway cannot make a label jump. The test places the labels of the own sector and of
        all its neighbours for every own sector of a 3-D, 4-D and 5-D universe with every portal open at the sizes of
        1280x720 and 1920x1080: no overlap at all with 8 and 16 sectors; with 32 a tenth of a label at most.
      - *Size*: 300 px wide at 1080p (was 260), 390 px from 32 sectors on; at 1280x720 200 and 260 px. Height about the
        same as before (the legend lost two rows).
      - *Cost*: game thread, a timer round the call, averages of 2000 frames (editor binary, 1280x720): 0.13-0.16 ms
        a frame with 8 and with 16 sectors, 0.24-0.27 ms with 32. Most of it is text: the 8-sector map with a third
        of the lines takes as long as the 16-sector one (about 14 texts: the title, three legend words, YOU, the ids,
        the badges; 150 to 400 canvas lines). The whole frame with and without the map (`-HCJNoMap`; bots held still,
        sector 0101, medians of 1.5 s): 8 sectors 7.59-7.68 / 7.60-7.76 ms, 16 sectors 7.69-7.84 / 7.81-7.86 ms, 32
        sectors 8.14-8.23 / 8.03-8.14 ms - nothing this measurement can show for 8 and 16, a tenth of a millisecond at
        most for 32 (the frame waits for the GPU). The old map was not measured this way (its code is gone). Per frame
        the map itself allocates nothing: inline arrays, the sector names / title / badge digits and all text sizes
        are kept per universe and canvas size (measuring them every frame was a fifth of the time with 16 sectors);
        `DrawLabel` builds one `FText` per text, as for every label of the HUD. No render target.
      - *Problems found in the pictures and fixed*: a label one line away from its mark read as the label of the sector
        next to it (the far places were too cheap: now only better than two square pixels of overlap, and tied to the
        mark by a line); a badge covered the label of its own sector (badges are obstacles for the labels now); the
        wormhole arcs were long dashes that read as noise (shorter, fainter unless it is the own one); the three dashes
        of the legend ran into one line.
      - Not built: a larger map on the scoreboard (there was none before either).
    - **B. A ship's explosion** (`UHCJEffectsDirector::PlayShipDeath`; `README.md`, "Explosions").
      - *What it was* (`Media/151`, slowed to 6 %): the flare with its streak for half a second, the small Niagara
        explosion, fourteen light grey plates and ten embers. For the own ship the view was put AT the place of the
        ship (`SetInitialLocationAndRotation` in `NotifyShipKilled`): the player sat inside the flare and saw white
        rectangles fly past for four seconds.
      - *Staged* (stages and times: `HCJEffects::BlastStageTime`, tested). The hull fails for 0.32 s
        (`HullFailSeconds`): it stays in view, rolls faster and faster, its lights flare orange-white, and three pops
        run along it - an impact star, the metal impact system, a few sparks, the hull impact sound at 0.7 of its
        pitch. Then the blast: the flare as the core flash; the first puff of the fireball; a shockwave ring in the
        plane of the hull (tipped towards a view that is close to that plane); the Niagara explosion of before; three
        large plates that tumble away slowly with a trail of fire and smoke (`NS_RocketTrail` of the Niagara Examples
        on a pool of five components that follow a debris slot), fourteen plates, twenty-two sparks (the ember cube
        drawn out along its flight, gone in under a second), ten embers; the camera shake; the sounds. Three more puffs come
        0.07, 0.15 and 0.27 s later, off-centre, the second with a small flash of its own. The first puff lives three
        seconds: fire for 0.7 s, then the smoke that stays and thins out, with cells that glow a little longer.
      - *Materials* (`docs/tools/make_explosion_materials.py`, editor Python like the other `make_*` scripts; nothing
        imported): `M_HCJ_Fireball` - alpha composite, so that fire adds light and smoke covers; a ball whose edge
        billows by two samples of `T_HCJ_CloudNoise` (the cloud texture of the sky) that it carries outwards; heat
        from white-yellow through orange to dark red by the seconds it has burnt; a rounder outline for the smoke.
        `M_HCJ_ShockRing` - additive, a thin broken-up ring that runs out fast and slows. `M_HCJ_HullDebris` - lit
        grey metal whose torn edges glow by per-instance custom data (`HCJEffects::PlateHeat`: hot for a second and a
        half). No new Niagara system: the trail and the two impact systems are the pack's, unchanged.
      - *The own ship*: full detail whatever else is going on; the shake is 2.4 times a normal blast's for 0.45 s
        (off with the "Camera shake" setting); the screen flashes warm for 0.22 s (alpha 0.4 at most) with an orange
        vignette for a second; a low boom (the torpedo explosion at 0.6 of its pitch) under the explosion sound.
        **Death camera** (`AHCJPlayerController::CalcCamera`): from where the chase camera was the view pulls back
        15 m along the line wreck -> camera, turns 7 degrees a second round the wreck and keeps it in the middle for
        the whole respawn countdown (4 s; left mouse or R after 1.5 s, unchanged); it stops in front of a rock. The
        respawn lines moved below the middle of the screen (69 % and 81 % of the height instead of 44 % and 60 %).
      - *Budget*: three levels (`HCJEffects::PickBlastDetail`, tested) - within 70 m everything; to 160 m, or with two
        other blasts in their first second (unless nearer than 28 m), one lobe, half the pieces, no trails; beyond
        that, or with four running, no pops, no ring, one short puff, a few pieces. Hard caps: 14 puffs, 3 rings, 5
        trails, 8 blasts, 120 debris pieces (was 72), 6 flares (as before). The director still ticks only while
        something is alive.
      - *Frame time*: `-HCJBlastBench` (new; `AHCJPlayerController::TickBlastBench`): bots held still, the own ship in
        sector 0101 looking at dark sky, cinematic look on; per round the frame times of 1.5 s with nothing going on,
        then a ship is blown up and the frame times of the next 3.5 s. Editor binary with `-game`, 1280x720, GTX 950,
        nothing else running, no picture taken; eight rounds a run, the first left out (it compiles shaders in the
        editor binary: one frame of 0.3-0.9 s with either version). "Before" is the same binary with `-HCJClassicBlast`.

        | ms a frame (fps) | | quiet, median | the 3.5 s after, mean | first 0.5 s | 0.5-1.5 s | 1.5-3.5 s | slowest half second of any round | longest frame |
        |---|---|---|---|---|---|---|---|---|
        | a bot 15 m ahead | before | 7.38 (135) | 8.38 (119) | 8.69 | 8.45 | 8.27 | 8.75 (114) | 19 |
        | | after | 7.35 (136) | 8.67 (115) | 8.56 | 8.92 | 8.57 | 9.23 (108) | 30 |
        | the own ship | before | 7.39 (135) | 8.50 (118) | 8.79 | 8.64 | 8.35 | 8.97 (111) | 29 |
        | | after | 7.37 (136) | 9.06 (110) | 8.74 | 9.22 | 9.06 | 9.74 (103) | 29 |
        | four bots in one frame, 15 to 36 m | before | 7.46 (134) | 9.19 (109) | 9.74 | 9.43 | 8.92 | 9.91 (101) | 16 |
        | | after | 7.49 (134) | 9.47 (106) | 9.06 | 10.01 | 9.32 | 10.54 (95) | 21 |

        So: the new explosion costs 0.3 to 0.6 ms a frame more than the old one while it runs, most while the fireball
        is at its largest; the slowest half second measured is 95 fps (four ships at once), 103 fps for the own ship.
        A kill costs about a millisecond a frame for a few seconds with either version (messages, the kill feed, the
        Niagara system). The longest single frames (19-30 ms, one round in seven) come with both.
        - The first version of the fireball - five puffs, the last a smoke cloud 1.3 times as wide, three octaves of
          hash noise per pixel - was measured before it was changed: 12.05 ms in its slowest half second for the own
          ship (83 fps) against 8.84 ms before. A fireball 23 m across seen from 15 m is several layers over most of
          the screen. Now the first puff turns into the smoke (one layer less, and the largest) and the billows are
          two texture samples: 9.5 ms.
        - A run in which I worked on the machine at the same time read 11-12 ms for the same quiet scene (7.4 ms
          otherwise) and is not in the table.
        - Not measured: the packaged build (nothing was packaged), a fight in a dense field, a client.
      - *Problems found in the pictures and fixed*: the first fireball was a pale butter-coloured blob with a flat
        edge (brightness 3 -> 2.4, a steeper ramp to red, the heat follows the noise); the ring of the own ship swept
        through the camera, which sits in the plane of the hull (tipped towards the view when the view is within 30
        degrees of the plane); the vignette showed its bands (16 bands of 9 px instead of 6 of 26); the respawn text
        and the recap panel lay on the wreck (moved down); with the cloud texture the first version sampled the wrong
        channels (mean 0.26 instead of 0.5: almost no fire and no smoke - seen at once in the pictures); the smoke had
        the ragged outline of the flames (its own rounder outline); plates were dark frames (lighter metal); the trail
        was a hair (scale 2.5).
      - `-HCJClassicBlast`: the single pop and the view of before, in the same binary - the baseline of the numbers.
    - **Verification.** `HypeCubeJamEditor` and `HypeCubeJam` (Win64 Development) built. **73 of 73** automation tests on the
      final code (two new: `HypeCubeJam.UI.HypercubeMap` - the projection inside the square, sectors apart, portals of
      one dimension parallel, depth in [0, 1], the labels as above; `HypeCubeJam.Effects.BlastStages` - the stages in
      order and on the right side of the blast, the detail by distance and by the number of blasts, the own ship always
      full, plates cool, the caps of the settings carry one full blast). The first run was 72 of 73: the new map test
      itself asked a universe of one dimension (two sectors on a line, which no match has) to stay inside the panel
      under the sway; the check now starts at two dimensions. Playthrough suite (editor binary, headless, four scenarios
      at a time), twice - before the last map changes and on the final binaries: **8 of 8** both times (522 and 528 s,
      exit code 0).

      | scenario | result | checks | what the last run saw |
      |---|---|---|---|
      | `match_cycle` | pass | 21 | match 1 won with 5 kills after 83 s; layouts 7C0CB60C -> 5671F10D |
      | `difficulty_easy` | pass | 29 | 49 kills, 58 portal / 7 wormhole jumps, 55 pickups |
      | `difficulty_normal` | pass | 30 | 65 kills, 66 / 9 jumps, 55 pickups |
      | `difficulty_hard` | pass | 31 | 75 kills, 57 / 6 jumps, 65 pickups |
      | `small_universe` | pass | 14 | 98 kills on four sectors; 86 turret shots, 2 turrets destroyed, 1 rock |
      | `net_host_client_late` | pass | 49 | client 1 kill / 1 death, 6.1 corrections per 100 s; late joiner 2 / 2, 2.7 per 100 s and one rock already gone; 3 rocks destroyed |
      | `autopilot_hangar_match` | pass | 17 | cruiser chosen, PLAY, three matches; the player 5 kills / 3 deaths |
      | `all_matches_together` | pass | 20 | kills: phaser 151, torpedo 87, missiles 53, blaster 41, pulse 30, scatter 28, rail 22; 7 ships shot down by turrets, 9 rocks destroyed |

      The bot matches are not repeatable (they run in real time): `difficulty_easy` had 74, 60 and 49 kills in three
      runs of this session's builds (item 29: 52), `difficulty_normal` 70, 74 and 65 (76), `small_universe` 83, 89 and
      98 (92). Nothing in this item touches a bot, a weapon or the server; the layout checksum of seed 1234 is
      unchanged (7C0CB60C).
    - Development aids added: `-HCJClassicBlast`, `-HCJBlastBench=<enemy|own|crowd>[,<rounds>[,<uu>[,<sector>]]]` (the
      `BlastBench:` lines), `-HCJNoMap`, the verbose line `HUD: the hypercube map takes ... microseconds`,
      `HCJMenuCmd mapjump <s>` (the jump animation of the map frozen at that age; no number: as it runs).
    - Pictures (editor binary with `-game`, 1280x720 unless said, the capture switch; scenes by `HCJMenuCmd scene
      5,dark`, `peace`, `HCJParkEnemy`, `killenemy`, `die`). **The explosion pictures were taken with the game slowed
      to 5 % (`HCJMenuCmd slomo 0.05`), one picture every 0.1 s of game time; nothing was lengthened.**
      `Media/131_map_before_3d_build_0_17.png`, `133_map_before_5d_build_0_17.png` (the map as it was, from the
      packaged build 0.17.0 started with `?Dim=3` and `?Dim=5`), `132_map_before_4d.png` (the editor binary before the
      first change); `134_map_new_3d.png` (three portals out of the own sector, badges, a fight on 001),
      `135_map_new_4d.png` (the way to the enemies in yellow: PORTAL > 0100, brackets on the sector it leads to, a
      fight top centre), `136_map_new_5d.png` (32 sectors: four portals out of the own sector, a fight bottom right),
      `137_map_new_4d_after_a_jump.png` (`HCJMenuCmd mapjump 0.25`: the rings, the line just travelled),
      `138_map_new_4d_1920x1080.png`, `139_map_new_5d_1920x1080.png`, `140_map_before_and_after_enlarged_3x.png` (the six
      panels of 131-136 cut out and enlarged three times); `141`-`148_own_ship_*.png` (the own ship: the first pop,
      the hull glowing and rolling, flash and ring with the vignette, the ring passing, the fireball, the flames
      cooling, dark smoke with embers and glowing plates, the smoke thinning - 0.1, 0.3, 0.4, 0.5, 0.7, 0.9, 1.2 and
      1.9 s after the death; the blast is at 0.32 s), `149_enemy_explosion_mid_distance_90m.png` (a bot 90 m ahead, the simpler version: ring
      and fireball), `150_enemy_explosion_35m_sequence.png` (eight frames of a bot 35 m ahead, full detail),
      `151_own_ship_explosion_before_sequence.png` (six frames of the own ship's death before the change, slowed to 6 %:
      the view inside the flare, the plates).
    - Weak, or not done: nobody has seen either in motion; the explosion was judged from stills in slow motion, the sounds (the
      pops, the low boom) were not heard (the runs had no sound).
      - The fireball is flat: quads that face the view, so as the death camera turns round the wreck the flames turn
        with it, and from close up their edge looks cut out. It burns for 0.7 s.
      - The smoke is thinned near the view (so that a blast in front of the nose does not blind): for the own ship it
        is a faint brown cloud; 35 m ahead it is a dark blob for about two seconds that can hide a ship behind it.
      - The trails behind the three large plates are rows of white puffs (the pack's rocket trail at 2.5 times its
        size), not fire. The hull does not break up: it is hidden at the blast and the plates are boxes.
      - "KILLED BY ..." (the centre message, 30 % of the height) lies over the top of the fireball.
      - The death camera was only run on a listen host, in third person, in an open field. On a client it starts when
        the death arrives (the network scenario ran it headless: no picture); first person takes another branch (the
        view starts inside the ship) that was not run; the camera stops in front of a rock on the line to the wreck
        but may then sit close to it.
      - The frame times are the editor binary's on a quiet machine; the packaged build was not measured.
      - Map: with 32 sectors it is dense, and the labels of an own sector with all five portals open may touch by a
        corner (the test allows a tenth of a label); 64 sectors work by the test only. The sway was seen as two
        pictures that differ, not in motion. A locked target is nearly always in the own sector, so the red brackets
        were never seen. A "fight" is two ships in a sector or a death there - two bots passing each other count.
        0.13-0.27 ms of the game thread a frame is not nothing, even though the frame time does not show it.
      - `-HCJClassicBlast` keeps the old explosion code alive (about forty lines).

## 15 Nov 2026 (sixteenth session) - review fixes, Normal tuned, final measurement, packaged
31. Items 29 and 30 were read by four reviewers (netcode, design, regression, performance / look). This session checked
    each finding against the code, fixed what was real, tuned Normal, measured again, and packaged. No replicated
    property, no RPC, no lobby key changed (`ProjectVersion` stays 0.2.0; layout checksum of seed 1234 still 7C0CB60C).
    - **Findings and what was done.**
      - *Death camera cancelled on a network client* (major, real by reading): `PlayerTick` ended the camera whenever
        the controller had a pawn, and on a client the dead ship can still be the pawn for a frame (the ship's
        `bIsDead` and the controller's `Pawn` travel on two actors). Now `AHCJPlayerController::HasLivePawn` (a pawn
        that is not a destroyed ship) decides, in `PlayerTick` and in `CalcCamera`. Seen on a real client: packaged
        build, a headless listen host (`-nullrhi`, Hard, `-HCJNoFair`, so that the client dies often) and a rendered
        client at 1280x720, one picture a second - five deaths in a minute, five `Death camera:` lines, and the four
        pictures looked at (0.2 to 1.4 s after a death) show the view pulled back with the wreck in the middle
        (`Media/162`).
      - *`bWasLocalShip` overwritten every tick* (minor, real): the other order of the same two updates cleared it just
        before the death, and the own ship then got a stranger's explosion and no camera. It is only ever set now.
      - *Fairness telemetry allocated strings on every step of a beam* (minor, real): the attacker's name is made once
        per beam entry (two a second), the victim's once per ship; discrete hits as before.
      - *Normal untuned* (major, real): see below.
      - *The hunters cap only blocked the "hunt" route* (minor, real): `ChooseTravelDim` now also counts the bots that
        are travelling into a person's sector, and the roam fallback drops a portal (or the wormhole) into a sector
        that is full while another way out is open. New counter `held_hunters` on the `Fair: event=status` line
        (`fair_stats.py` prints it): 44 in six Easy matches, 6 in six Normal ones. Among bots nothing changes (no
        such sector without a person; the same draw from `BotRandom`).
      - *ENEMY BEHIND cue is the torpedo warning, and fires for a nose that sweeps past* (minor, real - and the
        fallback the reviewer proposed, `LowHealthSounds`, is the same wave: every warning of the game is the one
        tick). The line and the cue now wait until the nose has stayed on the own ship for 0.3 s
        (`HCJUi::AimWarnDwellSeconds`; the "!" arrow and the radar ring still show at once), and the cue is that tick
        at 0.67 of its pitch (`AimWarnCuePitch`; `UHCJAssetSettings::PlaySound2D` got a pitch). Still nobody has heard
        it. It still fires for a bot that keeps its nose on the player while aiming at somebody behind the player.
      - *`net_host_client_late` at the limit of its score checks* (major, real: five runs of the day gave 1 to 6 events
        per remote player and once a single remote death): the host of that scenario runs with `-HCJNoFair` now. The
        scenario is about the network; its checks were written for easy bots that treat a person like any ship. The
        rules with remote people in a match are therefore no longer part of the suite (they ran in the five suite
        runs of items 29 and 30, without an error line).
      - *Spawn changes not switched off by `-HCJNoFair`* (minor, real as a wrong comment): the comment of
        `AHCJGameMode::IsNoFair` and the README now say that the 3 s spawn shield, the 80 m and the facing stay. So
        every "before" column of items 29 and 31 is the game with those three changes, not build 0.17.0.
      - *Crowd throttle of ship blasts saw only the first 0.27 s* (minor, real): a blast slot stays taken until one
        second after its blast, which is what `PlayShipDeath` counts.
      - *Kill feed over the TARGET panel under the larger map* (minor, real by reading; not reproduced): the feed
        shows as many rows as fit above half the screen height (at least two); the oldest wait out.
      - *Map: travelled line and sliding mark after a respawn in a sector that is not joined* (minor, real): only for
        a jump (one open portal apart, or the wormhole); a new ship elsewhere gets the rings alone.
      - Not changed: "KILLED BY ..." over the top of the fireball, "RESPAWN IN" on the failing hull for the first
        0.3 s, the fill cost of the fireball at 1920x1080 (not measured), the first-person branch of the death camera
        (not run).
    - **Normal** (`FHCJBotSkill::Get(1)`): `ArrivalGrace` 2 -> 3.5 s, `RespiteSeconds` 3 -> 5 s, `DamageToHumanScale`
      0.8 -> 0.55, `TorpedoOnHumanChance` 0.7 -> 0.45 (first step 3 / 4.5 / 0.65 / 0.5: 3.10 deaths a minute over three
      matches). Cap 2, rear wait 1.2 s and the rest as they were. The aiming pilot now dies 2.69 times a minute (six
      matches, 1.96 to 3.92) against 4.65 without the rules; 24 of 66 deaths within 12 s of the spawn against 43 of
      57; median 14 s alive against 5. The reviewer's 2 to 2.5 a minute is not reached: one Normal bot wins its duel
      with this autopilot in about ten seconds, and a pilot that shoots first has no grace. Not measured on Normal:
      the blind pilot.
    - **Final measurement** (`fair_matches.py`, as in item 29: one autopilot against seven bots, seed 99, 245 s a
      process, six at a time, headless editor binary; logs `Saved\Fair\final_pool`). The Easy and the baseline
      matches ran on the binary before the last change of Normal's numbers, which they do not read.

      | | matches / minutes | deaths a minute (deaths) | per match | within 12 s of the spawn | median s alive | rear share of bot damage | deaths by a bot in the rear arc | `peak_attackers` | held cap / rear / grace / respite / hunters |
      |---|---|---|---|---|---|---|---|---|---|
      | Easy, `-HCJNoFair`, aiming | 6 / 24.5 | 2.45 (60) | 1.96-3.18 | 20 | 18 | 3.5 % | 1 of 57 | 3 3 3 2 3 3 | 0 |
      | **Easy, fair, aiming** | 6 / 24.5 | **1.06** (26) = -57 % | 0.73-1.22 | 0 | 41 | 0.1 % | 0 of 26 | 2 2 2 2 1 2 | 1109 / 30 / 398 / 1544 / 44 |
      | Easy, `-HCJNoFair`, blind | 3 / 12.2 | 2.53 (31) | 2.45-2.69 | 15 | 13 | **45.7 %** | 15 of 30 | 2 2 2 | 0 |
      | **Easy, fair, blind** | 3 / 12.2 | **1.22** (15; 13 by bots) = -52 % | 0.73-1.71 | 0 | 31 | **3.0 %** | 0 of 13 | 1 1 1 | 473 / 231 / 151 / 770 / 31 |
      | Normal, `-HCJNoFair`, aiming | 3 / 12.2 | 4.65 (57) | 4.16-5.14 | 43 | 5 | 2.4 % | 2 of 57 | 2 2 2 | 0 |
      | **Normal, fair, aiming** | 6 / 24.5 | **2.69** (66; 63 by bots) = -42 % | 1.96-3.92 | 24 | 14 | 2.1 % | 1 of 63 | 2 2 2 2 2 2 | 111 / 83 / 688 / 704 / 6 |

      Easy: every one of the six matches of the aiming pilot is below 1.5 a minute; of the three of the blind pilot
      one is above (1.71: seven deaths, one of them by a turret). The baseline of this session is lower than item
      29's (2.45 against 3.44 a minute for the same pilot and switches): the matches of an afternoon differ by that
      much, so only columns of one session compare. No error line and no `stuck` / `out_of_bounds` / `nan` event in
      the 27 logs. `peak_attackers` 2 on Easy is the moment in which the bot the player shot comes in and the other
      has not let go yet (two bots had hit the pilot in its last 8 s in 4 of 26 deaths, against 20 of 60).
    - **Verification.** `HypeCubeJamEditor` (Win64 Development) built; **73 of 73** automation tests, twice (the second
      time on the final sources), exit code 0. Playthrough suite, editor binary, headless: **8 of 8**, 518 s, exit
      code 0 (`Saved\Playthrough\20261115_163415`; after it only comments and documents changed).

      | scenario | result | checks | what it saw |
      |---|---|---|---|
      | `match_cycle` | pass | 21 | match 1 won with 5 kills after 72 s; layouts 7C0CB60C -> 5671F10D |
      | `difficulty_easy` | pass | 29 | 48 kills, 61 portal / 9 wormhole jumps, 73 pickups |
      | `difficulty_normal` | pass | 30 | 60 kills, 70 / 9 jumps, 78 pickups |
      | `difficulty_hard` | pass | 31 | 73 kills, 57 / 10 jumps, 63 pickups |
      | `small_universe` | pass | 13 | 93 kills on four sectors; 76 turret shots, 4 turrets destroyed |
      | `net_host_client_late` | pass | 49 | client 2 kills / 3 deaths, 5.6 corrections per 100 s; late joiner 1 / 2, none, one rock already gone; 2 rocks destroyed |
      | `autopilot_hangar_match` | pass | 17 | cruiser chosen, PLAY, three matches; the player 2 kills / 4 deaths |
      | `all_matches_together` | pass | 20 | kills: phaser 141, torpedo 95, scatter 44, missiles 42, blaster 37, pulse 36, rail 23; 4 ships shot down by turrets, 9 turrets destroyed, 6 rocks |

      The same suite on the packaged build (`--binary packaged`, headless, after the zip was made; its logs go to the
      stage's `Saved\`, which the zip leaves out): **8 of 8**, 489 s, exit code 0 - 60 / 71 / 69 kills on easy /
      normal / hard, 98 on four sectors, client 1 kill / 2 deaths and late joiner 3 / 3 in the network scenario, 5
      rocks destroyed there, the autopilot player 3 kills / 3 deaths as a cruiser over three matches.
    - **Packaged** (the command of the README, "Packaged build"; 3 min 14 s): the game target compiled and linked, 1083
      packages in the cook (1054 cooked, 29 skipped for the platform; 1080 before - the three explosion materials are
      new and compiled for SM5 and SM6), 0 errors, 0 warnings. `docs\tools\make_zip.ps1`:
      `Packaged\mariuz_HypeCubeJam.zip` 585.0 MB (613,390,843 bytes), 883.9 MB unpacked, 48 files; `python -I` /
      `zipfile.testzip()`: nothing wrong, 48 entries, forward slashes, no `.pdb`.
      - Run from a copy extracted to `F:\work\HCJ_final_check` (not in the project), 1280x720, `-HCJNoEOS -HCJNoStats
        -HCJNoIntro`, ports 7821 to 7826: the title menu comes up with the Difficulty row first and its line; the row
        steps to Normal and back; PLAY starts a match on Easy with the rules in the log (`Fair: event=rules ...
        max_attackers=1 ...`), 64 to 88 fps averages over 4 s with a picture taken in each; the process quits by
        itself. One error line per log (the known CelestialVault message). On the 2 GB card two of the runs
        logged "video memory 89 to 229 MB over budget" in two of their intervals each, 0 in all the others.
      - EOS values (`web_secret_scan.py` on the extracted copy, with UnrealPak; it prints names and counts, never a
        value): loose files - 0 values, 1 hit for the section name (a string of the executable); inside
        `HypeCubeJam-Windows.pak` - 6 hits, all in the staged `Config/DefaultEngine.ini` (the five values of
        `[HypeCubeJam.EOS]` and the section name). That is how the Windows build has been since the client secret
        went into `DefaultEngine.ini` (the game signs in with it; the web build's scan found the same in the Windows
        stage). The logs of the packaged runs, of the fairness matches and of the suite: 0 hits.
    - Pictures (**packaged build**, 1280x720, the capture switch; numbered from 152 because 141-151 are item 30's):
      `Media/152_packaged_title_menu_difficulty_row_easy.png`, `153_..._normal.png`;
      `154_packaged_match_with_the_hypercube_map.png` (a match after PLAY, the autopilot flying);
      `155_packaged_enemy_behind_warning.png` (`peace`, `scene 5,dark`, `enemybehind 2500`);
      `156_packaged_hit_from_behind_with_enemy_behind.png` (the beam of that bot: the wide arc with its label, AFT
      SHIELD DOWN, both warnings); `157`-`160_packaged_own_ship_explosion_*.png` (slowed to 5 %, 0.2 / 0.4 / 0.7 /
      1.2 s after the death: the hull glowing with pops, flash and ring with the vignette, the fireball with the
      ring passing, cooling flames with glowing plates); `161_..._real_time_recap_from_behind.png` (at full speed,
      0.7 s after a bot's kill from behind, with the recap line); `162_packaged_death_camera_on_a_network_client.png`.
    - Weak, or not done.
      - Nobody has played this build. Everything about the bots is autopilot data; nothing was heard.
      - Normal is above the 2 to 2.5 deaths a minute the review asked for (2.69), and the blind pilot was not run
        there. Hard and Ace were not measured again.
      - The Easy numbers are the two autopilots'. The margin to 1.5 is 0.44 for the aiming pilot (26 deaths: about
        +-0.2) and 0.28 for the blind one (15 deaths: about +-0.3; one match of three above 1.5).
      - The kill-feed limit, the map after a respawn and the blast throttle were changed by reading; none of the
        three situations was produced in a run. `held_hunters` counts, but whether fewer bots crowd a player's
        sector was not measured.
      - The ENEMY BEHIND cue has a pitch of its own, not a sound of its own.
      - The death camera on a client was seen in four pictures of one run on one PC (no packet loss, no load); the
        cockpit view was not run.
      - In the pictures "KILLED BY ..." still lies over the top of the fireball, and the fireball is flat.
      - Every picture taken costs a frame of 0.1 to 0.6 s in the capture runs (the "worst frame" of the `Capture`
        lines); frame times of the packaged build without pictures were not measured in this session.

## 15 Nov 2026 (seventeenth session) - computer voice
32. "Computer voice hints": the ship computer speaks twelve short warnings to the local player. Prepared outside the
    project by two earlier agents (lines rendered, limiter and its test drafted and reviewed, hook points named); this
    session put it into the tree, built, and ran it. No replicated property, no RPC, no lobby key changed
    (`ProjectVersion` stays 0.2.0): a build with the voice joins a 0.18.0 host and the other way round.
    - **Lines** (`EHCJVoiceLine`, priority / cooldown): "All systems online." (20, once per run), "Fore / Aft / Port /
      Starboard shield down." (70, 30 s shared by the four), "Hull critical." (90, 60 s), "Torpedo incoming." (100,
      30 s), "Missile locked on." (95, 30 s), "Enemy behind." (60, 60 s), "Gravity well. Boost away." (92, 30 s),
      "Tractor beam. Boost free." (80, 30 s), "Sector clear." (30, 90 s).
    - **Voice**: Flite (Carnegie Mellon University), voice cmu_us_rms, rendered offline with the library that ships
      with the engine's TextToSpeech plugin and processed by a script (band-pass, comb, ring modulation, chirp). The
      Windows voice (Zira) was rendered too and is not shipped: no licence term for its output was found; Flite's
      BSD-style licence is on disk and its text is now in `docs/NOTICES.txt`. Twelve WAVs (44.1 kHz, 16-bit, mono,
      1.04-2.16 s) in `SourceAssets/Voice`, imported by `docs/tools/import_voice.py` to
      `/Game/HCJ/Audio/Voice/VO_Computer_*` on the interface sound class (first run: 12 imported, 0 failed, 0 errors,
      0 warnings). Credit "Voice: Flite speech synthesiser, Carnegie Mellon University" on the SETTINGS page, in
      `README.txt`, `NOTICES.txt`, `ASSETS.md` and the project README.
    - **Not nagging** (`FHCJVoiceLimiter`, `Public/Audio/HCJVoiceLogic.h`, plain C++): nothing in the first 2.5 s of
      a ship; one line at a time, a line of priority 90 or more may cut a lower one off; 5 s of silence after a line
      (1.5 s before an urgent one); the cooldowns above; a line that is not urgent is refused once 3 lines started in
      the last 60 s, every line once 5 did. A refused request is dropped, never queued. One limiter per process (file
      static in `HCJAudioDirector.cpp`, like the hint director), clocked by game time.
    - **Wiring**: `UHCJAudioDirector::RequestVoice` (own `UAudioComponent`, not the shared interface one-shots, whose
      concurrency group would steal it) and `TickVoice`, called before `TickLocalPlayerCues` so that "Hull critical."
      holds the low-hull beep back 1.5 s. `TickVoice` speaks edges of the local ship's state: a facing's collapse time
      moved, hull under the low-hull mark (armed again above half), inside the no-escape radius of a black hole,
      held by a tractor beam, 1.5 s after the own kill nobody else in the sector, the first ship of the run. Three
      requests sit next to existing cues: the HUD's torpedo warning (one per torpedo), the HUD's ENEMY BEHIND cue
      (item 31's condition: the nose on the own ship for 0.3 s, at most every 4 s), the controller's missile warning.
      Silent on the title menu (no ship), after the own ship is destroyed (a line being spoken fades out in 0.15 s),
      during the intermission, on a dedicated server.
    - **Settings**: SETTINGS > "Computer voice (spoken warnings)" On / Off (`UHCJUserSettings::bComputerVoice`, on by
      default, saved like the other rows); `hcj.Audio.Voice 0 / 1` overrides it; `VoiceVolume=1.0` and twelve
      `+VoiceLines=` in `DefaultGame.ini`. The voice follows the Interface volume.
    - **Deviations from the brief**: (1) a refused request is logged too - `Voice: VO_Computer_X declined, <reason>
      (n spoken, m declined so far)` - for which the limiter remembers why it said no (`EHCJVoiceRefusal`); nothing
      is logged while the voice is switched off. (2) A second test, `HypeCubeJam.Audio.VoiceLinesConfig`: the twelve
      configured paths are the table's names in its order, under `/Game/HCJ/Audio/Voice`, and load as waves of 0.8 to
      2.5 s. (3) `docs/tools/voice_stats.py` (counts from logs). (4) `lines.json` was copied from `prep\voice\`, not
      from `final_flite\` (it is not in that folder). Limiter rules, table, hook points and file places as in the
      brief.
    - **Web (Wasm)**: nothing is compiled out and nothing is platform-specific - the lines are ordinary sound waves
      under `/Game/HCJ` (always cooked), played by the audio mixer like the music and the cues; no text-to-speech
      runs in the game. Checked by compiling: the game target `HypeCubeJam Wasm Shipping`
      (`Build.bat`, Emscripten from `F:\work\tools\emsdk`) compiled the changed files and linked
      `Binaries\Wasm\HypeCubeJam-Wasm-Shipping.js` / `.wasm` - "Result: Succeeded", exit code 0. It was not cooked,
      packaged or run in a browser in this session.
    - **Verification.** `HypeCubeJamEditor` (Win64 Development) built, 0 errors, 0 warnings. **75 of 75** automation
      tests, exit code 0 (73 + `HypeCubeJam.Audio.VoiceLimiter` + `HypeCubeJam.Audio.VoiceLinesConfig`). Playthrough
      suite, editor binary, headless: **8 of 8**, 519 s, exit code 0 (`Saved\Playthrough\20261115_173339`):

      | scenario | result | checks | what it saw |
      |---|---|---|---|
      | `match_cycle` | pass | 21 | match 1 won with 5 kills after 75 s; layouts 7C0CB60C -> 5671F10D |
      | `difficulty_easy` | pass | 30 | 60 kills, 46 portal / 2 wormhole jumps, 58 pickups |
      | `difficulty_normal` | pass | 30 | 73 kills, 58 / 12 jumps, 60 pickups |
      | `difficulty_hard` | pass | 31 | 64 kills, 73 / 13 jumps, 73 pickups |
      | `small_universe` | pass | 14 | 91 kills on four sectors; 67 turret shots, 1 turret destroyed, 1 rock |
      | `net_host_client_late` | pass | 49 | client 1 kill / 4 deaths, 14.4 corrections per 100 s; late joiner 0 / 2, 0.9; 5 rocks destroyed |
      | `autopilot_hangar_match` | pass | 17 | cruiser chosen, PLAY, three matches; the player 4 kills / 1 death |
      | `all_matches_together` | pass | 20 | kills: phaser 137, torpedo 78, missiles 56, pulse 46, scatter 39, blaster 35, rail 33 |

      (14.4 corrections per 100 s on the client is above item 31's 5.6; the scenario's limit was met. Not looked
      into: the voice adds no movement code and nothing to the network.)
    - **Measured in a match** (`docs/tools/voice_stats.py`). Editor binary, rendered in a 1280x720 window (the HUD's
      two requests need a HUD), listen host, the aiming autopilot against seven bots on Easy with the fairness rules,
      seed 99, no limits, 320 s, `-HCJNoEOS -HCJNoStats`, port 7910, 92-98 fps (`Saved\Voice\easy_rendered_1.log`):
      **17 lines spoken in 317 s = 3.2 a minute, at most 5 inside any 60 s; 30 requests declined** (cooldown 15, "3
      lines in the last minute" 7, another line being spoken 7, silence after a line 1). Spoken: missile lock 5,
      torpedo 3, fore shield 2, hull critical 2, tractor beam 2, starboard shield 1, sector clear 1, welcome 1; one of
      the torpedo lines cut "Missile locked on." off. Declined most: fore shield 10, missile lock 10. The pilot had
      6 kills and 5 deaths. 0 error lines.
      - The same autopilot headless in the suite (`autopilot.log`, no HUD, so no torpedo / enemy-behind requests):
        9 spoken in 226 s = 2.4 a minute, at most 4 in 60 s, 14 declined. The network client of the suite: 8 spoken
        in 195 s, 17 declined - the voice runs on a client as on a host.
      - "Enemy behind." was asked for twice in the Easy match and declined both times (a line was being spoken; 3
        in the last minute): the aiming autopilot turns towards whoever attacks it. A scripted run (`peace`, `scene
        5,dark`, `enemybehind 2500`, 45 s, port 7911): `Voice: VO_Computer_EnemyBehind (2 spoken ...)` in the same
        frame as the `Threat warning:` line, then seven requests 4 s apart, each `declined, cooldown of the line`.
      - "Gravity well. Boost away.": `-HCJHoleTest=0.3` (port 7912): spoken 0.5 s after the ship was put 1800 uu
        from the hole, just before it crossed the horizon.
      - Never spoken in any run: "Aft shield down." and "Port shield down." (both were requested and declined on
        the network client). They go through the same code as the fore and starboard lines.
    - Weak, or not done.
      - **Nobody has heard the voice**: whether the words are understood ("Port shield down" was the weak one in the
        speech-recogniser check of the preparation), the level against music and effects (`VoiceVolume`), whether
        the chirp in front is pleasant, whether the line that is cut off sounds wrong - all unknown. That a wave
        reached the audio device is not in the log either: the log says the limiter said yes and `Play` was called.
      - The SETTINGS row and the credit line were not looked at in a picture and the row was not clicked; switching
        the voice off was run only in the unit test (`bEnabled` false), not through the menu or the console variable.
      - 3.2 lines a minute is the aiming autopilot on Easy; a person who is hit from all sides, or Normal / Hard,
        was not measured. The cap of 5 in 60 s holds by the unit test (two ten-minute worst cases).
      - An urgent line can be refused by the 1.5 s of silence after any line: in one run "Torpedo incoming." was
        declined 1.7 s after "Fore shield down." had started. The torpedo's beep and HUD line still came.
      - "Enemy behind." shares item 31's weakness: it also fires for a bot that aims at somebody else on the same line.
      - Not packaged in this session (Windows or web); the voice in a browser was not run.

## 15 Nov 2026 (eighteenth session) - Epic Online Services extras that need no portal work
33. "Brief B" of the preparation (`result_2.md`): three additions to the online part that use what the Developer
    Portal already has. Nothing replicated, no RPC, no change to the eleven lobby attributes of 0.18
    (`ProjectVersion` stays 0.2.0, network version FFB5210D as before).
    - **Pilot record.** `UHCJEOSSubsystem::RefreshPilotRecord`: `EOS_Stats_QueryStats` for the local user's five SUM
      stats, at the first sign-in, when LEADERBOARDS opens and on its REFRESH. Shown as "Your record:  3 kills  -  57
      deaths  -  K/D 0.1  -  1 win in 6 matches  -  0 turrets" in three places: a second live line under the status
      line of the title menu, a "YOUR RECORD" strip above the two boards, a line under the stats line of the
      end-of-match board. An accepted ingest is added at once. Not known = nothing drawn.
      - **Was the query permitted? Yes.** Live service, this PC, `-HCJEosBucket=HCJ_TEST_1 -HCJNoStats`, every one of
        the seven processes of the new build that signed in, verbatim: `EOS: pilot record: 3 kills  -  57 deaths  -  K/D 0.1
        -  1 win in 6 matches  -  0 turrets  (EOS_Success, 4 of 5 stats held by the service: kills 3, deaths 57, wins
        1, turrets 0, matches 6)`. So the client policy allows a player to read its own stats, and `HCJ_KILLS`,
        `HCJ_DEATHS`, `HCJ_WINS`, `HCJ_MATCHES` exist under those names. `HCJ_TURRETS` came back "not found": this user
        never destroyed a turret in a reporting run, so that name is still unproven. The numbers agree with the
        boards (3 kills, 1 win on both).
      - The run without `-HCJNoStats` that the brief allowed was not needed and not made: the switch stops the
        ingest only, the query runs with it. Nothing was written to the live stats in this session (every process
        logged `EOS: stats not sent (-HCJNoStats): ...`; no `sending stats` line in any log).
      - If the policy refuses (`EOS_AccessDenied`, `EOS_MissingPermissions`, `EOS_InvalidAuth`): one line, `EOS: pilot
        record not read (<result>): the client policy of this product does not allow reading stats; the menus show
        kills and wins from the leaderboards instead, and the query is not tried again in this run`, and the three
        places show "12 kills (rank 3)  -  2 wins (rank 5)" from the local rows of the two boards
        (`HCJEos::DescribeBoardRecord`), or nothing when the player is on neither. Any other failure keeps what was
        shown and is asked again at the next occasion. **This path never ran** (the query was permitted); its text
        is unit-tested, the branch is not.
    - **Open-matches counter.** While the title page is up the search of FIND GAMES runs every 45 s
      (`SetTitleMenuVisible` from the controller once a second; not while a search or Quick Play runs). The status
      line becomes "Online as User  -  1 open match, 2 pilots flying" or "... -  no open match right now: QUICK PLAY
      hosts one". Deviation taken from the review: counted after `FilterJoinableLobbies`, so the number is what FIND
      GAMES lists. A failed search or an answer older than 135 s gives the old "signed in" text.
    - **Richer FIND GAMES rows.** Four optional lobby attributes: `HCJDIFF` (0-3, sent when the host knows it),
      `HCJFRAG`, `HCJEND` (unix seconds of the end of the match on the host's clock, 0 = no limit or intermission),
      `HCJPILOTS` (the other humans; whole names, at most 64 printable ASCII characters, "+N" for those that did not
      fit, "-" for nobody). The MAP cell reads "4D - 16 sectors  -  Hard" (only with bots), and a dim line under the
      row "first to 25  -  about 9 min left  -  with User-2". `FromAttributes` never requires them and treats a wrong
      type or value as "not said". 15 attributes now (limit 64).
      - Problem: the first layout put that line into the host column; "with User-2" ran into the PLAYERS column.
        It is a line of its own across the row now (picture 172 is the second build).
      - Problem: for its first two seconds a joiner is called by its machine name (the engine's default until the
        name of its settings arrives), and the host advertised that for one update (`... with DESKTOP-...`, then
        `with User-2`). A pilot is listed 5 s after it joined now; the second run shows `2/16 players` first and
        `with User-2` 4 s later.
    - **Old builds.** Run against the 0.18.0 package of item 31 (`Packaged\Temp\Windows`, Development, same switches):
      (1) new host, old client with `-HCJQuickPlay`: `lobby search: EOS_Success, 1 listed`, `lobby listed: 'User' 1/16
      players, 5 bots, 4D - 16 sectors, seed 1234, port 7931, version FFB5210D`, `Welcomed by server`, `Quick play:
      joined the match of 'User' at 127.0.0.1:7931`; the host: `2/16 players ... with User-2`. (2) old host, new
      client: `lobby listed: 'User' 1/16 players, 2 bots, 4D - 16 sectors, seed 5, port 7932, version FFB5210D,
      rules: not said`, `open matches: ... 1 open match, 1 pilot flying`, joined, 46 s until the host quit.
    - **Live test, one PC** (editor binary with `-game`; host and joiner `-nullrhi`, the browsing instance in a
      1280x720 window with the capture switch; all `-HCJEosBucket=HCJ_TEST_1 -HCJNoStats`; ports 7911-7934; both
      instances with EOS are the same product user; logs in `Saved\BriefB`):
      - Host (`?FragLimit=25?TimeLimit=600?Bots=6?Difficulty=2`): `EOS: lobby advertised: 'User' 1/16 players, 5 bots,
        4D - 16 sectors  -  Hard, port 7911, 3 LAN address(es), public address not set, rules: first to 25  -  about
        10 min left`. No `lobby attribute ... refused`, `lobby update failed` or `Search failed` line in any of the
        eleven logs of these runs: the service takes the four attributes.
      - Browser on the title page, 6 s after its start: `lobby search: EOS_Success, 1 listed`, `open matches: Online
        as User  -  1 open match, 2 pilots flying` (a second instance had joined the host by address); 75 s later,
        after that one had left, `1 open match, 1 pilot flying`; the host quit at 16:17:51 UTC and the browser's next
        search, 45 s after its last, logged `0 listed` and `no open match right now: QUICK PLAY hosts one`. Searches
        on the title page were 45 s apart (16:16:17, 16:17:03, 16:17:48, 16:18:36, 16:19:21).
      - End of a match (`HCJMenuCmd endmatch`, rendered host): the board shows "Stats not sent (switched off for this
        run)" and under it the record line.
    - Pictures (editor binary, 1280x720, capture switch, live service, test bucket):
      `Media/171_title_menu_open_matches_and_pilot_record.png`, `172_find_games_richer_row.png`,
      `173_leaderboards_your_record.png`, `174_title_menu_no_open_match.png`,
      `175_end_of_match_board_your_record.png`. 171, 173 and 174 are from the first build of the session (the two
      fixes above touch neither page).
    - `-HCJEosDemo` rows carry the new fields (the first row is a 0.18 host without them) and a made-up record.
    - **Web**: `HCJEOSSubsystemWeb.cpp` got the stubs (`RefreshPilotRecord`, `CountOpenMatches` empty,
      `GetOpenMatches` -1, `GetPilotRecordLine` empty). The Wasm target was NOT built in this session.
    - **Verification.** `HypeCubeJamEditor` (Win64 Development) built, 0 errors, 0 compiler warnings. **77 of 77**
      automation tests, exit code 0 (75 + `HypeCubeJam.Online.LobbyRowExtras` - round trip of the four, each one
      missing, a 0.18 lobby, junk types, a hostile 400-character `HCJPILOTS`, the pilot list of fifteen names, the
      5 s tolerance, the counter texts and that only joinable lobbies count - and `HypeCubeJam.Online.PilotRecord`).
      Playthrough suite, editor binary, headless, default ports 7791 upwards:
      **8 of 8**, 516 s, exit code 0 (`Saved\Playthrough\20261115_182902`, the final build):

      | scenario | result | checks | what it saw |
      |---|---|---|---|
      | `match_cycle` | pass | 21 | match 1 won with 5 kills after 71 s; layouts 7C0CB60C -> 5671F10D |
      | `difficulty_easy` | pass | 29 | 59 kills, 53 portal / 7 wormhole jumps, 59 pickups |
      | `difficulty_normal` | pass | 30 | 75 kills, 57 / 11 jumps, 71 pickups |
      | `difficulty_hard` | pass | 31 | 63 kills, 68 / 11 jumps, 60 pickups |
      | `small_universe` | pass | 14 | 88 kills on four sectors; 83 turret shots, 0 turrets destroyed, 2 rocks |
      | `net_host_client_late` | pass | 49 | client 2 kills / 2 deaths, 11.1 corrections per 100 s; late joiner 2 / 4, 8.2; 5 rocks destroyed |
      | `autopilot_hangar_match` | pass | 17 | cruiser, PLAY, three matches; the player 4 kills / 3 deaths |
      | `all_matches_together` | pass | 20 | kills: phaser 146, torpedo 77, missiles 52, scatter 48, blaster 43, pulse 35, rail 32 |

      (The suite runs with `-HCJNoEOS`: it says that nothing else broke, not that the online part works.)
      The client secret is in none of the logs of this session (string compare in a script, value not printed).
    - Weak, or not done.
      - Not packaged (as told); the Wasm target not built; nothing was clicked - pages were opened with `HCJMenuCmd`.
      - Two different EOS users were not tested (one PC = one product user): "pilots flying" and "with ..." were
        seen with a second local instance only.
      - The "policy refuses" fallback never ran. `HCJ_TURRETS` was not read back (no value on the service).
      - The record grows by an accepted ingest only by code and by its unit test: no stats were sent, so the line
        was never seen to change.
      - "about N min left" compares the host's clock with the viewer's; a host whose clock is minutes off shows a
        wrong time (anything over or beyond a day is not shown). The row text is as old as the page (REFRESH).
      - The counter line on the LEADERBOARDS page repeats the title page's last count (up to 135 s old).
      - A title page left open asks the service every 45 s for as long as it is up.

## 15 Nov 2026 (nineteenth session) - review of the voice and the online extras, fixes, packaged
34. Items 32 and 33 were read by three reviewers (voice, online, regression): no blocker, no major, eleven minor
    findings (two of them twice). This session judged each against the code, fixed all of them, found one more in a
    picture, built, tested and packaged. No replicated property, no RPC, no lobby key changed (`ProjectVersion` stays
    0.2.0, network version FFB5210D): the build joins 0.18.0 hosts and the other way round as in item 33.
    - **Findings and what was done** (all real by reading; none was reproduced before its fix).
      - *"Sector clear." after shooting a turret, or with turrets still firing* (minor): the check is armed only by
        the kill of a ship (the victim "a station turret" is skipped), and a live `AHCJTurret` in the sector counts
        as company.
      - *"Sector clear." withheld by a cloaked ship* (minor: the voice gave the cloaker away): a cloak-hidden ship
        does not count, as in the HUD's "enemies in sector".
      - *"Sector clear." about a sector the fight was not in* (minor): the sector of the own ship at the kill is
        remembered; the line is said only when the ship is still in it 1.5 s later.
      - *A line being spoken finished after the voice was switched off, or over the end-of-match board* (minor):
        `TickVoice` fades it out in 0.15 s, once, with a log line `Voice: the line being spoken is faded out (the
        voice was switched off | the match is over)`.
      - *"Missile locked on." over the death camera* (minor): `RequestVoice` refuses without a live own ship.
      - *"All systems online." lost for the run when its request was refused by the switch or the intermission*
        (minor): it is asked once the voice is on and a match runs.
      - *Pilot record counted a match twice, or stepped back, when a stats query and an ingest overlapped* (minor):
        a query waits for an ingest in flight; an ingest that starts or answers while a query runs makes that
        answer unused and asks again instead of adding its numbers.
      - *`EOS_InvalidAuth` treated as "the client policy forbids reading stats" for the rest of the run* (minor):
        only `EOS_AccessDenied` and `EOS_MissingPermissions` are refusals; anything else is asked again with the
        title page's next search and after the next sign-in.
      - *The open-matches text on every page and in the Quick Play log line* (minor): `GetTitleStatusLine` is the
        title page's; `GetStatusLine` is the plain sign-in text again (picture 184: the LEADERBOARDS header).
      - *The 45 s search of the title page logged every time* (minor): a search started by that timer logs its
        result only when it differs from the last logged one (result, counts, lobby ids and their players).
      - *The record line fell back to the leaderboard rows whenever the query had not answered* (minor, reported
        twice): only after a refusal; otherwise nothing is drawn until the query answers.
      - Found in a picture of the packaged build (not by a reviewer): the MAP cell of FIND GAMES cut "Normal" off
        behind the JOIN button ("4D - 16 sectors  -  Norma"). The column is 275 wide instead of 230 (picture 183).
      - `HCJEosLogic.cpp`: the signed / unsigned comparison a reviewer named for the Wasm compiler has a cast.
      - `docs/tools/playthrough.py`: the port limit was the fixed number 7850, so `--port` above 7790 ran out of
        ports; it is the first port + 60 now.
    - **Verification.** `HypeCubeJamEditor` (Win64 Development) built, 0 errors, 0 warnings. **77 of 77** automation
      tests, exit code 0, twice (the second time on the final sources).
      - Playthrough suite, editor binary, headless, `--port 7910`: first run **6 of 8**, exit code 1 - the two
        failures verbatim: `net_host_client_late     FAIL        1       0  the scenario raised RuntimeError: out of
        ports (7790-7850)` and the same line for `autopilot_hangar_match` (the runner's fixed port limit, see
        above; neither scenario started a process). After the fix of the runner the two alone: **2 of 2**, 273 s,
        exit code 0 (`Saved\Playthrough\20261115_185130` and `..._185940`). These ran before the MAP column was
        widened (one constant of the menu); the editor suite was not run again after it.

        | scenario | result | checks | what it saw |
        |---|---|---|---|
        | `match_cycle` | pass | 21 | match 1 won with 5 kills after 32 s; layouts 7C0CB60C -> 5671F10D |
        | `difficulty_easy` | pass | 29 | 50 kills, 46 portal / 12 wormhole jumps, 56 pickups |
        | `difficulty_normal` | pass | 31 | 74 kills, 50 / 8 jumps, 61 pickups |
        | `difficulty_hard` | pass | 30 | 58 kills, 62 / 17 jumps, 66 pickups |
        | `small_universe` | pass | 14 | 88 kills on four sectors; 190 turret shots, 2 turrets destroyed, 1 rock |
        | `net_host_client_late` (second run) | pass | 49 | client 1 kill / 3 deaths, 5.0 corrections per 100 s; late joiner 2 / 0, 0.9; 5 rocks destroyed |
        | `autopilot_hangar_match` (second run) | pass | 17 | cruiser, PLAY, three matches; the player 5 kills / 2 deaths |
        | `all_matches_together` | pass | 20 | kills: phaser 113, torpedo 59, rail 40, pulse 37, blaster 34, missiles 23, scatter 14 (six scenarios) |

      - The same suite on the final packaged build (`--binary packaged --port 7920`, headless, after the zip was
        made): **8 of 8**, 492 s, exit code 0 (`Saved\Playthrough\20261115_192932`) - 49 / 54 / 72 kills on easy /
        normal / hard, 92 on four sectors, client 0 kills / 3 deaths with 15.6 corrections per 100 s and late joiner
        2 / 3 with 9.1 in the network scenario (the Wasm link ran beside it), the autopilot player 3 kills / 2
        deaths over three matches; layouts 3976D716 -> C9934379, the packaged build's as in item 31. The voice in
        those logs: host 11 spoken / 32 declined, client 8 / 28, late joiner 3 / 14, autopilot 7 / 27.
      - **Wasm**: the game target `HypeCubeJam Wasm Shipping` (`Build.bat`, Emscripten from `F:\work\tools\emsdk`)
        compiled the sources of items 32 to 34 and linked: "Result: Succeeded", exit code 0, one linker warning
        (`-pthread + ALLOW_MEMORY_GROWTH may run non-wasm code slowly`, as before). This rewrote
        `Binaries\Wasm\HypeCubeJam-Wasm-Shipping.*`. Not cooked, not packaged, not run in a browser; `PackagedWeb`
        untouched.
    - **Packaged** (the command of the README, twice - the second after the MAP column; 2 min 58 s and 1 min 30 s):
      1095 packages in the cook (1066 cooked, 29 skipped for the platform; 1083 in item 31 - the twelve voice waves
      are new: all twelve `/game/hcj/audio/voice/vo_computer_*` are in the cook's `ReferencedSet.txt`), 0 errors, 0
      warnings. `docs\tools\make_zip.ps1`: `Packaged\mariuz_HypeCubeJam.zip` 585.2 MB (613,600,520 bytes), 884.2 MB
      unpacked, 48 files; `python -I` / `zipfile.testzip()`: nothing wrong, 48 entries, forward slashes, no `.pdb`.
    - **The packaged build run** from a copy extracted to `F:\work\HCJ_final_19` (not in the project), 1280x720,
      `-HCJNoIntro`, ports 7911 to 7916 and 7985 / 7986.
      - Easy match (`-HCJNoEOS -HCJNoStats`, title menu, SETTINGS, PLAY by `HCJMenuCmd`, the aiming autopilot, 215
        s; the first of the two packages, whose voice code is the final one): `Fair: event=rules ... level=easy`;
        **10 lines spoken and 14 declined in 198 s = 3.0 a minute, at most 5 in any 60 s** (`voice_stats.py`).
        Spoken: welcome, enemy behind, fore shield 2, torpedo, missile lock, sector clear, gravity well, hull
        critical, tractor beam. Declined: cooldown of the line 8, silence after a line 4, "3 lines in the last
        minute" 2. A spoken line is only logged when its wave loaded, so the twelve waves load from the pak. 48 to
        61 fps averages over 4 s with a picture in each and "video memory 275 to 371 MB over budget" in most
        intervals (item 31: 64 to 88 fps; other programs were open on this PC). One error line (the known
        CelestialVault message).
      - Voice switched off while a line is spoken (final package, `hcj.Audio.Voice 0` by a capture command 0.6 s
        after `Voice: VO_Computer_Welcome`): `Voice: the line being spoken is faded out (the voice was switched
        off)` once, no `Voice:` line until `hcj.Audio.Voice 1` 4.5 s later, lines again after it.
      - Menu pictures with `-HCJEosDemo` (made-up rows, `EOS: -HCJEosDemo: ... no service contacted`).
      - Live service, test bucket (`-HCJEosBucket=HCJ_TEST_1 -HCJNoStats`, four rendered runs of the title page, three with a
        second headless instance hosting, and one headless pair): every process signed in and logged `EOS: pilot record: 3 kills  -  57
        deaths  -  K/D 0.1  -  1 win in 6 matches  -  0 turrets  (EOS_Success, 4 of 5 stats ...)`; `open matches:
        ... no open match right now: QUICK PLAY hosts one`, then `1 open match, 1 pilot flying` 45 s after the
        search before it, `1 open match, 2 pilots flying` after the join; `Quick play: started (Online: signed in
        to Epic Online Services as User)` (the plain text again). `stats not sent (-HCJNoStats)` in every one; the
        lobbies of the test bucket were removed at exit. The client secret is in none of the logs (count of the
        key name per log: 0).
      - **Somebody was using this PC during these runs**: the game window takes the keyboard and the mouse when
        it opens. In all four rendered live runs a match was started that no command asked for (QUICK PLAY five
        times in three runs, PLAY once in the run whose window was asked to open off the screen - the Enter key), in
        one FIND GAMES was opened, and a picture 36 s into one run shows the HANGAR page with a button under the mouse. `MenuQuickPlay`
        has three callers (the button, `HCJMenuCmd quickplay`, `-HCJQuickPlay`), none of them a timer. So no run
        sat on the title page long enough for two equal searches in a row: **that a repeated search of the timer
        is not logged was not seen in a run.**
    - Pictures (**packaged build**, 1280x720, the capture switch; 176-180 not used):
      `Media/181_packaged_settings_computer_voice_row.png` (the row, the credit line),
      `182_packaged_title_menu_open_matches_and_record_demo_rows.png`, `183_packaged_find_games_rows_demo_rows.png`
      (difficulty in the MAP cell, the line under the row; a 0.18 host's row below it),
      `184_packaged_leaderboards_demo_rows.png` (plain sign-in header, YOUR RECORD strip). 182 to 184 are made-up
      rows; the title page with the live counter was not caught in a picture.
    - Weak, or not done.
      - **Nobody has heard the voice.** Nothing about wording, level, the chirp, a cut-off or a faded line is known
        by ear; the log says the limiter said yes and `Play` was called.
      - Run after their fix: the voice-off fade only. By reading only: the fade at the intermission, the refusal
        without a live ship, the three changes of "Sector clear." (it was spoken once in the Easy match; no turret,
        cloak or portal case was staged), the query / ingest order (no stats were sent), `EOS_InvalidAuth`, the
        record line while the query has not answered, the quiet timer search.
      - The SETTINGS row was seen, not clicked; the title page's live counter and the LEADERBOARDS page with the
        live record were not looked at in a picture of this build (171-175 are item 33's editor pictures).
      - Two different EOS users, the refused-query fallback and `HCJ_TURRETS` are as open as in item 33.
      - The web build was compiled and linked only. The published web build is still 0.18.0 without the voice.
      - The frame rate of the packaged match was a quarter below item 31's on a PC that was in use; not measured
        again on a quiet machine.

## 15 Nov 2026 (twentieth session) - web build of 0.19.0 rebuilt
35. The web (Wasm) build made again from commit e256fa8 (0.19.0, tree clean) by item 28's procedure, to replace the
    older one on GitHub Pages. Nothing was committed, pushed or uploaded.
    - Package: `PackagedWeb\Ship` deleted, then the Shipping BuildCookRun of item 28 (log
      `PackagedWeb\logs\bcr_ship4.log`): exit code 0, BUILD SUCCESSFUL, 30 min 34 s (compile, link and wasm-opt
      1189 s; the cook 69 s, shaders from the cache). Cook: 1095 packages, 1032 cooked, 63 skipped for the platform,
      "Success - 0 error(s), 0 warning(s)". One warning in the whole log, the linker's known
      `-pthread + ALLOW_MEMORY_GROWTH` line.
    - Stage `PackagedWeb\Ship\Wasm`: 56 files, 300.7 MB with the `.br` copies; without them 33 files, 261.6 MB.
      Wasm 142 437 357 bytes (26.5 MB as Brotli), container `.ucas` **99 492 320 bytes** (99 332 944 before: the
      voice waves and explosion materials cost 0.16 MB; 507 680 bytes are left under 100 000 000), pak 9.2 MB.
    - `check_stage.py`: all passed, release scan 0 findings (1 warning, the stock engine font path, as before).
      `web_secret_scan.py` with UnrealPak: 56 staged files and 1077 extracted files (84 ini), 0 hits, RESULT clean;
      the staged `DefaultEngine.ini` has 7 sections, none of them EOS.
    - `docs/tools/web_fixtures.json` changed: the PLAY click of `hcj_click_play` and `hcj_click_soak` moved from
      y 365 to y 425. The Difficulty row and its line sit above PLAY since 0.18.0; the old click hit that line and
      the first run stayed on the title page (the harness still said PASS: it only checks READY and errors - look
      at the pictures).
    - Browser (harness, Chrome 157, off-screen window, deferred tier, `hcj_click_play`):
      `PackagedWeb\runs\ship4_deferred` (engine server, port 8090): PASS, READY after 6.0 s, 0 WebGPU errors, no
      error line; `title.png` shows the Difficulty row first (Easy, "One bot at a time. ...") above PLAY;
      `match_a..c.png` show a 4-D match with 6 bots, the hypercube map, the kill feed with bot kills, the player's
      ship killed twice and respawned. `PackagedWeb\runs\pages4_noheaders` (the GitHubPages folder from
      `web_plain_server.py`, port 8091, no isolation headers): "PAGE ISOLATION reload under the service worker",
      READY after 5.6 s, PASS, the match runs ("HIT FROM BEHIND" in a picture). Both servers stopped.
    - `PackagedWeb\GitHubPages` (deleted, then `web_split_for_pages.py`, exit 0): **36 files, 261 619 568 bytes
      (261.6 MB)**, the wasm in three parts (50 + 50 + 42.4 MB), the container 99.5 MB as it is, no `.br`,
      `.nojekyll` there, no file of 100 000 000 bytes or more.
    - Not verified: the forward tier (`?tier=forward`), the 10-minute soak, sound and the computer voice by ear (no
      log line of the game reaches the browser console), the explosion stages in a picture, the in-match difficulty
      row, the site on GitHub Pages itself, other browsers. `PackagedWeb\itch` and the itch zip are still the old
      build. The container is 0.5 MB under the Pages limit: the next content needs texture cuts or a second chunk.

## 15 Nov 2026 (twenty-first session) - game controller
36. "Add support for My Steam Controller (2015) and xbox controller if is enabled" (the player). The game had no gamepad
    binding at all. From commit b7b92f9 (0.19.0). No replicated property, no RPC, no lobby key changed (`ProjectVersion`
    stays 0.2.0): the build joins 0.19.0 hosts. Nothing was committed, pushed or uploaded. **No controller was used in
    this session: everything below was shown with injected key events, console commands and pictures.**
    - **What "Steam Controller" means here.** It has no Windows driver: Steam Input presents it as an XInput pad (game
      added to Steam as a non-Steam game, "Gamepad" template) or as mouse and keys (desktop layout). So the work is
      XInput bindings plus the two recipes in the README and in `docs/README.txt`. No Steamworks SDK, nothing downloaded.
    - **Bindings** (`AHCJPlayerController::BuildInputObjects`): a second mapping context `IMC_HCJ_Gamepad`, built in code
      like the keyboard's, with the same actions and six of its own. Left stick thrust / strafe (dead zone 0.2 per axis),
      right stick aim, A / B up / down, RT fire, LT torpedo, LB boost, RB next target, Y nearest enemy, X phaser /
      blaster, D-pad up warp (hold), left cloak, right tractor (hold), down drop weapon, R3 camera, L3 controls overlay,
      Back scoreboard (hold), Start menu, A respawn when destroyed (RT too: fire always did). Keyboard and mouse are
      untouched and work at the same time. The triggers are bound as the engine's threshold buttons, not as axes (a
      trigger resting a hair above zero would hold the phaser on).
    - **Aim stick** (`HCJPad::LookDelta`): radial dead zone 0.2, rescaled, squared, 150 degrees a second at full
      deflection times "Controller sensitivity", times the frame time (capped at 0.1 s). The mouse path (a step per
      count) is as it was. No gameplay value changed.
    - **"If is enabled"**: SETTINGS has "Controller (Xbox / Steam Controller)" On / Off (`bController`, default On) and
      "Controller sensitivity" 0.2 x to 3 x (`ControllerSensitivity`). Off removes the pad's mapping context
      (`SyncPadContext`, every frame; log line `Controller: gamepad input off`) and makes the menu swallow pad keys.
    - **Menus** (`SHCJMenu::Navigate`): every button, stepper and slider is an entry of a focus list; D-pad, left stick
      (`HCJPad::FStickNav`: a step, then a repeat), arrow keys and Tab move a yellow frame, left / right change a
      stepper or slider, A / Enter press, B / Start / Esc go back. Title page: the focus starts on PLAY (A = PLAY).
      Buttons and sliders no longer take Slate's keyboard focus (the menu widget keeps it, so a pad key always arrives
      there); Slate's own navigation is switched off (`OnNavigation` returns Explicit to nobody). The entries are sorted
      by their place in the widget tree after a page is built: they register while the page is built, and the order in
      which a compiler evaluates a chain of `+ Slot()[...]` is unspecified (MSVC happened to go top to bottom; the Wasm
      build is clang).
    - **Prompts**: with the pad as the device used last the HUD's key caps show pad names (`HCJPad::ToPadMarkup` in
      `AHCJHUD::DrawKeyMarkup`; the F1 overlay has its own pad table, the respawn line says "[RT] or [A]"), and the
      menu legends switch to the pad's. CONTROLS has a third column with the pad layout.
    - **Changed for keyboard users** (deliberate, small): the arrow keys and Tab now move the menu's own focus (Tab used
      to walk Slate's focus through buttons and text boxes); after an arrow key, Enter presses the framed entry
      instead of always PLAY / RESUME; a click on a button no longer leaves the keyboard focus on it. SETTINGS rows are
      2 units closer together (two rows more in the same height).
    - **Tests.** Four new automation tests, `HypeCubeJam.Input.*`: the stick's dead zone, curve and frame-time scaling
      (72 directions); the switch, the sensitivity clamp and the device in use; the focus steps and the stick repeat;
      the pad names of the prompts. **81 automation tests.**
      - First full run: **80 of 81**, exit code 255. The failure, verbatim: `Test Completed. Result={Fail}
        Name={OpenMatchKeepsMoving} Path={HypeCubeJam.Playthrough.OpenMatchKeepsMoving}` - `Expected 'no bot sat still
        for 20 s' to be 0, but it was 2.` Two bots (`Bot-06`, `Bot-03`, both `retreating=1 pad=1`, 175 uu apart in
        sector 15) sat nose to nose for 20 s: the unexplained case of item 27 ("Two bots sat nose to nose for 21 s
        once"). The test world has bots only and no player controller; nothing of this item runs in it. The two
        playthrough tests alone, twice: pass, pass.
      - Second full run, on the final sources: **81 of 81, exit code 0.**
    - **Shown without a controller.**
      - `HCJMenuCmd padtest` (new, not in shipping builds): presses every pad key through `APlayerController::InputKey`
        (`FInputKeyEventArgs::CreateSimulated`), a step every ten frames, and checks which handlers ran and what the
        ship or the controller did - thrust axes for the left stick (and none inside the dead zone), yaw / pitch for the
        right stick (and none inside it), `IsFiring` on RT and off at its release, boost, the scoreboard flag, the menu
        opening on Start, nothing at all with the switch off, fire again with it on, the respawn request on A without a
        ship. Editor binary, headless: `PadTest: result=PASS steps=30 checks=36 failed=0`, three times (two headless
        runs, one rendered). The packaged build, headless: the same
        `PadTest: result=PASS steps=30 checks=36 failed=0` (36 `ok=1` lines, no `ok=0`).
      - `HCJMenuCmd nav <up|down|left|right|accept|back>` (new): what the pad does in the open menu, with a log line
        naming the entry in focus. Headless, the in-match menu: RESUME -> down -> "Bot difficulty (applies now)" ->
        right (difficulty 1) -> left (0) -> HANGAR -> SETTINGS -> accept (the SETTINGS page, focus on "Mouse
        sensitivity") -> down ("Controller (Xbox / Steam Controller)") -> up twice (wraps to BACK) -> back (the menu,
        RESUME) -> up (QUIT GAME) -> back (menu closed). Rendered (the title menu does not open under `-nullrhi`):
        title page, down from PLAY to HANGAR; back on the title page "accept" on PLAY started the match.
      - One rendered run of the editor binary (1280x720 window at the top left of the desktop, 69 s from start to
        exit, about 45 s of it with the window up - somebody was at this PC and the window takes keyboard and mouse
        when it opens): pictures of the title page with the frame on HANGAR, SETTINGS with the two new rows, CONTROLS
        with three columns (it fits 1280x720 with about 75 px to spare at each side), the HUD with pad names
        ("[D-left]" in the cloak hint, "[LT]", "[D-up] WARP", "[X] switch to blaster"), the F1 overlay with the pad
        table, the in-match menu with the pad legend and the frame on RESUME. The pictures are in
        `Saved/Screenshots/HCJCapture` (not kept in `Media`). After them the frame of a focused stepper row was
        changed from a solid yellow band (the value could not be read on it) to a faint one; that was not looked at
        again.
    - **Verification.** `HypeCubeJamEditor` (Win64 Development) built, 0 errors, 0 warnings of the project.
      - Playthrough suite, editor binary, headless, `--port 7910`: **8 of 8**, 519 s, exit code 0
        (`Saved\Playthrough\20261115_222321`). It ran before two last menu-only changes (the sorting of the focus
        list; Esc on CONNECTING back to what it did); the editor suite was not run again after them.
      - Second full automation run (above): 81 of 81 on the final sources.
      - **Packaged** (the command of the README): exit code 0, BUILD SUCCESSFUL, 192 s; 1095 packages in the cook (1066
        cooked, 29 skipped for the platform), 0 errors, 0 warnings. `docs\tools\make_zip.ps1`:
        `Packaged\mariuz_HypeCubeJam.zip` 585.2 MB (613,631,179 bytes), 884.2 MB unpacked, 48 files; `python -I` /
        `zipfile.testzip()`: nothing wrong, 48 entries, forward slashes, no `.pdb`; the `README.txt` in it has the
        controller section.
      - The packaged build, headless (`-nullrhi`, port 7934): the in-match menu walked with `nav` as above (the same
        entries in the same order), `padtest` PASS; one error line, the known CelestialVault message.
      - Playthrough suite on the packaged build (`--binary packaged --port 7940`, headless): **6 of 8**, 490 s, exit
        code 1 (`Saved\Playthrough\20261115_224003`). The two failures, verbatim (shortened after the first fields):
        `difficulty_easy          FAIL       29     234  easy: 1 "stuck" event(s), first: {'event': 'stuck', 'name':
        'Bot-05', 't': '30', 'seconds': '21', 'sector': '6', ... 'state': '0', 'speed': '1320', 'type': 'ftr', ...
        'pad': '1', ... 'boosting': '1', ...}` and `difficulty_normal        FAIL       31     235  normal: 1 "stuck"
        event(s), first: {'event': 'stuck', 'name': 'Bot-03', 't': '123', 'seconds': '21', 'sector': '3', ... 'state':
        '2', 'speed': '28', 'type': 'sct', ... 'retreating': '1', ... 'pad': '1', ... 'wander_point': '1', ...
        'cloaked': '1', ...}`. A bot that stayed within 2.5 m for 21 s on its way to a pickup pad, once in each of the
        two matches (Bot-05 from its ninth second, pressing on at 1320 uu/s; it went on to kill and die as usual).
        The four bot scenarios again (`--only difficulty_easy,difficulty_normal,difficulty_hard,match_cycle --port
        7950`): **5 of 5** (the four and the sum), 242 s, exit code 0 (`Saved\Playthrough\20261115_224946`).
      - **What the stuck bots say about this change - not settled.** No `stuck` event is in the logs of the thirteen
        suite runs kept from the sessions before; this session has three incidents (the automation test above, these
        two). The code of this item is input, menu and HUD of the local player: the automation world has no player
        at all, and in a bots-only process the local player sits in the (headless) title menu, where the new menu
        code ticks but touches no ship. No path from it to a bot's movement was found by reading, all three bots
        were flying to a pad, and the rerun was clean - but the 0.19.0 build was not run side by side for a rate
        (a copy is in `F:\work\HCJ_final_19`).
    - **Not verified - nobody has held a controller on this game.**
      - Whether an Xbox pad or a Steam Controller is seen at all by the packaged build (the engine's `XInputDevice`
        plugin is enabled by default; no device was attached or looked for), the feel of the aim stick (150 degrees a
        second, squared curve, no invert option, no smoothing), whether RT / LT arrive as the threshold buttons on
        every pad, stick drift, vibration (none is sent).
      - The left stick in the menus on real hardware (`OnAnalogValueChanged` is assumed to reach the menu widget before
        the viewport); the D-pad's key repeat; Start opening the menu and the same press not closing it again (key
        repeats of A, B and Start are ignored for this - by reading only); A / B in a menu while a text box has the
        keyboard focus.
      - Steam Input: the non-Steam-game recipe and the desktop layout are written from how Steam Input works, not
        from a try.
      - The mouse: no menu page was clicked since the buttons stopped taking the keyboard focus (a click handler does
        not depend on it); the focus frame going away when the mouse moves was not seen.
      - Pad prompts: the device switch was only driven by injected events; "Middle mouse" (hold the target) and "H"
        (hints) have no pad button and keep their keyboard names in the prompts.
      - HANGAR, FIND GAMES, LEADERBOARDS, QUICK PLAY and CONNECTING were not walked with `nav` (same code as the pages
        that were); text boxes (seed, pilot name, address) cannot be filled with a pad.
      - The web (Wasm) build was not compiled: nothing Windows-only was added, the only new engine calls are Enhanced
        Input, Slate and `FInputKeyEventArgs` (the last one in non-shipping code only).

## 15 Nov 2026 (twenty-second session) - web build of 0.20.0 rebuilt
37. The web (Wasm) build made again from commit 2a4f1ea (0.20.0, the game controller; tree clean) by item 28's
    procedure, as in item 35. Nothing was committed, pushed or uploaded.
    - Package: `PackagedWeb\Ship` deleted, then the Shipping BuildCookRun (log `PackagedWeb\logs\bcr_ship5.log`):
      exit code 0, BUILD SUCCESSFUL, 23 min 30 s (compile, link and wasm-opt 910 s; the cook 66.5 s). **The
      controller code compiled for Wasm as it is - no source change.** Cook: 1095 packages, 1032 cooked, 63 skipped
      for the platform, "Success - 0 error(s), 0 warning(s)". One warning in the log, the linker's known
      `-pthread + ALLOW_MEMORY_GROWTH` line.
    - Stage `PackagedWeb\Ship\Wasm`: 56 files, 300 780 996 bytes with the `.br` copies; 33 files without them. Wasm
      142 485 106 bytes (26.6 MB as Brotli; +47 749 bytes of code), container `.ucas` **99 492 320 bytes**, the same
      as in item 35 (no new content; 507 680 bytes under 100 000 000), pak 9 158 691.
    - `check_stage.py`: all passed, release scan 0 findings (1 warning, the stock engine font path).
      `web_secret_scan.py` with UnrealPak: 56 staged files and 1077 extracted files (84 ini), 0 hits, RESULT clean.
    - Browser (harness, off-screen Chrome, deferred tier, `hcj_click_play`, fixture unchanged - the PLAY click at
      y 425 still hits): `PackagedWeb\runs\ship5_deferred` (engine server, port 8090): PASS, READY after 4.4 s,
      0 WebGPU errors, no error line in the console log; `title.png` is the title page as before (Difficulty row,
      PLAY, HANGAR, Bots, Hypercube, seed, pilot name, CONTROLS / SETTINGS / QUIT, keyboard prompts); `match_a..c.png`
      show a 4-D match with bots (HUD, hypercube map, kill feed, enemy markers, the player's ship killed twice).
      `PackagedWeb\runs\pages5_noheaders` (the GitHubPages folder from `web_plain_server.py`, port 8091): "PAGE
      ISOLATION reload under the service worker", compatibility check passed after the reload, READY after 5.7 s,
      PASS, the match runs. Both servers stopped. The harness needs the engine's Python
      (`Engine\Binaries\ThirdParty\Python3\Win64\python.exe`; the system Python has no `playwright`).
    - `PackagedWeb\GitHubPages` (deleted, then `web_split_for_pages.py`, exit 0): **36 files, 261 667 317 bytes**,
      the wasm in three parts (50 000 000 + 50 000 000 + 42 485 106), the container 99 492 320 as it is, no `.br`,
      `.nojekyll` there; no file of 100 000 000 bytes or more (so none near 100 MiB = 104 857 600 either).
    - Not verified: **a controller in the browser** (none attached; no pad input was sent, and the yellow focus
      frame is not visible on PLAY in `title.png` - only its green highlight), the SETTINGS page and its controller
      row on the web, the forward tier, the 10-minute soak, sound by ear, the site on GitHub Pages itself, other
      browsers. `PackagedWeb\itch` and the itch zip are still the old build (item 28).

## 16 Nov 2026 (twenty-third session) - Easy for a beginner, pickups that can be seen
38. "Improve the easy mode gameplay , make the health/weapons packs more visible" (the player, after two more matches
    on Easy against 7 bots). From commit b32461f (0.20.0, tree clean). No replicated property, no RPC, no lobby key
    changed (`ProjectVersion` stays 0.2.0); the changes to Easy are the server's, the pads' new looks and the HUD are
    local. Layout checksum of seed 1234 unchanged (7C0CB60C, playthrough suite). Nothing was committed, pushed or
    uploaded.
    - **What the player's two logs say** (`F:\work\HCJ_push\play19\...\Saved\Logs`, build 0.19.0 with the fairness
      rules of items 29 / 31, Easy, 7 bots; `fair_stats.py` and the `Kill:`, `Pickups t=`, `Local t=` lines).

      | | match 1 (9.2 min) | match 2 (8.7 min) |
      |---|---|---|
      | kills / deaths | 1 / 11 (10 at its last `event=human` line) | 4 / 9 |
      | deaths a minute | 1.20 | 1.03 |
      | killed by | bots 8 (blaster 3, rapid pulse 2, torpedo 2, scatter 1), out of the sector 2, a turret 1 | bots 9 (homing missiles 3, scatter 3, rapid pulse 2, phaser 1) |
      | by a bot in the rear arc / within 12 s of the spawn | 0 / 0 | 0 / 0 |
      | bearing of the killer, seconds alive | - | 6 to 34 degrees off the nose; 39 to 82 s, median 47 |
      | pads emptied in the match, by bots | 128, 127 | 105, 103 |
      | `own ship:` in the 15 s status lines | PHASER, no ammunition, no power-up in all 35 | PHASER in 30, RAPID PULSE in 2 |

      - The rules of item 29 do what they were made for: one bot at a time, from the front, nobody dies at the spawn.
        What is left is a duel, nose to nose, that the player loses after three quarters of a minute.
      - The player took one pad in the first match and two in the second, the bots 230. In the second match eight of
        the nine deaths came from a weapon a bot had taken from a pad; the player fought with the phaser. The
        low-hull hint ("a REPAIR pad is in this sector. Follow the green marker") was shown three times in each
        match, which is its limit - and the green marker only existed while hints were on.
      - The player's kills: 2 by torpedo, 1 blaster, 1 phaser in match 2. An Easy bot breaks off at 45 % hull and runs
        for a repair pad; a beginner's phaser does not finish it.
      - The same pilot the measurements use (the aiming autopilot) has 1.3 kills for every death on that Easy; the
        player 0.1 and 0.44. The autopilot is a far better shot than a beginner.
    - **A. Easy: help for a beginner** (`FHCJBotSkill`, three new fields next to the fairness fields, "off" on Normal,
      Hard and Ace; `README.md`, "Bots and people", has the table). Server only. Bots among themselves are untouched.
      - *A person's hits do 1.5 times their damage to a bot's ship* (`DamageFromHumanScale`;
        `AHCJShipCharacter::TakeDamage`, where the bots' damage on a person is already scaled: every weapon, each
        phaser step, the torpedo). The duel ends before the bot can run.
      - *Bots do 40 % of their damage to a person* instead of 50 % (`DamageToHumanScale`).
      - *The hull mends out of combat*: 4 points a second once nothing has taken hull or shield from the ship for
        5 s, up to 75 % of the full hull (`HumanHullRegenRate`, `HCJBalance::StepHullRegen`, `HullRegenDelay`,
        `HullRegenCeiling`; `AHCJShipCharacter::ServerTickHullRegen` - "hit" is any loss of hull or shield since the
        last tick, so a station, the void and a development command count like a weapon). The last quarter is what
        repair pads are for. With the 6 s of respite after a fight this is 20 to 40 points between two fights.
      - *A repair pad makes the ship whole*: 100 instead of 60 (`HumanRepairScale`, `ServerGiveItem`).
      - The line under the Difficulty row: "One bot at a time, from the front. Your guns hit harder, your hull
        mends." (73 characters; the test allows 73).
      - `-HCJClassicEasy` (`AHCJGameMode::IsClassicEasy`, `FHCJBotSkill::ClearBeginnerHelp`): Easy without these four
        - Easy as 0.20.0 had it, in the same binary; it changes nothing on the other levels (tested). `-HCJNoFair`
        clears them with the rest. `AHCJGameMode::GetFairSkill` is now the one place where the two switches are
        applied to the row; the bots, the damage scales and the `Fair: event=rules` line read it.
      - Not built: slower or wider bot fire at a person (the aim of an Easy bot is 7 degrees wide already, and the
        deaths are few once the duel is shorter), more forgiving torpedoes (two of the player's five kills were
        torpedoes: they work).
      - Telemetry: `Fair: event=rules ... classic_easy= human_damage_scale= hull_regen= repair_scale=`;
        `Fair: event=status ... human_kills= human_dmg= human_dmg_bonus= hull_regen= repairs= repair_points=` (damage
        people did to bots as applied and the part the scale added, hull points mended, repair pads people took and
        what they restored). `fair_stats.py` prints them, and the own kills a minute and kills over deaths;
        `fair_matches.py` has `<tag>:<level>:classic`.
    - **Measured** (`fair_matches.py`, as in items 29 and 31: one aiming autopilot - `-HCJAutoFly -HCJAutoAim
      -HCJAutoTorpedo` - against seven bots, `?Bots=8`, seed 99, no limits, 245 s a process, six at a time, three
      "before" and three "after" side by side in each batch, headless editor binary; "before" is the same binary with
      `-HCJClassicEasy`; logs `Saved\Fair\item38_a`, `item38_b`, `item38_normal`). Batch A ran on the binary with the
      Easy changes only, batch B on the final one.

      | Easy, aiming pilot | matches / minutes | kills (a minute) | deaths (a minute) | kills over deaths | kills per match | deaths per match | median s alive at death | bot damage applied (before the scale) | own damage on bots (of it the bonus) | hull mended | kills in the match, all ships |
      |---|---|---|---|---|---|---|---|---|---|---|---|
      | before, batch A | 3 / 12.1 | 14 (1.15) | 13 (1.07) | 1.08 | 3 4 7 | 4 5 4 | | 2617 (5232) | 5613 (0) | 0 | 185 |
      | after, batch A | 3 / 12.1 | 29 (2.39) | 7 (0.58) | 4.14 | 9 12 8 | 3 2 2 | | 2096 (5240) | 6007 (2002) | 303 | 192 |
      | before, batch B | 3 / 12.2 | 18 (1.47) | 11 (0.90; 9 by bots, 2 by a turret) | 1.64 | 7 8 3 | 5 2 4 | | 2258 (4515) | 5896 (0) | 0 | 193 |
      | after, batch B | 3 / 12.2 | 29 (2.37) | 7 (0.57) | 4.14 | 9 9 11 | 2 2 3 | | 2232 (5581) | 6588 (2196) | 256 | 178 |
      | **before, both** | 6 / 24.3 | **32 (1.32)** | **24 (0.99)** | **1.33** | 3 to 8 | 2 to 5 | 38 | 4875 (9747) | 11509 (0) | 0 | 378 |
      | **after, both** | 6 / 24.3 | **58 (2.39)** | **14 (0.58)** | **4.14** | 8 to 12 | 2 to 3 | 76 | 4328 (10821) | 12595 (4198) | 559 | 370 |

      - Read. Kills over deaths 1.33 -> 4.14; kills a minute +81 %, deaths a minute -41 % (target: clearly better
        kills over deaths, deaths a minute not higher). Every "after" match has at least as many kills as the best
        "before" match (8) and at most 3 deaths. 14 deaths put the "after" rate at 0.58 +- 0.15, 24 the "before" rate
        at 0.99 +- 0.2. It is still a fight: the pilot is destroyed every 100 s on average, 14 times in six matches
        (scatter cannon 8, homing missiles 4, rail 1, blaster 1), and the bots deal as much raw damage as before
        (10821 against 9747 before the scale: the pilot lives longer and is shot at longer).
      - The pilot's damage on bots before the scale is lower than before (8397 = 12595 - 4198, against 11509), for
        nearly twice the kills: 145 raw points a kill against 360. The hull mended 93 points a match (28 a life,
        20 lives): this pilot never stops fighting. It took **no repair pad** in any of the twelve matches (`repairs=0`), so the 100-point repair
        was not exercised by the measurement (see the check below). The bots still fight each other: 370 against 378
        kills in the matches.
      - No error line in the 18 logs of the three batches; one `event=stuck` (a Normal match, Bot-07 for 20 s at 37
        uu/s next to its target Bot-04, 242 uu apart, on the way to a pad: the case of items 27 and 36).
      - **Normal is unchanged.** By the code: `FHCJBotSkill::Get(1)` was not touched and has the three new fields at
        "off" (unit test, and `ClearBeginnerHelp` changes nothing above Easy); `GetFairSkill` returns the same row as
        the two lines it replaced in `AHCJBotController::Think`; the new branch of `TakeDamage` multiplies by 1; the
        hull tick returns at a rate of 0; the repair scale is 1. The `Fair: event=rules` line of a Normal match is
        the one of item 31 plus `classic_easy=0 human_damage_scale=1.00 hull_regen=0.0 repair_scale=1.00`. By
        measurement (six Normal matches of this binary at a time, three of them with `-HCJClassicEasy`, which is a
        no-op there): 2.94 deaths a minute (36; per match 3.18, 2.20, 3.44) and 2.78 (34; 2.20, 2.94, 3.18), own
        kills 7 and 10, bot damage 4917 and 4998 at the scale 0.55, `peak_attackers` 2 in all six, `hull_regen=0`,
        `human_dmg_bonus=0` in all six, and the one repair pad the pilot took restored 60 points (`repairs=1
        repair_points=60`). Item 31 measured 2.69 (six matches, 1.96 to 3.92). Hard and Ace: by the code and the
        unit tests only.
      - **Repair pad and hull mending, by command** (headless editor binary, three processes side by side, bots held
        still: `padview ...,20` sets the hull to 20, `HCJGive 0` three seconds later is a repair pad, then the hull is
        set to 20 again and left alone for 16 s; `Saved\Item38\repair`). Easy: `repairs=1 repair_points=80` (20 ->
        100) and `hull_regen=44` (20 -> 64: 11 s at 4 a second after the 5 s wait). Easy with `-HCJClassicEasy` and
        Normal: `repair_points=60`, `hull_regen=0`.
    - **B. Pads that can be seen.** Until now a pad was a disc of 4.2 m with a 1.5 m shape over it in a sector 520 m
      across, its marker appeared within 120 m, and the radar had a small diamond per pad.
      - *Kinds* (`HCJ::GetItemKind`, `GetItemKindColor`, `GetItemShortName`): **health** (repair, shield cell) green,
        **weapons** (scatter, rail, missiles, pulse, torpedoes) amber, **power-ups** (overdrive, quad damage, cloak)
        magenta. The pad, its column, its halo, its HUD marker and its radar icon have the kind's colour; the
        turning shape keeps the item's own.
      - *In the world* (`AHCJPickup`, `HCJPickupLook`): pad 6.2 m, shape 2.3 m (half as big again). Over a filled pad
        a column of light 42 m high - a cylinder with the pads' glow material, opaque, so that a bright nebula
        behind it does not wash it out - and around the shape a pulsing halo (the ships' glint material with its
        dark backing, a quad turned to the view). Both keep a size on the screen: the column is at least 0.8 % of
        the distance to the view wide (5 pixels at 1280x720; 0.7 m next to the view, 5.2 m at most), the halo quad at
        least 9 %. An empty pad has neither. Cost: two more mesh components per pad (64 pads in a 16-sector match,
        about five of them in view), no light, no particle system; per frame and filled pad one distance, one
        rotation and one scale for the halo, the column's scale only when it changes by 3 %; the view is asked for
        once a frame for all pads; nothing for a pad more than 900 m away (another sector).
      - *HUD* (`AHCJHUD::DrawPickupMarkers`, `DrawPickupIcon`): the six nearest filled pads within 450 m (all pads of
        the sector; 120 m before) have a marker - cross / diamond / triangle by kind - with a short name and the
        distance ("REPAIR  120 m"; beyond 50 m in steps of 5 m), smaller and fainter with the distance (strength 1
        up to 70 m, 0.7 from 260 m; the text never below 0.9), not on a HUD panel. **Hull below 35 %**: the nearest
        REPAIR pad (a SHIELD cell when the sector has no repair) is drawn larger and pulsing inside a ring; out of
        view, three chevrons on the edge ellipse point to it with name and distance on a dark plate. This was a
        part of the hint system (circle and two chevrons, only with hints on); it now belongs to the markers and is
        always there. No string is built per frame: the label of a marker slot is made again only when its pad,
        item or shown distance changes (`FPickupLabel`, `FString::Reset` + `Appendf`); the nearest six are kept in
        an array on the stack (`HCJUi::InsertNearest`).
      - *Radar*: green cross, amber diamond, magenta triangle; the pad the HUD points at is larger, pulses inside a
        ring and sits on the rim when it is beyond the radar's 160 m.
      - `docs/README.txt` says what the colours mean.
    - **Tests.** Three new automation tests - `HypeCubeJam.Balance.BeginnerHelp` (Easy alone has the help and still
      hurts, the classic switch gives back Easy's old row and changes no other level, the hull mending rule),
      `HypeCubeJam.Pickups.Visibility` (every item has a kind and a short name of at most 9 characters, the three
      colours are far from each other and from the red of an enemy, column and halo by distance),
      `HypeCubeJam.UI.PickupMarkers` (the nearest-N list against a full sort over 200 random streams, marker
      strength, distance text) - and the changed checks of `Balance.BotDifficulty` (Easy 0.4; `ClearFairness` also
      clears the help). **84 automation tests.**
      - First full run (sources before the pictures were tuned): **84 of 84**, exit code 0.
      - Second full run: **83 of 84**, exit code 255. The failure, verbatim: `Test Completed. Result={Fail}
        Name={Visibility} Path={HypeCubeJam.Pickups.Visibility}` - `Expected 'column at 100 m: still its own width'
        to be 70.000000, but it was 80.000000 and outside tolerance 0.000100.` The test's own number was stale: the
        column's minimum share of the distance had been raised from 0.6 to 0.8 % after the first pictures. The check
        now asks at 80 m.
      - Third full run, final sources: **84 of 84**, exit code 0 (`OpenMatchKeepsMoving` passed in all three).
    - **Verification.** `HypeCubeJamEditor` (Win64 Development) built, 0 errors, 0 warnings of the project.
      - Playthrough suite, editor binary, headless, `--port 7910`: **8 of 8**, 514 s, exit code 0
        (`Saved\Playthrough\20261116_123441`). It ran before the last two changes (a dark plate behind the pointer's
        label in the HUD; the test above). Again on the final sources: **8 of 8**, 513 s, exit code 0
        (`Saved\Playthrough\20261116_132047`). The table is the first run's.

        | scenario | result | checks | what it saw |
        |---|---|---|---|
        | `match_cycle` | pass | 21 | match 1 won with 5 kills after 85 s; layouts 7C0CB60C -> 5671F10D |
        | `difficulty_easy` | pass | 30 | 50 kills, 45 portal / 9 wormhole jumps, 57 pickups |
        | `difficulty_normal` | pass | 31 | 68 kills, 66 / 5 jumps, 61 pickups |
        | `difficulty_hard` | pass | 31 | 63 kills, 78 / 8 jumps, 66 pickups |
        | `small_universe` | pass | 13 | 92 kills on four sectors; 86 turret shots, 1 turret destroyed |
        | `net_host_client_late` | pass | 49 | client 0 kills / 3 deaths, 6.7 corrections per 100 s; late joiner 1 / 5, 5.5 per 100 s and one rock already gone; 4 rocks destroyed (host with `-HCJNoFair`) |
        | `autopilot_hangar_match` | pass | 17 | cruiser chosen, PLAY, three matches to the limit of 4 (Normal); the player 1 kill / 2 deaths |
        | `all_matches_together` | pass | 20 | kills: phaser 119, torpedo 95, missiles 55, blaster 45, rail 43, scatter 34, pulse 24; 8 ships shot down by turrets, 6 turrets destroyed, 13 rocks |

      - **Packaged** (the command of the README; log `Saved\Item38\bcr.log`): exit code 0, BUILD SUCCESSFUL, 211 s;
        1095 packages in the cook (1066 cooked, 29 skipped for the platform - no new content), 0 errors, 0 warnings.
        `docs\tools\make_zip.ps1` (exit code 0): `Packaged\mariuz_HypeCubeJam.zip` 585.2 MB (613,646,422 bytes), 884.2
        MB unpacked, 48 files; `python -I` / `zipfile.testzip()`: nothing wrong, 48 entries, forward slashes, no
        `.pdb`; the `README.txt` in it has the pad colours and the new Easy lines.
      - Playthrough suite on the packaged build (`--binary packaged --port 7940`, headless; final sources): **7 of
        8**, 489 s, exit code 1 (`Saved\Playthrough\20261116_130121`). The failure, verbatim: `net_host_client_late
        FAIL       49     254  late joiner: no kill and no death on the host's board`. The host's log has both: the
        late joiner (Lena) lived 103 s, was destroyed at t = 203 s (`Fair: event=death t=203 name=Lena killer=Clive`)
        and destroyed the client at t = 209 s (`Kill: Lena destroyed Clive (PHASER)`), then its process ended; the
        board is written every 15 s and its last line for Lena (0 / 0) is older than both. The host of this scenario
        runs with `-HCJNoFair`, which switches the help of this item off with the rules. The scenario again (`--only
        net_host_client_late --port 7950`): **1 of 1**, 254 s, exit code 0 (`Saved\Playthrough\20261116_131013`:
        client 5 kills / 2 deaths, late joiner 2 / 3). The other seven of the first run: 63 / 80 / 67 kills on easy /
        normal / hard, 97 on four sectors, match 1 won after 65 s, the autopilot player 6 kills / 0 deaths as a
        cruiser over three matches (Normal).
      - **0.20.0 and this build join each other** (headless, both ways, 95 s of host and 62 s of client each, Easy,
        6 bots, both on the aiming autopilot; "0.20.0" is a copy of the zip stage of item 36 made before this
        package; `Saved\Item38\join`). New host, 0.20.0 client: `Player joined ... (remote) -> 2 humans, 4 bots`, the
        client's six status lines have `ship=1` and the host's layout checksum (3976D716), it knows the pads of its
        sector (`3 pickup pads known`), 0 kills / 0 deaths in its minute; the host counts the client's and its own
        damage with the bonus (`human_dmg=539 human_dmg_bonus=180`). 0.20.0 host, new client: joined, `ship=1`, same
        checksum, 1 kill / 1 death, `5 pickup pads known, 5 hold an item`. Exit code 0 of all four processes; one
        error line per log, the known CelestialVault message. Not looked at: what the new client draws (headless).
    - Development aids added: `-HCJClassicEasy`, `HCJMenuCmd padview <uu>[,<item A>[,<item B>[,<yaw>[,<hull>]]]]`
      (listen host, with `peace`: the own ship that far from the nearest filled pad of its sector, looking at it;
      items put on that pad and the one next to it; the ship turned away; the hull set),
      `AHCJPickup::ServerDebugSetItem` (not in shipping builds), the `classic` mode of `fair_matches.py`, the help
      and kill columns of `fair_stats.py`.
    - Pictures (editor binary with `-game`, a 1280x720 window, the capture switch every 2.5 s, bots held still with
      `peace`, the ship placed by `padview`; seed 1234, Easy; four runs of 40, 31, 38 and 22 s of match - the first
      two to look at the first version and to find that a picture taken in the frame of a `padview` shows the chase
      camera half way round; 185, 190 and 191 are from the last run, the others from the third, which lacks only the
      dark plate of the pointer's label):
      `Media/185_pickup_pads_from_far_away_columns_and_halos.png` (a cloak, a shield cell and a rapid pulse pad at
      160 to 225 m: magenta, green and amber columns with halos and labels),
      `186_health_and_weapon_pads_side_by_side_shield_and_missiles.png` (80 m each, three more pads behind at 130 to
      220 m; the radar with its icons),
      `187_power_up_pad_overdrive_and_hud_markers.png` (magenta column and triangle at 60 m; hull 27: the repair pad
      at 135 m in its ring),
      `188_low_hull_repair_and_scatter_pads_against_a_bright_nebula.png` (hull 17; the columns stay green and amber
      on the white core of a nebula),
      `189_low_hull_repair_marker_emphasised.png` (hull 25: "REPAIR  90 m" large, the cross in a ring, the same
      ringed cross on the radar, the hint line),
      `190_low_hull_pointer_to_repair_pad_behind.png` and `191_low_hull_pointer_to_repair_pad_on_the_right.png` (the
      pad out of view: three green chevrons with "REPAIR  90 m" on its plate; in 190 two enemy arrows lie next to
      it),
      `192_radar_pad_icons_enlarged_4x_crop_of_189.png` (the radar of 189, cut out and enlarged four times without
      smoothing). In 187 and 188 the hull was low because the first `padview` of that run had put the ship outside
      the sector's safe radius (RETURN TO SECTOR), not by a command; in 186 and 191 the hull bar reads 46 and 35
      where a command had set 25 ten and seven seconds earlier - the hull mending at work.
    - **Not verified.**
      - Nobody has played this build. Every number about Easy is the aiming autopilot's, which aims far better than
        the player whose logs started this item (1.3 kills per death on the old Easy against the player's 0.1 and
        0.44). For that pilot Easy is now lopsided (4.1 kills per death); what it is for a beginner is a guess: the
        player's damage is multiplied by 1.5 and the bots' by 0.8 of before. The blind pilot of item 29 was not run.
      - The hull mending and the 100-point repair were seen working in headless runs and in the hull bar of two
        pictures; how they feel (whether 4 points a second is noticed at all) is untested. A hull that mends has no
        sound and no HUD cue of its own - the bar simply rises.
      - Twelve Easy matches of 4 minutes: a match has 2 to 5 deaths, so one match says little; the two batches agree
        (kills over deaths 4.14 in both, by chance to the last digit: 29 / 7 twice).
      - The pads' new looks were judged from eight still pictures of the editor binary on one PC, in three sectors.
        Not seen: the columns in motion, a pad taken and refilled (the column going out and coming back), the pulse
        of the halo, a network client (the looks are local and read the replicated item and flag, as before), the
        packaged build rendered (it ran headless only), 1920x1080, the mobile renderer of the web build. From
        straight above or below a column is seen end-on and only the halo is left. The columns stand along world Z
        whatever the ship's "up" is.
      - Frame cost was not measured against 0.20.0: no rendered A/B run was made (somebody was using the PC). The
        capture runs logged 49 to 118 fps averages over 2.5 s with a picture taken in each interval, which says
        nothing either way.
      - With five pads in view the screen carries five more labels than before in a fight; whether that is too much
        was not judged by anybody who plays.
      - The low-hull pointer and the enemy arrows share the edge ellipse and can lie on each other (picture 190).
      - The web (Wasm) build was not compiled: the new code uses nothing Windows-only (`GFrameCounter`, canvas
        lines, two mesh components).
      - Easy over the network with a second person: the help applies to every ship flown by a person, as the
        fairness rules do; only the headless join test above ran it.

## 16 Nov 2026 (twenty-fourth session) - web build of 0.21.0 rebuilt
39. The web (Wasm) build made again from commit cf0543b (0.21.0, easier Easy and visible pickups; tree clean) by
    item 28's procedure, as in items 35 and 37. Nothing was committed, pushed or uploaded.
    - Package: `PackagedWeb\Ship` deleted, then the Shipping BuildCookRun (log `PackagedWeb\logs\bcr_ship6.log`):
      exit code 0, BUILD SUCCESSFUL, 35 min 52 s (compile, link and wasm-opt 1617 s - the PC was in use; the cook
      121 s). **The new code of item 38 compiled for Wasm as it is - no source change.** Cook: 1095 packages, 1032
      cooked, 63 skipped for the platform, "Success - 0 error(s), 0 warning(s)". One warning in the log, the
      linker's known `-pthread + ALLOW_MEMORY_GROWTH` line.
    - Stage `PackagedWeb\Ship\Wasm`: 56 files, 300 794 564 bytes with the `.br` copies; 33 files, 261 676 413 bytes
      without them. Wasm 142 494 582 bytes (26.6 MB as Brotli; +9 476 bytes), container `.ucas` **99 492 320
      bytes**, the same as in items 35 and 37 (the columns and halos use existing meshes and the pads' material;
      507 680 bytes under 100 000 000), pak 9 158 707.
    - `check_stage.py`: all passed, release scan 0 findings (1 warning, the stock engine font path).
      `web_secret_scan.py` with UnrealPak: 56 staged files and 1077 extracted files (84 ini), 0 hits, RESULT clean.
    - Browser (harness, off-screen Chrome, deferred tier, `hcj_click_play`, fixture unchanged):
      `PackagedWeb\runs\ship6_deferred` (engine server, port 8090): PASS, READY after 8.0 s, 0 WebGPU errors, no
      error line in the console log; `title.png` shows the new Easy line ("One bot at a time, from the front. ...")
      and a yellow pad with its column behind the menu; `match_a..c.png` show the pickups of item 38: a yellow
      column on a PULSE pad with its diamond marker and distance, green columns on REPAIR and SHIELD pads (cross
      markers, "REPAIR 140 m"), the pad icons on the radar, and the match (HUD, hypercube map, kill feed, a missile
      lock, the player's ship killed twice). `PackagedWeb\runs\pages6_noheaders` (the GitHubPages folder from
      `web_plain_server.py`, port 8091): "PAGE ISOLATION reload under the service worker", compatibility check
      passed, READY after 6.4 s, PASS, 0 error lines; `match_c.png` shows REPAIR (37 m, a glowing cube on its pad)
      and SHIELD (90 m) columns in green. Both servers stopped.
    - `PackagedWeb\GitHubPages` (deleted, then `web_split_for_pages.py`, exit 0): **36 files, 261 676 809 bytes**,
      the wasm in three parts (50 000 000 + 50 000 000 + 42 494 582), the container 99 492 320 as it is (largest
      file), no `.br`, `.nojekyll` there; no file of 100 000 000 bytes or more.
    - Not verified: a magenta (power-up) column and a pad taken and refilled in a picture, the Easy rules played by a
      person in the browser, the forward tier, the 10-minute soak, sound by ear, a controller, the site on GitHub
      Pages itself, other browsers. `PackagedWeb\itch` and the itch zip are still the old build (item 28).

## 24 Nov 2026 (twenty-fifth session) - Easy: start armed, take pads in passing, a light aim assist; torpedoes that close
40. "Pickups easier to take, start each life with a weapon, a light aim assist" (Easy only), and from the player:
    "photon torpedo should aim to close and follow the target" (a person's torpedo, every level). From commit ab397f9
    (0.21.0, tree clean); this is **0.22.0**. No replicated property, no RPC, no lobby key changed (`ProjectVersion`
    stays 0.2.0): the Easy help is the server's, the torpedo's target is chosen by the server and replicated as before,
    and each machine works out by itself whether a torpedo is a person's (see C). Layout checksum of seed 1234
    unchanged (7C0CB60C, playthrough suite). Nothing was committed, pushed or uploaded.
    - **What the player's log says** (`F:\work\HCJ_push\play21\log_2026-11-24_easy_match.log`, build 0.21.0, Easy, 7
      bots, 4.5 min; `fair_stats.py`): 1 kill / 4 deaths (K/D 0.25), each death from the front by one bot at 12 to 23 m
      after 31 to 113 s; hull mended 153 points; **no pad taken** (`repairs=0`; the `own ship:` of all 18 status lines
      is the PHASER; the "PhaserEmpty" and "Pickup" hints twice each) while the bots emptied the pads; damage on bots
      581, of it 194 the Easy bonus. The pads of item 38 are seen - the player did not fly to them. The beginner
      rarely hits, so the 1.5 x of item 38 multiplies little. Torpedoes of the whole match (all ships): 65 fired, 22
      lost their target.
    - **A. Easy: more help for a beginner** (`FHCJBotSkill`, four new fields after the item 38 ones, "off" on Normal,
      Hard and Ace; `README.md`, "Bots and people", has the rows). Server only; bots among themselves and their own
      aim untouched.
      - *Pads in passing* (`HumanPickupReach` 2500 uu): the item of a filled pad goes to a person's ship within 25 m of
        the pad (`AHCJShipCharacter::ServerTickPickupReach`, five times a second, through the pad's own
        `AHCJPickup::ServerTryCollect` - now public and returning whether it was taken - so the rules are those of a
        touch: a repair kit at full hull stays). The simplest thing that is safe on the network: nothing is pulled,
        nothing moves, the pad goes dark and the ship's weapon / hull change as for a touch. Pads were 3.2 m of trigger
        before (`CollectRadius`).
      - *Start armed* (`HumanSpawnWeapon` = scatter cannon, `ServerGiveHumanStartWeapon` from `PossessedBy`): a
        person's every new ship holds a full scatter cannon (16 shots of 10 pellets); the phaser comes back when it is
        empty or dropped, as with a pad. Why the scatter cannon and not the rapid pulse: the player's duels were nose to
        nose at 12 to 23 m, where ten pellets in a 4 degree cone cover an aim that is 2 to 4 degrees off (a ship's hit
        sphere is 1.35 m: 4 degrees at 20 m); two shots that land break an Easy bot (with the 1.5 x of item 38); its
        reach of about 100 m keeps a beginner from spending it on ships at the far end of the sector. The pulse needs
        every bolt on the target and outlasts no better (11 s of fire against 10).
      - *Aim assist* (`HumanAimAssistDeg` 5): a person's bolt leaves towards the lead point (`HCJ::ComputeLeadAim`) of
        the locked ship, else of the ship nearest the crosshair, when that point lies within 5 degrees of the aim
        (`ServerFireProjectile`, `HCJBalance::PickAimAssist` = the phaser's `PickAssistTarget` with one cone,
        `AssistedAimDirection`); the scatter's pellets spread around the new direction as before. The server spawns the
        bolt, so every machine sees it fly where it goes; a client's prediction is not involved (bolts are not
        predicted). The phaser beam bends to a ship within 5 degrees (1.6 / 3 for everybody else). Bots never.
      - *The way to a weapon* (`bHumanWeaponPointer`): while the ship has only its base weapon, item 38's pad pointer
        (ring on the marker, three chevrons at the edge when it is out of view, ring on the radar) leads to the nearest
        weapon pad - not the torpedoes; ringed but not pulsing, and a low hull still takes precedence with its repair
        pad (`HCJUi::GuidePadRank`). The level is not replicated, so the HUD asks the authority game mode: **a host
        and a single player get it, a remote client does not.**
      - The Difficulty line: "One bot at a time, from the front. You start armed, your aim is helped." (71 characters;
        the test allows 73). `docs/README.txt` says what Easy does now.
      - Switches: `-HCJClassicEasy` now takes away the help of this item alone (`FHCJBotSkill::ClearBeginnerHelp`):
        Easy as build 0.21.0 had it; tested field by field. `-HCJClassicEasy20` (`ClearAllBeginnerHelp`) also takes
        item 38's away: Easy of 0.20.0. `-HCJNoFair` clears everything, as before.
    - **B. Torpedoes that close** (a person's, every level; `HCJTorpedo::GetHoming`, `PickTarget`, `SteerPoint`; bots'
      torpedoes unchanged, so Easy and Normal get no harder). Until now a torpedo homed only on its firer's lock, only
      if that lay within 60 degrees at launch, turned 40 degrees a second and was lost for good beyond 60 degrees (the
      coordinator quoted 110 / 40 from `HCJWeapons.cpp`; the `+Weapons=` row of `DefaultGame.ini` overrides them with
      40 / 60). A person's torpedo now: the lock within 60 degrees, else **the nearest live, uncloaked ship within 40
      degrees ahead** and 180 m; 180 degrees a second; it steers for where the target will be in up to 0.5 s; it keeps
      the target within **80** degrees of its nose. A ship that turns hard across its path under boost still gets out
      of the 80 degrees (unit test: a target at 85 degrees is lost). The server picks and drops the target
      (`FindTorpedoTarget`, `TorpedoKeepsTarget`); every machine steers its copy with the same numbers, because each
      works out `bHumanFirer` itself (server: the instigator's controller; client: the instigator's replicated player
      state is not a bot's) - nothing new on the wire. A 0.21.0 client steers a new host's torpedo with the old rate
      and gets corrected by the replicated movement. `-HCJClassicTorpedo` gives a person the bot's torpedo again.
      The HUD warning reads the replicated `HomingTarget` as before.
    - Found on the way: **in a headless run the player's lock is always empty.** `AHCJPlayerController` hands the ship
      its own target every frame, and that target is the HUD's auto target, which a headless run never draws - so the
      autopilot's `SetLockTarget` was overwritten, and the aiming autopilot of items 29 to 38 never fired a homing
      torpedo or had a locked missile (in batch A below: 40 torpedoes of the "before" pilot, none homing). The
      autopilot now also sets the controller's auto target (`HypeCubeJam.cpp`); batches B, C and the Normal batch ran
      with that. A rendered game has the lock from the HUD (the player's log: 65 of 65 homing).
    - Telemetry: `Fair: event=rules ... classic_easy20= pickup_reach= spawn_weapon= aim_assist= weapon_pointer=
      classic_torpedo=`; `Fair: event=status ... human_pickups= reach_pickups= armed_spawns= human_bolts=
      assisted_bolts= human_bolt_hits= assisted_hits= torp_fired= torp_homing= torp_acquired= torp_lost= torp_hit=`
      (pads people took and of them from beyond a touch, armed spawns, people's bolts - one per pellet - and how many
      the assist turned, their bolts that hit a ship and of those the assisted ones; people's torpedoes: fired, homing
      at launch, of them found without the lock, lost their target, damaged a ship). `fair_stats.py` prints them;
      `fair_matches.py` has `classic` (= `-HCJClassicEasy -HCJClassicTorpedo`, the game of 0.21.0), `classic20` and
      the pilot `poor<deg>`.
    - **A poor shot for the measurement** (`-HCJAutoAimError=<deg>`): the aiming autopilot's nose wanders around its
      target by up to <deg> degrees to each side (yaw; pitch 0.7 of it; two slow sine waves, so it is sometimes on,
      mostly off). With 8 degrees it still has 1.45 kills a death on 0.21.0 Easy, with 15 degrees 0.50 - the player had
      0.25.
    - **Measured** (`fair_matches.py` as in items 29 to 38: one autopilot against seven bots, `?Bots=8`, seed 99, no
      limits, 245 s a process, six at a time, three "before" and three "after" side by side, headless editor binary;
      "before" = the same binary with `-HCJClassicEasy -HCJClassicTorpedo`, i.e. 0.21.0; logs `Saved\Fair\item40_a`,
      `_b`, `_c`, `item40_normal`). Batch A ran before the lock fix above (no lock at all), B and C after it.

      | Easy | matches / min | kills (a minute) | deaths (a minute) | K/D | own damage on bots | pads taken (by reach) | bolts / hit % / assisted (hit %) | people's torpedoes fired / homing (no lock) / lost / hit a ship |
      |---|---|---|---|---|---|---|---|---|
      | poor shot 8 deg, before (B) | 3 / 12.2 | 16 (1.31) | 11 (0.90) | 1.45 | 4208 | 1 (0) | 2730 / 6.3 % / - | 53 / 14 (0) / 11 / 3 (6 %) |
      | **poor shot 8 deg, after (B)** | 3 / 12.2 | **33 (2.69)** | **4 (0.33)** | **8.25** | 6541 | **10 (10)** | 3624 / 6.9 % / 272 (76.5 %) | 28 / 19 (8) / 3 / 15 (54 %) |
      | poor shot 15 deg, before (C) | 3 / 12.2 | 7 (0.57) | 14 (1.15; 12 by bots, 2 by turrets) | 0.50 | 2654 | 2 (0) | 2630 / 3.8 % / - | 65 / 26 (0) / 21 / 3 (5 %) |
      | **poor shot 15 deg, after (C)** | 3 / 12.2 | **23 (1.88)** | **7 (0.57)** | **3.29** | 4705 | **6 (6)** | 3830 / 4.5 % / 168 (76.8 %) | 40 / 21 (8) / 3 / 18 (45 %) |
      | poor shot 8 deg, no lock, before (A) | 3 / 12.2 | 13 (1.06) | 7 (0.57) | 1.86 | 4296 | 0 | 2873 / 6.4 % / - | 40 / 0 / 0 / 0 |
      | poor shot 8 deg, no lock, after (A) | 3 / 12.2 | 31 (2.54) | 9 (0.74; 2 in a black hole, 1 turret) | 3.44 | 6705 | 12 (12) | 3702 / 6.1 % / 258 (81.0 %) | 48 / 24 (24) / 2 / 19 (40 %) |

      Per match (kills / deaths): B before 7/4, 4/4, 5/3, after 9/1, 13/2, 11/1; C before 1/7, 6/3, 0/4, after
      10/4, 7/2, 6/1; A before 2/4, 3/2, 8/1, after 8/2, 12/3, 11/4. Every "after" match of B and C has more kills
      than the best "before" match of its batch but one (C: 6 against 6).

      | Normal, aiming autopilot (torpedo only; the Easy help is off there) | matches / min | kills (a minute) | deaths (a minute) | K/D | people's torpedoes fired / homing (no lock) / lost / hit a ship |
      |---|---|---|---|---|---|
      | before (`-HCJClassicTorpedo`) | 3 / 11.8 | 6 (0.51) | 28 (2.37) | 0.21 | 118 / 47 (0) / 35 / 9 (8 %) |
      | after | 3 / 11.9 | 18 (1.51) | 32 (2.69) | 0.56 | 140 / 92 (30) / 11 / 73 (52 %) |

      - Read. For the pilot nearest to the player (15 degrees, 0.50 kills a death on 0.21.0 Easy) kills a minute
        rise 3.3 times and deaths a minute halve: K/D 0.50 -> 3.29. For the 8 degree pilot 1.45 -> 8.25: for anybody
        who can aim a little, Easy is now one-sided. Small numbers: 4 to 14 deaths per group (+- a third).
      - Where it comes from. The share of bolts that hit hardly moves (6.3 -> 6.9 %, 3.8 -> 4.5 %): only 7 and 4 % of
        this pilot's bolts were within the 5 degree cone of a ship - most of its fire goes at nothing - but of those
        three in four hit. The damage it does rises by half (+55 %, +77 %), its kills two to three times: the scatter
        cannon at the start of each life, the assisted bolts and the torpedoes (a person's torpedo damaged a ship in
        45 to 54 % of the shots instead of 5 to 6 %; lost 3 instead of 11 to 21). The three were not measured apart.
      - Pads: the autopilot does not fly to pads (it follows ships); it took 0 to 2 in three matches before and 6 to 12
        after, every one of them from beyond a touch. Every new ship started armed (`armed_spawns` = spawns).
      - Normal: the torpedo change alone triples the aiming pilot's kills (6 -> 18; 73 of 140 torpedoes hit against 9
        of 118) - more than asked ("dodgeable ... not a guaranteed hit": half of them hit a bot). Deaths a minute
        2.37 and 2.69 are inside each other's noise (item 31 measured 2.69); Normal's rules are untouched (the
        `Fair: event=rules` line of a Normal match is item 38's plus `classic_easy20=0 pickup_reach=0 spawn_weapon=none
        aim_assist=0.0 weapon_pointer=0 classic_torpedo=0|1`; `armed_spawns=0`, `assisted_bolts=0`, `reach_pickups=0`
        in all six).
      - No error line in the 24 logs; no `stuck` / `out_of_bounds` / `nan` event.
    - **Tests.** Three new automation tests - `HypeCubeJam.Balance.BeginnerArmsAndAim` (Easy alone has the four, the
      0.21.0 switch gives back Easy's row field by field and changes no other level, the 0.20.0 switch, the pickup reach,
      the aim assist's choice: locked ship first, nearest to the aim, cone, range), `HypeCubeJam.Torpedo.HumanHoming`
      (a person's torpedo against a bot's: rates and cones, the target at launch with and without a lock, keep and lose,
      the lead, and a crossing target at 60 m: a person's passes it at 25 uu, a bot's at 717 uu),
      `HypeCubeJam.UI.GuidePad` (which pad the pointer leads to) - and the changed checks of `Balance.BotDifficulty`
      and `Balance.BeginnerHelp` (the classic switch of item 38 is `ClearAllBeginnerHelp` now). **87 automation tests:
      87 of 87**, exit code 0, twice (`Saved\Item40\tests1.log`; `tests2.log` on the final binary). The runs log an
      engine ensure that was not there in item 38 (see Packaged).
    - **Verification.** `HypeCubeJamEditor` (Win64 Development) built, 0 errors, 0 warnings of the project (twice; the
      second time for the autopilot's lock).
      - Playthrough suite, editor binary, headless, `--port 7910`: **8 of 8**, 523 s, exit code 0
        (`Saved\Playthrough\20261124_113819`): match 1 won with 5 kills after 45 s, layouts 7C0CB60C -> 5671F10D; 61 /
        77 / 63 kills on easy / normal / hard, 62 / 71 / 67 pickups; 100 kills on four sectors; network scenario client
        1 kill / 4 deaths (3.3 corrections per 100 s), late joiner 1 / 1, 4 rocks destroyed; the autopilot player 16
        kills / 0 deaths as a cruiser over four matches (Normal: its torpedoes home now); kills by weapon phaser 152,
        torpedo 86, pulse 51, blaster 47, missiles 44, rail 42, scatter 31.
      - **Packaged** (the command of the README). First run (`Saved\Item40\bcr.log`): exit code 25, **BUILD FAILED** -
        `LogInit: Display: Failure - 29 error(s), 0 warning(s)` after "Packages Cooked: 1081, ... Skipped by Platform:
        29, Total Packages: 1110". The 29 "errors" are the lines of one engine ensure, verbatim: `Ensure condition failed:
        !DefaultDomain.IsValid() [File:...\AssetReferenceRestrictions\Private\AssetReferencingDomains.cpp] [Line: 101]`
        / `Registered the path /ToolsetRegistry/ for two different domains: Plugin:ToolsetRegistry and Never Cooked
        Content`. Not this item's: **the engine was updated on 22 Nov** (`Engine\Build\Build.version`: CL 60351404 ->
        60738873; `Plugins\Fab\Fab.uplugin` of the same evening), and the same ensure is in the automation run's log.
        Second run with `-IgnoreCookErrors` appended (`bcr2.log`): exit code 0, BUILD SUCCESSFUL (99 s; the same cook
        numbers, 1095 packages in item 38). The README says so now. `docs\tools\make_zip.ps1` (exit code 0):
        `Packaged\mariuz_HypeCubeJam.zip` **591.8 MB (620,524,820 bytes)**, 902.8 MB unpacked, 48 files; `python -I` /
        `zipfile.testzip()`: nothing wrong, 48 entries, forward slashes, no `.pdb`; its `README.txt` has the new Easy
        lines. Against 0.21.0's stage: `HypeCubeJam.exe` 367.5 MB (348.8; the engine update), the `.ucas` +0.3 MB.
      - Playthrough suite on the packaged build (`--binary packaged --port 7940`, headless): **8 of 8**, 490 s, exit
        code 0 (`Saved\Playthrough\20261124_115859`): 52 / 59 / 65 kills on easy / normal / hard, 87 on four sectors,
        client 2 kills / 3 deaths (4.2 corrections per 100 s), late joiner 3 / 3, 5 rocks destroyed, the autopilot
        player 12 kills / 1 death as a cruiser over four matches; layouts 3976D716 -> C9934379 (the packaged build's
        checksums, as in item 38's join test).
      - **0.21.0 and 0.22.0 together.** (1) The packaged builds, as item 38 did it (0.21.0 = a copy of item 38's zip
        stage; headless, Easy, 6 bots, both on the aiming autopilot; `Saved\Item40\join`): **neither joins the other.**
        Verbatim, both ways: `LogNet: Error: Server is incompatible with the local version of the game:
        RemoteNetworkVersion=2073374098 vs LocalNetworkVersion=4290060557` (and the reverse), `FailureType =
        OutdatedClient`. The logs give the reason: `Net CL: 60351404` (0.21.0) against `Net CL: 60738873` (0.22.0) -
        the engine update above; the engine's network changelist is part of the network version. So **the published
        0.21.0 (web build) and 0.20.0 (itch) cannot play with a build made on this engine**, whatever its source. (2) To
        check this item's own changes: the source of 0.21.0 (commit ab397f9, a temporary git worktree in
        `F:\work\HCJ_old021`, the asset packs linked in, removed afterwards) built as an editor binary on the current
        engine, against this build's editor binary (headless, 110 s of host, 70 s of client, Seed=1234, Easy, 6 bots;
        `Saved\Item40\join_editor`). New host, 0.21.0 client: `Player joined ... (remote) -> 2 humans, 4 bots`, the
        client has `ship=1`, the host's layout checksum (7C0CB60C), 5 kills / 0 deaths; the host applied the new help
        to the remote ship (`armed_spawns=3 reach_pickups=2 assisted_bolts=82`, 53 of its 59 bolt hits assisted;
        `torp_fired=12 torp_homing=7 torp_acquired=4` - the 0.21.0 client steered those torpedoes at its own, old rate
        between corrections). 0.21.0 host, new client: joined, `ship=1`, same checksum, 1 kill / 0 deaths. Exit code 0
        of all four processes, no error line. No replicated property, RPC or lobby key was changed.
    - **Amended: Easy toned down, the first numbers kept as "Super Easy"** (same item, still 0.22.0). The player played
      the package above on Easy (`F:\work\HCJ_push\play22\log_easy_match.log`, 5.9 min, 7 bots): **16 kills, 1 death**
      (2.70 kills a minute), 7 pads (all by reach), 641 bolts of which 226 assisted, 67 hits of which **64 assisted**,
      11 torpedoes / 8 homing / 7 hit, own damage 1815 (605 the bonus), hull mended 212, one repair pad - on 0.21.0 the
      same player had 1 / 4. "Tone it down"; the coordinator's target: about 2 to 4 kills per death and a death every
      1.5 to 2 minutes. And: "add as option super easy".
      - **Easy now**: aim assist **3 degrees** (was 5; the phaser bends 3 degrees too, the lock's value for everybody),
        a person's hits on a bot **1.35 x** (1.5), a bot's hits on a person **45 %** (40 %). Kept: 25 m reach, the
        scatter cannon at the start, the weapon pointer, hull mending, the full repair pad, the person's torpedo (all
        levels). Chosen with the poor-aim pilots (development overrides `-HCJEasyAssist= -HCJEasyHumanDamage=
        -HCJEasyBotDamage=`, `fair_matches.py --extra`): 3 degrees with 1.25 / 0.5 gave K/D 1.62 (8 degree pilot) and
        1.44 (15 degree pilot) over six matches each, a death every 1.2 to 1.4 min - too hard; 1.35 / 0.45 below.
        `-HCJClassicEasy` sets the two damage scales back to 0.21.0's 1.5 / 0.4 as well (tested).
      - **Super Easy** = Easy with the first numbers of this item: 5 degrees, 1.5 x, 40 % (`FHCJBotSkill::Get(4)`:
        Easy's row with those three; `HCJBalance::SuperEasy*`). Its line: "Bots go easy on you, your aim is helped a
        lot, you start armed." (63 characters). **It is index 4, not 0**: 0..3 keep their meaning everywhere they are
        stored or sent - `?Difficulty=` (4 = Super Easy now; an older build clamps a 4 to Ace, which only a typed URL
        can do), the saved settings (`BotDifficulty=0` + new `bSuperEasy=True`, so an older build on the same PC reads
        Easy), the lobby attribute `HCJDIFF` (4; an older build accepts 0..3 only and shows the row without a level - and
        cannot join anyway, see the engine update), the logs (`difficulty=4 level=super_easy`; `HCJBalance::GetDifficultyKey`).
        In the menu it comes first: `HCJBalance::StepDifficulty` / `GetMenuOrder` (Super Easy, Easy, Normal, Hard,
        Ace; held at both ends), used by the title-menu stepper, the in-match stepper and `HCJMenuCmd difficulty`.
        Its bots use Easy's tables (`GetSkillLevel` for the torpedo skill, `IsEasyLevel`); `NumBotDifficulties` = 5 and
        `NormalizeDifficulty` keep any other number clamped to 0..3 as before (12 is still Ace).
      - Tests (still 87; new checks in `Balance.BotDifficulty` - five levels, names, the log key, the menu order and
        stepping, Easy's 0.45 - in `Balance.BeginnerArmsAndAim` - Super Easy's three numbers, otherwise Easy's row
        field by field, its 0.21.0 switch = 0.21.0's Easy; 0.21.0's damage scales - and in `Online.LobbyRowExtras` every level
        0..4 goes over the lobby wire): **87 of 87**, exit code 0, twice (`Saved\Item40\tests3.log` with 1.25 / 0.5,
        `tests4.log` on the final binary).
      - **Measured** (as above: one poor-aim autopilot against seven bots, seed 99, 245 s a process, six at a time,
        headless editor binary; "0.21.0" is batches B and C above; Easy = the final numbers, two batches of three
        each per pilot, `Saved\Fair\item40_t2` (by the overrides, same numbers) and `item40_easy3` (final binary);
        Super Easy one batch, `item40_super`).

        | pilot | level | matches / min | kills (a minute) | deaths (a minute; a death every) | K/D | own damage | pads (by reach) | bolts / assisted (hit %) | person's torpedoes fired / hit a ship |
        |---|---|---|---|---|---|---|---|---|---|
        | poor 8 deg | Easy of 0.21.0 | 3 / 12.2 | 16 (1.31) | 11 (0.90; 1.1 min) | 1.45 | 4208 | 1 (0) | 2730 / - | 53 / 3 |
        | poor 8 deg | **Easy now** | 6 / 24.4 | **47 (1.93)** | **15 (0.61; 1.6 min)** | **3.13** | 11020 | 11 (11) | 7689 / 215 (82 %) | 90 / 50 |
        | poor 8 deg | **Super Easy** | 3 / 12.2 | 49 (4.01) | 1 (0.08) | 49 | 8496 | 5 (5) | 3288 / 359 (86 %) | 22 / 10 |
        | poor 8 deg | (first 0.22.0 Easy, batch B) | 3 / 12.2 | 33 (2.69) | 4 (0.33) | 8.25 | 6541 | 10 (10) | 3624 / 272 (77 %) | 28 / 15 |
        | poor 15 deg | Easy of 0.21.0 | 3 / 12.2 | 7 (0.57) | 14 (1.15; 0.9 min) | 0.50 | 2654 | 2 (0) | 2630 / - | 65 / 3 |
        | poor 15 deg | **Easy now** | 6 / 24.4 | **21 (0.86)** | **16 (0.66; 1.5 min)** | **1.31** | 5794 | 21 (21) | 7678 / 130 (68 %) | 93 / 39 |
        | poor 15 deg | **Super Easy** | 3 / 12.2 | 20 (1.63) | 4 (0.33) | 5.00 | 5275 | 14 (14) | 3730 / 181 (80 %) | 31 / 16 |
        | poor 15 deg | (first 0.22.0 Easy, batch C) | 3 / 12.2 | 23 (1.88) | 7 (0.57) | 3.29 | 4705 | 6 (6) | 3830 / 168 (77 %) | 40 / 18 |

        Per match, Easy now (kills / deaths): 8 degrees 7/2, 10/4, 3/2, 13/2, 5/3, 9/2; 15 degrees 2/4, 5/1, 5/2, 4/4,
        4/3, 1/2. Super Easy: 8 degrees 17/0, 17/0, 15/1; 15 degrees 6/1, 7/1, 7/2.
      - Read. Easy now lands inside the target for the 8 degree pilot (K/D 3.1, a death every 1.6 min) and below it for
        the 15 degree pilot (1.3, every 1.5 min) - which still has 2.6 times its K/D of 0.21.0. The player hit 64 of 67
        with the assist and fought like the better of the two pilots (16 / 1 on the first numbers, where the 8 degree
        pilot had 33 / 4 and 49 / 1), so the 8 degree row is the nearer guess for them. The assisted bolts: the 3 degree
        cone turned 215 of the 8 degree pilot's bolts in 24 minutes, 5 degrees 272 and 359 in 12 minutes each - about
        a third as many a minute - and they still hit 68 to 82 %. Super Easy reproduces the first numbers' direction (one-sided for the 8 degree pilot; 49 / 1 against
        33 / 4 in batch B - three matches each say little), and gives the 15 degree pilot K/D 5. No error line, no
        `stuck` / `out_of_bounds` / `nan` in the 24 logs of these runs (with the 1.25 / 0.5 batches).
      - Built again (editor, 0 errors), packaged again with `-IgnoreCookErrors` (`Saved\Item40\bcr4.log`: exit code 0,
        BUILD SUCCESSFUL; the same 29 lines of the engine ensure, nothing else). `make_zip.ps1` (exit code 0):
        `Packaged\mariuz_HypeCubeJam.zip` **591.8 MB (620,526,099 bytes)**, 902.8 MB unpacked, 48 files; `testzip`
        nothing wrong, forward slashes, no `.pdb`; its `README.txt` has Super Easy and the 45 %; the exe holds both new
        menu lines. Playthrough suite on this package (`--binary packaged --port 7940`, headless): **8 of 8**, 490 s,
        exit code 0 (`Saved\Playthrough\20261124_132537`): 64 / 69 / 78 kills on easy / normal / hard, 90 on four
        sectors, client 2 / 1 (7.9 corrections per 100 s), late joiner 3 / 2, the autopilot player 8 kills / 1 death
        over three matches as a cruiser. The editor-binary suite and the cross-join were not run again after the
        amendment (no replicated property, RPC or lobby key changed by it; the lobby attribute HCJDIFF now carries 4
        for Super Easy - see above).
    - Development aids added: `-HCJClassicEasy` (new meaning: 0.21.0's Easy), `-HCJClassicEasy20`,
      `-HCJClassicTorpedo`, `-HCJAutoAimError=<deg>`, `-HCJEasyAssist= -HCJEasyHumanDamage= -HCJEasyBotDamage=`
      (Easy and Super Easy, development builds), `fair_matches.py` `classic` / `classic20` / `poor<deg>` / `--extra`,
      difficulty 4, the item 40 columns and the level names of `fair_stats.py`.
    - Pictures: none. Every picture of this game needs a rendered window, and the PC was in use; everything above ran
      headless.
    - **Not verified.**
      - Nobody has played the amended build. The player played the first one (16 / 1 on Easy: far too easy); Easy was
        then tuned with an autopilot that aims badly by a sine wave, and a person misses differently (late, behind a
        turning ship, at the wrong ship). Easy is now 3.1 kills per death for the 8 degree pilot and 1.3 for the 15
        degree one (six matches each, 15 and 16 deaths: +- a quarter); where the player lands is a guess.
      - Super Easy was measured in one batch of three matches per pilot. The menu row with five entries, its new first
        entry and its line were not seen on a screen (the stepper's order is unit-tested).
      - The weapon pointer, the reach and the start weapon were not seen on a screen (no pictures, no rendered run):
        whether the ring and chevrons on a weapon pad are noticed, or annoying, is open. They are drawn by the code of
        item 38's low-hull pointer, which was seen in pictures then. A remote client on Easy gets no weapon pointer (the
        level is not replicated); the reach, the start weapon and the aim assist work for it (server).
      - Pads taken from 25 m vanish without a sign on the ship's side other than the usual pickup sound at the pad and
        the HUD's pickup line; a pad's item is not seen flying to the ship.
      - The aim assist was measured by its counters, not seen: from a person's view a bolt that leaves 5 degrees off the
        crosshair may look odd (3 degrees on Easy now, 5 on Super Easy). Its lead uses the target's velocity at the moment of the shot.
      - The torpedo change makes a person's torpedo hit bots in half the shots on Easy and Normal; against people in a
        network match (every level) it is equally strong, and it was not tried whether a person can dodge it (the unit
        test only says a target beyond 80 degrees is lost). Hard and Ace: by the code and the unit tests only.
      - The three parts of the Easy help (start weapon, assist, torpedo) were not measured one at a time.
      - The old autopilot numbers of items 29 to 38 were measured without a lock (see above): their missiles and
        torpedoes did not home. Comparisons with them are off by that.
      - The web (Wasm) build was not compiled (no Windows-only code was added).
      - The engine update of 22 Nov: 0.22.0 cannot join or host 0.21.0 / 0.20.0 builds (network version). Not tried:
        whether FIND GAMES / QUICK PLAY list such hosts as joinable (the lobby compares the project version, which is
        still 0.2.0) - a player of the old build would then see a failed join. The ensure in the cook was not looked
        into beyond reading it (engine / Fab plugin code).

## 24 Nov 2026 (twenty-sixth session) - web build of 0.22.0 rebuilt
41. The web (Wasm) build made again from commit 895d185 (0.22.0, Super Easy, a gentler Easy, torpedoes that follow;
    tree clean) by item 28's procedure, as in items 35, 37 and 39 - the first web build on the engine of 22 Nov.
    Nothing was committed, pushed or uploaded.
    - Package: `PackagedWeb\Ship` deleted, then the Shipping BuildCookRun of item 28. **First run failed in the
      cook** (log kept as `PackagedWeb\logs\bcr_ship7_cookfail.log`): compile, link and wasm-opt 867 s (exit code 0;
      the game module and the engine's web libraries were linked again after the engine update), then the cook ran to
      "Cooked packages 1110 Packages Remain 0" and still ended `ExitCode=1`, "Cook failed.", UAT exit code 25
      (`Error_UnknownCookFailure`), 30 min 29 s. Its only error lines are the engine ensure seen in the Windows
      packaging of item 40: `Ensure condition failed: !DefaultDomain.IsValid()` (`AssetReferencingDomains.cpp`,
      line 101), "Registered the path /ToolsetRegistry/ for two different domains: Plugin:ToolsetRegistry and Never
      Cooked Content". **Second run with `-IgnoreCookErrors` added** (otherwise the same command; log
      `PackagedWeb\logs\bcr_ship7.log`): exit code 0, BUILD SUCCESSFUL, 7 min 50 s (the build was up to date, 2 s;
      the cook 47 s - its summary "Failure - 29 error(s), 0 warning(s)", all 29 lines the same ensure and its stack;
      the rest staging, pak / iostore and Brotli). Cook: 1110 packages, 1047 cooked, 63 skipped for the platform
      (1095 / 1032 / 63 before: the updated engine brings 15 more packages). One warning in the logs, the linker's
      known `-pthread + ALLOW_MEMORY_GROWTH` line. Wall time of both runs together about 38 min. **Until the ensure is
      fixed in the engine, the web BuildCookRun needs `-IgnoreCookErrors`, as the Windows one does.**
    - Stage `PackagedWeb\Ship\Wasm`: 55 files, 298 586 461 bytes with the `.br` copies; 33 files, 266 366 548 bytes
      without them (56 / 300 794 564 and 33 / 261 676 413 in item 39: one `.br` copy fewer). Wasm **146 955 416
      bytes** (27.4 MB as Brotli; +4 460 834 bytes against item 39, mostly the updated engine), container `.ucas`
      **99 548 096 bytes** (+55 776; **451 904 bytes under 100 000 000**), pak 9 287 513.
    - `check_stage.py`: all passed, release scan 0 findings (1 warning, the stock engine font path).
      `web_secret_scan.py` with UnrealPak: 55 staged files and 1145 extracted files (93 ini; 1077 / 84 before), 0
      hits, RESULT clean.
    - Browser (harness, off-screen Chrome, deferred tier, `hcj_click_play`, fixture unchanged):
      `PackagedWeb\runs\ship7_deferred` (engine server, port 8090): PASS, READY after 8.5 s, 0 WebGPU errors, no
      error line in the console log (3 warnings, Chrome's `powerPreference` note, as before; one log line "the browser
      refused pointer lock (WrongDocumentError)", new on this engine, not an error); `title.png` shows Difficulty
      **Easy** (the default) with its line "One bot at a time, from the front. You start armed, your aim is helped.";
      `match_a..c.png` show a 4-D match with bots, the player's Fighter holding the SCATTER CANNON from the start (16
      shots), torpedoes READY, a SHIELD pad column, the hypercube map and the kill feed, the player's ship killed once.
      `PackagedWeb\runs\pages7_noheaders` (the GitHubPages folder from `web_plain_server.py`, port 8091): "PAGE
      ISOLATION reload under the service worker", READY after 7.9 s, PASS, 0 error lines; the match runs (scatter
      cannon at the start, a planet, portals, the player's ship killed three times). **Its first attempt failed**
      (kept as `PackagedWeb\runs\pages7_noheaders_devicelost`): WebGPU device lost 9.2 s after the load, "D3D12
      create command allocator failed with DXGI_ERROR_DEVICE_REMOVED", before READY - the GTX 950 (2 GB) had 1.6 GB
      in use by other programs on the PC at the time; the retry, unchanged, passed. A third run with a scratch fixture
      (not in `docs/tools`; the start click, two clicks on the Difficulty row's "<" at 939,328, then PLAY):
      `PackagedWeb\runs\ship7_supereasy` (engine server): PASS, READY after 6.9 s, 0 error lines; after one "<" the
      row shows **Super Easy** with "Bots go easy on you, your aim is helped a lot, you start armed."; after the
      second "<" it stays on Super Easy (first entry, held at the end); its match starts armed (SCATTER CANNON, an
      incoming torpedo warning). Both servers stopped.
    - `PackagedWeb\GitHubPages` (deleted, then `web_split_for_pages.py`): **36 files, 266 366 957 bytes**, the wasm
      in three parts (50 000 000 + 50 000 000 + 46 955 416), the container 99 548 096 as it is (largest file), no
      `.br`, `.nojekyll` there; no file of 100 000 000 bytes or more.
    - Not verified: the ensure itself (engine code; whether `-IgnoreCookErrors` could hide another cook error later -
      the 29 lines here are all the ensure), Super Easy and Easy played by a person in the browser (the harness only
      clicks and holds W), the in-match difficulty row, the weapon pointer and the reach of Easy in a picture, the
      forward tier, the 10-minute soak, sound by ear, a controller, the site on GitHub Pages itself, other browsers,
      whether the device loss comes back on a PC with a free GPU. `PackagedWeb\itch` and the itch zip are still the
      old build (item 28).

## 24 Nov 2026 - the weapon pointer of Easy on a remote client
42. Item 40's open point "a remote client on Easy gets no weapon pointer": the HUD asked the authority game mode for
    the level, which a client does not have. Now it reads a replicated flag. Gameplay numbers unchanged.
    - **The flag**: `AHCJGameState::bWeaponPadPointer` (`UPROPERTY(Replicated)`, `DOREPLIFETIME`; one bool, false by
      default), set only by the server through `SetWeaponPadPointer` (changes it and `ForceNetUpdate`). The game mode
      copies its own answer to it (`AHCJGameMode::PublishWeaponPadPointer` = `ShowsWeaponPadPointer` =
      `GetFairSkill().bHumanWeaponPointer`, so `-HCJClassicEasy`, `-HCJClassicEasy20` and `-HCJNoFair` on the server
      still switch it off) in `InitGameState`, in `BeginMatch` (every new match, also PLAY from the menu) and in
      `SetBotDifficulty` (the Esc menu's level change). A bool rather than the level: the client needs nothing else,
      and the server's switches are already applied. The level itself is still not replicated.
    - **The HUD** (`AHCJHUD`, pad markers) no longer looks at the game mode: `HCJUi::WantsWeaponPad(flag, low hull,
      only the base weapon)` (pure) feeds `GuidePadRank` as before. A host and a single player read the same game
      state the server writes, so their behaviour is unchanged; a client gets the flag with the game state (false
      until it has replicated - no pointer for the first moments of a join at most).
    - **Log**: one line per change, `HUD: weapon_pointer=0|1 net=host|client|standalone pad=<item>` (LogHCJ, Log), so
      a run shows when a HUD pointed to a weapon pad.
    - **Test** (88 now, all passed, exit code 0): `HypeCubeJam.UI.WeaponPointerReplicated` - `WantsWeaponPad` for the
      four cases (on; flag off; low hull; holding a pickup weapon), its result fed to `GuidePadRank`, the game state's
      default is off and the property is marked replicated (`CPF_Net`). A first version also called
      `GetLifetimeReplicatedProps` on the class default object; that asserts in the engine (`bIsPushBased ==
      Other.bIsPushBased`, CoreNet.h) and was taken out - the registration is shown by the two-process run below.
    - **Two processes on this PC** (editor binary, `-game`, `-HCJNoEOS -HCJNoStats`): listen server
      `"/Engine/Maps/Entry?listen?Difficulty=0?Bots=3"` (640x360, `-HCJQuitAfter=260`), then a client `127.0.0.1`
      with `-HCJAutoFly -HCJCapture=12 -HCJQuitAfter=220` (1280x720). Server: `Fair: event=rules ... level=easy ...
      weapon_pointer=1`, `armed_spawns=6` (the client's new ships started with the scatter cannon, as on Easy). The
      client's autopilot fires the scatter cannon empty within about 25 s of each ship, which leaves the phaser - no
      switch was needed. Client log: `HUD: weapon_pointer=1 net=client pad=SCATTER CANNON` (15:23:38), `=0` (15:24:07),
      `=1 ... pad=RAIL SHOT` (15:24:39), `=0` (15:25:12), `=1 ... pad=RAIL SHOT` (15:25:37). Picture `Shot_02`
      (15:23:52; kept outside the project, the project's `Saved\Screenshots\HCJCapture` holds its old pictures again):
      weapon panel PHASER, the SCATTER pad at 120 m with the ring, its ring on the radar. The run "before" was not
      repeated (the old code gave a client no way to know the level: `GetAuthGameMode` is null there). The host also
      logged `weapon_pointer=1 net=host pad=RAPID PULSE` on its first frame and `=0` 23 s later (its ship idle; the
      reason was not looked into - item 40's behaviour, not changed here).
    - **Compatibility**: a new replicated property changes the game state's replication layout, so a build with this
      change and 0.22.0 must not play together (an old client would misread the game state). 0.22.0 already cannot
      play with 0.21.0 and older (engine network version, item 40); between 0.22.0 and the next build the project
      version (lobby `FHCJLobbyInfo`, still compared as before) should be raised so that FIND GAMES / QUICK PLAY keep
      them apart. The EOS lobby keys and `HCJEosTests` are untouched (the flag is not a lobby attribute; all
      Online tests passed).
    - Not verified: Super Easy over the network (same flag, same code path; only Easy was run), the Esc menu's level
      change reaching a client while it is connected (the setter is called there, not exercised), a dedicated server,
      a packaged build, the web build. Nothing was committed, packaged or uploaded.

## 24 Nov 2026 - the cook ensure of the 22 Nov engine, a guard for -IgnoreCookErrors
43. Why the cook of items 40 and 41 needed `-IgnoreCookErrors`, a fix in the project, and a guard for the flag.
    - **Cause.** `Engine\Plugins\Editor\AssetReferenceRestrictions\...\Private\AssetReferencingDomains.cpp`:
      `FDomainDatabase::RebuildFromScratch` makes one domain per content plugin (`ToolsetRegistry.uplugin` has
      `CanContainContent: true`, root `/ToolsetRegistry/`) and, when `DirectoriesToNeverCook` is not empty, a "Never
      Cooked Content" domain with those folders (lines 269-282); our `Config\DefaultGame.ini` has
      `+DirectoriesToNeverCook=(Path="/ToolsetRegistry")` (item 7). Both roots go into one path tree and
      `FDomainPathNode::AddDomain` ensures (`ensureMsgf(!DefaultDomain.IsValid(), ...)`, line 101) on the second one.
      The plugin was not loaded before the update: the 22 Nov engine brought `Plugins\Experimental\Toolsets\AllToolsets`
      (`"EnabledByDefault": true`, an aggregator of every toolset plugin; file of 22 Nov 20:39), whose
      `GameFeaturesToolset` enables `GameFeatures`, which enables `AssetReferenceRestrictions`. The cook logs show it:
      62 toolsets registered in `bcr_ship6.log` (before), 100 in `bcr_ship7.log`, among them `GameFeaturesToolset`.
      Engine code and the project's config line each are fine; together, with `AllToolsets` on, they ensure. Nothing
      in the engine was changed.
    - **Fix (project side)**: `{ "Name": "AllToolsets", "Enabled": false }` in `HypeCubeJam.uproject` (the project
      enables its eight toolsets itself; the `DirectoriesToNeverCook` line stays). Test: a Windows cook only
      (`BuildCookRun -platform=Win64 -clientconfig=Development -cook -utf8output -unattended -nocompileeditor
      -skipbuildeditor`, **without** `-IgnoreCookErrors`, log `Saved\CookGuard\cook_B.log`): exit code 0, BUILD
      SUCCESSFUL, "Success - 0 error(s), 0 warning(s)", 51 s; 1095 packages, 1066 cooked, 29 skipped by platform -
      the totals of item 38, before the update (the 15 extra packages of items 40 / 41 were the aggregator's); 62
      toolsets registered, as before. Not done with the fix: a full Windows package, a Wasm build, the editor opened
      by a person (an MCP client now sees the pre-update toolset set).
    - **Guard**: `docs\tools\cook_error_guard.py <log> [...]` (`python -I`), exit code 0 / 1 (2: unreadable). It
      accepts the ensure only as a whole block between `LogOutputDevice: Error: begin: stack for UAT` and
      `... end: stack for UAT` that holds both of its message lines (`Ensure condition failed: !DefaultDomain.IsValid()
      [File:...AssetReferencingDomains.cpp]` and `Registered the path /ToolsetRegistry/ ... Plugin:ToolsetRegistry and
      Never Cooked Content`) and nothing but frame lines (`=== Handled ensure: ===`, `Stack:`, `[Callstack] ...`,
      empty); the copy in the cook summary (`LogInit: Display: ` prefix) likewise. Every other `<Category>: Error:` /
      `Fatal:` line, compiler / clang / UAT error, ensure or assert is printed with its line number; the summary's
      "N error(s)" must equal the lines of the accepted blocks before it (29), and a log without a cook summary fails.
      Reads UTF-8 and UTF-16 (PowerShell 5.1 `Tee-Object` writes UTF-16).
      - Results: `PackagedWeb\logs\bcr_ship7.log`, `Saved\Item40\bcr2.log` (Windows 0.22.0), `bcr.log`,
        `bcr_ship7_cookfail.log` (their cook errors were only the ensure; the failed runs' exit codes are the build's
        business, not the guard's), `bcr_ship1/5/6.log`, `bcr_dev1.log` and `cook_B.log`: OK, exit code 0.
        Synthetic copies of `bcr_ship7.log` (scratch folder, not kept): one extra `LogCook: Error:` line in the body
        and the summary with the count 30 - FAILED, the two lines and the count shown; the extra line alone - FAILED;
        a third message line inside the ensure block - FAILED, the whole block shown; the same ensure for `/Fab/` -
        FAILED; the count alone 31 - FAILED; an MSVC `error C2065` line - FAILED; no summary - FAILED; a UTF-16 copy
        of the clean log - OK.
    - **Procedure** (README "Packaged build" and item 28's web BuildCookRun): with the fix the flag is not needed.
      When `-IgnoreCookErrors` is used, the output goes to a log and the guard runs on it right after; the build
      counts as failed when UAT or the guard exits non-zero. Web (PowerShell, `$env:WASM_EMSDK_ROOT` set as in item
      28): `& <RunUAT.bat> BuildCookRun <item 28's Shipping arguments> -IgnoreCookErrors | Tee-Object -FilePath
      PackagedWeb\logs\bcr_shipN.log; $Uat = $LASTEXITCODE; python -I docs\tools\cook_error_guard.py
      PackagedWeb\logs\bcr_shipN.log; if ($Uat -ne 0 -or $LASTEXITCODE -ne 0) { throw "BUILD FAILED" }`. No script
      in `docs\tools` runs BuildCookRun (`make_zip.ps1` only zips a stage), so there was no wrapper to hook.
      Nothing committed.

## Measurements
- Automation tests: 9 at the start, 87 now (item 40: 87 of 87; item 38: 84 of 84; item 36: 81 of 81; items 33 and 34: 77 of 77), all passing (added: the start weapon, pickup reach and aim assist of Easy, a person's torpedo homing, the pad pointer; the beginner's help of Easy, the kinds / light columns of the pads and the pad markers of the HUD; the game controller - aim stick, switch, menu focus, prompt names -; the richer lobby rows and the pilot record; the computer voice - limiter rules, configured waves -; the hypercube map - projection and label placement -, the stages and the detail of a ship's explosion; two playthroughs - a match of bots to the frag limit and the next one, three minutes of an open match -, the lobby filter and the quick-play steps; decorative rocks - determinism, keep-outs, belt beyond the kill radius, layout untouched; ship systems - warp state machine, warp and boost followed by the server, tractor pull, cloak device - and ship types - table, server validation; the intro flow and its command-line rule, phasers, destroyable asteroids, shockwave / debris rules, music tracks, photon torpedoes, shield facings, online logic, hints, rock visuals, ship look, sector sky, black hole
  placement, black hole gravity, pickup pad placement, weapon rules, wormhole links, wormhole routing, station
  placement, lead aim).
- Frame rate (editor binary, -game, 8 s averages): 101-128 fps with basic shapes (`-HCJNoAssets`), 98-129 fps
  with ships/rocks/effects, 80-112 fps with the sky as well. Client of a dedicated server: 93-115 fps.
- Server and client layout checksums match (7C0CB60C for seed 1234, 2853 asteroids, 24 portal links).
- Typical 90 s bot match: about 900 shots, 190 hits, 20 kills, 34 portal jumps.
- The editor binary on the 2 GB card intermittently reports "video memory exhausted" (120-590 MB over budget),
  also with basic shapes only.

## Not done / open
- Item 40 (Easy: start armed, pads from 25 m, a 3 degree aim assist, the pointer to a weapon pad; the new level Super
  Easy with the first, stronger numbers; a person's torpedo that acquires and closes, every level; packaged as
  `Packaged\mariuz_HypeCubeJam.zip`): the player played the first version (16 kills / 1 death on Easy: "tone it down");
  **nobody has played the amended one**, and nothing of it was seen on a screen (no pictures). Measured with an
  autopilot made to aim badly: K/D on Easy 1.45 -> 3.13 (8 degree wander) and 0.50 -> 1.31 (15 degrees), a death every
  1.6 / 1.5 min; Super Easy 49 and 5.0. A person's torpedo hits in about half the shots instead of one in twenty, also
  on Normal (aiming pilot: kills 6 -> 18 in three matches). Whether Easy is now right for the player is open. The weapon pointer is not shown on a remote client. **0.22.0 cannot play with 0.21.0 / 0.20.0 builds** (the
  engine was updated on 22 Nov; network version), and the README's packaging command needs `-IgnoreCookErrors` since
  that update.
- Item 38 (Easy for a beginner, pads that can be seen; packaged as `Packaged\mariuz_HypeCubeJam.zip`): **nobody has
  played it.** Easy was measured with the aiming autopilot only (kills over deaths 1.33 -> 4.14, deaths a minute 0.99
  -> 0.58, twelve matches of four minutes), which aims far better than the player it was made for; whether Easy is
  now right, still too hard or too easy for a beginner is open. The pads' columns, halos, markers and the low-hull
  pointer were judged from eight still pictures of the editor binary; not seen in motion, not on a network client,
  not in the packaged build (headless only), not in the web build (not compiled). Frame cost not measured against
  0.20.0. The playthrough suite failed once on the packaged build in `net_host_client_late` (the late joiner's
  first kill and death came after the board's last line; passed when run again).
- Item 36 (game controller): **no controller was attached or used.** The bindings, the SETTINGS switch, the menu
  focus and the pad prompts were shown with injected key events (`HCJMenuCmd padtest`, `HCJMenuCmd nav`), unit tests
  and pictures. Open: an Xbox pad and a Steam Controller in the hand (is it seen, the feel of the aim stick, the left
  stick and Start in the menus), Steam Input's two recipes, the mouse on the menu after the buttons stopped taking the
  keyboard focus, the Wasm build (not compiled). No invert-Y, no vibration, no on-screen keyboard for the text boxes.
- Item 34 (review fixes for the voice and the online extras, packaged as `Packaged\mariuz_HypeCubeJam.zip`): of the
  twelve fixes one was seen working in a run (the voice fades when it is switched off); the others were checked by
  reading, the unit tests and the suites. Nobody has heard the voice. The web build compiles and links with all of
  it and was not cooked or run; the published web build is 0.18.0.
- Item 33 (pilot record, open-matches counter, richer FIND GAMES rows) ran against the live service in a test
  bucket with two instances of one EOS user, pages opened by console command. Packaged and compiled for Wasm in
  item 34. No second user, the "stats query refused" fallback never taken, the record never seen to grow (no stats were sent).
- Item 32 (computer voice) was judged from logs and tests. Nobody has heard a single line: wording, level, the
  chirp, the cut-off of an interrupted line are unverified by ear.
  - Packaged in item 34 (lines spoken from the pak, picture 181 of the SETTINGS row, the console switch used in a
    run); not run in a browser (the Wasm game target compiles and links with it). The row was not clicked.
  - "Aft shield down." and "Port shield down." were never spoken in a run (requested and declined); lines a minute
    were measured with one autopilot match on Easy (3.2) and two headless ones (2.4, 2.5).
- Item 30 (the hypercube map, the explosion of a ship) was judged from still pictures (the explosion in slow motion),
  frame-time measurements with the editor binary and the test runs. Nobody has played with either.
  - Packaged in item 31 (the three new materials cook; the explosion and the map were looked at in pictures of the
    packaged build).
  - The fireball is flat quads facing the view; the smoke of a blast 35 m ahead hides what is behind it for about two
    seconds; the trails of the burning pieces are rows of white puffs; the hull does not break up.
  - The death camera was run on a listen host and (item 31, after a fix) on one network client, in third person;
    not from the cockpit.
  - The sounds of the pops and the low boom of the own ship were not heard.
  - The map with 32 sectors is dense; 64 sectors were not looked at; it takes 0.13-0.27 ms of the game thread a frame.
  - No larger map on the scoreboard.
- Item 29 (difficulty in the menu, bots that are fair to a player) was judged from autopilot matches, test runs and
  still pictures. Nobody has played against the fair bots.
  - Packaged in item 31.
  - Normal was tuned in item 31: 2.69 deaths a minute for the aiming autopilot (4.65 without the rules), a death every
    22 s, a third of them within 12 s of the spawn. The review asked for 2 to 2.5.
  - Easy (item 31): 1.06 deaths a minute for the pilot that always fights, 1.22 for the one that does not find the
    enemy (target 1.5); one match of nine above it. A match has 3-7 human deaths, so one match says little.
  - `held_cap` is 0 on Hard: three bots on one player did not happen in the measured matches, with or without the cap.
  - Ace was measured with one match (its counters are 0); that it equals Hard without the rules is the unit test's word.
  - "Behind" is measured from the nose of the player's ship; whether a player can look elsewhere was not checked.
  - ENEMY BEHIND also fires for a bot that is aiming at somebody else on the same line (after 0.3 s since item 31),
    and its cue (the warning tick, lower) repeats every 4 s for as long as a ship sits behind the player. Nobody has
    heard it.
  - A player who flies out of a sector still dies of it without a shooter (most likely the three deaths with no
    killer of the player's match); the log now says so.
  - The rules count a ship as a person's by its controller on the server: they apply to every human in a network
    match, but that was only run with the autopilots of `net_host_client_late`, on Easy - and since item 31 that
    scenario runs with `-HCJNoFair`, so no test has the rules and a remote person together.
- Item 27 (bot tactics, playthrough tests, quick play) was judged from logs, bot matches, test runs and still pictures.
  - Bots: on easy and normal the interceptor is now clearly the best type (K/D about a third above the mean of the five
    over twelve matches each) and gunship and cruiser the worst (16-24 % below); on hard all five are inside 10 %. The
    numbers of one batch move by a quarter per type. Fights of two are still nose to nose; bots do not turn a shield
    facing while they shoot (it costs more fire than it saves); rocks are shot at once or twice a match; cover is
    reached about once a minute on normal and hard. Nobody has played against them.
  - Two bots sat nose to nose for 21 s once (second batch); not seen again, not explained.
  - Quick Play was tested live with two instances on one PC, which are one EOS user, in a test bucket. Not tested: two
    different users / PCs, a failing join against a real host (unit test and a run with made-up rows only), the mouse.
    The best lobby is the fullest, not the nearest: the ping to a host is not known before the join.
  - No EOS P2P / relay (see item 27 for what was looked at): a host listens either through the relay or by address, not
    both, and P2P between two processes of one EOS user cannot be tested on one PC.
  - The playthrough net scenario does not run rendered with three windows on the GTX 950 (video memory); headless it
    passes. An autopilot does not score in every three minutes, so the test asks for one kill between client and late
    joiner. The frame-time limits of the runner (mean 60 ms, longest 3 s) are loose because scenarios run side by side.
  - Player names are unique per match now ("User-2"); the EOS display name stays the pilot name of the settings.
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
    the edge). On normal and hard the scout was 30-40 % below the others (item 27: the bots fly their types differently now and the scout is level; the interceptor leads on easy and normal). How the types feel to a pilot
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
  - Bots shoot at a rock on purpose only when a small one hides their target (item 27: once or twice a match); how fast a player can clear a field was not measured.
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
