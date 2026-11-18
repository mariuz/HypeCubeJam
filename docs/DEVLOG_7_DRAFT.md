# HypeCubeJam devlog #7 — an easier Easy, and pickups you can see

**Status (18 Nov, build 0.21.0):** playable in the browser at https://mariuz.github.io/HypeCubeJam/. The Windows download on itch stays at 0.20.0, because new uploads are locked during the jam. The numbers below come from automated tests and an autopilot, not from a person playing.

Trailer: https://youtu.be/iwO5n33EsUc · Source: https://github.com/mariuz/HypeCubeJam

## Why

"Improve the easy mode gameplay, make the health/weapons packs more visible." My last two Easy matches ended 1 kill to 11 deaths and 4 to 9. The fairness rules worked: one bot at a time, from the front, nobody killed at the spawn. But I lost each duel after about 45 seconds. In those two matches I took 3 pickup pads and the bots took 230. Eight of my nine deaths in the second match came from a weapon a bot had picked up, while I was still flying with the starting phaser.

## What changed

1. **Easy helps a beginner.** Your hits do 1.5× damage to bots. Bots do 40% of their damage to you (was 50%). Your hull mends 4 points a second after 5 seconds without being hit, up to 75%. A repair pad now restores the hull fully. Normal, Hard and Ace are unchanged, and bots fight each other as before.
2. **Pads in three colours.** Green for health (repair, shield cell), amber for weapons (scatter, rail, missiles, pulse, torpedoes), magenta for power-ups (overdrive, quad damage, cloak).
3. **Light columns.** A filled pad is larger and has a 42 m column of light in its colour, with a pulsing halo around the item, so you can see it from across the sector.
4. **Markers with distances.** The HUD marks the six nearest pads within 450 m, for example "REPAIR 120 m". The radar has a cross, diamond or triangle for each kind.
5. **A way home when you are hurt.** Below 35% hull, the nearest repair pad is drawn larger inside a ring. If it is off screen, three chevrons on the edge of the screen point to it. This is now always on, not only with hints.

## Numbers

An aiming autopilot against 7 bots on Easy, six 4-minute matches before and six after: kills a minute went from 1.32 to 2.39, deaths a minute from 0.99 to 0.58, and kills per death from 1.33 to 4.14. It is still a fight: the autopilot died 14 times in those six matches. 84 automation tests pass, and builds 0.20.0 and 0.21.0 can still join each other.

## Honest list
- No person has played 0.21.0 for this report. The autopilot aims far better than a beginner, so whether Easy is now right for a beginner is a guess.
- Whether the hull mending feels noticeable has not been tested. It has no sound or HUD cue of its own; the bar just rises.
- The columns were judged from still pictures and one browser match. A magenta column and a pad being emptied and refilled were not seen in the browser.
- Frame cost was not measured against 0.20.0.
- The low-hull pointer and the enemy arrows can overlap at the screen edge.
- The itch download is 0.20.0 until uploads open again; the browser build is 0.21.0.

Made by one person directing AI coding agents (Claude Code with Unreal Engine 5.9).
