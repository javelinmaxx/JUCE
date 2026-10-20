#!/usr/bin/env python3
"""Render the mock-signal Markdown results as a plotted PNG and PDF report."""

import argparse
import re
from pathlib import Path

import matplotlib.pyplot as plt
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
    return rows, float(detector.group(1))


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
    results, rejection_db = read_results(source)
    spectrum_path = Path(source).with_name("smart-spectral-spectrum.csv")
    spectrum = np.genfromtxt(spectrum_path, delimiter=",", names=True)
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
    fig = plt.figure(figsize=(13, 12), facecolor="#182128")
    grid = fig.add_gridspec(3, 2, height_ratios=[1, 1, 0.95], left=0.09, right=0.97,
                            top=0.83, bottom=0.14, hspace=0.48, wspace=0.28)
    axes = np.array([[fig.add_subplot(grid[0, 0]), fig.add_subplot(grid[0, 1])],
                     [fig.add_subplot(grid[1, 0]), fig.add_subplot(grid[1, 1])]])
    fig.suptitle("SHUN DE-ESS  |  DETECTOR & ATTENUATOR TEST", color="#f2f5f7",
                 fontsize=19, fontweight="bold", x=0.08, y=0.955, ha="left")
    fig.text(0.08, 0.905,
             "Deterministic 48 kHz mock signal: broadband noise + 500 Hz body + late 7.8 kHz detail tone",
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
    ax.set_title("Smart measured spectrum · detail tone present", color="#eef2f5", loc="left",
                 fontsize=12, fontweight="bold", pad=10)
    f = spectrum["frequency_hz"][measured]
    input_db = spectrum["input_dbfs"][measured]
    output_db = spectrum["output_dbfs"][measured]
    change_db = spectrum["change_db"][measured]
    ax.plot(f, input_db, color="#96a6b0", linewidth=0.8, alpha=0.58, label="Mock input")
    ax.plot(f, output_db, color="#54c2d8", linewidth=1.0, alpha=0.9, label="Smart output")
    ax.axvspan(7600, 8000, color="#e7ad5b", alpha=0.10)
    ax.axvline(7800, color="#e7ad5b", linewidth=1.1, linestyle="--", alpha=0.85)
    ax.set_xlim(2000, 16000)
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Magnitude (dBFS)")
    ax.set_ylim(min(float(input_db.min()), float(output_db.min())) - 4.0,
                max(float(input_db.max()), float(output_db.max())) + 10.0)
    ax.set_xticks([2000, 4000, 6000, 7800, 10000, 12000, 14000, 16000])
    ax.set_xticklabels(["2k", "4k", "6k", "7.8k", "10k", "12k", "14k", "16k"])
    ax.grid(True, color="#52616c", alpha=0.35, linewidth=0.7)
    for spine in ax.spines.values():
        spine.set_color("#52616c")
    delta_ax = ax.twinx()
    smoothed_change = np.convolve(change_db, np.ones(31) / 31.0, mode="same")
    delta_line, = delta_ax.plot(f, smoothed_change, color="#e7ad5b", linewidth=1.8,
                                label="Measured output − input")
    delta_ax.set_ylabel("Output − input (dB)", color="#e7ad5b")
    delta_ax.tick_params(axis="y", colors="#e7ad5b")
    delta_ax.set_ylim(min(-18.0, float(smoothed_change.min()) - 2.0),
                      max(3.0, float(smoothed_change.max()) + 2.0))
    delta_ax.axhline(0, color="#e7ad5b", linewidth=0.7, alpha=0.35)
    input_line, output_line = ax.get_lines()[:2]
    ax.legend([input_line, output_line, delta_line],
              ["Mock input", "Smart output", "Measured output − input"],
              frameon=False, labelcolor="#dce5eb", loc="lower left", ncols=3, fontsize=9)
    peak = np.argmax(output_db)
    peak_x, peak_y = f[peak], output_db[peak]
    ax.annotate("7.8 kHz detail tone", xy=(peak_x, peak_y), xytext=(peak_x + 900, peak_y + 8),
                color="#f0c77e", fontsize=9,
                arrowprops={"arrowstyle": "->", "color": "#e7ad5b", "lw": 1.0})

    smart_detail = results["Smart"][3]
    wide_detail = results["Wideband"][3]
    delta = smart_detail - wide_detail
    fig.text(0.08, 0.075,
             f"Smart keeps {smart_detail:.1f}% of the 7.8 kHz detail tone vs {wide_detail:.1f}% in Wideband "
             f"(+{delta:.1f} points).",
             color="#e7ad5b", fontsize=11, fontweight="bold")
    fig.text(0.08, 0.043,
             f"It retains {results['Smart'][2]:.1f}% of the 500 Hz body. Bottom panel plots measured Smart input/output spectra and their difference.",
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
