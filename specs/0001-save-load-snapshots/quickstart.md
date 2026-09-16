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

Expected: capture, restore, rejection, and no-mutation-on-save cases all pass. If the automation
framework reports zero tests for `NarrRailSaveSnapshotTests`, the module is not picking up the test
file and the build configuration must be fixed before any manual verification is meaningful.

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
