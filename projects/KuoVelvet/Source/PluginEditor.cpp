#include "PluginEditor.h"
#include "../../EditorScaling.h"
#include "../../EditorTooltips.h"
using namespace stageStyle;
KuoVelvetEditor::KuoVelvetEditor (KuoVelvetProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true); setLookAndFeel (&look);
    look.setColour (juce::TooltipWindow::backgroundColourId, panel);
    look.setColour (juce::TooltipWindow::textColourId, foreground);
    look.setColour (juce::TooltipWindow::outlineColourId, grid);
    addAndMakeVisible (bypass);
    bypass.setTooltip ("Bypasses processing while preserving the plugin's latency.");
    bypassAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "bypass", bypass);
    constexpr std::array<const char*, 4> ids { "width", "transientProtect", "guardAttack", "output" };
    constexpr std::array<const char*, 4> names { "WIDTH", "TRANSIENT", "ATTACK", "OUTPUT" };
    constexpr std::array<const char*, 4> units { " %", " %", " ms", " dB" };
    constexpr std::array<const char*, 4> hints {
        "Sets the amount of sparse convolution added to the stereo side signal.",
        "Reduces convolution input during clear spectral onsets to keep transients crisp.",
        "Sets how quickly transient protection attenuates the convolution input.",
        "Adjusts the final stereo output level." };
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
    constexpr std::array<const char*, 3> bandIds { "velvetLow", "velvetMid", "velvetHigh" };
    constexpr std::array<const char*, 3> bandNames { "LOW LEVEL", "MID LEVEL", "HIGH LEVEL" };
    constexpr std::array<const char*, 3> bandHints {
        "Scales the low-frequency signal entering Velvet from zero to two hundred percent.",
        "Scales the mid-frequency signal entering Velvet from zero to two hundred percent.",
        "Scales the high-frequency signal entering Velvet from zero to two hundred percent." };
    for (size_t i = 0; i < bandSliders.size(); ++i)
    {
        auto& slider = bandSliders[i];
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setName (bandNames[i]); slider.setTitle (bandNames[i]);
        slider.setTextValueSuffix (" %"); slider.setPopupDisplayEnabled (true, true, this);
        slider.setDoubleClickReturnValue (true, 100);
        addAndMakeVisible (slider);
        bandAttachments[i] = std::make_unique<State::SliderAttachment> (p.parameters, bandIds[i], slider);
        editorTooltips::setHint (slider, bandHints[i]);
    }
    editorScaling::enable (*this, designWidth, designHeight);
    startTimerHz (20);
}
void KuoVelvetEditor::resized()
{
    bypass.setBounds (442, 32, 82, 28);
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        const auto x = 38 + static_cast<int> (i) * 121;
        labels[i].setBounds (x, 150, 109, 16);
        sliders[i].setBounds (x, 166, 109, 84);
    }
    constexpr std::array<int, 3> bandX { 171, 332, 493 };
    for (size_t i = 0; i < bandSliders.size(); ++i) bandSliders[i].setBounds (bandX[i], 350, 16, 131);
    const auto scale = editorScaling::transform (*this, designWidth, designHeight);
    bypass.setTransform (scale);
    for (auto& slider : sliders) slider.setTransform (scale);
    for (auto& label : labels) label.setTransform (scale);
    for (auto& slider : bandSliders) slider.setTransform (scale);
}
void KuoVelvetEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.saveState();
    g.addTransform (editorScaling::transform (*this, designWidth, designHeight));
    g.setGradientFill (juce::ColourGradient (background.brighter (0.035f), 0, 0,
                                            background.darker (0.13f), 0, designHeight, false));
    g.fillRect (0, 0, designWidth, designHeight);
    g.setColour (secondary); g.setFont (juce::FontOptions (10.0f));
    g.drawText ("J-SONIC / VELVET STEREO FIELD", 24, 20, 360, 18, juce::Justification::centredLeft);
    g.setColour (foreground); g.setFont (juce::FontOptions (27.0f));
    g.drawText ("KUO VELVET", 24, 38, 410, 38, juce::Justification::centredLeft);
    drawPanel (g, { 24, 90, 500, 170 });
    drawPanel (g, { 24, 274, 500, 263 });
    g.setColour (foreground); g.setFont (juce::FontOptions (13.0f).withStyle ("bold"));
    g.drawText ("VELVET WIDTH", 38, 102, 450, 20, juce::Justification::centredLeft);
    g.drawText ("FREQUENCY FIELD", 38, 286, 450, 20, juce::Justification::centredLeft);
    g.setColour (secondary); g.setFont (juce::FontOptions (9.0f));
    g.drawText ("Sparse stereo texture with transient protection", 38, 124, 450, 16, juce::Justification::centredLeft);
    g.drawText ("LOW  /  MID  /  HIGH    -    AUTO SCALE  /  VELVET LEVEL", 38, 308, 472, 16, juce::Justification::centredLeft);
    paintScope (g, 0, { 39, 338, 124, 162 });
    paintScope (g, 1, { 200, 338, 124, 162 });
    paintScope (g, 2, { 361, 338, 124, 162 });
    g.setColour (secondary); g.setFont (juce::FontOptions (9.0f));
    g.drawText ("BELOW 250 Hz", 39, 509, 148, 16, juce::Justification::centred);
    g.drawText ("250 Hz - 2.5 kHz", 200, 509, 148, 16, juce::Justification::centred);
    g.drawText ("ABOVE 2.5 kHz", 361, 509, 148, 16, juce::Justification::centred);
    g.restoreState();
}
void KuoVelvetEditor::paintScope (juce::Graphics& g, int band, juce::Rectangle<float> bounds)
{
    g.setColour (inset);
    g.fillRoundedRectangle (bounds, 5);
    g.setColour (grid);
    g.drawRoundedRectangle (bounds, 5, 1);
    const auto centre = bounds.getCentre();
    g.setColour (secondary.withAlpha (0.15f));
    g.drawVerticalLine (juce::roundToInt (centre.x), bounds.getY() + 8, bounds.getBottom() - 8);
    g.drawHorizontalLine (juce::roundToInt (centre.y), bounds.getX() + 8, bounds.getRight() - 8);
    g.drawLine (bounds.getX() + 12, bounds.getBottom() - 12, bounds.getRight() - 12,
                bounds.getY() + 12, 0.7f);
    g.drawLine (bounds.getX() + 12, bounds.getY() + 12, bounds.getRight() - 12,
                bounds.getBottom() - 12, 0.7f);
    constexpr std::array<const char*, 3> names { "LOW", "MID", "HIGH" };
    g.setColour (foreground);
    g.setFont (juce::FontOptions (10.0f).withStyle ("bold"));
    g.drawText (names[static_cast<size_t> (band)], bounds.getX() + 8, bounds.getY() + 7,
                bounds.getWidth() - 16, 14, juce::Justification::centredLeft);
    std::array<juce::Point<float>, KuoVelvetProcessor::scopePoints> points;
    processor.readScope (band, points);
    const auto scale = std::min (bounds.getWidth(), bounds.getHeight()) * 0.43f;
    float peak = 0;
    for (const auto& point : points)
        peak = std::max ({ peak, std::abs (point.x), std::abs (point.y) });
    if (peak < 0.001f) return;
    const auto visualGain = std::min (8.0f, 0.75f / peak);
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto& point = points[i];
        const auto px = std::clamp (centre.x + (point.x - point.y) * scale * visualGain * 0.5f,
                                    bounds.getX() + 5, bounds.getRight() - 5);
        const auto py = std::clamp (centre.y - (point.x + point.y) * scale * visualGain * 0.5f,
                                    bounds.getY() + 5, bounds.getBottom() - 5);
        g.setColour (amber.withAlpha (0.12f + 0.62f * static_cast<float> (i) / points.size()));
        g.fillEllipse (px - 1.2f, py - 1.2f, 2.4f, 2.4f);
    }
}

void KuoVelvetEditor::timerCallback()
{
    repaint (editorScaling::repaintArea (*this, designWidth, designHeight, { 24, 274, 500, 263 }));
}
