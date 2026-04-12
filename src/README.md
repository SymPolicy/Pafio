# spio Source

**Purpose:** Hold the standalone `spio` implementation. This directory must remain independent from `styio` compiler internals.

**Last updated:** 2026-04-09

## Rule

- Do not add source-level dependencies on `styio` implementation files.
- The authoritative implementation path should move toward native `C++20` sources built by `CMake`.
- The current Python package under `src/` is a legacy bootstrap scaffold kept temporarily as migration reference, not the final implementation commitment.

## Registry Split

- `src/SpioRegistryClient/` owns registry consumption and local materialization, but not private trust/auth policy.
- `src/SpioRegistryServer/` owns registry write/publish transports, but not private auth/account policy.
- `src/SpioSecurity/` owns the public registry-security interface boundary.
- `src/SpioPublish/` owns shared publish-candidate preparation and must not absorb server transport logic.
- `src-private/` is gitignored and reserved for closed-source security implementations; public extractability excludes it.
