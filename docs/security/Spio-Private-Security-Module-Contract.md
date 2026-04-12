# Spio Private Security Module Contract

**Purpose:** Define which security-sensitive responsibilities stay out of the open-source `spio` tree and how closed-source implementations are injected without changing the public command or repository contracts.

**Last updated:** 2026-04-12

## 1. Public Boundary

The open-source tree exposes security hooks only through:

- [RegistrySecurity.hpp]($HOME/DevSpace/Unka-Malloc/styio-spio/src/SpioSecurity/RegistrySecurity.hpp)

The open-source build may also discover optional private sources under these gitignored paths:

- `src-private/`
- `tests-private/`
- `docs-private/`
- `scripts-private/`

The open-source extractability path must exclude those directories.

## 2. Closed-Source Responsibilities

The following logic must not live in the tracked public tree:

- remote publish auth and account mapping
- upload-token or request-signing logic
- write-origin policy/profile resolution that contains or derives credentials
- read-side registry trust allowlists and credential injection
- environment-specific decisions about which registries are trusted, blocked, or limited to `https`
- any diagnostic or JSON output that would reveal secret headers, tokens, account identifiers, or private policy file paths

## 3. Read-Side Security Hook

Registry consumption uses the public read-side interface in [RegistrySecurity.hpp]($HOME/DevSpace/Unka-Malloc/styio-spio/src/SpioSecurity/RegistrySecurity.hpp).

The public default build currently does only this:

- accepts `file://`, `http://`, and `https://` registry roots
- attaches no read-side credentials or custom headers
- leaves stricter trust policy to a private module

Closed-source implementations may override that behavior to:

- require `https`
- restrict allowed registry roots
- attach read-side request headers or other credentials
- enforce environment-specific trust policy

## 4. Write-Side Security Hook

Remote publish uses the public write-side interface in [RegistrySecurity.hpp]($HOME/DevSpace/Unka-Malloc/styio-spio/src/SpioSecurity/RegistrySecurity.hpp).

The public default build currently does only this:

- allows anonymous remote publish to `http://` and `https://` write roots
- rejects `--registry-header`, `--registry-policy-file`, and `--registry-profile`
- exposes only redacted observability in JSON results:
  - `registry_security_provider`
  - `registry_write_security_mode`
  - `registry_header_count`
  - optional `registry_profile`

Closed-source implementations may override that behavior to:

- resolve deployment-owned profiles
- map accounts or tenants to upload policy
- inject request headers or signatures
- enforce write-origin allowlists and environment-specific policy

## 5. Recommended Private Layout

Use this local layout for closed-source extensions:

```text
src-private/
  SpioSecurity/
    RegistrySecurity_private.cpp

tests-private/
  CMakeLists.txt
  interop/
  native/
```

The build automatically compiles `src-private/*.cpp` when present, and public test configuration may add `tests-private/CMakeLists.txt` when present.

## 6. Public/Private Documentation Split

- public docs define the command surface, redacted output, and integration boundary
- deployment-specific auth/account rules belong in private docs under `docs-private/`
- server/client registry docs must link here instead of embedding private auth behavior directly
