#include "PluginEditor.h"

CharacterCompressorEditor::CharacterCompressorEditor (CharacterCompressorProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    model.addItemList ({ "FET / Feed-forward", "VCA / Feedback", "Variable-mu", "Optical" }, 1);
    model.setAccessible (true);
    model.setTitle ("Compressor model");
    addAndMakeVisible (model);
    addAndMakeVisible (transformer);
    addAndMakeVisible (bypass);
    addAndMakeVisible (highTilt);
    addAndMakeVisible (upward);
    modelAttachment = std::make_unique<State::ComboBoxAttachment> (p.parameters, "model", model);
    transformerAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "transformer", transformer);
    bypassAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "bypass", bypass);
    tiltAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "highTilt", highTilt);
    upwardAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "upward", upward);
    upward.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff168aad));
    highTilt.setTooltip ("Sidechain high shelf: +3 dB at 1.5 kHz, rising to +6 dB. Increases high-frequency compression sensitivity; no direct output EQ.");
    upward.setTooltip ("Upward compression: levels above Threshold pass at 0 dB gain; quieter levels are boosted toward Threshold. Ratio sets the below-threshold slope.");

    const std::array<const char*, 10> ids { "input", "threshold", "ratio", "attack", "release",
        "knee", "output", "mix", "sidechain", "drive" };
    const std::array<const char*, 10> names { "INPUT", "THRESHOLD", "RATIO", "ATTACK", "RELEASE",
        "KNEE", "OUTPUT", "MIX", "DETECTOR HPF", "TRANSFORMER DRIVE" };
    const std::array<const char*, 10> units { " dB", " dB", ":1", " ms", " ms",
        " dB", " dB", " %", " Hz", " dB" };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& slider = sliders[i];
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 104, 24);
        slider.setTextValueSuffix (units[i]);
        slider.setName (names[i]);
        slider.setTitle (names[i]);
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff168aad));
        slider.setDoubleClickReturnValue (true, p.parameters.getParameter (ids[i])->convertFrom0to1 (
            p.parameters.getParameter (ids[i])->getDefaultValue()));
        labels[i].setText (names[i], juce::dontSendNotification);
        labels[i].setJustificationType (juce::Justification::centred);
        labels[i].setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (slider);
        addAndMakeVisible (labels[i]);
        attachments[i] = std::make_unique<State::SliderAttachment> (p.parameters, ids[i], slider);
    }
    sliders[8].setTooltip ("High-pass filter in the detector only. 0 Hz disables it.");
    sliders[9].setTooltip ("Transformer saturation drive. Generated distortion rolls off above 500 Hz; clean highs pass through.");
    modelDescription.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (modelDescription);
    meters.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (meters);
    setSize (780, 530);
    timerCallback();
    startTimerHz (30);
}

void CharacterCompressorEditor::paint (juce::Graphics& g)
{
    const auto upwardMode = upward.getToggleState();
    const auto interfaceAccent = upwardMode ? juce::Colour (0xfff28e2b) : juce::Colour (0xff168aad);
    const auto meterAccent = upwardMode ? juce::Colour (0xff168aad) : juce::Colour (0xffdbaf66);
    g.fillAll (juce::Colour (0xff171c22));
    g.setColour (interfaceAccent);
    g.setFont (juce::FontOptions (25.0f));
    g.drawText ("CHARACTER COMPRESSOR", 24, 12, 590, 40, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff333d49));
    g.fillRoundedRectangle (24.0f, 440.0f, 732.0f, 18.0f, 4.0f);
    g.setColour (meterAccent);
    g.fillRoundedRectangle (24.0f, 440.0f, 732.0f * std::clamp (displayedReduction / 30, 0.0f, 1.0f),
                            18.0f, 4.0f);
    // Tick every 5 dB for a readable 0–30 dB scale in both operating modes.
    g.setColour (juce::Colour (0xbfffffff));
    g.setFont (juce::FontOptions (10.0f));
    for (int db = 0; db <= 30; db += 5)
    {
        const auto x = 24.0f + 732.0f * static_cast<float> (db) / 30.0f;
        g.drawVerticalLine (static_cast<int> (x), 440.0f, 458.0f);
        g.drawText (juce::String (db), static_cast<int> (x) - 12, 460, 24, 16,
                    juce::Justification::centred);
    }
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (juce::Colours::lightgrey);
    g.drawText (upwardMode ? "UPWARD GAIN   0 - 30 dB" : "GAIN REDUCTION   0 - 30 dB",
                24, 483, 300, 20, juce::Justification::centredLeft);
}

void CharacterCompressorEditor::resized()
{
    bypass.setBounds (660, 20, 100, 28);
    upward.setBounds (520, 20, 120, 28);
    model.setBounds (24, 64, 300, 32);
    transformer.setBounds (340, 64, 220, 32);
    highTilt.setBounds (570, 64, 190, 32);
    modelDescription.setBounds (24, 105, 732, 28);
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        const int x = 24 + static_cast<int> (i % 5) * 148;
        const int y = 145 + static_cast<int> (i / 5) * 140;
        labels[i].setBounds (x, y, 140, 20);
        sliders[i].setBounds (x, y + 20, 140, 115);
    }
    meters.setBounds (340, 483, 416, 25);
}

void CharacterCompressorEditor::timerCallback()
{
    const auto upwardMode = upward.getToggleState();
    if (upwardMode != lastUpwardMode)
    {
        // Normal mode uses blue controls and an orange meter; upward mode swaps
        // those accents so the mode change is apparent throughout the editor.
        const auto interfaceAccent = upwardMode ? juce::Colour (0xfff28e2b) : juce::Colour (0xff168aad);
        for (auto& slider : sliders)
            slider.setColour (juce::Slider::rotarySliderFillColourId, interfaceAccent);
        upward.setColour (juce::ToggleButton::tickColourId, interfaceAccent);
        lastUpwardMode = upwardMode;
        repaint();
    }
    const std::array<const char*, 4> descriptions {
        "Fast peak detection / feed-forward gain control",
        "Output-sensing peak detector / feedback gain control",
        "RMS feedback / progressive ratio / program-dependent recovery",
        "RMS feed-forward / two-stage optical recovery" };
    modelDescription.setText (descriptions[static_cast<size_t> (std::clamp (model.getSelectedItemIndex(), 0, 3))],
                              juce::dontSendNotification);
    const auto gr = processor.reduction.load (std::memory_order_relaxed);
    displayedReduction = std::max (gr, displayedReduction * 0.9f);
    meters.setText ("IN " + juce::String (processor.inputPeak.load(), 1) + " dBFS    OUT "
        + juce::String (processor.outputPeak.load(), 1) + " dBFS    "
        + juce::String (upward.getToggleState() ? "GAIN " : "GR ")
        + juce::String (displayedReduction, 1) + " dB", juce::dontSendNotification);
    repaint (24, 440, 732, 40);
}
