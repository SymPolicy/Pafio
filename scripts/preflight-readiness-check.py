#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
NATIVE_CHECK = ROOT / "scripts" / "native-check.sh"
EXTRACTABILITY_CHECK = ROOT / "scripts" / "extractability-check.sh"
SPIO = ROOT / "scripts" / "spio"
STYIO_INTERFACE_GATE = ROOT / "scripts" / "styio-interface-gate.py"
FIXTURE_MANIFEST = ROOT / "tests" / "unit" / "fixtures" / "manifests" / "ok-single-package" / "spio.toml"


def run_step(name: str, command: list[str], env: dict[str, str] | None = None) -> dict:
    proc = subprocess.run(
        command,
        cwd=str(ROOT.parent),
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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="preflight-readiness-check.py")
    parser.add_argument("--styio-bin", help="external styio binary used for compatibility probing")
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    args = parser.parse_args(argv)

    steps: list[dict] = []
    steps.append(run_step("native_check", [str(NATIVE_CHECK)]))
    steps.append(run_step("extractability_check", [str(EXTRACTABILITY_CHECK)]))

    if args.styio_bin:
        env = dict(os.environ)
        env["SPIO_STYIO_BIN"] = args.styio_bin
        steps.append(
            run_step(
                "compatibility_check",
                [
                    str(SPIO),
                    "--json",
                    "check",
                    "--manifest-path",
                    str(FIXTURE_MANIFEST),
                    "--styio-bin",
                    args.styio_bin,
                ],
                env=env,
            )
        )
        steps.append(
            run_step(
                "styio_interface_gate",
                [
                    sys.executable,
                    str(STYIO_INTERFACE_GATE),
                    "--styio-bin",
                    args.styio_bin,
                    "--json",
                ],
                env=env,
            )
        )

    ok = all(step["ok"] for step in steps)
    if args.json:
        sys.stdout.write(json.dumps({"ok": ok, "steps": steps}, sort_keys=True) + "\n")
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
        sys.stdout.write(
            f"preflight {'passed' if ok else 'failed'} for {ROOT}\n"
        )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
