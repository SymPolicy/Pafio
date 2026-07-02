# ADR-0027: Remote Publish Reserves Write-Origin Header Hooks

**Last updated:** 2026-04-17  
**Status:** Accepted  
**Date:** 2026-04-12  
**Purpose:** Record the decision to add request-header injection for remote publish as a write-origin integration hook without turning it into a full auth or client-fetch feature.

**Last updated:** 2026-04-12

## Context

The current registry model already supports:

- local filesystem publish
- anonymous HTTP publish
- split write/read origins with promotion into a read-only serving root

That is enough for simple internal deployments, but many write origins sit behind:

- upload gateways
- API-key enforcement at a reverse proxy
- internal policy middleware that expects fixed request headers

Waiting for a full auth/account system would leave no way to exercise those deployments through the native `pafio publish` path.

## Decision

1. Add repeatable `pafio publish --registry-header <name:value>`.
2. Scope it only to remote publish against `http://` or `https://` write origins.
3. Attach the configured header list to publish-side `GET`, `HEAD`, and `PUT` requests.
4. Reject the flag for:
   - filesystem registry roots
   - `--dry-run`
   - malformed `name:value` inputs
5. Keep the feature out of read-side fetch semantics.

## Consequences

Positive:

- write-origin gateways can be integrated without changing the shared read-side registry contract
- future auth work has a transport hook already available
- service operators can test upload policy without teaching clients new fetch behavior

Negative:

- this is not a complete auth/account design
- operators still need external secret handling and proxy policy

## Follow-On Work

1. If first-class auth lands later, define it as an explicit higher-level contract rather than relying only on raw header injection.
2. Keep any future read-side auth decisions separate from this write-origin hook.
