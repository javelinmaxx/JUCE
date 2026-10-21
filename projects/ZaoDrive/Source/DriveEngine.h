#pragma once
#include "Circuits.h"
#include "../../CharacterCompressor/Source/OversampledTransformer.h"
#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace zao
{
struct Settings
{
    float inputDb = 0, driveDb = 12, toneHz = 10000, outputDb = -6, mix = 1;
    float coreDriveDb = 6, dcBias = 0;
    int model = 0;
    bool transformer = false, bypass = false;
};

class DriveEngine
{
public:
    void prepare (double rate, int maximumBlock)
    {
        sampleRate = rate;
        blockSize = std::max (1, maximumBlock);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (static_cast<size_t> (blockSize));
        circuitLatency = juce::roundToInt (oversampling->getLatencyInSamples());
        scratch.setSize (2, blockSize);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            bjt[ch].prepare (rate * 4); opamp[ch].prepare (rate * 4);
            transformers[ch].prepare (rate);
            dryDelay[ch].assign (static_cast<size_t> (std::max (1, circuitLatency)), 0.0f);
            bypassDelay[ch].assign (static_cast<size_t> (getLatency()), 0.0f);
        }
        cleanDelay = {}; toneState = {};
        circuitPosition = transformerPosition = bypassPosition = 0;
        for (auto* smoother : { &inputGain, &driveGain, &toneCutoff, &outputGain, &wetMix,
                                 &coreGain, &bias, &transformerBlend, &bypassBlend })
            smoother->reset (rate, 0.02);
        modelBlend.reset (rate * 4, 0.01);
        firstSettings = true; transformerRunning = false; warmSamples = 0;
    }

    int getLatency() const noexcept { return circuitLatency + character::OversampledTransformer::latencySamples; }

    void process (juce::AudioBuffer<float>& buffer, const Settings& settings)
    {
        setSettings (settings);
        const auto channels = std::min (2, buffer.getNumChannels());
        for (int offset = 0; offset < buffer.getNumSamples(); offset += blockSize)
        {
            const auto count = std::min (blockSize, buffer.getNumSamples() - offset);
            for (int i = 0; i < count; ++i)
            {
                const auto gain = inputGain.getNextValue() * driveGain.getNextValue() * 0.35f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto input = ch < channels ? buffer.getSample (ch, offset + i) : 0.0f;
                    scratch.setSample (ch, i, std::clamp (std::isfinite (input) ? input : 0.0f, -32.0f, 32.0f) * gain);
                }
            }
            auto hostBlock = juce::dsp::AudioBlock<float> (scratch).getSubBlock (0, static_cast<size_t> (count));
            auto highRate = oversampling->processSamplesUp (hostBlock);
            for (size_t i = 0; i < highRate.getNumSamples(); ++i)
            {
                const auto blend = modelBlend.getNextValue();
                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto c = static_cast<size_t> (ch);
                    const auto input = static_cast<double> (highRate.getSample (c, i));
                    const auto transistor = blend < 1.0f ? bjt[c].process (input) : 0.0;
                    const auto feedback = blend > 0.0f ? opamp[c].process (input) : 0.0;
                    highRate.setSample (c, i, static_cast<float> (transistor + blend * (feedback - transistor)));
                }
            }
            oversampling->processSamplesDown (hostBlock);
            for (int i = 0; i < count; ++i)
            {
                const auto cutoff = toneCutoff.getNextValue();
                const auto k = std::tan (static_cast<float> (pi) * cutoff / static_cast<float> (sampleRate));
                const auto coefficient = k / (1.0f + k);
                const auto mix = wetMix.getNextValue(), gain = outputGain.getNextValue();
                const auto core = coreGain.getNextValue(), asymmetry = bias.getNextValue();
                if (settings.transformer && warmSamples > 0 && --warmSamples == 0)
                    transformerBlend.setTargetValue (1.0f);
                const auto transformerAmount = transformerBlend.getNextValue();
                const auto bypassAmount = bypassBlend.getNextValue();
                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto c = static_cast<size_t> (ch);
                    const auto original = buffer.getSample (ch, offset + i);
                    const auto input = std::isfinite (original) ? std::clamp (original, -32.0f, 32.0f) : 0.0f;
                    const auto delayedDry = circuitLatency > 0 ? dryDelay[c][circuitPosition] : input;
                    if (circuitLatency > 0) dryDelay[c][circuitPosition] = input;
                    const auto sample = scratch.getSample (ch, i);
                    const auto stateDelta = coefficient * (sample - toneState[c]);
                    const auto toned = toneState[c] + stateDelta;
                    toneState[c] = toned + stateDelta;
                    const auto mixed = (delayedDry + mix * (toned - delayedDry)) * gain;
                    const auto clean = cleanDelay[c][transformerPosition];
                    cleanDelay[c][transformerPosition] = mixed;
                    const auto coloured = transformerRunning ? transformers[c].process (mixed, core, asymmetry) : clean;
                    const auto wet = clean + transformerAmount * (coloured - clean);
                    const auto bypassed = bypassDelay[c][bypassPosition];
                    bypassDelay[c][bypassPosition] = input;
                    const auto output = bypassAmount >= 1.0f ? bypassed : wet + bypassAmount * (bypassed - wet);
                    buffer.setSample (ch, offset + i, std::isfinite (output) ? output : 0.0f);
                }
                if (circuitLatency > 0) circuitPosition = (circuitPosition + 1) % static_cast<size_t> (circuitLatency);
                transformerPosition = (transformerPosition + 1) % cleanDelay[0].size();
                bypassPosition = (bypassPosition + 1) % static_cast<size_t> (getLatency());
            }
        }
        if (! settings.transformer && ! transformerBlend.isSmoothing()) transformerRunning = false;
    }

private:
    void setSettings (const Settings& s)
    {
        const auto set = [this] (auto& smoother, float target)
        {
            if (firstSettings) smoother.setCurrentAndTargetValue (target);
            else smoother.setTargetValue (target);
        };
        set (inputGain, juce::Decibels::decibelsToGain (std::clamp (s.inputDb, -24.0f, 24.0f)));
        set (driveGain, juce::Decibels::decibelsToGain (std::clamp (s.driveDb, 0.0f, 36.0f)));
        set (toneCutoff, std::clamp (s.toneHz, 1200.0f, static_cast<float> (std::min (18000.0, sampleRate * 0.45))));
        set (outputGain, juce::Decibels::decibelsToGain (std::clamp (s.outputDb, -24.0f, 12.0f)));
        set (wetMix, std::clamp (s.mix, 0.0f, 1.0f));
        set (coreGain, juce::Decibels::decibelsToGain (std::clamp (s.coreDriveDb, 0.0f, 18.0f)));
        set (bias, std::clamp (s.dcBias, 0.0f, 1.0f));
        set (bypassBlend, s.bypass ? 1.0f : 0.0f);
        const auto model = s.model == 0 ? 0.0f : 1.0f;
        if (! firstSettings && (model > 0.5f) != (modelBlend.getTargetValue() > 0.5f))
        {
            if (model > 0.5f && modelBlend.getCurrentValue() <= 0.0f)
                for (auto& circuit : opamp) circuit.reset();
            if (model < 0.5f && modelBlend.getCurrentValue() >= 1.0f)
                for (auto& circuit : bjt) circuit.reset();
        }
        set (modelBlend, model);
        if (s.transformer && ! transformerRunning)
        {
            for (auto& transformer : transformers) transformer.reset();
            transformerRunning = true; warmSamples = character::OversampledTransformer::latencySamples;
            transformerBlend.setCurrentAndTargetValue (0.0f);
        }
        else if (! s.transformer)
            transformerBlend.setTargetValue (0.0f);
        firstSettings = false;
    }

    double sampleRate = 48000;
    int blockSize = 256, circuitLatency = 0, warmSamples = 0;
    bool firstSettings = true, transformerRunning = false;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> scratch;
    std::array<BjtCommonEmitter, 2> bjt;
    std::array<OpAmpFeedbackClipper, 2> opamp;
    std::array<character::OversampledTransformer, 2> transformers;
    std::array<std::vector<float>, 2> dryDelay, bypassDelay;
    std::array<std::array<float, character::OversampledTransformer::latencySamples>, 2> cleanDelay {};
    std::array<float, 2> toneState {};
    size_t circuitPosition = 0, transformerPosition = 0, bypassPosition = 0;
    juce::SmoothedValue<float> inputGain, driveGain, toneCutoff, outputGain, wetMix;
    juce::SmoothedValue<float> coreGain, bias, transformerBlend, bypassBlend, modelBlend;
};
}
