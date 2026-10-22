#include "PhaseEngine.h"

namespace xuan
{
namespace
{
constexpr auto pi = juce::MathConstants<double>::pi;
// Leave headroom from the +/-180 degree wrap seam in the final combined response.
constexpr auto phaseLimit = static_cast<float> (150*pi/180);
float cappedPhase (double phase) { return static_cast<float> (std::clamp (phase, -static_cast<double> (phaseLimit), static_cast<double> (phaseLimit))); }
int64_t attackDeadline (int64_t onset, double rate)
{
    return onset+juce::roundToInt (rate*.05)-juce::roundToInt (rate*.025)
        -juce::roundToInt (rate*.005)-juce::roundToInt (rate*.008);
}
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
float PhaseEngine::releaseStrength (float current, double samples, double rate, float milliseconds)
{
    return current*static_cast<float> (std::exp (-std::max (0.0, samples)/(rate*.001*std::clamp (milliseconds, 20.f, 1000.f))));
}
bool PhaseEngine::transitionAllowed (int64_t streamSample, int64_t onsetSample, double rate) noexcept
{
    if (onsetSample < 0) return true;
    const auto reference = juce::roundToInt (rate*.025);
    const auto aheadSamples = juce::roundToInt (rate*.05)-reference;
    const auto safety = juce::roundToInt (rate*.008);
    // Finish the fade before delayed input reaches the FIR, and keep its entire
    // finite response stable. New late events are discarded by the audio handoff; releases of
    // an already accepted event wait until the protected hit has passed.
    const auto first = attackDeadline (onsetSample, rate);
    const auto last = onsetSample+aheadSamples+2*reference+safety;
    return streamSample < first || streamSample >= last;
}
double PhaseEngine::bandValue (double frequency, const Analysis& a, bool timing)
{
    if (a.count == 0) return 0;
    // Analyse emits ascending peak frequencies. Unwrap adjacent angles before
    // interpolation so +179/-179 degrees join across the phase seam, not zero.
    int left = 0; double angle = a.targets[0].angle;
    while (left+1 < a.count && frequency > a.targets[static_cast<size_t> (left+1)].frequency)
    {
        ++left;
        angle += std::remainder (a.targets[static_cast<size_t> (left)].angle-angle, 2*pi);
    }
    const auto& lo = a.targets[static_cast<size_t> (left)];
    const auto local = [&] (const Target& t, double unwrapped)
    {
        return timing ? static_cast<double> (t.delay)
                      : unwrapped+std::clamp (2*pi*(frequency-t.frequency)*t.delay, -pi, pi);
    };
    if (frequency < lo.frequency || left+1 == a.count)
    {
        const auto distance = (frequency-lo.frequency)/std::max (1.f, lo.width);
        // Only the outside edges taper to neutral; internal bands remain joined.
        return local (lo, angle)*std::exp (-.5*distance*distance);
    }
    const auto& hi = a.targets[static_cast<size_t> (left+1)];
    const auto next = angle+std::remainder (hi.angle-angle, 2*pi);
    const auto t = std::clamp ((frequency-lo.frequency)/std::max (1.0, static_cast<double> (hi.frequency-lo.frequency)), 0.0, 1.0);
    const auto blend = t*t*(3-2*t);
    return (1-blend)*local (lo, angle)+blend*local (hi, next);
}
namespace
{
double requestedPhase (double frequency, const Settings& s, const Analysis& a)
{
    const auto weight = 1.0/(1+std::pow (frequency/std::clamp (s.split, 80.f, 4000.f), 4));
    auto phase = (s.low*weight+s.high*(1-weight))*pi/180;
    if (s.automatic && ! juce::exactlyEqual (s.depth, 0.f))
    {
        phase += std::clamp (s.depth*.01f, -1.f, 1.f)*a.strength*PhaseEngine::bandValue (frequency, a, false);
    }
    return phase;
}
}
float PhaseEngine::phaseAt (double frequency, const Settings& s, const Analysis& a)
{
    return cappedPhase (requestedPhase (frequency, s, a));
}

namespace
{
std::vector<float> designImpulse (double rate, const std::function<float(double)>& phaseFunction, bool neutral)
{
    using Complex = std::complex<float>;
    const auto centre = juce::roundToInt (rate*.025);
    std::vector<float> kernel (static_cast<size_t> (2*centre+1), 0);
    if (neutral)
    { kernel[static_cast<size_t> (centre)] = 1; return kernel; }
    const auto order = orderFor (8*centre+1), n = 1 << order;
    juce::dsp::FFT designer (order);
    std::vector<Complex> response (static_cast<size_t> (n)), time (static_cast<size_t> (n));
    for (int k = 0; k <= n/2; ++k)
    {
        const auto phase = phaseFunction (rate*k/n);
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
}
std::vector<float> PhaseEngine::impulse (double rate, const Settings& s, const Analysis& a)
{
    const auto neutral = juce::exactlyEqual (s.low, 0.f) && juce::exactlyEqual (s.high, 0.f)
        && (! s.automatic || juce::exactlyEqual (s.depth, 0.f) || a.count == 0);
    return designImpulse (rate, [&] (double f) { return phaseAt (f, s, a); }, neutral);
}
float PhaseEngine::stereoPhaseAt (double f, const Settings& s, const StereoAnalysis& a, int channel)
{
    const auto& own = a.channel[static_cast<size_t> (channel)];
    const auto link = a.stereo ? std::clamp (s.link*.01f, -1.f, 1.f) : 0.f;
    if (link >= 1) return phaseAt (f, s, a.shared);
    const auto independent = requestedPhase (f, s, own);
    if (link >= 0) return cappedPhase (independent+link*(requestedPhase (f, s, a.shared)-independent));
    if (! s.automatic || juce::exactlyEqual (s.depth, 0.f)) return cappedPhase (independent);
    // Push already-earlier and already-later channel anchors farther apart;
    // equal anchors use L-earlier/R-later. No opposing shift in mono layouts.
    const auto rightLater = a.channel[1].anchor >= a.channel[0].anchor;
    const auto sign = (channel == 0) == rightLater ? -1.0 : 1.0;
    const auto strength = std::max (a.channel[0].count > 0 ? a.channel[0].strength : 0.f,
                                    a.channel[1].count > 0 ? a.channel[1].strength : 0.f);
    const auto shift = sign*.0025*(-link)*std::min (1.f, std::abs (s.depth)*.01f)*strength;
    return cappedPhase (independent-2*pi*f*shift);
}
std::vector<float> PhaseEngine::stereoImpulse (double rate, const Settings& s, const StereoAnalysis& a, int channel)
{
    const auto neutral = juce::exactlyEqual (s.low, 0.f) && juce::exactlyEqual (s.high, 0.f)
        && (! s.automatic || juce::exactlyEqual (s.depth, 0.f)
            || (a.shared.count == 0 && a.channel[0].count == 0 && a.channel[1].count == 0));
    return designImpulse (rate, [&] (double f) { return stereoPhaseAt (f, s, a, channel); }, neutral);
}

double PhaseEngine::meterFrequency (int row, double rate) noexcept
{
    return 20*std::pow (std::min (20000.0, rate*.45)/20, static_cast<double> (row)/(meterBins-1));
}
Analysis PhaseEngine::analyse (const std::vector<float>& input, double rate, std::vector<float>& previous, double& previousEnergy, SpectrumFrame* meter)
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
    if (meter != nullptr)
    {
        meter->fill (0);
        for (int row = 0; row < meterBins; ++row)
        {
            const auto centre = meterFrequency (row, rate);
            const auto ratio = std::sqrt (std::pow (std::min (20000.0, rate*.45)/20, 1.0/(meterBins-1)));
            const auto first = std::clamp (static_cast<int> (centre/ratio*n/rate), 1, n/2);
            const auto last = std::clamp (juce::roundToInt (centre*ratio*n/rate), first, n/2);
            float magnitude = 0;
            for (int k = first; k <= last; ++k) magnitude = std::max (magnitude, std::abs (spectrum[static_cast<size_t> (k)]));
            (*meter)[static_cast<size_t> (row)] = std::clamp ((20*std::log10 (std::max (1e-8f, 2*magnitude/n))+80)/80, 0.f, 1.f);
        }
        (*meter)[meterPeak] = peak;
        (*meter)[meterCeiling] = static_cast<float> (std::min (20000.0, rate*.45));
    }
    if (previous.size() != static_cast<size_t> (n/2+1)) previous.assign (static_cast<size_t> (n/2+1), 0);
    double flux = 0, total = 0; float largest = 0;
    for (int k = 1; k < n/2; ++k)
    {
        const auto m = std::abs (spectrum[static_cast<size_t> (k)]);
        flux += std::max (0.f, m-previous[static_cast<size_t> (k)]); total += m;
        previous[static_cast<size_t> (k)] = m; largest = std::max (largest, m);
    }
    energy /= n;
    Analysis result; result.anchor = anchor/rate; result.flux = static_cast<float> (flux/std::max (1e-12, total));
    result.crest = static_cast<float> (peak/std::sqrt (std::max (1e-12, energy)));
    const auto onset = energy > 1e-7 && result.crest > 2
        && ((energy > previousEnergy*1.5 && result.flux > .15f) || (result.flux > .4f && result.crest > 3));
    previousEnergy = energy;
    if (meter != nullptr && onset) (*meter)[meterTransient] = std::clamp ((result.crest-2)/4, 0.f, 1.f);
    if (! onset) return result;
    const auto sign = input[static_cast<size_t> (anchor)] < 0 ? pi : 0;
    // Reserve a strongest peak per logarithmic region before filling remaining
    // slots by magnitude: a low-to-high first-24 scan starves the upper bands.
    struct Candidate { Target target; float magnitude; bool selected = false; };
    std::vector<Candidate> candidates;
    const auto ceiling = std::min (20000.0, rate*.45);
    for (int k = 2; k < n/2-1; ++k)
    {
        const auto frequency = rate*k/n;
        if (frequency < 60 || frequency > ceiling) continue;
        const auto m = previous[static_cast<size_t> (k)];
        if (m < largest*.08f || m <= previous[static_cast<size_t> (k-1)] || m < previous[static_cast<size_t> (k+1)]) continue;
        const auto angle = std::remainder (sign-2*pi*k*anchor/n-std::arg (spectrum[static_cast<size_t> (k)]), 2*pi);
        // Remove the anchor's time origin before unwrapping the local slope;
        // otherwise a late transient aliases its group delay by half a frame.
        const auto slope = std::remainder (std::arg (spectrum[static_cast<size_t> (k+1)])-std::arg (spectrum[static_cast<size_t> (k-1)])+4*pi*anchor/n, 2*pi);
        const auto relativeTime = -slope/(4*pi*rate/n);
        candidates.push_back ({ {
            static_cast<float> (frequency), static_cast<float> (std::max (rate/n, frequency*.025)),
            static_cast<float> (angle), static_cast<float> (std::clamp (relativeTime, -.025, .025)) }, m });
        ++k;
    }
    std::array<int, 24> regions; regions.fill (-1);
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        const auto region = std::clamp (static_cast<int> (24*std::log (candidates[i].target.frequency/60)/std::log (ceiling/60)), 0, 23);
        auto& strongest = regions[static_cast<size_t> (region)];
        if (strongest < 0 || candidates[i].magnitude > candidates[static_cast<size_t> (strongest)].magnitude)
            strongest = static_cast<int> (i);
    }
    for (int i : regions)
        if (i >= 0) { candidates[static_cast<size_t> (i)].selected = true; ++result.count; }
    while (result.count < static_cast<int> (result.targets.size()))
    {
        auto best = candidates.end();
        for (auto it = candidates.begin(); it != candidates.end(); ++it)
            if (! it->selected && (best == candidates.end() || it->magnitude > best->magnitude)) best = it;
        if (best == candidates.end()) break;
        best->selected = true; ++result.count;
    }
    int index = 0;
    for (const auto& candidate : candidates)
        if (candidate.selected) result.targets[static_cast<size_t> (index++)] = candidate.target;
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
void PhaseEngine::prepareMeter (Slot& slot, const std::vector<float>& ir, int channel)
{
    for (int row = 0; row < meterBins; ++row)
    {
        const auto f = meterFrequency (row, sampleRate), omega = 2*pi*f/sampleRate;
        const auto step = std::polar (1.0, -omega);
        auto phasor = std::polar (1.0, omega*delay); std::complex<double> response {};
        for (auto tap : ir) { response += static_cast<double> (tap)*phasor; phasor *= step; }
        slot.response[static_cast<size_t> (channel)][static_cast<size_t> (row)] = static_cast<Complex> (response);
    }
}
void PhaseEngine::publishRotation()
{
    RotationFrame frame {};
    const auto t = pending >= 0 ? sigmoid (std::min (1.f, static_cast<float> (fadePosition)/fadeLength)) : 0.f;
    for (int channel = 0; channel < 2; ++channel)
        for (size_t row = 0; row < meterBins; ++row)
        {
            auto response = slots[static_cast<size_t> (active)].response[static_cast<size_t> (channel)][row];
            if (pending >= 0)
            {
                const auto next = slots[static_cast<size_t> (pending)].response[static_cast<size_t> (channel)][row];
                response += t*(next-response);
            }
            response += meterBypass*(Complex (1, 0)-response);
            const auto offset = channel == 0 ? size_t (0) : size_t (2*meterBins+1);
            frame[offset+row] = response.real(); frame[offset+meterBins+row] = response.imag();
        }
    frame[2*meterBins] = meterBypass;
    rotationMeter.publish (frame);
}
void PhaseEngine::prepare (double rate, std::function<Settings()> provider, bool worker)
{
    release(); sampleRate = rate; readSettings = std::move (provider);
    delay = juce::roundToInt (rate*.025); lookahead = juce::roundToInt (rate*.05)-delay; aheadPosition = 0; partitions = (2*delay+1-hop+hop-1)/hop;
    partitions = std::max (1, partitions); fadeLength = std::max (1, juce::roundToInt (rate*.005));
    history = {}; position = dryPosition = ringPosition = 0; spectrumPosition = -1;
    active = 0; pending = -1; fadePosition = 0;
    meterHopCounter = 0; meterBypass = 0; spectrumMeter.reset(); rotationMeter.reset();
    for (auto& channel : spectra) channel.assign (static_cast<size_t> ((partitions+1)*fftSize), {});
    for (auto& channel : dry) channel.assign (static_cast<size_t> (latency()), 0);
    for (auto& channel : ahead) channel.assign (static_cast<size_t> (lookahead), 0);
    const auto analysisSize = 1 << orderFor (juce::roundToInt (rate*.04));
    analysisRing.assign (static_cast<size_t> (analysisSize), 0); leftAnalysisRing = rightAnalysisRing = analysisRing; analysisJob = analysisRing;
    channelJobs = {analysisRing, analysisRing}; channelCount = jobChannels = 2;
    for (auto& slot : slots) { slot.state.store (0); slot.onsets.fill (-1); slot.valid = false; slot.tail = {}; slot.overlap = {}; }
    processedSamples = jobSample = 0;
    jobState.store (0); displayedTargets.store (0); autoActive.store (false); revision.store (1);
    const auto s = readSettings(); preparedSettings = s;
    const auto ir = impulse (rate, s);
    for (int ch = 0; ch < 2; ++ch) { slots[0].kernels[static_cast<size_t> (ch)].build (ir, partitions); prepareMeter (slots[0], ir, ch); }
    slots[0].state.store (3);
    bypassBlend.reset (rate, .005); bypassBlend.setCurrentAndTargetValue (s.bypass ? 1.f : 0.f);
    meterBypass = s.bypass ? 1.f : 0.f; publishRotation();
    if (worker) startThread (juce::Thread::Priority::normal);
}
void PhaseEngine::setOffline (const Settings& s, const Analysis& a)
{
    jassert (! isThreadRunning());
    const auto ir = impulse (sampleRate, s, a);
    for (int ch = 0; ch < 2; ++ch) { slots[0].kernels[static_cast<size_t> (ch)].build (ir, partitions); prepareMeter (slots[0], ir, ch); }
    publishRotation();
}
void PhaseEngine::setStereoOffline (const Settings& s, const StereoAnalysis& a)
{
    jassert (! isThreadRunning());
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto ir = stereoImpulse (sampleRate, s, a, ch);
        slots[0].kernels[static_cast<size_t> (ch)].build (ir, partitions); prepareMeter (slots[0], ir, ch);
    }
    publishRotation();
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
            transformTail (slot.kernels[static_cast<size_t> (ch)], ch, 1);
            for (int i = 0; i < hop; ++i) slot.overlap[static_cast<size_t> (ch)][static_cast<size_t> (i)] = inverse[static_cast<size_t> (i+hop)].real();
        }
        transformTail (slot.kernels[static_cast<size_t> (ch)], ch, 0);
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
    processedSamples += hop;
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
        if (newest >= 0 && ! slots[static_cast<size_t> (newest)].parameterChange)
        {
            const auto& candidate = slots[static_cast<size_t> (newest)];
            bool missedAttack = false;
            for (size_t event = 0; event < candidate.onsets.size(); ++event)
            {
                const auto onset = candidate.onsets[event];
                if (onset >= 0 && onset != slots[static_cast<size_t> (active)].onsets[event]
                    && static_cast<int64_t> (processedSamples) >= attackDeadline (onset, sampleRate))
                {
                    missedAttack = true;
                }
            }
            // A new target is useful only before its note. Applying it after
            // the protected FIR response still bends a piano's loud sustain.
            // Reject every later revision of that missed event too; accepted
            // events may continue their normal release toward the manual curve.
            if (missedAttack)
            {
                for (auto& slot : slots)
                    if (slot.state.load (std::memory_order_acquire) == 2)
                        slot.state.store (0, std::memory_order_release);
                newest = -1;
            }
        }
        if (newest >= 0 && std::all_of (slots[static_cast<size_t> (newest)].onsets.begin(),
                                        slots[static_cast<size_t> (newest)].onsets.end(),
                                        [&] (int64_t onset) { return transitionAllowed (static_cast<int64_t> (processedSamples), onset, sampleRate); }))
        {
            for (int i = 0; i < 3; ++i)
                if (i != newest && slots[static_cast<size_t> (i)].state.load() == 2) slots[static_cast<size_t> (i)].state.store (0, std::memory_order_release);
            pending = newest; slots[static_cast<size_t> (pending)].state.store (3); fadePosition = 0;
        }
    }
    renderTail (active); if (pending >= 0) renderTail (pending);
    if (meterVisible.load (std::memory_order_relaxed)) publishRotation();
    meterHopCounter += hop;
    const auto displayDue = meterHopCounter >= juce::roundToInt (sampleRate/30);
    if (displayDue) meterHopCounter = 0;
    if (autoActive.load (std::memory_order_relaxed) || (meterVisible.load (std::memory_order_relaxed) && displayDue))
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
                analysisJob[static_cast<size_t> (i)] = mid;
                channelJobs[0][static_cast<size_t> (i)] = left;
                channelJobs[1][static_cast<size_t> (i)] = rightAnalysisRing[at];
                midEnergy += mid*mid; leftEnergy += left*left;
            }
            // Select one reference for the entire frame, rather than switching
            // channels at individual zero crossings in anti-phase material.
            if (midEnergy < leftEnergy*.01)
                for (int i = 0; i < n; ++i) analysisJob[static_cast<size_t> (i)] = leftAnalysisRing[static_cast<size_t> ((ringPosition+i)%n)];
            jobChannels = channelCount; jobSample = processedSamples;
            jobState.store (2, std::memory_order_release);
        }
    }
}
void PhaseEngine::process (juce::AudioBuffer<float>& buffer, const Settings& s, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = std::min (2, buffer.getNumChannels());
    if (dry[0].empty()) return;
    autoActive.store (s.automatic && ! juce::exactlyEqual (s.depth, 0.f) && ! s.bypass && ! hostBypass, std::memory_order_relaxed);
    channelCount = channels;
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
            // Analysis observes raw input; convolution receives it 25 ms later.
            const auto delayed = ahead[c][static_cast<size_t> (aheadPosition)];
            ahead[c][static_cast<size_t> (aheadPosition)] = in[c];
            history[c][static_cast<size_t> (position)] = history[c][static_cast<size_t> (position+hop)] = delayed;
            bypassed[c] = dry[c][static_cast<size_t> (dryPosition)]; dry[c][static_cast<size_t> (dryPosition)] = in[c];
            const auto evaluate = [&] (int index)
            {
                const auto& slot = slots[static_cast<size_t> (index)];
                return slot.tail[c][static_cast<size_t> (position)] + (slot.kernels[c].hasHead
                    ? dot (slot.kernels[c].head.data(), history[c].data()+position+1, hop) : 0.f);
            };
            const auto old = evaluate (active);
            out[c] = pending >= 0 ? old+blend*(evaluate (pending)-old) : old;
        }
        const auto mid = channels == 1 ? in[0] : .5f*(in[0]+in[1]);
        analysisRing[static_cast<size_t> (ringPosition)] = mid;
        leftAnalysisRing[static_cast<size_t> (ringPosition)] = in[0];
        rightAnalysisRing[static_cast<size_t> (ringPosition)] = channels == 1 ? in[0] : in[1];
        ringPosition = (ringPosition+1)%static_cast<int> (analysisRing.size());
        const auto bypass = bypassBlend.getNextValue();
        meterBypass = hostBypass ? 1.f : bypass;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<size_t> (ch); const auto wet = out[c];
            buffer.setSample (ch, i, hostBypass || bypass >= 1 ? bypassed[c] : wet+bypass*(bypassed[c]-wet));
        }
        if (pending >= 0 && ++fadePosition >= fadeLength)
        {
            slots[static_cast<size_t> (active)].state.store (0, std::memory_order_release);
            active = pending; pending = -1;
        }
        dryPosition = (dryPosition+1)%latency();
        aheadPosition = (aheadPosition+1)%lookahead;
        if (++position == hop) { position = 0; finishHop(); }
    }
}
void PhaseEngine::run()
{
    auto last = preparedSettings; uint32_t seen = revision.load(), serial = 0; bool dirty = false;
    uint64_t lastAnalysisSample = 0;
    std::array<uint64_t, 3> lastReleaseSample {};
    std::array<float, 3> releaseGain {};
    std::array<Analysis, 3> collected;
    std::array<int64_t, 3> eventOnsets {{-1, -1, -1}};
    std::array<uint64_t, 3> collectUntil {}, holdUntil {};
    std::array<bool, 3> collecting {};
    StereoAnalysis targets; Analysis displayedAnalysis; std::vector<float> previous, displayPrevious;
    std::array<std::vector<float>, 2> channelPrevious;
    std::array<double, 2> channelEnergy {};
    double previousEnergy = 0, displayEnergy = 0;
    float transientHold = 0; double lastMeter = juce::Time::getMillisecondCounterHiRes();
    while (! threadShouldExit())
    {
        const auto s = readSettings(); const auto version = revision.load (std::memory_order_acquire);
        dirty |= ! (s == last);
        if (jobState.load (std::memory_order_acquire) == 2)
        {
            SpectrumFrame spectrum;
            const auto visible = meterVisible.load (std::memory_order_relaxed);
            // Display-only frames must not change the correction detector's
            // history or generate responses while Auto is inactive/bypassed.
            const auto correcting = s.automatic && ! juce::exactlyEqual (s.depth, 0.f) && autoActive.load (std::memory_order_relaxed);
            const auto result = analyse (analysisJob, sampleRate, correcting ? previous : displayPrevious,
                                         correcting ? previousEnergy : displayEnergy, visible ? &spectrum : nullptr);
            if (visible && meterVisible.load (std::memory_order_relaxed))
            {
                const auto now = juce::Time::getMillisecondCounterHiRes();
                transientHold = std::max (spectrum[meterTransient], transientHold*static_cast<float> (std::exp (-(now-lastMeter)/60)));
                if (result.count > 0) displayedAnalysis = result;
                for (int row = 0; row < meterBins; ++row)
                {
                    const auto f = meterFrequency (row, sampleRate);
                    spectrum[static_cast<size_t> (meterBins+row)] = static_cast<float> (1000*bandValue (f, displayedAnalysis, true))*transientHold;
                }
                lastMeter = now; spectrum[meterTransient] = transientHold; spectrumMeter.publish (spectrum);
            }
            const auto update = [&] (Analysis& held, const Analysis& detected, size_t index)
            {
                if (! correcting) return;
                if (detected.count > 0)
                {
                    auto next = detected;
                    next.anchor += (static_cast<double> (jobSample)-static_cast<double> (analysisJob.size()))/sampleRate;
                    const auto anchor = static_cast<int64_t> (std::llround (next.anchor*sampleRate));
                    // The same onset is visible in many overlapping FFT frames.
                    // Collect 5 ms of context, then latch once instead of retuning
                    // the filter for every frame of that same transient.
                    if (eventOnsets[index] < 0 || anchor-eventOnsets[index] > juce::roundToInt (sampleRate*.04))
                    {
                        collecting[index] = true; collected[index] = next; eventOnsets[index] = anchor;
                        collectUntil[index] = jobSample+static_cast<uint64_t> (juce::roundToInt (sampleRate*.005));
                    }
                    else if (collecting[index]) collected[index] = next;
                }
                if (collecting[index])
                {
                    if (jobSample < collectUntil[index]) return;
                    held = collected[index]; collecting[index] = false; releaseGain[index] = 1;
                    eventOnsets[index] = static_cast<int64_t> (std::llround (held.anchor*sampleRate));
                    holdUntil[index] = static_cast<uint64_t> (std::max (int64_t (0), eventOnsets[index]))
                        +static_cast<uint64_t> (lookahead+2*delay+juce::roundToInt (sampleRate*.008));
                    lastReleaseSample[index] = holdUntil[index]; dirty = true;
                }
                else if (held.count > 0 && jobSample > holdUntil[index])
                {
                    const auto elapsed = jobSample-std::max (lastAnalysisSample, holdUntil[index]);
                    releaseGain[index] = releaseStrength (releaseGain[index], static_cast<double> (elapsed), sampleRate, s.transient);
                    if (jobSample-lastReleaseSample[index] >= static_cast<uint64_t> (sampleRate*.02))
                    {
                        held.strength = releaseGain[index]; lastReleaseSample[index] = jobSample; dirty = true;
                        if (releaseGain[index] < .001f) { const auto anchor = held.anchor; held = {}; held.anchor = anchor; }
                    }
                }
            };
            update (targets.shared, result, 0);
            targets.stereo = jobChannels > 1;
            if (correcting && (s.link < 100 || ! targets.stereo))
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    const auto channelResult = analyse (channelJobs[ch], sampleRate, channelPrevious[ch], channelEnergy[ch]);
                    update (targets.channel[ch], channelResult, ch+1);
                }
            else if (correcting)
                for (size_t ch = 0; ch < 2; ++ch) update (targets.channel[ch], {}, ch+1);
            lastAnalysisSample = jobSample;
            jobState.store (0, std::memory_order_release);
        }
        if (! s.automatic || juce::exactlyEqual (s.depth, 0.f))
        {
            targets = {}; collecting.fill (false); eventOnsets.fill (-1); displayedTargets.store (0);
        }
        if (dirty)
        {
            int free = -1;
            for (int i = 0; i < 3; ++i)
            { int expected = 0; if (slots[static_cast<size_t> (i)].state.compare_exchange_strong (expected, 1, std::memory_order_acquire)) { free = i; break; } }
            if (free < 0) { wait (1); continue; }
            auto& slot = slots[static_cast<size_t> (free)];
            for (int ch = 0; ch < 2; ++ch)
            {
                if (ch == 1 && targets.stereo && s.link >= 100)
                {
                    slot.kernels[1] = slot.kernels[0]; slot.response[1] = slot.response[0];
                }
                else
                {
                    const auto ir = stereoImpulse (sampleRate, s, targets, ch);
                    slot.kernels[static_cast<size_t> (ch)].build (ir, partitions); prepareMeter (slot, ir, ch);
                }
            }
            slot.onsets = eventOnsets;
            if (targets.shared.count == 0) slot.onsets[0] = -1;
            for (size_t channel = 0; channel < 2; ++channel)
                if (targets.channel[channel].count == 0) slot.onsets[channel+1] = -1;
            // Only detectors contributing to this response constrain its handoff.
            if (s.link <= 0 || ! targets.stereo) slot.onsets[0] = -1;
            else if (s.link >= 100) slot.onsets[1] = slot.onsets[2] = -1;
            slot.parameterChange = ! (s == last);
            slot.valid = false; slot.version = ++serial;
            slot.state.store (2, std::memory_order_release);
            const auto independentCount = std::max (targets.channel[0].count, targets.channel[1].count);
            const auto count = ! targets.stereo || s.link <= 0 ? independentCount
                : s.link >= 100 ? targets.shared.count : std::max (independentCount, targets.shared.count);
            displayedTargets.store (count, std::memory_order_relaxed);
            last = s; dirty = false;
        }
        seen = version;
        if (seen != revision.load (std::memory_order_acquire)) continue;
        wait (autoActive.load (std::memory_order_relaxed) ? 1 : meterVisible.load (std::memory_order_relaxed) ? 5 : 20);
    }
}
}
