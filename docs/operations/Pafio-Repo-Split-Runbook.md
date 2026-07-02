# Pafio Repository Split Runbook

**Purpose:** Describe the exact preflight, copy, and post-copy sequence for moving `pafio` out of the current workspace into `<pafio-workspace>`.

**Last updated:** 2026-04-24

## 1. Preconditions

Before moving the subtree, confirm:

- the gates referenced in `Pafio-Verification-Matrix.md` are green for the current bootstrap phase
- `styio --machine-info=json` is available on a published compiler binary
- `pafio check --styio-bin ...` accepts that compiler
- `./scripts/styio-interface-gate.py --styio-bin ...` passes for the published compiler binary
- no `pafio` code reads `styio/src` or `styio/tests`

Recommended combined preflight:

```text
./scripts/preflight-readiness-check.py --styio-bin ./build-codex/bin/styio
```

## 2. Copy Procedure

Preferred command:

```text
./scripts/copy-to-external-repo.sh <pafio-workspace>
```

This copies only the `pafio` subtree and excludes:

- `.build/`
- `build/`
- `build-codex/`
- `.pafio/`
- `__pycache__/`
- `.pytest_cache/`

## 3. Post-Copy Validation

Inside `<pafio-workspace>`, run:

```text
./scripts/native-check.sh
./scripts/extractability-check.sh
./scripts/pafio --json check --manifest-path tests/unit/fixtures/manifests/ok-single-package/pafio.toml --styio-bin /absolute/path/to/styio
./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio
```

## 4. After the Split

After the subtree moves:

- update developer docs to point to the standalone `styio` checkout used by the team
- keep compiler-location contracts limited to explicit `--styio-bin`, `PAFIO_STYIO_BIN`, project-local `pafio-toolchain.toml`, and managed current compiler state
- keep `contracts/` inside the new repository as the source of truth for package-manager-side contracts
- keep hosted control-plane, registry server control-plane, compile-platform, cloud stress, and extensible cloud-service ownership in `pafio`
- keep offline package use, local package import/export, and project-local Styio environment optimization in `pafio`
- do not pull compiler implementation files into the new repository just for convenience

## 5. Known Defects

- This runbook assumes the current subtree layout remains stable.
- It intentionally references the verification matrix instead of duplicating every gate command; that keeps drift lower but adds one more document hop.
- It includes handshake and compile-plan-live compiler handoff validation through the published compatibility phase.
- Absolute paths in current developer docs are useful today but will need rewriting after the split.
