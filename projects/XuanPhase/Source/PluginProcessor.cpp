#include "PluginProcessor.h"
#include "PluginEditor.h"
namespace { constexpr std::array<const char*, 7> ids { "low", "high", "split", "depth", "output", "automatic", "bypass" }; }
juce::AudioProcessorValueTreeState::ParameterLayout XuanPhaseProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto add = [&] (const char* id, const char* name, float lo, float hi, float step, float initial, const char* unit)
    {
        juce::NormalisableRange<float> range (lo, hi, step);
        if (juce::String (id) == "split") range.setSkewForCentre (1000);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, initial,
            juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    add ("low", "Low phase", -180, 180, .1f, 0, "deg");
    add ("high", "High phase", -180, 180, .1f, 0, "deg");
    add ("split", "Crossover", 80, 4000, 1, 1000, "Hz");
    add ("depth", "Auto depth", 0, 100, .1f, 50, "%");
    add ("output", "Output", -24, 12, .1f, 0, "dB");
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "automatic", 1 }, "Auto phase", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}
XuanPhaseProcessor::XuanPhaseProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "XuanPhaseState", createParameterLayout())
{
    for (size_t i = 0; i < raw.size(); ++i) { raw[i] = parameters.getRawParameterValue (ids[i]); parameters.addParameterListener (ids[i], this); }
}
XuanPhaseProcessor::~XuanPhaseProcessor()
{
    for (const auto* id : ids) parameters.removeParameterListener (id, this);
    engine.release();
}
xuan::Settings XuanPhaseProcessor::settings() const noexcept
{
    const auto value = [&] (size_t i) { return raw[i]->load (std::memory_order_relaxed); };
    return { value (0), value (1), value (2), value (3), value (4), value (5) >= .5f, value (6) >= .5f };
}
void XuanPhaseProcessor::prepareToPlay (double rate, int)
{
    engine.prepare (std::isfinite (rate) && rate >= 8000 ? rate : 48000, [this] { return settings(); });
    setLatencySamples (engine.latency());
}
bool XuanPhaseProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && layout.getMainInputChannelSet() == output;
}
void XuanPhaseProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { engine.process (b, settings()); }
void XuanPhaseProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { engine.process (b, settings(), true); }
juce::AudioProcessorParameter* XuanPhaseProcessor::getBypassParameter() const { return parameters.getParameter ("bypass"); }
juce::AudioProcessorEditor* XuanPhaseProcessor::createEditor() { return new XuanPhaseEditor (*this); }
void XuanPhaseProcessor::getStateInformation (juce::MemoryBlock& b) { if (auto xml = parameters.copyState().createXml()) copyXmlToBinary (*xml, b); }
void XuanPhaseProcessor::setStateInformation (const void* b, int n)
{
    if (auto xml = getXmlFromBinary (b, n))
        if (xml->hasTagName (parameters.state.getType())) parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new XuanPhaseProcessor(); }
