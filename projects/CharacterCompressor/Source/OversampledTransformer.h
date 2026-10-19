#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "MagneticHysteresis.h"
#include "TransformerResponse.h"

namespace character
{
// Fixed 2x oversampling with two 129-tap Blackman-windowed sinc filters.
// The interpolation and decimation filters each delay the signal by 64 samples
// at 2x rate, for an integer 64-sample round-trip delay at the host rate.
class OversampledTransformer
{
public:
    static constexpr int factor = 2;
    static constexpr int latencySamples = 64;
    static constexpr double distortionCornerHz = 500.0;

    void prepare (double sampleRate) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        // Design one unity-sum Blackman-windowed sinc for both halves of the
        // 2x converter. Its 0.235-cycle cutoff leaves a transition below host
        // Nyquist (0.25 cycles at the oversampled rate).
        constexpr double cutoff = 0.235;
        double sum = 0;
        for (size_t i = 0; i < taps; ++i)
        {
            const auto n = static_cast<double> (i) - static_cast<double> (taps - 1) * 0.5;
            const auto sinc = i == (taps - 1) / 2 ? 2 * cutoff
                            : std::sin (2 * pi * cutoff * n) / (pi * n);
            const auto angle = 2 * pi * static_cast<double> (i) / static_cast<double> (taps - 1);
            coefficients[i] = static_cast<float> (sinc * (0.42 - 0.5 * std::cos (angle)
                                                            + 0.08 * std::cos (2 * angle)));
            sum += coefficients[i];
        }
        for (auto& c : coefficients)
            c = static_cast<float> (c / sum);

        const auto rate = sampleRate * factor;
        // Flux state runs at 2x as well; the 60 Hz pole sets its memory time.
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
        inputHistory = {};
        outputHistory = {};
        inputPosition = outputPosition = 0;
        flux = 0;
        hysteresis.reset();
        for (auto& filter : distortionFilters)
            filter.reset();
        response.reset();
    }

    float process (float x, float drive) noexcept
    {
        // Insert a zero between host samples, apply the appropriate FIR phase,
        // pass both high-rate samples through the nonlinear stage, then retain
        // every other output sample for decimation.
        inputPosition = inputPosition == 0 ? inputLength - 1 : inputPosition - 1;
        inputHistory[inputPosition] = inputHistory[inputPosition + inputLength] = x;
        float output = 0;
        for (size_t phase = 0; phase < factor; ++phase)
        {
            // Polyphase interpolation omits the inserted zeros. Multiplication
            // by 2 compensates zero insertion and preserves the passband level.
            float interpolated = 0;
            for (size_t k = phase; k < taps; k += factor)
                interpolated += coefficients[k] * inputHistory[inputPosition + k / factor];
            const auto shaped = saturate (interpolated * factor, drive);
            outputPosition = outputPosition == 0 ? taps - 1 : outputPosition - 1;
            outputHistory[outputPosition] = outputHistory[outputPosition + taps] = shaped;
            if (phase == 0)
                for (size_t k = 0; k < taps; ++k)
                    output += coefficients[k] * outputHistory[outputPosition + k];
        }
        return output;
    }

private:
    float saturate (float x, float drive) noexcept
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
        const auto bias = 0.12f * std::tanh (flux * drive);
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

    static constexpr size_t taps = 129;
    static constexpr size_t inputLength = (taps + 1) / factor;
    std::array<float, taps> coefficients {};
    // Mirrored ring buffers keep convolution reads contiguous and allocation-free.
    std::array<float, inputLength * 2> inputHistory {};
    std::array<float, taps * 2> outputHistory {};
    size_t inputPosition = 0, outputPosition = 0;
    float fluxCoefficient = 0, flux = 0;
    MagneticHysteresis hysteresis;
    std::array<Lowpass, 1> distortionFilters;
    TransformerResponse response;
};
} // namespace character
