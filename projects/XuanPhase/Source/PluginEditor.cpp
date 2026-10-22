#include "PluginEditor.h"
#include "../../EditorScaling.h"
#include "../../EditorTooltips.h"
using namespace stageStyle;
XuanPhaseEditor::XuanPhaseEditor (XuanPhaseProcessor& p) : AudioProcessorEditor (&p), processor (p), meter (p)
{
    setOpaque (true); setLookAndFeel (&look);
    look.setColour (juce::TooltipWindow::backgroundColourId, panel);
    look.setColour (juce::TooltipWindow::textColourId, foreground);
    look.setColour (juce::TooltipWindow::outlineColourId, grid);
    addAndMakeVisible (bypass); addAndMakeVisible (automatic); automatic.setButtonText ("AUTO");
    addAndMakeVisible (meter);
    bypass.setTooltip ("Bypasses phase processing while preserving the fixed 25 ms latency.");
    automatic.setTooltip ("Saves CPU when Off; uses transient spectrogram phases to align narrow frequency bands.");
    bypassAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "bypass", bypass);
    autoAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "automatic", automatic);
    constexpr std::array<const char*, 6> ids { "low", "split", "high", "depth", "transient", "output" };
    constexpr std::array<const char*, 6> names { "LOW PHASE", "CROSSOVER", "HIGH PHASE", "AUTO DEPTH", "TRANSIENT", "OUTPUT" };
    constexpr std::array<const char*, 6> units { " deg", " Hz", " deg", " %", " ms", " dB" };
    constexpr std::array<const char*, 6> hints {
        "Rotates frequencies below the crossover relative to the fixed 25 ms reference.",
        "Sets the smooth transition between the low and high manual phase controls.",
        "Rotates frequencies above the crossover relative to the fixed 25 ms reference.",
        "Sets automatic phase correction from reverse alignment at negative values to full alignment at positive values.",
        "Sets how quickly automatic phase correction releases after a transient, with longer times retaining it longer.",
        "Adjusts the output level after manual and automatic phase rotation." };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& slider = sliders[i]; slider.setName (names[i]); slider.setTitle (names[i]);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 18);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, foreground); slider.setTextValueSuffix (units[i]);
        slider.setDoubleClickReturnValue (true, p.parameters.getParameter (ids[i])->convertFrom0to1 (p.parameters.getParameter (ids[i])->getDefaultValue()));
        labels[i].setText (names[i], juce::dontSendNotification); labels[i].setJustificationType (juce::Justification::centred);
        labels[i].setFont (juce::FontOptions (10)); labels[i].setColour (juce::Label::textColourId, secondary);
        addAndMakeVisible (slider); addAndMakeVisible (labels[i]);
        attachments[i] = std::make_unique<State::SliderAttachment> (p.parameters, ids[i], slider);
        editorTooltips::setHint (slider, labels[i], hints[i]);
    }
    editorScaling::enable (*this, designWidth, designHeight);
}
void XuanPhaseEditor::visibilityChanged()
{
    meter.setActive (isShowing());
    if (isShowing()) startTimerHz (20); else stopTimer();
}
void XuanPhaseEditor::resized()
{
    bypass.setBounds (450, 30, 74, 28); automatic.setBounds (440, 299, 72, 26);
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        const auto x = 63+static_cast<int> (i%3)*156;
        const auto y = i < 3 ? 160 : 360;
        labels[i].setBounds (x, y-20, 110, 16); sliders[i].setBounds (x, y, 110, 98);
    }
    const auto scale = editorScaling::transform (*this, designWidth, designHeight);
    meter.setBounds (536, 88, 280, 386); meter.setTransform (scale);
    bypass.setTransform (scale); automatic.setTransform (scale);
    for (auto& s : sliders) s.setTransform (scale);
    for (auto& l : labels) l.setTransform (scale);
}
void XuanPhaseEditor::paint (juce::Graphics& g)
{
    g.fillAll (background); g.saveState(); g.addTransform (editorScaling::transform (*this, designWidth, designHeight));
    g.setColour (secondary); g.setFont (juce::FontOptions (10));
    g.drawText ("J-SONIC / TRANSIENT PHASE", 24, 12, 370, 16, juce::Justification::centredLeft);
    g.setColour (foreground); g.setFont (juce::FontOptions (25));
    g.drawText ("XUAN PHASE", 24, 30, 370, 36, juce::Justification::centredLeft);
    drawPanel (g, {24, 88, 500, 188}); drawPanel (g, {24, 288, 500, 186});
    g.setColour (foreground); g.setFont (juce::FontOptions (13).withStyle ("bold"));
    g.drawText ("MANUAL ROTATION", 38, 100, 340, 20, juce::Justification::centredLeft);
    g.drawText ("TRANSIENT ALIGNMENT", 38, 300, 340, 20, juce::Justification::centredLeft);
    g.setColour (secondary); g.setFont (juce::FontOptions (9));
    g.drawText ("25 ms reference / +/-180 degrees / linked stereo phase", 38, 120, 460, 16, juce::Justification::centredLeft);
    g.drawText ("Spectral phase targets: " + juce::String (processor.targetCount()), 38, 321, 300, 16, juce::Justification::centredLeft);
    g.restoreState();
}
