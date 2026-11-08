# Current State

**Purpose:** Summarize the accepted Pafio product boundary and its current delivery state.

**Last updated:** 2026-07-30

Pafio is the Styio ecosystem's package manager and terminal project workflow
entry. Its trusted dependency kernel supports manifest v1, deterministic lock
and resolution state, content-addressed storage, offline/frozen sync, and
vendoring. The product surface adds project creation, metadata v1,
check/build/run/test orchestration, tree, pack, and publish.

Styio is a system prerequisite and owns compilation outputs. Styio Platform
owns registry services and hosted execution. Vityo consumes their published
machine contracts without reading `PAFIO_HOME`.

The fixed-revision ecosystem matrix is accepted and recorded in
`docs/plan/pafio-product-convergence/`. No product-boundary implementation gap
remains in this delivery.
