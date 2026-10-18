#pragma once

#include <algorithm>
#include <cmath>

namespace character
{
// Fixed +6 dB RBJ high shelf, S = 1; 1.5 kHz is the +3 dB midpoint.
// Run in the sidechain at host rate; crossfade before detector rectification.
class HighTilt
{
public:
    void prepare (double sampleRate) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        const auto rate = std::isfinite (sampleRate) && sampleRate >= 8000 ? sampleRate : 44100.0;
        const auto a = std::pow (10.0, 6.0 / 40.0);
        const auto w = 2.0 * pi * std::min (1500.0, rate * 0.4) / rate;
        const auto c = std::cos (w);
        const auto beta = std::sin (w) * std::sqrt (2.0 * a);
        const auto a0 = (a + 1) - (a - 1) * c + beta;
        b0 = a * ((a + 1) + (a - 1) * c + beta) / a0;
        b1 = -2 * a * ((a - 1) + (a + 1) * c) / a0;
        b2 = a * ((a + 1) + (a - 1) * c - beta) / a0;
        a1 = 2 * ((a - 1) - (a + 1) * c) / a0;
        a2 = ((a + 1) - (a - 1) * c - beta) / a0;
        reset();
    }

    void reset() noexcept { z1 = z2 = 0; }

    float process (float input) noexcept
    {
        const auto output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return static_cast<float> (output);
    }

private:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};
} // namespace character
