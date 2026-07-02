# Golden Standard Test Suite

**Purpose:** Define the Pafio test level that makes a backend/toolchain version submittable.

**Last updated:** 2026-06-29

`test / smoke` runs the fast Python unit suite under `tests/unit`.

`test / golden-standard` runs the full submit gate for tracked public behavior, including repository hygiene, docs governance, native checks, extractability, performance baseline, and package delivery checks.

## Local Gate Profile

`pafio-submit-gate-registry-profile` is the repository-owned adaptation for Pafio package-manager and registry behavior. It is maintained in this repository through `submit-gate.py`, extractability checks, package delivery checks, and registry protocol evidence. The organization-level audit only verifies that this local profile is present and covered by `test / golden-standard`.

Required local markers: repo-owned adaptation, submit-gate.py, extractability, package delivery, registry protocol.

## Industry Gate Group

`package-manager / registry-safety` is the role-specific gate group for Pafio package-manager and registry-facing behavior. It keeps manifest, resolver, archive, publish, provenance, and credential checks inside `test / golden-standard`.

Required evidence markers: manifest validation, dependency integrity, package archive, publish dry-run, provenance, credential boundary, registry protocol.

## Submit Readiness

A Pafio version is submittable only when `platform-adaptation / linux-ci-gate`, `test / smoke`, and `test / golden-standard` all pass.
