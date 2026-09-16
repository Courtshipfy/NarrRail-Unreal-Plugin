# Data Model: Runtime save/load snapshots

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

## Entities

### Session snapshot

The complete resumable state of one running story session. Produced by the session, consumed by the
session. Not a story-format artifact — it never appears inside a `.nrstory`, GlobalConfig, or
`.nroutline` file.

Enumerated during T001 from `Courtshipfy/NarrRail@feature/narrrail-save-snapshots`
(`NarrRail/Source/NarrRail/Public/Runtime/NarrRailStorySession.h`). **Fourteen stored fields.** The
"Content-bound" column is what the FR-009 consistency gate has to defend; the "Purpose" column is
the reason the field is kept at all, which T005 uses to decide Blueprint visibility.

| # | Field | Type | Purpose | Content-bound | Blueprint |
|---|-------|------|---------|---------------|-----------|
| 1 | `SnapshotVersion` | `int32`, default `1` | Layout version for the version gate. | no | should not be writable by callers |
| 2 | `StoryId` | `FName` | Identity check against the loaded asset's `StoryId`. | no (identity) | no |
| 3 | `StoryAssetPath` | `FSoftObjectPath` | Identity check against the loaded story asset. | no (identity) | no |
| 4 | `GlobalConfigPath` | `FSoftObjectPath` | Identity check against the applied global config. | no (identity) | no |
| 5 | `SessionState` | `ENarrRailSessionState` | Which lifecycle state the session was in. | no | no |
| 6 | `StateBeforePause` | `ENarrRailSessionState` | The state to return to on `Resume()` after a restore mid-pause. | no | no |
| 7 | `CurrentNodeId` | `FName` | The node the session is sitting on at capture time. Stable id, not a positional index. | **yes** — must resolve | no |
| 8 | `NodeHistory` | `TArray<FName>` | Visited-node trail, used by history-dependent logic and the debugger. | node ids, **not yet validated** | no |
| 9 | `EmittedEvents` | `TArray<FName>` | Events already raised, so a restore does not re-raise them. | no (event ids) | no |
| 10 | `LocalVariableSnapshot` | `TMap<FName, FString>` | Session-local variables. Global variables are **not** here — see the separate entity below. | names bind to the story's local variable definitions | no |
| 11 | `CurrentMultiDialogueLineIndex` | `int32`, sentinel `INDEX_NONE` | How far into a `MultiDialogue` node the session has advanced. | **yes** — line range | no |
| 12 | `LastChoiceInfo` | `FNarrRailLastChoiceInfo` (5 sub-fields, below) | Which choice was taken last; read by branch and presenter logic. | **yes** — node id and index | no |
| 13 | `ExhaustiveChoiceSelections` | `TArray<FNarrRailChoiceSelectionSnapshot>` | Canonical form of "which options are already consumed" for every `Choice` node that has been partially consumed. | **yes** — node ids and indices | no |
| 14 | `ExhaustivePendingChoiceReturnStack` | `TArray<FName>` | Return stack for nested exhaustive-choice branches. | node ids, **not yet validated** | no |

**FR-007 narrowing note (for T005).** The reference implementation marks every one of these fields
`BlueprintReadWrite`. That is broader than FR-007 allows: a Blueprint authoring caller could set
`SnapshotVersion`, or splice together a snapshot from field assignments, which is exactly what the
version gate and the consistency gate exist to prevent. The port keeps `GetSessionSnapshot` as
`BlueprintPure` and `RestoreSessionSnapshot` as `BlueprintCallable`, and drops Blueprint write access
from the struct fields. A snapshot is only ever produced by capture and only ever consumed by
restore.

#### Struct field: `FNarrRailLastChoiceInfo`

Declared in `NarrRailStoryTypes.h`, kept as a distinct struct because it is also part of the public
runtime API (`GetLastChoice()`), not only of the snapshot.

| Field | Type | Meaning |
|-------|------|---------|
| `ChoiceNodeId` | `FName` | The `Choice` node the selection happened on. |
| `ChoiceIndex` | `int32`, `-1` when unset | The raw option index that was taken. |
| `TargetNodeId` | `FName` | The node the selection led to. |
| `ChoiceTextKey` | `FString` | Localisation key of the chosen option, kept for the presenter. |
| `bValid` | `bool` | Whether any choice has been recorded yet. |

#### Not stored — rebuilt at restore

These are runtime caches in the reference implementation. They are deliberately absent from the
snapshot; recording them would double the state and create a second source of truth that can
disagree with the first. The port MUST rebuild rather than persist them.

| Member | Type | Why it is not stored | Rebuilt how |
|--------|------|----------------------|-------------|
| `ExhaustiveSelectedChoiceIndices` | `TMap<FName, TSet<int32>>` | UHT does not support a container type as a `TMap` value, so it cannot carry a `UPROPERTY` and therefore cannot serialise. This is the mechanical reason #13 exists at all. | Rebuilt from `ExhaustiveChoiceSelections`, dropping `ChoiceNodeId == NAME_None` entries and negative indices. |
| `RuntimeVisibleChoiceIndexMap` | `TMap<FName, TArray<int32>>` | Derived from the resolved node's options and the consumed set; storing it would let it go stale against a re-resolved node. | `Reset()`, then repopulated only when the restored current node is a `Choice`. |
| `Context.VariableSnapshot` | `TMap<FName, FString>` | A merged view of global + local variables. | Recomputed by `SyncVariableSnapshotToContext()` after the local container is restored. |

#### Call parameter, not stored

`RestoreSessionSnapshot(..., bool bRefreshPresenter = true)` — whether restore re-drives the
presenter after restoring state. Satisfies FR-011: the caller decides the presenter's resulting
state rather than inheriting an in-progress animation.

### Global state snapshot

Global variables outlive a single session, so they are **not** part of the session snapshot. They
live in `UNarrRailGlobalStateSubsystem` and serialise through their own value type, which carries its
own independent `SnapshotVersion`.

Enumerated during T001 from `NarrRail/Source/NarrRail/Public/Runtime/NarrRailGlobalStateSubsystem.h`.

| # | Field | Type | Meaning | Content-bound |
|---|-------|------|---------|---------------|
| 1 | `SnapshotVersion` | `int32`, default `1` | Layout version for the global snapshot, independent of the session snapshot's version. | no |
| 2 | `AppliedGlobalConfigPaths` | `TArray<FSoftObjectPath>` | Which global configs have been applied, so a restore does not re-apply them and clobber restored values. | **yes** — paths must resolve |
| 3 | `GlobalVariableSnapshot` | `TMap<FName, FString>` | The global variables themselves. | names bind to the global config's variable definitions |

**Correction to an earlier draft.** A prior version of this table claimed global variable state was
carried by `VariableSnapshot` on the session context, and marked it "yes / confirmed". That is wrong
on both counts: `Context.VariableSnapshot` is a derived cache (see above), and the actual persisted
global state is `GlobalVariableSnapshot` on this separate struct. Recorded here so the error is not
reintroduced.

#### Global snapshot restore semantics

`RestoreGlobalStateSnapshot` restores definitions and values, not just values: a global variable that
the current global config declares but the snapshot does not mention still has to exist afterwards
with a default value. This is why `AppliedGlobalConfigPaths` is part of the snapshot rather than
being taken from the live subsystem.

### Choice selection record

`FNarrRailChoiceSelectionSnapshot` — **one entry per `Choice` node** that has been partially
consumed, not one entry per option. A single option-specific record would make a node with N taken
options cost N entries and would lose the fact that they belong together.

| Field | Type | Meaning |
|-------|------|---------|
| `ChoiceNodeId` | `FName` | The `Choice` node this record is about. `NAME_None` entries are ignored on restore. |
| `SelectedChoiceIndices` | `TArray<int32>` | Raw option indices already consumed on that node, **sorted ascending** by capture. Negative indices are ignored on restore. |

The sorted-array form is deliberate: it makes two captures of the same state compare equal, which is
what the "capture does not mutate state" test (T013) relies on. The in-memory `TSet` has no defined
iteration order, so serialising it directly would produce spuriously unequal snapshots.

### Save slot

A named, user-indexed persistence target holding exactly one serialized session snapshot **and** one
serialized global state snapshot.

| Field | Meaning |
|-------|---------|
| Slot name | Caller-provided name; reference host defaults to a demo slot name |
| User index | Platform user index, so slots are per-user |
| Payload | One `Session snapshot` plus one `Global state snapshot` |

Backed by `UNarrRailHostSaveGame : USaveGame` in the host module. The runtime module does not know
about slots; it only produces and consumes snapshots.

| Field | Type | Meaning |
|-------|------|---------|
| `SessionSnapshot` | `FNarrRailStorySessionSnapshot` | The session snapshot. |
| `GlobalStateSnapshot` | `FNarrRailGlobalStateSnapshot` | The global state snapshot. Co-located so one slot restores consistently. |
| `SavedAtUtc` | `FString` | Display-only timestamp. **Not** read by any restore path — it must never gate a restore. |

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

5. **Consistency gate (FR-009, decided 2026-09-16; scope extended same day during T001).** After the
   identity checks pass and before any state is written, restore MUST verify that state bound to node
   content still matches the loaded node:
   - the multi-dialogue line index is within the resolved node's line range, or holds the sentinel;
   - every node id referenced by a consumed choice record still resolves;
   - every consumed option index is within that node's option count;
   - every node id on the exhaustive-choice return stack still resolves.

   Any failure MUST reject the restore and leave the session unchanged, consistent with invariant 2.

   *Rationale*: node identity is a stable `FName` id, so ordinary authoring — editing text, adding
   nodes, reordering the graph — does not invalidate a save. Only a structural mismatch does, and
   those checks are where a structural mismatch actually surfaces.

6. **Gate scope: dereferenced node ids only, never merely recorded ones.** The return-stack check was
   added during T001 after reading the reference implementation; `NodeHistory` was deliberately left
   ungated. The rule that separates them:

   - `CurrentNodeId`, the consumed choice records, and the return stack are **dereferenced** by the
     runtime after a restore — they are passed to node lookup, so a dangling id produces a failure.
   - `NodeHistory` is **recorded** and never resolved inside the runtime (`GetHistory()` hands the raw
     list to the caller). History is also append-only, so a node deleted after it was visited stays in
     history forever.

   Gating on recorded ids would reject a save after an ordinary authoring edit — precisely the
   behaviour the FR-009 rationale rejects — and would do so for no behavioural gain. Gating on
   dereferenced ids catches the one case that matters.

   *Evidence for the return-stack check.* The reference implementation pops the stack in
   `TryPopExhaustiveReturn` and feeds the result straight to `AdvanceToNode`. If that node was
   deleted, `AdvanceToNode` sets `SessionState = Error` and returns `MissingNode` — but only at the
   moment the branch ends, which can be many interactions after the restore. The restore itself
   reports success, so the failure surfaces far from its cause. `AdvanceToNode` bails out before
   assigning `Context.CurrentNodeId`, so the state degrades to `Error` rather than being corrupted —
   survivable, but the opposite of FR-008's "reject what it cannot restore". Rejecting at restore
   moves the failure to where the cause is.
