#pragma once
#include "PluginProcessor.h"
#include "../../StagePluginStyle.h"

// A three-second input spectrogram: newest at left, older data moving right.
// Colour describes observed arrival error; white (L) and gold (R) curves show
// measured complex responses of the kernels actually used by the audio path.
class XuanPhaseMeter final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
{
public:
    explicit XuanPhaseMeter (XuanPhaseProcessor&);
    ~XuanPhaseMeter() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
    void setActive (bool);
    int historySize() const noexcept { return columnsWritten; }
    const juce::Image& historyImage() const noexcept { return history; }
    const xuan::RotationFrame& currentRotation() const noexcept { return rotation; }
    static float rotationDegrees (const xuan::RotationFrame&, int channel, int row) noexcept;
    static juce::Path rotationPath (const xuan::RotationFrame&, int channel, juce::Rectangle<float>);
    static juce::Colour timingColour (float milliseconds, float energy) noexcept;
private:
    static constexpr int columns = 90;
    void timerCallback() override;
    void append (const xuan::SpectrumFrame&);
    XuanPhaseProcessor& processor;
    juce::Image history { juce::Image::ARGB, columns, xuan::meterBins, true };
    std::array<float, columns> transient {}, peak {};
    xuan::SpectrumFrame spectrum {};
    xuan::RotationFrame rotation {};
    uint32_t spectrumVersion = 0, rotationVersion = 0;
    int columnsWritten = 0;
    double lastColumn = 0, lastInput = 0;
};
