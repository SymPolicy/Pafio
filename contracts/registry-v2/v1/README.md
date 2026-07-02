# pafio Registry V2 Contract Pack

**Purpose:** Hold the machine-readable source-of-truth for the industrialized `pafio` static registry `v2` protocol.

**Last updated:** 2026-04-21

## Scope

- static read-plane object layout
- trust metadata envelopes and role topology
- append-only package index records
- source/binary artifact descriptors
- transparency-log checkpoint and leaf objects

## Rules

- The static read plane is immutable and mirror-friendly.
- Authentication and write authorization live in the publish control plane, not in this directory.
- Any breaking change to object layout or signed metadata requires a new versioned directory.

## Contents

- `registry-v2.contract.json` — overview of the public object families and their schema bindings
- `registry-v2.examples.json` — sample objects for every major file class
- `*.schema.json` — JSON Schemas for config, signed metadata, package index records, and transparency-log objects
