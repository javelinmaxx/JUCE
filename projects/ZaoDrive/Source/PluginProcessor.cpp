#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr std::array<const char*, 10> ids {
    "input", "drive", "tone", "output", "mix", "coreDrive", "dcBias", "model", "transformer", "bypass" };
}

juce::AudioProcessorValueTreeState::ParameterLayout ZaoDriveProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto add = [&] (const char* id, const char* name, float minimum, float maximum,
                          float step, float initial, float centre, const char* unit)
    {
        juce::NormalisableRange<float> range (minimum, maximum, step);
        if (centre > minimum && centre < maximum) range.setSkewForCentre (centre);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
            range, initial, juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    add ("input", "Input", -24, 24, 0.1f, 0, 0, "dB");
    add ("drive", "Drive", 0, 36, 0.1f, 12, 18, "dB");
    add ("tone", "Tone", 1200, 18000, 1, 10000, 6000, "Hz");
    add ("output", "Output", -24, 12, 0.1f, -6, 0, "dB");
    add ("mix", "Mix", 0, 100, 0.1f, 100, 50, "%");
    add ("coreDrive", "Core drive", 0, 18, 0.1f, 6, 9, "dB");
    add ("dcBias", "DC bias", 0, 100, 0.1f, 0, 50, "%");
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "model", 1 },
        "Circuit", juce::StringArray { "BJT Common Emitter", "Op-Amp Feedback" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "transformer", 1 }, "Output transformer", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

ZaoDriveProcessor::ZaoDriveProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ZaoDriveState", createParameterLayout())
{
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = parameters.getRawParameterValue (ids[i]);
}

zao::Settings ZaoDriveProcessor::getSettings() const noexcept
{
    const auto value = [this] (size_t index) { return raw[index]->load (std::memory_order_relaxed); };
    return { value (0), value (1), value (2), value (3), value (4) * 0.01f,
             value (5), value (6) * 0.01f, juce::roundToInt (value (7)), value (8) >= 0.5f, value (9) >= 0.5f };
}

void ZaoDriveProcessor::prepareToPlay (double rate, int maximumBlock)
{
    engine.prepare (std::isfinite (rate) && rate >= 8000 ? rate : 48000, maximumBlock);
    setLatencySamples (engine.getLatency());
}

bool ZaoDriveProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && layout.getMainInputChannelSet() == output;
}

void ZaoDriveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    engine.process (buffer, getSettings());
}

void ZaoDriveProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto settings = getSettings(); settings.bypass = true;
    engine.process (buffer, settings);
}

juce::AudioProcessorParameter* ZaoDriveProcessor::getBypassParameter() const { return parameters.getParameter ("bypass"); }
juce::AudioProcessorEditor* ZaoDriveProcessor::createEditor() { return new ZaoDriveEditor (*this); }
void ZaoDriveProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary (*xml, data);
}
void ZaoDriveProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType())) parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ZaoDriveProcessor(); }
