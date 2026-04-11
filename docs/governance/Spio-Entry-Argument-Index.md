# Spio Entry and Argument Index

**Purpose:** Provide the single entrypoint index for user-visible `spio` arguments, repository-maintainer script arguments, and public environment variables so parameter lists do not drift across code, scripts, and contract documents.

**Last updated:** 2026-04-12

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
- `spio check [--manifest-path <path>] [--styio-bin <path>]`
- `spio add <package-name> (--path <path> | --git <source> --rev <rev>) [--alias <name>] [--dev] [--manifest-path <path>]`
- `spio remove <alias-or-package> [--dev] [--manifest-path <path>]`
- `spio fetch [--manifest-path <path>]`
- `spio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]`
- `spio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]`
- `spio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]`
- `spio lock [--manifest-path <path>] [--check]`
- `spio tree [--manifest-path <path>]`
- `spio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]`

### Reserved but Not Yet Implemented

- `spio publish`
- `spio tool install`

During the current phase, reserved commands are part of the public command surface but remain stubbed.

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
spio check [--manifest-path <path>] [--styio-bin <path>]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file
  - defaults to `spio.toml`
- `--styio-bin <path>`
  - optional
  - explicit compiler binary path used for compatibility probing
  - if omitted, `SPIO_STYIO_BIN` may provide the compiler path

Behavior summary:

- always validates the manifest
- resolves the active `single-version-v1` graph after manifest validation succeeds
- validates adjacent `spio.lock` when present
- treats a parse-valid but content-stale adjacent `spio.lock` as a failure
- may use `SPIO_HOME` when pinned git dependencies are present
- if a compiler path is available, also performs the `styio --machine-info=json` handshake

### `add`

Canonical form:

```text
spio add <package-name> (--path <path> | --git <source> --rev <rev>) [--alias <name>] [--dev] [--manifest-path <path>]
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
spio fetch [--manifest-path <path>]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`

Behavior summary:

- resolves the same `single-version-v1` graph used by `spio lock` and `spio tree`
- materializes dependency source state, especially pinned git mirrors and snapshots under `SPIO_HOME`
- does not require an on-disk `spio.lock`
- does not read, validate, or rewrite the adjacent `spio.lock`

### `lock`

Canonical form:

```text
spio lock [--manifest-path <path>] [--check]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `spio.toml`
- `--check`
  - optional
  - compares generated canonical output to the adjacent `spio.lock` without rewriting it

Behavior summary:

- resolves a `single-version-v1` graph from:
  - the selected package manifest when `[package]` is present
  - workspace members when `[workspace]` is present
  - recursively discovered `path` dependencies
  - pinned `git` dependencies and their transitive manifests at the selected `rev`
  - both `[dependencies]` and `[dev-dependencies]`
- writes or checks only the adjacent `spio.lock` next to the selected manifest
- uses `SPIO_HOME` to cache git source mirrors and extracted pinned snapshots
- returns the lock exit code when `--check` finds a missing or stale lockfile

### `build`

Canonical form:

```text
spio build [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]
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
  - required for non-dry-run compiler execution unless `SPIO_STYIO_BIN` is set

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- emits `compile-plan v1` to `.spio/build/<cache-key>/plan.json`
- creates sibling `.spio/build/<cache-key>/artifacts/` and `.spio/build/<cache-key>/diag/`
- limits entry-package selection to root packages of the selected manifest graph
- supports only explicit manifest `lib` and `bin` targets in the current native core
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
- requires `--lib` or `--bin <name>` when the selected package target set is ambiguous
- rejects cyclic graphs and mixed `edition` / `toolchain` tuples that `compile-plan v1` cannot represent
- `--dry-run` does not require compiler probing and does not change `spio machine-info`
- non-dry-run build is still gated by the published compatibility matrix; under the current bootstrap-only matrix it fails before compiler execution starts

### `run`

Canonical form:

```text
spio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]
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
  - required for non-dry-run compiler execution unless `SPIO_STYIO_BIN` is set

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- emits `compile-plan v1` with `intent = "run"` to `.spio/build/<cache-key>/plan.json`
- supports only explicit manifest `[[bin]]` targets in the current native core
- rejects `--lib`
- defaults to the unique binary target when the chosen package has exactly one `[[bin]]`
- requires `--bin <name>` when the chosen package has multiple binaries
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
- `--dry-run` does not require compiler probing and does not change `spio machine-info`
- non-dry-run run is still gated by the published compatibility matrix; under the current bootstrap-only matrix it fails before compiler execution starts

### `test`

Canonical form:

```text
spio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>]
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
  - required for non-dry-run compiler execution unless `SPIO_STYIO_BIN` is set

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- emits `compile-plan v1` with `intent = "test"` to `.spio/build/<cache-key>/plan.json`
- supports only explicit manifest `[[test]]` targets in the current native core
- rejects `--bin` and `--lib`
- defaults to the unique test target when the chosen package has exactly one `[[test]]`
- requires `--test <name>` when the chosen package has multiple tests
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
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

### `tool install`

Canonical surface:

```text
spio tool install
```

Status:

- reserved in the public command set
- no detailed argument list is frozen yet
- current phase returns the bootstrap not-implemented exit code

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

## 6. Public Environment Variables

- `SPIO_STYIO_BIN`
  - external compiler path used by `spio build` and `spio check` when `--styio-bin` is not passed
- `SPIO_HOME`
  - source cache root used by resolver-backed commands such as `spio add`, `spio check`, `spio fetch`, `spio lock`, and `spio tree`
  - defaults to `~/.spio` when not set
- `SPIO_BUILD_DIR`
  - build directory used by `scripts/spio` and `scripts/native-check.sh`

## 7. Update Rule

When any public argument, helper-script argument, or public environment variable changes:

1. update this index first
2. update the owning contract or operational document if needed
3. update implementation and tests in the same change
