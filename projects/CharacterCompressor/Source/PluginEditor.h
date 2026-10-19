#pragma once
#include "PluginProcessor.h"
#include "GainReductionHistory.h"

struct CompressorTheme
{
    juce::Colour background, card, meterPanel, text, secondary, control, meter,
                 switchSurface, gaugeTrack, grid, knobTop, knobBottom;
};

class AnimatedSwitch final : public juce::ToggleButton
{
public:
    explicit AnimatedSwitch (const juce::String& caption) : ToggleButton (caption) {}
    void advance (float elapsed);
    void snapToState() { position = getToggleState() ? 1.0f : 0.0f; }
    void setTheme (const CompressorTheme& theme)
    {
        accent = theme.control;
        surface = theme.switchSurface;
        foreground = theme.text;
        secondary = theme.secondary;
        repaint();
    }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    float position = 0;
    juce::Colour accent { 0xff168aad };
    juce::Colour surface { 0xff202a34 }, foreground { 0xfff0ddbb }, secondary { 0xff92a0ad };
};

class CompressorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void setTheme (const CompressorTheme& theme)
    {
        accent = theme.control;
        ring = theme.grid;
        knobTop = theme.knobTop;
        knobBottom = theme.knobBottom;
    }
private:
    juce::Colour accent { 0xff168aad };
    juce::Colour ring { 0xff38434e }, knobTop { 0xff46515e }, knobBottom { 0xff1b242d };
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
    void applyTheme();
    CharacterCompressorProcessor& processor;
    CompressorLookAndFeel look;
    juce::ComboBox model;
    AnimatedSwitch transformer { "OUTPUT TRANSFORMER" }, bypass { "BYPASS" };
    AnimatedSwitch highTilt { "SC HIGH TILT +6 dB" };
    AnimatedSwitch upward { "UPWARD COMPRESSION" };
    AnimatedSwitch midSide { "MID / SIDE MODE" };
    std::array<juce::Slider, 11> sliders;
    std::array<juce::Label, 11> labels;
    juce::Label modelDescription, meters;
    using State = juce::AudioProcessorValueTreeState;
    std::unique_ptr<State::ComboBoxAttachment> modelAttachment;
    std::array<std::unique_ptr<State::SliderAttachment>, 11> attachments;
    std::unique_ptr<State::ButtonAttachment> transformerAttachment, bypassAttachment, tiltAttachment, upwardAttachment, midSideAttachment;
    GainReductionHistory history;
    juce::Rectangle<int> meterBounds { 376, 176, 180, 564 };
    double lastTick = 0;
    float displayedReduction = 0;
    bool lastUpwardMode = false;
    int lastModelIndex = -1;
    CompressorTheme theme;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CharacterCompressorEditor)
};
