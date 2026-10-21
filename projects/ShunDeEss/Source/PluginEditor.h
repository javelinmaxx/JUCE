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
    explicit AnimatedSwitch (const juce::String& caption, bool horizontalLayout = false,
                             bool compactStyle = false, bool labeledButtonStyle = false)
        : ToggleButton (caption), horizontal (horizontalLayout), compact (compactStyle),
          labeledButton (labeledButtonStyle) {}
    void advance (float elapsed);
    void snapToState() { position = getToggleState() ? 1.0f : 0.0f; }
    void setStateCaptions (juce::String onText, juce::String offText)
    {
        onCaption = std::move (onText);
        offCaption = std::move (offText);
        repaint();
    }
    void setFunctionLabelVisible (bool visible) { showFunctionLabel = visible; repaint(); }
    void setTheme (const CompressorTheme& theme)
    {
        accent = theme.control; surface = theme.switchSurface;
        foreground = theme.text; secondary = theme.secondary; repaint();
    }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    float position = 0;
    bool horizontal = false, compact = false, labeledButton = false, showFunctionLabel = true;
    juce::Colour accent { 0xff168aad }, surface { 0xff202a34 };
    juce::Colour foreground { 0xfff0ddbb }, secondary { 0xff92a0ad };
    juce::String onCaption { "ON" }, offCaption { "OFF" };
};

class CompressorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    void setTheme (const CompressorTheme& theme)
    {
        accent = theme.control; ring = theme.grid;
        knobTop = theme.knobTop; knobBottom = theme.knobBottom;
    }
private:
    juce::Colour accent { 0xff168aad }, ring { 0xff38434e };
    juce::Colour knobTop { 0xff46515e }, knobBottom { 0xff1b242d };
};

class ShunDeEssEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ShunDeEssEditor (ShunDeEssProcessor&);
    ~ShunDeEssEditor() override { stopTimer(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    static constexpr int designWidth = 548, designHeight = 741;
    void timerCallback() override;
    void paintMeter (juce::Graphics&);
    void paintResponseDisplay (juce::Graphics&);
    void applyTheme();
    ShunDeEssProcessor& processor;
    CompressorLookAndFeel look;
    AnimatedSwitch bypass { "BYPASS", true, false, true }, listen { "LISTEN" };
    juce::Slider algorithm;
    juce::Label algorithmLabel;
    std::array<juce::Slider, 10> sliders;
    std::array<juce::Label, 10> labels;
    using State = juce::AudioProcessorValueTreeState;
    std::array<std::unique_ptr<State::SliderAttachment>, 10> attachments;
    std::unique_ptr<State::ButtonAttachment> bypassAttachment, listenAttachment;
    bool lastSpectralMode = false, lastSplitMode = false;
    GainReductionHistory history;
    juce::Rectangle<int> meterBounds { 344, 140, 180, 490 };
    double lastTick = 0;
    float displayedReduction = 0;
    CompressorTheme theme;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShunDeEssEditor)
};
