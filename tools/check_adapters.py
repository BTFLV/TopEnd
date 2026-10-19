#!/usr/bin/env python3
"""Compare AU and CLAP builds with the DSP core through headless harnesses.

The generated input starts with signal at the first frame, so initial-state
differences cannot hide in leading silence. The AU check also toggles the
host-level BypassEffect property and compares it with the core's own bypass.
Uses only TopEnd binaries; no other plugin or host is loaded.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

import numpy as np

RATE, BLOCK, MID, HIGH, TRIM = 48000, 127, 35, 50, -3


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, default=Path("build-release"))
    parser.add_argument("--native", type=Path, default=Path("build"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("measurements/adapter-equivalence.json"),
    )
    args = parser.parse_args()
    out = args.build / "out"
    with tempfile.TemporaryDirectory(prefix="topend-adapters-") as folder:
        work = Path(folder)
        t = np.arange(3 * RATE) / RATE
        rng = np.random.default_rng(5)
        left = 0.2 * np.sin(2 * np.pi * 7000 * t) * (0.2 + 0.8 * (t % 0.5 < 0.25))
        left += 0.05 * rng.standard_normal(len(t))
        right = 0.1 * np.sin(2 * np.pi * 11000 * t + 1)
        right += 0.08 * rng.standard_normal(len(t)) * np.exp(-20 * (t % 0.4))
        np.column_stack((left, right)).astype(np.float32).tofile(work / "input.f32")
        (work / "core-events.txt").write_text(f"{RATE} 4 1\n{2 * RATE} 4 0\n")
        # Reserved AU harness event ID 4294967295 sets kAudioUnitProperty_BypassEffect.
        (work / "au-events.txt").write_text(
            f"{RATE} 4294967295 1\n{2 * RATE} 4294967295 0\n"
        )
        common = [str(work / "input.f32")]
        settings = [str(RATE), "2", str(BLOCK)]
        render = [str(args.native / "topend-render")]
        au = [
            str(args.native / "topend-au-host"),
            "--bundle",
            str((out / "TopEnd.component").resolve()),
            "--render",
        ]
        au_values = [f"0={MID}", f"1={HIGH}", f"3={TRIM}"]
        values = [str(MID), str(HIGH), str(TRIM)]
        runs = {
            "core": render + common + [str(work / "core.f32")] + settings + values,
            "clap": [
                str(args.build / "topend-clap-host"),
                str((out / "TopEnd.clap/Contents/MacOS/TopEnd").resolve()),
            ]
            + common
            + [str(work / "clap.f32")]
            + settings
            + values,
            "au": au + common + [str(work / "au.f32")] + settings + au_values,
            "core-bypass": render
            + common
            + [str(work / "core-bypass.f32")]
            + settings
            + values
            + ["--events", str(work / "core-events.txt")],
            "au-host-bypass": au
            + common
            + [str(work / "au-host-bypass.f32")]
            + settings
            + au_values
            + ["--events", str(work / "au-events.txt")],
        }
        for command in runs.values():
            subprocess.run(command, check=True, capture_output=True)
        audio = {
            name: np.fromfile(work / f"{name}.f32", np.float32)
            .reshape(-1, 2)
            .astype(float)
            for name in list(runs) + ["input"]
        }

    def compare(name, reference):
        error = audio[name] - audio[reference]
        return dict(
            format=name,
            reference=reference,
            maximum_absolute_error=float(np.max(abs(error))),
            relative_residual=float(
                np.linalg.norm(error) / np.linalg.norm(audio[reference])
            ),
            sample_exact=bool(np.all(error == 0)),
        )

    bypassed = audio["au-host-bypass"][RATE : 2 * RATE]
    exact = np.all(bypassed == audio["input"][RATE : 2 * RATE], axis=1)
    report = dict(
        rate=RATE,
        channels=2,
        block=BLOCK,
        mid=MID,
        high=HIGH,
        trim_db=TRIM,
        input="3 s generated stereo, signal from the first frame",
        results=[
            compare("clap", "core"),
            compare("au", "core"),
            compare("au-host-bypass", "core-bypass"),
        ],
        au_host_bypass_exact_from_frame=int(np.argmax(exact)) if exact.any() else None,
        au_host_bypass_exact_until_unbypass=bool(exact[np.argmax(exact) :].all()),
    )
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
