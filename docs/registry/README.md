# Registry Client Docs

**Purpose:** Define the registry trust, read, cache, pack, and publish-client behavior owned by Pafio.

**Last updated:** 2026-07-30

Pafio consumes immutable package metadata and source artifacts, verifies trust
and hashes, caches validated objects, prepares `.pafio.src.tar` archives, and
sends bounded publish requests.

Styio Platform is the source of truth for `pafio-static-registry`, registry
storage, write authorization, promotion, mirrors, and
`/api/pafio-registry-control/v1`. Pafio contains no production registry server.

See [Pafio Registry Client Contract](./Pafio-Registry-Client-Contract.md).
