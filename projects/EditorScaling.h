#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cmath>

namespace editorScaling
{
inline void enable (juce::AudioProcessorEditor& editor, int designWidth, int designHeight)
{
    editor.setResizable (true, true);
    editor.setResizeLimits (juce::roundToInt (designWidth * 0.5f),
                            juce::roundToInt (designHeight * 0.5f),
                            juce::roundToInt (designWidth * 1.2f),
                            juce::roundToInt (designHeight * 1.2f));
    if (auto* constrainer = editor.getConstrainer())
        constrainer->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    editor.setSize (designWidth, designHeight);
}

inline juce::AffineTransform transform (const juce::Component& editor,
                                        int designWidth, int designHeight)
{
    const auto scale = std::min (static_cast<float> (editor.getWidth()) / designWidth,
                                 static_cast<float> (editor.getHeight()) / designHeight);
    const auto x = (editor.getWidth() - designWidth * scale) * 0.5f;
    const auto y = (editor.getHeight() - designHeight * scale) * 0.5f;
    return juce::AffineTransform::scale (scale).translated (x, y);
}

inline juce::Rectangle<int> repaintArea (const juce::Component& editor,
                                         int designWidth, int designHeight,
                                         juce::Rectangle<int> area)
{
    return area.toFloat().transformedBy (transform (editor, designWidth, designHeight))
               .expanded (2.0f).getSmallestIntegerContainer();
}
}
