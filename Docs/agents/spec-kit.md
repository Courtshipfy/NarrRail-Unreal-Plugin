# Spec-Kit Workflow

This repository uses GitHub Issues as the source of truth for work selection, and official Spec Kit project scaffolding for larger execution specs. It was initialized with GitHub Spec Kit `specify` CLI `0.12.11` using the Codex skill integration.

Reference: GitHub Spec Kit is the upstream "Spec-Driven Development" toolkit at <https://github.com/github/spec-kit>.

## Local CLI

Recommended local install:

```bash
brew install uv
uv tool install specify-cli --from git+https://github.com/github/spec-kit.git@v0.12.11
specify version
```

The official project files live in:

- `.specify/`: Spec Kit scripts, templates, workflow metadata, and this repository's constitution memory.
- `.agents/skills/speckit-*`: Codex skill integration for Spec Kit commands.
- `specs/`: feature specs generated or maintained for large execution issues.

## When To Use This

Use a spec before implementation when an issue has any of these traits:

- It spans more than one surface, such as plugin runtime + sample host, or C++ + Blueprint assets.
- It introduces or changes a file contract, asset contract, persistence format, or a cross-repository compatibility claim.
- It has multiple user-visible states, failure modes, or in-editor workflows.
- It will likely split into several implementation tasks.

Implement directly from the issue when the work is small and concrete:

- A focused docs update.
- A single-module refactor with clear acceptance criteria.
- A narrow bug fix.
- A test-only improvement.

Before planning a large feature, make sure `.specify/memory/constitution.md` still reflects the current repository direction.

## Issue To Spec Mapping

Each large execution issue gets one feature folder:

```text
specs/
  0001-save-load-snapshots/
    spec.md
    plan.md
    tasks.md
    checklist.md
```

Use the four-digit GitHub issue number plus a short kebab-case name. The issue body should link to the feature folder once it exists.

## Workflow

1. **Select the GitHub issue.**
   Read the issue body and comments, plus any linked spec or compatibility decision, before creating the spec.

2. **Create the feature folder.**
   Prefer the official Spec Kit skills, such as `$speckit-specify`, `$speckit-plan`, and `$speckit-tasks`. For existing GitHub issues, pass or persist `SPECIFY_FEATURE_DIRECTORY=specs/<four-digit-issue-number>-<short-name>` so the folder stays aligned with the issue number. When creating folders manually, use the same convention.

3. **Write `spec.md`.**
   Capture user-facing behavior, scope, non-goals, data and asset contracts, acceptance criteria, and open questions.

4. **Write `plan.md`.**
   Identify affected modules, data flow, integration seams, test seams, rollout order, and risks.

5. **Write `tasks.md`.**
   Break the plan into agent-sized implementation tasks with dependencies. A task should be small enough to commit and verify independently.

6. **Write `checklist.md`.**
   Turn the issue acceptance criteria and plan risks into a verification checklist.

7. **Then implement.**
   Use the `implement` skill against the issue plus its spec folder. Keep code changes aligned with the spec.

## Cross-Repository Format Changes

This repository does not own the neutral story format contracts. If a feature needs a `.nrstory`, GlobalConfig, or `.nroutline` semantics change:

1. Open a format issue in `Courtshipfy/NarrRail` first.
2. Wait for the compatibility and migration decision there.
3. Record the resulting compatibility claim in this repository's `README.md` and `Docs/04_narrrail_ue_host/UNREAL_PLUGIN_COMPATIBILITY.md`.
4. Only then implement the consumer side here.

## What Not To Do

- Do not force tiny bug fixes through a spec folder.
- Do not implement authoring-product workflows here; they belong to `Courtshipfy/NarrRail`.
- Do not duplicate long issue histories. Link to issues and compatibility decisions.
- Do not treat a spec as code ownership. The GitHub issue remains the tracker item; the spec is the execution contract.
