# Pafio Registry / Control-Plane Audit Shard 2026-04-22

**Purpose:** Record registry v2 and control-plane findings from the parallel external audit pass.

**Last updated:** 2026-04-22

**Scope:** registry v2 archive intake, trust-chain verification, and HTTP control-plane request handling.

**Status:** Closed in this worktree.

## Findings

| ID | Severity | Area | Finding | Evidence | Closure |
|----|----------|------|---------|----------|---------|
| PAFIO-REG-001 | High | Archive extraction | Tar manifest discovery accepted non-canonical member names and silently chose the first `pafio.toml` candidate. That let a crafted archive control which manifest was trusted, including traversal-style member names and duplicate manifest locations. | `src/pafio_registry_v2/publisher.py:36-59` | Fixed by rejecting non-canonical tar member paths and requiring exactly one manifest candidate. |
| PAFIO-REG-002 | Medium | Remote trust-chain reads | HTTP/HTTPS registry fetches had no read timeout, so a stalled mirror could block verification indefinitely. | `src/pafio_registry_v2/validator.py:27-94` | Fixed by adding a bounded HTTP read timeout and surfacing timeout failures as `RegistryV2Error`. |
| PAFIO-REG-003 | Medium | Control-plane body handling | The control-plane server bounded request size, but body reads had no socket timeout, so a slow client could hold a worker thread open indefinitely. | `scripts/registry-v2-control-plane-server.py:23-43`, `scripts/registry-v2-control-plane-server.py:80-89` | Fixed by setting a per-connection timeout and reporting read timeouts as request errors. |

## Verification

Passed after the fixes:

1. `python3 -m unittest tests.unit.test_registry_v2`
2. `bash tests/interop/registry-v2-http-read.sh scripts/pafio`
3. `bash tests/interop/registry-v2-control-plane-http.sh scripts/pafio`

## Residual Risk

The remaining registry v2 and control-plane work in this tree still assumes local or operator-controlled deployment boundaries. The fixes above remove the specific archive-manifest ambiguity and the obvious HTTP stall paths, but they do not add authentication or tenancy separation to the control plane.
