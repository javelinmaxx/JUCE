#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

class KuoStereoProcessor final : public juce::AudioProcessor
{
public:
    KuoStereoProcessor();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
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
    float getSelectedDelayMs() const noexcept { return selectedDelayMs.load (std::memory_order_relaxed); }
    float getCurrentDelayMs() const noexcept { return currentDelayMs.load (std::memory_order_relaxed); }
    juce::AudioProcessorValueTreeState parameters;

private:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, candidates = 7, velvetTaps = 16;
    struct ScopePoint { std::atomic<float> left { 0 }, right { 0 }; };
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    float readAgo (const std::vector<float>&, int samplesAgo) const noexcept;
    float readAgoInterpolated (const std::vector<float>&, float samplesAgo) const noexcept;
    void analyzeMidSpectrum (float centerDelayMs, float variabilityMs) noexcept;
    void pushScope (float left, float right) noexcept;

    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize * 2> spectrumInput {}, spectrumCandidate {};
    std::array<float, fftSize> window {};
    std::array<float, candidates> smoothedCandidateScores {};
    std::array<int, velvetTaps> tapOffsets {};
    std::array<float, velvetTaps> tapWeights {};
    std::vector<float> leftHistory, rightHistory, midHistory, velvetHistory;
    std::array<std::array<ScopePoint, scopePoints>, 3> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<float> selectedDelayMs { 12 };
    std::atomic<float> currentDelayMs { 12 };
    std::array<float, 2> lowState {}, upperState {};
    int writePosition = 0, sampleCount = 0, analysisCounter = 0, scopeCounter = 0;
    int selectedCandidate = -1;
    int transientHold = 0;
    float sampleRate = 48000.0f, targetDelaySamples = 576.0f, smoothDelaySamples = 576.0f;
    float previousCenterDelayMs = -1, previousVariabilityMs = -1;
    float widthBlend = 0, haasTransientEnvelope = 0, velvetGuardEnvelope = 0;
    std::array<float, 3> detectorLowStates {}, detectorCoefficients {};
    std::array<float, 4> detectorFastEnvelopes {}, detectorSlowEnvelopes {};
    float lowCoefficient = 0, upperCoefficient = 0, fastCoefficient = 0, slowCoefficient = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuoStereoProcessor)
};
