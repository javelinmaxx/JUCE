#pragma once
#include "PluginProcessor.h"

class CharacterCompressorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CharacterCompressorEditor (CharacterCompressorProcessor&);
    ~CharacterCompressorEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    CharacterCompressorProcessor& processor;
    juce::ComboBox model;
    juce::ToggleButton transformer { "OUTPUT TRANSFORMER" }, bypass { "BYPASS" };
    std::array<juce::Slider, 10> sliders;
    std::array<juce::Label, 10> labels;
    juce::Label modelDescription, meters;
    using State = juce::AudioProcessorValueTreeState;
    std::unique_ptr<State::ComboBoxAttachment> modelAttachment;
    std::array<std::unique_ptr<State::SliderAttachment>, 10> attachments;
    std::unique_ptr<State::ButtonAttachment> transformerAttachment, bypassAttachment;
    float displayedReduction = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CharacterCompressorEditor)
};
