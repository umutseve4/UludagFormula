#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""UludagFormula -- configuration hash drift guard (decision D-046).

Purpose
-------
``BlenderPipeline/scripts/af_pipeline_config.py`` already computes a stable
SHA-256 over ``effective_config()`` and ``describe()`` already prints its
first sixteen characters.  What did **not** exist before D-046 was any
mechanism that fails when that value silently changes.

A silent change is the dangerous case.  ``DESIGN`` is the single source of
truth for every generated dimension (decision D-041).  Editing one number in
it changes every downstream artefact, yet nothing in the repository or in CI
noticed, and any hash quoted in the documentation quietly became a lie.

This guard closes that gap with three checks:

  A: the hash computed by the real module equals the pinned constant
     ``EXPECTED_CONFIG_HASH`` below;
  B: every configuration hash quoted anywhere in the repository's Markdown
     is a prefix of the computed hash;
  C: ``describe()`` really emits the first sixteen characters of the hash,
     so the human-facing report cannot drift away from the computed value.

Scope and honesty
-----------------
This guard proves *agreement between the module, the pin and the prose*.
It does not prove that the configuration is correct, that Blender accepts
it, or that any mesh was ever generated.  No Blender, no Unreal, no DCC of
any kind is involved; the module is imported as plain CPython.

Changing a configuration value is expected to break check A.  That is the
point.  The correct response is to re-run this guard, read the reported
value, update ``EXPECTED_CONFIG_HASH`` deliberately in the same change set,
and update the quoted values in the documentation.  The response is never
to relax the check.

Exit codes
----------
  0  all selected checks passed
  1  at least one check failed
  2  the guard could not run (missing file, unreadable module, bad usage)

Standalone by design: this module imports nothing from any other guard in
``Tools/`` (precedent D-037).  Python 3.9 compatible: no f-strings, no
walrus, ``%``-formatting only, because CI runs a 3.9 matrix leg.
"""

from __future__ import annotations

import os
import re
import sys

# ---------------------------------------------------------------------------
# 0. Pinned expectation
# ---------------------------------------------------------------------------

# Full SHA-256 of json.dumps(effective_config(), sort_keys=True,
# separators=(",", ":")) as produced by
# BlenderPipeline/scripts/af_pipeline_config.py.
#
# Update this ONLY together with a deliberate configuration change, and say
# so in the commit message.
EXPECTED_CONFIG_HASH = (
    "933a11f6292ec2b5548913c2e0e89790fcf06f7fb3e37eed29875ea04339c225"
)

# Number of characters describe() and the documentation quote.
SHORT_LEN = 16

# ---------------------------------------------------------------------------
# 0b. Superseded pins -- check B allow-list
# ---------------------------------------------------------------------------
#
# The decision log is an append-only record. Entries of the form "old hash X,
# new hash Y" are true statements about the past, so check B's original
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

# ---------------------------------------------------------------------------
# 1. Repository geometry
# ---------------------------------------------------------------------------

PATH_CONFIG_MODULE = os.path.join(
    "BlenderPipeline", "scripts", "af_pipeline_config.py"
)

# Directories scanned by check B, relative to --root.
DOC_SCAN_DIRS = ("Documentation", "Tools", ".github")

# Files scanned by check B in the repository root itself.
DOC_SCAN_ROOT_SUFFIXES = (".md",)

# Extensions scanned inside DOC_SCAN_DIRS.
DOC_SCAN_SUFFIXES = (".md", ".yml", ".yaml")

# A hash claim is a "config hash" phrase followed, within a short window, by
# a run of at least SHORT_LEN lowercase hex characters.  The window keeps the
# match local so an unrelated SHA on the next line is not swept up.
CLAIM_WINDOW = 80

RE_CLAIM_ANCHOR = re.compile(r"config[ _\-]?hash", re.IGNORECASE)
RE_HEX_RUN = re.compile(r"\b([0-9a-f]{%d,64})\b" % SHORT_LEN)

# Files that legitimately hold the pin itself; their occurrences are still
# checked, they are simply reported under their own label.
SELF_FILES = (os.path.join("Tools", "af_config_hash_guard.py"),)


# ---------------------------------------------------------------------------
# 2. Result plumbing
# ---------------------------------------------------------------------------


class Finding(object):
    """One guard finding.  ``prefix`` is A, B or C."""

    def __init__(self, prefix, message):
        self.prefix = prefix
        self.message = message

    def __str__(self):
        return "%s: %s" % (self.prefix, self.message)


class GuardError(Exception):
    """Raised when the guard cannot run at all (exit code 2)."""


# ---------------------------------------------------------------------------
# 3. Module loading
# ---------------------------------------------------------------------------


def load_config_module(root, module_relpath=PATH_CONFIG_MODULE):
    """Import the pipeline configuration module from ``root`` by file path.

    Returns the loaded module object.  Raises GuardError if it cannot be
    found, cannot be imported, or does not expose the expected callables.
    """
    import importlib.util

    path = os.path.join(root, module_relpath)
    if not os.path.isfile(path):
        raise GuardError("configuration module not found: %s" % path)

    spec = importlib.util.spec_from_file_location(
        "af_pipeline_config__guard_load", path
    )
    if spec is None or spec.loader is None:
        raise GuardError("cannot build an import spec for %s" % path)

    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as exc:  # noqa: BLE001 - report, do not mask
        raise GuardError(
            "importing %s raised %s: %s" % (path, type(exc).__name__, exc)
        )

    for name in ("effective_config", "config_hash", "describe"):
        if not hasattr(module, name):
            raise GuardError("%s does not define %s()" % (path, name))
        if not callable(getattr(module, name)):
            raise GuardError("%s.%s is not callable" % (path, name))

    return module


# ---------------------------------------------------------------------------
# 4. Check A -- computed hash equals the pin
# ---------------------------------------------------------------------------


def check_hash_pin(computed, expected=EXPECTED_CONFIG_HASH):
    """Compare a computed hash against the pinned expectation.

    Returns (findings, detail_dict).
    """
    findings = []
    detail = {
        "computed": computed,
        "expected": expected,
        "computed_short": computed[:SHORT_LEN],
        "expected_short": expected[:SHORT_LEN],
        "match": computed == expected,
    }

    if not re.match(r"^[0-9a-f]{64}$", computed or ""):
        findings.append(
            Finding(
                "A",
                "computed configuration hash is not 64 lowercase hex "
                "characters: %r" % (computed,),
            )
        )
        return findings, detail

    if not re.match(r"^[0-9a-f]{64}$", expected or ""):
        findings.append(
            Finding(
                "A",
                "EXPECTED_CONFIG_HASH is not 64 lowercase hex characters: "
                "%r" % (expected,),
            )
        )
        return findings, detail

    if computed != expected:
        findings.append(
            Finding(
                "A",
                "configuration hash drift. computed=%s expected=%s. "
                "The effective configuration changed. If the change was "
                "deliberate, update EXPECTED_CONFIG_HASH in "
                "Tools/af_config_hash_guard.py and every quoted value in "
                "the documentation, in the same change set."
                % (computed, expected),
            )
        )

    return findings, detail


# ---------------------------------------------------------------------------
# 5. Check B -- quoted hashes in prose agree with the computed hash
# ---------------------------------------------------------------------------


def extract_hash_claims(text):
    """Return [(hex_token, anchor_offset)] for every config-hash claim.

    A claim is a hex run of at least SHORT_LEN characters that begins within
    CLAIM_WINDOW characters after a ``config hash`` / ``config_hash`` anchor.
    """
    claims = []
    for anchor in RE_CLAIM_ANCHOR.finditer(text):
        start = anchor.end()
        window = text[start : start + CLAIM_WINDOW]
        for hexmatch in RE_HEX_RUN.finditer(window):
            claims.append((hexmatch.group(1), anchor.start()))
    return claims


def iter_scanned_files(root):
    """Yield repository-relative paths that check B should read."""
    seen = set()

    try:
        entries = sorted(os.listdir(root))
    except OSError:
        entries = []
    for name in entries:
        if name.endswith(DOC_SCAN_ROOT_SUFFIXES):
            rel = name
            if rel not in seen and os.path.isfile(os.path.join(root, rel)):
                seen.add(rel)
                yield rel

    for directory in DOC_SCAN_DIRS:
        base = os.path.join(root, directory)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames.sort()
            for filename in sorted(filenames):
                if not filename.endswith(DOC_SCAN_SUFFIXES):
                    continue
                full = os.path.join(dirpath, filename)
                rel = os.path.relpath(full, root)
                if rel not in seen:
                    seen.add(rel)
                    yield rel


def read_text(path):
    handle = open(path, "r", encoding="utf-8", errors="replace")
    try:
        return handle.read()
    finally:
        handle.close()


def check_documented_claims(root, computed, scanned_paths=None):
    """Every quoted configuration hash must be a prefix of ``computed``.

    Returns (findings, detail_dict).
    """
    findings = []
    total = 0
    historical = 0
    per_file = []

    if scanned_paths is None:
        scanned_paths = list(iter_scanned_files(root))

    for rel in scanned_paths:
        full = os.path.join(root, rel)
        try:
            text = read_text(full)
        except OSError as exc:
            findings.append(
                Finding("B", "cannot read %s: %s" % (rel, exc))
            )
            continue

        claims = extract_hash_claims(text)
        if not claims:
            continue

        per_file.append((rel, len(claims)))
        for token, offset in claims:
            total += 1
            if computed.startswith(token):
                continue
            if is_historical_claim(token):
                historical += 1
                continue
            findings.append(
                Finding(
                    "B",
                    "%s quotes configuration hash %s near offset %d, which "
                    "is not a prefix of the computed hash %s"
                    % (rel, token, offset, computed),
                )
            )

    detail = {
        "claims_total": total,
        "claims_historical": historical,
        "files_with_claims": per_file,
        "files_scanned": len(scanned_paths),
    }
    return findings, detail


# ---------------------------------------------------------------------------
# 6. Check C -- describe() emits the short hash
# ---------------------------------------------------------------------------


def check_describe_emits_hash(describe_text, computed):
    """describe() output must contain computed[:SHORT_LEN]."""
    findings = []
    short = computed[:SHORT_LEN]
    if short not in describe_text:
        findings.append(
            Finding(
                "C",
                "describe() output does not contain the first %d characters "
                "of the configuration hash (%s). The human-readable report "
                "has drifted away from config_hash()." % (SHORT_LEN, short),
            )
        )

    stray = [
        token
        for token in RE_HEX_RUN.findall(describe_text)
        if not computed.startswith(token)
    ]
    for token in stray:
        findings.append(
            Finding(
                "C",
                "describe() output contains hash-like token %s that is not "
                "a prefix of the computed hash %s" % (token, computed),
            )
        )

    return findings, {"short": short, "stray": stray}


# ---------------------------------------------------------------------------
# 7. Self-test
# ---------------------------------------------------------------------------


class _SelfTest(object):
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.methods = 0
        self.failures = []

    def check(self, label, condition):
        if condition:
            self.passed += 1
        else:
            self.failed += 1
            self.failures.append(label)

    def equal(self, label, got, want):
        self.check("%s (got %r, want %r)" % (label, got, want), got == want)

    def run(self):
        for name in sorted(dir(self)):
            if name.startswith("t_"):
                self.methods += 1
                getattr(self, name)()
        return self.failed == 0

    # -- check A ---------------------------------------------------------

    GOOD = "a" * 64
    OTHER = "b" * 64

    def t_a01_matching_hash_is_clean(self):
        findings, detail = check_hash_pin(self.GOOD, self.GOOD)
        self.equal("A matching hash yields no finding", len(findings), 0)
        self.check("A detail reports match", detail["match"] is True)

    def t_a02_drifted_hash_is_reported(self):
        findings, detail = check_hash_pin(self.GOOD, self.OTHER)
        self.equal("A drift yields one finding", len(findings), 1)
        self.equal("A drift prefix", findings[0].prefix, "A")
        self.check("A drift detail", detail["match"] is False)
        self.check(
            "A drift message names both values",
            self.GOOD in findings[0].message
            and self.OTHER in findings[0].message,
        )

    def t_a03_malformed_computed_is_reported(self):
        findings, _ = check_hash_pin("not-a-hash", self.GOOD)
        self.equal("A malformed computed", len(findings), 1)
        self.check(
            "A malformed computed message",
            "not 64 lowercase hex" in findings[0].message,
        )

    def t_a04_malformed_expected_is_reported(self):
        findings, _ = check_hash_pin(self.GOOD, "ABCDEF")
        self.equal("A malformed expected", len(findings), 1)
        self.check(
            "A malformed expected names the constant",
            "EXPECTED_CONFIG_HASH" in findings[0].message,
        )

    def t_a05_uppercase_computed_rejected(self):
        findings, _ = check_hash_pin("A" * 64, self.GOOD)
        self.equal("A uppercase computed rejected", len(findings), 1)

    def t_a06_short_computed_rejected(self):
        findings, _ = check_hash_pin("a" * 63, self.GOOD)
        self.equal("A short computed rejected", len(findings), 1)

    def t_a07_real_pin_is_well_formed(self):
        self.check(
            "A shipped pin is 64 lowercase hex",
            bool(re.match(r"^[0-9a-f]{64}$", EXPECTED_CONFIG_HASH)),
        )

    # -- claim extraction ------------------------------------------------

    def t_b01_extracts_spaced_form(self):
        claims = extract_hash_claims("  config hash     : %s" % ("a" * 16))
        self.equal("B spaced form count", len(claims), 1)
        self.equal("B spaced form token", claims[0][0], "a" * 16)

    def t_b02_extracts_underscore_form(self):
        claims = extract_hash_claims("config_hash = %s" % ("c" * 64))
        self.equal("B underscore form count", len(claims), 1)
        self.equal("B underscore form token", claims[0][0], "c" * 64)

    def t_b03_extracts_hyphen_and_case(self):
        claims = extract_hash_claims("Config-Hash: %s" % ("d" * 20))
        self.equal("B hyphen form count", len(claims), 1)

    def t_b04_ignores_unanchored_hex(self):
        claims = extract_hash_claims("blob sha %s" % ("e" * 40))
        self.equal("B unanchored hex ignored", len(claims), 0)

    def t_b05_ignores_far_away_hex(self):
        text = "config hash" + (" " * (CLAIM_WINDOW + 10)) + ("f" * 16)
        self.equal(
            "B out-of-window hex ignored", len(extract_hash_claims(text)), 0
        )

    def t_b06_ignores_short_hex(self):
        self.equal(
            "B sub-16 hex ignored",
            len(extract_hash_claims("config hash abcdef0123456")),
            0,
        )

    def t_b07_multiple_claims_on_one_anchor(self):
        text = "config hash %s and %s" % ("a" * 16, "a" * 20)
        self.equal("B two tokens in window", len(extract_hash_claims(text)), 2)

    def t_b08_prefix_claim_accepted(self):
        computed = "c9ef9f7e985a1aaf" + "0" * 48
        findings, detail = check_documented_claims(
            self._fixture(
                {"Documentation/x.md": "config hash : c9ef9f7e985a1aaf"}
            ),
            computed,
        )
        self.equal("B prefix accepted", len(findings), 0)
        self.equal("B prefix counted", detail["claims_total"], 1)

    def t_b09_wrong_claim_reported(self):
        computed = "c9ef9f7e985a1aaf" + "0" * 48
        findings, detail = check_documented_claims(
            self._fixture(
                {"Documentation/x.md": "config hash : deadbeefdeadbeef"}
            ),
            computed,
        )
        self.equal("B wrong claim reported", len(findings), 1)
        self.equal("B wrong claim prefix", findings[0].prefix, "B")
        self.check(
            "B wrong claim names the file",
            "Documentation/x.md" in findings[0].message.replace(os.sep, "/"),
        )
        self.equal("B wrong claim counted", detail["claims_total"], 1)

    def t_b10_scans_yaml_and_root_markdown(self):
        computed = "abcd1234abcd1234" + "0" * 48
        root = self._fixture(
            {
                "README.md": "config hash abcd1234abcd1234",
                ".github/workflows/validate.yml": (
                    "# config hash abcd1234abcd1234\n"
                ),
                "Documentation/d.md": "config hash abcd1234abcd1234",
                "Tools/t.md": "config hash abcd1234abcd1234",
            }
        )
        findings, detail = check_documented_claims(root, computed)
        self.equal("B multi-location clean", len(findings), 0)
        self.equal("B multi-location count", detail["claims_total"], 4)

    def t_b11_ignores_unlisted_extensions(self):
        computed = "abcd1234abcd1234" + "0" * 48
        root = self._fixture(
            {"Documentation/notes.txt": "config hash deadbeefdeadbeef"}
        )
        findings, detail = check_documented_claims(root, computed)
        self.equal("B .txt not scanned", len(findings), 0)
        self.equal("B .txt not counted", detail["claims_total"], 0)

    def t_b12_zero_claims_is_not_a_failure(self):
        findings, detail = check_documented_claims(
            self._fixture({"Documentation/x.md": "no claim here"}),
            "a" * 64,
        )
        self.equal("B zero claims clean", len(findings), 0)
        self.equal("B zero claims counted", detail["claims_total"], 0)

    def t_b13_historical_claim_is_accepted(self):
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

    def t_c01_describe_containing_short_hash_is_clean(self):
        computed = "c9ef9f7e985a1aaf" + "0" * 48
        findings, _ = check_describe_emits_hash(
            "  config hash     : c9ef9f7e985a1aaf\n", computed
        )
        self.equal("C clean describe", len(findings), 0)

    def t_c02_describe_missing_short_hash_is_reported(self):
        computed = "c9ef9f7e985a1aaf" + "0" * 48
        findings, _ = check_describe_emits_hash("  config hash : ?\n", computed)
        self.equal("C missing short hash", len(findings), 1)
        self.equal("C missing prefix", findings[0].prefix, "C")

    def t_c03_describe_with_stray_hash_is_reported(self):
        computed = "c9ef9f7e985a1aaf" + "0" * 48
        text = "config hash c9ef9f7e985a1aaf\nother 1111222233334444\n"
        findings, detail = check_describe_emits_hash(text, computed)
        self.equal("C stray reported", len(findings), 1)
        self.equal("C stray recorded", detail["stray"], ["1111222233334444"])

    def t_c04_full_hash_in_describe_is_accepted(self):
        computed = "c9ef9f7e985a1aaf" + "0" * 48
        findings, _ = check_describe_emits_hash(
            "config hash %s\n" % computed, computed
        )
        self.equal("C full hash accepted", len(findings), 0)

    # -- module loading --------------------------------------------------

    def t_d01_missing_module_raises(self):
        try:
            load_config_module(self._fixture({}))
        except GuardError as exc:
            self.check("D missing module message", "not found" in str(exc))
            return
        self.check("D missing module raised", False)

    def t_d02_module_without_callables_raises(self):
        root = self._fixture({PATH_CONFIG_MODULE: "VALUE = 1\n"})
        try:
            load_config_module(root)
        except GuardError as exc:
            self.check(
                "D incomplete module message",
                "does not define" in str(exc),
            )
            return
        self.check("D incomplete module raised", False)

    def t_d03_valid_module_loads(self):
        source = (
            "def effective_config():\n"
            "    return {'a': 1}\n"
            "def config_hash():\n"
            "    return '%s'\n"
            "def describe():\n"
            "    return 'config hash %s'\n" % ("a" * 64, "a" * 16)
        )
        module = load_config_module(self._fixture({PATH_CONFIG_MODULE: source}))
        self.equal("D loaded hash", module.config_hash(), "a" * 64)

    def t_d04_raising_module_reports_exception(self):
        root = self._fixture({PATH_CONFIG_MODULE: "raise ValueError('boom')\n"})
        try:
            load_config_module(root)
        except GuardError as exc:
            self.check("D raising module message", "ValueError" in str(exc))
            return
        self.check("D raising module raised", False)

    # -- fixtures --------------------------------------------------------

    def _fixture(self, files):
        import tempfile

        root = tempfile.mkdtemp(prefix="af_cfg_hash_guard_")
        for rel, content in files.items():
            full = os.path.join(root, rel.replace("/", os.sep))
            directory = os.path.dirname(full)
            if directory and not os.path.isdir(directory):
                os.makedirs(directory)
            handle = open(full, "w", encoding="utf-8")
            try:
                handle.write(content)
            finally:
                handle.close()
        return root


def run_self_test(verbose=False):
    tester = _SelfTest()
    ok = tester.run()
    print(
        "self-test: %d cases across %d methods, %d failed"
        % (tester.passed + tester.failed, tester.methods, tester.failed)
    )
    if not ok:
        for failure in tester.failures:
            print("  FAILED  %s" % failure)
    elif verbose:
        print("  all cases passed")
    return 0 if ok else 1


# ---------------------------------------------------------------------------
# 8. Driver
# ---------------------------------------------------------------------------


def run_guard(root, verbose=False):
    module = load_config_module(root)

    computed = module.config_hash()
    findings_a, detail_a = check_hash_pin(computed)
    findings_b, detail_b = check_documented_claims(root, computed)

    describe_text = module.describe()
    if describe_text is None:
        describe_text = ""
    if not isinstance(describe_text, str):
        describe_text = str(describe_text)
    findings_c, detail_c = check_describe_emits_hash(describe_text, computed)

    findings = findings_a + findings_b + findings_c

    if verbose:
        print("root            : %s" % os.path.abspath(root))
        print("config module   : %s" % PATH_CONFIG_MODULE)
        print("computed hash   : %s" % detail_a["computed"])
        print("pinned hash     : %s" % detail_a["expected"])
        print("short (%d chars): %s" % (SHORT_LEN, detail_a["computed_short"]))
        print("A pin match     : %s" % detail_a["match"])
        print(
            "B files scanned : %d, hash claims found: %d"
            % (detail_b["files_scanned"], detail_b["claims_total"])
        )
        for rel, count in detail_b["files_with_claims"]:
            print("    %-58s %d claim(s)" % (rel, count))
        print(
            "B superseded    : %d claim(s) matched a recorded historical pin"
            % detail_b.get("claims_historical", 0)
        )
        if detail_b["claims_total"] == 0:
            print(
                "    note: no configuration hash is quoted anywhere in the "
                "scanned files"
            )
        print(
            "C describe()    : short hash present: %s, stray tokens: %d"
            % (detail_c["short"] in describe_text, len(detail_c["stray"]))
        )

    if findings:
        print("")
        print("configuration hash guard: %d finding(s)" % len(findings))
        for finding in findings:
            print("  %s" % finding)
        return 1

    print(
        "configuration hash guard: clean (hash %s, %d documented claim(s) "
        "verified)" % (detail_a["computed_short"], detail_b["claims_total"])
    )
    return 0


def main(argv):
    root = "."
    verbose = False
    self_test = False
    show_hash = False

    index = 1
    while index < len(argv):
        argument = argv[index]
        if argument == "--root":
            index += 1
            if index >= len(argv):
                print("--root requires a path", file=sys.stderr)
                return 2
            root = argv[index]
        elif argument.startswith("--root="):
            root = argument.split("=", 1)[1]
        elif argument in ("--verbose", "-v"):
            verbose = True
        elif argument == "--self-test":
            self_test = True
        elif argument == "--print-hash":
            show_hash = True
        elif argument in ("--help", "-h"):
            print(__doc__)
            return 0
        else:
            print("unknown argument: %s" % argument, file=sys.stderr)
            return 2
        index += 1

    if self_test:
        return run_self_test(verbose=verbose)

    try:
        if show_hash:
            module = load_config_module(root)
            computed = module.config_hash()
            print(computed)
            print(computed[:SHORT_LEN])
            return 0
        return run_guard(root, verbose=verbose)
    except GuardError as exc:
        print("configuration hash guard could not run: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
