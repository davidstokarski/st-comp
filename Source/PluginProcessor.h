#pragma once
#include "Params.h"
#include "dsp/Engine.h"

class StCompProcessor : public st::ProcessorBase,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    StCompProcessor();
    ~StCompProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    /** ST Comp's module in the multiview (st::MultiView). */
    std::unique_ptr<juce::Component> createStripView() override;
    int getStripWidth (int height) const override;
    double getTailLengthSeconds() const override { return 0.0; }

    stcomp::Engine& getEngine() noexcept { return engine; }
    const stcomp::Engine& getEngine() const noexcept { return engine; }

    int getStyle() const noexcept { return refs.style.getInt(); }
    bool isUpward() const noexcept { return stcomp::getStyleInfo (getStyle()).upward; }
    /** Compressor settings as the parameters say now (knee display, tests). */
    stcomp::CompSettings getCompSettings() const noexcept;
    stcomp::SidechainSettings getSidechainSettings() const noexcept;
    double getSampleRateOrDefault() const noexcept { return getSampleRate() > 0.0 ? getSampleRate() : 48000.0; }

    /** What the detector hears before the side-chain EQ (main input, or the side-chain input when External is on). */
    st::AnalyzerSource sidechainAnalyzer;

    /** Applies a pending oversampling / lookahead change now (normally done asynchronously on the message thread). */
    void applyPendingChanges();

private:
    stcomp::Engine engine;
    struct Refs
    {
        st::params::Ref style, inGain, threshold, ratio, knee, range, attack, release, autoRelease, hold, lookahead,
                        character, drive, mix, outGain, autoGain, link, channelMode, scExt, scListen, scGain, scLowCut,
                        scHighCut, scBellFreq, scBellGain, scBellQ, quality, bypass;
        void bind (juce::AudioProcessorValueTreeState&);
    } refs;
    bool prepared = false;

    void updateEngineSettings() noexcept;
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { applyPendingChanges(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StCompProcessor)
};
