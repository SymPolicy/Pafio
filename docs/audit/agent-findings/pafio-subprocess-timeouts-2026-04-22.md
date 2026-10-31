# PAFIO Subprocess Timeout Audit - 2026-04-22

**Purpose:** Record subprocess timeout findings from the parallel external audit pass.

**Last updated:** 2026-04-22

Scope: resolver, source-build, registry, and native process execution paths in `pafio`.

## Findings And Remediation

1. `RunProcess` accepted a timeout but only triggered it when `poll` quiesced. A child that continuously produced stdout or stderr could overrun the wall-clock timeout.
   - Fixed in `src/PafioCore/Process.cpp` by checking the elapsed wall-clock budget before each poll and killing the child process group immediately once expired.
   - Added `ProcessTests.TimesOutWhenChildKeepsProducingOutput`.

2. Resolver git and tar subprocesses did not set explicit timeouts.
   - Added the shared process timeout constants to `git clone --mirror`, `git cat-file`, `git fetch`, `git archive --output`, and `tar -xf` in `src/PafioResolve/Resolver.cpp`.
   - Timeout failures now use `DescribeProcessFailure`, so errors identify timed-out subprocesses clearly.

3. Source-build subprocesses did not set explicit timeouts.
   - Added probe timeout for `git rev-parse`.
   - Added default step timeout for source fetch/checkout operations through `RunChecked`.
   - Added build timeout for `cmake configure` and `cmake --build` in `src/PafioToolchain/SourceBuild.cpp`.

4. Registry native subprocesses did not consistently set explicit timeouts.
   - Added timeouts for registry `curl` fetch/download and artifact `tar -xf` in `src/PafioRegistryClient/Client.cpp`.
   - Added timeouts for registry v2 keygen, filesystem publish, and HTTP control-plane `curl` publish in `src/PafioApp/PackageApp.cpp`.

5. Native compiler subprocesses could still hang without explicit timeouts.
   - Added probe timeout for `--machine-info=json` in `src/PafioCompat/Compat.cpp`.
   - Added build timeout for compiler `--compile-plan` execution in `src/PafioApp/WorkflowApp.cpp`.

6. Registry v2 Python helper subprocesses did not set `subprocess.run(..., timeout=...)`.
   - Added a 30s OpenSSL timeout and a 600s registry helper subprocess timeout in `src/pafio_registry_v2/common.py` and `src/pafio_registry_v2/publisher.py`.
   - Added unit tests that monkeypatch `subprocess.run` and verify timeout propagation/error reporting.

## Timeout Model

- Probe operations: `30s`
- External git/curl/tar/python helper steps: `10m`
- Source builds and compiler compile-plan execution: `60m`
- Registry v2 OpenSSL helpers: `30s`
- Registry v2 Python dry-run subprocess helper: `600s`

## Validation

- `cmake --build build-codex --target pafio_native_tests -j2`
- `./build-codex/bin/pafio_native_tests --gtest_filter=ProcessTests.*`
- `python3 tests/unit/test_registry_v2.py`

## Remaining Risks

- Timeout values are fixed constants, not command-line or environment configurable.
- C++ registry `curl` calls rely on the shared `RunProcess` wall-clock timeout rather than curl-specific `--max-time` or low-speed options.
- Test and maintenance scripts outside this scoped runtime path still contain subprocess/curl calls without explicit timeouts and were left untouched.
