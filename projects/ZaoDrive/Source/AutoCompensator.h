#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace zao
{
// A fixed preview-derived correction, shared by both channels. Only parameter
// changes are smoothed; live audio never drives a detector or gain envelope.
class AutoCompensator
{
public:
    static constexpr double inputFloor = 1e-8, outputFloor = 1e-10;
    static float matchingGain (double inputPower, double outputPower) noexcept
    {
        if (! std::isfinite (inputPower) || ! std::isfinite (outputPower)
            || inputPower < inputFloor || outputPower < outputFloor) return 1;
        return static_cast<float> (std::clamp (std::sqrt (inputPower/outputPower),
            .015848931924611134, 63.09573444801933)); // +/-36 dB
    }
    void prepare (double rate) { gain.reset (rate, .02); reset(); }
    void reset() noexcept { gain.setCurrentAndTargetValue (1); }
    void setTarget (float target) noexcept
    {
        gain.setTargetValue (std::isfinite (target) ? std::clamp (target, .01584893f, 63.095735f) : 1.0f);
    }
    std::array<float, 2> process (const std::array<float, 2>& output, int channels) noexcept
    {
        const auto amount = gain.getNextValue();
        auto result = output;
        for (int ch = 0; ch < channels; ++ch) result[static_cast<size_t> (ch)] *= amount;
        return result;
    }
    float currentGain() const noexcept { return gain.getCurrentValue(); }
private:
    juce::SmoothedValue<float> gain;
};
}
