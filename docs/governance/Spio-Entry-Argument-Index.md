# Spio Entry and Argument Index

**Purpose:** Provide the single entrypoint index for user-visible `spio` arguments, repository-maintainer script arguments, and public environment variables so parameter lists do not drift across code, scripts, and contract documents.

**Last updated:** 2026-04-17

## 1. Ownership

This document owns:

- detailed per-command argument spellings
- positional vs flag shapes for implemented `spio` commands
- helper-script argument summaries
- public environment variables consumed by `spio` entrypoints

This document does not own:

- exit-code policy
- machine-readable error object shape
- manifest or lockfile schema rules

Those remain in the existing governance contracts.

## 2. Global CLI Shape

Canonical top-level form:

```text
spio [--help] [--version] [--json] <command> [command-args...]
```

### Global Flags

- `--help`
  - prints top-level command usage
  - the native core treats this as a top-level flag before the command name
- `--version`
  - prints `spio <version>`
  - does not accept extra arguments
- `--json`
  - requests machine-readable diagnostics for command failures
  - may appear before the command name

## 3. Command Index

### Implemented Native Commands

- `spio machine-info [--json]`
- `spio new <package-name> [directory] [--lib|--bin]`
- `spio init [--name <package-name>] [--lib|--bin]`
- `spio check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen]`
- `spio project-graph [--manifest-path <path>] [--styio-bin <path>] [--json]`
- `spio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) [--alias <name>] [--dev] [--manifest-path <path>]`
- `spio remove <alias-or-package> [--dev] [--manifest-path <path>]`
- `spio fetch [--manifest-path <path>] [--locked|--offline|--frozen]`
- `spio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]`
- `spio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]`
- `spio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]`
- `spio lock [--manifest-path <path>] [--check] [--offline]`
- `spio tree [--manifest-path <path>]`
- `spio vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]`
- `spio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]`
- `spio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <path-or-url>] [--registry-profile <name>] [--registry-policy-file <path>] [--registry-header <name:value>] [--dry-run]`
- `spio tool status [--manifest-path <path>] [--styio-bin <path>] [--json]`
- `spio tool install --styio-bin <path>`
- `spio tool use --version <compiler-version> [--channel <channel>]`
- `spio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]`

## 4. Detailed Command Arguments

### `machine-info`

Canonical forms:

```text
spio machine-info
spio machine-info --json
```

Rules:

- output is machine-readable JSON in the current native core
- `--json` is accepted as the explicit compatibility spelling
- no other command-specific arguments are valid

### `new`

Canonical form:

```text
spio new <package-name> [directory] [--lib|--bin]
```

Arguments:

- `<package-name>`
  - required
  - package name in `namespace/name` form
- `[directory]`
  - optional
  - target directory for project creation
  - if omitted, the default directory is the package short name
- `--lib`
  - optional
  - create a library package scaffold
- `--bin`
  - optional
  - create a binary package scaffold
  - default if neither `--lib` nor `--bin` is provided

### `init`

Canonical form:

```text
spio init [--name <package-name>] [--lib|--bin]
```

Arguments:

- `--name <package-name>`
  - optional
  - explicit package name in `namespace/name` form
  - if omitted, the current native core infers `local/<cwd-basename>`
- `--lib`
  - optional
  - create a library package scaffold in the current directory
- `--bin`
  - optional
  - create a binary package scaffold in the current directory
  - default if neither `--lib` nor `--bin` is provided

### `check`

Canonical form:

```text
spio check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file
  - defaults to `spio.toml`
- `--styio-bin <path>`
  - optional
  - explicit compiler binary path used for compatibility probing
  - if omitted, compiler discovery continues through `SPIO_STYIO_BIN`, nearest project-local `spio-toolchain.toml`, and finally the managed current compiler
- `--locked`
  - optional
  - requires an adjacent `spio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- always validates the manifest
- resolves the active `single-version-v1` graph after manifest validation succeeds
- validates adjacent `spio.lock` when present
- requires an adjacent fresh `spio.lock` when `--locked` or `--frozen` is set
- treats a parse-valid but content-stale adjacent `spio.lock` as a failure
- may use `SPIO_HOME` when pinned git dependencies are present
- may prefer project-local vendored snapshots under `.spio/vendor/` when present
- if a compiler path is available, also performs the `styio --machine-info=json` handshake

### `project-graph`

Canonical form:

```text
spio project-graph [--manifest-path <path>] [--styio-bin <path>] [--json]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the project root
  - defaults to `spio.toml`
- `--styio-bin <path>`
  - optional
  - explicit compiler binary path used when probing the active compiler handshake for the graph payload

Behavior summary:

- emits a machine-readable `project_graph v1` payload owned by `spio`
- resolves workspace members, packages, dependencies, targets, editor files, and project-local roots
- publishes `toolchain`, `active_compiler`, `managed_toolchains`, `lock_state`, `vendor_state`, `package_distribution`, `source_state`, and `notes`
- package records include `publish_enabled`
- dependency records include source metadata such as `source_kind`, `package`, `path`, `git`, `rev`, `registry`, `version`, and `publish_blocking`
- `package_distribution` summarizes publish readiness per package and aggregates registry roots referenced by the active graph
- `source_state` publishes vendored snapshot metadata plus git/registry cache roots for IDE environment and deployment flows
- prefers published compiler handshakes over filesystem guesses when an active compiler is available
- must not fail only because a project pin references a missing managed compiler; that state is surfaced through `toolchain.detail`, `active_compiler = null`, and `notes`

### `add`

Canonical form:

```text
spio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) [--alias <name>] [--dev] [--manifest-path <path>]
```

Arguments:

- `<package-name>`
  - required
  - dependency package name in `namespace/name` form
- `--path <path>`
  - selects a local path dependency source
  - path must be explicit and relative to the package manifest directory
- `--git <source>`
  - selects a pinned git dependency source
- `--rev <rev>`
  - required with `--git`
  - names the pinned git revision
- `--registry <url>`
  - selects a registry dependency source
  - registry roots must use `file://`, `http://`, or `https://`
- `--version <x.y.z>`
  - required with `--registry`
  - names the requested registry package version
- `--alias <name>`
  - optional
  - dependency key written into the manifest
  - if omitted, defaults to the package short name
- `--dev`
  - optional
  - writes the dependency into `[dev-dependencies]`
  - default is `[dependencies]`
- `--manifest-path <path>`
  - optional
  - path to the package manifest to edit
  - defaults to `spio.toml`

Behavior summary:

- requires the selected manifest to define `[package]`
- writes canonical manifest output after the edit succeeds
- always materializes `package = "<namespace/name>"` in the dependency entry
- rejects duplicate dependency aliases and duplicate dependency package identities across both dependency sections
- refreshes the adjacent `spio.lock` through the active resolver before returning success
- rolls back manifest and adjacent lockfile changes if the post-edit resolver step fails
- registry adds canonicalize dependency tables as `package`, `version`, then `registry`

### `remove`

Canonical form:

```text
spio remove <alias-or-package> [--dev] [--manifest-path <path>]
```

Arguments:

- `<alias-or-package>`
  - required
  - dependency alias to remove
  - if the value contains `/` and alias matching finds nothing, the current native core may fall back to unique package-name matching
- `--dev`
  - optional
  - restricts the removal search to `[dev-dependencies]`
  - if omitted, the current native core searches both dependency sections
- `--manifest-path <path>`
  - optional
  - path to the package manifest to edit
  - defaults to `spio.toml`

Behavior summary:

- requires the selected manifest to define `[package]`
- rewrites canonical manifest output after the edit succeeds
- refreshes the adjacent `spio.lock` through the active resolver before returning success
- rolls back manifest and adjacent lockfile changes if the post-edit resolver step fails
- rejects ambiguous remove targets

### `fetch`

Canonical form:

```text
spio fetch [--manifest-path <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`
- `--locked`
  - optional
  - requires an adjacent `spio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the same `single-version-v1` graph used by `spio lock` and `spio tree`
- materializes dependency source state, including pinned git mirrors/snapshots and registry metadata/blob/checkout cache state under `SPIO_HOME`
- may reuse project-local vendored snapshots under `.spio/vendor/`
- requires an adjacent fresh `spio.lock` when `--locked` or `--frozen` is set
- does not require an on-disk `spio.lock` unless `--locked` or `--frozen` is set
- never rewrites the adjacent `spio.lock`

### `lock`

Canonical form:

```text
spio lock [--manifest-path <path>] [--check] [--offline]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`
- `--check`
  - optional
  - compares generated canonical output to the adjacent `spio.lock` without rewriting it
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots

Behavior summary:

- resolves a `single-version-v1` graph from:
  - the selected package manifest when `[package]` is present
  - workspace members when `[workspace]` is present
  - recursively discovered `path` dependencies
  - pinned `git` dependencies and their transitive manifests at the selected `rev`
  - both `[dependencies]` and `[dev-dependencies]`
- writes or checks only the adjacent `spio.lock` next to the selected manifest
- uses `SPIO_HOME` to cache git source mirrors and extracted pinned snapshots
- may reuse project-local vendored snapshots under `.spio/vendor/`
- returns the lock exit code when `--check` finds a missing or stale lockfile

### `build`

Canonical form:

```text
spio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`
- `--package <package-name>`
  - optional
  - selects a root package from the active graph when the selected manifest exposes multiple roots
  - package name must be in `namespace/name` form
- `--bin <name>`
  - optional
  - selects a specific binary target from the chosen package
- `--lib`
  - optional
  - selects the package library target
- `--profile <dev|release>`
  - optional
  - defaults to `dev`
- `--dry-run`
  - optional
  - writes the compile-plan and build directories locally without invoking the compiler
- `--styio-bin <path>`
  - optional for `--dry-run`
  - required for non-dry-run compiler execution unless compiler discovery succeeds through `SPIO_STYIO_BIN`, nearest project-local `spio-toolchain.toml`, or the managed current compiler
- `--locked`
  - optional
  - requires an adjacent `spio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- may reuse project-local vendored snapshots under `.spio/vendor/`
- emits `compile-plan v1` to `.spio/build/<cache-key>/plan.json`
- creates sibling `.spio/build/<cache-key>/artifacts/` and `.spio/build/<cache-key>/diag/`
- limits entry-package selection to root packages of the selected manifest graph
- supports only explicit manifest `lib` and `bin` targets in the current native core
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
- requires `--lib` or `--bin <name>` when the selected package target set is ambiguous
- rejects cyclic graphs and mixed `edition` / `toolchain` tuples that `compile-plan v1` cannot represent
- `--dry-run` does not require compiler probing and does not change `spio machine-info`
- non-dry-run build is gated by the published compatibility matrix and now succeeds against released `styio` compile-plan consumers
- `--json` success for non-dry-run build emits `workflow_success_payloads v1`, including receipt/artifact roots, diagnostics path, and captured stdout/stderr

### `run`

Canonical form:

```text
spio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`
- `--package <package-name>`
  - optional
  - selects a root package from the active graph when the selected manifest exposes multiple roots
  - package name must be in `namespace/name` form
- `--bin <name>`
  - optional
  - selects a specific binary target from the chosen package
- `--profile <dev|release>`
  - optional
  - defaults to `dev`
- `--dry-run`
  - optional
  - writes the compile-plan and build directories locally without invoking the compiler
- `--styio-bin <path>`
  - optional for `--dry-run`
  - required for non-dry-run compiler execution unless compiler discovery succeeds through `SPIO_STYIO_BIN`, nearest project-local `spio-toolchain.toml`, or the managed current compiler
- `--locked`
  - optional
  - requires an adjacent `spio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- may reuse project-local vendored snapshots under `.spio/vendor/`
- emits `compile-plan v1` with `intent = "run"` to `.spio/build/<cache-key>/plan.json`
- supports only explicit manifest `[[bin]]` targets in the current native core
- non-dry-run run now succeeds against released `styio` compile-plan consumers when the published compatibility matrix allows it
- `--json` success for non-dry-run run emits `workflow_success_payloads v1`, including receipt/artifact roots, diagnostics path, and captured stdout/stderr
- rejects `--lib`
- defaults to the unique binary target when the chosen package has exactly one `[[bin]]`
- requires `--bin <name>` when the chosen package has multiple binaries
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
- requires an adjacent fresh `spio.lock` when `--locked` or `--frozen` is set
- `--dry-run` does not require compiler probing and does not change `spio machine-info`
- non-dry-run run is still gated by the published compatibility matrix; under the current bootstrap-only matrix it fails before compiler execution starts

### `test`

Canonical form:

```text
spio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`
- `--package <package-name>`
  - optional
  - selects a root package from the active graph when the selected manifest exposes multiple roots
  - package name must be in `namespace/name` form
- `--test <name>`
  - optional
  - selects a specific explicit manifest test target from the chosen package
- `--profile <dev|release>`
  - optional
  - defaults to `dev`
- `--dry-run`
  - optional
  - writes the compile-plan and build directories locally without invoking the compiler
- `--styio-bin <path>`
  - optional for `--dry-run`
  - required for non-dry-run compiler execution unless compiler discovery succeeds through `SPIO_STYIO_BIN`, nearest project-local `spio-toolchain.toml`, or the managed current compiler
- `--locked`
  - optional
  - requires an adjacent `spio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- may reuse project-local vendored snapshots under `.spio/vendor/`
- emits `compile-plan v1` with `intent = "test"` to `.spio/build/<cache-key>/plan.json`
- supports only explicit manifest `[[test]]` targets in the current native core
- non-dry-run test now succeeds against released `styio` compile-plan consumers when the published compatibility matrix allows it
- `--json` success for non-dry-run test emits `workflow_success_payloads v1`, including receipt/artifact roots, diagnostics path, and captured stdout/stderr
- rejects `--bin` and `--lib`
- defaults to the unique test target when the chosen package has exactly one `[[test]]`
- requires `--test <name>` when the chosen package has multiple tests
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
- requires an adjacent fresh `spio.lock` when `--locked` or `--frozen` is set
- `--dry-run` does not require compiler probing and does not change `spio machine-info`
- non-dry-run test is still gated by the published compatibility matrix; under the current bootstrap-only matrix it fails before compiler execution starts

### `tree`

Canonical form:

```text
spio tree [--manifest-path <path>]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`

Behavior summary:

- resolves the same `single-version-v1` graph that `spio lock` uses
- accepts:
  - the selected package manifest when `[package]` is present
  - workspace members when `[workspace]` is present
  - recursively discovered `path` dependencies
  - pinned `git` dependencies and their transitive manifests at the selected `rev`
  - both `[dependencies]` and `[dev-dependencies]`
- does not require an on-disk `spio.lock`
- does not read, validate, or rewrite the adjacent `spio.lock`
- text output renders canonical package ids as an ASCII dependency tree
- global `--json` returns the resolved root ids plus package records

### `vendor`

Canonical form:

```text
spio vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`
- `--output <path>`
  - optional
  - explicit project-local vendor root
  - if omitted, the current native core writes to `.spio/vendor/` next to the selected manifest
- `--locked`
  - optional
  - requires an adjacent `spio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the active `single-version-v1` graph before copying vendor state
- copies pinned git snapshots into project-local vendor state
- writes vendor metadata to `<vendor-root>/spio-vendor.json`
- defaults to `.spio/vendor/` so vendored snapshots do not collide with ordinary project paths such as local `vendor/` dependencies
- may reuse the selected vendor root during resolution
- requires an adjacent fresh `spio.lock` when `--locked` or `--frozen` is set

### `pack`

Canonical form:

```text
spio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file that defines the active package root
  - defaults to `spio.toml`
- `--package <package-name>`
  - optional
  - explicit root package selection in `namespace/name` form
  - required when the active manifest root exposes multiple candidate root packages and none is the default root package
- `--output <path>`
  - optional
  - explicit archive output path
  - if omitted, the native core writes `<package-root>/dist/<short-name>-<version>.tar`
  - if the path is inside the selected package root, it must live under an excluded generated directory such as `dist/`

Behavior summary:

- selects exactly one local package from the active manifest root
- never talks to `styio` and does not require resolver graph materialization
- writes a deterministic uncompressed tar archive rooted at `<short-name>-<version>/`
- packages canonical `spio.toml` plus regular files recursively discovered under the selected package root
- excludes the adjacent `spio.lock`, generated top-level directories, and combined-workspace member/excluded subtrees
- rejects symlinks and unsupported filesystem node types inside the included tree
- ignores `package.publish`; local packing does not imply publish or registry semantics

### `publish`

Canonical form:

```text
spio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <path-or-url>] [--registry-profile <name>] [--registry-policy-file <path>] [--registry-header <name:value>] [--dry-run]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file that defines the active package root
  - defaults to `spio.toml`
- `--package <package-name>`
  - optional
  - explicit root package selection in `namespace/name` form
  - required when the active manifest root exposes multiple candidate root packages and none is the default root package
- `--output <path>`
  - optional
  - explicit publish-candidate archive output path
  - if omitted, the native core writes `<package-root>/dist/<short-name>-<version>.tar`
- `--registry <path-or-url>`
  - optional for `--dry-run`
  - required for non-dry-run publish
  - selects a local filesystem registry root or an HTTP(S) registry origin
- `--registry-profile <name>`
  - optional
  - only valid for non-dry-run publish to `http://` or `https://` registry roots
  - reserved for private security-module builds
  - public open-source builds reject it unless a private module is linked from `src-private/`
  - cannot be combined with `--registry-policy-file <path>`
- `--registry-policy-file <path>`
  - optional
  - only valid for non-dry-run publish to `http://` or `https://` registry roots
  - reserved for private security-module builds
  - public open-source builds reject it unless a private module is linked from `src-private/`
- `--registry-header <name:value>`
  - optional
  - repeatable
  - only valid for non-dry-run publish to `http://` or `https://` registry roots
  - reserved for private security-module builds
  - public open-source builds reject it unless a private module is linked from `src-private/`
- `--dry-run`
  - optional
  - activates local publish preflight and candidate-archive staging without any registry upload

Behavior summary:

- selects exactly one local package from the active manifest root using the same root-selection rules as `pack`
- requires `package.publish = true`
- allows dependency entries only when they are themselves registry-addressable
- stages the same deterministic source archive shape used by `spio pack`
- non-dry-run `publish` writes into the static registry layout rooted at `--registry <path-or-url>`
- local paths and `file://...` publish directly into the filesystem registry root
- `http://...` and `https://...` publish through anonymous HTTP `PUT`
- when a private security module accepts `--registry-profile <name>`, `--registry-policy-file <path>`, or `--registry-header <name:value>`, those options affect only remote publish against the write origin and do not affect client-side fetch semantics
- publish writes:
  - marker file: `<registry-root>/spio-registry.json`
  - immutable archive blob: `<registry-root>/blobs/sha256/<xx>/<yy>/<sha256>.tar`
  - version entry: `<registry-root>/index/<namespace>/<name>/<version>.json`
- version entries record package name, version, archive digest, archive size, publish timestamp, and dependency metadata for `[dependencies]` and `[dev-dependencies]`
- remote publish currently requires the origin to preserve immutable paths and reject overwrites
- publish JSON stays redacted and may expose only security-provider metadata, security mode, header count, and optional profile name
- auth/account behavior is intentionally kept behind the private security-module boundary
- republishing an existing package version into the same registry fails explicitly

### `tool status`

Canonical surface:

```text
spio tool status [--manifest-path <path>] [--styio-bin <path>] [--json]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - selected project manifest used to resolve the nearest project pin and workspace root
  - when omitted, the current native core reports only managed compiler state
- `--styio-bin <path>`
  - optional
  - explicit compiler binary path used for the active compiler preview in the returned payload
  - when omitted, preview discovery continues through `SPIO_STYIO_BIN`, nearest project-local `spio-toolchain.toml`, and finally the managed current compiler
- `--json`
  - optional
  - returns the published `toolchain_state v1` payload

Behavior summary:

- publishes the machine-readable toolchain and environment payload consumed by `styio-view`
- returns `toolchain`, `project_pin`, `active_compiler`, `current_compiler`, `managed_toolchains`, and `notes`
- reports the selected project pin even when the pinned managed compiler is missing
- reports compiler machine-info whenever the resolved binary can answer `--machine-info=json`
- does not fail only because a discovered compiler is not on the current compile-plan execution path

### `tool install`

Canonical surface:

```text
spio tool install --styio-bin <path>
```

Arguments:

- `--styio-bin <path>`
  - required
  - source path to a local self-contained `styio` executable that already supports `--machine-info=json`

Behavior summary:

- probes the selected compiler through the same published compatibility handshake used by `spio check`
- rejects compilers outside the published `spio/contracts/compat/styio-support.toml` matrix
- installs the compiler under `SPIO_HOME/tools/styio/<channel>/<compiler-version>/bin/styio`
- refreshes the managed default compiler copy at `SPIO_HOME/tools/styio/current/bin/styio`
- writes stable install metadata beside both the versioned install root and the managed current root
- `spio check`, `spio build`, `spio run`, and `spio test` continue compiler discovery through:
  - explicit `--styio-bin <path>`
  - `SPIO_STYIO_BIN`
  - nearest project-local `spio-toolchain.toml`
  - managed current compiler

### `tool use`

Canonical surface:

```text
spio tool use --version <compiler-version> [--channel <channel>]
```

Arguments:

- `--version <compiler-version>`
  - required
  - selects an already installed managed `styio` compiler version
- `--channel <channel>`
  - optional
  - narrows the lookup to one managed release channel such as `stable`
  - required only when the selected version is ambiguous across installed channels

Behavior summary:

- selects an already installed compiler under `SPIO_HOME/tools/styio/<channel>/<compiler-version>/`
- re-validates the selected managed compiler through the same published compatibility handshake used by `spio check`
- refreshes the managed default compiler copy at `SPIO_HOME/tools/styio/current/bin/styio` from the selected versioned install
- rewrites the managed current metadata to reflect the newly active compiler
- fails when the selected version is missing or ambiguous across installed channels

### `tool pin`

Canonical surface:

```text
spio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]
```

Arguments:

- `--version <compiler-version>`
  - required unless `--clear` is set
  - selects an already installed managed `styio` compiler version to pin for the selected project
- `--channel <channel>`
  - optional
  - narrows version lookup to one managed release channel such as `stable`
  - if omitted, pinning fails when the requested version is ambiguous across installed channels
- `--clear`
  - optional
  - removes the project-local toolchain pin instead of writing one
- `--manifest-path <path>`
  - optional
  - selected project manifest used to locate the pin file path
  - defaults to `spio.toml`

Behavior summary:

- validates the selected manifest before changing project-local toolchain state
- writes the project-local toolchain pin to `<selected-manifest-dir>/spio-toolchain.toml`
- stores an explicit managed `channel` and `version`, even if `--channel` was omitted on the command line
- re-validates the selected managed compiler through the same published compatibility handshake used by `spio check`
- pin discovery for `spio check`, `spio build`, `spio run`, and `spio test` searches upward from the selected manifest directory for the nearest `spio-toolchain.toml`
- a discovered project-local toolchain pin overrides the managed current compiler but not explicit `--styio-bin` or `SPIO_STYIO_BIN`
- a discovered project-local toolchain pin fails if the pinned managed compiler is missing

## 5. Helper Script Entry Index

### `scripts/spio`

Canonical form:

```text
./scripts/spio [spio-args...]
```

Behavior:

- builds the native binary on demand if the configured build directory does not already contain it
- forwards all remaining arguments to the native `spio` executable

### `scripts/native-check.sh`

Canonical form:

```text
./scripts/native-check.sh
```

Behavior:

- configures the native CMake build
- builds the project
- runs the native test suite

### `scripts/extractability-check.sh`

Canonical form:

```text
./scripts/extractability-check.sh
```

Behavior:

- copies a clean subtree to a temporary directory
- runs `scripts/native-check.sh` inside the copied tree

### `scripts/check_no_binaries.py`

Canonical form:

```text
./scripts/check_no_binaries.py [--repo-root <path>] [--mode auto|tracked|all] [--allow-glob <pattern> ...] [--policy <path>] [--no-policy-allowlist]
```

Arguments:

- `--repo-root <path>`
  - optional
  - defaults to the repository root
- `--mode auto|tracked|all`
  - optional
  - `auto` uses tracked files in git repositories and full-tree files in exported trees
- `--allow-glob <pattern>`
  - optional
  - repeatable
  - allows explicit binary paths when intentionally tracked
- `--policy <path>`
  - optional
  - override artifact policy JSON path
  - defaults to `scripts/artifact-policy.json`
- `--no-policy-allowlist`
  - optional
  - disables policy-managed binary allowlist (`tracked_binary_allow_globs`)

Behavior:

- validates that selected files do not contain binary content
- applies policy-managed binary allowlist unless disabled
- exits non-zero on the first binary-violation set

### `scripts/repo-hygiene-check.py`

Canonical form:

```text
./scripts/repo-hygiene-check.py [--repo-root <path>] [--mode auto|tracked|all] [--skip-doc-check]
```

Arguments:

- `--repo-root <path>`
  - optional
  - defaults to the repository root
- `--mode auto|tracked|all`
  - optional
  - `tracked` checks only tracked files and `all` checks complete exported trees
- `--skip-doc-check`
  - optional
  - skips verification that governance and operations docs reference gate entrypoints

Behavior:

- rejects forbidden generated/private paths
- verifies required `.gitignore` patterns
- validates gate-document references unless skipped

### `scripts/docs-index.py`

Canonical form:

```text
./scripts/docs-index.py --write
./scripts/docs-index.py --check
```

Behavior:

- rewrites generated `INDEX.md` files for approved docs collections
- verifies that generated indexes are current
- must be re-run after docs-tree changes that affect collection membership or summaries

### `scripts/docs-lifecycle.py`

Canonical form:

```text
./scripts/docs-lifecycle.py refresh
./scripts/docs-lifecycle.py validate
```

Behavior:

- `refresh` synchronizes `docs/archive/ARCHIVE-MANIFEST.json` and `docs/archive/ARCHIVE-LEDGER.md`
- `validate` checks lifecycle directories, rollups, history naming, and archive-ledger freshness

### `scripts/docs-audit.py`

Canonical form:

```text
./scripts/docs-audit.py
```

Behavior:

- validates required docs collections and entry files
- checks `Purpose` / `Last updated` metadata on tracked docs
- runs `scripts/docs-index.py --check`
- runs `scripts/docs-lifecycle.py validate`

### `scripts/perf-gate.py`

Canonical form:

```text
./scripts/perf-gate.py [--json] [--runs <count>] [--warmup-runs <count>] [--threshold-percent <percent>] [--baseline <path>] [--update-baseline]
```

Arguments:

- `--json`
  - optional
  - emits a machine-readable summary payload
- `--runs <count>`
  - optional
  - measured runs per benchmark after warmup
- `--warmup-runs <count>`
  - optional
  - warmup iterations discarded from measurement
- `--threshold-percent <percent>`
  - optional
  - allowed median regression over baseline
- `--baseline <path>`
  - optional
  - baseline file path
- `--update-baseline`
  - optional
  - writes current measurements as baseline
  - rejected in CI mode

Behavior:

- executes CLI benchmark set in isolated temporary project roots
- compares benchmark medians against committed baseline
- fails when any benchmark exceeds configured regression threshold

### `scripts/delivery-gate.py`

Canonical form:

```text
./scripts/delivery-gate.py [--json]
```

Arguments:

- `--json`
  - optional
  - emits a machine-readable summary payload

Behavior:

- exports a clean delivery tree using `scripts/copy-to-external-repo.sh`
- validates required/forbidden paths in the export
- runs binary, hygiene, docs, and native checks inside the exported tree

### `scripts/submit-gate.py`

Canonical form:

```text
./scripts/submit-gate.py [--profile pre-push|ci|release] [--styio-bin <path>] [--feature-config <path>] [--json]
```

Arguments:

- `--profile pre-push|ci|release`
  - optional
  - defaults to `pre-push`
- `--styio-bin <path>`
  - optional
  - enables release-mode `styio` compatibility validation
- `--feature-config <path>`
  - optional
  - JSON feature-flag file for release/styio/cloud checks
  - defaults to `scripts/submit-gate.features.json`
  - an example payload is tracked at `scripts/submit-gate.features.example.json`
  - missing or invalid config keeps release/styio/cloud checks disabled and emits warnings
- `--json`
  - optional
  - emits a machine-readable summary payload

Behavior:

- runs quality, regression, performance, and delivery gates in a fixed order
- includes docs governance validation before native/performance/delivery checks
- runs `styio` compatibility only when both release profile and feature flags enable it, with `--styio-bin` provided
- emits warnings instead of failing when disabled release/styio/cloud checks are requested
- exits non-zero on first failing gate

### `scripts/install-git-hooks.sh`

Canonical form:

```text
./scripts/install-git-hooks.sh
```

Behavior:

- installs `.git/hooks/pre-push`
- pre-push hook executes `python3 scripts/submit-gate.py --profile pre-push`

### `scripts/preflight-readiness-check.py`

Canonical form:

```text
./scripts/preflight-readiness-check.py [--styio-bin <path>] [--json]
```

Arguments:

- `--styio-bin <path>`
  - optional
  - compiler path used for the compatibility preflight step
- `--json`
  - optional
  - emits a machine-readable summary payload

Behavior:

- runs `scripts/native-check.sh`
- runs `scripts/extractability-check.sh`
- when `--styio-bin <path>` is set:
  - runs `spio --json check --styio-bin <path>`
  - runs `scripts/styio-interface-gate.py --styio-bin <path>`

### `scripts/styio-interface-gate.py`

Canonical form:

```text
./scripts/styio-interface-gate.py --styio-bin <path> [--spio-bin <path>] [--manifest-path <path>] [--require-compile-plan] [--json]
```

Arguments:

- `--styio-bin <path>`
  - required
  - published compiler binary under validation
- `--spio-bin <path>`
  - optional
  - `spio` wrapper or binary used for black-box compatibility checks
  - defaults to `./scripts/spio`
- `--manifest-path <path>`
  - optional
  - manifest used for the `spio check` compatibility step
  - defaults to the repository single-package fixture for handshake-only runs
- `--require-compile-plan`
  - optional
  - also validates direct `styio --compile-plan <path>` execution against a local dry-run plan
- `--json`
  - optional
  - emits a machine-readable summary payload

Behavior:

- probes `styio --machine-info=json`
- validates required handshake fields and types
- runs `spio --json check --styio-bin <path>` as a black-box compatibility step
- when `--require-compile-plan` is set:
  - creates a temporary sample package if needed
  - runs `spio build --dry-run`
  - invokes `styio --compile-plan <path>` directly
  - verifies that the declared output directories are materialized

### `scripts/registry-server-gate.py`

Canonical form:

```text
./scripts/registry-server-gate.py (--registry-root <path-or-url> | [--publish-root <path-or-url>] [--fetch-root <path-or-url>]) [--publish-profile <name>] [--publish-policy-file <path>] [--publish-header <name:value>] [--sync-timeout-seconds <seconds>] [--spio-bin <path>] [--json]
```

Arguments:

- `--registry-root <path-or-url>`
  - optional unless both `--publish-root` and `--fetch-root` are provided
  - single registry root used for both publish and fetch validation
- `--publish-root <path-or-url>`
  - optional
  - write root used for publish validation
  - may differ from the read root when upload and download origins are split
- `--fetch-root <path-or-url>`
  - optional
  - read root used for fetch validation
  - may differ from the write root when upload and download origins are split
- `--publish-header <name:value>`
  - optional
  - repeatable
  - forwarded only to remote publish requests against the configured write root
- `--publish-policy-file <path>`
  - optional
  - forwarded only to remote publish requests against the configured write root
  - points at the same write-origin policy file format accepted by `spio publish --registry-policy-file`
- `--publish-profile <name>`
  - optional
  - reserved for private security-module builds
  - written into the gate's isolated deployment-owned state only when the private module is under test
  - cannot be combined with `--publish-policy-file <path>`
- `--sync-timeout-seconds <seconds>`
  - optional
  - retry budget for publish-to-fetch synchronization when the read root lags the write root
  - defaults to `0`
- `--spio-bin <path>`
  - optional
  - `spio` wrapper or binary used for the black-box publish/fetch checks
  - defaults to `./scripts/spio`
- `--json`
  - optional
  - emits a machine-readable summary payload

Behavior:

- creates an isolated temporary `SPIO_HOME`
- publishes a temporary package into the configured write root
- validates the publish JSON payload shape
- verifies that duplicate publish is rejected
- fetches the newly published package from the configured read root
- when a private security module is under test, can synthesize a write-origin profile under isolated `SPIO_HOME` through `--publish-profile <name>`
- when a private security module is under test, can attach a write-origin policy file through `--publish-policy-file <path>`
- when a private security module is under test, can attach explicit write-origin headers through `--publish-header <name:value>`
- retries fetch within `--sync-timeout-seconds` when publish and fetch roots are decoupled

### `scripts/registry-promote.py`

Canonical form:

```text
./scripts/registry-promote.py --source-root <path-or-file-url> --dest-root <path-or-file-url> [--package <namespace/name>] [--version <x.y.z>] [--json]
```

Arguments:

- `--source-root <path-or-file-url>`
  - required
  - writable source registry root that already contains canonical marker, index entries, and blobs
- `--dest-root <path-or-file-url>`
  - required
  - read-side registry root that will serve the promoted objects
- `--package <namespace/name>`
  - optional
  - limits promotion to one package namespace/name
- `--version <x.y.z>`
  - optional
  - limits promotion to one specific package version
  - requires `--package`
- `--json`
  - optional
  - emits a machine-readable summary payload

Behavior:

- supports only local paths and `file://` roots
- validates the source registry marker
- copies marker, version entries, and referenced immutable blobs into the destination root
- treats destination objects as immutable and fails if an existing object differs from the source
- supports idempotent repeated promotion runs

### `scripts/copy-to-external-repo.sh`

Canonical form:

```text
./scripts/copy-to-external-repo.sh [target-dir]
```

Arguments:

- `[target-dir]`
  - optional
  - copy destination
  - defaults to `$HOME/DevSpace/Unka-Malloc/styio-spio`

Behavior:

- reads rsync exclude patterns from `scripts/artifact-policy.json` through `scripts/artifact-policy-rsync-excludes.py`
- keeps export filtering aligned with repository hygiene and delivery gates

### `scripts/artifact-policy-rsync-excludes.py`

Canonical form:

```text
./scripts/artifact-policy-rsync-excludes.py [--policy <path>]
```

Arguments:

- `--policy <path>`
  - optional
  - override artifact policy JSON path
  - defaults to `scripts/artifact-policy.json`

Behavior:

- emits newline-delimited `rsync --exclude` patterns
- intended for machine consumption by `scripts/copy-to-external-repo.sh`

## 6. Public Environment Variables

- `SPIO_STYIO_BIN`
  - external compiler path used by `spio build`, `spio run`, `spio test`, and `spio check` when `--styio-bin` is not passed
  - takes precedence over any project-local toolchain pin or compiler installed through `spio tool install`
- `SPIO_HOME`
  - source cache root used by resolver-backed commands such as `spio add`, `spio check`, `spio fetch`, `spio lock`, and `spio tree`
  - managed tool install root for `spio tool install`, `spio tool use`, and `spio tool pin`
  - defaults to `~/.spio` when not set
- `SPIO_BUILD_DIR`
  - build directory used by `scripts/spio` and `scripts/native-check.sh`

## 7. Update Rule

When any public argument, helper-script argument, or public environment variable changes:

1. update this index first
2. update the owning contract or operational document if needed
3. update implementation and tests in the same change
