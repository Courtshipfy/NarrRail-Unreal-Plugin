# Issue tracker: GitHub

Issues and PRDs for this repo live as GitHub issues. Use the `gh` CLI for all operations.

## Conventions

- **Create an issue**: `gh issue create --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue**: `gh issue view <number> --comments`, and also fetch labels.
- **List issues**: `gh issue list --state open --json number,title,body,labels --jq '[.[] | {number, title, labels: [.labels[].name]}]'` with appropriate `--label` and `--state` filters.
- **Comment on an issue**: `gh issue comment <number> --body "..."`
- **Apply / remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> --comment "..."`

Infer the repo from `git remote -v` — `gh` does this automatically when run inside a clone.

## Relationship to the main repository

This repository tracks its own work. The neutral story format contracts are owned by
`Courtshipfy/NarrRail`, so a format change is an issue **there**, not here. Link the two
rather than duplicating the discussion.

## Spec-kit execution specs

GitHub Issues remain the tracker source of truth. For large execution issues, create a
spec-kit-compatible folder under `specs/<four-digit-issue-number>-<short-name>/` and link
that folder from the issue. See `Docs/agents/spec-kit.md`.

Use the issue for assignment, discussion, labels, and close state. Use the spec folder for
feature spec, implementation plan, task breakdown, and verification checklist.
