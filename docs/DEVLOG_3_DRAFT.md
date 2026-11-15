# HypeCubeJam devlog #3 — phasers, warp, cloak, a hangar, a new look and Quick Play

**Status (15 Nov, build 0.17.0):** playable and uploaded. I played an earlier build for a few minutes and said enemies were hard to spot; apart from that, everything below was checked by automated tests, bot matches, logs and screenshots, not by play.

New trailer: https://youtu.be/iwO5n33EsUc

## What changed since devlog #2

1. **Phasers.** "Add phasers like in Star Trek." A held beam is now the default weapon: a four-second energy bank that refills, with the blaster on B. For normal and hard bots it became the main weapon (42–46% of kills); for easy bots it only ties with homing missiles.
2. **Breakable asteroids.** Phasers and torpedoes break rocks: big ones split, small ones are destroyed. The level layout every machine builds from the seed is untouched; the server only replicates the list of destroyed rocks. Host, client and a late joiner agreed on it rock by rock.
3. **Explosions with physics.** Flares, flying hull plates and rock chunks, a shockwave that pushes nearby ships, camera shake.
4. **Music from Lyria.** Three tracks generated with Google Lyria play on the menu, while exploring and in combat. Their endings don't loop, so the game cross-fades each track back into itself.
5. **Bigger, glowing enemies.** After playing I asked for enemies to be "more visible or bigger if possible". All ships are now 1.7× bigger; enemies have a red outline, a red engine trail and a point of light that keeps a distant ship from shrinking to a few pixels. The aim assist was trimmed so the bigger targets didn't raise the kill rate.
6. **A cinematic intro.** A ten-second clip generated with Google Veo plays before the title menu.
7. **A station model.** "Space Station 4" by Gerardo Justel (CC-BY) replaces the boxes. It only fits half of the collision shape, so some boxes still poke through.
8. **Warp drive, tractor beam, cloaking device.** Hold Z to charge and jump in a straight line; hold R to hold and drag your target; C to vanish, at the price of shields and weapons. The tractor pull runs inside the ship movement simulation, like the black holes, so the victim's own machine predicts it.
9. **A hangar with five ship types.** Interceptor, Fighter, Gunship, Scout and Cruiser, with different speed, hull, shields, torpedoes and systems, eight hulls each and ten colours.
10. **"Make it close to the cinematic intro."** The intro looked much better than the game, so the next request was to close the gap: layered nebula skies with a backlit core, rim-lit rocks with clouds of pebbles, long engine jets, star-shaped hit flares, portals with glowing runes, bloom and a teal-and-orange grade. It cost 8–12% of the frame rate.
11. **Smarter bots.** They pick targets by weakness instead of piling onto one victim, break off and turn a healthy shield side to the attacker, fire torpedoes through an open shield side, fly by ship type and steer clear of station turrets. Easy bots are about as lethal as before (14.1 kills a minute against 14.5); on hard, torpedoes went from 4% to 14% of kills.
12. **Playthrough tests.** Eight scripted full-match scenarios (match cycle, each difficulty, a small universe, host plus client plus late joiner, the hangar, every weapon scoring) now run as a suite. It found three real bugs: bots freezing at the edge of a station's range, several players all named "User", and a false "joined" status.
13. **Quick Play.** One button on the title menu: it searches Epic Online Services for an open match, joins the fullest one, and hosts a new one if there is none. Checked with two instances on one PC.
14. **Source on GitHub.** https://github.com/mariuz/HypeCubeJam (code, config, docs; the Fab asset packs are not included).

## Numbers
71 automation tests and 8 playthrough scenarios passing. About 118–122 fps at 1280×720 on a GTX 950 in a standard match (was about 134 before the look pass). Zip 613 MB.

## Honest list
- Almost no human play: a few minutes on one older build. Handling, balance, sounds, music and every effect in motion are judged from bot matches and still pictures.
- The trailer was cut by an agent and checked frame by frame, but nobody has watched or listened to it. Most of its scenes are staged with parked bots and the ship on autopilot.
- Ship balance shifted with the new bots: the Interceptor leads by about a third on easy and normal, the Gunship and Cruiser trail.
- The station model fits badly and its windows barely glow.
- The game still looks far from the intro: the rocks are the same four boulders, enemies are bright hulls rather than dark ships with red lights.
- This build cannot join matches hosted by builds before 0.15.0.
- Internet play still needs UDP port 7777 forwarded on the host; an Epic relay was not built.
- Quick Play and online play between two different players on two PCs are untested.
- Being held by a tractor beam causes small position snaps for the victim.

Made by one person directing AI coding agents (Claude Code with Unreal Engine 5.9). New credits: music generated with Google Lyria, intro video generated with Google Veo, "Space Station 4" by Gerardo Justel (CC-BY). Other assets as credited in devlog #1.
