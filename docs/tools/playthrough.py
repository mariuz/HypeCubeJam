#!/usr/bin/env python
"""Playthrough tests: whole matches of HypeCubeJam run as real processes, judged from their logs.

    python docs/tools/playthrough.py                     every scenario, editor binary, headless (-nullrhi)
    python docs/tools/playthrough.py --binary packaged   the packaged build (Packaged/Temp/Windows/HypeCubeJam.exe)
    python docs/tools/playthrough.py --only net_host_client_late,autopilot_hangar_match
    python docs/tools/playthrough.py --list
    options: --jobs N (scenarios side by side, default 4)  --render (windows instead of -nullrhi; packaged only)
             --out DIR (logs; default Saved/Playthrough/<time>)  --port N (first UDP port, default 7790)
             --engine DIR (default F:/EpicGames/UE_5.9)  --scale F (multiplies every duration)

Each scenario starts one to three processes with the game's unattended switches (-HCJQuitAfter, -HCJBotsOnly,
-HCJCapture / -HCJAutoFly / -HCJAutoAim, -HCJNoEOS, -HCJNoIntro), waits for them to quit by themselves, and reads the
structured "Playthrough: event=... key=value" lines the game mode and the clients write (plus a few plain lines).
Exit code 0 when every scenario passed, 1 otherwise. Only processes started here are ever stopped (by PID, on timeout).

Scenarios side by side share the processor: kill rates of a parallel run are lower than those of a single match, so
the thresholds here are loose on purpose and say nothing about balance (docs/DEVLOG_NOTES.md, item 27, has the numbers).
Ports: 7790 upwards, never 7777.
"""
import argparse
import datetime
import io
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PROJECT = os.path.join(ROOT, 'HypeCubeJam.uproject')
NOMCP = '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocol.ModelContextProtocolSettings]:bAutoStartServer=False'
WEAPONS = ['blaster', 'scatter', 'rail', 'missiles', 'pulse', 'torpedo', 'phaser']
TYPES = ['int', 'ftr', 'gun', 'sct', 'cru']
# Lines that are in every log of a healthy run (the packaged build has one known engine plugin message).
KNOWN_ERRORS = ['CelestialVault']


# ------------------------------------------------------------------------------------------------ logs

class Log:
    """One process log: its lines and the Playthrough events in them."""

    def __init__(self, path):
        self.path = path
        try:
            self.lines = io.open(path, encoding='utf-8', errors='replace').read().splitlines()
        except OSError:
            self.lines = []
        self.events = []
        for line in self.lines:
            i = line.find('Playthrough: event=')
            if i >= 0:
                fields = dict(part.split('=', 1) for part in line[i + 13:].split() if '=' in part)
                self.events.append(fields)

    def all(self, event, **where):
        return [e for e in self.events if e.get('event') == event and all(e.get(k) == str(v) for k, v in where.items())]

    def last(self, event, **where):
        found = self.all(event, **where)
        return found[-1] if found else None

    def grep(self, pattern):
        rx = re.compile(pattern)
        return [m for m in (rx.search(line) for line in self.lines) if m]

    def exited(self):
        return any('LogExit: Exiting' in line or 'Quitting: the time given' in line for line in self.lines)

    def stats_total(self):
        """The counters of every match of this log added up (the last stats line of each match)."""
        total = {}
        matches = sorted({e['match'] for e in self.all('stats')}, key=int)
        for match in matches:
            for key, value in self.last('stats', match=match).items():
                if key in ('event', 'match', 't', 'match_t', 'layout', 'destroyed_checksum', 'tick_mean_ms', 'tick_max_ms', 'humans', 'bots', 'rocks_alive', 'destroyed_count'):
                    continue
                try:
                    total[key] = total.get(key, 0) + int(value)
                except ValueError:
                    pass
        return total


def num(event, key, default=0.0):
    try:
        return float(event.get(key, default)) if event else default
    except ValueError:
        return default


class Checks:
    """Collects the assertions of one scenario."""

    def __init__(self):
        self.failed = []
        self.count = 0
        self.notes = []

    def ok(self, condition, what):
        self.count += 1
        if not condition:
            self.failed.append(what)
        return bool(condition)

    def note(self, text):
        self.notes.append(text)

    def clean(self, log, role):
        """No error / ensure / fatal / NaN lines, the process quit by itself, nothing stuck or out of bounds, sane frames."""
        self.ok(log.lines, '%s: no log written (%s)' % (role, log.path))
        self.ok(log.exited(), '%s: the process did not quit by itself' % role)
        bad = [l for l in log.lines if re.search(r'Error:|Ensure condition|Assertion failed|Fatal error|Unhandled Exception', l)
               and not any(k in l for k in KNOWN_ERRORS)]
        self.ok(not bad, '%s: %d error line(s), first: %s' % (role, len(bad), bad[0][:160] if bad else ''))
        nan = [l for l in log.lines if 'LogHCJ' in l and re.search(r'(?<![\w.=])-?(nan|inf)(?![\w=])|#IND|#INF', l, re.I)]
        self.ok(not nan, '%s: %d line(s) with NaN / infinity, first: %s' % (role, len(nan), nan[0][:160] if nan else ''))
        for event in ('stuck', 'out_of_bounds', 'nan'):
            found = log.all(event)
            self.ok(not found, '%s: %d "%s" event(s), first: %s' % (role, len(found), event, found[0] if found else ''))
        stats = log.all('stats')
        if stats:
            mean = max(num(e, 'tick_mean_ms') for e in stats)
            worst = max(num(e, 'tick_max_ms') for e in stats)
            self.ok(mean < 60.0, '%s: mean server frame %.1f ms (limit 60)' % (role, mean))
            self.ok(worst < 3000.0, '%s: longest server frame %.0f ms (limit 3000)' % (role, worst))
            self.note('%s frames: mean %.1f ms, longest %.0f ms' % (role, mean, worst))


# ------------------------------------------------------------------------------------------------ processes

class Runner:
    def __init__(self, args, out_dir):
        self.args = args
        self.out = out_dir
        self.next_port = args.port
        self.lock = threading.Lock()
        self.packaged = args.binary == 'packaged'
        self.exe = os.path.join(ROOT, 'Packaged', 'Temp', 'Windows', 'HypeCubeJam.exe') if self.packaged \
            else os.path.join(args.engine, 'Engine', 'Binaries', 'Win64', 'UnrealEditor-Cmd.exe')

    def port(self):
        with self.lock:
            self.next_port += 1
            if self.next_port > 7850:
                raise RuntimeError('out of ports (7790-7850)')
            return self.next_port

    def command(self, tag, url, seconds, extra, window=0):
        log = os.path.join(self.out, tag + '.log')
        cmd = [self.exe] if self.packaged else [self.exe, PROJECT]
        if url:
            cmd.append(url)
        if not self.packaged:
            cmd += ['-game', '-log', NOMCP]
        if self.packaged and self.args.render:
            cmd += ['-windowed', '-ResX=640', '-ResY=360', '-WinX=%d' % (20 + 660 * (window % 3)), '-WinY=%d' % (40 + 400 * (window // 3))]
        else:
            cmd.append('-nullrhi')
        cmd += ['-nosound', '-unattended', '-nosplash', '-HCJNoIntro', '-HCJNoEOS', '-HCJNoStats', '-HCJQuitAfter=%d' % seconds, '-abslog=' + log]
        return cmd + list(extra), log

    def run(self, specs):
        """specs: [(tag, url, seconds, extra args, start delay)]. Starts them (with their delays), waits, returns {tag: Log}."""
        procs = []
        start = time.time()
        logs = {}
        for index, (tag, url, seconds, extra, delay) in enumerate(sorted(specs, key=lambda s: s[4])):
            wait = start + delay * self.args.scale - time.time()
            if wait > 0:
                time.sleep(wait)
            cmd, log = self.command(tag, url, int(seconds * self.args.scale), extra, index)
            if os.path.exists(log):
                os.remove(log)
            logs[tag] = log
            procs.append((subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL), seconds * self.args.scale))
        for proc, seconds in procs:
            try:
                # Start-up and shut-down of a process take a while when several run at once.
                proc.wait(timeout=seconds + 240)
            except subprocess.TimeoutExpired:
                proc.kill()  # this process only: its PID
        time.sleep(1.0)
        return {tag: Log(path) for tag, path in logs.items()}


# ------------------------------------------------------------------------------------------------ scenarios

def bot_url(options):
    return '/Engine/Maps/Entry?' + options


def pilot(name):
    """The switch that gives a process its pilot name (the name of the user settings, not of the URL, is what the game uses)."""
    return '-ini:GameUserSettings:[/Script/HypeCubeJam.HCJUserSettings]:PlayerName=' + name


def check_bot_match(c, log, min_kills, want_systems=True, want_travel=True, want_cloak=True):
    total = log.stats_total()
    c.ok(log.all('match_start'), 'no match_start line')
    c.ok(total.get('kills', 0) >= min_kills, 'kills %d, wanted at least %d' % (total.get('kills', 0), min_kills))
    c.ok(total.get('deaths', 0) >= total.get('kills', 0), 'fewer deaths than kills')
    c.ok(total.get('shots', 0) > 0, 'no shots')
    if want_travel:
        c.ok(total.get('portal_jumps', 0) >= 5, 'portal jumps %d, wanted at least 5' % total.get('portal_jumps', 0))
        c.ok(total.get('pickups', 0) >= 5, 'pickups taken %d, wanted at least 5' % total.get('pickups', 0))
    if want_systems:
        # (Easy bots raise the cloak about once a minute between the eight of them: not asked of every easy match.)
        for key in ('warps', 'cloaks', 'tractor_holds') if want_cloak else ('warps', 'tractor_holds'):
            c.ok(total.get(key, 0) >= 1, '%s %d, wanted at least 1' % (key, total.get(key, 0)))
    # The destroyed set: the last line of the rock code and the last statistics line name the same set.
    rocks = log.grep(r'Asteroids \(server\): (\d+) destroyed, checksum ([0-9A-F]{8})')
    last_match = log.all('stats')[-1] if log.all('stats') else None
    if rocks and last_match and log.all('stats')[-1].get('match') == '1':
        same = [m for m in rocks if m.group(1) == last_match.get('destroyed_count')]
        c.ok(not same or same[-1].group(2) == last_match.get('destroyed_checksum'), 'destroyed-set checksum of the statistics differs from the rock code\'s')
    return total


def scenario_match_cycle(r, c):
    """A match to the kill limit, the board, and the next match on a new seed and layout."""
    logs = r.run([('match_cycle', bot_url('Seed=1234?FragLimit=5?TimeLimit=0?Bots=8?Difficulty=1'), 170, ['-HCJBotsOnly'], 0)])
    log = logs['match_cycle']
    c.clean(log, 'match')
    starts = log.all('match_start')
    ends = log.all('match_end')
    c.ok(len(starts) >= 2, '%d match_start lines, wanted at least 2' % len(starts))
    c.ok(len(ends) >= 1, 'the first match did not end')
    if ends:
        end = ends[0]
        c.ok(end.get('reason') == 'frag_limit', 'the match ended by "%s", not by the kill limit' % end.get('reason'))
        c.ok(num(end, 'winner_kills') >= 5, 'winner has %s kills, limit 5' % end.get('winner_kills'))
        c.ok(end.get('winner', '-') != '-', 'no winner')
        board = log.all('board', match=end.get('match'))
        c.ok(len(board) == 8 and num(end, 'board_rows') == 8, 'board has %d rows, wanted 8' % len(board))
        c.ok(board and board[0].get('name') == end.get('winner') and board[0].get('rank') == '1', 'the winner is not the first row of the board')
        kills = [num(row, 'kills') for row in board]
        c.ok(kills == sorted(kills, reverse=True), 'the board is not sorted by kills')
        c.note('match 1: %s won with %s kills after %s s' % (end.get('winner'), end.get('winner_kills'), end.get('duration')))
    if len(starts) >= 2:
        c.ok(starts[0].get('seed') == '1234', 'first seed is %s' % starts[0].get('seed'))
        c.ok(starts[1].get('seed') != starts[0].get('seed'), 'the second match has the same seed')
        c.ok(starts[1].get('layout') != starts[0].get('layout') and starts[1].get('layout') != '00000000', 'the second match has the same layout checksum')
        c.ok(starts[1].get('match') == '2', 'the second match is not match 2')
        c.note('layouts %s -> %s' % (starts[0].get('layout'), starts[1].get('layout')))
    return {'match_cycle': log}


def make_difficulty(level, name, min_kills):
    def scenario(r, c):
        tag = 'difficulty_' + name
        logs = r.run([(tag, bot_url('Seed=%d?FragLimit=0?TimeLimit=0?Bots=8?Difficulty=%d' % (1234 + level, level)), 230, ['-HCJBotsOnly'], 0)])
        log = logs[tag]
        c.clean(log, name)
        start = log.last('match_start')
        c.ok(start and start.get('difficulty') == str(level), 'the match does not run at difficulty %d' % level)
        total = check_bot_match(c, log, min_kills, want_cloak=level > 0)
        for t in TYPES:
            c.ok(total.get('kills_' + t, 0) >= 1, 'ship type %s has no kill' % t)
            c.ok(total.get('deaths_' + t, 0) >= 1, 'ship type %s has no death' % t)
        c.ok(total.get('wormhole_jumps', 0) >= 1, 'no wormhole jump')
        c.note('%d kills, %d portal / %d wormhole jumps, %d pickups, by type %s' % (
            total.get('kills', 0), total.get('portal_jumps', 0), total.get('wormhole_jumps', 0), total.get('pickups', 0),
            ' '.join('%s %d/%d' % (t, total.get('kills_' + t, 0), total.get('deaths_' + t, 0)) for t in TYPES)))
        return {tag: log}
    scenario.__doc__ = 'An open match of eight %s bots: every ship type kills and dies, portals, wormholes, pickups, warp / cloak / tractor.' % name
    return scenario


def scenario_small_universe(r, c):
    """Eight easy bots on four sectors (a crowd): station turrets shoot ships down and get destroyed, rocks break."""
    logs = r.run([('small_universe', bot_url('Seed=77?Dim=2?FragLimit=0?TimeLimit=0?Bots=8?Difficulty=0'), 260, ['-HCJBotsOnly'], 0)])
    log = logs['small_universe']
    c.clean(log, 'match')
    total = check_bot_match(c, log, 12, want_systems=False, want_travel=False)
    c.note('%d kills, turrets: %d shots, %d ships shot down, %d destroyed; rocks destroyed %d' % (
        total.get('kills', 0), total.get('turret_shots', 0), total.get('turret_deaths', 0), total.get('turrets_destroyed', 0), total.get('rocks_destroyed', 0)))
    return {'small_universe': log}


def scenario_net(r, c):
    """Host + client + late joiner on this PC; the client leaves and comes back. Checksums, scores, corrections."""
    host_port, client_port, late_port = r.port(), r.port(), r.port()
    address = '127.0.0.1:%d' % host_port
    fly = ['-HCJCapture=8', '-HCJAutoFly', '-HCJAutoAim', '-HCJAutoTorpedo']
    # The client leaves with its sixth picture (48 s after it started) and joins again two pictures later.
    leave_rejoin = '-HCJCaptureCmds=;;;;;;HCJMenuCmd_leave;;HCJMenuCmd_join_' + address
    # The host breaks a rock on purpose with its 8th, 16th and 21st picture (64 s, 128 s, 168 s; nothing happens when its
    # ship is gone at that moment), so that there is a destroyed set to compare whatever the fight did: a rock the client
    # sees break while it is connected, one that is already gone when the late joiner arrives, one that all three see.
    # (Until the bots were made fair to people - devlog item 29 - the torpedoes of autopilots that died every few
    # seconds broke a rock or two in every run; pilots that live longer get fewer torpedoes, and one run broke none.)
    break_rocks = '-HCJCaptureCmds=' + ';' * 7 + 'HCJMenuCmd_breakrock' + ';' * 8 + 'HCJMenuCmd_breakrock' + ';' * 5 + 'HCJMenuCmd_breakrock'
    # -HCJNoFair on the host: this scenario is about the network, and its score checks (a kill, a death, something on the
    # board for each remote player) were set for easy bots that treat a person like any other ship. With the fairness
    # rules an autopilot on Easy dies about once a minute and a late joiner has 110 s: five runs gave 1 to 6 events per
    # player and one remote death in one of them - one event fewer and the scenario fails with nothing broken.
    # How the bots treat people is measured by fair_matches.py, and autopilot_hangar_match plays with the rules on.
    logs = r.run([
        ('net_host', '/Engine/Maps/Entry?listen?Seed=1234?FragLimit=0?TimeLimit=0?Bots=6?Difficulty=0', 250, ['-port=%d' % host_port, '-HCJNoFair', break_rocks, pilot('Hana')] + fly, 0),
        ('net_client', address, 200, ['-port=%d' % client_port, leave_rejoin, pilot('Clive')] + fly, 25),
        ('net_late', address, 110, ['-port=%d' % late_port, pilot('Lena')] + fly, 100),
    ])
    host, client, late = logs['net_host'], logs['net_client'], logs['net_late']
    c.clean(host, 'host')
    c.clean(client, 'client')
    c.clean(late, 'late joiner')

    start = host.last('match_start')
    layout = start.get('layout') if start else None
    c.ok(layout and layout != '00000000', 'the host has no layout checksum')
    remote_kills = remote_deaths = 0
    for role, log, name in (('client', client, 'Clive'), ('late joiner', late, 'Lena')):
        seen = log.all('client')
        c.ok(len(seen) >= 3, '%s: only %d status lines (did it connect?)' % (role, len(seen)))
        c.ok(seen and all(e.get('layout') == layout for e in seen), '%s: layout checksum differs from the host\'s %s' % (role, layout))
        c.ok(seen and all(e.get('seed') == (start or {}).get('seed') for e in seen), '%s: another seed than the host' % role)
        # Every destroyed set the client had is one the server had: same number, same checksum.
        server_sets = {(m.group(1), m.group(2)) for m in host.grep(r'Asteroids \(server\): (\d+) destroyed, checksum ([0-9A-F]{8})')}
        client_sets = [(e.get('destroyed_count'), e.get('destroyed_checksum')) for e in seen if e.get('destroyed_count') not in (None, '0')]
        client_sets += [(m.group(1), m.group(2)) for m in log.grep(r'Asteroids \(client\): (\d+) destroyed, checksum ([0-9A-F]{8})')]
        wrong = [s for s in client_sets if s not in server_sets]
        c.ok(not wrong, '%s: destroyed set %s is not one the host had' % (role, wrong[0] if wrong else ''))
        c.ok(seen and any(e.get('ship') == '1' for e in seen), '%s: never had a ship' % role)
        rows = host.all('human', name=name)
        kills = max([num(e, 'kills') for e in rows] + [0])
        deaths = max([num(e, 'deaths') for e in rows] + [0])
        c.ok(rows, 'the host never listed %s' % name)
        c.ok(kills + deaths >= 1, '%s: no kill and no death on the host\'s board' % role)
        corrections = max([num(e, 'corrections') for e in seen] + [0])
        seconds = max(10.0 * len(seen), 1.0)
        rate = corrections * 100.0 / seconds
        c.ok(rate <= 30.0, '%s: %.0f movement corrections per 100 s (limit 30)' % (role, rate))
        c.note('%s: %d kills, %d deaths, %d corrections in about %d s (%.1f per 100 s), %d destroyed-set states compared' % (
            role, kills, deaths, corrections, seconds, rate, len(set(client_sets))))
        remote_kills += kills
        remote_deaths += deaths
    # (An autopilot does not score in every three minutes: one kill between the two players that joined is asked for.)
    c.ok(remote_kills >= 1, 'neither the client nor the late joiner has a kill (autopilot with aim)')
    c.ok(remote_deaths >= 1, 'neither the client nor the late joiner has a death')
    c.ok(len(server_sets) >= 1, 'no rock was destroyed in this run (nothing to compare)')

    # Leave and rejoin: the host saw Clive come, go and come again; the client built the universe twice with the same checksum.
    # (A join is logged before the pilot name arrives: remote joins are counted, leaves are by name.)
    joined = len(host.grep(r'Player joined: .* \(remote\)'))
    left = len(host.grep(r'Player left: Clive'))
    c.ok(joined >= 3, 'the host saw %d remote join(s), wanted 3 (client, client again, late joiner)' % joined)
    c.ok(left >= 1, 'the host never saw Clive leave')
    built = client.grep(r'Universe built \(client\).*layout checksum ([0-9A-F]{8})')
    c.ok(len(built) >= 2 and all(m.group(1) == layout for m in built), 'the client did not rebuild the same universe after rejoining (%d builds)' % len(built))
    c.ok(len(host.grep(r'Player left: Lena')) >= 1 or host.all('human', name='Lena'), 'the host never saw the late joiner')
    late_sync = late.grep(r'Asteroids \(client\): (\d+) destroyed.*already gone when this machine joined')
    c.note('late joiner got %s rock(s) that were already gone' % (late_sync[0].group(1) if late_sync else 'no'))
    total = host.stats_total()
    c.note('host: %d kills, %d rocks destroyed, %d remote joins, Clive left %d time(s)' % (total.get('kills', 0), total.get('rocks_destroyed', 0), joined, left))
    return logs


def user_settings_file(r):
    """The GameUserSettings.ini the binary of this run writes (the hangar choice of the autopilot scenario lands there)."""
    if r.packaged:
        return os.path.join(ROOT, 'Packaged', 'Temp', 'Windows', 'HypeCubeJam', 'Saved', 'Config', 'Windows', 'GameUserSettings.ini')
    return os.path.join(ROOT, 'Saved', 'Config', 'WindowsEditor', 'GameUserSettings.ini')


def scenario_autopilot(r, c):
    """The autopilot player: title menu, hangar choice (a cruiser), PLAY, a whole match to the kill limit, the next one."""
    settings = user_settings_file(r)
    before = io.open(settings, 'rb').read() if os.path.exists(settings) else None
    try:
        return run_autopilot(r, c)
    finally:
        # The player's own ship choice is put back (PLAY saves the settings, cruiser and all).
        if before is not None:
            io.open(settings, 'wb').write(before)
        elif os.path.exists(settings):
            os.remove(settings)


def run_autopilot(r, c):
    port = r.port()
    cmds = '-HCJCaptureCmds=HCJMenuCmd_hangar;HCJMenuCmd_ship_4,2,5;HCJMenuCmd_main;HCJMenuCmd_play'
    logs = r.run([('autopilot', '/Engine/Maps/Entry?listen?Seed=99?FragLimit=4?TimeLimit=0?Bots=7?Difficulty=1', 230,
                   ['-port=%d' % port, '-HCJMenu', '-HCJCapture=4', '-HCJAutoFly', '-HCJAutoAim', '-HCJAutoTorpedo', cmds], 0)])
    log = logs['autopilot']
    c.clean(log, 'autopilot')
    if r.packaged and r.args.render:
        # (Without a renderer there is no title menu: the same commands then go straight to the game mode.)
        c.ok(log.grep(r'the local player starts in the title menu'), 'the player did not start in the title menu')
    choice = log.grep(r'Ship choice: .* asked for type 4, hull 2, paint 5 -> Cruiser, hull 2, paint 5')
    c.ok(choice, 'the hangar choice (cruiser, hull 2, paint 5) did not reach the server')
    c.ok(log.grep(r'Menu: Play - '), 'PLAY did not start a match')
    ends = [e for e in log.all('match_end') if e.get('reason') == 'frag_limit']
    c.ok(ends, 'no match ended by the kill limit')
    if ends:
        board = log.all('board', match=ends[0].get('match'))
        humans = [row for row in board if row.get('bot') == '0']
        c.ok(len(humans) == 1, 'the board has %d human row(s), wanted 1' % len(humans))
        c.ok(humans and humans[0].get('type') == 'cru', 'the player is not listed as a cruiser')
        # Over all matches of the run that ended (the kill limit is low: one match may pass the player by).
        played = [row for row in log.all('board') if row.get('bot') == '0']
        c.ok(sum(num(row, 'kills') + num(row, 'deaths') for row in played) >= 1, 'the player neither killed nor died in %d finished match(es)' % len(ends))
        c.note('player over %d finished matches: %d kills, %d deaths' % (len(ends), sum(num(row, 'kills') for row in played), sum(num(row, 'deaths') for row in played)))
        c.ok(len(board) == 8, 'board has %d rows, wanted 8' % len(board))
        later = [s for s in log.all('match_start') if int(s.get('match', 0)) > int(ends[0].get('match', 0))]
        c.ok(later, 'no match started after the one that ended')
        if humans:
            c.note('player: %s kills, %s deaths as %s; winner %s (%s kills)' % (humans[0].get('kills'), humans[0].get('deaths'), humans[0].get('type'),
                                                                               ends[0].get('winner'), ends[0].get('winner_kills')))
    return logs


SCENARIOS = [
    ('match_cycle', scenario_match_cycle),
    ('difficulty_easy', make_difficulty(0, 'easy', 15)),
    ('difficulty_normal', make_difficulty(1, 'normal', 20)),
    ('difficulty_hard', make_difficulty(2, 'hard', 20)),
    ('small_universe', scenario_small_universe),
    ('net_host_client_late', scenario_net),
    ('autopilot_hangar_match', scenario_autopilot),
]
# Judged from the logs of the scenarios above, all matches together.
AGGREGATE = 'all_matches_together'


def aggregate(c, logs):
    """Over all bot matches of the run: every weapon kills, turrets kill and get destroyed, rocks get destroyed."""
    total = {}
    for log in logs:
        for key, value in log.stats_total().items():
            total[key] = total.get(key, 0) + value
    for weapon in WEAPONS:
        c.ok(total.get('kills_' + weapon, 0) >= 1, 'no kill with %s in any match' % weapon)
    c.ok(total.get('turret_deaths', 0) >= 1, 'no ship was shot down by a station turret in any match')
    c.ok(total.get('turrets_destroyed', 0) >= 1, 'no station turret was destroyed in any match')
    c.ok(total.get('rocks_destroyed', 0) >= 1, 'no rock was destroyed in any match')
    pickups = [k for k in total if k.startswith('pickup_')]
    for key in sorted(pickups):
        c.ok(total[key] >= 1 or key == 'pickup_cloak', 'item %s was never taken' % key[7:])
    c.note('kills by weapon: ' + ', '.join('%s %d' % (w, total.get('kills_' + w, 0)) for w in WEAPONS))
    c.note('turrets: %d ships shot down, %d turrets destroyed; rocks destroyed %d (%d by bots)' % (
        total.get('turret_deaths', 0), total.get('turrets_destroyed', 0), total.get('rocks_destroyed', 0), total.get('rocks_by_bots', 0)))
    c.note('bots: ' + ', '.join('%s %d' % (k[4:], total[k]) for k in sorted(total) if k.startswith('bot_')) + ', facing turns %d, open-facing torpedo kills %d' % (
        total.get('facing_turns', 0), total.get('torpedo_open_kills', 0)))


def main():
    parser = argparse.ArgumentParser(description='HypeCubeJam playthrough tests')
    parser.add_argument('--binary', choices=['editor', 'packaged'], default='editor')
    parser.add_argument('--only', default='')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--out', default='')
    parser.add_argument('--port', type=int, default=7790)
    parser.add_argument('--engine', default=r'F:\EpicGames\UE_5.9')
    parser.add_argument('--scale', type=float, default=1.0)
    args = parser.parse_args()

    if args.list:
        for name, fn in SCENARIOS:
            print('%-24s %s' % (name, (fn.__doc__ or '').strip()))
        print('%-24s %s' % (AGGREGATE, aggregate.__doc__.strip()))
        return 0
    wanted = [n for n in args.only.split(',') if n]
    unknown = [n for n in wanted if n not in dict(SCENARIOS)]
    if unknown:
        print('unknown scenario(s): %s' % ', '.join(unknown))
        return 2
    chosen = [(n, fn) for n, fn in SCENARIOS if not wanted or n in wanted]
    out = args.out or os.path.join(ROOT, 'Saved', 'Playthrough', datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
    os.makedirs(out, exist_ok=True)
    runner = Runner(args, out)
    if not os.path.exists(runner.exe):
        print('binary not found: %s' % runner.exe)
        return 2
    print('playthrough: %d scenario(s), %s binary%s, logs in %s' % (len(chosen), args.binary, ' (rendered)' if args.render else ', headless', out))

    results = {}
    all_logs = {}
    lock = threading.Lock()
    queue = list(chosen)

    def worker():
        while True:
            with lock:
                if not queue:
                    return
                name, fn = queue.pop(0)
            checks = Checks()
            begin = time.time()
            try:
                logs = fn(runner, checks) or {}
            except Exception as error:  # a scenario that breaks is a failed scenario, not a broken run
                checks.ok(False, 'the scenario raised %s: %s' % (type(error).__name__, error))
                logs = {}
            with lock:
                results[name] = (checks, time.time() - begin)
                all_logs.update(logs)
                print('  %-24s %s  (%d checks, %.0f s)' % (name, 'PASS' if not checks.failed else 'FAIL', checks.count, time.time() - begin))
                sys.stdout.flush()

    threads = [threading.Thread(target=worker) for _ in range(max(1, min(args.jobs, len(chosen))))]
    begin = time.time()
    for thread in threads:
        thread.start()
        time.sleep(2.0)
    for thread in threads:
        thread.join()

    if not wanted or len(chosen) >= 4:
        checks = Checks()
        aggregate(checks, [log for tag, log in all_logs.items() if log.all('stats')])
        results[AGGREGATE] = (checks, 0.0)

    print()
    print('%-24s %-6s %6s %7s  %s' % ('scenario', 'result', 'checks', 'seconds', 'failed checks / notes'))
    print('-' * 110)
    failed = 0
    for name in [n for n, _ in chosen] + ([AGGREGATE] if AGGREGATE in results else []):
        checks, seconds = results[name]
        failed += 1 if checks.failed else 0
        print('%-24s %-6s %6d %7.0f  %s' % (name, 'FAIL' if checks.failed else 'PASS', checks.count, seconds, checks.failed[0] if checks.failed else (checks.notes[0] if checks.notes else '')))
        for line in checks.failed[1:]:
            print('%-46s  FAILED: %s' % ('', line))
        for line in (checks.notes if checks.failed else checks.notes[1:]):
            print('%-46s  %s' % ('', line))
    print('-' * 110)
    print('%d of %d scenarios passed in %.0f s%s' % (len(results) - failed, len(results), time.time() - begin,
                                                    ' (scenarios ran %d at a time: kill rates are lower than in a single match)' % len(threads) if len(threads) > 1 else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
