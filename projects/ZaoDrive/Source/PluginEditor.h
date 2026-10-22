#pragma once
#include "PluginProcessor.h"

class ZaoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

class ZaoBypassButton final : public juce::ToggleButton
{
public:
    explicit ZaoBypassButton (const juce::String& caption = "BYPASS") : ToggleButton (caption) {}
    void paintButton (juce::Graphics&, bool, bool) override;
};

class ZaoTransformerSwitch final : public juce::ToggleButton
{
public:
    static constexpr int pivotOffsetY = 54;
    ZaoTransformerSwitch() : ToggleButton ("TRANSFORMER") {}
    void paintButton (juce::Graphics&, bool, bool) override;
};

class ZaoDriveEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ZaoDriveEditor (ZaoDriveProcessor&);
    ~ZaoDriveEditor() override { stopTimer(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;
    const std::array<float, 96>& getExpectedOutput() const noexcept { return expectedOutput; }
private:
    static constexpr int designWidth = 548, designHeight = 690;
    void timerCallback() override;
    void updateInputControl (int circuit);
    void refreshPreview (const zao::Settings&);
    void paintExpectedOutput (juce::Graphics&);
    ZaoDriveProcessor& processor;
    ZaoLookAndFeel look;
    juce::TooltipWindow tooltipWindow { this };
    ZaoBypassButton bypass, smart { "SMART" };
    ZaoTransformerSwitch transformer;
    juce::Slider model;
    juce::Label modelLabel;
    std::array<juce::Slider, 7> sliders;
    std::array<juce::Label, 7> labels;
    using State = juce::AudioProcessorValueTreeState;
    std::array<std::unique_ptr<State::SliderAttachment>, 7> attachments;
    std::unique_ptr<State::ButtonAttachment> bypassAttachment, transformerAttachment, smartAttachment;
    std::array<float, 13> previousSettings {};
    int inputControlCircuit = -1;
    uint32_t previousSmartRevision = 0;
    bool previewReady = false;
    std::array<float, 96> expectedInput {}, expectedOutput {};
    float expectedPeak = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZaoDriveEditor)
};
