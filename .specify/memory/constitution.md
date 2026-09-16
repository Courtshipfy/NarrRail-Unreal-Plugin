<!--
Sync Impact Report
- Version change: (unratified template) -> 1.0.0
- Modified principles: n/a (initial adoption; template placeholders replaced)
- Added sections: Core Principles (I-V), Product Constraints, Development Workflow, Governance
- Removed sections: none
- Templates requiring updates: plan-template.md (no constitution references found),
  spec-template.md (none found), tasks-template.md (none found) -- all verified, no edits needed
- Follow-up TODOs: none
-->

# NarrRail-Unreal-Plugin Constitution

## Core Principles

### I. Story Consumer Boundary (NON-NEGOTIABLE)
This repository is a Story Consumer. It reads story files authored by the main NarrRail
product, maps them into Unreal assets, and executes them at runtime. It MUST NOT grow into
a second authoring surface. Authoring-only workflows — story outline creation, script
conversion, import package review, Story Project management, and project-level review
queues — belong to the main NarrRail repository and MUST NOT be implemented here.

*Rationale*: the product split made the authoring product and the Unreal runtime two
separate surfaces with separate users. Re-absorbing authoring work here silently reverses
that decision and recreates the duplicate-product problem the split removed.

### II. Neutral Format Compatibility, Declared Not Assumed
The main repository owns the neutral `.nrstory`, GlobalConfig, and `.nroutline` contracts.
This repository MUST declare which contract version it is compatible with, and MUST NOT
introduce private `.nrstory`, GlobalConfig, or `.nroutline` semantics. Any change that
requires new or altered neutral-format semantics MUST first go through a format issue and
compatibility decision in the main repository, and only then be implemented here.

Compatibility claims MUST be explicit and versioned: plugin version, Unreal Engine version,
and supported `schemaVersion` values are recorded in `README.md` and
`Docs/04_narrrail_ue_host/UNREAL_PLUGIN_COMPATIBILITY.md`. A compatibility table entry MUST
NOT be widened without evidence that the claim holds.

*Rationale*: multiple Story Consumers exist, and each one inventing local format semantics
would make the neutral contract unenforceable.

### III. Runtime Semantics In C++, Blueprint Composes
State machine execution, condition evaluation, variable handling, persistence, and asset
validation MUST be implemented in C++. Blueprint MUST only compose calls against the
exposed API — it MUST NOT carry core execution rules. The C++-to-Blueprint surface MUST
stay a minimal, documented set rather than an open door to internals.

*Rationale*: core execution rules split across C++ and Blueprint become unverifiable and
untestable, and they drift between the plugin and the sample host.

### IV. Spec Before Large Execution
Large execution issues MUST move through a Spec Kit feature folder before implementation.
The spec MUST define scope, non-goals, user-facing behavior, data contracts, acceptance
criteria, plan, tasks, and a verification checklist. A specification is required when work
spans more than one surface (plugin + host + assets), introduces or changes a file or asset
contract, adds user-visible states or failure modes, or will split into several tasks.
Focused bug fixes, narrow docs edits, and small test-only improvements MAY implement
directly from the GitHub issue.

*Rationale*: the two surfaces of this repository (plugin and sample host) plus binary assets
make unplanned work expensive to verify and easy to leave half-migrated.

### V. Reviewable, Testable Changes
Changes that affect runtime behavior, persistence, validation, or the C++-to-Blueprint
surface MUST include automated tests at the appropriate seam. Where behavior is only
observable inside Unreal, the change MUST state how it was verified in a real Unreal
session, and MUST NOT claim verification that was not performed.

Binary assets (`.uasset`, `.umap`) MUST be edited in Unreal rather than text-patched, and a
change that alters an asset contract MUST be described explicitly in the issue or spec.

*Rationale*: binary assets and Unreal-only behavior are the two places where review cannot
see what actually changed.

## Product Constraints

- GitHub Issues in `Courtshipfy/NarrRail-Unreal-Plugin` are the source of truth for this
  repository's planning, assignment, and close state.
- `specs/` holds execution artifacts for large features. It does not replace GitHub Issues.
- Format contract changes are owned by the main repository. This repository follows them.
- `NarrRailUEHost/` is a sample and validation host, not a shipped product surface.
- The repository MUST NOT contain authoring-product code such as `NarrRailEditor/`.
- Unreal generated outputs (`Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`) and
  local IDE state MUST stay untracked.
- Legacy `.nrrail` and runtime `.nroutline` support remain unsupported unless a future issue
  reopens them with a compatibility decision.

## Development Workflow

1. Create or update GitHub issues in this repository before starting executable work.
2. For large execution issues, use `$speckit-specify`, `$speckit-plan`, `$speckit-tasks`, and
   `$speckit-implement`, naming the feature folder
   `specs/<four-digit-issue-number>-<short-name>`, and link that folder from the issue.
3. If the work requires a neutral-format change, open the format issue in the main
   repository first, then record the resulting compatibility decision here.
4. Run the relevant tests and build before closing an issue. State what was verified in
   Unreal when the behavior is runtime-visible.
5. Review implementation against both the GitHub issue and any spec folder.
6. Update `README.md` and `Docs/04_narrrail_ue_host/UNREAL_PLUGIN_COMPATIBILITY.md` when a
   compatibility claim changes.

## Governance

This constitution guides Spec Kit specifications, plans, tasks, and implementations in this
repository. Amendments require a commit that explains the product or engineering reason and
updates dependent documents when needed.

Versioning follows semantic versioning: MAJOR for backward-incompatible governance or
principle removal or redefinition, MINOR for a new principle or materially expanded
guidance, PATCH for clarifications and non-semantic refinements.

If a GitHub issue or generated spec conflicts with this constitution, the conflict MUST be
surfaced before implementation rather than resolved silently in code.

**Version**: 1.0.0 | **Ratified**: 2026-09-16 | **Last Amended**: 2026-09-16
