#pragma once
#include "PluginProcessor.h"
#include "../../StagePluginStyle.h"
class KuoVelvetEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KuoVelvetEditor (KuoVelvetProcessor&);
    ~KuoVelvetEditor() override { stopTimer(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    static constexpr int designWidth = 548, designHeight = 561;
    void timerCallback() override;
    void paintScope (juce::Graphics&, int, juce::Rectangle<float>);
    KuoVelvetProcessor& processor;
    StageLookAndFeel look;
    juce::TooltipWindow tooltipWindow { this };
    StageBypassButton bypass;
    std::array<juce::Slider, 3> bandSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> bandAttachments;
    std::array<juce::Slider, 4> sliders;
    std::array<juce::Label, 4> labels;
    using State = juce::AudioProcessorValueTreeState;
    std::array<std::unique_ptr<State::SliderAttachment>, 4> attachments;
    std::unique_ptr<State::ButtonAttachment> bypassAttachment;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuoVelvetEditor)
};
