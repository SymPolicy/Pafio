# Architecture

## Workspace Boundary

`docs/plan` is the repository planning root. The root `Manifest.json` indexes Plan objects only. Each Plan owns a dedicated directory and a local `Checkpoints.json` execution graph.

## Plan Layout

- `docs/plan/Manifest.json`: workspace index.
- `docs/plan/README.md`: navigation only.
- `docs/plan/repository-delivery-convergence/Requirements.md`: delivery contract.
- `docs/plan/repository-delivery-convergence/Evidence.md`: source inventory and observed open-work signals.
- `docs/plan/repository-delivery-convergence/Validation.md`: requirement-to-check mapping.
- `docs/plan/repository-delivery-convergence/Architecture.md`: current planning workspace boundary.
- `docs/plan/repository-delivery-convergence/Checkpoints.json`: state-machine validated task graph.

## Design Constraints

The workspace is organized by repository delivery responsibility and validation target. It does not preserve planning directories by historical stage, migration round, compatibility era, or old file layout.
