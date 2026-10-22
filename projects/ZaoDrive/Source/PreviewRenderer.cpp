#include "PreviewRenderer.h"

namespace zao
{
namespace
{
// Two 48-sample cycles fill the preview at 48 kHz; use the same triangle
// for the circuit input and its latency-aligned dry reference used by Auto.
float referenceSample (int sample) noexcept
{
    const auto phase = (sample % 48 + 48) % 48;
    return .25f*(1.f-std::abs (phase-24)/12.f);
}
}
PreviewRenderer::PreviewRenderer (bool startImmediately) : Thread ("Zao waveform preview")
{
    if (startImmediately) startThread (juce::Thread::Priority::low);
}
void PreviewRenderer::follow (std::function<Settings()> readSettings, std::function<SmartAnalyser::Result()> readAnalysis)
{
    jassert (! isThreadRunning());
    settingsProvider = std::move (readSettings); analysisProvider = std::move (readAnalysis);
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
    std::unique_ptr<DriveEngine> prepared;
    constexpr int warmup = 4096, total = warmup+96;
    std::array<float, total> source;
    for (int i = 0; i < total; ++i)
        source[static_cast<size_t> (i)] = referenceSample (i);
    juce::AudioBuffer<float> block (2, 128);
    std::array<float, 15> previousKey {};
    uint32_t previousAnalysis = 0;
    double lastRender = -1000;
    bool haveKey = false;
    while (! threadShouldExit())
    {
        Settings settings;
        SmartAnalyser::Result analysis;
        uint32_t requested = 0;
        std::array<float, 15> requestedKey {};
        if (settingsProvider)
        {
            settings = settingsProvider();
            if (! settings.autoCompensate && ! previewVisible.load()) { wait (50); continue; }
            analysis = settings.smart ? analysisProvider() : SmartAnalyser::Result {};
            requestedKey = keyFor (settings);
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if ((haveKey && requestedKey == previousKey && analysis.revision == previousAnalysis)
                || now-lastRender < 200) { wait (50); continue; }
            previousKey = requestedKey; previousAnalysis = analysis.revision;
            haveKey = true; lastRender = now; requested = revision.fetch_add (1)+1;
        }
        else
        {
            const juce::ScopedLock lock (mutex);
            if (pending)
            {
                settings = requestedSettings; analysis = requestedAnalysis;
                requested = revision.load(); pending = false;
            }
        }
        if (requested == 0) { wait (-1); continue; }
        if (! prepared)
        {
            prepared = std::make_unique<DriveEngine>(); prepared->prepare (48000, 128, false);
        }
        auto& engine = *prepared;
        engine.resetSignalPath();
        engine.getSmart().setPreview (settings.smart && ! settings.bypass
            && analysis.signature == SmartAnalyser::signature (settings) ? analysis : SmartAnalyser::Result {});
        const auto matchPreview = settings.autoCompensate && ! settings.bypass;
        const auto outputTrim = juce::Decibels::decibelsToGain (std::clamp (settings.outputDb, -24.0f, 12.0f));
        // Render without compensation, then derive the exact fixed RMS ratio
        // from this same preview snapshot for both the UI and live audio.
        settings.autoCompensate = false;
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
                    frame.input[at] = referenceSample (offset+i-engine.getLatency());
                    frame.output[at] = block.getSample (0, i);
                    frame.peak = std::max (frame.peak, std::abs (frame.output[at]));
                }
        }
        {
            double in = 0, out = 0;
            for (size_t i = 0; i < frame.output.size(); ++i)
            {
                in += static_cast<double> (frame.input[i])*frame.input[i];
                out += static_cast<double> (frame.output[i])*frame.output[i];
            }
            const auto amount = AutoCompensator::matchingGain (in/frame.input.size(), out/frame.output.size());
            frame.matchingGain = amount;
            frame.peak = 0;
            for (auto& sample : frame.output)
            {
                if (matchPreview) sample *= amount*outputTrim;
                frame.peak = std::max (frame.peak, std::abs (sample));
            }
        }
        frame.scale = std::max (.25f, std::ceil (frame.peak*8)*.125f);
        if (settingsProvider && keyFor (settingsProvider()) != requestedKey)
        {
            // A discarded frame is not a completed calibration. Retry even
            // if rapid automation returns to this same key before the poll.
            haveKey = false;
            continue;
        }
        const juce::ScopedLock lock (mutex);
        if (! threadShouldExit() && requested == revision.load())
        {
            result = frame; correction.store (frame.matchingGain, std::memory_order_relaxed);
        }
    }
}
}
