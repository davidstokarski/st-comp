#pragma once
#include "../PluginProcessor.h"
#include "Style.h"

namespace stcomp
{

/** The main display (Pro-style level history): input level as a dark fill rising from the bottom, output level as
    a lighter fill with a stroke, gain reduction as a red trace hanging from the top (upward gain in blue), all on
    one dB scale, newest at the right. The threshold is a dashed line you drag (anywhere on it, or by its pill
    handle). The running style is a chip at the top left; click it for the style menu. */
class LevelDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LevelDisplay (StCompProcessor&);

    void tick (double dt);
    /** dB shown from the top (0 dB) to the bottom (-range). */
    void setRangeDb (float r) { rangeDb = r; repaint(); }
    float getRangeDb() const noexcept { return rangeDb; }
    void setSeconds (float s) { seconds = s; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::Rectangle<float> getPlot() const;
    juce::Rectangle<float> getStyleChipBounds() const { return chip; }

private:
    StCompProcessor& proc;
    st::HistoryReader reader;
    st::SmoothScroll scroll;
    float rangeDb = 48.0f, seconds = 4.0f;
    bool hoverLine = false, hoverChip = false, dragging = false;
    float dragStartDb = 0.0f, dragStartY = 0.0f;
    juce::Rectangle<float> chip;

    float dbToY (float db) const;
    juce::Rectangle<float> handleBounds() const;
    bool overThreshold (juce::Point<float>) const;
    void showStyleMenu();
};

//==============================================================================
/** Transfer curve (input dB -> output dB) of the running style, with the live detector level as a dot riding it.
    Drag sideways to move the threshold, up / down to change the ratio. */
class KneeView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit KneeView (StCompProcessor&);
    void tick (double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    StCompProcessor& proc;
    st::LiveLevels live;
    float liveDb = -120.0f;
    CompSettings last;
    juce::Path curve;
    bool hover = false, dragging = false;
    float startThreshold = 0.0f, startRatioNorm = 0.0f;
    static constexpr float lo = -60.0f, hi = 0.0f;
    juce::Rectangle<float> plotArea() const;
    void rebuild (juce::Rectangle<float>);
};

//==============================================================================
/** Input, gain change and output meters: peak bars with hold and readouts, on a shared dB scale. */
class MeterView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MeterView (StCompProcessor&);
    void tick (double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    StCompProcessor& proc;
    st::LiveLevels live;
    std::array<float, 2> in { -120.0f, -120.0f }, out { -120.0f, -120.0f };
    float gr = 0.0f, inHold = -120.0f, outHold = -120.0f, grHold = 0.0f;
    double inHoldT = 0.0, outHoldT = 0.0, grHoldT = 0.0;
};

//==============================================================================
/** The side-chain filter: the detector's spectrum (before the filter) with the filter response on top and three
    handles: low cut (drag sideways), bell (drag anywhere, wheel = Q) and high cut. Double-click a handle to turn
    it off. */
class SidechainDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SidechainDisplay (StCompProcessor&);
    void tick (double dt);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    StCompProcessor& proc;
    st::SpectrumAnalyzer analyzer;
    SidechainSettings last;
    juce::Path response;
    int hover = -1, dragging = -1;   // 0 = low cut, 1 = bell, 2 = high cut
    static constexpr float minHz = 20.0f, maxHz = 20000.0f, minDb = -90.0f, maxDb = 0.0f, eqRange = 30.0f;

    juce::Rectangle<float> plot() const;
    float hzToX (float hz) const;
    float xToHz (float x) const;
    float eqDbToY (float db) const;
    float yToEqDb (float y) const;
    juce::Rectangle<float> handle (int which) const;
    int handleAt (juce::Point<float>) const;
};

} // namespace stcomp
