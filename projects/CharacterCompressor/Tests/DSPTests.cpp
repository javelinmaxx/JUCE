#include "../Source/CompressorDSP.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace
{
int checks = 0;
void check (bool ok, const char* message)
{
    ++checks;
    if (! ok) { std::cerr << "FAIL: " << message << '\n'; std::exit (1); }
}

void run (character::Compressor& dsp, std::vector<float>& left, std::vector<float>& right,
          int block = 256)
{
    for (size_t i = 0; i < left.size(); i += static_cast<size_t> (block))
    {
        float* channels[] { left.data() + i, right.data() + i };
        dsp.process (channels, 2, static_cast<int> (std::min (static_cast<size_t> (block), left.size() - i)));
    }
}

std::vector<float> delayed (const std::vector<float>& signal)
{
    std::vector<float> result (signal.size(), 0);
    constexpr auto delay = character::Compressor::latencySamples;
    if (signal.size() > delay)
        std::copy (signal.begin(), signal.end() - delay, result.begin() + delay);
    return result;
}

float steadyGain (character::Model model, float ratio)
{
    character::Compressor dsp;
    dsp.prepare (48000);
    character::Parameters p;
    p.model = model; p.ratio = ratio; p.sidechainHz = 0; p.kneeDb = 0;
    dsp.setParameters (p);
    std::vector<float> a (48000 * 3, 0.5f), b = a;
    run (dsp, a, b);
    return a.back() / 0.5f;
}
}

int main()
{
    using namespace character;
    // Closed-loop VCA compensation should produce the same steady 4:1 slope
    // as feed-forward FET, despite using a different detector topology.
    const auto expected = Compressor::gain (-(Compressor::db (0.5f) + 18) * 0.75f);
    check (std::abs (steadyGain (Model::fet, 4) - expected) < 0.002f, "FET static 4:1 curve");
    check (std::abs (steadyGain (Model::vca, 4) - expected) < 0.002f, "feedback VCA static 4:1 curve");
    const auto mu = steadyGain (Model::variMu, 4);
    check (mu > expected + 0.01f && mu < 0.95f, "variable-mu progressive curve");
    check (std::abs (steadyGain (Model::optical, 4) - expected) < 0.005f, "optical static curve");
    for (int m = 0; m < 4; ++m)
        check (std::abs (steadyGain (static_cast<Model> (m), 1) - 1) < 0.0001f, "1:1 is unity");

    std::vector<float> source (48000);
    for (size_t i = 0; i < source.size(); ++i)
        source[i] = 0.8f * std::sin (2 * 3.141592653589793 * 997 * static_cast<double> (i) / 48000);

    for (int m = 0; m < 4; ++m)
    {
        Compressor dsp;
        dsp.prepare (48000);
        Parameters p;
        p.model = static_cast<Model> (m);
        dsp.setParameters (p);
        auto a = source, b = source;
        for (auto& x : b) x *= 0.25f;
        run (dsp, a, b);
        bool linked = true;
        for (size_t i = 0; i < a.size(); ++i)
            linked = linked && std::abs (b[i] - a[i] * 0.25f) < 1.0e-6f;
        check (linked, "stereo detector applies identical gain to both channels");
        check (dsp.reductionDb() > 1, "meter reports gain reduction");

        // Block size must not change the waveform or the detector history.
        Compressor other;
        other.prepare (48000); other.setParameters (p);
        auto c = source, d = source;
        for (auto& x : d) x *= 0.25f;
        run (other, c, d, 17);
        check (a == c && b == d, "block partition invariance");
    }

    Compressor dry;
    dry.prepare (48000);
    Parameters p;
    p.mix = 0;
    dry.setParameters (p);
    auto a = source, b = source;
    run (dry, a, b);
    check (a == delayed (source) && b == delayed (source), "zero mix is exact latency-aligned dry signal");

    p.bypass = true; p.mix = 1; p.outputDb = 18; p.transformer = true;
    Compressor bypass;
    bypass.prepare (48000); bypass.setParameters (p);
    a = b = source;
    run (bypass, a, b);
    check (a == delayed (source) && b == delayed (source), "bypass is exact delayed input, independent of transformer/output gain");

    // The host latency must describe the actual clean-path delay, including
    // startup silence and operation across arbitrary block boundaries.
    Compressor impulse;
    impulse.prepare (48000);
    Parameters unity;
    unity.ratio = 1;
    impulse.setParameters (unity);
    a.assign (256, 0); a[0] = 1; b = a;
    run (impulse, a, b, 17);
    check (std::distance (a.begin(), std::max_element (a.begin(), a.end())) == 64,
           "clean impulse arrives at the declared 64-sample latency");
    check (a[64] == 1 && std::count (a.begin(), a.end(), 0.0f) == 255,
           "disabled transformer introduces only an exact delay");

    Compressor osA, osB;
    osA.prepare (48000); osB.prepare (48000);
    unity.transformer = true; unity.driveDb = 12;
    osA.setParameters (unity); osB.setParameters (unity);
    auto osLeft = source, osRight = source, osOtherLeft = source, osOtherRight = source;
    run (osA, osLeft, osRight, 256);
    run (osB, osOtherLeft, osOtherRight, 17);
    check (osLeft == osOtherLeft && osRight == osOtherRight,
           "2x transformer processing is invariant to block partitioning");

    // Automation should ramp rather than instantaneously jump to a new gain/model.
    Compressor automated;
    automated.prepare (48000);
    p = {}; p.sidechainHz = 0;
    automated.setParameters (p);
    a.assign (48000, 0.5f); b = a;
    run (automated, a, b);
    const auto before = a.back();
    p.model = Model::vca; p.outputDb = 18; p.thresholdDb = -35; p.transformer = true;
    automated.setParameters (p);
    float first = 0.5f, second = 0.5f;
    float* frame[] { &first, &second };
    automated.process (frame, 2, 1);
    check (std::abs (first - before) < 0.01f, "automation/model switch begins without a step");
    p.bypass = true;
    automated.setParameters (p);
    a.assign (48000, 0.5f); b = a;
    run (automated, a, b);
    check (a.back() == 0.5f, "bypass settles to exact unity");

    // Use a 100 Hz probe so its third harmonic lies below the new 500 Hz
    // residual cutoff. Measure magnitude to retain the transformer's phase.
    Compressor iron;
    iron.prepare (48000);
    p = {}; p.ratio = 1; p.transformer = true; p.driveDb = 18;
    iron.setParameters (p);
    a.resize (96000);
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = 0.6f * std::sin (2 * 3.141592653589793 * 100 * static_cast<double> (i) / 48000);
    b = a;
    run (iron, a, b);
    double thirdSin = 0, thirdCos = 0;
    for (size_t i = 48000; i < a.size(); ++i)
    {
        const auto phase = 2 * 3.141592653589793 * 300 * static_cast<double> (i) / 48000;
        thirdSin += a[i] * std::sin (phase);
        thirdCos += a[i] * std::cos (phase);
    }
    check (std::hypot (thirdSin, thirdCos) / 24000 > 0.005, "output transformer produces low-frequency odd harmonics");
    a.assign (96000, 0.5f); b = a;
    run (iron, a, b);
    check (std::abs (a.back()) < 0.001f, "transformer rejects DC");

    // Silence, out-of-range host controls, extreme settings, and multiple rates.
    std::mt19937 random (12345);
    std::uniform_real_distribution<float> noise (-2, 2);
    for (const auto rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int m = 0; m < 4; ++m)
        {
            Compressor dsp;
            dsp.prepare (rate);
            p = {}; p.model = static_cast<Model> (m); p.thresholdDb = -60;
            p.ratio = 20; p.attackMs = 0.05f; p.releaseMs = 10;
            p.inputDb = 24; p.outputDb = 24; p.driveDb = 18; p.transformer = true;
            dsp.setParameters (p);
            a.resize (24000); b.resize (24000);
            for (size_t i = 0; i < a.size(); ++i) { a[i] = noise (random); b[i] = noise (random); }
            run (dsp, a, b, 37);
            bool finite = true;
            for (size_t i = 0; i < a.size(); ++i)
                finite = finite && std::isfinite (a[i]) && std::isfinite (b[i])
                                // Clean highs are no longer amplitude-limited by
                                // full-band tanh. Allow the existing +24 dB output
                                // gain and filter transients at extreme controls.
                                && std::abs (a[i]) < 4096 && std::abs (b[i]) < 4096;
            check (finite, "extreme settings stay finite at every supported rate");
            dsp.reset(); dsp.setParameters (p);
            a.assign (1024, 0); b = a;
            run (dsp, a, b);
            check (a.back() == 0 && b.back() == 0, "reset clears all detector/transformer history");
        }

    Compressor invalid;
    invalid.prepare (std::numeric_limits<double>::quiet_NaN());
    p = {}; p.ratio = std::numeric_limits<float>::quiet_NaN();
    invalid.setParameters (p);
    float nan = std::numeric_limits<float>::quiet_NaN(), zero = 0;
    float* bad[] { &nan, &zero };
    invalid.process (bad, 2, 1);
    check (std::isfinite (nan), "nonfinite inputs/parameters do not poison DSP state");
    std::cout << checks << " DSP checks passed\n";
}
