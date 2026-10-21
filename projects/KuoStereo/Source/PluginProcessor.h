#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

class KuoStereoProcessor final : public juce::AudioProcessor
{
public:
    KuoStereoProcessor();
    ~KuoStereoProcessor() override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Kuo Stereo"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.04; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    static constexpr int scopePoints = 96;
    void readScope (int band, std::array<juce::Point<float>, scopePoints>&) const noexcept;
    float getSelectedDelayMs() const noexcept;
    float getCurrentDelayMs() const noexcept { return currentDelayMs.load (std::memory_order_relaxed); }
    juce::AudioProcessorValueTreeState parameters;

private:
    class HaasSearchWorker final : public juce::Thread
    {
    public:
        struct SearchJob
        {
            std::atomic<int> state { 0 }; // free, writing, ready, processing
            std::vector<float> leftAgo, rightAgo;
            float centerMs = 12.0f, variabilityMs = 6.0f;
            uint32_t generation = 0, sequence = 0;
        };

        HaasSearchWorker();
        void prepare (float newSampleRate, size_t historySamples);
        void run() override;
        void setFixedDelay (float delayMs) noexcept;
        void setAdaptiveMode() noexcept;
        void invalidateSearch (float centerDelayMs) noexcept;
        SearchJob* acquireJob() noexcept;
        void publishJob (SearchJob& job) noexcept;
        float getSelectedDelayMs() const noexcept
        { return selectedDelayMs.load (std::memory_order_relaxed); }
        float getTargetDelayMs() const noexcept
        { return targetDelayMs.load (std::memory_order_relaxed); }

    private:
        void process (const SearchJob&) noexcept;
        void resetSearch() noexcept;
        juce::dsp::FFT fft { 11 };
        static constexpr int fftSize = 2048, maxCandidates = 121;
        std::array<float, fftSize * 2> spectrumInput {}, spectrumCandidate {};
        std::array<float, fftSize> window {};
        std::array<float, maxCandidates> smoothedCandidateScores {};
        std::array<bool, maxCandidates> candidateScoreInitialized {};
        std::array<uint32_t, maxCandidates> candidateLastScoreFrame {};
        SearchJob job;
        std::atomic<float> selectedDelayMs { 12 }, targetDelayMs { 12 };
        std::atomic<uint32_t> generation { 0 }, latestSequence { 0 };
        std::atomic<bool> fixedMode { true };
        float sampleRate = 48000.0f;
        int selectedCandidate = -1, coarseSurveyCursor = 0;
        uint32_t scoreFrame = 0, activeGeneration = 0;
        float previousCenterDelayMs = -1, previousVariabilityMs = -1;
        bool coarseSurveyComplete = false;
    };

    static constexpr int fftSize = 2048;
    static constexpr int analysisHop = fftSize;
    static constexpr int velvetTaps = 16;
    struct ScopePoint { std::atomic<float> left { 0 }, right { 0 }; };
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    float readAgo (const std::vector<float>&, int samplesAgo) const noexcept;
    float readAgoInterpolated (const std::vector<float>&, float samplesAgo) const noexcept;
    void enqueueMidSpectrum (float centerDelayMs, float variabilityMs) noexcept;
    void pushScope (float left, float right) noexcept;

    HaasSearchWorker searchWorker;
    std::array<int, velvetTaps> tapOffsets {};
    std::array<float, velvetTaps> tapWeights {};
    std::vector<float> leftHistory, rightHistory, velvetHistory;
    std::array<std::array<ScopePoint, scopePoints>, 3> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<float> currentDelayMs { 12 };
    std::array<float, 2> lowState {}, upperState {};
    int writePosition = 0, sampleCount = 0, analysisCounter = 0, scopeCounter = 0;
    int transientHold = 0;
    int lookaheadSamples = 0;
    bool onsetActive = false, instantTransientOnset = false;
    float sampleRate = 48000.0f, targetDelaySamples = 576.0f, smoothDelaySamples = 576.0f;
    float previousCenterDelayMs = -1, previousVariabilityMs = -1;
    float widthBlend = 0, haasTransientEnvelope = 0, velvetGuardEnvelope = 0;
    float haasAttackStartEnvelope = 0, haasAttackProgress = 1;
    std::array<float, 3> detectorLowStates {}, detectorCoefficients {};
    std::array<float, 4> detectorFastEnvelopes {}, detectorSlowEnvelopes {};
    std::array<float, 2> velvetBandLowStates {};
    std::array<float, 3> velvetBandGains { 1.0f, 1.0f, 1.0f };
    float lowCoefficient = 0, upperCoefficient = 0, fastCoefficient = 0, slowCoefficient = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuoStereoProcessor)
};
