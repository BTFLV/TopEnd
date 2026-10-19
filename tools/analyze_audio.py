#!/usr/bin/env python3
"""General floating-point audio integrity and spectrum analysis."""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy import signal
import soundfile as sf


def load_audio(path, rate=None, channels=2):
    path = Path(path)
    if path.suffix == ".f32":
        if not rate:
            raise ValueError("Raw float input requires --rate")
        x = np.fromfile(path, np.float32)
        if x.size % channels:
            raise ValueError("Incomplete raw frame")
        return x.reshape(-1, channels).astype(float), rate
    x, r = sf.read(path, dtype="float64", always_2d=True)
    return x, r


def integrity(x, rate):
    if len(x) == 0 or not np.all(np.isfinite(x)):
        raise ValueError("Empty/non-finite audio")
    return dict(
        rate=rate,
        channels=x.shape[1],
        frames=len(x),
        peak=float(np.max(abs(x))),
        rms=np.sqrt(np.mean(x * x, axis=0)).tolist(),
        dc=np.mean(x, axis=0).tolist(),
        over_zero_dbfs=int(np.count_nonzero(abs(x) > 1)),
    )


def main():
    p = argparse.ArgumentParser()
    p.add_argument("input")
    p.add_argument("--rate", type=int)
    p.add_argument("--channels", type=int, default=2)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    x, rate = load_audio(args.input, args.rate, args.channels)
    result = integrity(x, rate)
    f, power = signal.welch(x, fs=rate, nperseg=min(8192, len(x)), axis=0)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        plt.semilogx(f[1:], 10 * np.log10(np.maximum(power[1:], 1e-30)))
        plt.xlabel("Hz")
        plt.ylabel("Power spectral density (dB/Hz)")
        plt.grid(alpha=0.2)
        plt.tight_layout()
        plt.savefig(args.output.with_suffix(".png"), dpi=170)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
