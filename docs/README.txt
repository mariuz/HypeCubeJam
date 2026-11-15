HypeCubeJam - multiplayer space shooter on a randomly connected hypercube of sectors.

Start:  HypeCubeJam.exe            plays a 10 s intro (any key, mouse button or gamepad button skips it;
                                    -HCJNoIntro starts without it), then opens the title menu.
                                    PLAY hosts a match with bots (one click).
                                    HANGAR: choose your ship - type, hull and paint (see "Ship types").
                                    The menu also has: number of bots, hypercube size (3D-5D), bot difficulty,
                                    seed, pilot name, JOIN by address, CONTROLS (all keys), SETTINGS (mouse
                                    sensitivity, volumes, hints, camera shake, enemy highlight, cinematic look,
                                    music), QUIT.
                                    Enter = PLAY.
Join:   type the host's address in the menu and press JOIN,
        or start with the address:  HypeCubeJam.exe <ip>       e.g. HypeCubeJam.exe 127.0.0.1
Find games:  FIND GAMES in the menu lists the matches other players host (through Epic Online Services) with a
        JOIN button. A match you host with PLAY is listed for the others automatically. Games are joined by
        network address: on one network that just works; over the internet the host must forward UDP port 7777
        and start with   HypeCubeJam.exe -HCJPublicAddress=<its public address>
Leaderboards:  LEADERBOARDS in the menu shows the top pilots by kills and by wins and your own row. Your kills,
        deaths, wins, destroyed turrets and matches are sent when a match ends and when you leave; the boards
        follow a few minutes later. A pilot is the Windows user of a PC (no account, no login).
        When the online service cannot be reached the menu says "Online services unavailable"; hosting and
        joining by address work without it.  -HCJNoEOS switches the online part off.
Host without the menu:              HypeCubeJam.exe -AutoHost
Window: add  -windowed -ResX=1280 -ResY=720

Goal: free-for-all. Portals lead to neighbouring corners of the hypercube. First to 25 kills wins the match;
      then a new hypercube is generated and the next match starts.

Controls: mouse aim (the ship flies where you look), W/S/A/D thrust, Space/E up, Ctrl/Q down,
          left mouse fire, right mouse (or X) photon torpedo, Shift boost (a meter: 3.5 s, then it recharges),
          T next target, F nearest enemy, middle mouse holds the target,
          hold left mouse for the phaser beam, B switches phaser / blaster,
          G drops a pickup weapon (back to the phaser), V camera,
          hold Z: warp drive, C: cloak on / off, hold R: tractor beam on the target,
          Tab scoreboard, F1 controls overlay, H hints on / off, R or Enter or fire: respawn early,
          Esc menu (resume, hangar, settings, controls, leave to the title menu, quit).

Hints: the first ship gets key prompts at the bottom of the screen (move, fire, boost, target keys); each goes
away when you have done it. Later, short tips appear near portals, wormholes, pads, stations and black holes, when
the hull is low (with a green marker to a repair pad), when the sector is empty, and after a death. Each is shown
only a few times. H or SETTINGS switches them off.

Enemies glow: every other ship has a red rim, a red engine glow and trail, and from a distance a bright red glint
that never gets smaller on the screen (your own ship keeps its own colour). SETTINGS > Enemy highlight switches the
rim and the big glint off. A cloaked ship shows none of it: only a faint shimmer of its hull.
SETTINGS > Cinematic look (on by default) is the bloom, the colour grade and the full amount of drifting pebbles;
switch it off for a plainer, slightly faster picture. The pebbles and the far boulders are scenery: you fly through them.
On screen: every enemy in the sector has a heavy red bracket with name, distance and hull (dimmer when behind a rock);
red arrows at the screen edge point to enemies out of view. The enemy nearest your crosshair is the target
(orange diamond): put the crosshair on its yellow lead circle - it turns green and says FIRE when you are on it.
Homing missiles go for the target. A red arc shows where damage comes from. Bottom left: your ship inside the
four arcs of its shield, hull and boost bars, above them the radar (you in the middle, forward is up; red = enemies, squares = portals) with the number of
enemies in the sector, or the way to the nearest enemy when the sector is empty. Bottom right: weapon, ammunition
and running power-ups. Top left: sector, your kills against the frag limit and who leads. Top right: the map of
the hypercube (legend below it) and the kill feed. Labels over portals and wormholes say where they lead; a banner
names each sector you enter and what is in it.

Shields: four facings - fore, aft, port, starboard - the arcs around the little ship at the bottom left. A hit
drains the side it lands on (the bubble around the ship flares there); when a side is down (red arc) hits from
that side go straight to the hull, so turn a fresh side to the enemy. A side comes back 4 s after its last hit.
The target panel on the right shows the same four arcs for your target and which side faces you: aim for a weak one.
Photon torpedoes: every ship carries a few (TORPEDOES at the bottom right: one pip each, a reload bar). Right mouse
fires one; it flies slower than a bolt and steers a little for your target. A full shield facing stops most of it
(the facing is gone, the hull barely scratched); through a facing that is down it nearly finishes a ship - so strip
a side with the guns first, or put two on the same side. It arms after 12 m and has a small blast. A TORPEDOES pad
restocks. When one is after you the screen says INCOMING TORPEDO and marks where it is: boost across its path (it
turns slowly), or take it on a full side.
Phaser: the weapon every ship starts with. Hold the left mouse button: a beam that hits at once, bends a little
towards an enemy near the crosshair and is strongest up close. It lands on the shield facing it touches. It drains
the phaser bank (the bar under WEAPON, bottom right: about 4 s of beam, refilling when you let go); with the bank
empty the same button fires the blaster until the bank is back at the mark. B switches between phaser and blaster;
a pickup weapon takes the place of the phaser and gives it back when it is empty or dropped (G).
Rocks: phasers and torpedoes break asteroids - big ones into two or three pieces, small ones for good (bolts do
nothing to rock). The field is whole again when the next match starts.
Explosions push the ships around them; SETTINGS has a switch for the camera shake.

Ship types (HANGAR in the title menu, and under Esc for your next ship): five types, each with eight hulls and ten
paints. The hull turns next to the page; bars show speed, agility, hull, shields, firepower and systems.
  Interceptor  fastest and most agile, light hull, phasers that recharge quickly, 2 torpedoes
  Fighter      the all-rounder, 4 torpedoes
  Gunship      slow, thick hull, fore armour (hits from the front do less), harder phasers and bolts, 6 torpedoes
  Scout        fast, longest boost and cloak, the quickest warp drive, light hull, weaker guns
  Cruiser      biggest and slowest, strongest shields all round, the strongest and longest tractor beam
The scoreboard, the kill feed and the target panel show the type of every ship. Bots fly a mix of all five.
Warp drive: point the nose where you want to go and hold Z. The drive charges for 1.6 s (WARP CHARGING), then the ship
jumps ahead in a straight line at 150 m/s for as long as you hold the key, 2.2 to 3 s at most; then it must cool down
(6 to 16 s by ship type; WARP row at the bottom right). No weapons while it charges or runs; a hit while it charges
stops it; it drops out by itself before rock and at the edge of the sector, and a ship that still runs into something
at warp is damaged. A portal can be entered at warp: you come out of it at boost speed. No warp while cloaked or while
a tractor beam holds you.
Cloak: C raises and drops it. Cloaked, other pilots see only a faint shimmer - no marker, no radar blip, no lock, no
aim assist - while the CLOAK meter lasts (8 to 20 s by ship type; it refills slowly). Your shields are down while
cloaked and come back from nothing afterwards, and weapons are off: pulling the trigger drops the cloak, weapons work
a second later. A hit, an enemy within 15 m or a tractor beam reveals you for a moment (CLOAK REVEALED). A CLOAK pad
fills the meter and stops the drain for 8 s.
Tractor beam: hold R with a target within reach (60 to 85 m by ship type, in front of you, nothing in between): a
blue-white beam holds it - it is pulled towards you and can only fly at about half speed - while you shoot it. The
beam feeds on the phaser bank (about 3 s from a full bank, 5 s on a cruiser). It lets go when the target boosts for
about a second, when you take a hard hit, when rock gets in between or the target leaves the beam's reach. Caught in
one yourself (CAUGHT IN TRACTOR BEAM): hold Shift, or shoot the ship that holds you.
A fresh ship has a 2 s spawn shield (lights shimmer white) until it fires. Bot difficulty (easy / normal / hard) is set in the menu; easy is the default.
Fly through the portals - rings with a swirl in the colour of the sector they lead to - to reach neighbouring
sectors; from afar a portal is a glint in that colour. Stay away from black holes.
Space stations (two wheels on an axle, with a mast and panels) have gun turrets that shoot every ship in range;
turrets can be destroyed.
Bright ringed spheres are wormholes: shortcuts to far sectors (dashed lines on the map).
Glowing pads hold pickups: repair, shield cell, overdrive (speed), quad damage, cloak cell, torpedoes, and four
limited-ammo weapons (scatter cannon, rail shot, homing missiles, rapid pulse). Fly over a pad to take it.
Host options without the menu (append to the exe as one argument):
  "/Engine/Maps/Entry?listen?Seed=1234?Dim=4?Bots=8?Difficulty=0?Pickups=1?Wormholes=4?Stations=1"

Music: three tracks generated with Google Lyria (Gemini) - title menu, exploring, combat. SETTINGS can switch to the
older layered stems. Sound effects (and those stems) are from Epic's "Lyra Starter Game". Volumes: SETTINGS in the menu.

Credits: Space Station 4 by Gerardo Justel (CC-BY). Intro video generated with Google Veo (Gemini). Music generated
with Google Lyria (Gemini). Everything else: see NOTICES.txt.
