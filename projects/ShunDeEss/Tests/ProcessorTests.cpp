#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

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
    constexpr std::array<Example, 6> examples {{
        { "wideband-default.png", 0, 6500, 1.4f, 6500, 1.2f, 8, false },
        { "wideband-high-shelf.png", 0, 9500, 2.4f, 9000, 3.0f, 14, false },
        { "split-tight-bell.png", 1, 8000, 2.6f, 7500, 3.2f, 16, false },
        { "split-wide-bell.png", 1, 5000, 0.8f, 5500, 0.7f, 10, false },
        { "brilliant-spectral.png", 0, 8500, 2.8f, 7500, 2.6f, 12, true },
        { "smooth-spectral.png", 1, 8500, 2.8f, 7500, 2.6f, 12, true }
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
    std::vector<float> smartInputSamples;
    std::vector<float> smartOutputSamples;
};

ModeReport runMockMode (int mode, bool smartMode, float attenuationQ = 2.8f)
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
    set (processor, "attenuationQ", attenuationQ);
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
    std::vector<float> smartInputSamples, smartOutputSamples;
    if (smartMode)
    {
        smartInputSamples.reserve (8192);
        smartOutputSamples.reserve (8192);
    }

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
            {
                inputEnergy += input * input;
                if (smartMode)
                    smartInputSamples.push_back (input);
            }
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
                if (smartMode)
                    smartOutputSamples.push_back (block.getSample (0, i));
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
             static_cast<float> (std::sqrt (outputEnergy / 8192.0)),
             std::move (smartInputSamples), std::move (smartOutputSamples) };
}

void writeBrilliantSpectrumCsv (const juce::File& destination, const ModeReport& smart)
{
    constexpr int fftSize = 8192;
    constexpr double pi = 3.14159265358979323846;
    if (smart.smartInputSamples.size() != fftSize || smart.smartOutputSamples.size() != fftSize)
        throw std::runtime_error ("Brilliant mock signal is missing samples for its measured spectrum");

    juce::dsp::FFT fft (13);
    std::vector<float> inputSpectrum (static_cast<size_t> (fftSize * 2), 0.0f);
    std::vector<float> outputSpectrum (static_cast<size_t> (fftSize * 2), 0.0f);
    double windowSum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos (2.0 * pi * i / (fftSize - 1));
        inputSpectrum[static_cast<size_t> (i)] = smart.smartInputSamples[static_cast<size_t> (i)] * static_cast<float> (window);
        outputSpectrum[static_cast<size_t> (i)] = smart.smartOutputSamples[static_cast<size_t> (i)] * static_cast<float> (window);
        windowSum += window;
    }
    fft.performFrequencyOnlyForwardTransform (inputSpectrum.data());
    fft.performFrequencyOnlyForwardTransform (outputSpectrum.data());

    destination.deleteFile();
    juce::FileOutputStream stream (destination);
    if (! stream.openedOk() || ! stream.writeText ("frequency_hz,input_dbfs,output_dbfs,change_db\n", false, false, "\n"))
        throw std::runtime_error ("could not create Brilliant spectral measurement CSV");
    for (int bin = 1; bin <= fftSize / 2; ++bin)
    {
        const auto frequency = static_cast<float> (bin) * 48000.0f / fftSize;
        const auto inputDb = 20.0f * std::log10 (std::max (inputSpectrum[static_cast<size_t> (bin)]
                                                * static_cast<float> (2.0 / windowSum), 1.0e-9f));
        const auto outputDb = 20.0f * std::log10 (std::max (outputSpectrum[static_cast<size_t> (bin)]
                                                 * static_cast<float> (2.0 / windowSum), 1.0e-9f));
        const auto row = juce::String (frequency, 2) + "," + juce::String (inputDb, 4) + ","
                       + juce::String (outputDb, 4) + "," + juce::String (outputDb - inputDb, 4) + "\n";
        if (! stream.writeText (row, false, false, "\n"))
            throw std::runtime_error ("could not write Brilliant spectral measurement CSV");
    }
}

struct RollingMeasurements
{
    float detailChangeDb = 0.0f;
    float sibilanceChangeDb = 0.0f;
    float idleChangeDb = 0.0f;
};

RollingMeasurements writeRollingModeSpectrogram (const juce::File& destination, float attenuationQ,
                                                  int mode, bool smartMode, bool pulseFocused = false,
                                                  float detectorQ = 2.2f)
{
    constexpr int fftSize = 2048, hop = 512, totalSamples = 48000 * 4;
    constexpr double pi = 3.14159265358979323846;
    ShunDeEssProcessor processor;
    processor.prepareToPlay (48000, hop);
    set (processor, "mode", static_cast<float> (mode));
    set (processor, "smart", smartMode ? 1.0f : 0.0f);
    set (processor, "threshold", smartMode && ! pulseFocused ? -48.0f : -30.0f);
    set (processor, "depth", 12.0f); set (processor, "frequency", 7800.0f);
    set (processor, "q", detectorQ); set (processor, "attenuationFrequency", 7200.0f);
    set (processor, "attenuationQ", attenuationQ); set (processor, "attack", 0.2f); set (processor, "release", 10.0f);
    std::vector<float> input (totalSamples), output (totalSamples);
    std::mt19937 random (0x5eedu); std::normal_distribution<float> noise (0.0f, 0.055f);
    float sibilanceLowpass = 0.0f;
    const auto highpassCoefficient = 1.0f - std::exp (-2.0f * pi * 4500.0f / 48000.0f);
    juce::AudioBuffer<float> block (2, hop); juce::MidiBuffer midi;
    for (int offset = 0; offset < totalSamples; offset += hop)
    {
        for (int i = 0; i < hop; ++i)
        {
            const int n = offset + i; const float t = static_cast<float> (n) / 48000.0f;
            const auto white = noise (random);
            sibilanceLowpass += highpassCoefficient * (white - sibilanceLowpass);
            const auto highpassedNoise = white - sibilanceLowpass;
            const auto pulseEnvelope = [&] (float start, float duration)
            {
                const auto phase = (t - start) / duration;
                if (phase <= 0.0f || phase >= 1.0f) return 0.0f;
                const auto raisedCosine = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * phase));
                return raisedCosine * raisedCosine;
            };
            const auto sibilancePulse = std::max (pulseEnvelope (0.82f, 0.42f),
                                                   pulseEnvelope (2.05f, 0.38f));
            const float sweep = 4300.0f + 6900.0f * t / 4.0f;
            const auto fadeInPhase = std::clamp ((t - 0.35f) / 0.25f, 0.0f, 1.0f);
            const auto fadeOutPhase = std::clamp ((3.95f - t) / 0.4f, 0.0f, 1.0f);
            const auto fadeIn = 0.5f - 0.5f * std::cos (static_cast<float> (pi) * fadeInPhase);
            const auto fadeOut = 0.5f - 0.5f * std::cos (static_cast<float> (pi) * fadeOutPhase);
            const auto sweepWindow = std::min (fadeIn, fadeOut);
            const float detailLevel = smartMode && ! pulseFocused ? 0.16f : 0.005f;
            const float detail = detailLevel * sweepWindow * (1.0f - sibilancePulse)
                * std::sin (2.0 * pi * sweep * t);
            const float sample = 0.045f * white + 3.5f * sibilancePulse * highpassedNoise
                + 0.08f * std::sin (2.0 * pi * 500.0f * t) + detail;
            input[static_cast<size_t> (n)] = sample;
            block.setSample (0, i, sample); block.setSample (1, i, sample);
        }
        processor.processBlock (block, midi);
        for (int i = 0; i < hop; ++i) output[static_cast<size_t> (offset + i)] = block.getSample (0, i);
    }
    juce::dsp::FFT fft (11);
    std::vector<float> inFft (fftSize * 2, 0.0f), outFft (fftSize * 2, 0.0f);
    destination.deleteFile();
    juce::FileOutputStream stream (destination);
    if (! stream.openedOk() || !stream.writeText ("time_s,frequency_hz,input_dbfs,output_dbfs,change_db\n", false, false, "\n"))
        throw std::runtime_error ("could not create rolling mode spectrogram CSV");
    double detailChangeSum = 0.0;
    int detailChangeCount = 0;
    double pulseChangeSum = 0.0;
    int pulseChangeCount = 0;
    double idleChangeSum = 0.0;
    int idleChangeCount = 0;
    for (int end = fftSize; end <= totalSamples; end += hop)
    {
        double windowSum = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const float w = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * i / (fftSize - 1)));
            inFft[static_cast<size_t> (i)] = input[static_cast<size_t> (end - fftSize + i)] * w;
            outFft[static_cast<size_t> (i)] = output[static_cast<size_t> (end - fftSize + i)] * w;
            windowSum += w;
        }
        fft.performFrequencyOnlyForwardTransform (inFft.data()); fft.performFrequencyOnlyForwardTransform (outFft.data());
        const auto frameTime = static_cast<float> (end) / 48000.0f;
        const auto inPulseCenter = (frameTime >= 0.92f && frameTime <= 1.14f)
                                || (frameTime >= 2.13f && frameTime <= 2.33f);
        const auto inQuietBaseline = (frameTime >= 1.55f && frameTime <= 1.85f)
                                  || (frameTime >= 2.55f && frameTime <= 2.85f);
        if (inPulseCenter || inQuietBaseline)
        {
            double inputPower = 0.0, outputPower = 0.0;
            for (int bin = 96; bin <= 256; bin += 4)
            {
                inputPower += std::pow (inFft[static_cast<size_t> (bin)], 2.0f);
                outputPower += std::pow (outFft[static_cast<size_t> (bin)], 2.0f);
            }
            const auto change = 10.0 * std::log10 (std::max (outputPower, 1.0e-18)
                                                     / std::max (inputPower, 1.0e-18));
            if (inPulseCenter)
            {
                pulseChangeSum += change;
                ++pulseChangeCount;
            }
            else
            {
                idleChangeSum += change;
                ++idleChangeCount;
            }
        }
        for (int bin = 85; bin <= 683; bin += 4)
        {
            const float f = static_cast<float> (bin) * 48000.0f / fftSize;
            const float inDb = 20.0f * std::log10 (std::max (inFft[static_cast<size_t> (bin)] * static_cast<float> (2.0 / windowSum), 1.0e-8f));
            const float outDb = 20.0f * std::log10 (std::max (outFft[static_cast<size_t> (bin)] * static_cast<float> (2.0 / windowSum), 1.0e-8f));
            const auto time = static_cast<float> (end) / 48000.0f;
            const auto sweptFrequency = 4300.0f + 6900.0f * time / 4.0f;
            if (time > 0.6f && time < 3.5f && std::abs (f - sweptFrequency) < 200.0f)
            {
                detailChangeSum += outDb - inDb;
                ++detailChangeCount;
            }
            const auto row = juce::String (time, 3) + "," + juce::String (f, 1) + ","
                + juce::String (inDb, 2) + "," + juce::String (outDb, 2) + "," + juce::String (outDb - inDb, 2) + "\n";
            if (!stream.writeText (row, false, false, "\n")) throw std::runtime_error ("could not write rolling Brilliant spectrogram CSV");
        }
    }
    return { static_cast<float> (detailChangeSum / std::max (detailChangeCount, 1)),
             static_cast<float> (pulseChangeSum / std::max (pulseChangeCount, 1)),
             static_cast<float> (idleChangeSum / std::max (idleChangeCount, 1)) };
}

void generateAnalysisReport (const juce::File& destination)
{
    const auto wide = runMockMode (0, false);
    const auto split = runMockMode (1, false);
    const auto smart = runMockMode (0, true);
    const auto smooth = runMockMode (1, true);
    constexpr std::array<float, 4> smartQValues { 0.5f, 1.2f, 2.8f, 4.0f };
    std::array<ModeReport, smartQValues.size()> qReports;
    for (size_t i = 0; i < smartQValues.size(); ++i)
        qReports[i] = runMockMode (0, true, smartQValues[i]);
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
    auto report = juce::String ("# SHUN DE-ESS detector and attenuator mock-signal report\n\n")
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
        + "| Brilliant | " + juce::String (smart.grDb, 2) + " | "
        + juce::String (100.0f * (1.0f - smart.outputRms / std::max (smart.inputRms, 1.0e-9f)), 1)
        + "% | " + juce::String (smart.lowToneGain * 100.0f, 1)
        + "% | " + juce::String (smart.detailToneGain * 100.0f, 1) + "% |\n"
        + "| Smooth | " + juce::String (smooth.grDb, 2) + " | "
        + juce::String (100.0f * (1.0f - smooth.outputRms / std::max (smooth.inputRms, 1.0e-9f)), 1)
        + "% | " + juce::String (smooth.lowToneGain * 100.0f, 1)
        + "% | " + juce::String (smooth.detailToneGain * 100.0f, 1) + "% |\n\n"
        + "Detector controls: 7.8 kHz center, Q 2.2, −48 dB threshold. Attenuator controls: 7.2 kHz center, Q 2.8, "
          "12 dB maximum depth. The retained-detail column is a sine/cosine projection of the final 8192 samples; "
          "it shows how much of the isolated 7.8 kHz tone survives broadband attenuation. Brilliant is expected to "
          "retain more of that tone than Wideband while still reducing the broadband mock input. "
          "Smooth reverses Brilliant's narrow-band restoration for additional detail suppression.\n\n"
          "### Brilliant attenuator-Q detail check\n\n"
          "| Attenuator Q | Broadband RMS reduction | 7.8 kHz detail retained |\n"
          "|---:|---:|---:|\n"
        + [&]
          {
              juce::String rows;
              for (size_t i = 0; i < smartQValues.size(); ++i)
                  rows += "| " + juce::String (smartQValues[i], 1) + " | "
                      + juce::String (100.0f * (1.0f - qReports[i].outputRms
                          / std::max (qReports[i].inputRms, 1.0e-9f)), 1) + "% | "
                      + juce::String (qReports[i].detailToneGain * 100.0f, 1) + "% |\n";
              return rows + "\n";
          } ()
          + "This sweeps Attenuator Q while holding the mock signal fixed. The measured change confirms that Q "
            "affects detail retention: the narrower high-Q restoration retains more of this tone, while higher Q "
            "increases broadband attenuation. The phase plot shows the shelf's minimum-phase rotation; at low Q, "
            "the original RBJ slope setting allowed passband peaking. The shelf slope is now capped at 1.0, and a "
            "processor test verifies that Q 0.5 does not boost the shelf passband. The Brilliant restoration retains "
            "its additive band-pass path, whose phase response is plotted alongside the shelf phase. Four rolling "
            "heatmaps compare Q 0.5, 1.2, 2.8, and 4.0 using distinct measured runs. The rolling input is a "
            "four-second time-varying mock with two gated 4.5 kHz high-pass noise pulses to mimic sibilant "
            "consonants. The swept detail tone is gated out during the pulses so their attenuation reads clearly; "
            "the signal combines a 4.3–11.2 kHz swept detail tone, two high-pass noise pulses, broadband noise, "
            "and a 500 Hz body. The test verifies that integrated 4.5–12 kHz output energy is lower during "
            "each pulse at all four Q settings. "
            "Source data files are `smart-rolling-q-*.csv`; `smart-rolling-spectrogram.csv` is the Q 2.8 run.\n\n"
          "Plotted report: [PNG](detector-attenuator-mock-report.png) · "
          "[PDF](detector-attenuator-mock-report.pdf). Regenerate with `Tests/generate_analysis_report.py`.\n";

    const auto spectrumCsv = destination.getSiblingFile ("smart-spectral-spectrum.csv");
    const auto rollingCsv = destination.getSiblingFile ("smart-rolling-spectrogram.csv");
    if (! destination.getParentDirectory().createDirectory())
        throw std::runtime_error ("could not create detector/attenuator report directory");
    writeBrilliantSpectrumCsv (spectrumCsv, smart);
    constexpr std::array<float, 4> heatmapQValues { 0.5f, 1.2f, 2.8f, 4.0f };
    std::array<RollingMeasurements, heatmapQValues.size()> heatmapMeasurements {};
    for (size_t i = 0; i < heatmapQValues.size(); ++i)
    {
        const auto file = destination.getSiblingFile ("smart-rolling-q-" + juce::String (heatmapQValues[i], 1) + ".csv");
        heatmapMeasurements[i] = writeRollingModeSpectrogram (file, heatmapQValues[i], 0, true);
        if (i == 2)
        {
            rollingCsv.deleteFile();
            file.copyFileTo (rollingCsv);
        }
    }
    report += "\n### Sibilance pulse attenuation\n\n"
              "| Attenuator Q | 4.5–12 kHz output − input during pulses |\n"
              "|---:|---:|\n";
    for (size_t i = 0; i < heatmapQValues.size(); ++i)
        report += "| " + juce::String (heatmapQValues[i], 1) + " | "
            + juce::String (heatmapMeasurements[i].sibilanceChangeDb, 2) + " dB |\n";
    report += "\nNegative values indicate attenuation. The blue shaded regions in the rolling heatmaps "
              "mark these two noise pulses; the measured band energy should stay below 0 dB for every Q.\n\n";
    report += "### Time-varying pulse test across all modes\n\n"
              "The same seeded input and Q sweep were processed in all four modes. The "
              "detector threshold is −30 dB; the swept detail tone is kept below it so the quiet baseline releases to "
              "unity, while the gated noise bursts cross threshold. Values are integrated output − input "
              "energy from 4.5–12 kHz during pulse centers and quiet intervals.\n\n"
              "| Mode | Q 0.5 (pulse / quiet) | Q 1.2 (pulse / quiet) | Q 2.8 (pulse / quiet) | Q 4.0 (pulse / quiet) |\n"
              "|---|---:|---:|---:|---:|\n";
    std::array<std::array<RollingMeasurements, heatmapQValues.size()>, 4> pulseModeMeasurements {};
    const std::array<const char*, 4> pulseModeNames { "Wideband", "Split-band", "Brilliant", "Smooth" };
    for (size_t modeIndex = 0; modeIndex < pulseModeNames.size(); ++modeIndex)
    {
        const auto mode = modeIndex == 1 || modeIndex == 3 ? 1 : 0;
        const auto smartMode = modeIndex >= 2;
        report += "| " + juce::String (pulseModeNames[modeIndex]) + " |";
        for (size_t qIndex = 0; qIndex < heatmapQValues.size(); ++qIndex)
        {
            const auto stem = modeIndex == 0 ? "wideband" : modeIndex == 1 ? "splitband"
                            : modeIndex == 2 ? "smart-pulse" : "smooth-pulse";
            const auto file = destination.getSiblingFile (juce::String (stem) + "-rolling-q-"
                + juce::String (heatmapQValues[qIndex], 1) + ".csv");
            pulseModeMeasurements[modeIndex][qIndex] = writeRollingModeSpectrogram (
                file, heatmapQValues[qIndex], mode, smartMode, true);
            const auto& measurement = pulseModeMeasurements[modeIndex][qIndex];
            report += " " + juce::String (measurement.sibilanceChangeDb, 2) + " / "
                + juce::String (measurement.idleChangeDb, 2) + " dB |";
        }
        report += "\n";
    }
    report += "\n### Brilliant detector-Q time-response test\n\n"
              "Brilliant pulse-focused runs hold Attenuator Q at 2.8 and vary Detector Q. Values are integrated "
              "4.5–12 kHz output − input during pulse centers and quiet intervals.\n\n"
              "| Detector Q | Pulse change | Quiet change |\n"
              "|---:|---:|---:|\n";
    std::array<RollingMeasurements, heatmapQValues.size()> detectorQMeasurements {};
    for (size_t i = 0; i < heatmapQValues.size(); ++i)
    {
        const auto detectorQ = heatmapQValues[i];
        const auto file = destination.getSiblingFile ("smart-detector-q-" + juce::String (detectorQ, 1) + ".csv");
        detectorQMeasurements[i] = writeRollingModeSpectrogram (file, 2.8f, 0, true, true, detectorQ);
        report += "| " + juce::String (detectorQ, 1) + " | "
            + juce::String (detectorQMeasurements[i].sibilanceChangeDb, 2) + " dB | "
            + juce::String (detectorQMeasurements[i].idleChangeDb, 2) + " dB |\n";
    }
    report += "\n[Brilliant detector-Q pulse response plots](smart-detector-q-report.png) · "
              "[PDF](smart-detector-q-report.pdf).\n\n";
    report += "\nPulse values should be more negative than quiet-baseline values, showing detector-linked "
              "attenuation that follows the bursts. "
              "[Four-mode pulse plots](wide-split-pulse-test-report.png) · "
              "[PDF](wide-split-pulse-test-report.pdf).\n\n";
    if (! destination.replaceWithText (report.trimEnd() + "\n", false, false, "\n"))
        throw std::runtime_error ("could not write detector/attenuator mock-signal report");
    check (wide.grDb > 0.0f && split.grDb > 0.0f && smart.grDb > 0.0f && smooth.grDb > 0.0f,
           "all detector modes report gain reduction on the mock sibilance input");
    check (smooth.detailToneGain < smart.detailToneGain,
           "Smooth suppresses more narrow-band detail than Brilliant");
    check (detectorRejectionDb > 10.0f, "detector Listen path favors its selected frequency over out-of-band audio");
    check (smart.outputRms < smart.inputRms * 0.90f, "Brilliant attenuates the broadband mock input");
    check (smart.detailToneGain > wide.detailToneGain * 1.05f,
           "Brilliant retains more isolated high-frequency detail than Wideband");
    check (qReports.back().detailToneGain > qReports.front().detailToneGain + 0.02f,
           "Brilliant attenuator Q measurably affects retained detail");
    std::array<float, heatmapQValues.size()> heatmapDetailChanges {};
    std::array<float, heatmapQValues.size()> sibilanceChanges {};
    for (size_t i = 0; i < heatmapMeasurements.size(); ++i)
    {
        heatmapDetailChanges[i] = heatmapMeasurements[i].detailChangeDb;
        sibilanceChanges[i] = heatmapMeasurements[i].sibilanceChangeDb;
    }
    const auto heatmapMinMax = std::minmax_element (heatmapDetailChanges.begin(), heatmapDetailChanges.end());
    check (*heatmapMinMax.second - *heatmapMinMax.first > 0.1f,
           "rolling heatmap changes with Brilliant attenuator Q");
    check (std::all_of (sibilanceChanges.begin(), sibilanceChanges.end(), [] (float change) { return change < 0.0f; }),
           "Brilliant attenuates the gated sibilance noise pulses at every tested Q");
    for (size_t modeIndex = 0; modeIndex < pulseModeNames.size(); ++modeIndex)
    {
        const auto modeAttenuatesEveryPulse = std::all_of (
            pulseModeMeasurements[modeIndex].begin(), pulseModeMeasurements[modeIndex].end(),
            [] (const auto& measurements)
            {
                return measurements.sibilanceChangeDb < -0.25f
                    && measurements.sibilanceChangeDb < measurements.idleChangeDb - 0.5f;
            });
        check (modeAttenuatesEveryPulse, modeIndex == 0
            ? "Wideband attenuates the gated sibilance pulses at every tested Q"
            : modeIndex == 1 ? "Split-band attenuates the gated sibilance pulses at every tested Q"
            : modeIndex == 2 ? "Brilliant attenuates the gated sibilance pulses above its quiet baseline at every tested Q"
                             : "Smooth attenuates the gated sibilance pulses above its quiet baseline at every tested Q");
    }
    const auto detectorPulseExtrema = std::minmax_element (detectorQMeasurements.begin(), detectorQMeasurements.end(),
        [] (const auto& a, const auto& b) { return a.sibilanceChangeDb < b.sibilanceChangeDb; });
    check (std::all_of (detectorQMeasurements.begin(), detectorQMeasurements.end(), [] (const auto& measurements)
          { return measurements.sibilanceChangeDb < measurements.idleChangeDb - 0.5f; }),
          "Brilliant detector Q settings all attenuate pulses above their quiet baseline");
    check (detectorPulseExtrema.second->sibilanceChangeDb - detectorPulseExtrema.first->sibilanceChangeDb > 0.05f,
           "Brilliant detector Q changes measured pulse response");
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
            std::cout << "6 SHUN response previews generated\n";
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
        for (int mode = 0; mode < 4; ++mode)
        {
            ShunDeEssProcessor saved;
            set (saved, "mode", static_cast<float> (mode % 2));
            set (saved, "smart", mode >= 2 ? 1.0f : 0.0f);
            juce::MemoryBlock state;
            saved.getStateInformation (state);
            ShunDeEssProcessor restored;
            restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            check ((restored.parameters.getRawParameterValue ("mode")->load() >= 0.5f) == (mode % 2 == 1)
                   && (restored.parameters.getRawParameterValue ("smart")->load() >= 0.5f) == (mode >= 2),
                   "all four modes survive a session state round trip");
        }
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

        ShunDeEssProcessor lowQShelf;
        lowQShelf.prepareToPlay (48000, 256);
        set (lowQShelf, "frequency", 3400.0f);
        set (lowQShelf, "attenuationFrequency", 7200.0f);
        set (lowQShelf, "attenuationQ", 0.5f);
        set (lowQShelf, "threshold", -48.0f);
        set (lowQShelf, "depth", 12.0f);
        set (lowQShelf, "attack", 0.2f);
        set (lowQShelf, "release", 10.0f);
        juce::AudioBuffer<float> lowQSine (2, 32768);
        renderSine (lowQShelf, lowQSine, 3400.0f, 0.25f);
        check (measureToneAmplitude (lowQSine, 3400.0f) <= 0.252f,
               "low-Q wideband attenuation shelf does not create passband gain");

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
        bool finiteBrilliant = true;
        for (int ch = 0; ch < spectralNoise.getNumChannels(); ++ch)
            for (int i = 0; i < spectralNoise.getNumSamples(); ++i)
                finiteBrilliant = finiteBrilliant && std::isfinite (spectralNoise.getSample (ch, i));
        check (finiteBrilliant, "Brilliant FFT detector and zero-latency filterbank remain finite on broadband input");
        check (measureRms (spectralNoise) < noiseRmsBefore * 0.99f,
               "Brilliant mode attenuates spectrally flat broadband content");
        check (smart.getLatencySamples() == 0, "Brilliant mode preserves zero reported latency");

        const auto brilliantReport = runMockMode (0, true);
        const auto smoothReport = runMockMode (1, true);
        check (smoothReport.grDb > 0.0f, "Smooth uses the spectral detector");
        check (smoothReport.outputRms < smoothReport.inputRms,
               "Smooth attenuates the same processed band as Brilliant");
        check (smoothReport.detailToneGain < brilliantReport.detailToneGain,
               "Smooth reverses Brilliant's narrow-band detail restoration");
        check (std::abs (smoothReport.lowToneGain - brilliantReport.lowToneGain) < 0.03f,
               "Smooth preserves the same low-frequency body as Brilliant");

        if (argc > 1 && juce::String (argv[1]) == "--editor")
        {
            ShunDeEssProcessor editorProcessor;
            std::unique_ptr<juce::AudioProcessorEditor> editor (editorProcessor.createEditor());
            editor->setVisible (true);
            check (editor && editor->getWidth() == 548 && editor->getHeight() == 741,
                   "editor matches the YinYang compressor layout dimensions");
            check (editor->getNumChildComponents() >= 20, "editor contains all de-esser controls");
            int tooltipWindows = 0, hintedControls = 0;
            for (auto* child : editor->getChildren())
            {
                if (dynamic_cast<juce::TooltipWindow*> (child) != nullptr) ++tooltipWindows;
                if (dynamic_cast<juce::Slider*> (child) == nullptr
                    && dynamic_cast<juce::Button*> (child) == nullptr
                    && dynamic_cast<juce::Label*> (child) == nullptr) continue;
                ++hintedControls;
                auto* client = dynamic_cast<juce::TooltipClient*> (child);
                const auto hint = client != nullptr ? client->getTooltip() : juce::String();
                check (hint.isNotEmpty() && hint.endsWithChar ('.')
                       && hint.dropLastCharacters (1).indexOfChar ('.') < 0,
                       "each Shun control has a one-sentence hover hint");
                if (auto* slider = dynamic_cast<juce::Slider*> (child))
                    for (auto* nested : slider->getChildren())
                        if (auto* readout = dynamic_cast<juce::Label*> (nested))
                            check (readout->getTooltip() == hint, "Shun readout shares its control hint");
            }
            check (tooltipWindows == 1 && hintedControls == 24,
                   "Shun has one tooltip window and hints for every control and caption");
            juce::Slider* modeKnob = nullptr;
            for (auto* child : editor->getChildren())
                if (auto* slider = dynamic_cast<juce::Slider*> (child); slider && slider->getName() == "MODE")
                    modeKnob = slider;
            check (modeKnob != nullptr && modeKnob->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
                   "four-mode selector is a rotary knob");
            juce::Label* modeValueLabel = nullptr;
            for (auto* child : modeKnob->getChildren())
                if (auto* label = dynamic_cast<juce::Label*> (child))
                    modeValueLabel = label;
            check (modeValueLabel != nullptr && modeValueLabel->getText() == "WIDEBAND",
                   "the mode selector displays Wideband when the editor first opens");
            for (int mode = 0; mode < 4; ++mode)
            {
                modeKnob->setValue (mode, juce::sendNotificationSync);
                const auto splitSelected = editorProcessor.parameters.getRawParameterValue ("mode")->load() >= 0.5f;
                const auto spectral = editorProcessor.parameters.getRawParameterValue ("smart")->load() >= 0.5f;
                check (splitSelected == (mode % 2 == 1) && spectral == (mode >= 2),
                       "rotary mode position updates the compatible host parameters");
            }
            check (modeKnob->getTextFromValue (0) == "WIDEBAND"
                   && modeKnob->getTextFromValue (1) == "SPLIT"
                   && modeKnob->getTextFromValue (2) == "BRILLIANT"
                   && modeKnob->getTextFromValue (3) == "SMOOTH",
                   "rotary mode positions display their full names");
            modeKnob->setValue (0, juce::sendNotificationSync);
            const auto ebonySample = editor->createComponentSnapshot (editor->getLocalBounds());
            for (int mode = 1; mode < 4; ++mode)
            {
                modeKnob->setValue (mode, juce::sendNotificationSync);
                juce::Timer::callPendingTimersSynchronously();
                const auto sample = editor->createComponentSnapshot (editor->getLocalBounds());
                check (ebonySample.getPixelAt (10, 10) == sample.getPixelAt (10, 10)
                       && ebonySample.getPixelAt (34, 130) == sample.getPixelAt (34, 130)
                       && ebonySample.getPixelAt (350, 300) == sample.getPixelAt (350, 300),
                       "all modes share the Ebony background, panel, and meter palette");
            }
            modeKnob->setValue (0, juce::sendNotificationSync);
            editorProcessor.parameters.getParameter ("mode")->setValueNotifyingHost (1.0f);
            editorProcessor.parameters.getParameter ("smart")->setValueNotifyingHost (1.0f);
            const auto waitForMode = [&] (int expected)
            {
                for (int attempt = 0; attempt < 10 && juce::roundToInt (modeKnob->getValue()) != expected; ++attempt)
                {
                    juce::Thread::sleep (20);
                    juce::Timer::callPendingTimersSynchronously();
                }
            };
            waitForMode (3);
            check (juce::roundToInt (modeKnob->getValue()) == 3,
                   "the rotary mode selector follows host automation");
            modeKnob->setValue (0, juce::sendNotificationSync);
            waitForMode (0);
            check (juce::roundToInt (modeKnob->getValue()) == 0
                   && editorProcessor.parameters.getRawParameterValue ("mode")->load() < 0.5f
                   && editorProcessor.parameters.getRawParameterValue ("smart")->load() < 0.5f,
                   "the rotary mode selector returns to Wideband after host automation");
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
            const auto originalModeWidth = editor->getLocalArea (modeKnob, modeKnob->getLocalBounds()).getWidth();
            for (const auto size : { juce::Point<int> (274, 371), juce::Point<int> (658, 889) })
            {
                editor->setSize (size.x, size.y);
                const auto control = editor->getLocalArea (modeKnob, modeKnob->getLocalBounds());
                check (editor->getLocalBounds().contains (control)
                       && (size.x < 548 ? control.getWidth() < originalModeWidth
                                        : control.getWidth() > originalModeWidth),
                       "Shun controls scale and remain inside the editor");
                auto* hit = editor->getComponentAt (control.getCentre());
                check (hit == modeKnob || modeKnob->isParentOf (hit),
                       "Shun scaled controls remain clickable");
                check (editor->createComponentSnapshot (editor->getLocalBounds()).isValid(),
                       "Shun renders at both resize limits");
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
