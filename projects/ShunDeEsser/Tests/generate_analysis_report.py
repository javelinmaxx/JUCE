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
    for mode in ("Wideband", "Split-band", "Brilliant", "Smooth"):
        report_name = "Smart" if mode == "Brilliant" and "| Brilliant |" not in text else mode
        match = re.search(
            rf"\| {re.escape(report_name)} \| ([\d.]+) \| ([\d.]+)% \| ([\d.]+)% \| ([\d.]+)% \|",
            text,
        )
        if not match:
            if mode == "Smooth":
                continue  # Historical reports have only three modes.
            raise ValueError(f"Could not find {mode} data in {path}")
        rows[mode] = tuple(float(value) for value in match.groups())
    detector = re.search(r"in-band tone is ([\d.]+) dB", text)
    if not detector:
        raise ValueError(f"Could not find detector selectivity in {path}")
    q_heading = "### Brilliant attenuator-Q" if "### Brilliant attenuator-Q" in text else "### Smart attenuator-Q"
    qrows = re.findall(r"\| ([\d.]+) \| ([\d.]+)% \| ([\d.]+)% \|", text[text.find(q_heading):])
    if len(qrows) < 2:
        raise ValueError("Could not find Brilliant attenuator-Q sweep")
    pulse_section = text.split("### Sibilance pulse attenuation", 1)[-1].split("\n###", 1)[0]
    pulse_rows = re.findall(r"\| ([\d.]+) \| (-?[\d.]+) dB \|", pulse_section)
    if len(pulse_rows) != 4:
        raise ValueError("Could not find all four sibilance pulse measurements")
    return rows, float(detector.group(1)), np.array(qrows, dtype=float), np.array(pulse_rows, dtype=float)


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
    slope = np.clip(1.0 / q, 0.25, 1.0)
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
    results, rejection_db, qresults, pulse_results = read_results(source)
    spectrum_path = Path(source).with_name("smart-spectral-spectrum.csv")
    spectrum = np.genfromtxt(spectrum_path, delimiter=",", names=True)
    rolling_sets = {}
    for q in (0.5, 1.2, 2.8, 4.0):
        rolling_path = Path(source).with_name(f"smart-rolling-q-{q:.1f}.csv")
        rolling = np.genfromtxt(rolling_path, delimiter=",", names=True)
        rolling = rolling[np.isfinite(rolling["time_s"]) & np.isfinite(rolling["frequency_hz"])
                          & np.isfinite(rolling["output_dbfs"]) & np.isfinite(rolling["change_db"])]
        rolling_sets[q] = rolling
    measured = (spectrum["frequency_hz"] >= 2000.0) & (spectrum["frequency_hz"] <= 16000.0)
    if not np.any(measured):
        raise ValueError(f"No 2–16 kHz Brilliant spectrum samples in {spectrum_path}")
    modes = tuple(results)
    colors = {"Wideband": "#54c2d8", "Split-band": "#bd91ed",
              "Brilliant": "#e7ad5b", "Smooth": "#80bfae"}
    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "text.color": "#e7edf2",
        "axes.labelcolor": "#c6d0d8",
        "xtick.color": "#aebbc5",
        "ytick.color": "#aebbc5",
    })
    fig = plt.figure(figsize=(13, 16), facecolor="#182128")
    grid = fig.add_gridspec(4, 2, height_ratios=[1, 1, 0.9, 1.45], left=0.09, right=0.90,
                            top=0.83, bottom=0.17, hspace=0.48, wspace=0.28)
    axes = np.array([[fig.add_subplot(grid[0, 0]), fig.add_subplot(grid[0, 1])],
                     [fig.add_subplot(grid[1, 0]), fig.add_subplot(grid[1, 1])]])
    fig.suptitle("Shun De-Esser  |  DETECTOR & ATTENUATOR TEST", color="#f2f5f7",
                 fontsize=19, fontweight="bold", x=0.08, y=0.955, ha="left")
    fig.text(0.08, 0.905,
             "Deterministic 48 kHz mocks: fixed detail for Q sweep; swept tone plus gated sibilance noise pulses",
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
    # The shelf is minimum-phase. Plot its phase too because the Brilliant path
    # mixes this filtered signal with dry audio, which can affect the measured
    # magnitude around a moving narrow tone.
    omega = 2.0 * np.pi * frequencies / 48000.0
    z1 = np.exp(-1j * omega)
    phase_ax = ax.twinx()
    for q, color, style in ((0.5, "#f0c77e", "--"), (2.8, "#91d2aa", ":"), (4.0, "#f08f84", "-.") ):
        b_shelf, a_shelf = shelf_filter(q=q)
        shelf_h = (b_shelf[0] + b_shelf[1] * z1 + b_shelf[2] * z1 * z1) / (1.0 + a_shelf[0] * z1 + a_shelf[1] * z1 * z1)
        phase_ax.plot(frequencies, np.angle(shelf_h, deg=True), color=color, linewidth=1.4,
                      linestyle=style, label=f"Wideband phase (Q {q:g})")
    bp_b, bp_a = detector_filter(center=7800.0, q=2.8)
    bp_h = (bp_b[0] + bp_b[1] * z1 + bp_b[2] * z1 * z1) / (1.0 + bp_a[0] * z1 + bp_a[1] * z1 * z1)
    phase_ax.plot(frequencies, np.angle(bp_h, deg=True), color="#bd91ed", linewidth=1.5,
                  linestyle="-", label="Restoration band-pass (7.8 kHz, Q 2.8)")
    phase_ax.set_ylabel("Phase (degrees)", color="#f0c77e")
    phase_ax.tick_params(axis="y", colors="#f0c77e")
    phase_ax.set_ylim(-90, 90)
    lines = [line for line in ax.get_lines() + phase_ax.get_lines() if not line.get_label().startswith("_")]
    ax.legend(lines, [line.get_label() for line in lines], frameon=False, labelcolor="#dce5eb", loc="lower left", fontsize=8)

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
    ax.set_title("Brilliant detail retention vs attenuator Q", color="#eef2f5", loc="left", fontsize=12, fontweight="bold", pad=10)
    ax.plot(qresults[:, 0], qresults[:, 2], marker="o", color="#e7ad5b", linewidth=2.2, label="7.8 kHz detail tone")
    ax.set_xlabel("Attenuator Q"); ax.set_ylabel("Detail tone retained (%)")
    ax.grid(True, color="#52616c", alpha=0.35, linewidth=0.7)
    for spine in ax.spines.values(): spine.set_color("#52616c")
    ax.legend(frameon=False, labelcolor="#dce5eb", loc="lower right")

    heatmap_grid = grid[3, :].subgridspec(2, 2, hspace=0.28, wspace=0.18)
    fig.text(0.09, 0.355, "Rolling attenuator shape · blue sibilance pulses",
             color="#eef2f5", fontsize=12, fontweight="bold")
    heat_axes = []
    norm = TwoSlopeNorm(vmin=-18, vcenter=0, vmax=6)
    for index, q in enumerate((0.5, 1.2, 2.8, 4.0)):
        ax = fig.add_subplot(heatmap_grid[index // 2, index % 2])
        heat_axes.append(ax)
        ax.set_facecolor("#202c34")
        rolling = rolling_sets[q]
        cells = np.rec.fromarrays([rolling["time_s"], rolling["frequency_hz"]], names="time,frequency")
        _, first = np.unique(cells, return_index=True)
        rolling = rolling[np.sort(first)]
        times = np.unique(rolling["time_s"]); freqs = np.unique(rolling["frequency_hz"])
        ti = np.searchsorted(times, rolling["time_s"]); fi = np.searchsorted(freqs, rolling["frequency_hz"])
        change = np.full((len(times), len(freqs)), np.nan)
        change[ti, fi] = rolling["change_db"]
        if np.isnan(change).any():
            change = np.where(np.isnan(change), np.nanmedian(change), change)
        mesh = ax.pcolormesh(times, freqs, change.T, shading="auto", cmap="RdBu_r", norm=norm)
        mesh.set_rasterized(True)
        ax.axvspan(0.82, 1.24, color="#54c2d8", alpha=0.07)
        ax.axvspan(2.05, 2.43, color="#54c2d8", alpha=0.07)
        ax.set_title(f"Attenuator Q {q:.1f}", color="#eef2f5", loc="left", fontsize=9, pad=4)
        ax.set_ylim(2000, 16000); ax.set_xlabel("Time (s)"); ax.set_ylabel("Frequency (Hz)")
        ax.set_xlim(0.65, 2.60)
        ax.set_yticks([2000, 6000, 10000, 14000], ["2k", "6k", "10k", "14k"])
        ax.tick_params(labelsize=8)
        for spine in ax.spines.values(): spine.set_color("#52616c")
    cb = fig.colorbar(mesh, ax=heat_axes, pad=0.012, fraction=0.025)
    cb.set_label("Measured output − input (dB)", color="#c6d0d8"); cb.ax.tick_params(colors="#aebbc5")

    smart_detail = results["Brilliant"][3]
    wide_detail = results["Wideband"][3]
    delta = smart_detail - wide_detail
    fig.text(0.08, 0.105,
             f"Brilliant keeps {smart_detail:.1f}% of the 7.8 kHz detail tone vs {wide_detail:.1f}% in Wideband "
             f"(+{delta:.1f} points).",
             color="#e7ad5b", fontsize=11, fontweight="bold")
    fig.text(0.08, 0.065,
             "Pulse band change (4.5–12 kHz; negative = attenuation): "
             + "   ".join(f"Q {q:.1f} {change:+.2f} dB" for q, change in pulse_results)
             + ". Blue is attenuation; red is gain restoration.",
             color="#c4d0d8", fontsize=8.5)

    for path in (png_path, pdf_path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png_path, dpi=180, facecolor=fig.get_facecolor())
    fig.savefig(pdf_path, facecolor=fig.get_facecolor(), metadata={"Title": "Shun De-Esser analysis report"})
    plt.close(fig)


def render_wide_split_comparison(source):
    source = Path(source)
    q_values = (0.5, 1.2, 2.8, 4.0)
    report_text = source.read_text(encoding="utf-8")
    pulse_text = report_text.split("### Time-varying pulse test across all modes", 1)[-1]
    mode_rows = {}
    all_modes = (("Wideband", "wideband"), ("Split-band", "splitband"),
                 ("Brilliant", "smart-pulse"))
    if "| Smooth |" in pulse_text:
        all_modes += (("Smooth", "smooth-pulse"),)
    for mode, _ in all_modes:
        report_name = "Smart" if mode == "Brilliant" and "| Brilliant |" not in pulse_text else mode
        match = re.search(rf"\| {re.escape(report_name)} \| ([^\n]+)\|", pulse_text)
        if not match:
            raise ValueError(f"Could not find {mode} pulse measurements")
        mode_rows[mode] = [(float(pulse), float(quiet)) for pulse, quiet in
                           re.findall(r"(-?[\d.]+) / (-?[\d.]+) dB", match.group(1))]
        if len(mode_rows[mode]) != len(q_values):
            raise ValueError(f"Expected one pulse measurement for each Q in {mode}")
    fig, axes = plt.subplots(len(all_modes), 4, figsize=(14, 3 * len(all_modes)),
                             facecolor="#182128", sharex=True, sharey=True)
    fig.subplots_adjust(left=0.07, right=0.91, top=0.84, bottom=0.11, wspace=0.16, hspace=0.34)
    fig.suptitle("Shun De-Esser  |  TIME-VARYING SIBILANCE PULSES",
                 color="#f2f5f7", fontsize=16, fontweight="bold", x=0.07, y=0.96, ha="left")
    fig.text(0.07, 0.90, "Same seeded input across modes · quiet baseline releases to unity",
             color="#aebbc5", fontsize=10)
    norm = TwoSlopeNorm(vmin=-18, vcenter=0, vmax=6)
    mesh = None
    for row, (mode, stem) in enumerate(all_modes):
        for col, q in enumerate(q_values):
            ax = axes[row, col]
            ax.set_facecolor("#202c34")
            path = source.with_name(f"{stem}-rolling-q-{q:.1f}.csv")
            rolling = np.genfromtxt(path, delimiter=",", names=True)
            rolling = rolling[np.isfinite(rolling["time_s"]) & np.isfinite(rolling["frequency_hz"])
                              & np.isfinite(rolling["change_db"])]
            cells = np.rec.fromarrays([rolling["time_s"], rolling["frequency_hz"]], names="time,frequency")
            _, first = np.unique(cells, return_index=True)
            rolling = rolling[np.sort(first)]
            times = np.unique(rolling["time_s"]); freqs = np.unique(rolling["frequency_hz"])
            ti = np.searchsorted(times, rolling["time_s"]); fi = np.searchsorted(freqs, rolling["frequency_hz"])
            change = np.full((len(times), len(freqs)), np.nan)
            change[ti, fi] = rolling["change_db"]
            if np.isnan(change).any():
                change = np.where(np.isnan(change), np.nanmedian(change), change)
            mesh = ax.pcolormesh(times, freqs, change.T, shading="auto", cmap="RdBu_r", norm=norm)
            mesh.set_rasterized(True)
            ax.axvspan(0.82, 1.24, color="#54c2d8", alpha=0.10)
            ax.axvspan(2.05, 2.43, color="#54c2d8", alpha=0.10)
            pulse_db, quiet_db = mode_rows[mode][col]
            ax.set_title(f"{mode} · Q {q:.1f} · pulse {pulse_db:+.2f} / quiet {quiet_db:+.2f} dB",
                         color="#eef2f5", loc="left", fontsize=8, pad=5)
            ax.set_xlim(0.65, 2.60); ax.set_ylim(2000, 16000)
            ax.set_xticks([0.8, 1.2, 1.6, 2.0, 2.4])
            ax.set_yticks([2000, 6000, 10000, 14000], ["2k", "6k", "10k", "14k"])
            ax.tick_params(colors="#aebbc5", labelsize=8)
            if col == 0:
                ax.set_ylabel("Frequency (Hz)", color="#c6d0d8")
            if row == len(all_modes) - 1:
                ax.set_xlabel("Time (s)", color="#c6d0d8")
            for spine in ax.spines.values():
                spine.set_color("#52616c")
    cb = fig.colorbar(mesh, ax=axes, pad=0.015, fraction=0.025)
    cb.set_label("Measured output − input (dB)", color="#c6d0d8")
    cb.ax.tick_params(colors="#aebbc5")
    fig.text(0.07, 0.045, "Panel titles show integrated pulse / quiet 4.5–12 kHz change; each pulse should be lower than its quiet baseline. Blue indicates attenuation.",
             color="#c4d0d8", fontsize=9)
    png_path = source.with_name("wide-split-pulse-test-report.png")
    pdf_path = source.with_name("wide-split-pulse-test-report.pdf")
    fig.savefig(png_path, dpi=180, facecolor=fig.get_facecolor())
    fig.savefig(pdf_path, facecolor=fig.get_facecolor(), metadata={"Title": "SHUN Wideband and Split-band pulse test"})
    plt.close(fig)


def render_smart_detector_q(source):
    source = Path(source)
    report_text = source.read_text(encoding="utf-8")
    q_section = report_text.split("### Brilliant detector-Q time-response test", 1)[-1]
    rows = re.findall(r"\| ([\d.]+) \| (-?[\d.]+) dB \| (-?[\d.]+) dB \|", q_section)
    if len(rows) != 4:
        raise ValueError("Could not find all Brilliant detector-Q response measurements")
    values = np.array(rows, dtype=float)
    fig, ax = plt.subplots(figsize=(8.5, 4.5), facecolor="#182128")
    ax.set_facecolor("#202c34")
    ax.axhline(0, color="#aebbc5", linewidth=1, alpha=0.7)
    ax.plot(values[:, 0], values[:, 1], marker="o", linewidth=2.5, markersize=7,
            color="#e7ad5b", label="Sibilance pulse")
    ax.plot(values[:, 0], values[:, 2], marker="o", linewidth=2, markersize=6,
            color="#54c2d8", label="Quiet baseline")
    for q, pulse, _quiet in values:
        ax.annotate(f"{pulse:.2f} dB", (q, pulse), xytext=(0, -17), textcoords="offset points",
                    ha="center", color="#f3c778", fontsize=8)
    ax.set_title("Brilliant detector Q · time-varying pulse response · Attenuator Q 2.8",
                 color="#eef2f5", loc="left", fontsize=12, fontweight="bold", pad=12)
    ax.set_xlabel("Detector Q", color="#c6d0d8")
    ax.set_ylabel("Integrated 4.5–12 kHz output − input (dB)", color="#c6d0d8")
    ax.set_xticks(values[:, 0], [f"{q:.1f}" for q in values[:, 0]])
    low = min(float(np.min(values[:, 1])) - 0.4, -0.5)
    ax.set_ylim(low, 0.5)
    ax.grid(True, color="#52616c", alpha=0.35, linewidth=0.7)
    ax.tick_params(colors="#aebbc5")
    for spine in ax.spines.values():
        spine.set_color("#52616c")
    ax.legend(frameon=False, labelcolor="#dce5eb", loc="lower right")
    png_path = source.with_name("smart-detector-q-report.png")
    pdf_path = source.with_name("smart-detector-q-report.pdf")
    fig.savefig(png_path, dpi=180, facecolor=fig.get_facecolor())
    fig.savefig(pdf_path, facecolor=fig.get_facecolor(), metadata={"Title": "SHUN Brilliant detector-Q test"})
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default="projects/ShunDeEsser/Tests/Reports/detector-attenuator-mock-report.md")
    parser.add_argument("--png", default="projects/ShunDeEsser/Tests/Reports/detector-attenuator-mock-report.png")
    parser.add_argument("--pdf", default="projects/ShunDeEsser/Tests/Reports/detector-attenuator-mock-report.pdf")
    args = parser.parse_args()
    render(args.input, args.png, args.pdf)
    render_wide_split_comparison(args.input)
    render_smart_detector_q(args.input)
    print(f"Wrote {args.png} and {args.pdf}")


if __name__ == "__main__":
    main()
