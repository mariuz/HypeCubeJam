# HypeCubeJam devlog #9 — 0.23.0: the weapon pointer online, and clean builds again

**Status (25 Nov, build 0.23.0):** playable in the browser at https://mariuz.github.io/HypeCubeJam/, and as a Windows download on the GitHub release: https://github.com/mariuz/HypeCubeJam/releases/tag/v0.23.0. The itch download stays at 0.20.0, because new uploads are locked during the jam.

Trailer: https://youtu.be/iwO5n33EsUc · Source: https://github.com/mariuz/HypeCubeJam

A small release that closes two points from devlog #8's honest list.

## What changed

1. **The weapon pointer now works for a player who joins someone else's game.** On Easy and Super Easy, while you only have the phaser, a ring and edge chevrons lead to the nearest weapon pad. Before, the HUD asked the server's game mode for the level, and a joining player's game doesn't have one, so only the host or a single player saw the pointer. Now the server sends one replicated flag with the game state. I checked it with a host and a joining client on one PC: the client's log shows the pointer switching on and off as its ship ran out of scatter-cannon shots, and a screenshot shows the ring on the pad 120 m away.
2. **Clean builds after the engine update.** Since the 22 Nov engine update, every cook stopped on an engine check about `/ToolsetRegistry/`, and the 0.22.0 builds needed `-IgnoreCookErrors`. The cause was a new engine plugin, `AllToolsets`, which is on by default and pulled in 38 extra editor toolsets. One of them loads a plugin whose content rule clashes with a line in the project's config. The project now switches `AllToolsets` off and keeps its own eight toolsets, so the editor registers the same 62 toolsets as before the update. Both the Windows and the web build now cook with **0 errors** without the flag. As a side effect, the Windows exe is 18.5 MB smaller and the web build's wasm is 4.4 MB smaller.
3. **A guard for the flag.** If a build ever needs `-IgnoreCookErrors` again, `docs/tools/cook_error_guard.py` reads the build log and fails on any cook error other than that one known engine check.
4. **The itch page got a new look**: a banner and a background made from the game's intro, and a refreshed description.

## Numbers

- 88 automation tests pass (one new test for the pointer).
- The packaged playthrough suite passes 8 of 8: bots on easy, normal and hard, four sectors, a client, a late joiner.
- The web build passes the browser check, and its largest file stays just under GitHub's 100 MB file limit.

## Honest list

- **0.23.0 can play online only with 0.23.0.** The new flag changes what the server sends, so I raised the project version, and the engine refuses a join between 0.23.0 and 0.22.0 in both directions. FIND GAMES and QUICK PLAY between the two versions through Epic Online Services were not tried; by the code they show the other build as "other version".
- The pointer was tested on Easy only, on one PC, with my autopilot. Nobody has joined a real game over the internet to see it.
- The itch download is still 0.20.0. Use the browser version or the GitHub release for the current build.

Made by one person directing AI coding agents (Claude Code with Unreal Engine 5.9).
