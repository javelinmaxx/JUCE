#include "PluginEditor.h"

CharacterCompressorEditor::CharacterCompressorEditor (CharacterCompressorProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    setLookAndFeel (&look);
    look.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff222b34));
    look.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff39444f));
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
    highTilt.setTooltip ("Sidechain high shelf: +3 dB at 1.5 kHz, rising to +6 dB. Increases high-frequency compression sensitivity; no direct output EQ.");
    upward.setTooltip ("Boosts levels below threshold toward it; ratio sets the below-threshold slope.");

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
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 22);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xfff0ddbb));
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
    transformer.snapToState();
    highTilt.snapToState();
    bypass.snapToState();
    upward.snapToState();
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    setSize (580, 940);
    timerCallback();
    startTimerHz (30);
}

namespace
{
const juce::Colour gold (0xffdbaf66), blue (0xff168aad), orange (0xfff28e2b),
                   ink (0xff10171e), muted (0xff92a0ad);
}

void AnimatedSwitch::advance (float elapsed)
{
    const auto target = getToggleState() ? 1.0f : 0.0f;
    position += (target - position) * (1.0f - std::exp (-elapsed / 0.055f));
    if (std::abs (target - position) < 0.001f) position = target;
    repaint();
}

void AnimatedSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (1);
    g.setColour (juce::Colour (highlighted ? 0xff283440 : 0xff202a34));
    g.fillRoundedRectangle (bounds, 7);
    if (hasKeyboardFocus (true))
    {
        g.setColour (accent);
        g.drawRoundedRectangle (bounds, 7, 1);
    }
    g.setColour (getToggleState() ? accent : muted);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (getButtonText(), getLocalBounds().reduced (12, 0).withTrimmedRight (100),
                juce::Justification::centredLeft);
    const auto track = juce::Rectangle<float> (static_cast<float> (getWidth() - 64), 7, 48, 22);
    g.setColour (juce::Colour (0xff414c57).interpolatedWith (accent, position));
    g.fillRoundedRectangle (track, 11);
    g.setColour (down ? juce::Colours::white : juce::Colour (0xfff4ead9));
    g.fillEllipse (track.getX() + 3 + position * 26, track.getY() + 3, 16, 16);
    g.setColour (muted);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (getToggleState() ? "ON" : "OFF", getWidth() - 105, 0, 32, getHeight(),
                juce::Justification::centredRight);
}

void CompressorLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float value, float start, float end, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height)).reduced (8);
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    auto dial = juce::Rectangle<float> (radius * 2, radius * 2).withCentre (centre);
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0, start, end, true);
    g.setColour (juce::Colour (0xff38434e));
    g.strokePath (arc, juce::PathStrokeType (3.0f));
    juce::Path active;
    const auto angle = start + value * (end - start);
    active.addCentredArc (centre.x, centre.y, radius, radius, 0, start, angle, true);
    g.setColour (slider.isEnabled() ? accent : muted);
    g.strokePath (active, juce::PathStrokeType (3.0f));
    dial.reduce (6, 6);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff46515e), dial.getTopLeft(),
                                            juce::Colour (0xff1b242d), dial.getBottomRight(), false));
    g.fillEllipse (dial);
    g.setColour (juce::Colour (0xff53606d));
    g.drawEllipse (dial, 1);
    const auto outer = dial.getWidth() * 0.40f;
    const auto inner = outer * 0.58f;
    g.setColour (accent);
    g.drawLine (centre.x + std::sin (angle) * inner, centre.y - std::cos (angle) * inner,
                centre.x + std::sin (angle) * outer, centre.y - std::cos (angle) * outer, 3);
}

void CharacterCompressorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff171e26));
    g.setColour (upward.getToggleState() ? orange : blue);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("CHARACTER AUDIO  /  DYNAMICS", 24, 18, 532, 20, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xfff0ddbb));
    g.setFont (juce::FontOptions (29.0f));
    g.drawText ("Character Compressor", 24, 40, 532, 38, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff202a34));
    g.fillRoundedRectangle (24, 170, 330, 190, 10);
    for (const auto y : { 370, 498, 626 })
    {
        g.setColour (juce::Colour (0xff303c48));
        g.drawHorizontalLine (y, 24, 354);
    }
    paintMeter (g);
}

void CharacterCompressorEditor::paintMeter (juce::Graphics& g)
{
    g.setColour (ink);
    g.fillRoundedRectangle (meterBounds.toFloat(), 10);
    g.setColour (muted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (upward.getToggleState() ? "UPWARD GAIN" : "GAIN REDUCTION",
                388, 188, 156, 20, juce::Justification::centred);
    const juce::Rectangle<float> bar (392, 242, 14, 414), plot (442, 242, 98, 414);
    g.setColour (juce::Colour (0xff283440));
    g.fillRoundedRectangle (bar, 4);
    for (int db = 0; db <= 30; db += 6)
    {
        const auto y = plot.getY() + plot.getHeight() * static_cast<float> (db) / 30.0f;
        g.setColour (juce::Colour (0xff293540));
        g.drawLine (plot.getX(), y, plot.getRight(), y);
        g.setColour (muted);
        g.drawText (juce::String (db), 410, juce::roundToInt (y) - 8, 26, 16, juce::Justification::centred);
    }
    for (int second = 0; second <= 5; ++second)
    {
        const auto x = plot.getX() + plot.getWidth() * static_cast<float> (second) / 5.0f;
        g.setColour (juce::Colour (0xff202c36));
        g.drawLine (x, plot.getY(), x, plot.getBottom());
    }
    const auto meterAccent = upward.getToggleState() ? blue : gold;
    g.setColour (meterAccent);
    const auto fill = bar.withHeight (bar.getHeight() * std::clamp (displayedReduction / 30, 0.0f, 1.0f));
    if (fill.getHeight() > 0) g.fillRoundedRectangle (fill, 3);
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    juce::Path trace;
    bool started = false;
    double previous = 0;
    history.visit (now, [&] (float position, float reduction, double time)
    {
        const auto x = plot.getX() + plot.getWidth() * position;
        const auto y = plot.getY() + plot.getHeight() * reduction / 30.0f;
        if (! started || time - previous > 0.25) trace.startNewSubPath (x, y);
        else trace.lineTo (x, y);
        previous = time;
        started = true;
    });
    g.saveState();
    g.reduceClipRegion (plot.expanded (1).toNearestInt());
    g.setColour (meterAccent.withAlpha (0.12f));
    g.strokePath (trace, juce::PathStrokeType (5));
    g.setColour (meterAccent);
    g.strokePath (trace, juce::PathStrokeType (1.7f));
    g.restoreState();
    g.setColour (muted);
    g.drawText ("-5 s", 440, 662, 44, 18, juce::Justification::centredLeft);
    g.drawText ("NOW", 498, 662, 44, 18, juce::Justification::centredRight);
    g.setColour (meterAccent);
    g.setFont (juce::FontOptions (24.0f));
    g.drawText (juce::String (displayedReduction, 1) + " dB", 388, 694, 156, 32, juce::Justification::centred);
}

void CharacterCompressorEditor::resized()
{
    model.setBounds (24, 92, 532, 32);
    modelDescription.setBounds (24, 132, 532, 24);
    const auto place = [this] (size_t index, int x, int y, int width, int height)
    {
        labels[index].setBounds (x, y, width, 20);
        sliders[index].setBounds (x, y + 20, width, height);
    };
    place (1, 32, 178, 155, 154);
    place (2, 191, 178, 155, 154);
    place (3, 32, 378, 155, 94);
    place (4, 191, 378, 155, 94);
    place (0, 24, 506, 110, 88);
    place (6, 134, 506, 110, 88);
    place (7, 244, 506, 110, 88);
    place (5, 24, 634, 110, 88);
    place (8, 134, 634, 110, 88);
    place (9, 244, 634, 110, 88);
    // The long drive label needs a compact caption at the secondary control size.
    labels[9].setText ("IRON DRIVE", juce::dontSendNotification);
    meters.setBounds (24, 742, 532, 20);
    transformer.setBounds (24, 766, 532, 36);
    highTilt.setBounds (24, 806, 532, 36);
    upward.setBounds (24, 846, 532, 36);
    bypass.setBounds (24, 886, 532, 36);
}

void CharacterCompressorEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto elapsed = static_cast<float> (std::max (0.0, now - lastTick));
    lastTick = now;
    transformer.advance (elapsed);
    highTilt.advance (elapsed);
    upward.advance (elapsed);
    bypass.advance (elapsed);
    const auto upwardMode = upward.getToggleState();
    if (upwardMode != lastUpwardMode)
    {
        const auto accent = upwardMode ? orange : blue;
        look.setAccent (accent);
        for (auto* control : { &transformer, &highTilt, &upward, &bypass })
            control->setAccent (accent);
        for (auto& slider : sliders) slider.repaint();
        history.clear();
        displayedReduction = 0;
        lastUpwardMode = upwardMode;
        repaint();
    }
    const std::array<const char*, 4> descriptions {
        "Fast peak detection / feed-forward gain control",
        "Output-sensing peak detector / feedback gain control",
        "RMS feedback / progressive ratio / program-dependent recovery",
        "RMS feed-forward / two-stage optical recovery" };
    modelDescription.setFont (juce::FontOptions (12.0f));
    modelDescription.setColour (juce::Label::textColourId, muted);
    modelDescription.setText (descriptions[static_cast<size_t> (std::clamp (model.getSelectedItemIndex(), 0, 3))],
                              juce::dontSendNotification);
    const auto raw = processor.reduction.load (std::memory_order_relaxed);
    const auto gr = std::isfinite (raw) ? std::clamp (raw, 0.0f, 30.0f) : 0.0f;
    displayedReduction = std::max (gr, displayedReduction * std::exp (-elapsed / 0.25f));
    history.push (now, gr);
    meters.setFont (juce::FontOptions (11.0f));
    meters.setColour (juce::Label::textColourId, muted);
    meters.setText ("IN " + juce::String (processor.inputPeak.load(), 1) + " dBFS    /    OUT "
        + juce::String (processor.outputPeak.load(), 1) + " dBFS", juce::dontSendNotification);
    repaint (meterBounds);
}
