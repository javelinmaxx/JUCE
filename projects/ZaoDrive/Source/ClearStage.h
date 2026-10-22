#pragma once
#include "GreyNoiseFilter.h"

namespace zao
{
// The flat and shaped feeds share the FIR's delay. Warm the filter before
// fading it in, and suspend convolution only after the Off fade completes.
class ClearStage
{
public:
    void prepare (double rate)
    {
        filter.prepare (rate);
        for (auto& channel : delay) channel.resize (static_cast<size_t> (getLatencySamples()));
        blend.reset (rate, .02);
        reset();
    }
    void reset()
    {
        filter.reset();
        for (auto& channel : delay) std::fill (channel.begin(), channel.end(), 0.0f);
        position = 0; warmSamples = 0; first = true; running = enabled = false;
        blend.setCurrentAndTargetValue (0);
    }
    void setEnabled (bool on)
    {
        enabled = on;
        if (first)
        {
            running = on; blend.setCurrentAndTargetValue (on ? 1.0f : 0.0f); first = false;
        }
        else if (on && ! running)
        {
            filter.reset(); running = true; warmSamples = 2*getLatencySamples()+1;
            blend.setCurrentAndTargetValue (0);
        }
        else if (! on)
        {
            warmSamples = 0; blend.setTargetValue (0);
        }
        else if (warmSamples == 0)
            blend.setTargetValue (1);
    }
    std::array<float, 2> process (const std::array<float, 2>& input) noexcept
    {
        if (enabled && warmSamples > 0 && --warmSamples == 0) blend.setTargetValue (1);
        const auto amount = blend.getNextValue();
        std::array<float, 2> result;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const auto flat = delay[ch][position]; delay[ch][position] = input[ch];
            const auto shaped = running ? filter.process (input[ch], ch) : flat;
            result[ch] = amount >= 1 ? shaped : amount <= 0 ? flat : flat+amount*(shaped-flat);
        }
        if (++position == delay[0].size()) position = 0;
        if (! enabled && ! blend.isSmoothing()) running = false;
        return result;
    }
    int getLatencySamples() const noexcept { return filter.getLatencySamples(); }
    bool isProcessing() const noexcept { return running; }
private:
    GreyNoiseFilter filter;
    std::array<std::vector<float>, 2> delay;
    juce::SmoothedValue<float> blend;
    size_t position = 0;
    int warmSamples = 0;
    bool first = true, running = false, enabled = false;
};
}
