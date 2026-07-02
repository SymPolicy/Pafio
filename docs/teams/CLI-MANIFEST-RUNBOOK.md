# CLI / Manifest Runbook

**Purpose:** Provide the daily-work entrypoint for maintainers of the `pafio` CLI surface, entry-argument routing, and manifest or lock semantics.

**Last updated:** 2026-04-16

## Mission

Own user-facing command shape, manifest and lock interpretation, and stable argument contracts. This team does not own resolver internals, registry transport, or external compiler compatibility policy.

## Owned Surface

Primary paths:

1. `src/PafioCLI/`
2. `src/PafioManifest/`
3. `src/PafioTool/`
4. `docs/governance/Pafio-CLI-Contract.md`
5. `docs/governance/Pafio-Entry-Argument-Index.md`
6. `docs/governance/Pafio-Manifest-and-Lock-Conventions.md`
7. `tests/unit/fixtures/`

Key SSOTs:

1. `CLI contract -> ../governance/Pafio-CLI-Contract.md`
2. `Argument index -> ../governance/Pafio-Entry-Argument-Index.md`
3. `Manifest and lock conventions -> ../governance/Pafio-Manifest-and-Lock-Conventions.md`

## Daily Workflow

1. Read the CLI and manifest owner docs before changing flags, JSON payloads, exit codes, or manifest rules.
2. Update fixtures and error payload expectations in the same change as implementation edits.
3. Check whether command output feeds resolver, workflow, or compatibility handoffs before finalizing naming or payload changes.
4. Run the smallest CLI-facing check first, then expand to submit-gate coverage when contract surface moved.

## Change Classes

1. Small: local wording, help text, or isolated manifest validation fix. Run targeted native and fixture checks.
2. Medium: new flag, changed manifest field handling, changed JSON error shape, or lockfile behavior change. Update governance docs and relevant fixtures.
3. High: CLI exit-code contract, command removal, default workflow path, or published manifest rule change. Use coordinator review and full submit coverage.

## Required Gates

Minimum:

```bash
./scripts/native-check.sh
./scripts/pafio --version
./scripts/pafio machine-info --json
```

Expanded:

```bash
python3 ./scripts/submit-gate.py --profile pre-push
```

## Cross-Team Dependencies

1. Resolve / Workflow must review changes that alter plan inputs, dependency graph interpretation, or workflow payloads.
2. Compat / Security must review changes that affect published machine-info or `styio` handshake expectations.
3. Delivery / Quality must review fixture or submit-gate expectation changes.
4. Docs / Governance must review normative contract edits.

## Handoff / Recovery

Record:

1. Commands, flags, or manifest fields changed.
2. Fixtures updated and gates already run.
3. JSON payload or exit-code deltas still pending.
4. Rollback point and next owner in `../planning/Pafio-Workstreams-and-TODOs.md` when work spans another checkpoint.
