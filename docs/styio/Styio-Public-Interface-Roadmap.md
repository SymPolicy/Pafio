# Styio Public Interface Roadmap for Spio

**Purpose:** Describe the exact compiler-facing interfaces that `spio` needs from `styio`, so future `spio` maintainers know what to request, test, and vendor without depending on compiler internals.

**Last updated:** 2026-04-12

This file is the sequencing view.

The normative compiler handoff contract now lives in:

- [Styio-External-Interface-Requirement-Spec.md](./Styio-External-Interface-Requirement-Spec.md)

## Required Compiler Interfaces

### 1. Machine Info

Published bootstrap command:

```text
styio --machine-info=json
```

Bootstrap-stable fields needed by `spio`:

- tool identity
- compiler version
- release channel
- supported contract versions
- supported capability flags
- maximum supported edition

Current bootstrap expectation:

- compile-plan support is still empty
- `spio` may use this command for handshake and compatibility gating
- `spio` may not yet use it as proof that project build orchestration is available

### 2. Compile Plan Entry

Planned command:

```text
styio --compile-plan <path>
```

`spio` needs:

- explicit acceptance or rejection of a plan version
- isolated output directories
- stable error reporting through text or JSON diagnostics
- direct black-box acceptance through `scripts/styio-interface-gate.py --require-compile-plan`

Status:

- not yet published by `styio`
- must not be guessed or reverse-engineered from compiler internals

### 3. JSON Diagnostics

`spio` should consume only stable machine-readable diagnostics, not human-only stderr text.

### 4. Handoff Gate

Compiler publication is not complete until the released binary passes:

```text
./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio
```

and, once compile-plan support is advertised:

```text
./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio --require-compile-plan
```

## Interface Ownership

- `spio` owns compile-plan schema source.
- `styio` owns compiler capability declarations.
- compatibility is determined by handshake plus contract version support.

## Non-Goals

`spio` does not need:

- direct AST access
- direct parser access
- direct type-checker access
- direct linker/runtime embedding

## Known Tradeoffs

- Waiting for formal interfaces is slower than reaching into compiler internals.
- Public interface design takes longer upfront but makes the later repository split much safer.
