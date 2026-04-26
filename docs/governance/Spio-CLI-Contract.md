# Spio CLI Contract

**Purpose:** Freeze the command surface, exit code ranges, and machine-readable output rules for the `spio` bootstrap phase so later implementations can evolve behind a stable interface.

**Last updated:** 2026-04-23

## 1. Command Surface

Detailed argument spellings and helper-script parameter lists are owned by:

- `docs/governance/Spio-Entry-Argument-Index.md`

The intended public command set is:

- `spio new`
- `spio init`
- `spio install`
- `spio project-graph`
- `spio cloud`
- `spio use`
- `spio set`
- `spio add`
- `spio remove`
- `spio sync`
- `spio fetch`
- `spio lock`
- `spio build`
- `spio run`
- `spio check`
- `spio test`
- `spio tree`
- `spio vendor`
- `spio pack`
- `spio publish`
- `spio tool install`
- `spio tool status`
- `spio tool use`
- `spio tool pin`

The bootstrap implementation may expose stubs for recognized commands before the full behavior exists.

During the bootstrap phase, `spio check` is the first command allowed to talk to a published external compiler, and only for handshake/compatibility validation:

```text
spio check --manifest-path path/to/spio.toml --styio-bin /path/to/styio
```

Resolver-backed lock generation is also part of the active command surface:

```text
spio lock --manifest-path path/to/spio.toml
spio lock --manifest-path path/to/spio.toml --check
```

Resolver-backed dependency tree rendering is part of the active command surface:

```text
spio tree --manifest-path path/to/spio.toml
```

Resolver-backed project graph introspection is also part of the active command surface:

```text
spio project-graph --json --manifest-path path/to/spio.toml
```

Project-local vendored snapshot materialization is also part of the active command surface:

```text
spio vendor --manifest-path path/to/spio.toml
spio vendor --manifest-path path/to/spio.toml --output path/to/.spio/vendor
```

Local source-package archive emission is also part of the active command surface:

```text
spio pack --manifest-path path/to/spio.toml
spio pack --manifest-path path/to/spio.toml --package namespace/name
```

Local publish preflight is also part of the active command surface:

```text
spio publish --manifest-path path/to/spio.toml --dry-run
spio publish --manifest-path path/to/spio.toml --package namespace/name --dry-run
spio publish --manifest-path path/to/spio.toml --registry path/to/registry-root
spio publish --manifest-path path/to/spio.toml --registry https://packages.example.test
# private security-module builds only:
spio publish --manifest-path path/to/spio.toml --registry https://packages.example.test --registry-profile write-dev
spio publish --manifest-path path/to/spio.toml --registry https://packages.example.test --registry-policy-file path/to/publish-policy.toml
spio publish --manifest-path path/to/spio.toml --registry https://packages.example.test --registry-header 'X-Spio-Write-Token: dev-token'
```

Managed local compiler installation is also part of the active command surface:

```text
spio install styio
spio install styio@latest
spio tool install --styio-bin /path/to/styio
spio tool status --json --manifest-path path/to/spio.toml
spio tool use --version 0.0.5 --channel stable
spio tool pin --version 0.0.5 --channel stable --manifest-path path/to/spio.toml
```

Project-local toolchain state selection is also part of the active command surface:

```text
spio use binary --manifest-path path/to/spio.toml
spio use build --manifest-path path/to/spio.toml
spio set channel as stable --manifest-path path/to/spio.toml
spio set channel as nightly --manifest-path path/to/spio.toml
spio set build as minimal --manifest-path path/to/spio.toml
spio set risk as trusted-internal --manifest-path path/to/spio.toml
spio set lane as warm-shared --manifest-path path/to/spio.toml
spio set security as trusted-warm --manifest-path path/to/spio.toml
spio cloud status --json --manifest-path path/to/spio.toml
spio cloud plan --json build minimal --manifest-path path/to/spio.toml
```

Local compile-plan emission is also part of the active command surface:

```text
spio build minimal --manifest-path path/to/spio.toml --dry-run
spio run --manifest-path path/to/spio.toml --dry-run
spio test --manifest-path path/to/spio.toml --dry-run
```

Basic dependency edit and source fetch commands are also part of the active command surface:

```text
spio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) ...
spio remove <alias-or-package> ...
spio sync --manifest-path path/to/spio.toml
spio fetch --manifest-path path/to/spio.toml
```

Project sync is the user-facing dependency preparation loop. It refreshes the lockfile unless `--locked` or `--frozen` is set, then materializes dependency sources through the same resolver and cache path as `fetch`:

```text
spio sync --manifest-path path/to/spio.toml
spio sync --manifest-path path/to/spio.toml --locked
spio sync --manifest-path path/to/spio.toml --frozen
```

## 2. Global Flags

- `--help`
- `--version`
- `--json`

`--json` requests machine-readable diagnostics for command failures.

## 3. Machine Info

`spio` must expose:

```text
spio machine-info --json
```

This is the package-manager-side self-description endpoint. It reports:

- tool version
- bootstrap status
- supported manifest and lockfile versions
- supported machine contract versions
- `supported_contracts.project_graph` reports `[1]`
- `supported_contracts.build_job_request` reports `[1]`
- `supported_contracts.toolchain_state` reports `[1]`
- `supported_contracts.workflow_success_payloads` reports `[1]`
- `supported_contracts.compile_plan` reports `[1]`
- `supported_contracts.cloud_execution_policy` reports `[1]`
- `supported_contracts.worker_pool_keys` reports `[1]`

Compile-plan publication rule:

- `supported_contracts.compile_plan` may advertise `[1]` because the active compatibility matrix is `compile-plan-live`
- owning `contracts/compile-plan/` schema files still does not authorize future versions by itself
- every advertised compile-plan version must have a matching `styio --compile-plan <path>` consumer and an interop gate

### 3.1 `spio project-graph --json`

- `spio project-graph --json` publishes `project_graph v1`
- `project_graph v1` includes at least `packages`, `dependencies`, `targets`, `toolchain`, `managed_toolchains`, `lock_state`, `vendor_state`, `notes`, `package_distribution`, and `source_state`

### 3.2 `spio tool status --json`

- `spio tool status --json` publishes `toolchain_state v1`
- `project_pin`
- `current_compiler`
- `managed_toolchains`

### 3.3 `spio --json build/run/test`

- `workflow_success_payloads v1`
- `receipt.json`
- `diagnostics.jsonl` path
- captured stdout/stderr
- resolved `cloud_execution_policy v1`

### 3.4 `spio cloud status --json`

- `spio cloud status --json` publishes `cloud_execution_policy v1`
- project-local persisted values include:
  - `toolchain_mode`
  - `channel`
  - `build_mode`
  - `risk_class`
  - `preferred_execution_lane`
  - `security_profile`
- resolved values include:
  - `execution_lane`
  - `worker_trust_tier`
  - `cache_policy`
  - `worker_pool_key`

### 3.5 `spio cloud plan --json`

- `spio cloud plan --json` publishes `build_job_request v1`
- the payload freezes the normalized request body shape for `POST /api/styio-platform/v1/jobs`
- the request includes:
  - `action`
  - `toolchain`
  - `profile`
  - `workflow`
  - `target`
  - `source`
  - resolved `cloud` policy

### 3.6 Supporting JSON Success Commands

- spio --json fetch --manifest-path path/to/spio.toml ...
- spio --json sync --manifest-path path/to/spio.toml ...
- spio --json install styio@latest
- spio --json tool install --styio-bin /path/to/styio
- supporting internal commands invoked through `spio --json fetch/vendor/pack/publish/tool install/tool use/tool pin`
- `spio --json sync` participates in the same stable JSON success rule for dependency preparation
- must also return one stable JSON success object on stdout

## 4. Exit Codes

- `0` success
- `2` CLI usage error
- `10` manifest invalid
- `11` lockfile invalid or stale
- `12` workspace invalid
- `13` dependency resolution failed
- `14` dependency fetch failed
- `15` cache failure
- `16` source package archive failure
- `17` publish preflight or transport failure
- `18` tool management failure
- `19` project-local vendor materialization failure
- `20` compile-plan generation failed
- `21` compile-plan contract mismatch
- `22` compiler process spawn failed
- `23` compiler check/build failure
- `24` target run failure
- `25` test failure
- `30` internal error
- `31` recognized but not implemented in bootstrap

`31` is a bootstrap-only code. It should disappear once the command is fully implemented.

`21` is also used when the external compiler handshake is outside the published compatibility matrix.

## 5. Error Object Shape

When `--json` is requested, failures must be emitted as a single JSON object with stable keys:

- `category`
- `code`
- `message`
- `command`

Optional keys:

- `package`
- `source_kind`
- `child_program`
- `child_exit_code`

## 6. Bootstrap Check Behavior

- `spio check` always validates `spio.toml`
- `spio check` validates the active resolver graph after manifest parsing succeeds
- `spio check` validates adjacent `spio.lock` when present
- project-local `spio-toolchain.lock` stores the selected workflow mode, release channel, and build mode
- `spio check` treats a parse-valid but content-stale adjacent `spio.lock` as a lock failure
- phase-2 native `check` validates `toolchain`, target tables, dependency source-kind rules, and workspace membership rules
- resolver-backed `check` may use `SPIO_HOME` for pinned git source cache state
- resolver-backed `check` may also use `SPIO_HOME/registry/` for cached registry metadata, blobs, and extracted snapshots
- if `--styio-bin` is passed, `SPIO_STYIO_BIN` is set, a project toolchain pin resolves, or a managed current compiler exists, `check` must also call `styio --machine-info=json`
- resolver-backed `check` runs before the optional compiler handshake
- compiler compatibility must be decided from the machine handshake plus `contracts/compat/styio-support.toml`
- bootstrap `check` must not read compiler source code or infer compatibility from repository layout
- `add` and `remove` only target manifests that define `[package]`
- `add` and `remove` must rewrite manifests canonically before returning success
- successful `add` and `remove` must refresh the adjacent `spio.lock` through the active resolver path
- failed post-edit resolution in `add` or `remove` must roll back both manifest and adjacent lockfile state
- resolver-backed `fetch` resolves the active graph and materializes source cache state without rewriting manifest or lock files
- `check`, `fetch`, `vendor`, `build`, `run`, and `test` accept workflow reproducibility flags:
  - `--locked` requires an adjacent fresh `spio.lock`
  - `--offline` forbids network fetches and relies only on local cache or vendored snapshots
  - `--frozen` combines `--locked` and `--offline`
- resolver-backed `lock` only reads or writes the adjacent `spio.lock` next to the selected manifest
- resolver-backed `lock` resolves workspace, path, pinned-git, and registry graphs under `single-version-v1`
- resolver-backed `lock` may use `SPIO_HOME` for git and registry source cache state
- resolver-backed `lock` accepts `--offline` in the current native core
- `spio lock --check` returns exit `11` when the adjacent lockfile is missing or stale
- resolver-backed `tree` resolves the same workspace, path, pinned-git, and registry graph directly from the selected manifest
- resolver-backed `tree` is read-only and does not require or rewrite an adjacent `spio.lock`
- human-readable `tree` output uses canonical package ids with ASCII connectors
- `spio vendor` resolves the active graph and copies pinned git snapshots into project-local `.spio/vendor/` by default
- `spio vendor` writes vendor metadata to `.spio/vendor/spio-vendor.json`
- resolver-backed commands may prefer project-local vendored snapshots under `.spio/vendor/` before falling back to `SPIO_HOME`
- `spio pack` selects exactly one local package from the active manifest root and never talks to the compiler
- `spio pack` writes a deterministic local source archive to `<package-root>/dist/<short-name>-<version>.tar` unless `--output <path>` overrides it
- `spio pack` packages canonical `spio.toml` plus regular files under the selected package root
- `spio pack` excludes the adjacent `spio.lock`, generated top-level directories, and workspace member/excluded subtrees when packing a combined root package manifest
- `spio pack` rejects symlinks and unsupported filesystem node types inside the included tree
- `spio publish --dry-run` performs local publish preflight and stages the same local source archive shape used by `spio pack`
- `spio publish --dry-run` requires `package.publish = true`
- published packages may include dependency entries only when those dependencies are themselves registry-addressable
- non-dry-run `spio publish` is active for an explicit registry root passed through `--registry <path-or-url>`
- local paths and `file://...` roots publish into the local `registry v2` static read plane by generating signing keys under `SPIO_HOME/server/registry/v2/keys` on first use
- `http://...` and `https://...` roots publish through the versioned registry control-plane route family rooted at `/api/spio-registry-control/v1`
- `--registry-profile <name>`, `--registry-policy-file <path>`, and `--registry-header <name:value>` reserve the private write-side security interface for remote publish
- the tracked open-source core rejects those three options unless a private security module is linked from `src-private/`
- when a private security module accepts them, they apply only to remote publish against the write origin and do not affect read-side fetch behavior
- local publish writes:
  - registry config: `<registry-root>/config.json`
  - signed namespace targets: `<registry-root>/trust/targets/<namespace>.json`
  - append-only package index: `<registry-root>/index/<namespace>/<name>.jsonl`
  - immutable source artifact: `<registry-root>/artifacts/source/sha256/<xx>/<yy>/<sha256>.spio.src.tar`
  - transparency leaf/checkpoint: `<registry-root>/log/...`
- registry `v2` index records store dependency metadata for `[dependencies]` and `[dev-dependencies]`
- publish must reject republishing an existing package version record
- remote publish assumes a control-plane service that appends releases and refreshes signed metadata without exposing raw static-root writes
- publish JSON must not expose raw request headers or resolved private policy file paths; it may expose only redacted security metadata
- first-class auth/account policy remains outside the tracked public tree behind the private security-module boundary
- registry dependency roots in manifests use `file://`, `http://`, or `https://`
- resolver-backed `fetch` materializes registry `config`, namespace targets, append-only package indexes, immutable source artifacts, and extracted snapshots under `SPIO_HOME/registry/`
- `spio install styio` and `spio install styio@latest` default to stable latest and build the compiler from source while the platform package repository is unavailable
- source install fetches from `SPIO_STYIO_SOURCE_ORIGIN` when set, otherwise from `https://github.com/SymPolicy/styio.git`
- source install uses `SPIO_STYIO_SOURCE_REF` when set, otherwise `main` for `latest`; explicit `styio@<ref>` maps to that source revision
- source install defaults to non-interactive fetch approval so fresh machines can bootstrap with one command
- source install validates the resulting compiler through the same compatibility matrix as `spio tool install` before promoting it to `SPIO_HOME/tools/styio/current/`
- `spio tool install` installs only local self-contained `styio` executables in the current native core
- `spio tool install` must validate the compiler through `styio --machine-info=json` plus the published compatibility matrix before writing managed state
- `spio tool install` writes managed compiler state under `SPIO_HOME/tools/styio/`
- `spio tool use` switches the managed current compiler to an already installed versioned root under `SPIO_HOME/tools/styio/`
- `spio tool use` must re-validate the selected managed compiler through `styio --machine-info=json` plus the published compatibility matrix before promoting it to current
- `spio tool pin` writes a project-local toolchain pin file beside the selected manifest
- project-local toolchain pin discovery searches upward from the selected manifest directory for `spio-toolchain.toml`
- `spio use <binary|build>` writes a project-local `spio-toolchain.lock` beside the selected manifest
- `spio set channel as <stable|nightly>` updates the project-local release channel in `spio-toolchain.lock`
- `spio set build as minimal` updates the project-local build mode in `spio-toolchain.lock`
- `spio set risk as <trusted-internal|partner-controlled|untrusted-user>` updates the project-local cloud risk class in `spio-toolchain.lock`
- `spio set lane as <isolated|warm-shared>` updates the project-local preferred execution lane in `spio-toolchain.lock`
- `spio set security as <sandbox-default|partner-restricted|trusted-warm>` updates the project-local security profile in `spio-toolchain.lock`
- `spio cloud status --json` resolves the active cloud execution policy from project-local state without requiring a published external compiler
- `spio check` and `spio build`, `spio run`, or `spio test` in `binary` mode resolve compilers in this order:
  - explicit `--styio-bin <path>`
  - `SPIO_STYIO_BIN`
  - nearest project-local `spio-toolchain.toml`
  - managed current compiler under `SPIO_HOME/tools/styio/current/bin/styio`
- a discovered project-local toolchain pin is authoritative and must fail if the pinned managed compiler is missing
- `spio build minimal --dry-run` resolves the active graph and writes a local `compile-plan v1` to `.spio/build/<cache-key>/plan.json`
- `spio build --dry-run` must not require compiler probing
- `spio build minimal` uses the selected project toolchain mode:
  - `binary` continues through published compiler discovery and compatibility gating
  - `build` resolves or fetches the official `styio` source tree from `https://github.com/SymPolicy/Styio.git`, maps `stable` and `nightly` to the same-named source branches, builds a local compiler under `SPIO_HOME/toolchains/source/`, and then runs the compile-plan through that source-built compiler
- Source-build alignment requirements for `spio build minimal`:
  - official source origin is `https://github.com/SymPolicy/Styio.git`
  - channel mapping is `stable` and `nightly` to the same-named source branches
  - project-local workflow state remains in `spio-toolchain.lock`
  - source-build mode bypasses the published binary compatibility matrix
- `spio build` accepts `minimal` as the only current build mode; bare `spio build` normalizes to the same mode through project defaults
- `spio build`, `spio run`, and `spio test` accept `--source-root`, `--source-rev`, `--yes`, `--no-fetch`, and `--non-interactive` when the selected project mode is `build`
- `spio run --dry-run` resolves the active graph and writes a local `compile-plan v1` with `intent = "run"` to `.spio/build/<cache-key>/plan.json`
- `spio run` only supports explicit binary targets in the current native core
- `spio test --dry-run` resolves the active graph and writes a local `compile-plan v1` with `intent = "test"` to `.spio/build/<cache-key>/plan.json`
- `spio test` only supports explicit manifest `[[test]]` targets in the current native core
- in `binary` mode, non-dry-run `spio build` requires a resolved compiler from explicit `--styio-bin <path>`, `SPIO_STYIO_BIN`, a project toolchain pin, or the managed current compiler
- in `binary` mode, non-dry-run `spio build` calls `styio --compile-plan <path>` after compiler discovery and compatibility gating confirm compile-plan v1
- in `binary` mode, non-dry-run `spio run` follows the same published compile-plan gate as `spio build`
- in `binary` mode, non-dry-run `spio test` follows the same published compile-plan gate as `spio build`
- after a successful compiler exit, `spio build`, `spio run`, and `spio test` require the declared output directories and `outputs.build_root/receipt.json` to exist
- in `build` mode, non-dry-run `spio build`, `spio run`, and `spio test` use the locally built compiler path produced from the selected or fetched source tree
- source-build mode bypasses the published binary compatibility matrix and instead uses the locally built compiler revision recorded in `spio-toolchain.lock`
- the current native baseline resolves cloud execution policy locally:
  - `untrusted-user` always resolves to `isolated`
  - `partner-controlled` currently resolves to `isolated` even if `warm-shared` is preferred
  - `trusted-internal` may keep `warm-shared`
- workflow success payloads for `spio build`, `spio run`, and `spio test` must include the resolved cloud policy object so later remote execution can reuse the same semantics
- compile-plan generation currently supports only explicit `lib`, `bin`, and `test` targets
- compile-plan generation may reject graphs that are otherwise resolvable when compile-plan v1 cannot represent them, such as cyclic graphs or mixed toolchain tuples

## 7. Known Tradeoffs

- The bootstrap CLI shape is larger than a single `install` command.
- The temporary bootstrap-only exit code `31` is inelegant but useful while commands are stubbed.
- Freezing the interface before implementation increases upfront design work.
