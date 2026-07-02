# Styio / Contracts Runbook

**Purpose:** Provide the daily-work entrypoint for `pafio` maintainers of external compiler contracts, compatibility boundaries, and compiler-facing handoff docs.

**Last updated:** 2026-06-29

## Mission

Own `pafio`'s published external compiler contract for `binary` mode,
controlled source-build handoff rules for `build` mode, local Styio environment
optimization contracts, and the client side of the `pafio` migration
without letting any path drift into undocumented behavior.

## Owned Surface

1. `contracts/`
2. `docs/external/for-styio/`
3. `docs/governance/Pafio-CLI-Contract.md`
4. `scripts/styio-interface-gate.py`
5. `scripts/preflight-readiness-check.py`
6. `docs/planning/Pafio-Styio-Cloud-Migration-Handoff.md`
7. `docs/governance/Pafio-Local-Offline-Package-Contract.md`

## Daily Workflow

1. Treat published compiler interaction as a machine contract, not an internal source dependency.
2. Treat source-build mode as a separate documented contract with explicit source origin, branch-channel mapping, revision, cache rules, installer bootstrap semantics, and platform compatibility semantics, and keep machine-readable sync/graph/tool-status/cloud-plan entrypoints aligned with that contract vocabulary.
3. Keep handoff docs and interface gates aligned in the same checkpoint.
4. Use `--styio-bin` health legs when validating the published binary path.
5. Treat compile-plan v1 as live only when `styio --machine-info=json`, `contracts/compat/styio-support.toml`, and the black-box interop gate all agree.
6. Keep the source-build doc needles exact for the cross-repo gate: official origin, `release`/`stable`/`nightly` branch mapping, `pafio build minimal`, `pafio-toolchain.lock`, and the binary compatibility-matrix bypass statement must all remain visible in `Pafio-CLI-Contract.md`.
7. Keep hosted workspace, registry server control-plane, compile-platform,
   mirror synchronization, and cloud-service ownership in `pafio`;
   this repo documents the package-manager client contract, offline package
   behavior, local compiler environment, and compatibility expectations.
8. Keep client/server HTTP contracts as native JSON packages only; `pafio` gates must reject generated third-party API-description artifacts and stale route references.
9. Keep the `pafio` cloud-plan submit target aligned with `pafio`'s `submitJob` contract route, currently `POST /api/pafio/v1/jobs`.
10. Keep `contracts/registry-control-plane/v1/` byte-aligned with the platform copy when only README/example governance wording changes; if JSON route shape changes, coordinate both repositories before claiming compatibility.
11. Treat any branch outside `release`, `stable`, and `nightly` as an explicit temporary source-build ref. Contract docs must not assign special compiler handoff meaning to a temporary branch name.

## Change Classes

1. Small: compatibility doc wording, source-build needle wording, or fixture updates.
2. Medium: handshake fields, compile-plan consumer expectations, source-build fetch rules, installer bootstrap JSON fields, platform compatibility JSON fields, native JSON contract package updates, or CLI JSON contract updates.
3. High: compatibility phase changes, official source origin rules, public machine contract expansion, registry-control-plane route changes, or platform service ownership changes.

## Required Gates

```bash
./scripts/checkpoint-health.sh --styio-bin /absolute/path/to/styio
python3 scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio --pafio-bin ./build-codex/bin/pafio --require-compile-plan --json
```

## Cross-Team Dependencies

1. Core / Workflow reviews changes that alter build/check/run/test orchestration, installer bootstrap behavior, or project-local toolchain state behavior.
2. Docs / Delivery reviews cross-repo doc or gate entrypoint changes.

## Handoff / Recovery

Record the exact published external compiler binary or source revision, compatibility phase, supported compile-plan versions, failing contract command, and whether the next service-side action belongs in `pafio`.
