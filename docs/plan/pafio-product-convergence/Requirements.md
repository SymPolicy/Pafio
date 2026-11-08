# Pafio Product and Ecosystem Convergence Requirements

**Purpose:** Freeze the accepted Pafio product boundary and clean-break delivery requirements.

**Last updated:** 2026-07-30

- **REQ-CONV-001:** One active product model describes the trusted dependency
  kernel as internal and assigns compiler, platform, and editor work to their owners.
- **REQ-CONV-002:** The CLI keeps `new/init/doctor/metadata/add/remove/sync/tree/
  check/build/run/test/vendor/pack/publish/registry trust/machine-info` only.
- **REQ-CONV-003:** Compiler workflows perform one shared sync transaction; locked,
  offline, and frozen have deterministic meanings.
- **REQ-CONV-004:** Styio discovery is `--styio-bin`, `PAFIO_STYIO_BIN`, then PATH,
  with no compiler lifecycle in Pafio.
- **REQ-CONV-005:** Metadata v1 contains package-project state only.
- **REQ-CONV-006:** Workflow JSON records Pafio intent and status while Styio owns
  receipt, diagnostic, and runtime-event contracts.
- **REQ-CONV-007:** Manifest v1 uses `[pafio]`, package/workspace/targets/dependencies,
  and `[build].implicit-std`, with no toolchain channel or pin.
- **REQ-CONV-008:** All public and internal identity changes to Pafio with no alias,
  shim, fixture, protocol fallback, or legacy-data migration.
- **REQ-CONV-009:** Styio, Platform, Vityo, the site, and audit policy consume or
  advertise the owner-specific Pafio contracts.
- **REQ-CONV-010:** Focused closure tests precede one full suite per repository and
  one fixed-revision product matrix.

Styio system package distribution and migration of old project data are out of scope.
