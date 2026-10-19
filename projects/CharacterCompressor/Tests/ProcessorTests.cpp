#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
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
    check (p.getParameters().size() == 19, "all host parameters registered");
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
    set (p, "highTilt", 1); set (p, "evenHarmonics", 37.5f); set (p, "variTransient", 200);
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

    // Exercise the automatable Upward parameter through the real processor.
    set (restored, "model", 0);
    set (restored, "input", 0); set (restored, "threshold", -18); set (restored, "ratio", 4);
    set (restored, "attack", 10); set (restored, "release", 150); set (restored, "knee", 0);
    set (restored, "output", 0); set (restored, "mix", 100); set (restored, "sidechain", 0);
    set (restored, "drive", 0); set (restored, "transformer", 0); set (restored, "highTilt", 0);
    set (restored, "bypass", 0); set (restored, "upward", 1);
    restored.prepareToPlay (48000, 256);
    const auto quietInput = std::pow (10.0f, -30.0f / 20.0f);
    double quietOutputEnergy = 0;
    for (int block = 0; block < 240; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), quietInput, buffer.getNumSamples());
        restored.processBlock (buffer, midi);
        if (block >= 180)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                quietOutputEnergy += buffer.getSample (0, i) * buffer.getSample (0, i);
    }
    const auto quietOutputDb = 10 * std::log10 (quietOutputEnergy
        / (60.0 * buffer.getNumSamples()));
    check (quietOutputDb > -22 && quietOutputDb < -20,
           "host processor boosts below-threshold audio toward threshold in Upward mode");
    check (restored.reduction.load() > 8,
           "host processor reports upward gain on the GR meter");

    const auto loudInput = std::pow (10.0f, -6.0f / 20.0f);
    for (int block = 0; block < 240; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), loudInput, buffer.getNumSamples());
        restored.processBlock (buffer, midi);
    }
    if (std::abs (buffer.getSample (0, 255) - loudInput) >= 0.001f || restored.reduction.load() >= 0.01f)
        std::cerr << "Upward above-threshold output " << buffer.getSample (0, 255)
                  << " vs input " << loudInput << ", gain meter " << restored.reduction.load() << " dB\n";
    check (std::abs (buffer.getSample (0, 255) - loudInput) < 0.001f
           && restored.reduction.load() < 0.01f,
           "host processor leaves above-threshold audio unchanged in Upward mode");

    if (argc > 1 && juce::String (argv[1]) == "--editor")
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
        check (editor && editor->getWidth() == 580 && editor->getHeight() == 980, "portrait editor creates at expected size");
        check (editor->getNumChildComponents() >= 25, "editor contains controls and meters");
        juce::Slider *threshold = nullptr, *knee = nullptr, *ratio = nullptr;
        juce::Slider *attack = nullptr, *release = nullptr, *output = nullptr, *mix = nullptr;
        AnimatedSwitch *transformer = nullptr, *highTilt = nullptr, *upward = nullptr;
        AnimatedSwitch *bypass = nullptr, *midSide = nullptr;
        juce::Slider *stereoLink = nullptr, *evenHarmonics = nullptr, *variTransient = nullptr;
        for (auto* child : editor->getChildren())
        {
            check (editor->getLocalBounds().contains (child->getBounds()), "controls fit inside portrait panel");
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
            {
                if (slider->getName() == "THRESHOLD") threshold = slider;
                if (slider->getName() == "KNEE") knee = slider;
                if (slider->getName() == "RATIO") ratio = slider;
                if (slider->getName() == "ATTACK") attack = slider;
                if (slider->getName() == "RELEASE") release = slider;
                if (slider->getName() == "OUTPUT") output = slider;
                if (slider->getName() == "MIX") mix = slider;
                if (slider->getName() == "STEREO LINK") stereoLink = slider;
                if (slider->getName() == "EVEN HARMONICS") evenHarmonics = slider;
                if (slider->getName() == "VARITRANSIENT") variTransient = slider;
            }
            if (auto* button = dynamic_cast<AnimatedSwitch*> (child))
            {
                if (button->getButtonText() == "OUTPUT TRANSFORMER") transformer = button;
                if (button->getButtonText() == "SC HIGH TILT +6 dB") highTilt = button;
                if (button->getButtonText() == "UPWARD COMPRESSION") upward = button;
                if (button->getButtonText() == "BYPASS") bypass = button;
                if (button->getButtonText() == "MID / SIDE MODE") midSide = button;
            }
        }
        check (threshold && knee && ratio && attack && release && output && mix && transformer
               && highTilt && upward && bypass && midSide && stereoLink && evenHarmonics && variTransient,
               "editor includes the complete knob and switch set");
        check (threshold->getHeight() > attack->getHeight() && ratio->getHeight() > release->getHeight(),
               "primary knobs are larger than timing knobs");
        check (threshold->getY() == knee->getY() && knee->getY() == ratio->getY()
               && threshold->getRight() <= knee->getX() && knee->getRight() <= ratio->getX(),
               "knee is between threshold and ratio");
        check (attack->getY() == variTransient->getY() && variTransient->getY() == release->getY()
               && attack->getRight() <= variTransient->getX() && variTransient->getRight() <= release->getX(),
               "VariTransient is between attack and release");
        check (output->getY() == mix->getY() && mix->getY() == stereoLink->getY()
               && output->getY() > 176 + 400,
               "output, mix, and stereo link sit below the shortened GR section");
        check (transformer->getHeight() > transformer->getWidth()
               && highTilt->getHeight() > highTilt->getWidth()
               && upward->getHeight() > upward->getWidth()
               && bypass->getHeight() > bypass->getWidth()
               && midSide->getHeight() > midSide->getWidth(),
               "all animated switches use a vertical layout");
        GainReductionHistory history;
        for (int i = 0; i <= 360; ++i) history.push (i / 30.0, static_cast<float> (i % 31));
        int visible = 0;
        float previousX = -1;
        history.visit (12.0, [&] (float x, float gr, double time)
        {
            check (x >= previousX && x >= 0 && x <= 1 && gr >= 0 && gr <= 30 && time >= 7,
                   "wrapped history remains chronological within five seconds");
            previousX = x;
            ++visible;
        });
        check (visible >= 150 && visible <= 151, "history retains five seconds after ring wraps");
        visible = 0;
        history.visit (18.0, [&] (float, float, double) { ++visible; });
        check (visible == 0, "history expires by elapsed time when UI timer stalls");
        // Run a real message loop so switch clicks and timer-driven animation execute.
        set (restored, "transformer", 0);
        transformer->setToggleState (false, juce::dontSendNotification);
        set (restored, "upward", 0);
        upward->setToggleState (false, juce::dontSendNotification);
        struct CaptureTimer final : juce::Timer
        {
            std::function<void()> tick;
            void timerCallback() override { tick(); }
        } capture;
        int frame = 0;
        bool clickUpdatedParameter = false, automationUpdatedSwitch = false, upwardUpdatedParameter = false;
        capture.tick = [&]
        {
            ++frame;
            restored.reduction.store (8.0f + 6.0f * std::sin (static_cast<float> (frame) * 0.09f));
            if (frame == 8)
            {
                clickUpdatedParameter = transformer->getToggleState()
                    && exact (restored.parameters.getRawParameterValue ("transformer")->load(), 1.0f);
                set (restored, "transformer", 0);
            }
            if (frame == 16)
            {
                automationUpdatedSwitch = ! transformer->getToggleState();
                set (restored, "transformer", 1);
            }
            if (frame == 20) upward->triggerClick();
            if (frame == 28)
                upwardUpdatedParameter = upward->getToggleState()
                    && exact (restored.parameters.getRawParameterValue ("upward")->load(), 1.0f);
            if (frame == 180) juce::MessageManager::getInstance()->stopDispatchLoop();
        };
        transformer->triggerClick();
        capture.startTimer (34);
        juce::MessageManager::getInstance()->runDispatchLoop();
        capture.stopTimer();
        check (clickUpdatedParameter, "animated switch click updates host parameter");
        check (automationUpdatedSwitch, "host automation updates animated switch");
        check (upwardUpdatedParameter, "upward switch updates new host parameter");
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
    check (p.getParameters().size() == 19, "stereo, mid/side, and transformer parameters registered");
    auto* tilt = p.parameters.getParameter ("highTilt");
    check (tilt && tilt == p.getParameters()[13]
           && p.parameters.getParameter ("upward") == p.getParameters()[14],
           "high tilt and upward retain their appended parameter indices");
    check (p.parameters.getParameter ("stereoLink") == p.getParameters()[15]
           && p.parameters.getParameter ("midSide") == p.getParameters()[16],
           "stereo link and mid/side append without moving existing automation");
    check (p.parameters.getParameter ("evenHarmonics") == p.getParameters()[17]
           && exact (p.parameters.getRawParameterValue ("evenHarmonics")->load(), 0.0f),
           "Even harmonics is appended and defaults off");
    check (p.parameters.getParameter ("variTransient") == p.getParameters()[18]
           && exact (p.parameters.getRawParameterValue ("variTransient")->load(), 0.0f),
           "VariTransient is appended and defaults to no envelope change");
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
    oldState.removeChild (oldState.getChildWithProperty ("id", "evenHarmonics"), nullptr);
    oldState.removeChild (oldState.getChildWithProperty ("id", "variTransient"), nullptr);
    juce::MemoryBlock legacy;
    juce::AudioProcessor::copyXmlToBinary (*oldState.createXml(), legacy);
    restored.setStateInformation (legacy.getData(), static_cast<int> (legacy.getSize()));
    check (exact (restored.parameters.getRawParameterValue ("highTilt")->load(), 0.0f),
           "legacy state turns shelf off even when previously enabled");
    check (exact (restored.parameters.getRawParameterValue ("ratio")->load(), 1.0f),
           "legacy state preserves existing settings");
    check (exact (restored.parameters.getRawParameterValue ("evenHarmonics")->load(), 0.0f),
           "legacy state defaults transformer DC current to off");
    check (exact (restored.parameters.getRawParameterValue ("variTransient")->load(), 0.0f),
           "legacy state defaults VariTransient to no envelope change");

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

// Capture all four simulation palettes and their Upward reversals using the real editor.
int renderThemePreviews (const char* directory)
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::File destination (directory);
    check (destination.createDirectory(), "preview directory can be created");
    CharacterCompressorProcessor processor;
    processor.prepareToPlay (48000, 256);
    set (processor, "model", 0);
    set (processor, "upward", 0);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    const std::array<juce::String, 4> names { "fet", "vca", "variable-mu", "optical" };
    const std::array<juce::Colour, 8> background {
        juce::Colour (0xff252b31), juce::Colour (0xff142b3d),
        juce::Colour (0xffe4f0df), juce::Colour (0xfff5f7f6),
        juce::Colour (0xff382f22), juce::Colour (0xff23465d),
        juce::Colour (0xff355a46), juce::Colour (0xff3f2026) };
    const std::array<juce::Colour, 8> meter {
        juce::Colour (0xffe2b85f), juce::Colour (0xfff7fbff),
        juce::Colour (0xfff8fff7), juce::Colour (0xffdc4a55),
        juce::Colour (0xffa9b1b9), juce::Colour (0xff48a9dc),
        juce::Colour (0xff91c98a), juce::Colour (0xfff8faf9) };
    struct CaptureTimer final : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } timer;
    int stage = 0, frame = 0;
    bool succeeded = true;
    timer.tick = [&]
    {
        ++frame;
        processor.reduction.store (12.0f + 4.0f * std::sin (static_cast<float> (frame) * 0.09f));
        if (frame < 155) return;
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        const auto filename = names[static_cast<size_t> (stage % 4)]
                            + (stage < 4 ? ".png" : "-upward.png");
        auto file = destination.getChildFile (filename);
        succeeded = succeeded && (! file.existsAsFile() || file.deleteFile());
        auto stream = file.createOutputStream();
        succeeded = succeeded && image.isValid() && stream != nullptr;
        if (succeeded)
            succeeded = juce::PNGImageFormat().writeImageToStream (image, *stream)
                     && image.getPixelAt (0, 0) == background[static_cast<size_t> (stage)]
                     && image.getPixelAt (398, 250) == meter[static_cast<size_t> (stage)];
        ++stage;
        frame = 0;
        if (stage == 8)
        {
            juce::MessageManager::getInstance()->stopDispatchLoop();
            return;
        }
        set (processor, "model", stage % 4);
        set (processor, "upward", stage >= 4 ? 1.0f : 0.0f);
    };
    timer.startTimer (34);
    juce::MessageManager::getInstance()->runDispatchLoop();
    timer.stopTimer();
    check (succeeded && stage == 8, "eight model and upward preview colors match their palettes");
    std::cout << "8 theme previews saved to " << destination.getFullPathName() << '\n';
    return 0;
}

int main (int argc, char** argv)
{
    try
    {
        if (argc > 2 && juce::String (argv[1]) == "--preview-modes")
            return renderThemePreviews (argv[2]);
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
