# HypeCubeJam devlog #8 — Super Easy, a gentler Easy, and torpedoes that follow

**Status (24 Nov, build 0.22.0):** playable in the browser at https://mariuz.github.io/HypeCubeJam/, and as a Windows download on the GitHub release: https://github.com/mariuz/HypeCubeJam/releases/tag/v0.22.0. The itch download stays at 0.20.0, because new uploads are locked during the jam.

Trailer: https://youtu.be/iwO5n33EsUc · Source: https://github.com/mariuz/HypeCubeJam

## Why

After devlog #7 I played a match of 0.21.0 on Easy myself. I finished with 1 kill and 4 deaths, and I took no pickup pads at all. I could see the light columns, but in a fight I never flew to one, so I kept the starting phaser the whole match. Easy has to help a player who aims badly and ignores the pads. I also asked for one thing: "photon torpedo should aim to close and follow the target".

## What changed

1. **You start armed on Easy.** Each new ship has a full scatter cannon. Its ten pellets cover a beginner's aim at the short range where my duels happened.
2. **You take pads in passing.** Flying within 25 m of a filled pad collects its item, so you don't need to touch the pad.
3. **A light aim assist on Easy.** A bolt that would pass within 3° of a ship is sent to where that ship will be. The phaser beam bends a little too. Bots get no assist.
4. **A way to a weapon.** While you have only the phaser, a ring and edge chevrons point to the nearest weapon pad.
5. **Super Easy**, a new level that comes first in the menu. It has my first, stronger numbers: a 5° assist, your hits on bots do 1.5×, and their hits on you do 40%. In my first match with those numbers I won 16 to 1, which was too easy for Easy. So Easy got 3°, 1.35× and 45%, and the strong version became its own level.
6. **Photon torpedoes that close and follow**, for every player on every level. A torpedo takes the locked ship, or else the nearest ship ahead. It turns 180° a second, aims where the ship is going and keeps hold of it. Bot torpedoes are unchanged, so the bots don't get harder.

## Numbers

I measured with an autopilot that aims badly on purpose, against 7 bots:

| Autopilot's aim error | Easy, kills per death | Super Easy, kills per death |
|---|---|---|
| 8° | 3.1 (a death every 1.6 min) | 49 |
| 15° | 1.3 | 5 |

In my own match on the new Easy, I scored 5 kills to 2 deaths in 2 minutes. I took 2 pads by flying past them, and 4 of my 6 torpedoes hit. 87 automation tests pass.

## Honest list
- These numbers come from one player in three short matches and from an autopilot. No one else has played Super Easy yet.
- The weapon-pad pointer appears only for the host or in single player. A player who joins someone else's game doesn't see it.
- After the engine update, 0.22.0 can't play online with 0.21.0 or older builds, including the 0.20.0 itch download.
- The aim assist is easy to notice once you know it's there. Whether it feels like cheating hasn't been asked of anyone.

Made by one person directing AI coding agents (Claude Code with Unreal Engine 5.9).
