#!/usr/bin/env python3
"""Render the mock-signal Markdown results as a plotted PNG and PDF report."""

import argparse
import re
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm
import numpy as np


def read_results(path):
    text = Path(path).read_text(encoding="utf-8")
    rows = {}
    for mode in ("Wideband", "Split-band", "Smart"):
        match = re.search(
            rf"\| {re.escape(mode)} \| ([\d.]+) \| ([\d.]+)% \| ([\d.]+)% \| ([\d.]+)% \|",
            text,
        )
        if not match:
            raise ValueError(f"Could not find {mode} data in {path}")
        rows[mode] = tuple(float(value) for value in match.groups())
    detector = re.search(r"in-band tone is ([\d.]+) dB", text)
    if not detector:
        raise ValueError(f"Could not find detector selectivity in {path}")
    qrows = re.findall(r"\| ([\d.]+) \| ([\d.]+)% \| ([\d.]+)% \|", text[text.find("### Smart attenuator-Q"):])
    if len(qrows) < 2:
        raise ValueError("Could not find Smart attenuator-Q sweep")
    return rows, float(detector.group(1)), np.array(qrows, dtype=float)


def response_db(b, a, frequencies, sample_rate=48000.0):
    omega = 2.0 * np.pi * frequencies / sample_rate
    z1 = np.exp(-1j * omega)
    z2 = z1 * z1
    response = (b[0] + b[1] * z1 + b[2] * z2) / (1.0 + a[0] * z1 + a[1] * z2)
    return 20.0 * np.log10(np.maximum(np.abs(response), 1.0e-8))


def detector_filter(center=7800.0, q=2.2, sample_rate=48000.0):
    omega = 2.0 * np.pi * center / sample_rate
    alpha = np.sin(omega) / (2.0 * q)
    a0 = 1.0 + alpha
    return ([alpha / a0, 0.0, -alpha / a0], [-2.0 * np.cos(omega) / a0, (1.0 - alpha) / a0])


def bell_filter(center=7200.0, q=2.8, depth=12.0, sample_rate=48000.0):
    omega = 2.0 * np.pi * center / sample_rate
    sine, cosine = np.sin(omega), np.cos(omega)
    alpha = sine / (2.0 * q)
    gain = 10.0 ** (-depth / 40.0)
    a0 = 1.0 + alpha / gain
    return (
        [(1.0 + alpha * gain) / a0, -2.0 * cosine / a0, (1.0 - alpha * gain) / a0],
        [-2.0 * cosine / a0, (1.0 - alpha / gain) / a0],
    )


def shelf_filter(center=7200.0, q=2.8, depth=12.0, sample_rate=48000.0):
    omega = 2.0 * np.pi * center / sample_rate
    sine, cosine = np.sin(omega), np.cos(omega)
    gain = 10.0 ** (-depth / 40.0)
    slope = np.clip(1.0 / q, 0.25, 2.0)
    radicand = max(0.0, (gain + 1.0 / gain) * (1.0 / slope - 1.0) + 2.0)
    alpha = sine * 0.5 * np.sqrt(radicand)
    beta = 2.0 * np.sqrt(gain) * alpha
    b = [
        gain * ((gain + 1.0) + (gain - 1.0) * cosine + beta),
        -2.0 * gain * ((gain - 1.0) + (gain + 1.0) * cosine),
        gain * ((gain + 1.0) + (gain - 1.0) * cosine - beta),
    ]
    a0 = (gain + 1.0) - (gain - 1.0) * cosine + beta
    a = [2.0 * ((gain - 1.0) - (gain + 1.0) * cosine) / a0,
         ((gain + 1.0) - (gain - 1.0) * cosine - beta) / a0]
    return ([value / a0 for value in b], a)


def render(source, png_path, pdf_path):
    results, rejection_db, qresults = read_results(source)
    spectrum_path = Path(source).with_name("smart-spectral-spectrum.csv")
    spectrum = np.genfromtxt(spectrum_path, delimiter=",", names=True)
    rolling_path = Path(source).with_name("smart-rolling-spectrogram.csv")
    rolling = np.genfromtxt(rolling_path, delimiter=",", names=True)
    rolling = rolling[np.isfinite(rolling["time_s"]) & np.isfinite(rolling["frequency_hz"])
                      & np.isfinite(rolling["output_dbfs"])]
    measured = (spectrum["frequency_hz"] >= 2000.0) & (spectrum["frequency_hz"] <= 16000.0)
    if not np.any(measured):
        raise ValueError(f"No 2–16 kHz Smart spectrum samples in {spectrum_path}")
    modes = ("Wideband", "Split-band", "Smart")
    colors = {"Wideband": "#54c2d8", "Split-band": "#bd91ed", "Smart": "#e7ad5b"}
    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "text.color": "#e7edf2",
        "axes.labelcolor": "#c6d0d8",
        "xtick.color": "#aebbc5",
        "ytick.color": "#aebbc5",
    })
    fig = plt.figure(figsize=(13, 15), facecolor="#182128")
    grid = fig.add_gridspec(4, 2, height_ratios=[1, 1, 0.9, 1.45], left=0.09, right=0.90,
                            top=0.83, bottom=0.17, hspace=0.48, wspace=0.28)
    axes = np.array([[fig.add_subplot(grid[0, 0]), fig.add_subplot(grid[0, 1])],
                     [fig.add_subplot(grid[1, 0]), fig.add_subplot(grid[1, 1])]])
    fig.suptitle("SHUN DE-ESS  |  DETECTOR & ATTENUATOR TEST", color="#f2f5f7",
                 fontsize=19, fontweight="bold", x=0.08, y=0.955, ha="left")
    fig.text(0.08, 0.905,
             "Deterministic 48 kHz mocks: fixed detail for Q sweep; swept tone and bursts for rolling analysis",
             color="#aebbc5", fontsize=10)
    fig.text(0.08, 0.865,
             f"Detector Listen selectivity: {rejection_db:.1f} dB at 7.8 kHz vs 500 Hz  ·  "
             "Detector Q 2.2  ·  Attenuator Q 2.8  ·  Maximum depth 12 dB",
             color="#90a3b1", fontsize=9)

    frequencies = np.geomspace(1000.0, 20000.0, 700)

    def style_axis(ax, title, ylabel):
        ax.set_facecolor("#202c34")
        ax.set_title(title, color="#eef2f5", loc="left", fontsize=12, fontweight="bold", pad=10)
        ax.set_ylabel(ylabel)
        ax.grid(True, color="#52616c", alpha=0.35, linewidth=0.7)
        for spine in ax.spines.values():
            spine.set_color("#52616c")
        ax.set_xscale("log")
        ax.set_xlim(1000, 20000)
        ax.set_xticks([1000, 2000, 4000, 8000, 16000])
        ax.set_xticklabels(["1k", "2k", "4k", "8k", "16k"])
        ax.set_xlabel("Frequency (Hz)")

    ax = axes[0, 0]
    style_axis(ax, "Detector band-pass", "Relative level (dB)")
    b, a = detector_filter()
    curve = response_db(b, a, frequencies)
    ax.plot(frequencies, curve - curve.max(), color="#54c2d8", linewidth=2.5,
            label="7.8 kHz · Q 2.2")
    ax.axvline(7800, color="#54c2d8", alpha=0.45, linestyle="--", linewidth=1)
    ax.set_ylim(-50, 3)
    ax.legend(frameon=False, labelcolor="#dce5eb", loc="lower left")

    ax = axes[0, 1]
    style_axis(ax, "Attenuator filter shapes", "Gain (dB)")
    b, a = shelf_filter()
    ax.plot(frequencies, response_db(b, a, frequencies), color="#54c2d8", linewidth=2.3,
            label="Wideband high-shelf")
    b, a = bell_filter()
    ax.plot(frequencies, response_db(b, a, frequencies), color="#bd91ed", linewidth=2.3,
            label="Split-band bell")
    ax.axhline(0, color="#91a0aa", linewidth=0.8, alpha=0.6)
    ax.set_ylim(-15, 2)
    ax.legend(frameon=False, labelcolor="#dce5eb", loc="lower left")

    x = np.arange(len(modes))
    ax = axes[1, 0]
    style_axis(ax, "Broadband attenuation", "RMS reduction (%)")
    reductions = [results[mode][1] for mode in modes]
    bars = ax.bar(x, reductions, color=[colors[m] for m in modes], width=0.62)
    ax.set_xscale("linear")
    ax.set_xlim(-0.55, len(modes) - 0.45)
    ax.set_xlabel("")
    ax.set_xticks(x, modes)
    ax.set_ylim(0, max(reductions) * 1.28)
    for bar, value in zip(bars, reductions):
        ax.text(bar.get_x() + bar.get_width() / 2, value + 0.7, f"{value:.1f}%",
                ha="center", color="#edf2f5", fontsize=10, fontweight="bold")

    ax = axes[1, 1]
    ax.set_facecolor("#202c34")
    ax.set_title("Tone retention", color="#eef2f5", loc="left", fontsize=12, fontweight="bold", pad=10)
    width = 0.32
    body = [results[mode][2] for mode in modes]
    detail = [results[mode][3] for mode in modes]
    bars_body = ax.bar(x - width / 2, body, width, color="#8da1ad", label="500 Hz body")
    bars_detail = ax.bar(x + width / 2, detail, width, color=[colors[m] for m in modes], label="7.8 kHz detail")
    ax.set_xticks(x, modes)
    ax.set_ylabel("Input tone retained (%)")
    ax.set_ylim(0, 120)
    ax.grid(True, axis="y", color="#52616c", alpha=0.35, linewidth=0.7)
    ax.set_axisbelow(True)
    for spine in ax.spines.values():
        spine.set_color("#52616c")
    ax.legend(frameon=False, labelcolor="#dce5eb", loc="upper left", ncols=2, fontsize=9)
    for bars in (bars_body, bars_detail):
        for bar in bars:
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 2,
                    f"{bar.get_height():.0f}", ha="center", color="#edf2f5", fontsize=8)

    ax = fig.add_subplot(grid[2, :])
    ax.set_facecolor("#202c34")
    ax.set_title("Smart detail retention vs attenuator Q", color="#eef2f5", loc="left", fontsize=12, fontweight="bold", pad=10)
    ax.plot(qresults[:, 0], qresults[:, 2], marker="o", color="#e7ad5b", linewidth=2.2, label="7.8 kHz detail tone")
    ax.set_xlabel("Attenuator Q"); ax.set_ylabel("Detail tone retained (%)")
    ax.grid(True, color="#52616c", alpha=0.35, linewidth=0.7)
    for spine in ax.spines.values(): spine.set_color("#52616c")
    ax.legend(frameon=False, labelcolor="#dce5eb", loc="lower right")

    ax = fig.add_subplot(grid[3, :])
    ax.set_facecolor("#202c34")
    ax.set_title("Smart rolling attenuator shape · measured gain change during sweep", color="#eef2f5", loc="left",
                 fontsize=12, fontweight="bold", pad=10)
    # JUCE stream writes can repeat newline-terminated rows on some platforms;
    # collapse repeated time/frequency cells before arranging FFT frames.
    cells = np.rec.fromarrays([rolling["time_s"], rolling["frequency_hz"]], names="time,frequency")
    _, first = np.unique(cells, return_index=True)
    rolling = rolling[np.sort(first)]
    times = np.unique(rolling["time_s"]); freqs = np.unique(rolling["frequency_hz"])
    ti = np.searchsorted(times, rolling["time_s"]); fi = np.searchsorted(freqs, rolling["frequency_hz"])
    change = np.full((len(times), len(freqs)), np.nan)
    change[ti, fi] = rolling["change_db"]
    if np.isnan(change).any():
        change = np.where(np.isnan(change), np.nanmedian(change), change)
    norm = TwoSlopeNorm(vmin=-18, vcenter=0, vmax=6)
    mesh = ax.pcolormesh(times, freqs, change.T, shading="auto", cmap="RdBu_r", norm=norm)
    ax.set_ylim(2000, 16000); ax.set_xlabel("Time (s)"); ax.set_ylabel("Frequency (Hz)")
    ax.set_yticks([2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000], ["2k", "4k", "6k", "8k", "10k", "12k", "14k", "16k"])
    cb = fig.colorbar(mesh, ax=ax, pad=0.012); cb.set_label("Measured output − input (dB)", color="#c6d0d8"); cb.ax.tick_params(colors="#aebbc5")
    for spine in ax.spines.values(): spine.set_color("#52616c")

    smart_detail = results["Smart"][3]
    wide_detail = results["Wideband"][3]
    delta = smart_detail - wide_detail
    fig.text(0.08, 0.105,
             f"Smart keeps {smart_detail:.1f}% of the 7.8 kHz detail tone vs {wide_detail:.1f}% in Wideband "
             f"(+{delta:.1f} points).",
             color="#e7ad5b", fontsize=11, fontweight="bold")
    fig.text(0.08, 0.065,
             f"It retains {results['Smart'][2]:.1f}% of the 500 Hz body. Bottom panel maps measured Smart gain change over frequency and time: cuts below 0 dB, restored detail above 0 dB.",
             color="#c4d0d8", fontsize=9.5)

    for path in (png_path, pdf_path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png_path, dpi=180, facecolor=fig.get_facecolor())
    fig.savefig(pdf_path, facecolor=fig.get_facecolor(), metadata={"Title": "SHUN DE-ESS analysis report"})
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default="projects/ShunDeEss/Tests/Reports/detector-attenuator-mock-report.md")
    parser.add_argument("--png", default="projects/ShunDeEss/Tests/Reports/detector-attenuator-mock-report.png")
    parser.add_argument("--pdf", default="projects/ShunDeEss/Tests/Reports/detector-attenuator-mock-report.pdf")
    args = parser.parse_args()
    render(args.input, args.png, args.pdf)
    print(f"Wrote {args.png} and {args.pdf}")


if __name__ == "__main__":
    main()
