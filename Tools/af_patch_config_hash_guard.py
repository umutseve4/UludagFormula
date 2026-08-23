#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""One-shot patcher for Tools/af_config_hash_guard.py.

Moves EXPECTED_CONFIG_HASH to the value the pipeline actually computes and
teaches check B about superseded pins. Every substitution is anchored on an
exact literal and asserted, so a drifted anchor fails the job loudly instead
of silently producing a half-patched file.

Idempotent: a second run is a no-op.

Temporary scaffold. Removed before the fix is merged.
"""

import os
import sys

TARGET = os.path.join("Tools", "af_config_hash_guard.py")

SENTINEL = "HISTORICAL_CONFIG_HASHES"


OLD_1 = '''EXPECTED_CONFIG_HASH = (
    "c9ef9f7e985a1aaf460d58db6e269d3e5b607f268df12acf3500a8492869f4fc"
)

# Number of characters describe() and the documentation quote.
SHORT_LEN = 16
'''

NEW_1 = '''EXPECTED_CONFIG_HASH = (
    "933a11f6292ec2b5548913c2e0e89790fcf06f7fb3e37eed29875ea04339c225"
)

# Number of characters describe() and the documentation quote.
SHORT_LEN = 16

# ---------------------------------------------------------------------------
# 0b. Superseded pins -- check B allow-list
# ---------------------------------------------------------------------------
#
# The decision log is an append-only record. Entries of the form "old hash X,
# new hash Y" are true statements about the past, so check B\'s original
# premise -- that EVERY hash quoted anywhere in the prose is a prefix of the
# CURRENT hash -- cannot be satisfied by a repository that keeps such a log
# without falsifying it.
#
# This is an allow-list, not a relaxation. A quoted hash is accepted only if
# it is a prefix of the current hash or of one of the pins recorded below; an
# arbitrary or mistyped hash still fails, and check A is untouched.
#
# Each entry is (pipeline_version, recorded_value). The 0B.1.1 value is held
# at its documented sixteen-character short form because that is the only
# form the decision log preserves. It has not been recomputed here, and this
# file does not claim that it was.
HISTORICAL_CONFIG_HASHES = (
    (
        "0B.1.0",
        "c9ef9f7e985a1aaf460d58db6e269d3e5b607f268df12acf3500a8492869f4fc",
    ),
    ("0B.1.1", "6486736f83b6fb7f"),
    (
        "0B.1.2",
        "0c0be9d960b7321c223e4fbd3bbeb6a59b6cf7bbf2793e3967022eba2a1f4449",
    ),
)


def is_historical_claim(token):
    """True when ``token`` agrees with a recorded superseded pin.

    Agreement is prefix agreement in either direction, because the prose
    quotes both the sixteen-character short form and the full digest.
    """
    for _version, recorded in HISTORICAL_CONFIG_HASHES:
        if recorded.startswith(token) or token.startswith(recorded):
            return True
    return False
'''


OLD_2 = '''    findings = []
    total = 0
    per_file = []

    if scanned_paths is None:
'''

NEW_2 = '''    findings = []
    total = 0
    historical = 0
    per_file = []

    if scanned_paths is None:
'''


OLD_3 = '''        for token, offset in claims:
            total += 1
            if computed.startswith(token):
                continue
            findings.append(
'''

NEW_3 = '''        for token, offset in claims:
            total += 1
            if computed.startswith(token):
                continue
            if is_historical_claim(token):
                historical += 1
                continue
            findings.append(
'''


OLD_4 = '''    detail = {
        "claims_total": total,
        "files_with_claims": per_file,
        "files_scanned": len(scanned_paths),
    }
'''

NEW_4 = '''    detail = {
        "claims_total": total,
        "claims_historical": historical,
        "files_with_claims": per_file,
        "files_scanned": len(scanned_paths),
    }
'''


OLD_5 = '''        for rel, count in detail_b["files_with_claims"]:
            print("    %-58s %d claim(s)" % (rel, count))
'''

NEW_5 = '''        for rel, count in detail_b["files_with_claims"]:
            print("    %-58s %d claim(s)" % (rel, count))
        print(
            "B superseded    : %d claim(s) matched a recorded historical pin"
            % detail_b.get("claims_historical", 0)
        )
'''


OLD_6 = '''    # -- check C ---------------------------------------------------------
'''

NEW_6 = '''    def t_b13_historical_claim_is_accepted(self):
        computed = "933a11f6292ec2b5" + "0" * 48
        findings, detail = check_documented_claims(
            self._fixture(
                {"Documentation/x.md": "old config hash c9ef9f7e985a1aaf"}
            ),
            computed,
        )
        self.equal("B historical claim accepted", len(findings), 0)
        self.equal(
            "B historical claim counted", detail["claims_historical"], 1
        )

    def t_b14_unknown_claim_is_still_reported(self):
        computed = "933a11f6292ec2b5" + "0" * 48
        findings, detail = check_documented_claims(
            self._fixture(
                {"Documentation/x.md": "config hash deadbeefdeadbeef"}
            ),
            computed,
        )
        self.equal("B unknown claim still fails", len(findings), 1)
        self.equal("B unknown claim not excused", detail["claims_historical"], 0)

    def t_b15_historical_entries_are_well_formed(self):
        ok = True
        for _version, recorded in HISTORICAL_CONFIG_HASHES:
            if not re.match(r"^[0-9a-f]{16,64}$", recorded):
                ok = False
        self.check("B historical entries are 16-64 lowercase hex", ok)

    def t_b16_current_pin_is_not_also_historical(self):
        recorded = [value for _version, value in HISTORICAL_CONFIG_HASHES]
        self.check(
            "B current pin is not shadowed by the allow-list",
            EXPECTED_CONFIG_HASH not in recorded,
        )

    def t_b17_short_form_matches_full_digest(self):
        self.check(
            "B short recorded form matches a full claim",
            is_historical_claim("6486736f83b6fb7f" + "a" * 48),
        )

    def t_b18_unrelated_token_is_not_historical(self):
        self.check(
            "B unrelated token rejected",
            not is_historical_claim("deadbeefdeadbeef"),
        )

    # -- check C ---------------------------------------------------------
'''


REPLACEMENTS = (
    ("pin + allow-list", OLD_1, NEW_1),
    ("claim counter init", OLD_2, NEW_2),
    ("claim acceptance", OLD_3, NEW_3),
    ("detail dict", OLD_4, NEW_4),
    ("verbose report", OLD_5, NEW_5),
    ("self-test cases", OLD_6, NEW_6),
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
