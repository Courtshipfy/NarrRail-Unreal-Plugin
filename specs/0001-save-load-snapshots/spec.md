# Feature Specification: Runtime save/load snapshots

**Feature Branch**: `spec/0001-save-load-snapshots`

**Created**: 2026-09-16

**Status**: Draft

**Input**: GitHub issue `Courtshipfy/NarrRail-Unreal-Plugin#1` — "Implement runtime save/load snapshots (Persistence layer)"

**Reference implementation**: `Courtshipfy/NarrRail@feature/narrrail-save-snapshots` (HEAD `2e3903f8a42711f54e9aa279135239387beab799`, base `70fcdb0b837c428cf2e8c5de812809aa27177ff9`). It is a starting point, not the contract. Where it lacks versioning or migration behavior, this spec requires adding it rather than reproducing the gap.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Resume a session exactly where it stopped (Priority: P1)

A player running a NarrRail story in Unreal reaches a branch point, saves, leaves, and comes back later. Loading must put the session back at the same node with the same variable state and the same branch bookkeeping, so that continuing produces the same story the player would have seen had they never stopped.

**Why this priority**: This is the entire reason a Persistence layer exists. Without a faithful resume, the feature has no value.

**Independent Test**: Run the sample host, advance the story into a node with modified variables, save to a slot, tear the session down, load the slot again, and assert the current node, global variables, and local variables match the pre-save state.

**Acceptance Scenarios**:

1. **Given** a live session at node `N` with global variables `V`, **When** the session state is saved to a slot and later restored, **Then** the restored session reports node `N` and variables equal to `V`.
2. **Given** a `MultiDialogue` node advanced to line index `i`, **When** saved and restored, **Then** the restored session resumes at line index `i`, not at the first line.
3. **Given** a `Choice` node where some options have already been consumed, **When** saved and restored, **Then** the restored session still knows which options were consumed and offers only the remaining ones.
4. **Given** a restored session, **When** the player advances the story, **Then** the next node is the same one the uninterrupted session would have reached.

---

### User Story 2 - Save and load from the host surface (Priority: P2)

A developer or player using the sample host can save the current session into a named slot and load it back through the exposed Blueprint/host API, with a simple in-game surface (save slot list) to choose the slot.

**Why this priority**: The runtime capability in User Story 1 is only usable once it is reachable from the host. It is separable: the session-level snapshot can be verified with automated tests before any UI exists.

**Independent Test**: From Blueprint in the sample host, call save, verify a slot is written; call load, verify the session resumes. Then verify the same via the save slot widget.

**Acceptance Scenarios**:

1. **Given** a running session in the host, **When** the save entry point is called with a slot name, **Then** the slot exists and contains the current session snapshot.
2. **Given** an existing slot, **When** the load entry point is called, **Then** the session resumes from that slot's snapshot.
3. **Given** no slot exists for the requested name, **When** load is called, **Then** the call fails explicitly and the current session is left untouched.
4. **Given** the save slot UI is open, **When** the player selects a saved slot, **Then** loading resumes the story in that slot.

---

### User Story 3 - Reject incompatible saves instead of resuming corrupt state (Priority: P2)

A save written by a different snapshot layout must not be silently applied. Loading an unsupported snapshot must fail loudly and leave the running session unchanged.

**Why this priority**: The architecture already requires "version migration and exception recovery" for Persistence. Silent partial restores produce story states that no author designed, which is worse than a visible failure.

**Independent Test**: Construct a snapshot declaring an unsupported version, attempt to restore it, and assert the restore reports failure and the session state is unchanged.

**Acceptance Scenarios**:

1. **Given** a snapshot whose version the runtime does not support, **When** restore is attempted, **Then** restore reports failure and no session state changes.
2. **Given** a snapshot missing a required field for its declared version, **When** restore is attempted, **Then** restore reports failure and no session state changes.
3. **Given** a snapshot written by the currently supported version, **When** restored, **Then** restore succeeds.

---

### Edge Cases

- Save requested with no active session: MUST fail explicitly rather than write an empty snapshot.
- Load requested while a typewriter animation is still revealing text: MUST settle the presenter into a defined state rather than resume mid-animation.
- A restored node id no longer exists in the story asset (story was re-authored or re-exported): MUST be reported as a failure with the offending node identified, not treated as "start of story".
- Story asset loaded at restore time differs from the asset the snapshot was taken against: MUST be detectable; the resolution policy is an open question (see FR-009).
- Saving MUST NOT mutate the running session; a save followed immediately by continued play MUST behave identically to play without a save.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The runtime MUST capture the complete resumable state of a session as a single snapshot value, covering at minimum: current node, global variables, local variables, current `MultiDialogue` line index, last-choice information, and consumed `Choice` selections.
- **FR-002**: The runtime MUST restore a snapshot into a live session such that subsequent advancement is indistinguishable from an uninterrupted session.
- **FR-003**: Snapshots MUST carry an explicit version. A snapshot whose version is not supported MUST be rejected with an explicit failure, and MUST NOT be partially applied.
- **FR-004**: Snapshot capture and restore MUST be implemented in C++ and exposed as a single call each. Blueprint MUST NOT be required to reconstruct or reassemble session state.
- **FR-005**: The host MUST persist a snapshot to a named, user-indexed save slot and read it back.
- **FR-006**: Saving MUST NOT alter the running session's observable state.
- **FR-007**: The C++-to-Blueprint surface for this feature MUST stay minimal: snapshot capture and restore on the session, and slot save and load on the host.
- **FR-008**: Restoring a snapshot MUST leave the session unchanged when it fails, so that a failed load cannot destroy in-progress play.
- **FR-009**: [NEEDS CLARIFICATION: policy for a snapshot taken against a different story asset — reject always, or reject only when the referenced node is missing?]
- **FR-010**: [NEEDS CLARIFICATION: forward-compatibility policy for older snapshot versions — reject outright, or migrate forward one version at a time?]
- **FR-011**: Where behavior depends on the Unreal presenter or typewriter state, restore MUST define the presenter's resulting state rather than inheriting an animation-in-progress state.

### Key Entities *(include if feature involves data)*

- **Session snapshot**: the resumable state of one running story session. Key attributes: version, current node reference, global variable map, local variable map, multi-dialogue line index, last-choice information, consumed choice selections. See `data-model.md`.
- **Save slot**: a named, user-indexed persistence target holding one serialized session snapshot.
- **Snapshot version**: an integer declaring the snapshot layout, used to gate restore.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Automated round-trip tests show the restored session matching the saved session's node and variables in 100% of covered cases, including a mid-`MultiDialogue` case and a partially-consumed `Choice` case.
- **SC-002**: Saving during active play leaves observable session state identical, verified by a test that saves and then compares state.
- **SC-003**: An unsupported or malformed snapshot produces an explicit failure and zero session mutation, in 100% of rejection tests.
- **SC-004**: The snapshot round trip is exercised by automated tests that run in the repository's existing test flow, with no manual step required.
- **SC-005**: `README.md` compatibility claims and `Docs/04_narrrail_ue_host/UNREAL_PLUGIN_COMPATIBILITY.md` are updated to state the persistence capability and its snapshot version support.

## Assumptions

- Target is the existing technical preview shape: plugin `0.1.0-beta` on Unreal Engine `5.7`.
- The sample host `NarrRailUEHost/` is a validation and delivery surface for this capability, not a shipped product.
- This feature requires no change to the neutral `.nrstory`, GlobalConfig, or `.nroutline` contracts and therefore needs no main-repository format decision.
- Save slots are local to the machine; cloud or multi-device synchronisation is out of scope.
- The `Docs/01_architecture/TECH_ARCHITECTURE.md` requirement row `NR-RUN-006-*` ("存档恢复") describes this capability and should move from "规划中" once implemented.
- Existing UE assets from the reference branch are indicative; assets that conflict with the current repository state must be reconciled in the editor rather than copied blindly.
