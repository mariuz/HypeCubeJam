# HypeCubeJam devlog #5 — the ship computer speaks, and Epic remembers your record

**Status (15 Nov, build 0.19.0):** playable and uploaded. Checked by automated tests, autopilot matches, logs and screenshots. Nobody has heard the voice and nobody has played this build yet.

## What changed since devlog #4

1. **A computer voice.** "Add computer voice for a few hints, not annoying." The ship computer now speaks twelve short warnings: "All systems online", the four shield sides going down, "Hull critical", "Torpedo incoming", "Missile locked on", "Enemy behind", "Gravity well. Boost away", "Tractor beam. Boost free" and "Sector clear".
2. **Rules so it does not nag.** One voice at a time, a pause after every line, a cooldown of 30 to 90 seconds per line, and no more than five lines in any minute. Lines that lose are dropped, never queued. It is silent on the menu, after your ship dies and between matches. A reviewer simulated a bad match (a death every 20 seconds, an enemy always behind) before any code was written; the first rules spoke about seven times a minute, so they were tightened.
3. **A switch.** Settings has a "Computer voice (spoken warnings)" row, on by default.
4. **A voice we may ship.** The lines were recorded with Flite, the speech synthesiser from Carnegie Mellon University that comes with Unreal Engine, because its licence is on disk. The Windows voice sounded fine as well, but no terms on this PC cover shipping its audio.
5. **Your pilot record.** The game already sent kills, deaths, wins and matches to Epic Online Services. It now reads them back and shows your record on the title menu, the leaderboards page and the end-of-match board.
6. **An open-matches counter.** The title menu shows, for example, "1 open match, 2 pilots flying", refreshed every 45 seconds.
7. **Richer FIND GAMES rows.** Each row now shows the bot difficulty, the frag limit, the time left and who is flying. Builds 0.18.0 and 0.19.0 can still find and join each other.

## Numbers

In a packaged Easy match with an autopilot the voice spoke 10 lines in 198 seconds (3.0 a minute) and declined 14 requests, never more than five in a minute. 77 automation tests pass, and the 8 playthrough scenarios pass on the packaged build. Zip 585 MB.

## Honest list
- Nobody has heard the voice: wording, loudness against the music and how a cut-off line sounds are all unknown. The logs only prove that lines were triggered and limited.
- "Aft shield down" and "Port shield down" were never spoken in any test run; they share code with the two that were.
- The eleven review findings were minor and are fixed, most of them checked by reading the code only.
- Online play between two different players on two PCs is still untested, and internet play still needs UDP port 7777 forwarded on the host.
- The browser build (https://mariuz.github.io/HypeCubeJam/) is now 0.19.0 too, bots only. Its menu and a match were checked in Chrome; its sound and the voice were not.

Made by one person directing AI coding agents (Claude Code with Unreal Engine 5.9). New credit: voice by the Flite speech synthesiser, Carnegie Mellon University. Source: https://github.com/mariuz/HypeCubeJam
