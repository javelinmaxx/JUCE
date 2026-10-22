#include "../Source/PluginEditor.h"
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace
{
int checks = 0;
struct SolverMeasurement { double rate, level, bjtResidual, opampResidual; };
std::vector<SolverMeasurement> solverMeasurements;
struct DiodeMeasurement { double rate, shuntResidual, seriesResidual; };
std::vector<DiodeMeasurement> diodeMeasurements;
double aliasReductionDb = 0, siliconFlatFraction = 0, schottkyFlatFraction = 0;
struct ShapeResidual { double rate, control, bjt, shunt, series; };
std::vector<ShapeResidual> shapeResiduals;
void check (bool pass, const char* message)
{
    ++checks;
    if (! pass) throw std::runtime_error (message);
}
void set (ZaoDriveProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    if (parameter == nullptr) throw std::runtime_error (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}
void selectMode (ZaoDriveProcessor& processor, int mode)
{
    set (processor, "model", static_cast<float> (mode % 2));
    set (processor, "diodeCircuit", mode >= 2 ? 1.0f : 0.0f);
}
bool finite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (ch, i))) return false;
    return true;
}
double amplitude (const std::vector<float>& samples, double hz, double rate = 48000)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto phase = 2.0 * zao::pi * hz * static_cast<double> (i) / rate;
        re += samples[i] * std::cos (phase); im += samples[i] * std::sin (phase);
    }
    return 2.0 * std::hypot (re, im) / static_cast<double> (samples.size());
}
std::vector<float> render (zao::Settings s, int count = 9600, double hz = 1000, double input = 0.25)
{
    zao::DriveEngine engine; engine.prepare (48000, 128);
    juce::AudioBuffer<float> block (2, 128);
    std::vector<float> result;
    for (int offset = 0; offset < count; offset += 128)
    {
        const auto n = std::min (128, count - offset);
        block.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const auto sample = static_cast<float> (input * std::sin (2.0 * zao::pi * hz * (offset + i) / 48000.0));
            block.setSample (0, i, sample); block.setSample (1, i, sample);
        }
        engine.process (block, s);
        for (int i = 0; i < n; ++i) result.push_back (block.getSample (0, i));
    }
    return result;
}

void circuitTests()
{
    zao::BjtCommonEmitter transistor; transistor.prepare (192000);
    const auto dc = transistor.getOperatingPoint();
    check (transistor.getResidual() < 1.0e-9 && dc[0] > dc[1] && dc[2] > dc[0],
           "BJT bias converges to a forward-active operating point");
    // Independent DC resistor currents must satisfy supply and emitter loading.
    const auto collectorCurrent = (9.0 - dc[2]) / 4700.0;
    const auto emitterCurrent = dc[1] / 1040.0;
    const auto baseCurrent = (9.0 - dc[0]) / 100000.0 - dc[0] / 22000.0;
    check (std::abs (emitterCurrent - collectorCurrent - baseCurrent) < 1.0e-9,
           "BJT DC operating point conserves current through the physical bias network");
    for (double rate : { 128000.0, 192000.0, 384000.0 })
        for (double level : { 0.035, 0.35, 3.5, 12.0 })
        {
            zao::BjtCommonEmitter bjt; bjt.prepare (rate);
            zao::OpAmpFeedbackClipper opamp; opamp.prepare (rate);
            double maxBjtResidual = 0, maxOpampResidual = 0, maxRail = 0;
            bool valid = true;
            for (int i = 0; i < 4096; ++i)
            {
                const auto input = level * std::sin (2.0 * zao::pi * 1300.0 * i / rate);
                valid = valid && std::isfinite (bjt.process (input)) && std::isfinite (opamp.process (input));
                maxBjtResidual = std::max (maxBjtResidual, bjt.getResidual());
                maxOpampResidual = std::max (maxOpampResidual, opamp.getResidual());
                maxRail = std::max (maxRail, std::abs (opamp.getOutputVolts()));
            }
            check (valid && maxBjtResidual < 1.0e-7, "charge-based BJT solve converges through cutoff and saturation");
            check (maxOpampResidual < 1.0e-6 && maxRail <= 4.000001,
                   "feedback diode solve respects finite op-amp rails and converges");
            solverMeasurements.push_back ({ rate, level, maxBjtResidual, maxOpampResidual });
        }
    zao::OpAmpFeedbackClipper positive, negative;
    positive.prepare (192000); negative.prepare (192000);
    double symmetryError = 0;
    for (int i = 0; i < 4096; ++i)
    {
        const auto x = 2.0 * std::sin (2.0 * zao::pi * 500 * i / 192000.0);
        symmetryError = std::max (symmetryError, std::abs (positive.process (x) + negative.process (-x)));
    }
    check (symmetryError < 1.0e-8, "antiparallel feedback diodes produce odd-symmetric clipping");
    zao::BjtCommonEmitter transientBjt; transientBjt.prepare (192000);
    zao::OpAmpFeedbackClipper transientOpamp; transientOpamp.prepare (192000);
    std::mt19937 random (42); std::uniform_real_distribution<double> noise (-12.0, 12.0);
    double bjtResidual = 0, opampResidual = 0, slew = 0, previousOutput = 0;
    for (int i = 0; i < 8192; ++i)
    {
        const auto input = i < 4096 ? noise (random) : (i % 128 < 64 ? 12.0 : -12.0);
        transientBjt.process (input); transientOpamp.process (input);
        bjtResidual = std::max (bjtResidual, transientBjt.getResidual());
        opampResidual = std::max (opampResidual, transientOpamp.getResidual());
        slew = std::max (slew, std::abs (transientOpamp.getOutputVolts() - previousOutput));
        previousOutput = transientOpamp.getOutputVolts();
    }
    check (bjtResidual < 1.0e-7 && opampResidual < 1.0e-6,
           "both nonlinear solves converge through broadband noise and abrupt saturation reversals");
    check (slew <= zao::OpAmpFeedbackClipper::slewRate / 192000 + 1.0e-6,
           "op-amp output obeys its slew rate through abrupt transients");
    for (double rate : { 128000.0, 192000.0, 384000.0 })
    {
        zao::DiodeShuntClipper shunt, inverseShunt;
        zao::SeriesDiodeCrossover series, inverseSeries;
        shunt.prepare (rate); inverseShunt.prepare (rate); series.prepare (rate); inverseSeries.prepare (rate);
        double shuntResidual = 0, seriesResidual = 0, error = 0;
        bool valid = true;
        for (int i = 0; i < 8192; ++i)
        {
            const auto x = i < 4096 ? noise (random) : (i % 128 < 64 ? 12.0 : -12.0);
            const auto a = shunt.process (x), b = series.process (x);
            valid = valid && std::isfinite (a) && std::isfinite (b);
            error = std::max ({ error, std::abs (a + inverseShunt.process (-x)), std::abs (b + inverseSeries.process (-x)) });
            shuntResidual = std::max (shuntResidual, shunt.getResidual());
            seriesResidual = std::max (seriesResidual, series.getResidual());
        }
        check (valid && shuntResidual < 1.0e-9 && seriesResidual < 1.0e-9,
               "both passive diode networks converge through noise and overload reversals");
        check (error < 1.0e-7, "both antiparallel diode networks retain odd symmetry");
        diodeMeasurements.push_back ({ rate, shuntResidual, seriesResidual });
    }
    const auto dcOutput = [] (auto& circuit, double input)
    {
        circuit.reset();
        for (int i = 0; i < 2048; ++i) circuit.process (input);
        return circuit.getOutputVolts();
    };
    zao::DiodeShuntClipper shunt; shunt.prepare (192000);
    zao::SeriesDiodeCrossover series; series.prepare (192000);
    const auto lowShunt = dcOutput (shunt, 0.02), highShunt = dcOutput (shunt, 4.0);
    const auto lowSeries = dcOutput (series, 0.05), highSeries = dcOutput (series, 1.0);
    check (lowShunt > 0.015 && lowShunt < 0.020 && highShunt > 0.25 && highShunt < 0.5,
           "diode shunt passes quiet input and limits high-amplitude peaks");
    check (lowSeries < 0.01 && highSeries > 0.4 && highSeries < 0.8,
           "series diodes suppress the zero-crossing region and conduct above their forward voltage");
}

void shapeTests()
{
    double previousCollector = 10;
    for (double control : { -1.0, 0.0, 1.0 })
    {
        zao::BjtCommonEmitter bjt; bjt.prepare (192000); bjt.setBiasVolts (control * 0.8); bjt.reset();
        check (bjt.getOperatingPoint()[2] < previousCollector && bjt.getResidual() < 1.0e-9,
               "BJT Bias physically shifts the DC operating point and clipping headroom");
        previousCollector = bjt.getOperatingPoint()[2];
        for (double rate : { 128000.0, 192000.0, 384000.0 })
        {
            bjt.prepare (rate); bjt.setBiasVolts (control * 0.8); bjt.reset();
            zao::DiodeShuntClipper shunt; zao::SeriesDiodeCrossover series;
            shunt.prepare (rate); series.prepare (rate);
            const auto knee = 0.5 * (control + 1.0); shunt.setKnee (knee); series.setKnee (knee);
            double rb = 0, rs = 0, rc = 0; bool valid = true;
            std::mt19937 random (73); std::uniform_real_distribution<double> noise (-12, 12);
            for (int i = 0; i < 4096; ++i)
            {
                const auto input = i < 2048 ? noise (random) : (i % 128 < 64 ? 12.0 : -12.0);
                valid &= std::isfinite (bjt.process (input)) && std::isfinite (shunt.process (input)) && std::isfinite (series.process (input));
                rb = std::max (rb, bjt.getResidual()); rs = std::max (rs, shunt.getResidual()); rc = std::max (rc, series.getResidual());
            }
            check (valid && rb < 1.0e-7 && rs < 1.0e-9 && rc < 1.0e-9,
                   "nonideal Bias and all Knee settings converge under broadband overload at three rates");
            shapeResiduals.push_back ({ rate, control, rb, rs, rc });
        }
    }
    for (double knee : { 0.0, 0.5, 1.0 })
    {
        zao::DiodeDriver driver; driver.prepare (192000); driver.setKnee (knee);
        for (int i = 0; i < 2048; ++i) driver.process (100);
        check (driver.getVoltage() <= zao::DiodeDriver::rail
               && std::abs (driver.current (-100, 1000).value) <= driver.getParameters().currentLimit,
               "loaded driver respects finite voltage swing and delivered current");
    }
    // Compare normalized flat tops with an independent static solve of the old
    // silicon network at the same 4 V, 1 kHz command (the old 100 pF is negligible here).
    zao::DiodeShuntClipper schottky; schottky.prepare (192000);
    std::vector<double> actual, silicon;
    for (int i = 0; i < 15360; ++i)
    {
        const auto input = 4.0 * std::sin (2 * zao::pi * 1000 * i / 192000.0);
        const auto output = schottky.process (input);
        if (i < 11520) continue;
        double low = -1.5, high = 1.5;
        for (int iteration = 0; iteration < 48; ++iteration)
        {
            const auto voltage = 0.5 * (low + high);
            const auto current = (voltage-input) / 4700 + voltage / 100000
                + 2.0e-9 * (std::exp (voltage/0.049115) - std::exp (-voltage/0.049115));
            if (current > 0) high = voltage; else low = voltage;
        }
        actual.push_back (output); silicon.push_back (0.125 * (low+high));
    }
    const auto flatFraction = [] (const auto& values)
    {
        double peak = 0; for (auto x : values) peak = std::max (peak, std::abs (x));
        int flat = 0; for (auto x : values) if (std::abs (x) >= 0.95 * peak) ++flat;
        return static_cast<double> (flat) / static_cast<double> (values.size());
    };
    siliconFlatFraction = flatFraction (silicon); schottkyFlatFraction = flatFraction (actual);
    check (schottkyFlatFraction < siliconFlatFraction * 0.85,
           "Schottky ballast and the loaded amplifier produce less flat-topped clipping than the old silicon source");
    for (int mode : { 0, 2, 3 })
    {
        zao::Settings settings; settings.model = mode; settings.outputDb = 0;
        settings.bjtBias = -1; settings.diodeKnee = 0; const auto low = render (settings);
        settings.bjtBias = 1; settings.diodeKnee = 1; const auto high = render (settings);
        double difference = 0;
        for (size_t i = low.size()-4800; i < low.size(); ++i) difference += std::abs (low[i]-high[i]);
        check (difference > 1, "Bias and Knee change the audio generated by their intended circuit");
    }
}

zao::SmartAnalyser::Frame smartFrame (double third = 329.6276, bool antiphase = false)
{
    zao::SmartAnalyser::Frame f; f.settings.smart = true; f.generation = 2;
    f.signature = zao::SmartAnalyser::signature (f.settings);
    for (int i = 0; i < zao::SmartAnalyser::fftSize; ++i)
    {
        const auto t = i/48000.0;
        double input = 0, wet = 0;
        for (double hz : { 261.6256, third, 391.9954 })
        {
            input += .1*std::sin (2*zao::pi*hz*t);
            for (int order = 2; order <= zao::SmartAnalyser::orders; ++order)
                wet += (.08/order)*std::sin (2*zao::pi*hz*order*t);
        }
        const auto j = static_cast<size_t> (i);
        f.left[j] = static_cast<float> (input); f.right[j] = antiphase ? -f.left[j] : f.left[j];
        f.wetLeft[j] = static_cast<float> (wet); f.wetRight[j] = antiphase ? -f.wetLeft[j] : f.wetLeft[j];
    }
    return f;
}

void smartTests()
{
    using A = zao::SmartAnalyser;
    for (double step : { 0., 3., 4., 5., 7., 9., 12. })
    {
        const auto hz = 440*std::pow (2.0, step/12);
        check (A::intervalDissonance (440, hz) < A::intervalDissonance (440, 440*std::pow (2.0, (step+.65)/12)),
               "12-TET consonant anchors are valleys in the dissonance graph");
        check (std::abs (A::intervalDissonance (440, hz)-A::intervalDissonance (hz, 440)) < 1e-7f, "dissonance is symmetric");
    }
    A analyser; auto frame = smartFrame(); auto result = analyser.analyse (frame);
    check (result.count > 0 && result.selectedCost < result.baselineCost && result.confidence > .5,
           "measured generated harmonic orders receive content-dependent dissonance cuts");
    for (int i = 0; i < result.count; ++i)
    {
        const auto& b = result.bands[static_cast<size_t> (i)];
        check (b.order >= 2 && b.order <= 12 && b.gainDb >= -6.01f && b.gainDb < 0,
               "Smart cuts only added orders with at most six dB reduction per target");
        bool protectedInput = true;
        for (double hz : { 261.6256, 329.6276, 391.9954 }) protectedInput &= std::abs (b.frequency-hz) > 4*b.bandwidth;
        check (protectedInput, "Smart targets stay outside protected input peaks");
    }
    auto anti = analyser.analyse (smartFrame (329.6276, true));
    check (anti.count == result.count && std::abs (anti.baselineCost-result.baselineCost) < 1e-4,
           "anti-phase stereo retains the same harmonic analysis");
    auto minor = analyser.analyse (smartFrame (311.127));
    double change = 0;
    for (int i = 0; i < std::min (minor.count, result.count); ++i)
        change += std::abs (minor.bands[static_cast<size_t> (i)].frequency-result.bands[static_cast<size_t> (i)].frequency);
    check (change > 10, "major-to-minor content changes the harmonic targets");
    A::Frame silence; silence.settings.smart = true;
    check (analyser.analyse (silence).count == 0, "silence produces no Smart cuts");
    std::mt19937 random (72); std::normal_distribution<float> noise (0, .1f);
    for (int i = 0; i < A::fftSize; ++i)
    { auto j = static_cast<size_t> (i); silence.left[j] = silence.right[j] = noise (random); silence.wetLeft[j] = silence.wetRight[j] = silence.left[j]; }
    check (analyser.analyse (silence).count == 0, "unpitched noise does not cause arbitrary harmonic cuts");
    // Measure actual causal filtering, rather than just the predicted score.
    zao::SmartHarmonics shaper; shaper.prepare (48000, false); shaper.setPreview (result);
    std::vector<float> input, filtered, raw;
    for (int i = 0; i < 48000; ++i)
    {
        double x = 0, wet = 0;
        for (double hz : { 261.6256, 329.6276, 391.9954 })
        {
            x += .1*std::sin (2*zao::pi*hz*i/48000);
            for (int order = 2; order <= A::orders; ++order) wet += (.08/order)*std::sin (2*zao::pi*hz*order*i/48000);
        }
        std::array<float, 2> in { static_cast<float> (x), static_cast<float> (x) }, out { static_cast<float> (wet), static_cast<float> (wet) };
        shaper.process (in, out);
        if (i >= 24000) { input.push_back (in[0]); filtered.push_back (in[0]+out[0]); raw.push_back (in[0]+static_cast<float> (wet)); }
    }
    for (double hz : { 261.6256, 329.6276, 391.9954 })
        check (std::abs (20*std::log10 (amplitude (filtered, hz)/amplitude (raw, hz))) < .15,
               "original chord tones stay within 0.15 dB under actual harmonic filtering");
    const auto hz = result.bands[0].frequency;
    check (amplitude (filtered, hz) < .95*amplitude (raw, hz), "actual audio loses dissonant generated harmonic energy");
    // Real worker publication, Off scheduling, stale results and transition back.
    zao::SmartHarmonics worker; worker.prepare (48000); worker.beginBlock (frame.settings);
    for (int i = 0; i < A::fftSize; ++i)
    {
        auto j = static_cast<size_t> (i);
        std::array<float, 2> in { frame.left[j], frame.right[j] }, wet { frame.wetLeft[j], frame.wetRight[j] };
        worker.process (in, wet);
    }
    const auto deadline = juce::Time::getMillisecondCounter()+2000;
    while (worker.readResult().count == 0 && juce::Time::getMillisecondCounter() < deadline) juce::Thread::sleep (5);
    check (worker.getSubmittedJobs() == 1 && worker.readResult().count > 0, "worker publishes its first complete spectral frame");
    worker.beginBlock (frame.settings);
    double liveDifference = 0;
    for (int i = 0; i < A::fftSize; ++i)
    {
        const auto j = static_cast<size_t> (i);
        std::array<float, 2> in { frame.left[j], frame.right[j] }, wet { frame.wetLeft[j], frame.wetRight[j] };
        worker.process (in, wet); liveDifference += std::abs (wet[0]-frame.wetLeft[j]);
    }
    check (liveDifference > .1, "published harmonic targets affect live audio through the crossfaded bank");
    auto manual = frame.settings; manual.driveDb = 23; worker.beginBlock (manual);
    check (worker.readResult().count == 0, "manual circuit changes invalidate stale harmonic targets");
    manual.smart = false; worker.beginBlock (manual);
    const auto jobs = worker.getSubmittedJobs(); bool exact = true;
    for (int i = 0; i < 24000; ++i)
    {
        std::array<float, 2> in { .2f, -.2f }, out { .3f, -.3f };
        worker.process (in, out); if (i >= 6000) exact &= std::abs (out[0]-.3f) < 1e-8f && std::abs (out[1]+.3f) < 1e-8f;
    }
    check (exact && worker.getSubmittedJobs() == jobs, "Off is exactly transparent and submits no spectral jobs");
    ZaoDriveProcessor processor; processor.prepareToPlay (48000, 64); set (processor, "smart", 1);
    const auto latency = processor.getLatencySamples();
    juce::MemoryBlock state; processor.getStateInformation (state);
    ZaoDriveProcessor restored; restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (restored.getSettings().smart, "Smart button survives host state recall");
    auto legacy = processor.parameters.copyState(); legacy.removeChild (legacy.getChildWithProperty ("id", "smart"), nullptr);
    juce::AudioProcessor::copyXmlToBinary (*legacy.createXml(), state);
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (! restored.getSettings().smart, "older presets explicitly load Smart Off");
    juce::MidiBuffer midi; juce::AudioBuffer<float> block (2, 128);
    for (int i = 0; i < 4096; i += 128)
    {
        for (int j = 0; j < 128; ++j) { block.setSample (0, j, frame.left[static_cast<size_t> (i+j)]); block.setSample (1, j, frame.right[static_cast<size_t> (i+j)]); }
        processor.processBlock (block, midi);
    }
    check (processor.getSmartSubmittedJobs() == 1 && processor.getLatencySamples() == latency,
           "live circuit supplies paired input/output analysis without extra plugin latency");
    check (std::abs (processor.getSettings().driveDb-12) < 1e-6f && std::abs (processor.getSettings().bjtBias) < 1e-6f && std::abs (processor.getSettings().diodeKnee-.5f) < 1e-6f,
           "Smart never changes Drive, Bias, Knee or their host automation values");
}

void greyNoiseTests()
{
    using Filter = zao::GreyNoiseFilter;
    // Published ISO equation reference values at 80 phons, not A weighting.
    for (const auto& point : std::array<std::pair<size_t, double>, 5> { {
        { 0, 118.990011 }, { 7, 92.479723 }, { 17, 80.012075 }, { 23, 77.916229 }, { 28, 92.969435 } } })
        check (std::abs (zao::EqualLoudness80::splAtIndex (point.first)-point.second) < 1e-6,
               "80-phon ISO 226 SPL anchors match independent reference values");
    check (std::abs (Filter::targetDb (1000)) < 1e-12
           && std::abs (Filter::targetDb (4000)+2.095846) < 1e-6,
           "80-phon contour is referenced to 1 kHz and retains its unscaled midrange valley");
    check (std::abs (Filter::targetDb (20)-3) < 1e-12 && std::abs (Filter::targetDb (12500)-3) < 1e-12
           && std::abs (Filter::targetDb (0)-3) < 1e-12 && std::abs (Filter::targetDb (20000)-3) < 1e-12,
           "only upward contour gain is capped at three dB and ISO endpoints are held outside the table");
    for (double rate : { 8000., 32000., 44100., 48000., 96000., 192000. })
    {
        Filter filter; filter.prepare (rate); const auto h = filter.getImpulseResponse();
        double sum = 0, symmetry = 0, impulseError = 0, phaseError = 0, minimumResponse = 1, maximumResponse = 0;
        for (int i = 0; i < static_cast<int> (h.size()); ++i)
        {
            const auto j = static_cast<size_t> (i);
            sum += h[j]; symmetry = std::max (symmetry, std::abs (static_cast<double> (h[j]-h[h.size()-1-j])));
            impulseError = std::max (impulseError, std::abs (static_cast<double> (filter.process (i == 0 ? 1.0f : 0.0f, 0)-h[j])));
        }
        check (symmetry < 1e-12 && impulseError < 1e-7, "grey FIR has an exactly symmetric impulse and the advertised group delay");
        check (sum > 1 && sum <= std::pow (10.0, 3.0/20)+1e-6 && std::abs (filter.process (0, 1)) < 1e-12f,
               "grey FIR has bounded bass emphasis and independent stereo history");
        const auto response = [&] (double hz)
        {
            std::complex<double> result {};
            for (int i = 0; i < static_cast<int> (h.size()); ++i)
                result += static_cast<double> (h[static_cast<size_t> (i)])*std::polar (1.0, -2*zao::pi*hz*i/rate);
            return result;
        };
        for (int i = 1; i < 512; ++i)
        {
            const auto hz = .5*rate*i/512;
            const auto compensated = response (hz)*std::polar (1.0, 2*zao::pi*hz*filter.getLatencySamples()/rate);
            phaseError = std::max (phaseError, std::abs (compensated.imag()));
            minimumResponse = std::min (minimumResponse, compensated.real());
            maximumResponse = std::max (maximumResponse, std::abs (compensated));
        }
        check (phaseError < 1e-10 && minimumResponse > .4,
               "delay compensation leaves a real positive response with no frequency-dependent phase rotation");
        check (std::abs (20*std::log10 (std::abs (response (1000)))) < 1e-5
               && 20*std::log10 (std::abs (response (3150))) < -1,
               "implemented 80-phon FIR keeps 1 kHz at unity and attenuates the sensitive midrange");
        check (20*std::log10 (maximumResponse) <= 3.00001,
               "the actual FIR response respects the three dB upward gain cap");
        if (rate >= 32000)
            check (20*std::log10 (std::abs (response (50))) > 2
                   && 20*std::log10 (std::abs (response (15000))) > 2,
                   "80-phon bass and treble emphasis remain present below the cap");
        filter.reset();
        std::mt19937 random (61); std::uniform_real_distribution<float> noise (-1, 1);
        std::vector<float> input; double error = 0;
        for (int i = 0; i < static_cast<int> (h.size())*3; ++i)
        {
            input.push_back (noise (random)); double expected = 0;
            for (int j = 0; j <= std::min (i, static_cast<int> (h.size())-1); ++j)
                expected += h[static_cast<size_t> (j)]*static_cast<double> (input[static_cast<size_t> (i-j)]);
            error = std::max (error, std::abs (filter.process (input.back(), 0)-expected));
        }
        check (error < 1e-7, "optimized symmetric FIR matches direct convolution through history wraps");
    }
    for (double rate : { 44100., 48000., 96000. })
        for (bool hostBypass : { false, true })
        {
            ZaoDriveProcessor processor; processor.prepareToPlay (rate, 64);
            if (! hostBypass) { set (processor, "mix", 0); set (processor, "output", 0); }
            juce::AudioBuffer<float> impulse (2, 1024); impulse.clear(); impulse.setSample (0, 0, 1);
            juce::MidiBuffer midi;
            if (hostBypass) processor.processBlockBypassed (impulse, midi); else processor.processBlock (impulse, midi);
            double error = impulse.getMagnitude (1, 0, 1024);
            for (int i = 0; i < 1024; ++i)
                error = std::max (error, std::abs (static_cast<double> (impulse.getSample (0, i))-(i == processor.getLatencySamples() ? 1.0 : 0.0)));
            check (error < 1e-7 && processor.getLatencySamples() >= Filter::latencyForRate (rate)+64,
                   "clean mix and host bypass remain exact impulses at the full FIR-compensated latency");
        }
    for (int mode : { 0, 1, 2, 3 })
    {
        const auto run = [mode] (int chunk)
        {
            zao::DriveEngine engine; engine.prepare (48000, 64, false);
            zao::Settings settings; settings.model = mode;
            std::vector<float> output; juce::AudioBuffer<float> block (2, chunk);
            for (int i = 0; i < 4096; i += chunk)
            {
                const auto n = std::min (chunk, 4096-i); block.setSize (2, n, false, false, true);
                for (int j = 0; j < n; ++j)
                {
                    const auto x = static_cast<float> (.2*std::sin ((i+j)*.13));
                    block.setSample (0, j, x); block.setSample (1, j, x);
                }
                engine.process (block, settings);
                for (int j = 0; j < n; ++j) output.push_back (block.getSample (0, j));
            }
            return output;
        };
        const auto shortBlocks = run (17), longBlocks = run (513); double error = 0;
        for (size_t i = 0; i < shortBlocks.size(); ++i) error = std::max (error, std::abs (static_cast<double> (shortBlocks[i]-longBlocks[i])));
        check (error < 2e-6, "all circuit paths retain wet-reference alignment across arbitrary host block boundaries");
    }
}

void processorTests()
{
    ZaoDriveProcessor processor;
    check (processor.getName() == "Zao Drive" && processor.getParameters().size() == 14,
           "all circuits register compatible legacy parameters plus independent Bias and Knee");
    processor.prepareToPlay (48000, 64);
    const auto latency = processor.getLatencySamples();
    check (latency >= 64+zao::GreyNoiseFilter::referenceLatencySamples && latency < 100+zao::GreyNoiseFilter::referenceLatencySamples, "host latency includes grey FIR, circuit oversampling and the shared transformer delay");
    ZaoDriveProcessor mono;
    auto layout = mono.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::mono());
    layout.outputBuses.set (0, juce::AudioChannelSet::mono());
    check (mono.setBusesLayout (layout), "Zao accepts a mono host bus");
    mono.prepareToPlay (48000, 64);
    juce::AudioBuffer<float> monoBuffer (1, 512); monoBuffer.clear(); monoBuffer.setSample (0, 0, 0.5f);
    juce::MidiBuffer monoMidi; mono.processBlock (monoBuffer, monoMidi);
    check (finite (monoBuffer) && monoBuffer.getMagnitude (0, 512) > 0.01f,
           "mono bus processes the circuit without requiring a second channel");
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    check (! mono.isBusesLayoutSupported (layout), "mismatched mono/stereo buses are rejected");
    for (const auto* id : { "bypass", "mix" })
    {
        ZaoDriveProcessor unity; unity.prepareToPlay (48000, 64);
        set (unity, "output", 0);
        set (unity, id, juce::String (id) == "bypass" ? 1.0f : 0.0f);
        juce::AudioBuffer<float> impulse (2, 512); impulse.clear(); impulse.setSample (0, 0, 1);
        juce::MidiBuffer midi; unity.processBlock (impulse, midi);
        bool exact = true;
        for (int i = 0; i < 512; ++i)
            exact = exact && std::abs (impulse.getSample (0, i) - (i == latency ? 1.0f : 0.0f)) < 1.0e-7f;
        check (exact && impulse.getMagnitude (1, 0, 512) < 1.0e-7f,
               "dry mix and bypass retain exact reported timing without stereo crosstalk");
    }
    set (processor, "model", 1); set (processor, "drive", 29); set (processor, "transformer", 1);
    set (processor, "dcBias", 37); set (processor, "tone", 4321);
    set (processor, "bjtBias", -45); set (processor, "diodeKnee", 73);
    juce::MemoryBlock state; processor.getStateInformation (state);
    ZaoDriveProcessor restored; restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    const auto saved = restored.getSettings();
    check (saved.model == 1 && saved.transformer && std::abs (saved.driveDb - 29) < 0.01f
           && std::abs (saved.bjtBias + 0.45f) < 0.001f && std::abs (saved.diodeKnee - 0.73f) < 0.001f
           && std::abs (saved.dcBias - 0.37f) < 0.001f && std::abs (saved.toneHz - 4321) < 0.1f,
           "circuit and transformer settings survive host state recall");
    for (int mode = 0; mode < 4; ++mode)
    {
        selectMode (processor, mode); processor.getStateInformation (state);
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (restored.getSettings().model == mode, "all four circuits survive saved-state round trips");
    }
    auto legacyState = processor.parameters.copyState();
    for (const auto* id : { "diodeCircuit", "bjtBias", "diodeKnee" })
        legacyState.removeChild (legacyState.getChildWithProperty ("id", id), nullptr);
    legacyState.getChildWithProperty ("id", "model").setProperty ("value", 1.0f, nullptr);
    juce::AudioProcessor::copyXmlToBinary (*legacyState.createXml(), state);
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (std::abs (restored.getSettings().bjtBias) < 1.0e-6f && std::abs (restored.getSettings().diodeKnee - 0.5f) < 1.0e-6f,
           "legacy states reset new Bias and Knee to neutral defaults after another preset");
    check (restored.getSettings().model == 1, "loading an old two-mode state restores Op-Amp after a diode-mode session");
    restored.parameters.getParameter ("model")->setValueNotifyingHost (0.0f);
    check (restored.getSettings().model == 0, "legacy normalized circuit automation still selects BJT and Op-Amp");
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        ZaoDriveProcessor stress; stress.prepareToPlay (rate, 64);
        juce::MidiBuffer midi;
        for (int mode = 0; mode < 4; ++mode)
        {
            selectMode (stress, mode); set (stress, "drive", 36);
            set (stress, "input", 24); set (stress, "transformer", 1); set (stress, "dcBias", 100);
            juce::AudioBuffer<float> signal (2, 513);
            for (int block = 0; block < 5; ++block)
            {
                set (stress, "bjtBias", block % 2 == 0 ? 100.0f : -100.0f);
                set (stress, "diodeKnee", block % 2 == 0 ? 100.0f : 0.0f);
                for (int i = 0; i < signal.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        signal.setSample (ch, i, static_cast<float> (0.9 * std::sin (i * 0.17 + block)));
                stress.processBlock (signal, midi);
                check (finite (signal), "all four circuits remain finite under maximum drive, Bias/Knee automation and transformer bias");
            }
        }
    }
    zao::Settings s; s.outputDb = 0; s.driveDb = 12; s.toneHz = 18000;
    auto bjt = render (s); s.model = 1; auto opamp = render (s);
    bjt.erase (bjt.begin(), bjt.end() - 4800); opamp.erase (opamp.begin(), opamp.end() - 4800);
    const auto bjtEven = amplitude (bjt, 2000), opampEven = amplitude (opamp, 2000);
    check (bjtEven > 0.002 && opampEven < bjtEven * 0.1,
           "BJT asymmetry and symmetric op-amp feedback have distinct harmonic signatures");
    s.model = 0; s.driveDb = 0; auto clean = render (s); s.driveDb = 24; auto driven = render (s);
    clean.erase (clean.begin(), clean.end() - 4800); driven.erase (driven.begin(), driven.end() - 4800);
    check (amplitude (driven, 3000) > amplitude (clean, 3000) * 2.0,
           "Drive increases circuit-generated harmonics");

    s.model = 2; s.driveDb = 0; auto shuntQuiet = render (s);
    s.driveDb = 24; auto shuntDriven = render (s);
    shuntQuiet.erase (shuntQuiet.begin(), shuntQuiet.end() - 4800);
    shuntDriven.erase (shuntDriven.begin(), shuntDriven.end() - 4800);
    check (amplitude (shuntDriven, 1000) < 4.0 * amplitude (shuntQuiet, 1000)
           && amplitude (shuntDriven, 3000) > 0.005,
           "shunt mode limits peak growth and generates clipping harmonics in the audio path");
    s.model = 3; s.driveDb = 0; auto crossoverQuiet = render (s);
    s.driveDb = 12; auto crossoverDriven = render (s);
    crossoverQuiet.erase (crossoverQuiet.begin(), crossoverQuiet.end() - 4800);
    crossoverDriven.erase (crossoverDriven.begin(), crossoverDriven.end() - 4800);
    check (amplitude (crossoverDriven, 3000) > 2.0 * amplitude (crossoverQuiet, 3000)
           && amplitude (crossoverDriven, 3000) > 0.001 && amplitude (crossoverDriven, 2000) < 1.0e-5,
           "Drive increases added crossover harmonics while symmetric clipping keeps even orders negligible");

    s.model = 1; s.driveDb = 24;
    auto oversampled = render (s, 9600, 9000);
    zao::OpAmpFeedbackClipper singleRate; singleRate.prepare (48000);
    std::vector<float> direct;
    zao::GreyNoiseFilter aliasInput; aliasInput.prepare (48000);
    for (int i = 0; i < 9600; ++i)
    {
        const auto x = static_cast<float> (.25*std::sin (2*zao::pi*9000*i/48000.0));
        direct.push_back (static_cast<float> (singleRate.process (aliasInput.process (x, 0)*.35
            * juce::Decibels::decibelsToGain (24.0))));
    }
    oversampled.erase (oversampled.begin(), oversampled.end() - 4800);
    direct.erase (direct.begin(), direct.end() - 4800);
    const auto directAlias = amplitude (direct, 21000);
    const auto oversampledAlias = amplitude (oversampled, 21000);
    aliasReductionDb = 20.0 * std::log10 (directAlias / std::max (oversampledAlias, 1.0e-12));
    check (directAlias > 1.0e-4 && aliasReductionDb > 6.0,
           "4x circuit oversampling reduces the folded third harmonic of a 9 kHz overdrive input");

    // Compare the reused stage against YinYang's original class sample for
    // sample, using Mix=0 to isolate the output transformer from either circuit.
    zao::DriveEngine engine; engine.prepare (48000, 128);
    character::OversampledTransformer original; original.prepare (48000);
    s = {}; s.mix = 0; s.outputDb = 0; s.transformer = true; s.dcBias = 0.3f;
    const auto circuitLatency = engine.getLatency() - character::OversampledTransformer::latencySamples;
    double error = 0;
    juce::AudioBuffer<float> block (2, 128);
    for (int offset = 0; offset < 8192; offset += 128)
    {
        for (int i = 0; i < 128; ++i)
        {
            const auto x = static_cast<float> (0.4 * std::sin (2.0 * zao::pi * 100 * (offset + i) / 48000.0));
            block.setSample (0, i, x); block.setSample (1, i, x);
        }
        engine.process (block, s);
        for (int i = 0; i < 128; ++i)
        {
            const auto index = offset + i - circuitLatency;
            const auto x = index >= 0 ? static_cast<float> (0.4 * std::sin (2.0 * zao::pi * 100 * index / 48000.0)) : 0.0f;
            const auto expected = original.process (x, juce::Decibels::decibelsToGain (6.0f), 0.3f);
            if (offset > 4096) error = std::max (error, static_cast<double> (std::abs (expected - block.getSample (0, i))));
        }
    }
    check (error < 1.0e-6, "Zao transformer matches YinYang's implementation sample for sample");
}

void writeReport (const juce::File& directory)
{
    directory.createDirectory();
    std::ofstream csv (directory.getChildFile ("waveforms.csv").getFullPathName().toStdString());
    csv << "model,drive,transformer,sample,input,output\n";
    zao::DriveEngine latencyEngine; latencyEngine.prepare (48000, 128);
    for (int model = 0; model < 4; ++model)
        for (float drive : { 0.0f, 12.0f, 24.0f })
            for (int transformer = 0; transformer < 2; ++transformer)
            {
                zao::Settings s; s.model = model; s.driveDb = drive; s.outputDb = 0; s.toneHz = 18000;
                s.transformer = transformer != 0; s.coreDriveDb = 12; s.dcBias = 0.3f;
                const auto signal = render (s);
                for (size_t i = signal.size() - 960; i < signal.size(); ++i)
                    csv << model << ',' << drive << ',' << transformer << ',' << i << ','
                        << 0.25 * std::sin (2.0 * zao::pi * (static_cast<double> (i) - latencyEngine.getLatency()) / 48.0)
                        << ',' << signal[i] << '\n';
            }
    zao::BjtCommonEmitter bjt; bjt.prepare (192000);
    std::ofstream dc (directory.getChildFile ("operating-point.csv").getFullPathName().toStdString());
    dc.precision (16); dc << "base,emitter,collector,bypass,residual\n";
    for (auto v : bjt.getOperatingPoint()) dc << v << ',';
    dc << bjt.getResidual() << '\n';
    std::ofstream solvers (directory.getChildFile ("solver-residuals.csv").getFullPathName().toStdString());
    solvers << "rate,input_volts,bjt_residual_amps,opamp_residual_volts\n";
    for (const auto& m : solverMeasurements)
        solvers << m.rate << ',' << m.level << ',' << m.bjtResidual << ',' << m.opampResidual << '\n';
    std::ofstream alias (directory.getChildFile ("alias-check.csv").getFullPathName().toStdString());
    alias << "frequency,drive,reduction_db\n9000,24," << aliasReductionDb << '\n';
    std::ofstream diodeResiduals (directory.getChildFile ("diode-residuals.csv").getFullPathName().toStdString());
    diodeResiduals << "rate,shunt_residual_amps,series_residual_amps\n";
    for (const auto& m : diodeMeasurements) diodeResiduals << m.rate << ',' << m.shuntResidual << ',' << m.seriesResidual << '\n';
    std::ofstream diodeDc (directory.getChildFile ("diode-transfer.csv").getFullPathName().toStdString());
    diodeDc.precision (16); diodeDc << "input_volts,shunt_volts,series_volts\n";
    zao::DiodeShuntClipper shunt; zao::SeriesDiodeCrossover series;
    shunt.prepare (192000); series.prepare (192000);
    for (int index = -120; index <= 120; ++index)
    {
        const auto input = index * 0.025;
        // Trapezoidal integration of small, stiff parasitic capacitances can
        // retain a Nyquist transient after a DC step; settle it before comparing
        // this transient simulation with an independent static root solve.
        for (int i = 0; i < 16384; ++i) { shunt.process (input); series.process (input); }
        diodeDc << input << ',' << shunt.getOutputVolts() << ',' << series.getOutputVolts() << '\n';
    }
    std::ofstream shape (directory.getChildFile ("shape-waveforms.csv").getFullPathName().toStdString());
    shape << "model,control,sample,input,output\n";
    for (int mode : { 0, 2, 3 })
        for (float control : { -1.0f, 0.0f, 1.0f })
        {
            zao::Settings settings; settings.model = mode; settings.outputDb = 0; settings.toneHz = 18000;
            settings.driveDb = 12; settings.bjtBias = control; settings.diodeKnee = 0.5f * (control + 1);
            const auto samples = render (settings);
            for (size_t i = samples.size()-960; i < samples.size(); ++i)
                shape << mode << ',' << (mode == 0 ? control*100 : (control+1)*50) << ',' << i << ','
                      << 0.25 * std::sin (2*zao::pi * (static_cast<double> (i)-latencyEngine.getLatency()) / 48.0)
                      << ',' << samples[i] << '\n';
        }
    std::ofstream biasDc (directory.getChildFile ("bias-operating-points.csv").getFullPathName().toStdString());
    biasDc.precision (16); biasDc << "bias,base,emitter,collector,bypass,residual\n";
    for (double bias : { -1.0, 0.0, 1.0 })
    {
        zao::BjtCommonEmitter biased; biased.prepare (192000); biased.setBiasVolts (bias*0.8); biased.reset();
        biasDc << bias*100 << ','; for (auto value : biased.getOperatingPoint()) biasDc << value << ',';
        biasDc << biased.getResidual() << '\n';
    }
    std::ofstream knees (directory.getChildFile ("knee-transfer.csv").getFullPathName().toStdString());
    knees.precision (16); knees << "knee,input_volts,shunt_volts,series_volts\n";
    for (double knee : { 0.0, 1.0 })
    {
        shunt.reset(); series.reset(); shunt.setKnee (knee); series.setKnee (knee);
        for (int index = -60; index <= 60; ++index)
        {
            const auto input = index*0.05;
            for (int i = 0; i < 16384; ++i) { shunt.process (input); series.process (input); }
            knees << knee*100 << ',' << input << ',' << shunt.getOutputVolts() << ',' << series.getOutputVolts() << '\n';
        }
    }
    std::ofstream shapeResidual (directory.getChildFile ("shape-residuals.csv").getFullPathName().toStdString());
    shapeResidual << "rate,control,bjt_amps,shunt_amps,series_amps\n";
    for (const auto& m : shapeResiduals) shapeResidual << m.rate << ',' << m.control << ',' << m.bjt << ',' << m.shunt << ',' << m.series << '\n';
    std::ofstream softness (directory.getChildFile ("softness.csv").getFullPathName().toStdString());
    softness << "silicon_flat_fraction,schottky_flat_fraction\n" << siliconFlatFraction << ',' << schottkyFlatFraction << '\n';
    std::cout << "Measurements written to " << directory.getFullPathName() << '\n';
}

void writeGreyReport (const juce::File& directory)
{
    directory.createDirectory();
    std::ofstream impulse (directory.getChildFile ("grey-impulse.csv").getFullPathName().toStdString());
    std::ofstream latency (directory.getChildFile ("grey-latency.csv").getFullPathName().toStdString());
    std::ofstream target (directory.getChildFile ("grey-iso80-target.csv").getFullPathName().toStdString());
    target.precision (17); target << "frequency,spl80,uncapped_db,target_db\n";
    for (size_t i = 0; i < zao::EqualLoudness80::frequencies.size(); ++i)
    {
        const auto hz = zao::EqualLoudness80::frequencies[i];
        target << hz << ',' << zao::EqualLoudness80::splAtIndex (i) << ','
               << zao::EqualLoudness80::contourDb (hz) << ',' << zao::GreyNoiseFilter::targetDb (hz) << '\n';
    }
    impulse.precision (17); impulse << "rate,tap,coefficient\n";
    latency << "rate,taps,fir_samples,plugin_samples\n";
    for (double rate : { 8000., 32000., 44100., 48000., 96000., 192000. })
    {
        zao::GreyNoiseFilter filter; filter.prepare (rate); const auto h = filter.getImpulseResponse();
        zao::DriveEngine engine; engine.prepare (rate, 128, false);
        latency << rate << ',' << h.size() << ',' << filter.getLatencySamples() << ',' << engine.getLatency() << '\n';
        for (size_t i = 0; i < h.size(); ++i) impulse << rate << ',' << i << ',' << h[i] << '\n';
    }
}

void writeSmartReport (const juce::File& directory)
{
    directory.createDirectory();
    std::ofstream audio (directory.getChildFile ("smart-audio.csv").getFullPathName().toStdString());
    std::ofstream bands (directory.getChildFile ("smart-bands.csv").getFullPathName().toStdString());
    std::ofstream summary (directory.getChildFile ("smart-scores.csv").getFullPathName().toStdString());
    audio << "model,chord,sample,input,residual,smart_residual,output,smart_output\n";
    bands << "model,chord,order,frequency,bandwidth,gain_db\n";
    summary << "model,chord,confidence,bands,before_cost,predicted_cost\n";
    for (int model = 0; model < 4; ++model)
        for (int chord = 0; chord < 2; ++chord)
        {
            constexpr int count = 24000;
            const std::array<double, 3> notes = chord == 0 ? std::array<double, 3> { 261.6256, 329.6276, 391.9954 }
                                                          : std::array<double, 3> { 220., 261.6256, 329.6276 };
            std::vector<float> input (count), output;
            for (int i = 0; i < count; ++i)
                for (auto hz : notes) input[static_cast<size_t> (i)] += static_cast<float> (.1*std::sin (2*zao::pi*hz*i/48000));
            zao::DriveEngine engine; engine.prepare (48000, 128, false);
            zao::Settings settings; settings.model = model; settings.driveDb = 24; settings.outputDb = 0; settings.toneHz = 18000;
            juce::AudioBuffer<float> block (2, 128);
            for (int i = 0; i < count; i += 128)
            {
                const auto n = std::min (128, count-i); block.setSize (2, n, false, false, true);
                for (int j = 0; j < n; ++j) block.setSample (0, j, input[static_cast<size_t> (i+j)]);
                block.copyFrom (1, 0, block, 0, 0, n); engine.process (block, settings);
                for (int j = 0; j < n; ++j) output.push_back (block.getSample (0, j));
            }
            zao::SmartAnalyser::Frame frame; frame.settings = settings; frame.settings.smart = true;
            for (int i = 0; i < zao::SmartAnalyser::fftSize; ++i)
            {
                const auto t = count-zao::SmartAnalyser::fftSize+i;
                const auto j = static_cast<size_t> (i);
                frame.left[j] = frame.right[j] = input[static_cast<size_t> (t-engine.getLatency())];
                frame.wetLeft[j] = frame.wetRight[j] = output[static_cast<size_t> (t)]-frame.left[j];
            }
            zao::SmartAnalyser analyser; const auto result = analyser.analyse (frame);
            check (result.count > 0, "all four physical circuits provide measurable added harmonics for Smart");
            summary << model << ',' << chord << ',' << result.confidence << ',' << result.count << ',' << result.baselineCost << ',' << result.selectedCost << '\n';
            for (int i = 0; i < result.count; ++i)
            {
                const auto& b = result.bands[static_cast<size_t> (i)];
                bands << model << ',' << chord << ',' << b.order << ',' << b.frequency << ',' << b.bandwidth << ',' << b.gainDb << '\n';
            }
            zao::SmartHarmonics shaper; shaper.prepare (48000, false); shaper.setPreview (result);
            for (int i = engine.getLatency(); i < count; ++i)
            {
                const auto dry = input[static_cast<size_t> (i-engine.getLatency())];
                const auto wet = output[static_cast<size_t> (i)]-dry;
                std::array<float, 2> in { dry, dry }, residual { wet, wet }; shaper.process (in, residual);
                if (i >= count-8192)
                    audio << model << ',' << chord << ',' << i << ',' << dry << ',' << wet << ',' << residual[0] << ',' << output[static_cast<size_t> (i)] << ',' << dry+residual[0] << '\n';
            }
        }
    std::ofstream curve (directory.getChildFile ("smart-dissonance.csv").getFullPathName().toStdString());
    curve << "semitones,cost\n";
    for (int i = 0; i <= 1200; ++i)
        curve << i*.01 << ',' << zao::SmartAnalyser::intervalDissonance (440, 440*std::pow (2.0, i/1200.0)) << '\n';
}

void editorTests (const juce::File& destination, bool tooltip, int previewMode, bool smartPreview)
{
    ZaoDriveProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setVisible (true);
    check (editor->getWidth() == 548 && editor->getHeight() == 690, "Zao shares the plugin family layout");
    auto* zaoEditor = dynamic_cast<ZaoDriveEditor*> (editor.get());
    check (zaoEditor != nullptr, "native Zao editor exists");
    auto expectedBefore = zaoEditor->getExpectedOutput();
    juce::Slider* model = nullptr;
    juce::Slider* contextual = nullptr;
    juce::Button* transformer = nullptr;
    juce::Button* smart = nullptr;
    juce::TooltipWindow* window = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* slider = dynamic_cast<juce::Slider*> (child); slider && slider->getName() == "CIRCUIT") model = slider;
        if (auto* slider = dynamic_cast<juce::Slider*> (child); slider && slider->getName() == "BIAS") contextual = slider;
        if (auto* button = dynamic_cast<juce::Button*> (child); button && button->getButtonText() == "TRANSFORMER") transformer = button;
        if (auto* button = dynamic_cast<juce::Button*> (child); button && button->getButtonText() == "SMART") smart = button;
        if (auto* tip = dynamic_cast<juce::TooltipWindow*> (child)) window = tip;
        if (dynamic_cast<juce::Slider*> (child) || dynamic_cast<juce::Button*> (child) || dynamic_cast<juce::Label*> (child))
        {
            auto* client = dynamic_cast<juce::TooltipClient*> (child);
            const auto hint = client != nullptr ? client->getTooltip() : juce::String();
            check (hint.isNotEmpty() && hint.endsWithChar ('.') && hint.dropLastCharacters (1).indexOfChar ('.') < 0,
                   "all Zao controls and captions have one-sentence hover hints");
        }
    }
    check (smart && smart->getY() == 98 && smart->getX() == 205, "Smart button sits beside the circuit header");
    smart->setToggleState (true, juce::sendNotificationSync);
    check (processor.getSettings().smart, "Smart button updates its host parameter");
    set (processor, "smart", 0);
    check (! smart->getToggleState(), "Smart button follows host automation");
    check (model && contextual && transformer && window && model->getTextFromValue (0) == "BJT CE",
           "initial circuit caption displays the model name");
    model->setValue (1, juce::sendNotificationSync);
    check (processor.getSettings().model == 1 && model->getTextFromValue (1) == "OPAMP FB",
           "circuit knob updates its host choice parameter");
    for (int mode = 0; mode < 4; ++mode)
    {
        model->setValue (mode, juce::sendNotificationSync);
        check (processor.getSettings().model == mode && model->getTextFromValue (mode) == zao::circuitNames[static_cast<size_t> (mode)],
               "all four named circuit positions update compatible host parameters");
        const auto id = mode == 0 ? "bjtBias" : mode == 1 ? "input" : "diodeKnee";
        const auto name = mode == 0 ? "BIAS" : mode == 1 ? "INPUT" : "KNEE";
        check (contextual->getName() == name && contextual->getTooltip().isNotEmpty(),
               "first knob name and hover hint follow the selected circuit");
        contextual->setValue (mode == 0 ? -35 : mode == 1 ? 8 : 72, juce::sendNotificationSync);
        check (std::abs (processor.parameters.getRawParameterValue (id)->load() - contextual->getValue()) < 0.01,
               "contextual knob writes the correct independent host parameter");
    }
    set (processor, "input", 0); set (processor, "bjtBias", 0); set (processor, "diodeKnee", 50);
    selectMode (processor, 2);
    for (int i = 0; i < 12; ++i) { juce::Thread::sleep (20); juce::Timer::callPendingTimersSynchronously(); }
    check (juce::roundToInt (model->getValue()) == 2, "circuit selector follows diode-pair host automation");
    check (contextual->getName() == "KNEE", "contextual knob follows host-selected diode mode");
    set (processor, "diodeKnee", 25);
    check (std::abs (contextual->getValue()-25) < 0.01, "Knee readout follows host automation");
    selectMode (processor, 0);
    for (int i = 0; i < 12; ++i) { juce::Thread::sleep (20); juce::Timer::callPendingTimersSynchronously(); }
    check (contextual->getName() == "BIAS", "host circuit automation rebinds the first knob to BJT Bias");
    set (processor, "bjtBias", -40);
    check (std::abs (contextual->getValue()+40) < 0.01, "BJT Bias readout follows host automation");
    selectMode (processor, 3);
    for (int i = 0; i < 12; ++i) { juce::Thread::sleep (20); juce::Timer::callPendingTimersSynchronously(); }
    check (contextual->getName() == "KNEE" && std::abs (contextual->getValue()-25) < 0.01
           && std::abs (processor.getSettings().bjtBias+0.4f) < 0.001f,
           "mode switching recalls independent Bias and Knee settings without overwriting either");
    set (processor, "bjtBias", 0); set (processor, "diodeKnee", 50); selectMode (processor, 2);
    for (int i = 0; i < 12; ++i) { juce::Thread::sleep (20); juce::Timer::callPendingTimersSynchronously(); }
    set (processor, "drive", 0);
    for (int i = 0; i < 12; ++i) { juce::Thread::sleep (20); juce::Timer::callPendingTimersSynchronously(); }
    const auto& expectedAfter = zaoEditor->getExpectedOutput();
    double difference = 0;
    for (size_t i = 0; i < expectedBefore.size(); ++i) difference += std::abs (expectedBefore[i] - expectedAfter[i]);
    check (difference > 0.1, "expected-output display changes with circuit and drive settings");
    set (processor, "drive", 12); model->setValue (0, juce::sendNotificationSync);
    if (previewMode >= 0)
    {
        model->setValue (previewMode, juce::sendNotificationSync);
        set (processor, "drive", previewMode == 3 ? 12.0f : 24.0f);
        set (processor, "output", 0); set (processor, "transformer", previewMode == 1 ? 1.0f : 0.0f);
    }
    for (int i = 0; i < 12; ++i) { juce::Thread::sleep (20); juce::Timer::callPendingTimersSynchronously(); }
    if (smartPreview)
    {
        processor.prepareToPlay (48000, 128); set (processor, "smart", 1);
        juce::AudioBuffer<float> signal (2, 128); juce::MidiBuffer midi;
        for (int offset = 0; offset < 16384; offset += 128)
        {
            for (int i = 0; i < 128; ++i)
            {
                const auto x = static_cast<float> (.1*(std::sin (2*zao::pi*261.6256*(offset+i)/48000)+std::sin (2*zao::pi*329.6276*(offset+i)/48000)+std::sin (2*zao::pi*391.9954*(offset+i)/48000)));
                signal.setSample (0, i, x); signal.setSample (1, i, x);
            }
            processor.processBlock (signal, midi);
            if (offset%4096 == 0) juce::Thread::sleep (20);
        }
        for (int i = 0; i < 12; ++i) { juce::Thread::sleep (20); juce::Timer::callPendingTimersSynchronously(); }
        check (processor.getSmartAnalysis().count > 0, "Smart preview includes live circuit harmonic analysis");
        window->displayTip (editor->localPointToGlobal (smart->getBounds().getCentre()), smart->getTooltip());
    }
    if (tooltip)
    {
        window->displayTip (editor->localPointToGlobal (model->getBounds().getCentre()), model->getTooltip());
        check (window->isVisible(), "four-circuit selector tooltip is visible in the preview");
    }
    auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::MemoryOutputStream png;
    check (image.isValid() && juce::PNGImageFormat().writeImageToStream (image, png)
           && destination.replaceWithData (png.getData(), png.getDataSize()), "Zao native preview renders and writes");
    for (auto size : { juce::Point<int> (274, 345), juce::Point<int> (658, 828) })
    {
        editor->setSize (size.x, size.y);
        const auto area = editor->getLocalArea (model, model->getLocalBounds());
        auto* hit = editor->getComponentAt (area.getCentre());
        check (editor->getLocalBounds().contains (area) && (hit == model || model->isParentOf (hit)),
               "Zao controls remain visible and clickable from 50 to 120 percent");
    }
}
}

int main (int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        circuitTests(); shapeTests(); processorTests(); smartTests(); greyNoiseTests();
        if (argc > 2 && juce::String (argv[1]) == "--editor")
        {
            const auto option = argc > 3 ? juce::String (argv[3]) : juce::String();
            const auto mode = option == "--opamp-preview" ? 1 : option == "--shunt-preview" ? 2
                            : option == "--crossover-preview" ? 3 : -1;
            editorTests (juce::File (argv[2]), option == "--tooltip", mode, option == "--smart-preview");
        }
        if (argc > 2 && juce::String (argv[1]) == "--grey-report") writeGreyReport (juce::File (argv[2]));
        if (argc > 2 && juce::String (argv[1]) == "--smart-report") writeSmartReport (juce::File (argv[2]));
        if (argc > 2 && juce::String (argv[1]) == "--report") writeReport (juce::File (argv[2]));
        std::cout << checks << " Zao Drive checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
