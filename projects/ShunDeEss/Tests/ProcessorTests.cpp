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

void set (ShunDeEssProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    check (parameter != nullptr, "parameter exists");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float measureRms (const juce::AudioBuffer<float>& buffer, int channel = 0)
{
    double sum = 0;
    const auto start = std::max (0, buffer.getNumSamples() - 4096);
    for (int i = start; i < buffer.getNumSamples(); ++i)
        sum += buffer.getSample (channel, i) * buffer.getSample (channel, i);
    return static_cast<float> (std::sqrt (sum / (buffer.getNumSamples() - start)));
}

void renderSine (ShunDeEssProcessor& processor, juce::AudioBuffer<float>& buffer, float frequency, float amplitude)
{
    juce::MidiBuffer midi;
    constexpr double pi = 3.14159265358979323846;
    constexpr double sampleRate = 48000;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto sample = amplitude * std::sin (2.0 * pi * frequency * i / sampleRate);
            buffer.setSample (channel, i, static_cast<float> (sample));
        }
    processor.processBlock (buffer, midi);
}
}

int main (int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        ShunDeEssProcessor processor;
        check (processor.getParameters().size() == 12, "all de-esser parameters are registered");
        check (processor.getName() == "SHUN DE-ESS", "plug-in identity is SHUN DE-ESS");
        processor.prepareToPlay (48000, 256);
        set (processor, "threshold", -42);
        set (processor, "depth", 12);
        set (processor, "attack", 0.2f);
        set (processor, "release", 10);
        juce::AudioBuffer<float> highTone (2, 16384);
        renderSine (processor, highTone, 6500, 0.5f);
        check (measureRms (highTone) < 0.20f, "wideband mode reduces sustained sibilance");
        check (processor.reduction.load() > 8.0f, "gain-reduction meter reports de-essing amount");

        ShunDeEssProcessor split;
        split.prepareToPlay (48000, 256);
        set (split, "mode", 1);
        set (split, "threshold", -42);
        set (split, "depth", 12);
        set (split, "attack", 0.2f);
        set (split, "release", 10);
        juce::AudioBuffer<float> lowTone (2, 16384);
        renderSine (split, lowTone, 250, 0.5f);
        check (std::abs (measureRms (lowTone) - 0.35355f) < 0.015f,
               "split-band mode preserves low-frequency audio");

        ShunDeEssProcessor listen;
        listen.prepareToPlay (48000, 256);
        set (listen, "listen", 1);
        set (listen, "mix", 0);
        juce::AudioBuffer<float> lowListen (2, 8192);
        renderSine (listen, lowListen, 500, 0.5f);
        check (measureRms (lowListen) < 0.03f, "listen mode auditions the detector band");

        ShunDeEssProcessor bypass;
        bypass.prepareToPlay (48000, 256);
        set (bypass, "bypass", 1);
        juce::AudioBuffer<float> bypassed (2, 512);
        for (int i = 0; i < bypassed.getNumSamples(); ++i)
            for (int ch = 0; ch < bypassed.getNumChannels(); ++ch)
                bypassed.setSample (ch, i, 0.2f * std::sin (static_cast<float> (i) * 0.13f));
        juce::AudioBuffer<float> original (2, 512);
        for (int ch = 0; ch < original.getNumChannels(); ++ch)
            original.copyFrom (ch, 0, bypassed, ch, 0, bypassed.getNumSamples());
        juce::MidiBuffer midi;
        bypass.processBlock (bypassed, midi);
        bool identical = true;
        for (int ch = 0; ch < bypassed.getNumChannels(); ++ch)
            identical = identical && std::memcmp (bypassed.getReadPointer (ch), original.getReadPointer (ch),
                static_cast<size_t> (bypassed.getNumSamples()) * sizeof (float)) == 0;
        check (identical, "bypass preserves the input samples");

        if (argc > 1 && juce::String (argv[1]) == "--editor")
        {
            ShunDeEssProcessor editorProcessor;
            std::unique_ptr<juce::AudioProcessorEditor> editor (editorProcessor.createEditor());
            check (editor && editor->getWidth() == 548 && editor->getHeight() == 726,
                   "editor matches the YinYang compressor layout dimensions");
            check (editor->getNumChildComponents() >= 20, "editor contains all de-esser controls");
            for (auto* child : editor->getChildren())
                check (editor->getLocalBounds().contains (child->getBounds()), "editor control fits the shared layout");
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            check (image.isValid(), "de-esser editor renders to an image");
            if (argc > 2)
            {
                auto stream = juce::File (argv[2]).createOutputStream();
                check (stream != nullptr, "editor preview destination opens");
                check (juce::PNGImageFormat().writeImageToStream (image, *stream), "editor preview writes");
            }
        }
        std::cout << checks << " SHUN DE-ESS processor checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
