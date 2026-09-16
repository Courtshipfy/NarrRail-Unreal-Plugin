## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues for `Courtshipfy/NarrRail-Unreal-Plugin`. See `Docs/agents/issue-tracker.md`.

### Spec-driven execution

Large execution issues should use the repo's spec-kit-compatible workflow before implementation. See `Docs/agents/spec-kit.md`.

### Constitution

Project principles for specs, plans, and implementation live in `.specify/memory/constitution.md`. Read it before planning a large feature.

### Story Consumer boundary

This repository is a Story Consumer. It does not own the neutral `.nrstory`, GlobalConfig, or `.nroutline` contracts. Format changes go through `Courtshipfy/NarrRail` first. See `.specify/memory/constitution.md` principles I and II.
