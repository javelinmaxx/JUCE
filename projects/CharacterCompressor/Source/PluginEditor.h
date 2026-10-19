#pragma once
#include "PluginProcessor.h"
#include "GainReductionHistory.h"

class AnimatedSwitch final : public juce::ToggleButton
{
public:
    explicit AnimatedSwitch (const juce::String& caption) : ToggleButton (caption) {}
    void advance (float elapsed);
    void snapToState() { position = getToggleState() ? 1.0f : 0.0f; }
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    float position = 0;
    juce::Colour accent { 0xff168aad };
};

class CompressorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void setAccent (juce::Colour colour) { accent = colour; }
private:
    juce::Colour accent { 0xff168aad };
};

class CharacterCompressorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CharacterCompressorEditor (CharacterCompressorProcessor&);
    ~CharacterCompressorEditor() override { stopTimer(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintMeter (juce::Graphics&);
    CharacterCompressorProcessor& processor;
    CompressorLookAndFeel look;
    juce::ComboBox model;
    AnimatedSwitch transformer { "OUTPUT TRANSFORMER" }, bypass { "BYPASS" };
    AnimatedSwitch highTilt { "SC HIGH TILT +6 dB" };
    AnimatedSwitch upward { "UPWARD COMPRESSION" };
    std::array<juce::Slider, 10> sliders;
    std::array<juce::Label, 10> labels;
    juce::Label modelDescription, meters;
    using State = juce::AudioProcessorValueTreeState;
    std::unique_ptr<State::ComboBoxAttachment> modelAttachment;
    std::array<std::unique_ptr<State::SliderAttachment>, 10> attachments;
    std::unique_ptr<State::ButtonAttachment> transformerAttachment, bypassAttachment, tiltAttachment, upwardAttachment;
    GainReductionHistory history;
    juce::Rectangle<int> meterBounds { 376, 176, 180, 564 };
    double lastTick = 0;
    float displayedReduction = 0;
    bool lastUpwardMode = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CharacterCompressorEditor)
};
