# Security Docs

**Purpose:** Define Pafio's local supply-chain and subprocess security boundary.

**Last updated:** 2026-07-30

Pafio owns validation for manifests, locks, registry trust metadata, immutable
content hashes, bounded downloads, archive prescan, atomic writes, file locks,
and the explicit Styio subprocess.

Credentials, production keys, account policy, registry write authorization,
hosted workspace data, and backend runtime records are not stored in this
repository. Service-side security policy belongs to Styio Platform.

Security changes require focused malicious-input and rollback tests before the
final repository regression.
