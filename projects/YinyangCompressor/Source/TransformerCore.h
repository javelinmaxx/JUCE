#pragma once
#include <array>
#include <cmath>
#include "MagneticHysteresis.h"
#include "TransformerResponse.h"

namespace character
{
// Transformer equations at the caller's processing rate, without conversion.
// Used inside the full-rate compressor and the standalone oversampled wrapper.
class TransformerCore
{
public:
    static constexpr double distortionCornerHz = 500.0;
    void prepare (double rate) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        // The 60 Hz pole sets flux memory in seconds at the processing rate.
        fluxCoefficient = static_cast<float> (std::exp (-2 * pi * 60 / rate));
        // Second-order Butterworth at 500 Hz, applied only to the nonlinear
        // residual. Generated harmonics roll off at 12 dB/octave above the corner.
        const auto k = std::tan (pi * distortionCornerHz / rate);
        constexpr double butterworthQ = 0.7071067811865476;
        distortionFilters[0].prepare (k, butterworthQ);
        response.prepare (rate);
        reset();
    }

    void reset() noexcept
    {
        flux = 0;
        hysteresis.reset();
        for (auto& filter : distortionFilters) filter.reset();
        response.reset();
    }
    float process (float x, float drive, float evenHarmonics) noexcept
    {
        // Filter only the generated-distortion residual below. Clean highs
        // retain the transformer response.
        const auto field = drive * x;
        const auto magnetization = hysteresis.process (field, drive);
        // The field/magnetization difference changes after a reversal. Scale
        // that branch gap to get a compact, rate-independent hysteresis loop.
        constexpr float hysteresisCoupling = 0.35f;
        const auto effectiveField = field + hysteresisCoupling * (field - magnetization);

        flux = x + fluxCoefficient * (flux - x);
        // Flux adds a small signal-dependent bias for asymmetric coloration.
        // A controlled DC magnetizing current biases the core and makes its
        // transfer curve asymmetric, increasing even-order harmonics. The
        // zero setting preserves the original symmetric operating point.
        const auto bias = 0.12f * std::tanh (flux * drive) + 0.8f * evenHarmonics;
        const auto b = std::tanh (bias);
        // Bias-corrected tanh, normalized for unity minor-loop small-signal
        // slope. Filtering only (shaped - x) preserves clean highs while
        // generated distortion rolls off above 500 Hz.
        const auto shaped = (std::tanh (effectiveField + bias) - b)
                          / (drive * (1 + hysteresisCoupling) * (1 - b * b));
        auto residual = shaped - x;
        for (auto& filter : distortionFilters)
            residual = filter.process (residual);
        return response.process (x + residual);
    }

private:
    struct Lowpass
    {
        void prepare (double k, double q) noexcept
        {
            // Bilinear-transform second-order low-pass, prewarped by k.
            const auto norm = 1.0 / (1.0 + k / q + k * k);
            b0 = k * k * norm;
            a1 = 2.0 * (k * k - 1.0) * norm;
            a2 = (1.0 - k / q + k * k) * norm;
            reset();
        }
        void reset() noexcept { z1 = z2 = 0; }
        float process (float input) noexcept
        {
            // Transposed direct-form II: two persistent states, no allocation.
            const auto output = b0 * input + z1;
            z1 = 2.0 * b0 * input - a1 * output + z2;
            z2 = b0 * input - a2 * output;
            return static_cast<float> (output);
        }
        double b0 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    };

    float fluxCoefficient = 0, flux = 0;
    MagneticHysteresis hysteresis;
    std::array<Lowpass, 1> distortionFilters;
    TransformerResponse response;
};
} // namespace character
