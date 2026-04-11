# Spio Workstreams and TODOs

**Purpose:** Break the `spio` implementation into independent task lines that can be assigned, implemented, and verified in parallel.

**Last updated:** 2026-04-09

## Workstream A. Repository Independence

Ownership boundary:

- `spio/README.md`
- `spio/docs/*`
- `spio/scripts/*`
- `spio/tests/*`

TODOs:

- keep every `spio`-owned contract, test, and helper under `spio/`
- prevent direct imports from `styio/src` and `styio/tests`
- maintain a clean extraction path to `$HOME/DevSpace/Unka-Malloc/styio-spio`
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

- `spio/contracts/compat/*`
- `spio/src/spio_bootstrap/compat.py`
- `spio/docs/governance/Spio-Version-Decoupling-Constraints.md`
- `spio/docs/styio/Styio-Public-Interface-Roadmap.md`

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

- `spio/src/spio_bootstrap/validation.py`
- future manifest/lock serializer modules
- `spio/tests/unit/fixtures/manifests/*`
- `spio/tests/unit/fixtures/locks/*`
- `spio/docs/governance/Spio-Manifest-and-Lock-Conventions.md`

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

- future resolver modules under `spio/src`
- future cache modules under `spio/src`
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

- `spio/src/spio_bootstrap/cli.py`
- future command modules
- `spio/docs/governance/Spio-CLI-Contract.md`

TODOs:

- keep `new`, `init`, and `check` stable
- migrate the authoritative CLI path to a native `C++20` implementation built with `CMake`
- grow `add`, `remove`, `fetch`, `lock`, `tree`
- preserve exit code contracts
- preserve machine-readable error shapes
- avoid filesystem side effects before compatibility and manifest checks pass

Blocks:

- compatibility and manifest validation for anything beyond local scaffolding

Feeds:

- all user-facing workflow commands

Gate:

- `spio_cli_gate`

Defect:

- bootstrap CLI will remain partially stubbed until resolver and compiler integration are available

## Workstream F. Compile-Plan and External Compiler Integration

Ownership boundary:

- `spio/contracts/compile-plan/*`
- future plan generator modules
- black-box integration fixtures

TODOs:

- publish compile-plan schema
- add schema fixtures
- negotiate plan support against published `styio`
- generate plan files only after compatibility checks pass
- never bypass process boundary integration

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

- `spio/tests/README.md`
- `spio/tests/unit/*`
- `spio/tests/integration/*`
- `spio/scripts/bootstrap-check.py`
- future verification scripts

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

- actual move to `$HOME/DevSpace/Unka-Malloc/styio-spio`

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
