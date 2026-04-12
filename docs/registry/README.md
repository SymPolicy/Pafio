# Registry Docs

**Purpose:** Separate shared registry layout from client-side consumption rules, server-side write rules, and deployment guidance so `spio` can serve both client and server roles without mixing responsibilities.

**Last updated:** 2026-04-12

## Scope

- registry client behavior
- registry server/write behavior
- deployment and operational baseline for package distribution

## Ownership

- shared repository layout and object format remain owned by [../governance/Spio-Registry-Repository-Contract.md](../governance/Spio-Registry-Repository-Contract.md)
- security-sensitive auth/account/trust boundaries live in [../security/Spio-Private-Security-Module-Contract.md](../security/Spio-Private-Security-Module-Contract.md)
- client-side consumption rules live in [Spio-Registry-Client-Contract.md](./Spio-Registry-Client-Contract.md)
- server-side write rules live in [Spio-Registry-Server-Contract.md](./Spio-Registry-Server-Contract.md)
- deployment baseline lives in [Spio-Registry-Deployment-Baseline.md](./Spio-Registry-Deployment-Baseline.md)
- executable server validation steps live in [../operations/Spio-Registry-Server-Runbook.md](../operations/Spio-Registry-Server-Runbook.md)

## Maintenance Rule

- do not redefine the shared blob/index layout here
- do not mix client cache rules into server deployment checklists
- do not mix server upload/auth policy into client fetch semantics
- do not place private credential, allowlist, or account policy details under this tracked directory
