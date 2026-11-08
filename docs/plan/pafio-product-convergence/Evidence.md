# Pafio Product and Ecosystem Convergence Evidence

**Purpose:** Record passing revisions, commands, and contract digests for each convergence gate.

**Last updated:** 2026-07-30

No evidence is recorded until its gate passes. Records exclude credentials,
private paths, user information, and backend runtime data.

## SSOT convergence regression

- Gate: focused regression passed; independent audit correction applied
- Recorded at: `2026-07-30T10:54:30Z`
- Command digests:
  - `d4bbb0945c1a1c73a6fe594c02a42560899a8c3df3752518b8f44d9dc767245d`
  - `4f9cc27bc72718b087c25ac27966dc2508259ede9e8bf0a3cc382e90134755bf`
- Contract digest: `5afb6ba6087d683d4305bca890cd48b7ec9bc83c606162756809f2def962e251`
- Content fingerprint: `cf8b851efd82ed7b6e572733fe0669e5e8a776a3a8edfda419ca218486b9f124`
- Result: manifest validation and documentation audit both reported zero issues.

## Pafio product surface regression

- Gate: focused configure, build, and product-surface regression passed; independent audit passed
- Recorded at: `2026-07-30T11:58:51Z`
- Contract digest: `37f95adf776809c532d11a2b66a60a3bde074783fe60b60884089559eb20efd9`
- Content fingerprint: `905bbcbc261a17c24b5bef189ca1e4db4a18c8fd62afabef9b49b9bf514ecdb6`
- Result: the retained command surface, shared sync workflow, metadata v1,
  external Styio discovery, read-only doctor, and package lifecycle passed.

## Ecosystem owner cutover

- Owner-matrix schema: `1`
- Matrix digest: `e6d3fa94c1874db196fe948a3e1463c70a7849390d1827aca8d6f7f9c6913b30`
- Result: 7 immutable repository revisions, 10 owner contracts, and 15
  producer/consumer assertions passed.

| Owner | Accepted revision |
| --- | --- |
| Pafio | `ec1a6ae9eb35a0ce54cdc6966944bfa4b0b98df3` |
| Styio | `3a6a25def4b48d9bea07501170d7449af57d60d5` |
| Styio Platform | `0aa1082a4a5d979ee738ae98e4c4b5973a4bb7ea` |
| Vityo | `602fdc9e622444fd63bfd8c6a8a648a7a1a21ebe` |
| Public site | `20c5411d46c7df74fde833a039f9d840aab12d99` |
| Audit policy | `e23f0086d00d3bc5795abae638317ce39f8bf45e` |
| Aggregate workspace | `842895ed910335bf3e2c493860eef64ed743d029` |

Vityo consumes Pafio metadata/workflow, Styio machine-info, and Platform hosted
contracts through separate owner adapters. The fixed consumer revision does not
read Pafio's private home state.

## Atomic identity cutover

The one-time fixed-revision inventory scanned committed content and committed
file names in all seven repositories. Every repository reported zero retired
identity matches and zero compatibility artifacts. The inventory was not
retained as a permanent gate.

## Maintained full regressions

| Repository | Result |
| --- | --- |
| Pafio | 116 tests, 0 failures; 2 optional external-fixture cases skipped |
| Styio | 1138 tests, 0 failures; 9 conditional cases skipped |
| Styio Platform | 54 tests, 0 failures |
| Vityo | 198 Python tests and 2730 Flutter tests passed; 3 conditional Flutter cases skipped |
| Public site | site build, release-root validation, and smoke checks passed |
| Audit policy | 40 tests, module validation, and framework-only self-audit passed |
| Aggregate workspace | root configure and declared Styio target build passed |

## Fixed-revision product matrix

- Matrix digest: `e6d3fa94c1874db196fe948a3e1463c70a7849390d1827aca8d6f7f9c6913b30`
- Cold `pafio new` and `pafio build`: passed
- Frozen rebuild: passed without changing lock or resolution bytes
- Metadata v1: exactly package, workspace, dependencies, targets, lock,
  resolution, and vendor
- Styio compile-plan consumer: advertised and exercised
- Vityo public product scenario: passed
- Platform registry/control-plane and worker contracts: passed in the Platform
  maintained suite

The recorded evidence contains only revisions, digests, test counts, and
contract outcomes. It excludes private paths, credentials, user information,
and backend runtime data.

## Documentation and plan closure

- Better Plan manifest validation: passed with zero issues
- Repository-local documentation and lifecycle gate: passed
- Explicit fixed-workspace documentation gate: 8 contract groups passed across
  Pafio, Styio, Styio Platform, and Vityo
- Repository hygiene and Markdown diff checks: passed
