# Pafio Registry Client Contract

**Purpose:** Freeze Pafio's trust, immutable read, cache, extraction, pack, and publish-client responsibilities.

**Last updated:** 2026-10-23

## Read Flow

For an exact registry package version, Pafio:

1. resolves the configured `file://`, `http://`, or `https://` read root;
2. validates the Platform-published registry descriptor and trust metadata;
3. refuses replayed timestamp/snapshot metadata below the persisted
   authenticated version floors, then raises those floors on acceptance;
4. selects the exact immutable package record;
5. bounds the response and archive size;
6. verifies the declared SHA-256 digest;
7. rejects unsafe archive paths and invalid manifest cardinality;
8. atomically materializes the object into the content-addressed cache.

Cached and vendored content is revalidated before use. `--offline` never falls
back to a network request. Network URLs never carry userinfo credentials;
authentication comes from the credential provider boundary.

## Publish Flow

`pafio pack` produces a deterministic `.pafio.src.tar` candidate.
`pafio publish` validates the candidate and sends the bounded client request to
the Platform route `/api/pafio-registry-control/v1/publish`. Remote publish
requires `https://`; plaintext `http://` is accepted only behind the explicit
local-development `--allow-plaintext-http` opt-in, with pinned scheme and
bounded connect/total timeouts on the transfer.

Pafio does not implement registry storage, signing, write authorization,
promotion, mirror replication, or server deployment.

## Local State

Registry cache and imported trust state live under `PAFIO_HOME/registry/`.
This private local state is not a Vityo integration surface and must not appear
in metadata v1.
