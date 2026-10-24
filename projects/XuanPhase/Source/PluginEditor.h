#pragma once
#include "../../EditorTooltips.h"
#include "PluginProcessor.h"
#include "../../StagePluginStyle.h"
#include "PhaseMeter.h"
class XuanPhaseEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ComponentMovementWatcher
{
public:
    explicit XuanPhaseEditor (XuanPhaseProcessor&);
    ~XuanPhaseEditor() override { stopTimer(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
private:
    using juce::ComponentMovementWatcher::componentMovedOrResized;
    using juce::ComponentMovementWatcher::componentVisibilityChanged;
    void updateVisibility();
    void componentMovedOrResized (bool, bool) override {}
    void componentPeerChanged() override { updateVisibility(); }
    void componentVisibilityChanged() override { updateVisibility(); }
    void timerCallback() override { updateVisibility(); if (isShowing()) repaint(); }
    XuanPhaseProcessor& processor;
    static constexpr int designWidth = 840, designHeight = 490;
    StageLookAndFeel look;
    juce::TooltipWindow tooltipWindow { this };
    editorTooltips::TitleHint titleHint { JucePlugin_VersionString };
    StageBypassButton bypass, automatic;
    std::array<juce::Slider, 6> sliders;
    std::array<juce::Label, 6> labels;
    using State = juce::AudioProcessorValueTreeState;
    std::array<std::unique_ptr<State::SliderAttachment>, 6> attachments;
    std::unique_ptr<State::ButtonAttachment> bypassAttachment, autoAttachment;
    XuanPhaseMeter meter;
};
