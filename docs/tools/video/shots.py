"""The staged capture runs of the trailer: python shots.py <shot name>... (or "all").

Each entry: frames to dump, game seconds before the dump starts, {dumped frame: console command(s)}, extra switches.
Every run is the packaged build as a listen host (seed 1234, 7 bots) at a fixed 30 fps game step. PRE holds the bots
still ("peace") - everything staged with it is a set-up scene, not a fight.
"""
import subprocess, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
MUTE = 'hcj.Audio.MusicVolume 0'   # the trailer has its own music bed; the game's effects are kept
# The dump starts with the game (start 0), the first commands run 1.5 s in: before any bot has done anything.
# "help" closes the controls page that shows itself on a first ship when hints are off.
PRE = MUTE + ' | HCJMenuCmd help | HCJMenuCmd skiptutorial | HCJMenuCmd peace'
VERBOSE = ['--arg', '-LogCmds="LogHCJ Verbose"']

SHOTS = {
    # title menu, hangar with three ship types, back
    'menu': (540, 3, {45: MUTE + ' | HCJMenuCmd main', 150: 'HCJMenuCmd hangar', 165: 'HCJMenuCmd ship 0', 255: 'HCJMenuCmd ship 4',
                      345: 'HCJMenuCmd ship 2', 435: 'HCJMenuCmd ship 1', 495: 'HCJMenuCmd main'}, ['--menu', '--nourl']),
    # flying through the dense field of sector 0101 under boost, towards its backlit core cloud
    'field_a': (300, 0, {45: PRE + ' | HCJMenuCmd scene 5,backlight,6', 75: 'HCJMenuCmd fly 2'}, []),
    'field_b': (300, 0, {45: PRE + ' | HCJMenuCmd scene 5,backlight,3', 75: 'HCJMenuCmd fly 2'}, []),
    # the own phaser on a parked cruiser until the facing is down and the hull is at 62, then a torpedo
    'phaser': (300, 0, {45: PRE + ' | HCJMenuCmd scene 1,nebula0', 50: 'HCJParkEnemy 2500 0 0 0 | HCJMenuCmd enemyship 4,-1,-1,0',
                        120: 'HCJMenuCmd fire 1', 180: 'HCJMenuCmd fire 0 | HCJMenuCmd torpedo'}, VERBOSE + ['--step', '5']),
    # hero picture: in front of the core cloud of sector 0000 the own beam on a parked fighter until it blows up,
    # a second parked ship firing its beam at the own ship (which is undamageable here)
    'hero': (300, 0, {45: PRE + ' | HCJMenuCmd scene 0,backlight', 50: 'HCJParkEnemy 2500 0 0 0 | HCJMenuCmd enemyship 1,-1,-1,0',
                      55: 'HCJParkEnemy 3300 1200 350 1', 60: 'HCJMenuCmd enemyship 4,-1,-1,1',
                      120: 'HCJMenuCmd fire 1 | HCJMenuCmd enemyfire 1,1,-1'}, VERBOSE + ['--step', '5']),
    # a rock broken by command, its pieces then shot with the phaser and a torpedo
    'rock': (330, 0, {45: PRE + ' | HCJMenuCmd scene 5,nebula1', 75: 'HCJMenuCmd breakrock 500', 120: 'HCJMenuCmd fire 1',
                      210: 'HCJMenuCmd fire 0 | HCJMenuCmd torpedo'}, VERBOSE),
    # tractor beam on a parked ship; in the second run a parked cruiser holds the own ship
    'tractor_a': (300, 0, {45: PRE + ' | HCJMenuCmd scene 3,nebula0', 50: 'HCJParkEnemy 4500 500 150 0', 120: 'HCJMenuCmd tractor 1',
                           240: 'HCJMenuCmd tractor 0'}, VERBOSE + ['--step', '5']),
    'tractor_b': (300, 0, {45: PRE + ' | HCJMenuCmd scene 3,nebula0', 50: 'HCJParkEnemy 6500 600 200 0 | HCJMenuCmd enemyship 4,-1,-1,0',
                           120: 'HCJMenuCmd enemytractor 1,0', 270: 'HCJMenuCmd enemytractor 0,0'}, VERBOSE + ['--step', '5']),
    # a parked cruiser cloaks 20 m from a parked second ship (inside 15 m it would stay "revealed"), decloaks; then the own ship cloaks
    'cloak_a': (420, 0, {45: PRE + ' | HCJMenuCmd scene 1,backlight', 50: 'HCJParkEnemy 2700 -1000 150 0 | HCJMenuCmd enemyship 4,-1,-1,0',
                         55: 'HCJParkEnemy 3000 1000 100 1', 120: 'HCJMenuCmd enemycloak 0', 225: 'HCJMenuCmd enemycloak 0',
                         285: 'HCJMenuCmd cloak', 400: 'HCJMenuCmd cloak'}, VERBOSE + ['--step', '5']),
    'cloak_b': (420, 0, {45: PRE + ' | HCJMenuCmd scene 6,nebula1', 50: 'HCJParkEnemy 2700 -1000 150 0 | HCJMenuCmd enemyship 4,-1,-1,0',
                         55: 'HCJParkEnemy 3000 1000 100 1', 120: 'HCJMenuCmd enemycloak 0', 225: 'HCJMenuCmd enemycloak 0',
                         285: 'HCJMenuCmd cloak', 400: 'HCJMenuCmd cloak'}, VERBOSE + ['--step', '5']),
    # which way do the portals and the wormhole of sector 0000 lie from each spawn point (the log has the directions)
    'scan0': (420, 0, dict([(45, PRE)] + [(60 + 6 * k, 'HCJMenuCmd scene 0,%s,%d' % (look, sp)) for k, (sp, look) in
                                           enumerate((sp, look) for sp in range(10) for look in ('portal0', 'portal1', 'portal2', 'portal3', 'wormhole'))]),
              ['--step', '3']),
    # warp: charge, jump, drop-out
    'warp_a': (300, 0, {45: PRE + ' | HCJMenuCmd scene 2,nebula2', 90: 'HCJMenuCmd warp 1', 240: 'HCJMenuCmd warp 0'}, VERBOSE),
    'warp_b': (300, 0, {45: PRE + ' | HCJMenuCmd scene 0,dark', 90: 'HCJMenuCmd warp 1', 240: 'HCJMenuCmd warp 0'}, VERBOSE),
    # portal jump under boost
    'portal': (330, 0, {45: PRE + ' | HCJMenuCmd scene 3,portal0,0,9000', 105: 'HCJMenuCmd fly 2'}, VERBOSE),
    # scenery
    'station': (300, 0, {45: PRE + ' | HCJMenuCmd scene 3,station,0,13000', 60: 'HCJMenuCmd peace 0 | HCJMenuCmd fly 1', 150: 'HCJMenuCmd fly 0'}, []),
    'saturn': (240, 0, {45: PRE + ' | HCJMenuCmd scene 3,body0', 60: 'HCJMenuCmd fly 1'}, []),
    'hole': (240, 0, {45: PRE + ' | HCJMenuCmd god | HCJGotoHole 0.8'}, []),
    'wormhole': (300, 0, {45: PRE + ' | HCJMenuCmd scene 0,wormhole,0,6000', 90: 'HCJMenuCmd fly 1'}, VERBOSE),
    # looking for a line through the dense field: every spawn point of sector 0101 in turn
    'scout5': (720, 0, dict([(45, PRE)] + [(60 + 80 * k, 'HCJMenuCmd scene 5,backlight,%d | HCJMenuCmd fly 2' % k) for k in range(8)]), ['--step', '5']),
    # looking for backdrops: sectors x looks, a cruiser parked 25 m ahead each time (24 frames per scene)
    'looks': (780, 0, dict([(45, PRE)] + [(60 + 24 * k, 'HCJMenuCmd scene %d,%s | HCJParkEnemy 2500 0 0 0 | HCJMenuCmd enemyship 4,-1,-1,0' % (sec, look))
                                           for k, (sec, look) in enumerate((sec, look) for sec in (0, 3, 5, 1, 2, 6) for look in ('backlight', 'nebula0', 'nebula1', 'nebula2', 'body0'))]),
              ['--step', '3']),
    # a real bot match: normal bots, the own ship on the capture autopilot
    'fight1': (1500, 0, {45: MUTE + ' | HCJMenuCmd help | HCJMenuCmd skiptutorial'}, ['--url', '/Engine/Maps/Entry?listen?Seed=1234?Difficulty=1',
                                                        '--arg', '-HCJAutoFly', '--arg', '-HCJAutoAim', '--arg', '-HCJAutoTorpedo']),
    'fight2': (1500, 0, {45: MUTE + ' | HCJMenuCmd help | HCJMenuCmd skiptutorial'}, ['--url', '/Engine/Maps/Entry?listen?Seed=77?Difficulty=1',
                                                        '--arg', '-HCJAutoFly', '--arg', '-HCJAutoAim', '--arg', '-HCJAutoTorpedo']),
}
# hero pictures: the same staging as 'hero', but aimed at portal 0 from a spawn point from which the core cloud is
# 17 / 21 degrees off the aim, so that the target is not in front of its white middle; with the HUD and without (ShowHUD)
for _sp in (0, 8):
    for _hud in (True, False):
        _at = {45: PRE + ' | HCJMenuCmd scene 0,portal0,%d' % _sp, 50: 'HCJParkEnemy 2500 0 0 0 | HCJMenuCmd enemyship 1,-1,-1,0',
               55: 'HCJParkEnemy 3300 1200 350 1', 60: 'HCJMenuCmd enemyship 4,-1,-1,1', 120: 'HCJMenuCmd fire 1 | HCJMenuCmd enemyfire 1,1,-1'}
        if not _hud:
            _at[100] = 'ShowHUD'
        SHOTS['hero%d%s' % (_sp, '' if _hud else '_nohud')] = (270, 0, _at, VERBOSE + ['--step', '5'])
for _n, _seed in ((3, 5), (4, 9), (5, 21), (6, 42), (7, 314), (8, 2026)):
    SHOTS['fight%d' % _n] = (1800, 0, {45: MUTE + ' | HCJMenuCmd help | HCJMenuCmd skiptutorial'},
                             ['--url', '/Engine/Maps/Entry?listen?Seed=%d?Difficulty=1' % _seed, '--arg', '-HCJAutoFly', '--arg', '-HCJAutoAim', '--arg', '-HCJAutoTorpedo'])


def main():
    names = sys.argv[1:]
    if names == ['all']:
        names = list(SHOTS)
    for name in names:
        frames, start, at, extra = SHOTS[name]
        cmd = [sys.executable, os.path.join(HERE, 'capture_run.py'), name, str(frames), str(start)]
        for f, c in at.items():
            cmd += ['--at', str(f), c]
        subprocess.run(cmd + extra)


if __name__ == '__main__':
    main()
