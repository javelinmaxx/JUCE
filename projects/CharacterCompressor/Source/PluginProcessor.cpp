#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr std::array<const char*, 13> ids { "model", "input", "threshold", "ratio", "attack",
    "release", "knee", "output", "mix", "sidechain", "drive", "transformer", "bypass" };
}

juce::AudioProcessorValueTreeState::ParameterLayout CharacterCompressorProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "model", 1 },
        "Compressor model", juce::StringArray { "FET - feed-forward", "VCA - feedback",
        "Variable-mu", "Optical" }, 0));
    const auto add = [&] (const char* id, const char* name, float lo, float hi,
                          float step, float initial, float centre, const char* unit)
    {
        juce::NormalisableRange<float> range { lo, hi, step };
        if (centre > lo && centre < hi)
            range.setSkewForCentre (centre);
        result.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 },
            name, range, initial, juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    add ("input", "Input", -24, 24, 0.1f, 0, 0, "dB");
    add ("threshold", "Threshold", -60, 0, 0.1f, -18, -30, "dB");
    add ("ratio", "Ratio", 1, 20, 0.1f, 4, 4, ":1");
    add ("attack", "Attack", 0.05f, 100, 0.01f, 10, 10, "ms");
    add ("release", "Release", 10, 2000, 1, 150, 200, "ms");
    add ("knee", "Knee", 0, 24, 0.1f, 6, 12, "dB");
    add ("output", "Output", -24, 24, 0.1f, 0, 0, "dB");
    add ("mix", "Mix", 0, 100, 0.1f, 100, 50, "%");
    add ("sidechain", "Detector HPF", 0, 300, 1, 80, 80, "Hz");
    add ("drive", "Transformer drive", 0, 18, 0.1f, 6, 9, "dB");
    result.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "transformer", 1 },
        "Output transformer", false));
    result.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 },
        "Bypass", false));
    return result;
}

CharacterCompressorProcessor::CharacterCompressorProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "CharacterCompressorState", layout())
{
    for (size_t i = 0; i < raw.size(); ++i)
        raw[i] = parameters.getRawParameterValue (ids[i]);
}

character::Parameters CharacterCompressorProcessor::readParameters() const noexcept
{
    const auto value = [&] (size_t i) { return raw[i]->load (std::memory_order_relaxed); };
    character::Parameters p;
    p.model = static_cast<character::Model> (static_cast<int> (value (0)));
    p.inputDb = value (1); p.thresholdDb = value (2); p.ratio = value (3);
    p.attackMs = value (4); p.releaseMs = value (5); p.kneeDb = value (6);
    p.outputDb = value (7); p.mix = value (8) * 0.01f;
    p.sidechainHz = value (9); p.driveDb = value (10);
    p.transformer = value (11) >= 0.5f; p.bypass = value (12) >= 0.5f;
    return p;
}

void CharacterCompressorProcessor::prepareToPlay (double rate, int)
{
    engine.prepare (rate);
    engine.setParameters (readParameters());
    reduction.store (0); inputPeak.store (-100); outputPeak.store (-100);
    setLatencySamples (0);
}

bool CharacterCompressorProcessor::isBusesLayoutSupported (const BusesLayout& buses) const
{
    const auto in = buses.getMainInputChannelSet();
    return in == buses.getMainOutputChannelSet()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void CharacterCompressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void CharacterCompressorProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                       juce::MidiBuffer&)
{
    process (buffer, true);
}

juce::AudioProcessorParameter* CharacterCompressorProcessor::getBypassParameter() const
{
    return parameters.getParameter ("bypass");
}

void CharacterCompressorProcessor::process (juce::AudioBuffer<float>& buffer, bool hostBypassed)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = std::min ({ getTotalNumInputChannels(), buffer.getNumChannels(), 2 });
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    if (buffer.getNumSamples() == 0 || channels == 0)
        return;
    float in = 0, out = 0;
    for (int ch = 0; ch < channels; ++ch)
        in = std::max (in, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    auto p = readParameters();
    p.bypass = p.bypass || hostBypassed;
    engine.setParameters (p);
    engine.process (buffer.getArrayOfWritePointers(), channels, buffer.getNumSamples());
    for (int ch = 0; ch < channels; ++ch)
        out = std::max (out, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    inputPeak.store (character::Compressor::db (in), std::memory_order_relaxed);
    outputPeak.store (character::Compressor::db (out), std::memory_order_relaxed);
    reduction.store (engine.reductionDb(), std::memory_order_relaxed);
}

void CharacterCompressorProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, data);
}

void CharacterCompressorProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* CharacterCompressorProcessor::createEditor()
{
    return new CharacterCompressorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CharacterCompressorProcessor();
}
