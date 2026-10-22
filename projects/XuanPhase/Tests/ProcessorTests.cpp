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
void stereoTests (const juce::File& directory, const std::vector<float>& hit, const xuan::Analysis& detected)
{
    xuan::StereoAnalysis identical; identical.shared = detected; identical.channel = {detected, detected};
    const auto render = [] (xuan::Settings s, const xuan::StereoAnalysis& analysis,
                            const std::vector<float>& left, const std::vector<float>& right, int blockSize)
    {
        xuan::PhaseEngine engine; engine.prepare (48000, [s] { return s; }, false); engine.setStereoOffline (s, analysis);
        std::array<std::vector<float>, 2> out;
        for (size_t at = 0; at < left.size(); at += static_cast<size_t> (blockSize))
        {
            const auto n = std::min (blockSize, static_cast<int> (left.size()-at));
            juce::AudioBuffer<float> block (2, n);
            for (int i = 0; i < n; ++i) { block.setSample (0, i, left[at+static_cast<size_t> (i)]); block.setSample (1, i, right[at+static_cast<size_t> (i)]); }
            engine.process (block, s);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i) out[static_cast<size_t> (ch)].push_back (block.getSample (ch, i));
        }
        return out;
    };
    auto padded = hit; padded.resize (7000);
    std::ofstream audio (directory.getChildFile ("stereo-link.csv").getFullPathName().toStdString());
    audio << "link,sample,left,right\n";
    const auto peakAt = [] (const std::vector<float>& x)
    { return std::max_element (x.begin(), x.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); })-x.begin(); };
    for (float link : {-100.f, -50.f, 0.f, 50.f, 100.f})
    {
        xuan::Settings s; s.automatic = true; s.depth = 100; s.link = link;
        const auto out = render (s, identical, padded, padded, 127);
        const auto separation = std::abs (peakAt (out[1])-peakAt (out[0]));
        std::cout << "Stereo Link=" << link << " peak spacing=" << separation << " samples\n";
        if (link < 0)
        {
            double difference = 0;
            for (size_t i = 0; i < padded.size(); ++i) difference += std::abs (out[0][i]-out[1][i]);
            check (difference > .01, "negative Link retains opposed correction within the phase cap");
        }
        else check (out[0] == out[1], "identical inputs remain identical at zero and positive Stereo Link");
        for (size_t i = 0; i < padded.size(); ++i) audio << link << ',' << i << ',' << out[0][i] << ',' << out[1][i] << '\n';
        if (link < -99)
        {
            const auto otherBlock = render (s, identical, padded, padded, 1);
            check (out == otherBlock, "independent stereo filters remain block-size invariant");
            xuan::PhaseEngine measured; measured.prepare (48000, [s] { return s; }, false); measured.setStereoOffline (s, identical);
            xuan::RotationFrame frame {}; uint32_t version = 0; measured.readRotation (frame, version);
            double error = 0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto ir = xuan::PhaseEngine::stereoImpulse (48000, s, identical, ch);
                const auto offset = ch == 0 ? size_t (0) : size_t (2*xuan::meterBins+1);
                for (size_t row = 0; row < xuan::meterBins; ++row)
                    error = std::max (error, std::abs (Complex (frame[offset+row], frame[offset+xuan::meterBins+row])
                        -response (ir, xuan::PhaseEngine::meterFrequency (static_cast<int> (row), 48000), 48000)));
            }
            check (error < 1e-5, "both stereo phase traces reflect the corresponding applied FIR response");
        }
    }
    // Distinct channel packets must each derive their correction independently.
    auto right = hit;
    for (size_t i = 0; i < hit.size(); ++i) right[i] = i >= 80 ? hit[i-80] : 0;
    std::vector<float> previous; double energy = 0;
    const auto rightTargets = xuan::PhaseEngine::analyse (right, 48000, previous, energy);
    xuan::StereoAnalysis distinct = identical; distinct.channel[1] = rightTargets;
    xuan::Settings independent; independent.automatic = true; independent.depth = 100; independent.link = 0;
    right.resize (7000);
    const auto separate = render (independent, distinct, padded, right, 127);
    check (separate[0] == run (independent, padded, 127, 48000, detected)
           && separate[1] == run (independent, right, 127, 48000, rightTargets),
           "zero Stereo Link is identical to processing each channel's own phase targets separately");
    distinct.shared = rightTargets;
    check (render (independent, distinct, padded, right, 127) == separate,
           "a shared target cannot influence either channel at zero Stereo Link");
    independent.link = 100;
    check (xuan::PhaseEngine::stereoImpulse (48000, independent, distinct, 0)
           == xuan::PhaseEngine::stereoImpulse (48000, independent, distinct, 1),
           "full positive Stereo Link applies exactly the same response to both channels");
    independent.link = -100; distinct.stereo = false;
    check (xuan::PhaseEngine::stereoImpulse (48000, independent, distinct, 0)
           == xuan::PhaseEngine::impulse (48000, independent, detected),
           "mono layouts use the channel correction without artificial stereo separation");
    distinct.stereo = true;
    distinct.channel[0].anchor = .04; distinct.channel[1].anchor = .01;
    check (xuan::PhaseEngine::stereoPhaseAt (80, independent, distinct, 0) < xuan::PhaseEngine::phaseAt (80, independent, distinct.channel[0])-.5f
           && xuan::PhaseEngine::stereoPhaseAt (80, independent, distinct, 1) > xuan::PhaseEngine::phaseAt (80, independent, distinct.channel[1])+.5f,
           "negative Link pushes the later left anchor later and the earlier right anchor earlier");
    independent.depth = 0;
    check (xuan::PhaseEngine::stereoImpulse (48000, independent, distinct, 0)
           == xuan::PhaseEngine::impulse (48000, independent),
           "zero Auto Depth disables stereo separation as well as alignment");
    for (float link : {0.f, 100.f})
    {
        XuanPhaseProcessor isolated; set (isolated, "automatic", 1); set (isolated, "depth", 100);
        set (isolated, "transient", 40); set (isolated, "link", link); isolated.prepareToPlay (48000, 128); isolated.setMeterVisible (true);
        juce::AudioBuffer<float> block (2, 128); juce::MidiBuffer midi;
        float leftPhase = 0, rightPhase = 0; uint32_t version = 0; xuan::RotationFrame frame {};
        for (int at = 0; at < 36096; at += 128)
        {
            for (int i = 0; i < 128; ++i)
            {
                const auto index = at+i-22000;
                block.setSample (0, i, index >= 0 && index < static_cast<int> (hit.size()) ? hit[static_cast<size_t> (index)] : 0);
                block.setSample (1, i, static_cast<float> (.03*std::sin (2*juce::MathConstants<double>::pi*2000*(at+i)/48000)));
            }
            isolated.processBlock (block, midi); juce::Thread::sleep (3);
            if (isolated.readRotation (frame, version) && at > 25000)
            {
                for (size_t row = 0; row < xuan::meterBins; ++row)
                {
                    leftPhase = std::max (leftPhase, std::abs (std::atan2 (frame[xuan::meterBins+row], frame[row]))*180/juce::MathConstants<float>::pi);
                    const auto offset = 2*xuan::meterBins+1;
                    rightPhase = std::max (rightPhase, std::abs (std::atan2 (frame[offset+xuan::meterBins+row], frame[offset+row]))*180/juce::MathConstants<float>::pi);
                }
            }
        }
        std::cout << "Live Link=" << link << " transient L phase=" << leftPhase << " stationary R phase=" << rightPhase << '\n';
        if (link < 1) check (leftPhase > 5 && rightPhase < 1, "independent live analysis leaves a stationary right channel unrotated while correcting left transients");
        else check (leftPhase > 5 && std::abs (leftPhase-rightPhase) < .01f, "shared live correction rotates both channels equally even when only left has transients");
    }
    // Exercise the actual background worker on repeated mono hits.
    XuanPhaseProcessor live; set (live, "automatic", 1); set (live, "depth", 100); set (live, "link", -100); live.prepareToPlay (48000, 128);
    juce::AudioBuffer<float> block (2, 128); juce::MidiBuffer midi;
    std::array<std::vector<float>, 2> liveAudio;
    std::ofstream liveData (directory.getChildFile ("stereo-link-live.csv").getFullPathName().toStdString());
    liveData << "sample,left,right\n";
    for (int at = 0; at < 49152; at += 128)
    {
        for (int i = 0; i < 128; ++i)
        {
            const auto index = (at+i)%16384-4096;
            const auto x = index >= 0 && index < static_cast<int> (hit.size()) ? hit[static_cast<size_t> (index)] : 0;
            block.setSample (0, i, x); block.setSample (1, i, x);
        }
        live.processBlock (block, midi); juce::Thread::sleep (3);
        for (int i = 0; i < 128; ++i)
        {
            for (int ch = 0; ch < 2; ++ch) liveAudio[static_cast<size_t> (ch)].push_back (block.getSample (ch, i));
            liveData << at+i << ',' << block.getSample (0, i) << ',' << block.getSample (1, i) << '\n';
        }
    }
    std::array<std::vector<float>, 2> last;
    for (int ch = 0; ch < 2; ++ch) last[static_cast<size_t> (ch)] = {liveAudio[static_cast<size_t> (ch)].begin()+38000, liveAudio[static_cast<size_t> (ch)].begin()+44000};
    double liveSide = 0;
    for (size_t i = 0; i < last[0].size(); ++i) liveSide += std::abs (last[0][i]-last[1][i]);
    check (live.targetCount() > 0 && liveSide > .01,
           "live negative Link applies opposed channel correction within the phase cap");
}

void phaseCapTests (const juce::File& directory)
{
    xuan::StereoAnalysis a; a.shared.count = 1;
    a.shared.targets[0] = {1000, 10000, 2.8f, .025f}; a.channel = {a.shared, a.shared};
    std::ofstream cap (directory.getChildFile ("phase-cap.csv").getFullPathName().toStdString());
    cap << "frequency,link,left_deg,right_deg\n";
    constexpr auto pi = juce::MathConstants<double>::pi;
    for (float manual : {-180.f, 0.f, 180.f})
        for (float depth : {-100.f, 0.f, 100.f})
            for (float link : {-100.f, -50.f, 0.f, 50.f, 100.f})
            {
                xuan::Settings s; s.automatic = true; s.low = s.high = manual; s.depth = depth; s.link = link;
                for (double f = 20; f <= 20000; f += 37)
                {
                    check (std::abs (xuan::PhaseEngine::phaseAt (f, s, a.shared)) <= pi,
                           "combined manual and automatic phase saturates at +/-180 degrees");
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const auto value = xuan::PhaseEngine::stereoPhaseAt (f, s, a, ch);
                        check (std::isfinite (value) && std::abs (value) <= pi,
                               "all stereo correction settings stay within the DSP phase cap before FIR design");
                    }
                }
            }
    xuan::Settings s; s.automatic = true; s.depth = 100; s.link = -100;
    for (double f = 20; f <= 20000; f += 10)
        cap << f << ',' << s.link << ',' << xuan::PhaseEngine::stereoPhaseAt (f, s, a, 0)*180/pi
            << ',' << xuan::PhaseEngine::stereoPhaseAt (f, s, a, 1)*180/pi << '\n';
    s.low = s.high = 180; s.link = 100;
    const auto saturated = xuan::PhaseEngine::phaseAt (1000, s, a.shared);
    check (saturated > 3.14f && std::atan2 (std::sin (saturated), std::cos (saturated)) > 3.14f,
           "over-range positive correction stops at +180 rather than wrapping negative");
    s.low = s.high = -180; s.depth = -100;
    const auto negative = xuan::PhaseEngine::phaseAt (1000, s, a.shared);
    check (negative < -3.14f && std::atan2 (std::sin (negative), std::cos (negative)) < -3.14f,
           "over-range negative correction stops at -180 rather than wrapping positive");
}
void phaseDisplayTests()
{
    const juce::Rectangle<float> track {210, 64, 56, 204};
    xuan::RotationFrame frame {};
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto offset = ch == 0 ? size_t (0) : size_t (2*xuan::meterBins+1);
        for (int row = 0; row < xuan::meterBins; ++row)
        {
            const auto degrees = std::array<float, 16> {0, 45, -45, 90, -90, 135, -135, 179, -179, 180, -180, 200, -200, 540, -540, 900}[static_cast<size_t> (row%16)]*(ch == 0 ? 1.f : -1.f);
            const auto expected = std::polar (1.f, degrees*juce::MathConstants<float>::pi/180);
            frame[offset+static_cast<size_t> (row)] = expected.real();
            frame[offset+xuan::meterBins+static_cast<size_t> (row)] = expected.imag();
            const auto shown = XuanPhaseMeter::rotationDegrees (frame, ch, row);
            check (std::isfinite (shown) && std::abs (shown) <= 180,
                   "phase readout stays within +/-180 degrees even for multi-turn rotations");
            check (std::abs (std::polar (1.f, shown*juce::MathConstants<float>::pi/180)-expected) < 2e-5,
                   "display wraps multi-turn phase rather than clamping the underlying rotation");
        }
        const auto path = XuanPhaseMeter::rotationPath (frame, ch, track);
        juce::PathFlatteningIterator lines (path, juce::AffineTransform(), .001f);
        int segments = 0;
        while (lines.next())
        {
            ++segments;
            check (lines.x1 >= track.getX() && lines.x1 <= track.getRight()
                   && lines.x2 >= track.getX() && lines.x2 <= track.getRight(),
                   "smoothed phase curve never overshoots the +/-180 degree track edges");
        }
        check (segments > 0, "bound checks cover rendered cubic segments");
        for (int row = 0; row < xuan::meterBins; ++row)
        {
            const auto value = std::polar (1.f, (row%2 == 0 ? 179.f : -179.f)*juce::MathConstants<float>::pi/180);
            frame[offset+static_cast<size_t> (row)] = value.real(); frame[offset+xuan::meterBins+static_cast<size_t> (row)] = value.imag();
        }
        const auto seam = XuanPhaseMeter::rotationPath (frame, ch, track);
        juce::PathFlatteningIterator seams (seam);
        check (! seams.next(), "phase wrap crossings split the path instead of drawing across the display");
    }
}
void dspTests (const juce::File& directory)
{
    directory.createDirectory();
    phaseDisplayTests();
    phaseCapTests (directory);
    XuanPhaseProcessor saved, restored;
    set (saved, "link", -70);
    set (saved, "depth", -75); set (saved, "transient", 700);
    juce::MemoryBlock state; saved.getStateInformation (state); restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (std::abs (restored.settings().depth+75) < .001f && std::abs (restored.settings().transient-700) < .001f && std::abs (restored.settings().link+70) < .001f,
           "negative Depth and Transient release survive session recall");
    set (saved, "depth", 50);
    auto oldOutput = saved.parameters.copyState();
    for (int i = oldOutput.getNumChildren()-1; i >= 0; --i)
        if (oldOutput.getChild (i).getProperty ("id").toString() == "link") oldOutput.removeChild (i, nullptr);
    juce::ValueTree gain ("PARAM"); gain.setProperty ("id", "output", nullptr); gain.setProperty ("value", 12.f, nullptr); oldOutput.addChild (gain, -1, nullptr);
    juce::MemoryBlock oldData; juce::AudioProcessor::copyXmlToBinary (*oldOutput.createXml(), oldData);
    restored.setStateInformation (oldData.getData(), static_cast<int> (oldData.getSize()));
    check (std::abs (restored.settings().link-100) < .001f && restored.parameters.getParameter ("output") == nullptr,
           "older Output states migrate to shared Stereo Link without treating gain as a link value");
    auto oldState = saved.parameters.copyState();
    for (int i = oldState.getNumChildren()-1; i >= 0; --i)
        if (oldState.getChild (i).getProperty ("id").toString() == "transient") oldState.removeChild (i, nullptr);
    XuanPhaseProcessor legacy;
    legacy.parameters.replaceState (oldState);
    check (std::abs (legacy.settings().depth-50) < .001f && std::abs (legacy.settings().transient-200) < .001f,
           "sessions predating Transient retain their stored positive Depth and the new default release");
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
    xuan::PhaseEngine metered; metered.prepare (48000, [anchors] { return anchors; }, false);
    xuan::RotationFrame measured {}; uint32_t measuredVersion = 0;
    check (metered.readRotation (measured, measuredVersion), "applied-phase meter has an initial measured response");
    double meterError = 0;
    for (int row = 0; row < xuan::meterBins; ++row)
    {
        const auto actual = Complex (measured[static_cast<size_t> (row)], measured[static_cast<size_t> (xuan::meterBins+row)]);
        meterError = std::max (meterError, std::abs (actual-response (anchored, xuan::PhaseEngine::meterFrequency (row, 48000), 48000)));
    }
    check (meterError < 1e-5, "meter reports measured FIR phase and magnitude rather than requested knob angles");
    XuanPhaseProcessor applied; applied.prepareToPlay (48000, 128); applied.setMeterVisible (true);
    xuan::RotationFrame rotation {}; uint32_t rotationVersion = 0; applied.readRotation (rotation, rotationVersion);
    set (applied, "low", 90); juce::Thread::sleep (60);
    check (! applied.readRotation (rotation, rotationVersion), "a queued response does not appear as applied phase before audio picks it up");
    juce::AudioBuffer<float> silent (2, 128); juce::MidiBuffer emptyMidi;
    for (int block = 0; block < 8; ++block) { silent.clear(); applied.processBlock (silent, emptyMidi); }
    check (applied.readRotation (rotation, rotationVersion)
           && std::abs (std::atan2 (rotation[xuan::meterBins], rotation[0])*180/juce::MathConstants<double>::pi-90) < 1,
           "audio handoff and fade update the applied-phase display");
    silent.clear(); applied.processBlockBypassed (silent, emptyMidi);
    check (applied.readRotation (rotation, rotationVersion) && rotation[2*xuan::meterBins] > .999f
           && std::abs (rotation[xuan::meterBins]) < 1e-6 && std::abs (rotation[0]-1) < 1e-6,
           "host bypass displays zero applied rotation");
    // Widely separated equal-angle peaks must not fall back to zero between
    // them, and opposite wrapped angles must connect through +/-180 degrees.
    xuan::Analysis joined; joined.count = 2;
    joined.targets[0] = {500, 25, .6f, .001f};
    joined.targets[1] = {2000, 50, .6f, .001f};
    check (std::abs (xuan::PhaseEngine::bandValue (1250, joined, true)-.001) < 1e-8,
           "timing colours stay connected across gaps between detected peaks");
    joined.targets[0].delay = joined.targets[1].delay = 0;
    check (std::abs (xuan::PhaseEngine::bandValue (1250, joined, false)-.6) < 1e-6,
           "equal phase targets remain connected instead of returning to neutral between peaks");
    joined.targets[0].angle = static_cast<float> (179*juce::MathConstants<double>::pi/180);
    joined.targets[1].angle = -joined.targets[0].angle;
    check (std::abs (xuan::PhaseEngine::bandValue (1250, joined, false)-juce::MathConstants<double>::pi) < 1e-6,
           "adjacent phase targets unwrap across the +/-180 degree seam");
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
    // More than 24 distinct low/mid peaks must not consume every target before
    // the high-frequency part of a broadband transient is inspected.
    std::vector<float> broadHit (2048);
    for (int i = 0; i < 2048; ++i)
    {
        const auto t = i-1050;
        for (int bin = 4; bin <= 830; bin += 13)
            broadHit[static_cast<size_t> (i)] += static_cast<float> (.01*std::exp (-.5*std::pow (t/100., 2))
                *std::cos (2*juce::MathConstants<double>::pi*bin*t/2048));
    }
    std::vector<float> broadPrevious; double broadEnergy = 0;
    const auto broad = xuan::PhaseEngine::analyse (broadHit, 48000, broadPrevious, broadEnergy);
    check (broad.count == 24 && broad.targets[static_cast<size_t> (broad.count-1)].frequency > 16000
           && broad.targets[0].frequency < 1000,
           "24 automatic bands cover low and high frequencies instead of filling exclusively from low peaks");
    std::vector<float> previous; double energy = 0;
    const auto targets = xuan::PhaseEngine::analyse (hit, 48000, previous, energy);
    check (targets.count >= 3, "transient spectrogram phase produces multiple narrow-band alignment targets");
    for (int i = 0; i < targets.count; ++i)
        check (std::abs (targets.targets[static_cast<size_t> (i)].delay) <= .025, "automatic group-delay targets stay inside the lookahead budget");
    xuan::Settings full; full.automatic = true; full.depth = 100;
    const auto autoIr = xuan::PhaseEngine::impulse (48000, full, targets);
    stereoTests (directory, hit, targets);
    std::ofstream curve (directory.getChildFile ("auto-phase.csv").getFullPathName().toStdString());
    curve << "frequency,target_deg,measured_deg,gain_db\n";
    for (double f = 60; f <= 12000; f += 5)
    {
        const auto h = response (autoIr, f, 48000);
        curve << f << ',' << xuan::PhaseEngine::phaseAt (f, full, targets)*180/juce::MathConstants<double>::pi
              << ',' << std::arg (h)*180/juce::MathConstants<double>::pi << ',' << 20*std::log10 (std::abs (h)) << '\n';
    }
    for (int t = 1; t < targets.count; ++t)
    {
        const auto f = targets.targets[static_cast<size_t> (t)].frequency;
        check (std::abs (xuan::PhaseEngine::phaseAt (f-.001, full, targets)-xuan::PhaseEngine::phaseAt (f+.001, full, targets)) < .001,
               "detected automatic phase bands join continuously at each target");
    }
    std::ofstream transient (directory.getChildFile ("transient.csv").getFullPathName().toStdString());
    transient << "sample,dry,depth0,depth50,depth100,depthminus100\n";
    std::array<std::vector<float>, 3> outputs;
    auto padded = hit; padded.resize (7000);
    for (int d = 0; d < 3; ++d)
    {
        xuan::Settings s; s.automatic = true; s.depth = static_cast<float> (50*d);
        outputs[static_cast<size_t> (d)] = run (s, padded, 127, 48000, targets);
    }
    xuan::Settings reversed = full; reversed.depth = -100;
    const auto negative = run (reversed, padded, 127, 48000, targets);
    const auto reversedIr = xuan::PhaseEngine::impulse (48000, reversed, targets);
    std::ofstream targetData (directory.getChildFile ("auto-targets.csv").getFullPathName().toStdString());
    targetData << "frequency,target_deg,positive_deg,negative_deg\n";
    for (int t = 0; t < targets.count; ++t)
    {
        const auto& target = targets.targets[static_cast<size_t> (t)];
        const auto positive = response (autoIr, target.frequency, 48000);
        const auto inverse = response (reversedIr, target.frequency, 48000);
        const auto scale = 180/juce::MathConstants<double>::pi;
        targetData << target.frequency << ',' << target.angle*scale << ',' << std::arg (positive)*scale << ',' << std::arg (inverse)*scale << '\n';
        check (std::abs (std::remainder (std::arg (positive)-target.angle, 2*juce::MathConstants<double>::pi)) < juce::MathConstants<double>::pi/180,
               "full Depth aligns each detected peak within one degree of the transient anchor");
        check (std::abs (inverse-std::conj (positive)) < 1e-5,
               "negative Depth reverses the measured automatic phase rotation");
    }
    check (std::abs (xuan::PhaseEngine::releaseStrength (1, 9600, 48000, 200)-std::exp (-1.f)) < 1e-6,
           "Transient release has the specified exponential time constant in audio samples");
    check (xuan::PhaseEngine::releaseStrength (1, 9600, 48000, 40) < xuan::PhaseEngine::releaseStrength (1, 9600, 48000, 800),
           "shorter Transient values release phase correction faster");
    float before = 0, after = 0;
    for (size_t i = 0; i < padded.size(); ++i)
    {
        before = std::max (before, std::abs (outputs[0][i])); after = std::max (after, std::abs (outputs[2][i]));
        transient << i << ',' << (i >= 1200 ? padded[i-1200] : 0) << ',' << outputs[0][i] << ',' << outputs[1][i] << ',' << outputs[2][i] << ',' << negative[i] << '\n';
    }
    std::cout << "Transient targets=" << targets.count << " peak before=" << before << " after=" << after << '\n';
    check (after > before*1.05f, "phase-guided alignment increases the synthetic dispersed hit peak at full depth");
    float negativePeak = 0;
    for (auto x : negative) negativePeak = std::max (negativePeak, std::abs (x));
    check (negativePeak < before*.95f, "negative Depth spreads the constructed transient instead of aligning its peak");
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
    std::ofstream liveAudio (directory.getChildFile ("auto-live.csv").getFullPathName().toStdString());
    liveAudio << "sample,reference,automatic\n";
    double liveDifference = 0, settledInputEnergy = 0, settledOutputEnergy = 0;
    float settledInputPeak = 0, settledOutputPeak = 0; bool linked = true;
    for (int at = 0; at < static_cast<int> (stream.size()); at += 128)
    {
        for (int i = 0; i < 128; ++i)
        { block.setSample (0, i, stream[static_cast<size_t> (at+i)]); block.setSample (1, i, -.5f*stream[static_cast<size_t> (at+i)]); }
        live.processBlock (block, midi); juce::Thread::sleep (3);
        for (int i = 0; i < 128; ++i)
        {
            const auto expected = at+i >= 1200 ? stream[static_cast<size_t> (at+i-1200)] : 0;
            liveAudio << at+i << ',' << expected << ',' << block.getSample (0, i) << '\n';
            liveDifference += std::abs (block.getSample (0, i)-expected);
            if (at+i >= 36864+1200 && at+i < 36864+1200+4096)
            {
                const auto out = block.getSample (0, i);
                settledInputPeak = std::max (settledInputPeak, std::abs (expected));
                settledOutputPeak = std::max (settledOutputPeak, std::abs (out));
                settledInputEnergy += expected*expected; settledOutputEnergy += out*out;
            }
            linked &= std::isfinite (block.getSample (0, i)) && std::abs (block.getSample (1, i)+.5f*block.getSample (0, i)) < 2e-6;
        }
    }
    check (live.targetCount() > 0 && liveDifference > .1 && linked,
           "live transient worker publishes audible phase responses while preserving linked stereo through kernel changes");
    check (settledOutputPeak > settledInputPeak*1.05f,
           "live Auto increases the settled dispersed transient peak without a preloaded target");
    check (std::abs (std::sqrt (settledOutputEnergy/settledInputEnergy)-1) < .02,
           "live Auto punch improvement is phase alignment rather than a substantial RMS boost");
    set (live, "depth", 0);
    for (int i = 0; i < 32; ++i) { block.clear(); live.processBlock (block, midi); juce::Thread::sleep (2); }
    check (live.targetCount() == 0, "zero Depth clears automatic targets in the live worker");
    std::ofstream releaseData (directory.getChildFile ("auto-release.csv").getFullPathName().toStdString());
    releaseData << "sample,release_ms,rotation_deg\n";
    std::array<float, 3> remaining {};
    for (int trial = 0; trial < 3; ++trial)
    {
        const auto milliseconds = std::array<float, 3> {40, 200, 800}[static_cast<size_t> (trial)];
        XuanPhaseProcessor releasing; set (releasing, "automatic", 1); set (releasing, "depth", -100);
        set (releasing, "transient", milliseconds); releasing.prepareToPlay (48000, 128); releasing.setMeterVisible (true);
        xuan::RotationFrame frame {}; uint32_t version = 0; float maxDuring = 0;
        for (int at = 0; at < 60032; at += 128)
        {
            for (int i = 0; i < 128; ++i)
            {
                const auto index = at+i-4096;
                const auto x = index >= 0 && index < static_cast<int> (hit.size()) ? hit[static_cast<size_t> (index)] : 0;
                block.setSample (0, i, x); block.setSample (1, i, x);
            }
            releasing.processBlock (block, midi); juce::Thread::sleep (3);
            if (releasing.readRotation (frame, version))
            {
                float maximum = 0;
                for (int row = 0; row < xuan::meterBins; ++row)
                    maximum = std::max (maximum, std::abs (std::atan2 (frame[xuan::meterBins+static_cast<size_t> (row)], frame[static_cast<size_t> (row)]))*180/juce::MathConstants<float>::pi);
                releaseData << at+128 << ',' << milliseconds << ',' << maximum << '\n';
                maxDuring = std::max (maxDuring, maximum); remaining[static_cast<size_t> (trial)] = maximum;
            }
        }
        check (maxDuring > 10, "negative Depth activates live transient analysis and applies a reversed response");
    }
    check (remaining[0] < 1 && remaining[2] > remaining[1]+1,
           "the live worker releases short Transient settings faster than long settings");
    XuanPhaseProcessor bypassed; set (bypassed, "low", 90); set (bypassed, "link", -100); bypassed.prepareToPlay (48000, 128);
    double bypassError = 0;
    for (int at = 0; at < 4096; at += 128)
    {
        block.clear(); if (at == 0) block.setSample (0, 0, 1);
        bypassed.processBlockBypassed (block, midi);
        for (int i = 0; i < 128; ++i)
            bypassError = std::max (bypassError, std::abs (static_cast<double> (block.getSample (0, i))-(at+i == 1200 ? 1 : 0)));
    }
    check (bypassError < 1e-6, "host bypass ignores nonzero rotation and negative Stereo Link while retaining exactly 25 ms latency");
}
void editor (const juce::File& path)
{
    XuanPhaseProcessor p; std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor()); e->addToDesktop (0); e->setVisible (true);
    check (e->getWidth() == 840 && e->getHeight() == 490, "Xuan includes its vertical side meter at the intended scalable size");
    XuanPhaseMeter* meter = nullptr;
    for (auto* child : e->getChildren())
    {
        if (auto* field = dynamic_cast<XuanPhaseMeter*> (child)) meter = field;
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
            if (slider->getName() == "TRANSIENT")
            { slider->setValue (400, juce::sendNotificationSync); check (std::abs (p.settings().transient-400) < .01f, "Transient knob changes its host parameter"); }
            if (slider->getName() == "STEREO LINK")
            {
                check (slider->getX() == 375, "Stereo Link occupies the far-right control position");
                slider->setValue (-50, juce::sendNotificationSync);
                check (std::abs (p.settings().link+50) < .01f, "Stereo Link knob controls its signed host parameter");
                slider->setValue (100, juce::sendNotificationSync);
            }
            if (slider->getName() == "LOW PHASE")
            { slider->setValue (90, juce::sendNotificationSync); check (std::abs (p.settings().low-90) < .01f, "manual knob changes its host parameter"); }
        }
    }
    set (p, "low", 0);
    check (meter != nullptr && meter->getX() >= 524 && meter->getHeight() > meter->getWidth(), "vertical phase meter sits beside the original controls");
    check (meter->getTooltip().isNotEmpty(), "phase meter has a hover hint explaining colours and scroll direction");
    const auto green = XuanPhaseMeter::timingColour (-1, 1), red = XuanPhaseMeter::timingColour (1, 1);
    check (green.getGreen() > green.getRed() && red.getRed() > red.getGreen(), "early bands use green and late bands use red");
    p.prepareToPlay (48000, 128); p.setMeterVisible (true);
    set (p, "automatic", 1); set (p, "depth", 80); set (p, "link", -60); set (p, "low", 25); set (p, "high", -20);
    juce::AudioBuffer<float> audio (2, 128); juce::MidiBuffer midi;
    bool earlySeen = false, lateSeen = false, onsetSeen = false, movedRight = false;
    xuan::SpectrumFrame source {}; uint32_t sourceVersion = 0;
    std::array<juce::Colour, xuan::meterBins> previousColumn {};
    int previousColumns = 0;
    for (int at = 0; at < 144000; at += 128)
    {
        for (int i = 0; i < 128; ++i)
        {
            const auto t = (at+i)%12000; double x = 0;
            for (int band = 0; band < 4; ++band)
            {
                const auto centre = 1050+55*band;
                const auto hz = std::array<double, 4> {281.25, 843.75, 2437.5, 6750}[static_cast<size_t> (band)];
                x += .3*std::exp (-.5*std::pow ((t-centre)/70., 2))*std::cos (2*juce::MathConstants<double>::pi*hz*(t-centre)/48000);
            }
            audio.setSample (0, i, static_cast<float> (x)); audio.setSample (1, i, static_cast<float> (x*.5));
        }
        p.processBlock (audio, midi); juce::Thread::sleep (3); juce::Timer::callPendingTimersSynchronously();
        if (p.readSpectrum (source, sourceVersion))
        {
            onsetSeen |= source[xuan::meterTransient] > .1f;
            for (int row = 0; row < xuan::meterBins; ++row)
            { earlySeen |= source[static_cast<size_t> (xuan::meterBins+row)] < -.02f; lateSeen |= source[static_cast<size_t> (xuan::meterBins+row)] > .02f; }
        }
        const auto written = meter->historySize();
        if (written == previousColumns+1 && previousColumns > 0)
        {
            bool same = true, activity = false;
            for (int row = 0; row < xuan::meterBins; ++row)
            {
                same &= meter->historyImage().getPixelAt (1, row) == previousColumn[static_cast<size_t> (row)];
                activity |= previousColumn[static_cast<size_t> (row)].getBrightness() > .2f;
            }
            movedRight |= same && activity;
        }
        previousColumns = written;
        for (int row = 0; row < xuan::meterBins; ++row) previousColumn[static_cast<size_t> (row)] = meter->historyImage().getPixelAt (0, row);
    }
    check (onsetSeen && earlySeen && lateSeen, "live transient spectra contain both early and lagging timing indications");
    check (meter->historySize() > 30 && movedRight, "new spectra enter on the left and existing columns move right");
    auto image = e->createComponentSnapshot (e->getLocalBounds()); juce::MemoryOutputStream png;
    path.getParentDirectory().createDirectory();
    check (juce::PNGImageFormat().writeImageToStream (image, png) && path.replaceWithData (png.getData(), png.getDataSize()), "native phase editor preview is saved");
    set (p, "automatic", 0); bool manualSpectrum = false;
    for (int at = 0; at < 12032; at += 128)
    {
        for (int i = 0; i < 128; ++i)
        {
            const auto t = at+i-4000;
            const auto x = static_cast<float> (.5*std::exp (-.5*std::pow (t/100., 2))*std::cos (2*juce::MathConstants<double>::pi*900*t/48000));
            audio.setSample (0, i, x); audio.setSample (1, i, x);
        }
        p.processBlock (audio, midi); juce::Thread::sleep (3);
        if (p.readSpectrum (source, sourceVersion)) manualSpectrum |= source[xuan::meterTransient] > .1f;
    }
    check (! p.settings().automatic && manualSpectrum, "input transient meter remains live in manual mode");
    e->setVisible (false); juce::Thread::sleep (15); p.readSpectrum (source, sourceVersion);
    for (int i = 0; i < 32; ++i) { audio.clear(); p.processBlock (audio, midi); juce::Thread::sleep (2); }
    check (! p.readSpectrum (source, sourceVersion), "hidden manual-mode editor stops display-only FFT publication");
    e->setVisible (true);
    for (float scale : {.5f, 1.2f})
    {
        e->setSize (juce::roundToInt (840*scale), juce::roundToInt (490*scale));
        check (e->getLocalBounds().contains (e->getLocalArea (meter, meter->getLocalBounds())), "side meter remains visible at both resize limits");
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
        stateTests<XuanPhaseProcessor> (8);
        dspTests (argc > 2 && juce::String (argv[1]) == "--report" ? juce::File (argv[2]) : juce::File ("/tmp/xuan-report"));
        if (argc > 2 && juce::String (argv[1]) == "--editor") editor (juce::File (argv[2]));
        std::cout << checks << " Xuan Phase checks passed\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
