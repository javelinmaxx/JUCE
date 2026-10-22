#include "PreviewRenderer.h"

namespace zao
{
PreviewRenderer::PreviewRenderer() : Thread ("Zao waveform preview")
{
    startThread (juce::Thread::Priority::low);
}
PreviewRenderer::~PreviewRenderer()
{
    signalThreadShouldExit(); notify(); stopThread (-1);
}
void PreviewRenderer::request (const Settings& settings, const SmartAnalyser::Result& analysis)
{
    {
        const juce::ScopedLock lock (mutex);
        requestedSettings = settings; requestedAnalysis = analysis;
        revision.fetch_add (1); pending = true;
    }
    notify();
}
void PreviewRenderer::cancel()
{
    const juce::ScopedLock lock (mutex);
    revision.fetch_add (1); pending = false;
}
bool PreviewRenderer::read (PreviewFrame& frame) const
{
    const juce::ScopedLock lock (mutex);
    if (result.revision == 0 || result.revision != revision.load() || result.revision == frame.revision) return false;
    frame = result;
    return true;
}
void PreviewRenderer::run()
{
    DriveEngine engine;
    engine.prepare (48000, 128, false);
    constexpr int warmup = 4096, total = warmup+96;
    std::array<float, total> source;
    for (int i = 0; i < total; ++i)
        source[static_cast<size_t> (i)] = .25f*std::sin (static_cast<float> (2*pi*i/48.0));
    juce::AudioBuffer<float> block (2, 128);
    while (! threadShouldExit())
    {
        Settings settings;
        SmartAnalyser::Result analysis;
        uint32_t requested = 0;
        {
            const juce::ScopedLock lock (mutex);
            if (pending)
            {
                settings = requestedSettings; analysis = requestedAnalysis;
                requested = revision.load(); pending = false;
            }
        }
        if (requested == 0) { wait (-1); continue; }
        engine.resetSignalPath();
        engine.getSmart().setPreview (settings.smart && ! settings.bypass
            && analysis.signature == SmartAnalyser::signature (settings) ? analysis : SmartAnalyser::Result {});
        PreviewFrame frame; frame.revision = requested;
        for (int offset = 0; offset < total && ! threadShouldExit() && revision.load() == requested; offset += 128)
        {
            const auto count = std::min (128, total-offset);
            block.setSize (2, count, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::copy (block.getWritePointer (ch), source.data()+offset, count);
            engine.process (block, settings);
            for (int i = 0; i < count; ++i)
                if (offset+i >= warmup)
                {
                    const auto at = static_cast<size_t> (offset+i-warmup);
                    frame.input[at] = .25f*std::sin (static_cast<float> (2*pi*(offset+i-engine.getLatency())/48.0));
                    frame.output[at] = block.getSample (0, i);
                    frame.peak = std::max (frame.peak, std::abs (frame.output[at]));
                }
        }
        frame.scale = std::max (.25f, std::ceil (frame.peak*8)*.125f);
        const juce::ScopedLock lock (mutex);
        if (! threadShouldExit() && requested == revision.load()) result = frame;
    }
}
}
