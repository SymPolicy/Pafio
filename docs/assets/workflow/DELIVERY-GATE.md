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
3. `./scripts/audit-gate.sh`
4. `./scripts/checkpoint-health.sh`

`auto` mode is the default. It must not report success from an empty staged
diff when unstaged worktree changes or PR-range changes exist. Low-level
`staged`, `checkpoint`, and `push` modes are retained for hooks and targeted
debugging, but the delivery-facing command is the no-argument entrypoint.
