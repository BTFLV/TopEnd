#!/usr/bin/env python3
"""Run native/plugin validation without launching or controlling any DAW."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--build", type=Path, default=Path("build-release"))
    p.add_argument(
        "--sdk-validator",
        type=Path,
        default=Path("build-validator-local/validator/validator"),
    )
    a = p.parse_args()
    out = a.build / "out"
    installed = Path.home() / "Library/Audio/Plug-Ins/Components/TopEnd.component"
    pluginval = "build/tools/pluginval.app/Contents/MacOS/pluginval"
    tasks = [
        ("dsp", ["ctest", "--test-dir", "build", "--output-on-failure"]),
        ("auval", ["auval", "-v", "aufx", "TpEn", "TpEA"]),
        ("vst3-sdk", [str(a.sdk_validator), str(out / "TopEnd.vst3")]),
        ("clap", ["build/tools/clap-validator", "validate", str(out / "TopEnd.clap")]),
        (
            "pluginval-vst3",
            [
                pluginval,
                "--strictness-level",
                "5",
                "--validate",
                str((out / "TopEnd.vst3").resolve()),
            ],
        ),
        (
            "pluginval-au",
            [pluginval, "--strictness-level", "5", "--validate", str(installed)],
        ),
    ]
    results = []
    for name, command in tasks:
        run = subprocess.run(command, capture_output=True, text=True, timeout=240)
        text = run.stdout + run.stderr
        Path("build", f"{name}-validation.log").write_text(text)
        clean = re.sub(r"\x1b\[[0-9;]*m", "", text)
        row = dict(test=name, exit_code=run.returncode, passed=run.returncode == 0)
        if name == "clap":
            found = re.search(
                r"(\d+) tests run, (\d+) passed, (\d+) failed, (\d+) warnings, (\d+) skipped",
                clean,
            )
            if found:
                row.update(
                    dict(
                        zip(
                            (
                                "run",
                                "passed_count",
                                "failed_count",
                                "warnings",
                                "skipped",
                            ),
                            map(int, found.groups()),
                        )
                    )
                )
        if name == "vst3-sdk":
            found = re.search(r"Result: (\d+) tests passed, (\d+) tests failed", clean)
            if found:
                row.update(passed_count=int(found[1]), failed_count=int(found[2]))
        results.append(row)
        print(name, "PASS" if row["passed"] else "FAIL", flush=True)
    Path("measurements/validation.json").write_text(
        json.dumps(results, indent=2) + "\n"
    )
    if not all(r["passed"] for r in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
