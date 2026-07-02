# ADR-0020: Phase-6 Activates Project-Local Managed `styio` Pinning Through `pafio tool pin`

**Purpose:** Record the decision, context, alternatives, and consequences for project-local compiler pin files and the first native `pafio tool pin` workflow.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

`pafio tool install` and `pafio tool use` now provide a managed local compiler store and a managed current compiler under `PAFIO_HOME/tools/styio/current/`.

That solves machine-local activation, but not repository-local reproducibility. A team still needs a durable way to say which already installed compatible `styio` compiler a given project expects, without requiring every caller to export `PAFIO_STYIO_BIN`.

The existing fallback order also made project intent invisible:

1. explicit `--styio-bin <path>`
2. `PAFIO_STYIO_BIN`
3. managed current compiler

That is workable for a single developer machine, but weak for shared repositories, CI, and worktree isolation because the project itself does not participate in tool selection.

## Decision

1. Activate a new tool-management subcommand:
   - `pafio tool pin (--version <compiler-version> [--channel <channel>] | --clear) [--manifest-path <path>]`
2. `tool pin` targets already installed managed compilers only.
   - it does not pin arbitrary unmanaged filesystem paths
   - it does not download remote releases
3. `tool pin` validates the selected manifest before changing project-local pin state.
4. `tool pin` selects the managed compiler using the same version/channel ambiguity rules as `tool use`.
5. `tool pin` writes a project-local pin file beside the selected manifest:
   - `<selected-manifest-dir>/pafio-toolchain.toml`
6. The pin file format is:
   - `[styio]`
   - `channel = "<channel>"`
   - `version = "<compiler-version>"`
7. The written pin file always stores both `channel` and `version`, even when `--channel` was omitted on the command line.
8. `tool pin` re-validates the selected managed compiler through `styio --machine-info=json` plus the published compatibility matrix before writing pin state.
9. Compiler discovery for `pafio check`, `pafio build`, `pafio run`, and `pafio test` now resolves in this order:
   - explicit `--styio-bin <path>`
   - `PAFIO_STYIO_BIN`
   - nearest project-local `pafio-toolchain.toml`, discovered by searching upward from the selected manifest directory
   - managed current compiler under `PAFIO_HOME/tools/styio/current/bin/styio`
10. A discovered project-local pin is authoritative.
    - `pafio` must fail if the pinned managed compiler is missing
    - it must not silently fall back to managed current in that case
11. `tool pin --clear` removes the exact pin file beside the selected manifest.

## Alternatives

1. Keep project selection entirely in `PAFIO_STYIO_BIN`.
   - Rejected because environment variables are caller-local state and do not travel with the repository.
2. Store compiler version selection directly inside `pafio.toml`.
   - Rejected because manifest `toolchain` already models language/toolchain semantics for package resolution and planning, not machine-local managed install selection.
3. Make project pins refer to arbitrary binary paths.
   - Rejected because that would bypass the managed compiler store and weaken reproducibility across machines.
4. Make project pins silently fall back to managed current if the pinned version is missing.
   - Rejected because that would hide configuration drift and defeat the point of a project pin.

## Consequences

Positive:

1. Repository-local compiler selection becomes durable project state instead of only user-local environment state.
2. CI and local development can converge on the same managed compiler version without injecting absolute binary paths into the manifest.
3. Nested repositories or workspace members can override parent compiler pins through nearest-file discovery.
4. The compiler-selection contract stays decoupled from compiler internals because pinning still resolves to published `styio --machine-info=json` validation.

Negative:

1. The tool-management CLI surface grows again.
2. Compiler discovery rules are now slightly more complex because they include both project-local and machine-local state.
3. A stale project pin can block `check` and future compiler-facing commands until the matching managed compiler is installed.
