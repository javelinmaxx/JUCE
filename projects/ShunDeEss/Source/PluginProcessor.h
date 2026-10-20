#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class ShunDeEssProcessor final : public juce::AudioProcessor
{
public:
    ShunDeEssProcessor();
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SHUN DE-ESS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float> reduction { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void process (juce::AudioBuffer<float>&, bool hostBypassed);
    void updateDetectorCoefficients (float frequency, float q) noexcept;
    struct BiquadState { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    std::array<std::atomic<float>*, 12> raw {};
    std::array<BiquadState, 2> detectorState {};
    std::array<float, 2> envelopes {}, lowBand {};
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float sampleRate = 44100, lastFrequency = -1, lastQ = -1;
    float attackCoefficient = 0, releaseCoefficient = 0;
    float lowPassCoefficient = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShunDeEssProcessor)
};
