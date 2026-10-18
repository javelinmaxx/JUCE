#pragma once

#include <algorithm>
#include <array>
#include <cmath>
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
        constexpr double cutoff = 0.235; // cycles / oversampled sample; host Nyquist is 0.25
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
        fluxCoefficient = static_cast<float> (std::exp (-2 * pi * 60 / rate));
        // Fourth-order Butterworth at 500 Hz, applied only to the nonlinear
        // residual. Generated harmonics above this corner roll off at 24 dB/oct.
        const auto k = std::tan (pi * distortionCornerHz / rate);
        constexpr std::array<double, 2> q { 0.541196100146197, 1.306562964876377 };
        for (size_t i = 0; i < distortionFilters.size(); ++i)
            distortionFilters[i].prepare (k, q[i]);
        response.prepare (rate);
        reset();
    }

    void reset() noexcept
    {
        inputHistory = {};
        outputHistory = {};
        inputPosition = outputPosition = 0;
        flux = 0;
        for (auto& filter : distortionFilters)
            filter.reset();
        response.reset();
    }

    float process (float x, float drive) noexcept
    {
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
        // Isolate generated distortion from the unity small-signal path. Only
        // this residual is low-passed; clean highs retain the transformer response.
        flux = x + fluxCoefficient * (flux - x);
        const auto bias = 0.12f * std::tanh (flux * drive);
        const auto b = std::tanh (bias);
        const auto shaped = (std::tanh (drive * x + bias) - b) / (drive * (1 - b * b));
        auto residual = shaped - x;
        for (auto& filter : distortionFilters)
            residual = filter.process (residual);
        return response.process (x + residual);
    }

    struct Lowpass
    {
        void prepare (double k, double q) noexcept
        {
            const auto norm = 1.0 / (1.0 + k / q + k * k);
            b0 = k * k * norm;
            a1 = 2.0 * (k * k - 1.0) * norm;
            a2 = (1.0 - k / q + k * k) * norm;
            reset();
        }
        void reset() noexcept { z1 = z2 = 0; }
        float process (float input) noexcept
        {
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
    std::array<Lowpass, 2> distortionFilters;
    TransformerResponse response;
};
} // namespace character
