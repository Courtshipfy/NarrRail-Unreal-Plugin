# Implementation Plan: Runtime save/load snapshots

**Branch**: `spec/0001-save-load-snapshots` | **Date**: 2026-09-16 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/0001-save-load-snapshots/spec.md`

**Issue**: `Courtshipfy/NarrRail-Unreal-Plugin#1`

## Summary

Add a Persistence capability to the NarrRail runtime: capture the complete resumable state of a
running story session into a versioned snapshot value, serialise it to a named save slot, and
restore it so that play continues as if it had never stopped. The work lands as C++ changes in the
`NarrRail` runtime module plus a thin host surface in `NarrRailUEHost`, with automated coverage of
the round trip.

The architecture document already reserves this capability but nothing is implemented:

> `Persistence`：存档读写、版本迁移、异常恢复
> 蓝图接口最小集: `SaveSession` / `LoadSession`
> 需求表: 存档恢复 | C++ | 规划中 | NR-RUN-006-*

The approach reuses the proven shape from the reference branch, with two deliberate strengthening
changes: an explicit rejection path for unsupported snapshot versions (FR-003, FR-008), and
detection of a story asset that no longer matches the snapshot (FR-009).

## Technical Context

**Language/Version**: C++ (Unreal Engine `5.7` toolchain; `*.Build.cs` module definitions)

**Primary Dependencies**: Unreal Engine `5.7` runtime; existing `NarrRail` runtime module
(`NarrRailStorySession`, `NarrRailGlobalStateSubsystem`, `NarrRailStoryAsset`); `NarrRailHost`
module in the sample host; UE `USaveGame` for slot serialisation

**Storage**: Unreal save slots via `UGameplayStatics` save/load slot APIs, backed by a
`USaveGame` subclass holding one serialized snapshot

**Testing**: UE automation tests under `NarrRail/Source/NarrRail/Private/Tests/`, run through the
editor automation framework

**Target Platform**: Unreal Engine `5.7` editor and PIE on the existing development workstations

**Project Type**: game-engine plugin plus sample host project (two surfaces in one repository)

**Performance Goals**: Snapshot capture and restore must complete within a single frame; snapshots
are small (one node reference plus variable maps) and are not expected to be a bottleneck

**Constraints**: No Blueprint-side state reconstruction; binary assets authored in the editor, never
text-patched; compatibility claim stays within plugin `0.1.0-beta` / UE `5.7`

**Scale/Scope**: One snapshot entity (~10 fields), two modules, five new UI assets, one new
automation test file

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Gate | Principle | Status | Note |
|------|-----------|--------|------|
| GATE-1 | I. Story Consumer Boundary | PASS | Adds runtime execution capability only. No outline creation, conversion, import review, or Story Project workflow is introduced. |
| GATE-2 | II. Neutral Format Compatibility | PASS | Snapshots are save-slot artifacts, not `.nrstory` / GlobalConfig / `.nroutline` content. No private story-format semantics are added and no main-repository format decision is required. |
| GATE-3 | II. Snapshot version vs story `schemaVersion` | PASS with decision | Snapshot version MUST be an independent counter from the story `schemaVersion`. Recorded in `data-model.md`. |
| GATE-4 | III. Runtime Semantics In C++ | PASS | Capture, restore, and version gating are entirely C++. Blueprint only composes calls. |
| GATE-5 | IV. Spec Before Large Execution | PASS | This folder. |
| GATE-6 | V. Reviewable, Testable Changes | PASS with obligation | Automated round-trip tests required; Unreal-session verification steps recorded in `quickstart.md`; conflicting binary assets reconciled in-editor per tasks. |

No violations. The Complexity Tracking table is therefore empty.

**Post-design re-check**: unchanged. The design adds no new surface beyond the two modules and the
five assets already listed, and introduces no neutral-format dependency.

## Project Structure

### Documentation (this feature)

```text
specs/0001-save-load-snapshots/
├── spec.md          # Feature specification
├── plan.md          # This file
├── data-model.md    # Snapshot and save slot entities
├── quickstart.md    # In-editor verification steps
├── tasks.md         # Implementation task breakdown
└── checklist.md     # Verification checklist
```

Contract detail is carried by `spec.md` Functional Requirements and `data-model.md`; a separate
`research.md` and `contracts/` folder were not needed because the capability is already specified
by the architecture document and there is a working reference implementation to read.

### Source Code (repository root)

```text
NarrRail/Source/NarrRail/
├── Public/Runtime/
│   ├── NarrRailStorySession.h            # snapshot capture/restore API + snapshot fields
│   └── NarrRailGlobalStateSubsystem.h    # global variable carry-over for restore
└── Private/
    ├── Runtime/
    │   ├── NarrRailStorySession.cpp
    │   └── NarrRailGlobalStateSubsystem.cpp
    └── Tests/
        └── NarrRailSaveSnapshotTests.cpp # new automation coverage

NarrRailUEHost/Source/NarrRailHost/
├── NarrRailHostSaveGame.h                # new USaveGame holding one snapshot
├── NarrRailPlayerController.h            # SaveNarrRailState / LoadNarrRailState
└── NarrRailPlayerController.cpp

NarrRailUEHost/Content/NarrRailStage/UI/
├── Enum_SaveMode.uasset                  # new
├── WBP_Start.uasset                      # new
├── WBP_TextLine.uasset                   # new
└── SaveGame/
    ├── WBP_SaveGame.uasset               # new
    └── WBP_SaveSlot.uasset               # new
```

**Structure Decision**: The feature follows the repository's existing two-surface layout. Session
semantics live in the `NarrRail` plugin runtime module; slot persistence and the user-facing entry
points live in the `NarrRailUEHost` sample host. No new module and no new repository structure is
introduced.

## Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Six binary assets differ between the reference branch and this repository, and the difference cannot be attributed from a diff | Copying them could silently revert unrelated work | Tasks treat these assets as in-editor reconciliation, never file copy; `quickstart.md` defines what to compare |
| Reference implementation has `SnapshotVersion = 1` but no rejection or migration path | Would violate FR-003 and FR-008 if ported as-is | Phase 2 adds the version gate and the fail-without-mutation contract before any restore path is wired to the host |
| Restore interacts with the presenter and typewriter state | Resuming mid-animation produces an ill-defined visual state | FR-011 requires an explicit resulting presenter state; covered by a task and a checklist item |
| Snapshot struct is `BlueprintReadWrite` with many fields | Exposes internals beyond the minimal surface required by FR-007 | Task reviews the exposed surface and narrows it to the capture/restore calls where the reference implementation over-exposes |
| Story asset changes after a snapshot is taken | Restore could silently fall back to a wrong node | FR-009 keeps the policy open but requires detectability; task implements detection |

## Rollout Order

1. Snapshot value and version gate in the runtime module (independently testable, no host needed).
2. Automated round-trip coverage for capture, restore, rejection, and no-mutation-on-save.
3. Host slot persistence and Blueprint entry points.
4. Save slot UI assets, reconciled in-editor.
5. Compatibility documentation update (`README.md`,
   `Docs/04_narrrail_ue_host/UNREAL_PLUGIN_COMPATIBILITY.md`) and the `TECH_ARCHITECTURE.md`
   requirement row moving off "规划中".

Steps 1-2 deliver a usable, verifiable capability without any UI and are the MVP.
