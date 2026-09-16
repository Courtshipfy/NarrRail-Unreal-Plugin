# Tasks: Runtime save/load snapshots

**Input**: Design documents from `/specs/0001-save-load-snapshots/`

**Prerequisites**: [plan.md](./plan.md), [spec.md](./spec.md), [data-model.md](./data-model.md), [quickstart.md](./quickstart.md)

**Issue**: `Courtshipfy/NarrRail-Unreal-Plugin#1`

**Tests**: Required. This feature changes runtime state semantics and persistence, so principle V of
the constitution obliges automated coverage at the session seam.

**Reference material**: `Courtshipfy/NarrRail@feature/narrrail-save-snapshots`
(HEAD `2e3903f8a42711f54e9aa279135239387beab799`, base `70fcdb0b837c428cf2e8c5de812809aa27177ff9`).
Read it as a source of proven behaviour, not as a patch to apply wholesale.

**Path conventions**: paths are relative to the repository root. Two modules are involved:
`NarrRail/Source/NarrRail/` (runtime) and `NarrRailUEHost/Source/NarrRailHost/` (host).

## Phase 1: Setup

- [ ] T001 Read the reference implementation at the two commits above and enumerate the full set of stored snapshot fields, recording each field's name, type, and purpose. Reconcile the result against the field table in `data-model.md` and update that table before writing any code.
- [ ] T002 Decide FR-009 and FR-010, the story-asset mismatch policy and the older-version policy. Record both decisions in `data-model.md`. If either decision changes required behaviour, update `spec.md` before implementation rather than encoding the decision in code.
- [ ] T003 Confirm the automation test entry point: how `NarrRail/Source/NarrRail/Private/Tests/` is picked up by the module build, and what command runs the suite. Record the command in `quickstart.md` step 1.

## Phase 2: Foundational

**⚠️ CRITICAL**: No user story work can begin until this phase is complete. This phase defines the snapshot value and the version gate that every restore path depends on.

- [ ] T004 [US-shared] Add the snapshot value types to `NarrRail/Source/NarrRail/Public/Runtime/NarrRailStorySession.h`: the session snapshot struct with its version field, and the choice selection record struct. Expose only what FR-007 allows.
- [ ] T005 Narrow the exposed surface in `NarrRail/Source/NarrRail/Public/Runtime/NarrRailStorySession.h`: keep the capture and restore entry points and hide the internal snapshot fields from Blueprint where the reference implementation over-exposes them. Justify each field that remains Blueprint-visible in a comment.
- [ ] T006 Implement capture in `NarrRail/Source/NarrRail/Private/Runtime/NarrRailStorySession.cpp` as a const operation that cannot mutate session state (FR-001, FR-006).
- [ ] T007 Implement the version gate in `NarrRail/Source/NarrRail/Private/Runtime/NarrRailStorySession.cpp`: reject unsupported, absent, or malformed versions before any state is written (FR-003, FR-008).
- [ ] T008 Implement restore in `NarrRail/Source/NarrRail/Private/Runtime/NarrRailStorySession.cpp`, including node resolution and the presenter's resulting state (FR-002, FR-004, FR-008, FR-011).
- [ ] T009 Carry global variable state through capture and restore in `NarrRail/Source/NarrRail/Public/Runtime/NarrRailGlobalStateSubsystem.h` and `NarrRail/Source/NarrRail/Private/Runtime/NarrRailGlobalStateSubsystem.cpp`, so globals set before a save are present after a load.

## Phase 3: User Story 1 - Resume a session exactly where it stopped (Priority: P1)

**Goal**: A session restored from a snapshot continues as if it had never stopped.

**Independent Test**: automated round trip plus quickstart steps 2-4.

- [ ] T010 [US1] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for a plain node round trip: capture, restore, assert node and global variables match.
- [ ] T011 [US1] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for a `MultiDialogue` round trip that asserts the resumed line index is the captured one and not the first line.
- [ ] T012 [US1] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for a partially consumed `Choice` node, asserting consumed selections survive the round trip.
- [ ] T013 [US1] Add automation coverage asserting capture does not mutate session state: capture, compare all observable state, then capture again and compare the two snapshots (FR-006, SC-002).
- [ ] T014 [US1] Add automation coverage asserting that a restored session advances to the same next node as an uninterrupted session.

## Phase 4: User Story 2 - Save and load from the host surface (Priority: P2)

**Goal**: The capability is reachable from Blueprint and from an in-game save slot surface.

**Independent Test**: quickstart steps 2-4 driven from the host rather than from tests.

- [ ] T015 [US2] Add `NarrRailUEHost/Source/NarrRailHost/NarrRailHostSaveGame.h` holding one serialized snapshot as a `USaveGame`.
- [ ] T016 [US2] Add slot save and load entry points to `NarrRailUEHost/Source/NarrRailHost/NarrRailPlayerController.h` and `NarrRailUEHost/Source/NarrRailHost/NarrRailPlayerController.cpp`, covering the missing-slot failure path so the running session is left untouched (FR-005, FR-008).
- [ ] T017 [US2] Verify the Blueprint-exposed surface for this feature matches FR-007 exactly: capture and restore on the session, save and load on the host. Remove any additional exposure found.
- [ ] T018 [P] [US2] Migrate the save slot assets into `NarrRailUEHost/Content/NarrRailStage/UI/`: `Enum_SaveMode.uasset`, `WBP_Start.uasset`, `WBP_TextLine.uasset`, `SaveGame/WBP_SaveGame.uasset`, `SaveGame/WBP_SaveSlot.uasset`. Author or reconcile these in the editor; do not copy binary files blindly.
- [ ] T019 [US2] Author or reconcile `NarrRailUEHost/Content/NarrRailStage/BP_StoryController.uasset` and `BP_StoryGameMode.uasset` so the save slot surface is reachable from the running stage. Record each reconciliation decision.

## Phase 5: User Story 3 - Reject incompatible saves (Priority: P2)

**Goal**: An unsupported snapshot never produces a partial resume.

**Independent Test**: quickstart step 5, plus the rejection tests below.

- [ ] T020 [US3] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for a snapshot declaring a version newer than supported: restore must fail and session state must be unchanged.
- [ ] T021 [US3] Add automation coverage for a snapshot declaring a version older than supported, matching whichever policy T002 selected.
- [ ] T022 [US3] Add automation coverage for a malformed snapshot missing a required field: restore must fail and session state must be unchanged.
- [ ] T023 [US3] Add automation coverage for a snapshot whose referenced node does not resolve in the loaded story asset: restore must fail and identify the offending node.

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T024 [P] Update `README.md` compatibility table and `Docs/04_narrrail_ue_host/UNREAL_PLUGIN_COMPATIBILITY.md` to state the persistence capability and the snapshot version supported (SC-005).
- [ ] T025 [P] Update the requirement row in `Docs/01_architecture/TECH_ARCHITECTURE.md` so 存档恢复 is no longer marked 规划中, and record the snapshot versioning policy there.
- [ ] T026 [P] Reconcile the remaining conflicting dialogue assets in the editor and record the decisions: `NarrRail/Content/UI/ADV/WBP_Dialogue_ADV.uasset`, `NarrRail/Content/UI/NVL/WBP_Dialogue_NVL.uasset`, `NarrRail/Content/UI/NVL/WBP_ButtonLine.uasset`, `NarrRail/Content/UI/NVL/WBP_TextLine.uasset`.
- [ ] T027 Run the full automation suite and the host build; confirm both are clean.
- [ ] T028 Run the unmatched quickstart steps in a real Unreal session and record which steps were performed, which were not, and each asset reconciliation decision, per the Reporting section of `quickstart.md`.
- [ ] T029 Review the diff against `spec.md` and the constitution, confirming no neutral-format semantics were introduced and that the Blueprint surface is still minimal.

## Dependencies

- T001 and T002 must precede T004: the field set and the version policy define the struct.
- T003 must precede T010 so that newly added tests are known to run.
- T004-T009 (Phase 2) must precede every user-story phase.
- T008 must precede T015-T016: the host persists a snapshot the runtime can already restore.
- T018-T019 are editor work and can proceed in parallel with T010-T014.
- T024-T026 can proceed in parallel once Phase 2 lands.
- T027-T029 run last.

## MVP Scope

Phase 1, Phase 2, and User Story 1. That combination delivers a verifiable, automated, Blueprint-free
capability: the runtime can capture and restore a session faithfully and rejects what it cannot
restore. Host slots, UI assets, and documentation follow as User Story 2 and the polish phase.
