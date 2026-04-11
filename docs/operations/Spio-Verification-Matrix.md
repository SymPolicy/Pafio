# Spio Verification Matrix

**Purpose:** Define the named gates, required commands, and pass conditions that close each `spio` implementation stream.

**Last updated:** 2026-04-09

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
./build-codex/bin/styio_test --gtest_filter=StyioDiagnostics.MachineInfoJsonReportsStableHandshakeFields
```

Pass conditions:

- `styio` emits valid machine-info JSON
- `spio` accepts the published bootstrap compiler handshake
- compiler-side handshake regression remains green

Defect:

- only validates handshake and compatibility, not project build support

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
- documentation and compatibility matrix are present in the copied subtree

Defect:

- still does not certify compile-plan and registry functionality before those phases land
