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

float steadyUpwardOutput (character::Model model, float inputDb, float ratio,
                          double sampleRate = 48000, float mix = 1,
                          bool bypass = false)
{
    using namespace character;
    Compressor dsp;
    dsp.prepare (sampleRate);
    Parameters p;
    p.model = model;
    p.thresholdDb = -18;
    p.ratio = ratio;
    p.kneeDb = 0;
    p.attackMs = 10;
    p.releaseMs = 150;
    p.sidechainHz = 0;
    p.driveDb = 0;
    p.mix = mix;
    p.bypass = bypass;
    p.upward = true;
    dsp.setParameters (p);
    const auto input = Compressor::gain (inputDb);
    const auto frames = static_cast<size_t> (sampleRate * 2);
    std::vector<float> left (frames, input), right = left;
    run (dsp, left, right, 37);
    return left.back();
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

    // Upward mode leaves signals above threshold at unity and raises signals
    // below threshold by the configured below-threshold ratio.
    const auto quietDb = -30.0f, thresholdDb = -18.0f, ratio = 4.0f;
    const auto expectedBoostDb = (thresholdDb - quietDb) * (1 - 1 / ratio);
    for (int m = 0; m < 4; ++m)
    {
        const auto model = static_cast<Model> (m);
        const auto above = steadyUpwardOutput (model, -6, ratio);
        check (std::abs (Compressor::db (above) - (-6)) < 0.03f,
               "upward mode preserves above-threshold unity for every model");

        const auto boosted = steadyUpwardOutput (model, quietDb, ratio);
        const auto actualBoostDb = Compressor::db (boosted) - quietDb;
        if (model == Model::variMu)
        {
            const auto effectiveRatio = 1 + (ratio - 1) * (1 - std::exp (-12.0f / 12));
            const auto muBoostDb = 12 * (1 - 1 / effectiveRatio);
            check (std::abs (actualBoostDb - muBoostDb) < 0.12f,
                   "variable-mu upward ratio rises progressively toward its maximum");
        }
        else
        {
            if (std::abs (actualBoostDb - expectedBoostDb) >= 0.12f)
                std::cerr << "Upward static curve model " << m << ": expected " << expectedBoostDb
                          << " dB boost, got " << actualBoostDb << " dB\n";
            check (std::abs (actualBoostDb - expectedBoostDb) < 0.12f,
                   "upward mode follows the below-threshold ratio for every model");
        }

        check (std::abs (steadyUpwardOutput (model, quietDb, 1) - Compressor::gain (quietDb)) < 1.0e-6f,
               "upward 1:1 remains unity for every model");
        check (steadyUpwardOutput (model, quietDb, ratio, 48000, 0) == Compressor::gain (quietDb),
               "zero wet mix preserves the exact latency-aligned dry signal");
        check (steadyUpwardOutput (model, quietDb, ratio, 48000, 1, true) == Compressor::gain (quietDb),
               "bypass preserves the exact latency-aligned input");
    }

    // Exercise upward gain at every supported sample rate and verify the
    // linked stereo channels receive the same gain.
    for (const auto rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int m = 0; m < 4; ++m)
        {
            const auto output = steadyUpwardOutput (static_cast<Model> (m), -30, 20, rate);
            check (std::isfinite (output) && Compressor::db (output) > -30 && Compressor::db (output) < -17,
                   "upward gain stays finite and below threshold at every sample rate");
        }

    Compressor upwardStereo;
    upwardStereo.prepare (48000);
    Parameters upwardParams;
    upwardParams.upward = true;
    upwardParams.thresholdDb = -18;
    upwardParams.ratio = 4;
    upwardParams.sidechainHz = 0;
    upwardStereo.setParameters (upwardParams);
    std::vector<float> upwardLeft (48000, Compressor::gain (-30)), upwardRight (48000, Compressor::gain (-42));
    run (upwardStereo, upwardLeft, upwardRight, 37);
    check (std::abs (upwardLeft.back() / Compressor::gain (-30) - upwardRight.back() / Compressor::gain (-42)) < 1.0e-5f,
           "upward compression preserves stereo linking");

    const auto renderUnequalStereo = [] (float link, bool midSide)
    {
        Compressor dsp;
        dsp.prepare (48000);
        Parameters p;
        p.thresholdDb = -24; p.ratio = 4; p.kneeDb = 0; p.sidechainHz = 0;
        p.driveDb = 0; p.stereoLink = link; p.midSide = midSide;
        dsp.setParameters (p);
        std::vector<float> left (48000, Compressor::gain (-12));
        std::vector<float> right (48000, Compressor::gain (-30));
        run (dsp, left, right);
        return std::array<float, 2> { left.back(), right.back() };
    };
    const auto independent = renderUnequalStereo (0, false);
    check (std::abs (independent[0] / Compressor::gain (-12)
                   - independent[1] / Compressor::gain (-30)) > 0.05f,
           "zero stereo link allows independent channel gain reduction");
    const auto fullyLinked = renderUnequalStereo (100, false);
    check (std::abs (fullyLinked[0] / Compressor::gain (-12)
                   - fullyLinked[1] / Compressor::gain (-30)) < 1.0e-5f,
           "full stereo link applies the same gain to both channels");
    const auto midSide = renderUnequalStereo (0, true);
    check (std::isfinite (midSide[0]) && std::isfinite (midSide[1])
           && std::abs (midSide[0] - independent[0]) > 0.01f,
           "mid/side mode encodes stereo before compression and decodes afterward");

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
           "4x transformer processing is invariant to block partitioning");

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
    const auto amplitudeAt = [] (const std::vector<float>& signal, int frequency)
    {
        double sine = 0, cosine = 0;
        for (size_t i = 48000; i < signal.size(); ++i)
        {
            const auto phase = 2 * 3.141592653589793 * frequency * static_cast<double> (i) / 48000;
            sine += signal[i] * std::sin (phase);
            cosine += signal[i] * std::cos (phase);
        }
        return std::hypot (sine, cosine) / 24000;
    };
    const auto highDriveThird = amplitudeAt (a, 300);
    const auto highDriveFundamental = amplitudeAt (a, 100);
    const auto symmetricSecond = amplitudeAt (a, 200);
    check (highDriveThird > 0.005, "output transformer produces low-frequency odd harmonics");
    Compressor lowDriveIron;
    lowDriveIron.prepare (48000);
    p.driveDb = 0;
    lowDriveIron.setParameters (p);
    a.resize (96000);
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = 0.6f * std::sin (2 * 3.141592653589793 * 100 * static_cast<double> (i) / 48000);
    b = a;
    run (lowDriveIron, a, b);
    const auto lowDriveThird = amplitudeAt (a, 300);
    const auto lowDriveFundamental = amplitudeAt (a, 100);
    check (highDriveThird / highDriveFundamental > 4 * lowDriveThird / lowDriveFundamental,
           "increasing transformer drive substantially increases low-frequency saturation harmonics");
    Compressor dcBiasedIron;
    dcBiasedIron.prepare (48000);
    p.evenHarmonics = 100;
    dcBiasedIron.setParameters (p);
    a.resize (96000);
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = 0.6f * std::sin (2 * 3.141592653589793 * 100 * static_cast<double> (i) / 48000);
    b = a;
    run (dcBiasedIron, a, b);
    const auto biasedSecond = amplitudeAt (a, 200);
    check (biasedSecond > symmetricSecond + 0.001f,
           "simulated transformer DC current increases even-order harmonics");
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
