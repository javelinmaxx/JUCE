#include "../Source/CompressorDSP.h"
#include <chrono>
#include <iostream>
#include <vector>

// Diagnostic timing only: run a Release build on an idle machine. Preparation
// and buffer generation are excluded; all three trials process identical audio.
int main()
{
    std::cout << "transformer,model,median_ms,samples\n";
    for (bool colour : { false, true })
        for (int mode = 0; mode < 4; ++mode)
        {
            std::array<double, 3> times {};
            for (int trial = 0; trial < 3; ++trial)
            {
                character::Compressor dsp; dsp.prepare (48000);
                character::Parameters p; p.model = static_cast<character::Model> (mode);
                p.transformer = colour; dsp.setParameters (p);
                std::vector<float> left (12000), right (12000);
                for (int i = 0; i < 12000; ++i)
                {
                    left[static_cast<size_t> (i)] = .3f*std::sin (i*.031f)+.1f*std::cos (i*.7f);
                    right[static_cast<size_t> (i)] = .2f*std::sin (i*.047f);
                }
                const auto start = std::chrono::steady_clock::now();
                for (int offset = 0; offset < 12000; offset += 128)
                {
                    float* channels[] { left.data()+offset, right.data()+offset };
                    dsp.process (channels, 2, std::min (128, 12000-offset));
                }
                times[static_cast<size_t> (trial)] = std::chrono::duration<double, std::milli>
                    (std::chrono::steady_clock::now()-start).count();
            }
            std::sort (times.begin(), times.end());
            std::cout << colour << ',' << mode << ',' << times[1] << ",12000\n";
        }
}
