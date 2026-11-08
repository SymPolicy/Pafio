#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_DIRS = {
    "pafio": "pafio-nightly",
    "styio": "styio-nightly",
    "platform": "styio-cloud-nightly",
    "vityo": "vityo-nightly",
}


@dataclass(frozen=True)
class DocRule:
    repository: str
    path: str
    needles: tuple[str, ...]


@dataclass(frozen=True)
class ContractRule:
    key: str
    summary: str
    docs: tuple[DocRule, ...]


LOCAL_CONTRACTS: tuple[ContractRule, ...] = (
    ContractRule(
        key="pafio.project_surface",
        summary="Pafio manifest, lock, sync, workflow, and external compiler boundaries stay aligned",
        docs=(
            DocRule(
                "pafio",
                "docs/governance/Pafio-Manifest-and-Lock-Conventions.md",
                (
                    "`pafio.toml`",
                    "[pafio]",
                    "[build]",
                    "implicit-std = true",
                    "`pafio.lock`",
                    "`.pafio/resolution-v1.json`",
                    "`single-version-v1`",
                    "`sync` is the sole public lock-refresh",
                ),
            ),
            DocRule(
                "pafio",
                "docs/governance/Pafio-CLI-Contract.md",
                (
                    "new  init  doctor  metadata  add  remove  sync  tree",
                    "check  build  run  test  vendor  pack  publish",
                    "`--locked`",
                    "`--offline`",
                    "`--frozen`",
                    "`--styio-bin`, `PAFIO_STYIO_BIN`, then `styio`",
                    "`pafio doctor` is read-only",
                ),
            ),
        ),
    ),
    ContractRule(
        key="pafio.machine_contracts",
        summary="Pafio metadata and workflow JSON ownership stays narrow",
        docs=(
            DocRule(
                "pafio",
                "docs/governance/Pafio-CLI-Contract.md",
                (
                    "`pafio metadata --json` emits metadata v1",
                    "package, workspace,",
                    "dependencies, targets, lock, resolution, and vendor state",
                    "`pafio --json check|build|run|test`",
                    "target intent",
                    "Styio process status",
                    "Styio owns concrete",
                ),
            ),
        ),
    ),
    ContractRule(
        key="pafio.registry_client",
        summary="Pafio remains the registry client while Platform owns service behavior",
        docs=(
            DocRule(
                "pafio",
                "docs/registry/README.md",
                (
                    "`pafio-static-registry`",
                    "`/api/pafio-registry-control/v1`",
                    "Pafio contains no production registry server",
                ),
            ),
            DocRule(
                "pafio",
                "docs/registry/Pafio-Registry-Client-Contract.md",
                (
                    "`.pafio.src.tar`",
                    "`/api/pafio-registry-control/v1/publish`",
                    "Pafio does not implement registry storage",
                    "not a Vityo integration surface",
                    "in metadata v1",
                ),
            ),
        ),
    ),
)


CROSS_REPOSITORY_CONTRACTS: tuple[ContractRule, ...] = (
    ContractRule(
        key="styio.compile_plan",
        summary="Styio consumes Pafio compile plans and owns compiler result contracts",
        docs=(
            DocRule(
                "pafio",
                "docs/external/for-styio/Styio-for-Pafio-Developers.md",
                (
                    "styio --machine-info=json",
                    "styio --compile-plan <path>",
                    "does not reinterpret or",
                    "runtime-event schemas",
                ),
            ),
            DocRule(
                "styio",
                "docs/external/for-pafio/Styio-Nano-Pafio-Coordination.md",
                (
                    '"tool": "pafio"',
                    "`check`",
                    "`build`",
                    "`run`",
                    "`test`",
                    "`diagnostics.jsonl`",
                    "`receipt.json`",
                    "`runtime-events.jsonl`",
                ),
            ),
            DocRule(
                "vityo",
                "docs/external/for-styio/Styio-Compile-Run-Contract.md",
                (
                    "`styio --machine-info=json`",
                    "`styio --compile-plan <path>`",
                    "`pafio`",
                    "`CliError`",
                    "`diagnostics.jsonl`",
                ),
            ),
        ),
    ),
    ContractRule(
        key="vityo.metadata",
        summary="Vityo consumes metadata v1 instead of reconstructing local project state",
        docs=(
            DocRule(
                "pafio",
                "docs/governance/Pafio-CLI-Contract.md",
                (
                    "`pafio metadata --json` emits metadata v1",
                    "package, workspace,",
                    "dependencies, targets, lock, resolution, and vendor state",
                ),
            ),
            DocRule(
                "vityo",
                "docs/external/for-pafio/Pafio-Metadata-Contract.md",
                (
                    "pafio metadata --json",
                    "`metadata v1`",
                    "`package`",
                    "`workspace`",
                    "`dependencies`",
                    "`targets`",
                    "`lock`",
                    "`resolution`",
                    "`vendor`",
                    "`styio --machine-info=json`",
                ),
            ),
        ),
    ),
    ContractRule(
        key="vityo.workflow",
        summary="Vityo consumes the Pafio workflow envelope while Styio owns concrete results",
        docs=(
            DocRule(
                "pafio",
                "docs/governance/Pafio-CLI-Contract.md",
                (
                    "`pafio --json check|build|run|test`",
                    "stable envelope",
                    "Styio owns concrete",
                    "diagnostic, receipt, and runtime-event schemas",
                ),
            ),
            DocRule(
                "vityo",
                "docs/external/for-pafio/Pafio-Workflow-Success-Payloads.md",
                (
                    "pafio --json build --manifest-path <path>",
                    "pafio --json run --manifest-path <path>",
                    "pafio --json test --manifest-path <path>",
                    "`workflow_success_payloads`",
                    "`receipt_path`",
                    "`diagnostics_path`",
                    "`runtime_events_path`",
                ),
            ),
        ),
    ),
    ContractRule(
        key="platform.hosted_worker",
        summary="Platform owns hosted state and invokes Pafio with a system Styio",
        docs=(
            DocRule(
                "platform",
                "docs/operations/Platform-Regional-Node-Runbook.md",
                (
                    "`pafio build --manifest-path <relative path>`",
                    "`PAFIO_STYIO_BIN`",
                    "system-provided Styio executable",
                ),
            ),
            DocRule(
                "vityo",
                "docs/external/for-platform/Platform-Hosted-Workspace-Contract.md",
                (
                    "`/api/styio-hosted/v1`",
                    "Pafio remains the project workflow executable",
                    "workers invoke `pafio build`",
                    "Vityo uses the Platform adapter for hosted state only",
                ),
            ),
        ),
    ),
    ContractRule(
        key="platform.registry",
        summary="Platform owns the Pafio registry service and control route",
        docs=(
            DocRule(
                "pafio",
                "docs/registry/README.md",
                (
                    "`pafio-static-registry`",
                    "`/api/pafio-registry-control/v1`",
                    "Pafio contains no production registry server",
                ),
            ),
            DocRule(
                "platform",
                "docs/registry/Pafio-Registry-Control-Plane-Contract.md",
                (
                    "`GET /api/pafio-registry-control/v1/status`",
                    "`GET /api/pafio-registry-control/v1/descriptor`",
                    "`POST /api/pafio-registry-control/v1/publish`",
                    "`POST /api/pafio-registry-control/v1/verify`",
                    "`styio-platform` owns the hosted registry",
                ),
            ),
        ),
    ),
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="ecosystem-cli-doc-gate.py")
    parser.add_argument(
        "--workspace-root",
        type=Path,
        help=(
            "optional root containing pafio-nightly, styio-nightly, "
            "styio-cloud-nightly, and vityo-nightly fixed-revision checkouts"
        ),
    )
    parser.add_argument(
        "--require-workspace",
        action="store_true",
        help="require all four fixed-revision repository checkouts",
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    return parser


def check_doc_rule(repository_roots: Mapping[str, Path], rule: DocRule) -> dict:
    path = repository_roots[rule.repository] / rule.path
    result = {
        "repository": rule.repository,
        "path": rule.path,
        "exists": path.is_file(),
        "missing_needles": [],
        "ok": False,
    }
    if not path.is_file():
        return result
    text = path.read_text(encoding="utf-8")
    result["missing_needles"] = [needle for needle in rule.needles if needle not in text]
    result["ok"] = not result["missing_needles"]
    return result


def check_contract(repository_roots: Mapping[str, Path], contract: ContractRule) -> dict:
    docs = [check_doc_rule(repository_roots, rule) for rule in contract.docs]
    return {
        "key": contract.key,
        "summary": contract.summary,
        "ok": all(doc["ok"] for doc in docs),
        "docs": docs,
    }


def print_contracts(contracts: Sequence[dict]) -> None:
    for contract in contracts:
        status = "OK" if contract["ok"] else "FAIL"
        print(f"[{status}] {contract['key']}: {contract['summary']}")
        if contract["ok"]:
            continue
        for doc in contract["docs"]:
            if doc["ok"]:
                continue
            label = f"{doc['repository']}:{doc['path']}"
            if not doc["exists"]:
                print(f"  - missing file: {label}")
                continue
            print(f"  - {label}")
            for needle in doc["missing_needles"]:
                print(f"    missing: {needle}")


def print_human(payload: dict) -> None:
    print_contracts(payload["contracts"])
    cross_status = payload["cross_repository_status"]
    if cross_status == "not_requested":
        print("[INFO] cross-repository docs were not requested")
    elif cross_status == "skipped":
        print(
            "[SKIP] cross-repository docs unavailable: "
            + ", ".join(payload["missing_repositories"])
        )
    elif cross_status == "failed_missing":
        print(
            "[FAIL] required cross-repository docs unavailable: "
            + ", ".join(payload["missing_repositories"])
        )
    print(
        "ecosystem CLI doc gate "
        + ("passed" if payload["ok"] else "failed")
        + f" ({len(payload['contracts'])} contract groups)"
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.require_workspace and args.workspace_root is None:
        parser.error("--require-workspace requires --workspace-root")

    local_roots = {"pafio": ROOT}
    contracts = [check_contract(local_roots, rule) for rule in LOCAL_CONTRACTS]
    missing_repositories: list[str] = []
    cross_status = "not_requested"

    if args.workspace_root is not None:
        workspace_root = args.workspace_root.resolve()
        repository_roots = {
            key: workspace_root / directory
            for key, directory in REPOSITORY_DIRS.items()
        }
        missing_repositories = [
            REPOSITORY_DIRS[key]
            for key, path in repository_roots.items()
            if not path.is_dir()
        ]
        if missing_repositories:
            cross_status = "failed_missing" if args.require_workspace else "skipped"
        else:
            cross_status = "checked"
            contracts.extend(
                check_contract(repository_roots, rule)
                for rule in CROSS_REPOSITORY_CONTRACTS
            )

    ok = all(contract["ok"] for contract in contracts)
    if cross_status == "failed_missing":
        ok = False
    payload = {
        "ok": ok,
        "cross_repository_status": cross_status,
        "missing_repositories": missing_repositories,
        "contracts": contracts,
    }
    if args.json:
        print(json.dumps(payload, sort_keys=True))
    else:
        print_human(payload)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
