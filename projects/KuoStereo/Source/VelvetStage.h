#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace kuo
{
// Shared by Kuo Stereo and Kuo Velvet: spectral onset protection feeds a
// deterministic sparse convolution whose opposite stereo additions preserve mid.
// prepare() allocates; setParameters() and process() are audio-thread only.
class VelvetStage
{
public:
    struct Result { float side; bool onset, instant; };
    void prepare (float rate, std::array<float, 3> initialBands, bool enableBandLevels = true)
    {
        sampleRate = rate;
        bandLevelsEnabled = enableBandLevels;
        lookaheadSamples = juce::roundToInt (0.0015f * sampleRate);
        velvetHistory.assign (static_cast<size_t> (std::ceil (sampleRate * 0.04f)) + 2052, 0.0f);
        writePosition = 0;
        detectorLowStates = {}; detectorFastEnvelopes = {}; detectorSlowEnvelopes = {};
        velvetBandLowStates = {}; velvetBandGains = initialBands;
        onsetActive = instantTransientOnset = false;
        velvetGuardEnvelope = widthBlend = 0;
        const auto coefficient = [this] (float hz)
        { return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / sampleRate); };
        fastCoefficient = coefficient (160.0f);
        slowCoefficient = coefficient (8.0f);
        constexpr std::array<float, 3> detectorCrossovers { 250, 2500, 8000 };
        for (size_t band = 0; band < detectorCoefficients.size(); ++band)
            detectorCoefficients[band] = coefficient (detectorCrossovers[band]);
        auto random = 0x4b554f31u;
        int pairSign = 1;
        for (int i = 0; i < velvetTaps; ++i)
        {
            random = random * 1664525u + 1013904223u;
            const auto jitter = static_cast<float> (random % 1000u) / 1000.0f - 0.5f;
            const auto timeMs = 4.0f + 24.0f * (static_cast<float> (i) + 0.5f + jitter * 0.55f)
                                                / static_cast<float> (velvetTaps);
            tapOffsets[static_cast<size_t> (i)] = juce::roundToInt (timeMs * 0.001f * sampleRate);
            if (i % 2 == 0) pairSign = (random & 0x10000u) != 0 ? 1 : -1;
            tapWeights[static_cast<size_t> (i)] = static_cast<float> (i % 2 == 0 ? pairSign : -pairSign)
                                                   / std::sqrt (static_cast<float> (velvetTaps));
        }
    }
    int getLatency() const noexcept { return lookaheadSamples; }
    void setParameters (float width, float transient, float guardAttackMs,
                        std::array<float, 3> bands) noexcept
    {
        widthTarget = width;
        protection = transient;
        if (bandLevelsEnabled)
        {
            velvetBandTargets = bands;
            bandGainCoefficient = 1.0f - std::exp (-1.0f / (0.010f * sampleRate));
            velvetLowCoefficient = 1.0f - std::exp (
                -juce::MathConstants<float>::twoPi * 250.0f / sampleRate);
            velvetUpperCoefficient = 1.0f - std::exp (
                -juce::MathConstants<float>::twoPi * 2500.0f / sampleRate);
        }
        tonalGuardAttackStep = 1.0f
            / (std::max (guardAttackMs, 0.5f) * 0.001f * sampleRate);
        transientGuardAttackStep = guardAttackMs <= 0.0f ? 1.0f
            : 1.0f / (guardAttackMs * 0.001f * sampleRate);
    }
    Result process (float mid) noexcept
    {
        for (size_t band = 0; band < detectorLowStates.size(); ++band)
            detectorLowStates[band] += detectorCoefficients[band] * (mid - detectorLowStates[band]);
        const std::array<float, 4> spectralBands {
            detectorLowStates[0],
            detectorLowStates[1] - detectorLowStates[0],
            detectorLowStates[2] - detectorLowStates[1],
            mid - detectorLowStates[2]
        };
        bool onset = false;
        bool strongSpectralRise = false;
        float onsetStrength = 0.0f;
        float fastEnvelopeSum = 0.0f;
        float slowEnvelopeSum = 0.0f;
        for (size_t band = 0; band < spectralBands.size(); ++band)
        {
            const auto level = std::abs (spectralBands[band]);
            detectorFastEnvelopes[band] += fastCoefficient * (level - detectorFastEnvelopes[band]);
            detectorSlowEnvelopes[band] += slowCoefficient * (level - detectorSlowEnvelopes[band]);
            // Positive per-band spectral change detects attacks even when total level stays steady.
            const auto fast = detectorFastEnvelopes[band];
            const auto slow = detectorSlowEnvelopes[band];
            fastEnvelopeSum += fast;
            slowEnvelopeSum += slow;
            const auto bandOnset = fast > 0.004f && fast > slow * 3.5f;
            onset |= bandOnset;
            strongSpectralRise |= fast > 0.018f && fast > slow * 3.5f;
            if (bandOnset)
            {
                // Fade near both onset thresholds so detector chatter cannot switch
                // the convolution input abruptly, even with a short Attack.
                const auto ratioStrength = std::clamp ((fast - slow * 3.5f)
                    / (0.004f + slow * 0.5f), 0.0f, 1.0f);
                const auto levelStrength = std::clamp ((fast - 0.004f) / 0.004f, 0.0f, 1.0f);
                const auto strength = ratioStrength * levelStrength;
                onsetStrength = std::max (onsetStrength, strength * strength * (3.0f - 2.0f * strength));
            }
        }
        // An isolated band fluctuation is insufficient: require a strong,
        // fast rise across the mid spectrum as well.
        onset = onset && fastEnvelopeSum > 0.015f
            && (fastEnvelopeSum > slowEnvelopeSum * 1.5f || strongSpectralRise);
        if (! onset) onsetStrength = 0.0f;
        if (onset && ! onsetActive)
            instantTransientOnset = std::abs (mid) > 25.0f * (fastEnvelopeSum + 0.001f);
        onsetActive = onset;
        if (onset && instantTransientOnset) onsetStrength = 1.0f;
        // Sharp transients use the chosen attack (including immediate 0 ms);
        // tonal onsets get at least a 0.5 ms de-click ramp.
        const auto attackStep = instantTransientOnset
            ? transientGuardAttackStep : tonalGuardAttackStep;
        velvetGuardEnvelope = onset ? std::min (1.0f, velvetGuardEnvelope + attackStep) : 0.0f;
        const auto velvetInput = mid * (1.0f - protection * velvetGuardEnvelope * onsetStrength);
        // Standalone Kuo Velvet feeds the full-band signal directly to the
        // convolution; Kuo Stereo retains its adjustable three-band input.
        auto velvetBands = velvetInput;
        if (bandLevelsEnabled)
        {
            velvetBandLowStates[0] += velvetLowCoefficient
                * (velvetInput - velvetBandLowStates[0]);
            velvetBandLowStates[1] += velvetUpperCoefficient
                * (velvetInput - velvetBandLowStates[1]);
            for (size_t band = 0; band < velvetBandGains.size(); ++band)
                velvetBandGains[band] += bandGainCoefficient
                    * (velvetBandTargets[band] - velvetBandGains[band]);
            velvetBands = velvetBandGains[0] * velvetBandLowStates[0]
                + velvetBandGains[1] * (velvetBandLowStates[1] - velvetBandLowStates[0])
                + velvetBandGains[2] * (velvetInput - velvetBandLowStates[1]);
        }
        velvetHistory[static_cast<size_t> (writePosition)] = velvetBands;
        writePosition = (writePosition + 1) % static_cast<int> (velvetHistory.size());
        widthBlend += (widthTarget - widthBlend) * 0.001f;
        float velvetLeft = 0, velvetRight = 0;
        for (int tap = 0; tap < velvetTaps; ++tap)
        {
            const auto tapSignal = tapWeights[static_cast<size_t> (tap)]
                * readAgo (velvetHistory,
                    lookaheadSamples + tapOffsets[static_cast<size_t> (tap)]);
            if ((tap & 1) == 0) velvetLeft += tapSignal;
            else velvetRight -= tapSignal;
        }
        // Cross-subtraction makes Velvet's additions exactly opposite, so it
        // changes the side while leaving the incoming mid untouched.
        const auto velvetSide = 0.5f * widthBlend * (velvetLeft - velvetRight);
        return { velvetSide, onset, instantTransientOnset };
    }
private:
    float readAgo (const std::vector<float>& history, int samplesAgo) const noexcept
    {
        auto index = writePosition - 1 - samplesAgo;
        if (index < 0) index += static_cast<int> (history.size());
        return history[static_cast<size_t> (index)];
    }
    static constexpr int velvetTaps = 16;
    std::array<int, velvetTaps> tapOffsets {};
    std::array<float, velvetTaps> tapWeights {};
    std::vector<float> velvetHistory;
    int writePosition = 0, lookaheadSamples = 0;
    float sampleRate = 48000, widthBlend = 0, velvetGuardEnvelope = 0;
    float widthTarget = 0, protection = 0, bandGainCoefficient = 0;
    float velvetLowCoefficient = 0, velvetUpperCoefficient = 0;
    float tonalGuardAttackStep = 1, transientGuardAttackStep = 1;
    bool onsetActive = false, instantTransientOnset = false, bandLevelsEnabled = true;
    std::array<float, 3> detectorLowStates {}, detectorCoefficients {}, velvetBandTargets {};
    std::array<float, 4> detectorFastEnvelopes {}, detectorSlowEnvelopes {};
    std::array<float, 2> velvetBandLowStates {};
    std::array<float, 3> velvetBandGains { 1, 1, 1 };
    float fastCoefficient = 0, slowCoefficient = 0;
};
}
