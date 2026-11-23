#include "Panels.h"

namespace stcomp
{

//==============================================================================
BarButton::BarButton (const juce::String& text, const juce::String& tooltip) : juce::Button (text)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void BarButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const bool lit = isLit && isLit();
    st::paint::greyButton (g, r, 5.0f, highlighted, down, false);
    auto text = r;
    if (lit)
    {
        // small indicator light at the left
        const auto led = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
        g.setColour (t.accent.withAlpha (0.35f));
        g.fillEllipse (led.expanded (2.5f));
        g.setColour (t.accent2);
        g.fillEllipse (led);
        text.removeFromLeft (10.0f);
    }
    g.setFont (st::fonts::medium (12.5f));
    g.setColour (lit ? t.accent2 : t.text.withAlpha (highlighted ? 1.0f : 0.88f));
    g.drawText (getButtonText(), text, juce::Justification::centred, false);
}

//==============================================================================
ControlPanel::ControlPanel (StCompProcessor& p) : proc (p)
{
    const auto& t = st::Theme::get();
    auto* um = &proc.undoManager;
    using S = st::Knob::Size;
    using L = st::Knob::Look;

    knob (ids::threshold, "Threshold", S::large, L::ringed, "Level where compression starts (also draggable in the display)");
    knob (ids::ratio, "Ratio", S::medium, L::classic, "How hard levels past the threshold are pushed back (Upward: how far quiet parts come up)");
    knob (ids::knee, "Knee", S::medium, L::classic, "Width of the soft transition around the threshold");
    knob (ids::range, "Range", S::medium, L::classic, "Most gain change the compressor may apply");
    knob (ids::attack, "Attack", S::medium, L::classic, "How fast gain reduction starts. Longer lets more of each transient through.");
    knob (ids::release, "Release", S::medium, L::classic, "How fast gain reduction recovers");
    knob (ids::hold, "Hold", S::medium, L::classic, "How long gain reduction waits before it starts to release");
    knob (ids::lookahead, "Lookahead", S::medium, L::classic, "Delays the audio so the detector sees peaks coming (adds latency)");
    knob (ids::drive, "Drive", S::medium, L::ringed, "Saturation after compression (pick the character above)");
    knob (ids::mix, "Mix", S::medium, L::classic, "Parallel compression: blend of dry and compressed signal (latency aligned)");
    knob (ids::outputGain, "Output", S::medium, L::ringed, "Output gain (make-up)");
    knob (ids::stereoLink, "Link", S::small, L::classic, "Stereo link: 100% = one gain for both channels");

    styleBox = std::make_unique<st::Dropdown> (proc.param (ids::style), um);
    styleBox->setGreyStyle (true);
    styleBox->setTooltip ("Style: the compressor design (modern or vintage). Each has its own detector, curve, timing and colour.");
    styleBox->buildMenu = [] (int current) { return buildStyleMenu (current); };

    character = std::make_unique<st::Dropdown> (proc.param (ids::character), um);
    character->setGreyStyle (true);
    character->setTooltip ("Saturation after the compressor: Tube (even), Diode (gritty), Tape (soft, darker), Transformer (thick lows), Bright (airy highs)");

    autoRelease = std::make_unique<st::ToggleChip> (proc.param (ids::autoRelease), um, "Auto", t.accent2);
    autoRelease->setGreyStyle (true);
    autoRelease->setTooltip ("Programme-dependent release: short peaks recover fast, sustained compression slowly");
    autoGain = std::make_unique<st::ToggleChip> (proc.param (ids::autoGain), um, "Auto Gain", t.accent2);
    autoGain->setGreyStyle (true);
    autoGain->setTooltip ("Make-up gain estimated from the curve (and the drive): compare levels honestly");

    sidechain.isLit = [this]
    {
        const auto s = proc.getSidechainSettings();
        auto on = [this] (const juce::String& id) { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; };
        return on (ids::scExternal) || on (ids::scListen) || SidechainEq::lowCutOn (s) || SidechainEq::highCutOn (s)
               || SidechainEq::bellOn (s) || std::abs (s.gainDb) > 0.05f;
    };
    sidechain.onClick = [this] { if (onSidechain) onSidechain(); };

    for (juce::Component* c : { (juce::Component*) styleBox.get(), (juce::Component*) character.get(), (juce::Component*) autoRelease.get(),
                                (juce::Component*) autoGain.get(), (juce::Component*) &sidechain })
        addAndMakeVisible (c);
    startTimerHz (10);
}

st::Knob& ControlPanel::knob (const juce::String& id, const char* label, st::Knob::Size size, st::Knob::Look look, const char* tip)
{
    auto k = std::make_unique<st::Knob> (proc.param (id), &proc.undoManager, label);
    k->setSize (size);
    k->setLook (look);
    k->setColour (st::Theme::get().accent);
    k->setTooltip (tip);
    addAndMakeVisible (*k);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

st::Knob* ControlPanel::getKnob (const juce::String& id) const
{
    for (auto& k : knobs)
        if (k->getParameter().getParameterID() == id) return k.get();
    return nullptr;
}

void ControlPanel::timerCallback()
{
    const bool lit = sidechain.isLit();
    if (lit != lastLit) { lastLit = lit; sidechain.repaint(); }
    // Drive does nothing with the Clean character: say so by dimming it
    const bool clean = proc.apvts.getRawParameterValue (ids::character)->load() < 0.5f;
    if (auto* d = getKnob (ids::drive); d != nullptr && clean != driveDimmed)
    {
        driveDimmed = clean;
        d->setAlpha (clean ? 0.42f : 1.0f);
        d->setTooltip (clean ? "Drive: pick a character above (Tube, Diode, Tape, Transformer, Bright) to use it"
                             : "Saturation after compression, in the character picked above");
    }
}

void ControlPanel::resized()
{
    const int w = getWidth();
    box = juce::Rectangle<float> (6.0f, 22.0f, (float) w - 12.0f, 66.0f);
    dividers.clear();

    const int ky = 38, kw = 68, kh = 76;
    auto at = [this] (const juce::String& id) { return getKnob (id); };
    int x = 16;
    at (ids::threshold)->setBounds (x, 4, 112, 132);   x += 116;
    const int styleX = x;
    at (ids::ratio)->setBounds (x, ky, kw, kh);        x += kw;
    at (ids::knee)->setBounds (x, ky, kw, kh);         x += kw;
    at (ids::range)->setBounds (x, ky, kw, kh);        x += kw;
    styleBox->setBounds (styleX + 6, 10, x - styleX - 12, 23);
    x += 10;
    dividers.push_back ((float) x - 5.0f);
    at (ids::attack)->setBounds (x, ky, kw, kh);       x += kw;
    at (ids::release)->setBounds (x, ky, kw, kh);
    autoRelease->setBounds (x + kw / 2 - 26, 10, 52, 23);
    x += kw;
    at (ids::hold)->setBounds (x, ky, kw, kh);         x += kw;
    at (ids::lookahead)->setBounds (x, ky, kw + 16, kh); x += kw + 16 + 10;   // the longest caption
    dividers.push_back ((float) x - 5.0f);
    at (ids::drive)->setBounds (x, 36, 86, 96);
    character->setBounds (x + 43 - 60, 10, 120, 23);
    x += 86;
    at (ids::mix)->setBounds (x, ky, kw, kh);          x += kw;
    at (ids::outputGain)->setBounds (x, 36, 86, 96);
    autoGain->setBounds (x + 43 - 40, 10, 80, 23);
    x += 86 + 10;
    dividers.push_back ((float) x - 5.0f);
    at (ids::stereoLink)->setBounds (x + 14, 44, 64, 68);
    sidechain.setBounds (x, 10, 92, 23);
    x += 92;

    // centre the row in the bar
    const int shift = juce::jmax (0, (w - (x + 16)) / 2);
    for (auto* c : getChildren()) c->setTopLeftPosition (c->getX() + shift, c->getY());
    for (auto& d : dividers) d += (float) shift;
}

void ControlPanel::paint (juce::Graphics& g)
{
    style::paintGlass (g, box, 8.0f);
    for (float x : dividers)
        st::paint::fadedLine (g, { x, box.getY() + 8.0f }, { x, box.getBottom() + 36.0f }, juce::Colours::white.withAlpha (0.08f), 1.0f, 0.35f);
}

//==============================================================================
FooterBar::FooterBar (StCompProcessor& p)
    : quality (p.param (ids::quality), &p.undoManager),
      channels (p.param (ids::channelMode), &p.undoManager),
      input (p.param (ids::inputGain), &p.undoManager, "In"),
      bypass (p.param (ids::bypass), &p.undoManager, "Bypass", st::Theme::get().warning)
{
    quality.setFlatStyle (true);
    quality.setCaption ("Oversampling");
    quality.setTooltip ("Oversampling for the colour stage (style harmonics, drive): less aliasing, adds latency");
    channels.setFlatStyle (true);
    channels.setCaption ("Channels");
    channels.setTooltip ("Stereo, mid / side, or one channel only (mid, side, left or right): the others pass untouched");
    input.setFlatStyle (true);
    input.setTooltip ("Input gain into the compressor (the Mix dry signal gets it too)");
    bypass.setFlatStyle (true);
    bypass.setTooltip ("Bypass (latency compensated)");
    range.setDropdownArrow (true);
    speed.setDropdownArrow (true);

    range.onClick = [this]
    {
        juce::PopupMenu m;
        for (float r : { 24.0f, 36.0f, 48.0f, 60.0f })
            m.addItem (juce::PopupMenu::Item (juce::String ((int) r) + " dB").setTicked (std::abs (r - rangeDb) < 0.5f)
                           .setAction ([this, r] { setDisplay (r, seconds); if (onDisplayChanged) onDisplayChanged (rangeDb, seconds); }));
        st::showMenu (m, &range);
    };
    speed.onClick = [this]
    {
        juce::PopupMenu m;
        for (float s : { 2.0f, 4.0f, 8.0f, 16.0f })
            m.addItem (juce::PopupMenu::Item (juce::String ((int) s) + " s").setTicked (std::abs (s - seconds) < 0.1f)
                           .setAction ([this, s] { setDisplay (rangeDb, s); if (onDisplayChanged) onDisplayChanged (rangeDb, seconds); }));
        st::showMenu (m, &speed);
    };

    for (juce::Component* c : { (juce::Component*) &quality, (juce::Component*) &channels, (juce::Component*) &range,
                                (juce::Component*) &speed, (juce::Component*) &input, (juce::Component*) &bypass })
        addAndMakeVisible (c);
    refreshText();
}

void FooterBar::setDisplay (float r, float s)
{
    rangeDb = r;
    seconds = s;
    refreshText();
}

void FooterBar::refreshText()
{
    range.setButtonText (juce::String ((int) rangeDb) + " dB");
    range.setDimmedText ("Range  ");
    speed.setButtonText (juce::String ((int) seconds) + " s");
    speed.setDimmedText ("Time  ");
    repaint();
}

void FooterBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    style::paintTray (g, r);
    for (float x : dividers)
        st::paint::fadedLine (g, { x + 0.5f, r.getY() + 8.0f }, { x + 0.5f, r.getBottom() - 8.0f }, juce::Colours::white.withAlpha (0.10f), 1.0f, 0.5f);
}

void FooterBar::resized()
{
    auto r = getLocalBounds().reduced (28, 0);
    dividers.clear();
    auto divide = [this, &r] { dividers.push_back ((float) r.getX() + 10.0f); r.removeFromLeft (20); };
    quality.setBounds (r.removeFromLeft (150));
    divide();
    channels.setBounds (r.removeFromLeft (150));
    divide();
    range.setBounds (r.removeFromLeft (110));
    divide();
    speed.setBounds (r.removeFromLeft (90));

    bypass.setBounds (r.removeFromRight (84));
    r.removeFromRight (8);
    dividers.push_back ((float) r.getRight() - 4.0f);
    r.removeFromRight (16);
    input.setBounds (r.removeFromRight (110));
}

//==============================================================================
SidechainPopover::SidechainPopover (StCompProcessor& p)
    : st::Popover ("Side Chain", "what the detector hears"), proc (p), display (p),
      external (p.param (ids::scExternal), &p.undoManager, "External", st::Theme::get().accent2),
      listen (p.param (ids::scListen), &p.undoManager, "Listen", st::Theme::get().warning)
{
    addAndMakeVisible (display);
    auto make = [this] (const juce::String& id, const char* label, const char* tip)
    {
        auto k = std::make_unique<st::Knob> (proc.param (id), &proc.undoManager, label);
        k->setSize (st::Knob::Size::small);
        k->setLook (st::Knob::Look::classic);
        k->setColour (st::Theme::get().accent);
        k->setTooltip (tip);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    };
    make (ids::scGain, "Level", "Side-chain level: more = compresses harder at the same threshold");
    make (ids::scLowCut, "Low Cut", "The detector ignores the lows below this (less pumping from bass and kick)");
    make (ids::scBellFreq, "Bell", "Frequency the detector hears more (or less) of: de-essing, focus");
    make (ids::scBellGain, "Bell Gain", "How much more (or less) the detector hears around the bell");
    make (ids::scBellQ, "Q", "Width of the bell (narrower = higher)");
    make (ids::scHighCut, "High Cut", "The detector ignores the highs above this");
    make (ids::stereoLink, "Link", "Stereo link: 100% = one gain for both channels");

    external.setGreyStyle (true);
    external.setTooltip ("Detect on the side-chain input (ducking, pumping from another track)");
    listen.setGreyStyle (true);
    listen.setTooltip ("Hear what the detector hears (the side chain after its filter)");
    addAndMakeVisible (external);
    addAndMakeVisible (listen);
}

void SidechainPopover::layoutContent (juce::Rectangle<int> c)
{
    display.setBounds (c.removeFromTop (172));
    c.removeFromTop (12);
    auto chips = c.removeFromRight (96);
    external.setBounds (chips.removeFromTop (26).reduced (2, 0));
    chips.removeFromTop (8);
    listen.setBounds (chips.removeFromTop (26).reduced (2, 0));
    c.removeFromRight (10);
    const int kw = c.getWidth() / (int) knobs.size();
    for (auto& k : knobs) k->setBounds (c.removeFromLeft (kw).withHeight (juce::jmin (70, c.getHeight())));
}

void SidechainPopover::paintContent (juce::Graphics&) {}

} // namespace stcomp
