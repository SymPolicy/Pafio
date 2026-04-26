# Specs Docs

**Purpose:** Define the scope and naming rules for `docs/specs/`; generated inventory lives in [INDEX.md](./INDEX.md).

**Last updated:** 2026-04-24

## Scope

1. Store cross-cutting agent, audit, repository-boundary, dependency, and documentation rules here.
2. Keep product, registry, security, and operations contracts in their owning directories; link to those documents instead of duplicating them.
3. Use this directory for rules that every agent or reviewer must apply across modules.
4. Post-push GitHub Actions checking rules live in [POST-COMMIT-CI-CHECKS.md](./POST-COMMIT-CI-CHECKS.md).
5. Technology-stack, internal-component, open-source-component, and dependency-manifest inventory maintenance rules live in [TECHNOLOGY-COMPONENT-INVENTORY.md](./TECHNOLOGY-COMPONENT-INVENTORY.md).
6. Registry-management reviews must prove the minimum package-manager coverage
   for publish, verify, mirror handoff, offline mode, cache reuse, and security
   boundary docs before claiming the change is measurable.

Generated inventory: [INDEX.md](./INDEX.md).
