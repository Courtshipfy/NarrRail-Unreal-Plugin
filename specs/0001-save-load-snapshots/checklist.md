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
- [ ] CHK012 Is a snapshot with a version older than supported handled according to the policy recorded in `data-model.md`?
- [ ] CHK013 Is a snapshot with an absent or malformed version rejected with an explicit failure?
- [ ] CHK014 Is the session state provably unchanged on every restore failure path, not merely on the ones that are easy to test?
- [ ] CHK015 Does an unresolvable node reference produce a failure that names the offending node?
- [ ] CHK016 Are FR-009 and FR-010 answered and recorded, rather than left as open questions at implementation time?

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

- [ ] CHK032 Does the automated suite include capture, restore, rejection, no-mutation-on-save, and unresolvable-node cases, and did the suite actually run?
- [ ] CHK033 Were the manual Unreal steps in `quickstart.md` performed, or explicitly reported as not performed?
- [ ] CHK034 Does the closing report state which verification steps were executed and which were not, without claiming anything unverified?

## Notes

- Check items off as completed: `[x]`
- Add comments or findings inline
- Link the pull request and the issue when reporting
- Items are numbered sequentially for easy reference
