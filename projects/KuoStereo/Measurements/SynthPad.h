#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace kuoTestSignal
{
constexpr int padSampleRate = 48000;
constexpr int chordChangeSample = 3 * padSampleRate;
constexpr int padSamples = 6 * padSampleRate;

inline float padChordSample (int sample, bool aMinor)
{
    constexpr std::array<float, 3> cMajor { 261.6256f, 329.6276f, 391.9954f };
    constexpr std::array<float, 3> aMinorNotes { 220.0f, 261.6256f, 329.6276f };
    const auto& notes = aMinor ? aMinorNotes : cMajor;
    const auto time = static_cast<float> (sample) / padSampleRate;
    float output = 0;
    for (const auto frequency : notes)
    {
        const auto phase = 6.283185307179586f * frequency * time;
        // Detuned oscillators and harmonics make a sustained pad without a level jump at the chord change.
        output += 0.065f * (std::sin (phase) + 0.35f * std::sin (phase * 1.003f + 0.2f)
                            + 0.28f * std::sin (2.0f * phase + 0.4f)
                            + 0.12f * std::sin (3.0f * phase + 0.7f));
    }
    return output;
}

inline float padSample (int sample)
{
    constexpr int crossfadeSamples = padSampleRate / 8;
    const auto fade = std::clamp (static_cast<float> (sample - chordChangeSample)
                                  / crossfadeSamples, 0.0f, 1.0f);
    const auto attack = std::clamp (static_cast<float> (sample) / (padSampleRate / 5), 0.0f, 1.0f);
    return attack * ((1.0f - fade) * padChordSample (sample, false)
                     + fade * padChordSample (sample, true));
}
}
