# Pafio Product and Ecosystem Convergence Architecture

**Purpose:** Define the implementation composition and ownership handoffs for Pafio convergence.

**Last updated:** 2026-07-30

Pafio owns manifest, lock, resolution, sync, metadata, local project workflows,
vendor, pack, and publish client behavior. Styio owns compilation contracts.
Platform owns registry and hosted execution. Vityo composes their machine APIs.

The local workflow is sync → discover/validate Styio → emit compile plan with
`generated_by.tool = "pafio"` → invoke Styio → emit a stable workflow envelope.

Metadata is a canonical project snapshot. The dependency kernel retains sorted
O(V + E) resolution and verified content-addressed storage. Compiler selection is
stateless. Capability cropping happens before one mechanical and semantic identity
cutover. Release evidence records exact repository revisions and contract digests.
