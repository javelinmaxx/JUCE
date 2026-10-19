// Focused output-stage bridge; runs the production compressor with ratio 1:1.
#include "../Source/CompressorDSP.h"
extern "C" void render_output (float* data, int count, double rate, float drive,
                               int transformer, int tilt, int bypass)
{
    character::Compressor dsp;
    dsp.prepare (rate);
    character::Parameters p;
    p.ratio = 1; p.sidechainHz = 0; p.driveDb = drive;
    p.transformer = transformer != 0; p.highTiltDb = tilt != 0 ? 6.0f : 0.0f;
    p.bypass = bypass != 0;
    dsp.setParameters (p);
    float* channels[] { data };
    dsp.process (channels, 1, count);
}

extern "C" void render_detector_shelf (float* data, int count, double rate)
{
    character::HighTilt shelf;
    shelf.prepare (rate);
    shelf.setGainDb (6);
    for (int i = 0; i < count; ++i)
        data[i] = shelf.process (data[i]);
}

extern "C" void render_sidechain (float* data, int count, double rate, int model,
                                  int tilt, float highpass)
{
    character::Compressor dsp;
    dsp.prepare (rate);
    character::Parameters p;
    p.model = static_cast<character::Model> (model);
    p.thresholdDb = -24; p.ratio = 4; p.kneeDb = 0; p.sidechainHz = highpass;
    p.highTiltDb = tilt != 0 ? 6.0f : 0.0f;
    dsp.setParameters (p);
    float* channels[] { data };
    dsp.process (channels, 1, count);
}
