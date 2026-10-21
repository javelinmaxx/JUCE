#include "PluginEditor.h"
#include "../../EditorScaling.h"
#include "../../EditorTooltips.h"
using namespace stageStyle;
RenTransformerEditor::RenTransformerEditor (RenTransformerProcessor& p) : AudioProcessorEditor (&p)
{
    setOpaque (true); setLookAndFeel (&look);
    look.setColour (juce::TooltipWindow::backgroundColourId, panel);
    look.setColour (juce::TooltipWindow::textColourId, foreground);
    look.setColour (juce::TooltipWindow::outlineColourId, grid);
    addAndMakeVisible (bypass);
    bypass.setTooltip ("Bypasses processing while preserving the plugin's latency.");
    bypassAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "bypass", bypass);
    constexpr std::array<const char*, 4> ids { "drive", "evenHarmonics", "output", "mix" };
    constexpr std::array<const char*, 4> names { "CORE DRIVE", "DC BIAS", "OUTPUT", "MIX" };
    constexpr std::array<const char*, 4> units { " dB", " %", " dB", " %" };
    constexpr std::array<const char*, 4> hints {
        "Increases saturation in YinYang's oversampled transformer stage.",
        "Adds transformer asymmetry to increase even harmonics.",
        "Adjusts output gain before the transformer, also changing its input level.",
        "Blends transformer coloration with the latency-aligned clean signal." };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& slider = sliders[i];
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, 18);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, foreground);
        slider.setTextValueSuffix (units[i]); slider.setName (names[i]); slider.setTitle (names[i]);
        slider.setDoubleClickReturnValue (true, p.parameters.getParameter (ids[i])->convertFrom0to1 (
            p.parameters.getParameter (ids[i])->getDefaultValue()));
        labels[i].setText (names[i], juce::dontSendNotification);
        labels[i].setJustificationType (juce::Justification::centred);
        labels[i].setFont (juce::FontOptions (10.0f));
        labels[i].setColour (juce::Label::textColourId, secondary);
        addAndMakeVisible (slider); addAndMakeVisible (labels[i]);
        attachments[i] = std::make_unique<State::SliderAttachment> (p.parameters, ids[i], slider);
        editorTooltips::setHint (slider, labels[i], hints[i]);
    }
    addAndMakeVisible (transformer);
    transformer.setTooltip ("Saves CPU when Off; adds YinYang's transformer coloration when On.");
    transformerAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "transformer", transformer);
    editorScaling::enable (*this, designWidth, designHeight);
}
void RenTransformerEditor::resized()
{
    bypass.setBounds (442, 32, 82, 28);
    constexpr std::array<int, 4> x { 74, 224, 112, 336 };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        const auto y = i < 2 ? 142 : 310;
        labels[i].setBounds (x[i], y, 100, 16);
        sliders[i].setBounds (x[i], y + 16, 100, 82);
    }
    const auto knobBounds = look.getSliderLayout (sliders[0]).sliderBounds;
    const auto pivot = sliders[0].getY() + knobBounds.getCentreY();
    transformer.setBounds (382, pivot - StageTransformerSwitch::pivotOffsetY, 90, 106);
    const auto scale = editorScaling::transform (*this, designWidth, designHeight);
    bypass.setTransform (scale);
    for (auto& slider : sliders) slider.setTransform (scale);
    for (auto& label : labels) label.setTransform (scale);
    transformer.setTransform (scale);
}
void RenTransformerEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.saveState();
    g.addTransform (editorScaling::transform (*this, designWidth, designHeight));
    g.setGradientFill (juce::ColourGradient (background.brighter (0.035f), 0, 0,
                                            background.darker (0.13f), 0, designHeight, false));
    g.fillRect (0, 0, designWidth, designHeight);
    g.setColour (secondary); g.setFont (juce::FontOptions (10.0f));
    g.drawText ("J-SONIC / TRANSFORMER COLOR", 24, 20, 360, 18, juce::Justification::centredLeft);
    g.setColour (foreground); g.setFont (juce::FontOptions (27.0f));
    g.drawText ("REN TRANSFORMER", 24, 38, 410, 38, juce::Justification::centredLeft);
    drawPanel (g, { 24, 90, 500, 160 });
    drawPanel (g, { 24, 266, 500, 154 });
    g.setColour (foreground); g.setFont (juce::FontOptions (13.0f).withStyle ("bold"));
    g.drawText ("TRANSFORMER", 38, 102, 400, 20, juce::Justification::centredLeft);
    g.drawText ("OUTPUT", 38, 278, 400, 20, juce::Justification::centredLeft);
    g.setColour (secondary); g.setFont (juce::FontOptions (9.0f));
    g.drawText ("Magnetic coloration / 4x oversampling", 38, 124, 400, 16, juce::Justification::centredLeft);
    g.restoreState();
}
