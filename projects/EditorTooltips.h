#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace editorTooltips
{
// Transparent hit target over the painted heading; version comes from the
// same CMake metadata used by the host, VST3 bundle and Audio Unit.
class TitleHint final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TitleHint (const juce::String& version)
    {
        setName ("Plugin title");
        setTooltip ("Developer: J-Sonic Team\nYear: 2026\nVersion: " + version);
        setTitle ("Plugin information");
        setDescription (getTooltip());
        setInterceptsMouseClicks (true, false);
    }
};

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
