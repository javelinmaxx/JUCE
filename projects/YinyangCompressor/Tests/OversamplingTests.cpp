#include "../Source/CompressorDSP.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace character
{
struct CompressorTestAccess
{
    // The same equations at 1x isolate converter/processing-rate effects; no
    // alternate production mode or legacy implementation is added to the DSP.
    static void native (Compressor& dsp, float* const* channels, int count)
    { dsp.processHighRate (channels, 2, count); }
    static double rate (const Compressor& dsp) { return dsp.sampleRate; }
};
}
namespace
{
int checks = 0;
void check (bool pass, const char* message)
{
    ++checks;
    if (! pass) throw std::runtime_error (message);
}
double amplitude (const std::vector<float>& data, double hz)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < data.size(); ++i)
    {
        const auto phase = 2*3.14159265358979323846*hz*static_cast<double> (i)/48000;
        re += data[i]*std::cos (phase); im += data[i]*std::sin (phase);
    }
    return 2*std::hypot (re, im)/static_cast<double> (data.size());
}
std::vector<float> render (int model, bool native)
{
    character::Compressor dsp;
    // prepare() always configures a 4x core; a 12 kHz host gives the 48 kHz
    // native reference, which the test access then feeds without conversion.
    dsp.prepare (native ? 12000 : 48000);
    character::Parameters p; p.model = static_cast<character::Model> (model);
    p.attackMs = .05f; p.releaseMs = 10; p.ratio = 20; p.thresholdDb = -30;
    p.kneeDb = 0; p.sidechainHz = 0;
    dsp.setParameters (p);
    constexpr int count = 24000, warmup = 12000;
    std::vector<float> left (count), right (count);
    for (int i = 0; i < count; ++i)
        left[static_cast<size_t> (i)] = right[static_cast<size_t> (i)]
            = .8f*static_cast<float> (std::sin (2*3.14159265358979323846*10000*i/48000));
    for (int offset = 0; offset < count; offset += 127)
    {
        float* channels[] { left.data()+offset, right.data()+offset };
        const auto n = std::min (127, count-offset);
        if (native) character::CompressorTestAccess::native (dsp, channels, n);
        else dsp.process (channels, 2, n);
    }
    return { left.begin()+warmup, left.end() };
}
double aliasDb (const std::vector<float>& signal)
{
    // The 3rd/5th/7th/9th/11th harmonics of a coherent 10 kHz tone fold to
    // these distinct bins at 48 kHz; none is an in-band harmonic of the input.
    double power = 0;
    for (double hz : { 18000., 2000., 22000., 6000., 14000. })
    {
        const auto value = amplitude (signal, hz); power += value*value;
    }
    const auto fundamental = amplitude (signal, 10000);
    return 10*std::log10 (std::max (power, 1e-30)/(fundamental*fundamental));
}
}
int main (int argc, char** argv)
{
    try
    {
        std::ofstream csv;
        if (argc > 1) { csv.open (argv[1]); csv << "model,native_alias_dbc,oversampled_alias_dbc,reduction_db\n" << std::setprecision (9); }
        for (double rate : { 8000., 44100., 48000., 96000., 192000. })
        {
            character::Compressor dsp; dsp.prepare (rate);
            check (character::CompressorTestAccess::rate (dsp) == rate*4,
                   "the entire detector/compressor/transformer core runs at exactly four times the host rate");
            check (dsp.latencySamples == 64, "shared conversion retains the 64-sample latency at every host rate");
        }
        for (int model = 0; model < 4; ++model)
        {
            const auto native = aliasDb (render (model, true));
            const auto oversampled = aliasDb (render (model, false));
            std::cout << "Model " << model << ": native " << native << " dBc, 4x " << oversampled
                      << " dBc, improvement " << native-oversampled << " dB\n";
            if (csv) csv << model << ',' << native << ',' << oversampled << ',' << native-oversampled << '\n';
            check (oversampled < native-6 || (native < -110 && oversampled < -110),
                   "full 4x compression suppresses folded harmonics with the transformer disabled");
        }
        for (double rate : { 44100., 48000., 96000. })
        {
            character::Compressor dsp; dsp.prepare (rate);
            character::Parameters p; p.ratio = 1; p.transformer = true; p.driveDb = 12; p.evenHarmonics = 37;
            dsp.setParameters (p);
            character::OversampledTransformer reference; reference.prepare (rate);
            double error = 0;
            for (int i = 0; i < 8192; ++i)
            {
                const auto x = .3f*static_cast<float> (std::sin (i*.017)+.1*std::cos (i*.7));
                float left = x, right = x; float* channels[] { &left, &right };
                dsp.process (channels, 2, 1);
                error = std::max (error, static_cast<double> (std::abs (left-reference.process (
                    x, character::Compressor::gain (p.driveDb), p.evenHarmonics*.01f))));
            }
            check (error < 1e-6, "unity compression plus transformer matches a single 4x transformer, with no nested conversion");
        }
        std::cout << checks << " whole-path oversampling checks passed\n";
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
