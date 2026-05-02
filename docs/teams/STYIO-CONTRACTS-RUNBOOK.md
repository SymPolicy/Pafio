# Styio / Contracts Runbook

**Purpose:** Provide the daily-work entrypoint for `spio` maintainers of external compiler contracts, compatibility boundaries, and compiler-facing handoff docs.

**Last updated:** 2026-05-02

## Mission

Own `spio`'s published external compiler contract for `binary` mode,
controlled source-build handoff rules for `build` mode, local Styio environment
optimization contracts, and the client side of the `styio-platform` migration
without letting any path drift into undocumented behavior.

## Owned Surface

1. `contracts/`
2. `docs/external/for-styio/`
3. `docs/governance/Spio-CLI-Contract.md`
4. `scripts/styio-interface-gate.py`
5. `scripts/preflight-readiness-check.py`
6. `docs/planning/Spio-Platform-Migration-Handoff.md`
7. `docs/governance/Spio-Local-Offline-Package-Contract.md`

## Daily Workflow

1. Treat published compiler interaction as a machine contract, not an internal source dependency.
2. Treat source-build mode as a separate documented contract with explicit source origin, branch-channel mapping, revision, cache rules, installer bootstrap semantics, and platform compatibility semantics, and keep machine-readable sync/graph/tool-status/cloud-plan entrypoints aligned with that contract vocabulary.
3. Keep handoff docs and interface gates aligned in the same checkpoint.
4. Use `--styio-bin` health legs when validating the published binary path.
5. Treat compile-plan v1 as live only when `styio --machine-info=json`, `contracts/compat/styio-support.toml`, and the black-box interop gate all agree.
6. Keep the source-build doc needles exact for the cross-repo gate: official origin, `stable`/`nightly` branch mapping, `spio build minimal`, `spio-toolchain.lock`, and the binary compatibility-matrix bypass statement must all remain visible in `Spio-CLI-Contract.md`.
7. Keep hosted workspace, registry server control-plane, compile-platform,
   mirror synchronization, and cloud-service ownership in `styio-platform`;
   this repo documents the package-manager client contract, offline package
   behavior, local compiler environment, and compatibility expectations.
8. Keep client/server HTTP contracts as native JSON packages only; `spio` gates must reject generated third-party API-description artifacts and stale route references.
9. Keep the `spio` cloud-plan submit target aligned with `styio-platform`'s `submitJob` contract route, currently `POST /api/styio-platform/v1/jobs`.
10. Keep `contracts/registry-control-plane/v1/` byte-aligned with the platform copy when only README/example governance wording changes; if JSON route shape changes, coordinate both repositories before claiming compatibility.
11. Keep `registryDescriptor` as the client/server trust handoff operation:
    `styio-platform` owns descriptor issuance and `styio-spio` owns descriptor
    import, pin storage, and remote fetch enforcement.

## Change Classes

1. Small: compatibility doc wording, source-build needle wording, or fixture updates.
2. Medium: handshake fields, compile-plan consumer expectations, source-build fetch rules, installer bootstrap JSON fields, platform compatibility JSON fields, native JSON contract package updates, or CLI JSON contract updates.
3. High: compatibility phase changes, official source origin rules, public machine contract expansion, registry-control-plane route changes, or platform service ownership changes.

## Required Gates

```bash
./scripts/checkpoint-health.sh --styio-bin /absolute/path/to/styio
python3 scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio --spio-bin ./build-codex/bin/spio --require-compile-plan --json
python3 tests/interop/registry-control-plane-contract-gate.py
```

## Cross-Team Dependencies

1. Core / Workflow reviews changes that alter build/check/run/test orchestration, installer bootstrap behavior, or project-local toolchain state behavior.
2. Docs / Delivery reviews cross-repo doc or gate entrypoint changes.

## Handoff / Recovery

Record the exact published external compiler binary or source revision, compatibility phase, supported compile-plan versions, failing contract command, and whether the next service-side action belongs in `styio-platform`.
