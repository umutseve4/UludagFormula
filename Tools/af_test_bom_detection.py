"""Behavioural proof for the BOM rule.

Asserts the property that actually matters: after the fix a BOM'd file fails
with a message that names the BOM, and does NOT fail the copyright rule.
Before the fix the reverse was true, which is what made the fault so
expensive to diagnose - the validator accused the wrong line.
"""
import importlib.util
import os
import sys
import tempfile

VALIDATOR = os.environ.get("VALIDATOR", "Tools/af_static_validate.py")

spec = importlib.util.spec_from_file_location("afv", VALIDATOR)
afv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(afv)

BODY = afv.COPYRIGHT_LINE + "\n\n#include \"Foo.h\"\n\nvoid Foo() {}\n"


def run_case(with_bom):
    root = tempfile.mkdtemp()
    d = os.path.join(root, "Unreal", "Source", "ApexFormulaRace", "Private")
    os.makedirs(d)
    enc = "utf-8-sig" if with_bom else "utf-8"
    with open(os.path.join(d, "AFBomProbe.cpp"), "w", encoding=enc) as fh:
        fh.write(BODY)
    report = afv.Report(verbose=False)
    afv.check_header_hygiene(root, report)
    return report.failures


failures = 0

clean = run_case(False)
if clean:
    print("FAIL clean file produced failures: %s" % clean)
    failures += 1
else:
    print("PASS clean UTF-8 file: no failures")

bommed = run_case(True)
bom_named = [f for f in bommed if "UTF-8 BOM" in f]
copyright_hit = [f for f in bommed if "copyright line" in f]

if len(bom_named) == 1:
    print("PASS BOM file reported under its own name: %s" % bom_named[0].strip())
else:
    print("FAIL BOM not reported by name; failures=%s" % bommed)
    failures += 1

if copyright_hit:
    print("FAIL BOM still masquerades as a copyright violation: %s" % copyright_hit)
    failures += 1
else:
    print("PASS copyright rule no longer misfires on a BOM")

sys.exit(1 if failures else 0)
