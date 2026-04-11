# ADR-0015: Phase-4 Activates `spio pack` with Deterministic Local Source Archives

**Purpose:** Record the decision, context, alternatives, and consequences for the first native `pack` command surface and the minimal source-package archive rules.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

`spio` now owns the basic local package-management loop: manifest validation, lock refresh, dependency edits, resolver-backed tree inspection, and local compile-plan emission. The next missing user-facing capability is producing a stable source artifact that can move between machines or become the input to a later publish phase.

Waiting for registry or compiler publication to activate `pack` would leave another public command stubbed even though local source packaging does not depend on `styio --compile-plan` or on any registry transport.

At the same time, this project still does not define manifest-side include/exclude rules, registry metadata, or publish authentication. The first `pack` command therefore needs a deliberately narrow contract.

## Decision

1. Activate `spio pack` ahead of `publish` and registry work with the minimal public surface:
   - `spio pack [--manifest-path <path>] [--package <package-name>] [--output <path>]`
2. `spio pack` selects exactly one local package from the active manifest root.
   - if the selected manifest defines `[package]`, that root package is the default selection
   - if the selected manifest is workspace-only and exposes exactly one member package, that member is the default selection
   - if multiple candidate root packages exist, `--package <namespace/name>` is required
3. `spio pack` writes a deterministic uncompressed tar archive.
   - default output path: `<package-root>/dist/<short-name>-<version>.tar`
   - `--output <path>` may override the target path
   - if `--output` points inside the selected package root, it must live under an excluded generated directory such as `dist/`
4. Archive member paths use the stable source-package prefix `<short-name>-<version>/`.
5. The archive includes:
   - canonical `spio.toml` for the selected package manifest
   - regular files recursively discovered under the selected package root
6. The archive excludes:
   - the raw on-disk `spio.toml` in favor of canonical manifest serialization
   - the adjacent `spio.lock`
   - generated or repo-local top-level directories: `.git/`, `.spio/`, `dist/`, `build/`, and `build-*`
   - workspace member and workspace-excluded subtrees when packing a combined root package manifest that also owns `[workspace]`
7. Symlinks and unsupported filesystem node types inside the included tree are hard errors for `pack`.
8. `spio pack` ignores `package.publish` and does not imply registry or authentication semantics.
   - `publish` remains a separate later-phase command
9. Reserve exit code `16` for source-package archive failures.

## Alternatives

1. Keep `spio pack` stubbed until `publish` and registry work land.
   - Rejected because deterministic local source packaging is independently useful and does not depend on registry transport.
2. Package only explicit target files plus the manifest.
   - Rejected because that would silently omit package-authored supporting files such as README text or additional source-tree files.
3. Include `spio.lock` by default.
   - Rejected because the project has not yet frozen source-package semantics around local resolution snapshots, and a stale lockfile would be worse than omitting it.
4. Follow symlinks during archive traversal.
   - Rejected because it makes archive boundaries ambiguous and risks packaging content outside the selected package tree.

## Consequences

Positive:

1. `spio` now has a native source-artifact command that is independent of compiler publication status.
2. Later `publish` work can build on a stable local package archive instead of inventing transport and archive behavior at the same time.
3. Canonical manifest serialization makes repeated `pack` runs deterministic even when the checked-in manifest formatting drifts.

Negative:

1. The first archive rule is intentionally coarse and may still include extra package-root files until a future manifest include/exclude model exists.
2. `spio pack` currently emits plain `.tar` archives without compression.
3. Lockfile transport remains undefined for source packages and will need a later explicit decision if registry publishing wants it.
