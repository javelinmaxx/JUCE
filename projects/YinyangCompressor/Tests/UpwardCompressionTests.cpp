#include "../Source/CompressorDSP.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
int checks = 0;

void check (bool condition, const char* message)
{
    ++checks;
    if (! condition)
        throw std::runtime_error (message);
}

void render (character::Compressor& compressor, std::vector<float>& left,
             std::vector<float>& right, int blockSize = 256)
{
    for (size_t offset = 0; offset < left.size(); offset += static_cast<size_t> (blockSize))
    {
        float* channels[] { left.data() + offset, right.data() + offset };
        const auto count = static_cast<int> (std::min (static_cast<size_t> (blockSize), left.size() - offset));
        compressor.process (channels, 2, count);
    }
}

float steadyOutput (character::Model model, float levelDb, float ratio,
                    double sampleRate = 48000, bool upward = true,
                    float mix = 1, bool bypass = false)
{
    character::Compressor compressor;
    compressor.prepare (sampleRate);
    character::Parameters parameters;
    parameters.model = model;
    parameters.thresholdDb = -18;
    parameters.ratio = ratio;
    parameters.kneeDb = 0;
    parameters.sidechainHz = 0;
    parameters.driveDb = 0;
    parameters.upward = upward;
    parameters.mix = mix;
    parameters.bypass = bypass;
    compressor.setParameters (parameters);

    const auto input = character::Compressor::gain (levelDb);
    std::vector<float> left (static_cast<size_t> (sampleRate * 2), input);
    auto right = left;
    render (compressor, left, right, 37);
    return left.back();
}

void testModelCurves()
{
    using namespace character;
    constexpr float thresholdDb = -18;
    constexpr float quietDb = -30;
    constexpr float ratio = 2;
    const auto expectedBoostDb = (thresholdDb - quietDb) * (1 - 1 / ratio);

    for (int index = 0; index < 4; ++index)
    {
        const auto model = static_cast<Model> (index);
        const auto above = steadyOutput (model, -6, ratio);
        check (std::abs (Compressor::db (above) + 6) < 0.03f,
               "above-threshold signal remains at unity in Upward mode for every model");

        const auto boosted = steadyOutput (model, quietDb, ratio);
        const auto measuredBoostDb = Compressor::db (boosted) - quietDb;
        if (model == Model::variMu)
        {
            const auto effectiveRatio = 1 + (ratio - 1) * (1 - std::exp (-12.0f / 12));
            const auto expectedMuBoostDb = 12 * (1 - 1 / effectiveRatio);
            check (std::abs (measuredBoostDb - expectedMuBoostDb) < 0.12f,
                   "Variable-mu Upward curve rises progressively toward its maximum ratio");
        }
        else
        {
            check (std::abs (measuredBoostDb - expectedBoostDb) < 0.12f,
                   "below-threshold boost follows the configured ratio for every other model");
        }

        for (const auto requestedRatio : { 4.0f, 20.0f })
        {
            const auto cappedOutput = steadyOutput (model, quietDb, requestedRatio);
            check (std::abs (Compressor::db (cappedOutput) - Compressor::db (boosted)) < 0.03f,
                   "Upward ratios above 2:1 are capped at the supported maximum for every model");
        }

        check (std::abs (steadyOutput (model, quietDb, 1) - Compressor::gain (quietDb)) < 1.0e-6f,
               "1:1 ratio stays at unity in Upward mode");
        check (std::abs (steadyOutput (model, quietDb, ratio, 48000, true, 0) - Compressor::gain (quietDb)) < 1.0e-6f,
               "zero wet mix retains DC level through the shared 4x filters");
        check (steadyOutput (model, quietDb, ratio, 48000, true, 1, true) == Compressor::gain (quietDb),
               "bypass remains an exact latency-aligned input");
    }
}

void testSampleRates()
{
    using namespace character;
    for (const auto rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int index = 0; index < 4; ++index)
        {
            const auto output = steadyOutput (static_cast<Model> (index), -30, 20, rate);
            const auto outputDb = Compressor::db (output);
            check (std::isfinite (output) && outputDb > -30 && outputDb < -17,
                   "Upward gain remains finite and below threshold at every supported rate");
        }
}

}

int main()
{
    try
    {
        testModelCurves();
        testSampleRates();
        std::cout << checks << " Upward Compression checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
