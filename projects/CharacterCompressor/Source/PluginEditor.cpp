#include "PluginEditor.h"

CharacterCompressorEditor::CharacterCompressorEditor (CharacterCompressorProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    setLookAndFeel (&look);
    model.addItemList ({ "FET / Feed-forward", "VCA / Feedback", "Variable-mu", "Optical" }, 1);
    model.setAccessible (true);
    model.setTitle ("Compressor model");
    addAndMakeVisible (model);
    addAndMakeVisible (transformer);
    addAndMakeVisible (bypass);
    addAndMakeVisible (upward);
    addAndMakeVisible (midSide);
    modelAttachment = std::make_unique<State::ComboBoxAttachment> (p.parameters, "model", model);
    transformerAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "transformer", transformer);
    bypassAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "bypass", bypass);
    upwardAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "upward", upward);
    midSideAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "midSide", midSide);
    upward.setTooltip ("Boosts levels below threshold toward it; ratio sets the below-threshold slope.");

    const std::array<const char*, 14> ids { "input", "threshold", "ratio", "attack", "release",
        "knee", "output", "mix", "sidechain", "drive", "stereoLink", "evenHarmonics", "variTransient", "scHighTilt" };
    const std::array<const char*, 14> names { "INPUT", "THRESHOLD", "RATIO", "ATTACK", "RELEASE",
        "KNEE", "OUTPUT", "MIX", "DETECTOR HPF", "TRANSFORMER DRIVE", "STEREO LINK", "EVEN HARMONICS", "VARITRANSIENT", "SC HIGH TILT" };
    const std::array<const char*, 14> units { " dB", " dB", ":1", " ms", " ms",
        " dB", " dB", " %", " Hz", " dB", " %", " %", " %", " dB" };
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
    sliders[10].setTooltip ("Blends each channel's detector with the louder detector: 0% is independent, 100% is fully linked.");
    sliders[11].setTooltip ("Simulates transformer DC magnetizing current, increasing transfer asymmetry and even-order harmonics.");
    sliders[12].setTooltip ("GR-dependent attack and release: 0% has no effect; 400% reaches 4x attack and 1/4 release. Ramps through a 3 dB soft knee from 3 to 6 dB gain reduction. Disabled in upward mode.");
    sliders[13].setTooltip ("Sidechain high-shelf boost from 0 to 12 dB. The shelf midpoint is 1.5 kHz; it changes compression sensitivity only and does not EQ the output.");
    midSide.setTooltip ("Encode stereo to Mid and Side before compression, then decode back to Left and Right.");
    modelDescription.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (modelDescription);
    meters.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (meters);
    transformer.snapToState();
    bypass.snapToState();
    upward.snapToState();
    midSide.snapToState();
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    setSize (580, 980);
    timerCallback();
    startTimerHz (30);
}

namespace
{
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

void drawAnalogPanel (juce::Graphics& g, juce::Rectangle<float> bounds, float radius,
                      juce::Colour material, juce::Colour edge)
{
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (bounds.translated (0, 3), radius + 1);
    g.setGradientFill (juce::ColourGradient (material.brighter (0.10f), bounds.getTopLeft(),
        material.darker (0.12f), bounds.getBottomLeft(), false));
    g.fillRoundedRectangle (bounds, radius);
    auto path = juce::Path();
    path.addRoundedRectangle (bounds, radius);
    g.saveState();
    g.reduceClipRegion (path);
    const auto& grain = analogGrain();
    for (auto y = juce::roundToInt (bounds.getY()); y < juce::roundToInt (bounds.getBottom()); y += grain.getHeight())
        for (auto x = juce::roundToInt (bounds.getX()); x < juce::roundToInt (bounds.getRight()); x += grain.getWidth())
            g.drawImageAt (grain, x, y);
    g.restoreState();
    g.setColour (edge.withAlpha (0.72f));
    g.drawRoundedRectangle (bounds, radius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (material.getPerceivedBrightness() > 0.45f ? 0.17f : 0.10f));
    g.drawHorizontalLine (juce::roundToInt (bounds.getY() + 1), bounds.getX() + radius, bounds.getRight() - radius);
    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.drawHorizontalLine (juce::roundToInt (bounds.getBottom() - 1), bounds.getX() + radius, bounds.getRight() - radius);
}

CompressorTheme themeFor (int model, bool upward)
{
    const auto colour = [] (juce::uint32 argb) { return juce::Colour (argb); };
    switch (model)
    {
        case 0: // FET: charcoal panel and gold reduction.
            if (upward)
                return { colour (0xff382f22), colour (0xff473b2b), colour (0xff191c1f),
                         colour (0xfffff2d8), colour (0xffd4c5a7), colour (0xffe2b85f),
                         colour (0xffa9b1b9), colour (0xff504331), colour (0xff4b5055),
                         colour (0xff4c4436), colour (0xff695843), colour (0xff292622) };
            return { colour (0xff252b31), colour (0xff303840), colour (0xff151a1e),
                     colour (0xfff0f1ee), colour (0xffb7c0c6), colour (0xffa9b1b9),
                     colour (0xffe2b85f), colour (0xff35404a), colour (0xff414b53),
                     colour (0xff354048), colour (0xff59646d), colour (0xff222930) };
        case 1: // VCA: blue panel and white reduction.
            if (upward)
                return { colour (0xff23465d), colour (0xff2d536b), colour (0xff102333),
                         colour (0xfff7fbff), colour (0xffcadce8), colour (0xfff7fbff),
                         colour (0xff48a9dc), colour (0xff365f78), colour (0xff435d70),
                         colour (0xff36566b), colour (0xff617d91), colour (0xff263f50) };
            return { colour (0xff142b3d), colour (0xff203e54), colour (0xff0d1d2b),
                     colour (0xfff2f8fc), colour (0xffadc9d8), colour (0xff48a9dc),
                     colour (0xfff7fbff), colour (0xff24475e), colour (0xff3c5262),
                     colour (0xff314e60), colour (0xff54738b), colour (0xff1a3447) };
        case 2: // Variable-mu: light green panel and white reduction.
            if (upward)
                return { colour (0xff355a46), colour (0xff406b52), colour (0xff182e25),
                         colour (0xfff8fff7), colour (0xffd5e7d5), colour (0xfff8fff7),
                         colour (0xff91c98a), colour (0xff4a7358), colour (0xff4f7058),
                         colour (0xff496b54), colour (0xff789582), colour (0xff294a37) };
            return { colour (0xffe4f0df), colour (0xffd1e5c9), colour (0xff1d392b),
                     colour (0xff253b29), colour (0xff526b52), colour (0xff70a963),
                     colour (0xfff8fff7), colour (0xffd1e1ce), colour (0xff526b59),
                     colour (0xff49624f), colour (0xffedf5e9), colour (0xff6c9273) };
        default: // Optical: white panel and red reduction.
            if (upward)
                return { colour (0xff3f2026), colour (0xff542a33), colour (0xff1c1b20),
                         colour (0xfffff7f6), colour (0xffdec6c9), colour (0xffdc4a55),
                         colour (0xfff8faf9), colour (0xff60333b), colour (0xff51454a),
                         colour (0xff554046), colour (0xff8d5960), colour (0xff37262a) };
            return { colour (0xfff5f7f6), colour (0xffe8edeb), colour (0xff252a30),
                     colour (0xff283138), colour (0xff56646b), colour (0xfff8faf9),
                     colour (0xffdc4a55), colour (0xffd6dedb), colour (0xff56616a),
                     colour (0xff525d64), colour (0xff59656c), colour (0xff252d32) };
    }
}
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
    drawAnalogPanel (g, bounds, 7, highlighted ? surface.brighter (0.06f) : surface, secondary);
    if (hasKeyboardFocus (true))
    {
        g.setColour (accent);
        g.drawRoundedRectangle (bounds, 7, 1);
    }
    if (horizontal)
    {
        g.setColour (getToggleState() ? foreground : secondary);
        g.setFont (juce::FontOptions (11.0f).withStyle ("bold"));
        g.drawText (getButtonText(), 7, 0, getWidth() - 48, getHeight(), juce::Justification::centred);
        const auto track = juce::Rectangle<float> (static_cast<float> (getWidth() - 39),
            static_cast<float> (getHeight()) * 0.5f - 9, 34, 18);
        drawAnalogPanel (g, track, 9, secondary.darker (0.35f).interpolatedWith (accent.darker (0.20f), position),
                         accent.withAlpha (0.56f));
        const auto thumb = juce::Rectangle<float> (track.getX() + 2 + position * 14, track.getY() + 2, 14, 14);
        g.setColour (juce::Colours::black.withAlpha (0.34f));
        g.fillEllipse (thumb.translated (0, 1));
        g.setGradientFill (juce::ColourGradient (down ? juce::Colours::white : juce::Colour (0xfff4f0e6),
            thumb.getTopLeft(), juce::Colour (0xff8a8c88), thumb.getBottomLeft(), false));
        g.fillEllipse (thumb);
        g.setColour (juce::Colours::white.withAlpha (0.42f));
        g.drawEllipse (thumb, 0.7f);
        if (hasKeyboardFocus (true))
        {
            g.setColour (accent);
            g.drawRoundedRectangle (bounds, 7, 1);
        }
        return;
    }

    const auto titleHeight = std::min (42, juce::roundToInt (getHeight() * 0.32f));
    g.setColour (getToggleState() ? foreground : secondary);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (getButtonText(), getLocalBounds().reduced (5).withHeight (titleHeight),
                      juce::Justification::centred, 3);
    const auto trackWidth = 26.0f;
    const auto trackHeight = std::min (56.0f, static_cast<float> (getHeight()) * 0.38f);
    const auto trackY = static_cast<float> (titleHeight) + 5.0f;
    const auto track = juce::Rectangle<float> (static_cast<float> (getWidth()) * 0.5f - trackWidth * 0.5f,
        trackY, trackWidth, trackHeight);
    drawAnalogPanel (g, track, 11, secondary.darker (0.35f).interpolatedWith (accent.darker (0.20f), position),
                     accent.withAlpha (0.56f));
    const auto thumb = juce::Rectangle<float> (track.getCentreX() - 8,
        track.getBottom() - 19 - position * (trackHeight - 20), 16, 16);
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillEllipse (thumb.translated (0, 1));
    g.setGradientFill (juce::ColourGradient (down ? juce::Colours::white : juce::Colour (0xfff4f0e6),
        thumb.getTopLeft(), juce::Colour (0xff8a8c88), thumb.getBottomLeft(), false));
    g.fillEllipse (thumb);
    g.setColour (juce::Colours::white.withAlpha (0.42f));
    g.drawEllipse (thumb, 0.7f);
    g.setColour (secondary);
    g.setFont (juce::FontOptions (10.0f));
    const auto statusY = juce::roundToInt (track.getBottom()) + 3;
    g.drawText (getToggleState() ? "ON" : "OFF", 0, statusY, getWidth(),
                std::max (0, getHeight() - statusY), juce::Justification::centredTop);
}

void CompressorLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float value, float start, float end, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height)).reduced (8);
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    auto dial = juce::Rectangle<float> (radius * 2, radius * 2).withCentre (centre);
    const auto outerRadius = radius + 1.5f;
    for (int tick = 0; tick <= 10; ++tick)
    {
        const auto tickAngle = start + (end - start) * static_cast<float> (tick) / 10.0f;
        const auto major = tick % 5 == 0;
        const auto outer = outerRadius;
        const auto inner = outer - (major ? 5.0f : 2.5f);
        g.setColour (ring.withAlpha (major ? 0.82f : 0.48f));
        g.drawLine (centre.x + std::sin (tickAngle) * inner, centre.y - std::cos (tickAngle) * inner,
                    centre.x + std::sin (tickAngle) * outer, centre.y - std::cos (tickAngle) * outer,
                    major ? 1.25f : 0.8f);
    }
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0, start, end, true);
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.strokePath (arc, juce::PathStrokeType (4.4f));
    g.setColour (ring.brighter (0.12f));
    g.strokePath (arc, juce::PathStrokeType (3.0f));
    juce::Path active;
    const auto angle = start + value * (end - start);
    active.addCentredArc (centre.x, centre.y, radius, radius, 0, start, angle, true);
    g.setColour (slider.isEnabled() ? accent : ring);
    g.strokePath (active, juce::PathStrokeType (3.0f));
    dial.reduce (6, 6);
    g.setColour (juce::Colours::black.withAlpha (0.48f));
    g.fillEllipse (dial.translated (0, 2));
    g.setGradientFill (juce::ColourGradient (knobTop.brighter (0.16f), dial.getTopLeft(),
                                            knobBottom.darker (0.10f), dial.getBottomLeft(), false));
    g.fillEllipse (dial);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (dial.reduced (0.7f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (dial.reduced (2.1f), 0.75f);
    auto face = dial.reduced (5.0f);
    g.setGradientFill (juce::ColourGradient (knobTop.brighter (0.24f), face.getTopLeft(),
                                            knobBottom, face.getBottomRight(), false));
    g.fillEllipse (face);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillEllipse (face.withHeight (face.getHeight() * 0.34f).translated (0, 1));
    for (int groove = 0; groove < 24; ++groove)
    {
        const auto a = juce::MathConstants<float>::twoPi * static_cast<float> (groove) / 24.0f;
        const auto r0 = face.getWidth() * 0.41f;
        const auto r1 = face.getWidth() * 0.45f;
        g.setColour (juce::Colours::white.withAlpha (groove % 3 == 0 ? 0.10f : 0.045f));
        g.drawLine (centre.x + std::sin (a) * r0, centre.y - std::cos (a) * r0,
                    centre.x + std::sin (a) * r1, centre.y - std::cos (a) * r1, 0.7f);
    }
    const auto outer = dial.getWidth() * 0.40f;
    const auto inner = outer * 0.58f;
    g.setColour (accent);
    g.drawLine (centre.x + std::sin (angle) * inner, centre.y - std::cos (angle) * inner,
                centre.x + std::sin (angle) * outer, centre.y - std::cos (angle) * outer, 3);
}

void CharacterCompressorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (theme.background.brighter (0.035f), 0, 0,
        theme.background.darker (0.13f), 0, static_cast<float> (getHeight()), false));
    g.fillAll();
    const auto& grain = analogGrain();
    for (int y = 0; y < getHeight(); y += grain.getHeight())
        for (int x = 0; x < getWidth(); x += grain.getWidth())
            g.drawImageAt (grain, x, y);
    g.setColour (theme.background.getPerceivedBrightness() > 0.7f ? theme.text : theme.control);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("CHARACTER AUDIO  /  DYNAMICS", 24, 18, 532, 20, juce::Justification::centredLeft);
    g.setColour (theme.text);
    g.setFont (juce::FontOptions (29.0f));
    g.drawText ("Character Compressor", 24, 40, 430, 38, juce::Justification::centredLeft);
    drawAnalogPanel (g, { 24, 170, 330, 306 }, 10, theme.card, theme.grid);
    drawAnalogPanel (g, { 24, 490, 330, 164 }, 10, theme.card, theme.grid);
    drawAnalogPanel (g, { 24, 670, 330, 256 }, 10, theme.card, theme.grid);
    g.setColour (theme.text);
    g.setFont (juce::FontOptions (13.0f).withStyle ("bold"));
    g.drawText ("COMPRESSOR", 38, 178, 220, 20, juce::Justification::centredLeft);
    g.drawText ("SIDECHAIN", 38, 498, 280, 18, juce::Justification::centredLeft);
    g.drawText ("TRANSFORMER STAGE", 38, 678, 300, 20, juce::Justification::centredLeft);
    drawAnalogPanel (g, { 376, 584, 180, 128 }, 10, theme.card, theme.grid);
    g.setColour (theme.secondary);
    g.setFont (juce::FontOptions (10.0f).withStyle ("bold"));
    g.drawText ("OUTPUT  /  MIX  /  STEREO LINK", 382, 586, 168, 16, juce::Justification::centred);
    for (const auto y : { 482, 658 })
    {
        g.setColour (theme.grid);
        g.drawHorizontalLine (y, 24, 354);
    }
    paintMeter (g);
}

void CharacterCompressorEditor::paintMeter (juce::Graphics& g)
{
    drawAnalogPanel (g, meterBounds.toFloat(), 10, theme.meterPanel, theme.grid);
    g.setColour (juce::Colour (0xffb9c8c4));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (upward.getToggleState() ? "UPWARD GAIN" : "GAIN REDUCTION",
                388, 188, 156, 20, juce::Justification::centred);
    const juce::Rectangle<float> bar (392, 232, 14, 250), plot (442, 232, 98, 250);
    g.setColour (theme.gaugeTrack);
    g.fillRoundedRectangle (bar, 4);
    for (int db = 0; db <= 30; db += 6)
    {
        const auto y = plot.getY() + plot.getHeight() * static_cast<float> (db) / 30.0f;
        g.setColour (theme.grid);
        g.drawLine (plot.getX(), y, plot.getRight(), y);
        g.setColour (juce::Colour (0xffb9c8c4));
        g.drawText (juce::String (db), 410, juce::roundToInt (y) - 8, 26, 16, juce::Justification::centred);
    }
    for (int second = 0; second <= 5; ++second)
    {
        const auto x = plot.getX() + plot.getWidth() * static_cast<float> (second) / 5.0f;
        g.setColour (theme.grid.withAlpha (0.55f));
        g.drawLine (x, plot.getY(), x, plot.getBottom());
    }
    const auto meterAccent = theme.meter;
    const auto glow = std::clamp (0.22f + displayedReduction / 38.0f, 0.22f, 0.95f);
    const auto fill = bar.withHeight (bar.getHeight() * std::clamp (displayedReduction / 30, 0.0f, 1.0f));
    if (fill.getHeight() > 0)
    {
        for (int layer = 5; layer >= 1; --layer)
        {
            const auto spread = static_cast<float> (layer) * 1.8f;
            g.setColour (meterAccent.withAlpha (glow * (0.10f / static_cast<float> (layer))));
            g.fillRoundedRectangle (fill.expanded (spread, 0), 4 + spread);
        }
        g.setColour (meterAccent.withAlpha (glow * 0.62f));
        g.fillRoundedRectangle (fill, 3);
        g.setColour (juce::Colours::white.withAlpha (glow * 0.52f));
        g.drawVerticalLine (juce::roundToInt (fill.getCentreX()), fill.getY() + 2, fill.getBottom() - 2);
    }
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
    for (int layer = 5; layer >= 1; --layer)
    {
        const auto spread = static_cast<float> (layer) * 2.4f;
        g.setColour (meterAccent.withAlpha (glow * (0.10f / static_cast<float> (layer))));
        g.strokePath (trace, juce::PathStrokeType (2.0f + spread));
    }
    g.setColour (meterAccent.withAlpha (0.98f));
    g.strokePath (trace, juce::PathStrokeType (2.1f));
    g.setColour (meterAccent);
    g.strokePath (trace, juce::PathStrokeType (1.7f));
    g.setColour (juce::Colours::white.withAlpha (glow * 0.78f));
    g.strokePath (trace, juce::PathStrokeType (0.75f));
    g.restoreState();
    g.setColour (juce::Colour (0xffb9c8c4));
    g.drawText ("-5 s", 440, 486, 44, 18, juce::Justification::centredLeft);
    g.drawText ("NOW", 498, 486, 44, 18, juce::Justification::centredRight);
    g.setColour (meterAccent);
    g.setFont (juce::FontOptions (24.0f));
    g.setColour (meterAccent.withAlpha (glow * 0.22f));
    g.drawText (juce::String (displayedReduction, 1) + " dB", 387, 511, 158, 34, juce::Justification::centred);
    g.setColour (meterAccent.withAlpha (glow * 0.30f));
    g.drawText (juce::String (displayedReduction, 1) + " dB", 388, 512, 156, 32, juce::Justification::centred);
    g.setColour (juce::Colour (0xfff2f1eb));
    g.drawText (juce::String (displayedReduction, 1) + " dB", 388, 512, 156, 32, juce::Justification::centred);
}

void CharacterCompressorEditor::resized()
{
    model.setBounds (24, 92, 532, 32);
    modelDescription.setBounds (24, 128, 532, 20);
    bypass.setBounds (464, 42, 92, 36);
    const auto place = [this] (size_t index, int x, int y, int width, int height)
    {
        labels[index].setBounds (x, y, width, 20);
        if (width <= 80)
        {
            labels[index].setFont (juce::FontOptions (9.0f));
            sliders[index].setTextBoxStyle (juce::Slider::TextBoxBelow, false, width, 20);
        }
        else
            sliders[index].setTextBoxStyle (juce::Slider::TextBoxBelow, false, std::min (width, 100), 22);
        sliders[index].setBounds (x, y + 20, width, height);
    };
    place (1, 24, 258, 104, 96);
    place (5, 134, 258, 104, 70);
    place (2, 244, 258, 104, 96);
    place (3, 24, 376, 78, 76);
    place (12, 106, 376, 78, 52);
    place (4, 188, 376, 78, 76);
    place (0, 270, 376, 78, 70);
    place (8, 24, 514, 102, 98);
    place (13, 134, 514, 102, 98);
    place (9, 36, 698, 96, 140);
    place (11, 144, 698, 96, 140);
    place (6, 376, 602, 60, 72);
    place (7, 436, 602, 60, 72);
    place (10, 496, 602, 60, 72);
    labels[6].setText ("OUTPUT", juce::dontSendNotification);
    labels[10].setText ("ST LINK", juce::dontSendNotification);
    labels[9].setText ("IRON DRIVE", juce::dontSendNotification);
    labels[11].setText ("EVEN HARMONICS", juce::dontSendNotification);
    labels[13].setText ("SC HIGH TILT", juce::dontSendNotification);
    meters.setBounds (24, 150, 532, 20);
    upward.setBounds (268, 174, 78, 80);
    midSide.setBounds (250, 510, 92, 132);
    transformer.setBounds (250, 694, 92, 220);
}

void CharacterCompressorEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto elapsed = static_cast<float> (std::max (0.0, now - lastTick));
    lastTick = now;
    transformer.advance (elapsed);
    upward.advance (elapsed);
    midSide.advance (elapsed);
    bypass.advance (elapsed);
    const auto upwardMode = upward.getToggleState();
    const auto modelIndex = std::clamp (model.getSelectedItemIndex(), 0, 3);
    if (upwardMode != lastUpwardMode || modelIndex != lastModelIndex)
    {
        theme = themeFor (modelIndex, upwardMode);
        applyTheme();
        history.clear();
        displayedReduction = 0;
        lastUpwardMode = upwardMode;
        lastModelIndex = modelIndex;
        repaint();
    }
    const std::array<const char*, 4> descriptions {
        "Fast peak detection / feed-forward gain control",
        "Output-sensing peak detector / feedback gain control",
        "RMS feedback / progressive ratio / program-dependent recovery",
        "RMS feed-forward / two-stage optical recovery" };
    modelDescription.setFont (juce::FontOptions (12.0f));
    modelDescription.setColour (juce::Label::textColourId, theme.secondary);
    modelDescription.setText (descriptions[static_cast<size_t> (std::clamp (model.getSelectedItemIndex(), 0, 3))],
                              juce::dontSendNotification);
    const auto raw = processor.reduction.load (std::memory_order_relaxed);
    const auto gr = std::isfinite (raw) ? std::clamp (raw, 0.0f, 30.0f) : 0.0f;
    displayedReduction = std::max (gr, displayedReduction * std::exp (-elapsed / 0.25f));
    history.push (now, gr);
    meters.setFont (juce::FontOptions (11.0f));
    meters.setColour (juce::Label::textColourId, theme.secondary);
    meters.setText ("IN " + juce::String (processor.inputPeak.load(), 1) + " dBFS    /    OUT "
        + juce::String (processor.outputPeak.load(), 1) + " dBFS", juce::dontSendNotification);
    repaint (meterBounds);
}

void CharacterCompressorEditor::applyTheme()
{
    look.setTheme (theme);
    look.setColour (juce::ComboBox::backgroundColourId, theme.card);
    look.setColour (juce::ComboBox::outlineColourId, theme.grid);
    look.setColour (juce::ComboBox::textColourId, theme.text);
    look.setColour (juce::ComboBox::arrowColourId, theme.text);
    look.setColour (juce::PopupMenu::backgroundColourId, theme.card);
    look.setColour (juce::PopupMenu::textColourId, theme.text);
    look.setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.control);
    look.setColour (juce::PopupMenu::highlightedTextColourId, theme.control.contrasting());
    model.setColour (juce::ComboBox::textColourId, theme.text);
    model.setColour (juce::ComboBox::arrowColourId, theme.text);
    for (auto& slider : sliders)
    {
        slider.setColour (juce::Slider::textBoxTextColourId, theme.text);
        slider.repaint();
    }
    for (auto& label : labels) label.setColour (juce::Label::textColourId, theme.text);
    for (auto* control : { &transformer, &upward, &bypass, &midSide })
        control->setTheme (theme);
}
