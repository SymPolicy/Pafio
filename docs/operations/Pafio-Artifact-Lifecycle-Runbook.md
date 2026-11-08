# Pafio Artifact Lifecycle Runbook

**Purpose:** Keep generated project, build, cache, and package outputs out of tracked source and delivery trees.

**Last updated:** 2026-07-30

## Source of Truth

- `scripts/artifact-policy.json`
- `scripts/artifact_policy.py`
- `scripts/artifact-policy-rsync-excludes.py`

## Generated State

The following are local artifacts and must not be tracked:

- `.pafio/` and build directories
- `PAFIO_HOME` contents
- Python and tool caches
- deterministic package archives such as `*.pafio.src.tar`
- logs, temporary files, and exported delivery trees

## Validation

```bash
python3 scripts/repo-hygiene-gate.py --mode tracked
python3 scripts/check_no_binaries.py --mode tracked
python3 scripts/delivery-gate.py
```

When a new build step creates another artifact class, update the JSON policy
and `.gitignore` together, then run the focused hygiene tests.
