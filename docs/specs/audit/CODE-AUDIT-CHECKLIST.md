# Code Audit Checklist

**Purpose:** Provide the mandatory audit checklist for agents and reviewers; the first audit items are design-principle compliance, lifecycle test coverage, data lifecycle safety, and delivery-gate strictness.

**Last updated:** 2026-04-22

## Agent Compliance Rule

Every agent performing code review, implementation review, defect triage, or closure verification must apply this checklist. If a finding violates one of the seven design principles below, record the principle number in the defect note or review finding. Every closure decision must also record the test evidence used, the data lifecycle evidence for touched structures or resources, and whether the delivery gate is strict enough to protect software quality.

For data and resource lifecycle claims, use the centralized `General-Auditor` framework as the auditable source of truth: `modules/default` plus `for-pafio`.

## 1. Check The Seven Design Principles First

Before checking style, naming, or local code shape, verify that the change follows these seven principles.

### 1. Performance And User Value First

Code must preserve the project priority order: performance first, usability second, implementation convenience last.

Audit questions:

1. Does the design avoid unnecessary work on hot paths, request loops, parsing, compilation, execution, and UI update paths?
2. Does it make the recommended user path also the efficient path?
3. Are performance claims backed by tests, benchmark evidence, or a documented gate?
4. If the change sacrifices performance for convenience, is that tradeoff explicitly justified and bounded?

Violation examples:

1. Adding global rescans for small events without coalescing or caps.
2. Introducing blocking I/O in request loops.
3. Preserving a slow legacy path only because replacing it is inconvenient.

### 2. Explicit Ownership And Single Responsibility

Each module, adapter, command, script, and document must have one clear responsibility and one clear owner boundary.

Audit questions:

1. Does the code put behavior in the layer that owns it?
2. Are parser, analyzer, IR, codegen, runtime, CLI, IDE, registry, UI, and docs boundaries respected?
3. Are cross-repo responsibilities expressed through contracts instead of private imports or accidental file coupling?
4. Does the change avoid turning a router, UI shell, or script into a domain-logic dumping ground?

Violation examples:

1. UI code parsing registry internals instead of consuming an adapter contract.
2. CLI argument routing containing package-resolution policy.
3. Compiler code taking ownership of package-manager lifecycle behavior.

### 3. Typed, Structured, Machine-Readable Contracts

Important behavior must flow through typed data, schemas, structured diagnostics, and explicit contracts rather than ad hoc strings.

Audit questions:

1. Are errors and machine responses structured enough for callers and tests to classify?
2. Are protocol boundaries documented with schemas, examples, or frozen payload shapes?
3. Does the implementation parse structured formats with structured parsers?
4. Are stringly-typed conventions limited to presentation text rather than control flow?

Violation examples:

1. Grepping human error text to decide behavior.
2. Passing opaque shell command strings where argv/process APIs are needed.
3. Returning success with embedded unstructured failure text.

### 4. Fail Closed, Never Silently Substitute

Unsupported, unsafe, invalid, or ambiguous states must fail with typed diagnostics. They must not silently fall back, emit placeholder values, or pretend success.

Audit questions:

1. Does every unsupported syntax, AST node, protocol version, or runtime state produce an explicit failure?
2. Are placeholder values such as `0`, empty objects, empty diagnostics, or default success avoided on active paths?
3. Does verifier, parser, sema, codegen, adapter, and gate failure propagate to the caller?
4. Do fallback paths have explicit capability/status reporting and exit criteria?

Violation examples:

1. Unknown function calls compiling as `0`.
2. LLVM verification failures that only print and continue.
3. Hosted/API errors returned as HTTP success without strong machine classification.

### 5. Secure And Bounded By Default

Security, filesystem, network, archive, process, and resource behavior must be explicit, bounded, and least-privilege by default.

Audit questions:

1. Are authentication, authorization, origin, and tenant/workspace boundaries explicit for control-plane operations?
2. Are path joins, archive extraction, symlinks, hardlinks, and relative paths contained by component-aware checks?
3. Do network and subprocess calls have timeouts, size limits, and clear error mapping?
4. Are shell strings avoided for production execution paths?
5. Are secrets, keys, tokens, and local absolute paths redacted from logs and status payloads?

Violation examples:

1. Unauthenticated publish endpoints using server-side signing keys.
2. Extracting tar archives without validating entries.
3. Reading HTTP responses or request bodies without size caps.

### 6. Evidence Must Match The Claim

No audit conclusion is complete unless tests, gates, diagnostics, and docs prove the exact claim being made.

Audit questions:

1. Does a green test actually cover the behavior being claimed?
2. Are positive, negative, malformed, timeout, security, and compatibility edges covered when relevant?
3. Is a skipped, optional, or environment-gated test clearly marked as insufficient for full closure?
4. Are docs, runbooks, indexes, and gates updated with the code change?

Violation examples:

1. Claiming product closure while the product workflow gate is skipped by default.
2. Treating a happy-path contract test as proof of security hardening.
3. Updating code without updating the owning runbook or docs index.

### 7. Recoverable Evolution With Explicit Exit Criteria

Large changes, compatibility bridges, legacy paths, staged rollouts, and temporary audit records are allowed only when recovery and exit rules are explicit.

Audit questions:

1. Is the change small enough to verify, or split into checkpoints with clear recovery points?
2. Do compatibility bridges and shadow routes state when they can be removed?
3. Are audit defects moved to durable tracked work, closed with evidence, or removed before submission?
4. Does the change make future maintenance easier rather than hiding complexity?

Violation examples:

1. Adding a legacy fallback without an owner or removal condition.
2. Leaving open defect notes without gate enforcement.
3. Making a broad rewrite with no staged verification path.

## 2. Verify Lifecycle Test Coverage

Auditors have the responsibility and obligation to use the project's test tools to verify coverage across the full software lifecycle. A review is incomplete if it only reads code and does not run, inspect, or improve the tests needed to prove the behavior.

Lifecycle coverage means the relevant layer is exercised by the right kind of test:

1. Unit tests for local logic, parsing, validation, adapters, state transitions, and error classification.
2. Integration tests for module boundaries, repository contracts, filesystem behavior, network behavior, subprocess behavior, and generated artifacts.
3. End-to-end or workflow tests for user-visible commands, IDE/UI flows, packaging, publishing, runtime execution, and cross-repo handoff.
4. Regression tests for every fixed defect, including the exact failure mode that exposed the defect.
5. Negative, malformed, timeout, permission, compatibility, recovery, and security tests whenever those risks exist.

Audit questions:

1. Did the auditor run the smallest useful test set and the owning gate for the changed behavior?
2. Do tests prove the expected user-visible and machine-visible effects, not only that a function returns successfully?
3. Are boundary conditions covered, including empty input, minimum and maximum sizes, invalid syntax, missing files, permission failures, unsupported versions, partial writes, cancellation, and retries when relevant?
4. Are failures asserted with structured diagnostics or stable machine-readable outcomes instead of brittle human text?
5. Is coverage measured with the project's available tools, or is the absence of a coverage tool recorded as an audit defect?
6. Are skipped, flaky, environment-gated, or manually verified tests called out as incomplete evidence rather than treated as full closure?

Violation examples:

1. Closing a parser defect after only a happy-path CLI smoke test.
2. Claiming security coverage without testing unauthorized, malformed, and oversized inputs.
3. Accepting a UI state change without testing restore, empty workspace, failed adapter, and repeated action paths.
4. Treating a green build as coverage when no test observes the changed behavior.

## 3. Audit Data Structures, Data Flow, And Lifecycle State Machines

Auditors must map how data structures are created, copied, shared, mutated, persisted, handed off, and destroyed. Code that uses data without complete lifecycle management is dangerous code and must not be accepted for submission.

This rule covers ordinary in-memory structures and resource-backed structures, including buffers, AST and IR nodes, caches, registries, workspace models, UI state, database connections, transactions, cursors, file descriptors, streams, temporary files, sockets, subprocess handles, locks, futures, and any state crossing a thread, async, process, filesystem, database, network, repository, plugin, or UI boundary.

Every data structure or resource must have a complete auditable state machine. The state machine can be enforced by types, enums, sealed variants, ownership wrappers, RAII/destructors, context managers, protocol schemas, documented transitions, and tests, but it must be explicit enough for another auditor to verify valid states, transitions, ownership, release, and failure handling. If a database resource, file resource, shared mutable structure, or long-lived object has no state machine, the finding is a blocking defect.

Audit questions:

1. Can the auditor trace every relevant data structure from creation through ownership transfer, mutation, read access, persistence, release, and error cleanup?
2. Are copies, clones, serialization round trips, deep copies, buffer growth, cache duplication, and cross-boundary conversions justified and bounded?
3. Could the design avoid excessive copying with borrowing, references, move semantics, streaming, paging, views, interning, or stable identifiers without weakening safety?
4. Are shared mutable states protected by minimal lock scope, clear lock ordering, non-blocking paths, cancellation behavior, and tests that expose lock contention or deadlock risks?
5. Are memory leaks, resource leaks, use-after-free, stale references, dangling pointers, null pointers, uninitialized values, and double-close paths structurally impossible or explicitly tested?
6. Are database transactions, file handles, temp files, sockets, subprocesses, and external resources released on success, failure, timeout, cancellation, and partial initialization?
7. Does each lifecycle state define allowed operations and fail closed for invalid operations, such as reading after close, committing after rollback, mutating finalized state, or using missing/null data?
8. Are ownership and lifetime boundaries visible in APIs, contracts, diagnostics, and tests rather than hidden in comments or caller assumptions?

Violation examples:

1. A file handle that can be opened, partially written, and abandoned without close, cleanup, or durable failure state.
2. A database transaction API without explicit begin, pending, committed, rolled-back, failed, and closed states.
3. A cache that clones large package graphs on every lookup without caps, eviction, or evidence that copying is safe.
4. A shared workspace model guarded by one broad lock that serializes UI, disk I/O, and adapter calls.
5. A pointer or nullable value dereferenced without an enforced valid state and negative-path test.
6. A resource wrapper whose destructor or cleanup path is bypassed by early returns, exceptions, process errors, or cancellation.

Blocking rule:

1. If lifecycle ownership is ambiguous, mark the code as dangerous and block submission.
2. If a state machine is missing for a data structure or resource with non-trivial lifetime, mark the code as dangerous and block submission.
3. If tests or gates cannot prove lifecycle transitions, cleanup, and invalid-state rejection, record the missing coverage and gate gap as part of the defect.

## 4. Optimize Delivery Gates For Quality

Auditors must improve delivery gates when the gates are too weak to protect the product. Gate quality is judged independently from whether the current software can pass the gate. A weak gate must not be weakened, bypassed, or accepted merely because strict enforcement would expose existing defects.

Gate strictness means the gate blocks unsafe delivery states:

1. Required tests, contract checks, docs checks, hygiene checks, security checks, and packaging checks are run by default for the relevant delivery path.
2. Failures propagate as non-zero exits with actionable diagnostics.
3. Optional or environment-dependent checks clearly report reduced confidence and cannot be used as full closure evidence.
4. Gates validate quality claims directly instead of checking unrelated success conditions.
5. Gate bypasses, allowlists, temporary skips, and legacy bridges have owners, scope limits, and explicit exit criteria.

Audit questions:

1. Would this gate catch the defect class being reviewed before release?
2. Does the gate fail closed when required tools, fixtures, services, credentials, or generated files are missing?
3. Does the gate exercise the durable contract rather than a local implementation detail that can drift?
4. Are gate thresholds, required checks, and skip rules strict enough for product quality even if that causes the current branch to fail?
5. If the current code cannot pass the appropriate gate, is that recorded as a defect with closure evidence rather than hidden by relaxing the gate?
6. Are docs and runbooks updated so future agents know which gate proves each quality claim?

Violation examples:

1. Making a failing contract check optional to get a clean commit.
2. Treating docs generation as a delivery gate while test execution is skipped.
3. Allowing audit defect records to exist without a gate that blocks submission.
4. Reporting success when a required tool is missing, a test directory is empty, or a child process fails.

## Required Finding Format

When an agent records an audit defect, use this minimum shape:

```md
**Status:** Open
**Principle(s):** 4 Fail Closed, 5 Secure And Bounded
**Severity:** High
**Evidence:** path/to/file:line
**Test evidence:** Tests run, coverage inspected, or missing coverage gap.
**Data lifecycle:** State machine, ownership path, resource cleanup proof, or dangerous-code gap.
**Gate impact:** Gate that should catch this, or required gate improvement.
**Required closure:** Concrete fix and required verification.
```

Closed records must include:

```md
**Status:** Closed
**Closure evidence:** Command, test, PR, commit, or documented proof.
**Coverage evidence:** Test or coverage result proving the lifecycle behavior.
**Data lifecycle evidence:** State machine and cleanup evidence proving valid transitions and invalid-state rejection.
**Gate evidence:** Gate command or policy proving the issue cannot ship silently.
```
