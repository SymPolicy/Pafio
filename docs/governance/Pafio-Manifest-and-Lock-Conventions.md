# Pafio Manifest and Lock Conventions

**Purpose:** Record the v1 manifest and lock conventions that remain authoritative through the current minimal resolver phase.

**Last updated:** 2026-04-12

## Phase 2 Core and Phase 3 Minimal Resolver

The current native implementation freezes:

- `toolchain`
- explicit `lib` / `bin` / `test` targets
- workspace membership rules
- `workspace`, `path`, pinned `git`, and registry dependencies
- `single-version-v1` resolution across workspace, path, pinned git, and registry sources

## Manifest

- File name: `pafio.toml`
- Required top-level table: `[pafio]`
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
- `package.publish` is optional and defaults to `false`
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

- the current native core accepts exactly one dependency source kind per entry
- allowed source kinds are:
  - `path`
  - `git`
  - `registry`
- `git` dependencies require `rev`
- registry dependencies require:
  - `package = "namespace/name"`
  - `version = "x.y.z"`
  - `registry = "<url>"`
- registry roots must use `file://`, `http://`, or `https://`

## Lockfile

- File name: `pafio.lock`
- Required top-level field: `lock-version = 1`
- `[metadata]` is required
- `[metadata].generated-by` must be a non-empty string
- `[metadata].resolver` must be `single-version-v1`
- `[[package]]` entries must be emitted deterministically
- lockfile package `source-kind` values are limited to `workspace`, `path`, `git`, and `registry`
- git lock entries must record both `git` source and pinned `rev`
- registry lock entries must record both `registry` root and immutable blob `sha256`
- lockfiles must not encode absolute filesystem paths
- Produced by tooling, not intended for hand-authoring

### Phase-3 Minimal Resolver Rules

- `pafio lock` resolves:
  - the selected root package when present
  - explicit workspace members when present
  - recursive `path` dependencies
  - pinned `git` dependencies
  - registry dependencies declared through `version` + `registry`
  - transitive manifests discovered inside pinned git snapshots
  - transitive manifests discovered inside registry package snapshots
  - both `[dependencies]` and `[dev-dependencies]`
- the manifest at the pinned git revision is authoritative for package name, version, and transitive dependencies
- the manifest inside the registry package snapshot is authoritative for package name, version, and transitive dependencies
- `single-version-v1` allows one effective package version and one effective source fingerprint per package name
- git-sourced `path` dependencies must stay within the pinned snapshot instead of escaping onto host-local paths
- registry packages are fetched by immutable blob digest and extracted under `PAFIO_HOME/registry/checkouts/`
- the lockfile path is fixed to the adjacent `pafio.lock` next to the selected manifest
- lock package identifiers use:
  - `workspace:<package-name>@<package-version>`
  - `path:<package-name>@<package-version>`
  - `git:<package-name>@<package-version>#<rev>`
  - `registry:<package-name>@<package-version>#<sha256>`
- if two different source fingerprints resolve the same package name, lock generation must fail explicitly instead of silently merging them

## Canonical Write-Back

Native manifest/lock write-back must be deterministic and stable across repeated runs.

### Manifest Section Order

Canonical `pafio.toml` output uses this section order:

1. `[pafio]`
2. `[package]` when present
3. `[toolchain]` when `[package]` is present
4. `[lib]` when present
5. `[[bin]]` entries sorted by `name`
6. `[[test]]` entries sorted by `name`
7. `[dependencies]` sorted by alias
8. `[dev-dependencies]` sorted by alias
9. `[workspace]` when present

### Manifest Field Order

- `[pafio]`: `manifest-version`
- `[package]`: `name`, `version`, `edition`, `publish`
- `[toolchain]`: `channel`, `implicit-std`
- `[lib]`: `path`
- `[[bin]]`: `name`, `path`
- `[[test]]`: `name`, `path`
- `[workspace]`: `members`, `exclude`, `resolver`
- dependency inline tables:
  - `package` when present
  - source selector field: `path`, `git`, or `version`
  - `rev` for `git`
  - `registry` for registry dependencies

Canonical native manifest output materializes `publish = false` when the parsed package configuration leaves it at the phase-2 default.

### Lockfile Order

Canonical `pafio.lock` output uses this order:

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
- `registry` for `source-kind = "registry"`
- `sha256` for `source-kind = "registry"`
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
- registry dependency
- bad package name
- bad source declaration
- bad lock version

## Known Tradeoffs

- These conventions freeze a subset before the full implementation exists.
- Validation-first scaffolding can feel slow compared with writing the resolver directly.
- TOML does not have a universally adopted schema system, so semantic validation still needs custom code.
