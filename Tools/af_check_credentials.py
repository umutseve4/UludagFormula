#!/usr/bin/env python3
"""Fail the build if a credential-bearing INI key comes back with a value.

Why this exists
---------------
Issue #45. ``SecurityToken`` under
``[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]`` in
``Unreal/Config/DefaultEngine.ini`` was blanked in PR #42. Blanking the tip
does not remove the value from history, and -- more importantly for this
file -- it does not stop the same thing happening again. Unreal regenerates
the token on demand and writes it straight back into the tracked config.
Without a gate, the next editor session re-commits a credential and nobody
notices until an audit.

So this is a regression guard, not a secret scanner. It answers exactly one
question: *does any watched key currently carry a value?*

What it deliberately does NOT claim
-----------------------------------
- It does not scan git history. The value from before ``e271c5e8`` is still
  retrievable and this script cannot change that.
- It does not prove any past token was invalidated.
- It is not a substitute for GitHub secret scanning + push protection, which
  are free on public repositories and catch the commit at push time rather
  than at CI time. Enable both; this file is the belt, that is the braces.

Self-test
---------
``--self-test`` mutates a fixture in memory and requires a finding each time,
*and* requires the clean fixture to produce none. A checker that cannot fail
is decoration, so the checker's ability to fail is itself checked -- the same
rule already applied by ``af_validate_interfaces.py --self-test`` (D-030).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# key name -> human explanation of why it must stay empty
WATCHED_KEYS = {
    "SecurityToken": (
        "AndroidFileServer debug credential; Unreal regenerates it locally, "
        "so it must never be committed with a value (issue #45)"
    ),
}

# Only these extensions are read. Keeping the surface narrow keeps the
# check fast and keeps false positives out of documentation prose.
SCANNED_SUFFIXES = {".ini", ".cfg"}

SKIPPED_DIRS = {".git", "Binaries", "Intermediate", "DerivedDataCache", "Saved"}

# key, optional whitespace, '=', then the rest of the line
_LINE = re.compile(r"^\s*(?P<key>[A-Za-z0-9_]+)\s*=\s*(?P<value>.*?)\s*$")


def findings_in_text(text, origin):
    """Return one message per watched key that carries a non-empty value."""
    out = []
    for number, line in enumerate(text.splitlines(), start=1):
        if line.lstrip().startswith((";", "#")):
            continue
        match = _LINE.match(line)
        if match is None:
            continue
        key = match.group("key")
        if key not in WATCHED_KEYS:
            continue
        value = match.group("value")
        if value:
            out.append(
                "{}:{}: {} has a value ({} characters). {}".format(
                    origin, number, key, len(value), WATCHED_KEYS[key]
                )
            )
    return out


def scan_tree(root):
    out = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix not in SCANNED_SUFFIXES:
            continue
        if any(part in SKIPPED_DIRS for part in path.parts):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        out.extend(findings_in_text(text, str(path.relative_to(root))))
    return out


CLEAN_FIXTURE = """\
[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]
bEnablePlugin=True
bAllowNetworkConnection=True
; SecurityToken is deliberately left empty.
SecurityToken=
bIncludeInShipping=False
"""

# Each mutation must be caught. If any one of them slips through, the
# checker is lying about the clean tree too.
MUTATIONS = [
    ("plain value", "SecurityToken=", "SecurityToken=abc123"),
    ("spaced value", "SecurityToken=", "SecurityToken = abc123"),
    ("leading indent", "SecurityToken=", "\tSecurityToken=abc123"),
    ("single character", "SecurityToken=", "SecurityToken=x"),
    ("quoted value", "SecurityToken=", 'SecurityToken="abc123"'),
]


def self_test():
    failures = []

    clean = findings_in_text(CLEAN_FIXTURE, "<clean>")
    if clean:
        failures.append(
            "the clean fixture produced findings, so the checker fires on "
            "everything and proves nothing: {}".format(clean)
        )
    else:
        print("  ok    clean fixture -> no findings")

    for name, old, new in MUTATIONS:
        mutated = CLEAN_FIXTURE.replace(old, new, 1)
        if mutated == CLEAN_FIXTURE:
            failures.append("mutation '{}' did not change the fixture".format(name))
            continue
        if not findings_in_text(mutated, "<mutant>"):
            failures.append("mutation '{}' was NOT detected".format(name))
        else:
            print("  ok    mutation '{}' -> detected".format(name))

    # A commented-out token is not a committed credential; firing on it
    # would train people to ignore this check.
    commented = CLEAN_FIXTURE.replace(
        "SecurityToken=", "; SecurityToken=abc123\nSecurityToken=", 1
    )
    if findings_in_text(commented, "<comment>"):
        failures.append("a commented-out line was reported as a finding")
    else:
        print("  ok    commented-out value -> not a finding")

    if failures:
        print("\nSELF-TEST FAILED", file=sys.stderr)
        for item in failures:
            print("  - {}".format(item), file=sys.stderr)
        return 1
    print(
        "\nself-test passed ({} mutations + 2 negative cases)".format(len(MUTATIONS))
    )
    return 0


def main():
    parser = argparse.ArgumentParser(description="committed-credential regression guard")
    parser.add_argument("--root", default=".", help="repository root to scan")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="check that the checker can still fail, then exit",
    )
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    root = Path(args.root).resolve()
    if not root.is_dir():
        print("not a directory: {}".format(root), file=sys.stderr)
        return 2

    findings = scan_tree(root)
    if findings:
        print("committed credential values found:", file=sys.stderr)
        for item in findings:
            print("  {}".format(item), file=sys.stderr)
        print(
            "\nBlank the value, do not delete the key -- Unreal will "
            "regenerate it locally. See issue #45.",
            file=sys.stderr,
        )
        return 1

    watched = ", ".join(sorted(WATCHED_KEYS))
    print("no committed values for watched keys ({}) under {}".format(watched, root))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
