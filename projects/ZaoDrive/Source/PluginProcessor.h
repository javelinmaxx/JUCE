#pragma once
#include "DriveEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

class ZaoDriveProcessor final : public juce::AudioProcessor
{
public:
    ZaoDriveProcessor();
    void prepareToPlay (double, int) override;
    void releaseResources() override { engine.release(); }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Zao Drive"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    zao::Settings getSettings() const noexcept;
    zao::SmartAnalyser::Result getSmartAnalysis() const noexcept { return engine.getSmart().readResult(); }
    uint32_t getSmartSubmittedJobs() const noexcept { return engine.getSmart().getSubmittedJobs(); }
    juce::AudioProcessorValueTreeState parameters;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::array<std::atomic<float>*, 14> raw {};
    zao::DriveEngine engine;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZaoDriveProcessor)
};
