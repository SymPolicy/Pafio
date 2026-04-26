# Spio Platform Migration Handoff

**Purpose:** Define the local package-manager-to-global-platform boundary after moving server, package-distribution, and compile-platform ownership into `styio-platform`.

**Last updated:** 2026-04-24

## Ownership Boundary

`styio-spio` owns local package-manager behavior: manifests, lockfiles, resolver
state, package fetch, pack, local import/export, offline package use, publish
client UX, local toolchain selection, project-local Styio environment
optimization, and compatibility payload rendering needed by local CLI users.

`styio-platform` owns global service behavior: hosted workspaces,
compile-platform execution, registry server control planes, global package
distribution, multi-region deployment nodes, mirror synchronization,
cloud-service extension contracts, cloud stress tooling, and service-side
runbooks.

## Compatibility Rule

Existing `spio cloud` and registry server helpers may remain temporarily as
compatibility shims while `styio-platform` gets its own CI and release path.
New service behavior should be implemented in `styio-platform` first and then
consumed by `styio-spio` through contracts or released tooling.

`styio-spio` must keep an offline path. If the required package graph is
available through local sources, vendor output, cache entries, or imported
offline bundles, the package manager should not require `styio-platform`.

## Docs Rule

When platform service docs change, update `styio-platform` as the owner. This
repo should link to the platform source of truth and document only the
package-manager client contract or compatibility behavior.
