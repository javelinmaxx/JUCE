#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <complex>
#include <functional>

namespace xuan
{
struct Settings
{
    float low = 0, high = 0, split = 1000, depth = 50, output = 0;
    bool automatic = false, bypass = false;
    bool operator== (const Settings& b) const noexcept
    { return juce::exactlyEqual (low, b.low) && juce::exactlyEqual (high, b.high)
          && juce::exactlyEqual (split, b.split) && juce::exactlyEqual (depth, b.depth) && automatic == b.automatic; }
};
struct Target { float frequency = 0, width = 0, angle = 0, delay = 0; };
struct Analysis
{
    std::array<Target, 12> targets {};
    int count = 0;
    float flux = 0, crest = 0;
};
// The worker designs a bounded, real FIR around a 25 ms reference. The audio
// path uses a direct head plus partitioned FFT tail, with no extra block delay.
class PhaseEngine final : private juce::Thread
{
public:
    PhaseEngine();
    ~PhaseEngine() override;
    void prepare (double rate, std::function<Settings()> provider, bool worker = true);
    void release();
    void changed() noexcept { revision.fetch_add (1, std::memory_order_release); notify(); }
    void process (juce::AudioBuffer<float>&, const Settings&, bool hostBypass = false);
    int latency() const noexcept { return delay; }
    int targetCount() const noexcept { return displayedTargets.load (std::memory_order_relaxed); }
    static float phaseAt (double frequency, const Settings&, const Analysis&);
    static Analysis analyse (const std::vector<float>&, double rate, std::vector<float>& previousMagnitude, double& previousEnergy);
    static std::vector<float> impulse (double rate, const Settings&, const Analysis& = {});
    // Deterministic offline loading uses exactly the same partitioned audio path.
    void setOffline (const Settings&, const Analysis& = {});
private:
    using Complex = std::complex<float>;
    static constexpr int hop = 128, fftSize = 2*hop;
    struct Kernel
    {
        std::array<float, hop> head {};
        std::vector<Complex> tail;
        std::vector<int> nonzero;
        bool hasHead = false;
        void build (const std::vector<float>&, int partitions);
    };
    struct Slot
    {
        Kernel kernel;
        std::atomic<int> state { 0 }; // free, building, ready, audio-owned
        uint32_t version = 0;
        std::array<std::array<float, hop>, 2> tail {}, overlap {};
        bool valid = false;
    };
    void run() override;
    void finishHop();
    void renderTail (int slot);
    void transformTail (const Kernel&, int channel, int age);
    std::array<Slot, 3> slots;
    juce::dsp::FFT fft { 8 };
    std::array<Complex, fftSize> work {}, transformed {}, sum {}, inverse {};
    std::array<std::vector<Complex>, 2> spectra;
    std::array<std::array<float, 2*hop>, 2> history {};
    std::array<std::vector<float>, 2> dry;
    std::vector<float> analysisRing, leftAnalysisRing, analysisJob;
    std::atomic<int> jobState { 0 }, displayedTargets { 0 };
    std::atomic<uint32_t> revision { 1 };
    std::atomic<bool> autoActive { false };
    std::function<Settings()> readSettings;
    Settings preparedSettings;
    double sampleRate = 48000;
    int delay = 1200, partitions = 18, spectrumPosition = -1, position = 0, dryPosition = 0, ringPosition = 0;
    int active = 0, pending = -1, fadePosition = 0, fadeLength = 240;
    juce::SmoothedValue<float> outputGain, bypassBlend;
};
}
