# HypeCubeJam devlog #1 — directing agents through a hypercube

**Status (14 Nov, build 0.8.1):** playable and uploaded, with a menu, target lock, wormholes and space stations. No human has play-tested it yet; everything below was checked by automated runs, logs and screenshots.

## What it is
A multiplayer space shooter where each sector is a corner of a 4-D hypercube. Portals link neighbouring corners; the asteroid fields and the portal links are regenerated from a new seed every match. Bots fill the empty slots and hunt across sectors.

## How it was made
One person directing AI coding agents (Claude Code with Unreal Engine 5.9 and its MCP server). I gave directions; the agents wrote the C++, imported the assets, built, tested, packaged and captured the media. Everything below happened on 14 November.

1. **Core game, basic shapes only.** A multi-agent workflow wrote the project: hypercube graph, deterministic sector generator, ships, combat, bots, HUD with a hypercube minimap. Only the match seed is replicated; server and clients build identical worlds from it and compare checksums. 9 automation tests, then a headless match: 29 kills and 49 portal jumps in about 140 seconds.
2. **Fab assets.** Star Sparrow ships (40 hulls, 10 colours), four Quixel Megascans boulders as asteroids, Niagara weapon effects. Rocks are drawn only; collision stays the generator's simple shapes so every machine agrees.
3. **A real sky.** 5,070 stars from the HYG catalogue, the Milky Way and Solar System planets from the engine's CelestialVault plugin, plus procedural nebulae.
4. **Black holes with gravity.** The pull runs inside the ship movement simulation as a pure function of position, so client prediction and server agree: 2 movement corrections in a test run, both the test teleports.
5. **First packaged build.** The first cook failed on editor-only MCP content; one config line fixed it. 503 MB zip, 87–109 fps on a GTX 950.
6. **Audio.** 47 Lyra sound waves and a small C++ sequencer that layers music stems by how intense the fight is.
7. **Power-ups and weapon pickups.** Repair, shield, overdrive, quad damage, cloak; scatter cannon, rail shot, homing missiles, rapid pulse. In a 213-second bot match, bots took 68 items.

8. **Wormholes.** Four two-way shortcuts between sectors that are not hypercube neighbours. Bots route through them: 33 portal jumps and 4 wormhole jumps in a 92-second match.
9. **Space stations with turrets.** Destructible turrets that track and lead their aim. After retuning they cause about 10% of deaths in bot matches.
10. **Menu and HUD.** Title menu, pause menu, help overlay, kill feed, scoreboard, end-of-match board. The 24 graphics errors in the packaged log turned out to be unused engine anti-aliasing shaders, not missing effects; one config line removed them.
11. **Seeing and hitting enemies.** I asked for better enemy visibility after looking at screenshots where ships were dark specks. Result: red brackets, off-screen arrows, a beacon dot on distant ships, soft target lock with a lead circle, a sector radar, and a small server-side aim assist. Bot hit rate went from about 21% to 58%.
12. **Feel.** Boost meter, faster braking, banking, spawn protection, three bot difficulty levels, weapon retune (pickup weapons now get more kills than the blaster in bot matches).

## Numbers
28 automation tests passing. 107–150 fps at 1280×720 on a GTX 950 (packaged, one instance). Zip 546 MB.

## Honest list
- No human has played it: handling, mouse aim, difficulty and the aim assist are judged from autopilot runs and still pictures.
- Nobody has listened to the audio mix; it was verified from logs and level measurements only.
- The video was captured without ffmpeg: the game dumps frames and a Python script stitches them, so the uploaded copy is low resolution. It is an autopilot with bots, not a person playing.
- The station body is made of plain boxes; the Fab station model (2.1 million triangles) was too heavy to bring in.
- Planned but not built: Epic Online Services lobby and leaderboards.

Assets: Star Sparrow by Ebal Studios, Quixel Megascans, Lyra Starter Game and Niagara Examples by Epic Games, Sci-Fi Modular Hover Vehicles (all via Fab); HYG star database by David Nash.
