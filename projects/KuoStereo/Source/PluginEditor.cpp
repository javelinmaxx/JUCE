#include "PluginEditor.h"

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

void KuoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
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

juce::Label* KuoLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (10.0f));
    return label;
}

void KuoBypassButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
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

KuoStereoEditor::KuoStereoEditor (KuoStereoProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    setLookAndFeel (&look);
    addAndMakeVisible (bypass);
    bypassAttachment = std::make_unique<State::ButtonAttachment> (p.parameters, "bypass", bypass);
    constexpr std::array<const char*, 5> ids { "haasMix", "haasMax", "width", "transientProtect", "output" };
    constexpr std::array<const char*, 5> names { "HAAS MIX", "MAX DELAY", "VELVET WIDTH", "TRANSIENT GUARD", "OUTPUT" };
    constexpr std::array<const char*, 5> units { " %", " ms", " %", " %", " dB" };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& slider = sliders[i];
        slider.setLookAndFeel (&look);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 124, 17);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, foreground);
        slider.setTextValueSuffix (units[i]);
        slider.setName (names[i]);
        slider.setTitle (names[i]);
        labels[i].setText (names[i], juce::dontSendNotification);
        labels[i].setJustificationType (juce::Justification::centred);
        labels[i].setFont (juce::FontOptions (9.0f));
        labels[i].setColour (juce::Label::textColourId, secondary);
        addAndMakeVisible (slider);
        addAndMakeVisible (labels[i]);
        attachments[i] = std::make_unique<State::SliderAttachment> (p.parameters, ids[i], slider);
    }
    setSize (548, 741);
    startTimerHz (30);
}

void KuoStereoEditor::resized()
{
    bypass.setBounds (450, 33, 74, 28);
    const std::array<juce::Rectangle<int>, 5> positions {
        juce::Rectangle<int> (52, 163, 120, 70),
        juce::Rectangle<int> (184, 163, 120, 70),
        juce::Rectangle<int> (44, 319, 130, 75),
        juce::Rectangle<int> (189, 319, 130, 75),
        juce::Rectangle<int> (344, 319, 130, 75)
    };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        labels[i].setBounds (positions[i].getX(), positions[i].getY() - 17, positions[i].getWidth(), 16);
        sliders[i].setBounds (positions[i]);
    }
}

void KuoStereoEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (background.brighter (0.035f), 0, 0,
                                            background.darker (0.13f), 0, 741, false));
    g.fillAll();
    g.setColour (secondary);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("J-SONIC / STEREO FIELD", 24, 20, 260, 18, juce::Justification::centredLeft);
    g.setColour (foreground);
    g.setFont (juce::FontOptions (27.0f));
    g.drawText ("KUO STEREO", 24, 38, 410, 38, juce::Justification::centredLeft);
    drawPanel (g, { 24, 90, 500, 150 });
    drawPanel (g, { 24, 250, 500, 150 });
    drawPanel (g, { 24, 410, 500, 307 });
    g.setColour (foreground);
    g.setFont (juce::FontOptions (13.0f).withStyle ("bold"));
    g.drawText ("SMART HAAS", 38, 102, 250, 20, juce::Justification::centredLeft);
    g.drawText ("VELVET WIDTH", 38, 262, 250, 20, juce::Justification::centredLeft);
    g.drawText ("FREQUENCY FIELD", 38, 422, 250, 20, juce::Justification::centredLeft);
    g.setColour (secondary);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("Mid-aware delay", 38, 124, 250, 16, juce::Justification::centredLeft);
    g.drawText ("Sparse stereo texture with transient protection", 38, 284, 390, 16,
                juce::Justification::centredLeft);
    g.drawText ("LOW  /  MID  /  HIGH    -    AUTO SCALE", 38, 444, 390, 16,
                juce::Justification::centredLeft);
    g.setColour (secondary);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("SELECTED DELAY", 345, 151, 150, 17, juce::Justification::centred);
    g.setColour (amber);
    g.setFont (juce::FontOptions (22.0f));
    g.drawText (juce::String (processor.getSelectedDelayMs(), 1) + " ms", 335, 176, 170, 34,
                juce::Justification::centred);
    paintScope (g, 0, { 39, 474, 148, 202 });
    paintScope (g, 1, { 200, 474, 148, 202 });
    paintScope (g, 2, { 361, 474, 148, 202 });
    g.setColour (secondary);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("BELOW 250 Hz", 39, 684, 148, 16, juce::Justification::centred);
    g.drawText ("250 Hz - 2.5 kHz", 200, 684, 148, 16, juce::Justification::centred);
    g.drawText ("ABOVE 2.5 kHz", 361, 684, 148, 16, juce::Justification::centred);
}

void KuoStereoEditor::paintScope (juce::Graphics& g, int band, juce::Rectangle<float> bounds)
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
    std::array<juce::Point<float>, KuoStereoProcessor::scopePoints> points;
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
