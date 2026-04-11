# Spio Manifest and Lock Conventions

**Purpose:** Record the v1 manifest and lock conventions that remain authoritative through the current minimal resolver phase.

**Last updated:** 2026-04-12

## Phase 2 Core and Phase 3 Minimal Resolver

The current native implementation freezes:

- `toolchain`
- explicit `lib` / `bin` / `test` targets
- workspace membership rules
- `workspace`, `path`, and pinned `git` dependencies only
- `single-version-v1` resolution across workspace, path, and pinned git sources

Registry-oriented dependency declarations remain out of scope for this phase.

## Manifest

- File name: `spio.toml`
- Required top-level table: `[spio]`
- Required field: `manifest-version = 1`
- Allowed manifest forms:
  - package manifest with `[package]`
  - virtual workspace manifest with `[workspace]`
  - combined root manifest with both

### Package Rules

If `[package]` is present:

- `package.name` must match `namespace/name`
- `package.version` must be strict semver `x.y.z`
- `package.edition` must be an explicit string
- `[toolchain]` is required
- `[toolchain].channel` must be a non-empty string
- `[toolchain].implicit-std` must be a boolean
- at least one explicit target must exist through `[lib]`, `[[bin]]`, `[[test]]`, or a combination of them

### Target Rules

- `[lib]` may appear at most once
- `[lib].path` is required when `[lib]` is present
- `[[bin]]` entries require both `name` and `path`
- `[[bin]].name` values must be unique within the package
- `[[test]]` entries require both `name` and `path`
- `[[test]].name` values must be unique within the package
- target paths must be explicit project-relative paths; native phase 2 does not accept inferred or absolute manifest target paths

### Workspace Rules

- virtual workspace manifests must define `[workspace].members`
- `[workspace].members` must be a non-empty array of explicit relative paths
- `[workspace].exclude` is optional and must also use explicit relative paths
- `[workspace].resolver` must be `"1"`
- phase 2 does not support workspace member globbing or auto-discovery

### Dependency Rules

- phase 2 accepts exactly one dependency source kind per entry
- allowed phase-2 source kinds are:
  - `path`
  - `git`
- `git` dependencies require `rev`
- registry-style `version` dependencies are reserved for a later registry phase and must be rejected by the phase-2 native core

## Lockfile

- File name: `spio.lock`
- Required top-level field: `lock-version = 1`
- `[metadata]` is required
- `[metadata].generated-by` must be a non-empty string
- `[metadata].resolver` must be `single-version-v1`
- `[[package]]` entries must be emitted deterministically
- lockfile package `source-kind` values are limited to `workspace`, `path`, and `git`
- git lock entries must record both `git` source and pinned `rev`
- lockfiles must not encode absolute filesystem paths
- Produced by tooling, not intended for hand-authoring

### Phase-3 Minimal Resolver Rules

- `spio lock` resolves:
  - the selected root package when present
  - explicit workspace members when present
  - recursive `path` dependencies
  - pinned `git` dependencies
  - transitive manifests discovered inside pinned git snapshots
  - both `[dependencies]` and `[dev-dependencies]`
- the manifest at the pinned git revision is authoritative for package name, version, and transitive dependencies
- `single-version-v1` allows one effective package version and one effective source fingerprint per package name
- git-sourced `path` dependencies must stay within the pinned snapshot instead of escaping onto host-local paths
- the lockfile path is fixed to the adjacent `spio.lock` next to the selected manifest
- lock package identifiers use:
  - `workspace:<package-name>@<package-version>`
  - `path:<package-name>@<package-version>`
  - `git:<package-name>@<package-version>#<rev>`
- if two different source fingerprints resolve the same package name, lock generation must fail explicitly instead of silently merging them

## Canonical Write-Back

Native manifest/lock write-back must be deterministic and stable across repeated runs.

### Manifest Section Order

Canonical `spio.toml` output uses this section order:

1. `[spio]`
2. `[package]` when present
3. `[toolchain]` when `[package]` is present
4. `[lib]` when present
5. `[[bin]]` entries sorted by `name`
6. `[[test]]` entries sorted by `name`
7. `[dependencies]` sorted by alias
8. `[dev-dependencies]` sorted by alias
9. `[workspace]` when present

### Manifest Field Order

- `[spio]`: `manifest-version`
- `[package]`: `name`, `version`, `edition`, `publish`
- `[toolchain]`: `channel`, `implicit-std`
- `[lib]`: `path`
- `[[bin]]`: `name`, `path`
- `[[test]]`: `name`, `path`
- `[workspace]`: `members`, `exclude`, `resolver`
- dependency inline tables:
  - `package` when present
  - source selector field: `path` or `git`
  - `rev` for `git`

Canonical native manifest output materializes `publish = false` when the parsed package configuration leaves it at the phase-2 default.

### Lockfile Order

Canonical `spio.lock` output uses this order:

1. top-level `lock-version`
2. `[metadata]`
3. `[[package]]` entries sorted by `id`

Within `[metadata]`, field order is:

- `generated-by`
- `resolver`

Within each `[[package]]`, field order is:

- `id`
- `name`
- `version`
- `source-kind`
- `git` for `source-kind = "git"`
- `rev` for `source-kind = "git"`
- `dependencies`

`dependencies` arrays inside canonical lock output are sorted lexicographically.

## Fixture Policy

Fixtures are grouped by expected result:

- `ok-*` must parse and validate
- `bad-*` must be rejected

Fixture classes include:

- single package
- workspace root
- explicit toolchain and target validation
- explicit test-target validation
- path dependency
- git dependency
- bad package name
- bad source declaration
- bad lock version

## Known Tradeoffs

- These conventions freeze a subset before the full implementation exists.
- Validation-first scaffolding can feel slow compared with writing the resolver directly.
- TOML does not have a universally adopted schema system, so semantic validation still needs custom code.
