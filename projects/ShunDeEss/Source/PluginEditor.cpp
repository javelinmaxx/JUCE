#include "PluginEditor.h"
#include "../../EditorScaling.h"
#include "../../EditorTooltips.h"

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

void drawBrushedMetalPanel (juce::Graphics& g, juce::Rectangle<float> bounds, float radius,
                            juce::Colour material, juce::Colour edge)
{
    drawAnalogPanel (g, bounds, radius, material, edge);
    juce::Path clip;
    clip.addRoundedRectangle (bounds.reduced (2), std::max (0.0f, radius - 2));
    g.saveState();
    g.reduceClipRegion (clip);
    const auto lightSurface = material.getPerceivedBrightness() > 0.45f;
    g.setGradientFill (juce::ColourGradient (
        juce::Colours::white.withAlpha (lightSurface ? 0.12f : 0.09f), bounds.getX(), bounds.getY(),
        juce::Colours::transparentBlack, bounds.getRight() * 0.78f, bounds.getBottom() * 0.88f, false));
    g.fillRoundedRectangle (bounds.reduced (1), std::max (0.0f, radius - 1));
    auto state = 0x9e3779b9u;
    for (int streak = 0; streak < 360; ++streak)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const auto y = juce::roundToInt (bounds.getY()) + 2
                     + static_cast<int> (state % static_cast<unsigned int> (std::max (1, juce::roundToInt (bounds.getHeight()) - 4)));
        state = state * 1664525u + 1013904223u;
        const auto x = juce::roundToInt (bounds.getX()) + 3
                     + static_cast<int> (state % static_cast<unsigned int> (std::max (1, juce::roundToInt (bounds.getWidth()) - 8)));
        state = state * 1664525u + 1013904223u;
        const auto length = 8 + static_cast<int> (state % 130u);
        const auto alpha = 0.008f + static_cast<float> ((state >> 16) % 23u) * 0.0012f;
        g.setColour ((lightSurface ? juce::Colours::black : juce::Colours::white).withAlpha (alpha));
        g.drawHorizontalLine (y, static_cast<float> (x), std::min (bounds.getRight() - 3, static_cast<float> (x + length)));
    }
    g.restoreState();
}

CompressorTheme ebonyTheme()
{
    return { juce::Colour (0xff100e12), juce::Colour (0xff1c1920), juce::Colour (0xff09080c),
        juce::Colour (0xffeee9e2), juce::Colour (0xffaaa0a0), juce::Colour (0xffb77b45),
        juce::Colour (0xffe3a45b), juce::Colour (0xff211b20), juce::Colour (0xff33262a),
        juce::Colour (0xff30262c), juce::Colour (0xff65504a), juce::Colour (0xff30252a) };
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
    if (labeledButton)
    {
        if (position > 0.001f)
            for (int layer = 5; layer >= 1; --layer)
            {
                const auto spread = static_cast<float> (layer) * 1.8f;
                g.setColour (accent.withAlpha (position * (0.095f / static_cast<float> (layer))));
                g.fillRoundedRectangle (bounds.expanded (spread), 6 + spread);
            }
        auto face = surface.interpolatedWith (accent.darker (0.48f), position * 0.70f);
        if (highlighted) face = face.brighter (0.06f);
        if (down) face = face.darker (0.08f);
        g.setColour (face);
        g.fillRoundedRectangle (bounds, 6);
        g.setColour (position > 0.05f ? accent : secondary.withAlpha (0.70f));
        g.drawRoundedRectangle (bounds, 6, 1.0f);
        g.setColour (getToggleState() ? foreground : secondary);
        g.setFont (juce::FontOptions (getButtonText() == "BYPASS" ? 10.5f : 13.0f).withStyle ("bold"));
        const auto caption = getButtonText() == "BYPASS"
            ? getButtonText() : (getToggleState() ? onCaption : offCaption);
        g.drawText (caption, bounds.toNearestInt(), juce::Justification::centred);
        if (hasKeyboardFocus (true))
        {
            g.setColour (accent);
            g.drawRoundedRectangle (bounds.expanded (2), 7, 1.2f);
        }
        return;
    }
    if (compact)
    {
        g.setColour (getToggleState() ? (highlighted ? accent.brighter (0.12f) : accent) : secondary);
        g.setFont (juce::FontOptions (10.0f).withStyle ("bold"));
        g.drawText ("YIN", bounds.withWidth (bounds.getWidth() - 20), juce::Justification::centredRight);
        const auto led = juce::Rectangle<float> (bounds.getRight() - 13, bounds.getCentreY() - 5, 10, 10);
        if (getToggleState())
        {
            g.setColour (accent.withAlpha (0.20f));
            g.fillEllipse (led.expanded (4));
        }
        g.setColour (surface.darker (0.25f));
        g.fillEllipse (led.expanded (1.0f));
        g.setColour (getToggleState() ? accent : secondary.withAlpha (0.72f));
        g.fillEllipse (led);
        if (hasKeyboardFocus (true))
        {
            g.setColour (accent);
            g.drawEllipse (led.expanded (4), 1.0f);
        }
        return;
    }
    if (horizontal)
    {
        g.setColour (getToggleState() ? foreground : (highlighted ? foreground.withAlpha (0.90f) : secondary));
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
            g.drawEllipse (thumb.expanded (2), 1.0f);
        }
        return;
    }

    const auto titleHeight = showFunctionLabel ? std::min (26, getHeight() / 4) : 0;
    const auto title = getLocalBounds().reduced (2, 0).withHeight (titleHeight);
    if (showFunctionLabel)
    {
        g.setColour (getToggleState() ? foreground : (highlighted ? foreground.withAlpha (0.90f) : secondary));
        g.setFont (juce::FontOptions (9.0f));
        g.drawFittedText (getButtonText(), title, juce::Justification::centred, 2);
    }

    // Side legend and hardware follow a standard bat-handle panel toggle.
    const auto cx = static_cast<float> (getWidth()) * 0.5f;
    const auto onY = title.getBottom() + 1;
    const auto pivotY = static_cast<float> (onY) + 27.0f;
    g.setFont (juce::FontOptions (8.0f).withStyle ("bold"));
    g.setColour (getToggleState() ? accent : secondary.withAlpha (0.75f));
    g.drawText (onCaption, 0, onY, getWidth(), 10, juce::Justification::centred);
    g.setColour (getToggleState() ? secondary.withAlpha (0.75f) : accent);
    g.drawText (offCaption, 0, juce::roundToInt (pivotY) + 20, getWidth(), 10,
                juce::Justification::centred);

    const auto pivot = juce::Point<float> (cx, pivotY);
    juce::Path nut, innerNut;
    for (int corner = 0; corner < 6; ++corner)
    {
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float> (corner) / 6.0f;
        const juce::Point<float> point (cx + std::cos (angle) * 16.0f,
                                        pivot.y + std::sin (angle) * 16.0f);
        const juce::Point<float> inset (cx + std::cos (angle) * 13.2f,
                                        pivot.y + std::sin (angle) * 13.2f);
        if (corner == 0)
        {
            nut.startNewSubPath (point);
            innerNut.startNewSubPath (inset);
        }
        else
        {
            nut.lineTo (point);
            innerNut.lineTo (inset);
        }
    }
    nut.closeSubPath();
    innerNut.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillPath (nut, juce::AffineTransform::translation (0.0f, 1.5f));
    g.setGradientFill (juce::ColourGradient (secondary.brighter (0.16f), cx - 12.0f, pivot.y - 13.0f,
                                             secondary.darker (0.34f), cx + 10.0f, pivot.y + 13.0f, false));
    g.fillPath (nut);
    g.setColour (secondary.darker (0.15f));
    g.strokePath (nut, juce::PathStrokeType (0.8f));
    g.setColour (secondary.darker (0.20f));
    g.fillPath (innerNut);
    g.setColour (secondary.darker (0.34f).withAlpha (0.8f));
    g.strokePath (innerNut, juce::PathStrokeType (0.65f));

    const auto flange = juce::Rectangle<float> (cx - 12.0f, pivot.y - 12.0f, 24.0f, 24.0f);
    g.setColour (surface.darker (0.42f));
    g.fillEllipse (flange.expanded (1.0f));
    g.setGradientFill (juce::ColourGradient (secondary.brighter (0.18f), flange.getTopLeft(),
                                             secondary.darker (0.28f), flange.getBottomLeft(), false));
    g.fillEllipse (flange);
    g.setColour (surface.darker (0.35f));
    g.fillEllipse (flange.reduced (3.0f));
    g.setColour (secondary.brighter (0.12f).withAlpha (0.85f));
    g.drawEllipse (flange, 0.7f);

    g.setColour (surface.darker (0.50f));
    g.fillEllipse (pivot.x - 6.0f, pivot.y - 6.0f, 12.0f, 12.0f);
    g.setColour (secondary.darker (0.18f));
    g.drawEllipse (pivot.x - 5.3f, pivot.y - 5.3f, 10.6f, 10.6f, 1.0f);
    const auto leverLength = 17.0f;
    const auto tipY = pivot.y + (0.5f - position) * leverLength;
    const juce::Point<float> tip (cx, tipY);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine (pivot.x + 1.0f, pivot.y + 1.0f, tip.x + 1.0f, tip.y + 1.0f, 5.6f);
    g.setColour (juce::Colour (0xffc6cbd0));
    g.drawLine (pivot.x, pivot.y, tip.x, tip.y, 4.8f);
    g.setColour (juce::Colour (0xff8e969d));
    g.drawLine (pivot.x - 0.8f, pivot.y, tip.x - 0.8f, tip.y, 1.0f);
    const auto cap = juce::Rectangle<float> (cx - 4.4f, tip.y - 4.4f, 8.8f, 8.8f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0e3e5), cap.getTopLeft(),
                                             juce::Colour (0xff777f86), cap.getBottomRight(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colour (0xff515a61));
    g.drawEllipse (cap, 0.8f);
    if (hasKeyboardFocus (true))
    {
        g.setColour (accent);
        g.drawEllipse (flange.expanded (3.0f, 4.0f), 1.0f);
    }
}

DetectorModeSwitch::DetectorModeSwitch()
{
    setSliderStyle (juce::Slider::LinearVertical);
    setRange (0.0, 2.0, 1.0);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setName ("DETECTOR MODE");
    setTitle ("Detector mode");
    textFromValueFunction = [] (double value)
    {
        constexpr std::array<const char*, 3> captions { "ABSOLUTE", "LISTEN", "RELATIVE" };
        return juce::String (captions[static_cast<size_t> (std::clamp (juce::roundToInt (value), 0, 2))]);
    };
    valueFromTextFunction = [] (const juce::String& text)
    {
        if (text.equalsIgnoreCase ("LISTEN")) return 1.0;
        if (text.equalsIgnoreCase ("RELATIVE")) return 2.0;
        return 0.0;
    };
}

void DetectorModeSwitch::selectAt (float y)
{
    // These boundaries sit halfway between the three painted lever tips.
    setValue (y < 32.0f ? 2.0 : y < 44.0f ? 1.0 : 0.0, juce::sendNotificationSync);
}

void DetectorModeSwitch::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isLeftButtonDown())
    {
        if (event.getNumberOfClicks() > 1) setValue (0.0, juce::sendNotificationSync);
        else selectAt (event.position.y);
    }
}

void DetectorModeSwitch::mouseDrag (const juce::MouseEvent& event)
{
    if (event.mods.isLeftButtonDown()) selectAt (event.position.y);
}

void DetectorModeSwitch::paint (juce::Graphics& g)
{
    const auto position = std::clamp (juce::roundToInt (getValue()), 0, 2);
    g.setColour (theme.secondary);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("DETECTOR", 0, 0, getWidth(), 16, juce::Justification::centred);
    const auto cx = static_cast<float> (getWidth()) * 0.5f;
    constexpr float pivotY = 38.0f;
    juce::Path nut, innerNut;
    for (int corner = 0; corner < 6; ++corner)
    {
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float> (corner) / 6.0f;
        const juce::Point<float> outer (cx + std::cos (angle) * 15.0f,
                                        pivotY + std::sin (angle) * 15.0f);
        const juce::Point<float> inner (cx + std::cos (angle) * 12.5f,
                                        pivotY + std::sin (angle) * 12.5f);
        if (corner == 0)
        {
            nut.startNewSubPath (outer);
            innerNut.startNewSubPath (inner);
        }
        else
        {
            nut.lineTo (outer);
            innerNut.lineTo (inner);
        }
    }
    nut.closeSubPath();
    innerNut.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillPath (nut, juce::AffineTransform::translation (0.0f, 1.5f));
    g.setGradientFill (juce::ColourGradient (theme.secondary.brighter (0.16f), cx - 12.0f, pivotY - 13.0f,
                                             theme.secondary.darker (0.34f), cx + 10.0f, pivotY + 13.0f, false));
    g.fillPath (nut);
    g.setColour (theme.secondary.darker (0.2f));
    g.strokePath (nut, juce::PathStrokeType (0.8f));
    g.fillPath (innerNut);
    const auto flange = juce::Rectangle<float> (cx - 11.0f, pivotY - 11.0f, 22.0f, 22.0f);
    g.setColour (theme.switchSurface.darker (0.42f));
    g.fillEllipse (flange.expanded (1.0f));
    g.setGradientFill (juce::ColourGradient (theme.secondary.brighter (0.18f), flange.getTopLeft(),
                                             theme.secondary.darker (0.28f), flange.getBottomLeft(), false));
    g.fillEllipse (flange);
    g.setColour (theme.switchSurface.darker (0.35f));
    g.fillEllipse (flange.reduced (3.0f));
    g.setColour (theme.secondary.brighter (0.12f).withAlpha (0.85f));
    g.drawEllipse (flange, 0.7f);
    const auto tipY = pivotY + (1.0f - static_cast<float> (position)) * 13.0f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine (cx + 1.0f, pivotY + 1.0f, cx + 1.0f, tipY + 1.0f, 5.6f);
    g.setColour (juce::Colour (0xffc6cbd0));
    g.drawLine (cx, pivotY, cx, tipY, 4.8f);
    const auto cap = juce::Rectangle<float> (cx - 4.4f, tipY - 4.4f, 8.8f, 8.8f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0e3e5), cap.getTopLeft(),
                                             juce::Colour (0xff777f86), cap.getBottomRight(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colour (0xff515a61));
    g.drawEllipse (cap, 0.8f);
    for (int index = 0; index < 3; ++index)
    {
        g.setColour (index == position ? theme.control : theme.secondary.withAlpha (0.55f));
        g.fillEllipse (cx + 22.0f, pivotY + (1.0f - static_cast<float> (index)) * 11.0f - 1.5f,
                       3.0f, 3.0f);
    }
    g.setColour (theme.control);
    g.setFont (juce::FontOptions (9.5f).withStyle ("bold"));
    g.drawText (getTextFromValue (position), 0, 60, getWidth(), 14, juce::Justification::centred);
    if (hasKeyboardFocus (true))
        g.drawEllipse (flange.expanded (3.0f, 4.0f), 1.0f);
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
    const auto ticks = slider.getName() == "MODE" ? 3 : 10;
    for (int tick = 0; tick <= ticks; ++tick)
    {
        const auto tickAngle = start + (end - start) * static_cast<float> (tick) / static_cast<float> (ticks);
        const auto major = ticks == 3 || tick % 5 == 0;
        const auto outer = outerRadius;
        const auto inner = outer - (major ? 5.0f : 2.5f);
        g.setColour (ring.withAlpha (major ? 0.62f : 0.34f));
        g.drawLine (centre.x + std::sin (tickAngle) * inner, centre.y - std::cos (tickAngle) * inner,
                    centre.x + std::sin (tickAngle) * outer, centre.y - std::cos (tickAngle) * outer,
                    major ? 1.25f : 0.8f);
    }
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0, start, end, true);
    g.setColour (ring.withAlpha (0.78f));
    g.strokePath (arc, juce::PathStrokeType (2.0f));
    juce::Path active;
    const auto angle = start + value * (end - start);
    active.addCentredArc (centre.x, centre.y, radius, radius, 0, start, angle, true);
    g.setColour (slider.isEnabled() ? accent : ring);
    g.strokePath (active, juce::PathStrokeType (2.2f));
    dial.reduce (5, 5);
    auto rim = dial.expanded (2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillEllipse (rim.translated (0.0f, 1.2f));
    g.setColour (ring.darker (0.25f));
    g.fillEllipse (rim);
    g.setColour (ring.withAlpha (0.70f));
    g.drawEllipse (rim, 0.8f);
    g.setGradientFill (juce::ColourGradient (knobTop.withAlpha (0.66f), dial.getTopLeft(),
                                            knobBottom.brighter (0.12f), dial.getBottomLeft(), false));
    g.fillEllipse (dial);
    g.setColour (ring.withAlpha (0.45f));
    g.drawEllipse (dial.reduced (2.0f), 0.7f);
    const auto outer = dial.getWidth() * 0.40f;
    const auto inner = outer * 0.58f;
    g.setColour (accent);
    g.drawLine (centre.x + std::sin (angle) * inner, centre.y - std::cos (angle) * inner,
                centre.x + std::sin (angle) * outer, centre.y - std::cos (angle) * outer, 1.8f);

}

juce::Label* CompressorLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* valueDisplay = juce::LookAndFeel_V4::createSliderTextBox (slider);
    const auto font = valueDisplay->getFont();
    valueDisplay->setFont (font.withHeight (font.getHeight() * 0.85f));
    return valueDisplay;
}

juce::Slider::SliderLayout CompressorLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);
    if (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
        layout.textBoxBounds.translate (0, -3);
    return layout;
}

ShunDeEssEditor::ShunDeEssEditor (ShunDeEssProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      theme (ebonyTheme())
{
    setOpaque (true);
    setLookAndFeel (&look);
    addAndMakeVisible (algorithm);
    addAndMakeVisible (algorithmLabel);
    addAndMakeVisible (bypass);
    addAndMakeVisible (detectorMode);
    lastSpectralMode = p.parameters.getRawParameterValue ("smart")->load() >= 0.5f;
    lastSplitMode = p.parameters.getRawParameterValue ("mode")->load() >= 0.5f;
    lastDetectorMode = p.parameters.getRawParameterValue ("relative")->load() >= 0.5f ? 2
                     : p.parameters.getRawParameterValue ("listen")->load() >= 0.5f ? 1 : 0;
    detectorMode.setValue (lastDetectorMode, juce::dontSendNotification);
    detectorMode.setTooltip ("Selects Absolute level detection, Listen monitoring, or Relative band-to-full-signal detection.");
    detectorMode.onValueChange = [this]
    {
        const auto selected = juce::roundToInt (detectorMode.getValue());
        auto* listenParameter = processor.parameters.getParameter ("listen");
        auto* relativeParameter = processor.parameters.getParameter ("relative");
        listenParameter->beginChangeGesture();
        relativeParameter->beginChangeGesture();
        listenParameter->setValueNotifyingHost (selected == 1 ? 1.0f : 0.0f);
        relativeParameter->setValueNotifyingHost (selected == 2 ? 1.0f : 0.0f);
        relativeParameter->endChangeGesture();
        listenParameter->endChangeGesture();
        lastDetectorMode = selected;
    };
    algorithm.setLookAndFeel (&look);
    algorithm.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    algorithm.setRange (0.0, 3.0, 1.0);
    algorithm.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 100, 18);
    algorithm.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    algorithm.setName ("MODE");
    algorithm.setTitle ("De-esser mode");
    algorithm.textFromValueFunction = [] (double value)
    {
        constexpr std::array<const char*, 4> names { "WIDEBAND", "SPLIT", "BRILLIANT", "SMOOTH" };
        return juce::String (names[static_cast<size_t> (std::clamp (juce::roundToInt (value), 0, 3))]);
    };
    algorithm.valueFromTextFunction = [] (const juce::String& text)
    {
        if (text.equalsIgnoreCase ("SPLIT")) return 1.0;
        if (text.equalsIgnoreCase ("BRILLIANT")) return 2.0;
        if (text.equalsIgnoreCase ("SMOOTH")) return 3.0;
        return 0.0;
    };
    algorithm.setValue (lastSpectralMode ? (lastSplitMode ? 3 : 2) : (lastSplitMode ? 1 : 0),
                        juce::dontSendNotification);
    // Position 0 is already the slider's default, so setValue does not refresh its text box.
    algorithm.updateText();
    algorithm.onValueChange = [this]
    {
        const auto selected = juce::roundToInt (algorithm.getValue());
        auto* split = processor.parameters.getParameter ("mode");
        auto* spectral = processor.parameters.getParameter ("smart");
        split->beginChangeGesture();
        spectral->beginChangeGesture();
        split->setValueNotifyingHost (selected == 1 || selected == 3 ? 1.0f : 0.0f);
        spectral->setValueNotifyingHost (selected >= 2 ? 1.0f : 0.0f);
        spectral->endChangeGesture();
        split->endChangeGesture();
        lastSplitMode = selected == 1 || selected == 3;
        lastSpectralMode = selected >= 2;
        repaint (24, 340, 300, 125);
    };
    algorithmLabel.setText ("MODE", juce::dontSendNotification);
    algorithmLabel.setJustificationType (juce::Justification::centred);
    algorithmLabel.setFont (juce::FontOptions (10.0f));
    editorTooltips::setHint (algorithm, algorithmLabel,
        "Selects Wideband, Split, Brilliant, or Smooth de-essing.");
    bypass.setTooltip ("Bypasses de-essing for a direct comparison.");
    bypassAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "bypass", bypass);

    const std::array<const char*, 10> ids { "frequency", "q", "attenuationFrequency", "attenuationQ",
        "threshold", "depth", "attack", "release", "output", "stereoLink" };
    const std::array<const char*, 10> names { "DET FREQ", "DET Q", "ATT FREQ", "ATT Q", "THRESHOLD",
        "RANGE", "ATTACK", "RELEASE", "OUTPUT", "ST LINK" };
    const std::array<const char*, 10> units { " Hz", " Q", " Hz", " Q", " dB", " dB", " ms", " ms", " dB", " %" };
    const std::array<const char*, 10> hints {
        "Sets the frequency emphasized by the sibilance detector.",
        "Adjusts detector focus or spectral averaging, depending on the mode.",
        "Sets the center of the frequency region being attenuated.",
        "Adjusts how narrowly attenuation focuses around the selected frequency.",
        "Sets the detector level in Absolute or the band-to-full-signal difference in Relative.",
        "Limits the maximum amount of sibilance attenuation.",
        "Sets how quickly attenuation engages after sibilance is detected.",
        "Sets how quickly attenuation fades after sibilance ends.",
        "Adjusts the final output level.",
        "Links left and right detection to keep stereo de-essing balanced."
    };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& slider = sliders[i];
        slider.setLookAndFeel (&look);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, theme.text);
        slider.setTextValueSuffix (units[i]);
        slider.setName (names[i]);
        slider.setTitle (names[i]);
        labels[i].setText (names[i], juce::dontSendNotification);
        labels[i].setJustificationType (juce::Justification::centred);
        labels[i].setFont (juce::FontOptions (10.0f));
        addAndMakeVisible (slider);
        addAndMakeVisible (labels[i]);
        attachments[i] = std::make_unique<State::SliderAttachment> (p.parameters, ids[i], slider);
        editorTooltips::setHint (slider, labels[i], hints[i]);
        slider.onValueChange = [this] { repaint (24, 340, 300, 125); };
        slider.setColour (juce::Slider::textBoxTextColourId, theme.text);
    }
    bypass.snapToState();
    applyTheme();
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    editorScaling::enable (*this, designWidth, designHeight);
    startTimerHz (30);
}

void ShunDeEssEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    g.saveState();
    g.addTransform (editorScaling::transform (*this, designWidth, designHeight));
    g.setGradientFill (juce::ColourGradient (theme.background.brighter (0.035f), 0, 0,
        theme.background.darker (0.13f), 0, static_cast<float> (designHeight), false));
    g.fillRect (0, 0, designWidth, designHeight);
    g.setColour (theme.secondary);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("J-SONIC / DYNAMICS", 24, 20, 240, 18, juce::Justification::centredLeft);
    g.setColour (theme.text);
    g.setFont (juce::FontOptions (27.0f));
    g.drawText ("SHUN DE-ESS", 24, 38, 410, 38, juce::Justification::centredLeft);
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (0.0f, -50.0f));
    drawBrushedMetalPanel (g, { 24, 140, 300, 252 }, 10, theme.card, theme.grid);
    drawAnalogPanel (g, { 24, 514, 300, 116 }, 10, theme.card, theme.grid);
    drawAnalogPanel (g, { 24, 642, 300, 116 }, 10, theme.card, theme.grid);
    drawAnalogPanel (g, { 344, 642, 180, 116 }, 10, theme.card, theme.grid);
    g.setColour (theme.text);
    g.setFont (juce::FontOptions (13.0f).withStyle ("bold"));
    g.drawText ("DE-ESSER", 38, 148, 250, 20, juce::Justification::centredLeft);
    g.drawText ("DETECTOR", 38, 522, 250, 18, juce::Justification::centredLeft);
    g.drawText ("ATTENUATOR", 38, 650, 250, 18, juce::Justification::centredLeft);
    g.drawText ("OUTPUT", 358, 650, 152, 18, juce::Justification::centredLeft);
    paintResponseDisplay (g);
    paintMeter (g);
    g.restoreState();
    g.restoreState();
}

void ShunDeEssEditor::paintResponseDisplay (juce::Graphics& g)
{
    const juce::Rectangle<float> plot (42.0f, 407.0f, 266.0f, 57.0f);
    const auto detectorFrequency = static_cast<float> (sliders[0].getValue());
    const auto detectorQ = static_cast<float> (sliders[1].getValue());
    const auto attenuationFrequency = static_cast<float> (sliders[2].getValue());
    const auto attenuationQ = static_cast<float> (sliders[3].getValue());
    const auto maximumCut = static_cast<float> (sliders[5].getValue());
    const auto splitBand = juce::roundToInt (algorithm.getValue()) == 1;
    const auto xForFrequency = [&] (float frequency)
    {
        const auto octaves = std::log2 (std::clamp (frequency, 2000.0f, 16000.0f) / 2000.0f);
        return plot.getX() + plot.getWidth() * octaves / 3.0f;
    };
    const auto yForDb = [&] (float db)
    {
        return plot.getY() + plot.getHeight() * std::clamp (-db / 18.0f, 0.0f, 1.0f);
    };

    g.setColour (theme.meterPanel.withAlpha (0.62f));
    g.fillRoundedRectangle (plot.expanded (7.0f, 4.0f), 5.0f);
    for (const auto db : { 0, -9, -18 })
    {
        const auto y = yForDb (static_cast<float> (db));
        g.setColour (theme.grid.withAlpha (0.72f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (theme.secondary.withAlpha (0.82f));
        g.setFont (juce::FontOptions (7.0f));
        g.drawText (juce::String (db), 20, juce::roundToInt (y) - 5, 19, 10,
                    juce::Justification::centredRight);
    }
    for (const auto frequency : { 2000.0f, 4000.0f, 8000.0f, 16000.0f })
    {
        const auto x = xForFrequency (frequency);
        g.setColour (theme.grid.withAlpha (0.62f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (theme.secondary);
        const auto label = juce::String (static_cast<int> (frequency / 1000.0f)) + "k";
        g.drawText (label, x - 14.0f, plot.getBottom() + 2.0f, 28.0f, 10.0f,
                    juce::Justification::centred);
    }

    juce::Path detectorCurve, attenuationCurve;
    constexpr int points = 160;
    for (int index = 0; index <= points; ++index)
    {
        const auto proportion = static_cast<float> (index) / static_cast<float> (points);
        const auto frequency = 2000.0f * std::pow (8.0f, proportion);
        const auto detectorRatio = frequency / detectorFrequency;
        const auto detectorDenominator = std::sqrt (
            std::pow (1.0f - detectorRatio * detectorRatio, 2.0f)
            + std::pow (detectorRatio / detectorQ, 2.0f));
        const auto detectorDb = 20.0f * std::log10 (std::max (
            (detectorRatio / detectorQ) / detectorDenominator, 1.0e-5f));

        const auto relative = frequency / attenuationFrequency;
        const auto cut = splitBand
            ? -maximumCut / (1.0f + std::pow (attenuationQ * (relative - 1.0f / relative), 2.0f))
            : -maximumCut / (1.0f + std::exp (-std::clamp (1.5f * attenuationQ
                                                           * std::log (relative), -20.0f, 20.0f)));
        const auto x = plot.getX() + plot.getWidth() * proportion;
        const auto detectorY = yForDb (detectorDb);
        const auto attenuationY = yForDb (cut);
        if (index == 0)
        {
            detectorCurve.startNewSubPath (x, detectorY);
            attenuationCurve.startNewSubPath (x, attenuationY);
        }
        else
        {
            detectorCurve.lineTo (x, detectorY);
            attenuationCurve.lineTo (x, attenuationY);
        }
    }
    g.saveState();
    g.reduceClipRegion (plot.expanded (1.0f).toNearestInt());
    g.setColour (theme.secondary.withAlpha (0.22f));
    g.strokePath (detectorCurve, juce::PathStrokeType (4.0f));
    g.setColour (theme.secondary);
    g.strokePath (detectorCurve, juce::PathStrokeType (1.5f));
    g.setColour (theme.meter.withAlpha (0.22f));
    g.strokePath (attenuationCurve, juce::PathStrokeType (4.0f));
    g.setColour (theme.meter);
    g.strokePath (attenuationCurve, juce::PathStrokeType (1.8f));
    g.restoreState();

    g.setFont (juce::FontOptions (7.5f).withStyle ("bold"));
    g.setColour (theme.secondary);
    g.drawText ("DETECTOR", 38, 480, 72, 12, juce::Justification::centredLeft);
    g.setColour (theme.meter);
    const auto mode = juce::roundToInt (algorithm.getValue());
    const auto modeCaption = mode == 3 ? "ATTENUATOR / SMOOTH"
                            : mode == 2 ? "ATTENUATOR / BRILLIANT"
                            : splitBand ? "ATTENUATOR / SPLIT" : "ATTENUATOR / WIDE";
    g.drawText (modeCaption,
                208, 480, 108, 12, juce::Justification::centredRight);
}

void ShunDeEssEditor::paintMeter (juce::Graphics& g)
{
    drawAnalogPanel (g, meterBounds.toFloat(), 10, theme.meterPanel, theme.grid);
    g.setColour (theme.secondary);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("GAIN REDUCTION", 356, 158, 156, 20, juce::Justification::centred);
    const juce::Rectangle<float> bar (360, 202, 14, 330), plot (410, 202, 98, 330);
    g.setColour (theme.gaugeTrack);
    g.fillRoundedRectangle (bar, 4);
    for (int db = 0; db <= 18; db += 6)
    {
        const auto y = plot.getY() + plot.getHeight() * static_cast<float> (db) / 18.0f;
        g.setColour (theme.grid);
        g.drawLine (plot.getX(), y, plot.getRight(), y);
        g.setColour (theme.secondary);
        g.drawText (juce::String (db), 378, juce::roundToInt (y) - 8, 26, 16, juce::Justification::centred);
    }
    for (int second = 0; second <= static_cast<int> (GainReductionHistory::duration); ++second)
    {
        const auto x = plot.getX() + plot.getWidth() * static_cast<float> (second)
                     / static_cast<float> (GainReductionHistory::duration);
        g.setColour (theme.grid.withAlpha (0.55f));
        g.drawLine (x, plot.getY(), x, plot.getBottom());
    }
    const auto amount = std::clamp (displayedReduction / 18.0f, 0.0f, 1.0f);
    const auto fill = bar.withHeight (bar.getHeight() * amount);
    const auto glow = std::clamp (0.2f + amount * 0.7f, 0.2f, 0.9f);
    if (fill.getHeight() > 0)
    {
        for (int layer = 5; layer >= 1; --layer)
        {
            const auto spread = static_cast<float> (layer) * 1.8f;
            g.setColour (theme.meter.withAlpha (glow * 0.1f / static_cast<float> (layer)));
            g.fillRoundedRectangle (fill.expanded (spread, 0), 4 + spread);
        }
        g.setColour (theme.meter.withAlpha (glow * 0.65f));
        g.fillRoundedRectangle (fill, 3);
        g.setColour (juce::Colours::white.withAlpha (glow * 0.5f));
        g.drawVerticalLine (juce::roundToInt (fill.getCentreX()), fill.getY() + 2, fill.getBottom() - 2);
    }
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    juce::Path trace;
    bool started = false;
    double previous = 0;
    history.visit (now, [&] (float position, float reduction, double time)
    {
        const auto x = plot.getX() + plot.getWidth() * position;
        const auto y = plot.getY() + plot.getHeight() * reduction / 18.0f;
        if (! started || time - previous > 0.25) trace.startNewSubPath (x, y);
        else trace.lineTo (x, y);
        previous = time;
        started = true;
    });
    g.saveState();
    g.reduceClipRegion (plot.expanded (1).toNearestInt());
    g.setColour (theme.meter.withAlpha (0.24f));
    g.strokePath (trace, juce::PathStrokeType (8.0f));
    g.setColour (theme.meter);
    g.strokePath (trace, juce::PathStrokeType (1.8f));
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.strokePath (trace, juce::PathStrokeType (0.7f));
    g.restoreState();
    g.setColour (theme.secondary);
    g.drawText ("-3 s", 408, 536, 44, 18, juce::Justification::centredLeft);
    g.drawText ("NOW", 466, 536, 44, 18, juce::Justification::centredRight);
    g.setFont (juce::FontOptions (24.0f));
    g.setColour (theme.meter.withAlpha (glow * 0.25f));
    g.drawText (juce::String (displayedReduction, 1) + " dB", 355, 561, 158, 34, juce::Justification::centred);
    g.setColour (theme.text);
    g.drawText (juce::String (displayedReduction, 1) + " dB", 356, 562, 156, 32, juce::Justification::centred);
}

void ShunDeEssEditor::resized()
{
    const auto up = [] (int y) { return y - 80; };
    bypass.setBounds (450, 33, 74, 28);
    const auto place = [this, &up] (size_t index, int x, int y, int width, int height)
    {
        labels[index].setBounds (x, up (y + 3), width, 17);
        sliders[index].setTextBoxStyle (juce::Slider::TextBoxBelow, false, width, 17);
        sliders[index].setBounds (x, up (y + 20), width, height);
    };
    place (4, 38, 198, 128, 108);
    place (5, 168, 198, 156, 108);
    place (6, 64, 325, 76, 64);
    place (7, 208, 325, 76, 64);
    place (0, 30, 570, 76, 64);
    place (1, 122, 570, 76, 64);
    place (2, 30, 702, 76, 64);
    place (3, 122, 702, 76, 64);
    place (8, 358, 702, 76, 64);
    place (9, 434, 702, 76, 64);
    detectorMode.setBounds (207, up (570) + 6, 112, 76);
    algorithmLabel.setBounds (205, up (702) + 3, 112, 16);
    algorithm.setBounds (205, up (702) + 19, 112, 65);

    const auto scale = editorScaling::transform (*this, designWidth, designHeight);
    bypass.setTransform (scale);
    detectorMode.setTransform (scale);
    algorithm.setTransform (scale);
    algorithmLabel.setTransform (scale);
    for (auto& slider : sliders) slider.setTransform (scale);
    for (auto& label : labels) label.setTransform (scale);
}

void ShunDeEssEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto elapsed = static_cast<float> (std::max (0.0, now - lastTick));
    lastTick = now;
    bypass.advance (elapsed);
    const auto spectral = processor.parameters.getRawParameterValue ("smart")->load() >= 0.5f;
    const auto split = processor.parameters.getRawParameterValue ("mode")->load() >= 0.5f;
    const auto detector = processor.parameters.getRawParameterValue ("relative")->load() >= 0.5f ? 2
                        : processor.parameters.getRawParameterValue ("listen")->load() >= 0.5f ? 1 : 0;
    if (detector != lastDetectorMode)
    {
        detectorMode.setValue (detector, juce::dontSendNotification);
        lastDetectorMode = detector;
    }
    if (spectral != lastSpectralMode || split != lastSplitMode)
    {
        algorithm.setValue (spectral ? (split ? 3 : 2) : (split ? 1 : 0), juce::dontSendNotification);
        lastSpectralMode = spectral;
        lastSplitMode = split;
        repaint (editorScaling::repaintArea (*this, designWidth, designHeight,
                                            { 24, 340, 300, 125 }));
    }
    const auto rawValue = processor.reduction.load (std::memory_order_relaxed);
    const auto reduction = std::isfinite (rawValue) ? std::clamp (rawValue, 0.0f, 18.0f) : 0.0f;
    displayedReduction = std::max (reduction, displayedReduction * std::exp (-elapsed / 0.25f));
    history.push (now, reduction);
    repaint (editorScaling::repaintArea (*this, designWidth, designHeight,
                                        meterBounds.translated (0, -50)));
}

void ShunDeEssEditor::applyTheme()
{
    look.setTheme (theme);
    look.setColour (juce::TooltipWindow::backgroundColourId, theme.card);
    look.setColour (juce::TooltipWindow::textColourId, theme.text);
    look.setColour (juce::TooltipWindow::outlineColourId, theme.grid);
    look.setColour (juce::PopupMenu::backgroundColourId, theme.card);
    look.setColour (juce::PopupMenu::textColourId, theme.text);
    look.setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.control);
    for (auto& slider : sliders)
    {
        slider.setColour (juce::Slider::textBoxTextColourId, theme.text);
        slider.repaint();
    }
    for (auto& label : labels)
        label.setColour (juce::Label::textColourId, theme.text);
    bypass.setTheme (theme);
    detectorMode.setTheme (theme);
    algorithm.setColour (juce::Slider::textBoxTextColourId, theme.text);
    algorithmLabel.setColour (juce::Label::textColourId, theme.text);
    algorithm.repaint();
}
