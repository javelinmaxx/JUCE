#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr std::array<const char*, 13> ids {
    "input", "drive", "tone", "output", "mix", "coreDrive", "dcBias", "model", "transformer", "bypass", "diodeCircuit", "bjtBias", "diodeKnee" };
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
    // Keep the original two-choice parameter's normalized automation intact.
    // Its 0/1 values select BJT/Op-Amp or Shunt/Crossover in the new diode pair.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "diodeCircuit", 1 }, "Diode circuit pair", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "bjtBias", 2 },
        "BJT bias", juce::NormalisableRange<float> { -100, 100, 0.1f }, 0,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "diodeKnee", 2 },
        "Diode knee", juce::NormalisableRange<float> { 0, 100, 0.1f }, 50,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
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
             value (5), value (6) * 0.01f, juce::roundToInt (value (7)) + (value (10) >= 0.5f ? 2 : 0),
             value (8) >= 0.5f, value (9) >= 0.5f, value (11) * 0.01f, value (12) * 0.01f };
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
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            // Missing controls must use their documented defaults rather than
            // inherit whatever was selected in the previously loaded session.
            for (const auto* id : { "diodeCircuit", "bjtBias", "diodeKnee" })
                if (! state.getChildWithProperty ("id", id).isValid())
                {
                    auto legacyDefault = parameters.copyState().getChildWithProperty ("id", id).createCopy();
                    legacyDefault.setProperty ("value", juce::String (id) == "diodeKnee" ? 50.0f : 0.0f, nullptr);
                    state.appendChild (legacyDefault, nullptr);
                }
            parameters.replaceState (state);
        }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ZaoDriveProcessor(); }
