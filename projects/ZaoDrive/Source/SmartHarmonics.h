#pragma once
#include "DriveSettings.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace zao
{
class SmartAnalyser
{
public:
    static constexpr int fftSize = 4096, orders = 12, maxPeaks = 24, maxBands = 16;
    struct Band { float frequency = 0, bandwidth = 0, gainDb = 0; int order = 0; };
    struct Frame
    {
        std::array<float, fftSize> left {}, right {}, wetLeft {}, wetRight {};
        Settings settings;
        double sampleRate = 48000;
        uint32_t generation = 0, signature = 0;
    };
    struct Result
    {
        std::array<Band, maxBands> bands {};
        std::array<float, orders+1> before {}, after {};
        float baselineCost = 0, selectedCost = 0, confidence = 0;
        int count = 0;
        uint32_t generation = 0, signature = 0, revision = 0;
    };
    SmartAnalyser();
    Result analyse (const Frame&);
    static float intervalDissonance (double, double) noexcept;
    static uint32_t signature (const Settings&) noexcept;
private:
    struct Peak { double frequency = 0, power = 0; };
    juce::dsp::FFT fft { 12 };
    std::array<float, fftSize*2> data {};
    std::array<float, fftSize> window {};
    std::array<double, fftSize/2+1> inputPower {}, wetPower {};
    std::array<Peak, maxPeaks> peaks {};
};

// FFT and interval scoring have a preallocated, nonblocking job slot. Audio
// uses only bounded biquads, with two banks crossfaded when the score changes.
class SmartHarmonics final : private juce::Thread
{
    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free,
                   "Smart audio publication requires lock-free scalar atomics");
public:
    SmartHarmonics();
    ~SmartHarmonics() override;
    void prepare (double rate, bool worker = true);
    void release();
    void setPreview (const SmartAnalyser::Result&) noexcept;
    void beginBlock (const Settings&) noexcept;
    void process (const std::array<float, 2>& input, std::array<float, 2>& wet) noexcept;
    SmartAnalyser::Result readResult() const noexcept;
    uint32_t getSubmittedJobs() const noexcept { return submitted.load(); }
private:
    struct Filter
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z1 {}, z2 {};
        void configure (const SmartAnalyser::Band&, double) noexcept;
        float tick (float, size_t) noexcept;
    };
    struct Bank
    {
        std::array<Filter, SmartAnalyser::maxBands> filters;
        int count = 0;
        float tick (float, size_t) noexcept;
    };
    void run() override;
    void publish (const SmartAnalyser::Result&) noexcept;
    SmartAnalyser analyser;
    SmartAnalyser::Frame job, ring;
    Settings settings;
    std::array<Bank, 2> banks;
    juce::SmoothedValue<float> transition;
    std::atomic<int> jobState { 0 };
    std::atomic<uint32_t> generation { 1 }, submitted { 0 };
    // Atomic payload plus sequence validation: readers never wait for a writer.
    std::atomic<uint32_t> resultVersion { 0 }, resultGeneration { 0 }, resultSignature { 0 };
    std::array<std::atomic<float>, 4 + 2*(SmartAnalyser::orders+1) + 4*SmartAnalyser::maxBands> values {};
    int position = 0, filled = 0, sinceJob = 0, active = 0, pending = 1;
    uint32_t previousSignature = 0, appliedVersion = 0;
    double sampleRate = 48000;
    bool enabled = false, firstJob = true, previewMode = false;
};
}
