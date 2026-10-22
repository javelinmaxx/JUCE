#pragma once

#include <algorithm>
#include <cmath>

namespace character
{
// Small-signal loaded-transformer approximation. The LF magnetizing branch is
// a damped two-pole high-pass; leakage inductance and winding capacitance form
// a second damped low-pass. Magnitude and phase come from the same causal filters.
// These defaults are design choices, not measurements of a particular unit.
class TransformerResponse
{
public:
    static constexpr double lowCornerHz = 20.0;
    static constexpr double lowQ = 1.0;
    static constexpr double highCornerHz = 19000.0;
    static constexpr double highQ = 1.2;

    void prepare (double processingRate) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        const auto rate = std::isfinite (processingRate) && processingRate >= 8000
                        ? processingRate : 96000.0;

        // Bilinear-transform the LF and HF second-order filters, prewarped at
        // their corners. These run at the oversampled rate. Clamp the corners
        // below Nyquist at low host rates.
        const auto lowK = std::tan (pi * lowCornerHz / rate);
        const auto lowNormalise = 1.0 / (1.0 + lowK / lowQ + lowK * lowK);
        lowB0 = lowNormalise;
        lowB1 = -2.0 * lowNormalise;
        lowB2 = lowNormalise;
        lowA1 = 2.0 * (lowK * lowK - 1.0) * lowNormalise;
        lowA2 = (1.0 - lowK / lowQ + lowK * lowK) * lowNormalise;

        const auto highK = std::tan (pi * std::min (highCornerHz, rate * 0.4) / rate);
        const auto normalise = 1.0 / (1.0 + highK / highQ + highK * highK);
        b0 = highK * highK * normalise;
        b1 = 2.0 * b0;
        b2 = b0;
        a1 = 2.0 * (highK * highK - 1.0) * normalise;
        a2 = (1.0 - highK / highQ + highK * highK) * normalise;
        reset();
    }

    void reset() noexcept { lowZ1 = lowZ2 = z1 = z2 = 0; }

    float process (float sample) noexcept
    {
        const auto input = static_cast<double> (sample);
        // First remove the LF magnetizing-branch response, then apply the
        // damped HF pole pair. These causal filters create the modeled phase
        // shift and magnitude response together.
        const auto highpass = lowB0 * input + lowZ1;
        lowZ1 = lowB1 * input - lowA1 * highpass + lowZ2;
        lowZ2 = lowB2 * input - lowA2 * highpass;
        const auto output = b0 * highpass + z1;
        z1 = b1 * highpass - a1 * output + z2;
        z2 = b2 * highpass - a2 * output;
        return static_cast<float> (output);
    }

private:
    double lowB0 = 0, lowB1 = 0, lowB2 = 0, lowA1 = 0, lowA2 = 0;
    double lowZ1 = 0, lowZ2 = 0;
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};
} // namespace character
