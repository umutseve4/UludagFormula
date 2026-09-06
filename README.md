<h1 align="center">Uludağ Formula</h1>

<p align="center">
  An original 3D formula racing game, built so that nothing is claimed until a machine proves it.<br>
  Unreal Engine 5.8 + Blender 5.2 LTS, with standalone validators that check the two sides against each other<br>
  — and a decision log that records every rejected option and every defect the checks caught.
</p>

<p align="center">
  <a href="https://github.com/umutseve4/UludagFormula/actions/workflows/static-validation.yml"><img src="https://github.com/umutseve4/UludagFormula/actions/workflows/static-validation.yml/badge.svg" alt="Static validation"></a>
  <a href="https://github.com/umutseve4/UludagFormula/actions/workflows/validate.yml"><img src="https://github.com/umutseve4/UludagFormula/actions/workflows/validate.yml/badge.svg" alt="Validate"></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/automation%20tests-37%2F37-FF4D4F?style=flat-square" alt="37/37 tests">
  <img src="https://img.shields.io/badge/pre--export%20checks-19%20passed%2C%200%20failed-FF4D4F?style=flat-square" alt="19 passed 0 failed">
  <img src="https://img.shields.io/badge/decision%20records-D--001%E2%80%A6D--098-FF4D4F?style=flat-square" alt="D-001 to D-098">
</p>

---

## Where the project actually stands

- **Blender pipeline:** executed and green in CI on every push.
- **Milestone 1 (Unreal foundation):** **accepted 2026-08-14** (D-076/D-077) — compiled clean on the developer's machine, editor opens with no module load errors, **37/37** automation tests pass.
- **Vehicle content:** placeholder FBX imported, skeleton contract verified in-editor (D-079/D-080).
- **Milestone 5 (visual + physics integration):** in execution. M5.2, M5.3 and M5.4a–M5.4e are closed (D-087, D-091, D-093–D-097).
- **D-098 drive-test acceptance:** **open**. The m56s v4 PASS run (2026-08-17) measured and corrected a chassis collision-box defect (actor-space bottom **−46.7 cm → +2.4 cm**), but causality for the XY=0 driving failure and formal driving acceptance remain unverified pending `m56m_drive_test_v2`, which is **blocked: the development machine is currently unreachable**.

**No closure is claimed for M5.1, M5.4 overall, or Milestone 5 overall. No milestone beyond 1 is claimed complete.**

## What the checks caught

Three defects that no amount of code review had found:

| Check | Defect it found |
|---|---|
| `af_mesh_quality.py` | Every face produced by the box generator was wound **inward** — signed volume **−1.0**. The generator was fixed, not the test (D-047). |
| Headless Blender CI job | The generated halo arc reached **0.97415 m** against a design envelope of **0.950 m** ± **0.010 m** — a **24 mm** breach that only exists once geometry is actually built. The apex is now solved downward from the envelope and lands at **0.940 m** (D-040). |
| `af_validate_interfaces.py` | D-035, a real `override` return-type mismatch sitting in `main` that the first validator was structurally unable to detect (D-037). |

## Run the validators

```bash
python3 Tools/af_static_validate.py     --root .
python3 Tools/af_validate_interfaces.py --self-test
python3 Tools/af_validate_interfaces.py --root .
python3 Tools/af_mesh_quality.py        --self-test
python3 Tools/af_config_hash_guard.py   --self-test
```

Standard library only; none of them needs an engine. All run in CI on Python 3.9
and 3.12 on every push.

House rule: **every checker's `--self-test` runs before the real check**, so a
checker that has stopped working fails the build instead of reporting a green tree
(D-037). Mutation-suite sizes: `af_static_validate.py` **11 of 11** injected
defects detected (D-030); `af_validate_interfaces.py` **9** cases;
`af_mesh_quality.py` **46** cases across **13** check families;
`af_config_hash_guard.py` **44** cases (D-046).

The Blender job:

```bash
blender --background --factory-startup --python BlenderPipeline/scripts/af_smoke_test.py
```

All seven stages pass — scene setup, geometry generation, rig, materials,
pre-export validation, export, post-export validation — and the pre-export
validator reports **19 passed, 0 failed, 1 skipped of 21 checks** (the skip is
permanent and documented). CI resolves the newest `5.2.x` build from
`download.blender.org` by directory listing rather than pinning a patch number,
installs the GL libraries a headless Blender still links against, and uploads
`BlenderPipeline/reports/` as an artifact whether the run passes or fails. The
harness exits 0 on success, 1 on validation failure, 2 if `bpy` is unavailable,
3 if a stage raised.

## Verification labels

Every claim in this repository carries one of these labels, used literally, not
decoratively:

`statically inspected` · `automatically validated` · `requires Blender execution` ·
`requires Unreal Editor verification` · `requires local compilation` ·
`requires visual inspection` · `requires playtesting`

Each document ends with a Verification Ledger applying these labels to its own
claims. Since the Milestone 2 merge, `requires Blender execution` is no longer an
open label for the pipeline scripts — Blender runs them in CI.

## Repository layout

```
Documentation/     Design documents, decision log, version matrix
BlenderPipeline/   Blender-side authoring, validation and export scripts + UE Python tools
Unreal/            The Unreal Engine 5.8 project and its six C++ modules  (D-027)
Unreal/Content/    Imported vehicle assets (.uasset/.umap), tracked with Git LFS
Tools/             Standalone tooling that needs neither Blender nor Unreal
```

| Area | Choice |
| --- | --- |
| Engine | Unreal Engine 5.8 |
| DCC | Blender 5.2 LTS |
| Platform | Windows |
| Gameplay architecture | C++ |
| Asset assignment, tuning, animation, UI, level config | Blueprints |
| Procedural generation and validation | Blender Python |
| Primary interchange format | FBX |
| Optional preview format | GLB (never an import path) |
| Version control | Git with Git LFS |

<details>
<summary><b>Milestone progress table</b></summary>

| Milestone | State | Output |
| --- | --- | --- |
| **0A — Technical foundation** | Complete | `Documentation/` — design documents |
| **0B — Blender pipeline foundation** | Complete, **executed and green in CI** | `BlenderPipeline/` — nine `af_*.py` scripts |
| **1 — Unreal project foundation** | **Accepted 2026-08-14** (D-076/D-077): `requires local compilation` — performed; `requires Unreal Editor verification` — performed; 37/37 automation tests `automatically validated` | `Unreal/` — six C++ modules; `Tools/af_static_validate.py` |
| **2 — Vehicle implementation** | Authored and merged; 2 of 4 acceptance criteria met (D-079/D-080); the two driving criteria are gated on D-098 | Vehicle/pawn/controller implementations, 37 automation tests, `Tools/af_validate_interfaces.py` |
| **3 — Circuit and lap rules** | Partial | `Tools/af_circuit_generate.py`, `Tools/af_lap_rules_model.py` |
| **4 — Quality gates and rename** | In progress | `Tools/af_mesh_quality.py`, `Tools/af_config_hash_guard.py`, wave 1 of D-048 |
| **5 — Visual + physics integration** | In execution — M5.2, M5.3 and M5.4a–M5.4e closed (D-087, D-091, D-093–D-097); D-098 open and blocked pending `m56m_drive_test_v2` (development machine unreachable); M5.6/`m56w` unexecuted, separately gated on a D-098 PASS | `Unreal/Content/` vehicle assets (Git LFS; the m56s-corrected PhysicsAsset `.uasset` commit is pending), `BlenderPipeline/tools/` UE scripts, `Documentation/UE_SCRIPT_INVENTORY.md` |

The Milestone 1 row previously read "Complete, never compiled": every file was
authored and the static validator passed, but nothing had been compiled and no
editor had been opened. That stopped being true on 2026-08-14, when the project
compiled clean after a 9-file UE 5.8 API fix (`a5ca90c`), the editor opened
`Unreal/ApexFormula.uproject` with no module load errors, and all 37 automation
tests ran green (D-076/D-077). The row was corrected openly rather than silently,
per the project's status-correction convention.

**Milestone 2 acceptance criteria:**

1. The vehicle accelerates, brakes and steers — **not met — not demonstrated**, `requires playtesting`. Diagnostic runs under D-098 do not constitute formal acceptance; gated on `m56m_drive_test_v2` (blocked — development machine unreachable).
2. It does not fall through the ground, oscillate or invert at rest — **not met — not demonstrated**, `requires playtesting`, same gate.
3. All engine vehicle access goes through `UAFVehicleCompatibilityLayer` — **met**, `automatically validated`.
4. Imported skeleton bone names match `UAFBoneNameMap` — **met**, verified 2026-08-14 (D-079/D-080), `requires Unreal Editor verification` — performed.

Criterion 4 closed on 2026-08-14. The producing side stays continuously verified
in CI (the rig stage asserts `bone_order_matches_config == True`). The consuming
side was verified by importing `AF_Vehicle_Proto.fbx` into UE 5.8 via Interchange:
the Skeleton Tree read back all **11** contract bones with exact names, plus one
accepted armature root node, `AF_Armature_Proto` — D-080 records why that extra
root is inherent Interchange behaviour and how acceptance was amended for it
(**12** nodes: accepted root + 11 contract bones).

</details>

<details>
<summary><b>Naming — three names, and why they are not interchangeable</b></summary>

**Rename in progress (D-048):** the project was previously called *Apex Formula*.
The product name is now **Uludağ Formula**. The rename is being applied in waves
and is **not finished**. Unreal module names, C++ file names and validator scripts
still carry the old identity. The `AF_`/`af_` symbol prefixes are a separate
matter: they are **deliberately retained**.

| Form | Value | Where it is used | Status |
| --- | --- | --- | --- |
| Product name | `Uludağ Formula` | Displayed title, project description, documentation prose | **Applied** |
| Identifier form | `UludagFormula` | Repository name, `ProjectName`, `CompanyName` | **Partially applied** |
| Internal code name | `ApexFormula*` | Unreal module names, directories, `.uproject`, `.Build.cs`, `.Target.cs` | **Queued — wave 2** |
| Symbol prefix | `AF_`, `af_`, `UAF`, `AAF`, `FAF`, `IAF` | C++ types, asset prefix, bone names, Python scripts | **Retained by decision — will not move** |

The identifier form drops the breve because Unreal Build Tool requires the module
name, the directory name and the `ModuleRules` C# class name to be the same ASCII
token. The same constraint applies to FBX bone names crossing the Blender→Unreal
boundary and to shell paths in CI. The accented form is confined to display
strings and prose, which is where it is actually seen.

**Scope of the rename (D-048, option 2).** Three options were costed; option 2 was
chosen and the choice is final.

| Option | Scope | Files touched | CI risk |
| --- | --- | --- | --- |
| 1 | Display identity only | ~19 | near zero |
| **2 — chosen** | Option 1 plus the six Unreal modules, `.uproject`, both `.Target.cs`, `Config/DefaultApexFormula.ini`, the `APEXFORMULA*_API` macros, the module class names, the copyright header in 65 C++ files, and the validator's module tables | ~35 edited, 65 moved | medium |
| 3 | Option 2 plus `AF_` → `UF_` everywhere | ~114 | high |

Option 3 was rejected for four measured reasons:

1. Eleven bone names begin with `AF_` and the static validator asserts on that prefix in four separate places. The bone names are a contract crossing the Blender→Unreal boundary; the FBX and the C++ `UAFBoneNameMap` must agree exactly.
2. The `AF_CP_` checkpoint prefix is embedded in `af_circuit_generate.py` and `af_lap_rules_model.py`, whose self-test suites carry **84** and **68** cases respectively — **152** assertions would have to be re-derived.
3. Renaming `af_pipeline_config.py` breaks a hard-coded path inside the configuration digest guard and forces its pinned digest to be re-derived (D-046).
4. It is invisible to a player. Roughly **80 %** of the rename cost buys **0 %** of the user-visible benefit.

`AF_`/`af_` is therefore reclassified from "old product name" to **internal code
name** — documented, not accidental.

**Applied so far:** `Unreal/Config/DefaultGame.ini` (`ProjectName`, `CompanyName`,
`ProjectDisplayedTitle`, `Description`, `Homepage`); this README;
`Documentation/MILESTONE_4_IMPLEMENTATION.md` and
`Documentation/DECISION_LOG_VOL2.md`; the GitHub repository itself.

**Not applied yet** (each a tracked wave): product-name prose in ten remaining
`Documentation/` files, `Unreal/README.md` and `BlenderPipeline/README.md`; six
Unreal module names and directories (`ApexFormulaCore`, `ApexFormulaVehicle`,
`ApexFormulaRace`, `ApexFormulaUI`, `ApexFormulaEditor`, `ApexFormulaTests`);
`ApexFormula.uproject`, `ApexFormula.Target.cs`, `ApexFormulaEditor.Target.cs`,
`Config/DefaultApexFormula.ini`; 65 C++ files and their `APEXFORMULA*_API` macros,
`FApexFormula*Module` class names and `// Copyright ApexFormula.` header line;
the module tables inside `Tools/af_static_validate.py`, then both workflow files.

**Out of scope, staying as they are:** the `AF_`/`af_` prefixes, the eleven bone
names, the `AF_CP_` checkpoint prefix, the `UAF`/`AAF`/`FAF`/`IAF` C++ type
prefixes, the seven `Tools/af_*.py` filenames and the nine
`BlenderPipeline/scripts/af_*.py` filenames.

`Tools/af_static_validate.py` hard-codes the module dependency graph, the
`.uproject` filename, both `.Target.cs` filenames, the settings section name and
the copyright header line — **87** occurrences of the old identity in one file.
Any module rename must land **in the same commit** as the corresponding validator
change, or CI turns red. That is why this is a staged migration, not a single
sweep.

</details>

<details>
<summary><b>Document index and reading order</b></summary>

| Document | Purpose |
| --- | --- |
| `PROJECT_VISION.md` | Identity, design goals, originality rules, quality philosophy |
| `TECHNICAL_ARCHITECTURE.md` | Layers, C++ modules, components, Data Assets, telemetry |
| `VEHICLE_SYSTEM_DECISION.md` | Vehicle system evaluation, prototype and long-term decisions |
| `BLENDER_PIPELINE_DESIGN.md` | Blender→Unreal contract: units, axes, naming, validation, export |
| `DRIVER_PIPELINE_DESIGN.md` | Driver/MetaHuman workflow and its privacy constraints |
| `MILESTONE_PLAN.md` | Milestones 0A–12 with acceptance criteria and exclusions |
| `MILESTONE_2_IMPLEMENTATION.md` | What Milestone 2 added, and what it does not prove |
| `MILESTONE_3_CIRCUIT.md` | The Crescent Vale test circuit specification |
| `MILESTONE_3_IMPLEMENTATION.md` | What Milestone 3 added, and what it does not prove |
| `MILESTONE_4_IMPLEMENTATION.md` | Mesh quality gate, the configuration digest guard, the rename |
| `M56_WHEELSPIN_PLAN.md` | The M5.6 wheel-spin AnimBP execution plan, gated on a D-098 PASS |
| `UE_SCRIPT_INVENTORY.md` | Inventory of UE Python scripts under `BlenderPipeline/tools/` — delivery vs execution status |
| `VERSION_MATRIX.md` | Pinned environment and version-sensitive assumptions |
| `CI_EVIDENCE.md` | Recorded CI run identifiers and what each one proves |
| `DECISION_LOG.md` | Numbered decision records D-001 to D-044 — **frozen** |
| `DECISION_LOG_VOL2.md` … `DECISION_LOG_VOL17.md` | Records D-045 onward; a new volume opens at ~20 KB; the highest-numbered volume is live |
| `DECISION_LOG_VOL17_D098.md` | The open D-098 drive-test record — authoritative execution evidence for M5.5 |

Volume 1 reached 50 KB, at which point appending a single row meant retranscribing
the whole file and the ledger stopped being updated. Volume 1 is frozen and
authoritative for D-001 to D-044; a new volume opens whenever the current one
passes roughly 20 KB.

Suggested reading order: `PROJECT_VISION` → `TECHNICAL_ARCHITECTURE` →
`VEHICLE_SYSTEM_DECISION` → `BLENDER_PIPELINE_DESIGN` → `DRIVER_PIPELINE_DESIGN` →
`MILESTONE_PLAN` → `VERSION_MATRIX` → `DECISION_LOG` → the latest volume.

**Conventions:** asset prefix `AF_`; C++ prefixes `UAF`, `AAF`, `FAF`, `IAF`;
Blender script prefix `af_` — permanent internal code name. Metres inside Blender,
centimetres at the Unreal boundary (`CM_PER_UNIT = 100.0`). Bone names are defined
once (`af_pipeline_config.py` / `UAFBoneNameMap`) and never hardcoded. Vehicle
dimensions are defined once, in `af_pipeline_config.py::DESIGN`;
`UAFVehicleDefinition` follows it and never contradicts it (D-041).

</details>

## Limits

- **There is no playable build and no driving acceptance.** The two driving criteria of Milestone 2 are `requires playtesting` and remain **not demonstrated**.
- Work is currently **blocked** on `m56m_drive_test_v2` (PASS = XY displacement **> 300 cm** AND speed **> 100 cm/s**) because the development machine is unreachable. M5.6 wheel-spin (`m56w`) is separately gated on a D-098 PASS.
- CI proves static contracts and headless Blender execution. It does not prove engine runtime behaviour, visual correctness, frame rate, or gameplay feel.
- The rename is mid-migration: the code still says *ApexFormula* in six module names, the `.uproject`, both `.Target.cs` files and 65 C++ headers.
- **Privacy:** reference photographs and any biometric-adjacent material stay in a machine-local `LocalReference/` directory, excluded by `.gitignore` by name. No such material is committed, packaged or transmitted. See `Documentation/DRIVER_PIPELINE_DESIGN.md` §1–§2.
- **Originality:** no real motorsport branding, teams, drivers, sponsors, liveries, or exact reproductions of real cars or circuits are used.
