#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout RenTransformerProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    const auto add = [&] (const char* id, const char* name, float lo, float hi, float step,
                          float initial, const char* unit)
    {
        result.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { lo, hi, step }, initial,
            juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    add ("drive", "Core drive", 0, 18, 0.1f, 6, "dB");
    add ("evenHarmonics", "DC bias", 0, 100, 0.1f, 0, "%");
    add ("output", "Output", -24, 24, 0.1f, 0, "dB");
    add ("mix", "Mix", 0, 100, 0.1f, 100, "%");
    result.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "transformer", 1 }, "Transformer", true));
    result.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return result;
}
RenTransformerProcessor::RenTransformerProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RenTransformerState", createParameterLayout()) {}

juce::AudioProcessorParameter* RenTransformerProcessor::getBypassParameter() const { return parameters.getParameter ("bypass"); }
juce::AudioProcessorEditor* RenTransformerProcessor::createEditor() { return new RenTransformerEditor (*this); }
void RenTransformerProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary (*xml, data);
}
void RenTransformerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RenTransformerProcessor(); }

void RenTransformerProcessor::prepareToPlay (double rate, int)
{
    stage.prepare (std::isfinite (rate) && rate >= 8000 ? rate : 48000);
    setLatencySamples (ren::TransformerStage::latency);
}
bool RenTransformerProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && layout.getMainInputChannelSet() == output;
}
void RenTransformerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    stage.process (buffer, parameters.getRawParameterValue ("drive")->load(),
        parameters.getRawParameterValue ("evenHarmonics")->load() * 0.01f,
        parameters.getRawParameterValue ("output")->load(),
        parameters.getRawParameterValue ("mix")->load() * 0.01f,
        parameters.getRawParameterValue ("transformer")->load() >= 0.5f,
        parameters.getRawParameterValue ("bypass")->load() >= 0.5f);
}
void RenTransformerProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    stage.process (buffer, parameters.getRawParameterValue ("drive")->load(),
        parameters.getRawParameterValue ("evenHarmonics")->load() * 0.01f,
        parameters.getRawParameterValue ("output")->load(),
        parameters.getRawParameterValue ("mix")->load() * 0.01f,
        parameters.getRawParameterValue ("transformer")->load() >= 0.5f, true);
}
