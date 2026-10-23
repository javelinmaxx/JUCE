#pragma once
#include "../../YinyangCompressor/Source/OversampledTransformer.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ren
{
// Yinyang's original transformer is used directly: no compressor, detector,
// circuit approximation, or additional oversampling stage is inserted here.
class TransformerStage
{
public:
    static constexpr int latency = character::OversampledTransformer::latencySamples;
    void prepare (double rate)
    {
        for (auto& transformer : transformers) transformer.prepare (rate);
        cleanHistory = {}; bypassHistory = {}; position = 0;
        for (auto* smoother : { &drive, &bias, &output, &mix, &enabledBlend, &bypassBlend })
            smoother->reset (rate, 0.02);
        first = true; running = requested = false; warmSamples = 0;
    }
    bool isRunning() const noexcept { return running; }
    void process (juce::AudioBuffer<float>& buffer, float driveDb, float dcBias,
                  float outputDb, float amount, bool enabled, bool bypassed) noexcept
    {
        // The nonlinear stage cannot contribute at zero Mix.
        // Existing fade-out and warm-up handling also applies to these states.
        enabled = enabled && amount > 0.0f;
        const auto set = [this] (auto& smoother, float target)
        {
            if (first) smoother.setCurrentAndTargetValue (target);
            else smoother.setTargetValue (target);
        };
        set (drive, juce::Decibels::decibelsToGain (driveDb)); set (bias, dcBias);
        set (output, juce::Decibels::decibelsToGain (outputDb)); set (mix, amount);
        set (bypassBlend, bypassed ? 1.0f : 0.0f);
        if (first)
        {
            running = enabled;
            enabledBlend.setCurrentAndTargetValue (enabled ? 1.0f : 0.0f);
        }
        else if (enabled && ! requested)
        {
            if (! running)
            {
                // Fill the transformer's latency before fading back from clean.
                for (auto& transformer : transformers) transformer.reset();
                running = true; warmSamples = latency;
                enabledBlend.setCurrentAndTargetValue (0.0f);
            }
            else enabledBlend.setTargetValue (1.0f);
        }
        if (! enabled) { warmSamples = 0; enabledBlend.setTargetValue (0.0f); }
        requested = enabled; first = false;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (enabled && warmSamples > 0 && --warmSamples == 0) enabledBlend.setTargetValue (1.0f);
            const auto core = drive.getNextValue(), asymmetry = bias.getNextValue();
            const auto gain = output.getNextValue(), wet = mix.getNextValue();
            const auto colour = enabledBlend.getNextValue(), bypass = bypassBlend.getNextValue();
            for (int channel = 0; channel < std::min (2, buffer.getNumChannels()); ++channel)
            {
                const auto c = static_cast<size_t> (channel);
                const auto original = buffer.getSample (channel, i);
                const auto input = std::isfinite (original) ? std::clamp (original, -32.0f, 32.0f) : 0.0f;
                const auto clean = cleanHistory[c][position], dry = bypassHistory[c][position];
                cleanHistory[c][position] = input * gain; bypassHistory[c][position] = input;
                const auto coloured = running ? transformers[c].process (input * gain, core, asymmetry) : clean;
                const auto processed = clean + wet * colour * (coloured - clean);
                buffer.setSample (channel, i, bypass >= 1.0f ? dry : processed + bypass * (dry - processed));
            }
            position = (position + 1) % static_cast<size_t> (latency);
            if (! enabled && ! enabledBlend.isSmoothing()) running = false;
        }
    }
private:
    std::array<character::OversampledTransformer, 2> transformers;
    std::array<std::array<float, latency>, 2> cleanHistory {}, bypassHistory {};
    size_t position = 0;
    int warmSamples = 0;
    bool first = true, running = false, requested = false;
    juce::SmoothedValue<float> drive, bias, output, mix, enabledBlend, bypassBlend;
};
}
