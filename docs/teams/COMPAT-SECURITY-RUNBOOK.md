# Compat / Security Runbook

**Purpose:** Provide the daily-work entrypoint for maintainers of external compiler compatibility, public machine contracts, and `pafio`'s tracked security boundary.

**Last updated:** 2026-04-16

## Mission

Own published compatibility matrices, machine contracts, `styio` handshake expectations, and the public/private security split. This team does not own closed-source security implementations or unrelated CLI semantics.

## Owned Surface

Primary paths:

1. `src/PafioCompat/`
2. `src/PafioSecurity/`
3. `contracts/compat/`
4. `contracts/compile-plan/`
5. `docs/security/`
6. `docs/styio/`
7. `scripts/styio-interface-gate.py`

Key SSOTs:

1. `Compatibility policy -> ../governance/Pafio-Version-Decoupling-Constraints.md`
2. `Public contracts -> ../../contracts/README.md`
3. `Security boundary -> ../security/Pafio-Private-Security-Module-Contract.md`
4. `Styio handoff -> ../styio/Styio-External-Interface-Requirement-Spec.md`

## Daily Workflow

1. Read the published compatibility and security owner docs before changing schemas, matrix entries, or machine-info expectations.
2. Keep contract files, compatibility docs, and gate coverage aligned in the same delivery.
3. Treat `src-private/` and `tests-private/` as reserved boundaries; tracked code must not leak deployment-owned policy into public trees.
4. Validate changes through public handshakes only; do not read `styio` implementation internals to justify compatibility behavior.

## Change Classes

1. Small: matrix entry update or isolated compatibility helper cleanup. Run the interface gate and targeted fixtures.
2. Medium: schema evolution, new handshake field, or public security-boundary clarification. Update contracts and docs together.
3. High: published compatibility phase, compile-plan contract line, or public/private boundary change. Use coordinator review and release-profile validation.

## Required Gates

Minimum:

```bash
python3 ./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio
python3 ./scripts/preflight-readiness-check.py --styio-bin /absolute/path/to/styio
```

Expanded:

```bash
python3 ./scripts/submit-gate.py --profile release --styio-bin /absolute/path/to/styio --feature-config /absolute/path/to/submit-gate.features.json --json
```

## Cross-Team Dependencies

1. Resolve / Workflow must review published `compile-plan` or workflow contract changes.
2. CLI / Manifest must review machine-info or error-surface changes that leak into user-facing commands.
3. Registry teams must review any compatibility metadata that affects fetch or publish behavior.
4. Docs / Governance must review security-boundary or compatibility-policy owner doc updates.

## Handoff / Recovery

Record:

1. Contract directories or version lines changed.
2. External `styio` version and exact handshake command used.
3. Public/private boundary assumptions still open.
4. Remaining consumer teams that still need adaptation.
