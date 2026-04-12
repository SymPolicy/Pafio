# Spio Repository Split Runbook

**Purpose:** Describe the exact preflight, copy, and post-copy sequence for moving `spio` out of the current workspace into `$HOME/DevSpace/Unka-Malloc/styio-spio`.

**Last updated:** 2026-04-12

## 1. Preconditions

Before moving the subtree, confirm:

- the gates referenced in `Spio-Verification-Matrix.md` are green for the current bootstrap phase
- `styio --machine-info=json` is available on a published compiler binary
- `spio check --styio-bin ...` accepts that compiler
- `./scripts/styio-interface-gate.py --styio-bin ...` passes for the published compiler binary
- no `spio` code reads `styio/src` or `styio/tests`

Recommended combined preflight:

```text
./spio/scripts/preflight-readiness-check.py --styio-bin ./build-codex/bin/styio
```

## 2. Copy Procedure

Preferred command:

```text
./spio/scripts/copy-to-external-repo.sh $HOME/DevSpace/Unka-Malloc/styio-spio
```

This copies only the `spio` subtree and excludes:

- `.build/`
- `build/`
- `build-codex/`
- `.spio/`
- `__pycache__/`
- `.pytest_cache/`

## 3. Post-Copy Validation

Inside `$HOME/DevSpace/Unka-Malloc/styio-spio`, run:

```text
./scripts/native-check.sh
./scripts/extractability-check.sh
./scripts/spio --json check --manifest-path tests/unit/fixtures/manifests/ok-single-package/spio.toml --styio-bin /absolute/path/to/styio
./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio
```

## 4. After the Split

After the subtree moves:

- update developer docs to point to the standalone `styio` checkout used by the team
- keep compiler-location contracts limited to explicit `--styio-bin`, `SPIO_STYIO_BIN`, project-local `spio-toolchain.toml`, and managed current compiler state
- keep `contracts/` inside the new repository as the source of truth for package-manager-side contracts
- do not pull compiler implementation files into the new repository just for convenience

## 5. Known Defects

- This runbook assumes the current subtree layout remains stable.
- It intentionally references the verification matrix instead of duplicating every gate command; that keeps drift lower but adds one more document hop.
- It includes handshake-era compiler handoff validation, but compile-plan execution remains blocked until the published compatibility phase enables it.
- Absolute paths in current developer docs are useful today but will need rewriting after the split.
