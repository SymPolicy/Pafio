# Spio Verification Matrix

**Purpose:** Define the named gates, required commands, and pass conditions that close each `spio` implementation stream.

**Last updated:** 2026-04-17

## Gate Matrix

### `quality_no_binaries_gate`

Objective:

- reject tracked binary files in repository sources and delivery exports

Commands:

```text
python3 ./scripts/check_no_binaries.py --repo-root . --mode tracked
```

Pass conditions:

- tracked files contain no accidental binary content

Defect:

- this gate only checks source-tree binaries; archive integrity belongs to delivery checks

### `quality_repo_hygiene_gate`

Objective:

- enforce generated/private-path hygiene and documentation linkage for gate entrypoints

Commands:

```text
python3 ./scripts/repo-hygiene-check.py --repo-root . --mode tracked
```

Pass conditions:

- tracked files contain no forbidden generated/private paths
- `.gitignore` includes required generated/private patterns from `scripts/artifact-policy.json`
- tracked docs/tests temp-build-style fixtures stay visible via explicit negate rules when needed
- operations and governance docs reference gate entrypoints

Defect:

- it reports violations but does not auto-clean local files

### `quality_docs_governance_gate`

Objective:

- enforce generated index freshness, docs lifecycle integrity, and required docs collection skeletons

Commands:

```text
python3 ./scripts/docs-audit.py
```

Pass conditions:

- required docs collections exist and contain `README.md` plus `INDEX.md`
- generated docs indexes are current
- docs lifecycle metadata and ledger are current
- tracked docs include required `Purpose` and `Last updated` headers

Defect:

- it validates structure and freshness, not prose quality or architectural correctness

### `performance_baseline_gate`

Objective:

- detect command-level performance regressions against a committed baseline

Commands:

```text
python3 ./scripts/perf-gate.py
```

Pass conditions:

- each benchmark median stays within the configured regression threshold

Defect:

- benchmark scope is intentionally CLI-focused and does not model end-to-end workload throughput

### `delivery_package_gate`

Objective:

- validate exported delivery tree structure and run checks in the copied package

Commands:

```text
python3 ./scripts/delivery-gate.py
```

Pass conditions:

- exported tree contains required repository modules
- exported tree excludes build/cache/tmp/private artifacts per `scripts/artifact-policy.json`
- binary, hygiene, docs, and native checks pass inside the export

Defect:

- this gate validates repository delivery shape, not registry publish topology

### `spio_submit_gate`

Objective:

- provide one enforceable submission entrypoint for quality, regression, performance, and delivery checks

Commands:

```text
python3 ./scripts/submit-gate.py --profile pre-push
python3 ./scripts/submit-gate.py --profile ci --json
python3 ./scripts/submit-gate.py --profile release --styio-bin /absolute/path/to/styio --feature-config /absolute/path/to/submit-gate.features.json --json
```

Pass conditions:

- `quality_no_binaries_gate`, `quality_repo_hygiene_gate`, `quality_docs_governance_gate`, `spio_manifest_lock_gate`, `spio_extractability_gate`, `performance_baseline_gate`, and `delivery_package_gate` are green
- `styio` compatibility runs when release mode provides `--styio-bin`

Defect:

- release/styio/cloud checks stay disabled when feature config is missing or keeps defaults

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
