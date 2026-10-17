#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace character
{
enum class Model { fet, vca, variMu, optical };

struct Parameters
{
    Model model = Model::fet;
    float inputDb = 0, thresholdDb = -18, ratio = 4, attackMs = 10;
    float releaseMs = 150, kneeDb = 6, outputDb = 0, mix = 1;
    float sidechainHz = 80, driveDb = 6;
    bool transformer = false, bypass = false;
};

// No allocation, locks, host calls, or GUI dependencies in the processing path.
// Both channels share the same detector and gain to preserve the stereo image.
class Compressor
{
public:
    static float gain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
    static float db (float level) noexcept
    {
        return 20.0f * std::log10 (std::max (level, 1.0e-10f));
    }

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 8000
                   ? newSampleRate : 44100;
        smoothing = coefficient (20);
        reset();
    }

    void reset() noexcept
    {
        states = {};
        transformers = {};
        hpInput = hpOutput = {};
        meterReduction = 0;
        firstSet = true;
    }

    void setParameters (Parameters p) noexcept
    {
        const auto safe = [] (float v, float lo, float hi, float fallback)
        { return std::isfinite (v) ? std::clamp (v, lo, hi) : fallback; };
        p.inputDb = safe (p.inputDb, -24, 24, 0);
        p.thresholdDb = safe (p.thresholdDb, -60, 0, -18);
        p.ratio = safe (p.ratio, 1, 20, 4);
        p.attackMs = safe (p.attackMs, 0.05f, 100, 10);
        p.releaseMs = safe (p.releaseMs, 10, 2000, 150);
        p.kneeDb = safe (p.kneeDb, 0, 24, 6);
        p.outputDb = safe (p.outputDb, -24, 24, 0);
        p.mix = safe (p.mix, 0, 1, 1);
        p.sidechainHz = safe (p.sidechainHz, 0, 300, 80);
        p.driveDb = safe (p.driveDb, 0, 18, 6);
        selected = std::clamp (static_cast<int> (p.model), 0, 3);
        targets = { gain (p.inputDb), p.thresholdDb, p.ratio, p.kneeDb,
                    gain (p.outputDb), p.mix, gain (p.driveDb),
                    p.transformer ? 1.0f : 0.0f, p.bypass ? 1.0f : 0.0f };
        hpEnabled = p.sidechainHz > 0;
        hpCoefficient = std::exp (-2 * pi * p.sidechainHz / static_cast<float> (sampleRate));
        attacks = { coefficient (p.attackMs * 0.25f), coefficient (p.attackMs),
                    coefficient (p.attackMs * 1.5f), coefficient (p.attackMs * 2) };
        releases = { coefficient (p.releaseMs * 0.7f), coefficient (p.releaseMs),
                     coefficient (p.releaseMs * 1.6f), coefficient (p.releaseMs) };
        slowRelease = coefficient (p.releaseMs * 6);
        rmsCoefficient = coefficient (10);
        if (firstSet)
        {
            values = targets;
            weights = {};
            weights[static_cast<size_t> (selected)] = 1;
            firstSet = false;
        }
    }

    float reductionDb() const noexcept { return meterReduction; }

    void process (float* const* channels, int channelCount, int frames) noexcept
    {
        channelCount = std::clamp (channelCount, 0, 2);
        if (channelCount == 0 || frames <= 0)
            return;

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

            std::array<float, 2> dry {}, driven {};
            float detector = 0;
            for (int ch = 0; ch < channelCount; ++ch)
            {
                const auto c = static_cast<size_t> (ch);
                const auto raw = channels[ch][sample];
                dry[c] = std::isfinite (raw) ? raw : 0;
                driven[c] = std::clamp (dry[c] * values[input], -64.0f, 64.0f);
                const auto hp = driven[c] - hpInput[c] + hpCoefficient * hpOutput[c];
                hpInput[c] = driven[c];
                hpOutput[c] = hp;
                detector = std::max (detector, std::abs (hpEnabled ? hp : driven[c]));
            }

            float combinedGain = 0, reduction = 0;
            for (size_t m = 0; m < states.size(); ++m)
            {
                auto& s = states[m];
                // Feedback senses the previous compressed sidechain sample, before
                // makeup gain and transformer. This gives a causal one-sample loop.
                const auto sensed = (m == 1 || m == 2) ? s.feedback : detector;
                s.energy = rmsCoefficient * s.energy + (1 - rmsCoefficient) * sensed * sensed;
                const auto level = db ((m == 2 || m == 3) ? std::sqrt (s.energy) : sensed);
                const auto over = level - values[threshold];
                float effectiveRatio = values[ratio];
                if (m == 2)
                    effectiveRatio = 1 + (effectiveRatio - 1)
                                       * (1 - std::exp (-std::max (over, 0.0f) / 12));
                const auto slope = (m == 1 || m == 2) ? effectiveRatio - 1
                                                                     : 1 - 1 / effectiveRatio;
                const auto desired = std::clamp (slope * softKnee (over, values[knee]), 0.0f, 80.0f);
                auto attack = attacks[m];
                // Keep the discrete feedback loop stable at high ratios / low rates.
                if (m == 1 || m == 2)
                    attack = std::max (attack, 1 - 0.5f / values[ratio]);
                auto release = releases[m];
                if (m == 2)
                    release = std::pow (release, 1 / (1 + s.reduction * 0.06f));
                const auto a = desired > s.reduction ? attack : release;
                s.reduction = desired + a * (s.reduction - desired);
                auto gr = s.reduction;
                if (m == 3)
                {
                    const auto slow = desired > s.memory ? attacks[m] : slowRelease;
                    s.memory = desired + slow * (s.memory - desired);
                    gr = 0.65f * gr + 0.35f * s.memory;
                }
                const auto g = gain (-gr);
                s.feedback = detector * g;
                combinedGain += weights[m] * g;
                reduction += weights[m] * gr;
            }
            meterReduction = std::max (meterReduction, reduction * (1 - values[bypass]));

            for (int ch = 0; ch < channelCount; ++ch)
            {
                const auto c = static_cast<size_t> (ch);
                const auto mixed = (dry[c] + values[mix] * (driven[c] * combinedGain - dry[c]))
                                 * values[output];
                const auto coloured = transformerSample (mixed, transformers[c]);
                const auto wet = mixed + values[transformer] * (coloured - mixed);
                // Snap the last fraction to ensure settled bypass is sample exact.
                channels[ch][sample] = values[bypass] > 0.99999f ? dry[c]
                                    : wet + values[bypass] * (dry[c] - wet);
            }
        }
    }

private:
    static constexpr float pi = 3.14159265358979323846f;
    enum Control { input, threshold, ratio, knee, output, mix, drive, transformer, bypass };
    struct Detector { float reduction = 0, energy = 0, feedback = 0, memory = 0; };
    struct Transformer { float flux = 0, lowpass = 0, previous = 0, dc = 0; };

    float coefficient (float ms) const noexcept
    {
        return std::exp (-1.0f / (std::max (ms, 0.02f) * 0.001f * static_cast<float> (sampleRate)));
    }

    static float softKnee (float over, float width) noexcept
    {
        if (width < 0.001f)
            return std::max (over, 0.0f);
        if (over <= -width * 0.5f) return 0;
        if (over >= width * 0.5f) return over;
        const auto x = over + width * 0.5f;
        return x * x / (2 * width);
    }

    float transformerSample (float x, Transformer& s) const noexcept
    {
        // Phenomenological iron coloration: low-frequency flux memory modulates
        // asymmetric saturation, followed by bandwidth shaping and DC rejection.
        // Not a measured circuit model; high-drive aliasing is a prototype limit.
        const auto fs = static_cast<float> (sampleRate);
        const auto fluxA = std::exp (-2 * pi * 60 / fs);
        s.flux = x + fluxA * (s.flux - x);
        const auto bias = 0.12f * std::tanh (s.flux * values[drive]);
        const auto b = std::tanh (bias);
        const auto shaped = (std::tanh (values[drive] * x + bias) - b)
                          / (values[drive] * (1 - b * b));
        const auto hfA = std::exp (-2 * pi * std::min (18000.0f, fs * 0.45f) / fs);
        s.lowpass = shaped + hfA * (s.lowpass - shaped);
        const auto dcA = std::exp (-2 * pi * 5 / fs);
        s.dc = s.lowpass - s.previous + dcA * s.dc;
        s.previous = s.lowpass;
        return s.dc;
    }

    double sampleRate = 44100;
    bool firstSet = true, hpEnabled = true;
    int selected = 0;
    float smoothing = 0, hpCoefficient = 0, slowRelease = 0, rmsCoefficient = 0;
    float meterReduction = 0;
    std::array<float, 9> values {}, targets {};
    std::array<float, 4> weights {}, attacks {}, releases {};
    std::array<Detector, 4> states {};
    std::array<Transformer, 2> transformers {};
    std::array<float, 2> hpInput {}, hpOutput {};
};
} // namespace character
