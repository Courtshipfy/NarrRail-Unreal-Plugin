# Quickstart: verifying runtime save/load snapshots

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

Constitution principle V requires that behaviour only observable inside Unreal is verified in a real
Unreal session, and that no verification is claimed that was not performed. This file records the
steps to run, so that whoever closes the issue can state exactly what was checked.

## Prerequisites

- Unreal Engine `5.7` installed, matching the compatibility claim in `README.md`.
- The sample host project `NarrRailUEHost/NarrRailUEHost.uproject` opens and builds.
- The repository has no `Binaries/`, `Intermediate/`, or `Saved/` content tracked; these are ignored
  by `.gitignore` and are regenerated locally.
- A story asset is available from the sample content, for example a `MultiDialogue` containing node
  and a `Choice` node with at least three options.

## 1. Automated round trip

Run the runtime automation tests and confirm the save snapshot tests execute and pass.

### How the test file is picked up (established during T003)

No build change is required to add the test file. The details, so this is not re-investigated:

- UBT compiles every source file under `Source/<Module>/Private/`, subdirectories included. A file at
  `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` is therefore compiled with no
  entry in `NarrRail.Build.cs`. (The `Private\Tests` convention is the engine's, not a build rule.)
- `Misc/AutomationTest.h` lives in `Core`, which the `NarrRail` module already declares as a public
  dependency. No new module dependency is needed for `IMPLEMENT_SIMPLE_AUTOMATION_TEST`.
- The test body is wrapped in `#if WITH_DEV_AUTOMATION_TESTS`, which is `1` in Editor/Development/Test
  and `0` in Shipping. The tests therefore compile out of shipping builds with no extra guard, which
  is why the file can live in a `Runtime`-type module.
- The `NarrRail` module is `"Type": "Runtime"` in `NarrRail.uplugin`. That is fine here because the
  tests use no `WITH_EDITOR`-only APIs — they build their fixtures from `NewObject`,
  `CreatePackage`, and `GetTransientPackage`. If a future test needs editor-only types, it belongs in
  `NarrRailEditor`, not here.
- `NarrRail/Source/NarrRail/Private/Tests/` **does not exist yet**. This feature creates it; it is the
  first automation test in this repository.

### Command

Editor GUI, no command line needed: **Tools → Test Automation → Automation** tab, filter on
`NarrRail.Save`.

Headless, from the engine's `Engine/Binaries/Mac/` directory (UE 5.7 documented flags):

```sh
UnrealEditor-Cmd \
  /path/to/NarrRailUEHost/NarrRailUEHost.uproject \
  -ExecCmds="Automation RunTest NarrRail.Save;Quit" \
  -unattended -nopause -nullrhi \
  -ReportExportPath="/path/to/NarrRailUEHost/Saved/AutomationReports"
```

Notes on the flags, all checked against the UE 5.7 documentation rather than memory:

- The 5.7 form is `Automation RunTest` (singular). Older community posts use `RunTests`; both are
  accepted, but the docs for the version this repo targets say `RunTest`.
- `NarrRail.Save` is a dotted **prefix**, so it selects every test whose pretty name starts with it —
  currently `NarrRail.Save.SessionSnapshot.*`. To select one case, name it in full.
- `-ReportExportPath` writes JSON plus an HTML report. Older engines used `-ReportOutputPath`; 5.7
  documents `-ReportExportPath`.
- `;Quit` exits after the run. `-testexit="Automation Test Queue Empty"` is the alternative and is
  what CI setups tend to use.
- A test needs **both** a context flag and a filter flag to be registered at all. These tests declare
  `EditorContext | EngineFilter`; drop either and the test silently never appears in the run.

### Expected result

Capture, restore, rejection, gate, and no-mutation-on-save cases all pass. If the framework reports
zero tests matching `NarrRail.Save`, the test file is not being compiled and the build configuration
must be fixed before any manual verification is meaningful.

The suite consists of eleven tests, all under `NarrRail.Save.`, so the single filter above selects
them all:

| Test | Covers |
|------|--------|
| `SessionSnapshot.PlainNodeRoundTrip` | T010, FR-001/FR-002 |
| `SessionSnapshot.MultiDialogueLineRoundTrip` | T011 |
| `SessionSnapshot.ConsumedChoiceRoundTrip` | T012 |
| `SessionSnapshot.CaptureDoesNotMutate` | T013, FR-006/SC-002 |
| `SessionSnapshot.ResumeAdvancesIdentically` | T014 |
| `SessionSnapshot.RejectUnsupportedVersion` | T020-T022, FR-003/FR-010 |
| `SessionSnapshot.RejectMissingRequiredField` | T022 |
| `SessionSnapshot.RejectUnresolvableNode` | T023 |
| `SessionSnapshot.ConsistencyGate` | T032, FR-009 |
| `GlobalStateSnapshot.RestoresPresetSpeakersAndValues` | T009 |
| `GlobalStateSnapshot.RejectUnsupportedVersion` | T009, FR-010 |

To run one case by name, pass its full dotted name, e.g.
`-ExecCmds="Automation RunTest NarrRail.Save.SessionSnapshot.ConsistencyGate;Quit"`.

### ⚠️ This cannot be run in the environment used for the port

The port was performed on a machine with **no Unreal Engine installed** — only the Epic Games
Launcher's staging area exists (`/Users/Shared/UnrealEngine/Launcher`), and the sole engine-association
directory under `~/Library/Application Support/Epic/UnrealEngine/` is a `5.5` folder holding Launcher
config and crash records, not an engine. There is no `UnrealEditor` or `UnrealEditor-Cmd` binary
anywhere on the machine, and no `UE_5.7` install directory.

Consequence, recorded here so it is not glossed over at closing time: **steps 1 through 6 of this file
are unverified by the port.** The test code will be written to the conventions above and reviewed, but
"the suite passed" cannot be claimed. Steps 1-6 must be run by someone on a machine with UE 5.7, and
the reporting section below must say so.

## 2. Manual: save and resume at a plain node

1. Open the host project, start PIE, and advance the story to a node with at least one variable
   modification already applied.
2. Record the current node and the current value of each modified variable.
3. Save to a slot.
4. Stop PIE entirely.
5. Start PIE again, load the slot.
6. Confirm the session sits on the recorded node and every recorded variable has the recorded value.
7. Advance once and confirm the next node is the one the uninterrupted session would have reached.

## 3. Manual: resume mid-`MultiDialogue`

1. Advance into a `MultiDialogue` node and step forward at least two lines.
2. Record the visible line.
3. Save, stop PIE, restart, load.
4. Confirm the visible line matches the recorded one and is not the node's first line.
5. Confirm advancing continues from that line rather than repeating earlier ones.

## 4. Manual: resume with partially consumed `Choice` options

1. Reach a `Choice` node with at least three options where options are consumed as they are picked.
2. Pick one option and let the story return to the same `Choice` node.
3. Confirm the picked option is no longer offered.
4. Save, stop PIE, restart, load.
5. Confirm the picked option is still not offered and the remaining options still are.

## 5. Manual: load failures leave play intact

1. With a live session, attempt to load a slot name that does not exist.
2. Confirm an explicit failure is reported and the session keeps playing from where it was.
3. Repeat with a snapshot deliberately edited to declare an unsupported version.
4. Confirm the same: explicit failure, no state change, no partial resume.

## 6. Manual: save does not disturb play

1. Play to a node, save, and continue without reloading.
2. Confirm the story behaves identically to a run in which no save happened.

## 7. In-editor asset reconciliation

Six binary assets differ between the reference branch and this repository and the difference cannot
be attributed from a diff. For each one, open both versions in the editor and decide which to keep:

- `NarrRailUEHost/Content/NarrRailStage/BP_StoryController.uasset`
- `NarrRailUEHost/Content/NarrRailStage/BP_StoryGameMode.uasset`
- `NarrRail/Content/UI/ADV/WBP_Dialogue_ADV.uasset`
- `NarrRail/Content/UI/NVL/WBP_Dialogue_NVL.uasset`
- `NarrRail/Content/UI/NVL/WBP_ButtonLine.uasset`
- `NarrRail/Content/UI/NVL/WBP_TextLine.uasset`

None of these may be resolved by copying a file over the other. Record the decision for each in the
pull request description.

### ⚠️ Five further assets exist ONLY on the reference branch

These do not exist in this repository in any form — not as a conflict, not as an older copy. They are
the save slot UI, and **the reference branch is their only copy**:

| Asset | Size |
|-------|------|
| `NarrRailUEHost/Content/NarrRailStage/UI/Enum_SaveMode.uasset` | 2,203 B |
| `NarrRailUEHost/Content/NarrRailStage/UI/WBP_Start.uasset` | 337,906 B |
| `NarrRailUEHost/Content/NarrRailStage/UI/WBP_TextLine.uasset` | 56,251 B |
| `NarrRailUEHost/Content/NarrRailStage/UI/SaveGame/WBP_SaveGame.uasset` | 104,456 B |
| `NarrRailUEHost/Content/NarrRailStage/UI/SaveGame/WBP_SaveSlot.uasset` | 130,664 B |

Consequences:

1. These are binary assets. They must be **authored or reconciled in the editor**, never copied across
   repositories and never text-patched — the same rule as the six assets above, and for the same
   reason: a difference between two same-named `.uasset` files cannot be attributed from a diff.
2. **The branch was deleted on 2026-09-16, after archiving.** Two independent copies were made first, and
   neither of them is this repository's working tree:

   - Tag `archive/narrrail-save-snapshots` in `Courtshipfy/NarrRail` (annotated tag `84b90f3` →
     `2e3903f8a42711f54e9aa279135239387beab799`). Complete — the whole tree, reference C++ included.
   - `Docs/05_reference_archive/reference-branch-payload.zip` in this repository: 27 files, 1.7 MB, with a
     per-file SHA-256 manifest in `MANIFEST.md`. Contents: the 5 UI assets, the 6 conflicting assets, the
     4 unmigrated Unreal remote-tooling scripts, the reference C++, and 2 renamed story assets.

   Prefer the tag. Verify against `MANIFEST.md`'s SHA-256 rather than its git blob shas — a blob sha is
   only reproducible while the source repository still holds the object.

**Archiving removed the data-loss reason to keep the branch. It did not close the work.** T018, T019, T026,
T027 and T028 are all still open, and User Story 2 acceptance scenario 4 — "Given the save slot UI is open,
When the player selects a saved slot, Then loading resumes the story in that slot" — is **not satisfied** in
this repository. See `tasks.md`, "Known residual: the branch was deleted with work still open".

### Audited and cleared — looks branch-only, is not

The check above ("present on the branch, absent here") was originally done as a path-set difference, which
over-reports: a file can be absent under one name and present under another, or absent and worthless. Each
remaining hit was then audited individually. These are **not** deletion risks:

- `.../NarrRailEditor_TestRepo/Stories/伤物语.uasset` (15,823 B) and `.../抚物语.uasset` (27,970 B). The
  **branch** renamed these from `傷物語.uasset` / `撫物語.uasset` (git reports 95% and 96% similarity) and
  deleted `君の知らない物語.uasset`. This repository kept the original names and still has them, at
  identical byte sizes but different blob hashes — a UE asset embeds its own asset name, so the rename
  alone changes the hash. Content is preserved under the original names. Both branch-side copies are in
  the archive, and the deletion is listed in its manifest.
  **Correction:** an earlier version of this section described these as having been renamed *during the
  migration*. That had the direction backwards — `git diff 70fcdb0 feature/narrrail-save-snapshots` shows
  the branch as the side that renamed them. The conclusion (not a deletion risk) is unchanged, but the
  explanation was wrong and is corrected here rather than quietly dropped.
- `NarrRailUEHost/Source/NarrRailHost/NarrRailHostTest.h` (228 B) and `.cpp` (196 B). An empty class with a
  default constructor and destructor, added by `5c030cc 示例项目初步重构`, never modified since, and
  referenced by no other file in either repository. Dead scaffolding; its absence here is correct, not a gap.

### Method — and the mistake that had to be fixed twice

For deletion safety the question is **"what does the branch hold that survives nowhere else?"**, and three
traps each produced a wrong answer before the fourth attempt held:

1. **Path sets are not enough** — a file absent under one name may exist under another.
2. **Do not diff against the source repository's `main`.** It no longer carries any Unreal content, so
   everything under `NarrRail/` and `NarrRailUEHost/` reads as "added" and the diff says nothing.
3. **Do not stop at a two-tree hash comparison.** Comparing `feature/narrrail-save-snapshots` against this
   repository's `main` still over-reported by a factor of four: **95 files / 4 MB**, of which about 85 were
   pure noise. A branch forked months ago differs from `main` in *both* directions, and a two-tree
   difference cannot say which direction any given file runs in.

The correct scope is **paths the branch changed since its merge base**, which is `70fcdb0` (2026-06-16);
verify it with `git merge-base main feature/narrrail-save-snapshots` rather than assuming it. That reduces
the payload to 27 files / 1.7 MB.

Two examples of the noise that scoping removes, both of which look alarming as raw differences:

- **`NarrRailEditor/` appears heavily changed, but the branch never touched it.**
  `git diff --name-only 70fcdb0 feature/narrrail-save-snapshots -- NarrRailEditor/` returns nothing. Every
  editor difference is `main`'s later `refactor/canvas-graph-renderer` work (PR #31/#32) — including the
  `*.svelte` node components that `main` replaced with Vue Canvas rendering, and which therefore exist only
  on the branch merely because the branch is older.
- **`Docs/06_planning/TASK_PLAN.md`** is on the branch and absent from `main`, but `main` removed it
  deliberately in `5fbdd4c` ("Remove Unreal consumer code from main repo").

Tooling, if this audit is ever re-run: `archive_branch_payload.py` in the `spec-kit-cross-repo-port` skill
takes `--scope-since <merge-base>` and produces the zip plus the SHA-256 manifest. Also note two Git traps —
`core.quotePath` is on by default, so non-ASCII paths arrive octal-escaped; and a trailing quote left on a
decoded path makes `cat-file -s` fail silently and report every such file as 0 B. Use `git ls-tree -r -z
--long` and both disappear.

## Reporting

When closing the issue, state:

- Which of the steps above were performed, and the result of each.
- Which steps were not performed, and why.
- The outcome of each asset reconciliation decision.
