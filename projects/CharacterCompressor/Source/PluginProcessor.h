#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CompressorDSP.h"

class CharacterCompressorProcessor final : public juce::AudioProcessor
{
public:
    CharacterCompressorProcessor();
    void prepareToPlay (double, int) override;
    void releaseResources() override { engine.reset(); }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Character Compressor"; }
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
    std::atomic<float> reduction { 0 }, inputPeak { -100 }, outputPeak { -100 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    character::Parameters readParameters() const noexcept;
    void process (juce::AudioBuffer<float>&, bool hostBypassed);
    character::Compressor engine;
    // Cache parameter pointers once; no string lookup/allocation in processBlock.
    std::array<std::atomic<float>*, 17> raw {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CharacterCompressorProcessor)
};
