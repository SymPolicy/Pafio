# Spio CLI Contract

**Purpose:** Freeze the command surface, exit code ranges, and machine-readable output rules for the `spio` bootstrap phase so later implementations can evolve behind a stable interface.

**Last updated:** 2026-04-12

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
- `spio test`
- `spio tree`
- `spio pack`
- `spio publish`
- `spio tool install`

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

Local source-package archive emission is also part of the active command surface:

```text
spio pack --manifest-path path/to/spio.toml
spio pack --manifest-path path/to/spio.toml --package namespace/name
```

Local compile-plan emission is also part of the active command surface:

```text
spio build --manifest-path path/to/spio.toml --dry-run
spio run --manifest-path path/to/spio.toml --dry-run
spio test --manifest-path path/to/spio.toml --dry-run
```

Basic dependency edit and source fetch commands are also part of the active command surface:

```text
spio add <package-name> (--path <path> | --git <source> --rev <rev>) ...
spio remove <alias-or-package> ...
spio fetch --manifest-path path/to/spio.toml
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

Phase-2 rule:

- `supported_contracts.compile_plan` must remain an empty list until `styio` publishes a real compile-plan consumer and the compatibility matrix allows that phase
- owning `contracts/compile-plan/` schema files does not by itself authorize advertising active compile-plan support
- local `spio build --dry-run`, `spio run --dry-run`, and `spio test --dry-run` plan emission also do not authorize advertising active compile-plan support

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
- if `--styio-bin` is passed or `SPIO_STYIO_BIN` is set, `check` must also call `styio --machine-info=json`
- resolver-backed `check` runs before the optional compiler handshake
- compiler compatibility must be decided from the machine handshake plus `contracts/compat/styio-support.toml`
- bootstrap `check` must not read compiler source code or infer compatibility from repository layout
- `add` and `remove` only target manifests that define `[package]`
- `add` and `remove` must rewrite manifests canonically before returning success
- successful `add` and `remove` must refresh the adjacent `spio.lock` through the active resolver path
- failed post-edit resolution in `add` or `remove` must roll back both manifest and adjacent lockfile state
- resolver-backed `fetch` resolves the active graph and materializes source cache state without rewriting manifest or lock files
- resolver-backed `lock` only reads or writes the adjacent `spio.lock` next to the selected manifest
- resolver-backed `lock` resolves workspace, path, and pinned-git graphs under `single-version-v1`
- resolver-backed `lock` may use `SPIO_HOME` for git source cache state
- `spio lock --check` returns exit `11` when the adjacent lockfile is missing or stale
- resolver-backed `tree` resolves the same workspace, path, and pinned-git graph directly from the selected manifest
- resolver-backed `tree` is read-only and does not require or rewrite an adjacent `spio.lock`
- human-readable `tree` output uses canonical package ids with ASCII connectors
- `spio pack` selects exactly one local package from the active manifest root and never talks to the compiler
- `spio pack` writes a deterministic local source archive to `<package-root>/dist/<short-name>-<version>.tar` unless `--output <path>` overrides it
- `spio pack` packages canonical `spio.toml` plus regular files under the selected package root
- `spio pack` excludes the adjacent `spio.lock`, generated top-level directories, and workspace member/excluded subtrees when packing a combined root package manifest
- `spio pack` rejects symlinks and unsupported filesystem node types inside the included tree
- `spio build --dry-run` resolves the active graph and writes a local `compile-plan v1` to `.spio/build/<cache-key>/plan.json`
- `spio build --dry-run` must not require compiler probing
- `spio run --dry-run` resolves the active graph and writes a local `compile-plan v1` with `intent = "run"` to `.spio/build/<cache-key>/plan.json`
- `spio run` only supports explicit binary targets in the current native core
- `spio test --dry-run` resolves the active graph and writes a local `compile-plan v1` with `intent = "test"` to `.spio/build/<cache-key>/plan.json`
- `spio test` only supports explicit manifest `[[test]]` targets in the current native core
- non-dry-run `spio build` requires `--styio-bin <path>` or `SPIO_STYIO_BIN`
- non-dry-run `spio build` may call `styio --compile-plan <path>` only when the published compatibility matrix enables compile-plan v1 for the current phase
- non-dry-run `spio run` follows the same published compile-plan gate as `spio build`
- non-dry-run `spio test` follows the same published compile-plan gate as `spio build`
- compile-plan generation currently supports only explicit `lib`, `bin`, and `test` targets
- compile-plan generation may reject graphs that are otherwise resolvable when compile-plan v1 cannot represent them, such as cyclic graphs or mixed toolchain tuples

## 7. Known Tradeoffs

- The bootstrap CLI shape is larger than a single `install` command.
- The temporary bootstrap-only exit code `31` is inelegant but useful while commands are stubbed.
- Freezing the interface before implementation increases upfront design work.
