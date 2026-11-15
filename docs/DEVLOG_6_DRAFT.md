# HypeCubeJam devlog #6 — fly it with a controller

**Status (16 Nov, build 0.20.0):** playable and uploaded, Windows and browser. I played this build with a Steam Controller through Steam and it works. Everything else below was checked by automated tests, injected button presses, logs and screenshots.

Play in the browser: https://mariuz.github.io/HypeCubeJam/ (now 0.20.0 too) · Trailer: https://youtu.be/iwO5n33EsUc

## What changed since devlog #5

1. **Controller support.** "Add support for my Steam Controller (2015) and Xbox controller." The game had no gamepad binding at all. It now has a full layout, and keyboard and mouse keep working at the same time.
2. **The layout.** Left stick thrust and strafe, right stick aim, A / B up and down, RT fire, LT torpedo, LB boost, RB next target, Y nearest enemy, X switch weapon. D-pad: up warp, left cloak, right tractor beam, down drop weapon. R3 camera, L3 controls overlay, Back scoreboard, Start menu.
3. **Menus without a mouse.** D-pad or left stick moves a yellow frame over buttons, steppers and sliders; A presses, B goes back. On the title page A starts a match.
4. **Prompts that follow your hands.** Touch the controller and the on-screen hints switch to its button names ("[LT]", "[D-up] WARP"); touch the keyboard and they switch back. The CONTROLS page has a third column with the controller layout.
5. **A switch and a slider.** Settings has "Controller (Xbox / Steam Controller)" On / Off and a "Controller sensitivity" slider.
6. **Steam Controller.** It has no Windows driver of its own, so add HypeCubeJam.exe to Steam as a non-Steam game and pick the Gamepad template. The game then sees an Xbox controller.

## The second play session

Before the controller work I played build 0.19.0 twice on Easy. The first match under the new fairness rules ended 1 kill to 11 deaths, with no death from behind and 6 of 306 hits from the rear. The second ended 4 kills to 9 deaths, never more than one bot attacking me at a time, and the ship computer spoke 20 lines. The session before the rules was 2 kills to 30 deaths. I left the balance as it is.

## Numbers

81 automation tests pass (four new ones for the stick curve, the switch, menu focus and prompt names). A scripted run presses every controller button and checks what the ship did: 36 of 36 checks pass, in the editor and in the packaged build. Zip 585 MB.

## Honest list
- Only one controller has been tried: a Steam Controller through Steam, by me, for a short session. A real Xbox controller has not been plugged in.
- A controller in the browser build has not been tried.
- The aim stick has no invert option and no smoothing, and there is no vibration.
- Text boxes (seed, pilot name, address) cannot be filled with a controller, and two actions (hold target, hints) have no controller button.
- The packaged playthrough suite failed 2 of 8 scenarios once (a bot stuck for 21 seconds on its way to a pickup) and passed on the rerun. The cause is not known.
- Nobody has judged the computer voice by ear on purpose yet.
- Online play between two PCs is still untested, and internet play needs UDP port 7777 forwarded on the host.

Made by one person directing AI coding agents (Claude Code with Unreal Engine 5.9). Source: https://github.com/mariuz/HypeCubeJam
