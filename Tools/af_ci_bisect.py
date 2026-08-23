#!/usr/bin/env python3
# Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.
#
# af_ci_bisect.py -- TEMPORARY diagnostic harness.
#
# Purpose
# -------
# af_static_validate.py runs fifteen check groups and exits 1 if any of them
# report a failure. That is the correct design for a gate, but it makes remote
# diagnosis hard: the exit code says "something is wrong" without saying what,
# and the workflow logs are not always reachable (for example when triaging
# through an API surface that exposes check-run conclusions but not their
# output).
#
# This harness runs exactly ONE group per invocation and exits non-zero only if
# that group failed. Driven from a workflow matrix, it turns the question
# "which check is broken?" into "which check run is red?", which is answerable
# from conclusions alone.
#
# Each invocation builds a FRESH Report, so a failure in one group cannot leak
# into another group's verdict.
#
# Delete this file and .github/workflows/ci-bisect.yml once the underlying
# failure is fixed. It validates nothing on its own and must never become a
# substitute for af_static_validate.py.

from __future__ import annotations

import argparse
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import af_static_validate as validator  # noqa: E402

#: Group names map to `check_<name>` functions in af_static_validate, in the
#: same order main() runs them.
GROUPS = [
    "build_graph",
    "boundaries",
    "acyclic",
    "module_implementations",
    "uproject",
    "targets",
    "header_hygiene",
    "include_resolution",
    "originality",
    "portability",
    "vehicle_backend_isolation",
    "telemetry_literals",
    "bone_convention",
    "config",
    "tests",
]

#: These two take the parsed module dependency graph rather than the root path.
GRAPH_CONSUMERS = {"boundaries", "acyclic"}


def run_group(group: str, root: str) -> int:
    report = validator.Report(verbose=False)

    if group == "build_graph":
        validator.check_build_graph(root, report)
    elif group in GRAPH_CONSUMERS:
        # Rebuild the graph through a throwaway report so that a broken
        # build graph is attributed to the build_graph job only.
        scratch = validator.Report(verbose=False)
        deps = validator.check_build_graph(root, scratch)
        getattr(validator, "check_" + group)(deps, report)
    else:
        getattr(validator, "check_" + group)(root, report)

    failed = len(report.failures)
    print("")
    print("=" * 72)
    print("group    : %s" % group)
    print("passes   : %d" % report.passes)
    print("failures : %d" % failed)
    if report.failures:
        print("")
        print("FAILURES:")
        for item in report.failures:
            print("  - %s" % item)
    print("=" * 72)
    print("GROUP RESULT: %s" % ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


def main(argv) -> int:
    parser = argparse.ArgumentParser(
        description="Run a single af_static_validate check group (diagnostic)."
    )
    parser.add_argument("--group", required=True, choices=GROUPS)
    parser.add_argument("--root", default=".")
    args = parser.parse_args(list(argv))

    root = os.path.abspath(args.root)
    print("ApexFormula CI bisect harness (diagnostic, not a gate)")
    print("repository root: %s" % root)
    print("group          : %s" % args.group)

    if not os.path.isdir(os.path.join(root, validator.SOURCE_DIR)):
        print("ERROR: %s not found under %s" % (validator.SOURCE_DIR, root))
        return 2

    try:
        return run_group(args.group, root)
    except Exception:  # noqa: BLE001 - a raising check is itself the finding
        traceback.print_exc()
        print("")
        print("GROUP RESULT: RAISED (%s)" % args.group)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
