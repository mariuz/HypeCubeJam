# Makes a copy of a staged web build (BuildCookRun -platform=Wasm ... <out>\Wasm) for hosts with a per-file size
# limit and no header control (GitHub Pages: 100 MB a file).
#
#   <engine python> docs/tools/web_split_for_pages.py <stage>\Wasm <out folder> [--limit-mb 100] [--part-mb 50]
#
# Every file of the stage's wasm-cache.json that is larger than the limit (100 MB = 100 000 000 bytes by default)
# is written as <file>.part000, .part001, ... of --part-mb each and its entry gets "parts": [{"url", "sha1", "size"}] - the page's own format for a file over a host's limit
# (Engine/Platforms/Wasm/Build/Template/sw.js fetchWhole, wasm-loader.js "splitFiles"): the service worker fetches
# the parts in order and hands the page one body. Only whole-file loads can be split (the .wasm); an IoStore
# container (.ucas) is read in byte ranges and must stay under the limit by itself - the script stops if one is not.
# The .br siblings are left out (a host without header control cannot send Content-Encoding) and a .nojekyll file
# is added so that GitHub Pages serves every file as it is, and a .gitattributes "* -text" so that git commits every
# file byte for byte (no CRLF -> LF). Nothing is uploaded.
import argparse
import hashlib
import json
import os
import shutil
import sys


def sha1_of(path):
    digest = hashlib.sha1()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(4 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=(__doc__ or '').split('\n\n')[0])
    parser.add_argument('stage')
    parser.add_argument('out')
    parser.add_argument('--limit-mb', type=float, default=100.0, help='the host limit per file, in units of 1 000 000 bytes')
    parser.add_argument('--part-mb', type=float, default=50.0, help='size of the parts a larger whole-file load is cut into')
    args = parser.parse_args()
    stage = os.path.abspath(args.stage)
    out = os.path.abspath(args.out)
    limit = int(args.limit_mb * 1000 * 1000)
    part_size = int(min(args.part_mb, args.limit_mb) * 1000 * 1000)
    cache_path = os.path.join(stage, 'wasm-cache.json')
    if not os.path.isfile(cache_path):
        print('not a staged web build (no wasm-cache.json): ' + stage)
        return 2
    if os.path.exists(out):
        print('the output folder exists already; remove it first: ' + out)
        return 2

    entries = json.load(open(cache_path, encoding='utf-8'))
    listed = {entry['url']: entry for entry in entries if isinstance(entry, dict) and 'url' in entry}

    # 1. copy everything but the .br siblings
    copied = 0
    for folder, _, names in os.walk(stage):
        for name in names:
            if name.endswith('.br'):
                continue
            source = os.path.join(folder, name)
            target = os.path.join(out, os.path.relpath(source, stage))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(source, target)
            copied += 1

    # 2. split what is over the limit
    problems = []
    for url, entry in listed.items():
        entry['br'] = 0
        path = os.path.join(out, url.replace('/', os.sep))
        size = os.path.getsize(path)
        if size <= limit:
            continue
        if not url.lower().endswith('.wasm'):
            problems.append('%s is %.1f MB and is read in byte ranges: it cannot be split (cut content or cook in chunks)' % (url, size / 1e6))
            continue
        parts = []
        with open(path, 'rb') as handle:
            index = 0
            while True:
                block = handle.read(part_size)
                if not block:
                    break
                part_url = '%s.part%03d' % (url, index)
                with open(os.path.join(out, part_url.replace('/', os.sep)), 'wb') as part:
                    part.write(block)
                parts.append({'url': part_url, 'sha1': hashlib.sha1(block).hexdigest(), 'size': len(block)})
                index += 1
        os.remove(path)
        entry['parts'] = parts
        print('split %s (%.1f MB) into %d parts' % (url, size / 1e6, len(parts)))

    with open(os.path.join(out, 'wasm-cache.json'), 'w', encoding='utf-8', newline='\n') as handle:
        handle.write('[\n' + ',\n'.join('  ' + json.dumps(entry) for entry in entries) + '\n]\n')
    open(os.path.join(out, '.nojekyll'), 'w').close()
    # Git must store every file byte for byte: with core.autocrlf the engine's .js (CRLF lines, listed with its sha1
    # in wasm-cache.json) was committed with LF and the page refused it as a mixed upload (0.23.0, 25 Nov).
    with open(os.path.join(out, '.gitattributes'), 'w', encoding='utf-8', newline='\n') as handle:
        handle.write('* -text\n')

    # 3. report
    total = 0
    count = 0
    largest = []
    for folder, _, names in os.walk(out):
        for name in names:
            path = os.path.join(folder, name)
            size = os.path.getsize(path)
            total += size
            count += 1
            largest.append((size, os.path.relpath(path, out)))
            if size > limit:
                problems.append('%s is %.1f MB, over the limit' % (os.path.relpath(path, out), size / 1e6))
    largest.sort(reverse=True)
    print('%s: %d files, %.1f MB; largest: %s' % (out, count, total / 1e6, ', '.join('%s %.1f MB' % (name, size / 1e6) for size, name in largest[:4])))
    for problem in problems:
        print('PROBLEM: ' + problem)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
