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

## Reporting

When closing the issue, state:

- Which of the steps above were performed, and the result of each.
- Which steps were not performed, and why.
- The outcome of each asset reconciliation decision.
