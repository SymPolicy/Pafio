# ADR-0029: Remote Publish Profiles Discover Write-Origin Policy Under `SPIO_HOME`

- Status: Accepted
- Date: 2026-04-12

## Context

`ADR-0028` introduced `spio publish --registry-policy-file <path>`, which fixed the immediate reuse problem for write-origin gateway headers. It still left one operational gap:

- deployment tooling had to pass an explicit file path into every publish invocation

That is manageable, but it is not the cleanest end-state for a managed internal registry environment where the write-origin rules already belong to server-side deployment config rather than to project-level state.

## Decision

1. Add `spio publish --registry-profile <name>`.
2. A profile resolves to:

```text
SPIO_HOME/server/registry/publish-profiles/<name>.toml
```

3. The profile file uses the same TOML schema as `--registry-policy-file <path>`.
4. Profile names must be simple identifiers and may contain only ASCII letters, digits, `.`, `_`, and `-`.
5. `--registry-profile <name>`:
   - is only valid for remote `http://` or `https://` publish
   - is invalid with `--dry-run`
   - is invalid with local filesystem registry roots
   - cannot be combined with `--registry-policy-file <path>`
6. Explicit `--registry-header <name:value>` values remain allowed and are appended after the profile-derived headers.
7. The profile lives under a server-scoped subtree inside `SPIO_HOME`, not under the client-side registry cache tree.

## Consequences

Positive:

- operators get a stable named entrypoint for write-origin policy
- publish commands no longer need to carry absolute policy-file paths in the common managed case
- server-side config stays under `SPIO_HOME/server/...`, which keeps it separate from client cache state

Negative:

- deployments still need out-of-band provisioning of the profile file
- this is still not a real auth/account model

## Non-Goals

This ADR does not:

- define profile discovery for read-side fetch behavior
- mix write-origin policy into manifests or lockfiles
- replace secret management or gateway auth
