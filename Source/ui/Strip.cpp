#include "Strip.h"

namespace stcomp
{

namespace
{
    constexpr int styleWidth = 104;
}

CompStrip::CompStrip (StCompProcessor& p) : proc (p), reader (p.getEngine().history, 1024, 260.0f)
{
    const auto& t = st::Theme::get();
    auto make = [&] (const juce::String& id, const char* label)
    {
        auto k = std::make_unique<st::Knob> (proc.param (id), &proc.undoManager, label);
        k->setSize (st::Knob::Size::small);
        k->setLook (st::Knob::Look::arc);
        k->setColour (t.accent);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    };
    make (ids::threshold, "Thresh");
    make (ids::ratio, "Ratio");
    make (ids::attack, "Attack");
    make (ids::release, "Release");
    make (ids::mix, "Mix");
    make (ids::outputGain, "Out");
    startTimerHz (30);
}

CompStrip::~CompStrip() { stopTimer(); }

int CompStrip::knobWidth (int h) { return juce::jlimit (52, 64, juce::roundToInt ((float) h * 0.38f)); }
int CompStrip::historyWidth (int h) { return juce::jlimit (100, 220, juce::roundToInt ((float) h * 1.1f)); }

int CompStrip::widthFor (int height)
{
    return styleWidth + historyWidth (height) + 12 + 6 * knobWidth (height) + 14;
}

void CompStrip::resized()
{
    auto r = getLocalBounds();
    styleArea = r.removeFromLeft (styleWidth);
    powerArea = juce::Rectangle<int> (styleArea.getX() + 4, styleArea.getY() + 4, 20, 20);
    historyArea = r.removeFromLeft (historyWidth (getHeight())).reduced (0, 6);
    r.removeFromLeft (12);
    const int kw = knobWidth (getHeight());
    const int kh = juce::jmin (r.getHeight() - 8, kw + 18);
    auto row = r.withSizeKeepingCentre (kw * (int) knobs.size(), kh).withX (r.getX());
    for (auto& k : knobs) k->setBounds (row.removeFromLeft (kw));
}

void CompStrip::timerCallback()
{
    // strongest gain change since the last tick, held then released smoothly (readable at 30 fps)
    const auto peak = reader.peakOfNewest (reader.update());
    scroll.update (1.0 / 30.0, reader.end(), reader.getFrameRate());
    gr = std::abs (peak.grDb) > std::abs (gr) ? peak.grDb : gr + 0.25f * (peak.grDb - gr);
    repaint (historyArea.expanded (2));
    repaint (styleArea);
}

void CompStrip::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    g.setColour (t.bg1);
    g.fillRect (getLocalBounds());
    {
        const auto sa = styleArea.toFloat();
        g.setGradientFill (juce::ColourGradient (t.displayGlow.withAlpha (0.45f), sa.getCentreX(), sa.getY() + sa.getHeight() * 0.3f,
                                                 t.displayGlow.withAlpha (0.0f), sa.getRight() + 10.0f, sa.getBottom(), true));
        g.fillRect (sa);
    }
    st::paint::fadedLine (g, { (float) styleArea.getRight() - 0.5f, 6.0f }, { (float) styleArea.getRight() - 0.5f, (float) getHeight() - 6.0f },
                          juce::Colours::white.withAlpha (0.08f), 1.0f, 0.3f);

    const bool bypassed = proc.apvts.getRawParameterValue (ids::bypass)->load() > 0.5f;
    st::icons::draw (g, st::icons::Icon::power, powerArea.toFloat().reduced (3.0f), bypassed ? t.warning : t.textDim.withAlpha (0.7f), 1.4f);

    // style + live gain change
    const auto& info = getStyleInfo (proc.getStyle());
    auto sa = styleArea.toFloat().reduced (8.0f, 8.0f).withTrimmedTop (18.0f);
    g.setFont (st::fonts::semibold (14.0f));
    g.setColour (t.accent2);
    g.drawText (juce::String (info.name) + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")), sa.removeFromTop (20.0f), juce::Justification::centred, false);
    g.setFont (st::fonts::medium (10.5f));
    g.setColour (t.textDim);
    g.drawText (info.family == Family::vintage ? "Vintage" : "Modern", sa.removeFromTop (14.0f), juce::Justification::centred, false);
    if (sa.getHeight() >= 16.0f)
    {
        g.setFont (st::fonts::semibold (12.0f));
        g.setColour (std::abs (gr) > 0.05f ? style::gainColour (gr) : t.textFaint);
        g.drawText ((gr > 0.05f ? "+" : "") + st::params::formatDb (gr, 1, true, false), sa.removeFromTop (18.0f).translated (0.0f, 2.0f), juce::Justification::centred, false);
    }

    // history: input (dark), output (light), gain change and the threshold
    const auto h = historyArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (h, 5.0f);
    constexpr float range = 48.0f;
    auto yOf = [&] (float db) { return juce::jmap (juce::jlimit (-range, 0.0f, db), -range, 0.0f, h.getBottom() - 3.0f, h.getY() + 3.0f); };
    const double fpp = 3.0 * reader.getFrameRate() / (double) juce::jmax (1.0f, h.getWidth());   // 3 seconds
    st::HistoryPaths paths;
    paths.build (reader, scroll.getPosition(), h.getX() - 2.0f, h.getRight(), fpp, yOf,
                 [&] (float grDb) { return juce::jmap (juce::jlimit (0.0f, range, std::abs (grDb)), 0.0f, range, h.getY() + 3.0f, h.getBottom() - 3.0f); },
                 h.getBottom(), h.getY());
    g.saveState();
    g.reduceClipRegion (historyArea);
    g.setColour (t.textFaint.withAlpha (0.3f));
    g.fillPath (paths.inFill);
    g.setColour (t.textDim.withAlpha (0.28f));
    g.fillPath (paths.outFill);
    g.setColour (t.text.withAlpha (0.45f));
    g.strokePath (paths.outLine, juce::PathStrokeType (0.9f));
    g.setColour (proc.isUpward() ? style::upColour() : style::grColour());
    g.strokePath (paths.grLine, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ty = yOf (proc.apvts.getRawParameterValue (ids::threshold)->load());
    const float dashes[] = { 3.0f, 3.0f };
    g.setColour (t.accent.withAlpha (0.6f));
    g.drawDashedLine ({ h.getX() + 4.0f, ty, h.getRight() - 4.0f, ty }, dashes, 2, 1.0f);
    g.restoreState();
}

void CompStrip::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (styleArea.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void CompStrip::mouseDown (const juce::MouseEvent& e)
{
    if (powerArea.expanded (2).contains (e.getPosition()))
    {
        auto& p = proc.param (ids::bypass);
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getValue() > 0.5f ? 0.0f : 1.0f);
        p.endChangeGesture();
        repaint();
        return;
    }
    if (! styleArea.contains (e.getPosition())) return;

    // native menu at the mouse (works for remote views too)
    auto m = buildStyleMenu (proc.getStyle());
    st::showMenu (m, this, [safe = juce::Component::SafePointer<CompStrip> (this)] (int id)
    {
        if (safe == nullptr || id <= 0) return;
        auto& p = safe->proc.param (ids::style);
        safe->proc.undoManager.beginNewTransaction ("Style");
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 ((float) (id - 1)));
        p.endChangeGesture();
        safe->repaint();
    }, true);
}

} // namespace stcomp
