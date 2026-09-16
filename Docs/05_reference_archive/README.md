# Reference archive: `feature/narrrail-save-snapshots`

This directory holds the payload of `Courtshipfy/NarrRail@feature/narrrail-save-snapshots` that exists
**nowhere else**. It was created so that branch could be deleted without losing work.

The branch was deleted on 2026-09-16. These are the two independent copies that were kept first:

| Copy | Where | Covers |
|------|-------|--------|
| Git tag | `Courtshipfy/NarrRail` → `archive/narrrail-save-snapshots` (`84b90f3`, pointing at `2e3903f`) | **Everything** — the whole tree, including the reference C++ and every asset |
| This archive | `reference-branch-payload.zip` | Only what this repository lacks or differs on (27 files, 1.7 MB) |

Prefer the tag. It is complete, costs no extra objects, and needs no extraction. This archive exists so
that whoever does the Unreal work does not have to know a tag in another repository exists.

```sh
# Recover any single file from the tag, without cloning anything extra:
git -C /path/to/NarrRail show archive/narrrail-save-snapshots:NarrRailUEHost/Content/NarrRailStage/UI/WBP_Start.uasset > WBP_Start.uasset

# Or extract this archive over this repository's root; paths are repository-relative:
unzip reference-branch-payload.zip -d /path/to/NarrRail-Unreal-Plugin
```

`MANIFEST.md` lists every archived file with its byte size, git blob sha, SHA-256, and the reason it was
archived. Verify against the **SHA-256** — a git blob sha is only reproducible while the source repository
still holds the object.

## What is here, and what it is for

**Save slot UI — 5 files, ~631 KB, previously blocking T018.** `UI/Enum_SaveMode.uasset`,
`UI/WBP_Start.uasset`, `UI/WBP_TextLine.uasset`, `UI/SaveGame/WBP_SaveGame.uasset`,
`UI/SaveGame/WBP_SaveSlot.uasset`. These are the save slot surface. No copy existed in this repository,
and their absence is why the branch could not be deleted earlier.

**Conflicting assets — 6 files.** `BP_StoryController`, `BP_StoryGameMode`, `ADV/WBP_Dialogue_ADV`,
`NVL/WBP_Dialogue_NVL`, `NVL/WBP_ButtonLine`, `NVL/WBP_TextLine`. Both repositories have a version and
they differ. The branch side is the one carrying the save-slot wiring. T026 is the reconciliation.

**Unreal remote tooling — 4 files.** `NarrRailUEHost/Content/Python/init_unreal.py` and
`narrrail_remote_server.py`, plus `Tools/dump_widgets.py` and `Tools/unreal_remote.py`. These were written
on the branch, were never migrated, and exist in no other tree. They drive the editor over Unreal's remote
Python interface, which is likely how the widget work on the branch was done at all.

**Reference implementation — 5 files.** The branch's versions of `NarrRailStorySession.{h,cpp}`,
`NarrRailGlobalStateSubsystem.{h,cpp}` and `NarrRailSaveSnapshotTests.cpp`. The ported code in this
repository deliberately deviates from these in three places; the deviations are recorded in
`specs/0001-save-load-snapshots/data-model.md`, and these files are what the deviations are deviations
*from*. They are here so that reasoning can be re-checked rather than taken on trust.

Also: `NarrRailUEHost/NarrRailUEHost.uproject`, `NarrRailHostSaveGame.h`, `NarrRailPlayerController.{h,cpp}`,
and `Docs/03_ui_blueprint/BLUEPRINT_QUICKSTART.md`.

## What is deliberately NOT here

The archive is scoped to paths the branch actually changed since its merge base (`70fcdb0`). A plain
two-tree comparison was tried first and archived **95 files / 4 MB** — about 85 of them noise. Two reasons,
both worth knowing before re-running any comparison:

- **`NarrRailEditor/` looks heavily changed, but the branch never touched it.** Verify:
  `git -C /path/to/NarrRail diff --name-only 70fcdb0 feature/narrrail-save-snapshots -- NarrRailEditor/`
  returns nothing. Every editor difference is `main`'s later `refactor/canvas-graph-renderer` work
  (PR #31/#32). The branch just holds superseded pre-refactor copies, including the `*.svelte` node
  components that `main` replaced with Vue Canvas rendering.
- **`Docs/06_planning/TASK_PLAN.md` existed on the branch but was deleted from `main`** in `5fbdd4c`
  ("Remove Unreal consumer code from main repo"). Deliberate removal, not branch work.

A branch forked months ago differs from `main` in both directions. Only the merge base tells you which
direction any given difference runs in.

## Note on the story assets

The branch **renamed** `傷物語.uasset` → `伤物语.uasset` and `撫物語.uasset` → `抚物语.uasset` (git reports
these at 95–96% similarity) and **deleted** `君の知らない物語.uasset`. This repository kept the original
traditional names. Both archived copies are the branch's renamed versions; the rename is recorded rather
than treated as lost content. `MANIFEST.md` lists the deletion.

## Still open after the archive

Archiving removes the **data-loss** reason to keep the branch. It does not close the **work**. User Story 2
acceptance scenario 4 — "Given the save slot UI is open, When the player selects a saved slot, Then loading
resumes the story in that slot" — remains unimplemented in this repository, and T018/T019/T026/T027/T028 all
still require a machine with Unreal Engine 5.7. Nothing in this feature has been compiled. See
`specs/0001-save-load-snapshots/quickstart.md` §7 and the "Known residual" section of `tasks.md`.
