#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Keep this order aligned with raw[] and the positional reads in process().
// The IDs themselves are persisted in host automation and saved sessions.
constexpr std::array<const char*, 14> parameterIds { "mode", "frequency", "q", "attenuationFrequency",
    "attenuationQ", "threshold", "depth", "attack", "release", "output", "stereoLink", "bypass", "listen", "smart" };
constexpr float pi = 3.14159265358979323846f;
}

juce::AudioProcessorValueTreeState::ParameterLayout ShunDeEssProcessor::createParameterLayout()
{
    using Float = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "mode", 1 }, "Split-band", false));
    const auto addFloat = [&] (const char* id, const char* title, float minimum, float maximum,
                               float interval, float initial, float centre, const char* unit)
    {
        juce::NormalisableRange<float> range (minimum, maximum, interval);
        if (centre > minimum && centre < maximum)
            range.setSkewForCentre (centre);
        layout.add (std::make_unique<Float> (juce::ParameterID { id, 1 }, title, range, initial,
            juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    addFloat ("frequency", "Detector frequency", 2000, 12000, 10, 6500, 6000, "Hz");
    addFloat ("q", "Detector bandwidth", 0.5f, 4, 0.01f, 1.4f, 1.2f, "Q");
    addFloat ("attenuationFrequency", "Attenuation frequency", 2000, 16000, 10, 6500, 7000, "Hz");
    addFloat ("attenuationQ", "Attenuation Q", 0.5f, 4, 0.01f, 1.2f, 1.2f, "Q");
    addFloat ("threshold", "Threshold", -60, 0, 0.1f, -28, -30, "dB");
    addFloat ("depth", "Maximum reduction", 0, 18, 0.1f, 8, 8, "dB");
    addFloat ("attack", "Attack", 0.1f, 20, 0.01f, 1, 2, "ms");
    addFloat ("release", "Release", 10, 500, 1, 80, 100, "ms");
    addFloat ("output", "Output", -18, 18, 0.1f, 0, 0, "dB");
    addFloat ("stereoLink", "Stereo link", 0, 100, 1, 100, 50, "%");
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "listen", 1 }, "Listen", false));
    // Keep the legacy IDs: mode=0/1 selects Wideband/Split or Brilliant/Smooth,
    // while smart=0/1 selects the conventional or spectral pair.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "smart", 1 }, "Brilliant / Smooth", false));
    return layout;
}

ShunDeEssProcessor::ShunDeEssProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ShunDeEssState", createParameterLayout())
{
    for (size_t i = 0; i < raw.size(); ++i)
        raw[i] = parameters.getRawParameterValue (parameterIds[i]);
}

void ShunDeEssProcessor::prepareToPlay (double rate, int)
{
    sampleRate = std::isfinite (rate) && rate >= 8000 ? static_cast<float> (rate) : 44100.0f;
    detectorState = {};
    shelfState = {};
    bellState = {};
    envelopes = {};
    analysisRing = {}; meanSpectrum = {}; sparkleStates = {}; sparkleWeights = {};
    analysisPosition = hopCounter = 0; spectralFlatness = spectralAttenuation = 0;
    lastDetectorFrequency = lastDetectorQ = lastAttenuationFrequency = lastAttenuationQ = lastDepth = -1;
    reduction.store (0, std::memory_order_relaxed);
}

void ShunDeEssProcessor::analyseSpectralDetail (float detectorFrequency, float detectorQ,
                                                float attenuationQ) noexcept
{
    constexpr int fftSize = 1024;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto source = analysisRing[static_cast<size_t> ((analysisPosition + i) % fftSize)];
        const auto window = 0.5f - 0.5f * std::cos (2.0f * pi * static_cast<float> (i) / (fftSize - 1));
        fftData[static_cast<size_t> (i)] = source * window;
    }
    spectralFft.performFrequencyOnlyForwardTransform (fftData.data());
    // Detector Q controls temporal smoothing of each FFT bin: higher Q tracks
    // changes faster, so short spectral peaks can stand out from the baseline.
    const auto smoothing = std::clamp (0.92f - (detectorQ - 0.5f) * 0.15f, 0.35f, 0.92f);
    float arithmetic = 0, logarithmic = 0, weightSum = 0;
    std::array<float, 513> magnitudes {};
    for (int bin = 1; bin <= 512; ++bin)
    {
        const auto hz = static_cast<float> (bin) * sampleRate / fftSize;
        const auto magnitude = fftData[static_cast<size_t> (bin)];
        magnitudes[static_cast<size_t> (bin)] = magnitude;
        const auto previous = meanSpectrum[static_cast<size_t> (bin)];
        const auto mean = previous + (magnitude - previous) * (1.0f - smoothing);
        meanSpectrum[static_cast<size_t> (bin)] = mean;
        if (hz >= 2000.0f && hz <= 16000.0f)
        {
            const auto octave = std::log2 (std::max (hz, 1.0f) / detectorFrequency);
            const auto weight = std::exp (-0.5f * octave * octave / 1.5f);
            arithmetic += mean * weight;
            logarithmic += std::log (std::max (mean, 1.0e-9f)) * weight;
            weightSum += weight;
        }
    }
    const auto meanLevel = arithmetic / std::max (weightSum, 1.0f);
    // Spectral flatness is the weighted geometric mean divided by the weighted
    // arithmetic mean. Broadband sibilance approaches one; tonal peaks do not.
    spectralFlatness = std::clamp (std::exp (logarithmic / std::max (weightSum, 1.0f))
                                 / std::max (meanLevel, 1.0e-9f), 0.0f, 1.0f);
    spectralAttenuation = std::clamp (spectralFlatness * 1.8f, 0.0f, 1.0f);
    // Subtract the smoothed spectrogram from the current spectrum. The strongest
    // positive residuals identify narrow peaks to restore after broad attenuation.
    sparkleWeights.fill (0.0f);
    std::array<int, 4> chosenBins { 0, 0, 0, 0 };
    for (int pick = 0; pick < 4; ++pick)
    {
        float best = 0; int bestBin = 0;
        for (int bin = 1; bin <= 512; ++bin)
        {
            const auto hz = static_cast<float> (bin) * sampleRate / fftSize;
            if (hz < 2000 || hz > 16000) continue;
            float localFloor = 0.0f;
            int localCount = 0;
            for (int offset = -5; offset <= 5; ++offset)
                if (std::abs (offset) > 1 && bin + offset > 0 && bin + offset <= 512)
                {
                    localFloor += magnitudes[static_cast<size_t> (bin + offset)];
                    ++localCount;
                }
            // A broadband noise burst can produce a large temporal residual
            // in every bin. Restore only peaks that stand out spectrally from
            // their neighbors, so transient sibilance remains attenuated.
            if (magnitudes[static_cast<size_t> (bin)]
                < 4.0f * localFloor / static_cast<float> (std::max (localCount, 1)))
                continue;
            bool nearChosen = false;
            for (auto selected : chosenBins)
                nearChosen = nearChosen || (selected != 0 && std::abs (selected - bin) < 5);
            if (nearChosen) continue;
            const auto residual = magnitudes[static_cast<size_t> (bin)] - meanSpectrum[static_cast<size_t> (bin)];
            const auto weight = std::exp (-0.5f * std::pow (std::log2 (hz / detectorFrequency), 2.0f) / 1.5f);
            const auto score = residual * weight;
            if (score > best) { best = score; bestBin = bin; }
        }
        if (bestBin == 0 || best < meanLevel * 0.45f) break;
        chosenBins[static_cast<size_t> (pick)] = bestBin;
        const auto hz = static_cast<float> (bestBin) * sampleRate / fftSize;
        const auto omega = 2.0f * pi * hz / sampleRate;
        const auto alpha = std::sin (omega) / (2.0f * std::clamp (attenuationQ, 0.5f, 4.0f));
        auto& f = sparkleFilters[static_cast<size_t> (pick)];
        f = { alpha, 0.0f, -alpha, -2.0f * std::cos (omega), 1.0f - alpha };
        const auto inv = 1.0f / (1.0f + alpha);
        f.b0 *= inv; f.b1 *= inv; f.b2 *= inv; f.a1 *= inv; f.a2 *= inv;
        sparkleWeights[static_cast<size_t> (pick)] = std::clamp (best / std::max (meanLevel, 1.0e-9f), 0.0f, 1.0f) * 0.28f;
    }
}

void ShunDeEssProcessor::releaseResources()
{
    detectorState = {};
    shelfState = {};
    bellState = {};
    envelopes = {};
}

bool ShunDeEssProcessor::isBusesLayoutSupported (const BusesLayout& buses) const
{
    const auto input = buses.getMainInputChannelSet();
    return input == buses.getMainOutputChannelSet()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

float ShunDeEssProcessor::filterSample (float input, BiquadState& state,
                                        const BiquadCoefficients& coefficients) noexcept
{
    const auto output = coefficients.b0 * input + coefficients.b1 * state.x1 + coefficients.b2 * state.x2
                      - coefficients.a1 * state.y1 - coefficients.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = input;
    state.y2 = state.y1;
    state.y1 = std::isfinite (output) ? output : 0.0f;
    return state.y1;
}

void ShunDeEssProcessor::updateFilterCoefficients (float detectorFrequency, float detectorQ,
                                                   float attenuationFrequency, float attenuationQ,
                                                   float maximumReduction) noexcept
{
    const auto nyquistLimit = sampleRate * 0.45f;
    detectorFrequency = std::clamp (detectorFrequency, 2000.0f, std::min (12000.0f, nyquistLimit));
    attenuationFrequency = std::clamp (attenuationFrequency, 2000.0f, std::min (16000.0f, nyquistLimit));
    detectorQ = std::clamp (detectorQ, 0.5f, 4.0f);
    attenuationQ = std::clamp (attenuationQ, 0.5f, 4.0f);
    maximumReduction = std::clamp (maximumReduction, 0.0f, 18.0f);

    const auto normalize = [] (BiquadCoefficients& c, float a0)
    {
        const auto inverse = 1.0f / a0;
        c.b0 *= inverse; c.b1 *= inverse; c.b2 *= inverse;
        c.a1 *= inverse; c.a2 *= inverse;
    };
    const auto detectorOmega = 2.0f * pi * detectorFrequency / sampleRate;
    const auto detectorAlpha = std::sin (detectorOmega) / (2.0f * detectorQ);
    detectorFilter = { detectorAlpha, 0.0f, -detectorAlpha,
                       -2.0f * std::cos (detectorOmega), 1.0f - detectorAlpha };
    normalize (detectorFilter, 1.0f + detectorAlpha);

    const auto omega = 2.0f * pi * attenuationFrequency / sampleRate;
    const auto sine = std::sin (omega);
    const auto cosine = std::cos (omega);
    const auto alpha = sine / (2.0f * attenuationQ);
    const auto bellA = std::pow (10.0f, -maximumReduction / 40.0f);
    bellFilter = { 1.0f + alpha * bellA, -2.0f * cosine, 1.0f - alpha * bellA,
                   -2.0f * cosine, 1.0f - alpha / bellA };
    normalize (bellFilter, 1.0f + alpha / bellA);

    // The shelf's slope is inversely related to its Q control: higher Q gives
    // a steeper transition. The shelf gain is the configured maximum cut.
    const auto shelfA = bellA;
    // RBJ shelving filters can overshoot above unity for slopes greater than
    // one. Cap the mapped Q slope so this attenuation stage never boosts its
    // passband as Q is reduced.
    const auto slope = std::clamp (1.0f / attenuationQ, 0.25f, 1.0f);
    const auto radicand = std::max (0.0f, (shelfA + 1.0f / shelfA) * (1.0f / slope - 1.0f) + 2.0f);
    const auto shelfAlpha = sine * 0.5f * std::sqrt (radicand);
    const auto beta = 2.0f * std::sqrt (shelfA) * shelfAlpha;
    shelfFilter = {
        shelfA * ((shelfA + 1.0f) + (shelfA - 1.0f) * cosine + beta),
        -2.0f * shelfA * ((shelfA - 1.0f) + (shelfA + 1.0f) * cosine),
        shelfA * ((shelfA + 1.0f) + (shelfA - 1.0f) * cosine - beta),
        2.0f * ((shelfA - 1.0f) - (shelfA + 1.0f) * cosine),
        (shelfA + 1.0f) - (shelfA - 1.0f) * cosine - beta
    };
    const auto shelfA0 = (shelfA + 1.0f) - (shelfA - 1.0f) * cosine + beta;
    normalize (shelfFilter, shelfA0);

    lastDetectorFrequency = detectorFrequency;
    lastDetectorQ = detectorQ;
    lastAttenuationFrequency = attenuationFrequency;
    lastAttenuationQ = attenuationQ;
    lastDepth = maximumReduction;
}

void ShunDeEssProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void ShunDeEssProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

juce::AudioProcessorParameter* ShunDeEssProcessor::getBypassParameter() const
{
    return parameters.getParameter ("bypass");
}

void ShunDeEssProcessor::process (juce::AudioBuffer<float>& buffer, bool hostBypassed)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = std::min ({ getTotalNumInputChannels(), buffer.getNumChannels(), 2 });
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    if (channels == 0 || buffer.getNumSamples() == 0)
        return;

    const auto value = [&] (size_t index) { return raw[index]->load (std::memory_order_relaxed); };
    const auto splitBand = value (0) >= 0.5f;
    const auto detectorFrequency = value (1);
    const auto detectorQ = value (2);
    const auto attenuationFrequency = value (3);
    const auto attenuationQ = value (4);
    const auto threshold = value (5);
    const auto depth = value (6);
    const auto attackMs = value (7);
    const auto releaseMs = value (8);
    const auto outputGain = std::pow (10.0f, value (9) / 20.0f);
    const auto stereoLink = value (10) * 0.01f;
    const auto bypass = hostBypassed || value (11) >= 0.5f;
    const auto listen = value (12) >= 0.5f;
    spectralMode = value (13) >= 0.5f;
    const auto smoothMode = spectralMode && splitBand;
    if (std::abs (detectorFrequency - lastDetectorFrequency) > 0.1f
        || std::abs (detectorQ - lastDetectorQ) > 0.001f
        || std::abs (attenuationFrequency - lastAttenuationFrequency) > 0.1f
        || std::abs (attenuationQ - lastAttenuationQ) > 0.001f
        || std::abs (depth - lastDepth) > 0.001f)
        updateFilterCoefficients (detectorFrequency, detectorQ, attenuationFrequency, attenuationQ, depth);
    const auto attack = std::exp (-1.0f / (0.001f * attackMs * sampleRate));
    const auto release = std::exp (-1.0f / (0.001f * releaseMs * sampleRate));
    float blockReduction = 0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        std::array<float, 2> dry {}, detected {}, detectorLevel {};
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<size_t> (ch);
            const auto input = buffer.getSample (ch, i);
            dry[c] = std::isfinite (input) ? input : 0.0f;
            // Share one spectral analysis across both channels while keeping each
            // channel's detector envelope and audio path independent.
            if (spectralMode && ch == 0)
            {
                analysisRing[static_cast<size_t> (analysisPosition)] = dry[c];
                if (++hopCounter >= 256)
                {
                    hopCounter = 0;
                    analyseSpectralDetail (detectorFrequency, detectorQ, attenuationQ);
                }
            }
            detected[c] = filterSample (dry[c], detectorState[c], detectorFilter);
            const auto magnitude = std::abs (detected[c]);
            auto& envelope = envelopes[c];
            const auto coefficient = magnitude > envelope ? attack : release;
            envelope = magnitude + coefficient * (envelope - magnitude);
            detectorLevel[c] = 20.0f * std::log10 (std::max (envelope, 1.0e-9f));
        }
        const auto linkedLevel = channels == 2 ? std::max (detectorLevel[0], detectorLevel[1]) : detectorLevel[0];
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<size_t> (ch);
            const auto level = detectorLevel[c] + stereoLink * (linkedLevel - detectorLevel[c]);
            const auto reductionDb = std::clamp (level - threshold, 0.0f, depth);
            blockReduction = std::max (blockReduction, reductionDb);
            const auto amount = depth > 0.0f ? reductionDb / depth : 0.0f;
            const auto shelfOutput = filterSample (dry[c], shelfState[c], shelfFilter);
            const auto bellOutput = filterSample (dry[c], bellState[c], bellFilter);
            const auto shaped = splitBand ? bellOutput : shelfOutput;
            auto wet = listen ? detected[c] : dry[c] + amount * (shaped - dry[c]);
            if (spectralMode && ! listen)
            {
                // Both spectral modes use the same broad cut. Brilliant restores
                // selected detail; Smooth reverses that term to suppress peaks.
                const auto spectralAmount = amount * spectralAttenuation;
                wet = dry[c] + spectralAmount * (shelfOutput - dry[c]);
                for (size_t peak = 0; peak < sparkleWeights.size(); ++peak)
                    wet += filterSample (dry[c], sparkleStates[c][peak], sparkleFilters[peak])
                         * spectralAmount * sparkleWeights[peak] * (smoothMode ? -1.0f : 1.0f);
            }
            buffer.setSample (ch, i, bypass ? dry[c] : wet * outputGain);
        }
        analysisPosition = (analysisPosition + 1) % 1024;
    }
    reduction.store (bypass ? 0.0f : blockReduction, std::memory_order_relaxed);
}

void ShunDeEssProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, data);
}

void ShunDeEssProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ShunDeEssProcessor::createEditor()
{
    return new ShunDeEssEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ShunDeEssProcessor();
}
