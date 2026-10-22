#include "SmartHarmonics.h"
#include <cmath>
#include <cstring>
namespace zao
{
SmartAnalyser::SmartAnalyser()
{
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t> (i)] = static_cast<float> (.5-.5*std::cos (2*juce::MathConstants<double>::pi*i/(fftSize-1)));
}
float SmartAnalyser::intervalDissonance (double a, double b) noexcept
{
    if (a <= 0 || b <= 0) return 0;
    const auto distance = 12.0 * std::abs (std::log2 (a / b));
    const auto octaves = std::floor (distance / 12.0);
    const auto step = distance - 12.0 * octaves;
    // A continuous 12-TET approximation of the supplied qualitative curve:
    // beating near unison, valleys at thirds/fourth/fifth/sixth and octave.
    constexpr std::array<double, 18> x { 0, .75, 1, 2, 3, 3.5, 4, 4.5, 5, 6, 7, 8, 9, 10, 11, 11.5, 11.85, 12 };
    constexpr std::array<double, 18> y { 0, 1, .85, .4, .12, .3, .08, .2, .06, .3, 0, .12, .09, .26, .48, .65, .65, 0 };
    for (size_t i = 1; i < x.size(); ++i)
        if (step <= x[i])
        {
            const auto t = (step - x[i-1]) / (x[i] - x[i-1]);
            const auto shaped = t * t * (3 - 2*t);
            return static_cast<float> ((y[i-1] + shaped * (y[i]-y[i-1])) / (1 + .35 * octaves));
        }
    return 0;
}

uint32_t SmartAnalyser::signature (const Settings& s) noexcept
{
    uint32_t hash = 2166136261u;
    for (float value : { s.inputDb, s.driveDb, s.toneHz, s.bjtBias, s.diodeKnee,
                         static_cast<float> (s.model), s.smart && ! s.bypass && s.mix > .001f ? 1.0f : 0.0f,
                         s.clear ? 1.0f : 0.0f })
    {
        uint32_t bits = 0; std::memcpy (&bits, &value, sizeof (bits));
        hash = (hash ^ bits) * 16777619u;
    }
    return hash;
}


SmartAnalyser::Result SmartAnalyser::analyse (const Frame& frame)
{
    Result result; result.generation = frame.generation; result.signature = frame.signature;
    inputPower.fill (0); wetPower.fill (0);
    // Channel powers are summed, so anti-phase stereo cannot cancel detection.
    for (int channel = 0; channel < 4; ++channel)
    {
        const auto& source = channel == 0 ? frame.left : channel == 1 ? frame.right
                            : channel == 2 ? frame.wetLeft : frame.wetRight;
        data.fill (0);
        for (int i = 0; i < fftSize; ++i) data[static_cast<size_t> (i)] = source[static_cast<size_t> (i)] * window[static_cast<size_t> (i)];
        fft.performFrequencyOnlyForwardTransform (data.data());
        auto& spectrum = channel < 2 ? inputPower : wetPower;
        for (size_t i = 0; i < spectrum.size(); ++i) spectrum[i] += .5*data[i]*data[i];
    }
    double total = 0, maximum = 0, wetMaximum = 0;
    for (size_t i = 1; i < inputPower.size(); ++i)
    { total += inputPower[i]; maximum = std::max (maximum, inputPower[i]); wetMaximum = std::max (wetMaximum, wetPower[i]); }
    if (maximum < 1e-6 || wetMaximum < 1e-6) return result;
    const double binHz = frame.sampleRate/fftSize;
    int peakCount = 0;
    for (int i = std::max (2, static_cast<int> (40/binHz)); i < std::min (fftSize/2-1, static_cast<int> (12000/binHz)); ++i)
    {
        const auto j = static_cast<size_t> (i);
        if (inputPower[j] < maximum*.0025 || inputPower[j] <= inputPower[j-1] || inputPower[j] <= inputPower[j+1]) continue;
        const auto l = std::log (std::max (inputPower[j-1], 1e-30)), c = std::log (std::max (inputPower[j], 1e-30)), r = std::log (std::max (inputPower[j+1], 1e-30));
        const auto denominator = l-2*c+r;
        const auto offset = std::abs (denominator) > 1e-12 ? std::clamp (.5*(l-r)/denominator, -.5, .5) : 0;
        Peak peak { (i+offset)*binHz, inputPower[j-1]+inputPower[j]+inputPower[j+1] };
        auto at = std::min (peakCount, maxPeaks-1);
        if (peakCount == maxPeaks && peak.power <= peaks.back().power) continue;
        while (at > 0 && peaks[static_cast<size_t> (at-1)].power < peak.power)
        { peaks[static_cast<size_t> (at)] = peaks[static_cast<size_t> (at-1)]; --at; }
        peaks[static_cast<size_t> (at)] = peak; peakCount = std::min (maxPeaks, peakCount+1);
    }
    double captured = 0;
    for (int i = 0; i < peakCount; ++i) captured += peaks[static_cast<size_t> (i)].power;
    result.confidence = static_cast<float> (std::min (1.0, captured/std::max (total, 1e-30)));
    if (result.confidence < .5f) return result; // Silence and unpitched noise stay neutral.
    struct Candidate { Band band; double power = 0, cost = 0, priority = 0; };
    std::array<Candidate, maxBands> candidates {};
    for (int i = 0; i < peakCount; ++i)
        for (int order = 2; order <= orders; ++order)
        {
            const auto frequency = peaks[static_cast<size_t> (i)].frequency*order;
            if (frequency > frame.sampleRate*.43) break;
            const auto bandwidth = std::max (binHz*1.5, frequency*.018);
            bool protectedInput = false;
            double weighted = 0, weight = 0;
            for (int j = 0; j < peakCount; ++j)
            {
                const auto& peak = peaks[static_cast<size_t> (j)];
                if (std::abs (frequency-peak.frequency) < 4*bandwidth) protectedInput = true;
                const auto w = std::sqrt (peak.power);
                weighted += w*intervalDissonance (frequency, peak.frequency); weight += w;
            }
            if (protectedInput) continue;
            const auto bin = static_cast<size_t> (std::clamp (juce::roundToInt (frequency/binHz), 1, fftSize/2-1));
            const auto harmonicPower = wetPower[bin-1]+wetPower[bin]+wetPower[bin+1];
            if (harmonicPower < wetMaximum*.0001) continue;
            const auto cost = weighted/std::max (weight, 1e-30);
            if (cost < .035) continue;
            const auto gainDb = -std::min (6.0, cost*18.0);
            Candidate candidate { { static_cast<float> (frequency), static_cast<float> (bandwidth), static_cast<float> (gainDb), order }, harmonicPower, cost,
                harmonicPower*cost*(1-std::pow (10.0, gainDb/10.0)) };
            // Merge overlapping harmonic targets rather than stacking cuts.
            bool duplicate = false;
            for (const auto& existing : candidates)
                if (existing.priority > 0 && std::abs (existing.band.frequency-frequency) < 2*bandwidth) duplicate = true;
            if (duplicate) continue;
            auto at = maxBands-1;
            if (candidate.priority <= candidates.back().priority) continue;
            while (at > 0 && candidates[static_cast<size_t> (at-1)].priority < candidate.priority)
            { candidates[static_cast<size_t> (at)] = candidates[static_cast<size_t> (at-1)]; --at; }
            candidates[static_cast<size_t> (at)] = candidate;
        }
    for (const auto& c : candidates)
        if (c.priority > 0)
        {
            result.bands[static_cast<size_t> (result.count++)] = c.band;
            const auto order = static_cast<size_t> (c.band.order);
            const auto after = c.power*std::pow (10.0, c.band.gainDb/10.0);
            result.before[order] += static_cast<float> (c.power); result.after[order] += static_cast<float> (after);
            result.baselineCost += static_cast<float> (c.power*c.cost); result.selectedCost += static_cast<float> (after*c.cost);
        }
    return result;
}

void SmartHarmonics::Filter::configure (const SmartAnalyser::Band& band, double rate) noexcept
{
    // RBJ peaking EQ removes energy locally, rather than adjusting the circuit.
    const auto omega = 2*juce::MathConstants<double>::pi*band.frequency/rate;
    const auto a = std::pow (10.0, band.gainDb/40.0);
    const auto alpha = std::sin (omega)/(2*std::max (1.0f, band.frequency/band.bandwidth));
    const auto a0 = 1+alpha/a;
    b0 = (1+alpha*a)/a0; b1 = -2*std::cos (omega)/a0; b2 = (1-alpha*a)/a0;
    a1 = b1; a2 = (1-alpha/a)/a0; z1 = {}; z2 = {};
}
float SmartHarmonics::Filter::tick (float input, size_t ch) noexcept
{
    const auto output = b0*input+z1[ch];
    z1[ch] = b1*input-a1*output+z2[ch]; z2[ch] = b2*input-a2*output;
    return static_cast<float> (output);
}
float SmartHarmonics::Bank::tick (float input, size_t ch) noexcept
{
    for (int i = 0; i < count; ++i) input = filters[static_cast<size_t> (i)].tick (input, ch);
    return input;
}
SmartHarmonics::SmartHarmonics() : Thread ("Zao harmonic analysis") {}
SmartHarmonics::~SmartHarmonics() { release(); }
void SmartHarmonics::release() { stopThread (-1); }
void SmartHarmonics::prepare (double rate, bool worker)
{
    release(); sampleRate = rate; ring = {}; job = {}; banks = {}; settings = {};
    position = filled = sinceJob = active = 0; pending = 1; previousSignature = appliedVersion = 0;
    enabled = false; firstJob = true; previewMode = ! worker; generation.store (1); submitted.store (0); jobState.store (0);
    analysisActive.store (false, std::memory_order_relaxed);
    resultVersion.store (0); resultGeneration.store (0); resultSignature.store (0);
    for (auto& value : values) value.store (0);
    updateSamples = std::max (1, juce::roundToInt (rate*updateSeconds));
    transition.reset (updateSamples); transition.setCurrentAndTargetValue (1);
    if (worker) startThread (juce::Thread::Priority::low);
}
void SmartHarmonics::setPreview (const SmartAnalyser::Result& result) noexcept
{
    if (! previewMode) return;
    auto& bank = banks[static_cast<size_t> (active)];
    bank.count = std::clamp (result.count, 0, SmartAnalyser::maxBands);
    for (int i = 0; i < bank.count; ++i) bank.filters[static_cast<size_t> (i)].configure (result.bands[static_cast<size_t> (i)], sampleRate);
}
void SmartHarmonics::beginBlock (const Settings& s) noexcept
{
    settings = s;
    if (previewMode) return;
    const auto key = SmartAnalyser::signature (s);
    if (key != previousSignature)
    {
        previousSignature = key; generation.fetch_add (1); filled = sinceJob = position = 0; firstJob = true;
    }
    enabled = s.smart && ! s.bypass && s.mix > .001f;
    analysisActive.store (enabled, std::memory_order_relaxed);
    updateFilters();
}
void SmartHarmonics::updateFilters() noexcept
{
    if (transition.isSmoothing()) return;
    auto result = readResult();
    const auto version = result.revision;
    const auto valid = enabled && result.generation == generation.load() && result.signature == previousSignature;
    if (valid && version == appliedVersion) return;
    if (! valid && banks[static_cast<size_t> (active)].count == 0) return;
    pending = 1-active;
    auto& bank = banks[static_cast<size_t> (pending)];
    bank.count = valid ? result.count : 0;
    for (int i = 0; i < bank.count; ++i) bank.filters[static_cast<size_t> (i)].configure (result.bands[static_cast<size_t> (i)], sampleRate);
    appliedVersion = valid ? version : 0;
    transition.setCurrentAndTargetValue (0); transition.setTargetValue (1);
}
void SmartHarmonics::process (const std::array<float, 2>& input, std::array<float, 2>& wet) noexcept
{
    if (enabled)
    {
        const auto p = static_cast<size_t> (position);
        ring.left[p] = input[0]; ring.right[p] = input[1]; ring.wetLeft[p] = wet[0]; ring.wetRight[p] = wet[1];
        position = (position+1)%SmartAnalyser::fftSize; filled = std::min (SmartAnalyser::fftSize, filled+1); ++sinceJob;
        // Keep the full spectral window, but overlap successive frames so the
        // hop matches the 50 ms filter transition even below 81.92 kHz.
        const auto interval = firstJob ? SmartAnalyser::fftSize : updateSamples;
        if (filled == SmartAnalyser::fftSize && sinceJob >= interval)
        {
            int expected = 0;
            if (jobState.compare_exchange_strong (expected, 1, std::memory_order_acquire))
            {
                for (int j = 0; j < SmartAnalyser::fftSize; ++j)
                {
                    const auto from = static_cast<size_t> ((position+j)%SmartAnalyser::fftSize), to = static_cast<size_t> (j);
                    job.left[to] = ring.left[from]; job.right[to] = ring.right[from];
                    job.wetLeft[to] = ring.wetLeft[from]; job.wetRight[to] = ring.wetRight[from];
                }
                job.settings = settings; job.sampleRate = sampleRate; job.signature = previousSignature; job.generation = generation.load();
                submitted.fetch_add (1); sinceJob = 0; firstJob = false; jobState.store (2, std::memory_order_release);
            }
        }
    }
    if (! transition.isSmoothing())
    {
        for (size_t ch = 0; ch < 2; ++ch) wet[ch] = banks[static_cast<size_t> (active)].tick (wet[ch], ch);
    }
    else
    {
        const auto phase = transition.getNextValue();
        // Quintic smoothstep is an endpoint-exact sigmoid with zero first and
        // second derivatives at each end, avoiding slope jumps between banks.
        const auto blend = phase*phase*phase*(phase*(phase*6.0f-15.0f)+10.0f);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const auto old = banks[static_cast<size_t> (active)].tick (wet[ch], ch);
            const auto next = banks[static_cast<size_t> (pending)].tick (wet[ch], ch);
            wet[ch] = old+blend*(next-old);
        }
        if (! transition.isSmoothing())
        {
            active = pending;
            // A ready target starts on the next sample, without waiting for
            // another host block or interrupting the fade that just finished.
            updateFilters();
        }
    }
}
void SmartHarmonics::run()
{
    juce::ScopedNoDenormals noDenormals;
    while (! threadShouldExit())
    {
        int expected = 2;
        if (! jobState.compare_exchange_strong (expected, 3, std::memory_order_acquire))
        {
            wait (analysisActive.load (std::memory_order_relaxed) ? 2 : 20);
            continue;
        }
        const auto result = analyser.analyse (job);
        if (result.generation == generation.load()) publish (result);
        jobState.store (0, std::memory_order_release);
    }
}
void SmartHarmonics::publish (const SmartAnalyser::Result& r) noexcept
{
    resultVersion.fetch_add (1);
    resultGeneration.store (r.generation); resultSignature.store (r.signature);
    values[0].store (static_cast<float> (r.count)); values[1].store (r.baselineCost);
    values[2].store (r.selectedCost); values[3].store (r.confidence);
    for (int i = 0; i <= SmartAnalyser::orders; ++i)
    { values[static_cast<size_t> (4+i)].store (r.before[static_cast<size_t> (i)]); values[static_cast<size_t> (5+SmartAnalyser::orders+i)].store (r.after[static_cast<size_t> (i)]); }
    for (int i = 0; i < SmartAnalyser::maxBands; ++i)
    {
        const auto offset = static_cast<size_t> (4+2*(SmartAnalyser::orders+1)+4*i);
        const auto& b = r.bands[static_cast<size_t> (i)];
        values[offset].store (b.frequency); values[offset+1].store (b.bandwidth); values[offset+2].store (b.gainDb); values[offset+3].store (static_cast<float> (b.order));
    }
    resultVersion.fetch_add (1);
}
SmartAnalyser::Result SmartHarmonics::readResult() const noexcept
{
    SmartAnalyser::Result r;
    const auto version = resultVersion.load();
    if (version == 0 || (version&1u) != 0) return r;
    r.revision = version;
    r.generation = resultGeneration.load(); r.signature = resultSignature.load();
    r.count = static_cast<int> (values[0].load()); r.baselineCost = values[1].load(); r.selectedCost = values[2].load(); r.confidence = values[3].load();
    for (int i = 0; i <= SmartAnalyser::orders; ++i)
    { r.before[static_cast<size_t> (i)] = values[static_cast<size_t> (4+i)].load(); r.after[static_cast<size_t> (i)] = values[static_cast<size_t> (5+SmartAnalyser::orders+i)].load(); }
    for (int i = 0; i < SmartAnalyser::maxBands; ++i)
    {
        const auto offset = static_cast<size_t> (4+2*(SmartAnalyser::orders+1)+4*i);
        r.bands[static_cast<size_t> (i)] = { values[offset].load(), values[offset+1].load(), values[offset+2].load(), static_cast<int> (values[offset+3].load()) };
    }
    if (version != resultVersion.load() || r.generation != generation.load()) return {};
    return r;
}
}
