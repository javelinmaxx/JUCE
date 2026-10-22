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
    float low = 0, high = 0, split = 1000, depth = 50, link = 100;
    bool automatic = false, bypass = false;
    float transient = 200;
    bool operator== (const Settings& b) const noexcept
    { return juce::exactlyEqual (low, b.low) && juce::exactlyEqual (high, b.high)
          && juce::exactlyEqual (split, b.split) && juce::exactlyEqual (depth, b.depth) && juce::exactlyEqual (link, b.link) && automatic == b.automatic
          && juce::exactlyEqual (transient, b.transient); }
};
struct Target { float frequency = 0, width = 0, angle = 0, delay = 0; };
struct Analysis
{
    std::array<Target, 24> targets {};
    int count = 0;
    float flux = 0, crest = 0, strength = 1;
    double anchor = 0; // analyse returns frame-relative seconds; the worker stores absolute audio time
};
struct StereoAnalysis { Analysis shared; std::array<Analysis, 2> channel {}; bool stereo = true; };
inline constexpr int meterBins = 64;
inline constexpr int meterTransient = 2*meterBins, meterPeak = meterTransient+1, meterCeiling = meterTransient+2;
using SpectrumFrame = std::array<float, 2*meterBins+3>; // energy, input timing ms, transient, peak, ceiling Hz
using RotationFrame = std::array<float, 4*meterBins+1>; // left real/imag, bypass, right real/imag
template <size_t N> class MeterExchange
{
    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free);
public:
    void reset() noexcept { std::array<float, N> empty {}; publish (empty); }
    void publish (const std::array<float, N>& frame) noexcept
    {
        version.fetch_add (1);
        for (size_t i = 0; i < N; ++i) data[i].store (frame[i]);
        version.fetch_add (1);
    }
    bool read (std::array<float, N>& frame, uint32_t& last) const noexcept
    {
        const auto before = version.load();
        if (before == 0 || (before&1u) || before == last) return false;
        std::array<float, N> copy;
        for (size_t i = 0; i < N; ++i) copy[i] = data[i].load();
        if (before != version.load()) return false;
        frame = copy; last = before; return true;
    }
private:
    std::array<std::atomic<float>, N> data {};
    std::atomic<uint32_t> version { 0 };
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
    void setMeterVisible (bool visible) noexcept { meterVisible.store (visible); }
    bool readSpectrum (SpectrumFrame& f, uint32_t& v) const noexcept { return spectrumMeter.read (f, v); }
    bool readRotation (RotationFrame& f, uint32_t& v) const noexcept { return rotationMeter.read (f, v); }
    static double meterFrequency (int row, double rate) noexcept;
    static float releaseStrength (float current, double samples, double rate, float milliseconds);
    static double bandValue (double frequency, const Analysis&, bool timing);
    static float phaseAt (double frequency, const Settings&, const Analysis&);
    static Analysis analyse (const std::vector<float>&, double rate, std::vector<float>& previousMagnitude, double& previousEnergy, SpectrumFrame* = nullptr);
    static float stereoPhaseAt (double frequency, const Settings&, const StereoAnalysis&, int channel);
    static std::vector<float> stereoImpulse (double rate, const Settings&, const StereoAnalysis&, int channel);
    static std::vector<float> impulse (double rate, const Settings&, const Analysis& = {});
    // Deterministic offline loading uses exactly the same partitioned audio path.
    void setOffline (const Settings&, const Analysis& = {});
    void setStereoOffline (const Settings&, const StereoAnalysis&);
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
        std::array<Kernel, 2> kernels;
        std::atomic<int> state { 0 }; // free, building, ready, audio-owned
        uint32_t version = 0;
        std::array<std::array<std::complex<float>, meterBins>, 2> response {};
        std::array<std::array<float, hop>, 2> tail {}, overlap {};
        bool valid = false;
    };
    void run() override;
    void finishHop();
    void renderTail (int slot);
    void transformTail (const Kernel&, int channel, int age);
    void prepareMeter (Slot&, const std::vector<float>&, int channel);
    void publishRotation();
    std::array<Slot, 3> slots;
    juce::dsp::FFT fft { 8 };
    std::array<Complex, fftSize> work {}, transformed {}, sum {}, inverse {};
    std::array<std::vector<Complex>, 2> spectra;
    std::array<std::array<float, 2*hop>, 2> history {};
    std::array<std::vector<float>, 2> dry;
    std::vector<float> analysisRing, leftAnalysisRing, rightAnalysisRing, analysisJob;
    std::array<std::vector<float>, 2> channelJobs;
    int channelCount = 2, jobChannels = 2;
    uint64_t processedSamples = 0, jobSample = 0; // jobState synchronizes the timestamp with its audio frame
    std::atomic<int> jobState { 0 }, displayedTargets { 0 };
    std::atomic<uint32_t> revision { 1 };
    std::atomic<bool> autoActive { false };
    std::atomic<bool> meterVisible { false };
    MeterExchange<2*meterBins+3> spectrumMeter;
    MeterExchange<4*meterBins+1> rotationMeter;
    int meterHopCounter = 0;
    float meterBypass = 0;
    std::function<Settings()> readSettings;
    Settings preparedSettings;
    double sampleRate = 48000;
    int delay = 1200, partitions = 18, spectrumPosition = -1, position = 0, dryPosition = 0, ringPosition = 0;
    int active = 0, pending = -1, fadePosition = 0, fadeLength = 240;
    juce::SmoothedValue<float> bypassBlend;
};
}
