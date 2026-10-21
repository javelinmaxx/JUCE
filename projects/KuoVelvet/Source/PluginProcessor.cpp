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
    add ("velvetLow", "Velvet low band", 0, 200, 0.1f, 100, "%");
    add ("velvetMid", "Velvet mid band", 0, 200, 0.1f, 100, "%");
    add ("velvetHigh", "Velvet high band", 0, 200, 0.1f, 100, "%");
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
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KuoVelvetProcessor(); }

void KuoVelvetProcessor::prepareToPlay (double rate, int)
{
    const auto sampleRate = static_cast<float> (std::isfinite (rate) && rate >= 8000 ? rate : 48000);
    velvet.prepare (sampleRate, { parameters.getRawParameterValue ("velvetLow")->load() * 0.01f,
        parameters.getRawParameterValue ("velvetMid")->load() * 0.01f,
        parameters.getRawParameterValue ("velvetHigh")->load() * 0.01f });
    setLatencySamples (velvet.getLatency());
    for (auto& history : dryHistory) history.assign (static_cast<size_t> (velvet.getLatency()), 0.0f);
    writePosition = 0; lowState = upperState = {}; scopeCounter = 0;
    lowCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 250 / sampleRate);
    upperCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 2500 / sampleRate);
    for (auto& band : scope)
        for (auto& point : band) { point.left.store (0); point.right.store (0); }
    scopeWrite.store (0);
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
        parameters.getRawParameterValue ("guardAttack")->load(), {
            parameters.getRawParameterValue ("velvetLow")->load() * 0.01f,
            parameters.getRawParameterValue ("velvetMid")->load() * 0.01f,
            parameters.getRawParameterValue ("velvetHigh")->load() * 0.01f });
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
        pushScope (left[i], right[i]);
    }
}
void KuoVelvetProcessor::pushScope (float left, float right) noexcept
{
    const std::array<float, 2> values { left, right };
    std::array<std::array<float, 2>, 3> bands {};
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto input = values[static_cast<size_t> (channel)];
        lowState[static_cast<size_t> (channel)] += lowCoefficient
            * (input - lowState[static_cast<size_t> (channel)]);
        upperState[static_cast<size_t> (channel)] += upperCoefficient
            * (input - upperState[static_cast<size_t> (channel)]);
        bands[0][static_cast<size_t> (channel)] = lowState[static_cast<size_t> (channel)];
        bands[1][static_cast<size_t> (channel)] = upperState[static_cast<size_t> (channel)]
                                                   - lowState[static_cast<size_t> (channel)];
        bands[2][static_cast<size_t> (channel)] = input - upperState[static_cast<size_t> (channel)];
    }
    if (++scopeCounter < 23) return;
    scopeCounter = 0;
    const auto index = scopeWrite.load (std::memory_order_relaxed) % scopePoints;
    for (int band = 0; band < 3; ++band)
    {
        auto& point = scope[static_cast<size_t> (band)][static_cast<size_t> (index)];
        point.left.store (bands[static_cast<size_t> (band)][0], std::memory_order_relaxed);
        point.right.store (bands[static_cast<size_t> (band)][1], std::memory_order_relaxed);
    }
    scopeWrite.store (index + 1, std::memory_order_release);
}

void KuoVelvetProcessor::readScope (int band,
    std::array<juce::Point<float>, scopePoints>& output) const noexcept
{
    if (band < 0 || band >= 3) return;
    const auto current = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < scopePoints; ++i)
    {
        const auto index = (current + i) % scopePoints;
        const auto& point = scope[static_cast<size_t> (band)][static_cast<size_t> (index)];
        output[static_cast<size_t> (i)] = { point.left.load (std::memory_order_relaxed),
                                           point.right.load (std::memory_order_relaxed) };
    }
}
