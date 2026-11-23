#include "Style.h"

namespace stcomp::style
{

void applyTheme()
{
    // Graphite with a deep green cast: a studio tool, emerald for the identity, red for what it takes away.
    auto& t = st::Theme::get();
    t.bg0 = juce::Colour (0xff070a09);
    t.bg1 = juce::Colour (0xff0c100f);
    t.bg2 = juce::Colour (0xff131816);
    t.bg3 = juce::Colour (0xff1b211f);
    t.bg4 = juce::Colour (0xff262d2a);
    t.stroke = juce::Colour (0xff242b28);
    t.strokeSoft = juce::Colour (0xff151a18);
    t.highlight = juce::Colour (0x12ffffff);
    t.displayTop = juce::Colour (0xff0f1916);    // dark green haze at the top of the window
    t.displayGlow = juce::Colour (0xff163a2f);   // soft emerald light
    t.glassPanel = juce::Colour (0xbd111715);
    t.knobLight = juce::Colour (0xffb8bdbb);
    t.knobDark = juce::Colour (0xff676c6a);
    t.buttonTop = juce::Colour (0xff525856);
    t.buttonBottom = juce::Colour (0xff3a3f3d);
    t.text = juce::Colour (0xffecf2ef);
    t.textDim = juce::Colour (0xffa1aba6);
    t.textFaint = juce::Colour (0xff65706b);
    t.accent = juce::Colour (0xff2fc896);        // emerald
    t.accent2 = juce::Colour (0xff8de9c7);       // mint
    t.spectrum = juce::Colour (0xffa9c3b9);
    t.curve = juce::Colour (0xffe3f6ed);
    t.danger = juce::Colour (0xffff5a4c);        // gain reduction
    t.positive = juce::Colour (0xff5cb8ff);      // upward gain
    t.warning = juce::Colour (0xffffcf5c);
}

juce::Colour grColour() { return st::Theme::get().danger; }
juce::Colour upColour() { return st::Theme::get().positive; }

void paintBackdrop (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = st::Theme::get();
    juce::ColourGradient base (t.displayTop, r.getCentreX(), r.getY(), t.bg0, r.getCentreX(), r.getBottom(), false);
    base.addColour (0.45, t.displayTop.interpolatedWith (t.bg0, 0.55f));
    base.addColour (0.8, t.bg1.interpolatedWith (t.bg0, 0.5f));
    g.setGradientFill (base);
    g.fillRect (r);

    // light pools: a wide one over the top centre, softer ones low left and right
    auto pool = [&] (juce::Point<float> c, float radius, juce::Colour col, float alpha)
    {
        juce::ColourGradient rg (col.withAlpha (alpha), c.x, c.y, col.withAlpha (0.0f), c.x + radius, c.y, true);
        g.setGradientFill (rg);
        g.fillRect (r);
    };
    pool ({ r.getCentreX(), r.getY() - r.getHeight() * 0.12f }, r.getWidth() * 0.6f, t.displayGlow, 0.42f);
    pool ({ r.getX() + r.getWidth() * 0.1f, r.getY() + r.getHeight() * 0.78f }, r.getWidth() * 0.32f, t.displayGlow, 0.13f);
    pool ({ r.getRight() - r.getWidth() * 0.08f, r.getY() + r.getHeight() * 0.36f }, r.getWidth() * 0.28f, t.displayGlow, 0.10f);

    juce::ColourGradient vignette (juce::Colours::transparentBlack, r.getCentreX(), r.getY() + r.getHeight() * 0.42f,
                                   juce::Colours::black.withAlpha (0.42f), r.getX() - r.getWidth() * 0.05f, r.getY() + r.getHeight() * 0.42f, true);
    g.setGradientFill (vignette);
    g.fillRect (r);
}

void paintTray (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = st::Theme::get();
    juce::Path bar;
    bar.addRoundedRectangle (r.getX() + 6.0f, r.getY(), r.getWidth() - 12.0f, r.getHeight() + 20.0f, 16.0f, 16.0f, true, true, false, false);
    g.setGradientFill (juce::ColourGradient (t.bg2.withAlpha (0.92f), 0.0f, r.getY(), t.bg0.withAlpha (0.96f), 0.0f, r.getBottom(), false));
    g.fillPath (bar);
    g.saveState();
    g.reduceClipRegion (bar);
    g.setGradientFill (juce::ColourGradient (t.displayGlow.withAlpha (0.18f), r.getCentreX(), r.getY(),
                                             t.displayGlow.withAlpha (0.0f), r.getCentreX() + r.getWidth() * 0.45f, r.getY(), true));
    g.fillRect (r);
    g.restoreState();
    st::paint::fadedLine (g, { r.getX() + 24.0f, r.getY() + 0.5f }, { r.getRight() - 24.0f, r.getY() + 0.5f },
                          juce::Colours::white.withAlpha (0.10f), 1.0f, 0.18f);
}

void paintGlass (juce::Graphics& g, juce::Rectangle<float> box, float radius, float opacity)
{
    g.setColour (juce::Colours::black.withAlpha (0.32f * opacity));
    g.fillRoundedRectangle (box.translated (0.0f, 3.0f).expanded (2.0f), radius + 1.0f);
    g.setColour (st::Theme::get().glassPanel.withMultipliedAlpha (opacity));
    g.fillRoundedRectangle (box, radius);
    g.setColour (juce::Colours::white.withAlpha (0.035f * opacity));
    g.drawRoundedRectangle (box.reduced (0.5f), radius, 1.0f);
    // the top edge light: brightest in the middle, fading towards the corners
    juce::ColourGradient edge (juce::Colours::white.withAlpha (0.0f), box.getX(), 0.0f, juce::Colours::white.withAlpha (0.0f), box.getRight(), 0.0f, false);
    edge.addColour (0.25, juce::Colours::white.withAlpha (0.12f * opacity));
    edge.addColour (0.75, juce::Colours::white.withAlpha (0.12f * opacity));
    g.setGradientFill (edge);
    g.fillRect (box.getX() + radius, box.getY() + 0.5f, box.getWidth() - 2.0f * radius, 1.0f);
}

} // namespace stcomp::style
