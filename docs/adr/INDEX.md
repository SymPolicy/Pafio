# ADR Index

**Purpose:** Provide the inventory for `docs/adr/`; decision-record conventions live in [README.md](./README.md).

**Last updated:** 2026-04-12

## Files

| Path | Entry | Summary |
|------|-------|---------|
| `ADR-0001-spio-adopts-dedicated-adr-directory.md` | [ADR-0001: Spio Adopts a Dedicated ADR Directory](./ADR-0001-spio-adopts-dedicated-adr-directory.md) | Record the decision that durable `spio` design and implementation decisions live under `docs/adr/`. |
| `ADR-0002-native-cpp20-cmake-phase2-core.md` | [ADR-0002: Native C++20 and CMake as the Phase-2 Implementation Path](./ADR-0002-native-cpp20-cmake-phase2-core.md) | Record the decision to move the authoritative `spio` implementation path from the Python bootstrap toward a native `C++20` + `CMake` core. |
| `ADR-0003-entry-argument-index-ssot.md` | [ADR-0003: Entry and Argument Index as the Parameter SSOT](./ADR-0003-entry-argument-index-ssot.md) | Record the decision to centralize user-visible command, helper-script, and environment-variable parameters in a dedicated index document. |
| `ADR-0004-phase2-canonical-manifest-lock-writeback.md` | [ADR-0004: Phase-2 Canonical Manifest and Lockfile Write-Back](./ADR-0004-phase2-canonical-manifest-lock-writeback.md) | Record the deterministic section order, field order, and write-back policy for the native phase-2 manifest and lock core. |
| `ADR-0005-phase2-lock-command-local-graph-scope.md` | [ADR-0005: Phase-2 `spio lock` Uses Only Local Workspace and Path Graphs](./ADR-0005-phase2-lock-command-local-graph-scope.md) | Record the decision to implement a minimal phase-2 `spio lock` over local graphs first and defer pinned `git` resolution to the resolver phase. |
| `ADR-0006-phase2-lock-cli-and-local-identity.md` | [ADR-0006: Phase-2 `spio lock` CLI Surface and Local Identity Rules](./ADR-0006-phase2-lock-cli-and-local-identity.md) | Record the first public `spio lock` command surface, stale-check policy, and deterministic local lock identity encoding. |
| `ADR-0007-phase3-minimal-single-version-resolver.md` | [ADR-0007: Phase-3 Minimal Resolver Uses `single-version-v1` Across Workspace, Path, and Pinned Git](./ADR-0007-phase3-minimal-single-version-resolver.md) | Record the first native resolver boundary and the strict single-version conflict policy across supported source kinds. |
| `ADR-0008-hermetic-git-snapshot-cache-under-spio-home.md` | [ADR-0008: Hermetic Git Snapshot Cache Lives Under `SPIO_HOME`](./ADR-0008-hermetic-git-snapshot-cache-under-spio-home.md) | Record the first git source cache layout under `SPIO_HOME` for pinned-git resolution. |
| `ADR-0009-phase3-tree-renders-resolver-graph.md` | [ADR-0009: Phase-3 `spio tree` Renders the Resolver Graph Directly](./ADR-0009-phase3-tree-renders-resolver-graph.md) | Record the first native `spio tree` command shape and the decision to render directly from the active resolver graph. |
| `ADR-0010-phase3-basic-dependency-edit-and-fetch-commands.md` | [ADR-0010: Phase-3 Activates Basic Dependency Edit and Fetch Commands](./ADR-0010-phase3-basic-dependency-edit-and-fetch-commands.md) | Record the first native `add`, `remove`, and `fetch` command surfaces plus manifest/lock rollback policy. |
| `ADR-0011-phase3-check-validates-resolver-graph-and-lock-drift.md` | [ADR-0011: Phase-3 `spio check` Validates the Resolver Graph and Lock Drift](./ADR-0011-phase3-check-validates-resolver-graph-and-lock-drift.md) | Record the decision to make `spio check` graph-aware and drift-aware instead of only syntax-aware. |
| `ADR-0012-phase4-build-dry-run-compile-plan.md` | [ADR-0012: Phase-4 Activates `spio build --dry-run` and Local Compile-Plan Emission](./ADR-0012-phase4-build-dry-run-compile-plan.md) | Record the first native `build` surface, local compile-plan emission path, and phase gate for real compiler execution. |
| `ADR-0013-phase4-run-dry-run-and-test-gap.md` | [ADR-0013: Phase-4 Activates `spio run --dry-run` and Freezes the Current Test-Target Gap](./ADR-0013-phase4-run-dry-run-and-test-gap.md) | Record the first native `run` surface and the explicit decision to keep `test` stubbed until a real test-target contract exists. |
| `ADR-0014-phase4-test-dry-run-with-explicit-test-targets.md` | [ADR-0014: Phase-4 Activates `spio test --dry-run` with Explicit `[[test]]` Targets](./ADR-0014-phase4-test-dry-run-with-explicit-test-targets.md) | Record the explicit manifest test-target model, compile-plan package extension, and first native `test` surface. |
| `ADR-0015-phase4-pack-deterministic-source-archive.md` | [ADR-0015: Phase-4 Activates `spio pack` with Deterministic Local Source Archives](./ADR-0015-phase4-pack-deterministic-source-archive.md) | Record the first native `pack` surface, deterministic tar archive format, and minimal package-tree inclusion rules. |
