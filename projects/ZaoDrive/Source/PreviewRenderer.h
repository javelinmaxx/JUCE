#pragma once
#include "DriveEngine.h"

namespace zao
{
struct PreviewFrame
{
    std::array<float, 96> input {}, output {};
    float peak = 0, scale = .25f;
    uint32_t revision = 0;
};

// These short locks belong only to the UI and its renderer, never to audio.
// A new request replaces pending work and cancels stale renders between blocks.
class PreviewRenderer final : private juce::Thread
{
public:
    PreviewRenderer();
    ~PreviewRenderer() override;
    void request (const Settings&, const SmartAnalyser::Result&);
    void cancel();
    bool read (PreviewFrame&) const;
private:
    void run() override;
    mutable juce::CriticalSection mutex;
    Settings requestedSettings;
    SmartAnalyser::Result requestedAnalysis;
    PreviewFrame result;
    std::atomic<uint32_t> revision { 0 };
    bool pending = false;
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
