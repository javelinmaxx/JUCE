#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class ShunDeEssProcessor final : public juce::AudioProcessor
{
public:
    ShunDeEssProcessor();
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SHUN DE-ESS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float> reduction { 0.0f };
    struct SpectrumSample { float left = 0, right = 0; };
    // Single audio producer / editor consumer; a full queue drops display data.
    void setSpectrumVisible (bool visible) noexcept { spectrumVisible.store (visible, std::memory_order_relaxed); }
    int readSpectrumSamples (SpectrumSample*, int maximum) noexcept;
    float spectrumSampleRate() const noexcept { return displaySampleRate.load (std::memory_order_relaxed); }
    // Four selected centre frequencies, four DSP weights, Q, effective amount,
    // spectral mode (0/2/3), and sample rate. Published together, without locks.
    using ResponseFrame = std::array<float, 12>;
    bool readResponseFrame (ResponseFrame&, uint32_t&) const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void process (juce::AudioBuffer<float>&, bool hostBypassed);
    void updateFilterCoefficients (float detectorFrequency, float detectorQ,
                                   float attenuationFrequency, float attenuationQ,
                                   float maximumReduction) noexcept;
    struct BiquadCoefficients { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct BiquadState { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static float filterSample (float, BiquadState&, const BiquadCoefficients&) noexcept;
    void analyseSpectralDetail (float detectorFrequency, float detectorQ, float attenuationQ) noexcept;
    std::array<std::atomic<float>*, 15> raw {};
    std::array<BiquadState, 2> detectorState {}, shelfState {}, bellState {};
    std::array<float, 2> envelopes {}, bandPowers {}, fullPowers {};
    BiquadCoefficients detectorFilter, shelfFilter, bellFilter;
    juce::dsp::FFT spectralFft { 10 };
    std::array<float, 2048> fftData {};
    std::array<float, 1024> analysisRing {};
    std::array<float, 513> meanSpectrum {};
    std::array<std::array<BiquadState, 4>, 2> sparkleStates {};
    std::array<BiquadCoefficients, 4> sparkleFilters {};
    std::array<float, 4> sparkleWeights {};
    int analysisPosition = 0, hopCounter = 0;
    std::array<float, 4> sparkleFrequencies {};
    static constexpr int spectrumCapacity = 8192;
    juce::AbstractFifo spectrumFifo { spectrumCapacity };
    std::array<SpectrumSample, spectrumCapacity> spectrumSamples {};
    std::atomic<bool> spectrumVisible { false };
    std::atomic<float> displaySampleRate { 44100 };
    std::array<std::atomic<float>, 12> responseData {};
    std::atomic<uint32_t> responseVersion { 0 };
    float spectralFlatness = 0, spectralAttenuation = 0;
    bool spectralMode = false;
    float sampleRate = 44100;
    float lastDetectorFrequency = -1, lastDetectorQ = -1;
    float lastAttenuationFrequency = -1, lastAttenuationQ = -1, lastDepth = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShunDeEssProcessor)
};
