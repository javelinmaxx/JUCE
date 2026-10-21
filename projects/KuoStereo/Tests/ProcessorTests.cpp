#include "../Source/PluginProcessor.h"
#include "../Measurements/SynthPad.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
int checks = 0;
void check (bool condition, const char* message)
{
    ++checks;
    if (! condition) throw std::runtime_error (message);
}

void set (KuoStereoProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    check (parameter != nullptr, "parameter exists");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float impulseSideEnergy (float guard, float attackMs = 0)
{
    KuoStereoProcessor processor;
    set (processor, "haasCenter", 30);
    set (processor, "haasRange", 0);
    set (processor, "width", 100);
    set (processor, "transientProtect", guard);
    set (processor, "guardAttack", attackMs);
    processor.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    float energy = 0;
    for (int block = 0; block < 36; ++block)
    {
        buffer.clear();
        for (int i = 0; i < 256; ++i)
            if (block * 256 + i == 4000)
                buffer.setSample (0, i, 1.0f), buffer.setSample (1, i, 1.0f);
        processor.processBlock (buffer, midi);
        for (int i = 0; i < 256; ++i)
        {
            const auto sample = block * 256 + i;
            if (sample >= 4192 && sample < 5400)
            {
                const auto side = buffer.getSample (0, i) - buffer.getSample (1, i);
                energy += side * side;
            }
        }
    }
    return energy;
}

float noiseBedSideEnergy (float guard)
{
    KuoStereoProcessor processor, baseline;
    for (auto* instance : { &processor, &baseline })
    {
        set (*instance, "haasCenter", 30);
        set (*instance, "haasRange", 0);
        set (*instance, "width", instance == &processor ? 100.0f : 0.0f);
        set (*instance, "transientProtect", guard);
        instance->prepareToPlay (48000, 256);
    }
    juce::AudioBuffer<float> buffer (2, 256), dryWidth (2, 256);
    juce::MidiBuffer midi;
    float energy = 0;
    constexpr int onset = 4800;
    for (int block = 0; block < 34; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const auto sample = block * 256 + i;
            const auto value = 0.018f * std::sin (juce::MathConstants<float>::twoPi
                * 523.0f * static_cast<float> (sample) / 48000.0f)
                + (sample == onset ? 1.0f : 0.0f);
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value);
        }
        dryWidth.makeCopyOf (buffer);
        processor.processBlock (buffer, midi);
        baseline.processBlock (dryWidth, midi);
        for (int i = 0; i < 256; ++i)
        {
            const auto sample = block * 256 + i;
            if (sample >= onset + 192 && sample < onset + 1344)
            {
                const auto addedSide = (buffer.getSample (0, i) - buffer.getSample (1, i))
                                     - (dryWidth.getSample (0, i) - dryWidth.getSample (1, i));
                energy += addedSide * addedSide;
            }
        }
    }
    return energy;
}

float shortAttackGuardMaxStep (float attackMs)
{
    KuoStereoProcessor processor;
    set (processor, "haasCenter", 0);
    set (processor, "haasRange", 0);
    set (processor, "width", 100);
    set (processor, "transientProtect", 100);
    set (processor, "guardAttack", attackMs);
    processor.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    float previousSide = 0, maxStep = 0;
    for (int block = 0; block < 24; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const auto sampleIndex = block * 256 + i;
            const auto sample = 0.2f * std::sin (juce::MathConstants<float>::twoPi
                * 1000.0f * static_cast<float> (sampleIndex) / 48000.0f);
            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
        }
        processor.processBlock (buffer, midi);
        for (int i = 0; i < 256; ++i)
        {
            const auto sampleIndex = block * 256 + i;
            const auto side = 0.5f * (buffer.getSample (0, i) - buffer.getSample (1, i));
            if (sampleIndex > 720 && sampleIndex < 1920)
                maxStep = std::max (maxStep, std::abs (side - previousSide));
            previousSide = side;
        }
    }
    return maxStep;
}

struct HaasImpulseTiming
{
    int leftPeakOffset = -1, rightPeakOffset = -1;
    float activeDelayMs = 0;
};

HaasImpulseTiming haasImpulseTiming (float centerMs, float keep = 0)
{
    KuoStereoProcessor processor;
    set (processor, "haasCenter", centerMs);
    set (processor, "haasRange", 0);
    set (processor, "haasKeepTransient", keep);
    set (processor, "width", 0);
    processor.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    HaasImpulseTiming result;
    for (int block = 0; block < 6; ++block)
    {
        buffer.clear();
        if (block == 0)
            buffer.setSample (0, 0, 1.0f), buffer.setSample (1, 0, 1.0f);
        processor.processBlock (buffer, midi);
        if (block == 0)
            result.activeDelayMs = processor.getCurrentDelayMs();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (buffer.getSample (0, i) > 0.9f && result.leftPeakOffset < 0)
                result.leftPeakOffset = block * 256 + i;
            if (buffer.getSample (1, i) > 0.9f && result.rightPeakOffset < 0)
                result.rightPeakOffset = block * 256 + i;
        }
    }
    return result;
}

struct HaasTransientResult
{
    float activeDelayMs = 0, heldDelayMs = 0, releasingDelayMs = 0;
    float laterReleaseDelayMs = 0, recoveredDelayMs = 0;
    int rightPeakOffset = -1;
};

HaasTransientResult haasTransientResponse (float keep)
{
    KuoStereoProcessor processor;
    set (processor, "haasCenter", 12);
    set (processor, "haasRange", 0);
    set (processor, "haasKeepTransient", keep);
    set (processor, "width", 0);
    processor.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    HaasTransientResult result;
    for (int block = 0; block < 30; ++block)
    {
        buffer.clear();
        if (block == 4)
        {
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);
        }
        processor.processBlock (buffer, midi);
        if (block == 4)
            result.activeDelayMs = processor.getCurrentDelayMs();
        if (block == 5)
            result.heldDelayMs = processor.getCurrentDelayMs();
        if (block == 8)
            result.releasingDelayMs = processor.getCurrentDelayMs();
        if (block == 12)
            result.laterReleaseDelayMs = processor.getCurrentDelayMs();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (block >= 4 && buffer.getSample (1, i) > 0.9f && result.rightPeakOffset < 0)
                result.rightPeakOffset = (block - 4) * 256 + i;
    }
    result.recoveredDelayMs = processor.getCurrentDelayMs();
    return result;
}

float scopePeak (KuoStereoProcessor& processor, int band)
{
    std::array<juce::Point<float>, KuoStereoProcessor::scopePoints> points;
    processor.readScope (band, points);
    float peak = 0;
    for (const auto& point : points)
        peak = std::max (peak, std::max (std::abs (point.x), std::abs (point.y)));
    return peak;
}
}

int main (int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        KuoStereoProcessor processor;
        check (processor.getName() == "Kuo Stereo", "plug-in identity is Kuo Stereo");
        check (std::abs (processor.getCurrentDelayMs() - 12.0f) < 0.001f,
               "active delay initially displays the default center before audio starts");
        check (processor.getParameters().size() == 9, "all nine host parameters are registered");
        auto buses = processor.getBusesLayout();
        check (processor.isBusesLayoutSupported (buses), "stereo layout is supported");
        buses.outputBuses.set (0, juce::AudioChannelSet::mono());
        check (! processor.isBusesLayoutSupported (buses), "mismatched stereo layout is rejected");
        set (processor, "haasCenter", -15.0f);
        set (processor, "haasRange", 8.0f);
        set (processor, "haasRate", 2.5f);
        set (processor, "haasKeepTransient", 67.0f);
        set (processor, "width", 71.0f);
        set (processor, "transientProtect", 91.0f);
        set (processor, "guardAttack", 3.5f);
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        KuoStereoProcessor restored;
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        for (auto* parameter : processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            check (ranged && std::abs (ranged->getValue()
                - restored.parameters.getParameter (ranged->getParameterID())->getValue()) < 0.0001f,
                "host parameter survives a state round-trip");
        }

        restored.prepareToPlay (48000, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        set (restored, "bypass", 1);
        for (int i = 0; i < 256; ++i)
        {
            buffer.setSample (0, i, 0.2f * std::sin (i * 0.11f));
            buffer.setSample (1, i, 0.15f * std::cos (i * 0.07f));
        }
        juce::AudioBuffer<float> original (buffer);
        restored.processBlock (buffer, midi);
        bool identical = true;
        for (int ch = 0; ch < 2; ++ch)
            identical = identical && std::memcmp (buffer.getReadPointer (ch), original.getReadPointer (ch),
                256 * sizeof (float)) == 0;
        check (identical, "bypass preserves both input channels exactly");

        KuoStereoProcessor width;
        set (width, "width", 100);
        set (width, "transientProtect", 0);
        width.prepareToPlay (48000, 256);
        KuoStereoProcessor noWidth;
        set (noWidth, "width", 0);
        noWidth.prepareToPlay (48000, 256);
        juce::AudioBuffer<float> baseline (2, 256);
        float sideEnergy = 0;
        for (int block = 0; block < 180; ++block)
        {
            for (int i = 0; i < 256; ++i)
            {
                const auto sample = 0.3f * std::sin (juce::MathConstants<float>::twoPi
                    * 523.0f * static_cast<float> (block * 256 + i) / 48000.0f);
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }
            baseline.makeCopyOf (buffer);
            width.processBlock (buffer, midi);
            noWidth.processBlock (baseline, midi);
            if (block > 100)
                for (int i = 0; i < 256; ++i)
                {
                    const auto side = (buffer.getSample (0, i) - buffer.getSample (1, i))
                                    - (baseline.getSample (0, i) - baseline.getSample (1, i));
                    sideEnergy += side * side;
                }
        }
        check (sideEnergy > 0.01f, "velvet convolution widens a centered sustained tone");
        const auto positiveHaas = haasImpulseTiming (12);
        const auto zeroHaas = haasImpulseTiming (0);
        const auto negativeHaas = haasImpulseTiming (-12);
        const auto minimumHaas = haasImpulseTiming (-30);
        const auto negativeKept = haasImpulseTiming (-12, 100);
        check (positiveHaas.leftPeakOffset == 0 && positiveHaas.rightPeakOffset == 576,
               "positive Haas delay shifts the right channel");
        check (zeroHaas.leftPeakOffset == 0 && zeroHaas.rightPeakOffset == 0
                   && std::abs (zeroHaas.activeDelayMs) < 0.001f,
               "zero Haas delay leaves both channels aligned");
        check (negativeHaas.leftPeakOffset == 576 && negativeHaas.rightPeakOffset == 0
                   && std::abs (negativeHaas.activeDelayMs + 12.0f) < 0.001f,
               "negative Haas delay shifts the left channel");
        check (minimumHaas.leftPeakOffset == 1440 && minimumHaas.rightPeakOffset == 0,
               "minimum Haas delay reaches negative 30 ms");
        check (negativeKept.leftPeakOffset == 0 && negativeKept.rightPeakOffset == 0
                   && std::abs (negativeKept.activeDelayMs) < 0.001f,
               "Keep Transient protects onsets at negative Haas delays");
        const auto unguarded = impulseSideEnergy (0);
        const auto guarded = impulseSideEnergy (100);
        const auto slowGuard = impulseSideEnergy (100, 5);
        const auto noGuardSlowAttack = impulseSideEnergy (0, 5);
        check (unguarded > 0.01f && guarded < unguarded * 0.2f,
               "transient guard suppresses the widened impulse tail");
        check (slowGuard > guarded && slowGuard < unguarded,
               "Guard Attack softens attenuation of a sharp impulse");
        check (std::abs (noGuardSlowAttack - unguarded) < 0.00001f,
               "Guard Attack has no effect when Transient Guard is zero");
        const auto guardedBed = noiseBedSideEnergy (100);
        const auto unguardedBed = noiseBedSideEnergy (0);
        check (guardedBed > 0.0001f && guardedBed < unguardedBed * 0.25f,
               "full transient guard retains sustained texture while suppressing the attack");
        const auto instantGuardStep = shortAttackGuardMaxStep (0.0f);
        const auto fastGuardStep = shortAttackGuardMaxStep (0.5f);
        const auto mediumGuardStep = shortAttackGuardMaxStep (2.0f);
        const auto slowGuardStep = shortAttackGuardMaxStep (3.0f);
        check (instantGuardStep < 0.0085f && instantGuardStep < slowGuardStep * 1.25f
                   && fastGuardStep < 0.0085f && fastGuardStep < slowGuardStep * 1.25f
                   && mediumGuardStep < slowGuardStep * 1.25f,
               "short Guard Attack avoids abrupt side-signal steps at onset threshold");

        KuoStereoProcessor haas;
        set (haas, "haasCenter", 12);
        set (haas, "haasRange", 6);
        set (haas, "haasRate", 10);
        set (haas, "width", 0);
        haas.prepareToPlay (48000, 256);
        float channelDifference = 0;
        bool finite = true;
        float majorTargetSum = 0, minorTargetSum = 0;
        float previousMajorTarget = 0, previousMinorTarget = 0;
        float majorEndDelay = 0, minorEndDelay = 0;
        double majorInputEnergy = 0, minorInputEnergy = 0;
        int majorBlocks = 0, minorBlocks = 0, majorSwitches = 0, minorSwitches = 0;
        for (int block = 0; block < kuoTestSignal::padSamples / 256; ++block)
        {
            for (int i = 0; i < 256; ++i)
            {
                const auto sample = kuoTestSignal::padSample (block * 256 + i);
                const auto position = block * 256 + i;
                if (position >= 24000 && position < 134400)
                    majorInputEnergy += static_cast<double> (sample) * sample;
                if (position >= 168000 && position < 278400)
                    minorInputEnergy += static_cast<double> (sample) * sample;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }
            haas.processBlock (buffer, midi);
            const auto blockEnd = (block + 1) * 256;
            if (blockEnd >= 24000 && blockEnd < 134400)
            {
                const auto target = haas.getSelectedDelayMs();
                if (majorBlocks > 0 && std::abs (target - previousMajorTarget) > 0.01f)
                    ++majorSwitches;
                previousMajorTarget = target;
                majorEndDelay = haas.getCurrentDelayMs();
                majorTargetSum += target;
                ++majorBlocks;
            }
            if (blockEnd >= 168000 && blockEnd < 278400)
            {
                const auto target = haas.getSelectedDelayMs();
                if (minorBlocks > 0 && std::abs (target - previousMinorTarget) > 0.01f)
                    ++minorSwitches;
                previousMinorTarget = target;
                minorEndDelay = haas.getCurrentDelayMs();
                minorTargetSum += target;
                ++minorBlocks;
            }
            if (block > 200)
                for (int i = 0; i < 256; ++i)
                {
                    const auto left = buffer.getSample (0, i), right = buffer.getSample (1, i);
                    finite = finite && std::isfinite (left) && std::isfinite (right);
                    channelDifference += std::abs (left - right);
                }
        }
        check (haas.getSelectedDelayMs() >= 6.0f && haas.getSelectedDelayMs() <= 18.0f,
               "mid-spectrum analyzer selects a delay within center plus or minus variability");
        check (finite && channelDifference > 1.0f, "adaptive Haas processing creates finite stereo separation");
        check (std::abs (majorInputEnergy / minorInputEnergy - 1.0) < 0.1,
               "C major and A minor pad chords have comparable steady input energy");
        check (majorBlocks > 0 && minorBlocks > 0
                   && std::abs (majorTargetSum / majorBlocks - minorTargetSum / minorBlocks) > 0.5f,
               "Smart Haas target responds differently to C major and A minor pad chords");
        check (majorSwitches == 0 && minorSwitches <= 2,
               "Smart Haas target avoids repeated switching while each pad chord sustains");
        check (std::abs (majorEndDelay - previousMajorTarget) < 0.1f
                   && std::abs (minorEndDelay - previousMinorTarget) < 0.1f,
               "rate-limited Haas delay settles at each pad chord's selected target");
        check (scopePeak (width, 1) > 0.01f, "mid-band goniometer receives processed stereo points");

        for (const auto center : { 0.0f, -12.0f })
        {
            set (haas, "haasCenter", center);
            for (int block = 0; block < 8; ++block)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const auto sample = kuoTestSignal::padSample (kuoTestSignal::padSamples - 256 + i);
                    buffer.setSample (0, i, sample);
                    buffer.setSample (1, i, sample);
                }
                haas.processBlock (buffer, midi);
            }
            check (haas.getSelectedDelayMs() >= center - 6.0f
                       && haas.getSelectedDelayMs() <= center + 6.0f,
                   "Smart Haas searches both zero-crossing and negative delay ranges");
        }

        KuoStereoProcessor limited;
        set (limited, "haasCenter", 12);
        set (limited, "haasRange", 0);
        set (limited, "haasRate", 1);
        limited.prepareToPlay (48000, 256);
        set (limited, "haasCenter", 20);
        buffer.clear();
        for (int block = 0; block < 100; ++block)
            limited.processBlock (buffer, midi);
        const auto slowDelay = limited.getCurrentDelayMs();
        check (std::abs (limited.getSelectedDelayMs() - 20.0f) < 0.01f,
               "zero variability fixes the selected delay at the center");
        check (slowDelay > 12.03f && slowDelay < 12.07f,
               "sigmoid delay movement starts gently under a one ms/s peak rate");
        set (limited, "haasRate", 10);
        limited.processBlock (buffer, midi);
        const auto fastStep = limited.getCurrentDelayMs() - slowDelay;
        check (fastStep > 0.005f && fastStep < 0.02f,
               "maximum rate control changes the sigmoid transition speed");

        KuoStereoProcessor curved;
        set (curved, "haasCenter", 12);
        set (curved, "haasRange", 0);
        set (curved, "haasRate", 10);
        curved.prepareToPlay (48000, 256);
        set (curved, "haasCenter", 20);
        buffer.clear();
        float earlyStep = 0, middleStep = 0, lateStep = 0, peakStep = 0;
        for (int block = 0; block < 225; ++block)
        {
            const auto previous = curved.getCurrentDelayMs();
            curved.processBlock (buffer, midi);
            const auto step = curved.getCurrentDelayMs() - previous;
            peakStep = std::max (peakStep, step);
            if (block == 20) earlyStep = step;
            if (block == 112) middleStep = step;
            if (block == 205) lateStep = step;
        }
        check (middleStep > earlyStep * 2.0f && middleStep > lateStep * 2.0f
                   && peakStep < 10.0f * 256.0f / 48000.0f + 0.001f
                   && std::abs (curved.getCurrentDelayMs() - 20.0f) < 0.01f,
               "Haas delay follows a sigmoid with a bounded peak rate");

        const auto noKeep = haasTransientResponse (0);
        const auto halfKeep = haasTransientResponse (50);
        const auto fullKeep = haasTransientResponse (100);
        check (std::abs (noKeep.activeDelayMs - 12.0f) < 0.01f && noKeep.rightPeakOffset == 576,
               "zero Keep Transient leaves the 12 ms Haas delay unchanged");
        check (std::abs (halfKeep.activeDelayMs - 6.0f) < 0.01f && halfKeep.rightPeakOffset == 288,
               "half Keep Transient halves the delay during an onset");
        check (fullKeep.activeDelayMs < 0.001f && fullKeep.rightPeakOffset == 0,
               "maximum Keep Transient makes the onset's right channel undelayed");
        constexpr float releaseIntervalSeconds = 4.0f * 256.0f / 48000.0f;
        const auto expectedRemainingRatio = std::exp (std::log (0.01f)
            * releaseIntervalSeconds / 0.065f);
        const auto actualRemainingRatio = (12.0f - fullKeep.laterReleaseDelayMs)
            / (12.0f - fullKeep.releasingDelayMs);
        check (fullKeep.heldDelayMs < 0.01f && fullKeep.releasingDelayMs > 0.0f
                   && fullKeep.laterReleaseDelayMs > fullKeep.releasingDelayMs
                   && std::abs (actualRemainingRatio - expectedRemainingRatio) < 0.01f,
               "Haas protection holds for 10 ms then decays exponentially to 1% over 65 ms");
        check (std::abs (fullKeep.recoveredDelayMs - 12.0f) < 0.01f,
               "the Haas delay returns after its transient hold and release");

        KuoStereoProcessor spectralOnset;
        set (spectralOnset, "haasCenter", 12);
        set (spectralOnset, "haasRange", 0);
        set (spectralOnset, "haasKeepTransient", 100);
        set (spectralOnset, "width", 0);
        spectralOnset.prepareToPlay (48000, 256);
        for (int block = 0; block < 190; ++block)
        {
            for (int i = 0; i < 256; ++i)
            {
                const auto time = static_cast<float> (block * 256 + i) / 48000.0f;
                const auto sample = 0.18f * std::sin (juce::MathConstants<float>::twoPi * 100.0f * time);
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }
            spectralOnset.processBlock (buffer, midi);
        }
        check (std::abs (spectralOnset.getCurrentDelayMs() - 12.0f) < 0.01f,
               "steady low-band tone does not keep spectral transient protection active");
        for (int i = 0; i < 256; ++i)
        {
            const auto time = static_cast<float> (i) / 48000.0f;
            const auto sample = 0.18f * std::sin (juce::MathConstants<float>::twoPi * 5000.0f * time);
            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
        }
        spectralOnset.processBlock (buffer, midi);
        check (spectralOnset.getCurrentDelayMs() < 0.01f,
               "spectral detector responds to a frequency change at equal tone amplitude");

        if (argc > 1 && juce::String (argv[1]) == "--editor")
        {
            KuoStereoProcessor preview;
            preview.prepareToPlay (48000, 256);
            for (int block = 0; block < 180; ++block)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const auto time = static_cast<float> (block * 256 + i) / 48000.0f;
                    const auto bass = 0.22f * std::sin (juce::MathConstants<float>::twoPi * 80.0f * time);
                    buffer.setSample (0, i, bass
                        + 0.18f * std::sin (juce::MathConstants<float>::twoPi * 700.0f * time)
                        + 0.10f * std::sin (juce::MathConstants<float>::twoPi * 5000.0f * time));
                    buffer.setSample (1, i, bass
                        + 0.18f * std::sin (juce::MathConstants<float>::twoPi * 700.0f * time + 0.12f)
                        + 0.10f * std::sin (juce::MathConstants<float>::twoPi * 5000.0f * time + 0.3f));
                }
                preview.processBlock (buffer, midi);
            }
            check (scopePeak (preview, 0) > 0.01f && scopePeak (preview, 1) > 0.01f
                   && scopePeak (preview, 2) > 0.01f,
                   "each frequency-split goniometer receives its own processed band");
            std::unique_ptr<juce::AudioProcessorEditor> editor (preview.createEditor());
            check (editor && editor->getWidth() == 548 && editor->getHeight() == 741,
                   "editor uses the shared J-SONIC panel dimensions");
            int knobs = 0;
            for (auto* child : editor->getChildren())
                if (dynamic_cast<juce::Slider*> (child) != nullptr) ++knobs;
            check (knobs == 8, "editor exposes Guard Attack beside the widening controls");
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            check (image.isValid(), "frequency-split goniometer editor renders");
            if (argc > 2)
            {
                juce::MemoryOutputStream png;
                check (juce::PNGImageFormat().writeImageToStream (image, png)
                       && juce::File (argv[2]).replaceWithData (png.getData(), png.getDataSize()),
                       "editor preview writes");
            }
        }
        std::cout << checks << " Kuo Stereo checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
