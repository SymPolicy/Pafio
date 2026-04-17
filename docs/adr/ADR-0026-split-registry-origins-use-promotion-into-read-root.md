# ADR-0026: Split Registry Origins Use Promotion Into a Read-Only Read Root

**Last updated:** 2026-04-17  
**Status:** Accepted  
**Date:** 2026-04-12  
**Purpose:** Record the decision to treat package publication and client download as separate operational concerns, with promotion from a writable upload root into a read-only serving root.

## Context

The current registry contract already supports:

- publish into a writable filesystem or HTTP origin
- direct client fetch from a static registry layout

But the recommended deployment baseline is not a single shared mutable endpoint. Operationally, the safer shape is:

- internal write origin for publication
- separate read origin or CDN for client traffic

That separation needs more than a policy statement. It needs:

- an explicit operational model
- an executable local promotion helper
- black-box verification that clients cannot fetch before promotion and can fetch after promotion

## Decision

1. Keep the shared registry object layout unchanged.
2. Treat write origin and read origin as separate operational roles.
3. Define promotion from write root to read root as an out-of-band step, not part of the client protocol.
4. Provide a repository-level helper script:
   - `scripts/registry-promote.py`
   - scope: local paths and `file://` roots only
5. Validate the model through a dedicated black-box gate:
   - `tests/interop/registry-split-origin-promotion.sh`

## Consequences

Positive:

- upload traffic and public read traffic no longer need to share one mutable endpoint
- the recommended deployment model now has an executable path instead of only narrative guidance
- teams can stage or promote registry objects into a read-only serving root before exposing them to clients

Negative:

- promotion is still infrastructure work outside `spio publish`
- the helper is intentionally local-filesystem-oriented and does not replace cloud-native replication systems

## Follow-On Work

1. Keep auth and upload policy attached to the write origin only.
2. Keep the read origin or CDN strictly read-only from the client perspective.
3. If future cloud deployments need a first-class remote promotion API, define that separately instead of overloading the current static repository contract.
