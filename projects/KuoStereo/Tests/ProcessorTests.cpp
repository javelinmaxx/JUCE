#include "../Source/PluginProcessor.h"
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

float impulseSideEnergy (float guard)
{
    KuoStereoProcessor processor;
    set (processor, "haasCenter", 30);
    set (processor, "haasRange", 0);
    set (processor, "width", 100);
    set (processor, "transientProtect", guard);
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

struct HaasTransientResult
{
    float activeDelayMs = 0, recoveredDelayMs = 0;
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
        check (processor.getParameters().size() == 8, "all eight host parameters are registered");
        auto buses = processor.getBusesLayout();
        check (processor.isBusesLayoutSupported (buses), "stereo layout is supported");
        buses.outputBuses.set (0, juce::AudioChannelSet::mono());
        check (! processor.isBusesLayoutSupported (buses), "mismatched stereo layout is rejected");
        set (processor, "haasCenter", 15.0f);
        set (processor, "haasRange", 8.0f);
        set (processor, "haasRate", 2.5f);
        set (processor, "haasKeepTransient", 67.0f);
        set (processor, "width", 71.0f);
        set (processor, "transientProtect", 91.0f);
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
        const auto unguarded = impulseSideEnergy (0);
        const auto guarded = impulseSideEnergy (100);
        check (unguarded > 0.01f && guarded < unguarded * 0.2f,
               "transient guard suppresses the widened impulse tail");
        const auto guardedBed = noiseBedSideEnergy (100);
        const auto unguardedBed = noiseBedSideEnergy (0);
        check (guardedBed > 0.0001f && guardedBed < unguardedBed * 0.25f,
               "full transient guard retains sustained texture while suppressing the attack");

        KuoStereoProcessor haas;
        set (haas, "haasCenter", 12);
        set (haas, "haasRange", 6);
        set (haas, "width", 0);
        haas.prepareToPlay (48000, 256);
        unsigned int random = 0x51a7u;
        float channelDifference = 0;
        bool finite = true;
        for (int block = 0; block < 280; ++block)
        {
            for (int i = 0; i < 256; ++i)
            {
                random = random * 1664525u + 1013904223u;
                const auto sample = (static_cast<float> ((random >> 16) & 0xffffu) / 32768.0f - 1.0f) * 0.15f;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }
            haas.processBlock (buffer, midi);
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
        check (scopePeak (width, 1) > 0.01f, "mid-band goniometer receives processed stereo points");

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
        check (slowDelay > 12.4f && slowDelay < 12.55f,
               "one ms per second rate limit bounds the active delay movement");
        set (limited, "haasRate", 10);
        limited.processBlock (buffer, midi);
        const auto fastStep = limited.getCurrentDelayMs() - slowDelay;
        check (fastStep > 0.04f && fastStep < 0.06f,
               "maximum rate control changes the delay slew limit");

        const auto noKeep = haasTransientResponse (0);
        const auto halfKeep = haasTransientResponse (50);
        const auto fullKeep = haasTransientResponse (100);
        check (std::abs (noKeep.activeDelayMs - 12.0f) < 0.01f && noKeep.rightPeakOffset == 576,
               "zero Keep Transient leaves the 12 ms Haas delay unchanged");
        check (std::abs (halfKeep.activeDelayMs - 6.0f) < 0.01f && halfKeep.rightPeakOffset == 288,
               "half Keep Transient halves the delay during an onset");
        check (fullKeep.activeDelayMs < 0.001f && fullKeep.rightPeakOffset == 0,
               "maximum Keep Transient makes the onset's right channel undelayed");
        check (std::abs (fullKeep.recoveredDelayMs - 12.0f) < 0.01f,
               "the Haas delay returns after the shared transient hold and release");

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
            check (knobs == 7, "editor exposes transient-aware Haas and widening controls");
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
