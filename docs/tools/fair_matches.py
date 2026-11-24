#!/usr/bin/env python
"""Fairness measurement matches: one human on the autopilot against bots, headless, several processes at a time.

    python docs/tools/fair_matches.py <out dir> <tag>:<difficulty 0..4, 4 = super easy>:<fair|nofair|classic|classic20>[:<count>[:blind|:poor<deg>]] ...
                                      [--seconds 245] [--bots 8] [--seed 99] [--port 7860] [--engine F:/EpicGames/UE_5.9]
                                      [--extra "<more switches for every process>"]

    python docs/tools/fair_matches.py Saved/Fair easy_nofair:0:nofair:3 easy_fair:0:fair:3
    python docs/tools/fair_matches.py Saved/Fair blind_nofair:0:nofair:3:blind blind_fair:0:fair:3:blind
    python docs/tools/fair_matches.py Saved/Fair before:0:classic:3:poor8 after:0:fair:3:poor8
    python docs/tools/fair_stats.py Saved/Fair/*.log

Each match is a listen host on the editor binary (-game -nullrhi): Seed=99, no kill or time limit, ?Bots=8 (seven bots and
the human), -HCJQuitAfter. "nofair" adds -HCJNoFair (the bots before the fairness rules), "classic" adds
-HCJClassicEasy -HCJClassicTorpedo (Easy without the help of item 40 and a person's torpedo as before: the game of build
0.21.0), "classic20" adds -HCJClassicEasy20 -HCJClassicTorpedo (no beginner's help at all: Easy of build 0.20.0). Pilots:
  the autopilot of the playthrough tests (-HCJAutoFly -HCJAutoAim -HCJAutoTorpedo): its nose follows the nearest ship in
      sight and it fires all the time - a player who always fights. It faces whoever attacks it, so nearly none of its
      damage comes from behind, with or without the rules: it measures deaths, not the rear share.
  "blind" (-HCJAutoFly only): it circles and fires straight ahead - a player who does not find the enemy. A third of
      its damage comes from behind without the rules: this one measures the rear share.
  "poor<deg>" (item 40): the aiming autopilot with -HCJAutoAimError=<deg> - its nose wanders up to <deg> degrees around
      the target - a beginner who rarely hits; the one to show an aim assist with.
Ports from 7860 (never 7777). Only the processes started here are waited for; one is stopped (by its PID) only when it
does not quit by itself. Six at a time is what the numbers of item 29 were measured with: kill rates depend on the load.
"""
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PROJECT = os.path.join(ROOT, 'HypeCubeJam.uproject')
NOMCP = '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocol.ModelContextProtocolSettings]:bAutoStartServer=False'


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    out = os.path.abspath(args.pop(0))
    seconds, bots, port, seed, engine = 245, 8, 7860, 99, r'F:\EpicGames\UE_5.9'
    specs = []
    extra = []
    while args:
        arg = args.pop(0)
        if arg == '--seconds':
            seconds = int(args.pop(0))
        elif arg == '--bots':
            bots = int(args.pop(0))
        elif arg == '--port':
            port = int(args.pop(0))
        elif arg == '--seed':
            seed = int(args.pop(0))
        elif arg == '--engine':
            engine = args.pop(0)
        elif arg == '--extra':
            extra = args.pop(0).split()
        else:
            parts = arg.split(':')
            if len(parts) < 3 or parts[2] not in ('fair', 'nofair', 'classic', 'classic20') or (len(parts) > 4 and not (parts[4] == 'blind' or parts[4].startswith('poor'))):
                print('not understood: %s' % arg)
                return 2
            pilot = parts[4] if len(parts) > 4 else 'aim'
            for index in range(int(parts[3]) if len(parts) > 3 else 1):
                specs.append(('%s_%d' % (parts[0], index + 1), int(parts[1]), parts[2], pilot))
    exe = os.path.join(engine, 'Engine', 'Binaries', 'Win64', 'UnrealEditor-Cmd.exe')
    if not os.path.exists(exe):
        print('binary not found: %s' % exe)
        return 2
    os.makedirs(out, exist_ok=True)
    procs = []
    for index, (tag, difficulty, nofair, pilot) in enumerate(specs):
        log = os.path.join(out, tag + '.log')
        if os.path.exists(log):
            os.remove(log)
        url = '/Engine/Maps/Entry?listen?Seed=%d?FragLimit=0?TimeLimit=0?Bots=%d?Difficulty=%d' % (seed, bots, difficulty)
        cmd = [exe, PROJECT, url, '-game', '-log', NOMCP, '-nullrhi', '-nosound', '-unattended', '-nosplash', '-HCJNoIntro', '-HCJNoEOS', '-HCJNoStats',
               '-HCJQuitAfter=%d' % seconds, '-abslog=' + log, '-port=%d' % (port + index), '-HCJCapture=4', '-HCJAutoFly',
               '-ini:GameUserSettings:[/Script/HypeCubeJam.HCJUserSettings]:PlayerName=Pilot']
        if pilot != 'blind':
            cmd += ['-HCJAutoAim', '-HCJAutoTorpedo']
        if pilot.startswith('poor'):
            cmd.append('-HCJAutoAimError=%s' % (pilot[4:] or '8'))
        if nofair == 'nofair':
            cmd.append('-HCJNoFair')
        elif nofair == 'classic':
            cmd += ['-HCJClassicEasy', '-HCJClassicTorpedo']
        elif nofair == 'classic20':
            cmd += ['-HCJClassicEasy20', '-HCJClassicTorpedo']
        cmd += extra
        procs.append((tag, subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)))
        print('started %s (pid %d)' % (tag, procs[-1][1].pid))
        sys.stdout.flush()
        time.sleep(1.5)
    for tag, proc in procs:
        try:
            proc.wait(timeout=seconds + 300)
        except subprocess.TimeoutExpired:
            proc.kill()  # this process only: its PID
            print('%s did not quit by itself and was stopped (pid %d)' % (tag, proc.pid))
    print('done: python %s %s' % (os.path.join('docs', 'tools', 'fair_stats.py'), os.path.join(out, '*.log')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
