# Audit

**Purpose:** Define the repository-local audit queue for security, correctness, and design defects discovered before they are converted into normal tracked work.

**Last updated:** 2026-05-13

## Defect Queue

Active audit records live under `docs/audit/defects/`. Markdown defect records are tracked when they represent deliberate repository-owned work; large exploratory audit scratch output should stay out of this directory unless it is ready to block the audit gate.

Generated inventory: [INDEX.md](./INDEX.md).

Reviewers and audit gates enforce this rule when an audit is performed:

1. Missing or empty `docs/audit/defects/` passes.
2. Every markdown record in `docs/audit/defects/` must declare `**Status:** Closed`, `Resolved`, or `Cleared`.
3. Closed records must include non-empty `**Closure evidence:** ...`.
4. Any open, malformed, or non-markdown record blocks the external audit result.

Move durable findings into tracked planning, issue, test, or implementation work once the record is ready for long-term ownership.
