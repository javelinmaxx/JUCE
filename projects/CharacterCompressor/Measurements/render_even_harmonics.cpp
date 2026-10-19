// Offline bridge for the transformer DC-current / even-harmonic measurement.
#include "../Source/CompressorDSP.h"

extern "C" int even_harmonics_latency_samples()
{
    return character::Compressor::latencySamples;
}

extern "C" void render_even_harmonics (float* samples, int count, double rate,
                                       float evenHarmonics)
{
    character::Compressor dsp;
    dsp.prepare (rate);
    character::Parameters parameters;
    parameters.ratio = 1;
    parameters.sidechainHz = 0;
    parameters.driveDb = 18;
    parameters.transformer = true;
    parameters.evenHarmonics = evenHarmonics;
    dsp.setParameters (parameters);
    float* channels[] { samples };
    dsp.process (channels, 1, count);
}
