# Checks a BuildCookRun / cook log for errors other than the one known engine ensure, so that a build made with
# -IgnoreCookErrors still fails on a real cook error (devlog item 43).
#
#   python -I docs\tools\cook_error_guard.py <log> [<log> ...]
#
# The known ensure (engine of 22 Nov 2026, AssetReferenceRestrictions plugin):
#   LogOutputDevice: Error: begin: stack for UAT
#   LogOutputDevice: Error: === Handled ensure: ===
#   LogOutputDevice: Error: Ensure condition failed: !DefaultDomain.IsValid()  [File:...AssetReferencingDomains.cpp] [Line: 101]
#   LogOutputDevice: Error: Registered the path /ToolsetRegistry/ for two different domains: Plugin:ToolsetRegistry and Never Cooked Content
#   LogOutputDevice: Error: Stack:
#   LogOutputDevice: Error: [Callstack] 0x... <module>!UnknownFunction []      (any number)
#   LogOutputDevice: Error: end: stack for UAT
# A block between "begin: stack for UAT" and "end: stack for UAT" is accepted only when it holds both of those two
# message lines and nothing else but the frame lines (=== Handled ensure: ===, Stack:, [Callstack] ..., empty).
# Every other error line - also one inside a block with anything extra - is reported. The cook summary's
# "N error(s)" must equal the number of error lines of accepted blocks before it (each "begin ... end" block counts
# all of its lines, 29 here); a larger count fails too, as does a log with no cook summary at all.
#
# Exit code: 0 clean (only the known ensure, or nothing), 1 unknown errors or count mismatch, 2 bad usage / unreadable.
import re
import sys

KNOWN_MESSAGES = (
    re.compile(r"^Ensure condition failed: !DefaultDomain\.IsValid\(\)\s+\[File:[^\]]*[\\/]AssetReferencingDomains\.cpp\]\s*\[Line: \d+\]\s*$"),
    re.compile(r"^Registered the path /ToolsetRegistry/ for two different domains: Plugin:ToolsetRegistry and Never Cooked Content\s*$"),
)
FRAME_LINE = re.compile(r"^(?:|=== Handled ensure: ===|Stack:|\[Callstack\] 0x[0-9a-fA-F]+ \S+ \[[^\]]*\])\s*$")

# Optional "[2026.11.24-15.55.44:123][  0]" time stamp, then the summary's "LogInit: Display: " repeat prefix.
STAMP = re.compile(r"^\[\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d:\d{3}\]\[\s*\d+\]")
SUMMARY_PREFIX = "LogInit: Display: "
# An Unreal error line: "<Category>: Error: <text>" (also "Fatal"); text may be empty.
UE_ERROR = re.compile(r"^(?P<cat>[A-Za-z0-9_]+): (?:Error|Fatal): ?(?P<msg>.*)$")
# Other error shapes: compiler / linker / clang / UAT / ensure / assert lines that are not in the Unreal form.
OTHER_ERROR = re.compile(
    r"(?:\berror [A-Z]{1,3}\d{2,5}\b"          # MSVC / C# / linker: error C2065, error CS0103, error LNK2019
    r"|:\s*(?:fatal )?error\s*:"               # clang / emscripten: file.cpp:1:2: error: ...
    r"|^ERROR:"                                # UAT
    r"|\bEnsure condition failed\b"
    r"|\bAssertion failed\b"
    r"|\bFatal error\b)"
)
SUMMARY_COUNT = re.compile(r"^(?:Success|Failure) - (\d+) error\(s\), (\d+) warning\(s\)\s*$")
SUMMARY_HEAD = "Warning/Error Summary (Unique only)"


def read_text(path):
    with open(path, "rb") as f:
        data = f.read()
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    if data.startswith(b"\xef\xbb\xbf"):
        data = data[3:]
    # A UTF-16 file without a BOM (rare): many NUL bytes.
    if data[:2000].count(b"\x00") > 200:
        return data.decode("utf-16-le", errors="replace")
    return data.decode("utf-8", errors="replace")


def normalise(line):
    line = line.rstrip("\r\n")
    line = STAMP.sub("", line)
    summary = line.startswith(SUMMARY_PREFIX)
    if summary:
        line = line[len(SUMMARY_PREFIX):]
    return line, summary


def check(path):
    """Returns (problems, info); problems is a list of strings, empty when the log passes."""
    lines = read_text(path).splitlines()
    problems = []
    accepted_blocks = 0
    accepted_lines_since_summary = 0
    summaries = 0
    in_summary = False
    i = 0
    n = len(lines)
    while i < n:
        text, from_summary = normalise(lines[i])
        if text.strip() == SUMMARY_HEAD:
            in_summary = True
            i += 1
            continue
        m = SUMMARY_COUNT.match(text)
        if m:
            summaries += 1
            count = int(m.group(1))
            if count != accepted_lines_since_summary:
                problems.append(
                    "line %d: cook summary says %d error(s), but only %d error line(s) are the known ensure: %s"
                    % (i + 1, count, accepted_lines_since_summary, text.strip()))
            accepted_lines_since_summary = 0
            in_summary = False
            i += 1
            continue
        ue = UE_ERROR.match(text)
        if ue and ue.group("msg").strip() == "begin: stack for UAT":
            # Collect the block up to its end marker (same category, same summary-ness).
            block = [(i, text)]
            j = i + 1
            closed = False
            while j < n:
                t2, s2 = normalise(lines[j])
                block.append((j, t2))
                u2 = UE_ERROR.match(t2)
                if s2 != from_summary or not u2 or u2.group("cat") != ue.group("cat"):
                    break  # a line that is not part of the block: the block is broken
                if u2.group("msg").strip() == "end: stack for UAT":
                    closed = True
                    break
                j += 1
            inner = [UE_ERROR.match(t).group("msg") for _, t in block[1:-1]] if closed else []
            seen = [False] * len(KNOWN_MESSAGES)
            ok = closed
            for msg in inner:
                hit = False
                for k, pat in enumerate(KNOWN_MESSAGES):
                    if pat.match(msg):
                        seen[k] = True
                        hit = True
                if not hit and not FRAME_LINE.match(msg):
                    ok = False
            if ok and all(seen):
                accepted_blocks += 1
                if not from_summary:
                    accepted_lines_since_summary += len(block)
                i = j + 1
                continue
            # Unknown block: report each error line of it, then go on after the block (or at the line that broke it).
            last = j if closed else j - 1
            for k in range(i, last + 1):
                t, _ = normalise(lines[k])
                problems.append("line %d: %s" % (k + 1, t.strip()))
            i = last + 1
            continue
        if ue or OTHER_ERROR.search(text):
            problems.append("line %d: %s" % (i + 1, lines[i].rstrip()))
        i += 1
    if summaries == 0:
        problems.append("no cook summary ('N error(s), M warning(s)') in the log: did the cook run?")
    info = "%d known ensure block(s), %d cook summar%s" % (accepted_blocks, summaries, "y" if summaries == 1 else "ies")
    return problems, info


def main(argv):
    if len(argv) < 2:
        print("usage: python -I cook_error_guard.py <BuildCookRun or cook log> [...]", file=sys.stderr)
        return 2
    failed = False
    for path in argv[1:]:
        try:
            problems, info = check(path)
        except OSError as e:
            print("COOK GUARD: cannot read %s: %s" % (path, e), file=sys.stderr)
            return 2
        if problems:
            failed = True
            print("COOK GUARD FAILED: %s (%s), %d unknown error line(s) / problem(s):" % (path, info, len(problems)))
            for p in problems[:200]:
                print("  " + p)
            if len(problems) > 200:
                print("  ... %d more" % (len(problems) - 200))
        else:
            print("COOK GUARD OK: %s (%s, no other error)" % (path, info))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
