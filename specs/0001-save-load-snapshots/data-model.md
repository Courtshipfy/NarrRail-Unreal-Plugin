# Data Model: Runtime save/load snapshots

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

## Entities

### Session snapshot

The complete resumable state of one running story session. Produced by the session, consumed by the
session. Not a story-format artifact — it never appears inside a `.nrstory`, GlobalConfig, or
`.nroutline` file.

| Field | Type | Meaning | Confirmed |
|-------|------|---------|-----------|
| Snapshot version | `int32` | Layout version, used to gate restore. Reference implementation starts at `1`. | yes |
| Current node | node reference | The node the session is sitting on at capture time. | to confirm exact member name |
| Global variable state | `TMap<FName, FString>` | Global variables as seen by the session context. | yes (`VariableSnapshot` on the session context) |
| Local variable state | `TMap<FName, FString>` | Session-local variables. | yes (`LocalVariableSnapshot`) |
| Multi-dialogue line index | `int32`, sentinel `INDEX_NONE` | How far into a `MultiDialogue` node the session has advanced. | yes (`CurrentMultiDialogueLineIndex`) |
| Last choice information | `FNarrRailLastChoiceInfo` | Which choice was taken last, needed by branch and presenter logic. | yes (`LastChoiceInfo`) |
| Consumed choice selections | `TArray<FNarrRailChoiceSelectionSnapshot>` | Per-choice bookkeeping for nodes whose options are consumed as they are picked. | yes (`ExhaustiveChoiceSelections`) |
| Presenter refresh intent | `bool` (call parameter, not stored) | Whether restore re-drives the presenter after restoring state. | yes (`RestoreSessionSnapshot(..., bool bRefreshPresenter)`) |

The reference implementation carries roughly twelve stored fields in total. The table records the
fields verified during porting analysis; the remaining fields MUST be enumerated from the reference
implementation at the start of implementation and either kept, with a stated reason, or dropped in
favour of the minimal surface required by FR-007.

### Choice selection record

One entry per consumed `Choice` option, so a restored session still offers only the options the
player has not yet taken.

| Field | Meaning | Confirmed |
|-------|---------|-----------|
| Choice identity | Which node/option the record refers to. | to confirm exact member names |
| Selection state | Whether the option has been consumed. | to confirm exact member names |

The reference implementation declares `FNarrRailChoiceSelectionSnapshot` with two properties. Their
exact names and types MUST be read from the reference implementation during T003.

### Save slot

A named, user-indexed persistence target holding exactly one serialized session snapshot.

| Field | Meaning |
|-------|---------|
| Slot name | Caller-provided name; reference host defaults to a demo slot name |
| User index | Platform user index, so slots are per-user |
| Payload | One serialized `Session snapshot` |

Backed by a `USaveGame` subclass in the host module (`NarrRailHostSaveGame`). The runtime module
does not know about slots; it only produces and consumes snapshots.

## Versioning decision

`SnapshotVersion` MUST be an independent counter, unrelated to the story `schemaVersion` declared in
the neutral format contract.

Rationale: the story schema version describes authored content that the main repository owns. The
snapshot version describes a runtime-internal save layout that this repository owns. Coupling them
would force a snapshot version bump whenever the story format changes, and would drag a
main-repository decision into a purely local save format.

Restore behaviour by version:

| Snapshot version | Behaviour |
|------------------|-----------|
| Equal to the supported version | Restore proceeds. |
| Less than the supported version | Reject with an explicit failure, and leave the session unchanged. No migration path is implemented; see the FR-010 decision below. |
| Greater than the supported version | Reject with an explicit failure, and leave the session unchanged. |
| Absent or malformed | Reject with an explicit failure, and leave the session unchanged. |

### FR-010 decision: strict rejection with a dispatch seam

**Decided 2026-09-16.** The runtime accepts a snapshot only when `SnapshotVersion` equals the
supported version. Every other value — older, newer, absent, or malformed — is rejected with an
explicit failure and no state change.

Forward migration is deliberately NOT implemented. At the time of the decision only version 1
exists and no real save has been produced, so migration code would be written and tested against
nothing. Shipped migration logic is also a common source of silent misreads, which is exactly what
FR-003 and FR-008 exist to prevent.

The obligation this decision places on the implementation is structural, not behavioural: the
restore path MUST be a per-version dispatch with exactly one supported branch today. Adding a
migration later is then a new branch, not a restructure of restore.

**Trigger for revisiting**: the first time real saves must survive a snapshot layout change. At that
point add one migration step (N to N+1) per version gap, with a test per step.

### FR-009 decision: consistency gate over a content fingerprint

**Decided 2026-09-16.** The story identity checks are kept, and a consistency gate is added over
state that is bound to node content. See invariant 5 below for the exact checks.

The alternative — recording a content fingerprint of the story asset and refusing whenever it
changes — was rejected. Node identity is a stable `FName` id rather than a positional index, so
editing dialogue text, adding nodes, reordering the graph, and re-exporting the story all leave
existing saves valid. A fingerprint would refuse in all of those ordinary cases, forcing creators to
replay, and it would require a new import-time mechanism to generate and store the fingerprint at
all. It also would not distinguish an innocuous text edit from a genuinely breaking change.

Leaving the behaviour as it is was also rejected: a node whose type or length changed, or a choice
node that was deleted, currently restores an out-of-range or dangling state with no signal at all.

## Invariants

1. Capture MUST NOT mutate session state. A capture followed by continued play MUST be
   indistinguishable from play without a capture (FR-006).
2. Restore MUST be atomic with respect to failure: on any failure path the session state is exactly
   what it was before the restore attempt (FR-008).
3. Restore MUST define the presenter's resulting state rather than inheriting an in-progress
   animation state (FR-011).
4. A snapshot MUST NOT be applied when the node it references cannot be resolved in the loaded story
   asset; the failure MUST identify the offending node (edge case in spec.md).

5. **Consistency gate (FR-009, decided 2026-09-16).** After the identity checks pass and before any
   state is written, restore MUST verify that state bound to node content still matches the loaded
   node:
   - the multi-dialogue line index is within the resolved node's line range, or holds the sentinel;
   - every node id referenced by a consumed choice record still resolves;
   - every consumed option index is within that node's option count.

   Any failure MUST reject the restore and leave the session unchanged, consistent with invariant 2.

   *Rationale*: node identity is a stable `FName` id, so ordinary authoring — editing text, adding
   nodes, reordering the graph — does not invalidate a save. Only a structural mismatch does, and
   those three checks are where a structural mismatch actually surfaces.
