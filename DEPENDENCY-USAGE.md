# Dependency Usage Boundary

**Purpose:** Record dependency authorization boundaries for `pafio`.

**Last updated:** 2026-04-24

`pafio` is an Apache-2.0 C++/Python source project. Its current build and test dependency boundary is:

- CMake `FetchContent` downloads `tomlplusplus` for TOML parsing, `nlohmann_json` for JSON serialization, and `googletest` for native test execution.
- Repository Python scripts and tests use the Python standard library only.
- Runtime registry and transport checks may invoke system tools such as `curl`, `git`, `tar`, `cmake`, and the configured `styio` compiler binary through explicit process boundaries.

Dependency policy:

- No dependency may require commercial authorization, paid licensing, subscription access, membership access, trial-only terms, proprietary-use approval, or private registry access.
- Any future dependency must be listed here with its license evidence, source boundary, and usage boundary before it can pass audit.
- Dependencies used only for tests must stay test-scoped and must not become runtime requirements without this file being updated.
- Generated reports and gate summaries must summarize dependency and license evidence without copying target repository source.
