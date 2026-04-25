# Spio Registry Origin Runbook

**Purpose:** Provide the executable validation and deployment procedure for a shared `spio` registry `v2` origin without mixing it with client cache behavior or hosted publish-service policy.

**Last updated:** 2026-04-21

## 1. Scope

This runbook owns:

- server-side smoke validation commands
- publish/read origin validation flow
- deployment checklist for the current registry `v2` static root and publish-control-plane boundary

Server policy lives in [../registry/Spio-Registry-Control-Plane-Contract.md](../registry/Spio-Registry-Control-Plane-Contract.md) and [../registry/Spio-Registry-V2-Publish-Control-Plane.md](../registry/Spio-Registry-V2-Publish-Control-Plane.md). Deployment baseline still lives in [../registry/Spio-Registry-Deployment-Baseline.md](../registry/Spio-Registry-Deployment-Baseline.md).

## 2. Preconditions

- native `spio` binary is buildable locally
- target registry root already exposes the canonical shared layout
- if publish and fetch roots are split, replication or synchronization is already configured between them

Recommended local build entry:

```text
./scripts/native-check.sh
```

## 3. Single-Origin Validation

Use this when one origin handles both publish and fetch:

```text
./scripts/registry-server-gate.py --registry-root https://registry.example.internal --spio-bin ./build-codex/bin/spio --json
```

What it proves:

- publish commits a valid registry `v2` release
- duplicate publish is rejected
- the new package is immediately fetchable from the same static root

If the write origin sits behind an upload gateway that expects fixed headers, pass them explicitly:

```text
./scripts/registry-server-gate.py --registry-root https://registry-upload.example.internal --publish-header 'X-Spio-Write-Token: dev-token' --spio-bin ./build-codex/bin/spio --json
```

If the deployment links a private security module and the write-origin rules should live in a reusable file instead of command-line headers:

```text
./scripts/registry-server-gate.py --registry-root https://registry-upload.example.internal --publish-policy-file /etc/spio/publish-policy.toml --spio-bin ./build-codex/bin/spio --json
```

If the deployment links a private security module and already provisions a named profile under `SPIO_HOME/server/registry/publish-profiles/`, validate that path directly:

```text
spio publish --manifest-path path/to/spio.toml --registry https://registry-upload.example.internal --registry-profile write-dev
```

## 4. Split Publish and Fetch Origins

Use this when write traffic goes to an upload origin and read traffic goes to a download origin or CDN:

```text
./scripts/registry-server-gate.py --publish-root https://registry-upload.example.internal --fetch-root https://registry.example.internal --sync-timeout-seconds 30 --spio-bin ./build-codex/bin/spio --json
```

Notes:

- `--sync-timeout-seconds` is the publish-to-fetch visibility budget
- keep it at `0` when the read root should be immediately consistent
- raise it only when the read root is populated by asynchronous replication or CDN propagation

## 5. Promotion Workflow for Split Origins

Use this when the write root and read root are backed by different local serving roots or mounted storage views:

```text
./scripts/registry-promote.py --source-root /srv/spio/upload-root --dest-root /srv/spio/read-root --json
```

Scoped promotion is also supported:

```text
./scripts/registry-promote.py --source-root /srv/spio/upload-root --dest-root /srv/spio/read-root --package acme/util --version 0.2.0 --json
```

What it proves:

- the source root has a valid registry `v2` shape
- destination objects remain immutable
- `config/`, `trust/`, `index/`, `artifacts/`, and `log/` objects are copied consistently
- repeated promotion is idempotent

## 6. Local Split-Origin Smoke Test

The repository black-box gate for the recommended upload/download split is:

```text
bash ./tests/interop/registry-split-origin-promotion.sh ./build-codex/bin/spio
```

This validates `publish -> promote -> fetch`.

## 7. Remote-Shape Split-Origin Smoke Test

The repository also ships a closer deployment-shape smoke test:

```text
bash ./tests/interop/registry-split-origin-http.sh ./build-codex/bin/spio
```

This validates:

- HTTP publish to a write origin
- promotion from the write backing root into the read backing root
- HTTP fetch from a separate read origin

Use this when you want to rehearse the recommended "internal upload origin plus read-only download origin" topology end-to-end.

## 8. Local HTTP Smoke Test

The repository black-box gate uses the local immutable test server:

```text
bash ./tests/interop/registry-server-gate.sh ./build-codex/bin/spio
```

Use this before touching a real shared registry.

Auth-bearing write-origin smoke tests are intentionally not shipped in the tracked public tree. If a private security module is linked, keep those validations under `tests-private/` and `docs-private/` instead of restoring them to `tests/interop/`.

## 9. Production Checklist

- use `https` for remote publish and fetch roots
- preserve immutable object semantics for artifacts and log leaves
- reject overwrite attempts with `409 Conflict`
- retain audit logs for publication attempts
- keep `config/`, `trust/`, `index/`, `artifacts/`, and `log/` backed up together
- keep upload and download origins synchronized before enabling client installs
- keep the promotion step auditable and repeatable when upload and read origins are separate
- if the write origin requires gateway headers, keep them scoped to the upload path only and do not require them from public read origins
- if a policy file is used for write-origin headers, keep it outside the source tree and rotate its contents through deployment config rather than project manifests
- if a named profile is used, provision it through the private security module under deployment-owned state rather than from project state

## 10. Failure Triage

- publish fails immediately:
  check `PUT` support, write permissions, proxy limits, and immutable-path handling
- publish fails only at a gated write origin:
  check required header policy, `--registry-profile`, `--registry-policy-file`, and `--registry-header` values first
- duplicate publish succeeds:
  immutable object enforcement is broken
- publish succeeds but fetch fails:
  upload and download roots are not serving the same objects yet, or sync lag exceeds `--sync-timeout-seconds`
- promotion fails before any fetch:
  check source `v2` root validity, destination immutability conflicts, and backing-store path permissions
- fetch succeeds but later client installs fail integrity checks:
  inspect artifact corruption, proxy rewriting, and backing-store immutability
