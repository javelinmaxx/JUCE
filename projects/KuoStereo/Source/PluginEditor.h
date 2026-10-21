#pragma once
#include "PluginProcessor.h"

class KuoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float,
                           const juce::Slider::SliderStyle, juce::Slider&) override;
};

class KuoBypassButton final : public juce::ToggleButton
{
public:
    KuoBypassButton() : ToggleButton ("BYPASS") {}
    void paintButton (juce::Graphics&, bool, bool) override;
};

class KuoStereoEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KuoStereoEditor (KuoStereoProcessor&);
    ~KuoStereoEditor() override { stopTimer(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint (24, 90, 500, 627); }
    void paintScope (juce::Graphics&, int, juce::Rectangle<float>);
    KuoStereoProcessor& processor;
    KuoLookAndFeel look;
    KuoBypassButton bypass;
    std::array<juce::Slider, 8> sliders;
    std::array<juce::Label, 8> labels;
    using State = juce::AudioProcessorValueTreeState;
    std::array<std::unique_ptr<State::SliderAttachment>, 8> attachments;
    std::array<juce::Slider, 3> bandSliders;
    std::array<std::unique_ptr<State::SliderAttachment>, 3> bandAttachments;
    std::unique_ptr<State::ButtonAttachment> bypassAttachment;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuoStereoEditor)
};
