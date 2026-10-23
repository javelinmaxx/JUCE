#pragma once
#include "Circuits.h"
#include "SmartHarmonics.h"
#include "ClearStage.h"
#include "AutoCompensator.h"
#include "../../YinyangCompressor/Source/OversampledTransformer.h"
#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace zao
{
class DriveEngine
{
public:
    void prepare (double rate, int maximumBlock, bool smartWorker = true)
    {
        sampleRate = rate;
        smart.prepare (rate, smartWorker);
        clear.prepare (rate);
        autoCompensator.prepare (rate);
        blockSize = std::max (1, maximumBlock);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (static_cast<size_t> (blockSize));
        circuitLatency = juce::roundToInt (oversampling->getLatencyInSamples());
        scratch.setSize (2, blockSize);
        filteredInput.setSize (2, blockSize);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            bjt[ch].prepare (rate * 4); opamp[ch].prepare (rate * 4);
            shunt[ch].prepare (rate * 4); crossover[ch].prepare (rate * 4);
            transformers[ch].prepare (rate);
            dryDelay[ch].assign (static_cast<size_t> (clear.getLatencySamples()+circuitLatency), 0.0f);
            filteredDelay[ch].assign (static_cast<size_t> (std::max (1, circuitLatency)), 0.0f);
            bypassDelay[ch].assign (static_cast<size_t> (getLatency()), 0.0f);
        }
        cleanDelay = {}; toneState = {};
        circuitPosition = dryPosition = transformerPosition = bypassPosition = 0;
        for (auto* smoother : { &inputGain, &driveGain, &toneCutoff, &outputGain, &wetMix,
                                 &coreGain, &bias, &transformerBlend, &bypassBlend, &autoTrimBlend })
            smoother->reset (rate, 0.02);
        for (auto& weight : modelWeights) weight.reset (rate * 4, 0.01);
        for (auto* smoother : { &bjtBias, &driverResistance, &driverCurrent, &driverCoefficient })
            smoother->reset (rate * 4, 0.02);
        firstSettings = true; transformerRunning = false; warmSamples = 0;
        toneCoefficientValid = false;
    }

    // Rewind a separate preview engine without redesigning its FIR, allocating
    // buffers, or rebuilding the oversampler. Smart preview bands are set by
    // the renderer after this reset.
    void resetSignalPath()
    {
        clear.reset(); oversampling->reset();
        autoCompensator.reset();
        for (size_t ch = 0; ch < 2; ++ch)
        {
            bjt[ch].reset(); opamp[ch].reset(); shunt[ch].reset(); crossover[ch].reset();
            transformers[ch].reset();
            std::fill (dryDelay[ch].begin(), dryDelay[ch].end(), 0.0f);
            std::fill (filteredDelay[ch].begin(), filteredDelay[ch].end(), 0.0f);
            std::fill (bypassDelay[ch].begin(), bypassDelay[ch].end(), 0.0f);
        }
        cleanDelay = {}; toneState = {};
        circuitPosition = dryPosition = transformerPosition = bypassPosition = 0;
        firstSettings = true; transformerRunning = false; warmSamples = 0;
        toneCoefficientValid = false;
        transformerBlend.setCurrentAndTargetValue (0.0f);
    }

    void release() { smart.release(); }
    SmartHarmonics& getSmart() noexcept { return smart; }
    const SmartHarmonics& getSmart() const noexcept { return smart; }

    int getLatency() const noexcept { return clear.getLatencySamples() + circuitLatency + character::OversampledTransformer::latencySamples; }

    void process (juce::AudioBuffer<float>& buffer, const Settings& settings)
    {
        setSettings (settings);
        clear.setEnabled (settings.clear);
        autoCompensator.setTarget (settings.autoCompensate && ! settings.bypass ? settings.autoGain : 1.0f);
        smart.beginBlock (settings);
        const auto channels = std::min (2, buffer.getNumChannels());
        for (int offset = 0; offset < buffer.getNumSamples(); offset += blockSize)
        {
            const auto count = std::min (blockSize, buffer.getNumSamples() - offset);
            for (int i = 0; i < count; ++i)
            {
                const auto gain = inputGain.getNextValue() * driveGain.getNextValue() * 0.35f;
                std::array<float, 2> inputSamples;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto input = ch < channels ? buffer.getSample (ch, offset + i) : 0.0f;
                    inputSamples[static_cast<size_t> (ch)] = std::clamp (std::isfinite (input) ? input : 0.0f, -32.0f, 32.0f);
                }
                const auto shaped = clear.process (inputSamples);
                for (int ch = 0; ch < 2; ++ch)
                {
                    filteredInput.setSample (ch, i, shaped[static_cast<size_t> (ch)]);
                    scratch.setSample (ch, i, shaped[static_cast<size_t> (ch)] * gain);
                }
            }
            auto hostBlock = juce::dsp::AudioBlock<float> (scratch).getSubBlock (0, static_cast<size_t> (count));
            auto highRate = oversampling->processSamplesUp (hostBlock);
            // Only the selected circuit and any outgoing fade participate.
            // Rebuild per chunk so completed fades leave the dispatch loop.
            std::array<size_t, circuitCount> activeModes {};
            size_t activeCount = 0;
            bool needsBjt = false, needsDriver = false;
            for (size_t mode = 0; mode < circuitCount; ++mode)
                if (modelWeights[mode].getCurrentValue() > 0.0f || modelWeights[mode].getTargetValue() > 0.0f)
                {
                    activeModes[activeCount++] = mode;
                    needsBjt = needsBjt || mode == 0;
                    needsDriver = needsDriver || mode >= 2;
                }
            const auto highCount = static_cast<int> (highRate.getNumSamples());
            // Keep automation time advancing without updating sleeping models.
            if (! needsBjt) bjtBias.skip (highCount);
            if (! needsDriver)
            {
                driverResistance.skip (highCount); driverCurrent.skip (highCount); driverCoefficient.skip (highCount);
            }
            for (size_t i = 0; i < highRate.getNumSamples(); ++i)
            {
                const auto biasVolts = needsBjt ? static_cast<double> (bjtBias.getNextValue()) : 0.0;
                const DriverParameters driver = needsDriver
                    ? DriverParameters { driverResistance.getNextValue(), driverCurrent.getNextValue(), driverCoefficient.getNextValue() }
                    : DriverParameters {};
                std::array<float, circuitCount> weights {};
                for (size_t active = 0; active < activeCount; ++active)
                    weights[active] = modelWeights[activeModes[active]].getNextValue();
                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto c = static_cast<size_t> (ch);
                    const auto input = static_cast<double> (highRate.getSample (c, i));
                    double output = 0;
                    for (size_t active = 0; active < activeCount; ++active)
                        if (weights[active] > 0.0f)
                            output += weights[active] * processCircuit (activeModes[active], c, input, biasVolts, driver);
                    highRate.setSample (c, i, static_cast<float> (output));
                }
            }
            oversampling->processSamplesDown (hostBlock);
            for (int i = 0; i < count; ++i)
            {
                // Test before advancing so the final smoothing sample also
                // receives its exact coefficient, then reuse it while idle.
                const auto toneMoving = toneCutoff.isSmoothing();
                const auto cutoff = toneCutoff.getNextValue();
                if (! toneCoefficientValid || toneMoving)
                {
                    const auto k = std::tan (static_cast<float> (pi) * cutoff / static_cast<float> (sampleRate));
                    toneCoefficient = k / (1.0f + k);
                    toneCoefficientValid = true;
                }
                const auto coefficient = toneCoefficient;
                const auto mix = wetMix.getNextValue(), gain = outputGain.getNextValue();
                // Auto corrects the complete circuit/transformer reference, then
                // Output offsets that matched level instead of being cancelled.
                const auto trim = 1.0f + autoTrimBlend.getNextValue() * (gain-1.0f);
                const auto core = coreGain.getNextValue(), asymmetry = bias.getNextValue();
                if (settings.transformer && warmSamples > 0 && --warmSamples == 0)
                    transformerBlend.setTargetValue (1.0f);
                const auto transformerAmount = transformerBlend.getNextValue();
                const auto bypassAmount = bypassBlend.getNextValue();
                std::array<float, 2> drySamples {}, wetSamples {};
                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto c = static_cast<size_t> (ch);
                    const auto original = buffer.getSample (ch, offset + i);
                    const auto input = std::isfinite (original) ? std::clamp (original, -32.0f, 32.0f) : 0.0f;
                    const auto delayedDry = dryDelay[c][dryPosition];
                    dryDelay[c][dryPosition] = input;
                    const auto filtered = filteredInput.getSample (ch, i);
                    const auto reference = circuitLatency > 0 ? filteredDelay[c][circuitPosition] : filtered;
                    if (circuitLatency > 0) filteredDelay[c][circuitPosition] = filtered;
                    const auto sample = scratch.getSample (ch, i);
                    // Subtract the aligned filtered reference: the grey contour
                    // changes circuit excitation, while the separate clean branch
                    // remains flat when we add the generated wet residual back.
                    const auto residual = sample - reference;
                    const auto stateDelta = coefficient * (residual - toneState[c]);
                    const auto toned = toneState[c] + stateDelta;
                    toneState[c] = toned + stateDelta;
                    drySamples[c] = delayedDry; wetSamples[c] = toned;
                }
                smart.process (drySamples, wetSamples);
                std::array<float, 2> processedSamples {}, bypassedSamples {};
                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto c = static_cast<size_t> (ch);
                    const auto delayedDry = drySamples[c], residual = wetSamples[c];
                    const auto original = buffer.getSample (ch, offset+i);
                    const auto input = std::isfinite (original) ? std::clamp (original, -32.0f, 32.0f) : 0.0f;
                    const auto mixed = (delayedDry + mix * residual) * gain;
                    const auto clean = cleanDelay[c][transformerPosition];
                    cleanDelay[c][transformerPosition] = mixed;
                    const auto coloured = transformerRunning ? transformers[c].process (mixed, core, asymmetry) : clean;
                    const auto wet = clean + transformerAmount * (coloured - clean);
                    const auto bypassed = bypassDelay[c][bypassPosition];
                    bypassDelay[c][bypassPosition] = input;
                    processedSamples[c] = std::isfinite (wet) ? wet : 0.0f;
                    bypassedSamples[c] = bypassed;
                }
                const auto matched = autoCompensator.process (processedSamples, channels);
                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto c = static_cast<size_t> (ch);
                    const auto wet = matched[c]*trim, bypassed = bypassedSamples[c];
                    const auto output = bypassAmount >= 1.0f ? bypassed : wet + bypassAmount * (bypassed - wet);
                    buffer.setSample (ch, offset + i, std::isfinite (output) ? output : 0.0f);
                }
                if (circuitLatency > 0) circuitPosition = (circuitPosition + 1) % static_cast<size_t> (circuitLatency);
                dryPosition = (dryPosition + 1) % dryDelay[0].size();
                transformerPosition = (transformerPosition + 1) % cleanDelay[0].size();
                bypassPosition = (bypassPosition + 1) % static_cast<size_t> (getLatency());
            }
        }
        if (! settings.transformer && ! transformerBlend.isSmoothing()) transformerRunning = false;
    }

private:
    friend struct DriveEngineTestAccess;
    double processCircuit (size_t mode, size_t channel, double input, double biasVolts, DriverParameters driver) noexcept
    {
        switch (mode)
        {
            case 0: bjt[channel].setBiasVolts (biasVolts); return bjt[channel].process (input);
            case 1: return opamp[channel].process (input);
            // Passive networks have no amplifier gain of their own. A fixed
            // input-conditioning gain lets the default Drive reach conduction.
            case 2: shunt[channel].setDriverParameters (driver); return shunt[channel].process (input * 4.0);
            default: crossover[channel].setDriverParameters (driver); return crossover[channel].process (input * 4.0);
        }
    }
    void resetCircuit (size_t mode) noexcept
    {
        for (size_t channel = 0; channel < 2; ++channel)
            switch (mode)
            {
                case 0: bjt[channel].reset(); break;
                case 1: opamp[channel].reset(); break;
                case 2: shunt[channel].reset(); break;
                default: crossover[channel].reset(); break;
            }
    }
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
        set (autoTrimBlend, s.autoCompensate ? 1.0f : 0.0f);
        set (wetMix, std::clamp (s.mix, 0.0f, 1.0f));
        set (coreGain, juce::Decibels::decibelsToGain (std::clamp (s.coreDriveDb, 0.0f, 18.0f)));
        set (bias, std::clamp (s.dcBias, 0.0f, 1.0f));
        set (bypassBlend, s.bypass ? 1.0f : 0.0f);
        set (bjtBias, std::clamp (s.bjtBias, -1.0f, 1.0f) * 0.8f);
        const auto driver = DriverParameters::fromKnee (sampleRate * 4.0, s.diodeKnee);
        set (driverResistance, static_cast<float> (driver.resistance));
        set (driverCurrent, static_cast<float> (driver.currentLimit));
        set (driverCoefficient, static_cast<float> (driver.coefficient));
        if (firstSettings)
            for (auto& circuit : bjt)
            {
                circuit.setBiasVolts (bjtBias.getCurrentValue());
                circuit.reset();
            }
        const auto selected = static_cast<size_t> (std::clamp (s.model, 0, static_cast<int> (circuitCount - 1)));
        for (size_t mode = 0; mode < circuitCount; ++mode)
        {
            auto& weight = modelWeights[mode];
            const auto target = mode == selected ? 1.0f : 0.0f;
            if (! firstSettings && target > 0.5f && weight.getTargetValue() < 0.5f
                && weight.getCurrentValue() <= 0.0f) resetCircuit (mode);
            set (weight, target);
        }
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

    SmartHarmonics smart;
    ClearStage clear;
    AutoCompensator autoCompensator;
    double sampleRate = 48000;
    int blockSize = 256, circuitLatency = 0, warmSamples = 0;
    bool firstSettings = true, transformerRunning = false;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> scratch, filteredInput;
    std::array<BjtCommonEmitter, 2> bjt;
    std::array<OpAmpFeedbackClipper, 2> opamp;
    std::array<DiodeShuntClipper, 2> shunt;
    std::array<SeriesDiodeCrossover, 2> crossover;
    std::array<character::OversampledTransformer, 2> transformers;
    std::array<std::vector<float>, 2> dryDelay, filteredDelay, bypassDelay;
    std::array<std::array<float, character::OversampledTransformer::latencySamples>, 2> cleanDelay {};
    std::array<float, 2> toneState {};
    float toneCoefficient = 0;
    bool toneCoefficientValid = false;
    size_t circuitPosition = 0, dryPosition = 0, transformerPosition = 0, bypassPosition = 0;
    juce::SmoothedValue<float> inputGain, driveGain, toneCutoff, outputGain, wetMix;
    juce::SmoothedValue<float> coreGain, bias, transformerBlend, bypassBlend, autoTrimBlend;
    juce::SmoothedValue<float> bjtBias, driverResistance, driverCurrent, driverCoefficient;
    std::array<juce::SmoothedValue<float>, circuitCount> modelWeights;
};
}
