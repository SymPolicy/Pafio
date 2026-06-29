# Styio for Spio Developers

**Purpose:** Give future `spio` maintainers a migration-ready knowledge pack for the `styio` compiler project: what is stable, what is not, which documents matter, and which files must never become hidden dependencies.

**Last updated:** 2026-04-23

Read this after:

- [Styio-External-Interface-Requirement-Spec.md](./Styio-External-Interface-Requirement-Spec.md)
- [Styio-Public-Interface-Roadmap.md](./Styio-Public-Interface-Roadmap.md)
- [Spio-Future-Direction-and-Styio-Coordination.md](../../planning/Spio-Future-Direction-and-Styio-Coordination.md)

## 1. Mental Model

`styio` is the compiler and language implementation.

`spio` is the package manager and project workflow tool.

The safe integration boundary is:

- process boundary
- machine-readable compiler metadata
- versioned compile-plan contract
- stable diagnostics format
- black-box acceptance through `scripts/styio-interface-gate.py`

The unsafe integration boundary is:

- compiler-private AST classes
- compiler-private IR classes
- parser implementation details
- local test harness internals

## 2. Current Styio Repository Entry Points

Current local checkout references:

- compiler CLI entry: [src/main.cpp](<styio-workspace>/src/main.cpp)
- compilation session shell: [src/StyioSession/CompilationSession.hpp](<styio-workspace>/src/StyioSession/CompilationSession.hpp)
- parser public header: [src/StyioParser/Parser.hpp](<styio-workspace>/src/StyioParser/Parser.hpp)
- tokenizer public header: [src/StyioParser/Tokenizer.hpp](<styio-workspace>/src/StyioParser/Tokenizer.hpp)
- pipeline test harness: [src/StyioTesting/PipelineCheck.cpp](<styio-workspace>/src/StyioTesting/PipelineCheck.cpp)
- compiler tests: [tests/styio_test.cpp](<styio-workspace>/tests/styio_test.cpp)

When `spio` moves to its own repository, these should be interpreted as canonical paths inside the separate `styio` repository checkout, not as local sibling imports.

## 3. Styio Documents Spio Developers Must Know

Language and syntax SSOT:

- [docs/design/Styio-Language-Design.md](<styio-workspace>/docs/design/Styio-Language-Design.md)
- [docs/design/Styio-EBNF.md](<styio-workspace>/docs/design/Styio-EBNF.md)
- [docs/design/Styio-Symbol-Reference.md](<styio-workspace>/docs/design/Styio-Symbol-Reference.md)

Resource and standard-library model:

- [docs/design/Styio-Resource-Topology.md](<styio-workspace>/docs/design/Styio-Resource-Topology.md)
- [docs/design/Styio-Resource-Driver.md](<styio-workspace>/docs/design/Styio-Resource-Driver.md)
- [docs/design/Styio-StdLib-Intrinsics.md](<styio-workspace>/docs/design/Styio-StdLib-Intrinsics.md)

Known contradictions and implementation gaps:

- [docs/review/Logic-Conflicts.md](<styio-workspace>/docs/review/Logic-Conflicts.md)

Testing and workflow discipline:

- [docs/assets/workflow/TEST-CATALOG.md](<styio-workspace>/docs/assets/workflow/TEST-CATALOG.md)
- [docs/specs/AGENT-SPEC.md](<styio-workspace>/docs/specs/AGENT-SPEC.md)

## 4. Stable Things Spio May Rely On

`spio` may rely on these only after they are explicitly implemented and published by `styio`:

- stable CLI flags
- stable JSON diagnostics
- `styio --machine-info=json`
- `styio --compile-plan <path>`
- supported contract version declarations

Until a behavior is explicitly frozen, `spio` must treat it as unstable.

### Currently Published Public Handshake

The first real public handshake now exists:

```text
styio --machine-info=json
```

Expected compile-plan-live fields:

- `tool`
- `compiler_version`
- `channel`
- `supported_contracts.compile_plan`
- `capabilities`
- `edition_max`

Current handoff expectation is compile-plan-live integration:

- `supported_contracts.compile_plan = [1]`
- `capabilities` includes `machine_info_json`
- `capabilities` includes `single_file_entry`
- `capabilities` includes `jsonl_diagnostics`

That means `spio` may detect compiler compatibility and, after the compatibility matrix agrees, execute project build orchestration through `styio --compile-plan <path>`.

## 5. Things Spio Must Not Depend On

- AST node names or memory layout
- IR node names or textual IR formatting
- parser route statistics
- internal fallback behavior
- exact test harness implementation inside `styio/tests`
- private runtime helper names

These may change even when the public compiler behavior does not.

## 6. Current Reality Check

Today, the public compiler contract baseline is live:

- `styio` has a stable single-file CLI
- `styio` has stable pipeline tests
- `styio` exposes `--machine-info=json` with compile-plan v1 support
- `styio` exposes `--compile-plan <path>` for build/check/run/test plan execution

That means `spio` developers should continue to treat compiler internals as a moving target and work only against formal contracts, even though compile-plan v1 is now an active contract.

## 7. Migration Guidance

Before moving `spio` to `<spio-workspace>`, keep this document and update it to point at the standalone `styio` repository checkout location used by the team.

## 8. Commands Spio Developers Should Know

Compiler metadata and handshake:

```text
styio --machine-info=json
```

Current single-file compiler entry:

```text
styio --file path/to/program.styio
```

Machine-readable diagnostics:

```text
styio --error-format=jsonl --file path/to/program.styio
```

Package-manager-side compatibility probe:

```text
spio check --manifest-path path/to/spio.toml --styio-bin /path/to/styio
```

Minimal compiler-side regression targets to watch:

```text
ctest --test-dir build-codex -R '^StyioDiagnostics\\.MachineInfoJsonReportsStableHandshakeFields$'
ctest --test-dir build-codex -L styio_pipeline
```

Useful supporting material for compatibility work:

- [docs/adr/INDEX.md](<styio-workspace>/docs/adr/INDEX.md)
- [docs/adr/ADR-0002-jsonl-diagnostics.md](<styio-workspace>/docs/adr/ADR-0002-jsonl-diagnostics.md)
- [docs/adr/ADR-0060-runtime-last-error-and-top-level-diagnostic-unification.md](<styio-workspace>/docs/adr/ADR-0060-runtime-last-error-and-top-level-diagnostic-unification.md)
- [docs/adr/ADR-0061-runtime-subcode-taxonomy-freeze.md](<styio-workspace>/docs/adr/ADR-0061-runtime-subcode-taxonomy-freeze.md)

## 9. Split-Repo Checklist

Before the subtree moves to `<spio-workspace>`, confirm:

- all `spio` compiler integration goes through explicit `--styio-bin`, `SPIO_STYIO_BIN`, project-local `spio-toolchain.toml`, or managed current compiler selection
- no `spio` code reads `styio/src` or `styio/tests` directly
- `spio/contracts/` remains the source of truth for package-manager-side contracts
- `styio --machine-info=json` is used for compatibility checks instead of compiler-source inspection
- future `compile-plan` work is gated on a published `styio` interface, not a local branch

## 10. Known Tradeoffs

- This guide duplicates knowledge that also exists in the `styio` repository.
- Absolute local links are convenient today but will need updating after repository split.
- Avoiding compiler internals slows short-term development but reduces long-term coupling.
