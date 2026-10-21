#pragma once
#include "PluginProcessor.h"
#include "../../StagePluginStyle.h"
class RenTransformerEditor final : public juce::AudioProcessorEditor
{
public:
    explicit RenTransformerEditor (RenTransformerProcessor&);
    ~RenTransformerEditor() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    static constexpr int designWidth = 548, designHeight = 444;
    StageLookAndFeel look;
    juce::TooltipWindow tooltipWindow { this };
    StageBypassButton bypass;
    StageTransformerSwitch transformer;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> transformerAttachment;
    std::array<juce::Slider, 4> sliders;
    std::array<juce::Label, 4> labels;
    using State = juce::AudioProcessorValueTreeState;
    std::array<std::unique_ptr<State::SliderAttachment>, 4> attachments;
    std::unique_ptr<State::ButtonAttachment> bypassAttachment;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenTransformerEditor)
};
