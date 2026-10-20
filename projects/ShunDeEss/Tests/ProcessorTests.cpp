#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
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

float measureToneAmplitude (const juce::AudioBuffer<float>& buffer, float frequency, int channel = 0)
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double sampleRate = 48000;
    const auto start = buffer.getNumSamples() - 4096;
    double sine = 0, cosine = 0;
    for (int i = start; i < buffer.getNumSamples(); ++i)
    {
        const auto phase = 2.0 * pi * frequency * i / sampleRate;
        const auto sample = buffer.getSample (channel, i);
        sine += sample * std::sin (phase);
        cosine += sample * std::cos (phase);
    }
    return static_cast<float> (2.0 * std::hypot (sine, cosine) / 4096.0);
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

void generateResponsePreviews (const juce::File& directory)
{
    struct Example
    {
        const char* file;
        float mode, detectorFrequency, detectorQ, attenuationFrequency, attenuationQ, depth;
        bool smart;
    };
    constexpr std::array<Example, 5> examples {{
        { "wideband-default.png", 0, 6500, 1.4f, 6500, 1.2f, 8, false },
        { "wideband-high-shelf.png", 0, 9500, 2.4f, 9000, 3.0f, 14, false },
        { "split-tight-bell.png", 1, 8000, 2.6f, 7500, 3.2f, 16, false },
        { "split-wide-bell.png", 1, 5000, 0.8f, 5500, 0.7f, 10, false },
        { "smart-spectral.png", 0, 8500, 2.8f, 7500, 2.6f, 12, true }
    }};

    if (! directory.createDirectory())
        throw std::runtime_error ("could not create response preview directory");

    for (const auto& example : examples)
    {
        ShunDeEssProcessor processor;
        const auto setParameter = [&] (const char* id, float value)
        {
            auto* parameter = processor.parameters.getParameter (id);
            if (parameter == nullptr)
                throw std::runtime_error ("response preview parameter is missing");
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };
        setParameter ("mode", example.mode);
        setParameter ("frequency", example.detectorFrequency);
        setParameter ("q", example.detectorQ);
        setParameter ("attenuationFrequency", example.attenuationFrequency);
        setParameter ("attenuationQ", example.attenuationQ);
        setParameter ("depth", example.depth);
        setParameter ("smart", example.smart ? 1.0f : 0.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        if (editor == nullptr)
            throw std::runtime_error ("could not create SHUN response preview editor");
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        juce::MemoryOutputStream png;
        if (! image.isValid() || ! juce::PNGImageFormat().writeImageToStream (image, png)
            || ! directory.getChildFile (example.file).replaceWithData (png.getData(), png.getDataSize()))
            throw std::runtime_error ("could not write SHUN response preview");
    }
}

struct ModeReport
{
    float grDb = 0.0f;
    float lowToneGain = 0.0f;
    float detailToneGain = 0.0f;
    float inputRms = 0.0f;
    float outputRms = 0.0f;
};

ModeReport runMockMode (int mode, bool smartMode)
{
    ShunDeEssProcessor processor;
    processor.prepareToPlay (48000, 256);
    set (processor, "mode", static_cast<float> (mode));
    set (processor, "smart", smartMode ? 1.0f : 0.0f);
    set (processor, "threshold", -48.0f);
    set (processor, "depth", 12.0f);
    set (processor, "frequency", 7800.0f);
    set (processor, "q", 2.2f);
    set (processor, "attenuationFrequency", 7200.0f);
    set (processor, "attenuationQ", 2.8f);
    set (processor, "attack", 0.2f);
    set (processor, "release", 10.0f);

    constexpr int blockSize = 256;
    constexpr int sampleCount = 65536;
    constexpr int detailStart = sampleCount / 2;
    constexpr double pi = 3.14159265358979323846;
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;
    std::mt19937 random (0x5eedu);
    std::normal_distribution<float> noise (0.0f, 0.075f);
    double inputLow = 0.0, outputLowSine = 0.0, outputLowCosine = 0.0;
    double inputDetail = 0.0, outputDetailSine = 0.0, outputDetailCosine = 0.0;
    double inputEnergy = 0.0, outputEnergy = 0.0;

    for (int offset = 0; offset < sampleCount; offset += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto sampleIndex = offset + i;
            const auto low = 0.12f * std::sin (2.0 * pi * 500.0 * sampleIndex / 48000.0);
            const auto detail = sampleIndex >= detailStart
                ? 0.11f * std::sin (2.0 * pi * 7800.0 * sampleIndex / 48000.0) : 0.0f;
            const auto input = noise (random) + low + detail;
            if (sampleIndex >= sampleCount - 8192)
                inputEnergy += input * input;
            for (int ch = 0; ch < 2; ++ch)
                block.setSample (ch, i, input);

            if (sampleIndex >= sampleCount - 8192)
            {
                const auto lowPhase = 2.0 * pi * 500.0 * sampleIndex / 48000.0;
                const auto detailPhase = 2.0 * pi * 7800.0 * sampleIndex / 48000.0;
                inputLow += low * std::sin (lowPhase);
                inputDetail += detail * std::sin (detailPhase);
            }
        }
        processor.processBlock (block, midi);
        if (offset >= sampleCount - 8192)
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sampleIndex = offset + i;
                const auto lowPhase = 2.0 * pi * 500.0 * sampleIndex / 48000.0;
                const auto detailPhase = 2.0 * pi * 7800.0 * sampleIndex / 48000.0;
                outputLowSine += block.getSample (0, i) * std::sin (lowPhase);
                outputLowCosine += block.getSample (0, i) * std::cos (lowPhase);
                outputDetailSine += block.getSample (0, i) * std::sin (detailPhase);
                outputDetailCosine += block.getSample (0, i) * std::cos (detailPhase);
                outputEnergy += block.getSample (0, i) * block.getSample (0, i);
            }
    }

    constexpr double projectionScale = 2.0 / 8192.0;
    const auto detailOut = projectionScale * std::hypot (outputDetailSine, outputDetailCosine);
    const auto detailIn = projectionScale * std::abs (inputDetail);
    const auto lowOut = projectionScale * std::hypot (outputLowSine, outputLowCosine);
    const auto lowIn = projectionScale * std::abs (inputLow);
    return { processor.reduction.load(), static_cast<float> (lowOut / std::max (lowIn, 1.0e-9)),
             static_cast<float> (detailOut / std::max (detailIn, 1.0e-9)),
             static_cast<float> (std::sqrt (inputEnergy / 8192.0)),
             static_cast<float> (std::sqrt (outputEnergy / 8192.0)) };
}

void generateAnalysisReport (const juce::File& destination)
{
    const auto wide = runMockMode (0, false);
    const auto split = runMockMode (1, false);
    const auto smart = runMockMode (0, true);
    ShunDeEssProcessor detector;
    detector.prepareToPlay (48000, 256);
    set (detector, "frequency", 7800.0f);
    set (detector, "q", 2.2f);
    set (detector, "listen", 1.0f);
    juce::AudioBuffer<float> detectorInput (2, 16384);
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < detectorInput.getNumSamples(); ++i)
    {
        const auto sample = 0.2 * (std::sin (2.0 * pi * 500.0 * i / 48000.0)
                                 + std::sin (2.0 * pi * 7800.0 * i / 48000.0));
        for (int ch = 0; ch < detectorInput.getNumChannels(); ++ch)
            detectorInput.setSample (ch, i, static_cast<float> (sample));
    }
    juce::MidiBuffer detectorMidi;
    detector.processBlock (detectorInput, detectorMidi);
    const auto lowDetectorTone = measureToneAmplitude (detectorInput, 500.0f);
    const auto inBandDetectorTone = measureToneAmplitude (detectorInput, 7800.0f);
    const auto detectorRejectionDb = 20.0f * std::log10 (inBandDetectorTone / std::max (lowDetectorTone, 1.0e-9f));
    const auto report = juce::String ("# SHUN DE-ESS detector and attenuator mock-signal report\n\n")
        + "Generated by `ShunDeEssTests --analysis-report`. Input is deterministic seeded broadband noise, "
          "a 500 Hz body tone, and a 7.8 kHz detail tone introduced after the detector has settled. "
          "All processing uses 48 kHz stereo blocks of 256 samples.\n\n"
        + "The detector Listen path was also measured with equal-level 500 Hz and 7.8 kHz tones; at the "
          "7.8 kHz center setting, the in-band tone is " + juce::String (detectorRejectionDb, 1)
        + " dB above the out-of-band tone.\n\n"
        + "| Mode | Detector GR (dB) | Broadband RMS reduction | 500 Hz body retained | 7.8 kHz detail retained |\n"
          "|---|---:|---:|---:|---:|\n"
        + "| Wideband | " + juce::String (wide.grDb, 2) + " | "
        + juce::String (100.0f * (1.0f - wide.outputRms / std::max (wide.inputRms, 1.0e-9f)), 1)
        + "% | " + juce::String (wide.lowToneGain * 100.0f, 1)
        + "% | " + juce::String (wide.detailToneGain * 100.0f, 1) + "% |\n"
        + "| Split-band | " + juce::String (split.grDb, 2) + " | "
        + juce::String (100.0f * (1.0f - split.outputRms / std::max (split.inputRms, 1.0e-9f)), 1)
        + "% | " + juce::String (split.lowToneGain * 100.0f, 1)
        + "% | " + juce::String (split.detailToneGain * 100.0f, 1) + "% |\n"
        + "| Smart | " + juce::String (smart.grDb, 2) + " | "
        + juce::String (100.0f * (1.0f - smart.outputRms / std::max (smart.inputRms, 1.0e-9f)), 1)
        + "% | " + juce::String (smart.lowToneGain * 100.0f, 1)
        + "% | " + juce::String (smart.detailToneGain * 100.0f, 1) + "% |\n\n"
        + "Detector controls: 7.8 kHz center, Q 2.2, −48 dB threshold. Attenuator controls: 7.2 kHz center, Q 2.8, "
          "12 dB maximum depth. The retained-detail column is a sine/cosine projection of the final 8192 samples; "
          "it shows how much of the isolated 7.8 kHz tone survives broadband attenuation. Smart is expected to "
          "retain more of that tone than Wideband while still reducing the broadband mock input.\n";

    if (! destination.getParentDirectory().createDirectory() || ! destination.replaceWithText (report))
        throw std::runtime_error ("could not write detector/attenuator mock-signal report");
    check (wide.grDb > 0.0f && split.grDb > 0.0f && smart.grDb > 0.0f,
           "all detector modes report gain reduction on the mock sibilance input");
    check (detectorRejectionDb > 10.0f, "detector Listen path favors its selected frequency over out-of-band audio");
    check (smart.outputRms < smart.inputRms * 0.90f, "Smart attenuates the broadband mock input");
    check (smart.detailToneGain > wide.detailToneGain * 1.05f,
           "Smart retains more isolated high-frequency detail than Wideband");
}
}

int main (int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        if (argc > 2 && juce::String (argv[1]) == "--response-previews")
        {
            generateResponsePreviews (juce::File (argv[2]));
            std::cout << "5 SHUN response previews generated\n";
            return 0;
        }
        if (argc > 2 && juce::String (argv[1]) == "--analysis-report")
        {
            generateAnalysisReport (juce::File (argv[2]));
            std::cout << "SHUN DE-ESS detector/attenuator report generated: " << argv[2] << '\n';
            return 0;
        }
        ShunDeEssProcessor processor;
        check (processor.getParameters().size() == 14, "all de-esser parameters are registered");
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

        ShunDeEssProcessor shelf;
        shelf.prepareToPlay (48000, 256);
        set (shelf, "threshold", -42);
        set (shelf, "depth", 12);
        set (shelf, "attack", 0.2f);
        set (shelf, "release", 10);
        juce::AudioBuffer<float> twoTones (2, 16384);
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < twoTones.getNumSamples(); ++i)
        {
            const auto sample = 0.25 * std::sin (2.0 * pi * 500.0 * i / 48000.0)
                              + 0.25 * std::sin (2.0 * pi * 10000.0 * i / 48000.0);
            for (int ch = 0; ch < twoTones.getNumChannels(); ++ch)
                twoTones.setSample (ch, i, static_cast<float> (sample));
        }
        juce::MidiBuffer shelfMidi;
        shelf.processBlock (twoTones, shelfMidi);
        check (measureToneAmplitude (twoTones, 500) > 0.23f,
               "wideband high-shelf attenuation preserves frequencies below the shelf");
        check (measureToneAmplitude (twoTones, 10000) < 0.20f,
               "wideband high-shelf attenuation reduces frequencies above the shelf");

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

        ShunDeEssProcessor smart;
        smart.prepareToPlay (48000, 256);
        set (smart, "smart", 1);
        set (smart, "threshold", -48);
        set (smart, "depth", 8);
        juce::AudioBuffer<float> spectralNoise (2, 8192);
        std::mt19937 random (0x51a7u);
        std::uniform_real_distribution<float> noise (-0.2f, 0.2f);
        for (int i = 0; i < spectralNoise.getNumSamples(); ++i)
            for (int ch = 0; ch < spectralNoise.getNumChannels(); ++ch)
                spectralNoise.setSample (ch, i, noise (random));
        const auto noiseRmsBefore = measureRms (spectralNoise);
        smart.processBlock (spectralNoise, midi);
        bool finiteSmart = true;
        for (int ch = 0; ch < spectralNoise.getNumChannels(); ++ch)
            for (int i = 0; i < spectralNoise.getNumSamples(); ++i)
                finiteSmart = finiteSmart && std::isfinite (spectralNoise.getSample (ch, i));
        check (finiteSmart, "Smart FFT detector and zero-latency filterbank remain finite on broadband input");
        check (measureRms (spectralNoise) < noiseRmsBefore * 0.99f,
               "Smart mode attenuates spectrally flat broadband content");
        check (smart.getLatencySamples() == 0, "Smart mode preserves zero reported latency");

        if (argc > 1 && juce::String (argv[1]) == "--editor")
        {
            ShunDeEssProcessor editorProcessor;
            std::unique_ptr<juce::AudioProcessorEditor> editor (editorProcessor.createEditor());
            check (editor && editor->getWidth() == 548 && editor->getHeight() == 741,
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
