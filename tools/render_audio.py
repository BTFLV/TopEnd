#!/usr/bin/env python3
"""Render a mono/stereo audio file through the framework-independent core."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import numpy as np
import soundfile as sf


def main():
    p = argparse.ArgumentParser()
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--renderer", default="build/topend-render")
    p.add_argument("--mid", type=float, default=0)
    p.add_argument("--high", type=float, default=0)
    p.add_argument("--trim", type=float, default=0)
    p.add_argument("--block", type=int, default=512)
    p.add_argument("--bypass", action="store_true")
    a = p.parse_args()
    if a.input.resolve() == a.output.resolve():
        p.error("Input and output must be different files")
    if a.output.exists():
        p.error("Output already exists; choose a new path")
    audio, rate = sf.read(a.input, dtype="float32", always_2d=True)
    if audio.shape[1] not in (1, 2) or not len(audio) or not np.isfinite(audio).all():
        p.error("Input must contain finite mono or stereo audio")
    with tempfile.TemporaryDirectory(prefix="topend-render-") as folder:
        source, output = Path(folder) / "input.f32", Path(folder) / "output.f32"
        audio.tofile(source)
        command = [
            a.renderer,
            str(source),
            str(output),
            str(rate),
            str(audio.shape[1]),
            str(a.block),
            str(a.mid),
            str(a.high),
            str(a.trim),
        ]
        if a.bypass:
            command.append("--bypass")
        subprocess.run(command, check=True)
        result = np.fromfile(output, np.float32).reshape(audio.shape)
        a.output.parent.mkdir(parents=True, exist_ok=True)
        # Floating-point WAV preserves peaks above 0 dBFS; no implicit limiter.
        sf.write(a.output, result, rate, format="WAV", subtype="FLOAT")


if __name__ == "__main__":
    main()
