# Registry / Publish Runbook

**Purpose:** Route maintenance for Pafio registry reads, trust, packaging, and publish-client behavior.

**Last updated:** 2026-10-23

## Mission

Maintain safe package consumption and publication clients without implementing
Platform storage, authorization, promotion, mirror, or deployment services.

## Owned Surface

1. `src/PafioRegistryClient/` and `src/PafioSecurity/`
2. `src/PafioPack/` and `src/PafioPublish/`
3. `docs/registry/`
4. focused registry, pack, publish, and malicious-archive tests

## Daily Workflow

1. Verify trust metadata, immutable identity, size, digest, and archive paths.
2. Keep cache promotion atomic and offline behavior deterministic.
3. Keep `.pafio.src.tar` and Platform publish-request contracts aligned.
4. Send service-side changes to Styio Platform.

## Change Classes

1. Small: local validation or documentation fix.
2. Medium: cache, trust, pack, or publish request behavior.
3. High: immutable object identity, archive security, or Platform route.

## Required Gates

```bash
ctest --test-dir build-codex -R 'Pack|Publish|Security|Tuf|Sync' --output-on-failure
python3 tests/interop/native-contract-source-gate.py
```

## Cross-Team Dependencies

Core / Workflow reviews dependency-visible behavior. Platform reviews service
contracts. Docs / Delivery reviews registry ownership text.

## Handoff / Recovery

Record the client stage, immutable identity, structured failure category, and
whether the next action belongs to Pafio or Platform. Never record credentials.
