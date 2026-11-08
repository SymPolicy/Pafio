#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_PAFIO = ROOT / "scripts" / "pafio"
DEFAULT_FIXTURE_MANIFEST = ROOT / "tests" / "unit" / "fixtures" / "manifests" / "ok-single-package" / "pafio.toml"
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")
EDITION_RE = re.compile(r"^\d+$")


def run_step(name: str, command: list[str], *, env: dict[str, str] | None = None) -> dict:
    try:
        proc = subprocess.run(
            command,
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            env=env,
        )
        return {
            "name": name,
            "command": command,
            "returncode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "ok": proc.returncode == 0,
        }
    except OSError as err:
        return {
            "name": name,
            "command": command,
            "returncode": 127,
            "stdout": "",
            "stderr": str(err),
            "ok": False,
        }


def load_json(stdout_text: str, context: str) -> dict:
    try:
        payload = json.loads(stdout_text)
    except json.JSONDecodeError as err:
        raise RuntimeError(f"{context} did not emit valid JSON") from err
    if not isinstance(payload, dict):
        raise RuntimeError(f"{context} must emit a top-level JSON object")
    return payload


def load_json_file(path: pathlib.Path, context: str) -> dict:
    if not path.exists():
        raise RuntimeError(f"{context} is missing: {path}")
    if not path.is_file():
        raise RuntimeError(f"{context} is not a file: {path}")
    return load_json(path.read_text(encoding="utf-8"), context)


def validate_machine_info(payload: dict, *, require_compile_plan: bool) -> list[str]:
    errors: list[str] = []

    tool = payload.get("tool")
    if tool != "styio":
        errors.append("machine-info field 'tool' must equal 'styio'")

    compiler_version = payload.get("compiler_version")
    if not isinstance(compiler_version, str) or not SEMVER_RE.match(compiler_version):
        errors.append("machine-info field 'compiler_version' must be strict semver x.y.z")

    channel = payload.get("channel")
    if not isinstance(channel, str) or not channel:
        errors.append("machine-info field 'channel' must be a non-empty string")

    integration_phase = payload.get("active_integration_phase")
    if not isinstance(integration_phase, str) or not integration_phase:
        errors.append("machine-info field 'active_integration_phase' must be a non-empty string")

    supported_contracts = payload.get("supported_contracts")
    if not isinstance(supported_contracts, dict):
        errors.append("machine-info field 'supported_contracts' must be an object")
    else:
        compile_plan = supported_contracts.get("compile_plan")
        if not isinstance(compile_plan, list) or not all(isinstance(item, int) for item in compile_plan):
            errors.append("machine-info field 'supported_contracts.compile_plan' must be an array of integers")
        elif require_compile_plan and 1 not in compile_plan:
            errors.append("machine-info must advertise compile-plan v1 support when --require-compile-plan is set")

    supported_contract_versions = payload.get("supported_contract_versions")
    if not isinstance(supported_contract_versions, dict):
        errors.append("machine-info field 'supported_contract_versions' must be an object")
    elif supported_contract_versions.get("machine_info") != [1]:
        errors.append("machine-info must advertise supported_contract_versions.machine_info = [1]")

    supported_adapter_modes = payload.get("supported_adapter_modes")
    if not isinstance(supported_adapter_modes, list) or not all(isinstance(item, str) for item in supported_adapter_modes):
        errors.append("machine-info field 'supported_adapter_modes' must be an array of strings")
    elif "cli" not in supported_adapter_modes:
        errors.append("machine-info must advertise 'cli' in supported_adapter_modes")

    feature_flags = payload.get("feature_flags")
    if not isinstance(feature_flags, dict):
        errors.append("machine-info field 'feature_flags' must be an object")
    elif feature_flags.get("jsonl_diagnostics") is not True:
        errors.append("machine-info feature_flags must declare jsonl_diagnostics = true")

    capabilities = payload.get("capabilities")
    if not isinstance(capabilities, list) or not all(isinstance(item, str) for item in capabilities):
        errors.append("machine-info field 'capabilities' must be an array of strings")
    else:
        for required in ("machine_info_json", "jsonl_diagnostics"):
            if required not in capabilities:
                errors.append(f"machine-info capabilities are missing required baseline capability '{required}'")

    edition_max = payload.get("edition_max")
    if not isinstance(edition_max, str) or not EDITION_RE.match(edition_max):
        errors.append("machine-info field 'edition_max' must be a numeric string")

    return errors


def write_temp_project(root: pathlib.Path) -> pathlib.Path:
    manifest_path = root / "pafio.toml"
    source_path = root / "src" / "main.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/interop\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = false\n\n"
        "[build]\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"interop\"\n"
        "path = \"src/main.styio\"\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"interop\")\n", encoding="utf-8")
    return manifest_path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def validate_outputs(plan_path: pathlib.Path, intent: str, machine_info: dict) -> dict:
    """Test-side contract assertions; production Pafio keeps receipts opaque."""
    plan = load_json_file(plan_path, "compile plan")
    outputs = plan["outputs"]
    require(plan["intent"] == intent, "compile plan intent differs from command")
    receipt = load_json_file(pathlib.Path(outputs["build_root"]) / "receipt.json", "receipt")
    require(receipt["intent"] == intent, "receipt intent differs from current command")
    require(all(receipt["outputs"].get(key) == value for key, value in outputs.items()),
            "receipt belongs to different invocation outputs")
    require(receipt["compiler_version"] == machine_info["compiler_version"], "receipt compiler version mismatch")
    require(receipt["channel"] == machine_info["channel"], "receipt compiler channel mismatch")
    require(bool(receipt["artifacts"]), "successful compilation has no artifacts")
    for artifact in receipt["artifacts"]:
        path = pathlib.Path(artifact)
        require(path.is_file() and path.stat().st_size > 0 and path.is_relative_to(outputs["artifact_dir"]),
                "artifact is empty, missing or outside current invocation")
    require((pathlib.Path(outputs["diag_dir"]) / "diagnostics.jsonl").is_file(), "current diagnostics missing")
    return receipt


def task_resource_workflow(work: pathlib.Path, pafio: str, styio: str,
                           machine_info: dict, steps: list[dict], *,
                           observation_only: bool = False) -> None:
    fixtures = ROOT / "tests/integration/fixtures/task-resource"
    project = work / "project"
    source = project / "src/main.styio"
    source.parent.mkdir(parents=True, exist_ok=True)
    manifest = project / "pafio.toml"
    shutil.copyfile(fixtures / "pafio.toml", manifest)
    # Explicit compiler selection must win even over an unusable environment value.
    env = {**os.environ, "PAFIO_HOME": str(work / "home"),
           "PAFIO_STYIO_BIN": str(work / "not-the-selected-compiler")}
    history = "t13_task_history_capture"
    saved: dict[pathlib.Path, bytes] = {}
    cache_keys: dict[str, str] = {}
    sessions: set[str] = set()

    def select(name: str) -> str:
        shutil.copyfile(fixtures / (name + ".styio"), source)
        return (fixtures / (name + ".expected")).read_text(encoding="utf-8")

    def command(intent: str, flags: list[str], *, as_json: bool = True) -> list[str]:
        return [pafio, *(["--json"] if as_json else []), intent,
                "--manifest-path", str(manifest), "--styio-bin", styio, *flags]

    def unchanged() -> None:
        for path, content in saved.items():
            require(path.is_file() and path.read_bytes() == content,
                    "a later invocation modified an earlier result: " + str(path))

    def remember(plan_path: pathlib.Path) -> None:
        for path in plan_path.parent.rglob("*"):
            if path.is_file():
                saved[path] = path.read_bytes()

    def execute(name: str, intent: str, expected: str = "", *, failure: bool = False,
                flags: list[str] | None = None) -> tuple[pathlib.Path, dict]:
        before = set(project.glob(".pafio/build/**/plan.json"))
        as_json = failure or intent not in ("run", "test")
        step = run_step(name, command(intent, flags or [], as_json=as_json), env=env)
        steps.append(step)
        step["expected_returncode"] = 23 if failure else 0
        step["ok"] = step["returncode"] == step["expected_returncode"]
        require(step["ok"], name + ": unexpected process exit")
        added = set(project.glob(".pafio/build/**/plan.json")) - before
        require(len(added) == 1, name + ": must own one new plan, including on failure")
        plan_path = added.pop()
        plan = load_json_file(plan_path, name + " plan")
        outputs = plan["outputs"]
        diagnostics = pathlib.Path(outputs["diag_dir"]) / "diagnostics.jsonl"
        require(diagnostics.is_file(), name + ": current diagnostics missing")
        records = [json.loads(line) for line in diagnostics.read_text().splitlines() if line]
        if failure:
            error = load_json(step["stderr"], name + " error")
            require(error["category"] == "CompilerError" and error["code"] == 23, name + ": failure envelope")
            require(expected.strip() in error["message"], name + ": expected error missing")
            require(any(expected.strip() in item["message"] for item in records), name + ": expected diagnostic missing")
            require(not (plan_path.parent / "receipt.json").exists(), name + ": failed invocation has a receipt")
            # Runtime failure may retain this invocation's compiled IR. A
            # compile error must not expose a successful compile's artifacts.
            if name.startswith("compile_error_"):
                require(not list(pathlib.Path(outputs["artifact_dir"]).iterdir()), name + ": compile error exposes artifacts")
        else:
            receipt = validate_outputs(plan_path, intent, machine_info)
            require(receipt["session_id"] not in sessions, name + ": reused compiler session")
            sessions.add(receipt["session_id"])
            require(receipt["executed"] == (intent in ("run", "test")), name + ": execution intent mismatch")
            require(not any(item["severity"] == "error" for item in records), name + ": stale error diagnostic")
            if as_json:
                payload = load_json(step["stdout"], name + " envelope")
                require(payload["status"] == "succeeded" and payload["styio"]["status"] == "succeeded", name + ": status mismatch")
                require(payload["plan"]["path"] == str(plan_path), name + ": plan path mismatch")
                compiler = payload["styio"]
                require(pathlib.Path(compiler["binary"]) == pathlib.Path(styio), name + ": explicit compiler ignored")
                require(compiler["compiler_version"] == machine_info["compiler_version"] and
                        compiler["compiler_channel"] == machine_info["channel"] and
                        set(compiler["capabilities"]) == set(machine_info["capabilities"]) and
                        1 in compiler["supported_compile_plan_versions"], name + ": compiler identity/capabilities mismatch")
            else:
                require(step["stdout"] == expected + f"completed Styio {intent} via compile-plan\n", name + ": task result mismatch")
        unchanged()
        remember(plan_path)
        return plan_path, plan

    if not observation_only:
        for phase, fixture in (("success", history), ("compile_error", "e06_task_order_non_task"), ("repaired", history)):
            expected = select(fixture)
            for intent, flag in (("check", "--offline"), ("build", "--locked"), ("test", "--frozen"), ("run", "--offline")):
                path, _ = execute(phase + "_" + intent, intent, expected,
                                  failure=phase == "compile_error", flags=[flag])
                if phase == "success":
                    cache_keys[intent] = path.parent.parent.name
                elif phase == "repaired":
                    require(cache_keys[intent] == path.parent.parent.name, "repair changed cache identity for identical inputs")

        lock = (project / "pafio.lock").read_bytes()
        for fixture in ("t22_task_capture_failure_cleanup", "t23_task_f64_history_precision", "e06_task_capture_failure_no_fallback"):
            expected = select(fixture)
            for intent in ("run", "test"):
                execute(fixture + "_" + intent, intent, expected,
                        failure=fixture.startswith("e06"), flags=["--frozen"])
        require((project / "pafio.lock").read_bytes() == lock, "locked/frozen invocations changed the lock")

        select(history)
        step = run_step("dry_run_preserves_results", command("build", ["--dry-run", "--frozen"]), env=env)
        steps.append(step)
        require(step["ok"], "dry-run failed")
        payload = load_json(step["stdout"], "dry-run")
        require(payload["status"] == "planned" and payload["styio"]["status"] == "not_run", "dry-run executed compiler")
        require(not (pathlib.Path(payload["plan"]["build_root"]) / "receipt.json").exists(), "dry-run adopted prior success")
        unchanged()

        # Real overlapping Pafio invocations, without wrappers or fake executors.
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(run_step, f"concurrent_build_{i}", command("build", ["--frozen"]), env=env) for i in range(2)]
            results = [future.result() for future in futures]
        steps.extend(results)
        roots = []
        for step in results:
            require(step["ok"], step["name"] + ": failed")
            payload = load_json(step["stdout"], step["name"])
            require(payload["plan"]["cache_key"] == cache_keys["build"], "concurrent build changed cache identity")
            path = pathlib.Path(payload["plan"]["path"])
            receipt = validate_outputs(path, "build", machine_info)
            require(receipt["session_id"] not in sessions, "concurrent invocation consumed another receipt")
            sessions.add(receipt["session_id"])
            roots.append(path.parent)
            remember(path)
        require(roots[0] != roots[1], "concurrent commands share output ownership")
        unchanged()

    # Exercise the existing opt-in transport contract through real Pafio too.
    # This keyword-only selection permits affected-case verification without
    # replaying the ordinary workflow; every registered gate uses the full path.
    # The ordinary phase establishes the lockfile first; an observation-only
    # selection on a fresh project must do the same before any --frozen call.
    expected = select(history)
    if not (project / "pafio.lock").is_file():
        step = run_step("observation_lock_sync",
                        [pafio, "sync", "--manifest-path", str(manifest), "--offline"],
                        env=env)
        steps.append(step)
        require(step["ok"], "observation-only selection could not establish the lockfile")
    snapshot_flags = ["--emit-observable-static-snapshot", "--frozen"]
    runtime_flags = ["--emit-runtime-observation", "--runtime-observation-mode", "detailed", "--frozen"]
    require(1 in machine_info["observable_static_snapshot"]["schema_versions"],
            "selected compiler does not advertise snapshot v1")
    require("snapshot-delta" in machine_info["observable_static_snapshot"]["optional_capabilities"],
            "selected compiler does not advertise snapshot delta")

    def artifact(plan_path: pathlib.Path, suffix: str) -> pathlib.Path:
        receipt = load_json_file(plan_path.parent / "receipt.json", "observation receipt")
        matches = [pathlib.Path(path) for path in receipt["artifacts"] if path.endswith(suffix)]
        require(len(matches) == 1, "expected exactly one current " + suffix + " artifact")
        return matches[0]

    first_path, _ = execute("snapshot_initial", "check", flags=snapshot_flags)
    parent = artifact(first_path, ".observable-static-snapshot.json")
    snapshot = load_json_file(parent, "snapshot")
    require(snapshot["contract"] == "styio.observable.static-snapshot" and snapshot["schema_version"] == 1,
            "snapshot contract mismatch")
    require(snapshot["compilation_unit"] == {"package_name": "acme/task-resource",
            "manifest_path": "pafio.toml", "entry_path": "src/main.styio"},
            "snapshot lost canonical package identity")
    first_receipt = load_json_file(first_path.parent / "receipt.json", "initial snapshot receipt")
    require("observable_static_snapshot" not in first_receipt, "absent parent produced a delta result")

    second_path, second_plan = execute("snapshot_parent", "check", flags=[*snapshot_flags,
        "--observable-parent-snapshot", str(parent.relative_to(project))])
    require(second_plan["emit"]["observable_static_snapshot"]["parent_snapshot_path"] == str(parent),
            "relative parent did not resolve to the prior artifact")
    require(first_path.parent.parent == second_path.parent.parent,
            "parent transport changed cache identity")
    require(first_path.parent != second_path.parent, "parent invocation reused output ownership")
    child = artifact(second_path, ".observable-static-snapshot.json")
    require(parent.read_bytes() == child.read_bytes(), "identical source changed snapshot identity")
    delta = load_json_file(artifact(second_path, ".observable-delta.json"), "delta")
    second_receipt = load_json_file(second_path.parent / "receipt.json", "parent receipt")
    require(second_receipt["observable_static_snapshot"] == {
        "delta": "published", "parent_snapshot_id": delta["parent_snapshot_id"],
        "target_snapshot_id": delta["target_snapshot_id"]}, "current delta result missing from receipt")
    require(delta["parent_snapshot_id"] == delta["target_snapshot_id"] and not delta["operations"],
            "identical retained parent did not produce an empty delta")

    missing = project / "absent-parent.observable-static-snapshot.json"
    require(not missing.exists(), "missing-parent fixture unexpectedly exists")
    missing_path, _ = execute("snapshot_missing_parent", "check", flags=[*snapshot_flags,
        "--observable-parent-snapshot", str(missing)])
    missing_receipt = load_json_file(missing_path.parent / "receipt.json", "missing-parent receipt")
    require(missing_receipt["observable_static_snapshot"] == {
        "delta": "full_snapshot_required", "reason": "parent_unreadable"},
        "unreadable parent did not request a full snapshot")
    require(artifact(missing_path, ".observable-static-snapshot.json").read_bytes() == parent.read_bytes(),
            "parent degradation changed the published snapshot")
    require(not any(path.endswith(".observable-delta.json") for path in missing_receipt["artifacts"]),
            "parent degradation reused a delta")

    execution_ids: set[str] = set()

    def runtime_events(plan_path: pathlib.Path) -> None:
        receipt = load_json_file(plan_path.parent / "receipt.json", "runtime receipt")
        event_path = pathlib.Path(receipt["outputs"]["runtime_events_path"])
        require(event_path.is_file() and event_path.parent == plan_path.parent,
                "runtime events are not owned by the current invocation")
        records = [json.loads(line) for line in event_path.read_text().splitlines() if line]
        require(len(records) >= 2 and records[0]["record_kind"] == "session.capability" and
                records[-1]["record_kind"] == "session.summary", "runtime session framing missing")
        require(sum(record["record_kind"] == "session.capability" for record in records) == 1 and
                sum(record["record_kind"] == "session.summary" for record in records) == 1,
                "runtime artifact contains more than one invocation")
        capability, summary = records[0], records[-1]
        require(capability["schema_version"] == 2 and capability["mode"] == "detailed" and
                "task-lifecycle" in capability["active_capabilities"], "runtime negotiation ignored")
        require(capability["execution_id"] == summary["execution_id"] and
                capability["execution_id"] not in execution_ids, "stale runtime session")
        execution_ids.add(capability["execution_id"])
        kinds = {record["event_kind"] for record in records}
        require({"task.created", "task.completed", "task.result_consumed"}.issubset(kinds),
                "real task settlement missing from runtime observation")
        require("task.failed" not in kinds and not summary["exporter_failed"],
                "successful task observation reports failure")

    runtime_path, runtime_plan = execute("runtime_only", "run", expected, flags=runtime_flags)
    require("observable_static_snapshot" not in runtime_plan["emit"], "runtime-only request gained snapshot emission")
    runtime_events(runtime_path)
    combined_flags = [*runtime_flags, "--emit-observable-static-snapshot"]
    for phase, fixture in (("success", history), ("compile_error", "e06_task_order_non_task"), ("repaired", history)):
        expected = select(fixture)
        path, _ = execute(phase + "_observed_run", "run", expected,
                          failure=phase == "compile_error", flags=combined_flags)
        if phase != "compile_error":
            artifact(path, ".observable-static-snapshot.json")
            runtime_events(path)
    unchanged()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="styio-interface-gate.py")
    parser.add_argument("--styio-bin", required=True, help="published styio binary to validate")
    parser.add_argument("--pafio-bin", default=str(DEFAULT_PAFIO), help="pafio wrapper used for black-box checks")
    parser.add_argument("--manifest-path", help="manifest used for the pafio compatibility check")
    parser.add_argument("--require-compile-plan", action="store_true", help="also validate Pafio compile-plan execution")
    parser.add_argument("--task-resource", action="store_true", help="exercise the real task/resource workflow (requires a real Styio)")
    parser.add_argument("--work-dir", type=pathlib.Path, help="retain the reusable fixture project and evidence under this directory")
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    args = parser.parse_args(argv)

    styio_bin = str(pathlib.Path(args.styio_bin).resolve())
    pafio_bin = str(pathlib.Path(args.pafio_bin).resolve())
    check_manifest_path = pathlib.Path(args.manifest_path).resolve() if args.manifest_path else DEFAULT_FIXTURE_MANIFEST

    steps: list[dict] = []
    validation_errors: list[str] = []
    temp_root: pathlib.Path | None = None
    if args.work_dir:
        args.work_dir.mkdir(parents=True, exist_ok=True)
        work_root = args.work_dir.resolve()
    else:
        temp_root = pathlib.Path(tempfile.mkdtemp(prefix="pafio-styio-interface-gate-"))
        work_root = temp_root
    gate_env = {**os.environ, "PAFIO_HOME": str(work_root / "home")}

    machine_info_step = run_step("machine_info", [styio_bin, "--machine-info=json"])
    steps.append(machine_info_step)

    machine_info_payload: dict | None = None
    if machine_info_step["ok"]:
        try:
            machine_info_payload = load_json(machine_info_step["stdout"], "styio --machine-info=json")
            validation_errors.extend(
                validate_machine_info(machine_info_payload, require_compile_plan=args.require_compile_plan or args.task_resource)
            )
        except RuntimeError as err:
            validation_errors.append(str(err))

    compatibility_step = run_step(
        "pafio_doctor",
        [
            pafio_bin,
            "--json",
            "doctor",
            "--manifest-path",
            str(check_manifest_path),
            "--styio-bin",
            styio_bin,
        ],
        env=gate_env,
    )
    steps.append(compatibility_step)

    if args.require_compile_plan or args.task_resource:
        try:
            if args.task_resource:
                task_resource_workflow(work_root, pafio_bin, styio_bin, machine_info_payload, steps)
            else:
                manifest = write_temp_project(work_root / "project")
                step = run_step("pafio_build_execute", [pafio_bin, "--json", "build",
                    "--manifest-path", str(manifest), "--styio-bin", styio_bin],
                    env=gate_env)
                steps.append(step)
                require(step["ok"], "real Pafio build failed")
                payload = load_json(step["stdout"], "pafio build")
                validate_outputs(pathlib.Path(payload["plan"]["path"]), "build", machine_info_payload)
        except (RuntimeError, KeyError, TypeError, OSError, ValueError) as err:
            validation_errors.append(str(err))

    ok = all(step["ok"] for step in steps) and not validation_errors

    summary = {
        "ok": ok,
        "styio_bin": styio_bin,
        "pafio_bin": pafio_bin,
        "require_compile_plan": args.require_compile_plan,
        "task_resource": args.task_resource,
        "machine_info": machine_info_payload,
        "validation_errors": validation_errors,
        "steps": steps,
    }

    if args.json:
        sys.stdout.write(json.dumps(summary, sort_keys=True) + "\n")
    else:
        for step in steps:
            status = "OK" if step["ok"] else "FAIL"
            sys.stdout.write(f"[{status}] {step['name']}\n")
            if step["stdout"].strip():
                sys.stdout.write(step["stdout"])
                if not step["stdout"].endswith("\n"):
                    sys.stdout.write("\n")
            if step["stderr"].strip():
                sys.stderr.write(step["stderr"])
                if not step["stderr"].endswith("\n"):
                    sys.stderr.write("\n")
        for error in validation_errors:
            sys.stderr.write(f"[FAIL] {error}\n")
        sys.stdout.write(f"styio interface gate {'passed' if ok else 'failed'}\n")

    if temp_root is not None:
        shutil.rmtree(temp_root, ignore_errors=True)

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
