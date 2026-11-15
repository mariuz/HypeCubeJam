# HypeCubeJam devlog #4 — I played it, died 30 times, and the bots learned some manners

**Status (15 Nov, build 0.18.0):** playable and uploaded. This is the first build shaped by a real play session: 9.4 minutes on Easy against 7 bots. Everything else below was checked by automated tests, autopilot matches, logs and screenshots.

Play in the browser: https://mariuz.github.io/HypeCubeJam/ (still build 0.17.0) · Trailer: https://youtu.be/iwO5n33EsUc

## The play session

I played one match on Easy and then asked the agents to read the log. It said: 2 kills, 30 deaths. All seven bots killed me at least once, eight deaths came within 12 seconds of the previous one, and I lasted 4.5 seconds once a bot opened fire. My request afterwards was "make it more fun … try to have less bots that shoots you from the back".

## What changed since devlog #3

1. **Four difficulty levels.** Easy, Normal, Hard and Ace now sit on the first row of the title menu, each with a one-line description, and the host can change the level during a match. Ace is the old behaviour with no fairness rules.
2. **Bots that fight fair against humans.** On the lower levels only a limited number of bots may attack one human at a time (one on Easy, two on Normal, three on Hard), a bot arriving behind you waits before it fires and aims worse from there, new arrivals give you a grace period, and after you shake an attacker off there is a short respite. Bots still fight each other exactly as before.
3. **Warnings and an honest death recap.** "ENEMY BEHIND" appears before the shooting starts, "HIT FROM BEHIND" when it lands, and the death recap says when the killer was behind you and what to do about it.
4. **Longer spawn protection** (3 seconds, was 2) and spawn points further from enemies.
5. **A hypercube map you can read.** "Improve the hypercube visualization on the right." The map was a tangle of dots; it is now drawn as cubes (one for 8 sectors, two for 16, four for 32). Your sector has a white ring, neighbouring sectors have thick lines and their ids, enemies show as red count badges, the nearest fight pulses, and the route to your target is drawn hop by hop.
6. **A staged ship explosion.** "Improve particles when my ship explodes." The hull now fails and glows first, then come a flash and a shockwave ring, a fireball with flying hull plates, cooling flames, dark smoke with embers and drifting wreckage, seen from a death camera that pulls back.

## Numbers

Easy, one autopilot "human" against 7 bots, old behaviour against new, same build:

| | Before | After |
|---|---|---|
| Deaths per minute, pilot that aims | 2.45 | 1.06 |
| Deaths per minute, pilot that never finds the enemy | 2.53 | 1.22 |
| Share of bot damage from behind (same pilot) | 45.7% | 3.0% |
| Deaths within 12 s of spawning (pilot that aims) | 20 of 60 | 0 of 26 |
| Median seconds alive (pilot that aims) | 18 | 41 |

Normal went from 4.65 to 2.69 deaths a minute for the pilot that aims. It is still clearly harder than Easy, and a third of its deaths still come within 12 seconds of spawning.

73 automation tests pass, and the 8 playthrough scenarios pass on both the editor build and the packaged build. Zip 585 MB.

## Honest list
- The new rules were measured with autopilots only. I have not played this build yet.
- "Before" is the same build with the rules switched off, not build 0.17.0, and it already has the longer spawn protection.
- The "ENEMY BEHIND" warning has no sound of its own; it reuses the warning tick at a lower pitch, and nobody has heard it.
- Hard is almost unchanged apart from fewer hits from behind, and was not re-measured after the last fixes.
- The explosion's frame cost was measured at 1280×720 only (2.4 ms more per frame in the worst half second on a GTX 950). The fireball looks flat and the "KILLED BY" text lies over it.
- The 5-D map with 32 sectors is dense.
- The browser build is still 0.17.0 and has none of this.
- Internet play still needs UDP port 7777 forwarded on the host; Quick Play between two PCs is untested.

Made by one person directing AI coding agents (Claude Code with Unreal Engine 5.9). Source: https://github.com/mariuz/HypeCubeJam
