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
LOOKAHEAD_SAMPLES = round(0.0015 * FS)
SEGMENT_SECONDS = 3
LABELS = ["C major pad", "A minor pad"]


def read_csv(path):
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def rms(samples):
    return float(np.sqrt(np.mean(np.square(samples, dtype=np.float64))))


def band_rms(samples, low_hz, high_hz):
    window = np.hanning(len(samples))
    spectrum = np.fft.rfft(samples * window)
    frequencies = np.fft.rfftfreq(len(samples), 1 / FS)
    band = (frequencies >= low_hz) & (frequencies <= high_hz)
    return float(np.sqrt(2 * np.sum(np.abs(spectrum[band]) ** 2)
                         / (len(samples) * np.sum(window ** 2))))


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
        start = SEGMENT_SECONDS * segment + 0.5
        end = SEGMENT_SECONDS * (segment + 1) - 0.2
        steady = (times >= start) & (times < end)
        source = adaptive_audio[int(start * FS) : int(end * FS)]
        aligned = adaptive_audio[int(start * FS) + LOOKAHEAD_SAMPLES
                                 : int(end * FS) + LOOKAHEAD_SAMPLES]
        adaptive_results.append(
            {
                "label": label,
                "mode": Counter(target[steady]).most_common(1)[0][0],
                "median": float(np.median(active[steady])),
                "target_changes": int(np.count_nonzero(np.diff(target[steady]))),
                "input_rms": rms(source[:, 0]),
                "adaptive": spectral_distance(source[:, 0], aligned[:, 1]),
                "fixed": spectral_distance(source[:, 0], aligned[:, 2]),
            }
        )
    max_rate = float(np.max(np.abs(np.diff(active) / np.diff(times))))

    plt.rcParams.update({"font.size": 9, "axes.spines.top": False, "axes.spines.right": False})
    fig, (ax, score_ax) = plt.subplots(2, 1, figsize=(10, 6.3), layout="constrained")
    ax.step(times, target, where="post", color="#ca7939", alpha=0.62, lw=0.85, label="selected target")
    ax.plot(times, active, color="#314e79", lw=1.5, label="rate-limited active delay")
    ax.axvline(3, color="0.65", ls=":", lw=0.8)
    for segment, label in enumerate(LABELS):
        ax.text(segment * 3 + 1.5, 18.65, label, ha="center", fontsize=8)
    ax.set(xlim=(0, 6), ylim=(4.5, 19.4), ylabel="Haas delay (ms)", title="Smart Haas response to a sustained synth pad chord change")
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
    save_wav(report_dir / "smart-haas-pad-input.wav", [adaptive_audio[:, 0]])
    save_wav(report_dir / "smart-haas-pad-adaptive.wav", [adaptive_audio[:, 1]])
    save_wav(report_dir / "smart-haas-pad-fixed.wav", [adaptive_audio[:, 2]])

    signed = read_csv(raw_dir / "signed_delay.csv")
    signed_time = np.array([float(row["time_ms"]) for row in signed])
    fig, axes = plt.subplots(1, 3, figsize=(10, 3.2), sharey=True, layout="constrained")
    signed_peaks = {}
    for axis, (name, title) in zip(axes, (("positive", "+12 ms"), ("zero", "0 ms"), ("negative", "−12 ms"))):
        left = np.array([float(row[f"{name}_left"]) for row in signed])
        right = np.array([float(row[f"{name}_right"]) for row in signed])
        signed_peaks[name] = (signed_time[np.argmax(np.abs(left))], signed_time[np.argmax(np.abs(right))])
        axis.plot(signed_time, left, color="#ca7939", label="left")
        axis.plot(signed_time, right, color="#314e79", linestyle="--", label="right")
        axis.set(xlim=(0, 30), ylim=(-0.05, 1.1), xlabel="time (ms)", title=f"Center Delay {title}")
        axis.legend(frameon=False)
    axes[0].set_ylabel("sample amplitude")
    fig.savefig(report_dir / "signed-haas-delay.png", dpi=170)
    plt.close(fig)
    zero_preference = {row["source"]: float(row["selected_ms"])
                       for row in read_csv(raw_dir / "zero_preference.csv")}

    slew = read_csv(raw_dir / "haas_slew.csv")
    slew_time = np.array([float(row["time_s"]) for row in slew])
    slew_delay = np.array([float(row["active_ms"]) for row in slew])
    slew_peak_rate = float(np.max(np.diff(slew_delay) / np.diff(slew_time)))
    fig, axis = plt.subplots(figsize=(8, 3.3), layout="constrained")
    axis.plot(slew_time, slew_delay, color="#ca7939", lw=2, label="linear active delay")
    axis.axhline(20, color="#314e79", ls="--", lw=1, label="20 ms target")
    axis.set(xlim=(0, 1.5), ylim=(11.5, 20.5), xlabel="time after Center Delay change (s)",
             ylabel="active Haas delay (ms)", title="12 to 20 ms with Max Rate 10 ms/s")
    axis.legend(frameon=False)
    fig.savefig(report_dir / "haas-linear.png", dpi=170)
    plt.close(fig)

    transient = read_csv(raw_dir / "transient.csv")
    transient_time = np.array([float(row["time_ms"]) for row in transient])
    columns = {key: np.array([float(row[key]) for row in transient]) for key in transient[0]}
    tail = (transient_time >= 4) & (transient_time < 28)
    no_guard = rms(columns["velvet_side_no_guard"][tail])
    guard = rms(columns["velvet_side_guard"][tail])
    guard_half_ms = rms(columns["velvet_side_attack_half_ms"][tail])
    guard_five_ms = rms(columns["velvet_side_attack_five_ms"][tail])
    input_onset_index = int(np.argmin(np.abs(transient_time)))
    onset_index = int(np.argmin(np.abs(transient_time - 1.5)))
    attack_error = float(np.max(np.abs(
        columns["right_kept"][onset_index : onset_index + 160]
        - columns["input"][input_onset_index : input_onset_index + 160])))
    delay = read_csv(raw_dir / "transient_delay.csv")
    delay_time = np.array([float(row["time_ms"]) for row in delay])
    normal_delay = np.array([float(row["normal_ms"]) for row in delay])
    kept_delay = np.array([float(row["kept_ms"]) for row in delay])
    release_end_index = int(np.argmin(np.abs(delay_time - 75.0)))
    remaining_protection = 100 * (normal_delay[release_end_index] - kept_delay[release_end_index]) / normal_delay[release_end_index]
    fig, axes = plt.subplots(3, 1, figsize=(10, 8), layout="constrained")
    axes[0].plot(transient_time, columns["input"], color="0.4", lw=0.9, label="input noise + transient")
    axes[0].plot(transient_time, columns["right_normal"], color="#314e79", lw=1, label="right, Dynamic 0%")
    axes[0].plot(transient_time, columns["right_kept"], color="#ca7939", lw=1, label="right, Dynamic 100%")
    axes[0].axvline(1.5, color="0.4", ls=":", lw=0.9, label="output onset")
    axes[0].set(xlim=(-1, 20), ylabel="sample amplitude", title="Haas transient alignment")
    axes[0].legend(frameon=False, ncol=3)
    axes[1].plot(delay_time, normal_delay, color="#314e79", label="Dynamic 0%")
    axes[1].plot(delay_time, kept_delay, color="#ca7939", label="Dynamic 100%")
    axes[1].set(xlim=(-10, 120), ylim=(-1, 14), ylabel="effective delay (ms)", title="Haas onset hold and tap crossfade")
    axes[1].legend(frameon=False)
    kernel = np.ones(24) / 24  # 0.5 ms moving RMS at 48 kHz
    for key, label, colour in (
        ("velvet_side_no_guard", "Transient 0%", "#314e79"),
        ("velvet_side_guard", "Transient 100%, attack 0 ms", "#ca7939"),
        ("velvet_side_attack_half_ms", "Transient 100%, attack 0.5 ms", "#569477"),
        ("velvet_side_attack_five_ms", "Transient 100%, attack 5 ms", "#9869a7"),
    ):
        envelope = np.sqrt(np.convolve(columns[key] ** 2, kernel, mode="same"))
        axes[2].plot(transient_time, envelope, color=colour, label=label)
    axes[2].set(xlim=(0, 35), xlabel="time relative to transient (ms)",
                ylabel="added side RMS", title="Velvet-generated side tail")
    axes[2].legend(frameon=False)
    fig.savefig(report_dir / "transients.png", dpi=170)
    plt.close(fig)

    guard_noise = read_csv(raw_dir / "guard_noise.csv")
    guard_time = np.array([float(row["time_ms"]) for row in guard_noise])
    guard_window = (guard_time >= 15) & (guard_time < 40)
    guard_columns = {key: np.array([float(row[key]) for row in guard_noise])
                     for key in guard_noise[0] if key != "time_ms"}
    guard_hf = {key: band_rms(values[guard_window], 8000, 20000)
                for key, values in guard_columns.items()}
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.4), layout="constrained")
    for key, label, color in (
        ("instant_guard_side", "0 ms", "#ca7939"),
        ("fast_guard_side", "0.5 ms", "#569477"),
        ("medium_guard_side", "2 ms", "#9869a7"),
        ("slow_guard_side", "3 ms", "#314e79"),
    ):
        axes[0].plot(guard_time[guard_window], guard_columns[key][guard_window],
                     color=color, lw=0.8, alpha=0.8, label=label)
    axes[0].set(xlim=(15, 30), xlabel="time from tone start (ms)", ylabel="Velvet side amplitude",
                title="Attack around onset release")
    axes[0].legend(frameon=False)
    labels = ["Transient 0%", "0 ms", "0.5 ms", "2 ms", "3 ms"]
    keys = ["unguarded_side", "instant_guard_side", "fast_guard_side",
            "medium_guard_side", "slow_guard_side"]
    axes[1].bar(labels, [guard_hf[key] for key in keys],
                color=["#77717a", "#ca7939", "#569477", "#9869a7", "#314e79"])
    axes[1].set(ylabel="8–20 kHz side RMS", title="High-frequency side energy, 15–40 ms")
    fig.savefig(report_dir / "guard-short-attack.png", dpi=170)
    plt.close(fig)

    haas_onset = read_csv(raw_dir / "haas_onset_noise.csv")
    haas_time = np.array([float(row["time_ms"]) for row in haas_onset])
    haas_input = np.array([float(row["input"]) for row in haas_onset])
    haas_normal = np.array([float(row["right_normal"]) for row in haas_onset])
    haas_kept = np.array([float(row["right_kept"]) for row in haas_onset])
    haas_step = float(np.max(np.abs(np.diff(haas_kept[:480]))))
    fig, axis = plt.subplots(figsize=(9, 3.3), layout="constrained")
    axis.plot(haas_time, haas_input, color="0.6", lw=0.9, label="mono input")
    axis.plot(haas_time, haas_normal, color="#314e79", lw=1, label="right, Dynamic 0%")
    axis.plot(haas_time, haas_kept, color="#ca7939", lw=1, label="right, Dynamic 100%")
    axis.set(xlim=(0, 4), xlabel="time from tonal onset (ms)",
             ylabel="sample amplitude", title="Haas Dynamic tonal-onset de-click")
    axis.legend(frameon=False, ncol=3)
    fig.savefig(report_dir / "haas-onset-declick.png", dpi=170)
    plt.close(fig)

    spectral_onset = read_csv(raw_dir / "spectral_onset.csv")
    switch_time = np.array([float(row["time_ms"]) for row in spectral_onset])
    switch_normal = np.array([float(row["normal_ms"]) for row in spectral_onset])
    switch_kept = np.array([float(row["kept_ms"]) for row in spectral_onset])
    before_switch = float(np.median(switch_kept[(switch_time >= -100) & (switch_time < 0)]))
    after_switch = float(switch_kept[np.flatnonzero(switch_time > 0)[0]])
    fig, axis = plt.subplots(figsize=(8, 3.3), layout="constrained")
    axis.plot(switch_time, switch_normal, color="#314e79", label="Dynamic 0%")
    axis.plot(switch_time, switch_kept, color="#ca7939", label="Dynamic 100%")
    axis.axvline(0, color="0.4", ls=":", lw=1)
    axis.set(xlim=(-100, 120), ylim=(-0.5, 13), xlabel="time relative to equal-amplitude tone change (ms)",
             ylabel="effective Haas delay (ms)", title="100 Hz to 5 kHz: per-band spectral onset")
    axis.legend(frameon=False)
    fig.savefig(report_dir / "spectral-onset.png", dpi=170)
    plt.close(fig)

    selectivity = read_csv(raw_dir / "detector_selectivity.csv")
    selectivity_time = np.array([float(row["time_ms"]) for row in selectivity])
    steady_delay = np.array([float(row["steady_ms"]) for row in selectivity])
    transient_delay = np.array([float(row["transient_ms"]) for row in selectivity])
    steady_window = (selectivity_time >= -300) & (selectivity_time < 0)
    fig, axis = plt.subplots(figsize=(8, 3.3), layout="constrained")
    axis.plot(selectivity_time, steady_delay, color="#314e79", label="steady tremolo tone")
    axis.plot(selectivity_time, transient_delay, color="#ca7939", label="same tone + impulse")
    axis.axvline(0, color="0.4", ls=":", lw=1)
    axis.set(xlim=(-300, 140), ylim=(-0.5, 13), xlabel="time relative to impulse (ms)",
             ylabel="effective Haas delay (ms)", title="Transient detector selectivity")
    axis.legend(frameon=False)
    fig.savefig(report_dir / "detector-selectivity.png", dpi=170)
    plt.close(fig)

    velvet_bands = read_csv(raw_dir / "velvet_bands.csv")
    frequencies = [int(float(row["tone_hz"])) for row in velvet_bands]
    fig, axis = plt.subplots(figsize=(8, 3.3), layout="constrained")
    positions = np.arange(len(frequencies))
    for offset, key, label, color in (
        (-1, "low_only", "Low only", "#ca7939"),
        (0, "mid_only", "Mid only", "#569477"),
        (1, "high_only", "High only", "#314e79"),
    ):
        axis.bar(positions + offset * 0.24,
                 [float(row[key]) for row in velvet_bands], 0.22,
                 label=label, color=color)
    axis.set(xticks=positions, xticklabels=[f"{freq} Hz" for freq in frequencies],
             ylabel="Velvet side RMS", title="Frequency-selective Velvet input levels")
    axis.legend(frameon=False, ncol=3)
    fig.savefig(report_dir / "velvet-bands.png", dpi=170)
    plt.close(fig)

    mono = np.fromfile(raw_dir / "mono.raw", dtype="<f4").reshape(-1, 7)
    input_mono, off_l, off_r, on_l, on_r, guard_l, guard_r = mono.T
    off_side = 0.5 * (off_l - off_r)
    on_side = 0.5 * (on_l - on_r)
    guard_side = 0.5 * (guard_l - guard_r)
    off_mid = 0.5 * (off_l + off_r)
    on_mid = 0.5 * (on_l + on_r)
    added_side = on_side - off_side
    off_side_rms, on_side_rms, guard_side_rms, added_side_rms = map(
        rms, (off_side, on_side, guard_side, added_side)
    )
    guarded_added_rms = rms(guard_side - off_side)
    off_corr = float(np.corrcoef(off_l, off_r)[0, 1])
    on_corr = float(np.corrcoef(on_l, on_r)[0, 1])
    mid_error = rms(on_mid - off_mid)
    # Subtract the width-off render sample by sample, then add only Velvet's
    # contribution to the original mono signal. The Haas path cancels exactly.
    velvet_only_l = input_mono + (on_l - off_l)
    velvet_only_r = input_mono + (on_r - off_r)
    velvet_only_side = 0.5 * (velvet_only_l - velvet_only_r)
    velvet_only_mid_error = rms(0.5 * (velvet_only_l + velvet_only_r) - input_mono)
    velvet_only_corr = float(np.corrcoef(velvet_only_l, velvet_only_r)[0, 1])
    fig, axes = plt.subplots(1, 3, figsize=(12.5, 4.3), layout="constrained")
    points = np.arange(0, len(mono), 37)
    axes[0].scatter(off_l[points], off_r[points], s=3, alpha=0.15, color="#314e79")
    axes[0].set(title="Mono noise through Haas only", xlabel="left", ylabel="right", xlim=(-0.25, 0.25), ylim=(-0.25, 0.25))
    axes[1].scatter(on_l[points], on_r[points], s=3, alpha=0.15, color="#ca7939")
    axes[1].set(title="Same mono noise + Velvet 100%", xlabel="left", ylabel="right", xlim=(-0.25, 0.25), ylim=(-0.25, 0.25))
    axes[2].scatter(guard_l[points], guard_r[points], s=3, alpha=0.15, color="#569477")
    axes[2].set(title="Velvet 100% + Transient 100%", xlabel="left", ylabel="right", xlim=(-0.25, 0.25), ylim=(-0.25, 0.25))
    fig.savefig(report_dir / "mono-width.png", dpi=170)
    plt.close(fig)
    save_wav(report_dir / "mono-input.wav", [input_mono])
    save_wav(report_dir / "mono-haas-only.wav", [off_l, off_r])
    save_wav(report_dir / "mono-velvet-100.wav", [on_l, on_r])
    save_wav(report_dir / "mono-velvet-guard-100.wav", [guard_l, guard_r])
    fig, axes = plt.subplots(1, 2, figsize=(8.5, 4.3), layout="constrained")
    axes[0].scatter(input_mono[points], input_mono[points], s=3, alpha=0.15, color="#314e79")
    axes[0].set(title="Dry mono, no Haas", xlabel="left", ylabel="right")
    axes[1].scatter(velvet_only_l[points], velvet_only_r[points], s=3, alpha=0.15, color="#ca7939")
    axes[1].set(title="Velvet 100%, Haas removed", xlabel="left", ylabel="right")
    for axis in axes:
        axis.set(xlim=(-0.25, 0.25), ylim=(-0.25, 0.25))
        axis.set_aspect("equal")
    fig.savefig(report_dir / "mono-velvet-only.png", dpi=170)
    plt.close(fig)
    save_wav(report_dir / "mono-velvet-only.wav", [velvet_only_l, velvet_only_r])

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
        "A six-second mono synth pad plays C major (C4–E4–G4) for 3 s, then",
        "A minor (A3–C4–E4) for 3 s. Detuned oscillators and harmonics give",
        "each note a sustained pad timbre; the chord transition crossfades over",
        "125 ms. The two chords share two notes and are close in overall level.",
        "Center is 12 ms, Variability is ±6 ms, Max Rate is 10 ms/s,",
        "and Velvet Width is 0%. Smart Haas scores 30 delays in 2048-sample",
        "windows every 1024 samples, averages candidate scores over 600 ms,",
        "and requires a 5% improvement before changing its selected delay.",
        "The score also rewards side energy up to a 0.20 side/mid RMS target,",
        "so an unchanged mid at 0 ms does not automatically beat every widening",
        "candidate. Spectral error still limits how far the delay may move.",
        "The fixed comparison uses the same processor with Variability at 0 ms.",
        "The first 0.5 s and final 0.2 s of each chord are excluded from the steady-state table.",
        "The error is the mean of weighted absolute log FFT-magnitude ratios",
        "between input and latency-aligned processed mid, over 512-sample Hann",
        "windows; lower is better.",
        "",
        "![Smart Haas delay and spectrum comparison](smart-haas.png)",
        "",
        "Listen to the [pad input](smart-haas-pad-input.wav),",
        "[adaptive mid](smart-haas-pad-adaptive.wav), and",
        "[fixed-delay mid](smart-haas-pad-fixed.wav).",
        "",
        "| Content | Most common target | Target changes | Median active delay | Adaptive error | Fixed 12 ms error |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in adaptive_results:
        lines.append(f'| {row["label"]} | {row["mode"]:.1f} ms | {row["target_changes"]} | {row["median"]:.2f} ms | '
                     f'{row["adaptive"]:.3f} | {row["fixed"]:.3f} |')
    lines += [
        "",
        f'The C major and A minor input levels are {adaptive_results[0]["input_rms"]:.3f}',
        f'and {adaptive_results[1]["input_rms"]:.3f} RMS over the steady windows.',
        f'The selected target moves from {adaptive_results[0]["mode"]:.1f} ms on C major '
        f'to {adaptive_results[1]["mode"]:.1f} ms on A minor.',
        f'C major has {adaptive_results[0]["target_changes"]} target changes during its steady window; '
        f'A minor has {adaptive_results[1]["target_changes"]}. The target stays inside 6–18 ms;',
        f"the largest active-delay step is {max_rate:.1f} ms/s (10 ms/s setting).",
        f'Adaptive processing reduces the spectral error by '
        f'{100 * (1 - adaptive_results[0]["adaptive"] / adaptive_results[0]["fixed"]):.1f}% '
        f'on C major and {100 * (1 - adaptive_results[1]["adaptive"] / adaptive_results[1]["fixed"]):.1f}% '
        "on A minor against the fixed 12 ms delay in this pad test.",
        "",
        "A separate 12-to-20 ms Center Delay step shows the linear ramp used",
        "between selected targets. Max Rate bounds its constant slope.",
        "",
        "![Linear Haas delay transition](haas-linear.png)",
        "",
        f'The measured peak movement is {slew_peak_rate:.1f} ms/s with Max Rate at 10 ms/s.',
        "",
        "## Smart Haas: zero and negative delay",
        "",
        "A mono impulse at 5 ms runs through the processor with Variability and",
        "Velvet Width at 0%. Positive Center Delay shifts the right channel;",
        "negative Center Delay shifts the left, so the right leads. Both channels",
        "also receive the fixed 1.5 ms lookahead reported to the host. At 0 ms",
        "Haas delay, both channels remain aligned.",
        "",
        "![Signed Haas delay impulse timing](signed-haas-delay.png)",
        "",
        f'The +12 ms setting peaks at {signed_peaks["positive"][0]:.1f} ms left and '
        f'{signed_peaks["positive"][1]:.1f} ms right; 0 ms peaks at '
        f'{signed_peaks["zero"][0]:.1f} ms on both channels; −12 ms peaks at '
        f'{signed_peaks["negative"][0]:.1f} ms left and '
        f'{signed_peaks["negative"][1]:.1f} ms right.',
        "",
        "With Center at 0 ms and Variability at ±3 ms, a 523 Hz mono tone",
        f'selects {zero_preference["mono"]:.1f} ms; a stereo version with a 0.6 radian '
        f'channel phase offset selects {zero_preference["already-wide"]:.1f} ms. '
        "The width target therefore avoids an automatic 0 ms choice for mono",
        "but permits 0 ms when the input already has enough side energy.",
        "",
        "## Dynamic and Velvet Transient",
        "",
        "The shared onset detector compares fast and slow magnitude envelopes in four",
        "mid-channel frequency bands: below 250 Hz, 250 Hz–2.5 kHz, 2.5–8 kHz,",
        "and above 8 kHz. A band rise also needs sufficient overall attack",
        "energy or a strong rise in that band; this rejects small fluctuations",
        "in sustained material. As an equal-amplitude check, a 100 Hz",
        "tone switches to 5 kHz after 1 s; both tones are 0.18 peak amplitude.",
        "Haas Dynamic holds for 10 ms after the last detected onset reaches the output, then decays",
        "exponentially to 1% protection over 65 ms.",
        "Haas fades into protection over 1 ms for tonal onsets. High-crest",
        "impulses still switch immediately to keep their channels aligned.",
        "The audio path waits 1.5 ms while the detector reads ahead, so the",
        "tonal crossfade completes before the first output attack samples.",
        "Velvet Transient attenuates convolution input only while the detector",
        "reports an onset. Its depth",
        "fades smoothly near the onset threshold, with no fixed suppression interval.",
        "Attack sets a 0–10 ms linear ramp into attenuation while the",
        "detector remains active. Values below 0.5 ms use a 0.5 ms de-click",
        "ramp for tonal onsets; high-crest transients use the selected attack,",
        "including immediate 0 ms. Longer settings let more of a brief attack",
        "into Velvet's convolution history.",
        "",
        "![Equal-amplitude spectral onset](spectral-onset.png)",
        "",
        f"Dynamic at 100% moves from {before_switch:.2f} ms before the switch",
        f"to {after_switch:.2f} ms at the first measured block after it; Dynamic 0%",
        "remains at 12 ms. This checks the frequency-specific trigger independently",
        "of a broadband level increase.",
        "",
        "A 700 Hz tone with 15% amplitude modulation at 5 Hz is compared",
        "with the same tone plus a sharp impulse at 1 s. Dynamic is 100%.",
        "",
        "![Transient detector selectivity](detector-selectivity.png)",
        "",
        f'The steady tone stays above {np.min(steady_delay[steady_window]):.2f} ms '
        f'in the pre-impulse window, while the impulse takes the delay to '
        f'{np.min(transient_delay[(selectivity_time >= 0) & (selectivity_time < 20)]):.2f} ms.',
        "",
        "A 1.0-amplitude onset with a short decaying noise burst is placed in a",
        "0.002-amplitude white-noise bed. Haas Center is 12 ms and Variability is 0 ms.",
        "Dynamic is compared at 0% and 100% with Velvet Width at 0%.",
        "Separately, Velvet Width is 100% and Transient is compared at 0% and",
        "100%; the Haas-only side signal is subtracted to isolate Velvet's added side.",
        "",
        "![Transient timing and side-tail protection](transients.png)",
        "",
        f'- At the 1.5 ms output onset, the right sample is {columns["right_normal"][onset_index]:.4f} with Dynamic 0% '
        f'and {columns["right_kept"][onset_index]:.4f} with Dynamic 100% '
        f'(input onset {columns["input"][input_onset_index]:.4f}).',
        f'- Across the first 160 transient samples (3.33 ms), the maximum error '
        f'between the latency-aligned input and the Dynamic 100% right output is '
        f'{attack_error:.6f}.',
        "",
        "| Attack | Velvet-added side RMS, 4–28 ms | Reduction vs Transient 0% |",
        "| --- | ---: | ---: |",
        f'| Transient 0% | {no_guard:.5f} | — |',
        f'| 0 ms | {guard:.5f} | {100 * (1 - guard / no_guard):.1f}% |',
        f'| 0.5 ms | {guard_half_ms:.5f} | {100 * (1 - guard_half_ms / no_guard):.1f}% |',
        f'| 5 ms | {guard_five_ms:.5f} | {100 * (1 - guard_five_ms / no_guard):.1f}% |',
        "",
        "- The protected Haas path reaches 0 ms during the onset and returns",
        "  exponentially after its 10 ms hold; normal Haas stays at 12 ms.",
        f'- At {delay_time[release_end_index]:.1f} ms after the transient, the active delay is '
        f'{kept_delay[release_end_index]:.2f} ms, leaving {remaining_protection:.1f}% protection.',
        "",
        "### Haas Dynamic at a tonal onset",
        "",
        "A 0.2-amplitude 1 kHz mono sine starts at 0 ms. Haas Center is 12 ms,",
        "Variability and Velvet Width are 0%. The initial 10 ms of the right",
        "output are checked for abrupt sample-to-sample changes.",
        "",
        "![Haas tonal-onset de-click](haas-onset-declick.png)",
        "",
        f"The largest right-channel step with Dynamic 100% is {haas_step:.4f},",
        "versus 0.1414 before the onset crossfade. A sharp impulse still",
        "reaches the right channel after the fixed 1.5 ms lookahead with Dynamic 100%.",
        "",
        "### Velvet Transient at short attacks",
        "",
        "A steady 1 kHz mono tone starts at 0 ms with Haas Center and Variability",
        "at 0 ms, Velvet Width and Transient at 100%. The plot compares",
        "0, 0.5, 2, and 3 ms Attack settings and measures 8–20 kHz",
        "side energy from 15–40 ms. Similar results indicate the tightened",
        "detector leaves the sustained tone largely unguarded.",
        "",
        "![Velvet Transient short-attack noise check](guard-short-attack.png)",
        "",
        f'At 0.5 ms attack, high-frequency side RMS is {guard_hf["fast_guard_side"]:.6f}; '
        f'at 3 ms it is {guard_hf["slow_guard_side"]:.6f} '
        f'({100 * (guard_hf["fast_guard_side"] / guard_hf["slow_guard_side"] - 1):.1f}% higher). '
        f'At 0 ms it is {guard_hf["instant_guard_side"]:.6f}; at 2 ms it is '
        f'{guard_hf["medium_guard_side"]:.6f}.',
        "",
        "## Velvet Width: widening a mono signal",
        "",
        "A 1.5 s mono, low-pass-filtered white-noise excerpt is processed with",
        "the same fixed 12 ms Haas delay: Velvet Width at 0%, at 100% with",
        "Transient 0%, and at 100% with Transient 100%.",
        "The Haas-only render is the baseline; the change between renders is Velvet.",
        "",
        "![Mono noise stereo scatter comparison](mono-width.png)",
        "",
        f'- Side RMS rises from {off_side_rms:.5f} to {on_side_rms:.5f} '
        f'({100 * (on_side_rms / off_side_rms - 1):.1f}% increase).',
        f'- Velvet itself adds {added_side_rms:.5f} side RMS while the mid difference '
        f'between renders is {mid_error:.7f} RMS.',
        f'- With Transient 100%, side RMS is {guard_side_rms:.5f} and the Velvet-added '
        f'side RMS is {guarded_added_rms:.5f}; sustained width remains audible.',
        f'- Left/right correlation changes from {off_corr:.3f} to {on_corr:.3f}.',
        "- Listen to [mono input](mono-input.wav), [Haas only](mono-haas-only.wav),",
        "  [Haas + Velvet 100%](mono-velvet-100.wav), and",
        "  [Haas + Velvet 100% + Transient 100%](mono-velvet-guard-100.wav).",
        "",
        "For the Velvet-only comparison, subtract the Width 0% render from the",
        "Width 100% render sample by sample, then add that residual to the dry",
        "mono input. This cancels the Haas path while retaining the processor's",
        "actual Velvet output.",
        "",
        "![Mono width with Haas removed](mono-velvet-only.png)",
        "",
        f'- With Haas removed, Velvet produces {rms(velvet_only_side):.5f} side RMS '
        f'and left/right correlation {velvet_only_corr:.3f}; the dry input has zero side RMS.',
        f'- The Velvet-only mid differs from the dry mono input by '
        f'{velvet_only_mid_error:.7f} RMS.',
        "- Listen to the [Velvet-only render](mono-velvet-only.wav).",
        "",
        "### Velvet input bands",
        "",
        "The input to Velvet is split below 250 Hz, from 250 Hz to 2.5 kHz,",
        "and above 2.5 kHz. The three sliders scale those bands before the",
        "convolution. They range from 0% to 200% and default to 100%.",
        "Each bar measures the side output from one slider at 100%,",
        "with the other two at 0%, using an 80 Hz, 700 Hz, or 5 kHz mono tone.",
        "",
        "![Velvet input band controls](velvet-bands.png)",
        "",
        f'At 200%, the low, mid, and high solo-band side outputs are '
        f'{float(velvet_bands[0]["low_200"]) / float(velvet_bands[0]["low_only"]):.2f}×, '
        f'{float(velvet_bands[1]["mid_200"]) / float(velvet_bands[1]["mid_only"]):.2f}×, and '
        f'{float(velvet_bands[2]["high_200"]) / float(velvet_bands[2]["high_only"]):.2f}× '
        "their 100% levels, respectively.",
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
