# Pafio Version Decoupling Constraints

**Purpose:** Keep Pafio, Styio, language editions, and machine contracts independently versioned.

**Last updated:** 2026-08-01

## Independent Axes

1. Styio language edition
2. Styio compiler version
3. compile-plan contract version
4. Pafio product version
5. Pafio manifest, lock, metadata, and resolution contract versions

Pafio never infers compatibility from matching product versions. It probes
`styio --machine-info=json`, validates the advertised compile-plan version and
capabilities, and then delegates compilation through the published process
boundary.

Pafio must not link compiler internals, inspect compiler source, install or
switch the compiler, or reuse compiler outputs across incompatible compiler,
contract, edition, profile, target, or source identities.

`pafio.lock` changes only for dependency or lock-schema reasons; the Pafio
binary version is not a semantic dependency.

The package-manager compatibility declaration is
`contracts/compat/styio-support.toml`. Styio remains the authority for
diagnostics, receipts, and runtime events.

## Platform-Independent Releases

Pafio's shared source branch and each operating-system release are separate
promotion units. Advancing a revision to `nightly` establishes a shared source
candidate; it does not claim that Linux, macOS, and Windows binaries are all
ready or released.

Each platform release independently owns:

1. the exact Pafio source revision;
2. its native build and runtime compatibility gate;
3. its artifact format, checksum, signature, and provenance evidence; and
4. its publication and rollback decision.

Passing one platform's gate never waives another platform's gate and never
authorizes another platform's artifacts. Windows publication is organized and
accepted from a Windows release environment. The shared `nightly` promotion may
continue without a Windows artifact, provided release wording names the actual
platform scope and does not advertise Windows availability.
