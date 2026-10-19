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
bool closeColour (juce::Colour a, juce::Colour b, int tolerance)
{
    return std::abs (static_cast<int> (a.getRed()) - static_cast<int> (b.getRed())) <= tolerance
        && std::abs (static_cast<int> (a.getGreen()) - static_cast<int> (b.getGreen())) <= tolerance
        && std::abs (static_cast<int> (a.getBlue()) - static_cast<int> (b.getBlue())) <= tolerance;
}
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
    check (p.getParameters().size() == 20, "all host parameters registered");
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
    set (p, "highTilt", 1); set (p, "scHighTilt", 8);
    set (p, "evenHarmonics", 37.5f); set (p, "variTransient", 200);
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
        check (editor && editor->getWidth() == 548 && editor->getHeight() == 756, "compact editor creates at expected size");
        check (editor->getNumChildComponents() >= 25, "editor contains controls and meters");
        juce::Slider *threshold = nullptr, *knee = nullptr, *ratio = nullptr;
        juce::Slider *attack = nullptr, *release = nullptr, *output = nullptr, *mix = nullptr;
        AnimatedSwitch *transformer = nullptr, *upward = nullptr;
        AnimatedSwitch *bypass = nullptr, *midSide = nullptr;
        juce::Slider *stereoLink = nullptr, *evenHarmonics = nullptr, *variTransient = nullptr;
        juce::Slider *detectorHpf = nullptr, *scHighTilt = nullptr, *input = nullptr;
        juce::Slider *transformerDrive = nullptr;
        juce::Label *thresholdLabel = nullptr, *ratioLabel = nullptr, *inputLabel = nullptr;
        for (auto* child : editor->getChildren())
        {
            check (editor->getLocalBounds().contains (child->getBounds()), "controls fit inside the editor panel");
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
            {
                if (slider->getName() == "THRESHOLD") threshold = slider;
                if (slider->getName() == "KNEE") knee = slider;
                if (slider->getName() == "RATIO") ratio = slider;
                if (slider->getName() == "ATTACK") attack = slider;
                if (slider->getName() == "RELEASE") release = slider;
                if (slider->getName() == "OUTPUT") output = slider;
                if (slider->getName() == "MIX") mix = slider;
                if (slider->getName() == "INPUT") input = slider;
                if (slider->getName() == "DETECTOR HPF") detectorHpf = slider;
                if (slider->getName() == "SC HIGH TILT") scHighTilt = slider;
                if (slider->getName() == "CORE DRIVE") transformerDrive = slider;
                if (slider->getName() == "STEREO LINK") stereoLink = slider;
                if (slider->getName() == "DC BIAS") evenHarmonics = slider;
                if (slider->getName() == "VARITRANSIENT") variTransient = slider;
            }
            if (auto* label = dynamic_cast<juce::Label*> (child))
            {
                if (label->getText() == "THRES") thresholdLabel = label;
                if (label->getText() == "RATIO") ratioLabel = label;
                if (label->getText() == "INPUT") inputLabel = label;
            }
            if (auto* button = dynamic_cast<AnimatedSwitch*> (child))
            {
                if (button->getButtonText() == "OUTPUT TRANSFORMER") transformer = button;
                if (button->getButtonText() == "YIN/YANG MODE") upward = button;
                if (button->getButtonText() == "BYPASS") bypass = button;
                if (button->getButtonText() == "MID / SIDE MODE") midSide = button;
            }
        }
        check (threshold && knee && ratio && attack && release && output && mix && transformer
               && upward && bypass && midSide && stereoLink && evenHarmonics && variTransient
               && input && detectorHpf && scHighTilt && transformerDrive,
               "editor includes the complete knob and switch set");
        check (threshold->getHeight() > attack->getHeight() && ratio->getHeight() > release->getHeight()
               && threshold->getHeight() == 108 && ratio->getHeight() == 108
               && knee->getHeight() == attack->getHeight() && attack->getHeight() == 60
               && variTransient->getHeight() == release->getHeight()
               && attack->getHeight() - attack->getTextBoxHeight()
                    == detectorHpf->getHeight() - detectorHpf->getTextBoxHeight()
               && release->getHeight() - release->getTextBoxHeight()
                    == scHighTilt->getHeight() - scHighTilt->getTextBoxHeight()
               && knee->getWidth() == attack->getWidth()
               && variTransient->getWidth() == release->getWidth(),
               "Knee and VariTransient match the Attack and Release knob sizes");
        check (threshold->getY() == ratio->getY()
               && threshold->getRight() <= ratio->getX()
               && attack->getY() == release->getY() && knee->getY() > attack->getY()
               && knee->getY() == variTransient->getY()
               && threshold->getX() + threshold->getWidth() / 2
                    == attack->getX() + attack->getWidth() / 2
               && attack->getX() == knee->getX() && release->getX() == variTransient->getX(),
               "Threshold/Ratio, Attack/Release, and Knee/VariTransient occupy successive rows");
        check (output->getY() == mix->getY() && mix->getY() == stereoLink->getY()
               && output->getY() > 140 + 470,
               "output, mix, and stereo link sit below the shortened GR section");
        check (transformer->getHeight() > transformer->getWidth()
               && upward->getWidth() > upward->getHeight()
               && upward->getWidth() >= bypass->getWidth()
               && upward->getY() < 180 && upward->getX() <= 40
               && upward->getWidth() >= 100 && upward->getRight() <= 150
               && bypass->getWidth() > bypass->getHeight()
               && bypass->getY() < 80 && bypass->getX() >= 450
               && midSide->getHeight() > midSide->getWidth(),
               "mode is controlled by the Compressor header and bypass remains beside the plug-in title");
        check (detectorHpf->getY() == scHighTilt->getY()
               && midSide->getY() < detectorHpf->getBottom()
               && midSide->getX() > scHighTilt->getRight(),
               "sidechain section groups the detector HPF, tilt amount, and mid/side mode");
        check (upward->getY() < threshold->getY() && input->getY() < threshold->getY()
               && input->getHeight() < attack->getHeight(),
               "upward mode and a compact input control are in the compressor header");
        check (thresholdLabel && ratioLabel && thresholdLabel->getY() >= upward->getBottom() + 4
               && thresholdLabel->getY() == 191 && ratioLabel->getY() == thresholdLabel->getY(),
               "main knob captions clear the Compressor header button");
        check (inputLabel && inputLabel->getY() + inputLabel->getHeight() / 2
                    == input->getY() + (input->getHeight() - input->getTextBoxHeight()) / 2
               && input->getX() + input->getWidth() / 2 - inputLabel->getRight() >= 0
               && input->getX() + input->getWidth() / 2 - inputLabel->getRight() <= 20,
               "Input caption sits close to and vertically centers on its knob");
        check (transformerDrive->getY() == evenHarmonics->getY()
               && transformerDrive->getWidth() == detectorHpf->getWidth()
               && transformerDrive->getHeight() == detectorHpf->getHeight()
               && evenHarmonics->getWidth() == scHighTilt->getWidth()
               && evenHarmonics->getHeight() == scHighTilt->getHeight()
               && std::abs (((scHighTilt->getX() + scHighTilt->getWidth() / 2)
                           - (detectorHpf->getX() + detectorHpf->getWidth() / 2))
                          - ((midSide->getX() + midSide->getWidth() / 2)
                           - (scHighTilt->getX() + scHighTilt->getWidth() / 2))) <= 1
               && std::abs (((evenHarmonics->getX() + evenHarmonics->getWidth() / 2)
                           - (transformerDrive->getX() + transformerDrive->getWidth() / 2))
                          - ((transformer->getX() + transformer->getWidth() / 2)
                           - (evenHarmonics->getX() + evenHarmonics->getWidth() / 2))) <= 1
               && std::abs ((midSide->getY() + 28) - (detectorHpf->getY() + (detectorHpf->getHeight() - 20) / 2)) <= 1
               && std::abs ((transformer->getY() + 28) - (transformerDrive->getY() + (transformerDrive->getHeight() - 20) / 2)) <= 1
               && transformerDrive->getY() > midSide->getBottom(),
               "toggle pivots align with the centers of knobs in their sections");
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
    check (p.getParameters().size() == 20, "all compressor parameters registered");
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
    const auto* tiltAmount = dynamic_cast<juce::RangedAudioParameter*> (
        p.parameters.getParameter ("scHighTilt"));
    check (tiltAmount && p.parameters.getParameter ("scHighTilt") == p.getParameters()[19]
           && exact (p.parameters.getRawParameterValue ("scHighTilt")->load(), 0.0f)
           && std::abs (tiltAmount->getNormalisableRange().end - 12.0f) < 0.001f,
           "SC High Tilt is appended and spans 0 to 12 dB");
    check (exact (tilt->getValue(), 0.0f), "high tilt defaults off");
    set (p, "ratio", 1); set (p, "highTilt", 1);
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    CharacterCompressorProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (exact (restored.parameters.getRawParameterValue ("highTilt")->load(), 1.0f),
           "high tilt restored from saved state");

    auto oldState = p.parameters.copyState();
    oldState.removeChild (oldState.getChildWithProperty ("id", "scHighTilt"), nullptr);
    juce::MemoryBlock legacyEnabled;
    juce::AudioProcessor::copyXmlToBinary (*oldState.createXml(), legacyEnabled);
    restored.setStateInformation (legacyEnabled.getData(), static_cast<int> (legacyEnabled.getSize()));
    check (exact (restored.parameters.getRawParameterValue ("scHighTilt")->load(), 6.0f),
           "legacy enabled sidechain shelf migrates to its previous 6 dB setting");

    oldState = p.parameters.copyState();
    oldState.removeChild (oldState.getChildWithProperty ("id", "highTilt"), nullptr);
    oldState.removeChild (oldState.getChildWithProperty ("id", "evenHarmonics"), nullptr);
    oldState.removeChild (oldState.getChildWithProperty ("id", "variTransient"), nullptr);
    oldState.removeChild (oldState.getChildWithProperty ("id", "scHighTilt"), nullptr);
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
    check (exact (restored.parameters.getRawParameterValue ("scHighTilt")->load(), 0.0f),
           "legacy disabled tilt migrates to 0 dB");

    // A real processBlock render verifies that host automation reaches the DSP.
    restored.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    const auto render = [&] (bool enabled)
    {
        set (restored, "scHighTilt", enabled ? 12.0f : 0.0f);
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
    const std::array<juce::Colour, 8> meter {
        juce::Colour (0xff80549b), juce::Colour (0xfff7fbff),
        juce::Colour (0xfff8fff7), juce::Colour (0xffdc4a55),
        juce::Colour (0xff694881), juce::Colour (0xff93b4c8),
        juce::Colour (0xff8dbd8c), juce::Colour (0xffa83e49) };
    const std::array<juce::Colour, 8> gaugeTrack {
        juce::Colour (0xff414b53), juce::Colour (0xff3c5262),
        juce::Colour (0xff526b59), juce::Colour (0xff56616a),
        juce::Colour (0xff343a40), juce::Colour (0xff304654),
        juce::Colour (0xff3b5140), juce::Colour (0xff42383d) };
    struct CaptureTimer final : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } timer;
    std::array<float, 4> normalPanelBrightness {}, normalBackgroundBrightness {};
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
        {
            const auto index = static_cast<size_t> (stage);
            const auto grLitPixel = gaugeTrack[index].interpolatedWith (meter[index], 0.50f);
            const auto meterPixel = image.getPixelAt (366, 220);
            const auto meterMatches = closeColour (meterPixel, grLitPixel, 20);
            const auto panelBrightness = image.getPixelAt (30, 220).getPerceivedBrightness();
            const auto backgroundBrightness = image.getPixelAt (536, 270).getPerceivedBrightness();
            const auto paletteIndex = static_cast<size_t> (stage % 4);
            if (stage < 4)
            {
                normalPanelBrightness[paletteIndex] = panelBrightness;
                normalBackgroundBrightness[paletteIndex] = backgroundBrightness;
            }
            else if (! (panelBrightness < normalPanelBrightness[paletteIndex]
                        && backgroundBrightness < normalBackgroundBrightness[paletteIndex]))
            {
                std::cerr << "Upward palette " << paletteIndex
                          << " is not darker than its normal panel/background.\n";
                succeeded = false;
            }
            if (! meterMatches || image.getWidth() != 548 || image.getHeight() != 726)
                std::cerr << "Preview " << stage << " dimensions " << image.getWidth() << 'x' << image.getHeight()
                          << ", meter RGB " << meterPixel.toDisplayString (false).toStdString()
                          << ", expected " << grLitPixel.toDisplayString (false).toStdString() << '\n';
            succeeded = succeeded && juce::PNGImageFormat().writeImageToStream (image, *stream)
                     && image.getWidth() == 548 && image.getHeight() == 726 && meterMatches;
        }
        ++stage;
        frame = 0;
        if (! succeeded || stage == 8)
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
    check (succeeded && stage == 8, "eight model and upward previews have the expected size and GR palette");
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
