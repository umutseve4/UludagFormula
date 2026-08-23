#!/usr/bin/env python3
"""One-shot, idempotent patcher: teach af_static_validate.py about the BOM.

Run on a CI runner rather than transcribed by hand. The point is blast
radius: a script that asserts each anchor matches exactly once, applied on
the runner, turns "I believe I edited the right lines" into a commit diff
that can be read and counted. A half-applied patch fails loudly here
instead of silently landing.

Exit codes: 0 patched or already patched, 2 an anchor did not match exactly
once (drift - do not commit).
"""

import io
import sys

TARGET = "Tools/af_static_validate.py"
SENTINEL = "def has_utf8_bom("

OLD_READ_TEXT = '''def read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()
'''

NEW_READ_TEXT = '''def read_text(path: str) -> str:
    """Read a source file as text, tolerating a UTF-8 BOM.

    "utf-8-sig" rather than "utf-8" is deliberate. Python's plain "utf-8"
    codec does not consume a leading EF BB BF; it decodes it to U+FEFF and
    hands it back as the first character of the string. Every rule that
    anchors on the start of a file then fails on a file that is, to the eye
    and to every editor, entirely correct - a BOM has zero width, so it is
    invisible in a diff, in a blob view and in review.

    This is not hypothetical. It held this repository's CI red and presented
    itself as ten simultaneous copyright-header violations. Stripping the
    BOM here is only half the fix; see has_utf8_bom and check_header_hygiene
    for the half that keeps the fault visible under its own name.
    """
    with open(path, "r", encoding="utf-8-sig") as handle:
        return handle.read()


def has_utf8_bom(path: str) -> bool:
    """True when the file begins with the UTF-8 byte order mark.

    Deliberately inspects raw bytes, because read_text now strips the BOM by
    design. The two functions are complements: one stops a BOM from
    corrupting every other rule, the other makes sure its presence is still
    reported. Silently tolerating it would trade a confusing failure for an
    invisible one, which is worse.

    PowerShell is the usual source - both Out-File and > default to UTF-8
    with a BOM.
    """
    try:
        with open(path, "rb") as handle:
            return handle.read(3) == b"\\xef\\xbb\\xbf"
    except OSError:
        return False
'''

OLD_ATTRIB = """        # Attribution on every file. This is an originality requirement, not a"""

NEW_ATTRIB = """        # A BOM is reported under its own name. Without this rule the fault
        # is mute - invisible in every editor - and surfaces as whichever
        # start-of-file rule happens to run first, sending the reader hunting
        # for a copyright bug that does not exist.
        report.check(
            not has_utf8_bom(path),
            "%s has no UTF-8 BOM" % rel,
            "write UTF-8 without BOM; PowerShell Out-File and > add one",
        )

        # Attribution on every file. This is an originality requirement, not a"""


def main():
    with io.open(TARGET, "r", encoding="utf-8") as handle:
        text = handle.read()

    if SENTINEL in text:
        sys.stdout.write("already patched; nothing to do\n")
        return 0

    for label, old in (("read_text", OLD_READ_TEXT), ("attribution", OLD_ATTRIB)):
        count = text.count(old)
        if count != 1:
            sys.stderr.write("ANCHOR DRIFT: %s matched %d times, expected 1\n"
                             % (label, count))
            return 2

    text = text.replace(OLD_READ_TEXT, NEW_READ_TEXT)
    text = text.replace(OLD_ATTRIB, NEW_ATTRIB)

    with io.open(TARGET, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)

    sys.stdout.write("patched %s\n" % TARGET)
    return 0


if __name__ == "__main__":
    sys.exit(main())
