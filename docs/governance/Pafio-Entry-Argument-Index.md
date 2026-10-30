# Pafio Entry and Argument Index

**Purpose:** Provide the single entrypoint index for user-visible `pafio` arguments, repository-maintainer script arguments, and public environment variables so parameter lists do not drift across code, scripts, and contract documents.

**Last updated:** 2026-04-20

## 1. Ownership

This document owns:

- detailed per-command argument spellings
- positional vs flag shapes for implemented `pafio` commands
- helper-script argument summaries
- public environment variables consumed by `pafio` entrypoints

This document does not own:

- exit-code policy
- machine-readable error object shape
- manifest or lockfile schema rules

Those remain in the existing governance contracts.

## 2. Global CLI Shape

Canonical top-level form:

```text
pafio [--help] [--version] [--json] <command> [command-args...]
```

### Global Flags

- `--help`
  - prints top-level command usage
  - the native core treats this as a top-level flag before the command name
- `--version`
  - prints `pafio <version>`
  - does not accept extra arguments
- `--json`
  - requests machine-readable diagnostics for command failures
  - may appear before the command name

## 3. Command Index

### Implemented Native Commands

- `pafio machine-info [--json]`
- `pafio project-graph --json [--manifest-path <path>] [--locked|--offline|--frozen]`
- `pafio cloud status --json [--manifest-path <path>]`
- `pafio cloud plan --json <build|run|test> [minimal] [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib|--test <name>] [--profile <dev|release>] [--source-root <path>] [--source-rev <rev>] [--yes|--no-fetch|--non-interactive] [--locked|--offline|--frozen]`
- `pafio new <package-name> [directory] [--lib|--bin]`
- `pafio init [--name <package-name>] [--lib|--bin]`
- `pafio use <binary|build> [--manifest-path <path>]`
- `pafio set channel [as] <stable|nightly> [--manifest-path <path>]`
- `pafio set build [as] <minimal> [--manifest-path <path>]`
- `pafio set risk [as] <trusted-internal|partner-controlled|untrusted-user> [--manifest-path <path>]`
- `pafio set lane [as] <isolated|warm-shared> [--manifest-path <path>]`
- `pafio set security [as] <sandbox-default|partner-restricted|trusted-warm> [--manifest-path <path>]`
- `pafio install styio[@latest] [--source-root <path>] [--source-rev <ref>] [--channel <stable|nightly>] [--build <minimal>] [--yes|--no-fetch|--offline|--non-interactive]`
- `pafio check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen]`
- `pafio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) [--alias <name>] [--dev] [--manifest-path <path>]`
- `pafio remove <alias-or-package> [--dev] [--manifest-path <path>]`
- `pafio sync [--manifest-path <path>] [--locked|--offline|--frozen]`
- `pafio fetch [--manifest-path <path>] [--locked|--offline|--frozen]`
- `pafio build [minimal] [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--source-root <path>] [--source-rev <rev>] [--yes|--no-fetch|--non-interactive] [--locked|--offline|--frozen]`
- `pafio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--source-root <path>] [--source-rev <rev>] [--yes|--no-fetch|--non-interactive] [--locked|--offline|--frozen]`
- `pafio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--source-root <path>] [--source-rev <rev>] [--yes|--no-fetch|--non-interactive] [--locked|--offline|--frozen]`
- `pafio lock [--manifest-path <path>] [--check] [--offline]`
- `pafio tree [--manifest-path <path>]`
- `pafio vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]`
- `pafio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]`
- `pafio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <path-or-url>] [--registry-profile <name>] [--registry-policy-file <path>] [--registry-header <name:value>] [--dry-run]`
- `pafio tool install --styio-bin <path>`
- `pafio tool status --json [--manifest-path <path>]`
- `pafio tool use --version <compiler-version> [--channel <channel>]`
- `pafio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]`

## 4. Detailed Command Arguments

### `machine-info`

Canonical forms:

```text
pafio machine-info
pafio machine-info --json
```

Rules:

- output is machine-readable JSON in the current native core
- `--json` is accepted as the explicit compatibility spelling
- no other command-specific arguments are valid

### `project-graph`

Canonical form:

```text
pafio project-graph --json [--manifest-path <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--json`
  - required in the current native core
  - machine-readable success output is the only supported form
- `--manifest-path <path>`
  - optional
  - path to the manifest file
  - defaults to `pafio.toml`
- `--locked`
  - optional
  - requires the adjacent `pafio.lock` to match the active graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- validates the selected manifest
- resolves the active `single-version-v1` graph
- reports packages, dependency edges, targets, toolchain state, managed toolchains, lock state, vendor state, source state, and package distribution
- embeds the resolved local cloud execution policy alongside the project-local toolchain state

### `cloud status`

Canonical form:

```text
pafio cloud status --json [--manifest-path <path>]
```

Arguments:

- `status`
  - required subcommand
  - prints the resolved local cloud execution policy
- `--json`
  - required in the current native core
  - machine-readable success output is the only supported form
- `--manifest-path <path>`
  - optional
  - selected project manifest used to locate the project-local toolchain state
  - defaults to `pafio.toml`

Behavior summary:

- validates the selected manifest
- loads or initializes adjacent `pafio-toolchain.lock`
- resolves local cloud policy from project-local state
- reports both persisted preferences and resolved execution policy

### `cloud plan`

Canonical form:

```text
pafio cloud plan --json <build|run|test> [minimal] [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib|--test <name>] [--profile <dev|release>] [--source-root <path>] [--source-rev <rev>] [--yes|--no-fetch|--non-interactive] [--locked|--offline|--frozen]
```

Arguments:

- `plan`
  - required subcommand
  - renders a normalized control-plane job request
- `--json`
  - required in the current native core
  - machine-readable success output is the only supported form
- `<build|run|test>`
  - required action
  - selects the workflow intent encoded in the job request
- `[minimal]`
  - optional build-mode selector
  - only valid when the action is `build`
- `--manifest-path <path>`
  - optional
  - defaults to `pafio.toml`
- `--package <package-name>`
  - optional
- `--bin <name>`
  - optional
  - valid for `build` and `run`
- `--lib`
  - optional
  - valid only for `build`
- `--test <name>`
  - optional
  - valid only for `test`
- `--profile <dev|release>`
  - optional
- `--source-root <path>`
  - optional
  - valid only when the project uses `pafio use build`
- `--source-rev <rev>`
  - optional
  - valid only when the project uses `pafio use build`
- `--yes`
  - optional
- `--no-fetch`
  - optional
- `--non-interactive`
  - optional
- `--locked`
  - optional
- `--offline`
  - optional
- `--frozen`
  - optional

Behavior summary:

- validates and normalizes the same target-selection grammar used by local `build/run/test`
- resolves the project-local toolchain state and local cloud-execution policy
- emits the frozen `build_job_request v1` request body for `POST /api/styio-platform/v1/jobs`
- does not execute the build and does not contact a remote scheduler

### `new`

Canonical form:

```text
pafio new <package-name> [directory] [--lib|--bin]
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
pafio init [--name <package-name>] [--lib|--bin]
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

### `use`

Canonical form:

```text
pafio use <binary|build> [--manifest-path <path>]
```

Arguments:

- `<binary|build>`
  - required
  - selects the project-local toolchain mode
- `--manifest-path <path>`
  - optional
  - selected project manifest used to locate the project-local toolchain state
  - defaults to `pafio.toml`

Behavior summary:

- validates the selected manifest before changing project-local toolchain state
- writes or refreshes `<selected-manifest-dir>/pafio-toolchain.lock`
- `binary` keeps build/run/test on the published-compiler path
- `build` opts the project into source-toolchain build orchestration
- does not modify the adjacent `pafio.lock`

### `set`

Canonical forms:

```text
pafio set channel as <stable|nightly> [--manifest-path <path>]
pafio set build as <minimal> [--manifest-path <path>]
pafio set risk as <trusted-internal|partner-controlled|untrusted-user> [--manifest-path <path>]
pafio set lane as <isolated|warm-shared> [--manifest-path <path>]
pafio set security as <sandbox-default|partner-restricted|trusted-warm> [--manifest-path <path>]
```

Compatibility forms accepted by the parser:

```text
pafio set channel <stable|nightly> [--manifest-path <path>]
pafio set build <minimal> [--manifest-path <path>]
pafio set risk <trusted-internal|partner-controlled|untrusted-user> [--manifest-path <path>]
pafio set lane <isolated|warm-shared> [--manifest-path <path>]
pafio set security <sandbox-default|partner-restricted|trusted-warm> [--manifest-path <path>]
```

Arguments:

- `channel`
  - selects the project-local release channel
- `build`
  - selects the project-local build mode
- `risk`
  - selects the project-local cloud risk class
- `lane`
  - selects the project-local preferred execution lane
- `security`
  - selects the project-local security profile
- `as`
  - optional for parsing compatibility
  - official docs, help output, and diagnostics keep the `as` spelling
- `<stable|nightly>`
  - valid values for `channel`
- `<minimal>`
  - the only currently supported build mode
- `<trusted-internal|partner-controlled|untrusted-user>`
  - valid values for `risk`
- `<isolated|warm-shared>`
  - valid values for `lane`
- `<sandbox-default|partner-restricted|trusted-warm>`
  - valid values for `security`
- `--manifest-path <path>`
  - optional
  - selected project manifest used to locate the project-local toolchain state
  - defaults to `pafio.toml`

Behavior summary:

- validates the selected manifest before changing project-local toolchain state
- writes or refreshes `<selected-manifest-dir>/pafio-toolchain.lock`
- `pafio set channel as ...` updates the selected project release channel for both `binary` and `build` mode
- `pafio set build as minimal` persists the current build mode default used by bare `pafio build`
- `pafio set risk as ...` persists the project-local cloud risk class
- `pafio set lane as ...` persists the preferred execution lane
- `pafio set security as ...` persists the project-local security profile
- does not modify the adjacent `pafio.lock`

### `check`

Canonical form:

```text
pafio check [--manifest-path <path>] [--styio-bin <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file
  - defaults to `pafio.toml`
- `--styio-bin <path>`
  - optional
  - explicit compiler binary path used for compatibility probing
  - if omitted, compiler discovery continues through `PAFIO_STYIO_BIN`, nearest project-local `pafio-toolchain.toml`, and finally the managed current compiler
- `--locked`
  - optional
  - requires an adjacent `pafio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- always validates the manifest
- resolves the active `single-version-v1` graph after manifest validation succeeds
- validates adjacent `pafio.lock` when present
- requires an adjacent fresh `pafio.lock` when `--locked` or `--frozen` is set
- treats a parse-valid but content-stale adjacent `pafio.lock` as a failure
- may use `PAFIO_HOME` when pinned git dependencies are present
- may prefer project-local vendored snapshots under `.pafio/vendor/` when present
- if a compiler path is available, also performs the `styio --machine-info=json` handshake

### `add`

Canonical form:

```text
pafio add <package-name> (--path <path> | --git <source> --rev <rev> | --registry <url> --version <x.y.z>) [--alias <name>] [--dev] [--manifest-path <path>]
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
  - defaults to `pafio.toml`

Behavior summary:

- requires the selected manifest to define `[package]`
- writes canonical manifest output after the edit succeeds
- always materializes `package = "<namespace/name>"` in the dependency entry
- rejects duplicate dependency aliases and duplicate dependency package identities across both dependency sections
- refreshes the adjacent `pafio.lock` through the active resolver before returning success
- rolls back manifest and adjacent lockfile changes if the post-edit resolver step fails
- registry adds canonicalize dependency tables as `package`, `version`, then `registry`

### `remove`

Canonical form:

```text
pafio remove <alias-or-package> [--dev] [--manifest-path <path>]
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
  - defaults to `pafio.toml`

Behavior summary:

- requires the selected manifest to define `[package]`
- rewrites canonical manifest output after the edit succeeds
- refreshes the adjacent `pafio.lock` through the active resolver before returning success
- rolls back manifest and adjacent lockfile changes if the post-edit resolver step fails
- rejects ambiguous remove targets

### `sync`

Canonical form:

```text
pafio sync [--manifest-path <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the project manifest
  - defaults to `pafio.toml`
- `--locked`
  - optional
  - requires the adjacent `pafio.lock` to match the active graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- validates the selected manifest
- resolves the active graph
- writes or refreshes the adjacent lockfile unless locked mode is requested
- materializes dependency sources through the same resolver/cache path as `fetch`
- reports lockfile mode as `write`, `unchanged`, or `locked`

### `fetch`

Canonical form:

```text
pafio fetch [--manifest-path <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `pafio.toml`
- `--locked`
  - optional
  - requires an adjacent `pafio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the same `single-version-v1` graph used by `pafio lock` and `pafio tree`
- materializes dependency source state, including pinned git mirrors/snapshots and registry metadata/blob/checkout cache state under `PAFIO_HOME`
- may reuse project-local vendored snapshots under `.pafio/vendor/`
- requires an adjacent fresh `pafio.lock` when `--locked` or `--frozen` is set
- does not require an on-disk `pafio.lock` unless `--locked` or `--frozen` is set
- never rewrites the adjacent `pafio.lock`

### `lock`

Canonical form:

```text
pafio lock [--manifest-path <path>] [--check] [--offline]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `pafio.toml`
- `--check`
  - optional
  - compares generated canonical output to the adjacent `pafio.lock` without rewriting it
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
- writes or checks only the adjacent `pafio.lock` next to the selected manifest
- uses `PAFIO_HOME` to cache git source mirrors and extracted pinned snapshots
- may reuse project-local vendored snapshots under `.pafio/vendor/`
- returns the lock exit code when `--check` finds a missing or stale lockfile

### `build`

Canonical form:

```text
pafio build [minimal] [--manifest-path <path>] [--package <package-name>] [--bin <name>|--lib] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--source-root <path>] [--source-rev <rev>] [--yes|--no-fetch|--non-interactive] [--locked|--offline|--frozen]
```

Arguments:

- `[minimal]`
  - optional positional build mode
  - the only currently supported build mode
  - bare `pafio build` normalizes to the same mode through project defaults
- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `pafio.toml`
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
  - valid only when the selected project toolchain mode is `binary`
  - overrides published compiler discovery for the current invocation
- `--source-root <path>`
  - optional
  - valid only when the selected project toolchain mode is `build`
  - points to an already available `styio` source tree
- `--source-rev <rev>`
  - optional
  - valid only when the selected project toolchain mode is `build`
  - overrides the source revision or branch selected from the project-local channel
- `--yes`
  - optional
  - valid only when the selected project toolchain mode is `build`
  - auto-confirms fetching the official `styio` source tree when it is missing locally
- `--no-fetch`
  - optional
  - valid only when the selected project toolchain mode is `build`
  - forbids auto-fetching the official `styio` source tree
- `--non-interactive`
  - optional
  - valid only when the selected project toolchain mode is `build`
  - rejects the fetch prompt instead of waiting for TTY input
- `--locked`
  - optional
  - requires an adjacent `pafio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
  - in `build` mode it also forbids fetching or updating the official `styio` source tree
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- may reuse project-local vendored snapshots under `.pafio/vendor/`
- emits `compile-plan v1` to `.pafio/build/<cache-key>/plan.json`
- creates sibling `.pafio/build/<cache-key>/artifacts/` and `.pafio/build/<cache-key>/diag/`
- limits entry-package selection to root packages of the selected manifest graph
- supports only explicit manifest `lib` and `bin` targets in the current native core
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
- requires `--lib` or `--bin <name>` when the selected package target set is ambiguous
- rejects cyclic graphs and mixed `edition` / `toolchain` tuples that `compile-plan v1` cannot represent
- `--dry-run` does not require compiler probing and does not change `pafio machine-info`
- records or reuses project-local toolchain state from `pafio-toolchain.lock`
- in `binary` mode, non-dry-run compiler execution resolves the published compiler through:
  - explicit `--styio-bin <path>`
  - `PAFIO_STYIO_BIN`
  - nearest project-local `pafio-toolchain.toml`
  - managed current compiler under `PAFIO_HOME/tools/styio/current/bin/styio`
- in `binary` mode, non-dry-run build still requires the published compatibility matrix to allow compile-plan v1
- in `build` mode, `pafio` resolves a local source root from:
  - explicit `--source-root <path>`
  - `PAFIO_STYIO_SOURCE_ROOT`
  - cached official source checkout under `PAFIO_HOME/src/styio/...`
- in `build` mode, the default official source origin is `https://github.com/SymPolicy/Styio.git`
- in `build` mode, project channel selection maps to the same-named source branch:
  - `stable` -> `stable`
  - `nightly` -> `nightly`
- in `build` mode, missing local source may trigger the interactive fetch prompt:
  - `styio source tree not found locally. Fetch from official Styio source origin? [Y/n]`
- in `build` mode, the source-built compiler is cached under `PAFIO_HOME/toolchains/source/...`
- in `build` mode, the resolved source revision is written back into `pafio-toolchain.lock`

### `run`

Canonical form:

```text
pafio run [--manifest-path <path>] [--package <package-name>] [--bin <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--source-root <path>] [--source-rev <rev>] [--yes|--no-fetch|--non-interactive] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `pafio.toml`
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
  - valid only when the selected project toolchain mode is `binary`
  - overrides published compiler discovery for the current invocation
- `--source-root <path>`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--source-rev <rev>`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--yes`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--no-fetch`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--non-interactive`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--locked`
  - optional
  - requires an adjacent `pafio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- may reuse project-local vendored snapshots under `.pafio/vendor/`
- emits `compile-plan v1` with `intent = "run"` to `.pafio/build/<cache-key>/plan.json`
- supports only explicit manifest `[[bin]]` targets in the current native core
- rejects `--lib`
- defaults to the unique binary target when the chosen package has exactly one `[[bin]]`
- requires `--bin <name>` when the chosen package has multiple binaries
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
- requires an adjacent fresh `pafio.lock` when `--locked` or `--frozen` is set
- `--dry-run` does not require compiler probing and does not change `pafio machine-info`
- records or reuses project-local toolchain state from `pafio-toolchain.lock`
- in `binary` mode, non-dry-run run uses the same published compiler discovery and compatibility gate as `pafio build`
- in `build` mode, non-dry-run run uses the same source-root resolution, fetch rules, and source-built compiler cache as `pafio build`

### `test`

Canonical form:

```text
pafio test [--manifest-path <path>] [--package <package-name>] [--test <name>] [--profile <dev|release>] [--dry-run] [--styio-bin <path>] [--source-root <path>] [--source-rev <rev>] [--yes|--no-fetch|--non-interactive] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `pafio.toml`
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
  - valid only when the selected project toolchain mode is `binary`
  - overrides published compiler discovery for the current invocation
- `--source-root <path>`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--source-rev <rev>`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--yes`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--no-fetch`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--non-interactive`
  - optional
  - valid only when the selected project toolchain mode is `build`
- `--locked`
  - optional
  - requires an adjacent `pafio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the active `single-version-v1` graph before generating a compile-plan
- may reuse project-local vendored snapshots under `.pafio/vendor/`
- emits `compile-plan v1` with `intent = "test"` to `.pafio/build/<cache-key>/plan.json`
- supports only explicit manifest `[[test]]` targets in the current native core
- rejects `--bin` and `--lib`
- defaults to the unique test target when the chosen package has exactly one `[[test]]`
- requires `--test <name>` when the chosen package has multiple tests
- requires `--package` when the selected manifest graph has multiple root packages and no root package at the selected manifest path
- requires an adjacent fresh `pafio.lock` when `--locked` or `--frozen` is set
- `--dry-run` does not require compiler probing and does not change `pafio machine-info`
- records or reuses project-local toolchain state from `pafio-toolchain.lock`
- in `binary` mode, non-dry-run test uses the same published compiler discovery and compatibility gate as `pafio build`
- in `build` mode, non-dry-run test uses the same source-root resolution, fetch rules, and source-built compiler cache as `pafio build`

### `tree`

Canonical form:

```text
pafio tree [--manifest-path <path>]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `pafio.toml`

Behavior summary:

- resolves the same `single-version-v1` graph that `pafio lock` uses
- accepts:
  - the selected package manifest when `[package]` is present
  - workspace members when `[workspace]` is present
  - recursively discovered `path` dependencies
  - pinned `git` dependencies and their transitive manifests at the selected `rev`
  - both `[dependencies]` and `[dev-dependencies]`
- does not require an on-disk `pafio.lock`
- does not read, validate, or rewrite the adjacent `pafio.lock`
- text output renders canonical package ids as an ASCII dependency tree
- global `--json` returns the resolved root ids plus package records

### `vendor`

Canonical form:

```text
pafio vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file used as the resolver graph root
  - defaults to `pafio.toml`
- `--output <path>`
  - optional
  - explicit project-local vendor root
  - if omitted, the current native core writes to `.pafio/vendor/` next to the selected manifest
- `--locked`
  - optional
  - requires an adjacent `pafio.lock` to exist and match the active resolver graph
- `--offline`
  - optional
  - forbids network fetches and uses only local cache or vendored snapshots
- `--frozen`
  - optional
  - shorthand for `--locked` plus `--offline`

Behavior summary:

- resolves the active `single-version-v1` graph before copying vendor state
- copies pinned git snapshots into project-local vendor state
- writes vendor metadata to `<vendor-root>/pafio-vendor.json`
- defaults to `.pafio/vendor/` so vendored snapshots do not collide with ordinary project paths such as local `vendor/` dependencies
- may reuse the selected vendor root during resolution
- requires an adjacent fresh `pafio.lock` when `--locked` or `--frozen` is set

### `pack`

Canonical form:

```text
pafio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file that defines the active package root
  - defaults to `pafio.toml`
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
- packages canonical `pafio.toml` plus regular files recursively discovered under the selected package root
- excludes the adjacent `pafio.lock`, generated top-level directories, and combined-workspace member/excluded subtrees
- rejects symlinks and unsupported filesystem node types inside the included tree
- ignores `package.publish`; local packing does not imply publish or registry semantics

### `publish`

Canonical form:

```text
pafio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--registry <path-or-url>] [--registry-profile <name>] [--registry-policy-file <path>] [--registry-header <name:value>] [--dry-run]
```

Arguments:

- `--manifest-path <path>`
  - optional
  - path to the manifest file that defines the active package root
  - defaults to `pafio.toml`
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
- stages the same deterministic source archive shape used by `pafio pack`
- non-dry-run `publish` writes into the registry `v2` static read plane or calls the registry `v2` control plane rooted at `--registry <path-or-url>`
- local paths and `file://...` publish directly into a filesystem registry `v2` root
- `http://...` and `https://...` publish through `/api/pafio-registry-control/v1/publish`
- when a private security module accepts `--registry-profile <name>`, `--registry-policy-file <path>`, or `--registry-header <name:value>`, those options affect only remote publish against the write origin and do not affect client-side fetch semantics
- publish writes:
  - registry config: `<registry-root>/config.json`
  - signed namespace targets: `<registry-root>/trust/targets/<namespace>.json`
  - append-only package index: `<registry-root>/index/<namespace>/<name>.jsonl`
  - source artifact: `<registry-root>/artifacts/source/sha256/<xx>/<yy>/<sha256>.pafio.src.tar`
  - transparency metadata: `<registry-root>/log/...`
- index records store package name, version, archive digest, archive size, publish timestamp, and dependency metadata for `[dependencies]` and `[dev-dependencies]`
- remote publish currently requires a registry control-plane service that preserves append-only index and immutable artifact semantics
- publish JSON stays redacted and may expose only security-provider metadata, security mode, header count, and optional profile name
- auth/account behavior is intentionally kept behind the private security-module boundary
- republishing an existing package version into the same registry fails explicitly

### `install`

Canonical surface:

```text
pafio install styio[@latest] [--source-root <path>] [--source-rev <ref>] [--channel <stable|nightly>] [--build <minimal>] [--yes|--no-fetch|--offline|--non-interactive]
```

Arguments:

- `styio` or `styio@latest`
  - required
  - defaults to stable latest
  - `styio@<ref>` is accepted as a source revision while hosted package distribution is unavailable
- `--source-root <path>`
  - optional
  - uses an existing local source checkout instead of fetching
- `--source-rev <ref>`
  - optional
  - overrides the source revision selected by the package spec
- `--channel <stable|nightly>`
  - optional
  - managed compiler channel, default `stable`
- `--build <minimal>`
  - optional
  - source build mode, default `minimal`
- `--yes`
  - optional
  - confirms source fetch; enabled by default for this top-level install path
- `--no-fetch`
  - optional
  - refuses remote source fetch and requires a local source root or cache
- `--offline`
  - optional
  - equivalent to no fetch plus offline source-cache behavior
- `--non-interactive`
  - optional
  - keeps the command non-interactive

Behavior summary:

- fetches source from `PAFIO_STYIO_SOURCE_ORIGIN` when set, otherwise from `https://github.com/SymPolicy/Styio.git`
- uses `PAFIO_STYIO_SOURCE_REF` when set; otherwise `latest` maps to `main`
- builds the `styio` target through the source-build path
- validates the resulting compiler through `styio --machine-info=json` and the compatibility matrix
- promotes the compiler into the managed current root under `PAFIO_HOME/tools/styio/current/`
- returns the same managed compiler fields as `tool install` plus source-root, source-revision, fetched, and built flags

### `tool install`

Canonical surface:

```text
pafio tool install --styio-bin <path>
```

Arguments:

- `--styio-bin <path>`
  - required
  - source path to a local self-contained `styio` executable that already supports `--machine-info=json`

Behavior summary:

- probes the selected compiler through the same published compatibility handshake used by `pafio check`
- rejects compilers outside the published `pafio/contracts/compat/styio-support.toml` matrix
- installs the compiler under `PAFIO_HOME/tools/styio/<channel>/<compiler-version>/bin/styio`
- refreshes the managed default compiler copy at `PAFIO_HOME/tools/styio/current/bin/styio`
- writes stable install metadata beside both the versioned install root and the managed current root
- `pafio check` plus `pafio build`, `pafio run`, and `pafio test` in `binary` mode continue compiler discovery through:
  - explicit `--styio-bin <path>`
  - `PAFIO_STYIO_BIN`
  - nearest project-local `pafio-toolchain.toml`
  - managed current compiler

### `tool status`

Canonical surface:

```text
pafio tool status --json [--manifest-path <path>]
```

Arguments:

- `--json`
  - required in the current native core
  - machine-readable success output is the only supported form
- `--manifest-path <path>`
  - optional
  - selected project manifest used to locate the project-local toolchain state and pin
  - defaults to omitting project-local state from the result

Behavior summary:

- reports `PAFIO_HOME`
- reports the active managed current compiler when present
- reports all installed managed toolchains under `PAFIO_HOME/tools/styio/`
- reports the nearest project-local `pafio-toolchain.toml` pin when a manifest path is supplied
- reports the project-local `pafio-toolchain.lock` state and resolved cloud policy when a manifest path is supplied

### `tool use`

Canonical surface:

```text
pafio tool use --version <compiler-version> [--channel <channel>]
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

- selects an already installed compiler under `PAFIO_HOME/tools/styio/<channel>/<compiler-version>/`
- re-validates the selected managed compiler through the same published compatibility handshake used by `pafio check`
- refreshes the managed default compiler copy at `PAFIO_HOME/tools/styio/current/bin/styio` from the selected versioned install
- rewrites the managed current metadata to reflect the newly active compiler
- fails when the selected version is missing or ambiguous across installed channels

### `tool pin`

Canonical surface:

```text
pafio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]
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
  - defaults to `pafio.toml`

Behavior summary:

- validates the selected manifest before changing project-local toolchain state
- writes the project-local toolchain pin to `<selected-manifest-dir>/pafio-toolchain.toml`
- stores an explicit managed `channel` and `version`, even if `--channel` was omitted on the command line
- re-validates the selected managed compiler through the same published compatibility handshake used by `pafio check`
- pin discovery for `pafio check`, `pafio build`, `pafio run`, and `pafio test` searches upward from the selected manifest directory for the nearest `pafio-toolchain.toml`
- a discovered project-local toolchain pin overrides the managed current compiler but not explicit `--styio-bin` or `PAFIO_STYIO_BIN`
- a discovered project-local toolchain pin fails if the pinned managed compiler is missing

## 5. Helper Script Entry Index

### `scripts/pafio`

Canonical form:

```text
./scripts/pafio [pafio-args...]
```

Behavior:

- builds the native binary on demand if the configured build directory does not already contain it
- forwards all remaining arguments to the native `pafio` executable

### `scripts/install-pafio.sh`

Canonical form:

```text
curl -fsSL <base-url>/install-pafio.sh | sh -s -- --base-url <base-url> [--install-dir <dir>]
```

Arguments:

- `--base-url <url>`
  - optional when `--binary-url` is provided
  - directory containing a `pafio` binary
- `--binary-url <url>`
  - optional
  - exact URL for the `pafio` binary
- `--install-dir <dir>`
  - optional
  - defaults to `/usr/local/bin`
- `--binary-name <name>`
  - optional
  - defaults to `pafio`
- `--no-styio-shim`
  - optional
  - skips installing the companion `styio` shim

Behavior:

- downloads the `pafio` binary with `curl`
- installs it into a PATH directory, using passwordless `sudo` when needed
- installs a companion `styio` shim that forwards to `PAFIO_HOME/tools/styio/current/bin/styio`

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

### `scripts/docs-index.py`

Canonical form:

```text
python3 scripts/docs-index.py
```

Behavior:

- validates documentation index ownership and generated doc-entry consistency

### `scripts/docs-lifecycle.py`

Canonical form:

```text
python3 scripts/docs-lifecycle.py
```

Behavior:

- validates documentation lifecycle metadata and stale-document policy

### `scripts/docs-audit.py`

Canonical form:

```text
python3 scripts/docs-audit.py
```

Behavior:

- runs the repository documentation ownership audit used by delivery gates

### `scripts/submit-gate.py`

Canonical form:

```text
python3 scripts/submit-gate.py [--profile <profile>] [--json]
```

Arguments:

- `--profile <profile>`
  - optional
  - selects the submit-gate profile, with `ci` used by repository checks
- `--json`
  - optional
  - emits a machine-readable step summary

Behavior:

- runs the submit-time quality gate bundle for the selected profile

### `scripts/perf-gate.py`

Canonical form:

```text
python3 scripts/perf-gate.py
```

Behavior:

- runs the configured performance smoke gate for submission readiness

### `scripts/repo-hygiene-check.py`

Canonical form:

```text
python3 scripts/repo-hygiene-check.py [--repo-root <path>] [--mode <auto|tracked|all>] [--skip-doc-check]
```

Arguments:

- `--repo-root <path>`
  - optional
  - repository or exported tree root to scan
- `--mode <auto|tracked|all>`
  - optional
  - selects tracked-file, filesystem, or automatic scan mode
- `--skip-doc-check`
  - optional
  - skips documentation reference validation

Behavior:

- validates artifact policy, `.gitignore` coverage, and required delivery documentation references

### `scripts/delivery-gate.py`

Canonical form:

```text
python3 scripts/delivery-gate.py [--json]
```

Arguments:

- `--json`
  - optional
  - emits a machine-readable delivery-tree validation summary

Behavior:

- copies the repository into an extractable delivery tree and runs delivery hygiene, docs, and native checks there

### `scripts/delivery-gate.sh`

Canonical form:

```text
./scripts/delivery-gate.sh [--mode <auto|checkpoint|staged|push>] [--base <rev>] [--range <rev-range>] [--skip-health]
```

Arguments:

- `--mode <auto|checkpoint|staged|push>`
  - optional
  - defaults to `auto`
  - `auto` runs worktree checks when local changes exist and push-range checks when `HEAD` is ahead of the inferred delivery base
  - `checkpoint` checks the current worktree, not only the staged index
  - `staged` is a low-level hook/debugging mode and is not the delivery closure default
  - `push` checks `base..HEAD` explicitly
- `--base <rev>`
  - optional
  - comparison base for push-mode hygiene and docs ownership validation
- `--range <rev-range>`
  - optional
  - explicit revision range for push-mode repository hygiene validation
  - defaults to `<base>..HEAD` when omitted in push mode
- `--skip-health`
  - optional
  - skips health checks that are already covered by a prior full run

Behavior:

- orchestrates repository hygiene, documentation, audit, native, and delivery-specific checks for branch submission
- refuses to silently pass when it cannot infer a delivery base in `auto` mode
- can infer fork-parent `nightly`/branch bases through the GitHub CLI when no local `upstream/<branch>` ref exists

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
  - runs `pafio --json check --styio-bin <path>`
  - runs `scripts/styio-interface-gate.py --styio-bin <path>`

### `scripts/styio-interface-gate.py`

Canonical form:

```text
./scripts/styio-interface-gate.py --styio-bin <path> [--pafio-bin <path>] [--manifest-path <path>] [--require-compile-plan] [--json]
```

Arguments:

- `--styio-bin <path>`
  - required
  - published compiler binary under validation
- `--pafio-bin <path>`
  - optional
  - `pafio` wrapper or binary used for black-box compatibility checks
  - defaults to `./scripts/pafio`
- `--manifest-path <path>`
  - optional
  - manifest used for the `pafio check` compatibility step
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
- runs `pafio --json check --styio-bin <path>` as a black-box compatibility step
- when `--require-compile-plan` is set:
  - creates a temporary sample package if needed
  - runs `pafio build --dry-run`
  - invokes `styio --compile-plan <path>` directly
  - verifies that the declared output directories are materialized

### `scripts/registry-server-gate.py`

Canonical form:

```text
./scripts/registry-server-gate.py (--registry-root <path-or-url> | [--publish-root <path-or-url>] [--fetch-root <path-or-url>]) [--publish-profile <name>] [--publish-policy-file <path>] [--publish-header <name:value>] [--fetch-trust-descriptor <path-or-url>] [--sync-timeout-seconds <seconds>] [--pafio-bin <path>] [--json]
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
  - points at the same write-origin policy file format accepted by `pafio publish --registry-policy-file`
- `--publish-profile <name>`
  - optional
  - reserved for private security-module builds
  - written into the gate's isolated deployment-owned state only when the private module is under test
  - cannot be combined with `--publish-policy-file <path>`
- `--sync-timeout-seconds <seconds>`
  - optional
  - retry budget for publish-to-fetch synchronization when the read root lags the write root
  - defaults to `0`
- `--fetch-trust-descriptor <path-or-url>`
  - optional
  - imports a platform registry trust descriptor into the gate's isolated `PAFIO_HOME` before remote fetch validation
  - required when validating public HTTP read roots that enforce descriptor-pinned trust
- `--pafio-bin <path>`
  - optional
  - `pafio` wrapper or binary used for the black-box publish/fetch checks
  - defaults to `./scripts/pafio`
- `--json`
  - optional
  - emits a machine-readable summary payload

Behavior:

- creates an isolated temporary `PAFIO_HOME`
- publishes a temporary package into the configured write root
- validates the publish JSON payload shape
- verifies that duplicate publish is rejected
- imports the fetch trust descriptor when `--fetch-trust-descriptor` is provided
- fetches the newly published package from the configured read root
- when a private security module is under test, can synthesize a write-origin profile under isolated `PAFIO_HOME` through `--publish-profile <name>`
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
  - writable source registry `v2` root that already contains canonical `config/`, `trust/`, `index/`, `artifacts/`, and `log/` objects
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
- validates the source registry `v2` root shape
- copies `config/`, `trust/`, `index/`, `artifacts/`, and `log/` objects into the destination root
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
  - defaults to the sibling pre-split repository checkout

## 6. Public Environment Variables

- `PAFIO_STYIO_BIN`
  - external published compiler path used by `pafio check` and `pafio build`, `pafio run`, or `pafio test` when the selected project mode is `binary` and `--styio-bin` is not passed
  - takes precedence over any project-local toolchain pin or compiler installed through `pafio tool install`
- `PAFIO_STYIO_SOURCE_ROOT`
  - source-tree override used by `pafio build`, `pafio run`, and `pafio test` when the selected project mode is `build` and `--source-root` is not passed
- `PAFIO_STYIO_SOURCE_ORIGIN`
  - official source origin override used by source-build mode when fetching the `styio` source tree into `PAFIO_HOME/src/styio/...`
- `PAFIO_HOME`
  - source cache root used by resolver-backed commands such as `pafio add`, `pafio check`, `pafio fetch`, `pafio lock`, and `pafio tree`
  - managed tool install root for `pafio tool install`, `pafio tool use`, and `pafio tool pin`
  - source-build checkout root for `PAFIO_HOME/src/styio/...`
  - source-built compiler cache root for `PAFIO_HOME/toolchains/source/...`
  - defaults to `~/.pafio` when not set
- `PAFIO_BUILD_DIR`
  - build directory used by `scripts/pafio` and `scripts/native-check.sh`

## 7. Update Rule

When any public argument, helper-script argument, or public environment variable changes:

1. update this index first
2. update the owning contract or operational document if needed
3. update implementation and tests in the same change
