#!/usr/bin/env python3
"""Render TopEnd's product graphs from the real DSP via build/topend-render."""

import json
from pathlib import Path
import subprocess
import tempfile

import matplotlib
import numpy as np

matplotlib.use("Agg")
from matplotlib import font_manager  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
RATE = 48000
OUT = ROOT / "assets/plots"
BACKGROUND, PANEL, GRID = "#0f1216", "#161b21", "#262e36"
INK, MUTED, AMBER, MINT = "#eef1ee", "#8a979c", "#f0b86e", "#6fe0c8"


def style():
    for weight in ("Regular", "Bold"):
        font_manager.fontManager.addfont(str(ROOT / f"assets/fonts/Lato-{weight}.ttf"))
    plt.rcParams.update(
        {
            "font.family": "Lato",
            "font.size": 10.5,
            "figure.facecolor": BACKGROUND,
            "axes.facecolor": PANEL,
            "axes.edgecolor": GRID,
            "axes.labelcolor": MUTED,
            "axes.titlecolor": INK,
            "axes.titleweight": "bold",
            "axes.titlesize": 12,
            "axes.titlelocation": "left",
            "axes.titlepad": 10,
            "axes.grid": True,
            "grid.color": GRID,
            "grid.linewidth": 0.8,
            "xtick.color": MUTED,
            "ytick.color": MUTED,
            "legend.frameon": False,
            "legend.labelcolor": INK,
            "legend.fontsize": 9.5,
            "savefig.facecolor": BACKGROUND,
        }
    )


def render(x, mid, high, trim=0.0):
    """Process mono float audio through the production DSP."""
    with tempfile.TemporaryDirectory(prefix="topend-graphs-") as folder:
        source, target = Path(folder) / "in.f32", Path(folder) / "out.f32"
        np.asarray(x, np.float32).tofile(source)
        subprocess.run(
            [
                str(ROOT / "build/topend-render"),
                str(source),
                str(target),
                str(RATE),
                "1",
                "256",
                str(mid),
                str(high),
                str(trim),
            ],
            check=True,
        )
        return np.fromfile(target, np.float32).astype(float)


def low_level_response(mid, high, n=16384):
    """Frequency response from a -60 dBFS impulse, well below every threshold."""
    x = np.zeros(n)
    x[0] = 1e-3
    y = render(x, mid, high)
    f = np.fft.rfftfreq(n, 1 / RATE)
    return f, 20 * np.log10(np.maximum(abs(np.fft.rfft(y)) / 1e-3, 1e-9))


def tone_gains(frequencies, levels, mid, high, seconds=0.5):
    """Steady-state gain of each tone, measured over its final 200 ms."""
    n = round(RATE * seconds)
    t = np.arange(n) / RATE
    pieces = [
        10 ** (level / 20) * np.sin(2 * np.pi * f * t)
        for level in levels
        for f in frequencies
    ]
    y = render(np.concatenate(pieces), mid, high)
    gains = []
    for k, piece in enumerate(pieces):
        tail = slice(k * n + n - RATE // 5, (k + 1) * n)
        rms_in = np.sqrt(np.mean(piece[-RATE // 5 :] ** 2))
        gains.append(20 * np.log10(np.sqrt(np.mean(y[tail] ** 2)) / rms_in))
    return np.array(gains).reshape(len(levels), len(frequencies))


def frequency_axis(ax):
    ax.set_xscale("log")
    ax.set_xlim(40, 20000)
    ax.set_xticks([100, 1000, 10000], ["100 Hz", "1 kHz", "10 kHz"])
    ax.set_xticks([50, 200, 500, 2000, 5000, 20000], minor=True)
    ax.set_xticklabels([], minor=True)
    ax.grid(True, which="minor", color=GRID, linewidth=0.5)
    ax.axhline(0, color=MUTED, linewidth=0.8, alpha=0.6)


def shade(color, amount):
    base = np.array(matplotlib.colors.to_rgb(color))
    return tuple(
        base * amount + np.array(matplotlib.colors.to_rgb(PANEL)) * (1 - amount)
    )


def controls_graph():
    fig, axes = plt.subplots(1, 2, figsize=(11, 4), sharey=True)
    for ax, band, color in zip(axes, ("Mid Air", "High Air"), (AMBER, MINT)):
        for value, amount in zip((10, 25, 50, 75, 100), (0.35, 0.5, 0.68, 0.84, 1.0)):
            f, db = low_level_response(
                value if band == "Mid Air" else 0, value if band == "High Air" else 0
            )
            ax.semilogx(
                f[1:],
                db[1:],
                color=shade(color, amount),
                linewidth=2,
                label=f"{value}%",
            )
        frequency_axis(ax)
        ax.set_title(band)
        ax.set_ylim(-10, 26)
        ax.legend(loc="upper left", ncol=5, handlelength=1.2, columnspacing=1)
    axes[0].set_ylabel("Gain (dB)")
    fig.suptitle(
        "What the controls do: response for quiet material",
        color=INK,
        fontweight="bold",
        x=0.01,
        ha="left",
        fontsize=13,
    )
    fig.tight_layout()
    fig.savefig(OUT / "controls.png", dpi=170)


def level_graph(rows):
    frequencies = np.geomspace(60, 20000, 72)
    levels = (-50, -20, -6)
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.9), sharey=True)
    settings = (
        (50, 0, "Mid Air 50%"),
        (0, 50, "High Air 50%"),
        (35, 50, "Mid 35% + High 50%"),
    )
    for ax, (mid, high, title) in zip(axes, settings):
        gains = tone_gains(frequencies, levels, mid, high)
        for level, gain, amount in zip(levels, gains, (1.0, 0.7, 0.45)):
            ax.semilogx(
                frequencies,
                gain,
                color=shade(MINT if high else AMBER, amount),
                linewidth=2.2,
                label=f"{level} dBFS",
            )
            rows += [
                dict(
                    mid=mid,
                    high=high,
                    frequency=float(f),
                    level=level,
                    gain_db=float(g),
                )
                for f, g in zip(frequencies, gain)
            ]
        frequency_axis(ax)
        ax.set_title(title)
        ax.set_ylim(-4, 19)
        ax.legend(loc="upper left", title="Tone level", title_fontsize=9)
        ax.get_legend().get_title().set_color(MUTED)
    axes[0].set_ylabel("Gain (dB)")
    fig.suptitle(
        "Louder material receives less lift",
        color=INK,
        fontweight="bold",
        x=0.01,
        ha="left",
        fontsize=13,
    )
    fig.tight_layout()
    fig.savefig(OUT / "level-response.png", dpi=170)


def transfer_graph():
    levels = np.arange(-72, 1, 3)
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for mid, high, f, label, color in (
        (50, 0, 5000, "Mid Air 50% at 5 kHz", AMBER),
        (0, 50, 12000, "High Air 50% at 12 kHz", MINT),
    ):
        gains = tone_gains([f], levels, mid, high)[:, 0]
        ax.plot(levels, gains, color=color, linewidth=2.4, label=label)
        ax.fill_between(levels, 0, gains, color=color, alpha=0.08)
    ax.set(xlabel="Tone level (dBFS peak)", ylabel="Added gain (dB)", xlim=(-72, 0))
    ax.set_title("Quiet detail is lifted most")
    ax.legend(loc="upper right")
    fig.tight_layout()
    fig.savefig(OUT / "dynamics-curve.png", dpi=170)


def timing_graph():
    t = np.arange(RATE * 3) / RATE
    # 5 ms raised-cosine transitions, as in real material, avoid filter ringing.
    ramp = lambda edge: 0.5 - 0.5 * np.cos(
        np.pi * np.clip((t - edge) / 0.005, 0, 1)
    )  # noqa: E731
    level = 0.01 + (0.4 - 0.01) * (ramp(0.8) - ramp(1.6))
    hop = RATE // 1000
    fig, ax = plt.subplots(figsize=(11, 3.8))
    ax.axvspan(0.8, 1.6, color=INK, alpha=0.05)
    ax.text(
        1.2,
        0.97,
        "−8 dBFS",
        transform=ax.get_xaxis_transform(),
        ha="center",
        va="top",
        color=MUTED,
    )
    ax.text(
        0.55,
        0.97,
        "−40 dBFS",
        transform=ax.get_xaxis_transform(),
        ha="center",
        va="top",
        color=MUTED,
    )
    for mid, high, f, label, color in (
        (50, 0, 5000, "Mid Air 50% · 5 kHz", AMBER),
        (0, 50, 12000, "High Air 50% · 12 kHz", MINT),
    ):
        x = level * np.sin(2 * np.pi * f * t)
        y = render(x, mid, high)
        n = len(t) // hop * hop
        rms = lambda a: np.sqrt(
            np.mean(a[:n].reshape(-1, hop) ** 2, axis=1)
        )  # noqa: E731
        gain = 20 * np.log10(rms(y) / rms(x))
        ax.plot(
            np.arange(len(gain)) / 1000, gain, color=color, linewidth=2, label=label
        )
    ax.set(xlabel="Seconds", ylabel="Added gain (dB)", xlim=(0.3, 3), ylim=(-1, 17))
    ax.set_title("Fast reduction, smooth recovery")
    ax.legend(loc="lower right")
    fig.tight_layout()
    fig.savefig(OUT / "timing.png", dpi=170)


def main():
    style()
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    controls_graph()
    level_graph(rows)
    transfer_graph()
    timing_graph()
    (ROOT / "measurements/topend-character.json").write_text(
        json.dumps(rows, indent=1) + "\n"
    )


if __name__ == "__main__":
    main()
