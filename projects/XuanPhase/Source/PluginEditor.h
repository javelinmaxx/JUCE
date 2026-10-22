#pragma once
#include "PluginProcessor.h"
#include "../../StagePluginStyle.h"
#include "PhaseMeter.h"
class XuanPhaseEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit XuanPhaseEditor (XuanPhaseProcessor&);
    ~XuanPhaseEditor() override { stopTimer(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
private:
    void timerCallback() override { repaint(); }
    XuanPhaseProcessor& processor;
    static constexpr int designWidth = 840, designHeight = 490;
    StageLookAndFeel look;
    juce::TooltipWindow tooltipWindow { this };
    StageBypassButton bypass, automatic;
    std::array<juce::Slider, 5> sliders;
    std::array<juce::Label, 5> labels;
    using State = juce::AudioProcessorValueTreeState;
    std::array<std::unique_ptr<State::SliderAttachment>, 5> attachments;
    std::unique_ptr<State::ButtonAttachment> bypassAttachment, autoAttachment;
    XuanPhaseMeter meter;
};
