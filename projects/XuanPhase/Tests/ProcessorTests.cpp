#include "../Source/PluginEditor.h"
#include "../../StagePluginTests.h"
#include <complex>
#include <random>
using namespace stageTests;
namespace
{
using Complex = std::complex<double>;
std::vector<float> run (xuan::Settings s, const std::vector<float>& input, int block = 127, double rate = 48000, const xuan::Analysis& a = {})
{
    xuan::PhaseEngine engine; engine.prepare (rate, [s] { return s; }, false); engine.setOffline (s, a);
    std::vector<float> result;
    for (int offset = 0; offset < static_cast<int> (input.size()); offset += block)
    {
        const auto n = std::min (block, static_cast<int> (input.size())-offset);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { b.setSample (0, i, input[static_cast<size_t> (offset+i)]); b.setSample (1, i, -.5f*input[static_cast<size_t> (offset+i)]); }
        engine.process (b, s);
        for (int i = 0; i < n; ++i)
        {
            check (std::isfinite (b.getSample (0, i)) && std::abs (b.getSample (1, i)+.5f*b.getSample (0, i)) < 2e-6,
                   "linked phase rotation stays finite and preserves stereo relationships");
            result.push_back (b.getSample (0, i));
        }
    }
    return result;
}
Complex response (const std::vector<float>& ir, double hz, double rate)
{
    Complex h {};
    for (size_t i = 0; i < ir.size(); ++i) h += static_cast<double> (ir[i])*std::polar (1., -2*juce::MathConstants<double>::pi*hz*i/rate);
    return h*std::polar (1., 2*juce::MathConstants<double>::pi*hz*juce::roundToInt (rate*.025)/rate);
}
void dspTests (const juce::File& directory)
{
    directory.createDirectory();
    std::ofstream phase ((directory.getChildFile ("phase.csv")).getFullPathName().toStdString());
    phase << "setting,frequency,phase_deg,gain_db\n";
    for (double rate : {8000., 44100., 48000., 96000., 192000.})
    {
        XuanPhaseProcessor p; p.prepareToPlay (rate, 127);
        const auto latency = juce::roundToInt (rate*.025);
        check (p.getLatencySamples() == latency, "host latency is the nearest sample to 25 ms at every sample rate");
        std::vector<float> input (static_cast<size_t> (2*latency+2048)); input[0] = 1;
        for (bool bypass : {false, true})
        {
            xuan::Settings s; s.bypass = bypass;
            const auto audio = run (s, input, 127, rate);
            double error = 0;
            for (size_t i = 0; i < audio.size(); ++i) error = std::max (error, std::abs (static_cast<double> (audio[i])-(i == static_cast<size_t> (latency) ? 1 : 0)));
            check (error < 2e-6, "neutral phase and bypass both produce only the exact reported delay");
        }
    }
    for (float degrees : {-180.f, -90.f, 0.f, 90.f, 180.f})
    {
        xuan::Settings s; s.low = degrees; s.high = -degrees;
        const auto ir = xuan::PhaseEngine::impulse (48000, s);
        for (double hz : {20., 50., 200., 1000., 5000., 12000.})
        {
            const auto h = response (ir, hz, 48000);
            const auto error = std::remainder (std::arg (h)-xuan::PhaseEngine::phaseAt (hz, s, {}), 2*juce::MathConstants<double>::pi);
            check (std::abs (error) < .03, "measured low/high phase follows the designed rotation including 20 Hz extremes");
            phase << degrees << ',' << hz << ',' << std::arg (h)*180/juce::MathConstants<double>::pi << ',' << 20*std::log10 (std::abs (h)) << '\n';
        }
        std::vector<float> impulse (8000); impulse[0] = 1;
        const auto actual = run (s, impulse);
        double error = 0;
        for (size_t i = 0; i < actual.size(); ++i) error = std::max (error, std::abs (static_cast<double> (actual[i])-(i < ir.size() ? ir[i] : 0)));
        check (error < 2e-6, "hybrid convolution matches the designed impulse without adding a block of latency");
        const auto other = run (s, impulse, 1);
        check (other == actual, "phase processing is independent of host block boundaries");
    }
    xuan::Settings anchors; anchors.low = 90; anchors.high = -45;
    const auto anchored = xuan::PhaseEngine::impulse (48000, anchors);
    check (std::abs (std::arg (response (anchored, 20, 48000))*180/juce::MathConstants<double>::pi-90) < 1
           && std::abs (std::arg (response (anchored, 12000, 48000))*180/juce::MathConstants<double>::pi+45) < 1,
           "independent low and high controls produce their specified angles at low and high frequency anchors");
    check (std::abs (std::arg (response (anchored, 1000, 48000))*180/juce::MathConstants<double>::pi-22.5) < 1,
           "the manual crossover smoothly averages the two requested phase angles");
    // Staggered narrow-band wave packets simulate a dispersed percussive hit.
    std::vector<float> hit (2048);
    for (int i = 0; i < 2048; ++i)
        for (int band = 0; band < 4; ++band)
        {
            const auto hz = std::array<double, 4> {281.25, 843.75, 2437.5, 6750}[static_cast<size_t> (band)];
            const auto centre = 1050+band*55;
            const auto envelope = std::exp (-.5*std::pow ((i-centre)/70., 2));
            hit[static_cast<size_t> (i)] += static_cast<float> (.15*envelope*std::cos (2*juce::MathConstants<double>::pi*hz*(i-centre)/48000));
        }
    std::vector<float> previous; double energy = 0;
    const auto targets = xuan::PhaseEngine::analyse (hit, 48000, previous, energy);
    check (targets.count >= 3, "transient spectrogram phase produces multiple narrow-band alignment targets");
    for (int i = 0; i < targets.count; ++i)
        check (std::abs (targets.targets[static_cast<size_t> (i)].delay) <= .025, "automatic group-delay targets stay inside the lookahead budget");
    std::ofstream transient (directory.getChildFile ("transient.csv").getFullPathName().toStdString());
    transient << "sample,dry,depth0,depth50,depth100\n";
    std::array<std::vector<float>, 3> outputs;
    auto padded = hit; padded.resize (7000);
    for (int d = 0; d < 3; ++d)
    {
        xuan::Settings s; s.automatic = true; s.depth = static_cast<float> (50*d);
        outputs[static_cast<size_t> (d)] = run (s, padded, 127, 48000, targets);
    }
    float before = 0, after = 0;
    for (size_t i = 0; i < padded.size(); ++i)
    {
        before = std::max (before, std::abs (outputs[0][i])); after = std::max (after, std::abs (outputs[2][i]));
        transient << i << ',' << (i >= 1200 ? padded[i-1200] : 0) << ',' << outputs[0][i] << ',' << outputs[1][i] << ',' << outputs[2][i] << '\n';
    }
    std::cout << "Transient targets=" << targets.count << " peak before=" << before << " after=" << after << '\n';
    check (after > before*1.05f, "phase-guided alignment increases the synthetic dispersed hit peak at full depth");
    const auto neutral = run ({}, padded);
    check (neutral == outputs[0], "zero Auto depth leaves the manual processing sample-identical");
    std::vector<float> noise (2048); std::mt19937 random (18); std::normal_distribution<float> normal (0, .03f);
    int falseTriggers = 0; previous.clear(); energy = 0;
    for (int frame = 0; frame < 20; ++frame)
    {
        for (auto& value : noise) value = normal (random);
        const auto r = xuan::PhaseEngine::analyse (noise, 48000, previous, energy);
        if (frame > 1 && r.count > 0) ++falseTriggers;
    }
    check (falseTriggers == 0, "stationary noise does not cause repeated automatic phase rotations");
    XuanPhaseProcessor live; set (live, "automatic", 1); set (live, "depth", 100); live.prepareToPlay (48000, 128);
    std::vector<float> stream (50048);
    for (int at : {4096, 20480, 36864})
        for (int i = 0; i < 2048; ++i) stream[static_cast<size_t> (at+i)] = hit[static_cast<size_t> (i)];
    juce::MidiBuffer midi; juce::AudioBuffer<float> block (2, 128);
    double liveDifference = 0; bool linked = true;
    for (int at = 0; at < static_cast<int> (stream.size()); at += 128)
    {
        for (int i = 0; i < 128; ++i)
        { block.setSample (0, i, stream[static_cast<size_t> (at+i)]); block.setSample (1, i, -.5f*stream[static_cast<size_t> (at+i)]); }
        live.processBlock (block, midi); juce::Thread::sleep (3);
        for (int i = 0; i < 128; ++i)
        {
            const auto expected = at+i >= 1200 ? stream[static_cast<size_t> (at+i-1200)] : 0;
            liveDifference += std::abs (block.getSample (0, i)-expected);
            linked &= std::isfinite (block.getSample (0, i)) && std::abs (block.getSample (1, i)+.5f*block.getSample (0, i)) < 2e-6;
        }
    }
    check (live.targetCount() > 0 && liveDifference > .1 && linked,
           "live transient worker publishes audible phase responses while preserving linked stereo through kernel changes");
    set (live, "depth", 0);
    for (int i = 0; i < 32; ++i) { block.clear(); live.processBlock (block, midi); juce::Thread::sleep (2); }
    check (live.targetCount() == 0, "zero Depth clears automatic targets in the live worker");
    XuanPhaseProcessor bypassed; set (bypassed, "low", 90); set (bypassed, "output", 6); bypassed.prepareToPlay (48000, 128);
    double bypassError = 0;
    for (int at = 0; at < 4096; at += 128)
    {
        block.clear(); if (at == 0) block.setSample (0, 0, 1);
        bypassed.processBlockBypassed (block, midi);
        for (int i = 0; i < 128; ++i)
            bypassError = std::max (bypassError, std::abs (static_cast<double> (block.getSample (0, i))-(at+i == 1200 ? 1 : 0)));
    }
    check (bypassError < 1e-6, "host bypass ignores nonzero rotation and output gain while retaining exactly 25 ms latency");
}
void editor (const juce::File& path)
{
    XuanPhaseProcessor p; std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor()); e->setVisible (true);
    check (e->getWidth() == 548 && e->getHeight() == 490, "Xuan uses its intended scalable editor size");
    for (auto* child : e->getChildren())
    {
        if (dynamic_cast<juce::Slider*> (child) || dynamic_cast<juce::Button*> (child) || dynamic_cast<juce::Label*> (child))
        {
            auto* hint = dynamic_cast<juce::TooltipClient*> (child);
            const auto text = hint ? hint->getTooltip() : juce::String();
            check (text.isNotEmpty() && text.endsWithChar ('.') && text.dropLastCharacters (1).indexOfChar ('.') < 0,
                   "every control and caption has a one-sentence hover hint");
        }
        if (auto* button = dynamic_cast<juce::Button*> (child); button && button->getButtonText() == "AUTO")
        {
            button->setToggleState (true, juce::sendNotificationSync);
            check (p.settings().automatic, "Auto button enables its host parameter");
            set (p, "automatic", 0);
            check (! button->getToggleState(), "Auto button follows host automation");
        }
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            auto* client = dynamic_cast<juce::TooltipClient*> (slider);
            check (client && client->getTooltip().isNotEmpty(), "every phase knob has a hover hint");
            if (slider->getName() == "LOW PHASE")
            { slider->setValue (90, juce::sendNotificationSync); check (std::abs (p.settings().low-90) < .01f, "manual knob changes its host parameter"); }
        }
    }
    set (p, "low", 0);
    auto image = e->createComponentSnapshot (e->getLocalBounds()); juce::MemoryOutputStream png;
    path.getParentDirectory().createDirectory();
    check (juce::PNGImageFormat().writeImageToStream (image, png) && path.replaceWithData (png.getData(), png.getDataSize()), "native phase editor preview is saved");
    for (float scale : {.5f, 1.2f})
    {
        e->setSize (juce::roundToInt (548*scale), juce::roundToInt (490*scale));
        for (auto* c : e->getChildren())
            if (dynamic_cast<juce::Slider*> (c) || dynamic_cast<juce::Button*> (c))
                check (e->getLocalBounds().contains (e->getLocalArea (c, c->getLocalBounds())), "phase controls fit at both resize limits");
    }
}
}
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        stateTests<XuanPhaseProcessor> (7);
        dspTests (argc > 2 && juce::String (argv[1]) == "--report" ? juce::File (argv[2]) : juce::File ("/tmp/xuan-report"));
        if (argc > 2 && juce::String (argv[1]) == "--editor") editor (juce::File (argv[2]));
        std::cout << checks << " Xuan Phase checks passed\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
