# Spio CLI Contract

**Purpose:** Freeze the command surface, exit code ranges, and machine-readable output rules for the `spio` bootstrap phase so later implementations can evolve behind a stable interface.

**Last updated:** 2026-04-17

## 1. Command Surface

Detailed argument spellings and helper-script parameter lists are owned by:

- `docs/governance/Spio-Entry-Argument-Index.md`

The intended public command set is:

- `spio new`
- `spio init`
- `spio add`
- `spio remove`
- `spio fetch`
- `spio lock`
- `spio build`
- `spio run`
- `spio check`
- `spio project-graph`
- `spio test`
- `spio tree`
- `spio vendor`
- `spio pack`
- `spio publish`
- `spio tool status`
- `spio tool install`
- `spio tool use`
- `spio tool pin`

The bootstrap implementation may expose stubs for recognized commands before the full behavior exists.

During the bootstrap phase, `spio check` is the first command allowed to talk to an external compiler, and only for handshake/compatibility validation:

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

Published project graph payload emission is also part of the active command surface:

```text
spio project-graph --manifest-path path/to/spio.toml --json
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
spio tool status --manifest-path path/to/spio.toml --json
spio tool status --manifest-path path/to/spio.toml --styio-bin /path/to/styio --json
spio tool install --styio-bin /path/to/styio
spio tool use --version 0.0.5 --channel stable
spio tool pin --version 0.0.5 --channel stable --manifest-path path/to/spio.toml
```

Local compile-plan emission is also part of the active command surface:

```text
spio build --manifest-path path/to/spio.toml --dry-run
spio run --manifest-path path/to/spio.toml --dry-run
spio test --manifest-path path/to/spio.toml --dry-run
```

Basic dependency edit and source fetch commands are also part of the active command surface:

```text
spio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) ...
spio remove <alias-or-package> ...
spio fetch --manifest-path path/to/spio.toml
```

## 2. Global Flags

- `--help`
- `--version`
- `--json`

`--json` requests machine-readable diagnostics for command failures.

## 2.1 Internal Machine-Readable Invocation Spellings

`styio-view`、cross-repo gates、以及其它内部 consumers 当前应按这些 canonical spellings 调用：

```text
spio machine-info --json
spio project-graph --manifest-path path/to/spio.toml --json
spio tool status --manifest-path path/to/spio.toml --json
spio --json build --manifest-path path/to/spio.toml ...
spio --json run --manifest-path path/to/spio.toml ...
spio --json test --manifest-path path/to/spio.toml ...
spio --json fetch --manifest-path path/to/spio.toml ...
spio --json vendor --manifest-path path/to/spio.toml ...
spio --json pack --manifest-path path/to/spio.toml ...
spio --json publish --manifest-path path/to/spio.toml --dry-run
spio --json publish --manifest-path path/to/spio.toml --registry <path-or-url>
spio --json tool install --styio-bin /path/to/styio
spio --json tool use --version 0.0.5 --channel stable
spio --json tool pin --version 0.0.5 --channel stable --manifest-path path/to/spio.toml
spio --json tool pin --clear --manifest-path path/to/spio.toml
```

规则：

- `machine-info`、`project-graph`、`tool status` 保留 command-local `--json` spelling
- 其余 internal workflow/toolchain/deployment commands 统一使用 top-level `spio --json <command> ...`
- 三仓 handoff 文档必须沿用这套 canonical spellings，不再混写 post-command `--json`

## 3. Machine Info

`spio` must expose:

```text
spio machine-info --json
```

This is the package-manager-side self-description endpoint. It reports:

- tool version
- bootstrap status
- active integration phase
- supported manifest and lockfile versions
- supported machine contract versions
- supported adapter modes
- feature flags

Current native rule:

- `supported_contracts.compile_plan` reports `[1]` only because the published compatibility matrix now enables compile-plan v1 against released `styio` binaries
- `supported_contracts.project_graph` reports `[1]` because `spio` now owns and publishes a machine-readable project graph payload for `styio-view`
- `supported_contracts.toolchain_state` reports `[1]` because `spio tool status --json` now publishes the managed compiler environment and project pin state for IDE consumers
- `supported_contracts.workflow_success_payloads` reports `[1]` because non-dry-run `build/run/test --json` now return structured success payloads with receipt, diagnostics path, and captured stdout/stderr
- `project_graph v1` now also carries package-distribution state for IDE deployment flows: package `publish_enabled`, dependency source metadata, a project-level `package_distribution` summary with registry roots and publish-blocking reasons, and `source_state` for vendored snapshots plus git/registry cache roots
- owning `contracts/compile-plan/` schema files by itself still does not authorize advertising active compile-plan support
- local dry-run plan emission is not sufficient on its own; the published compiler consumer and compatibility matrix remain the activation gate

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
- local paths and `file://...` roots publish directly into the local filesystem registry layout
- `http://...` and `https://...` roots publish through anonymous HTTP `PUT` to the same marker/blob/entry paths
- `--registry-profile <name>`, `--registry-policy-file <path>`, and `--registry-header <name:value>` reserve the private write-side security interface for remote publish
- the tracked open-source core rejects those three options unless a private security module is linked from `src-private/`
- when a private security module accepts them, they apply only to remote publish against the write origin and do not affect read-side fetch behavior
- publish writes:
  - registry marker: `<registry-root>/spio-registry.json`
  - immutable archive blob: `<registry-root>/blobs/sha256/<xx>/<yy>/<sha256>.tar`
  - version entry: `<registry-root>/index/<namespace>/<name>/<version>.json`
- registry version entries record dependency metadata for `[dependencies]` and `[dev-dependencies]`
- publish must reject republishing an existing package version entry
- remote publish currently assumes an origin that preserves immutable paths and rejects overwrites
- publish JSON must not expose raw request headers or resolved private policy file paths; it may expose only redacted security metadata
- first-class auth/account policy remains outside the tracked public tree behind the private security-module boundary
- registry dependency roots in manifests use `file://`, `http://`, or `https://`
- resolver-backed `fetch` materializes registry marker metadata, version entries, immutable blobs, and extracted snapshots under `SPIO_HOME/registry/`
- `spio tool install` installs only local self-contained `styio` executables in the current native core
- `spio tool install` must validate the compiler through `styio --machine-info=json` plus the published compatibility matrix before writing managed state
- `spio tool status --json` publishes `toolchain_state v1` for IDE environment-management consumers
- `toolchain_state v1` includes at least `toolchain`, `project_pin`, `active_compiler`, `current_compiler`, `managed_toolchains`, and `notes`
- `spio tool status --json` reports compiler machine-info whenever a binary can be probed, even if that compiler is not on the current compile-plan execution path
- `spio tool status --styio-bin <path> --json` previews that explicit compiler as the active toolchain without mutating managed or project-local state
- `spio tool install` writes managed compiler state under `SPIO_HOME/tools/styio/`
- `spio tool use` switches the managed current compiler to an already installed versioned root under `SPIO_HOME/tools/styio/`
- `spio tool use` must re-validate the selected managed compiler through `styio --machine-info=json` plus the published compatibility matrix before promoting it to current
- `spio tool pin` writes a project-local toolchain pin file beside the selected manifest
- project-local toolchain pin discovery searches upward from the selected manifest directory for `spio-toolchain.toml`
- `spio check`, `spio build`, `spio run`, and `spio test` resolve compilers in this order:
  - explicit `--styio-bin <path>`
  - `SPIO_STYIO_BIN`
  - nearest project-local `spio-toolchain.toml`
  - managed current compiler under `SPIO_HOME/tools/styio/current/bin/styio`
- a discovered project-local toolchain pin is authoritative and must fail if the pinned managed compiler is missing
- `spio build --dry-run` resolves the active graph and writes a local `compile-plan v1` to `.spio/build/<cache-key>/plan.json`
- `spio build --dry-run` must not require compiler probing
- `spio run --dry-run` resolves the active graph and writes a local `compile-plan v1` with `intent = "run"` to `.spio/build/<cache-key>/plan.json`
- `spio run` only supports explicit binary targets in the current native core
- `spio test --dry-run` resolves the active graph and writes a local `compile-plan v1` with `intent = "test"` to `.spio/build/<cache-key>/plan.json`
- `spio test` only supports explicit manifest `[[test]]` targets in the current native core
- non-dry-run `spio build` requires a resolved compiler from explicit `--styio-bin <path>`, `SPIO_STYIO_BIN`, a project toolchain pin, or the managed current compiler
- non-dry-run `spio build` may call `styio --compile-plan <path>` only when the published compatibility matrix enables compile-plan v1 for the current phase
- non-dry-run `spio run` follows the same published compile-plan gate as `spio build`
- non-dry-run `spio test` follows the same published compile-plan gate as `spio build`
- `spio project-graph --json` publishes `project_graph v1` for IDE and environment-management consumers
- `project_graph v1` includes at least `packages`, `dependencies`, `targets`, `toolchain`, `managed_toolchains`, `lock_state`, `vendor_state`, `notes`, `package_distribution`, and `source_state`
- package records include `publish_enabled`
- dependency records include source metadata such as `source_kind`, `package`, `path`, `git`, `rev`, `registry`, `version`, and `publish_blocking`
- `package_distribution` includes per-package publish readiness plus aggregated registry roots for deployment surfaces
- `source_state` includes `spio_home`, declared git/registry dependency counts, git cache roots, registry cache roots, and project-local vendor metadata presence
- non-dry-run `spio build/run/test --json` publish `workflow_success_payloads v1`
- `workflow_success_payloads v1` include at least the command metadata, build/artifact/diag roots, parsed `receipt.json` when present, `diagnostics.jsonl` path, and captured stdout/stderr
- compiler-originated non-dry-run `spio build/run/test --json` failures must keep the stable error keys and may additionally include compile-plan roots, parsed `receipt.json`, `diagnostics.jsonl` path plus parsed entries, and captured stdout/stderr
- supporting internal commands invoked through `spio --json fetch/vendor/pack/publish/tool install/tool use/tool pin` must also return one stable JSON success object on stdout
- those supporting success JSON objects must include at least `command` and `message`, plus command-specific metadata such as `archive_path`, `package`, managed compiler paths, or pin paths
- compile-plan generation currently supports only explicit `lib`, `bin`, and `test` targets
- compile-plan generation may reject graphs that are otherwise resolvable when compile-plan v1 cannot represent them, such as cyclic graphs or mixed toolchain tuples

## 7. Known Tradeoffs

- The bootstrap CLI shape is larger than a single `install` command.
- The temporary bootstrap-only exit code `31` is inelegant but useful while commands are stubbed.
- Freezing the interface before implementation increases upfront design work.
