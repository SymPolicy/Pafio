# Spio Audit Backlog 2026-04-22

**Purpose:** Preserve the 2026-04-22 `styio-spio` external audit findings as durable tracked work after removing the ignored temporary defect ledger.

**Last updated:** 2026-05-02

## Scope

This backlog migrates the ignored `docs/audit/defects/STYIO-SPIO-2026-04-22.md` record into tracked planning ownership. The migration does not claim unresolved defects are fixed. It gives each finding a stable identifier, owner stream, closure rule, and verification gate so the external `audit` defect queue can be empty while real work remains visible.

## Closure Rules

1. Keep the `SPIO-AUD-*` identifiers stable until the finding is either fixed or explicitly superseded.
2. Do not close an item with documentation alone when the finding describes unsafe runtime behavior.
3. Closure evidence must name the code change, regression coverage, data or resource lifecycle proof, and gate command.
4. If a finding moves to a GitHub issue or downstream platform task, record the durable link before removing it from this backlog.
5. Run `./scripts/delivery-gate.sh --mode checkpoint` and external `audit gate --repo $HOME/styio-spio --project styio-spio` before declaring the backlog migration complete.

## Backlog

| ID | Status | Owner stream | Finding | Required closure |
|----|--------|--------------|---------|------------------|
| SPIO-AUD-001 | Tracked | Registry / Publish | Registry v2 control-plane publish can use server-side registry keys without explicit auth or local-origin protection. | Add publish/verify auth and origin controls for local control-plane use; add negative tests proving unauthenticated publish is rejected. |
| SPIO-AUD-002 | Fixed, evidence retained | Registry / Publish | `/status` previously disclosed local registry and key paths. | Keep status responses redacted; retain regression evidence in `docs/audit/EXTERNAL-AUDIT-2026-04-22.md` and `tests/interop/registry-v2-control-plane-http.sh`. |
| SPIO-AUD-003 | Fixed, evidence retained | Registry / Publish | Control-plane request bodies previously had no read bound. | Keep `MAX_REQUEST_BYTES` and body timeout enforcement; retain regression evidence in the control-plane HTTP gate. |
| SPIO-AUD-004 | Fixed for public client root pinning, residual tracked | Registry / Publish | Registry verification still needs an external trust anchor for non-local registries instead of trusting only metadata fetched from the registry under verification. | Public remote fetches now require an imported platform descriptor and verify `trust/root.json` against the pinned SHA-256. Residual: add malicious self-signed-root integration coverage and full threshold policy in SPIO-AUD-011. |
| SPIO-AUD-005 | Tracked | Registry / Publish | Remote registry reads need complete timeout, size-cap, and media-policy enforcement. | Add per-object response size caps and content expectations for role metadata and artifacts; add slow and oversized endpoint tests. |
| SPIO-AUD-006 | Tracked | Registry / Publish | Archive manifest discovery needs per-member size limits in addition to canonical member selection. | Reject oversized candidate manifests before reading them into memory; add archive intake regressions. |
| SPIO-AUD-007 | Tracked | Core / Workflow | Bootstrap CLI still exposes commands that return `BootstrapNotImplemented`. | Either hide/rename future commands behind explicit experimental grammar or route them to implemented native paths with CLI tests. |
| SPIO-AUD-008 | Partially fixed, residual tracked | Registry / Publish | Native registry fetch validates shape and hashes but does not yet enforce registry v2 trust-chain parity with the Python verifier. | Native remote fetch now fails closed without a descriptor pin and validates `trust/root.json` against that pin. Residual: full Python verifier parity for signed timestamp/snapshot/targets threshold, expiry, and monotonic-version rules remains tracked by SPIO-AUD-011. |
| SPIO-AUD-009 | Tracked | Registry / Publish | Native registry checkout needs pre-extraction tar entry validation. | Pre-scan and reject absolute paths, `..` segments, links, and special files before extraction; add malicious archive tests. |
| SPIO-AUD-010 | Tracked | Registry / Publish | Registry publish and native paths need one strict package identity policy. | Reuse one namespace/name validator across Python and native registry paths; test traversal, slash, dot, and backslash cases. |
| SPIO-AUD-011 | Tracked | Registry / Publish | Registry v2 trust policy still needs threshold, expiration, freeze, and monotonic-version enforcement. | Enforce thresholds, expiry, and monotonic metadata versions; add expired and under-threshold regression tests. |
| SPIO-AUD-012 | Fixed for scoped runtime paths, residual tracked | Core / Workflow | Runtime subprocesses now have scoped wall-clock timeouts, but timeout configurability and curl low-speed controls remain residual work. | Preserve current timeout tests and add follow-up coverage when timeout policy becomes configurable or curl-specific controls are added. |
| SPIO-AUD-013 | Tracked | Registry / Publish | Native registry path normalization must match Python verifier segment policy before lexical normalization. | Align native path validation with verifier semantics and add native/Python parity tests. |
| SPIO-AUD-014 | Fixed for registry descriptor/status/publish/verify boundary, evidence retained | Styio / Contracts | Control-plane domain failures should not be reported as HTTP success envelopes at the transport boundary. | Registry control-plane failures return non-2xx transport status with machine-readable envelopes; descriptor and status regressions are covered by native platform tests. |
| SPIO-AUD-015 | Tracked | Registry / Publish | Registry server gate summaries need stronger redaction for publish headers, policy paths, and full commands. | Redact sensitive headers, local paths, and command details in summaries; add regression coverage. |

## Migration Evidence

The temporary defect ledger has been moved here instead of being marked closed. Existing fixed evidence remains in the tracked external audit and agent shard notes:

1. `docs/audit/EXTERNAL-AUDIT-2026-04-22.md`
2. `docs/audit/agent-findings/spio-registry-2026-04-22.md`
3. `docs/audit/agent-findings/spio-core-process-2026-04-22.md`
4. `docs/audit/agent-findings/spio-subprocess-timeouts-2026-04-22.md`

The durable backlog state is complete only when the ignored `docs/audit/defects/` queue is empty and external `audit` reports zero blocking findings.

## 2026-05-02 Registry Trust Closure Evidence

- `styio-platform` owns `/api/spio-registry-control/v1/descriptor` and returns
  the registry read root, control-plane base URL, and pinned `trust/root.json`
  SHA-256.
- `styio-spio` exposes `spio registry trust import` and
  `spio registry trust status --json`; imported pins are stored under
  `SPIO_HOME/registry/trust/registry-trust.json`.
- Native remote HTTP registry fetches fail closed when no descriptor pin exists
  or when the fetched root metadata digest does not match the imported pin.
- Verified locally with:
  `cmake --build build-codex --target spio_native_tests -j2`,
  `./build-codex/bin/spio_native_tests --gtest_filter='SecurityTests.*Registry*'`,
  `cmake --build build-codex --target spio -j2`, and
  `./build-codex/bin/spio --help`.
