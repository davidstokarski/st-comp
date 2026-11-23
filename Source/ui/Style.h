#pragma once
#include "../Params.h"

namespace stcomp::style
{

/** ST Comp's identity: the shared ST tokens recoloured near-black graphite with a deep green cast (display haze,
    fades), an emerald accent, red gain reduction (Theme::danger) and blue upward gain (Theme::positive).
    Called once from the processor constructor. */
void applyTheme();

/** The window backdrop: green-graphite haze at the top fading to near-black, soft light pools, vignette. */
void paintBackdrop (juce::Graphics&, juce::Rectangle<float> area);

/** Footer tray: dark, rounded top corners, faint green fade and a faded top edge light. */
void paintTray (juce::Graphics&, juce::Rectangle<float> area);

/** Translucent glass box for floating panels (control panel, knee display), with a tapered top-edge light. */
void paintGlass (juce::Graphics&, juce::Rectangle<float> box, float radius = 8.0f, float opacity = 1.0f);

/** Gain-reduction colour (red), upward gain colour (blue). */
juce::Colour grColour();
juce::Colour upColour();
inline juce::Colour gainColour (float gainDb) { return gainDb > 0.0f ? upColour() : grColour(); }

} // namespace stcomp::style
