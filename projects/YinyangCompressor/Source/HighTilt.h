#pragma once

#include <algorithm>
#include <cmath>

namespace character
{
// RBJ high shelf, S = 1; 1.5 kHz is its half-gain midpoint.
// Run in the sidechain at host rate, before detector rectification.
class HighTilt
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = std::isfinite (sampleRate) && sampleRate >= 8000 ? sampleRate : 44100.0;
        setGainDb (0);
        reset();
    }

    void setGainDb (float gainDb) noexcept
    {
        // Recalculate the shelf coefficients outside the sample loop. Its
        // parameter is already smoothed by the compressor's control ramp.
        constexpr double pi = 3.14159265358979323846;
        const auto gain = std::pow (10.0, std::clamp (static_cast<double> (gainDb), 0.0, 12.0) / 40.0);
        const auto w = 2.0 * pi * std::min (1500.0, rate * 0.4) / rate;
        const auto c = std::cos (w);
        const auto beta = std::sin (w) * std::sqrt (2.0 * gain);
        const auto a0 = (gain + 1) - (gain - 1) * c + beta;
        b0 = gain * ((gain + 1) + (gain - 1) * c + beta) / a0;
        b1 = -2 * gain * ((gain - 1) + (gain + 1) * c) / a0;
        b2 = gain * ((gain + 1) + (gain - 1) * c - beta) / a0;
        a1 = 2 * ((gain - 1) - (gain + 1) * c) / a0;
        a2 = ((gain + 1) - (gain - 1) * c - beta) / a0;
    }

    void reset() noexcept { z1 = z2 = 0; }

    float process (float input) noexcept
    {
        // Transposed direct-form II shelf response; independent state per channel.
        const auto output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return static_cast<float> (output);
    }

private:
    double rate = 44100;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};
} // namespace character
