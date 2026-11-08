# Pafio Manifest and Lock Conventions

**Purpose:** Define Pafio manifest v1, deterministic lock state, and project-local generated state.

**Last updated:** 2026-07-30

## Manifest v1

The only manifest name is `pafio.toml`. It begins with:

```toml
[pafio]
manifest-version = 1
```

A manifest contains `[package]`, `[workspace]`, or both. Package names use
`namespace/name`; versions are exact `x.y.z`; the edition is explicit; publish
defaults to false.

Package manifests declare explicit `[lib]`, `[[bin]]`, and/or `[[test]]` targets.
Target paths are project-relative. Build configuration is:

```toml
[build]
implicit-std = true
```

No compiler channel, version, pin, installation mode, or cloud policy belongs in
the manifest.

`[dependencies]` and `[dev-dependencies]` map aliases to exactly one source:
relative `path`, pinned `git` plus an immutable full commit id `rev` (branch
names and tags are not accepted), or exact `version` plus `registry` and
package name. Workspace members are explicit relative paths and resolver `"1"`.

Canonical serialization orders `[pafio]`, `[package]`, `[build]`, targets,
dependencies, dev-dependencies, then `[workspace]`; named entries are sorted.

## Lock and generated state

The only lock name is `pafio.lock`. Lock version 1 uses the deterministic
`single-version-v1` resolver, sorted package IDs and dependency arrays, immutable
git revisions and registry SHA-256 digests, and no absolute host paths.

`.pafio/resolution-v1.json` is generated after the lock commit and binds canonical
manifest and lock digests to resolved package roots. `.pafio/vendor/` is
project-local vendor state. `PAFIO_HOME` contains shared package cache and registry
trust state only.

`sync` is the sole public lock-refresh and source-materialization transaction.
`--locked` rejects a missing or stale lock, `--offline` rejects missing local
content instead of networking, and `--frozen` combines both.

## Clean break

Legacy manifest, lock, project-state, home, and environment names are not read or
migrated. There is no compatibility fallback.
