"""Edit decision list of the trailer (read by make_trailer.py).

  kind 'card'  : a title card, 'frames' long, lines = [(text, font file, size, colour, y)]
  kind 'intro' : the intro movie from movie frame 'first' to 'last' (24 per second), game frames merged from several runs
  kind 'clip'  : frames a..b (inclusive) of the frame dump capture/<src>, shown 1:1 (30 game frames per second)
  cap = caption ("HEAD | rest"), pos = 'low' (bottom left) or 'hud' (left, above the radar of the game's HUD)
  xf = frames of cross-fade from the cut before (0 = hard cut)
"""
import os

LYRIA = r'F:\work\HypeCubeJam\Saved\LyriaWav'
CYAN, WHITE, GREY = (124, 240, 255), (255, 255, 255), (150, 175, 190)

CUTS = [
    {'kind': 'card', 'frames': 66, 'fade_in': 10, 'fade_out': 8, 'lines': [
        ('A 2026 EPIC AI MEGAJAM ENTRY', 'seguisb.ttf', 30, CYAN, 330),
        ('gameplay captured from the Windows build 0.17.0', 'segoeui.ttf', 20, GREY, 384)]},
    # 1 - the game's start-up intro (Veo movie + the title the game fades in over it)
    {'kind': 'intro', 'first': 38, 'last': 227, 'cap': 'INTRO | as the game plays it at start-up', 'pos': 'low', 'xf': 0},
    # 2 - title menu, hangar (the match behind the menu is muted: menu music only)
    {'kind': 'clip', 'src': 'menu', 'a': 52, 'b': 146, 'cap': 'QUICK PLAY | one click into an open match', 'pos': 'low', 'xf': 10, 'mute': True},
    {'kind': 'clip', 'src': 'menu', 'a': 176, 'b': 329, 'cap': 'HANGAR | five ship types, forty hulls', 'pos': 'low', 'mute': True},
    {'kind': 'clip', 'src': 'menu', 'a': 345, 'b': 424, 'cap': 'HANGAR | five ship types, forty hulls', 'pos': 'low', 'mute': True},
    # 3 - dense field of sector 0101 under boost
    {'kind': 'clip', 'src': 'field_a', 'a': 98, 'b': 232, 'cap': 'ASTEROID FIELDS | boost through sixteen sectors', 'pos': 'hud'},
    # 4 - phaser until the facing is down, torpedo through it
    {'kind': 'clip', 'src': 'phaser', 'a': 98, 'b': 180, 'cap': 'PHASERS | burn down one shield facing', 'pos': 'hud'},
    {'kind': 'clip', 'src': 'phaser', 'a': 181, 'b': 252, 'cap': 'PHOTON TORPEDO | through the open facing', 'pos': 'hud'},
    # 5 - rock
    {'kind': 'clip', 'src': 'rock', 'a': 108, 'b': 245, 'cap': 'BREAKABLE ROCK | phasers and torpedoes cut through cover', 'pos': 'hud'},
    # 6 - tractor beam, both ends
    {'kind': 'clip', 'src': 'tractor_a', 'a': 106, 'b': 218, 'cap': 'TRACTOR BEAM | hold a target in place', 'pos': 'hud'},
    {'kind': 'clip', 'src': 'tractor_b', 'a': 112, 'b': 214, 'cap': 'CAUGHT IN A BEAM | boost to break free', 'pos': 'hud'},
    # 7 - cloak
    {'kind': 'clip', 'src': 'cloak_a', 'a': 100, 'b': 246, 'cap': 'CLOAKING DEVICE | no marker, no lock, shields down', 'pos': 'hud'},
    {'kind': 'clip', 'src': 'cloak_a', 'a': 280, 'b': 362, 'cap': 'CLOAKING DEVICE | no marker, no lock, shields down', 'pos': 'hud'},
    # 8 - warp, portal
    {'kind': 'clip', 'src': 'warp_b', 'a': 86, 'b': 212, 'cap': 'WARP DRIVE | charge, jump, drop out', 'pos': 'hud'},
    {'kind': 'clip', 'src': 'portal', 'a': 104, 'b': 262, 'cap': 'PORTALS | to a neighbouring corner of the hypercube', 'pos': 'hud'},
    # 9 - scenery
    {'kind': 'clip', 'src': 'station', 'a': 106, 'b': 246, 'cap': 'STATIONS | guarded by turrets', 'pos': 'hud'},
    {'kind': 'clip', 'src': 'saturn', 'a': 122, 'b': 216, 'cap': 'SATURN | a different sky in every sector', 'pos': 'hud'},
    {'kind': 'clip', 'src': 'hole', 'a': 100, 'b': 190, 'cap': 'BLACK HOLES | mind the gravity well', 'pos': 'hud'},
    # 10 - a real bot match
    {'kind': 'clip', 'src': 'fight2', 'a': 58, 'b': 548, 'cap': 'BOT MATCH | real time, player on autopilot', 'pos': 'hud'},
    # 11 - end card
    {'kind': 'card', 'frames': 186, 'fade_in': 12, 'fade_out': 20, 'xf': 0, 'lines': [
        ('HYPECUBE JAM', 'seguibl.ttf', 84, CYAN, 262),
        ('mariuz.itch.io/hypecubejam', 'seguisb.ttf', 38, WHITE, 352),
        ('made for the 2026 Epic AI MegaJam with Unreal Engine 5.9 and AI coding agents', 'segoeui.ttf', 24, GREY, 420),
        ('All pictures are from the game. Scenes were set up with development commands; ships are flown by the autopilot and bots.', 'segoeui.ttf', 17, (135, 155, 170), 664)]},
]

# The combat track has two hits after a break, at 27.5 s and 54.9 s. With offset 4.33 s they fall on 42.87 s (the parked
# cruiser cloaks) and 70.27 s (the cut to the bot match).
MUSIC = {
    'tracks': [
        {'file': os.path.join(LYRIA, 'MX_Lyria_Menu_Starward_Bound.wav'), 'from': 0.0, 'to': 21.7, 'offset': 0.0, 'fade_in': 0.6, 'fade_out': 2.0},
        {'file': os.path.join(LYRIA, 'MX_Lyria_Combat_Stellar_Breach.wav'), 'from': 19.7, 'to': 999.0, 'offset': 4.33, 'fade_in': 2.0, 'fade_out': 3.5},
    ],
    'bed_peak_db': -4.0,     # the music bed alone
    'sfx_db': -7.0,          # the game's recorded effects relative to their recorded level
    'final_peak_db': -3.0,   # the finished mix
}
