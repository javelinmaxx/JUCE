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
    const juce::String getName() const override { return "Kuo Velvet"; }
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
    static constexpr int scopePoints = 96;
    void readScope (int, std::array<juce::Point<float>, scopePoints>&) const noexcept;
    juce::AudioProcessorValueTreeState parameters;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    kuo::VelvetStage velvet;
    struct ScopePoint { std::atomic<float> left { 0 }, right { 0 }; };
    std::array<std::array<ScopePoint, scopePoints>, 3> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::array<float, 2> lowState {}, upperState {};
    std::array<std::vector<float>, 2> dryHistory;
    size_t writePosition = 0;
    int scopeCounter = 0;
    float lowCoefficient = 0, upperCoefficient = 0;
    void process (juce::AudioBuffer<float>&, bool hostBypass);
    void pushScope (float left, float right) noexcept;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuoVelvetProcessor)
};
