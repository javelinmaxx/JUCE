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
    add ("haasCenter", "Haas center delay", 3, 30, 0.1f, 12, "ms");
    add ("haasRange", "Haas variability", 0, 12, 0.1f, 6, "ms");
    add ("haasRate", "Haas maximum variation rate", 0.1f, 10, 0.1f, 1, "ms/s");
    add ("haasKeepTransient", "Haas keep transient", 0, 100, 0.1f, 0, "%");
    add ("width", "Velvet width", 0, 100, 0.1f, 35, "%");
    add ("transientProtect", "Transient protection", 0, 100, 0.1f, 80, "%");
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
    const auto historySize = static_cast<size_t> (std::ceil (sampleRate * 0.04f)) + fftSize + 4;
    leftHistory.assign (historySize, 0);
    rightHistory.assign (historySize, 0);
    midHistory.assign (historySize, 0);
    writePosition = sampleCount = analysisCounter = scopeCounter = transientHold = 0;
    lowState = upperState = {};
    fastEnvelope = slowEnvelope = haasTransientEnvelope = 0;
    widthBlend = 0;
    protectionGain = 1;
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
    const auto lowDelayMs = std::max (3.0f, centerDelayMs - variabilityMs);
    const auto highDelayMs = std::min (30.0f, centerDelayMs + variabilityMs);
    if (sampleCount < static_cast<int> (sampleRate * 0.03f) + fftSize)
    {
        targetDelaySamples = centerDelayMs * 0.001f * sampleRate;
        selectedDelayMs.store (centerDelayMs, std::memory_order_relaxed);
        return;
    }
    if (highDelayMs - lowDelayMs < 0.0001f)
    {
        targetDelaySamples = lowDelayMs * 0.001f * sampleRate;
        selectedDelayMs.store (lowDelayMs, std::memory_order_relaxed);
        return;
    }
    std::fill (spectrumInput.begin(), spectrumInput.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        spectrumInput[static_cast<size_t> (i)] = readAgo (midHistory, fftSize - 1 - i)
                                                 * window[static_cast<size_t> (i)];
    fft.performFrequencyOnlyForwardTransform (spectrumInput.data());
    float referenceLevel = 0;
    for (int bin = 2; bin < fftSize / 2; ++bin)
        referenceLevel += spectrumInput[static_cast<size_t> (bin)] / fftSize;
    if (referenceLevel < 0.001f)
    {
        targetDelaySamples = centerDelayMs * 0.001f * sampleRate;
        selectedDelayMs.store (centerDelayMs, std::memory_order_relaxed);
        return;
    }

    float bestScore = std::numeric_limits<float>::max();
    float bestDelayMs = centerDelayMs;
    for (int candidate = 0; candidate < candidates; ++candidate)
    {
        const auto delayMs = lowDelayMs + (highDelayMs - lowDelayMs)
                                         * static_cast<float> (candidate) / static_cast<float> (candidates - 1);
        const auto delaySamples = juce::roundToInt (delayMs * 0.001f * sampleRate);
        std::fill (spectrumCandidate.begin(), spectrumCandidate.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            const auto ago = fftSize - 1 - i;
            spectrumCandidate[static_cast<size_t> (i)] = 0.5f
                * (readAgo (leftHistory, ago) + readAgo (rightHistory, ago + delaySamples))
                * window[static_cast<size_t> (i)];
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
        if (score < bestScore)
        {
            bestScore = score;
            bestDelayMs = delayMs;
        }
    }
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
    const auto transientReleaseStep = 1.0f / (0.05f * sampleRate);
    const auto widthTarget = parameters.getRawParameterValue ("width")->load() * 0.01f;
    const auto protection = parameters.getRawParameterValue ("transientProtect")->load() * 0.01f;
    const auto gain = juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("output")->load());
    const auto bypass = parameters.getRawParameterValue ("bypass")->load() >= 0.5f;
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto dryLeft = left[i], dryRight = right[i];
        const auto mid = 0.5f * (dryLeft + dryRight);
        leftHistory[static_cast<size_t> (writePosition)] = dryLeft;
        rightHistory[static_cast<size_t> (writePosition)] = dryRight;
        midHistory[static_cast<size_t> (writePosition)] = mid;
        writePosition = (writePosition + 1) % static_cast<int> (rightHistory.size());
        if (sampleCount < static_cast<int> (sampleRate * 0.03f) + fftSize)
            ++sampleCount;
        if (++analysisCounter >= fftSize / 2)
        {
            analysisCounter = 0;
            analyzeMidSpectrum (centerDelay, variability);
        }

        fastEnvelope += fastCoefficient * (std::abs (mid) - fastEnvelope);
        slowEnvelope += slowCoefficient * (std::abs (mid) - slowEnvelope);
        if (fastEnvelope > 0.02f && fastEnvelope > slowEnvelope * 1.8f)
            transientHold = juce::roundToInt (0.032f * sampleRate);
        else if (transientHold > 0)
            --transientHold;
        // The same onset and hold signal protects Velvet Width and temporarily shortens Haas.
        if (transientHold > 0)
            haasTransientEnvelope = 1.0f;
        else
            haasTransientEnvelope = std::max (0.0f, haasTransientEnvelope - transientReleaseStep);
        const auto protectionTarget = transientHold > 0 ? 1.0f - protection : 1.0f;
        protectionGain += (protectionTarget - protectionGain)
            * (protectionTarget < protectionGain ? 0.1f : 0.001f);
        smoothDelaySamples += std::clamp (targetDelaySamples - smoothDelaySamples,
                                         -maxDelayStep, maxDelayStep);
        widthBlend += (widthTarget - widthBlend) * 0.001f;
        const auto regularRight = readAgoInterpolated (rightHistory, smoothDelaySamples);
        auto delayedRight = regularRight;
        if (keepTransient > 0.0f && haasTransientEnvelope > 0.0f)
        {
            const auto transientDelaySamples = smoothDelaySamples * (1.0f - keepTransient);
            const auto transientRight = readAgoInterpolated (rightHistory, transientDelaySamples);
            // Crossfade between fixed taps on release instead of sweeping the read position.
            delayedRight += haasTransientEnvelope * (transientRight - regularRight);
        }
        float velvet = 0;
        for (int tap = 0; tap < velvetTaps; ++tap)
            velvet += tapWeights[static_cast<size_t> (tap)]
                    * readAgo (midHistory, tapOffsets[static_cast<size_t> (tap)]);
        const auto widenedSide = 0.5f * (dryLeft - delayedRight)
                               + 0.5f * widthBlend * protectionGain * velvet;
        const auto processedMid = 0.5f * (dryLeft + delayedRight);
        left[i] = bypass ? dryLeft : (processedMid + widenedSide) * gain;
        right[i] = bypass ? dryRight : (processedMid - widenedSide) * gain;
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
