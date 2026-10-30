# Audit Specs

**Purpose:** Define the code-audit checklist and design-principle baseline every human reviewer and agent must apply before accepting implementation work.

**Last updated:** 2026-04-22

## Scope

This directory owns audit rules that apply across code, tests, docs, gates, and handoff contracts.

Start with [CODE-AUDIT-CHECKLIST.md](./CODE-AUDIT-CHECKLIST.md). Its first required check is whether the code follows the seven design principles.

Repository-local policy gates (`scripts/repo-hygiene-check.py`, `scripts/docs-audit.py`, `scripts/check_no_binaries.py`, `scripts/ecosystem-cli-doc-gate.py`) run in the `audit` workflow, and the publication gate applies the withheld-name, credential-shape, commit-identity, and workspace-hygiene contract to every outgoing range. This repository does not expose an audit interface.

Generated inventory: [INDEX.md](./INDEX.md).
