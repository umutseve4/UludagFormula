#!/usr/bin/env python3
"""Strip the UTF-8 BOM from every .h/.cpp file under Unreal/Source.

Why this exists
---------------
Tools/af_static_validate.py reads source files with:

    open(path, "r", encoding="utf-8")

The "utf-8" codec does NOT consume a byte-order mark; it decodes the three
bytes EF BB BF into a leading U+FEFF character. check_header_hygiene then
asserts:

    text.startswith(COPYRIGHT_LINE)

which is False for a BOM-prefixed file even when the copyright line itself is
byte-for-byte correct. The failure is invisible in any editor, because a BOM
is a zero-width character.

A UTF-8 BOM is never required. It is emitted by default by PowerShell's
Out-File / '>' redirection and by some Windows editors, which is the most
likely way these files acquired one.

This script is idempotent: running it on a clean tree changes nothing and
exits 0. It is intended to be run once, from the maintenance workflow, and
then deleted along with it.
"""

from __future__ import annotations

import os
import sys

BOM = b"\xef\xbb\xbf"
SOURCE_EXTENSIONS = (".h", ".cpp")

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_ROOT = os.path.join(REPO_ROOT, "Unreal", "Source")


def strip_bom_from_tree(source_root: str, repo_root: str) -> list:
    """Rewrite every BOM-prefixed source file in place. Return the relative paths."""
    stripped = []
    for dirpath, _dirnames, filenames in os.walk(source_root):
        for filename in sorted(filenames):
            if not filename.endswith(SOURCE_EXTENSIONS):
                continue
            path = os.path.join(dirpath, filename)
            with open(path, "rb") as handle:
                data = handle.read()
            if not data.startswith(BOM):
                continue
            with open(path, "wb") as handle:
                handle.write(data[len(BOM):])
            stripped.append(os.path.relpath(path, repo_root).replace(os.sep, "/"))
    return stripped


def main() -> int:
    if not os.path.isdir(SOURCE_ROOT):
        print("FAIL missing source root: %s" % SOURCE_ROOT)
        return 1

    scanned = 0
    for _dirpath, _dirnames, filenames in os.walk(SOURCE_ROOT):
        scanned += sum(1 for f in filenames if f.endswith(SOURCE_EXTENSIONS))

    stripped = strip_bom_from_tree(SOURCE_ROOT, REPO_ROOT)

    print("scanned %d source files under Unreal/Source" % scanned)
    for rel in stripped:
        print("stripped BOM: %s" % rel)
    print("total stripped: %d" % len(stripped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
