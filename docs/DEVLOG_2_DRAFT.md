# HypeCubeJam devlog #2 — shields, torpedoes and going online

**Status (14 Nov, build 0.12.0):** playable and uploaded. Still no human play-test: everything below was checked by automated tests, bot matches, logs and screenshots.

## What changed since devlog #1

1. **Hints and a clearer HUD.** A short onboarding sequence and contextual hints (press H to turn them off), a controls page in the menu, and a tidier HUD.
2. **Epic Online Services.** The game signs in with a device ID, advertises hosted matches as lobbies, lists them under FIND GAMES, and sends kills, deaths, wins, turrets and matches to two leaderboards. Checked against the live service with two instances on one PC: sign-in, lobby search and join, stat ingest and both leaderboards all returned success. Not tested: two different players on two PCs, and play over the internet (the connection itself is plain IP, so a host needs UDP port 7777 open).
3. **Star Trek style shields.** I asked for "ship shield like in Star Trek". Ships now have four shield facings: fore, aft, port and starboard. A hit drains the side it lands on; when that side is down, hits from there reach the hull. The shield is an invisible bubble that flares where it is hit and flickers when a facing collapses. The HUD shows your four facings as arcs around a ship icon, and the target panel shows which side of the enemy is toward you. With four facings of 20 points bots died faster than before, because fights mostly come from one side; 26 per facing brought the kill rate back to within 1% of the old rules.
4. **Photon torpedoes.** Next request: "photon torpedoes like in Star Trek". Every ship carries four (right mouse button or X), with a two-second reload. A torpedo is slower than a blaster bolt and homes only weakly, so it can be dodged. It strips a full shield facing, and through a facing that is already down it takes 70 of 100 hull points, so the play is to open a side and then fire. A unit test caught a design fault before any match was run: at the first speed and arming distance, a boosting ship flew into most of its own blast. The first tuning also had bots getting 44% of all kills with torpedoes; after retuning it is 19%. There is an "INCOMING TORPEDO" warning on the HUD, and bots sometimes boost away from one.

## Numbers
49 automation tests passing. 106–130 fps at 1280×720 on a GTX 950 (packaged, one instance), the same range as before the shields and torpedoes. Zip 554 MB.

## Honest list
- No human has played it. Handling, aim, difficulty, the shield facings and the torpedoes are judged from bot matches and still pictures.
- Nobody has seen the shield bubble in motion or listened to the new sounds.
- Bots rarely turn a healthy shield facing toward an attacker, and bot fights are mostly head-on, so they say little about a player who manoeuvres.
- Torpedoes were tuned against easy bots only; normal and hard bots fire them far more readily and were not measured.
- The torpedo screenshots were taken with the game slowed down so the effect would be in frame.
- Hits from straight above or below count as hits on the fore shield.
- The gameplay video still shows the old HUD.
- Online play between two different players is untested.

Made by one person directing AI coding agents (Claude Code with Unreal Engine 5.9). Assets as credited in devlog #1.
