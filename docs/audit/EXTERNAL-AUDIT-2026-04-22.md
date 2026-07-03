# External Audit 2026-04-22

**Purpose:** Record the external audit pass over `pafio` using the `styio-audit` project module `for-pafio`.

**Last updated:** 2026-04-24

## Scope

Reviewed the package-management, registry, resolver, toolchain, process, and control-plane surfaces that map to the external module's resource classes and state machines.

## Summary

The codebase is broadly organized along the expected boundaries:

- manifest and resolver logic stay mostly isolated in `PafioManifest` and `PafioResolve`
- managed toolchain state is modeled explicitly in `PafioToolchain` and `PafioTool`
- subprocess handling is centralized in `PafioCore::Process`
- registry v2 behavior is split between Python publish/verify helpers and the local control-plane wrapper script

The first audit concern closed during this pass was the control-plane surface:

- `/status` no longer leaks the real registry root or key directory
- request bodies now have a hard upper bound before parsing
- the interop gate now asserts both behaviors

Parallel shards then closed additional registry, process, and subprocess-lifecycle issues:

- [Pafio Registry / Control-Plane Audit Shard 2026-04-22](./agent-findings/pafio-registry-2026-04-22.md) rejects non-canonical or duplicate tar manifests, bounds HTTP/HTTPS registry reads, and times out slow control-plane body reads.
- [pafio-core-process audit shard - 2026-04-22](./agent-findings/pafio-core-process-2026-04-22.md) removes the `git archive` 1 MiB capture path and fixes the bidirectional process I/O deadlock window.
- [PAFIO Subprocess Timeout Audit - 2026-04-22](./agent-findings/pafio-subprocess-timeouts-2026-04-22.md) adds scoped wall-clock timeouts across runtime git, tar, curl, source-build, compiler, and registry helper subprocesses.

## Findings

| Severity | Area | Finding | Status | Evidence / Action |
|---|---|---|---|---|
| High | Registry v2 control plane | Status output exposed local filesystem paths, and request bodies had no upper bound before parsing. | Fixed in this audit | Updated `scripts/registry-v2-control-plane-server.py` to redact path fields and reject oversized/truncated bodies. Added a regression to `tests/interop/registry-v2-control-plane-http.sh`. |
| High | Registry v2 archive intake | Tar manifest discovery accepted non-canonical member paths and silently selected the first manifest candidate. | Fixed in this audit | Updated `src/pafio_registry_v2/publisher.py` to require canonical member names and exactly one `pafio.toml`; covered by `tests/unit/test_registry_v2.py`. |
| High | Process lifecycle | Large `git archive` snapshots could be truncated by the 1 MiB `RunProcess` stdout cap, and synchronous stdin writes left a bidirectional I/O deadlock window. | Fixed in this audit | `src/PafioResolve/Resolver.cpp` now archives directly to output; `src/PafioCore/Process.cpp` writes stdin through the poll loop. Native regressions cover both paths. |
| High | External subprocess lifecycle | Resolver, source-build, registry, compiler, and registry-v2 helper subprocesses did not consistently have explicit wall-clock timeouts. | Fixed for scoped runtime paths | Shared timeout constants now cover key git/tar/curl/cmake/compiler/helper subprocesses. `ProcessTests.TimesOutWhenChildKeepsProducingOutput` covers the wall-clock timeout. |
| Medium | Control-plane semantics | Publish and verify failures still return HTTP 200 with a failure envelope. That weakens HTTP-level observability and gateway enforcement. | Tracked | Migrated to [Pafio Audit Backlog 2026-04-22](../plan/repository-delivery-convergence/Evidence.md) as `PAFIO-AUD-014`. |
| High | Audit governance | The ignored open defect record was moved to durable tracked planning work, so the external `styio-audit` defect queue can be empty without losing the unresolved findings. | Fixed by migration | Tracked in [Pafio Audit Backlog 2026-04-22](../plan/repository-delivery-convergence/Evidence.md); verify by running `styio-audit gate` against `pafio`. |
| Low | Test coverage | Before this pass, there was no interop regression covering status redaction or oversized control-plane requests. | Fixed in this audit | Added checks to the control-plane HTTP gate so the regression is now exercised. |

## Design / State Machine Notes

- Manifest validation is strict about package identity, semver, and source-kind selection.
- Resolver behavior follows explicit graph, lock, and planning stages, with negative-path tests for ambiguous target selection and mixed-edition graphs.
- Toolchain state is modeled as a file-backed lifecycle, and the CLI tests cover set/use/pin transitions.
- Process execution now has stronger timeout, truncation, and bidirectional I/O coverage; the implementation is structured around a single request/result contract.

## Verification

Completed during this audit:

- external audit framework validation in `--framework-only` mode
- targeted control-plane regression coverage after the redaction and request-size hardening

## Residual Risk

The repository still has known open issues outside the hardening changes made here, especially around control-plane HTTP failure semantics, fixed timeout configurability, curl-specific low-speed controls, and the broader registry trust model. Those items remain tracked in [Pafio Audit Backlog 2026-04-22](../plan/repository-delivery-convergence/Evidence.md) and should be handled separately rather than treated as closed by this audit note.
