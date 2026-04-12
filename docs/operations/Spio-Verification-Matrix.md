# Spio Verification Matrix

**Purpose:** Define the named gates, required commands, and pass conditions that close each `spio` implementation stream.

**Last updated:** 2026-04-12

## Gate Matrix

### `spio_manifest_lock_gate`

Objective:

- validate manifest and lockfile parsing rules

Commands:

```text
./spio/scripts/native-check.sh
```

Pass conditions:

- all manifest fixtures behave as expected
- all lock fixtures behave as expected
- no non-deterministic failures across repeated runs

Defect:

- bootstrap validation still stops short of canonical write-back

### `styio_contract_compat_gate`

Objective:

- validate external compiler compatibility without reading compiler source

Commands:

```text
./build-codex/bin/styio --machine-info=json
./spio/scripts/spio --json check --manifest-path spio/tests/unit/fixtures/manifests/ok-single-package/spio.toml --styio-bin ./build-codex/bin/styio
./spio/scripts/styio-interface-gate.py --styio-bin ./build-codex/bin/styio
./build-codex/bin/styio_test --gtest_filter=StyioDiagnostics.MachineInfoJsonReportsStableHandshakeFields
```

Pass conditions:

- `styio` emits valid machine-info JSON
- `styio` machine-info includes the required published handshake fields
- `spio` accepts the published bootstrap compiler handshake
- compiler-side handshake regression remains green

Defect:

- only validates handshake and compatibility, not project build support

### `styio_compile_plan_contract_gate`

Objective:

- validate the published direct compiler-side compile-plan consumer once that phase is advertised

Commands:

```text
./spio/scripts/styio-interface-gate.py --styio-bin ./build-codex/bin/styio --require-compile-plan
```

Pass conditions:

- `styio --machine-info=json` advertises `supported_contracts.compile_plan = [1]` or another enabled line for the active phase
- `styio --compile-plan <path>` accepts a dry-run plan emitted by `spio`
- compile-plan execution materializes the declared output roots

Defect:

- blocked until the compiler team publishes compile-plan support for the active compatibility phase

### `contract_schema_gate`

Objective:

- validate `compile-plan` schema publication and fixture correctness

Commands:

- bootstrap phase: schema file must parse as JSON and stay extractable
- later phase: add positive and negative schema fixture tests here

Pass conditions:

- schema is valid JSON
- positive fixtures validate
- negative fixtures are rejected

Defect:

- currently only partially implemented because `styio` has not yet published compile-plan support

### `spio_cli_gate`

Objective:

- preserve CLI surface, exit codes, and error payload shape

Commands:

```text
./spio/scripts/native-check.sh
./spio/scripts/spio --version
./spio/scripts/spio machine-info --json
./spio/scripts/spio --json build
```

Pass conditions:

- known implemented commands succeed
- known stubbed commands fail with code `31`
- JSON error payload shape remains stable

Defect:

- some command semantics remain placeholder-only until later phases

### `spio_registry_server_gate`

Objective:

- validate that a registry write origin can publish immutable package objects and that a registry read origin can fetch the newly published package through the canonical layout

Commands:

```text
bash ./spio/tests/interop/registry-server-gate.sh ./spio/build-codex/bin/spio
```

Pass conditions:

- initial publish succeeds against the configured write root
- duplicate publish is rejected
- the published package is fetchable from the configured read root
- the gate runs under an isolated temporary `SPIO_HOME`

Defect:

- auth and account policy are intentionally deferred

### `spio_registry_promotion_gate`

Objective:

- validate the recommended split-origin deployment model where package publication lands in a writable upload root and a separate read root serves clients only after promotion

Commands:

```text
bash ./spio/tests/interop/registry-split-origin-promotion.sh ./spio/build-codex/bin/spio
```

Pass conditions:

- a package can be published into the write root
- client fetch fails before promotion into the read root
- promotion copies marker, entry, and blob into the read root
- repeated promotion is idempotent
- client fetch succeeds from the read root after promotion

Defect:

- the repository helper covers local backing-store promotion, not arbitrary remote replication platforms

### `spio_registry_split_origin_http_gate`

Objective:

- validate the deployed shape where package publication uses an HTTP write origin, clients read from a separate HTTP read origin, and promotion happens between the two backing roots

Commands:

```text
bash ./spio/tests/interop/registry-split-origin-http.sh ./spio/build-codex/bin/spio
```

Pass conditions:

- publish succeeds against the HTTP write origin
- fetch from the HTTP read origin fails before promotion
- promotion populates the read-side backing root
- fetch from the HTTP read origin succeeds after promotion
- duplicate publish remains rejected at the write origin

Defect:

- the gate still uses repository-local backing-store promotion rather than a cloud vendor's own replication mechanism

### `spio_registry_write_header_gate`

Objective:

- reserved for private security-module validation of explicit write-origin request headers without changing read-side client behavior

Commands:

```text
implement under ./spio/tests-private/interop/
```

Pass conditions:

- publish without the required write-origin header fails
- publish with `--registry-header <name:value>` succeeds only in a private build
- the remote publish JSON payload stays redacted and reports only security mode and header count
- after promotion into the read root, client fetch succeeds without requiring write-origin credentials

Defect:

- keep this gate out of the tracked public tree because it exercises private auth-bearing behavior

### `spio_registry_write_policy_file_gate`

Objective:

- reserved for private security-module validation of write-origin policy-file behavior without changing read-side client behavior

Commands:

```text
implement under ./spio/tests-private/interop/
```

Pass conditions:

- publish without a matching policy file fails at the gated write origin
- publish with `--registry-policy-file <path>` succeeds only in a private build
- the remote publish JSON payload stays redacted and does not emit raw headers or resolved private policy paths
- after promotion into the read root, client fetch succeeds without requiring write-origin credentials

Defect:

- keep this gate out of the tracked public tree because it exercises private auth-bearing behavior

### `spio_registry_write_profile_gate`

Objective:

- reserved for private security-module validation of named write-origin profiles under `SPIO_HOME` without changing read-side client behavior

Commands:

```text
implement under ./spio/tests-private/interop/
```

Pass conditions:

- publish without the registry profile fails at the gated write origin
- publish with `--registry-profile <name>` succeeds only in a private build
- the remote publish JSON payload may report the selected profile name but must stay redacted otherwise
- after promotion into the read root, client fetch succeeds without requiring write-origin credentials

Defect:

- keep this gate out of the tracked public tree because it exercises private auth-bearing behavior

### `spio_extractability_gate`

Objective:

- ensure the subtree remains movable as its own project

Commands:

```text
./spio/scripts/extractability-check.sh
```

Pass conditions:

- copied subtree runs native checks successfully
- copied subtree does not require compiler source layout assumptions

Defect:

- this gate proves subtree independence, not full standalone package-manager functionality

### `spio_workflow_gate`

Objective:

- validate build/run/test orchestration once compile-plan exists

Commands:

- deferred until published compile-plan support exists

Pass conditions:

- `spio build`
- `spio run`
- `spio test`

all work against an external published `styio` binary and isolated project temp roots

Defect:

- blocked on future compiler contract publication

### `styio_spio_dual_maintenance_gate`

Objective:

- prove both repositories can be maintained independently but interoperably

Commands:

```text
./spio/scripts/preflight-readiness-check.py --styio-bin ./build-codex/bin/styio
```

Pass conditions:

- native checks pass
- extractability check passes
- external compiler handshake passes
- styio handoff spec and black-box gate script are present in the copied subtree
- documentation and compatibility matrix are present in the copied subtree

Defect:

- still does not certify compile-plan and registry functionality before those phases land
