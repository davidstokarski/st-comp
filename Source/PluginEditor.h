#pragma once
#include "PluginProcessor.h"
#include "ui/Display.h"
#include "ui/Panels.h"

class StCompEditor : public st::EditorBase
{
public:
    explicit StCompEditor (StCompProcessor&);
    ~StCompEditor() override;

    static constexpr int designWidth = 1040, designHeight = 690;
    static constexpr int displayY = 44, displayH = 456, metersW = 96, panelY = 504, footerHeight = 40, kneeSize = 164;

    void openMultiView() { if (multiView == nullptr) toggleMultiView(); }
    st::MultiView* getMultiView() noexcept { return multiView.get(); }
    void openSidechain() { if (sidechain == nullptr) toggleSidechain(); }
    stcomp::SidechainPopover* getSidechainPopover() noexcept { return sidechain.get(); }
    stcomp::ControlPanel& getControlPanel() noexcept { return panel; }
    /** Recording / tests: stops the display clock; frames are then advanced with stepFrame (deterministic). */
    void driveFramesManually() { clock.reset(); }
    void stepFrame (double dt) { onFrame (dt); }

private:
    StCompProcessor& proc;
    stcomp::LevelDisplay display;
    stcomp::MeterView meters;
    stcomp::KneeView knee;
    stcomp::ControlPanel panel;
    stcomp::FooterBar footer;
    std::unique_ptr<stcomp::SidechainPopover> sidechain;
    st::Animated sidechainFade;
    std::unique_ptr<st::MultiView> multiView;
    std::unique_ptr<st::FrameClock> clock;
    juce::Image backdrop;
    float backdropScale = 0.0f;

    void layout (juce::Rectangle<int> body) override;
    void paintBackground (juce::Graphics&, juce::Rectangle<int> area) override;
    void toggleMultiView();
    void toggleSidechain();
    void closeSidechain();
    void placeSidechain();
    void setDisplay (float rangeDb, float seconds);
    void onFrame (double dt);
};
