#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace editorTooltips
{
inline void setHint (juce::Slider& slider, const juce::String& hint)
{
    slider.setTooltip (hint);
    // Slider text boxes are child Labels and receive their tooltip when they
    // are created, before the editor assigns the control-specific hint.
    for (auto* child : slider.getChildren())
        if (auto* readout = dynamic_cast<juce::Label*> (child))
            readout->setTooltip (hint);
}

inline void setHint (juce::Slider& slider, juce::Label& caption,
                     const juce::String& hint)
{
    setHint (slider, hint);
    caption.setTooltip (hint);
}
}
