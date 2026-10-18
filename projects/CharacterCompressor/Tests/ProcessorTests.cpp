#include "../Source/PluginProcessor.h"
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <stdexcept>

namespace
{
int checks = 0;
void check (bool ok, const char* message)
{
    ++checks;
    if (! ok) throw std::runtime_error (message);
}
bool exact (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; }
void set (CharacterCompressorProcessor& p, const char* id, float value)
{
    auto* parameter = p.parameters.getParameter (id);
    check (parameter != nullptr, "parameter exists");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}
}

int runTests (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    CharacterCompressorProcessor p;
    check (p.getParameters().size() == 14, "all host parameters registered");
    auto buses = p.getBusesLayout();
    buses.inputBuses.set (0, juce::AudioChannelSet::mono());
    buses.outputBuses.set (0, juce::AudioChannelSet::mono());
    check (p.isBusesLayoutSupported (buses), "mono supported");
    buses.outputBuses.set (0, juce::AudioChannelSet::stereo());
    check (! p.isBusesLayoutSupported (buses), "mismatched layout rejected");
    buses.inputBuses.set (0, juce::AudioChannelSet::create5point1());
    buses.outputBuses.set (0, juce::AudioChannelSet::create5point1());
    check (! p.isBusesLayoutSupported (buses), "unsupported surround rejected");

    set (p, "model", 2); set (p, "threshold", -27); set (p, "ratio", 7);
    set (p, "transformer", 1); set (p, "drive", 12); set (p, "mix", 65);
    set (p, "highTilt", 1);
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    CharacterCompressorProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    for (auto* parameter : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        check (ranged != nullptr && std::abs (ranged->getValue()
            - restored.parameters.getParameter (ranged->getParameterID())->getValue()) < 0.0001f,
            "every parameter round-trips through host state");
    }
    const char invalid[] = "not a plug-in state";
    restored.setStateInformation (invalid, sizeof (invalid));
    check (static_cast<int> (restored.parameters.getRawParameterValue ("model")->load()) == 2,
           "malformed state leaves current parameters intact");

    restored.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 200; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.setSample (ch, n, 0.5f * std::sin (static_cast<float> (block * 256 + n) * 0.1f));
        restored.processBlock (buffer, midi);
    }
    check (restored.reduction.load() > 1, "processor applies compression");
    check (restored.getLatencySamples() == 64, "host receives 64-sample oversampling latency");
    for (int block = 0; block < 100; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.25f, 256);
        restored.processBlockBypassed (buffer, midi);
    }
    check (exact (buffer.getSample (0, 255), 0.25f) && exact (buffer.getSample (1, 255), 0.25f),
           "host bypass settles to original audio");
    juce::AudioBuffer<float> empty (2, 0);
    restored.processBlock (empty, midi);
    check (restored.getBypassParameter() != nullptr, "host has a designated bypass parameter");

    if (argc > 1 && juce::String (argv[1]) == "--editor")
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
        check (editor && editor->getWidth() == 780 && editor->getHeight() == 530, "editor creates at expected size");
        check (editor->getNumChildComponents() >= 25, "editor contains controls and meters");
        // Exercise actual component rendering, not only construction.
        auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        check (image.isValid(), "native editor renders to an image");
        if (argc > 2)
        {
            auto stream = juce::File (argv[2]).createOutputStream();
            check (stream != nullptr, "editor screenshot destination opens");
            check (juce::PNGImageFormat().writeImageToStream (image, *stream), "editor screenshot writes");
        }
    }
    std::cout << checks << " processor checks passed\n";
    return 0;
}

// Focused checks for the new output option, runnable without repeating the
// existing processor/DSP suites: CharacterProcessorTests --output-stage [png].
int runOutputStageTests (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    CharacterCompressorProcessor p;
    check (p.getParameters().size() == 14, "new high-tilt parameter registered");
    auto* tilt = p.parameters.getParameter ("highTilt");
    check (tilt && tilt == p.getParameters().getLast(), "high tilt appended without shifting old indices");
    check (exact (tilt->getValue(), 0.0f), "high tilt defaults off");
    set (p, "ratio", 1); set (p, "highTilt", 1);
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    CharacterCompressorProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (exact (restored.parameters.getRawParameterValue ("highTilt")->load(), 1.0f),
           "high tilt restored from saved state");

    auto oldState = p.parameters.copyState();
    oldState.removeChild (oldState.getChildWithProperty ("id", "highTilt"), nullptr);
    juce::MemoryBlock legacy;
    juce::AudioProcessor::copyXmlToBinary (*oldState.createXml(), legacy);
    restored.setStateInformation (legacy.getData(), static_cast<int> (legacy.getSize()));
    check (exact (restored.parameters.getRawParameterValue ("highTilt")->load(), 0.0f),
           "legacy state turns shelf off even when previously enabled");
    check (exact (restored.parameters.getRawParameterValue ("ratio")->load(), 1.0f),
           "legacy state preserves existing settings");

    // A real processBlock render verifies that host automation reaches the DSP.
    restored.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    const auto render = [&] (bool enabled)
    {
        set (restored, "highTilt", enabled ? 1.0f : 0.0f);
        double energy = 0;
        for (int block = 0; block < 120; ++block)
        {
            for (int i = 0; i < 256; ++i)
            {
                const auto sample = static_cast<float> (0.1 * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 6000.0 * (block * 256 + i) / 48000.0));
                buffer.setSample (0, i, sample); buffer.setSample (1, i, sample);
            }
            restored.processBlock (buffer, midi);
            for (int i = 0; i < 256; ++i)
            {
                const auto sample = buffer.getSample (0, i);
                if (! std::isfinite (sample) || std::abs (sample) > 0.25f)
                    throw std::runtime_error ("nonfinite output or unexpected transient during shelf automation");
                if (! exact (sample, buffer.getSample (1, i)))
                    throw std::runtime_error ("shelf does not preserve identical stereo channels");
                if (block >= 100) energy += sample * sample;
            }
        }
        return energy;
    };
    const auto off = render (false), on = render (true), offAgain = render (false);
    check (std::abs (on / off - 1.0) < 0.00001, "sidechain tilt has no output EQ at ratio 1:1");
    check (std::abs (offAgain / off - 1.0) < 0.00001, "shelf automation settles back to exact off response");
    set (restored, "ratio", 4); set (restored, "threshold", -24);
    const auto compressedOff = render (false), compressedOn = render (true);
    check (10.0 * std::log10 (compressedOff / compressedOn) > 3.5,
           "sidechain high tilt increases high-frequency compression");
    set (restored, "mix", 0);
    const auto dry = render (true);
    check (std::abs (dry / off - 1.0) < 0.00001, "sidechain tilt leaves the dry audio path unchanged");
    check (restored.getLatencySamples() == 64, "high tilt preserves reported latency");

    if (argc > 2)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
        auto picture = editor->createComponentSnapshot (editor->getLocalBounds());
        auto stream = juce::File (argv[2]).createOutputStream();
        check (picture.isValid() && stream && juce::PNGImageFormat().writeImageToStream (picture, *stream),
               "updated editor renders");
    }
    std::cout << checks << " focused output-stage processor checks passed\n";
    return 0;
}

int main (int argc, char** argv)
{
    try
    {
        if (argc > 1 && juce::String (argv[1]) == "--output-stage")
            return runOutputStageTests (argc, argv);
        return runTests (argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
