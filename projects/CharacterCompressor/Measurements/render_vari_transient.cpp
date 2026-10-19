// Render a DC envelope step through the production compressor DSP.
#include "../Source/CompressorDSP.h"

extern "C" int latency_samples()
{
    return character::Compressor::latencySamples;
}

extern "C" void render_vari_transient (float* samples, int count, double rate,
                                        int model, float variTransient)
{
    character::Compressor dsp;
    dsp.prepare (rate);
    character::Parameters p;
    p.model = static_cast<character::Model> (model);
    p.thresholdDb = -18;
    p.ratio = 4;
    p.attackMs = 10;
    p.releaseMs = 150;
    p.kneeDb = 6;
    p.sidechainHz = 0;
    p.variTransient = variTransient;
    dsp.setParameters (p);
    float* channels[] { samples };
    dsp.process (channels, 1, count);
}
