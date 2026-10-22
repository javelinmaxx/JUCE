#include "PhaseEngine.h"

namespace xuan
{
namespace
{
constexpr auto pi = juce::MathConstants<double>::pi;
float sigmoid (float t) { return t*t*(3-2*t); }
int orderFor (int n) { int order = 0; while ((1 << order) < n) ++order; return order; }
float dot (const float* a, const float* b, int n)
{
#if JUCE_USE_SIMD
    using V = juce::dsp::SIMDRegister<float>;
    V sum (0.f);
    for (int i = 0; i < n; i += static_cast<int> (V::size()))
    {
        V x, y; std::memcpy (&x.value, a+i, sizeof (x.value)); std::memcpy (&y.value, b+i, sizeof (y.value));
        sum += x*y;
    }
    return sum.sum();
#else
    float sum = 0; for (int i = 0; i < n; ++i) sum += a[i]*b[i]; return sum;
#endif
}
}
float PhaseEngine::phaseAt (double frequency, const Settings& s, const Analysis& a)
{
    const auto weight = 1.0/(1+std::pow (frequency/std::clamp (s.split, 80.f, 4000.f), 4));
    auto phase = (s.low*weight+s.high*(1-weight))*pi/180;
    if (s.automatic && s.depth > 0)
    {
        double correction = 0, totalWeight = 0;
        for (int i = 0; i < a.count; ++i)
        {
            const auto& t = a.targets[static_cast<size_t> (i)];
            const auto distance = (frequency-t.frequency)/t.width;
            const auto w = std::exp (-.5*distance*distance);
            correction += w*std::clamp (t.angle+2*pi*(frequency-t.frequency)*t.delay, -pi, pi);
            totalWeight += w;
        }
        phase += std::clamp (s.depth*.01f, 0.f, 1.f)*correction/std::max (1.0, totalWeight);
    }
    return static_cast<float> (phase);
}
std::vector<float> PhaseEngine::impulse (double rate, const Settings& s, const Analysis& a)
{
    const auto centre = juce::roundToInt (rate*.025);
    std::vector<float> kernel (static_cast<size_t> (2*centre+1), 0);
    if (juce::exactlyEqual (s.low, 0.f) && juce::exactlyEqual (s.high, 0.f) && (! s.automatic || s.depth <= 0 || a.count == 0))
    { kernel[static_cast<size_t> (centre)] = 1; return kernel; }
    const auto order = orderFor (8*centre+1), n = 1 << order;
    juce::dsp::FFT designer (order);
    std::vector<Complex> response (static_cast<size_t> (n)), time (static_cast<size_t> (n));
    for (int k = 0; k <= n/2; ++k)
    {
        const auto phase = phaseAt (rate*k/n, s, a);
        const auto v = std::polar (1.f, phase);
        response[static_cast<size_t> (k)] = (k == 0 || k == n/2) ? Complex (v.real(), 0) : v;
        if (k > 0 && k < n/2) response[static_cast<size_t> (n-k)] = std::conj (v);
    }
    designer.perform (response.data(), time.data(), true);
    // Taper only the outer 20% of the finite +/-25 ms support. A finite FIR
    // approximates an ideal phase-only response; do not silently normalise it.
    const auto edge = std::max (1, centre/5);
    for (int i = 0; i <= 2*centre; ++i)
    {
        const auto distance = std::min (i, 2*centre-i);
        const auto window = distance >= edge ? 1.0 : std::pow (std::sin (.5*pi*distance/edge), 2);
        kernel[static_cast<size_t> (i)] = time[static_cast<size_t> ((i-centre+n)%n)].real()*static_cast<float> (window);
    }
    return kernel;
}
Analysis PhaseEngine::analyse (const std::vector<float>& input, double rate, std::vector<float>& previous, double& previousEnergy)
{
    const auto n = static_cast<int> (input.size());
    juce::dsp::FFT analyser (orderFor (n));
    std::vector<Complex> time (input.size()), spectrum (input.size());
    double energy = 0; float peak = 0; int anchor = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto x = input[static_cast<size_t> (i)]; energy += x*x;
        if (std::abs (x) > peak) { peak = std::abs (x); anchor = i; }
        // Short edge tapers let a recent onset reach the analysis quickly.
        const auto edge = std::min (i, n-1-i);
        const auto window = edge >= n/16 ? 1.0 : .5-.5*std::cos (pi*edge/(n/16));
        time[static_cast<size_t> (i)] = static_cast<float> (x*window);
    }
    analyser.perform (time.data(), spectrum.data(), false);
    if (previous.size() != static_cast<size_t> (n/2+1)) previous.assign (static_cast<size_t> (n/2+1), 0);
    double flux = 0, total = 0; float largest = 0;
    for (int k = 1; k < n/2; ++k)
    {
        const auto m = std::abs (spectrum[static_cast<size_t> (k)]);
        flux += std::max (0.f, m-previous[static_cast<size_t> (k)]); total += m;
        previous[static_cast<size_t> (k)] = m; largest = std::max (largest, m);
    }
    energy /= n;
    Analysis result; result.flux = static_cast<float> (flux/std::max (1e-12, total));
    result.crest = static_cast<float> (peak/std::sqrt (std::max (1e-12, energy)));
    const auto onset = energy > 1e-7 && result.crest > 2
        && ((energy > previousEnergy*1.5 && result.flux > .15f) || (result.flux > .4f && result.crest > 3));
    previousEnergy = energy;
    if (! onset) return result;
    const auto sign = input[static_cast<size_t> (anchor)] < 0 ? pi : 0;
    // Local spectral peaks define narrow bands. Their complex phase and phase
    // slope predict how each wave packet can meet the strongest onset sample.
    for (int k = 2; k < n/2-1 && result.count < static_cast<int> (result.targets.size()); ++k)
    {
        const auto frequency = rate*k/n;
        if (frequency < 60 || frequency > std::min (12000.0, rate*.45)) continue;
        const auto m = previous[static_cast<size_t> (k)];
        if (m < largest*.08f || m <= previous[static_cast<size_t> (k-1)] || m < previous[static_cast<size_t> (k+1)]) continue;
        const auto angle = std::remainder (sign-2*pi*k*anchor/n-std::arg (spectrum[static_cast<size_t> (k)]), 2*pi);
        // Remove the anchor's time origin before unwrapping the local slope;
        // otherwise a late transient aliases its group delay by half a frame.
        const auto slope = std::remainder (std::arg (spectrum[static_cast<size_t> (k+1)])-std::arg (spectrum[static_cast<size_t> (k-1)])+4*pi*anchor/n, 2*pi);
        const auto relativeTime = -slope/(4*pi*rate/n);
        result.targets[static_cast<size_t> (result.count++)] = {
            static_cast<float> (frequency), static_cast<float> (std::max (rate/n, frequency*.025)),
            static_cast<float> (angle), static_cast<float> (std::clamp (relativeTime, -.025, .025)) };
        ++k;
    }
    return result;
}
void PhaseEngine::Kernel::build (const std::vector<float>& ir, int count)
{
    head.fill (0); hasHead = false;
    for (int i = 0; i < hop && i < static_cast<int> (ir.size()); ++i)
    { head[static_cast<size_t> (hop-1-i)] = ir[static_cast<size_t> (i)]; hasHead |= std::abs (ir[static_cast<size_t> (i)]) > 0; }
    tail.assign (static_cast<size_t> (count*fftSize), {});
    nonzero.clear(); nonzero.reserve (static_cast<size_t> (count));
    juce::dsp::FFT transform (8);
    std::array<Complex, fftSize> block {};
    for (int p = 0; p < count; ++p)
    {
        block.fill ({});
        bool occupied = false;
        for (int i = 0; i < hop; ++i)
        {
            const auto at = hop+p*hop+i;
            if (at < static_cast<int> (ir.size()))
            { block[static_cast<size_t> (i)] = ir[static_cast<size_t> (at)]; occupied |= std::abs (ir[static_cast<size_t> (at)]) > 0; }
        }
        if (! occupied) continue;
        nonzero.push_back (p);
        transform.perform (block.data(), tail.data()+p*fftSize, false);
    }
}
PhaseEngine::PhaseEngine() : Thread ("Xuan phase analysis") {}
PhaseEngine::~PhaseEngine() { release(); }
void PhaseEngine::release() { signalThreadShouldExit(); notify(); stopThread (-1); }
void PhaseEngine::prepare (double rate, std::function<Settings()> provider, bool worker)
{
    release(); sampleRate = rate; readSettings = std::move (provider);
    delay = juce::roundToInt (rate*.025); partitions = (2*delay+1-hop+hop-1)/hop;
    partitions = std::max (1, partitions); fadeLength = std::max (1, juce::roundToInt (rate*.005));
    history = {}; position = dryPosition = ringPosition = 0; spectrumPosition = -1;
    active = 0; pending = -1; fadePosition = 0;
    for (auto& channel : spectra) channel.assign (static_cast<size_t> ((partitions+1)*fftSize), {});
    for (auto& channel : dry) channel.assign (static_cast<size_t> (delay), 0);
    const auto analysisSize = 1 << orderFor (juce::roundToInt (rate*.04));
    analysisRing.assign (static_cast<size_t> (analysisSize), 0); leftAnalysisRing = analysisRing; analysisJob = analysisRing;
    for (auto& slot : slots) { slot.state.store (0); slot.valid = false; slot.tail = {}; slot.overlap = {}; }
    jobState.store (0); displayedTargets.store (0); autoActive.store (false); revision.store (1);
    const auto s = readSettings(); preparedSettings = s;
    slots[0].kernel.build (impulse (rate, s), partitions); slots[0].state.store (3);
    outputGain.reset (rate, .02); outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (s.output));
    bypassBlend.reset (rate, .005); bypassBlend.setCurrentAndTargetValue (s.bypass ? 1.f : 0.f);
    if (worker) startThread (juce::Thread::Priority::normal);
}
void PhaseEngine::setOffline (const Settings& s, const Analysis& a)
{
    jassert (! isThreadRunning());
    slots[0].kernel.build (impulse (sampleRate, s, a), partitions);
}
void PhaseEngine::transformTail (const Kernel& kernel, int channel, int age)
{
    sum.fill ({});
    for (int p : kernel.nonzero)
    {
        const auto at = (spectrumPosition-age-p+2*(partitions+1))%(partitions+1);
        const auto* x = spectra[static_cast<size_t> (channel)].data()+at*fftSize;
        const auto* h = kernel.tail.data()+p*fftSize;
        for (int k = 0; k < fftSize; ++k) sum[static_cast<size_t> (k)] += x[k]*h[k];
    }
    fft.perform (sum.data(), inverse.data(), true);
}
void PhaseEngine::renderTail (int index)
{
    auto& slot = slots[static_cast<size_t> (index)];
    for (int ch = 0; ch < 2; ++ch)
    {
        if (! slot.valid)
        {
            transformTail (slot.kernel, ch, 1);
            for (int i = 0; i < hop; ++i) slot.overlap[static_cast<size_t> (ch)][static_cast<size_t> (i)] = inverse[static_cast<size_t> (i+hop)].real();
        }
        transformTail (slot.kernel, ch, 0);
        for (int i = 0; i < hop; ++i)
        {
            slot.tail[static_cast<size_t> (ch)][static_cast<size_t> (i)] = inverse[static_cast<size_t> (i)].real()+slot.overlap[static_cast<size_t> (ch)][static_cast<size_t> (i)];
            slot.overlap[static_cast<size_t> (ch)][static_cast<size_t> (i)] = inverse[static_cast<size_t> (i+hop)].real();
        }
    }
    slot.valid = true;
}
void PhaseEngine::finishHop()
{
    spectrumPosition = (spectrumPosition+1)%(partitions+1);
    for (int ch = 0; ch < 2; ++ch)
    {
        work.fill ({});
        for (int i = 0; i < hop; ++i) work[static_cast<size_t> (i)] = history[static_cast<size_t> (ch)][static_cast<size_t> (i)];
        fft.perform (work.data(), spectra[static_cast<size_t> (ch)].data()+spectrumPosition*fftSize, false);
    }
    if (pending < 0)
    {
        int newest = -1;
        for (int i = 0; i < 3; ++i)
            if (slots[static_cast<size_t> (i)].state.load (std::memory_order_acquire) == 2
                && (newest < 0 || slots[static_cast<size_t> (i)].version > slots[static_cast<size_t> (newest)].version)) newest = i;
        if (newest >= 0)
        {
            for (int i = 0; i < 3; ++i)
                if (i != newest && slots[static_cast<size_t> (i)].state.load() == 2) slots[static_cast<size_t> (i)].state.store (0, std::memory_order_release);
            pending = newest; slots[static_cast<size_t> (pending)].state.store (3); fadePosition = 0;
        }
    }
    renderTail (active); if (pending >= 0) renderTail (pending);
    if (autoActive.load (std::memory_order_relaxed))
    {
        int expected = 0;
        if (jobState.compare_exchange_strong (expected, 1, std::memory_order_acquire))
        {
            const auto n = static_cast<int> (analysisRing.size());
            double midEnergy = 0, leftEnergy = 0;
            for (int i = 0; i < n; ++i)
            {
                const auto at = static_cast<size_t> ((ringPosition+i)%n);
                const auto mid = analysisRing[at], left = leftAnalysisRing[at];
                analysisJob[static_cast<size_t> (i)] = mid; midEnergy += mid*mid; leftEnergy += left*left;
            }
            // Select one reference for the entire frame, rather than switching
            // channels at individual zero crossings in anti-phase material.
            if (midEnergy < leftEnergy*.01)
                for (int i = 0; i < n; ++i) analysisJob[static_cast<size_t> (i)] = leftAnalysisRing[static_cast<size_t> ((ringPosition+i)%n)];
            jobState.store (2, std::memory_order_release);
        }
    }
}
void PhaseEngine::process (juce::AudioBuffer<float>& buffer, const Settings& s, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = std::min (2, buffer.getNumChannels());
    if (dry[0].empty()) return;
    autoActive.store (s.automatic && s.depth > 0 && ! s.bypass && ! hostBypass, std::memory_order_relaxed);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (std::clamp (s.output, -24.f, 12.f)));
    bypassBlend.setTargetValue (s.bypass || hostBypass ? 1.f : 0.f);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        std::array<float, 2> in {}, out {}, bypassed {};
        const auto blend = pending >= 0 ? sigmoid (std::min (1.f, static_cast<float> (fadePosition+1)/fadeLength)) : 0.f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto c = static_cast<size_t> (ch);
            const auto raw = ch < channels ? buffer.getSample (ch, i) : 0;
            in[c] = std::isfinite (raw) ? std::clamp (raw, -32.f, 32.f) : 0;
            history[c][static_cast<size_t> (position)] = history[c][static_cast<size_t> (position+hop)] = in[c];
            bypassed[c] = dry[c][static_cast<size_t> (dryPosition)]; dry[c][static_cast<size_t> (dryPosition)] = in[c];
            const auto evaluate = [&] (int index)
            {
                const auto& slot = slots[static_cast<size_t> (index)];
                return slot.tail[c][static_cast<size_t> (position)] + (slot.kernel.hasHead
                    ? dot (slot.kernel.head.data(), history[c].data()+position+1, hop) : 0.f);
            };
            const auto old = evaluate (active);
            out[c] = pending >= 0 ? old+blend*(evaluate (pending)-old) : old;
        }
        const auto mid = channels == 1 ? in[0] : .5f*(in[0]+in[1]);
        analysisRing[static_cast<size_t> (ringPosition)] = mid;
        leftAnalysisRing[static_cast<size_t> (ringPosition)] = in[0];
        ringPosition = (ringPosition+1)%static_cast<int> (analysisRing.size());
        const auto gain = outputGain.getNextValue(), bypass = bypassBlend.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<size_t> (ch); const auto wet = out[c]*gain;
            buffer.setSample (ch, i, hostBypass || bypass >= 1 ? bypassed[c] : wet+bypass*(bypassed[c]-wet));
        }
        if (pending >= 0 && ++fadePosition >= fadeLength)
        {
            slots[static_cast<size_t> (active)].state.store (0, std::memory_order_release);
            active = pending; pending = -1;
        }
        dryPosition = (dryPosition+1)%delay;
        if (++position == hop) { position = 0; finishHop(); }
    }
}
void PhaseEngine::run()
{
    auto last = preparedSettings; uint32_t seen = revision.load(), serial = 0; bool dirty = false;
    Analysis targets; std::vector<float> previous; double previousEnergy = 0;
    while (! threadShouldExit())
    {
        const auto s = readSettings(); const auto version = revision.load (std::memory_order_acquire);
        dirty |= ! (s == last);
        if (jobState.load (std::memory_order_acquire) == 2)
        {
            const auto result = analyse (analysisJob, sampleRate, previous, previousEnergy);
            if (s.automatic && s.depth > 0 && result.count > 0) { targets = result; dirty = true; }
            jobState.store (0, std::memory_order_release);
        }
        if (! s.automatic || s.depth <= 0) { targets = {}; displayedTargets.store (0); }
        if (dirty)
        {
            int free = -1;
            for (int i = 0; i < 3; ++i)
            { int expected = 0; if (slots[static_cast<size_t> (i)].state.compare_exchange_strong (expected, 1, std::memory_order_acquire)) { free = i; break; } }
            if (free < 0) { wait (1); continue; }
            auto& slot = slots[static_cast<size_t> (free)];
            slot.kernel.build (impulse (sampleRate, s, targets), partitions);
            slot.valid = false; slot.version = ++serial;
            slot.state.store (2, std::memory_order_release);
            displayedTargets.store (targets.count, std::memory_order_relaxed);
            last = s; dirty = false;
        }
        seen = version;
        if (seen != revision.load (std::memory_order_acquire)) continue;
        wait (autoActive.load (std::memory_order_relaxed) ? 1 : 20);
    }
}
}
