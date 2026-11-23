#pragma once
#include "Display.h"

namespace stcomp
{

/** ST Comp's module in the multiview (st::MultiView via StCompProcessor::createStripView): the style (click for a
    native menu), live gain change, a bypass switch, a level / gain history with the threshold and
    Threshold / Ratio / Attack / Release / Mix / Output knobs. Other ST plug-ins render it off screen and forward
    the mouse, so it is knobs and self-handled clicks only. */
class CompStrip : public juce::Component, private juce::Timer
{
public:
    explicit CompStrip (StCompProcessor&);
    ~CompStrip() override;

    static int widthFor (int height);
    int getNumKnobs() const noexcept { return (int) knobs.size(); }
    st::Knob* getKnob (int i) noexcept { return juce::isPositiveAndBelow (i, (int) knobs.size()) ? knobs[(size_t) i].get() : nullptr; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    StCompProcessor& proc;
    st::HistoryReader reader;
    st::SmoothScroll scroll;
    std::vector<std::unique_ptr<st::Knob>> knobs;
    juce::Rectangle<int> styleArea, historyArea, powerArea;
    float gr = 0.0f;

    static int knobWidth (int height);
    static int historyWidth (int height);
    void timerCallback() override;
};

} // namespace stcomp
