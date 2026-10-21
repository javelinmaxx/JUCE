#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout KuoStereoProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    const auto add = [&] (const char* id, const char* name, float lo, float hi, float step,
                          float initial, const char* unit)
    {
        result.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { lo, hi, step }, initial,
            juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    add ("haasCenter", "Haas center delay", -30, 30, 0.1f, 12, "ms");
    add ("haasRange", "Haas variability", 0, 12, 0.1f, 6, "ms");
    add ("haasRate", "Haas maximum variation rate", 0.1f, 10, 0.1f, 1, "ms/s");
    add ("haasKeepTransient", "Dynamic", 0, 100, 0.1f, 0, "%");
    add ("width", "Velvet width", 0, 100, 0.1f, 35, "%");
    add ("transientProtect", "Transient", 0, 100, 0.1f, 80, "%");
    add ("guardAttack", "Attack", 0, 10, 0.1f, 0, "ms");
    add ("velvetLow", "Velvet low band", 0, 200, 0.1f, 100, "%");
    add ("velvetMid", "Velvet mid band", 0, 200, 0.1f, 100, "%");
    add ("velvetHigh", "Velvet high band", 0, 200, 0.1f, 100, "%");
    add ("output", "Output", -12, 12, 0.1f, 0, "dB");
    result.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 },
        "Bypass", false));
    return result;
}

KuoStereoProcessor::KuoStereoProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "KuoStereoState", layout())
{
}

void KuoStereoProcessor::prepareToPlay (double rate, int)
{
    sampleRate = static_cast<float> (std::max (rate, 1000.0));
    lookaheadSamples = juce::roundToInt (0.0015f * sampleRate);
    setLatencySamples (lookaheadSamples);
    const auto historySize = static_cast<size_t> (std::ceil (sampleRate * 0.04f)) + fftSize + 4;
    leftHistory.assign (historySize, 0);
    rightHistory.assign (historySize, 0);
    velvetHistory.assign (historySize, 0);
    writePosition = sampleCount = analysisCounter = scopeCounter = transientHold = 0;
    selectedCandidate = -1;
    previousCenterDelayMs = previousVariabilityMs = -1;
    smoothedCandidateScores = {};
    candidateScoreInitialized = {};
    lowState = upperState = {};
    detectorLowStates = {};
    detectorFastEnvelopes = detectorSlowEnvelopes = {};
    velvetBandLowStates = {};
    velvetBandGains = {
        parameters.getRawParameterValue ("velvetLow")->load() * 0.01f,
        parameters.getRawParameterValue ("velvetMid")->load() * 0.01f,
        parameters.getRawParameterValue ("velvetHigh")->load() * 0.01f
    };
    onsetActive = instantTransientOnset = false;
    haasTransientEnvelope = haasAttackStartEnvelope = 0;
    haasAttackProgress = 1;
    velvetGuardEnvelope = 0;
    widthBlend = 0;
    const auto centerDelayMs = parameters.getRawParameterValue ("haasCenter")->load();
    targetDelaySamples = smoothDelaySamples = centerDelayMs * 0.001f * sampleRate;
    selectedDelayMs.store (centerDelayMs, std::memory_order_relaxed);
    currentDelayMs.store (centerDelayMs, std::memory_order_relaxed);
    const auto coefficient = [this] (float hz)
    { return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / sampleRate); };
    lowCoefficient = coefficient (250.0f);
    upperCoefficient = coefficient (2500.0f);
    fastCoefficient = coefficient (160.0f);
    slowCoefficient = coefficient (8.0f);
    constexpr std::array<float, 3> detectorCrossovers { 250, 2500, 8000 };
    for (size_t band = 0; band < detectorCoefficients.size(); ++band)
        detectorCoefficients[band] = coefficient (detectorCrossovers[band]);
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (
            juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (fftSize - 1));
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
    for (auto& band : scope)
        for (auto& point : band)
        {
            point.left.store (0, std::memory_order_relaxed);
            point.right.store (0, std::memory_order_relaxed);
        }
    scopeWrite.store (0, std::memory_order_relaxed);
}

bool KuoStereoProcessor::isBusesLayoutSupported (const BusesLayout& buses) const
{
    return buses.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && buses.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

float KuoStereoProcessor::readAgo (const std::vector<float>& history, int samplesAgo) const noexcept
{
    const auto size = static_cast<int> (history.size());
    auto index = writePosition - 1 - samplesAgo;
    if (index < 0) index += size;
    return history[static_cast<size_t> (index)];
}

float KuoStereoProcessor::readAgoInterpolated (const std::vector<float>& history,
                                                float samplesAgo) const noexcept
{
    const auto whole = static_cast<int> (samplesAgo);
    const auto fraction = samplesAgo - static_cast<float> (whole);
    return readAgo (history, whole) * (1.0f - fraction)
         + readAgo (history, whole + 1) * fraction;
}

void KuoStereoProcessor::analyzeMidSpectrum (float centerDelayMs, float variabilityMs) noexcept
{
    const auto lowDelayMs = std::max (-30.0f, centerDelayMs - variabilityMs);
    const auto highDelayMs = std::min (30.0f, centerDelayMs + variabilityMs);
    if (std::abs (centerDelayMs - previousCenterDelayMs) > 0.0001f
        || std::abs (variabilityMs - previousVariabilityMs) > 0.0001f)
    {
        // Automation moves the grid, so old scores no longer describe its choices.
        selectedCandidate = -1;
        candidateScoreInitialized = {};
        previousCenterDelayMs = centerDelayMs;
        previousVariabilityMs = variabilityMs;
    }
    if (sampleCount < static_cast<int> (sampleRate * 0.03f) + fftSize
        || highDelayMs - lowDelayMs < 0.0001f)
    {
        targetDelaySamples = centerDelayMs * 0.001f * sampleRate;
        selectedDelayMs.store (centerDelayMs, std::memory_order_relaxed);
        return;
    }

    const auto firstTenth = static_cast<int> (std::ceil (lowDelayMs * 10.0f - 0.0001f));
    const auto lastTenth = static_cast<int> (std::floor (highDelayMs * 10.0f + 0.0001f));
    const auto candidateCount = lastTenth - firstTenth + 1;
    if (candidateCount <= 0 || candidateCount > maxCandidates)
    {
        targetDelaySamples = centerDelayMs * 0.001f * sampleRate;
        selectedDelayMs.store (centerDelayMs, std::memory_order_relaxed);
        return;
    }
    const auto candidateDelayMs = [=] (int candidate) noexcept
    { return 0.1f * static_cast<float> (firstTenth + candidate); };

    std::fill (spectrumInput.begin(), spectrumInput.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
    {
        const auto ago = fftSize - 1 - i;
        spectrumInput[static_cast<size_t> (i)] = 0.5f
            * (readAgo (leftHistory, ago) + readAgo (rightHistory, ago))
            * window[static_cast<size_t> (i)];
    }
    fft.performFrequencyOnlyForwardTransform (spectrumInput.data());
    float referenceLevel = 0;
    for (int bin = 2; bin < fftSize / 2; ++bin)
        referenceLevel += spectrumInput[static_cast<size_t> (bin)] / fftSize;
    if (referenceLevel < 0.001f)
    {
        selectedCandidate = -1;
        candidateScoreInitialized = {};
        targetDelaySamples = centerDelayMs * 0.001f * sampleRate;
        selectedDelayMs.store (centerDelayMs, std::memory_order_relaxed);
        return;
    }

    constexpr float switchMargin = 0.98f;
    const auto scoreAlpha = 1.0f - std::exp (-static_cast<float> (fftSize / 2)
                                             / (0.30f * sampleRate));
    std::array<bool, maxCandidates> scored {};
    const auto scoreCandidate = [&] (int candidate) noexcept
    {
        auto& average = smoothedCandidateScores[static_cast<size_t> (candidate)];
        if (scored[static_cast<size_t> (candidate)]) return average;
        scored[static_cast<size_t> (candidate)] = true;
        const auto delaySamples = candidateDelayMs (candidate) * 0.001f * sampleRate;
        const auto leftDelay = std::max (-delaySamples, 0.0f);
        const auto rightDelay = std::max (delaySamples, 0.0f);
        std::fill (spectrumCandidate.begin(), spectrumCandidate.end(), 0.0f);
        float sideEnergy = 0, midEnergy = 0;
        for (int i = 0; i < fftSize; ++i)
        {
            const auto ago = static_cast<float> (fftSize - 1 - i);
            const auto candidateLeft = readAgoInterpolated (leftHistory, ago + leftDelay);
            const auto candidateRight = readAgoInterpolated (rightHistory, ago + rightDelay);
            const auto candidateMid = 0.5f * (candidateLeft + candidateRight);
            const auto candidateSide = 0.5f * (candidateLeft - candidateRight);
            const auto weight = window[static_cast<size_t> (i)];
            spectrumCandidate[static_cast<size_t> (i)] = candidateMid * weight;
            midEnergy += candidateMid * candidateMid * weight;
            sideEnergy += candidateSide * candidateSide * weight;
        }
        fft.performFrequencyOnlyForwardTransform (spectrumCandidate.data());
        float score = 0, weightSum = 0;
        for (int bin = 2; bin < fftSize / 2; ++bin)
        {
            const auto reference = spectrumInput[static_cast<size_t> (bin)] / fftSize;
            const auto processed = spectrumCandidate[static_cast<size_t> (bin)] / fftSize;
            const auto weight = reference / (reference + 0.01f);
            score += weight * std::abs (std::log ((processed + 0.0001f) / (reference + 0.0001f)));
            weightSum += weight;
        }
        score /= std::max (weightSum, 1.0f);
        // Zero delay has perfect mid preservation; require useful side energy too.
        constexpr float targetSideRatio = 0.20f;
        const auto sideRatio = std::sqrt (sideEnergy / std::max (midEnergy, 0.000001f));
        score += 0.35f * std::max (0.0f, 1.0f - sideRatio / targetSideRatio);
        auto& initialized = candidateScoreInitialized[static_cast<size_t> (candidate)];
        average = initialized ? average + scoreAlpha * (score - average) : score;
        initialized = true;
        return average;
    };

    float bestScore = std::numeric_limits<float>::max();
    int bestCandidate = 0, coarseBest = 0;
    float coarseBestScore = bestScore;
    const auto consider = [&] (int candidate)
    {
        const auto average = scoreCandidate (candidate);
        if (average < bestScore)
        {
            bestScore = average;
            bestCandidate = candidate;
        }
        return average;
    };
    // A 0.5 ms survey locates promising regions, then 0.1 ms choices are
    // scored with the original windowed FFT near both the best and current time.
    for (int candidate = 0; candidate < candidateCount; candidate += 5)
    {
        const auto average = consider (candidate);
        if (average < coarseBestScore)
        {
            coarseBestScore = average;
            coarseBest = candidate;
        }
    }
    const auto lastAverage = consider (candidateCount - 1);
    if (lastAverage < coarseBestScore)
        coarseBest = candidateCount - 1;
    for (int candidate = std::max (0, coarseBest - 5);
         candidate <= std::min (candidateCount - 1, coarseBest + 5); ++candidate)
        consider (candidate);
    if (selectedCandidate >= 0)
        for (int candidate = std::max (0, selectedCandidate - 5);
             candidate <= std::min (candidateCount - 1, selectedCandidate + 5); ++candidate)
            consider (candidate);
    if (selectedCandidate < 0
        || bestScore < switchMargin * smoothedCandidateScores[static_cast<size_t> (selectedCandidate)])
        selectedCandidate = bestCandidate;
    const auto bestDelayMs = candidateDelayMs (selectedCandidate);
    targetDelaySamples = bestDelayMs * 0.001f * sampleRate;
    selectedDelayMs.store (bestDelayMs, std::memory_order_relaxed);
}

void KuoStereoProcessor::pushScope (float left, float right) noexcept
{
    const std::array<float, 2> values { left, right };
    std::array<std::array<float, 2>, 3> bands {};
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto input = values[static_cast<size_t> (channel)];
        lowState[static_cast<size_t> (channel)] += lowCoefficient
            * (input - lowState[static_cast<size_t> (channel)]);
        upperState[static_cast<size_t> (channel)] += upperCoefficient
            * (input - upperState[static_cast<size_t> (channel)]);
        bands[0][static_cast<size_t> (channel)] = lowState[static_cast<size_t> (channel)];
        bands[1][static_cast<size_t> (channel)] = upperState[static_cast<size_t> (channel)]
                                                   - lowState[static_cast<size_t> (channel)];
        bands[2][static_cast<size_t> (channel)] = input - upperState[static_cast<size_t> (channel)];
    }
    if (++scopeCounter < 23) return;
    scopeCounter = 0;
    const auto index = scopeWrite.load (std::memory_order_relaxed) % scopePoints;
    for (int band = 0; band < 3; ++band)
    {
        auto& point = scope[static_cast<size_t> (band)][static_cast<size_t> (index)];
        point.left.store (bands[static_cast<size_t> (band)][0], std::memory_order_relaxed);
        point.right.store (bands[static_cast<size_t> (band)][1], std::memory_order_relaxed);
    }
    scopeWrite.store (index + 1, std::memory_order_release);
}

void KuoStereoProcessor::readScope (int band,
    std::array<juce::Point<float>, scopePoints>& output) const noexcept
{
    if (band < 0 || band >= 3) return;
    const auto current = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < scopePoints; ++i)
    {
        const auto index = (current + i) % scopePoints;
        const auto& point = scope[static_cast<size_t> (band)][static_cast<size_t> (index)];
        output[static_cast<size_t> (i)] = { point.left.load (std::memory_order_relaxed),
                                           point.right.load (std::memory_order_relaxed) };
    }
}

void KuoStereoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2 || rightHistory.empty()) return;
    const auto centerDelay = parameters.getRawParameterValue ("haasCenter")->load();
    const auto variability = parameters.getRawParameterValue ("haasRange")->load();
    const auto maxRate = parameters.getRawParameterValue ("haasRate")->load();
    const auto maxDelayStep = maxRate * 0.001f; // Samples per sample equals ms per second.
    const auto keepTransient = parameters.getRawParameterValue ("haasKeepTransient")->load() * 0.01f;
    // After 65 ms of release, transient protection has fallen to 1% (-40 dB).
    const auto transientReleaseCoefficient = std::exp (std::log (0.01f) / (0.065f * sampleRate));
    const auto widthTarget = parameters.getRawParameterValue ("width")->load() * 0.01f;
    const auto protection = parameters.getRawParameterValue ("transientProtect")->load() * 0.01f;
    const std::array<float, 3> velvetBandTargets {
        parameters.getRawParameterValue ("velvetLow")->load() * 0.01f,
        parameters.getRawParameterValue ("velvetMid")->load() * 0.01f,
        parameters.getRawParameterValue ("velvetHigh")->load() * 0.01f
    };
    const auto bandGainCoefficient = 1.0f - std::exp (-1.0f / (0.010f * sampleRate));
    const auto velvetLowCoefficient = 1.0f - std::exp (
        -juce::MathConstants<float>::twoPi * 250.0f / sampleRate);
    const auto velvetUpperCoefficient = 1.0f - std::exp (
        -juce::MathConstants<float>::twoPi * 2500.0f / sampleRate);
    const auto guardAttackMs = parameters.getRawParameterValue ("guardAttack")->load();
    const auto tonalGuardAttackStep = 1.0f
        / (std::max (guardAttackMs, 0.5f) * 0.001f * sampleRate);
    const auto transientGuardAttackStep = guardAttackMs <= 0.0f ? 1.0f
        : 1.0f / (guardAttackMs * 0.001f * sampleRate);
    const auto gain = juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("output")->load());
    const auto bypass = parameters.getRawParameterValue ("bypass")->load() >= 0.5f;
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto dryLeft = left[i], dryRight = right[i];
        const auto mid = 0.5f * (dryLeft + dryRight);
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
        if (onset)
            // Include the audio lookahead so protection lasts 10 ms after
            // the detected onset reaches the output, not just the detector.
            transientHold = juce::roundToInt (0.010f * sampleRate) + lookaheadSamples;
        else if (transientHold > 0)
            --transientHold;
        if (onset && ! onsetActive)
        {
            instantTransientOnset = std::abs (mid) > 25.0f * (fastEnvelopeSum + 0.001f);
            haasAttackStartEnvelope = haasTransientEnvelope;
            haasAttackProgress = 0.0f;
        }
        onsetActive = onset;
        if (onset && instantTransientOnset)
            onsetStrength = 1.0f;
        // Sharp transients use the chosen attack (including immediate 0 ms);
        // tonal onsets get at least a 0.5 ms de-click ramp.
        const auto attackStep = instantTransientOnset
            ? transientGuardAttackStep : tonalGuardAttackStep;
        velvetGuardEnvelope = onset ? std::min (1.0f, velvetGuardEnvelope + attackStep) : 0.0f;
        const auto velvetInput = mid * (1.0f - protection * velvetGuardEnvelope * onsetStrength);
        velvetBandLowStates[0] += velvetLowCoefficient
            * (velvetInput - velvetBandLowStates[0]);
        velvetBandLowStates[1] += velvetUpperCoefficient
            * (velvetInput - velvetBandLowStates[1]);
        for (size_t band = 0; band < velvetBandGains.size(); ++band)
            velvetBandGains[band] += bandGainCoefficient
                * (velvetBandTargets[band] - velvetBandGains[band]);
        const auto velvetBands = velvetBandGains[0] * velvetBandLowStates[0]
            + velvetBandGains[1] * (velvetBandLowStates[1] - velvetBandLowStates[0])
            + velvetBandGains[2] * (velvetInput - velvetBandLowStates[1]);
        leftHistory[static_cast<size_t> (writePosition)] = dryLeft;
        rightHistory[static_cast<size_t> (writePosition)] = dryRight;
        velvetHistory[static_cast<size_t> (writePosition)] = velvetBands;
        writePosition = (writePosition + 1) % static_cast<int> (rightHistory.size());
        if (sampleCount < static_cast<int> (sampleRate * 0.03f) + fftSize)
            ++sampleCount;
        if (++analysisCounter >= fftSize / 2)
        {
            analysisCounter = 0;
            analyzeMidSpectrum (centerDelay, variability);
        }

        // Crossfade tonal onsets over 1 ms so the delayed and transient taps
        // cannot exchange energy in a single sample. Sharp impulses stay aligned.
        if (transientHold > 0)
        {
            if (instantTransientOnset)
                haasTransientEnvelope = 1.0f;
            else
            {
                haasAttackProgress = std::min (1.0f, haasAttackProgress
                    + 1.0f / (0.001f * sampleRate));
                const auto shaped = haasAttackProgress * haasAttackProgress
                    * (3.0f - 2.0f * haasAttackProgress);
                haasTransientEnvelope = haasAttackStartEnvelope
                    + (1.0f - haasAttackStartEnvelope) * shaped;
            }
        }
        else
            haasTransientEnvelope *= transientReleaseCoefficient;
        // Move at a constant rate toward the selected time, without overshoot.
        smoothDelaySamples += std::clamp (targetDelaySamples - smoothDelaySamples,
                                        -maxDelayStep, maxDelayStep);
        widthBlend += (widthTarget - widthBlend) * 0.001f;
        const auto leftDelaySamples = std::max (-smoothDelaySamples, 0.0f);
        const auto rightDelaySamples = std::max (smoothDelaySamples, 0.0f);
        // The detector reads the current input while all audible paths wait
        // 1.5 ms, giving the Haas attack crossfade time to finish first.
        const auto dryOutputLeft = readAgo (leftHistory, lookaheadSamples);
        const auto dryOutputRight = readAgo (rightHistory, lookaheadSamples);
        const auto regularLeft = readAgoInterpolated (leftHistory,
            static_cast<float> (lookaheadSamples) + leftDelaySamples);
        const auto regularRight = readAgoInterpolated (rightHistory,
            static_cast<float> (lookaheadSamples) + rightDelaySamples);
        auto delayedLeft = regularLeft;
        auto delayedRight = regularRight;
        if (keepTransient > 0.0f && haasTransientEnvelope > 0.0f)
        {
            // Crossfade between fixed taps on release instead of sweeping the read position.
            if (smoothDelaySamples < 0.0f)
            {
                const auto transientLeft = readAgoInterpolated (leftHistory,
                    static_cast<float> (lookaheadSamples)
                        + leftDelaySamples * (1.0f - keepTransient));
                delayedLeft += haasTransientEnvelope * (transientLeft - regularLeft);
            }
            else
            {
                const auto transientRight = readAgoInterpolated (rightHistory,
                    static_cast<float> (lookaheadSamples)
                        + rightDelaySamples * (1.0f - keepTransient));
                delayedRight += haasTransientEnvelope * (transientRight - regularRight);
            }
        }
        float velvet = 0;
        for (int tap = 0; tap < velvetTaps; ++tap)
            velvet += tapWeights[static_cast<size_t> (tap)]
                    * readAgo (velvetHistory,
                        lookaheadSamples + tapOffsets[static_cast<size_t> (tap)]);
        const auto widenedSide = 0.5f * (delayedLeft - delayedRight)
                               + 0.5f * widthBlend * velvet;
        const auto processedMid = 0.5f * (delayedLeft + delayedRight);
        left[i] = bypass ? dryOutputLeft : (processedMid + widenedSide) * gain;
        right[i] = bypass ? dryOutputRight : (processedMid - widenedSide) * gain;
        pushScope (left[i], right[i]);
    }
    currentDelayMs.store (smoothDelaySamples * (1.0f - keepTransient * haasTransientEnvelope)
                          * 1000.0f / sampleRate, std::memory_order_relaxed);
}

void KuoStereoProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, data);
}

void KuoStereoProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* KuoStereoProcessor::createEditor()
{
    return new KuoStereoEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KuoStereoProcessor();
}
