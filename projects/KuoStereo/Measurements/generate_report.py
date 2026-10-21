#!/usr/bin/env python3
"""Turn deterministic Kuo Stereo processor measurements into a reviewable report."""

import csv
import os
import sys
import tempfile
import wave
from collections import Counter
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", str(Path(tempfile.gettempdir()) / "kuo-matplotlib"))
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

FS = 48_000
SEGMENT_SECONDS = 2
LABELS = ["100 Hz + noise", "500 Hz + noise", "1350 Hz + noise", "broad noise"]


def read_csv(path):
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def rms(samples):
    return float(np.sqrt(np.mean(np.square(samples, dtype=np.float64))))


def spectral_distance(reference, processed):
    """Match the processor's weighted, log-magnitude FFT score on steady windows."""
    window = np.hanning(512)
    scores = []
    for start in range(0, len(reference) - 512, 256):
        original = np.abs(np.fft.rfft(reference[start : start + 512] * window)) / 512
        output = np.abs(np.fft.rfft(processed[start : start + 512] * window)) / 512
        original = original[2:256]
        output = output[2:256]
        weight = original / (original + 0.01)
        scores.append(
            np.sum(weight * np.abs(np.log((output + 0.0001) / (original + 0.0001))))
            / max(np.sum(weight), 1)
        )
    return float(np.mean(scores))


def save_wav(path, channels):
    values = np.column_stack(channels)
    pcm = np.clip(values * 32767, -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as output:
        output.setnchannels(len(channels))
        output.setsampwidth(2)
        output.setframerate(FS)
        output.writeframes(pcm.tobytes())


def main(raw_dir, report_dir):
    report_dir.mkdir(parents=True, exist_ok=True)
    adaptation = read_csv(raw_dir / "adaptation.csv")
    times = np.array([float(row["time_s"]) for row in adaptation])
    target = np.array([float(row["target_ms"]) for row in adaptation])
    active = np.array([float(row["active_ms"]) for row in adaptation])
    adaptive_audio = np.fromfile(raw_dir / "adaptation.raw", dtype="<f4").reshape(-1, 3)
    adaptive_results = []
    for segment, label in enumerate(LABELS):
        steady = (times >= 2 * segment + 0.5) & (times < 2 * segment + 2)
        source = adaptive_audio[(2 * segment * FS + FS // 2) : (2 * segment + 2) * FS]
        adaptive_results.append(
            {
                "label": label,
                "mode": Counter(target[steady]).most_common(1)[0][0],
                "median": float(np.median(active[steady])),
                "adaptive": spectral_distance(source[:, 0], source[:, 1]),
                "fixed": spectral_distance(source[:, 0], source[:, 2]),
            }
        )
    max_rate = float(np.max(np.abs(np.diff(active) / np.diff(times))))

    plt.rcParams.update({"font.size": 9, "axes.spines.top": False, "axes.spines.right": False})
    fig, (ax, score_ax) = plt.subplots(2, 1, figsize=(10, 6.3), layout="constrained")
    ax.step(times, target, where="post", color="#ca7939", alpha=0.62, lw=0.85, label="selected target")
    ax.plot(times, active, color="#314e79", lw=1.5, label="rate-limited active delay")
    for border in (2, 4, 6):
        ax.axvline(border, color="0.65", ls=":", lw=0.8)
    for segment, label in enumerate(LABELS):
        ax.text(segment * 2 + 1, 18.65, label, ha="center", fontsize=8)
    ax.set(xlim=(0, 8), ylim=(4.5, 19.4), ylabel="Haas delay (ms)", title="Smart Haas response to four sound spectra")
    ax.legend(loc="lower right", frameon=False, ncol=2)
    positions = np.arange(len(LABELS))
    score_ax.bar(positions - 0.18, [row["adaptive"] for row in adaptive_results], width=0.36,
                 color="#ca7939", label="adaptive")
    score_ax.bar(positions + 0.18, [row["fixed"] for row in adaptive_results], width=0.36,
                 color="#314e79", label="fixed 12 ms")
    score_ax.set(xticks=positions, xticklabels=LABELS,
                 ylabel="weighted log-spectrum error (lower is better)")
    score_ax.legend(frameon=False)
    fig.savefig(report_dir / "smart-haas.png", dpi=170)
    plt.close(fig)

    transient = read_csv(raw_dir / "transient.csv")
    transient_time = np.array([float(row["time_ms"]) for row in transient])
    columns = {key: np.array([float(row[key]) for row in transient]) for key in transient[0]}
    tail = (transient_time >= 4) & (transient_time < 28)
    no_guard = rms(columns["velvet_side_no_guard"][tail])
    guard = rms(columns["velvet_side_guard"][tail])
    onset_index = int(np.argmin(np.abs(transient_time)))
    delay = read_csv(raw_dir / "transient_delay.csv")
    delay_time = np.array([float(row["time_ms"]) for row in delay])
    normal_delay = np.array([float(row["normal_ms"]) for row in delay])
    kept_delay = np.array([float(row["kept_ms"]) for row in delay])
    fig, axes = plt.subplots(3, 1, figsize=(10, 8), layout="constrained")
    axes[0].plot(transient_time, columns["input"], color="0.4", lw=0.9, label="input noise + transient")
    axes[0].plot(transient_time, columns["right_normal"], color="#314e79", lw=1, label="right, Keep 0%")
    axes[0].plot(transient_time, columns["right_kept"], color="#ca7939", lw=1, label="right, Keep 100%")
    axes[0].set(xlim=(-1, 20), ylabel="sample amplitude", title="Haas transient alignment")
    axes[0].legend(frameon=False, ncol=3)
    axes[1].plot(delay_time, normal_delay, color="#314e79", label="Keep 0%")
    axes[1].plot(delay_time, kept_delay, color="#ca7939", label="Keep 100%")
    axes[1].set(xlim=(-10, 120), ylim=(-1, 14), ylabel="effective delay (ms)", title="Shared onset hold and tap crossfade")
    axes[1].legend(frameon=False)
    kernel = np.ones(24) / 24  # 0.5 ms moving RMS at 48 kHz
    for key, label, colour in (
        ("velvet_side_no_guard", "Velvet guard 0%", "#314e79"),
        ("velvet_side_guard", "Velvet guard 100%", "#ca7939"),
    ):
        envelope = np.sqrt(np.convolve(columns[key] ** 2, kernel, mode="same"))
        axes[2].plot(transient_time, envelope, color=colour, label=label)
    axes[2].set(xlim=(0, 35), xlabel="time relative to transient (ms)",
                ylabel="added side RMS", title="Velvet-generated side tail")
    axes[2].legend(frameon=False)
    fig.savefig(report_dir / "transients.png", dpi=170)
    plt.close(fig)

    mono = np.fromfile(raw_dir / "mono.raw", dtype="<f4").reshape(-1, 5)
    input_mono, off_l, off_r, on_l, on_r = mono.T
    off_side = 0.5 * (off_l - off_r)
    on_side = 0.5 * (on_l - on_r)
    off_mid = 0.5 * (off_l + off_r)
    on_mid = 0.5 * (on_l + on_r)
    added_side = on_side - off_side
    off_side_rms, on_side_rms, added_side_rms = map(rms, (off_side, on_side, added_side))
    off_corr = float(np.corrcoef(off_l, off_r)[0, 1])
    on_corr = float(np.corrcoef(on_l, on_r)[0, 1])
    mid_error = rms(on_mid - off_mid)
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.3), layout="constrained")
    points = np.arange(0, len(mono), 37)
    axes[0].scatter(off_l[points], off_r[points], s=3, alpha=0.15, color="#314e79")
    axes[0].set(title="Mono noise through Haas only", xlabel="left", ylabel="right", xlim=(-0.25, 0.25), ylim=(-0.25, 0.25))
    axes[1].scatter(on_l[points], on_r[points], s=3, alpha=0.15, color="#ca7939")
    axes[1].set(title="Same mono noise + Velvet 100%", xlabel="left", ylabel="right", xlim=(-0.25, 0.25), ylim=(-0.25, 0.25))
    fig.savefig(report_dir / "mono-width.png", dpi=170)
    plt.close(fig)
    save_wav(report_dir / "mono-input.wav", [input_mono])
    save_wav(report_dir / "mono-haas-only.wav", [off_l, off_r])
    save_wav(report_dir / "mono-velvet-100.wav", [on_l, on_r])

    scope = read_csv(raw_dir / "scope.csv")
    scope_rows = []
    fig, axes = plt.subplots(1, 3, figsize=(10, 3.7), layout="constrained")
    for axis, band in zip(axes, ("low", "mid", "high")):
        points = np.array([(float(row["left"]), float(row["right"]))
                           for row in scope if row["band"] == band])
        scope_rows.append((band, len(points), rms(points[:, 0]), rms(points[:, 1])))
        axis.scatter(points[:, 0], points[:, 1], color="#ca7939", s=8, alpha=0.6)
        axis.axhline(0, color="0.75", lw=0.7)
        axis.axvline(0, color="0.75", lw=0.7)
        axis.set(title=f"{band.title()} band", xlabel="left", ylabel="right", xlim=(-0.4, 0.4), ylim=(-0.4, 0.4))
        axis.set_aspect("equal")
    fig.savefig(report_dir / "frequency-field.png", dpi=170)
    plt.close(fig)

    lines = [
        "# Kuo Stereo feature measurement report",
        "",
        "This report measures the actual `KuoStereoProcessor` at 48 kHz in 256-sample blocks.",
        "Signals are synthetic and deterministic; no listening or host test is implied.",
        "The measurement executable and this plotting script reproduce every figure and sample.",
        "",
        "## Smart Haas: content adaptation and spectral preservation",
        "",
        "Four consecutive 2 s mono segments contain either a 100, 500, or 1350 Hz tone",
        "plus low-level white noise, or broad white noise alone. Center is 12 ms,",
        "Variability is ±6 ms, Max Rate is 10 ms/s, and Velvet Width is 0%.",
        "The fixed comparison uses the same processor with Variability at 0 ms.",
        "The first 0.5 s of each segment is excluded from the steady-state table.",
        "The error is the mean of weighted absolute log FFT-magnitude ratios",
        "between input and processed mid, over 512-sample Hann windows; lower is better.",
        "",
        "![Smart Haas delay and spectrum comparison](smart-haas.png)",
        "",
        "| Content | Most common target | Median active delay | Adaptive error | Fixed 12 ms error |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for row in adaptive_results:
        lines.append(f'| {row["label"]} | {row["mode"]:.1f} ms | {row["median"]:.2f} ms | '
                     f'{row["adaptive"]:.3f} | {row["fixed"]:.3f} |')
    lines += [
        "",
        f"The selected target changes with content and stays inside 6–18 ms; the largest observed",
        f"active-delay step is approximately {max_rate:.1f} ms/s (10 ms/s setting). The spectral comparison",
        "also exposes a limitation: adaptation does **not** consistently improve the",
        "processed mid spectrum against a fixed 12 ms delay in these signals.",
        "Broad noise makes the target fluctuate among candidates. This result demonstrates",
        "content response, but does not validate a universal spectral-preservation claim.",
        "The 500 Hz case is especially poor: the fixed 12 ms delay is six tone",
        "periods, but the six-candidate grid omits 12 ms and alternates between",
        "other period-matched targets. Rate-limited travel between them passes",
        "through delays that colour the tone. Candidate-grid coverage and",
        "temporal averaging merit investigation before claiming reliable improvement.",
        "",
        "## Keep Transient and Velvet Transient Guard",
        "",
        "A 1.0-amplitude onset with a short decaying noise burst is placed in a",
        "0.002-amplitude white-noise bed. Haas Center is 12 ms and Variability is 0 ms.",
        "Keep Transient is compared at 0% and 100% with Velvet Width at 0%.",
        "Separately, Velvet Width is 100% and Transient Guard is compared at 0% and",
        "100%; the Haas-only side signal is subtracted to isolate Velvet's added side.",
        "",
        "![Transient timing and side-tail protection](transients.png)",
        "",
        f'- At onset, the right sample is {columns["right_normal"][onset_index]:.4f} with Keep 0% '
        f'and {columns["right_kept"][onset_index]:.4f} with Keep 100% '
        f'(input {columns["input"][onset_index]:.4f}).',
        f'- Velvet-added side RMS from 4–28 ms is {no_guard:.5f} at Guard 0% '
        f'and {guard:.5f} at Guard 100% '
        f'({100 * (1 - guard / no_guard):.1f}% reduction).',
        "- The protected Haas path reaches 0 ms during the onset and returns after",
        "  the shared hold and tap crossfade; normal Haas stays at 12 ms.",
        "",
        "## Velvet Width: widening a mono signal",
        "",
        "A 1.5 s mono, low-pass-filtered white-noise excerpt is processed twice with",
        "the same fixed 12 ms Haas delay: Velvet Width at 0% and 100%.",
        "Transient Guard is 0% to expose the full velvet contribution.",
        "The Haas-only render is the baseline; the change between renders is Velvet.",
        "",
        "![Mono noise stereo scatter comparison](mono-width.png)",
        "",
        f'- Side RMS rises from {off_side_rms:.5f} to {on_side_rms:.5f} '
        f'({100 * (on_side_rms / off_side_rms - 1):.1f}% increase).',
        f'- Velvet itself adds {added_side_rms:.5f} side RMS while the mid difference '
        f'between renders is {mid_error:.7f} RMS.',
        f'- Left/right correlation changes from {off_corr:.3f} to {on_corr:.3f}.',
        "- Listen to [mono input](mono-input.wav), [Haas only](mono-haas-only.wav),",
        "  and [Haas + Velvet 100%](mono-velvet-100.wav).",
        "",
        "## Frequency Field: split goniometer",
        "",
        "An 80 Hz, 700 Hz, and 5 kHz stereo tone mix feeds the processor. The three",
        "plots are the actual 96-point scope buffers read from the processor, using",
        "its below-250 Hz, 250 Hz–2.5 kHz, and above-2.5 kHz splits.",
        "",
        "![Three frequency-split goniometers](frequency-field.png)",
        "",
        "| Band | Points | Left RMS | Right RMS |",
        "| --- | ---: | ---: | ---: |",
    ]
    for band, count, left, right in scope_rows:
        lines.append(f"| {band} | {count} | {left:.4f} | {right:.4f} |")
    lines += [
        "",
        "All bands receive nonzero processed stereo points; this verifies the visual",
        "data path, not calibrated filter separation.",
        "",
        "## Reproduce",
        "",
        "From the repository root, after configuring `projects/KuoStereo` with",
        "`-DKUO_BUILD_TESTS=ON`:",
        "",
        "```bash",
        "cmake --build build-kuo-stereo --target KuoStereoFeatureMeasurements --parallel 4",
        "./build-kuo-stereo/KuoStereoFeatureMeasurements /tmp/kuo-feature-data",
        "python3 projects/KuoStereo/Measurements/generate_report.py /tmp/kuo-feature-data",
        "```",
        "",
        "The plotting step requires NumPy and Matplotlib. Output goes to",
        "`projects/KuoStereo/Measurements/Report` unless a second path is supplied.",
        "",
    ]
    (report_dir / "REPORT.md").write_text("\n".join(lines))


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: generate_report.py RAW_DIR [REPORT_DIR]")
    default_report = Path(__file__).resolve().parent / "Report"
    main(Path(sys.argv[1]), Path(sys.argv[2]) if len(sys.argv) == 3 else default_report)
