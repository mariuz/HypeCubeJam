#!/usr/bin/env python
"""Fairness numbers out of game logs: what the bots did to the human players (docs/DEVLOG_NOTES.md, item 29).

    python docs/tools/fair_stats.py <log> [<log> ...]

Reads the server's "Fair: event=..." lines. One row per log; logs named <group>_<n>.log are also added up per group.
  rear      share of the damage bots did to humans that came from their rear arc (more than about 107 degrees off the
            nose): dmg_rear / dmg_total of the last "event=status" line - the server adds up every damage step, so
            this is exact. "(lines)" is the same from the "event=hit" lines (a phaser line carries the bearing of its
            last step, so it can differ by a point or two).
  deaths    "event=death" lines, per minute of the time between the first "event=spawn" and the last line; "by bots",
            "rear" (killed by a bot in the rear arc) and "<12s" (dead within 12 s of the spawn) are counted from them.
  peak      the most bots that were attacking one human at the same time.
  held      bot Thinks in which a bot left a human alone because of the attack cap / held its fire in the rear arc /
            because the human had just arrived / because its attacker had just gone; "hunters" counts the times a bot
            that travels did not go to a human's sector because enough bots were there. All 0 on Ace and with -HCJNoFair.
  help      the beginner's help of item 38 (Easy; 0 elsewhere, with -HCJNoFair and with -HCJClassicEasy, and in logs of
            older builds): damage people did to bots and the part of it the scale added, hull points mended out of
            combat, repair pads people took and what they restored. "K/D" is own kills over deaths (at least 1).
"""
import glob
import io
import os
import re
import sys


def fields(line, marker):
    """key=value tokens of a line from "event=" on."""
    i = line.find(marker)
    return dict(p.split('=', 1) for p in line[i + len(marker) - 6:].split() if '=' in p) if i >= 0 else None


def read(path):
    r = {'path': path, 'hits': [], 'deaths': [], 'spawns': [], 'status': None, 'human': None, 'stats': None, 'errors': 0, 'bad': 0}
    for line in io.open(path, encoding='utf-8', errors='replace'):
        if 'Fair: event=' in line:
            f = fields(line, 'Fair: event=')
            kind = f.get('event')
            if kind == 'hit':
                r['hits'].append(f)
            elif kind == 'death':
                r['deaths'].append(f)
            elif kind == 'spawn':
                r['spawns'].append(f)
            elif kind == 'status':
                r['status'] = f
        elif 'Playthrough: event=human' in line:
            r['human'] = fields(line, 'Playthrough: event=')
        elif 'Playthrough: event=stats' in line:
            r['stats'] = fields(line, 'Playthrough: event=')
        elif re.search(r'Playthrough: event=(stuck|out_of_bounds|nan)', line):
            r['bad'] += 1
        if re.search(r'Error:|Ensure condition|Assertion failed|Fatal error|Unhandled Exception', line) and 'CelestialVault' not in line:
            r['errors'] += 1
    return r


def row(r):
    s = r['status'] or {}
    f = lambda key: float(s.get(key, 0) or 0)
    bot_hits = [h for h in r['hits'] if h.get('attacker_bot') == '1']
    first = float(r['spawns'][0]['t']) if r['spawns'] else 0.0
    last = max([float(h['t']) for h in r['hits']] + [float(d['t']) for d in r['deaths']] + [f('t'), first])
    other = {}
    for d in r['deaths']:
        if d.get('killer_bot') != '1':
            other[d.get('cause')] = other.get(d.get('cause'), 0) + 1
    return {
        'name': os.path.splitext(os.path.basename(r['path']))[0], 'difficulty': s.get('difficulty', '?'), 'nofair': s.get('nofair', '?'),
        'minutes': max((last - first) / 60.0, 1e-6), 'dmg': f('dmg_total'), 'rear': f('dmg_rear'), 'aft': f('dmg_aft'), 'unscaled': f('dmg_unscaled'),
        'line_dmg': sum(float(h['dmg']) for h in bot_hits), 'line_rear': sum(float(h['dmg']) for h in bot_hits if h.get('rear') == '1'),
        'deaths': len(r['deaths']), 'bot_deaths': sum(1 for d in r['deaths'] if d.get('killer_bot') == '1'),
        'rear_deaths': sum(1 for d in r['deaths'] if d.get('rear') == '1'), 'quick': sum(1 for d in r['deaths'] if float(d.get('alive', 99)) < 12.0),
        'peak': str(int(f('peak_attackers'))), 'held_cap': int(f('held_cap')), 'held_rear': int(f('held_rear')), 'held_grace': int(f('held_grace')),
        'held_respite': int(f('held_respite')), 'held_hunters': int(f('held_hunters')), 'kills': int((r['human'] or {}).get('kills', 0)), 'all_kills': int((r['stats'] or {}).get('kills', 0)),
        'errors': r['errors'], 'bad': r['bad'], 'other': other,
        'human_dmg': f('human_dmg'), 'human_bonus': f('human_dmg_bonus'), 'hull_regen': f('hull_regen'), 'repairs': int(f('repairs')), 'repair_points': f('repair_points'),
    }


def show(x):
    pct = lambda part, whole: 100.0 * part / whole if whole > 0 else 0.0
    return ('%-20s level %s nofair %s  %5.1f min | bot damage %6.0f (%6.0f before the scale), rear %4.1f%% (lines %4.1f%%), aft facing %4.1f%% | deaths %3d = %4.2f/min, '
            'by bots %3d, rear %2d (%2.0f%%), <12s %2d | peak %s | held cap/rear/grace/respite/hunters %d/%d/%d/%d/%d | own kills %d = %4.2f/min, K/D %4.2f, all kills %d | '
            'help: own damage %.0f (%.0f of it the bonus), hull mended %.0f, repair pads %d (%.0f points) | error lines %d, stuck etc. %d%s') % (
        x['name'], x['difficulty'], x['nofair'], x['minutes'], x['dmg'], x['unscaled'], pct(x['rear'], x['dmg']), pct(x['line_rear'], x['line_dmg']), pct(x['aft'], x['dmg']),
        x['deaths'], x['deaths'] / x['minutes'], x['bot_deaths'], x['rear_deaths'], pct(x['rear_deaths'], x['deaths']), x['quick'], x['peak'],
        x['held_cap'], x['held_rear'], x['held_grace'], x['held_respite'], x['held_hunters'], x['kills'], x['kills'] / x['minutes'], x['kills'] / max(x['deaths'], 1), x['all_kills'],
        x['human_dmg'], x['human_bonus'], x['hull_regen'], x['repairs'], x['repair_points'], x['errors'], x['bad'],
        ' | other deaths: %s' % x['other'] if x['other'] else '')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    # (The Windows shells do not expand wildcards.)
    paths = [path for arg in sys.argv[1:] for path in (sorted(glob.glob(arg)) or [arg])]
    rows = [row(read(path)) for path in paths]
    groups = {}
    for x in rows:
        print(show(x))
        groups.setdefault(re.sub(r'_\d+$', '', x['name']), []).append(x)
    print()
    for name, xs in groups.items():
        if len(xs) < 2:
            continue
        total = {'name': '%s x%d' % (name, len(xs)), 'difficulty': xs[0]['difficulty'], 'nofair': xs[0]['nofair'], 'other': {}, 'peak': '/'.join(x['peak'] for x in xs)}
        for key in ('minutes', 'dmg', 'rear', 'aft', 'unscaled', 'line_dmg', 'line_rear', 'deaths', 'bot_deaths', 'rear_deaths', 'quick', 'held_cap', 'held_rear', 'held_grace',
                    'held_respite', 'held_hunters', 'kills', 'all_kills', 'errors', 'bad', 'human_dmg', 'human_bonus', 'hull_regen', 'repairs', 'repair_points'):
            total[key] = sum(x[key] for x in xs)
        for x in xs:
            for cause, count in x['other'].items():
                total['other'][cause] = total['other'].get(cause, 0) + count
        print(show(total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
