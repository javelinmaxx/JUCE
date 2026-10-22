#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace zao
{
// A bounded inverse A-weighting contour is a practical grey-noise-style
// pre-shaping: reduce the ear's most sensitive band without intentional bass
// or treble gain.
// Type-I symmetry gives one frequency-independent delay, scaled with the
// sample rate to keep the same contour and approximately 2.67 ms of latency.
class GreyNoiseFilter
{
public:
    static constexpr int referenceLatencySamples = 128;
    static int latencyForRate (double rate) noexcept
    {
        return std::clamp (juce::roundToInt (rate*referenceLatencySamples/48000), 16, 1024);
    }
    int getLatencySamples() const noexcept { return latency; }

    static double targetDb (double hz) noexcept
    {
        if (hz <= 0) return 0;
        const auto f2 = hz*hz;
        const auto response = (12200.0*12200.0*f2*f2)
            / ((f2+20.6*20.6)*std::sqrt ((f2+107.7*107.7)*(f2+737.9*737.9))*(f2+12200.0*12200.0));
        const auto aWeightDb = 20*std::log10 (std::max (response, 1e-30))+2.0;
        return -6*std::clamp ((aWeightDb+6)/7.3, 0.0, 1.0);
    }

    void prepare (double sampleRate)
    {
        latency = latencyForRate (sampleRate); taps = 2*latency+1;
        coefficients.resize (static_cast<size_t> (latency+1));
        for (auto& channel : history) channel.resize (static_cast<size_t> (2*taps));
        constexpr int size = 4096;
        juce::dsp::FFT fft (12);
        std::array<juce::dsp::Complex<float>, size> spectrum {}, impulse {};
        for (int i = 0; i < size; ++i)
        {
            const auto hz = std::min (i, size-i)*sampleRate/size;
            spectrum[static_cast<size_t> (i)] = static_cast<float> (std::pow (10.0, targetDb (hz)/20));
        }
        fft.perform (spectrum.data(), impulse.data(), true);
        double sum = 0;
        for (int i = 0; i <= latency; ++i)
        {
            const auto phase = juce::MathConstants<double>::pi*i/latency;
            const auto window = .42+.5*std::cos (phase)+.08*std::cos (2*phase);
            const auto value = impulse[static_cast<size_t> (i)].real()*window;
            coefficients[static_cast<size_t> (i)] = static_cast<float> (value);
            sum += (i == 0 ? 1 : 2)*value;
        }
        // DC is unity; both sides of every tap pair use the very same float.
        for (auto& coefficient : coefficients) coefficient /= static_cast<float> (sum);
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : history) std::fill (channel.begin(), channel.end(), 0.0f);
        position = {};
    }

    float process (float input, size_t channel) noexcept
    {
        auto& samples = history[channel];
        auto& index = position[channel];
        samples[index] = samples[index+static_cast<size_t> (taps)] = input;
        const auto* span = samples.data()+index+1;
        double output = coefficients[0]*static_cast<double> (span[latency]);
        for (int distance = 1; distance <= latency; ++distance)
            output += coefficients[static_cast<size_t> (distance)]
                * (static_cast<double> (span[latency-distance])+span[latency+distance]);
        if (++index == static_cast<size_t> (taps)) index = 0;
        return static_cast<float> (output);
    }

    std::vector<float> getImpulseResponse() const
    {
        std::vector<float> result (static_cast<size_t> (taps));
        for (int i = 0; i < taps; ++i)
            result[static_cast<size_t> (i)] = coefficients[static_cast<size_t> (std::abs (i-latency))];
        return result;
    }

private:
    int latency = referenceLatencySamples, taps = 2*referenceLatencySamples+1;
    std::vector<float> coefficients;
    std::array<std::vector<float>, 2> history;
    std::array<size_t, 2> position {};
};
}
