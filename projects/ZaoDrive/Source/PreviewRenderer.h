#pragma once
#include "DriveEngine.h"
#include <functional>

namespace zao
{
struct PreviewFrame
{
    std::array<float, 96> input {}, output {};
    float peak = 0, scale = .25f;
    float matchingGain = 1;
    uint32_t revision = 0;
};

// These short locks belong only to the UI and its renderer, never to audio.
// A new request replaces pending work and cancels stale renders between blocks.
class PreviewRenderer final : private juce::Thread
{
public:
    explicit PreviewRenderer (bool startImmediately = true);
    ~PreviewRenderer() override;
    void request (const Settings&, const SmartAnalyser::Result&);
    void cancel();
    bool read (PreviewFrame&) const;
    // Providers are installed before starting the worker, and read only host
    // parameter atomics / the analyser's lock-free snapshot. Audio reads just
    // the published scalar gain, even with the editor closed.
    void follow (std::function<Settings()>, std::function<SmartAnalyser::Result()>);
    // Parameter listeners request work without taking the preview/result lock.
    // Multiple changes during a render coalesce into the next latest snapshot.
    void controlsChanged() noexcept
    {
        controlRevision.fetch_add (1, std::memory_order_release);
        notify();
    }
    void setVisible (bool visible) noexcept
    {
        if (previewVisible.exchange (visible) != visible) notify();
    }
    float readGain() const noexcept { return correction.load (std::memory_order_relaxed); }
    static std::array<float, 15> keyFor (const Settings& s) noexcept
    {
        return { s.inputDb, s.driveDb, s.toneHz, s.outputDb, s.mix, s.coreDriveDb,
            s.dcBias, static_cast<float> (s.model), s.transformer ? 1.f : 0.f,
            s.bypass ? 1.f : 0.f, s.bjtBias, s.diodeKnee, s.smart ? 1.f : 0.f,
            s.clear ? 1.f : 0.f, s.autoCompensate ? 1.f : 0.f };
    }
private:
    void run() override;
    mutable juce::CriticalSection mutex;
    Settings requestedSettings;
    SmartAnalyser::Result requestedAnalysis;
    PreviewFrame result;
    std::atomic<uint32_t> revision { 0 };
    std::atomic<uint32_t> controlRevision { 0 };
    bool pending = false;
    std::function<Settings()> settingsProvider;
    std::function<SmartAnalyser::Result()> analysisProvider;
    std::atomic<bool> previewVisible { false };
    std::atomic<float> correction { 1 };
};

// Display-only interpolation never changes the audio or the rendered targets.
class PreviewAnimation
{
public:
    static constexpr double durationMs = 160;
    void setTarget (const PreviewFrame& frame, double now)
    {
        if (! initialized) { shown = target = frame; initialized = true; return; }
        advance (now);
        from = shown; target = frame; started = now; moving = true;
    }
    bool advance (double now)
    {
        if (! moving) return false;
        const auto t = static_cast<float> (std::clamp ((now-started)/durationMs, 0.0, 1.0));
        const auto blend = t*t*(3-2*t);
        shown.peak = 0;
        for (size_t i = 0; i < shown.output.size(); ++i)
        {
            shown.input[i] = from.input[i]+blend*(target.input[i]-from.input[i]);
            shown.output[i] = from.output[i]+blend*(target.output[i]-from.output[i]);
            shown.peak = std::max (shown.peak, std::abs (shown.output[i]));
        }
        shown.scale = from.scale+blend*(target.scale-from.scale);
        if (t >= 1) { shown = target; moving = false; }
        return true;
    }
    const PreviewFrame& current() const noexcept { return shown; }
private:
    PreviewFrame shown, from, target;
    double started = 0;
    bool moving = false, initialized = false;
};
}
