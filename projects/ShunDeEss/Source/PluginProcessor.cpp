#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr std::array<const char*, 12> parameterIds { "mode", "frequency", "q", "threshold", "depth",
    "attack", "release", "output", "mix", "stereoLink", "bypass", "listen" };
constexpr float pi = 3.14159265358979323846f;
}

juce::AudioProcessorValueTreeState::ParameterLayout ShunDeEssProcessor::createParameterLayout()
{
    using Float = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 },
        "De-ess mode", juce::StringArray { "Wideband", "Split-band" }, 0));
    const auto addFloat = [&] (const char* id, const char* title, float minimum, float maximum,
                               float interval, float initial, float centre, const char* unit)
    {
        juce::NormalisableRange<float> range (minimum, maximum, interval);
        if (centre > minimum && centre < maximum)
            range.setSkewForCentre (centre);
        layout.add (std::make_unique<Float> (juce::ParameterID { id, 1 }, title, range, initial,
            juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    addFloat ("frequency", "Sibilance frequency", 2000, 12000, 10, 6500, 6000, "Hz");
    addFloat ("q", "Detector bandwidth", 0.5f, 4, 0.01f, 1.4f, 1.2f, "Q");
    addFloat ("threshold", "Threshold", -60, 0, 0.1f, -28, -30, "dB");
    addFloat ("depth", "Maximum reduction", 0, 18, 0.1f, 8, 8, "dB");
    addFloat ("attack", "Attack", 0.1f, 20, 0.01f, 1, 2, "ms");
    addFloat ("release", "Release", 10, 500, 1, 80, 100, "ms");
    addFloat ("output", "Output", -18, 18, 0.1f, 0, 0, "dB");
    addFloat ("mix", "Mix", 0, 100, 0.1f, 100, 50, "%");
    addFloat ("stereoLink", "Stereo link", 0, 100, 1, 100, 50, "%");
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "listen", 1 }, "Listen", false));
    return layout;
}

ShunDeEssProcessor::ShunDeEssProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ShunDeEssState", createParameterLayout())
{
    for (size_t i = 0; i < raw.size(); ++i)
        raw[i] = parameters.getRawParameterValue (parameterIds[i]);
}

void ShunDeEssProcessor::prepareToPlay (double rate, int)
{
    sampleRate = std::isfinite (rate) && rate >= 8000 ? static_cast<float> (rate) : 44100.0f;
    detectorState = {};
    envelopes = {};
    lowBand = {};
    lastFrequency = lastQ = -1;
    reduction.store (0, std::memory_order_relaxed);
}

void ShunDeEssProcessor::releaseResources()
{
    detectorState = {};
    envelopes = {};
    lowBand = {};
}

bool ShunDeEssProcessor::isBusesLayoutSupported (const BusesLayout& buses) const
{
    const auto input = buses.getMainInputChannelSet();
    return input == buses.getMainOutputChannelSet()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void ShunDeEssProcessor::updateDetectorCoefficients (float frequency, float q) noexcept
{
    frequency = std::clamp (frequency, 2000.0f, std::min (12000.0f, sampleRate * 0.45f));
    q = std::clamp (q, 0.5f, 4.0f);
    const auto omega = 2.0f * pi * frequency / sampleRate;
    const auto alpha = std::sin (omega) / (2.0f * q);
    const auto normalizer = 1.0f / (1.0f + alpha);
    b0 = alpha * normalizer;
    b1 = 0.0f;
    b2 = -alpha * normalizer;
    a1 = -2.0f * std::cos (omega) * normalizer;
    a2 = (1.0f - alpha) * normalizer;
    lowPassCoefficient = 1.0f - std::exp (-2.0f * pi * frequency / sampleRate);
    lastFrequency = frequency;
    lastQ = q;
}

void ShunDeEssProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void ShunDeEssProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

juce::AudioProcessorParameter* ShunDeEssProcessor::getBypassParameter() const
{
    return parameters.getParameter ("bypass");
}

void ShunDeEssProcessor::process (juce::AudioBuffer<float>& buffer, bool hostBypassed)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = std::min ({ getTotalNumInputChannels(), buffer.getNumChannels(), 2 });
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    if (channels == 0 || buffer.getNumSamples() == 0)
        return;

    const auto value = [&] (size_t index) { return raw[index]->load (std::memory_order_relaxed); };
    const auto mode = value (0) >= 0.5f;
    const auto frequency = value (1);
    const auto q = value (2);
    const auto threshold = value (3);
    const auto depth = value (4);
    const auto attackMs = value (5);
    const auto releaseMs = value (6);
    const auto outputGain = std::pow (10.0f, value (7) / 20.0f);
    const auto mix = value (8) * 0.01f;
    const auto stereoLink = value (9) * 0.01f;
    const auto bypass = hostBypassed || value (10) >= 0.5f;
    const auto listen = value (11) >= 0.5f;
    if (std::abs (frequency - lastFrequency) > 0.1f || std::abs (q - lastQ) > 0.001f)
        updateDetectorCoefficients (frequency, q);
    const auto attack = std::exp (-1.0f / (0.001f * attackMs * sampleRate));
    const auto release = std::exp (-1.0f / (0.001f * releaseMs * sampleRate));
    float blockReduction = 0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        std::array<float, 2> dry {}, detected {}, level {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& state = detectorState[static_cast<size_t> (ch)];
            const auto input = buffer.getSample (ch, i);
            dry[static_cast<size_t> (ch)] = std::isfinite (input) ? input : 0.0f;
            const auto filtered = b0 * dry[static_cast<size_t> (ch)] + b1 * state.x1 + b2 * state.x2
                                - a1 * state.y1 - a2 * state.y2;
            state.x2 = state.x1; state.x1 = dry[static_cast<size_t> (ch)];
            state.y2 = state.y1; state.y1 = std::isfinite (filtered) ? filtered : 0.0f;
            detected[static_cast<size_t> (ch)] = state.y1;
            const auto magnitude = std::abs (state.y1);
            auto& envelope = envelopes[static_cast<size_t> (ch)];
            const auto coefficient = magnitude > envelope ? attack : release;
            envelope = magnitude + coefficient * (envelope - magnitude);
            level[static_cast<size_t> (ch)] = 20.0f * std::log10 (std::max (envelope, 1.0e-9f));
        }
        const auto linkedLevel = channels == 2 ? std::max (level[0], level[1]) : level[0];
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<size_t> (ch);
            const auto detectorDb = level[c] + stereoLink * (linkedLevel - level[c]);
            const auto reductionDb = std::clamp (detectorDb - threshold, 0.0f, depth);
            blockReduction = std::max (blockReduction, reductionDb);
            const auto gain = std::pow (10.0f, -reductionDb / 20.0f);
            lowBand[c] += lowPassCoefficient * (dry[c] - lowBand[c]);
            const auto highBand = dry[c] - lowBand[c];
            const auto wet = mode ? lowBand[c] + highBand * gain : dry[c] * gain;
            const auto mixed = listen ? detected[c] : dry[c] + mix * (wet - dry[c]);
            buffer.setSample (ch, i, bypass ? dry[c] : mixed * outputGain);
        }
    }
    reduction.store (bypass ? 0.0f : blockReduction, std::memory_order_relaxed);
}

void ShunDeEssProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, data);
}

void ShunDeEssProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ShunDeEssProcessor::createEditor()
{
    return new ShunDeEssEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ShunDeEssProcessor();
}
