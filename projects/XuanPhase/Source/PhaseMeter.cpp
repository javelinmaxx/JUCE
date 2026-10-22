#include "PhaseMeter.h"
using namespace stageStyle;
namespace
{
const auto early = juce::Colour (0xff72ce9c), late = juce::Colour (0xfff07880);
}
XuanPhaseMeter::XuanPhaseMeter (XuanPhaseProcessor& p) : processor (p)
{
    setTooltip ("Shows three seconds of input transients moving left to right, with early bands green, late bands red, and applied left phase in white and right phase in gold against degree guides.");
    lastColumn = lastInput = juce::Time::getMillisecondCounterHiRes();
}
XuanPhaseMeter::~XuanPhaseMeter() { stopTimer(); processor.setMeterVisible (false); }
void XuanPhaseMeter::visibilityChanged() { setActive (isShowing()); }
void XuanPhaseMeter::setActive (bool active)
{
    processor.setMeterVisible (active);
    if (active) { lastColumn = juce::Time::getMillisecondCounterHiRes(); startTimerHz (30); }
    else stopTimer();
}
juce::Colour XuanPhaseMeter::timingColour (float milliseconds, float energy) noexcept
{
    if (energy <= 0) return inset;
    const auto neutral = juce::Colour (0xffa59ba5);
    const auto colour = neutral.interpolatedWith (milliseconds < 0 ? early : late, std::clamp (std::abs (milliseconds)/.3f, 0.f, 1.f));
    return inset.interpolatedWith (colour, std::pow (std::clamp (energy, 0.f, 1.f), .85f));
}
void XuanPhaseMeter::append (const xuan::SpectrumFrame& frame)
{
    // Moving existing pixels right makes direction unambiguous without
    // resampling the spectrum or inventing activity between detector updates.
    history.moveImageSection (1, 0, 0, 0, columns-1, xuan::meterBins);
    juce::Image::BitmapData pixels (history, juce::Image::BitmapData::readWrite);
    for (int row = 0; row < xuan::meterBins; ++row)
        pixels.setPixelColour (0, xuan::meterBins-1-row, timingColour (frame[static_cast<size_t> (xuan::meterBins+row)], frame[static_cast<size_t> (row)]));
    std::move_backward (transient.begin(), transient.end()-1, transient.end());
    std::move_backward (peak.begin(), peak.end()-1, peak.end());
    transient[0] = frame[xuan::meterTransient];
    peak[0] = std::clamp ((juce::Decibels::gainToDecibels (frame[xuan::meterPeak], -60.f)+60)/60, 0.f, 1.f);
    columnsWritten = std::min (columns, columnsWritten+1);
}
void XuanPhaseMeter::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto oldCeiling = spectrum[xuan::meterCeiling];
    if (processor.readSpectrum (spectrum, spectrumVersion))
    {
        lastInput = now;
        if (oldCeiling > 20 && spectrum[xuan::meterCeiling] > 20
            && std::abs (oldCeiling-spectrum[xuan::meterCeiling]) > 1)
        {
            history.clear (history.getBounds()); transient.fill (0); peak.fill (0); columnsWritten = 0;
        }
    }
    processor.readRotation (rotation, rotationVersion);
    if (now-lastInput > 200) { const auto ceiling = spectrum[xuan::meterCeiling]; spectrum.fill (0); spectrum[xuan::meterCeiling] = ceiling; }
    constexpr double step = 1000.0/30;
    const auto advance = std::min (columns, static_cast<int> ((now-lastColumn)/step));
    if (advance > 0)
    {
        // Missed UI frames are blank, rather than made-up intermediate spectra.
        for (int i = 1; i < advance; ++i) append ({});
        append (spectrum); lastColumn = now-std::fmod (now-lastColumn, step); repaint();
    }
}
void XuanPhaseMeter::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    g.setColour (foreground); g.setFont (juce::FontOptions (13).withStyle ("bold"));
    g.drawText ("PHASE FIELD", 14, 12, 250, 20, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (9));
    g.setColour (early); g.drawText ("EARLY", 14, 34, 50, 16, juce::Justification::centredLeft);
    g.setColour (late); g.drawText ("LATE", 68, 34, 50, 16, juce::Justification::centredLeft);
    g.setColour (secondary); g.drawText ("INPUT / APPLIED PHASE", 114, 34, 155, 16, juce::Justification::centredRight);
    const juce::Rectangle<float> plot {38, 64, 168, 204}, phase {210, 64, 56, 204};
    g.setColour (inset); g.fillRect (plot); g.fillRect (phase);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (history, plot);
    const auto ceiling = spectrum[xuan::meterCeiling] > 20 ? spectrum[xuan::meterCeiling] : 20000.f;
    g.setFont (juce::FontOptions (9));
    for (float f : {20.f, 100.f, 1000.f, 10000.f})
    {
        if (f > ceiling) continue;
        const auto y = plot.getBottom()-static_cast<float> (std::log (f/20)/std::log (ceiling/20))*plot.getHeight();
        g.setColour (grid.withAlpha (.65f)); g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), phase.getRight());
        g.setColour (secondary); g.drawText (f >= 1000 ? juce::String (f/1000, 0)+"k" : juce::String (f, 0), 4, juce::roundToInt (y)-6, 30, 12, juce::Justification::centredRight);
    }
    g.setColour (secondary); g.drawText ("PHASE", 210, 48, 56, 14, juce::Justification::centred);
    const auto phaseX = [&] (float degrees) { return phase.getCentreX()+degrees/180*(phase.getWidth()*.5f-1); };
    // A four-row key keeps degree labels legible in the 56 px track; paired
    // positive/negative guides share a dash pattern and the same +/- label.
    const auto guide = [&] (juce::Line<float> line, int index)
    {
        if (index == 0) g.drawLine (line, .8f);
        else
        {
            const float dashes[] {static_cast<float> (index == 1 ? 1 : index == 2 ? 4 : 8), 3};
            g.drawDashedLine (line, dashes, 2, .7f);
        }
    };
    g.setFont (juce::FontOptions (9));
    for (int index = 0; index < 4; ++index)
    {
        const auto degrees = 45*index;
        g.setColour (secondary.withAlpha (index == 0 ? .65f : .35f));
        for (int sign : {-1, 1})
        {
            if (index == 0 && sign == -1) continue;
            const auto x = phaseX (static_cast<float> (sign*degrees));
            guide ({x, phase.getY(), x, phase.getBottom()}, index);
        }
        const auto y = 280.f+17*index;
        g.setColour (secondary); guide ({212, y+6, 224, y+6}, index);
        const auto text = (index == 0 ? juce::String ("0") : juce::String::fromUTF8 ("±")+juce::String (degrees))
                        +juce::String::fromUTF8 ("°");
        g.drawText (text, 227, juce::roundToInt (y), 39, 13, juce::Justification::centredLeft);
    }
    float maximum = 0;
    for (int channel : {1, 0})
    {
        juce::Path response;
        const auto offset = channel == 0 ? size_t (0) : size_t (2*xuan::meterBins+1);
        std::array<float, xuan::meterBins> xs {}, angles {};
        for (int row = 0; row < xuan::meterBins; ++row)
        {
            const auto index = static_cast<size_t> (row);
            angles[index] = std::atan2 (rotation[offset+xuan::meterBins+index], rotation[offset+index]);
            xs[index] = phaseX (angles[index]*180/juce::MathConstants<float>::pi);
            maximum = std::max (maximum, std::abs (angles[index])*180/juce::MathConstants<float>::pi);
        }
        const auto wrapped = [&] (int i) { return std::abs (angles[static_cast<size_t> (i+1)]-angles[static_cast<size_t> (i)]) > juce::MathConstants<float>::pi; };
        const auto tangent = [&] (int i)
        {
            if (i == 0) return wrapped (0) ? 0.f : xs[1]-xs[0];
            if (i == xuan::meterBins-1) return wrapped (i-1) ? 0.f : xs[static_cast<size_t> (i)]-xs[static_cast<size_t> (i-1)];
            if (wrapped (i-1) || wrapped (i)) return 0.f;
            const auto before = xs[static_cast<size_t> (i)]-xs[static_cast<size_t> (i-1)];
            const auto after = xs[static_cast<size_t> (i+1)]-xs[static_cast<size_t> (i)];
            return before*after > 0 ? 2*before*after/(before+after) : 0.f;
        };
        // Shape-preserving cubic interpolation rounds visual corners without
        // overshooting measured extrema or drawing across the +/-180 degree seam.
        const auto dy = phase.getHeight()/(xuan::meterBins-1);
        response.startNewSubPath (xs[0], phase.getBottom());
        for (int i = 0; i < xuan::meterBins-1; ++i)
        {
            const auto y = phase.getBottom()-i*dy, nextY = y-dy;
            if (wrapped (i)) response.startNewSubPath (xs[static_cast<size_t> (i+1)], nextY);
            else response.cubicTo (xs[static_cast<size_t> (i)]+tangent (i)/3, y-dy/3,
                                   xs[static_cast<size_t> (i+1)]-tangent (i+1)/3, nextY+dy/3,
                                   xs[static_cast<size_t> (i+1)], nextY);
        }
        g.setColour (channel == 0 ? foreground : amber); g.strokePath (response, juce::PathStrokeType (1.2f));
    }
    g.setColour (secondary); g.drawText ("NOW", 38, 271, 60, 15, juce::Justification::centredLeft);
    g.drawText ("3 s ago  >", 124, 271, 82, 15, juce::Justification::centredRight);
    g.drawText ("INPUT TRANSIENT", 14, 292, 192, 16, juce::Justification::centredLeft);
    const juce::Rectangle<float> envelope {38, 312, 168, 30};
    g.setColour (inset); g.fillRect (envelope);
    juce::Path trace;
    for (int col = 0; col < columns; ++col)
    {
        const auto x = envelope.getX()+static_cast<float> (col)/(columns-1)*envelope.getWidth();
        const auto strength = transient[static_cast<size_t> (col)];
        if (strength > .01f) { g.setColour (amber.withAlpha (.2f+.7f*strength)); g.drawVerticalLine (juce::roundToInt (x), envelope.getBottom()-strength*envelope.getHeight(), envelope.getBottom()); }
        const auto y = envelope.getBottom()-peak[static_cast<size_t> (col)]*envelope.getHeight();
        if (col == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
    }
    g.setColour (foreground.withAlpha (.65f)); g.strokePath (trace, juce::PathStrokeType (1));
    g.setColour (secondary); g.setFont (juce::FontOptions (9));
    g.drawText ("PHASE L:WHITE R:GOLD", 14, 351, 165, 16, juce::Justification::centredLeft);
    g.setColour (rotation[2*xuan::meterBins] >= .999f ? secondary : amber);
    g.drawText (rotation[2*xuan::meterBins] >= .999f ? "BYPASSED" : juce::String (maximum, 1)+" deg max", 177, 351, 89, 16, juce::Justification::centredRight);
}
