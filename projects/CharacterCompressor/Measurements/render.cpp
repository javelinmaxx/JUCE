// Thin offline bridge to the exact production DSP; no reimplementation of its equations.
#include "../Source/CompressorDSP.h"
extern "C" int latency_samples() { return character::Compressor::latencySamples; }
extern "C" void render (float* samples, int count, double rate, int model,
                        float threshold, float ratio, float attack, float release,
                        float knee, float highpass, int transformer, float drive)
{
    character::Compressor dsp;
    dsp.prepare (rate);
    character::Parameters p;
    p.model = static_cast<character::Model> (model);
    p.thresholdDb = threshold; p.ratio = ratio;
    p.attackMs = attack; p.releaseMs = release; p.kneeDb = knee;
    p.sidechainHz = highpass; p.transformer = transformer != 0; p.driveDb = drive;
    dsp.setParameters (p);
    float* channels[] { samples };
    dsp.process (channels, 1, count);
}
