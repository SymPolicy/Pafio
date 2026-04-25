# ADR-0025: Registry Client and Server Concerns Are Split in Source and Docs

**Last updated:** 2026-04-17  
**Status:** Accepted  
**Date:** 2026-04-12  
**Purpose:** Record the decision to keep registry consumption and registry publication in one repository while separating them explicitly at the source-tree and documentation levels.

**Last updated:** 2026-04-12

## Context

`spio` now has both:

- registry client behavior
- registry server-side publication behavior

Keeping both capabilities in one repository is practical because they share:

- the same repository layout
- the same package/archive identity rules
- the same test and contract surface

But leaving them mixed in the same source/documentation layer makes it harder to reason about responsibilities once client and server workflows evolve independently.

## Decision

1. Keep client and server registry logic in the same repository.
2. Split source modules explicitly:
   - registry client code lives under `src/SpioRegistryClient/`
   - registry server/write code lives under `src/SpioRegistryServer/`
   - publish-candidate preparation remains under `src/SpioPublish/` because it is shared package-preflight logic rather than server transport
3. Split documentation explicitly:
   - shared repository layout remains in governance
   - client contract lives under `docs/registry/`
   - server contract lives under `docs/registry/`
   - deployment baseline lives under `docs/registry/`

## Consequences

Positive:

- client fetch/cache logic and server upload behavior no longer drift together by accident
- future auth, deployment, and operational work can evolve on the server side without polluting client docs
- future client cache and lock behavior can evolve without polluting deployment guidance

Negative:

- there is one more docs module to maintain
- some registry concepts now require following links between shared layout docs and role-specific docs

## Follow-On Work

1. Keep future auth/account policy in server/deployment docs, not in client docs.
2. Keep future client offline/cache semantics in client docs, not in server docs.
3. Add source-level READMEs for new registry client/server modules as those areas grow.
