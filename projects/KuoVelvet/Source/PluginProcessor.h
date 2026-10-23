#pragma once
#include "../../KuoStereo/Source/VelvetStage.h"
#include <juce_audio_processors/juce_audio_processors.h>

class KuoVelvetProcessor final : public juce::AudioProcessor
{
public:
    KuoVelvetProcessor();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.04; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorValueTreeState parameters;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    kuo::VelvetStage velvet;
    std::array<std::vector<float>, 2> dryHistory;
    size_t writePosition = 0;
    void process (juce::AudioBuffer<float>&, bool hostBypass);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuoVelvetProcessor)
};
