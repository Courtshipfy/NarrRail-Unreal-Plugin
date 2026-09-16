# Implementation Checklist: Runtime save/load snapshots

**Purpose**: Verify the implementation against the issue acceptance criteria, the spec requirements,
and the constitution gates before the feature is considered complete.

**Created**: 2026-09-16

**Feature**: [spec.md](./spec.md) | [plan.md](./plan.md) | [tasks.md](./tasks.md)

## Snapshot fidelity

- [ ] CHK001 Does the captured snapshot contain every field listed in `data-model.md`, and is each remaining field from the reference implementation either kept with a stated reason or deliberately dropped?
- [ ] CHK002 Does a restored session sit on the captured node rather than on the story's first node?
- [ ] CHK003 Are global variables set before a save present with the same values after a load?
- [ ] CHK004 Are session-local variables set before a save present with the same values after a load?
- [ ] CHK005 Does a session restored mid-`MultiDialogue` resume at the captured line index instead of restarting the node?
- [ ] CHK006 Does a session restored after a partially consumed `Choice` node still withhold the already-consumed options and still offer the remaining ones?
- [ ] CHK007 Does advancing a restored session reach the same next node an uninterrupted session would have reached?
- [ ] CHK008 Does capture leave the running session's observable state unchanged, verified by comparison rather than by inspection?
- [ ] CHK009 Is the presenter's state after restore defined explicitly, rather than inherited from an animation that was in progress at capture time?

## Failure handling and versioning

- [ ] CHK010 Is the snapshot version an independent counter, unrelated to the story `schemaVersion`?
- [ ] CHK011 Is a snapshot with a version newer than supported rejected with an explicit failure?
- [ ] CHK012 Is a snapshot with a version older than supported rejected with an explicit failure, consistent with the FR-010 decision?
- [ ] CHK013 Is a snapshot with an absent or malformed version rejected with an explicit failure?
- [ ] CHK014 Is the session state provably unchanged on every restore failure path, not merely on the ones that are easy to test?
- [ ] CHK015 Does an unresolvable node reference produce a failure that names the offending node?
- [x] CHK016 Are FR-009 and FR-010 answered and recorded, rather than left as open questions at implementation time? **Yes — decided 2026-09-16**, recorded in `data-model.md` and reflected in `spec.md` FR-009, FR-010, SC-006, and SC-007.
- [ ] CHK035 Is the multi-dialogue line index validated against the resolved node's line range on restore, with the sentinel accepted?
- [ ] CHK036 Is every node id referenced by a consumed choice record validated to still resolve on restore?
- [ ] CHK037 Is every consumed option index validated against that node's option count on restore?
- [ ] CHK038 Is the restore path structured as a per-version dispatch with a single supported branch, so a future migration is a new branch rather than a restructure?
- [ ] CHK039 Is every node id on the exhaustive-choice return stack validated to still resolve on restore?
- [ ] CHK040 Is `NodeHistory` deliberately left ungated, with a test asserting a snapshot carrying a deleted history node id is accepted, so the gate cannot drift into over-rejecting ordinary authoring edits?

## C++ and Blueprint boundary

- [ ] CHK017 Is capture implemented in C++ without requiring Blueprint to assemble any part of the state?
- [ ] CHK018 Is restore implemented in C++ as a single call?
- [ ] CHK019 Is the Blueprint-visible surface limited to capture and restore on the session, and save and load on the host?
- [ ] CHK020 Does Blueprint carry no execution rules for this feature?

## Host persistence

- [ ] CHK021 Can the host write a snapshot into a named slot and read it back?
- [ ] CHK022 Does loading a slot that does not exist fail explicitly while leaving the current session playable?
- [ ] CHK023 Is the slot payload a single snapshot, with the runtime module unaware of slot names?

## Assets

- [ ] CHK024 Were the five new save slot assets authored or reconciled in the editor rather than copied as binary files?
- [ ] CHK025 Was each of the six conflicting binary assets decided in the editor, with the decision recorded in the pull request description?
- [ ] CHK026 Do the repository's ignore rules still keep `Binaries/`, `Intermediate/`, `Saved/`, and `DerivedDataCache/` untracked after the asset work?
- [ ] CHK027 Is the sample host's story content unchanged, with the older test stories from the reference branch not reintroduced?

## Constitution and documentation

- [ ] CHK028 Did the change introduce no private `.nrstory`, GlobalConfig, or `.nroutline` semantics, and require no main-repository format decision?
- [ ] CHK029 Did the change stay within the Story Consumer boundary, adding no authoring workflow?
- [ ] CHK030 Are the `README.md` compatibility table and `Docs/04_narrrail_ue_host/UNREAL_PLUGIN_COMPATIBILITY.md` updated for the persistence capability and the supported snapshot version?
- [ ] CHK031 Is the 存档恢复 row in `Docs/01_architecture/TECH_ARCHITECTURE.md` updated off 规划中, with the versioning policy recorded?

## Verification honesty

- [ ] CHK032 Does the automated suite include capture, restore, rejection, no-mutation-on-save, and unresolvable-node cases, and did the suite actually run? **Partially: the cases all exist (eleven tests, see `quickstart.md` §1), but the suite has NOT been run.** No Unreal Engine on the machine this port was done on. The "did it actually run" half is unanswered and must not be reported as yes.
- [ ] CHK033 Were the manual Unreal steps in `quickstart.md` performed, or explicitly reported as not performed? **Explicitly reported as not performed.** Steps 1-6 need an engine; none is installed here.
- [ ] CHK034 Does the closing report state which verification steps were executed and which were not, without claiming anything unverified? Answered by the note at the top of `quickstart.md` §1 and by `tasks.md` T027/T028.

## T029 review: boundary and surface

- [x] CHK041 Did the change touch any format-contract code — `.nrstory` / GlobalConfig / `.nroutline` parsing, importers, factories, or schema handling? **No.** The port touched `NarrRailStorySession`, `NarrRailGlobalStateSubsystem`, `NarrRailStoryTypes`, the new `Private/Tests/` file, the host save game and player controller, docs, and specs. Constitution principles I and II hold: no neutral-format semantics were introduced or reinterpreted.
- [x] CHK042 Is the Blueprint surface exactly what FR-007 enumerates? **Yes, after a correction.** FR-007 allows session capture/restore and host save/load. The port had additionally exposed `GetGlobalStateSnapshot` / `RestoreGlobalStateSnapshot` as `UFUNCTION`s; T017 requires removing additional exposure, and they were demoted to plain public C++ methods. The host and the tests still call them. Public is not the same as Blueprint-visible in UE, and the reference conflated the two.
- [x] CHK043 Is anything Blueprint-visible beyond FR-007's four functions? **One field, with a stated reason.** `UNarrRailHostSaveGame::SavedAtUtc` is `BlueprintReadOnly`. FR-007 speaks of the surface in terms of functions; a display timestamp is part of the save-slot UI and cannot be shown without it. Read-only and written only by C++, so it grants no ability to construct or alter a snapshot. Recorded as a deliberate exception rather than left as a silent extra.
- [x] CHK044 Is any dead test scaffolding left behind? **Removed.** `NarrRailHostTest.h` / `.cpp` was an empty class carried in from the host project template, referenced by nothing, containing no automation test. Its name implied host test coverage that did not exist, which is worse than no file at all. Deleted.
- [x] CHK045 Is the host's non-atomicity across the two snapshots recorded rather than hidden? **Yes**, in `tasks.md` under "Known residual", and at the load-ordering comment in `NarrRailPlayerController.cpp`, with a stated trigger for revisiting.

## Notes

- Check items off as completed: `[x]`
- Add comments or findings inline
- Link the pull request and the issue when reporting
- Items are numbered sequentially for easy reference
