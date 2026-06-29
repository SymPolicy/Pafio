# Spio Workstreams and TODOs

**Purpose:** Break the `spio` implementation into independent task lines that can be assigned, implemented, and verified in parallel.

**Last updated:** 2026-04-22

## Workstream A. Repository Independence

Ownership boundary:

- `README.md`
- `docs/*`
- `scripts/*`
- `tests/*`
- repository-local extraction and copy tooling

TODOs:

- keep every `spio`-owned contract, test, and helper under the repository root without hidden compiler-side dependencies
- prevent direct imports from `styio/src` and `styio/tests`
- maintain a clean extraction path to `<spio-workspace>`
- keep migration instructions current as the subtree evolves

Blocks:

- nothing; this stream starts immediately

Feeds:

- every other stream

Gate:

- `spio_extractability_gate`

Defect:

- high documentation burden; easy to neglect if implementation pressure rises

## Workstream B. Compatibility and Compiler Handshake

Ownership boundary:

- `contracts/compat/*`
- `src/SpioCompat/*`
- `docs/governance/Spio-Version-Decoupling-Constraints.md`
- `docs/external/for-styio/Styio-Public-Interface-Roadmap.md`

TODOs:

- maintain the `styio` compatibility matrix
- validate `styio --machine-info=json`
- reject missing required capabilities
- keep bootstrap and future compile-plan phases distinct
- document the difference between compatibility checks and build orchestration support

Blocks:

- published `styio` machine handshake

Feeds:

- CLI
- migration preflight
- future build orchestration

Gate:

- `styio_contract_compat_gate`

Defect:

- compatibility policy can drift if compiler capability names change without coordinated updates

## Workstream C. Manifest / Lockfile Core

Ownership boundary:

- `src/SpioManifest/*`
- `src/SpioWorkflow/*`
- `tests/unit/fixtures/manifests/*`
- `tests/unit/fixtures/locks/*`
- `docs/governance/Spio-Manifest-and-Lock-Conventions.md`

TODOs:

- parse manifest and lockfile
- freeze `toolchain`, `[lib]`, `[[bin]]`, and `[workspace]` rules in the native core
- add normalization and canonical write-back
- validate workspace structures
- freeze field ordering rules
- add failure fixtures for malformed dependencies, bad versions, invalid workspaces, and lock drift

Blocks:

- none for bootstrap validation

Feeds:

- resolver
- CLI
- future registry and build work

Gate:

- `spio_manifest_lock_gate`

Defect:

- some schema details may still be revised when compile-plan and module import semantics are finalized

## Workstream D. Resolver and Cache

Ownership boundary:

- `src/SpioResolve/*`
- `src/SpioCore/Paths.*`
- `src/SpioVendor/*`
- `src/SpioRegistryClient/*`
- resolver fixtures and integration fixtures

TODOs:

- implement `path` resolution
- implement `git` resolution
- add stable dependency graph errors
- introduce hermetic `SPIO_HOME` structure
- partition cache keys by compiler version, protocol version, edition, profile, and source hash

Blocks:

- manifest/lock schema stability

Feeds:

- build/run/test commands
- registry work

Gate:

- `spio_resolver_gate`

Defect:

- no registry in early phases and single-version resolution will intentionally reject some graphs

## Workstream E. CLI Workflow

Ownership boundary:

- `src/SpioCLI/*`
- `src/SpioApp/*`
- `src/SpioCloud/*`
- `docs/governance/Spio-CLI-Contract.md`
- `docs/governance/Spio-Cloud-Control-Plane-Contract.md`

TODOs:

- keep `new`, `init`, and `check` stable
- maintain resolver-backed `add`, `remove`, `fetch`, `lock`, and `tree`
- preserve exit code contracts
- preserve machine-readable error shapes
- keep command routing thin while domain validation, payload serialization, and process execution stay outside the CLI router
- keep project-local toolchain mode, channel, build mode, and cloud preference grammar aligned across CLI help, docs, and machine-readable payloads

Blocks:

- compatibility and manifest validation for anything beyond local scaffolding

Feeds:

- all user-facing workflow commands

Gate:

- `spio_cli_gate`

Defect:

- some commands still lead future phases rather than fully closed remote execution behavior

## Workstream F. Compile-Plan and External Compiler Integration

Ownership boundary:

- `contracts/compile-plan/*`
- `src/SpioPlan/*`
- `src/SpioToolchain/*`
- black-box integration fixtures

TODOs:

- publish compile-plan schema
- add schema fixtures
- negotiate plan support against published `styio`
- generate plan files only after compatibility checks pass
- never bypass process boundary integration
- keep local source-build mode and published binary-mode compiler execution distinct in docs and contracts

Blocks:

- published `styio --compile-plan <path>`

Feeds:

- build
- run
- test

Gate:

- `contract_schema_gate`
- `spio_workflow_gate`

Defect:

- largest external dependency; work here must wait for compiler publication rather than local assumptions

## Workstream G. Test and Verification Infrastructure

Ownership boundary:

- `tests/README.md`
- `tests/unit/*`
- `tests/integration/*`
- `tests/native/*`
- `scripts/bootstrap-check.py`
- verification scripts and gate entrypoints

TODOs:

- keep unit tests hermetic
- move phase-2 core validation coverage onto native unit tests
- add compatibility fixtures
- add integration fixtures using external compiler binaries only
- map each stream to a named gate
- keep migration and extractability checks runnable from a copied subtree

Blocks:

- none for bootstrap; later phases depend on resolver and compile-plan work

Feeds:

- every acceptance gate

Gate:

- all named gates rely on this stream

Defect:

- test infrastructure can become stale if commands are renamed without synchronized fixture updates

## Workstream H. Repository Split Runbook

Ownership boundary:

- `spio/scripts/copy-to-external-repo.sh`
- future preflight scripts
- migration docs

TODOs:

- add a single preflight command before copy
- document exact copy sequence
- document post-move checks
- keep the runbook neutral to implementation language

Blocks:

- extractability and compatibility checks must already exist

Feeds:

- actual move to `<spio-workspace>`

Gate:

- `styio_spio_dual_maintenance_gate`

Defect:

- runbook accuracy depends on keeping file layout assumptions current

## Final Closure Rule

Before declaring the planning stage complete, every workstream must have:

- clear ownership boundaries
- explicit TODOs
- one named gate
- at least one documented defect or limitation

## 2026-04-22 Closure Snapshot

### Closed (Verified by Current Test Evidence)

- `spio_styio_interface_gate_handshake` + `spio_styio_interface_gate_compile_plan` are passing, confirming the baseline `styio` machine-info/compile-plan handoff test path (`ctest --test-dir <spio-workspace>/build-codex --output-on-failure`).
- Registry/control-plane and hosted API contract suites are passing in the same run (`spio_registry_*` and `spio_hosted_api_*` tests), which gives evidence that contract/interop lanes are stable.

### Open / Not Yet Closed

- **A, B, C, D, E, F, G, H** remain open for full stage closure unless the stream-specific TODO list is fully converted to verified acceptance items.
- Current blockers remain:
  - stream-level end-to-end behavior still exceeds test coverage of the current default suite (`spio_native_tests_NOT_BUILT` is registered but not runnable in this environment/config).
  - several TODOs explicitly call out future-phase/implementation-behind-contract behavior, especially around remote execution and full CLI behavior guarantees (see workstream defects).
  - docs-to-runbook synchronization requires one-to-one closure on extractability and verification runbooks before stage close, as called out in stream H.
