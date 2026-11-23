#pragma once
#include "Display.h"

namespace stcomp
{

/** A grey hardware button for the control panel's top edge (not bound to a parameter), with an indicator light
    when `isLit()` says so. Opens things (the side-chain popover). */
class BarButton : public juce::Button
{
public:
    BarButton (const juce::String& text, const juce::String& tooltip);
    std::function<bool()> isLit;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

//==============================================================================
/** The compressor controls, floating over the lower part of the window: a translucent bar with grey buttons on its
    top edge (style, auto release, character, auto gain, side chain) and knobs hanging from it. */
class ControlPanel : public juce::Component, private juce::Timer
{
public:
    explicit ControlPanel (StCompProcessor&);
    ~ControlPanel() override { stopTimer(); }
    static constexpr int height = 144;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onSidechain;
    /** Where the side-chain popover should point (local coordinates of this panel). */
    juce::Rectangle<int> getSidechainAnchor() const { return sidechain.getBounds(); }
    st::Knob* getKnob (const juce::String& paramId) const;
    int getNumKnobs() const noexcept { return (int) knobs.size(); }

private:
    StCompProcessor& proc;
    std::vector<std::unique_ptr<st::Knob>> knobs;
    std::unique_ptr<st::Dropdown> styleBox, character;
    std::unique_ptr<st::ToggleChip> autoRelease, autoGain;
    BarButton sidechain { "Side Chain", "Side-chain filter, external input, listen and stereo link" };
    juce::Rectangle<float> box;
    std::vector<float> dividers;
    bool lastLit = false, driveDimmed = false;

    st::Knob& knob (const juce::String& id, const char* label, st::Knob::Size, st::Knob::Look, const char* tip);
    void timerCallback() override;
};

//==============================================================================
/** Bottom tray: oversampling, channel mode, display range / time, input gain, bypass. */
class FooterBar : public juce::Component
{
public:
    explicit FooterBar (StCompProcessor&);
    std::function<void (float rangeDb, float seconds)> onDisplayChanged;
    void setDisplay (float rangeDb, float seconds);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    st::Dropdown quality, channels;
    st::FlatButton range { "", "Level display range" }, speed { "", "How much time the level display shows" };
    st::NumberBox input;
    st::ToggleChip bypass;
    float rangeDb = 48.0f, seconds = 4.0f;
    std::vector<float> dividers;
    void refreshText();
};

//==============================================================================
/** Side-chain detail: what the detector hears and its filter (SidechainDisplay), side-chain level, filter knobs,
    stereo link, External input and Listen. */
class SidechainPopover : public st::Popover
{
public:
    explicit SidechainPopover (StCompProcessor&);
    static constexpr int panelWidth = 620, panelHeight = 316;

    void tick (double dt) { display.tick (dt); }

private:
    StCompProcessor& proc;
    SidechainDisplay display;
    std::vector<std::unique_ptr<st::Knob>> knobs;
    st::ToggleChip external, listen;
    void layoutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;
};

} // namespace stcomp
