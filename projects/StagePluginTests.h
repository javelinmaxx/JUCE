#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
namespace stageTests
{
inline int checks = 0;
inline void check (bool pass, const char* message)
{
    ++checks;
    if (! pass) throw std::runtime_error (message);
}
template <typename Processor> void set (Processor& p, const char* id, float value)
{
    auto* parameter = p.parameters.getParameter (id);
    if (parameter == nullptr) throw std::runtime_error (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}
template <typename Processor> void stateTests (int parameterCount)
{
    Processor p, restored;
    check (p.getParameters().size() == parameterCount, "only stage parameters are exposed to the host");
    for (auto* parameter : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            parameter->setValueNotifyingHost (ranged->convertTo0to1 (ranged->convertFrom0to1 (0.73f)));
    juce::MemoryBlock state; p.getStateInformation (state);
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    for (int i = 0; i < parameterCount; ++i)
        check (std::abs (p.getParameters()[i]->getValue() - restored.getParameters()[i]->getValue()) < 1.0e-6f,
               "all host parameters survive state restoration");
    check (p.getBypassParameter() == p.parameters.getParameter ("bypass"), "host bypass parameter is exposed");
}
template <typename Processor> void editorTests (const juce::File& destination, int height, int sliderCount)
{
    Processor p; p.prepareToPlay (48000, 128);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 128);
    for (int offset = 0; offset < 12000; offset += 128)
    {
        for (int i = 0; i < 128; ++i)
        {
            const auto x = static_cast<float> (0.16 * std::sin (2 * juce::MathConstants<double>::pi * 220 * (offset+i) / 48000.0)
                       + 0.08 * std::sin (2 * juce::MathConstants<double>::pi * 3300 * (offset+i) / 48000.0));
            block.setSample (0, i, x); block.setSample (1, i, x);
        }
        p.processBlock (block, midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setVisible (true);
    check (editor->getWidth() == 548 && editor->getHeight() == height, "native editor starts at its intended size");
    juce::TooltipWindow* tooltip = nullptr;
    juce::Slider* first = nullptr;
    int sliders = 0;
    for (auto* child : editor->getChildren())
    {
        if (auto* window = dynamic_cast<juce::TooltipWindow*> (child)) tooltip = window;
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            ++sliders; if (first == nullptr) first = slider;
            const auto expected = p.parameters.getParameter (sliders == 1 ? (sliderCount == 4 ? "drive" : "width") : "bypass");
            if (sliders == 1) check (std::abs (slider->getValue() - expected->convertFrom0to1 (expected->getValue())) < 0.01,
                                     "initial control value matches host state");
        }
        if (dynamic_cast<juce::Slider*> (child) == nullptr && dynamic_cast<juce::Button*> (child) == nullptr
            && dynamic_cast<juce::Label*> (child) == nullptr) continue;
        auto* client = dynamic_cast<juce::TooltipClient*> (child);
        const auto hint = client != nullptr ? client->getTooltip() : juce::String();
        check (hint.isNotEmpty() && hint.endsWithChar ('.') && hint.dropLastCharacters (1).indexOfChar ('.') < 0,
               "every control and caption has a one-sentence tooltip");
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
            for (auto* nested : slider->getChildren())
                if (auto* readout = dynamic_cast<juce::Label*> (nested))
                    check (readout->getTooltip() == hint, "editable value has the same tooltip");
    }
    check (tooltip != nullptr && first != nullptr && sliders == sliderCount, "stage controls and tooltip window exist");
    if (sliderCount == 4)
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::Button*> (child); button && button->getButtonText() == "CORE")
            {
                check (button->getToggleState(), "Ren's Core switch initializes On");
                check (button->getTooltip().contains ("Saves CPU when Off"), "Core tooltip explains CPU savings");
                button->setToggleState (false, juce::sendNotificationSync);
                check (p.parameters.getRawParameterValue ("transformer")->load() < 0.5f,
                       "Core switch updates its host parameter");
                button->setToggleState (true, juce::sendNotificationSync);
            }
    // Exercise UI-to-host and host-to-UI changes without an audio callback.
    const auto id = sliderCount == 4 ? "drive" : "width";
    first->setValue (sliderCount == 4 ? 12 : 75, juce::sendNotificationSync);
    check (std::abs (p.parameters.getRawParameterValue (id)->load() - first->getValue()) < 0.01,
           "mouse control updates its host parameter");
    set (p, id, sliderCount == 4 ? 6.0f : 35.0f);
    for (int tick = 0; tick < 3; ++tick) { juce::Thread::sleep (10); juce::Timer::callPendingTimersSynchronously(); }
    check (std::abs (first->getValue() - p.parameters.getRawParameterValue (id)->load()) < 0.01,
           "host automation updates its control");
    tooltip->displayTip (editor->localPointToGlobal (first->getBounds().getCentre()), first->getTooltip());
    check (tooltip->isVisible(), "native tooltip is visible in the preview");
    auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::MemoryOutputStream png;
    destination.getParentDirectory().createDirectory();
    check (image.isValid() && juce::PNGImageFormat().writeImageToStream (image, png)
        && destination.replaceWithData (png.getData(), png.getDataSize()), "native preview is saved");
    for (float factor : { 0.5f, 1.2f })
    {
        editor->setSize (juce::roundToInt (548 * factor), juce::roundToInt (height * factor));
        for (auto* child : editor->getChildren())
        {
            if (dynamic_cast<juce::Slider*> (child) == nullptr && dynamic_cast<juce::Button*> (child) == nullptr) continue;
            const auto area = editor->getLocalArea (child, child->getLocalBounds());
            auto* hit = editor->getComponentAt (area.getCentre());
            check (editor->getLocalBounds().contains (area) && (hit == child || child->isParentOf (hit)),
                   "controls remain visible and clickable at 50 and 120 percent");
        }
    }
}
}

namespace stageTests
{
template <typename Processor> void blockSizeTests()
{
    std::vector<float> baseline;
    for (int blockSize : { 127, 1, 512 })
    {
        Processor p; p.prepareToPlay (48000, blockSize); juce::MidiBuffer midi;
        uint32_t random = 7391; std::vector<float> output;
        for (int base = 0; base < 4096; base += blockSize)
        {
            const auto n = std::min (blockSize, 4096-base); juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i)
            {
                random = random * 1664525u + 1013904223u;
                const auto x = (static_cast<float> (random >> 8) / 8388608.0f - 1.0f) * 0.15f;
                b.setSample (0, i, x); b.setSample (1, i, x * -0.35f);
            }
            p.processBlock (b, midi);
            for (int i = 0; i < n; ++i) { output.push_back (b.getSample (0, i)); output.push_back (b.getSample (1, i)); }
        }
        if (baseline.empty()) baseline = output;
        else
        {
            double error = 0;
            for (size_t i = 0; i < output.size(); ++i) error = std::max (error, static_cast<double> (std::abs (output[i] - baseline[i])));
            check (error < 1.0e-6, "processing is invariant to 1, 127 and 512 sample host blocks");
        }
    }
}
}
