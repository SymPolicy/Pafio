# Audit Specs

**Purpose:** Define the code-audit checklist and design-principle baseline every human reviewer and agent must apply before accepting implementation work.

**Last updated:** 2026-04-22

## Scope

This directory owns audit rules that apply across code, tests, docs, gates, and handoff contracts.

Start with [CODE-AUDIT-CHECKLIST.md](./CODE-AUDIT-CHECKLIST.md). Its first required check is whether the code follows the seven design principles.

The auditable-code framework is external and centralized in the sibling `General-Auditor` repository. This repository does not expose an audit interface; external auditors run `General-Auditor` against the worktree and load `modules/default` plus `for-pafio`.

Generated inventory: [INDEX.md](./INDEX.md).
