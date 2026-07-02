# Styio Public Interface Roadmap for Pafio

**Purpose:** Describe the exact compiler-facing interfaces that `pafio` needs from `styio`, so future `pafio` maintainers know what to request, test, and vendor without depending on compiler internals.

**Last updated:** 2026-04-23

This file is the sequencing view.

The normative compiler handoff contract now lives in:

- [Styio-External-Interface-Requirement-Spec.md](./Styio-External-Interface-Requirement-Spec.md)

## Required Compiler Interfaces

### 1. Machine Info

Published bootstrap command:

```text
styio --machine-info=json
```

Compile-plan-live fields needed by `pafio`:

- tool identity
- compiler version
- release channel
- supported contract versions
- supported capability flags
- maximum supported edition

Current handoff expectation:

- compile-plan support advertises `[1]`
- `pafio` uses this command for handshake and compatibility gating
- `pafio` may use it as proof of project build orchestration only when the compatibility matrix also enables compile-plan v1

### 2. Compile Plan Entry

Published command:

```text
styio --compile-plan <path>
```

`pafio` needs:

- explicit acceptance or rejection of a plan version
- isolated output directories
- stable error reporting through text or JSON diagnostics
- direct black-box acceptance through `scripts/styio-interface-gate.py --require-compile-plan`

Status:

- active for compile-plan v1 and covered by `scripts/styio-interface-gate.py --require-compile-plan`
- must not be guessed or reverse-engineered from compiler internals

### 3. JSON Diagnostics

`pafio` should consume only stable machine-readable diagnostics, not human-only stderr text.

### 4. Handoff Gate

Compiler publication is not complete until the released binary passes the handshake gate:

```text
./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio
```

and the required compile-plan handoff gate:

```text
./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio --require-compile-plan
```

## Interface Ownership

- `pafio` owns compile-plan schema source.
- `styio` owns compiler capability declarations.
- compatibility is determined by handshake plus contract version support.

## Non-Goals

`pafio` does not need:

- direct AST access
- direct parser access
- direct type-checker access
- direct linker/runtime embedding

## Known Tradeoffs

- Waiting for formal interfaces is slower than reaching into compiler internals.
- Public interface design takes longer upfront but makes the later repository split much safer.
