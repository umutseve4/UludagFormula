#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""One-shot patcher for the Tools/af_config_hash_guard.py docstring.

Round 1 of this scaffold moved the pin and added the superseded-pin
allow-list. Round 2 (this file) brings the module docstring back in line
with the behaviour, so the guard does not misdescribe its own contract.

Every substitution is anchored on an exact literal and asserted.
Idempotent: a second run is a no-op.

Temporary scaffold. Removed before the fix is merged.
"""

import os
import sys

TARGET = os.path.join("Tools", "af_config_hash_guard.py")

SENTINEL = "superseded pins recorded in"


OLD_1 = '''  B: every configuration hash quoted anywhere in the repository\'s Markdown
     is a prefix of the computed hash;
'''

NEW_1 = '''  B: every configuration hash quoted anywhere in the repository\'s Markdown
     is a prefix of the computed hash, or of one of the superseded pins
     recorded in ``HISTORICAL_CONFIG_HASHES``;
'''


OLD_2 = '''Changing a configuration value is expected to break check A.  That is the
point.  The correct response is to re-run this guard, read the reported
value, update ``EXPECTED_CONFIG_HASH`` deliberately in the same change set,
and update the quoted values in the documentation.  The response is never
to relax the check.
'''

NEW_2 = '''Changing a configuration value is expected to break check A.  That is the
point.  The correct response is to re-run this guard, read the reported
value, update ``EXPECTED_CONFIG_HASH`` deliberately in the same change set,
move the value it replaced into ``HISTORICAL_CONFIG_HASHES``, and update
every documentation quotation that describes the *current* configuration.
The response is never to relax the check.

Quotations that describe a *past* configuration are a different matter.  The
decision log is append-only, so "old hash X, new hash Y" entries stay true
forever and must not be rewritten to match the present.  Check B therefore
consults an explicit allow-list of superseded pins.  That is a widening of
what counts as an honest quotation, not a weakening of the check: a hash
that is neither current nor a recorded predecessor still fails, and check A
is unaffected.
'''


REPLACEMENTS = (
    ("check B summary", OLD_1, NEW_1),
    ("maintenance guidance", OLD_2, NEW_2),
)


def main():
    if not os.path.isfile(TARGET):
        sys.stderr.write("target not found: %s\n" % TARGET)
        return 2

    handle = open(TARGET, "r", encoding="utf-8")
    try:
        text = handle.read()
    finally:
        handle.close()

    if SENTINEL in text:
        print("already patched; nothing to do")
        return 0

    for label, old, new in REPLACEMENTS:
        count = text.count(old)
        if count != 1:
            sys.stderr.write(
                "anchor %r matched %d times, expected exactly 1\n"
                % (label, count)
            )
            return 2
        text = text.replace(old, new, 1)
        print("patched: %s" % label)

    handle = open(TARGET, "w", encoding="utf-8", newline="\n")
    try:
        handle.write(text)
    finally:
        handle.close()

    print("wrote %s" % TARGET)
    return 0


if __name__ == "__main__":
    sys.exit(main())
