#pragma once
#include "EqualLoudness80.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace zao
{
// ISO 226:2003 at 80 phons, relative to 1.5 kHz and capped at +3 dB.
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

    static double targetDb (double hz) noexcept { return EqualLoudness80::gainDb (hz); }

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
        std::vector<float> window (static_cast<size_t> (taps));
        juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), static_cast<size_t> (taps),
            juce::dsp::WindowingFunction<float>::kaiser, false, 3.0f);
        double reference = 0;
        for (int i = 0; i <= latency; ++i)
        {
            const auto value = impulse[static_cast<size_t> (i)].real()*window[static_cast<size_t> (latency+i)];
            coefficients[static_cast<size_t> (i)] = static_cast<float> (value);
            reference += (i == 0 ? 1 : 2)*static_cast<double> (coefficients[static_cast<size_t> (i)])
                * std::cos (2*juce::MathConstants<double>::pi*EqualLoudness80::referenceFrequencyHz*i/sampleRate);
        }
        // Keep 1.5 kHz at unity; both sides of each pair use the same float.
        for (auto& coefficient : coefficients) coefficient /= static_cast<float> (reference);
        enforceBoostCeiling();
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
    void enforceBoostCeiling()
    {
        // Referencing the top of a smoothed contour can lift its plateaus above
        // the target cap. Blend with a pure delay to bound the actual response
        // while keeping the reference gain and Type-I symmetry intact.
        const auto size = juce::nextPowerOfTwo (taps*32);
        juce::dsp::FFT fft (juce::roundToInt (std::log2 (size)));
        std::vector<float> spectrum (static_cast<size_t> (2*size), 0.0f);
        for (int i = 0; i < taps; ++i)
            spectrum[static_cast<size_t> (i)] = coefficients[static_cast<size_t> (std::abs (i-latency))];
        fft.performFrequencyOnlyForwardTransform (spectrum.data());
        double peak = 0;
        for (int i = 0; i <= size/2; ++i) peak = std::max (peak, static_cast<double> (spectrum[static_cast<size_t> (i)]));
        // Small margin covers the FFT grid and float coefficient rounding.
        peak *= 1.000001;
        const auto limit = std::pow (10.0, (EqualLoudness80::maximumBoostDb-.0001)/20);
        if (peak <= limit) return;
        const auto blend = static_cast<float> ((limit-1)/(peak-1));
        for (auto& coefficient : coefficients) coefficient *= blend;
        coefficients[0] += 1-blend;
    }
    int latency = referenceLatencySamples, taps = 2*referenceLatencySamples+1;
    std::vector<float> coefficients;
    std::array<std::vector<float>, 2> history;
    std::array<size_t, 2> position {};
};
}
