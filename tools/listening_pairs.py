#!/usr/bin/env python3
"""Create level-matched, randomized pairs from legally supplied local recordings."""
import argparse
import json
from pathlib import Path
import secrets
import subprocess
import numpy as np
import soundfile as sf


def main():
    p = argparse.ArgumentParser()
    p.add_argument("input", type=Path)
    p.add_argument("--mid", type=float, default=35)
    p.add_argument("--high", type=float, default=50)
    p.add_argument(
        "--comparison",
        type=Path,
        help="Optional cached comparison WAV; otherwise compare with dry input",
    )
    p.add_argument("--output", type=Path, default=Path("measurements/raw/listening"))
    a = p.parse_args()
    if not 0 <= a.mid <= 100 or not 0 <= a.high <= 100:
        p.error("Control values must be 0–100")
    x, rate = sf.read(a.input, dtype="float32", always_2d=True)
    if x.shape[1] not in (1, 2) or not len(x) or not np.isfinite(x).all():
        p.error("Finite mono/stereo input required")
    # Unique ignored folders prevent replacing earlier tests or supplied recordings.
    folder = a.output / secrets.token_hex(6)
    folder.mkdir(parents=True, exist_ok=False)
    # Leading silence settles parameter initialization consistently.
    padded = np.vstack((np.zeros((round(rate * 0.5), x.shape[1]), np.float32), x))
    source = folder / "input.f32"
    padded.tofile(source)
    comparison = x
    if a.comparison:
        comparison, comparison_rate = sf.read(
            a.comparison, dtype="float32", always_2d=True
        )
        if (
            comparison_rate != rate
            or comparison.shape != x.shape
            or not np.isfinite(comparison).all()
        ):
            p.error(
                "Comparison must have the same rate, channels and frames and finite samples"
            )
    subprocess.run(
        [
            "build/topend-render",
            str(source),
            str(folder / "candidate.f32"),
            str(rate),
            str(x.shape[1]),
            "512",
            str(a.mid),
            str(a.high),
            "0",
        ],
        check=True,
    )
    candidate = np.fromfile(folder / "candidate.f32", np.float32).reshape(padded.shape)[
        round(rate * 0.5) :
    ]
    audio = [comparison, candidate]
    rms = [np.sqrt(np.mean(y.astype(float) ** 2)) for y in audio]
    if min(rms) <= 1e-12:
        p.error("Signal is too quiet for level matching")
    # Match RMS, then apply a shared reduction to preserve peak headroom.
    matched = [y * 0.1 / level for y, level in zip(audio, rms)]
    reduction = min(1.0, 0.98 / max(np.max(abs(y)) for y in matched))
    order = [0, 1] if secrets.randbits(1) else [1, 0]
    for label, index in zip(("A", "B"), order):
        sf.write(
            folder / f"{label}.wav", matched[index] * reduction, rate, subtype="FLOAT"
        )
    (folder / "answer.json").write_text(
        json.dumps(
            dict(
                A=["cached comparison" if a.comparison else "dry", "TopEnd"][order[0]],
                B=["cached comparison" if a.comparison else "dry", "TopEnd"][order[1]],
                mid=a.mid,
                high=a.high,
                rate=rate,
                method="whole-file RMS matched, shared peak reduction",
            ),
            indent=2,
        )
        + "\n"
    )
    print(folder)


if __name__ == "__main__":
    main()
