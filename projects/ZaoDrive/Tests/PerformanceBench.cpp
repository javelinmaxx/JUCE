#include "../Source/DriveEngine.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

// Diagnostic benchmark, not a timing-sensitive pass/fail test. Run the same
// Release binary on an idle machine; the optional file captures audio parity.
int main (int argc, char** argv)
{
    juce::ScopedNoDenormals noDenormals;
    std::ofstream audio;
    if (argc > 1) audio.open (argv[1], std::ios::binary);
    std::cout << "kind,rate,mode,median_ms,samples,checksum\n" << std::setprecision (10);
    for (double rate : { 48000., 96000., 192000. })
        for (int mode = -1; mode < 4; ++mode)
        {
            const int count = static_cast<int> (rate / 4);
            std::array<double, 3> timings {};
            double checksum = 0;
            for (int trial = 0; trial < 3; ++trial)
            {
                zao::DriveEngine engine;
                zao::GreyNoiseFilter filter;
                if (mode < 0) filter.prepare (rate); else engine.prepare (rate, 128, false);
                zao::Settings settings; settings.model = std::max (0, mode);
                juce::AudioBuffer<float> signal (2, count);
                for (int i = 0; i < count; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        signal.setSample (ch, i, static_cast<float> (.21*std::sin (2*zao::pi*(173+ch*79)*i/rate)
                            + .09*std::sin (2*zao::pi*2300*i/rate) + (i % 2048 == 0 ? .4 : 0)));
                const auto start = std::chrono::steady_clock::now();
                if (mode < 0)
                {
                    for (int i = 0; i < count; ++i)
                        for (int ch = 0; ch < 2; ++ch)
                            signal.setSample (ch, i, filter.process (signal.getSample (ch, i), static_cast<size_t> (ch)));
                }
                else
                    for (int offset = 0; offset < count; offset += 128)
                    {
                        const auto n = std::min (128, count-offset);
                        juce::AudioBuffer<float> block (signal.getArrayOfWritePointers(), 2, offset, n);
                        // Exercise coefficient smoothing and circuit controls too.
                        settings.toneHz = offset < count/2 ? 10000.f : 4300.f;
                        settings.bjtBias = offset < count/2 ? 0.f : -.3f;
                        settings.diodeKnee = offset < count/2 ? .5f : .8f;
                        engine.process (block, settings);
                    }
                timings[static_cast<size_t> (trial)] = std::chrono::duration<double, std::milli>
                    (std::chrono::steady_clock::now()-start).count();
                if (trial == 0)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        if (audio) audio.write (reinterpret_cast<const char*> (signal.getReadPointer (ch)),
                            static_cast<std::streamsize> (count)*static_cast<std::streamsize> (sizeof (float)));
                        for (int i = 0; i < count; ++i) checksum += signal.getSample (ch, i);
                    }
            }
            std::sort (timings.begin(), timings.end());
            std::cout << (mode < 0 ? "fir" : "engine") << ',' << rate << ',' << mode << ','
                      << timings[1] << ',' << count << ',' << checksum << '\n';
        }
}
