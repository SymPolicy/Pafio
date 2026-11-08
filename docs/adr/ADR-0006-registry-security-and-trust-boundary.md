# ADR-0006: Registry Security and Trust Boundary

**Purpose:** Record the functional decision that sensitive registry auth, deployment policy, and trust hardening live behind a private extension boundary while public reads fail closed on missing trust pins.

**Last updated:** 2026-06-28

## Status

Accepted.

## Supersedes

- `ADR-0030-private-security-extension-boundary.md`
- `ADR-0035-remote-registry-trust-descriptor-pinning.md`

## Context

Registry publication and consumption introduce supply-chain risks that should not be hidden inside convenience features. At the same time, the public repository needs stable interfaces and local behavior that can be tested without publishing private credentials, deployment-specific auth flows, or internal trust infrastructure.

The project has remote registry reads and writes, but stronger private-registry auth and deployment policy are intentionally not implemented in the public tree.

## Decision

The public tree owns stable security extension interfaces, local validation, and fail-closed client behavior. Auth-, trust-, and deployment-sensitive registry behavior belongs behind a private extension boundary.

Remote HTTP registry reads require imported trust descriptor pins before they are accepted. The descriptor pins the expected registry root metadata digest so a client cannot silently consume mutable or unexpected remote metadata.

Credentials, private auth flows, and deployment secrets must not be encoded into manifests, lockfiles, package archives, or generated public docs.

Public write-origin header and policy-file hooks may pass integration metadata to a remote write origin, but those hooks do not define the private auth model.

## Alternatives

Leaving trust policy fully open would make remote registry reads convenient but would normalize unauthenticated mutable metadata.

Implementing private auth directly in the public package manager would expose deployment-specific assumptions and make the public test suite depend on secrets.

Treating header hooks as auth would confuse transport integration with a real trust model.

## Consequences

Remote read tests and docs must be explicit about imported trust descriptors and fail-closed behavior.

Private deployments can extend auth and trust without forking the public package-manager contract.

Future signing, transparency, or checksum-database work should extend this boundary instead of bypassing it.
