#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout KuoVelvetProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    const auto add = [&] (const char* id, const char* name, float lo, float hi, float step,
                          float initial, const char* unit)
    {
        result.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { lo, hi, step }, initial,
            juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    add ("width", "Velvet width", 0, 100, 0.1f, 35, "%");
    add ("transientProtect", "Transient", 0, 100, 0.1f, 80, "%");
    add ("guardAttack", "Attack", 0, 10, 0.1f, 0, "ms");
    add ("output", "Output", -12, 12, 0.1f, 0, "dB");
    result.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return result;
}
KuoVelvetProcessor::KuoVelvetProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "KuoVelvetState", createParameterLayout()) {}

juce::AudioProcessorParameter* KuoVelvetProcessor::getBypassParameter() const { return parameters.getParameter ("bypass"); }
juce::AudioProcessorEditor* KuoVelvetProcessor::createEditor() { return new KuoVelvetEditor (*this); }
void KuoVelvetProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary (*xml, data);
}
void KuoVelvetProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            // Old presets retain the remaining controls; retired band settings
            // no longer alter the full-band convolution input or future saves.
            for (int i = state.getNumChildren(); --i >= 0;)
            {
                const auto id = state.getChild (i).getProperty ("id").toString();
                if (id == "velvetLow" || id == "velvetMid" || id == "velvetHigh")
                    state.removeChild (i, nullptr);
            }
            parameters.replaceState (state);
        }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KuoVelvetProcessor(); }

void KuoVelvetProcessor::prepareToPlay (double rate, int)
{
    const auto sampleRate = static_cast<float> (std::isfinite (rate) && rate >= 8000 ? rate : 48000);
    velvet.prepare (sampleRate, { 1, 1, 1 }, false);
    setLatencySamples (velvet.getLatency());
    for (auto& history : dryHistory) history.assign (static_cast<size_t> (velvet.getLatency()), 0.0f);
    writePosition = 0;
}
bool KuoVelvetProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
void KuoVelvetProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer, false); }
void KuoVelvetProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer, true); }
void KuoVelvetProcessor::process (juce::AudioBuffer<float>& buffer, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || dryHistory[0].empty()) return;
    velvet.setParameters (parameters.getRawParameterValue ("width")->load() * 0.01f,
        parameters.getRawParameterValue ("transientProtect")->load() * 0.01f,
        parameters.getRawParameterValue ("guardAttack")->load(), { 1, 1, 1 });
    const auto gain = juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("output")->load());
    const auto bypass = hostBypass || parameters.getRawParameterValue ("bypass")->load() >= 0.5f;
    auto* left = buffer.getWritePointer (0); auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto inputLeft = left[i], inputRight = right[i];
        const auto result = velvet.process (0.5f * (inputLeft + inputRight));
        const auto dryLeft = dryHistory[0][writePosition], dryRight = dryHistory[1][writePosition];
        dryHistory[0][writePosition] = inputLeft; dryHistory[1][writePosition] = inputRight;
        writePosition = (writePosition + 1) % dryHistory[0].size();
        left[i] = bypass ? dryLeft : (dryLeft + result.side) * gain;
        right[i] = bypass ? dryRight : (dryRight - result.side) * gain;
    }
}
