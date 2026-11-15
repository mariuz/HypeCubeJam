#!/usr/bin/env python
"""Computer voice: lines spoken and declined in game logs (LogHCJ "Voice:" lines; devlog item 32).

    python docs/tools/voice_stats.py <log> [<log> ...]

Per log: lines spoken, requests declined, lines a minute from the first line to the end of the log, the most lines
that started inside any 60 s, and the counts per line and per reason.
"""
import re, sys, datetime, collections
for path in sys.argv[1:]:
    spoken, declined = [], []
    first = last = None
    reasons = collections.Counter(); lines = collections.Counter(); dl = collections.Counter()
    errors = 0
    for raw in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'\[(\d+)\.(\d+)\.(\d+)-(\d+)\.(\d+)\.(\d+):(\d+)\]', raw)
        if not m:
            continue
        t = datetime.datetime(*map(int, m.groups()[:6]), int(m.group(7)) * 1000).timestamp()
        if 'Playthrough: event=stats' in raw or 'Voice:' in raw:
            first = first or t
            last = t
        if ': Error:' in raw:
            errors += 1
        v = re.search(r'Voice: (VO_Computer_\w+)(.*)', raw)
        if v:
            if ' declined, ' in v.group(2):
                declined.append(t); dl[v.group(1)] += 1
                reasons[raw.split('declined, ')[1].split(' (')[0]] += 1
            else:
                spoken.append(t); lines[v.group(1)] += 1
    span = last - spoken[0] if spoken else 0
    worst = max((sum(1 for u in spoken if 0 <= u - s < 60.0) for s in spoken), default=0)
    print(path)
    print('  spoken %d, declined %d, from first line to end of log %.0f s -> %.2f lines a minute; worst 60 s window %d; error lines %d' % (
        len(spoken), len(declined), span, len(spoken) / (span / 60.0) if span else 0, worst, errors))
    print('  spoken:', dict(lines))
    print('  declined:', dict(dl))
    print('  reasons:', dict(reasons))
