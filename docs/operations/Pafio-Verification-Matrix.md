# Pafio Verification Matrix

**Purpose:** Define the named gates, required commands, and pass conditions that close each `pafio` implementation stream.

**Last updated:** 2026-04-23

## Gate Matrix

### `pafio_repository_delivery_gate`

Objective:

- validate repository hygiene, documentation ownership, and release delivery entrypoints before PR submission

Commands:

```text
python3 scripts/docs-audit.py
python3 scripts/submit-gate.py --profile ci --json
python3 scripts/perf-gate.py
python3 scripts/repo-hygiene-check.py --mode tracked
python3 scripts/delivery-gate.py --json
./scripts/delivery-gate.sh --mode push --base origin/nightly
```

Pass conditions:

- `scripts/docs-audit.py` reports no documentation ownership drift
- `scripts/submit-gate.py` completes the CI profile without quality failures
- `scripts/perf-gate.py` keeps configured performance smoke checks within budget
- `scripts/repo-hygiene-check.py` reports no tracked hygiene or policy drift
- `scripts/delivery-gate.py` validates the extractable delivery tree
- `scripts/delivery-gate.sh` runs the consolidated push gate stack for the branch under submission

Defect:

- the push base must be selected for the target branch policy under review

### `pafio_manifest_lock_gate`

Objective:

- validate manifest and lockfile parsing rules

Commands:

```text
./scripts/native-check.sh
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
./scripts/pafio --json check --manifest-path tests/unit/fixtures/manifests/ok-single-package/pafio.toml --styio-bin ./build-codex/bin/styio
./scripts/styio-interface-gate.py --styio-bin ./build-codex/bin/styio
./build-codex/bin/styio_test --gtest_filter=StyioDiagnostics.MachineInfoJsonReportsStableHandshakeFields
```

Pass conditions:

- `styio` emits valid machine-info JSON
- `styio` machine-info includes the required published handshake fields
- `pafio` accepts the published bootstrap compiler handshake
- compiler-side handshake regression remains green

Defect:

- only validates handshake and compatibility, not project build support

### `styio_compile_plan_contract_gate`

Objective:

- validate the active compiler-side compile-plan v1 consumer and the `pafio` handoff

Commands:

```text
./scripts/styio-interface-gate.py --styio-bin <styio-workspace>/build-codex/bin/styio --pafio-bin ./build-codex/bin/pafio --require-compile-plan --json
```

Pass conditions:

- `styio --machine-info=json` advertises `supported_contracts.compile_plan = [1]` or another enabled line for the active phase
- `pafio check` reports `integration_phase = "compile-plan-live"` and `supported_compile_plan_versions = [1]`
- `styio --compile-plan <path>` accepts a dry-run plan emitted by `pafio`
- compile-plan execution materializes the declared output roots and `receipt.json`

Defect:

- no known baseline blocker; keep expanding malformed-plan and release-matrix coverage

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

- schema fixture coverage is still narrower than the live handoff and should keep growing with release hardening

### `pafio_cli_gate`

Objective:

- preserve CLI surface, exit codes, and error payload shape

Commands:

```text
./scripts/native-check.sh
./scripts/pafio --version
./scripts/pafio machine-info --json
./scripts/pafio sync --help
./scripts/pafio --json build
```

Pass conditions:

- known implemented commands succeed
- known stubbed commands fail with code `31`
- JSON error payload shape remains stable

Defect:

- some command semantics remain placeholder-only until later phases

### `pafio_installer_bootstrap_gate`

Objective:

- validate the fresh-machine bootstrap flow where `curl` installs `pafio`, `pafio install styio@latest` builds a managed compiler from source, and the managed `styio` shim executes from `PATH`

Commands:

```text
ctest --test-dir build-codex -R pafio_installer_bootstrap_smoke --output-on-failure
python3 -m http.server <port> --bind 127.0.0.1
curl -fsSL http://127.0.0.1:<port>/install-pafio.sh | sh -s -- --base-url http://127.0.0.1:<port> --install-dir /usr/local/bin
PAFIO_STYIO_SOURCE_ORIGIN=file://<styio-workspace> PAFIO_STYIO_SOURCE_REF=nightly pafio install styio@latest
styio --file /tmp/hello.styio
```

Pass conditions:

- `pafio_installer_bootstrap_smoke` passes in a temporary install root without requiring `/usr/local/bin`
- `command -v pafio` resolves to the installed binary
- `command -v styio` resolves to the installed shim
- `pafio install styio@latest` installs a compatible stable compiler under `PAFIO_HOME/tools/styio/current/`
- `styio --file` can execute a hello program and print `hello, styio!`

Defect:

- public GitHub source fetch is still subject to network TLS availability in the execution environment; local file-backed source origins are accepted for VM smoke rehearsal

### `pafio_registry_server_gate`

Objective:

- validate that a registry write origin can publish immutable package objects and that a registry read origin can fetch the newly published package through the canonical layout

Commands:

```text
bash ./tests/interop/registry-server-gate.sh ./build-codex/bin/pafio
```

Pass conditions:

- initial publish succeeds against the configured write root
- duplicate publish is rejected
- the published package is fetchable from the configured read root
- the gate runs under an isolated temporary `PAFIO_HOME`

Defect:

- auth and account policy are intentionally deferred

### `pafio_registry_promotion_gate`

Objective:

- validate the recommended split-origin deployment model where package publication lands in a writable upload root and a separate read root serves clients only after promotion

Commands:

```text
bash ./tests/interop/registry-split-origin-promotion.sh ./build-codex/bin/pafio
```

Pass conditions:

- a package can be published into the write root
- client fetch fails before promotion into the read root
- promotion mirrors a self-consistent `registry v2` static read root into the read origin
- repeated promotion is idempotent
- client fetch succeeds from the read root after promotion

Defect:

- the repository helper covers local backing-store promotion, not arbitrary remote replication platforms

### `pafio_registry_split_origin_http_gate`

Objective:

- validate the deployed shape where package publication uses an HTTP write origin, clients read from a separate HTTP read origin, and promotion happens between the two backing roots

Commands:

```text
bash ./tests/interop/registry-split-origin-http.sh ./build-codex/bin/pafio
```

Pass conditions:

- publish succeeds against the HTTP write origin
- fetch from the HTTP read origin fails before promotion
- promotion populates the read-side backing root
- fetch from the HTTP read origin succeeds after promotion
- duplicate publish remains rejected at the write origin

Defect:

- the gate still uses repository-local backing-store promotion rather than a cloud vendor's own replication mechanism

### `pafio_registry_v2_contract_gate`

Objective:

- validate the machine-readable `v2` static registry contract pack, schema inventory, and example-object coverage

Commands:

```text
python3 ./tests/interop/registry-v2-contract-gate.py
```

Pass conditions:

- `contracts/registry-v2/v1/registry-v2.contract.json` advertises the expected schema inventory
- every referenced schema file exists and parses as JSON
- `registry-v2.examples.json` covers config, signed metadata envelopes, package index records, and log leaves
- no drift is introduced between the tracked contract pack and the gate snapshot

Defect:

- the tracked public tree uses structural contract checks rather than a full external JSON Schema validator runtime

### `pafio_registry_v2_publish_gate`

Objective:

- validate the tracked local `v2` publish control-plane worker that commits source packages directly into the signed static read plane

Commands:

```text
bash ./tests/interop/registry-v2-publish.sh ./build-codex/bin/pafio
```

Pass conditions:

- a fresh `v2` registry root is initialized from role keys without requiring a `v1` import first
- `registry-v2-publish.py` can prepare a publish candidate from a real `pafio publish --dry-run` flow
- repeated publishes append new index records and new log leaves
- duplicate publish of the same version is rejected
- the resulting root passes `registry-v2-verify.py`

Defect:

- the tracked public worker currently targets local roots only; the hosted network control plane remains future work

### `pafio_registry_v2_static_http_gate`

Objective:

- validate that the `v2` static read plane is consumable over ordinary HTTP and not only from a local directory

Commands:

```text
bash ./tests/interop/registry-v2-http-read.sh ./build-codex/bin/pafio
```

Pass conditions:

- an imported `v2` registry root can be served by a plain static HTTP server
- `config`, trust metadata, package index, and source artifacts are fetchable through GET
- `registry-v2-verify.py --root http://...` accepts the served static root

Defect:

- the tracked gate validates static HTTP consumption and integrity, not CDN edge-caching semantics

### `pafio_registry_control_plane_contract_gate`

Objective:

- validate the versioned HTTP contract package for the registry `v2` publish/verify control plane

Commands:

```text
python3 ./tests/interop/registry-control-plane-contract-gate.py
python3 ./tests/interop/native-contract-source-gate.py
```

Pass conditions:

- the method/path snapshot remains frozen
- the example pack matches the contract shapes
- native contract sources remain free of generated API-description artifacts

Defect:

- the local shape gate is structural; it does not yet use a standalone JSON Schema validator runtime

### `pafio_registry_control_plane_http_gate`

Objective:

- validate the local HTTP implementation of the registry `v2` control plane against the published contract surface

Commands:

```text
bash ./tests/interop/registry-v2-control-plane-http.sh ./build-codex/bin/pafio
```

Pass conditions:

- `GET /status` reports the bound root before and after initialization
- `POST /publish` initializes a fresh root and commits a release
- `POST /verify` validates the resulting static root
- duplicate publish attempts are rejected through the published failure envelope

Defect:

- the tracked server binds a local root and local key directory; hosted tenancy and auth policy remain future work

### `pafio_cloud_compile_stress_gate`

Objective:

- validate the public multi-tenant compile-cloud stress framework, including high concurrency, bounded container capacity, tenant isolation, and hot replacement lifecycle behavior

Commands:

```text
./scripts/cloud-compile-stress.py \
  --tenants 4 \
  --containers-per-tenant 2 \
  --slots-per-container 4 \
  --jobs 1000 \
  --concurrency 128 \
  --hot-replace-every 200 \
  --require-hot-replacement \
  --summary-json /tmp/pafio-cloud-stress-summary.json \
  --events-jsonl /tmp/pafio-cloud-stress-events.jsonl
python3 tests/unit/test_cloud_compile_stress.py
```

Pass conditions:

- all scheduled compile jobs finish
- every tenant completes work
- no job crosses tenant/container ownership boundaries
- no container exceeds its declared slot capacity
- draining containers never accept new jobs
- no draining container remains after workload drain
- configured failure-rate and p95 thresholds are respected
- hot replacement occurs when required

Defect:

- the tracked public gate is synthetic and deterministic; it models the compile-cloud scheduler and container lifecycle before a production remote scheduler, queue, or container runtime is available

### `pafio_registry_write_header_gate`

Objective:

- reserved for private security-module validation of explicit write-origin request headers without changing read-side client behavior

Commands:

```text
implement under ./tests-private/interop/
```

Pass conditions:

- publish without the required write-origin header fails
- publish with `--registry-header <name:value>` succeeds only in a private build
- the remote publish JSON payload stays redacted and reports only security mode and header count
- after promotion into the read root, client fetch succeeds without requiring write-origin credentials

Defect:

- keep this gate out of the tracked public tree because it exercises private auth-bearing behavior

### `pafio_registry_write_policy_file_gate`

Objective:

- reserved for private security-module validation of write-origin policy-file behavior without changing read-side client behavior

Commands:

```text
implement under ./tests-private/interop/
```

Pass conditions:

- publish without a matching policy file fails at the gated write origin
- publish with `--registry-policy-file <path>` succeeds only in a private build
- the remote publish JSON payload stays redacted and does not emit raw headers or resolved private policy paths
- after promotion into the read root, client fetch succeeds without requiring write-origin credentials

Defect:

- keep this gate out of the tracked public tree because it exercises private auth-bearing behavior

### `pafio_registry_write_profile_gate`

Objective:

- reserved for private security-module validation of named write-origin profiles under `PAFIO_HOME` without changing read-side client behavior

Commands:

```text
implement under ./tests-private/interop/
```

Pass conditions:

- publish without the registry profile fails at the gated write origin
- publish with `--registry-profile <name>` succeeds only in a private build
- the remote publish JSON payload may report the selected profile name but must stay redacted otherwise
- after promotion into the read root, client fetch succeeds without requiring write-origin credentials

Defect:

- keep this gate out of the tracked public tree because it exercises private auth-bearing behavior

### `pafio_extractability_gate`

Objective:

- ensure the subtree remains movable as its own project

Commands:

```text
./scripts/extractability-check.sh
```

Pass conditions:

- copied subtree runs native checks successfully
- copied subtree does not require compiler source layout assumptions

Defect:

- this gate proves subtree independence, not full standalone package-manager functionality

### `pafio_workflow_gate`

Objective:

- validate build/run/test orchestration once compile-plan exists

Commands:

- deferred until published compile-plan support exists

Pass conditions:

- `pafio build`
- `pafio run`
- `pafio test`

all work against an external published `styio` binary and isolated project temp roots

Defect:

- blocked on future compiler contract publication

### `styio_pafio_dual_maintenance_gate`

Objective:

- prove both repositories can be maintained independently but interoperably

Commands:

```text
./scripts/preflight-readiness-check.py --styio-bin ./build-codex/bin/styio
```

Pass conditions:

- native checks pass
- extractability check passes
- external compiler handshake passes
- styio handoff spec and black-box gate script are present in the copied subtree
- documentation and compatibility matrix are present in the copied subtree

Defect:

- still does not certify compile-plan and registry functionality before those phases land
