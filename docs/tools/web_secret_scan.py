# Checks that nothing of [HypeCubeJam.EOS] (Config/DefaultEngine.ini) is in a staged web build.
#
#   <engine python> docs/tools/web_secret_scan.py <stage folder> [<UnrealPak.exe>] [<scratch folder>]
#
# The values are read from the project's ini and searched for (UTF-8 and UTF-16LE) in every staged file; with
# UnrealPak given, every .pak of the stage is also extracted (paks may be compressed) and the extracted files are
# searched, as are the ini files inside for the section name. NEVER prints a value: only key names, file names and
# counts. Exit code 0 = nothing found, 1 = something found, 2 = usage / setup error.
import os
import subprocess
import sys

KEYS = ('ClientSecret', 'ClientId', 'DeploymentId', 'SandboxId', 'ProductId')
SECTION = 'HypeCubeJam.EOS'


def read_values(ini_path):
    values = {}
    section = None
    with open(ini_path, encoding='utf-8-sig', errors='replace') as handle:
        for line in handle:
            line = line.strip()
            if line.startswith('[') and line.endswith(']'):
                section = line[1:-1]
            elif section == SECTION and '=' in line and not line.startswith(';'):
                key, value = line.split('=', 1)
                key = key.strip().lstrip('+')
                value = value.strip().strip('"')
                if key in KEYS and len(value) >= 6:
                    values[key] = value
    return values


def needles(values):
    result = []
    for key, value in values.items():
        result.append((key, value.encode('utf-8')))
        result.append((key, value.encode('utf-16-le')))
    result.append(('section name [' + SECTION + ']', ('[' + SECTION + ']').encode('utf-8')))
    result.append(('section name [' + SECTION + ']', ('[' + SECTION + ']').encode('utf-16-le')))
    return result


def scan_tree(root, patterns):
    hits = []
    files = 0
    total = 0
    longest = max(len(p) for _, p in patterns)
    for folder, _, names in os.walk(root):
        for name in names:
            path = os.path.join(folder, name)
            files += 1
            found = set()
            tail = b''
            with open(path, 'rb') as handle:
                while True:
                    block = handle.read(8 * 1024 * 1024)
                    if not block:
                        break
                    total += len(block)
                    data = tail + block
                    for key, pattern in patterns:
                        if pattern in data:
                            found.add(key)
                    tail = data[-(longest - 1):]
            for key in sorted(found):
                hits.append((os.path.relpath(path, root), key))
    return files, total, hits


def main():
    if len(sys.argv) < 2:
        print(__doc__ or 'usage: web_secret_scan.py <stage> [UnrealPak.exe] [scratch]')
        return 2
    stage = os.path.abspath(sys.argv[1])
    unreal_pak = sys.argv[2] if len(sys.argv) > 2 else None
    scratch = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None
    project = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    values = read_values(os.path.join(project, 'Config', 'DefaultEngine.ini'))
    if 'ClientSecret' not in values:
        print('setup error: no ClientSecret value in [%s] of Config/DefaultEngine.ini (nothing to search for)' % SECTION)
        return 2
    patterns = needles(values)
    print('searching for the values of: %s (and the section name); values are never printed' % ', '.join(sorted(values)))

    bad = False
    files, total, hits = scan_tree(stage, patterns)
    print('stage %s: %d files, %.1f MB read, %d hit(s)' % (stage, files, total / 1048576.0, len(hits)))
    for path, key in hits:
        print('  FOUND %s in %s' % (key, path))
        bad = True

    if unreal_pak and scratch:
        paks = []
        for folder, _, names in os.walk(stage):
            paks += [os.path.join(folder, n) for n in names if n.lower().endswith('.pak')]
        for index, pak in enumerate(paks):
            out = os.path.join(scratch, 'pak%d' % index)
            os.makedirs(out, exist_ok=True)
            run = subprocess.run([unreal_pak, pak, '-Extract', out], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            files, total, hits = scan_tree(out, patterns)
            inis = sum(1 for _, _, names in os.walk(out) for n in names if n.lower().endswith('.ini'))
            print('extracted %s (UnrealPak exit %d): %d files (%d .ini), %.1f MB read, %d hit(s)'
                  % (os.path.relpath(pak, stage), run.returncode, files, inis, total / 1048576.0, len(hits)))
            if run.returncode != 0 or files == 0:
                print('  the pak could not be extracted: NOT verified')
                bad = True
            for path, key in hits:
                print('  FOUND %s in %s' % (key, path))
                bad = True
    else:
        print('no UnrealPak given: the paks were searched as stored only (compressed blocks are NOT covered)')

    print('RESULT: %s' % ('FOUND - do not publish' if bad else 'clean'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
