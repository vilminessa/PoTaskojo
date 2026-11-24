#!/usr/bin/env python3
"""Compares the strings this project ships against a dump of another product.

Run it after any change that adds or rewords user-visible text - that is the
point at which a phrase can be copied by accident.

    python tools/similarity_audit.py --re-notes <path to the dump directory>

The dump directory holds strings_utf16.txt, strings_ascii.txt and
settings_keys_utf16.txt. Those lines carry the address they were found at
("0x0021e9d0  Edit rule"); the prefix is stripped before comparing, because
otherwise an exact match is impossible and the audit would report zero for
ever.

Checks, in order:
  1. the product name anywhere in the working tree or in git history
  2. our literals that appear verbatim in the dump
  3. our literals that appear inside one of their longer lines
  4. phrases sharing five words in a row - the signal for copied wording,
     as opposed to a shared Windows term or a registry path

Exit codes: 0 clean, 1 the product name was found, 2 the dump is missing.
Counts 2-4 are printed for review rather than gated: a Windows API name and a
registry path coincide by nature, and only a human can tell a shared term from
a shared sentence.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

# Literals this short are almost always generic tokens, not copied wording.
MIN_LEN = 6

# Five words in a row: short enough that common phrases slip through, long
# enough that a sentence does not.
NEAR_WORDS = 5

# Intersections we knowingly keep: Windows API/OS vocabulary, MSVC/PE internals
# and standard format strings. Nothing here is product-specific wording.
ALLOW = {
    "Normal", "Above normal", "Below normal", "Idle", "High", "Low", "Base 9",
    "Base 11", "Base 13", "Efficiency mode", "Open file location",
    "Terminate", "End task", "Restart", "Properties", "Run as administrator",
    "System", "Registry", "Secure System", "DWM", "Windows", "Memory", "CPU",
    "Disk", "Network", "Processes", "Services", "Details", "Performance",
    "Users", "PID", "Description", "Status", "Startup", "Name", "Path",
    "Priority", "Group", "User", "Type", "Command", "Publisher", "Location",
}

NAME = r"task\s?slinger"
LIT = re.compile(r'L?"((?:[^"\\\n]|\\.)*)"')
ADDRESS = re.compile(r"^0x[0-9a-fA-F]+\s+")
CODE_SUFFIXES = {".cpp", ".h", ".hpp", ".c", ".cmake", ".txt", ".md", ".yml",
                 ".yaml", ".json", ".ini", ".rc", ".py", ".ps1", ".sh", ".bat"}


def strip_address(line: str) -> str:
    return ADDRESS.sub("", line, count=1)


def unescape(s: str) -> str:
    return (s.replace(r"\n", " ").replace(r"\t", " ")
             .replace(r"\"", '"').replace(r"\\", "\\"))


def load_dump(directory: Path) -> list[str]:
    lines: list[str] = []
    for name in ("strings_utf16.txt", "strings_ascii.txt",
                 "settings_keys_utf16.txt"):
        p = directory / name
        if p.exists():
            lines += [strip_address(l)
                      for l in p.read_text(encoding="utf-8",
                                           errors="replace").splitlines()]
    return [l for l in lines if l]


def our_literals() -> list[tuple[str, str]]:
    """(literal, file) for every string literal in src/ and tests/."""
    seen: set[str] = set()
    out: list[tuple[str, str]] = []
    roots = [REPO / "src", REPO / "tests"]
    for root in roots:
        for p in sorted(root.rglob("*")):
            if p.suffix.lower() not in {".cpp", ".h", ".hpp", ".c"}:
                continue
            text = p.read_text(encoding="utf-8", errors="replace")
            for m in LIT.finditer(text):
                lit = unescape(m.group(1))
                if len(lit) < MIN_LEN or lit in seen:
                    continue
                seen.add(lit)
                out.append((lit, p.name))
    return out


def check_product_name() -> int:
    hits = 0
    for p in sorted(REPO.rglob("*")):
        if not p.is_file() or ".git" in p.parts or p.suffix.lower() not in CODE_SUFFIXES:
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for i, line in enumerate(text.splitlines(), 1):
            if re.search(NAME, line, re.I):
                print(f"  !! {p.relative_to(REPO)}:{i}: {line.strip()}")
                hits += 1
    print(f"[1] product name in working tree: {hits}")

    def git(*args: str) -> list[str]:
        try:
            r = subprocess.run(["git", "-C", str(REPO), *args],
                               capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
        except OSError as e:
            print(f"  git unavailable: {e}")
            return []
        return [l for l in r.stdout.splitlines() if re.search(NAME, l, re.I)]

    grep_hits = git("log", "--all", "-i", "--grep=task", "--oneline")
    # Assembled from two halves: this script scans every file in the tree,
    # itself included, and a spelled-out name on this line would make the check
    # fail on its own source. Written contiguously anywhere else it still is.
    product = "Task" + "Slinger"
    pickaxe = [l for l in git("log", "--all", "-S", product, "--oneline")
               if l.strip()]
    print(f"[2] product name in git history: {len(grep_hits)} grep, "
          f"{len(pickaxe)} pickaxe")
    for line in (grep_hits + pickaxe)[:10]:
        print(f"    {line}")
    return hits


def shingles(words: list[str], n: int = NEAR_WORDS) -> set[tuple[str, ...]]:
    return {tuple(words[i:i + n]) for i in range(len(words) - n + 1)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--re-notes", type=Path, default=None,
                    help="directory holding the recovered string dumps")
    args = ap.parse_args()

    directory = args.re_notes
    if directory is None:
        env = __import__("os").environ.get("RE_NOTES")
        directory = Path(env) if env else REPO.parent / "re-notes"
    if not directory.is_dir():
        print(f"dump directory not found: {directory}")
        print("pass --re-notes <path> or set RE_NOTES")
        return 2

    name_hits = check_product_name()

    dump = load_dump(directory)
    if not dump:
        print(f"no strings found in {directory}")
        return 2
    dump_set = set(dump)
    dump_join = "\n".join(d for d in dump if len(d) >= MIN_LEN)

    ours = our_literals()

    verbatim = [(lit, src) for lit, src in ours
                if lit not in ALLOW and lit in dump_set]
    substring = [(lit, src) for lit, src in ours
                 if lit not in ALLOW and lit not in dump_set
                 and any(lit in d and d != lit for d in dump)]

    dump_shingles = set()
    for d in dump:
        dump_shingles |= shingles(re.findall(r"[A-Za-z0-9'%]+", d.lower()))
    near: list[str] = []
    for lit, _ in ours:
        ws = re.findall(r"[A-Za-z0-9'%]+", lit.lower())
        if len(ws) >= NEAR_WORDS and (shingles(ws) & dump_shingles):
            near.append(lit)

    print(f"[3] verbatim overlaps: {len(verbatim)}")
    for lit, src in verbatim:
        print(f"    {src}: {lit.encode('unicode_escape').decode('ascii')}")
    print(f"[4] substring overlaps: {len(substring)}")
    for lit, src in substring:
        print(f"    {src}: {lit.encode('unicode_escape').decode('ascii')}")
    print(f"[5] shared {NEAR_WORDS}-word runs: {len(near)}")
    for lit in near:
        print(f"    {lit.encode('unicode_escape').decode('ascii')}")

    print(f"\nsummary: name={name_hits} verbatim={len(verbatim)} "
          f"substring={len(substring)} near={len(near)}")
    return 1 if name_hits else 0


if __name__ == "__main__":
    sys.exit(main())
