#pragma once
#include <array>
#include <cmath>

namespace character
{
// Fixed-storage 4x polyphase FIR converter. Two 257-tap linear-phase filters
// give a combined 64 host-sample group delay at every supported sample rate.
class Oversampling4x
{
public:
    static constexpr size_t taps = 257;
    static constexpr int factor = 4;
    static constexpr int latencySamples = static_cast<int> ((taps - 1) / factor);
    using Frame = std::array<float, factor>;
    void prepare() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        // Design a unity-sum Blackman-windowed sinc for both halves of the
        // 4x converter. The cutoff is just below host Nyquist (0.125 cycles
        // at the oversampled rate), with enough taps to retain a narrow
        // transition band and strong stopband rejection.
        constexpr double cutoff = 0.1175;
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

        reset();
    }
    void reset() noexcept
    {
        inputHistory = {}; outputHistory = {};
        inputPosition = outputPosition = 0;
    }
    Frame upsample (float input) noexcept
    {
        inputPosition = inputPosition == 0 ? inputLength - 1 : inputPosition - 1;
        inputHistory[inputPosition] = inputHistory[inputPosition + inputLength] = input;
        Frame result {};
        for (size_t phase = 0; phase < factor; ++phase)
        {
            float value = 0;
            for (size_t k = phase; k < taps; k += factor)
                value += coefficients[k] * inputHistory[inputPosition + k / factor];
            result[phase] = value * factor;
        }
        return result;
    }
    float downsample (const Frame& input) noexcept
    {
        float output = 0;
        for (size_t phase = 0; phase < factor; ++phase)
        {
            outputPosition = outputPosition == 0 ? taps - 1 : outputPosition - 1;
            outputHistory[outputPosition] = outputHistory[outputPosition + taps] = input[phase];
            if (phase == 0)
                for (size_t k = 0; k < taps; ++k)
                    output += coefficients[k] * outputHistory[outputPosition + k];
        }
        return output;
    }
private:
    static constexpr size_t inputLength = (taps + factor - 1) / factor;
    std::array<float, taps> coefficients {};
    // Mirrored histories make each FIR read contiguous, with no allocation.
    std::array<float, inputLength * 2> inputHistory {};
    std::array<float, taps * 2> outputHistory {};
    size_t inputPosition = 0, outputPosition = 0;
};
} // namespace character
