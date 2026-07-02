# Core / Workflow Runbook

**Purpose:** Provide the daily-work entrypoint for `pafio` core workflow maintainers covering the native CLI, manifests, lockfiles, resolver, offline package paths, local import/export, and build/test flow.

**Last updated:** 2026-05-02

## Mission

Own the local-first package-manager core and native workflow behavior, including
offline package use, local import/export, project-local toolchain mode
selection, source-build orchestration, and platform compatibility shims, without
redefining registry policy, platform service behavior, or published external
compiler contracts.

## Owned Surface

1. `src/`
2. `tests/`
3. `CMakeLists.txt`
4. `scripts/bootstrap-check.py`
5. `scripts/native-check.sh`
6. `scripts/checkpoint-health.sh`
7. offline resolver/cache behavior and future local import/export commands
8. Temporary platform compatibility surfaces such as `pafio cloud` until they are consumed from `styio-platform`
9. Native registry read-plane security in `src/PafioRegistryClient/`, `src/PafioSecurity/`, and the local `src/pafio_registry_v2/` compatibility helpers that keep offline package use safe while platform owns hosted service behavior

## Daily Workflow

1. Start from [../BUILD-AND-DEV-ENV.md](../BUILD-AND-DEV-ENV.md).
2. Keep native build/test behavior behind `scripts/checkpoint-health.sh`.
3. Treat `pafio use`, `pafio set`, `pafio sync`, `pafio project-graph --json`, `pafio tool status --json`, `pafio build minimal`, local import/export, offline package use, and `pafio-toolchain.lock` as owned workflow surface.
4. Keep the bootstrap path `curl ... install-pafio.sh | sh`, `pafio install styio@latest`, and the managed `styio` shim runnable on a fresh VM before claiming installer closure.
5. Update CLI or workflow docs when public behavior changes.
6. Keep `src/PafioCLI/CLI.cpp` thin. New payload builders, workflow validation rules, and private process helpers belong in domain or infrastructure modules, not in the CLI router.
7. Keep `src/PafioToolchain/` vocabulary and project-local state terminology aligned with docs. When source-build, channel, risk, lane, or security terms change in code, update the owning governance and delivery docs in the same checkpoint.
8. Keep any remaining local cloud compatibility deterministic and small; new cloud stress or scheduler behavior belongs in `styio-platform`.
9. Keep `pafio cloud plan --json` targeting the native JSON `styio-platform` submit-job route, currently `POST /api/styio-platform/v1/jobs`, and update native tests when that route changes.
10. Keep registry package identity, registry object paths, HTTP read limits, and tar pre-extraction checks aligned between the native client and Python registry v2 helpers.
11. Preserve offline package operation: cache reads must not require platform connectivity once metadata and artifacts are available locally.
12. Treat remote HTTP registry trust as a core fetch invariant: public clients
    must import a platform descriptor and validate the pinned root metadata
    before materializing registry packages.

## Change Classes

1. Small: local command behavior, fixture cleanup, or dry-run plan output. Run checkpoint health.
2. Medium: CLI shape, manifest/lock semantics, dependency preparation loops, local import/export bundle semantics, project-graph payloads, platform compatibility payloads, native contract source gates, tool-status payloads, toolchain-mode persistence, or resolver behavior. Update docs and tests together.
3. High: binary/build execution routing, source-build fetch/build semantics, installer bootstrap behavior, registry fetch/extract trust policy, offline package guarantees, platform compatibility semantics, or checkpoint entrypoint change. Coordinate with Docs / Delivery, Registry / Publish, and Styio / Contracts.

## Required Gates

```bash
./scripts/checkpoint-health.sh
./scripts/delivery-gate.sh --skip-audit --skip-health
ctest --test-dir build-codex -R pafio_installer_bootstrap_smoke --output-on-failure
python3 tests/unit/test_registry_v2.py
./build-codex/bin/pafio_native_tests --gtest_filter='SecurityTests.*'
./build-codex/bin/pafio_native_tests --gtest_filter='ToolInstallTests.*'
```

## Cross-Team Dependencies

1. Styio / Contracts reviews published-binary compatibility behavior and source-build contract wording.
2. Registry / Publish reviews changes that affect publish/fetch source semantics.
3. Docs / Delivery reviews workflow entrypoint or gate shape changes.

## Handoff / Recovery

Record the affected command path, fixture or test coverage gap, the selected project toolchain mode, and whether the next recovery step needs a published external `styio` binary, a source-build checkout, or a downstream `styio-platform` change.
