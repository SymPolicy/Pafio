# ADR-0028: Remote Publish Uses a Reusable Policy File for Write-Origin Hooks

**Purpose:** Record the decision to make write-origin policy reusable through a TOML policy file without changing read-side registry behavior.

**Last updated:** 2026-04-12

- Status: Accepted
- Date: 2026-04-12

## Context

`ADR-0027` introduced repeatable `spio publish --registry-header <name:value>` for remote write origins. That solved the immediate integration problem, but it still left two gaps:

1. Operators had to repeat the same upload-gateway headers on every publish command.
2. There was no reusable file format that deployment tooling or black-box server gates could point at without changing read-side fetch behavior.

The current phase still defers real auth and account policy. However, remote write-origins already need a stable place to carry fixed gateway headers and other future write-side-only integration hooks.

## Decision

1. Add `spio publish --registry-policy-file <path>`.
2. The file format is TOML and must declare `schema-version = 1`.
3. The file must contain one exact matching `[[registry]]` entry for the selected remote registry root.
4. Supported entry shape:

```toml
schema-version = 1

[[registry]]
root = "https://registry-upload.example.invalid"
headers = ["X-Spio-Write-Token: <write-token>"]
```

5. Policy-derived headers are applied only to remote publish-side `GET`, `HEAD`, and `PUT`.
6. Explicit `--registry-header <name:value>` values are appended after policy-derived headers.
7. The policy file is invalid for:
   - `--dry-run`
   - local filesystem registry roots
   - remote registry roots that do not match any policy entry
8. `scripts/registry-server-gate.py` accepts `--publish-policy-file <path>` so operations can validate this path as part of the server-side smoke gate.

## Consequences

Positive:

- write-origin upload policy becomes reusable and less error-prone than hand-written CLI headers
- deployment tooling can keep write-origin rules outside project manifests and outside read-side client config
- the repository gets a black-box path for validating upload gateways that need fixed headers

Negative:

- this still does not solve secret storage, delegated auth, or account policy
- operators must still manage the policy file path out-of-band

## Non-Goals

This ADR does not:

- define a real auth/account model
- add any read-side fetch headers or client-side registry policy
- replace deployment secret management or service-mesh policy
