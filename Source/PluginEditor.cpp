#include "PluginEditor.h"

using namespace stcomp;

StCompEditor::StCompEditor (StCompProcessor& p)
    : EditorBase (p, designWidth, designHeight),
      proc (p), display (p), meters (p), knee (p), panel (p), footer (p)
{
    header.setProductName ("Comp");
    header.setMultiViewCallback ([this] { toggleMultiView(); });

    auto& content = getContent();
    for (juce::Component* c : { (juce::Component*) &display, (juce::Component*) &meters, (juce::Component*) &knee,
                                (juce::Component*) &panel, (juce::Component*) &footer })
        content.addAndMakeVisible (c);

    panel.setBufferedToImage (true);
    footer.setBufferedToImage (true);
    header.setBufferedToImage (true);
    header.toFront (false);

    panel.onSidechain = [this] { toggleSidechain(); };
    footer.onDisplayChanged = [this] (float r, float s) { setDisplay (r, s); };
    setDisplay ((float) (double) proc.uiState.getProperty ("displayRange", 48.0), (float) (double) proc.uiState.getProperty ("displaySeconds", 4.0));

    clock = std::make_unique<st::FrameClock> (display, [this] (double dt) { onFrame (dt); });
    relayout();
    if ((bool) proc.uiState.getProperty ("sidechainOpen", false)) toggleSidechain();
}

StCompEditor::~StCompEditor()
{
    clock.reset();
    sidechain.reset();
    multiView.reset();
}

void StCompEditor::onFrame (double dt)
{
    display.tick (dt);
    knee.tick (dt);
    meters.tick (dt);
    if (sidechain != nullptr)
    {
        sidechain->tick (dt);
        if (sidechainFade.tick (dt, 16.0f)) placeSidechain();
    }
}

void StCompEditor::setDisplay (float rangeDb, float seconds)
{
    proc.uiState.setProperty ("displayRange", rangeDb, nullptr);
    proc.uiState.setProperty ("displaySeconds", seconds, nullptr);
    display.setRangeDb (rangeDb);
    display.setSeconds (seconds);
    footer.setDisplay (rangeDb, seconds);
}

void StCompEditor::toggleMultiView()
{
    if (multiView != nullptr) { multiView.reset(); return; }
    st::MultiView::Options o;
    o.leftmostKind = "ST Comp";
    multiView = std::make_unique<st::MultiView> (proc, o);
    multiView->onClose = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StCompEditor> (this)]
        {
            if (safe != nullptr && safe->multiView != nullptr) safe->toggleMultiView();
        });
    };
    getContent().addAndMakeVisible (*multiView);
    multiView->setBounds (getContent().getLocalBounds());
    multiView->toFront (true);
}

void StCompEditor::closeSidechain()
{
    sidechain.reset();
    proc.uiState.setProperty ("sidechainOpen", false, nullptr);
}

void StCompEditor::toggleSidechain()
{
    if (sidechain != nullptr) { closeSidechain(); return; }
    sidechain = std::make_unique<SidechainPopover> (proc);
    sidechain->onClose = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StCompEditor> (this)]
        {
            if (safe != nullptr) safe->closeSidechain();
        });
    };
    proc.uiState.setProperty ("sidechainOpen", true, nullptr);
    getContent().addAndMakeVisible (*sidechain);
    sidechain->toFront (true);
    if (multiView != nullptr) multiView->toFront (false);
    sidechainFade.snap (0.0f);
    sidechainFade.setTarget (1.0f);
    placeSidechain();
}

void StCompEditor::placeSidechain()
{
    if (sidechain == nullptr) return;
    // the panel sits just above the control panel's glass bar, centred on the Side Chain button; fades + slides up
    const auto size = st::Popover::totalSize (SidechainPopover::panelWidth, SidechainPopover::panelHeight);
    const auto anchor = panel.getSidechainAnchor().translated (panel.getX(), panel.getY());
    const int m = st::Popover::margin;
    const int x = juce::jlimit (-m + 8, designWidth - metersW - size.x + m, anchor.getCentreX() - size.x / 2);   // clear of the meters
    const int y = panelY + 14 - size.y + m + juce::roundToInt ((1.0f - sidechainFade.current) * 10.0f);
    sidechain->setBounds (x, y, size.x, size.y);
    sidechain->setAlpha (sidechainFade.current);
}

void StCompEditor::layout (juce::Rectangle<int>)
{
    const auto all = getContent().getLocalBounds();
    display.setBounds (0, displayY, designWidth - metersW, displayH);
    meters.setBounds (designWidth - metersW + 4, displayY + 10, metersW - 16, displayH - 20);
    const auto plot = display.getPlot().translated ((float) display.getX(), (float) display.getY());
    knee.setBounds ((int) plot.getX() + 6, (int) plot.getBottom() - kneeSize - 10, kneeSize, kneeSize);
    panel.setBounds (12, panelY, designWidth - 24, ControlPanel::height);
    footer.setBounds (all.withTop (designHeight - footerHeight));
    placeSidechain();
    if (multiView != nullptr) multiView->setBounds (all);
}

void StCompEditor::paintBackground (juce::Graphics& g, juce::Rectangle<int> area)
{
    // static: render once per physical scale, blit afterwards (it sits behind everything that animates)
    const float pixelScale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto full = getContent().getLocalBounds();
    if (backdrop.isNull() || std::abs (pixelScale - backdropScale) > 0.01f)
    {
        backdropScale = pixelScale;
        backdrop = juce::Image (juce::Image::RGB, juce::roundToInt ((float) full.getWidth() * pixelScale),
                                juce::roundToInt ((float) full.getHeight() * pixelScale), false);
        juce::Graphics bg (backdrop);
        bg.addTransform (juce::AffineTransform::scale (pixelScale));
        style::paintBackdrop (bg, full.toFloat());
        // a faded divider left of the meters
        const float mx = (float) (designWidth - metersW) + 0.5f;
        st::paint::fadedLine (bg, { mx, (float) displayY + 16.0f }, { mx, (float) (displayY + displayH) - 16.0f },
                              juce::Colours::white.withAlpha (0.05f), 1.0f, 0.2f);
    }
    g.drawImage (backdrop, area.toFloat(), juce::RectanglePlacement::stretchToFit);
}
