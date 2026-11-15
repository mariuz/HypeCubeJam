"""Runs the packaged HypeCubeJam build once in frame-dump capture mode and collects the frames.

    python capture_run.py <name> <frames> <start seconds> [--menu] [--intro] [--realtime] [--url URL | --nourl]
                          [--at FRAME "console command"]... [--step N] [--arg -Switch]...

  One PNG per game frame at a fixed 30 fps game step (-UseFixedTimeStep -FPS=30 -NonRealtimeAudio), so the
  frames play back in real time whatever the machine load. --at schedules console commands on dumped frame
  numbers through the game's -HCJDemoCmds (which runs command k of M at frame k*N/M + 45): frames are rounded
  to --step (default 15) and empty slots are filled with a no-op. Several commands for one frame: join with " | ".
  The game quits by itself (-HCJQuitAfter); if it does not, only the process started here is stopped, by PID.
  Frames, Audio.wav and the log are moved to F:/work/HCJ_push/capture/<name>/.
"""
import os, shutil, subprocess, sys, time

GAME = r'F:\work\HCJ_push\play\mariuz_HypeCubeJam'
EXE = os.path.join(GAME, 'HypeCubeJam', 'Binaries', 'Win64', 'HypeCubeJam.exe')
SAVED = os.path.join(GAME, 'HypeCubeJam', 'Saved')
OUT = r'F:\work\HCJ_push\capture'
PORT = 7823
NOP = 'HCJMenuCmd nop'


def main():
    a = sys.argv[1:]
    name, frames, start = a[0], int(a[1]), float(a[2])
    at, extra, step, url, menu, intro, realtime = {}, [], 15, '/Engine/Maps/Entry?listen?Seed=1234', False, False, False
    i = 3
    while i < len(a):
        if a[i] == '--at':
            f = int(a[i + 1]); at[f] = (at[f] + ' | ' if f in at else '') + a[i + 2]; i += 3
        elif a[i] == '--arg':
            extra.append(a[i + 1]); i += 2
        elif a[i] == '--step':
            step = int(a[i + 1]); i += 2
        elif a[i] == '--url':
            url = a[i + 1]; i += 2
        elif a[i] == '--nourl':
            url = ''; i += 1
        elif a[i] == '--menu':
            menu = True; i += 1
        elif a[i] == '--intro':
            intro = True; i += 1
        elif a[i] == '--realtime':
            realtime = True; i += 1
        else:
            raise SystemExit('unknown option ' + a[i])
    slots = max(1, frames // step)
    cmds = [NOP] * slots
    for f, c in sorted(at.items()):
        k = max(0, min(slots - 1, round((f - 45) / step)))
        cmds[k] = c if cmds[k] == NOP else cmds[k] + ' | ' + c
    while len(cmds) > 1 and cmds[-1] == NOP:
        pass_ = cmds.pop()
    # keep the slot count (the spacing depends on it): pad again
    cmds += [NOP] * (slots - len(cmds))
    demo = ';'.join(c.replace(' ', '_') for c in cmds)
    out = os.path.join(OUT, name)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    dump = os.path.join(SAVED, 'Screenshots', 'HCJFrames')
    if os.path.isdir(dump):
        shutil.rmtree(dump)
    log = os.path.join(out, 'game.log')
    quit_after = start + frames / 30.0 + 4
    cmd = [EXE]
    if url:
        cmd.append(url)
    cmd += ['-port=%d' % PORT, '-windowed', '-ResX=1280', '-ResY=720', '-WinX=40', '-WinY=40', '-HCJNoEOS', '-HCJNoStats',
            '-unattended', '-nosplash', '-HCJCapture=99999', '-HCJFrameDump=%d' % frames, '-HCJFrameDumpStart=%g' % start,
            '-HCJDemoCmds=' + demo, '-HCJQuitAfter=%g' % quit_after, '-abslog=' + log]
    if not realtime:
        cmd += ['-UseFixedTimeStep', '-FPS=30', '-NonRealtimeAudio']
    if menu:
        cmd.append('-HCJMenu')
    cmd.append('-HCJIntro' if intro else '-HCJNoIntro')
    # extra switches are passed verbatim (they may hold quotes of their own, e.g. -LogCmds="LogHCJ Verbose")
    cmd = subprocess.list2cmdline(cmd) + ''.join(' ' + e for e in extra)
    with open(os.path.join(out, 'cmdline.txt'), 'w') as fh:
        fh.write(cmd + '\n')
    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(EXE), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print('started pid %d' % proc.pid, flush=True)
    limit = 240 + frames * (0.2 if realtime else 0.9)
    try:
        proc.wait(timeout=limit)
    except subprocess.TimeoutExpired:
        print('timeout after %.0f s: stopping pid %d' % (limit, proc.pid))
        subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        proc.wait(timeout=30)
    time.sleep(1.0)
    n = 0
    if os.path.isdir(dump):
        for f in sorted(os.listdir(dump)):
            shutil.move(os.path.join(dump, f), os.path.join(out, f)); n += 1
    print('%s: exit %s after %.0f s, %d files -> %s' % (name, proc.returncode, time.time() - t0, n, out))


if __name__ == '__main__':
    main()
