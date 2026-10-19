#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "OversampledTransformer.h"
#include "HighTilt.h"

namespace character
{
// The order here matches the attack, release, weight, and state arrays below.
enum class Model { fet, vca, variMu, optical };

struct Parameters
{
    // All levels/times use the host control units. mix is normalized to 0..1;
    // sidechainHz == 0 disables the detector high-pass.
    Model model = Model::fet;
    float inputDb = 0, thresholdDb = -18, ratio = 4, attackMs = 10;
    float releaseMs = 150, kneeDb = 6, outputDb = 0, mix = 1;
    float sidechainHz = 80, driveDb = 6, stereoLink = 100, evenHarmonics = 0, highTiltDb = 0;
    float variTransient = 0;
    bool transformer = false, bypass = false, upward = false, midSide = false;
};

// No allocation, locks, host calls, or GUI dependencies in the processing path.
// Both channels share the same detector and gain to preserve the stereo image.
class Compressor
{
public:
    static constexpr int latencySamples = OversampledTransformer::latencySamples;

    // Convert between the linear signal/envelope domain and the dB control
    // domain. The floor keeps silence and tiny numerical residues finite.
    static float gain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
    static float db (float level) noexcept
    {
        return 20.0f * std::log10 (std::max (level, 1.0e-10f));
    }

    void prepare (double newSampleRate) noexcept
    {
        // Ignore invalid host rates. Prepare filters once here so the audio
        // callback only runs fixed-coefficient, fixed-storage DSP.
        sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 8000
                   ? newSampleRate : 44100;
        smoothing = coefficient (20);
        for (auto& t : transformers)
            t.prepare (sampleRate);
        for (auto& shelf : sidechainShelves)
            shelf.prepare (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        // Clear detector memories, coloration filters, and latency lines when
        // the host starts a new playback or explicitly resets the processor.
        states = {};
        for (auto& t : transformers)
            t.reset();
        for (auto& shelf : sidechainShelves)
            shelf.reset();
        cleanDelay = bypassDelay = {};
        delayPosition = 0;
        hpInput = hpOutput = {};
        meterReduction = 0;
        firstSet = true;
    }

    void setParameters (Parameters p) noexcept
    {
        // Clamp host/state input before it reaches logarithms, time constants,
        // filter coefficients, or nonlinear processing.
        const auto safe = [] (float v, float lo, float hi, float fallback)
        { return std::isfinite (v) ? std::clamp (v, lo, hi) : fallback; };
        p.inputDb = safe (p.inputDb, -24, 24, 0);
        p.thresholdDb = safe (p.thresholdDb, -60, 0, -18);
        p.ratio = safe (p.ratio, 1, 20, 4);
        if (p.upward)
            p.ratio = std::min (p.ratio, 2.0f);
        p.attackMs = safe (p.attackMs, 0.05f, 100, 10);
        p.releaseMs = safe (p.releaseMs, 10, 2000, 150);
        p.kneeDb = safe (p.kneeDb, 0, 24, 6);
        p.outputDb = safe (p.outputDb, -24, 24, 0);
        p.mix = safe (p.mix, 0, 1, 1);
        p.sidechainHz = safe (p.sidechainHz, 0, 300, 80);
        p.driveDb = safe (p.driveDb, 0, 18, 6);
        p.stereoLink = safe (p.stereoLink, 0, 100, 100);
        p.evenHarmonics = safe (p.evenHarmonics, 0, 100, 0);
        p.highTiltDb = safe (p.highTiltDb, 0, 12, 0);
        p.variTransient = safe (p.variTransient, 0, 400, 0);
        selected = std::clamp (static_cast<int> (p.model), 0, 3);
        targets = { gain (p.inputDb), p.thresholdDb, p.ratio, p.kneeDb,
                    gain (p.outputDb), p.mix, gain (p.driveDb),
                    p.transformer ? 1.0f : 0.0f, p.bypass ? 1.0f : 0.0f,
                    p.highTiltDb, p.upward ? 1.0f : 0.0f,
                    p.stereoLink * 0.01f, p.midSide ? 1.0f : 0.0f,
                    p.evenHarmonics * 0.01f, p.variTransient * 0.0025f };
        // This one-pole HPF affects only the detector. High Tilt is a separate
        // shelf applied to the signed sidechain after this HPF.
        hpEnabled = p.sidechainHz > 0;
        hpCoefficient = std::exp (-2 * pi * p.sidechainHz / static_cast<float> (sampleRate));

        // Array order is FET, VCA, variable-mu, optical. Relative to the Attack
        // control they use 0.25x, 1x, 1.5x, and 2x envelope constants. Releases
        // use 0.7x, 1x, 1.6x, and 1x; mu and optical add program-dependent memory.
        attacks = { coefficient (p.attackMs * 0.25f), coefficient (p.attackMs),
                    coefficient (p.attackMs * 1.5f), coefficient (p.attackMs * 2) };
        releases = { coefficient (p.releaseMs * 0.7f), coefficient (p.releaseMs),
                     coefficient (p.releaseMs * 1.6f), coefficient (p.releaseMs) };
        slowRelease = coefficient (p.releaseMs * 6);
        rmsCoefficient = coefficient (10);
        if (firstSet)
        {
            // Start at the selected model/control values rather than ramping
            // from constructor defaults on the first audio block.
            values = targets;
            weights = {};
            weights[static_cast<size_t> (selected)] = 1;
            firstSet = false;
        }
    }

    float reductionDb() const noexcept { return meterReduction; }

    void process (float* const* channels, int channelCount, int frames) noexcept
    {
        // Process in sample order so envelope and filter states remain
        // continuous across host blocks of any size.
        channelCount = std::clamp (channelCount, 0, 2);
        if (channelCount == 0 || frames <= 0)
            return;

        for (auto& shelf : sidechainShelves)
            shelf.setGainDb (values[highTilt]);
        meterReduction = 0;
        for (int sample = 0; sample < frames; ++sample)
        {
            for (size_t i = 0; i < values.size(); ++i)
            {
                values[i] = targets[i] + smoothing * (values[i] - targets[i]);
                // Float one-poles otherwise stall just short of the target,
                // especially at long time constants and high sample rates.
                if (std::abs (values[i] - targets[i]) < 0.001f * std::max (1.0f, std::abs (targets[i])))
                    values[i] = targets[i];
            }
            for (size_t m = 0; m < weights.size(); ++m)
                weights[m] = (1 - smoothing) * (static_cast<int> (m) == selected ? 1.0f : 0.0f)
                           + smoothing * weights[m];

            std::array<float, 2> dry {}, driven {}, sensedDetector {};
            std::array<float, 2> detector {};
            const auto encode = values[midSide] * 0.7071067811865475f;
            for (int ch = 0; ch < channelCount; ++ch)
            {
                const auto c = static_cast<size_t> (ch);
                auto raw = std::isfinite (channels[ch][sample]) ? channels[ch][sample] : 0.0f;
                if (channelCount == 2 && values[midSide] > 0)
                {
                    const auto other = std::isfinite (channels[1 - ch][sample]) ? channels[1 - ch][sample] : 0.0f;
                    // Orthonormal M/S encode: M = (L + R) / sqrt(2), S = (L - R) / sqrt(2).
                    raw = raw * (1.0f - values[midSide])
                        + (ch == 0 ? raw + other : other - raw) * encode;
                }
                dry[c] = raw;
                // Input gain feeds the compressor branch. The untouched input
                // is retained separately for the wet/dry mix and host bypass.
                driven[c] = std::clamp (dry[c] * values[input], -64.0f, 64.0f);
                const auto hp = driven[c] - hpInput[c] + hpCoefficient * hpOutput[c];
                hpInput[c] = driven[c];
                hpOutput[c] = hp;
                const auto sidechain = hpEnabled ? hp : driven[c];
                // Tilt only the signed detector signal, before rectification
                // and stereo linking. Keep filter history warm when disabled.
                const auto sensed = sidechainShelves[c].process (sidechain);
                // Peak-link the processed left/right detector signals. Every
                // model then sees the same control envelope, preserving stereo.
                sensedDetector[c] = std::abs (sensed);
            }
            const auto linkedPeak = channelCount == 2 ? std::max (sensedDetector[0], sensedDetector[1])
                                                      : sensedDetector[0];
            for (int ch = 0; ch < channelCount; ++ch)
                detector[static_cast<size_t> (ch)] = sensedDetector[static_cast<size_t> (ch)]
                    + values[stereoLink] * (linkedPeak - sensedDetector[static_cast<size_t> (ch)]);

            // Run every model continuously and fade their gain contributions
            // using the 20 ms model weights. Switching models won't start a cold
            // detector or cause an abrupt change in the audio path.
            std::array<float, 2> combinedGain {}, reduction {};
            for (int ch = 0; ch < channelCount; ++ch)
            for (size_t m = 0; m < states[0].size(); ++m)
            {
                const auto c = static_cast<size_t> (ch);
                auto& s = states[c][m];
                // Feedback senses the previous compressed sidechain sample, before
                // makeup gain and transformer. This gives a causal one-sample loop.
                // FET and optical use feed-forward input level. VCA and mu use
                // their own previous gain-reduced detector value as feedback.
                const auto feedbackMix = (m == 1 || m == 2) ? 1.0f - values[upward] : 0.0f;
                const auto sensed = detector[c] + feedbackMix * (s.feedback - detector[c]);
                s.energy = rmsCoefficient * s.energy + (1 - rmsCoefficient) * sensed * sensed;
                // FET/VCA use peak level. Mu/optical use a 10 ms exponential
                // mean-square detector, converted back to RMS amplitude.
                const auto level = db ((m == 2 || m == 3) ? std::sqrt (s.energy) : sensed);
                const auto over = level - values[threshold];
                const auto ratioMaximum = values[upward] > 0.0f ? 2.0f : 20.0f;
                float effectiveRatio = std::clamp (values[ratio], 1.0f, ratioMaximum);
                if (m == 2)
                {
                    // Mu ratio rises from 1:1 toward its maximum on the active
                    // side of threshold: above for downward, below for upward.
                    const auto activeOver = over * (1 - 2 * values[upward]);
                    effectiveRatio = 1 + (effectiveRatio - 1)
                                       * (1 - std::exp (-std::max (activeOver, 0.0f) / 12));
                }

                // Feed-forward curves use slope 1 - 1/R. Feedback curves use
                // R - 1 to compensate for the level reduction returning through
                // the detector; this keeps the steady-state ratio near R:1.
                const auto slope = (m == 1 || m == 2) ? effectiveRatio - 1
                                                                     : 1 - 1 / effectiveRatio;
                const auto downward = std::clamp (slope * softKnee (over, values[knee]), 0.0f, 80.0f);
                // Upward compression holds the above-threshold region at unity
                // gain and raises quieter levels toward threshold with a
                // below-threshold slope of 1/R. Negative reduction is gain.
                const auto upwardSlope = 1 - 1 / effectiveRatio;
                const auto upwardGain = std::max (-12.0f, -upwardSlope * softKnee (-over, values[knee]));
                const auto desired = downward * (1 - values[upward]) + upwardGain * values[upward];
                auto attack = attacks[m];
                // Keep the discrete feedback loop stable at high ratios / low rates.
                if (m == 1 || m == 2)
                    // Bound the feedback attack coefficient for stability at
                    // high ratios, where a one-sample loop can otherwise ring.
                    attack = std::max (attack, 1 - 0.5f / effectiveRatio);
                auto release = releases[m];
                if (m == 2)
                    // Mu recovery gets slower as stored gain reduction deepens.
                    release = std::pow (release, 1 / (1 + std::abs (s.reduction) * 0.06f));
                // VariTransient uses a 3 dB soft knee from 3 to 6 dB GR.
                // At 400%, attack reaches 4x and release reaches 1/4x.
                auto transientFactor = 1.0f;
                if (values[upward] < 0.5f)
                {
                    const auto kneePosition = std::clamp ((std::abs (s.reduction) - 3.0f) / 3.0f, 0.0f, 1.0f);
                    const auto grWeight = kneePosition * kneePosition * (3.0f - 2.0f * kneePosition);
                    transientFactor += 3.0f * values[variTransient] * grWeight;
                }
                attack = std::pow (attack, 1.0f / transientFactor);
                release = std::pow (release, transientFactor);
                // In upward mode the control state goes negative as gain rises,
                // so attack/release must compare gain magnitude, not sign.
                const auto a = std::abs (desired) > std::abs (s.reduction) ? attack : release;
                s.reduction = desired + a * (s.reduction - desired);
                auto gr = s.reduction;
                if (m == 3)
                {
                    // Optical combines its main envelope with a slower memory:
                    // the latter recovers at six times the Release setting.
                    const auto slowAttack = std::pow (attacks[m], 1.0f / transientFactor);
                    const auto slowRecovery = std::pow (slowRelease, transientFactor);
                    const auto slow = std::abs (desired) > std::abs (s.memory) ? slowAttack : slowRecovery;
                    s.memory = desired + slow * (s.memory - desired);
                    gr = 0.65f * gr + 0.35f * s.memory;
                }
                const auto g = gain (-gr);
                // Store this model's previous compressed detector level for
                // the next feedback-model sample. Makeup gain is not in the loop.
                s.feedback = detector[c] * g;
                combinedGain[c] += weights[m] * g;
                reduction[c] += weights[m] * gr;
            }
            for (int ch = 0; ch < channelCount; ++ch)
                meterReduction = std::max (meterReduction, std::abs (reduction[static_cast<size_t> (ch)]) * (1 - values[bypass]));

            for (int ch = 0; ch < channelCount; ++ch)
            {
                const auto c = static_cast<size_t> (ch);
                // Mix the compressed/input-gained path with the original input,
                // then apply output gain. Transformer coloration follows both.
                const auto mixed = (dry[c] + values[mix] * (driven[c] * combinedGain[c] - dry[c]))
                                 * values[output];
                // Keep the oversampler warm even when disabled; the clean and
                // bypass paths retain the same fixed latency during automation.
                const auto coloured = transformers[c].process (mixed, values[drive], values[evenHarmonics]);
                // Delay the clean branch to match the oversampler's 64 samples.
                // Bypass has its own equally delayed original-input line.
                const auto clean = cleanDelay[c][delayPosition];
                const auto delayedDry = bypassDelay[c][delayPosition];
                cleanDelay[c][delayPosition] = mixed;
                bypassDelay[c][delayPosition] = dry[c];
                // Blend transformer coloration independently, after the mix
                // and output gain. High Tilt changes detector sensitivity only.
                const auto wet = clean + values[transformer] * (coloured - clean);
                // Snap the last fraction to ensure settled bypass is sample exact.
                channels[ch][sample] = values[bypass] > 0.99999f ? delayedDry
                                    : wet + values[bypass] * (delayedDry - wet);
            }
            if (channelCount == 2 && values[midSide] > 0)
            {
                // Reverse M/S matrix restores L/R after compression and output processing.
                const auto left = channels[0][sample], right = channels[1][sample];
                const auto decodedLeft = (left + right) * 0.7071067811865475f;
                const auto decodedRight = (left - right) * 0.7071067811865475f;
                channels[0][sample] = left * (1.0f - values[midSide]) + decodedLeft * values[midSide];
                channels[1][sample] = right * (1.0f - values[midSide]) + decodedRight * values[midSide];
            }
            delayPosition = (delayPosition + 1) % static_cast<size_t> (latencySamples);
        }
    }

private:
    static constexpr float pi = 3.14159265358979323846f;
    // Indexes into the smoothed value/target arrays above.
    enum Control { input, threshold, ratio, knee, output, mix, drive, transformer, bypass, highTilt, upward, stereoLink, midSide, evenHarmonics, variTransient };

    struct Detector
    {
        float reduction = 0; // Main envelope, in dB of gain reduction.
        float energy = 0;    // Mean-square level used by the RMS detector models.
        float feedback = 0;  // Previous gain-reduced sidechain amplitude.
        float memory = 0;    // Optical model's slower recovery envelope, in dB.
    };

    float coefficient (float ms) const noexcept
    {
        // One-pole time coefficient: exp(-1 / (time in seconds * sample rate)).
        return std::exp (-1.0f / (std::max (ms, 0.02f) * 0.001f * static_cast<float> (sampleRate)));
    }

    static float softKnee (float over, float width) noexcept
    {
        // Continuous quadratic knee: zero below -K/2, linear above +K/2,
        // with a parabola joining the two regions. width is in dB.
        if (width < 0.001f)
            return std::max (over, 0.0f);
        if (over <= -width * 0.5f) return 0;
        if (over >= width * 0.5f) return over;
        const auto x = over + width * 0.5f;
        return x * x / (2 * width);
    }

    double sampleRate = 44100;
    bool firstSet = true, hpEnabled = true;
    int selected = 0;
    float smoothing = 0, hpCoefficient = 0, slowRelease = 0, rmsCoefficient = 0;
    float meterReduction = 0;
    std::array<float, 15> values {}, targets {};
    std::array<float, 4> weights {}, attacks {}, releases {};
    std::array<std::array<Detector, 4>, 2> states {};
    std::array<OversampledTransformer, 2> transformers {};
    std::array<HighTilt, 2> sidechainShelves {};
    std::array<std::array<float, latencySamples>, 2> cleanDelay {}, bypassDelay {};
    size_t delayPosition = 0;
    std::array<float, 2> hpInput {}, hpOutput {};
};
} // namespace character
