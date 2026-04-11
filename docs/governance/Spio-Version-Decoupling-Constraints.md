# Spio Version Decoupling Constraints

**Purpose:** Freeze the compatibility, release, protocol, and cache isolation rules that let `spio` and `styio` evolve as separately maintained projects without accidental version coupling.

**Last updated:** 2026-04-10

## 1. Version Axes

The system has four independent version axes:

1. Language edition: `edition`
2. Compiler version: `styio x.y.z`
3. Machine contract version: `compile-plan vN`
4. Package-manager version: `spio a.b.c`

These axes must never be collapsed into a single shared version number.

## 2. Non-Negotiable Rules

`spio` MUST:

- support only published `styio` releases by default
- release after the `styio` versions it claims to support
- negotiate capability through machine-readable metadata, not through source inspection
- treat `compile-plan` as a separately versioned protocol
- isolate caches and build outputs by compiler version, protocol version, edition, and profile
- avoid changing `spio.lock` format or contents for non-resolution reasons

`spio` MUST NOT:

- link against `styio` implementation libraries
- include `styio` private headers
- parse Styio syntax using its own private grammar fork
- generate plans that require unpublished language features
- assume that `spio` and `styio` share matching version numbers
- reuse build artifacts across incompatible compiler or contract versions

## 3. Capability Handshake

`spio` must query the compiler first:

```text
styio --machine-info=json
```

The returned payload is the authority for:

- supported machine contract versions
- supported capability flags
- maximum supported `edition`
- compiler version and release channel

`spio` must reject unsupported compilers using this handshake before any dependency resolution side effects are committed.

## 4. Release Policy

- `styio` publishes first.
- `spio` publishes later, after validating against released compiler builds.
- `spio` may define a minimum supported `styio` version and an optionally tested upper range.
- The real allow/deny decision must still be based on capability and contract support.

## 5. Contract Policy

- `spio/contracts/` is the source of truth for `spio`-owned machine contracts.
- `styio` may keep vendored snapshots, but vendored copies are read-only mirrors.
- Backward-compatible additions should prefer optional fields.
- Breaking protocol changes require a new major contract line such as `compile-plan/v2`.

## 6. Cache and Output Isolation

Every `spio` build cache key must include at least:

- `styio` compiler version
- compile-plan major version
- edition
- profile
- target identity
- source hash

No build output may be shared across mismatched values for those dimensions.

Pinned git source caches must also live under hermetic `SPIO_HOME` state rather than arbitrary developer-local checkouts.

## 7. Lockfile Stability

`spio.lock` must not encode the `spio` tool version as a semantic dependency.

Allowed reasons for `spio.lock` changes:

- dependency declarations changed
- resolution rules changed intentionally
- source metadata changed
- lock schema changed

Forbidden reasons for `spio.lock` changes:

- formatter drift
- map iteration order drift
- `spio` patch version drift without resolution changes

## 8. Compatibility Matrix

`spio/contracts/compat/styio-support.toml` is the package-manager-side published compatibility declaration.

It records:

- minimum supported compiler version
- optionally tested upper bound
- required capability flags
- supported compile-plan versions for the current integration phase
- maximum edition
- release channel constraints

Compatibility entries may be phased. During bootstrap, `spio` may support metadata-only integration against published `styio` releases before `compile-plan` exists. In that phase, compile-plan support must be recorded as an empty list instead of being guessed.

## 9. Acceptance Gates

The following gates are required before declaring the split architecture ready:

- `spio_version_decouple_gate`
- `spio_extractability_gate`
- `styio_contract_compat_gate`
- `styio_spio_dual_maintenance_gate`

Each gate must run in an isolated temporary environment and must not reuse developer-local caches.

## 10. Known Tradeoffs

- This design is heavier than a direct in-process integration.
- The machine handshake and compatibility matrix add maintenance work.
- Cache partitioning increases disk usage.
- Publishing `spio` after `styio` slows release cadence.

These tradeoffs are intentional and preferred over hidden version coupling.
