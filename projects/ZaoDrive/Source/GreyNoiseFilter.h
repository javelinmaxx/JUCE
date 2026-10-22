#pragma once
#include "EqualLoudness80.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstring>
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
        for (auto& channel : history) channel.resize (static_cast<size_t> (2*taps)+8);
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
#if JUCE_USE_SIMD
        // Prepack the unchanged impulse in double precision. Four independent
        // accumulators avoid a long scalar dependency chain without fast-math.
        packedCoefficients.resize ((static_cast<size_t> (taps)+Vector::size()-1)/Vector::size());
        for (size_t i = 0; i < packedCoefficients.size(); ++i)
        {
            alignas (Vector::SIMDRegisterSize) double values[Vector::size()] {};
            for (size_t lane = 0; lane < Vector::size(); ++lane)
            {
                const auto tap = static_cast<int> (i*Vector::size()+lane);
                if (tap < taps) values[lane] = coefficients[static_cast<size_t> (std::abs (tap-latency))];
            }
            packedCoefficients[i] = Vector::fromRawArray (values);
        }
#endif
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
#if JUCE_USE_SIMD
        Vector a (0.0), b (0.0), c (0.0), d (0.0);
        size_t i = 0;
        for (; i+3 < packedCoefficients.size(); i += 4)
        {
            a += packedCoefficients[i]   * loadUnaligned (span+i*Vector::size());
            b += packedCoefficients[i+1] * loadUnaligned (span+(i+1)*Vector::size());
            c += packedCoefficients[i+2] * loadUnaligned (span+(i+2)*Vector::size());
            d += packedCoefficients[i+3] * loadUnaligned (span+(i+3)*Vector::size());
        }
        for (; i < packedCoefficients.size(); ++i)
            a += packedCoefficients[i]*loadUnaligned (span+i*Vector::size());
        const auto output = ((a+b)+(c+d)).sum();
#else
        double output = coefficients[0]*static_cast<double> (span[latency]);
        for (int distance = 1; distance <= latency; ++distance)
            output += coefficients[static_cast<size_t> (distance)]
                * (static_cast<double> (span[latency-distance])+span[latency+distance]);
#endif
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
#if JUCE_USE_SIMD
    using Vector = juce::dsp::SIMDRegister<double>;
    static Vector loadUnaligned (const double* samples) noexcept
    {
        // A ring position need not be SIMD-aligned. memcpy permits an unaligned
        // load on both ARM and x86 without violating the register API contract.
        Vector result;
        std::memcpy (&result.value, samples, sizeof (result.value));
        return result;
    }
    std::vector<Vector> packedCoefficients;
#endif
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
    std::array<std::vector<double>, 2> history;
    std::array<size_t, 2> position {};
};
}
