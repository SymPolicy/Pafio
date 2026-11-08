# Core / Workflow Runbook

**Purpose:** Route maintenance for Pafio manifests, dependency transactions, metadata, and local workflows.

**Last updated:** 2026-10-23

## Mission

Maintain the local-first Pafio product without assuming ownership of compiler
installation or hosted execution.

## Owned Surface

1. `src/PafioCLI/`, `src/PafioApp/`, and `src/PafioManifest/`
2. `src/PafioResolve/`, `src/PafioWorkflow/`, and `src/PafioPlan/`
3. `src/PafioCore/`, `src/PafioTree/`, and `src/PafioVendor/`
4. focused native and CLI tests
5. black-box acceptance for public workflow and ecosystem-verifier behavior

## Daily Workflow

1. Start from the CLI and manifest contracts.
2. Keep one sync transaction behind `check`, `build`, `run`, and `test`.
3. Keep metadata bounded to its seven v1 fields.
4. Exercise the smallest relevant native filter before cross-repository tests.
5. Keep the fixed-revision verifier acceptance isolated from live owner
   worktrees; validate executable new/build/frozen/metadata behavior only in the
   final full product matrix.
6. Exercise registry graph resolution with a minimal verified cache fixture.
   Do not revive filesystem publish or duplicate the Platform registry service
   to prepare repository-local tests.
7. Keep the top-level `-h` and `--help` aliases equivalent and cover both forms
   with executable CLI tests.
8. Keep process requests and results shared across `ProcessPosix.cpp` and
   `ProcessWindows.cpp`; CMake selects the host implementation, while
   `Process.cpp` owns common result formatting. Windows uses literal Unicode
   arguments, an explicit inherited-handle list, overlapped pipes, and a Job
   Object for requested process-group termination. Capture limits bound retained
   output while every stream continues draining. Only `ProcessRequest.timeout`
   supplies a deadline; native Windows exit codes do not claim POSIX signals.
   The `pafio check|build|run|test` compile invocation deliberately passes no
   internal deadline so a healthy compiler runs to completion; the invoking
   job or environment supplies any bound, while every other external call
   still uses `kExternalProcessStepTimeout`.
   Exercise `pafio_process_tests` on the actual target platform before updating
   a downstream fixed product matrix. CLI headers declare their own standard
   library dependencies; MSVC does not supply the incidental `<string>` import
   previously inherited by `WorkflowApp.hpp` on other toolchains.
9. Keep filesystem paths native inside Pafio, and convert them to UTF-8 only
   when placing them in Windows process fields. Resolve the shared home from a
   nonempty `PAFIO_HOME`, then `HOME`, then the Unix account database; fail
   explicitly when no home can be resolved.

## Change Classes

1. Small: isolated validation or rendering fix.
2. Medium: manifest, lock, resolution, metadata, or workflow behavior.
3. High: public command, schema, atomicity, or external process boundary.

## Required Gates

```bash
cmake --build build-codex --target pafio pafio_native_tests
ctest --test-dir build-codex -R '<focused-pattern>' --output-on-failure
git diff --check
```

For process-only adaptation, build `pafio_process_tests` and run
`ctest --test-dir <build> -R '^PortableProcess\.' --output-on-failure`.
The Windows manual CI lane accepts `native_process_only=true` to run the CLI
build, process contracts, the Unicode compiler-probe producer test, and CLI
probes independently. Full platform release acceptance remains governed by the
post-commit and release specifications.

When `PAFIO_GATE_LOG_DIR` is set, `native-check.sh` retains each configure,
build and test command, full streams and actual exits in a unique subdirectory.
Verbose CTest output, JUnit and copied `Testing/` records preserve named results
before another run or isolated-build cleanup. Logging does not select tests or
change failure handling. Extractability always builds inside its isolated copy.

The Windows implementation follows the [Win32 process creation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
and [overlapped pipe](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-overlapped-i-o)
contracts; argument handling was compared with established LLVM/libuv process
implementations. Tests execute a native fixture to exercise arguments,
environment, working directory, streams, capture limits, and termination.

## Cross-Team Dependencies

Registry / Publish reviews source acquisition and package lifecycle changes.
Styio / Contracts reviews compile-plan and compiler discovery changes.
Docs / Delivery reviews public surface changes.

## Handoff / Recovery

Record the affected transaction, last passing focused test, and remaining owner
handoff. Do not record local paths, credentials, or backend runtime data.

2026-09-05: `check`, `build`, `run`, and `test` accept the opt-in
`--emit-observable-static-snapshot[=<schema-version>]` and repeatable
`--observable-capability <name>` flags, which only add
`emit.observable_static_snapshot` to the compile plan. Plans without the flag
keep ordinary semantic inputs and cache identity; focused coverage lives in `BuildPlanTests` and
`BuildCliTests` plus the `pafio_cli_*_help` CTest entries.

2026-09-05: The same commands accept `--observable-parent-snapshot <path>`
(requires the emission flag), which only adds
`emit.observable_static_snapshot.parent_snapshot_path`. The path stays out of
the cache key; output roots are independently allocated per invocation; the same test suites
cover the flag, its usage errors, and the unchanged cache key.

2026-09-05: The same commands accept `--emit-runtime-observation[=<version>]`
with the dependent `--runtime-observation-mode`,
`--runtime-observation-capability`, `--runtime-observation-lane-capacity`, and
`--runtime-observation-sampling <numerator>/<denominator>[@<seed>]`, which only
add `emit.runtime_observation` with the fields the caller set. The request enters
the cache key so an observed request has distinct cache identity; plans without the flag
keep ordinary semantic inputs and cache identity. The same test suites cover each flag, its dependency on the
emission flag, and the strict value parsing.

2026-10-23: Hardened the registry publication lane and its regression
coverage: role envelopes now require `expires` instead of accepting missing
expiry (`TufVerifier`), source archives stream into a uniquely created
same-directory temporary file instead of a predictable `.tmp` sibling
(`PafioPack`), manifest lib/bin/test target paths reject `..` traversal while
path dependencies keep their existing semantics (`PafioManifest`), registry
reads only follow redirects when no provider headers are attached
(`PafioRegistryClient`), and the publish help CTest matches the documented
`--allow-plaintext-http` usage line. Focused coverage lives in
`TufVerifierTests`, `PackTests`, `ManifestTests`, and the `pafio_cli_*_help`
CTest entries.
