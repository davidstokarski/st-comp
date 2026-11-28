#include "Display.h"

namespace stcomp
{

namespace
{
    juce::Colour inputFill()  { return st::Theme::get().textFaint.withAlpha (0.22f); }
    juce::Colour outputFill() { return st::Theme::get().textDim.withAlpha (0.22f); }
    juce::Colour outputLine() { return st::Theme::get().text.withAlpha (0.62f); }

    void dashedLine (juce::Graphics& g, juce::Line<float> l, juce::Colour c, float thickness = 1.0f)
    {
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (c);
        g.drawDashedLine (l, dashes, 2, thickness);
    }

    void setParam (StCompProcessor& p, const juce::String& id, float plain)
    {
        auto& prm = p.param (id);
        prm.setValueNotifyingHost (prm.convertTo0to1 (plain));
    }

    void gesture (StCompProcessor& p, const juce::String& id, bool begin)
    {
        if (begin) p.param (id).beginChangeGesture(); else p.param (id).endChangeGesture();
    }

    float raw (const StCompProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

    const juce::String dot (juce::CharPointer_UTF8 ("\xc2\xb7  "));
    const juce::String downArrow (juce::CharPointer_UTF8 (" \xe2\x96\xbe"));
}

//==============================================================================
LevelDisplay::LevelDisplay (StCompProcessor& p) : proc (p), reader (p.getEngine().history, 4096, 260.0f)
{
    setTooltip ("Level history: input (dark), output (light) and gain reduction (red; upward gain in blue). Drag the threshold line.");
}

void LevelDisplay::tick (double dt)
{
    reader.update();
    const double before = scroll.getPosition();
    const double pos = scroll.update (dt, reader.end(), reader.getFrameRate());
    if (std::abs (pos - before) > 1.0e-3 || dragging) repaint();
}

juce::Rectangle<float> LevelDisplay::getPlot() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (16.0f).withTrimmedRight (46.0f).reduced (0.0f, 10.0f);
}

float LevelDisplay::dbToY (float db) const
{
    const auto r = getPlot();
    return juce::jmap (juce::jlimit (-rangeDb, 0.0f, db), -rangeDb, 0.0f, r.getBottom(), r.getY());
}

juce::Rectangle<float> LevelDisplay::handleBounds() const
{
    const auto r = getPlot();
    return juce::Rectangle<float> (36.0f, 14.0f).withCentre ({ r.getRight() - 30.0f, dbToY (raw (proc, ids::threshold)) });
}

bool LevelDisplay::overThreshold (juce::Point<float> p) const
{
    if (handleBounds().expanded (6.0f, 5.0f).contains (p)) return true;
    const auto r = getPlot();
    return r.contains (p) && std::abs (p.y - dbToY (raw (proc, ids::threshold))) < 5.0f && ! chip.expanded (4.0f).contains (p);
}

void LevelDisplay::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const auto r = getPlot();

    // ---- dB grid (faded at both ends) + labels on the right
    const float step = rangeDb <= 30.0f ? 3.0f : (rangeDb <= 50.0f ? 6.0f : 12.0f);
    g.setFont (st::fonts::regular (10.0f));
    for (float db = -step; db > -rangeDb + 0.1f; db -= step)
    {
        const float y = dbToY (db);
        st::paint::fadedLine (g, { r.getX(), y }, { r.getRight(), y }, juce::Colours::white.withAlpha (0.045f), 1.0f, 0.08f);
        g.setColour (t.textFaint.withAlpha (0.85f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getRight() + 4.0f, y - 7.0f, 34.0f, 14.0f), juce::Justification::centredRight, false);
    }

    // ---- data: newest at the right, gliding with the display clock
    const double fpp = juce::jmax (0.05, (double) seconds * reader.getFrameRate() / (double) juce::jmax (1.0f, r.getWidth()));
    const float bottom = r.getBottom(), top = r.getY();
    st::HistoryPaths paths;
    paths.build (reader, scroll.getPosition(), r.getX() - 4.0f, r.getRight(), fpp,
                 [this] (float db) { return dbToY (db); },
                 [&] (float gr)
                 {
                     // gain change hangs from the top on the levels' dB scale; with none it tucks above the edge
                     const float depth = juce::jlimit (0.0f, rangeDb, std::abs (gr));
                     return juce::jmap (depth, 0.0f, rangeDb, top, bottom) - 12.0f * (1.0f - juce::jlimit (0.0f, 1.0f, depth / 0.5f));
                 },
                 bottom, top - 12.0f);

    if (paths.any)
    {
        const auto gc = proc.isUpward() ? style::upColour() : style::grColour();
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        auto fade = [&] (juce::Colour c)   // data fades in from the left edge (no hard start)
        {
            return juce::ColourGradient (c.withAlpha (0.0f), r.getX(), 0.0f, c, r.getX() + 70.0f, 0.0f, false);
        };
        g.setGradientFill (fade (inputFill()));
        g.fillPath (paths.inFill);
        g.setGradientFill (fade (outputFill()));
        g.fillPath (paths.outFill);
        g.setGradientFill (fade (outputLine()));
        g.strokePath (paths.outLine, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (fade (gc.withAlpha (0.13f)));
        g.fillPath (paths.grFill);
        g.setGradientFill (fade (gc));
        g.strokePath (paths.grLine, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
    }

    // ---- threshold line + handle + label
    {
        const float th = raw (proc, ids::threshold);
        const float y = dbToY (th);
        const bool lit = hoverLine || dragging;
        const auto hb = handleBounds();
        const auto label = st::params::formatDb (th);
        const float lw = st::fonts::stringWidth (st::fonts::medium (11.0f), label) + 10.0f;
        const auto lr = juce::Rectangle<float> (hb.getX() - 8.0f - lw, y - 8.0f, lw, 16.0f);
        st::paint::fadedLine (g, { r.getX(), y }, { r.getX() + 40.0f, y }, t.accent.withAlpha (0.4f), 1.0f, 0.5f);
        dashedLine (g, { r.getX() + 40.0f, y, lr.getX() - 6.0f, y }, t.accent.withAlpha (lit ? 0.9f : 0.55f), lit ? 1.3f : 1.0f);
        st::paint::pillHandle (g, hb, lit, dragging);
        g.setFont (st::fonts::medium (11.0f));
        g.setColour (t.bg0.withAlpha (0.75f));
        g.fillRoundedRectangle (lr, 4.0f);
        g.setColour (lit ? t.text : t.textDim);
        g.drawText (label, lr, juce::Justification::centred, false);
    }

    // ---- style chip (top left of the plot): click for the style menu
    {
        const auto& info = getStyleInfo (proc.getStyle());
        const juce::String name = juce::String (info.name) + downArrow, sub = dot + info.character;
        const float w1 = st::fonts::stringWidth (st::fonts::semibold (13.0f), name);
        const float w2 = st::fonts::stringWidth (st::fonts::regular (12.0f), sub);
        chip = juce::Rectangle<float> (r.getX() + 10.0f, 10.0f, w1 + w2 + 28.0f, 22.0f);
        g.setColour (t.bg0.withAlpha (hoverChip ? 0.8f : 0.62f));
        g.fillRoundedRectangle (chip, 6.0f);
        if (hoverChip)
        {
            g.setColour (t.accent.withAlpha (0.35f));
            g.drawRoundedRectangle (chip.reduced (0.5f), 6.0f, 1.0f);
        }
        auto row = chip.reduced (10.0f, 0.0f);
        g.setFont (st::fonts::semibold (13.0f));
        g.setColour (t.accent2);
        g.drawText (name, row, juce::Justification::centredLeft, false);
        g.setFont (st::fonts::regular (12.0f));
        g.setColour (t.textDim);
        g.drawText (sub, row.withTrimmedLeft (w1 + 8.0f), juce::Justification::centredLeft, false);
    }
}

void LevelDisplay::showStyleMenu()
{
    auto m = buildStyleMenu (proc.getStyle());
    st::showMenu (m, this, [this] (int id)
    {
        if (id <= 0) return;
        proc.undoManager.beginNewTransaction ("Style");
        gesture (proc, ids::style, true);
        setParam (proc, ids::style, (float) (id - 1));
        gesture (proc, ids::style, false);
        repaint();
    });
}

void LevelDisplay::mouseMove (const juce::MouseEvent& e)
{
    const bool line = overThreshold (e.position), c = chip.contains (e.position);
    if (line != hoverLine || c != hoverChip) { hoverLine = line; hoverChip = c; repaint(); }
    setMouseCursor (line ? juce::MouseCursor::UpDownResizeCursor : (c ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
}

void LevelDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverLine = hoverChip = false;
    repaint();
}

void LevelDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (chip.contains (e.position)) { showStyleMenu(); return; }
    if (! overThreshold (e.position)) return;
    dragging = true;
    dragStartDb = raw (proc, ids::threshold);
    dragStartY = e.position.y;
    proc.undoManager.beginNewTransaction ("Threshold");
    gesture (proc, ids::threshold, true);
}

void LevelDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dbPerPx = rangeDb / getPlot().getHeight() * (e.mods.isShiftDown() ? 0.2f : 1.0f);
    setParam (proc, ids::threshold, juce::jlimit (limits::thresholdMin, limits::thresholdMax, dragStartDb - (e.position.y - dragStartY) * dbPerPx));
    repaint();
}

void LevelDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    gesture (proc, ids::threshold, false);
    repaint();
}

void LevelDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! handleBounds().expanded (6.0f, 5.0f).contains (e.position)) return;
    auto& p = proc.param (ids::threshold);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.getDefaultValue());
    p.endChangeGesture();
}

void LevelDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! overThreshold (e.position)) return;
    gesture (proc, ids::threshold, true);
    setParam (proc, ids::threshold, juce::jlimit (limits::thresholdMin, limits::thresholdMax, raw (proc, ids::threshold) + (w.deltaY > 0 ? 0.5f : -0.5f)));
    gesture (proc, ids::threshold, false);
}

//==============================================================================
KneeView::KneeView (StCompProcessor& p) : proc (p), live (p.getEngine().history)
{
    setTooltip ("Transfer curve of the running style; the dot is what the detector hears now. Drag sideways for threshold, up / down for ratio.");
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

juce::Rectangle<float> KneeView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f).withTrimmedTop (12.0f);
}

void KneeView::tick (double dt)
{
    const auto s = proc.getCompSettings();
    auto differs = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
    const bool changed = s.style != last.style || differs (s.thresholdDb, last.thresholdDb) || differs (s.ratio, last.ratio)
                         || differs (s.kneeDb, last.kneeDb) || differs (s.rangeDb, last.rangeDb);
    if (changed) { last = s; curve.clear(); }
    const bool moved = live.update (dt, 0.25f);
    liveDb = live.detDb;
    if (changed || moved) repaint();
}

void KneeView::rebuild (juce::Rectangle<float> a)
{
    curve.clear();
    for (int i = 0; i <= 160; ++i)
    {
        const float in = lo + (hi - lo) * (float) i / 160.0f;
        const float out = in + Compressor::staticGainDb (in, last);
        const float x = juce::jmap (in, lo, hi, a.getX(), a.getRight());
        const float y = juce::jmap (juce::jlimit (lo - 6.0f, hi + 6.0f, out), lo, hi, a.getBottom(), a.getY());
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
}

void KneeView::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    style::paintGlass (g, r, 8.0f);
    if (hover || dragging)
    {
        g.setColour (t.accent.withAlpha (0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }

    const auto a = plotArea();
    for (float db = -48.0f; db < 0.0f; db += 12.0f)
    {
        const float x = juce::jmap (db, lo, hi, a.getX(), a.getRight());
        const float y = juce::jmap (db, lo, hi, a.getBottom(), a.getY());
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (x, a.getY(), 1.0f, a.getHeight());
        g.fillRect (a.getX(), y, a.getWidth(), 1.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);

    if (curve.isEmpty()) rebuild (a);
    const float tx = juce::jmap (juce::jlimit (lo, hi, last.thresholdDb), lo, hi, a.getX(), a.getRight());
    st::paint::fadedLine (g, { tx, a.getY() }, { tx, a.getBottom() }, t.accent.withAlpha (0.45f), 1.0f, 0.2f);

    g.saveState();
    g.reduceClipRegion (a.expanded (2.0f).toNearestInt());
    st::paint::glowStroke (g, curve, t.curve.withAlpha (0.9f), 1.6f, 3.0f);
    g.restoreState();

    if (liveDb > lo)
    {
        const float out = liveDb + Compressor::staticGainDb (liveDb, last);
        const juce::Point<float> p (juce::jmap (juce::jmin (liveDb, hi), lo, hi, a.getX(), a.getRight()),
                                    juce::jmap (juce::jlimit (lo, hi, out), lo, hi, a.getBottom(), a.getY()));
        g.setColour (t.accent2.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (p));
        g.setColour (t.accent2);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (p));
    }

    auto head = r.removeFromTop (22.0f).reduced (12.0f, 0.0f).translated (0.0f, 4.0f);
    st::paint::caption (g, "Knee", head, t.textFaint, juce::Justification::centredLeft, 9.5f);
    g.setFont (st::fonts::medium (10.5f));
    g.setColour (dragging ? t.text : t.textDim);
    g.drawText (st::params::formatRatio (last.ratio), head, juce::Justification::centredRight, false);
}

void KneeView::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    startThreshold = raw (proc, ids::threshold);
    startRatioNorm = proc.param (ids::ratio).getValue();
    proc.undoManager.beginNewTransaction ("Knee display");
    gesture (proc, ids::threshold, true);
    gesture (proc, ids::ratio, true);
}

void KneeView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto d = e.getOffsetFromDragStart().toFloat();
    const float dbPerPx = (hi - lo) / plotArea().getWidth() * fine;
    setParam (proc, ids::threshold, juce::jlimit (limits::thresholdMin, limits::thresholdMax, startThreshold + d.x * dbPerPx));
    auto& ratio = proc.param (ids::ratio);
    ratio.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, startRatioNorm - d.y * 0.006f * fine));
    repaint();
}

void KneeView::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    gesture (proc, ids::threshold, false);
    gesture (proc, ids::ratio, false);
    repaint();
}

//==============================================================================
MeterView::MeterView (StCompProcessor& p) : proc (p), live (p.getEngine().history)
{
    setTooltip ("Input, gain change and output (peak). Click to reset the hold values.");
}

void MeterView::mouseDown (const juce::MouseEvent&)
{
    inHold = outHold = -120.0f;
    grHold = 0.0f;
    repaint();
}

void MeterView::tick (double dt)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float fall = (float) dt * 26.0f;
    auto rin = proc.inputMeter.read(), rout = proc.outputMeter.read();
    for (int c = 0; c < 2; ++c)
    {
        const int ci = juce::jmin (c, rin.numChannels - 1), co = juce::jmin (c, rout.numChannels - 1);
        in[(size_t) c] = juce::jmax (st::dsp::gainToDb (rin.peak[(size_t) ci]), in[(size_t) c] - fall);
        out[(size_t) c] = juce::jmax (st::dsp::gainToDb (rout.peak[(size_t) co]), out[(size_t) c] - fall);
    }
    live.update (dt, 0.12f);
    gr = live.grDb;
    const float i = juce::jmax (in[0], in[1]), o = juce::jmax (out[0], out[1]);
    if (i >= inHold || now - inHoldT > 2.0) { inHold = i; inHoldT = now; }
    if (o >= outHold || now - outHoldT > 2.0) { outHold = o; outHoldT = now; }
    if (std::abs (gr) >= std::abs (grHold) || now - grHoldT > 2.0) { grHold = gr; grHoldT = now; }
    repaint();
}

void MeterView::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat();
    const float colW = r.getWidth() / 3.0f;
    auto readout = r.removeFromTop (18.0f);
    auto caption = r.removeFromBottom (16.0f);
    auto bars = r.reduced (0.0f, 4.0f);

    auto levelY = [&] (float db) { return juce::jmap (juce::jlimit (-60.0f, 6.0f, db), -60.0f, 6.0f, bars.getBottom(), bars.getY()); };

    auto drawLevel = [&] (int col, const std::array<float, 2>& lv, float hold, const char* name)
    {
        const float cx = bars.getX() + colW * ((float) col + 0.5f);
        for (int c = 0; c < 2; ++c)
        {
            auto slot = juce::Rectangle<float> (cx - 6.5f + 7.0f * (float) c, bars.getY(), 5.0f, bars.getHeight());
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (slot, 2.0f);
            const float y = levelY (lv[(size_t) c]);
            g.setGradientFill (juce::ColourGradient (t.accent2, 0.0f, bars.getY(), t.textDim.withAlpha (0.8f), 0.0f, bars.getY() + bars.getHeight() * 0.3f, false));
            if (lv[(size_t) c] > 0.0f) g.setColour (t.danger);
            g.fillRoundedRectangle (slot.withTop (y), 2.0f);
        }
        g.setFont (st::fonts::medium (10.5f));
        g.setColour (hold > 0.0f ? t.danger : t.text.withAlpha (0.85f));
        g.drawText (hold > -99.0f ? juce::String (hold, 1) : juce::String ("-inf"), readout.withX (cx - colW * 0.5f).withWidth (colW), juce::Justification::centred, false);
        st::paint::caption (g, name, caption.withX (cx - colW * 0.5f).withWidth (colW), t.textFaint, juce::Justification::centred, 9.5f);
    };

    drawLevel (0, in, inHold, "In");
    drawLevel (2, out, outHold, "Out");

    // gain change: a bar hanging from the top, 0..24 dB (red reduction, blue upward gain)
    {
        const float cx = bars.getX() + colW * 1.5f;
        auto slot = juce::Rectangle<float> (cx - 5.0f, bars.getY(), 10.0f, bars.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (slot, 2.0f);
        const auto col = style::gainColour (gr);
        const float depth = juce::jlimit (0.0f, 1.0f, std::abs (gr) / 24.0f);
        g.setColour (col);
        g.fillRoundedRectangle (slot.withHeight (slot.getHeight() * depth), 2.0f);
        const float hy = slot.getY() + slot.getHeight() * juce::jlimit (0.0f, 1.0f, std::abs (grHold) / 24.0f);
        g.setColour (style::gainColour (grHold).withAlpha (0.8f));
        g.fillRect (slot.getX(), hy - 1.0f, slot.getWidth(), 1.5f);
        for (float db : { 3.0f, 6.0f, 12.0f, 18.0f })
        {
            const float y = slot.getY() + slot.getHeight() * db / 24.0f;
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRect (slot.getRight() + 2.0f, y, 3.0f, 1.0f);
        }
        g.setFont (st::fonts::medium (10.5f));
        g.setColour (std::abs (grHold) > 0.05f ? style::gainColour (grHold) : t.text.withAlpha (0.85f));
        g.drawText ((grHold > 0.05f ? "+" : "") + juce::String (grHold, 1), readout.withX (cx - colW * 0.5f).withWidth (colW), juce::Justification::centred, false);
        st::paint::caption (g, proc.isUpward() ? "Gain" : "GR", caption.withX (cx - colW * 0.5f).withWidth (colW), t.textFaint, juce::Justification::centred, 9.5f);
    }
}

//==============================================================================
namespace
{
    st::SpectrumAnalyzer::Options sidechainSpectrum()
    {
        st::SpectrumAnalyzer::Options o;
        o.fftOrder = 12;
        o.hopSize = 0;   // a fresh FFT every display frame (in step with the monitor)
        o.numDisplayPoints = 240;
        o.minHz = 20.0f;
        o.maxHz = 20000.0f;
        o.slopeDbPerOct = 4.5f;
        o.releaseDbPerSec = 36.0f;
        o.nonConsuming = true;
        return o;
    }
    constexpr float eqLo = -36.0f, eqHi = 24.0f;
}

SidechainDisplay::SidechainDisplay (StCompProcessor& p) : proc (p), analyzer (p.sidechainAnalyzer, sidechainSpectrum())
{
    setTooltip ("What the detector hears, and the side-chain filter. Drag the handles; wheel on the bell changes its width; double-click turns a handle off.");
}

juce::Rectangle<float> SidechainDisplay::plot() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (16.0f);
}

float SidechainDisplay::hzToX (float hz) const
{
    const auto r = plot();
    return r.getX() + r.getWidth() * std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
}

float SidechainDisplay::xToHz (float x) const
{
    const auto r = plot();
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

float SidechainDisplay::eqDbToY (float db) const
{
    const auto r = plot().reduced (0.0f, 8.0f);
    return juce::jmap (juce::jlimit (eqLo, eqHi, db), eqLo, eqHi, r.getBottom(), r.getY());
}

float SidechainDisplay::yToEqDb (float y) const
{
    const auto r = plot().reduced (0.0f, 8.0f);
    return juce::jmap (y, r.getBottom(), r.getY(), eqLo, eqHi);
}

juce::Rectangle<float> SidechainDisplay::handle (int which) const
{
    const auto s = proc.getSidechainSettings();
    const juce::Rectangle<float> pill (30.0f, 13.0f);
    switch (which)
    {
        case 0:  return pill.withCentre ({ juce::jmax (hzToX (s.lowCutHz), plot().getX() + 16.0f), eqDbToY (SidechainEq::lowCutOn (s) ? -6.0f : 0.0f) });
        case 1:  return pill.withCentre ({ hzToX (s.bellHz), eqDbToY (s.bellGainDb) });
        default: return pill.withCentre ({ juce::jmin (hzToX (s.highCutHz), plot().getRight() - 16.0f), eqDbToY (SidechainEq::highCutOn (s) ? -6.0f : 0.0f) });
    }
}

int SidechainDisplay::handleAt (juce::Point<float> p) const
{
    for (int i : { 1, 0, 2 })
        if (handle (i).expanded (5.0f).contains (p)) return i;
    return -1;
}

void SidechainDisplay::tick (double dt)
{
    bool changed = analyzer.update (dt);
    const auto s = proc.getSidechainSettings();
    if (std::memcmp (&s, &last, sizeof (s)) != 0) { last = s; response.clear(); changed = true; }
    if (changed) repaint();
}

void SidechainDisplay::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const auto r = plot();
    g.setColour (juce::Colours::black.withAlpha (0.32f));
    g.fillRoundedRectangle (r, 6.0f);

    // grid
    g.setFont (st::fonts::regular (10.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = hzToX (hz);
        st::paint::fadedLine (g, { x, r.getY() + 4.0f }, { x, r.getBottom() - 2.0f }, juce::Colours::white.withAlpha (0.04f), 1.0f, 0.25f);
        g.setColour (t.textFaint.withAlpha (0.85f));
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x - 20.0f, r.getBottom() + 1.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
    const float zeroY = eqDbToY (0.0f);
    st::paint::fadedLine (g, { r.getX() + 6.0f, zeroY }, { r.getRight() - 6.0f, zeroY }, juce::Colours::white.withAlpha (0.08f), 1.0f, 0.1f);

    // the detector's spectrum (before the filter), faint
    if (analyzer.getNumPoints() > 1)
    {
        auto spec = r.reduced (0.0f, 6.0f);
        auto path = analyzer.createPath (spec, -90.0f, 0.0f, minHz, maxHz, true);
        g.setGradientFill (juce::ColourGradient (t.spectrum.withAlpha (0.18f), 0.0f, spec.getY(), t.spectrum.withAlpha (0.03f), 0.0f, spec.getBottom(), false));
        g.fillPath (path);
    }

    // the filter response (level excluded: that's the Gain knob)
    if (response.isEmpty())
    {
        auto s = last;
        s.gainDb = 0.0f;
        const double fs = proc.getSampleRateOrDefault();
        const int steps = juce::jmax (2, (int) (r.getWidth() / 2.0f));
        for (int i = 0; i <= steps; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
            const float y = eqDbToY (SidechainEq::magnitudeDb (xToHz (x), s, fs));
            if (i == 0) response.startNewSubPath (x, y); else response.lineTo (x, y);
        }
    }
    {
        juce::Path fill (response);
        fill.lineTo (r.getRight(), zeroY);
        fill.lineTo (r.getX(), zeroY);
        fill.closeSubPath();
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (t.accent.withAlpha (0.12f));
        g.fillPath (fill);
        st::paint::glowStroke (g, response, t.accent.withAlpha (0.95f), 1.5f, 3.0f);
        g.restoreState();
    }

    // handles + the hovered / dragged handle's value
    const char* names[] = { "Low cut", "Bell", "High cut" };
    for (int i = 0; i < 3; ++i)
    {
        const auto hb = handle (i);
        const bool on = i == 0 ? SidechainEq::lowCutOn (last) : (i == 1 ? SidechainEq::bellOn (last) : SidechainEq::highCutOn (last));
        g.setOpacity (on || hover == i || dragging == i ? 1.0f : 0.55f);
        st::paint::pillHandle (g, hb, hover == i, dragging == i);
        g.setOpacity (1.0f);
        if (hover != i && dragging != i) continue;
        juce::String text = names[i];
        if (i == 0) text << "  " << (on ? st::params::formatHz (last.lowCutHz) : juce::String ("Off"));
        if (i == 2) text << "  " << (on ? st::params::formatHz (last.highCutHz) : juce::String ("Off"));
        if (i == 1) text << "  " << st::params::formatHz (last.bellHz) << "  " << st::params::formatDb (last.bellGainDb) << "  Q " << juce::String (last.bellQ, 2);
        g.setFont (st::fonts::medium (11.0f));
        const float w = st::fonts::stringWidth (st::fonts::medium (11.0f), text) + 12.0f;
        auto lr = juce::Rectangle<float> (w, 17.0f).withCentre ({ hb.getCentreX(), hb.getY() - 13.0f });
        lr = lr.withX (juce::jlimit (r.getX() + 2.0f, r.getRight() - w - 2.0f, lr.getX())).withY (juce::jmax (r.getY() + 2.0f, lr.getY()));
        g.setColour (t.bg0.withAlpha (0.85f));
        g.fillRoundedRectangle (lr, 4.0f);
        g.setColour (t.text);
        g.drawText (text, lr, juce::Justification::centred, false);
    }
}

void SidechainDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h == 1 ? juce::MouseCursor::DraggingHandCursor : (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor));
}

void SidechainDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position);
    if (dragging < 0) return;
    proc.undoManager.beginNewTransaction ("Side-chain filter");
    if (dragging == 0) gesture (proc, ids::scLowCut, true);
    if (dragging == 2) gesture (proc, ids::scHighCut, true);
    if (dragging == 1) { gesture (proc, ids::scBellFreq, true); gesture (proc, ids::scBellGain, true); }
}

void SidechainDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const float hz = xToHz (e.position.x);
    if (dragging == 0) setParam (proc, ids::scLowCut, juce::jlimit (20.0f, 2000.0f, hz));
    if (dragging == 2) setParam (proc, ids::scHighCut, juce::jlimit (1000.0f, 20000.0f, hz));
    if (dragging == 1)
    {
        setParam (proc, ids::scBellFreq, juce::jlimit (40.0f, 16000.0f, hz));
        setParam (proc, ids::scBellGain, juce::jlimit (-24.0f, 24.0f, yToEqDb (e.position.y)));
    }
    repaint();
}

void SidechainDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging == 0) gesture (proc, ids::scLowCut, false);
    if (dragging == 2) gesture (proc, ids::scHighCut, false);
    if (dragging == 1) { gesture (proc, ids::scBellFreq, false); gesture (proc, ids::scBellGain, false); }
    dragging = -1;
    repaint();
}

void SidechainDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h < 0) return;
    const juce::String id = h == 0 ? ids::scLowCut : (h == 1 ? ids::scBellGain : ids::scHighCut);
    const float off = h == 0 ? 20.0f : (h == 1 ? 0.0f : 20000.0f);
    gesture (proc, id, true);
    setParam (proc, id, off);
    gesture (proc, id, false);
}

void SidechainDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (handleAt (e.position) != 1) return;
    const float q = raw (proc, ids::scBellQ) * std::exp (w.deltaY * 0.8f);
    gesture (proc, ids::scBellQ, true);
    setParam (proc, ids::scBellQ, juce::jlimit (0.2f, 8.0f, q));
    gesture (proc, ids::scBellQ, false);
}

} // namespace stcomp
