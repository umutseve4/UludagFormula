#!/usr/bin/env python3
# Copyright ApexFormula. Original work. Not affiliated with any real motorsport series.
"""
af_static_validate.py - static validation of the ApexFormula Unreal C++ tree.

WHAT THIS IS
------------
Milestone 1 produces C++ that cannot be compiled in the authoring environment:
there is no Unreal Engine, no UnrealBuildTool, no MSVC and no clang here. That
does not make the code unverifiable, it makes it verifiable at a *different*
tier. TECHNICAL_ARCHITECTURE.md section 9 names four testing tiers; this script
is tier 1, static validation, and it is the only tier that can actually run
without the engine.

So this script proves the things that are true of the *text* of the project:
the module dependency graph, the architectural boundaries, the naming rules,
the originality rules, and - most importantly - that the Unreal bone convention
and the Blender bone convention are byte-for-byte the same convention.

WHAT THIS IS NOT
----------------
This script does not compile anything. A clean pass here means "nothing is
provably wrong"; it does not mean "this compiles". Milestone 1 acceptance still
carries the labels `requires local compilation` and
`requires Unreal Editor verification`, and this script cannot discharge either.

DEPENDENCIES
------------
Standard library only. No bpy, no Unreal, no third-party packages. Python 3.9+.

USAGE
-----
    python3 Tools/af_static_validate.py                 # from the repo root
    python3 Tools/af_static_validate.py --root <path>   # explicit repo root
    python3 Tools/af_static_validate.py --verbose       # print every check

EXIT CODES
----------
    0   every check passed
    1   at least one check failed
    2   the script could not run (missing tree, unreadable file)

Status: automatically validated (this script's own results only).
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import re
import sys
from typing import Dict, List, Optional, Sequence, Set, Tuple

# ---------------------------------------------------------------------------
# 1. Result accumulation
# ---------------------------------------------------------------------------


class Report:
    """Collects check outcomes so one run reports every problem, not the first.

    A validator that stops at the first failure trains you to fix one thing,
    rerun, fix the next thing. That is slow and it hides clusters of related
    breakage. Everything here keeps going and reports at the end.
    """

    def __init__(self, verbose: bool = False) -> None:
        self.passes: int = 0
        self.failures: List[str] = []
        self.warnings: List[str] = []
        self.verbose = verbose

    def check(self, condition: bool, description: str, detail: str = "") -> bool:
        if condition:
            self.passes += 1
            if self.verbose:
                print("  PASS  %s" % description)
            return True
        message = description if not detail else "%s -- %s" % (description, detail)
        self.failures.append(message)
        print("  FAIL  %s" % message)
        return False

    def warn(self, description: str) -> None:
        self.warnings.append(description)
        print("  WARN  %s" % description)

    def section(self, title: str) -> None:
        print("")
        print("[%s]" % title)

    def summarise(self) -> int:
        print("")
        print("=" * 72)
        print("checks passed : %d" % self.passes)
        print("failures      : %d" % len(self.failures))
        print("warnings      : %d" % len(self.warnings))
        if self.failures:
            print("")
            print("FAILURES:")
            for item in self.failures:
                print("  - %s" % item)
        print("=" * 72)
        if self.failures:
            print("RESULT: FAIL")
            return 1
        print("RESULT: PASS  (static validation only - nothing was compiled)")
        return 0


# ---------------------------------------------------------------------------
# 2. Project layout constants
#
# Decision D-027 records that the prompt does not dictate an Unreal folder
# layout, so this layout is a project decision. These constants are the single
# place that decision is encoded on the validation side.
# ---------------------------------------------------------------------------

UNREAL_DIR = "Unreal"
SOURCE_DIR = os.path.join(UNREAL_DIR, "Source")
CONFIG_DIR = os.path.join(UNREAL_DIR, "Config")
UPROJECT = os.path.join(UNREAL_DIR, "ApexFormula.uproject")
PIPELINE_CONFIG = os.path.join(
    "BlenderPipeline", "scripts", "af_pipeline_config.py"
)

MODULES = [
    "ApexFormulaCore",
    "ApexFormulaVehicle",
    "ApexFormulaRace",
    "ApexFormulaUI",
    "ApexFormulaEditor",
    "ApexFormulaTests",
]

#: The authoritative dependency table, TECHNICAL_ARCHITECTURE.md section 2.
#: Only ApexFormula modules are listed; engine modules are checked separately
#: because the exact engine module names are an assumption, not a certainty.
EXPECTED_AF_DEPENDENCIES: Dict[str, Set[str]] = {
    "ApexFormulaCore": set(),
    "ApexFormulaVehicle": {"ApexFormulaCore"},
    "ApexFormulaRace": {"ApexFormulaCore"},
    "ApexFormulaUI": {"ApexFormulaCore"},
    "ApexFormulaEditor": {
        "ApexFormulaCore",
        "ApexFormulaVehicle",
        "ApexFormulaRace",
    },
    "ApexFormulaTests": {
        "ApexFormulaCore",
        "ApexFormulaVehicle",
        "ApexFormulaRace",
    },
}

#: Engine modules each ApexFormula module is required to declare. These come
#: from the same architecture table. Extra engine dependencies are allowed;
#: missing ones are a failure.
REQUIRED_ENGINE_DEPENDENCIES: Dict[str, Set[str]] = {
    "ApexFormulaCore": {"Core", "CoreUObject", "Engine"},
    "ApexFormulaVehicle": {"Core", "CoreUObject", "Engine"},
    "ApexFormulaRace": {"Core", "CoreUObject", "Engine"},
    "ApexFormulaUI": {"Core", "CoreUObject", "Engine", "UMG", "Slate"},
    "ApexFormulaEditor": {"Core", "CoreUObject", "Engine", "UnrealEd"},
    "ApexFormulaTests": {"Core", "CoreUObject", "Engine"},
}

#: The module-scoped DLL export macro each module's public headers must use.
API_MACROS = {name: name.upper() + "_API" for name in MODULES}

#: Engine vehicle API may appear only in these files. Decision D-008 makes
#: UAFVehicleCompatibilityLayer the single chokepoint; if engine vehicle types
#: leak anywhere else, swapping the physics backend stops being a local change.
VEHICLE_BACKEND_ALLOWED_FILES = {
    "AFVehicleCompatibilityLayer.h",
    "AFVehicleCompatibilityLayer.cpp",
}

#: Tokens that must never appear as engine vehicle API outside the chokepoint.
VEHICLE_BACKEND_TOKENS = [
    "ChaosVehicleMovementComponent",
    "UChaosWheeledVehicleMovementComponent",
    "ChaosWheeledVehicleMovementComponent",
    "UChaosVehicleWheel",
    "WheeledVehiclePawn",
    "ChaosVehicles/",
    "ChaosVehicleWheel",
]

#: Files exempt from the "no bare telemetry channel string literal" rule.
TELEMETRY_LITERAL_ALLOWED_FILES = {"AFTelemetryTypes.cpp"}

# The one file whose job is to *detect* absolute paths, so it must be allowed to
# spell one out. UAFDeveloperSettings::ValidateSelf tests incoming strings for a
# drive letter and for the UNC "\\" prefix; writing that guard requires writing
# the prefix. Exempting the file is safe only because check_portability also
# asserts the guard is genuinely present - see the note there.
ABSOLUTE_PATH_ALLOWED_FILES = {"AFDeveloperSettings.cpp"}

SOURCE_EXTENSIONS = (".h", ".cpp")


# ---------------------------------------------------------------------------
# 3. Small file helpers
# ---------------------------------------------------------------------------


def read_text(path: str) -> str:
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
            return handle.read(3) == b"\xef\xbb\xbf"
    except OSError:
        return False


def iter_source_files(root: str) -> List[str]:
    """Every .h/.cpp under Unreal/Source, sorted for deterministic output."""
    found: List[str] = []
    source_root = os.path.join(root, SOURCE_DIR)
    for dirpath, _dirnames, filenames in os.walk(source_root):
        for filename in sorted(filenames):
            if filename.endswith(SOURCE_EXTENSIONS):
                found.append(os.path.join(dirpath, filename))
    return sorted(found)


def strip_block_comments(text: str) -> str:
    """Remove /* */ and // comments.

    Needed because almost every rule below would otherwise trip on prose. The
    prohibition documentation, for instance, has to be able to *name* the thing
    it prohibits. Doing this with a regex is not a C++ parser and it is not
    trying to be; it removes string-literal contents from consideration too,
    which is handled separately where literals matter.
    """
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def relpath(root: str, path: str) -> str:
    return os.path.relpath(path, root).replace(os.sep, "/")


# ---------------------------------------------------------------------------
# 4. Build graph checks
# ---------------------------------------------------------------------------


def parse_build_cs(text: str) -> Tuple[Set[str], Set[str]]:
    """Return (public deps, private deps) from a .Build.cs file.

    Deliberately naive: it finds the AddRange initialiser lists and pulls the
    quoted names out. That is enough because every ApexFormula Build.cs is
    written in the same single-AddRange style, and the style itself is checked.
    """
    cleaned = strip_block_comments(text)

    def extract(kind: str) -> Set[str]:
        pattern = (
            r"%s\s*\.\s*AddRange\s*\(\s*new\s+string\s*\[\s*\]\s*\{(.*?)\}" % kind
        )
        match = re.search(pattern, cleaned, flags=re.S)
        if not match:
            return set()
        return set(re.findall(r'"([^"]+)"', match.group(1)))

    return extract("PublicDependencyModuleNames"), extract(
        "PrivateDependencyModuleNames"
    )


def check_build_graph(root: str, report: Report) -> Dict[str, Set[str]]:
    report.section("Module build graph")

    all_deps: Dict[str, Set[str]] = {}

    for module in MODULES:
        build_cs = os.path.join(root, SOURCE_DIR, module, module + ".Build.cs")
        if not report.check(
            os.path.isfile(build_cs), "%s has a Build.cs" % module, build_cs
        ):
            all_deps[module] = set()
            continue

        text = read_text(build_cs)
        public, private = parse_build_cs(text)
        combined = public | private
        all_deps[module] = combined

        # The rules class must be named after the module or UBT will not find it.
        report.check(
            re.search(r"public\s+class\s+%s\s*:\s*ModuleRules" % module, text)
            is not None,
            "%s.Build.cs declares class %s : ModuleRules" % (module, module),
        )

        # Explicit or shared PCHs. IWYU discipline is what keeps a six-module
        # tree from turning every header edit into a full rebuild.
        report.check(
            "PCHUsageMode.UseExplicitOrSharedPCHs" in text,
            "%s uses explicit or shared PCHs" % module,
        )

        # ApexFormula dependencies must match the architecture table exactly.
        af_deps = {dep for dep in combined if dep.startswith("ApexFormula")}
        expected = EXPECTED_AF_DEPENDENCIES[module]
        report.check(
            af_deps == expected,
            "%s ApexFormula dependencies match the architecture table" % module,
            "declared=%s expected=%s" % (sorted(af_deps), sorted(expected)),
        )

        # Engine dependencies: required set must be present.
        missing_engine = REQUIRED_ENGINE_DEPENDENCIES[module] - combined
        report.check(
            not missing_engine,
            "%s declares its required engine modules" % module,
            "missing=%s" % sorted(missing_engine),
        )

        # A module must never depend on itself.
        report.check(
            module not in combined,
            "%s does not depend on itself" % module,
        )

    return all_deps


def check_boundaries(all_deps: Dict[str, Set[str]], report: Report) -> None:
    report.section("Architectural boundaries")

    # The headline rule. Race is a rules and timing module; if it can see the
    # vehicle it will eventually reach into it, and then timing can no longer
    # be tested without spawning a car.
    report.check(
        "ApexFormulaVehicle" not in all_deps.get("ApexFormulaRace", set()),
        "ApexFormulaRace does NOT depend on ApexFormulaVehicle",
    )

    # Core is the foundation; if it depends on anything of ours it stops being
    # the foundation.
    core_af = {
        dep
        for dep in all_deps.get("ApexFormulaCore", set())
        if dep.startswith("ApexFormula")
    }
    report.check(
        not core_af,
        "ApexFormulaCore depends on no ApexFormula module",
        "found=%s" % sorted(core_af),
    )

    # UI reads; it must not be able to reach the vehicle or the race directly.
    ui_deps = all_deps.get("ApexFormulaUI", set())
    report.check(
        "ApexFormulaVehicle" not in ui_deps and "ApexFormulaRace" not in ui_deps,
        "ApexFormulaUI depends on Core only, not Vehicle or Race",
        "found=%s" % sorted(d for d in ui_deps if d.startswith("ApexFormula")),
    )

    # Editor-only modules must never be depended upon by a runtime module.
    for module in MODULES:
        if module == "ApexFormulaEditor":
            continue
        report.check(
            "ApexFormulaEditor" not in all_deps.get(module, set()),
            "%s does not depend on ApexFormulaEditor" % module,
        )
        report.check(
            "UnrealEd" not in all_deps.get(module, set()),
            "%s does not depend on UnrealEd" % module,
        )

    # Nothing may depend on the test module.
    for module in MODULES:
        if module == "ApexFormulaTests":
            continue
        report.check(
            "ApexFormulaTests" not in all_deps.get(module, set()),
            "%s does not depend on ApexFormulaTests" % module,
        )


def check_acyclic(all_deps: Dict[str, Set[str]], report: Report) -> None:
    report.section("Dependency graph is acyclic")

    colour: Dict[str, int] = {m: 0 for m in MODULES}  # 0 white, 1 grey, 2 black
    cycle: List[str] = []

    def visit(node: str, stack: List[str]) -> bool:
        colour[node] = 1
        stack.append(node)
        for dep in sorted(all_deps.get(node, set())):
            if dep not in colour:
                continue  # engine module, not part of our graph
            if colour[dep] == 1:
                cycle.extend(stack[stack.index(dep):] + [dep])
                return True
            if colour[dep] == 0 and visit(dep, stack):
                return True
        stack.pop()
        colour[node] = 2
        return False

    found_cycle = False
    for module in MODULES:
        if colour[module] == 0 and visit(module, []):
            found_cycle = True
            break

    report.check(
        not found_cycle,
        "module dependency graph is acyclic",
        " -> ".join(cycle) if cycle else "",
    )


# ---------------------------------------------------------------------------
# 5. Module plumbing checks
# ---------------------------------------------------------------------------


def check_module_implementations(root: str, report: Report) -> None:
    report.section("Module implementation macros")

    for module in MODULES:
        cpp = os.path.join(root, SOURCE_DIR, module, "Private", module + ".cpp")
        header = os.path.join(root, SOURCE_DIR, module, "Public", module + ".h")

        if not report.check(
            os.path.isfile(cpp), "%s has Private/%s.cpp" % (module, module)
        ):
            continue
        report.check(
            os.path.isfile(header), "%s has Public/%s.h" % (module, module)
        )

        text = read_text(cpp)
        # The name passed to IMPLEMENT_MODULE must equal the module name, or the
        # module loads under a name nothing references and every dependent
        # module fails at startup with a message that does not say why.
        matches = re.findall(
            r"IMPLEMENT_(?:PRIMARY_GAME_)?MODULE\s*\(\s*([A-Za-z_:<>]+)\s*,\s*([A-Za-z_]+)\s*\)",
            text,
        )
        report.check(
            len(matches) == 1,
            "%s.cpp has exactly one IMPLEMENT_MODULE" % module,
            "found %d" % len(matches),
        )
        if matches:
            impl_class, impl_name = matches[0]
            report.check(
                impl_name == module,
                "%s IMPLEMENT_MODULE name matches the module" % module,
                "declared '%s'" % impl_name,
            )
            report.check(
                impl_class == "F" + module + "Module",
                "%s IMPLEMENT_MODULE class is F%sModule" % (module, module),
                "declared '%s'" % impl_class,
            )


def check_uproject(root: str, report: Report) -> None:
    report.section("uproject descriptor")

    path = os.path.join(root, UPROJECT)
    if not report.check(os.path.isfile(path), "ApexFormula.uproject exists", path):
        return

    import json

    try:
        data = json.loads(read_text(path))
    except ValueError as exc:
        report.check(False, "ApexFormula.uproject is valid JSON", str(exc))
        return
    report.check(True, "ApexFormula.uproject is valid JSON")

    listed = [entry.get("Name") for entry in data.get("Modules", [])]
    report.check(
        sorted(listed) == sorted(MODULES),
        "uproject lists exactly the six modules",
        "listed=%s" % sorted(x for x in listed if x),
    )

    by_name = {entry.get("Name"): entry for entry in data.get("Modules", [])}

    # The Editor module must be typed Editor. Typed Runtime it would be cooked
    # into a shipping build and drag UnrealEd in with it, which does not link.
    editor = by_name.get("ApexFormulaEditor", {})
    report.check(
        editor.get("Type") == "Editor",
        "ApexFormulaEditor is typed Editor",
        "type=%s" % editor.get("Type"),
    )

    # The Tests module must not ship.
    tests = by_name.get("ApexFormulaTests", {})
    report.check(
        tests.get("Type") in ("DeveloperTool", "Developer", "Editor"),
        "ApexFormulaTests is a developer-only module type",
        "type=%s" % tests.get("Type"),
    )

    # Core must load before anything that depends on it.
    core = by_name.get("ApexFormulaCore", {})
    report.check(
        core.get("LoadingPhase") in ("PreDefault", "Default", "EarliestPossible"),
        "ApexFormulaCore has a sane loading phase",
        "phase=%s" % core.get("LoadingPhase"),
    )

    report.check(
        str(data.get("EngineAssociation", "")).startswith("5."),
        "uproject targets an Unreal Engine 5 association",
        "value=%s" % data.get("EngineAssociation"),
    )


def check_targets(root: str, report: Report) -> None:
    report.section("Build targets")

    for filename, target_type, expect_editor in (
        ("ApexFormula.Target.cs", "TargetType.Game", False),
        ("ApexFormulaEditor.Target.cs", "TargetType.Editor", True),
    ):
        path = os.path.join(root, SOURCE_DIR, filename)
        if not report.check(os.path.isfile(path), "%s exists" % filename, path):
            continue

        text = read_text(path)
        report.check(target_type in text, "%s declares %s" % (filename, target_type))

        module_match = re.search(
            r"ExtraModuleNames\s*\.\s*AddRange\s*\(\s*new\s+string\s*\[\s*\]\s*\{(.*?)\}",
            strip_block_comments(text),
            flags=re.S,
        )
        listed = set(re.findall(r'"([^"]+)"', module_match.group(1))) if module_match else set()

        # The game target must not pull in the editor module.
        if expect_editor:
            report.check(
                "ApexFormulaEditor" in listed,
                "%s includes ApexFormulaEditor" % filename,
            )
        else:
            report.check(
                "ApexFormulaEditor" not in listed,
                "%s does NOT include ApexFormulaEditor" % filename,
                "listed=%s" % sorted(listed),
            )

        # Every runtime module must appear in both targets.
        for required in (
            "ApexFormulaCore",
            "ApexFormulaVehicle",
            "ApexFormulaRace",
            "ApexFormulaUI",
        ):
            report.check(
                required in listed,
                "%s includes %s" % (filename, required),
            )


# ---------------------------------------------------------------------------
# 6. Header hygiene
# ---------------------------------------------------------------------------


COPYRIGHT_LINE = (
    "// Copyright ApexFormula. Original work. "
    "Not affiliated with any real motorsport series."
)


def check_header_hygiene(root: str, report: Report) -> None:
    report.section("Header and source hygiene")

    files = iter_source_files(root)
    report.check(len(files) > 0, "found C++ source files under Unreal/Source")

    for path in files:
        rel = relpath(root, path)
        text = read_text(path)
        module = rel.split("/")[2] if len(rel.split("/")) > 2 else ""

        # A BOM is reported under its own name. Without this rule the fault
        # is mute - invisible in every editor - and surfaces as whichever
        # start-of-file rule happens to run first, sending the reader hunting
        # for a copyright bug that does not exist.
        report.check(
            not has_utf8_bom(path),
            "%s has no UTF-8 BOM" % rel,
            "write UTF-8 without BOM; PowerShell Out-File and > add one",
        )

        # Attribution on every file. This is an originality requirement, not a
        # decoration: the disclaimer has to travel with any file that leaves
        # the repository on its own.
        report.check(
            text.startswith(COPYRIGHT_LINE),
            "%s starts with the ApexFormula copyright line" % rel,
        )

        if path.endswith(".h"):
            report.check("#pragma once" in text, "%s has #pragma once" % rel)

            # Public headers must export with their own module's API macro.
            if "/Public/" in rel:
                cleaned = strip_block_comments(text)
                declares_type = re.search(
                    r"\b(UCLASS|USTRUCT|UINTERFACE)\s*\(", cleaned
                )
                if declares_type:
                    expected_macro = API_MACROS.get(module, "")
                    used = set(re.findall(r"\b(APEXFORMULA[A-Z]+_API)\b", cleaned))
                    wrong = used - {expected_macro}
                    report.check(
                        not wrong,
                        "%s uses only its own module API macro" % rel,
                        "found=%s expected=%s" % (sorted(wrong), expected_macro),
                    )

            # Every reflected type needs GENERATED_BODY or UHT emits an error
            # that points at the wrong line.
            cleaned = strip_block_comments(text)
            reflected = len(
                re.findall(r"^\s*(?:UCLASS|USTRUCT|UINTERFACE)\s*\(", cleaned, re.M)
            )
            generated = len(re.findall(r"GENERATED_(?:BODY|UCLASS_BODY|IINTERFACE_BODY)", cleaned))
            if reflected:
                report.check(
                    generated >= reflected,
                    "%s has a GENERATED_BODY for every reflected type" % rel,
                    "reflected=%d generated=%d" % (reflected, generated),
                )
                # A header that declares reflected types must include its
                # .generated.h, and it must be the LAST include.
                stem = os.path.basename(path)[:-2]
                report.check(
                    '#include "%s.generated.h"' % stem in text,
                    "%s includes its .generated.h" % rel,
                )
                includes = re.findall(r'^\s*#include\s+"([^"]+)"', text, re.M)
                if includes:
                    report.check(
                        includes[-1] == "%s.generated.h" % stem,
                        "%s has .generated.h as its last include" % rel,
                        "last=%s" % includes[-1],
                    )


def check_include_resolution(root: str, report: Report) -> None:
    report.section("Include resolution")

    # Build the set of headers the project itself provides, keyed by basename.
    provided: Set[str] = set()
    for path in iter_source_files(root):
        if path.endswith(".h"):
            provided.add(os.path.basename(path))

    for path in iter_source_files(root):
        rel = relpath(root, path)
        text = read_text(path)
        for include in re.findall(r'^\s*#include\s+"([^"]+)"', text, re.M):
            base = os.path.basename(include)
            # Only our own headers are resolvable here; engine headers are not
            # present in this environment and are deliberately not checked.
            if not base.startswith("AF") and not base.startswith("ApexFormula"):
                continue
            if base.endswith(".generated.h"):
                continue
            report.check(
                base in provided,
                "%s includes a header that exists in the tree (%s)" % (rel, include),
            )


# ---------------------------------------------------------------------------
# 7. Originality and portability rules
# ---------------------------------------------------------------------------

#: Regexes for names that must never appear as identifiers. The prohibition
#: prose in the documentation is allowed to name them; source code is not.
# These are deliberately SUBSTRING matches, not \b word-boundary matches.
#
# Mutation testing exposed the reason: \bF1\b does not match "F1SeasonCount",
# because \b requires a non-word character after the "1" and "S" is a word
# character. The prohibition is that the token must never appear in any *name*,
# and an identifier is precisely where a name lives, so the word-boundary form
# was checking the one place the rule did not need checking and skipping the
# place it did. "Formula1" was likewise invisible to every pattern here: it
# contains neither "F1" nor "FormulaOne".
#
# Substring matching is only safe because the current tree contains zero
# case-insensitive occurrences of "f1", zero of "FIA", and zero of the rest;
# this was measured, not assumed. F1 stays case-sensitive so that a future
# innocent identifier such as "Buff1" is not flagged, while the realistic
# violations (F1Car, AF1Pawn, MyF1Thing) all use a capital F.
PROHIBITED_IDENTIFIER_PATTERNS = [
    (r"F1", "the token F1"),
    (r"FIA", "the token FIA"),
    (r"FormulaOne", "the token FormulaOne"),
    (r"Formula1", "the token Formula1"),
    (r"[Ff]ormula[ _-]1", "the token Formula 1"),
    (r"GrandPrix", "the token GrandPrix"),
    (r"[Gg]rand[ _-][Pp]rix", "the token Grand Prix"),
]


def check_originality(root: str, report: Report) -> None:
    report.section("Originality rules")

    for path in iter_source_files(root):
        rel = relpath(root, path)
        # Comments are stripped: prose is permitted to name a prohibited thing
        # in order to prohibit it. Code is not.
        cleaned = strip_block_comments(read_text(path))
        for pattern, label in PROHIBITED_IDENTIFIER_PATTERNS:
            hits = re.findall(pattern, cleaned)
            report.check(
                not hits,
                "%s contains no prohibited identifier (%s)" % (rel, label),
                "occurrences=%d" % len(hits),
            )


ABSOLUTE_PATH_PATTERNS = [
    (r'"[A-Za-z]:[\\/]', "a Windows drive-letter path"),
    (r'"\\\\\\\\', "a UNC network path"),
    (r'"/home/', "a Unix home path"),
    (r'"/Users/', "a macOS home path"),
    (r'"/app/', "a container-local path"),
]


def check_portability(root: str, report: Report) -> None:
    report.section("Portability rules")

    targets = iter_source_files(root)
    config_dir = os.path.join(root, CONFIG_DIR)
    if os.path.isdir(config_dir):
        for name in sorted(os.listdir(config_dir)):
            if name.endswith(".ini"):
                targets.append(os.path.join(config_dir, name))
    uproject = os.path.join(root, UPROJECT)
    if os.path.isfile(uproject):
        targets.append(uproject)

    for path in targets:
        rel = relpath(root, path)
        if os.path.basename(path) in ABSOLUTE_PATH_ALLOWED_FILES:
            continue
        text = read_text(path)
        if path.endswith(SOURCE_EXTENSIONS):
            text = strip_block_comments(text)
        for pattern, label in ABSOLUTE_PATH_PATTERNS:
            report.check(
                re.search(pattern, text) is None,
                "%s contains no absolute path (%s)" % (rel, label),
            )

    # Anti-false-pass guard. Skipping AFDeveloperSettings.cpp above is only
    # defensible while that file actually performs the rejection it is being
    # excused for. If someone deletes the guard, the exemption would silently
    # start covering a file that no longer validates anything - the exact shape
    # of the Milestone 0B bone-format bug, where an audit blind spot hid a real
    # leak behind a green result. So prove the guard exists.
    settings_cpp = os.path.join(
        root, "Unreal", "Source", "ApexFormulaCore", "Private", "AFDeveloperSettings.cpp"
    )
    report.check(
        os.path.isfile(settings_cpp),
        "the path-validation chokepoint AFDeveloperSettings.cpp exists",
    )
    if os.path.isfile(settings_cpp):
        guard_text = read_text(settings_cpp)
        report.check(
            "StartsWith(TEXT(\"\\\\\\\\\"))" in guard_text,
            "AFDeveloperSettings.cpp still rejects UNC network paths",
        )
        report.check(
            re.search(r"IsAlpha\(\)|:[\\/]|Drive", guard_text) is not None,
            "AFDeveloperSettings.cpp still rejects drive-letter paths",
        )


# ---------------------------------------------------------------------------
# 8. Decision D-008: the vehicle backend chokepoint
# ---------------------------------------------------------------------------


def check_vehicle_backend_isolation(root: str, report: Report) -> None:
    report.section("Vehicle backend isolation (decision D-008)")

    for path in iter_source_files(root):
        rel = relpath(root, path)
        name = os.path.basename(path)
        if name in VEHICLE_BACKEND_ALLOWED_FILES:
            continue
        cleaned = strip_block_comments(read_text(path))
        for token in VEHICLE_BACKEND_TOKENS:
            report.check(
                token not in cleaned,
                "%s does not reference engine vehicle API (%s)" % (rel, token),
            )

    # And the chokepoint must actually exist, or the rule above passes for the
    # wrong reason. This is the "suspect the checker" lesson from Milestone 0B:
    # an absence-based check must be paired with a presence-based one.
    layer_h = os.path.join(
        root, SOURCE_DIR, "ApexFormulaVehicle", "Public",
        "AFVehicleCompatibilityLayer.h",
    )
    layer_cpp = os.path.join(
        root, SOURCE_DIR, "ApexFormulaVehicle", "Private",
        "AFVehicleCompatibilityLayer.cpp",
    )
    report.check(os.path.isfile(layer_h), "the compatibility layer header exists")
    report.check(os.path.isfile(layer_cpp), "the compatibility layer source exists")


# ---------------------------------------------------------------------------
# 9. Telemetry channel literal containment
# ---------------------------------------------------------------------------


def load_channel_literals(root: str) -> Set[str]:
    """Channel string literals as defined in AFTelemetryTypes.cpp."""
    path = os.path.join(
        root, SOURCE_DIR, "ApexFormulaCore", "Private", "AFTelemetryTypes.cpp"
    )
    if not os.path.isfile(path):
        return set()
    text = read_text(path)
    # Channel names are dotted, lower_snake identifiers inside TEXT("...").
    return set(re.findall(r'TEXT\(\s*"([a-z0-9_]+\.[a-z0-9_]+)"\s*\)', text))


def check_telemetry_literals(root: str, report: Report) -> None:
    report.section("Telemetry channel literal containment")

    channels = load_channel_literals(root)
    report.check(
        len(channels) >= 10,
        "AFTelemetryTypes.cpp defines the telemetry channel literals",
        "found=%d" % len(channels),
    )

    for path in iter_source_files(root):
        name = os.path.basename(path)
        if name in TELEMETRY_LITERAL_ALLOWED_FILES:
            continue
        rel = relpath(root, path)
        # Strip comments first, exactly as the D-008 and originality checks do.
        # Without this, a doc comment that merely *illustrates* a channel name
        # is reported as a hard-coded literal. Prose that names a thing in order
        # to explain it is not a leak; only code that bakes the string in is.
        text = strip_block_comments(read_text(path))
        for channel in sorted(channels):
            report.check(
                '"%s"' % channel not in text,
                "%s does not hard-code telemetry channel '%s'" % (rel, channel),
            )


# ---------------------------------------------------------------------------
# 10. The bone convention - Blender and Unreal must agree exactly
#
# This is the single most valuable check in the file. The bone names exist in
# two languages, in two repositories' worth of code, and nothing at build time
# compares them. During Milestone 1 authoring, two doc comments in
# AFBoneNameMap.h already disagreed with af_pipeline_config.py; a human read
# them twice and did not notice. This function is why they were caught.
#
# The C++ builds its bone names programmatically rather than as literals, so a
# textual diff would prove nothing. Instead the ordering functions are parsed
# and *emulated* against a tiny symbol table, which reproduces the exact list
# the compiled code would produce.
# ---------------------------------------------------------------------------


def load_pipeline_config(root: str):
    """Import af_pipeline_config.py directly. It must not require bpy."""
    path = os.path.join(root, PIPELINE_CONFIG)
    if not os.path.isfile(path):
        return None
    spec = importlib.util.spec_from_file_location("af_pipeline_config", path)
    if spec is None or spec.loader is None:
        return None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract_function_body(text: str, signature_fragment: str) -> Optional[str]:
    """Return the brace-balanced body of the first function matching a fragment."""
    index = text.find(signature_fragment)
    if index < 0:
        return None
    open_index = text.find("{", index)
    if open_index < 0:
        return None
    depth = 0
    for position in range(open_index, len(text)):
        char = text[position]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[open_index + 1:position]
    return None


def build_symbol_table(text: str) -> Dict[str, object]:
    """Resolve the bone-name symbols used by the ordering functions.

    Everything is read out of AFBoneNameMap.cpp itself, so if the constructor
    changes a name, this table changes with it and the comparison stays honest.
    """
    prefix_match = re.search(
        r"AFBonePrefix\s*=\s*TEXT\(\s*\"([^\"]*)\"\s*\)", text
    )
    prefix = prefix_match.group(1) if prefix_match else ""

    def literal(field: str) -> str:
        match = re.search(
            r"%s\s*=\s*FName\(\s*TEXT\(\s*\"([^\"]+)\"\s*\)\s*\)" % field, text
        )
        return match.group(1) if match else ""

    corners_body = extract_function_body(text, "GetCornersInOrder()")
    corners = re.findall(r"EAFCorner::([A-Z]{2})", corners_body or "")
    # The first occurrence sequence inside the static array initialiser.
    seen: List[str] = []
    for corner in corners:
        if corner not in seen:
            seen.append(corner)

    wheel_fmt = re.search(r'WheelBones\.Add\([^,]+,\s*FName\(\*FString::Printf\(TEXT\("([^"]+)"\)', text)
    susp_fmt = re.search(r'SuspensionBones\.Add\([^,]+,\s*FName\(\*FString::Printf\(TEXT\("([^"]+)"\)', text)

    def apply(fmt_match, suffix: str) -> str:
        if not fmt_match:
            return ""
        fmt = fmt_match.group(1)
        # Only the two-substitution "%s<Kind>_%s" shape is supported, which is
        # the shape actually used. Anything else is reported as unsupported
        # rather than silently mis-emulated.
        parts = fmt.split("%s")
        if len(parts) != 3:
            return ""
        return prefix + parts[1] + suffix

    return {
        "prefix": prefix,
        "root": literal("RootBone"),
        "chassis": literal("ChassisBone"),
        "steering": literal("SteeringBone"),
        "corners": seen,
        "wheel": {c: apply(wheel_fmt, c) for c in seen},
        "suspension": {c: apply(susp_fmt, c) for c in seen},
    }


def emulate_ordering(body: str, symbols: Dict[str, object]) -> List[str]:
    """Interpret a sequence of Result.Add(...) calls, including corner loops.

    Supports exactly the two shapes present in AFBoneNameMap.cpp:
      Result.Add(RootBone);
      for (const EAFCorner Corner : GetCornersInOrder()) { Result.Add(...); }
    Anything else is ignored, which would show up as a length mismatch rather
    than as a false pass.
    """
    corners: List[str] = symbols["corners"]  # type: ignore[assignment]

    def resolve(expression: str, corner: Optional[str]) -> Optional[str]:
        expression = expression.strip()
        if expression == "RootBone":
            return symbols["root"]  # type: ignore[return-value]
        if expression == "ChassisBone":
            return symbols["chassis"]  # type: ignore[return-value]
        if expression == "SteeringBone":
            return symbols["steering"]  # type: ignore[return-value]
        if expression == "GetWheelBone(Corner)" and corner:
            return symbols["wheel"][corner]  # type: ignore[index]
        if expression == "GetSuspensionBone(Corner)" and corner:
            return symbols["suspension"][corner]  # type: ignore[index]
        return None

    result: List[str] = []
    position = 0
    length = len(body)

    while position < length:
        loop_match = re.compile(
            r"for\s*\(\s*const\s+EAFCorner\s+Corner\s*:\s*GetCornersInOrder\(\)\s*\)\s*\{"
        ).search(body, position)
        add_match = re.compile(r"Result\.Add\(([^;]+?)\);").search(body, position)

        if loop_match and (not add_match or loop_match.start() < add_match.start()):
            # Consume the loop block, then replay its adds once per corner.
            depth = 1
            index = loop_match.end()
            while index < length and depth:
                if body[index] == "{":
                    depth += 1
                elif body[index] == "}":
                    depth -= 1
                index += 1
            block = body[loop_match.end():index - 1]
            expressions = re.findall(r"Result\.Add\(([^;]+?)\);", block)
            for corner in corners:
                for expression in expressions:
                    value = resolve(expression, corner)
                    if value:
                        result.append(value)
            position = index
            continue

        if add_match:
            value = resolve(add_match.group(1), None)
            if value:
                result.append(value)
            position = add_match.end()
            continue

        break

    return result


def check_bone_convention(root: str, report: Report) -> None:
    report.section("Bone convention: Blender and Unreal must agree")

    config = load_pipeline_config(root)
    if not report.check(
        config is not None,
        "af_pipeline_config.py imports without bpy",
        os.path.join(root, PIPELINE_CONFIG),
    ):
        return

    cpp_path = os.path.join(
        root, SOURCE_DIR, "ApexFormulaCore", "Private", "AFBoneNameMap.cpp"
    )
    if not report.check(os.path.isfile(cpp_path), "AFBoneNameMap.cpp exists"):
        return

    text = read_text(cpp_path)
    symbols = build_symbol_table(text)

    report.check(
        symbols["prefix"] == "AF_",
        "the C++ bone prefix is AF_",
        "found=%r" % symbols["prefix"],
    )
    report.check(
        list(symbols["corners"]) == list(config.CORNERS),
        "corner order matches af_pipeline_config.CORNERS",
        "cpp=%s python=%s" % (symbols["corners"], list(config.CORNERS)),
    )
    report.check(
        symbols["root"] == config.BONE_ROOT,
        "root bone name matches", "cpp=%r python=%r" % (symbols["root"], config.BONE_ROOT),
    )
    report.check(
        symbols["chassis"] == config.BONE_CHASSIS,
        "chassis bone name matches",
        "cpp=%r python=%r" % (symbols["chassis"], config.BONE_CHASSIS),
    )
    report.check(
        symbols["steering"] == config.BONE_STEERING,
        "steering bone name matches",
        "cpp=%r python=%r" % (symbols["steering"], config.BONE_STEERING),
    )

    # --- hierarchy-interleaved order, 11 bones -----------------------------
    all_body = extract_function_body(text, "UAFBoneNameMap::GetAllBoneNamesInOrder()")
    if report.check(
        all_body is not None, "GetAllBoneNamesInOrder() body was parsed"
    ):
        emulated = emulate_ordering(all_body, symbols)
        expected = list(config.BONE_ORDER)
        report.check(
            len(emulated) == 11,
            "GetAllBoneNamesInOrder() yields 11 bones",
            "yielded=%d" % len(emulated),
        )
        report.check(
            emulated == expected,
            "GetAllBoneNamesInOrder() matches BONE_ORDER exactly (interleaved)",
            "cpp=%s\n            python=%s" % (emulated, expected),
        )

    # --- grouped deform order, 9 bones ------------------------------------
    deform_body = extract_function_body(text, "UAFBoneNameMap::GetDeformBoneNames()")
    if report.check(
        deform_body is not None, "GetDeformBoneNames() body was parsed"
    ):
        emulated = emulate_ordering(deform_body, symbols)
        expected = list(config.DEFORM_BONES)
        report.check(
            len(emulated) == 9,
            "GetDeformBoneNames() yields 9 bones",
            "yielded=%d" % len(emulated),
        )
        report.check(
            emulated == expected,
            "GetDeformBoneNames() matches DEFORM_BONES exactly (grouped)",
            "cpp=%s\n            python=%s" % (emulated, expected),
        )
        # The two orderings must genuinely differ. If a refactor ever made
        # GetDeformBoneNames() delegate to GetAllBoneNamesInOrder() minus two
        # entries, the grouped contract would be silently broken.
        report.check(
            emulated != [b for b in config.BONE_ORDER if b in config.DEFORM_BONES],
            "the deform order is grouped, not a filtered interleaved order",
        )

    # --- control bones are excluded from deformation ----------------------
    report.check(
        config.BONE_ROOT not in config.DEFORM_BONES,
        "AF_Root is a control bone and carries no weights",
    )
    report.check(
        config.BONE_STEERING not in config.DEFORM_BONES,
        "AF_Steering is a control bone and carries no weights",
    )

    # --- parent map --------------------------------------------------------
    parent_body = extract_function_body(text, "UAFBoneNameMap::GetParentBone(")
    if report.check(parent_body is not None, "GetParentBone() body was parsed"):
        emulated_parents = emulate_parent_map(parent_body, symbols)
        expected_parents = dict(config.BONE_PARENTS)
        report.check(
            emulated_parents == expected_parents,
            "GetParentBone() matches BONE_PARENTS exactly",
            "cpp=%s\n            python=%s"
            % (sorted(emulated_parents.items()), sorted(expected_parents.items())),
        )

    # --- cross-checks that hold regardless of language ---------------------
    report.check(
        len(set(config.BONE_ORDER)) == len(config.BONE_ORDER),
        "BONE_ORDER contains no duplicates",
    )
    report.check(
        set(config.BONE_ORDER) == set(config.BONE_PARENTS.keys()),
        "BONE_ORDER and BONE_PARENTS cover the same bones",
    )
    for bone in config.BONE_ORDER:
        report.check(
            bone.startswith("AF_"),
            "bone %s carries the AF_ prefix" % bone,
        )


def emulate_parent_map(body: str, symbols: Dict[str, object]) -> Dict[str, Optional[str]]:
    """Reproduce the mapping GetParentBone() implements, as a dict."""
    corners: List[str] = symbols["corners"]  # type: ignore[assignment]
    wheel: Dict[str, str] = symbols["wheel"]  # type: ignore[assignment]
    suspension: Dict[str, str] = symbols["suspension"]  # type: ignore[assignment]

    names = {
        "RootBone": symbols["root"],
        "ChassisBone": symbols["chassis"],
        "SteeringBone": symbols["steering"],
    }

    mapping: Dict[str, Optional[str]] = {}

    # The leading if-chain: if (BoneName == X) { return Y; }
    for subject, returned in re.findall(
        r"if\s*\(\s*BoneName\s*==\s*(\w+)\s*\)\s*\{\s*return\s+([A-Za-z_]+)\s*;",
        body,
    ):
        if subject not in names:
            continue
        key = names[subject]
        if returned == "NAME_None":
            mapping[key] = None
        elif returned in names:
            mapping[key] = names[returned]

    # The corner loop: suspension parents to chassis, wheel parents to its
    # own suspension. Both branches are read from the text rather than assumed.
    if re.search(
        r"if\s*\(\s*BoneName\s*==\s*GetSuspensionBone\(Corner\)\s*\)\s*\{\s*return\s+ChassisBone\s*;",
        body,
    ):
        for corner in corners:
            mapping[suspension[corner]] = names["ChassisBone"]  # type: ignore[assignment]

    if re.search(
        r"if\s*\(\s*BoneName\s*==\s*GetWheelBone\(Corner\)\s*\)\s*\{\s*return\s+GetSuspensionBone\(Corner\)\s*;",
        body,
    ):
        for corner in corners:
            mapping[wheel[corner]] = suspension[corner]

    return mapping


# ---------------------------------------------------------------------------
# 11. Configuration files
# ---------------------------------------------------------------------------


def check_config(root: str, report: Report) -> None:
    report.section("Configuration files")

    for name in (
        "DefaultEngine.ini",
        "DefaultGame.ini",
        "DefaultInput.ini",
        "DefaultApexFormula.ini",
    ):
        path = os.path.join(root, CONFIG_DIR, name)
        report.check(os.path.isfile(path), "Config/%s exists" % name, path)

    # The developer settings section name must match the UCLASS specifier, or
    # the ini is silently ignored and the defaults quietly win.
    header = os.path.join(
        root, SOURCE_DIR, "ApexFormulaCore", "Public", "AFDeveloperSettings.h"
    )
    ini = os.path.join(root, CONFIG_DIR, "DefaultApexFormula.ini")
    if os.path.isfile(header) and os.path.isfile(ini):
        header_text = read_text(header)
        config_match = re.search(r"UCLASS\s*\(([^)]*)\)", header_text, re.S)
        specifier = config_match.group(1) if config_match else ""
        report.check(
            re.search(r"Config\s*=\s*ApexFormula\b", specifier) is not None,
            "UAFDeveloperSettings declares Config = ApexFormula",
        )
        report.check(
            "DefaultConfig" in specifier,
            "UAFDeveloperSettings declares DefaultConfig",
        )
        report.check(
            "[/Script/ApexFormulaCore.AFDeveloperSettings]" in read_text(ini),
            "DefaultApexFormula.ini uses the correct settings section name",
        )

    # Every property written in the ini must exist in the header. A stale key
    # is not an error to Unreal - it is simply ignored, forever.
    if os.path.isfile(header) and os.path.isfile(ini):
        header_text = read_text(header)
        for line in read_text(ini).splitlines():
            line = line.strip()
            if not line or line.startswith((";", "[", "#")):
                continue
            if "=" not in line:
                continue
            key = line.split("=", 1)[0].strip()
            report.check(
                re.search(r"\b%s\b" % re.escape(key), header_text) is not None,
                "ini key %s exists on UAFDeveloperSettings" % key,
            )


# ---------------------------------------------------------------------------
# 12. Automation test discovery
# ---------------------------------------------------------------------------


def check_tests(root: str, report: Report) -> None:
    report.section("Automation tests")

    test_dir = os.path.join(root, SOURCE_DIR, "ApexFormulaTests", "Private")
    if not report.check(os.path.isdir(test_dir), "test source directory exists"):
        return

    total_tests = 0
    names: List[str] = []

    for filename in sorted(os.listdir(test_dir)):
        if not filename.endswith("Tests.cpp"):
            continue
        path = os.path.join(test_dir, filename)
        text = read_text(path)
        rel = relpath(root, path)

        declared = re.findall(
            r"IMPLEMENT_SIMPLE_AUTOMATION_TEST\s*\(\s*(\w+)\s*,\s*\"([^\"]+)\"\s*,\s*(\w+)\s*\)",
            text,
        )
        if not declared:
            continue

        # Test bodies must be compiled out of shipping builds.
        report.check(
            "#if WITH_DEV_AUTOMATION_TESTS" in text,
            "%s is guarded by WITH_DEV_AUTOMATION_TESTS" % rel,
        )
        report.check(
            '#include "Misc/AutomationTest.h"' in text,
            "%s includes Misc/AutomationTest.h" % rel,
        )

        for class_name, test_name, flags in declared:
            total_tests += 1
            names.append(test_name)
            report.check(
                test_name.startswith("ApexFormula."),
                "test %s is namespaced under ApexFormula." % test_name,
            )
            report.check(
                len(test_name.split(".")) >= 3,
                "test %s uses the Module.Area.Case naming shape" % test_name,
            )
            report.check(
                class_name.startswith("F"),
                "test class %s follows the F-prefix convention" % class_name,
            )
            # Every declared test must have a matching RunTest definition, or
            # the macro expands to a class nobody ever runs.
            report.check(
                "%s::RunTest" % class_name in text,
                "test class %s defines RunTest" % class_name,
            )

    report.check(total_tests > 0, "at least one automation test is declared")
    report.check(
        len(names) == len(set(names)),
        "automation test names are unique",
        "duplicates=%s" % sorted({n for n in names if names.count(n) > 1}),
    )
    print("  note  %d automation tests declared (NOT executed here)" % total_tests)


# ---------------------------------------------------------------------------
# 13. Entry point
# ---------------------------------------------------------------------------


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Static validation of the ApexFormula Unreal C++ tree."
    )
    parser.add_argument(
        "--root",
        default=None,
        help="Repository root. Defaults to the parent of this script's directory.",
    )
    parser.add_argument("--verbose", action="store_true", help="Print passing checks.")
    args = parser.parse_args(list(argv))

    root = args.root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    root = os.path.abspath(root)

    print("ApexFormula static validation")
    print("repository root: %s" % root)
    print("")
    print("This script does NOT compile anything. A pass means nothing is")
    print("provably wrong in the text of the project. Compilation and editor")
    print("verification remain outstanding.")

    if not os.path.isdir(os.path.join(root, SOURCE_DIR)):
        print("")
        print("ERROR: %s not found under %s" % (SOURCE_DIR, root))
        return 2

    report = Report(verbose=args.verbose)

    all_deps = check_build_graph(root, report)
    check_boundaries(all_deps, report)
    check_acyclic(all_deps, report)
    check_module_implementations(root, report)
    check_uproject(root, report)
    check_targets(root, report)
    check_header_hygiene(root, report)
    check_include_resolution(root, report)
    check_originality(root, report)
    check_portability(root, report)
    check_vehicle_backend_isolation(root, report)
    check_telemetry_literals(root, report)
    check_bone_convention(root, report)
    check_config(root, report)
    check_tests(root, report)

    return report.summarise()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
