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

## Status

**27 of 32 tasks complete.** Everything that does not require an Unreal Engine is done: the runtime
capability, the host surface, the test suite, and the documentation.

**Five tasks remain, and every one of them needs a machine with UE 5.7.** None could be performed where
the port was done — there is no engine installed there (see `quickstart.md` §1). They are not blocked by
unfinished work; they are blocked by the absence of a toolchain:

| Task | What it needs |
|------|---------------|
| T018 | Editor. Save slot UI assets. Binary — must not be copied or text-patched. |
| T019 | Editor. Wire the save slot surface into the stage Blueprints. Depends on T018. |
| T026 | Editor. Reconcile four conflicting dialogue assets. |
| T027 | Engine. Run the automation suite and the host build. |
| T028 | Editor. Run quickstart steps 2-6 in a live session. |

Because T018 and T019 are open, **User Story 2 acceptance scenario 4** — selecting a saved slot from the
save slot UI resumes the story — is not satisfied. That is an explicitly open criterion, not a hidden one.

The reference branch this feature was ported from was deleted on 2026-09-16. Its payload is archived two
ways before deletion; T018 onward should read `Docs/05_reference_archive/README.md` first. See the second
"Known residual" section at the end of this file.

Also note the scope of what was done: T029 (the constitution review) **is** complete, but it is a source
review and could not check a build or a test run.

**Nothing in this feature has been compiled.** That single sentence is the most important thing in this
file. See `quickstart.md` §1 before reporting the feature as working.

## Phase 1: Setup

- [x] T001 Read the reference implementation at the two commits above and enumerate the full set of stored snapshot fields, recording each field's name, type, and purpose. **Done 2026-09-16.** Enumerated all fourteen session-snapshot fields from `NarrRailStorySession.h`, plus the three-field `FNarrRailGlobalStateSnapshot`, the five sub-fields of `FNarrRailLastChoiceInfo`, the two fields of `FNarrRailChoiceSelectionSnapshot`, and the three derived members that are rebuilt rather than stored. `data-model.md` updated: the field table is now complete, the fabricated "global variable state on the session context" row was corrected to the separate global-state entity, all `to confirm` markers are resolved, and FR-009's gate scope gained a return-stack check with a stated dereferenced-vs-recorded rule. Two findings recorded rather than silently fixed: `NodeHistory` and `ExhaustivePendingChoiceReturnStack` carried node ids that no gate validated.
- [x] T002 Decide FR-009 and FR-010. **Decided 2026-09-16**: FR-009 = consistency gate over content-bound state; FR-010 = strict rejection with a per-version dispatch seam. Both recorded in `data-model.md`, with `spec.md` FR-009, FR-010, SC-006, and SC-007 updated to match.
- [x] T003 Confirm the automation test entry point: how `NarrRail/Source/NarrRail/Private/Tests/` is picked up by the module build, and what command runs the suite. Record the command in `quickstart.md` step 1. **Done 2026-09-16.** No build change is needed: UBT compiles `Private/` recursively, `Misc/AutomationTest.h` is in `Core` which the module already depends on, and `#if WITH_DEV_AUTOMATION_TESTS` compiles the tests out of shipping. The `Tests/` directory does not exist yet — this feature creates it, the first automation test in this repository. The UE 5.7 command is recorded in `quickstart.md` §1. **Blocking finding recorded there: no Unreal Engine is installed on the port machine**, so the suite cannot be run here and steps 1-6 must be verified by the user.

## Phase 2: Foundational

**⚠️ CRITICAL**: No user story work can begin until this phase is complete. This phase defines the snapshot value and the version gate that every restore path depends on.

- [x] T004 [US-shared] Add the snapshot value types to `NarrRail/Source/NarrRail/Public/Runtime/NarrRailStorySession.h`: the session snapshot struct with its version field, and the choice selection record struct. Expose only what FR-007 allows. **Done 2026-09-16.** Both structs added with per-field comments stating purpose and content-binding. One deviation from the reference: `SnapshotVersion` defaults to `0`, not `1`, and capture stamps the supported version explicitly — with a default of `1` a save missing the field deserialises to `1` and passes the gate, so FR-003 could not be satisfied. Recorded in `data-model.md`.
- [x] T005 Narrow the exposed surface in `NarrRail/Source/NarrRail/Public/Runtime/NarrRailStorySession.h`: keep the capture and restore entry points and hide the internal snapshot fields from Blueprint where the reference implementation over-exposes them. Justify each field that remains Blueprint-visible in a comment. **Done 2026-09-16.** No field is Blueprint-visible: the struct stays `BlueprintType` only because it appears in the two UFUNCTION signatures. The reference marked all fourteen fields `BlueprintReadWrite`, which lets a Blueprint caller hand-assemble a snapshot or rewrite `SnapshotVersion` — the exact things the version gate and consistency gate exist to stop. Capture stays `BlueprintPure`, restore stays `BlueprintCallable`; those two are the entire FR-007 surface.
- [x] T006 Implement capture in `NarrRail/Source/NarrRail/Private/Runtime/NarrRailStorySession.cpp` as a const operation that cannot mutate session state (FR-001, FR-006). **Done 2026-09-16.** `GetSessionSnapshot()` is `const` and only reads. The `TSet`-backed consumed-choice map is converted to sorted per-node records so two captures of the same state compare equal — the no-mutation assertion (T013) depends on that.
- [x] T007 Implement the version gate in `NarrRail/Source/NarrRail/Private/Runtime/NarrRailStorySession.cpp` as a per-version dispatch that accepts only the supported version and rejects older, newer, absent, and malformed versions before any state is written (FR-003, FR-008, FR-010). Keep the dispatch shaped as the seam a future migration branch slots into rather than a flat comparison. **Done 2026-09-16.** `ValidateSnapshotVersion` is a `switch` on the version with a single supported `case` and a `default` that rejects everything else. The supported version lives in one constant in the `NarrRailRuntime` namespace. A future migration is a new `case`, not a restructure.
- [x] T008 Implement restore in `NarrRail/Source/NarrRail/Private/Runtime/NarrRailStorySession.cpp`, including node resolution and the presenter's resulting state (FR-002, FR-004, FR-008, FR-011). **Done 2026-09-16.** `RestoreSessionSnapshot` runs all three gates, then commits with no remaining failure path. Node resolution moved into the consistency gate; `RefreshCurrentNodeAfterRestore` handles the presenter and the `bRefreshPresenter` parameter decides the presenter's resulting state (FR-011).
- [x] T009 Carry global variable state through capture and restore in `NarrRail/Source/NarrRail/Public/Runtime/NarrRailGlobalStateSubsystem.h` and `NarrRail/Source/NarrRail/Private/Runtime/NarrRailGlobalStateSubsystem.cpp`, so globals set before a save are present after a load. **Done 2026-09-16.** `FNarrRailGlobalStateSnapshot` and the get/restore pair added; `ApplyGlobalConfigContent` factored out so `ApplyGlobalConfig` and restore share the loading path. Two deviations from the reference, both recorded in `data-model.md`: the version gate rejects any value other than the supported one rather than only non-positive ones, and restore resolves every config path before mutating anything so a deleted config cannot leave a half-applied global state.
- [x] T030 [US-shared] Implement the FR-009 consistency gate in `NarrRail/Source/NarrRail/Private/Runtime/NarrRailStorySession.cpp`, covering the four checks in `data-model.md` invariant 5: validate the multi-dialogue line index against the resolved node, validate that every node id referenced by a consumed choice record still resolves, validate every consumed option index against that node's option count, and validate that every node id on the exhaustive-choice return stack still resolves. Reject on any failure, naming the offending field and node. Do **not** validate `NodeHistory` — see invariant 6 for why recorded node ids stay ungated. **Done 2026-09-16.** All four checks implemented in `ValidateSnapshotConsistency`; every failure message names the field and the node. `NodeHistory` and `LastChoiceInfo` are deliberately ungated, each with a comment stating why, so the exclusion reads as a decision rather than an oversight. The return-stack check is additionally reported as a `MissingNode` failure carrying the offending node id, so the caller can name it.
- [x] T031 [US-shared] Order the gates so that version, identity, and consistency checks all run before the first write to session state, so a rejection at any gate leaves the session untouched (FR-008). **Done 2026-09-16.** The three gate functions are all `const` and touch no state; `RestoreSessionSnapshot` calls them in sequence and only then begins the commit block, which is marked in the source as the point past which no failure path exists.

## Phase 3: User Story 1 - Resume a session exactly where it stopped (Priority: P1)

**Goal**: A session restored from a snapshot continues as if it had never stopped.

**Independent Test**: automated round trip plus quickstart steps 2-4.

**Test file**: `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` — created by this phase. It is the first automation test in this repository, so the `Private/Tests/` directory did not exist before. All eleven tests sit under `NarrRail.Save.`, which is the filter recorded in `quickstart.md` §1.

- [x] T010 [US1] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for a plain node round trip: capture, restore, assert node and global variables match. **Done 2026-09-16.** `PlainNodeRoundTrip` captures at a plain `Dialogue` node with a rewritten variable, restores into a session deliberately set to a different value, and asserts the node, session state and variable value come back. Note: the shared fixture is Blueprint-free and builds its story from `NewObject`, so it runs headless. Global variables are covered by the `GlobalStateSnapshot` tests rather than here, because globals live in the subsystem and not in the session snapshot.
- [x] T011 [US1] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for a `MultiDialogue` round trip that asserts the resumed line index is the captured one and not the first line. **Done 2026-09-16.** `MultiDialogueLineRoundTrip`. The "not the first line" assertion is separate from the "equals 1" assertion on purpose: a bug that always restored line 0 would still satisfy a node-id-only assertion.
- [x] T012 [US1] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for a partially consumed `Choice` node, asserting consumed selections survive the round trip. **Done 2026-09-16.** `ConsumedChoiceRoundTrip` walks into an `ExhaustiveUntilComplete` choice, picks one option, and asserts the consumed record and the return stack before and after the round trip, then compares the re-captured snapshot field by field.
- [x] T013 [US1] Add automation coverage asserting capture does not mutate session state: capture, compare all observable state, then capture again and compare the two snapshots (FR-006, SC-002). **Done 2026-09-16.** `CaptureDoesNotMutate` captures at the richest state the fixture can reach, compares the observable state around the capture, then compares two snapshots across all fourteen fields. The comparison helper compares every field rather than a sample, because a sampled comparison leaves an unobserved gap in exactly the assertion that is supposed to rule out mutation.
- [x] T014 [US1] Add automation coverage asserting that a restored session advances to the same next node as an uninterrupted session. **Done 2026-09-16.** `ResumeAdvancesIdentically` plays a reference session one step past the capture point, restores the snapshot into a second session, and asserts both land on the same node.

## Phase 4: User Story 2 - Save and load from the host surface (Priority: P2)

**Goal**: The capability is reachable from Blueprint and from an in-game save slot surface.

**Independent Test**: quickstart steps 2-4 driven from the host rather than from tests.

- [x] T015 [US2] Add `NarrRailUEHost/Source/NarrRailHost/NarrRailHostSaveGame.h` holding one serialized snapshot as a `USaveGame`. **Done 2026-09-16.** Holds the session snapshot *and* the global state snapshot plus a display timestamp. One deviation from the reference: the two snapshot fields are `VisibleAnywhere` rather than `BlueprintReadWrite`, so a Blueprint cannot hand-assemble a slot payload — same FR-007 reasoning as the runtime struct.
- [x] T016 [US2] Add slot save and load entry points to `NarrRailUEHost/Source/NarrRailHost/NarrRailPlayerController.h` and `NarrRailUEHost/Source/NarrRailHost/NarrRailPlayerController.cpp`, covering the missing-slot failure path so the running session is left untouched (FR-005, FR-008). **Done 2026-09-16.** `SaveNarrRailState` / `LoadNarrRailState`. A missing or foreign slot returns false before anything is touched; a failed restore logs the runtime's own message rather than swallowing it. **Known residual, documented in-source and below:** the host restores global state and session state as two steps, so if the global restore succeeds and the session restore is then rejected, global state stays at the saved values. Making that atomic needs a validate-only entry point on the runtime; see the note under Dependencies.
- [x] T017 [US2] Verify the Blueprint-exposed surface for this feature matches FR-007 exactly: capture and restore on the session, save and load on the host. Remove any additional exposure found. **Done 2026-09-16 — and it found something.** The review's first pass claimed the surface was already minimal; it was not. `GetGlobalStateSnapshot` and `RestoreGlobalStateSnapshot` had been carried over as `UFUNCTION`s, and FR-007 enumerates only session capture/restore plus host save/load. Both were demoted to plain public C++ methods — still callable by the host and the tests, no longer visible to Blueprint. Public is not the same as Blueprint-visible in UE, and the reference implementation conflated the two. Final surface: `GetSessionSnapshot` (`BlueprintPure`), `RestoreSessionSnapshot` (`BlueprintCallable`), `SaveNarrRailState` / `LoadNarrRailState` (`BlueprintCallable`), plus `SavedAtUtc` as a documented read-only display exception (CHK043). No snapshot field is Blueprint-readable or writable at any layer.
- [ ] T018 [P] [US2] **⚠️ Requires the editor.** Migrate the save slot assets into `NarrRailUEHost/Content/NarrRailStage/UI/`: `Enum_SaveMode.uasset` (2,203 B), `WBP_Start.uasset` (337,906 B), `WBP_TextLine.uasset` (56,251 B), `SaveGame/WBP_SaveGame.uasset` (104,456 B), `SaveGame/WBP_SaveSlot.uasset` (130,664 B) — ~631 KB. Author or reconcile these in the editor; do not copy binary files blindly. **Source:** the reference branch was deleted 2026-09-16; these five now come from `Docs/05_reference_archive/reference-branch-payload.zip` (or the `archive/narrrail-save-snapshots` tag in the main repository). Read that directory's `README.md` first — it also lists 4 unmigrated Unreal remote-tooling scripts that may be the fastest route to reproducing this work. **This task and T019 close User Story 2 acceptance scenario 4.**
- [ ] T019 [US2] **⚠️ Requires the editor.** Author or reconcile `NarrRailUEHost/Content/NarrRailStage/BP_StoryController.uasset` and `BP_StoryGameMode.uasset` so the save slot surface is reachable from the running stage. Record each reconciliation decision. Depends on T018: both need the same editor session.

## Phase 5: User Story 3 - Reject incompatible saves (Priority: P2)

**Goal**: An unsupported snapshot never produces a partial resume.

**Independent Test**: quickstart step 5, plus the rejection tests below.

- [x] T020 [US3] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for a snapshot declaring a version newer than supported: restore must fail and session state must be unchanged. **Done 2026-09-16**, in `RejectUnsupportedVersion`. The target session is deliberately advanced past the snapshot's state first, so that "unchanged" is falsifiable — with the target and the snapshot in the same state, the assertion would hold even if the gate never fired.
- [x] T021 [US3] Add automation coverage for a snapshot declaring a version older than supported: restore must fail and session state must be unchanged (FR-010). **Done 2026-09-16**, same test. Recorded honestly: with only version 1 in existence no positive integer is older, so "older" and "absent" collapse onto the same value, 0. Tested as such and labelled as such rather than pretending the two cases are distinct.
- [x] T022 [US3] Add automation coverage for a malformed snapshot missing a required field: restore must fail and session state must be unchanged. **Done 2026-09-16.** `RejectMissingRequiredField` restores a default-constructed snapshot, which is equivalent to an old save in which the fields do not exist. This is the case a `SnapshotVersion` default of `1` would have let through silently. `RejectUnsupportedVersion` additionally covers a negative and a far-future version.
- [x] T023 [US3] Add automation coverage for a snapshot whose referenced node does not resolve in the loaded story asset: restore must fail and identify the offending node. **Done 2026-09-16.** `RejectUnresolvableNode` asserts both the `MissingNode` code and that the reported node id names the missing node.
- [x] T032 [US3] Add automation coverage in `NarrRail/Source/NarrRail/Private/Tests/NarrRailSaveSnapshotTests.cpp` for the FR-009 consistency gate: a snapshot whose line index exceeds the resolved node's line count is rejected; a snapshot whose consumed choice record references a deleted node is rejected; a snapshot whose consumed option index exceeds that node's option count is rejected; a snapshot whose exhaustive-choice return stack holds a deleted node id is rejected. Each case asserts zero session mutation. Add a fifth case asserting the deliberate negative: a snapshot whose `NodeHistory` holds a deleted node id is **accepted**, so the gate cannot drift into over-rejecting. **Done 2026-09-16.** `ConsistencyGate` covers all four rejections, all three node-naming assertions, the deliberate `NodeHistory` negative, and a sixth case not in the original list: a line index set while the resolved node is not a `MultiDialogue` node at all.

## Phase 6: Polish & Cross-Cutting Concerns

- [x] T024 [P] Update `README.md` compatibility table and `Docs/04_narrrail_ue_host/UNREAL_PLUGIN_COMPATIBILITY.md` to state the persistence capability and the snapshot version supported (SC-005). **Done 2026-09-16.** README gains a `Session snapshot` column (`version 1`). The compatibility doc gains two version rows, a new §1.1 explaining that the snapshot version is this repository's own version line and independent of the story `schemaVersion`, five capability rows (session save/load, global save/load, rejection, migration not implemented, save slot UI host-only), and the full snapshot-versioning policy.
- [x] T025 [P] Update the requirement row in `Docs/01_architecture/TECH_ARCHITECTURE.md` so 存档恢复 is no longer marked 规划中, and record the snapshot versioning policy there. **Done 2026-09-16.** The row now reads 已完成 with the policy in the notes column, and the `Persistence` layer description no longer claims 版本迁移 without qualification — forward migration is explicitly not implemented and the dispatch seam is named as the place it would go.
- [ ] T026 [P] Reconcile the remaining conflicting dialogue assets in the editor and record the decisions: `NarrRail/Content/UI/ADV/WBP_Dialogue_ADV.uasset`, `NarrRail/Content/UI/NVL/WBP_Dialogue_NVL.uasset`, `NarrRail/Content/UI/NVL/WBP_ButtonLine.uasset`, `NarrRail/Content/UI/NVL/WBP_TextLine.uasset`. **⚠️ Requires the editor** — no engine on the port machine, and these are binary assets that must not be copied or text-patched.
- [ ] T027 Run the full automation suite and the host build; confirm both are clean. **⚠️ Not executable on the port machine** — no Unreal Engine is installed there (see `quickstart.md` §1). This task and T028 are the handoff: they must be run by the user on a machine with UE 5.7, and the result reported honestly rather than assumed.
- [ ] T028 Run the unmatched quickstart steps in a real Unreal session and record which steps were performed, which were not, and each asset reconciliation decision, per the Reporting section of `quickstart.md`. **⚠️ Same blocker as T027.**
- [x] T029 Review the diff against `spec.md` and the constitution, confirming no neutral-format semantics were introduced and that the Blueprint surface is still minimal. Also resolve the dead `NarrRailHostTest.h` / `NarrRailHostTest.cpp` pair carried in from the host project template: an empty class, referenced by nothing, containing no automation test. Either delete it or give it a stated purpose — as it stands the name implies test coverage in the host module that does not exist. **Done 2026-09-16.** Verified by enumerating every file the port touched: no importer, factory, schema, or format-contract code appears, so constitution principles I and II hold. The surface review found one real over-exposure (the two global-snapshot `UFUNCTION`s) which was removed — see T017. The dead `NarrRailHostTest` pair was confirmed unreferenced and deleted. Findings recorded as CHK041-CHK045. **What this review could not do: check a build or a test run.** It is a source review, and it is labelled as one.

## Dependencies

- T001 must precede T004: the field set defines the struct. T002 is complete: the FR-009 and FR-010 decisions are recorded in `data-model.md` and reflected in `spec.md`.
- T007 and T030 are the two gates that must both land before T008 wires restore to the host.
- T031 depends on T007 and T030 both existing.
- T003 must precede T010 so that newly added tests are known to run.
- T004-T009 (Phase 2) must precede every user-story phase.
- T008 must precede T015-T016: the host persists a snapshot the runtime can already restore.
- T018-T019 are editor work and can proceed in parallel with T010-T014.
- T024-T026 can proceed in parallel once Phase 2 lands.
- T027-T029 run last.
- T018, T019, T026, T027 and T028 all require a machine with Unreal Engine 5.7. None of them could be
  performed on the machine this port was done on; see `quickstart.md` §1. They are the handoff.

## Known residual: the host load is atomic per snapshot, not across the two

FR-008's atomicity clause is about **session** state, and the session restore satisfies it: all three
gates run before the first write, so a rejection leaves the session untouched. The host `LoadNarrRailState`
however restores two snapshots in sequence (T016). If the global restore succeeds and the session restore
is then rejected, global state stays at the saved values while the session does not move.

Not fixed here, deliberately. Making it atomic requires the runtime to offer a validate-without-committing
entry point so the host can two-phase both snapshots. That is an API change, and with no real save produced
yet and no engine available to test against, it would be compensation logic that nothing exercises.
**Trigger to revisit**: the first real need to recover from a partial host load. Recorded in-source at the
ordering comment in `NarrRailPlayerController.cpp` so it is not rediscovered from scratch.

## Known residual: the branch was deleted with work still open

`Courtshipfy/NarrRail@feature/narrrail-save-snapshots` **was deleted on 2026-09-16**, after archiving. The
port's goal was to reach a state where it could go; it was reached by moving the payload out rather than by
finishing the work, and that distinction matters.

**Archiving removed the data-loss reason. It did not close the work.** Two independent copies were made
first:

- Tag `archive/narrrail-save-snapshots` in `Courtshipfy/NarrRail` — annotated `84b90f3` →
  `2e3903f8a42711f54e9aa279135239387beab799`. Complete: the whole tree, reference C++ included.
- `NarrRailUEHost/../Docs/05_reference_archive/reference-branch-payload.zip` in this repository — 27 files,
  1.7 MB, with a per-file SHA-256 manifest and a `README.md` covering recovery. See that README for the
  full contents list.

What was at risk, precisely. Scope is **paths the branch changed since its merge base `70fcdb0`** — not a
two-tree comparison, which over-reported this by 4× (see `quickstart.md` §7 for why):

| Category | Count | Disposition |
|----------|-------|-------------|
| UI asset absent from this repository | 5 (~631 KB) | Archived. **T018 still open.** |
| Asset in both, branch version differs | 6 | Archived (branch side). **T019/T026 still open.** |
| Unreal remote-tooling scripts, unmigrated | 4 | Archived. Not previously noticed; see below. |
| Reference C++ (deviations' evidence base) | 5 | Archived. |
| Story assets renamed by the branch | 2 + 1 deletion | Archived / recorded. Not a risk. |
| Dead host scaffolding (`NarrRailHostTest.*`) | 2 | Not archived. Empty class, deleted here in T029. |
| `NarrRailEditor/**`, `Docs/06_planning/TASK_PLAN.md` | ~85 | **Not branch payload at all** — `main`'s own later changes. Excluded. |

Three things worth stating plainly:

1. **The 4 Unreal remote-tooling scripts were a genuine find.** `NarrRailUEHost/Content/Python/init_unreal.py`,
   `NarrRailUEHost/Content/Python/narrrail_remote_server.py`, `Tools/dump_widgets.py`, `Tools/unreal_remote.py`
   — written on the branch, never migrated, present in no other tree. They drive the editor over Unreal's
   remote Python interface, which is likely how the branch's widget work was done at all. Whoever does T018
   should look at them first; they may be the fastest route to reproducing that work.
2. **The five UI assets are the save slot surface.** Without T018 the ported C++ has no way to be exercised
   from the running stage, so T028 could not be performed either.
3. **User Story 2 acceptance scenario 4 is not satisfied** and is now an explicitly open acceptance
   criterion, not a hidden one: "Given the save slot UI is open, When the player selects a saved slot, Then
   loading resumes the story in that slot." T018 and T019 are what close it.

**T018, T019, T026, T027 and T028 remain open and all require a machine with Unreal Engine 5.7.** Status is
therefore still 27/32, and nothing in this feature has been compiled.

## MVP Scope

Phase 1, Phase 2, and User Story 1. That combination delivers a verifiable, automated, Blueprint-free
capability: the runtime can capture and restore a session faithfully and rejects what it cannot
restore. Host slots, UI assets, and documentation follow as User Story 2 and the polish phase.
