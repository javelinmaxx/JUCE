#pragma once
#include "PhaseEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

class XuanPhaseProcessor final : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    XuanPhaseProcessor();
    ~XuanPhaseProcessor() override;
    xuan::Settings settings() const noexcept;
    int targetCount() const noexcept { return engine.targetCount(); }
    void setMeterVisible (bool visible) noexcept { engine.setMeterVisible (visible); }
    bool readSpectrum (xuan::SpectrumFrame& f, uint32_t& v) const noexcept { return engine.readSpectrum (f, v); }
    bool readRotation (xuan::RotationFrame& f, uint32_t& v) const noexcept { return engine.readRotation (f, v); }
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
    const juce::String getName() const override { return "Xuan Phase"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }
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
    void parameterChanged (const juce::String&, float) override { engine.changed(); }
    xuan::PhaseEngine engine;
    std::array<std::atomic<float>*, 8> raw {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XuanPhaseProcessor)
};
