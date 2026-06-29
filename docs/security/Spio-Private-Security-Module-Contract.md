# Spio Private Security Module Contract

**Purpose:** Define which security-sensitive responsibilities stay out of the open-source `spio` tree and how closed-source implementations are injected without changing the public command or repository contracts.

**Last updated:** 2026-04-13

## 1. Public Boundary

The open-source tree exposes security hooks only through:

- [RegistrySecurity.hpp](<spio-workspace>/src/SpioSecurity/RegistrySecurity.hpp)

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

Registry consumption uses the public read-side interface in [RegistrySecurity.hpp](<spio-workspace>/src/SpioSecurity/RegistrySecurity.hpp).

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

Remote publish uses the public write-side interface in [RegistrySecurity.hpp](<spio-workspace>/src/SpioSecurity/RegistrySecurity.hpp).

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

## 5. Extension Pattern

Private implementations should prefer wrapping the public default helpers instead of copying the tracked open-source logic:

- [ResolveDefaultRegistryReadSecurity](<spio-workspace>/src/SpioSecurity/RegistrySecurity.hpp)
- [ResolveDefaultRegistryWriteSecurity](<spio-workspace>/src/SpioSecurity/RegistrySecurity.hpp)

Recommended rules:

- keep one read resolver and one write resolver as the private entrypoints registered through `RegisterRegistryReadSecurityResolver` and `RegisterRegistryWriteSecurityResolver`
- let the public default helper keep ownership of scheme normalization and anonymous fallback behavior
- for write-side private hooks such as `--registry-profile`, `--registry-policy-file`, or explicit headers, consume or validate the private-only inputs first, then delegate to the default helper with a sanitized request that contains only the public baseline fields
- do not add extra file or network I/O on the no-hook fast path; if the request is equivalent to the public default path, return the public default result directly
- keep private diagnostics redacted and never emit resolved credentials, raw headers, account identifiers, or private policy file paths

Example pattern for a private write resolver:

```cpp
static spio::RegistryWriteSecurityDecision PrivateWriteResolver(
    const spio::RegistryWriteSecurityRequest &request)
{
  if (!request.profile_name.has_value() &&
      !request.policy_file.has_value() &&
      request.explicit_request_headers.empty())
  {
    return spio::ResolveDefaultRegistryWriteSecurity(request);
  }

  spio::RegistryWriteSecurityDecision decision =
      spio::ResolveDefaultRegistryWriteSecurity({
          .registry_root = request.registry_root,
      });

  if (!decision.registry_root.starts_with("https://"))
  {
    throw spio::PublishError("private write policy requires https");
  }

  decision.provider_name = "private-module";
  decision.mode = "profile";
  decision.profile_name = request.profile_name;
  decision.request_headers = LoadPrivateHeaders(decision.registry_root, request);
  decision.request_headers.insert(
      decision.request_headers.end(),
      request.explicit_request_headers.begin(),
      request.explicit_request_headers.end());
  return decision;
}
```

The same pattern applies on the read side: call `ResolveDefaultRegistryReadSecurity`, then tighten trust rules or append credentials only when the private deployment actually requires it.

## 6. Recommended Private Layout

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

## 7. Public/Private Documentation Split

- public docs define the command surface, redacted output, and integration boundary
- deployment-specific auth/account rules belong in private docs under `docs-private/`
- server/client registry docs must link here instead of embedding private auth behavior directly
