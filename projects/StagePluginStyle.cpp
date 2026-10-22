#include "StagePluginStyle.h"
#include <cmath>

namespace stageStyle
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

using namespace stageStyle;

void StageLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
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

juce::Label* StageLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (10.0f));
    return label;
}

void StageBypassButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
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
    g.drawText (getButtonText(), bounds.toNearestInt(), juce::Justification::centred);
}

void StageTransformerSwitch::paintButton (juce::Graphics& g, bool, bool)
{
    const auto cx = static_cast<float> (getWidth()) * 0.5f;
    constexpr auto pivotY = static_cast<float> (StageTransformerSwitch::pivotOffsetY);
    g.setColour (secondary);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("CORE", 0, 0, getWidth(), 15, juce::Justification::centred);
    juce::Path nut;
    for (int corner = 0; corner < 6; ++corner)
    {
        const auto angle = juce::MathConstants<double>::pi * 2.0 * corner / 6.0;
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
    g.drawText (getToggleState() ? "ON" : "OFF", 0, 76, getWidth(), 18, juce::Justification::centred);
    if (hasKeyboardFocus (true)) g.drawEllipse (cx - 18, pivotY - 18, 36, 36, 1);
}

void StageLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float, float, const juce::Slider::SliderStyle, juce::Slider&)
{
    const auto centre = static_cast<float> (x + width / 2);
    const auto top = static_cast<float> (y + 5);
    const auto bottom = static_cast<float> (y + height - 5);
    g.setColour (grid.brighter (0.3f));
    g.fillRoundedRectangle (centre - 2.0f, top, 4.0f, bottom - top, 2.0f);
    g.setColour (amber.withAlpha (0.72f));
    g.fillRoundedRectangle (centre - 1.0f, sliderPos, 2.0f,
                            std::max (0.0f, bottom - sliderPos), 1.0f);
    g.setColour (amber);
    g.fillRoundedRectangle (centre - 6.0f, sliderPos - 3.0f, 12.0f, 6.0f, 2.0f);
}
