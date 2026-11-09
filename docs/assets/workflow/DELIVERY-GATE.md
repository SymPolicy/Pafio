# Delivery Gate

**Purpose:** Define the common delivery-floor entrypoint for `pafio` so contributors can run repository hygiene, the unified docs gate, and checkpoint health through one command before checkpoint merge or branch delivery.

**Last updated:** 2026-05-02

## Command

Unified delivery floor:

```bash
./scripts/delivery-gate.sh
```

Push or branch-delivery floor:

```bash
./scripts/delivery-gate.sh --mode push --base origin/main
```

Docs/process-only delivery:

```bash
./scripts/delivery-gate.sh --skip-health
```

## What It Runs

1. worktree hygiene and docs ownership checks when local changes exist
2. push-range hygiene and docs ownership checks when `HEAD` is ahead of the delivery base
3. `python3 scripts/secret-scan.py`
4. `./scripts/checkpoint-health.sh`

`auto` mode is the default. It must not report success from an empty staged
diff when unstaged worktree changes or PR-range changes exist. Low-level
`staged`, `checkpoint`, and `push` modes are retained for hooks and targeted
debugging, but the delivery-facing command is the no-argument entrypoint.

## Credential Scan Coverage

The CI `range-scan` job runs the repository-local `scripts/secret-scan.py`
over the outgoing range (token/key shapes only, per commit including merges
against their first parent). The broader privacy, identity, local-path and
workspace-hygiene contract is owned by the operator-side publication gate,
not by CI; this split is deliberate and recorded here so the required merge
checks stay unambiguous.
