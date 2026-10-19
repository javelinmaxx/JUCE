// Thin offline bridge to measure the production DSP with Upward enabled.
#include "../Source/CompressorDSP.h"

extern "C" void render_upward (float* samples, int count, double rate, int model,
                               float threshold, float ratio)
{
    character::Compressor dsp;
    dsp.prepare (rate);
    character::Parameters parameters;
    parameters.model = static_cast<character::Model> (model);
    parameters.thresholdDb = threshold;
    parameters.ratio = ratio;
    parameters.kneeDb = 0;
    parameters.sidechainHz = 0;
    parameters.upward = true;
    dsp.setParameters (parameters);
    float* channels[] { samples };
    dsp.process (channels, 1, count);
}
