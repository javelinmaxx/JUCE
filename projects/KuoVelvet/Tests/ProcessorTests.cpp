#include "../Source/PluginEditor.h"
#include "../../StagePluginTests.h"
using namespace stageTests;
namespace
{
void referenceTests()
{
    // Captured from commit b5297a76ba before the shared-stage extraction, with
    // Haas center and variability at zero; this fixture is independent of the new code.
    std::ifstream file (juce::File (STAGE_PROJECT_DIR).getChildFile ("Tests/Fixtures/kuo-stereo-velvet.csv").getFullPathName().toStdString());
    check (file.good(), "pre-refactor Kuo Stereo audio fixture exists");
    std::string line; std::getline (file, line);
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        KuoVelvetProcessor p;
        set (p, "width", 85); set (p, "transientProtect", 100); set (p, "guardAttack", 0.7f);
        set (p, "velvetLow", 175); set (p, "velvetMid", 65); set (p, "velvetHigh", 140);
        p.prepareToPlay (rate, 127);
        check (p.getLatencySamples() == juce::roundToInt (0.0015 * rate), "Kuo Velvet retains 1.5 ms lookahead");
        uint32_t state = 12345; juce::MidiBuffer midi; double error = 0;
        for (int base = 0; base < 8192; base += 127)
        {
            const int n = std::min (127, 8192 - base); juce::AudioBuffer<float> b (2, n);
            if (base > 4096) set (p, "guardAttack", 5);
            for (int i = 0; i < n; ++i)
            {
                state = state * 1664525u + 1013904223u;
                const auto noise = (static_cast<float> (state >> 8) / 8388608.0f - 1.0f) * 0.075f;
                const auto t = static_cast<double> (base + i) / rate;
                const auto x = noise + static_cast<float> (0.12 * std::sin (2 * 3.141592653589793 * 220 * t));
                b.setSample (0, i, x + ((base+i) % 2000 == 0 ? 0.9f : 0.0f)); b.setSample (1, i, x * 0.6f);
            }
            p.processBlock (b, midi);
            for (int i = 0; i < n; ++i)
            {
                if (! std::getline (file, line)) throw std::runtime_error ("incomplete reference fixture");
                std::replace (line.begin(), line.end(), ',', ' '); std::istringstream row (line);
                double storedRate = 0, left = 0, right = 0; int index = 0; row >> storedRate >> index >> left >> right;
                if (! row || std::abs (storedRate - rate) > 0.01 || index != base + i) throw std::runtime_error ("invalid reference fixture");
                error = std::max ({ error, std::abs (b.getSample (0, i) - left), std::abs (b.getSample (1, i) - right) });
            }
        }
        check (error < 1.0e-6, "Velvet matches original Kuo Stereo with transient protection, automation and band trims");
        std::cout << "Pre-refactor parity at " << rate << " Hz: " << error << " maximum error\n";
    }
}
float impulseEnergy (float protect, float attack)
{
    KuoVelvetProcessor p; set (p, "width", 100); set (p, "transientProtect", protect); set (p, "guardAttack", attack);
    p.prepareToPlay (48000, 128); juce::MidiBuffer midi; float energy = 0;
    for (int base = 0; base < 8192; base += 128)
    {
        juce::AudioBuffer<float> b (2, 128); b.clear();
        if (base <= 4000 && base + 128 > 4000) { b.setSample (0, 4000-base, 1); b.setSample (1, 4000-base, 1); }
        p.processBlock (b, midi);
        for (int i = 0; i < 128; ++i)
        {
            const auto side = 0.5f * (b.getSample (0, i) - b.getSample (1, i)); energy += side * side;
        }
    }
    return energy;
}
void velvetTests()
{
    std::array<double, 4> modeSideEnergy {};
    for (int mode = 0; mode < 4; ++mode)
    {
        KuoVelvetProcessor p; set (p, "transientProtect", 0); set (p, "width", mode == 1 ? 0.0f : 100.0f);
        if (mode == 2) { set (p, "velvetLow", 0); set (p, "velvetMid", 0); set (p, "velvetHigh", 0); }
        if (mode == 3) { set (p, "velvetLow", 200); set (p, "velvetMid", 200); set (p, "velvetHigh", 200); }
        p.prepareToPlay (48000, 128);
        juce::MidiBuffer midi; std::vector<float> input; double error = 0, sideEnergy = 0;
        uint32_t random = 98765;
        for (int base = 0; base < 16384; base += 128)
        {
            juce::AudioBuffer<float> b (2, 128);
            for (int i = 0; i < 128; ++i)
            {
                random = random * 1664525u + 1013904223u;
                const auto x = (static_cast<float> (random >> 8) / 8388608.0f - 1.0f) * 0.2f;
                input.push_back (x); b.setSample (0, i, x); b.setSample (1, i, x);
            }
            p.processBlock (b, midi);
            for (int i = 0; i < 128; ++i)
            {
                const auto index = base+i-72;
                const auto expected = index >= 0 ? input[static_cast<size_t> (index)] : 0.0f;
                const auto left = b.getSample (0, i), right = b.getSample (1, i);
                error = std::max (error, static_cast<double> (std::abs (0.5f * (left+right) - expected)));
                const auto side = 0.5f * (left-right); sideEnergy += side * side;
            }
        }
        modeSideEnergy[static_cast<size_t> (mode)] = sideEnergy;
        check (error < 1.0e-6, "mono input's mid signal remains unchanged by Velvet");
        if (mode == 0 || mode == 3) check (sideEnergy > 1.0, "Velvet widens mono audio with 100 and 200 percent band levels");
        else check (sideEnergy < 1.0e-10, "zero width or all bands at zero produces no additional side energy");
    }
    check (std::abs (modeSideEnergy[3] / modeSideEnergy[0] - 4.0) < 1.0e-4,
           "200 percent band levels double added side amplitude relative to 100 percent");
    const auto dryEnergy = impulseEnergy (0, 0), protectedEnergy = impulseEnergy (100, 0), slowEnergy = impulseEnergy (100, 5);
    check (dryEnergy > 0.1f && protectedEnergy < dryEnergy * 0.01f, "full transient protection removes the isolated impulse's convolution tail");
    check (slowEnergy > protectedEnergy && slowEnergy < dryEnergy, "Attack controls the amount of convolution admitted at an onset");
    // Full protection must still widen a settled noise bed between real onsets.
    KuoVelvetProcessor noiseProcessor;
    set (noiseProcessor, "width", 100); set (noiseProcessor, "transientProtect", 100);
    noiseProcessor.prepareToPlay (48000, 128);
    juce::MidiBuffer noiseMidi; uint32_t noiseState = 13579; double noiseSideEnergy = 0;
    for (int base = 0; base < 16384; base += 128)
    {
        juce::AudioBuffer<float> b (2, 128);
        for (int i = 0; i < 128; ++i)
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto x = (static_cast<float> (noiseState >> 8) / 8388608.0f - 1.0f) * 0.1f;
            b.setSample (0, i, x); b.setSample (1, i, x);
        }
        noiseProcessor.processBlock (b, noiseMidi);
        if (base >= 8192)
            for (int i = 0; i < 128; ++i)
            {
                const auto side = 0.5f * (b.getSample (0, i) - b.getSample (1, i));
                noiseSideEnergy += side * side;
            }
    }
    check (noiseSideEnergy > 0.1, "Transient at 100 percent still widens a settled noise bed");
    for (bool hostBypass : { false, true })
    {
        KuoVelvetProcessor p; set (p, "output", 12); set (p, "bypass", hostBypass ? 0.0f : 1.0f); p.prepareToPlay (48000, 256);
        juce::AudioBuffer<float> b (2, 256); b.clear(); b.setSample (0, 0, 1); b.setSample (1, 0, -0.5f);
        juce::MidiBuffer midi;
        if (hostBypass) p.processBlockBypassed (b, midi); else p.processBlock (b, midi);
        check (std::abs (b.getSample (0, 72) - 1) < 1.0e-7f && std::abs (b.getSample (1, 72) + 0.5f) < 1.0e-7f,
               "host and parameter bypass preserve input level and lookahead latency");
    }
}
}
int main (int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        stateTests<KuoVelvetProcessor> (8); referenceTests(); velvetTests();
        blockSizeTests<KuoVelvetProcessor>();
        if (argc > 2 && juce::String (argv[1]) == "--editor") editorTests<KuoVelvetProcessor> (juce::File (argv[2]), 561, 7);
        std::cout << checks << " Kuo Velvet checks passed\n"; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
