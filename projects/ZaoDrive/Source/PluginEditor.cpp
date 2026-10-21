#include "PluginEditor.h"
#include "../../EditorScaling.h"
#include "../../EditorTooltips.h"
#include <cmath>

namespace
{
const auto background = juce::Colour (0xff100e12);
const auto panel = juce::Colour (0xff1c1920);
const auto inset = juce::Colour (0xff09080c);
const auto foreground = juce::Colour (0xffeee9e2);
const auto secondary = juce::Colour (0xffaaa0a0);
const auto amber = juce::Colour (0xffe3a45b);
const auto grid = juce::Colour (0xff30262c);

const juce::Image& analogGrain()
{
    static const auto grain = []
    {
        juce::Image image (juce::Image::ARGB, 96, 96, true);
        image.clear (image.getBounds(), juce::Colours::transparentBlack);
        auto state = 0x6d2b79f5u;
        for (int i = 0; i < 1500; ++i)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const auto x = static_cast<int> (state % 96u);
            state = state * 1664525u + 1013904223u;
            const auto y = static_cast<int> ((state >> 8) % 96u);
            const auto alpha = static_cast<juce::uint8> (3 + ((state >> 20) % 10u));
            image.setPixelAt (x, y, juce::Colour::fromRGBA (220, 211, 190, alpha));
        }
        return image;
    }();
    return grain;
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (bounds.translated (0, 3), 10);
    g.setGradientFill (juce::ColourGradient (panel.brighter (0.1f), bounds.getTopLeft(),
                                            panel.darker (0.12f), bounds.getBottomLeft(), false));
    g.fillRoundedRectangle (bounds, 10);
    juce::Path clip;
    clip.addRoundedRectangle (bounds, 10);
    g.saveState();
    g.reduceClipRegion (clip);
    const auto& grain = analogGrain();
    for (auto y = juce::roundToInt (bounds.getY()); y < juce::roundToInt (bounds.getBottom()); y += 96)
        for (auto x = juce::roundToInt (bounds.getX()); x < juce::roundToInt (bounds.getRight()); x += 96)
            g.drawImageAt (grain, x, y);
    g.restoreState();
    g.setColour (grid.withAlpha (0.72f));
    g.drawRoundedRectangle (bounds, 10, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (juce::roundToInt (bounds.getY() + 1), bounds.getX() + 10,
                          bounds.getRight() - 10);
}
}

void ZaoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float value, float start, float end, juce::Slider&)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height)).reduced (8);
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = start + value * (end - start);
    for (int tick = 0; tick <= 10; ++tick)
    {
        const auto a = start + (end - start) * static_cast<float> (tick) / 10.0f;
        const auto inner = radius - (tick % 5 == 0 ? 4.0f : 2.0f);
        g.setColour (secondary.withAlpha (tick % 5 == 0 ? 0.5f : 0.25f));
        g.drawLine (centre.x + std::sin (a) * inner, centre.y - std::cos (a) * inner,
                    centre.x + std::sin (a) * radius, centre.y - std::cos (a) * radius, 1.0f);
    }
    juce::Path track, active;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0, start, end, true);
    active.addCentredArc (centre.x, centre.y, radius, radius, 0, start, angle, true);
    g.setColour (grid.brighter (0.25f));
    g.strokePath (track, juce::PathStrokeType (2.0f));
    g.setColour (amber);
    g.strokePath (active, juce::PathStrokeType (2.2f));
    auto dial = juce::Rectangle<float> (radius * 2 - 10, radius * 2 - 10).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (dial.translated (0, 2));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff65504a), dial.getTopLeft(),
                                            juce::Colour (0xff30252a), dial.getBottomRight(), false));
    g.fillEllipse (dial);
    g.setColour (secondary.withAlpha (0.25f));
    g.drawEllipse (dial.reduced (2), 0.7f);
    g.setColour (amber);
    const auto inner = dial.getWidth() * 0.23f, outer = dial.getWidth() * 0.39f;
    g.drawLine (centre.x + std::sin (angle) * inner, centre.y - std::cos (angle) * inner,
                centre.x + std::sin (angle) * outer, centre.y - std::cos (angle) * outer, 1.8f);
}

juce::Label* ZaoLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (10.0f));
    return label;
}

void ZaoBypassButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1);
    const auto active = getToggleState();
    const auto face = active ? panel.interpolatedWith (amber.darker (0.5f), 0.6f) : panel;
    g.setColour (down ? face.darker (0.1f) : highlighted ? face.brighter (0.06f) : face);
    g.fillRoundedRectangle (bounds, 6);
    g.setColour (active ? amber : secondary.withAlpha (0.7f));
    g.drawRoundedRectangle (bounds, 6, 1.0f);
    g.setColour (active ? foreground : secondary);
    g.setFont (juce::FontOptions (10.0f).withStyle ("bold"));
    g.drawText ("BYPASS", bounds.toNearestInt(), juce::Justification::centred);
}

void ZaoTransformerSwitch::paintButton (juce::Graphics& g, bool, bool)
{
    const auto cx = static_cast<float> (getWidth()) * 0.5f;
    constexpr auto pivotY = static_cast<float> (ZaoTransformerSwitch::pivotOffsetY);
    g.setColour (secondary);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("CORE", 0, 0, getWidth(), 15, juce::Justification::centred);
    juce::Path nut;
    for (int corner = 0; corner < 6; ++corner)
    {
        const auto angle = zao::pi * 2.0 * corner / 6.0;
        const juce::Point<float> point (cx + 15.0f * static_cast<float> (std::cos (angle)),
                                        pivotY + 15.0f * static_cast<float> (std::sin (angle)));
        if (corner == 0) nut.startNewSubPath (point); else nut.lineTo (point);
    }
    nut.closeSubPath();
    g.setGradientFill (juce::ColourGradient (secondary.brighter (0.16f), cx - 12, pivotY - 13,
                                             secondary.darker (0.34f), cx + 10, pivotY + 13, false));
    g.fillPath (nut);
    g.setColour (panel.darker (0.5f));
    g.fillEllipse (cx - 11, pivotY - 11, 22, 22);
    g.setColour (secondary);
    g.drawEllipse (cx - 11, pivotY - 11, 22, 22, 1);
    const auto tipY = pivotY + (getToggleState() ? -9.0f : 9.0f);
    g.setColour (juce::Colour (0xffc6cbd0));
    g.drawLine (cx, pivotY, cx, tipY, 4.8f);
    const auto cap = juce::Rectangle<float> (cx - 4.4f, tipY - 4.4f, 8.8f, 8.8f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0e3e5), cap.getTopLeft(),
                                             juce::Colour (0xff777f86), cap.getBottomRight(), false));
    g.fillEllipse (cap);
    g.setColour (getToggleState() ? amber : secondary);
    g.setFont (juce::FontOptions (9.0f).withStyle ("bold"));
    g.drawText (getToggleState() ? "ON" : "OFF", 0, 88, getWidth(), 18, juce::Justification::centred);
    if (hasKeyboardFocus (true)) g.drawEllipse (cx - 18, pivotY - 18, 36, 36, 1);
}

ZaoDriveEditor::ZaoDriveEditor (ZaoDriveProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    setLookAndFeel (&look);
    look.setColour (juce::TooltipWindow::backgroundColourId, panel);
    look.setColour (juce::TooltipWindow::textColourId, foreground);
    look.setColour (juce::TooltipWindow::outlineColourId, grid);
    addAndMakeVisible (bypass); addAndMakeVisible (transformer);
    bypass.setTooltip ("Bypasses the overdrive and transformer while preserving plugin latency.");
    transformer.setTooltip ("Saves CPU when Off; adds YinYang's transformer coloration when On.");
    bypassAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "bypass", bypass);
    transformerAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "transformer", transformer);
    constexpr std::array<const char*, 7> ids { "input", "drive", "tone", "output", "mix", "coreDrive", "dcBias" };
    constexpr std::array<const char*, 7> names { "INPUT", "DRIVE", "TONE", "OUTPUT", "MIX", "CORE DRIVE", "DC BIAS" };
    constexpr std::array<const char*, 7> units { " dB", " dB", " Hz", " dB", " %", " dB", " %" };
    constexpr std::array<const char*, 7> hints {
        "Adjusts the level entering the selected overdrive circuit.",
        "Increases the signal driving the transistor or op-amp feedback circuit.",
        "Sets the low-pass cutoff after the overdrive circuit.",
        "Adjusts gain after the wet and dry mix and before the output transformer.",
        "Blends the overdriven signal with the latency-aligned original input.",
        "Increases saturation in the enabled output transformer.",
        "Adds transformer asymmetry to increase even harmonics."
    };
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
    model.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    model.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 100, 18);
    model.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    model.setColour (juce::Slider::textBoxTextColourId, foreground);
    model.setName ("CIRCUIT"); model.setTitle ("Circuit");
    modelLabel.setText ("CIRCUIT", juce::dontSendNotification);
    modelLabel.setJustificationType (juce::Justification::centred);
    modelLabel.setFont (juce::FontOptions (10.0f));
    modelLabel.setColour (juce::Label::textColourId, secondary);
    addAndMakeVisible (model); addAndMakeVisible (modelLabel);
    modelAttachment = std::make_unique<State::SliderAttachment> (p.parameters, "model", model);
    model.textFromValueFunction = [] (double value) { return value < 0.5 ? "BJT CE" : "OPAMP FB"; };
    model.updateText();
    editorTooltips::setHint (model, modelLabel,
        "Selects the common-emitter BJT amplifier or the op-amp diode feedback clipper.");
    editorScaling::enable (*this, designWidth, designHeight);
    timerCallback();
    startTimerHz (5);
}

void ZaoDriveEditor::refreshPreview (const zao::Settings& settings)
{
    // This independent engine lives on the message thread; the display never
    // reads or advances the audio thread's nonlinear circuit states.
    zao::DriveEngine preview;
    preview.prepare (48000, 128);
    constexpr int warmup = 4096;
    const auto total = warmup + static_cast<int> (expectedOutput.size());
    juce::AudioBuffer<float> block (2, 128);
    expectedPeak = 0;
    for (int offset = 0; offset < total; offset += 128)
    {
        const auto count = std::min (128, total - offset);
        block.setSize (2, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const auto sample = 0.25f * std::sin (static_cast<float> (2.0 * zao::pi * (offset + i) / 48.0));
            block.setSample (0, i, sample); block.setSample (1, i, sample);
        }
        preview.process (block, settings);
        for (int i = 0; i < count; ++i)
            if (offset + i >= warmup)
            {
                const auto index = static_cast<size_t> (offset + i - warmup);
                expectedInput[index] = 0.25f * std::sin (static_cast<float> (
                    2.0 * zao::pi * (offset + i - preview.getLatency()) / 48.0));
                expectedOutput[index] = block.getSample (0, i);
                expectedPeak = std::max (expectedPeak, std::abs (expectedOutput[index]));
            }
    }
}

void ZaoDriveEditor::timerCallback()
{
    const auto s = processor.getSettings();
    const std::array<float, 10> current { s.inputDb, s.driveDb, s.toneHz, s.outputDb, s.mix,
        s.coreDriveDb, s.dcBias, static_cast<float> (s.model), s.transformer ? 1.0f : 0.0f, s.bypass ? 1.0f : 0.0f };
    if (! previewReady || current != previousSettings)
    {
        refreshPreview (s);
        previousSettings = current; previewReady = true;
    }
    repaint();
}

void ZaoDriveEditor::paintExpectedOutput (juce::Graphics& g)
{
    g.setColour (foreground); g.setFont (juce::FontOptions (12.0f).withStyle ("bold"));
    g.drawText ("EXPECTED OUTPUT", 38, 303, 240, 18, juce::Justification::centredLeft);
    g.setColour (secondary); g.setFont (juce::FontOptions (9.0f));
    g.drawText ("1 kHz sine  /  -12 dBFS reference", 38, 324, 280, 16, juce::Justification::centredLeft);
    const auto scale = std::max (1.0f, std::ceil (expectedPeak * 2.0f) * 0.5f);
    g.drawText ("SCALE +/-" + juce::String (scale, 1) + " FS", 350, 324, 154, 16, juce::Justification::centredRight);
    const auto plot = juce::Rectangle<float> (44, 352, 460, 106);
    g.setColour (inset); g.fillRoundedRectangle (plot.expanded (6), 5);
    g.setColour (grid);
    for (int line = 0; line <= 4; ++line)
    {
        const auto x = plot.getX() + plot.getWidth() * static_cast<float> (line) * 0.25f;
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        const auto y = plot.getY() + plot.getHeight() * static_cast<float> (line) * 0.25f;
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }
    const auto drawWave = [&] (const auto& samples, juce::Colour colour, float thickness)
    {
        juce::Path wave;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (samples.size() - 1);
            const auto y = plot.getCentreY() - samples[i] / scale * plot.getHeight() * 0.48f;
            if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
        }
        g.setColour (colour); g.strokePath (wave, juce::PathStrokeType (thickness));
    };
    drawWave (expectedInput, secondary.withAlpha (0.7f), 1.0f);
    drawWave (expectedOutput, amber, 2.0f);
    g.setFont (juce::FontOptions (9.0f)); g.setColour (secondary);
    g.drawText ("INPUT", 44, 473, 45, 14, juce::Justification::centredLeft);
    g.setColour (amber); g.drawText ("OUTPUT", 96, 473, 60, 14, juce::Justification::centredLeft);
    g.drawText ("PEAK " + juce::String (juce::Decibels::gainToDecibels (expectedPeak, -100.0f), 1) + " dBFS",
                300, 473, 204, 14, juce::Justification::centredRight);
}

void ZaoDriveEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.addTransform (editorScaling::transform (*this, designWidth, designHeight));
    g.setGradientFill (juce::ColourGradient (background.brighter (0.08f), 0, 0,
                                            background.darker (0.13f), 0, designHeight, false));
    g.fillRect (0, 0, designWidth, designHeight);
    g.setColour (secondary); g.setFont (juce::FontOptions (10.0f));
    g.drawText ("J-SONIC / CIRCUIT OVERDRIVE", 24, 20, 350, 16, juce::Justification::centredLeft);
    g.setColour (foreground); g.setFont (juce::FontOptions (25.0f));
    g.drawText ("ZAO DRIVE", 24, 39, 350, 30, juce::Justification::centredLeft);
    for (const auto bounds : { juce::Rectangle<float> (24, 90, 500, 188),
                              juce::Rectangle<float> (24, 292, 500, 208),
                              juce::Rectangle<float> (24, 514, 300, 152),
                              juce::Rectangle<float> (340, 514, 184, 152) }) drawPanel (g, bounds);
    g.setColour (foreground); g.setFont (juce::FontOptions (12.0f).withStyle ("bold"));
    g.drawText ("OVERDRIVE CIRCUIT", 38, 101, 250, 18, juce::Justification::centredLeft);
    g.drawText ("TRANSFORMER STAGE", 38, 526, 270, 18, juce::Justification::centredLeft);
    g.drawText ("OUTPUT", 354, 526, 150, 18, juce::Justification::centredLeft);
    paintExpectedOutput (g);
}

void ZaoDriveEditor::resized()
{
    bypass.setBounds (450, 33, 74, 28);
    const auto place = [this] (size_t index, int x, int y, int width, int height)
    {
        labels[index].setBounds (x, y, width, 17);
        sliders[index].setBounds (x, y + 20, width, height);
    };
    modelLabel.setBounds (36, 129, 100, 17);
    model.setBounds (36, 149, 100, 101);
    place (1, 161, 129, 100, 101);
    place (0, 286, 129, 100, 101);
    place (2, 411, 129, 100, 101);
    place (5, 38, 553, 88, 85);
    place (6, 138, 553, 88, 85);
    const auto knobCentreY = 553 + 20 + look.getSliderLayout (sliders[5]).sliderBounds.getCentreY();
    transformer.setBounds (240, knobCentreY - ZaoTransformerSwitch::pivotOffsetY, 62, 106);
    place (3, 350, 553, 80, 85);
    place (4, 438, 553, 80, 85);
    const auto scale = editorScaling::transform (*this, designWidth, designHeight);
    bypass.setTransform (scale); transformer.setTransform (scale);
    model.setTransform (scale); modelLabel.setTransform (scale);
    for (auto& slider : sliders) slider.setTransform (scale);
    for (auto& label : labels) label.setTransform (scale);
}
