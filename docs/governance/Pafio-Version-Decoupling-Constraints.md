# Pafio Version Decoupling Constraints

**Purpose:** Freeze the compatibility, release, protocol, and cache isolation rules that let `pafio` and `styio` evolve as separately maintained projects without accidental version coupling.

**Last updated:** 2026-04-10

## 1. Version Axes

The system has four independent version axes:

1. Language edition: `edition`
2. Compiler version: `styio x.y.z`
3. Machine contract version: `compile-plan vN`
4. Package-manager version: `pafio a.b.c`

These axes must never be collapsed into a single shared version number.

## 2. Non-Negotiable Rules

`pafio` MUST:

- support only published `styio` releases by default
- release after the `styio` versions it claims to support
- negotiate capability through machine-readable metadata, not through source inspection
- treat `compile-plan` as a separately versioned protocol
- isolate caches and build outputs by compiler version, protocol version, edition, and profile
- avoid changing `pafio.lock` format or contents for non-resolution reasons

`pafio` MUST NOT:

- link against `styio` implementation libraries
- include `styio` private headers
- parse Styio syntax using its own private grammar fork
- generate plans that require unpublished language features
- assume that `pafio` and `styio` share matching version numbers
- reuse build artifacts across incompatible compiler or contract versions

## 3. Capability Handshake

`pafio` must query the compiler first:

```text
styio --machine-info=json
```

The returned payload is the authority for:

- tool identity
- supported machine contract versions
- supported capability flags
- maximum supported `edition`
- compiler version and release channel

`pafio` must reject unsupported compilers using this handshake before any dependency resolution side effects are committed.

## 4. Release Policy

- `styio` publishes first.
- `pafio` publishes later, after validating against released compiler builds.
- `pafio` may define a minimum supported `styio` version and an optionally tested upper range.
- The real allow/deny decision must still be based on capability and contract support.

## 5. Contract Policy

- `pafio/contracts/` is the source of truth for `pafio`-owned machine contracts.
- `styio` may keep vendored snapshots, but vendored copies are read-only mirrors.
- Backward-compatible additions should prefer optional fields.
- Breaking protocol changes require a new major contract line such as `compile-plan/v2`.

## 6. Cache and Output Isolation

Every `pafio` build cache key must include at least:

- `styio` compiler version
- compile-plan major version
- edition
- profile
- target identity
- source hash

No build output may be shared across mismatched values for those dimensions.

Pinned git source caches must also live under hermetic `PAFIO_HOME` state rather than arbitrary developer-local checkouts.

Managed compiler installs created by `pafio tool install` must also live under hermetic `PAFIO_HOME/tools/` state rather than arbitrary mutable developer-local paths.

## 7. Lockfile Stability

`pafio.lock` must not encode the `pafio` tool version as a semantic dependency.

Allowed reasons for `pafio.lock` changes:

- dependency declarations changed
- resolution rules changed intentionally
- source metadata changed
- lock schema changed

Forbidden reasons for `pafio.lock` changes:

- formatter drift
- map iteration order drift
- `pafio` patch version drift without resolution changes

## 8. Compatibility Matrix

`pafio/contracts/compat/styio-support.toml` is the package-manager-side published compatibility declaration.

It records:

- minimum supported compiler version
- optionally tested upper bound
- required capability flags
- supported compile-plan versions for the current integration phase
- maximum edition
- release channel constraints

Compatibility entries may be phased. During bootstrap, `pafio` may support metadata-only integration against published `styio` releases before `compile-plan` exists. In that phase, compile-plan support must be recorded as an empty list instead of being guessed.

## 9. Acceptance Gates

The following gates are required before declaring the split architecture ready:

- `pafio_version_decouple_gate`
- `pafio_extractability_gate`
- `styio_contract_compat_gate`
- `styio_pafio_dual_maintenance_gate`

Each gate must run in an isolated temporary environment and must not reuse developer-local caches.

## 10. Known Tradeoffs

- This design is heavier than a direct in-process integration.
- The machine handshake and compatibility matrix add maintenance work.
- Cache partitioning increases disk usage.
- Publishing `pafio` after `styio` slows release cadence.

These tradeoffs are intentional and preferred over hidden version coupling.
