#pragma once
#include "Oversampling4x.h"
#include "TransformerCore.h"

namespace character
{
// Original standalone transformer stage for Ren Transformer and Zao Drive.
// Yinyang's full 4x signal path uses TransformerCore directly to avoid 16x DSP.
class OversampledTransformer
{
public:
    static constexpr size_t taps = Oversampling4x::taps;
    static constexpr int factor = Oversampling4x::factor;
    static constexpr int latencySamples = Oversampling4x::latencySamples;
    static constexpr double distortionCornerHz = TransformerCore::distortionCornerHz;
    void prepare (double sampleRate) noexcept
    {
        converter.prepare(); core.prepare (sampleRate * factor);
    }
    void reset() noexcept { converter.reset(); core.reset(); }
    float process (float input, float drive, float evenHarmonics = 0) noexcept
    {
        auto frame = converter.upsample (input);
        for (auto& sample : frame) sample = core.process (sample, drive, evenHarmonics);
        return converter.downsample (frame);
    }
private:
    Oversampling4x converter;
    TransformerCore core;
};
} // namespace character
