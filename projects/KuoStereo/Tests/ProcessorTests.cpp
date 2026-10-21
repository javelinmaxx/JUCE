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
    set (processor, "haasMix", 0);
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
            if (sample >= 4000 && sample < 5600)
            {
                const auto side = buffer.getSample (0, i) - buffer.getSample (1, i);
                energy += side * side;
            }
        }
    }
    return energy;
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
        check (processor.getParameters().size() == 6, "all six host parameters are registered");
        auto buses = processor.getBusesLayout();
        check (processor.isBusesLayoutSupported (buses), "stereo layout is supported");
        buses.outputBuses.set (0, juce::AudioChannelSet::mono());
        check (! processor.isBusesLayoutSupported (buses), "mismatched stereo layout is rejected");
        set (processor, "haasMix", 62.5f);
        set (processor, "haasMax", 24.0f);
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
        set (width, "haasMix", 0);
        set (width, "width", 100);
        set (width, "transientProtect", 0);
        width.prepareToPlay (48000, 256);
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
            width.processBlock (buffer, midi);
            if (block > 100)
                for (int i = 0; i < 256; ++i)
                {
                    const auto side = buffer.getSample (0, i) - buffer.getSample (1, i);
                    sideEnergy += side * side;
                }
        }
        check (sideEnergy > 0.01f, "velvet convolution widens a centered sustained tone");
        const auto unguarded = impulseSideEnergy (0);
        const auto guarded = impulseSideEnergy (100);
        check (unguarded > 0.01f && guarded < unguarded * 0.2f,
               "transient guard suppresses the widened impulse tail");

        KuoStereoProcessor haas;
        set (haas, "haasMix", 100);
        set (haas, "haasMax", 18);
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
        check (haas.getSelectedDelayMs() >= 3.0f && haas.getSelectedDelayMs() <= 18.0f,
               "mid-spectrum analyzer selects a delay within the requested Haas range");
        check (finite && channelDifference > 1.0f, "adaptive Haas processing creates finite stereo separation");
        check (scopePeak (width, 1) > 0.01f, "mid-band goniometer receives processed stereo points");

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
            check (knobs == 5, "editor exposes both widening sections and output controls");
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
