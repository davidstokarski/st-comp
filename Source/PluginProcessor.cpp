#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "ui/Strip.h"
#include "ui/Style.h"

using namespace stcomp;

namespace
{
    constexpr int kChunk = 64;   // parameter update granularity (samples)
}

void StCompProcessor::Refs::bind (juce::AudioProcessorValueTreeState& s)
{
    style.bind (s, ids::style);             inGain.bind (s, ids::inputGain);
    threshold.bind (s, ids::threshold);     ratio.bind (s, ids::ratio);
    knee.bind (s, ids::knee);               range.bind (s, ids::range);
    attack.bind (s, ids::attack);           release.bind (s, ids::release);
    autoRelease.bind (s, ids::autoRelease); hold.bind (s, ids::hold);
    lookahead.bind (s, ids::lookahead);     character.bind (s, ids::character);
    drive.bind (s, ids::drive);             mix.bind (s, ids::mix);
    outGain.bind (s, ids::outputGain);      autoGain.bind (s, ids::autoGain);
    link.bind (s, ids::stereoLink);         channelMode.bind (s, ids::channelMode);
    scExt.bind (s, ids::scExternal);        scListen.bind (s, ids::scListen);
    scGain.bind (s, ids::scGain);           scLowCut.bind (s, ids::scLowCut);
    scHighCut.bind (s, ids::scHighCut);     scBellFreq.bind (s, ids::scBellFreq);
    scBellGain.bind (s, ids::scBellGain);   scBellQ.bind (s, ids::scBellQ);
    quality.bind (s, ids::quality);         bypass.bind (s, ids::bypass);
}

StCompProcessor::StCompProcessor()
    : ProcessorBase (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                      .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false),
                     createParameterLayout())
{
    refs.bind (apvts);
    registerFactoryPresets (presets);
    stcomp::style::applyTheme();                   // graphite with an emerald accent
    st::Knob::setDoubleClickEntersValue (true);    // Pro-style: double-click types a value, Cmd-click resets
    apvts.addParameterListener (ids::quality, this);
    apvts.addParameterListener (ids::lookahead, this);
}

StCompProcessor::~StCompProcessor()
{
    unregisterInstance();   // first: views of this instance (elsewhere) still use its members
    apvts.removeParameterListener (ids::quality, this);
    apvts.removeParameterListener (ids::lookahead, this);
    cancelPendingUpdate();
}

bool StCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void StCompProcessor::applyPendingChanges()
{
    if (! prepared) return;
    const int q = juce::jlimit (0, 3, refs.quality.getInt());
    if (q != engine.getQuality())
    {
        // re-preparing the colour stage allocates: never on the audio thread
        suspendProcessing (true);
        engine.setQuality (q);
        suspendProcessing (false);
    }
    setLatencySamples (engine.latencyFor (q, refs.lookahead.get()));
}

stcomp::CompSettings StCompProcessor::getCompSettings() const noexcept
{
    CompSettings c;
    c.style = juce::jlimit (0, kNumStyles - 1, refs.style.getInt());
    c.thresholdDb = refs.threshold.get();
    c.ratio = refs.ratio.get();
    c.kneeDb = refs.knee.get();
    c.rangeDb = refs.range.get();
    c.attackMs = refs.attack.get();
    c.releaseMs = refs.release.get();
    c.holdMs = refs.hold.get();
    c.autoRelease = refs.autoRelease.getBool();
    c.stereoLink = refs.link.get() * 0.01f;
    return c;
}

stcomp::SidechainSettings StCompProcessor::getSidechainSettings() const noexcept
{
    SidechainSettings s;
    s.gainDb = refs.scGain.get();
    s.lowCutHz = refs.scLowCut.get();
    s.highCutHz = refs.scHighCut.get();
    s.bellHz = refs.scBellFreq.get();
    s.bellGainDb = refs.scBellGain.get();
    s.bellQ = refs.scBellQ.get();
    return s;
}

void StCompProcessor::updateEngineSettings() noexcept
{
    EngineSettings s;
    s.comp = getCompSettings();
    s.inputDb = refs.inGain.get();
    s.outputDb = refs.outGain.get();
    s.autoGain = refs.autoGain.getBool();
    s.character = juce::jlimit (0, 5, refs.character.getInt());
    s.drive = refs.drive.get() * 0.01f;
    s.mix = refs.mix.get() * 0.01f;
    s.lookaheadMs = refs.lookahead.get();
    s.channelMode = juce::jlimit (0, 5, refs.channelMode.getInt());
    s.sidechain = getSidechainSettings();
    s.externalSidechain = refs.scExt.getBool();
    s.listen = refs.scListen.getBool();
    s.bypass = refs.bypass.getBool();
    engine.setSettings (s);
}

void StCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepareShell (sampleRate);
    sidechainAnalyzer.prepare (sampleRate);
    updateEngineSettings();
    engine.setQuality (juce::jlimit (0, 3, refs.quality.getInt()));
    engine.prepare (sampleRate, kChunk, juce::jlimit (1, 2, getMainBusNumOutputChannels()));
    prepared = true;
    setLatencySamples (engine.getLatencySamples());
    juce::ignoreUnused (samplesPerBlock);
}

void StCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();

    auto main = getBusBuffer (buffer, false, 0);
    for (int c = getMainBusNumInputChannels(); c < main.getNumChannels(); ++c) main.clear (c, 0, total);
    const int channels = juce::jmin (2, main.getNumChannels());
    if (channels == 0) return;

    // (a buffer that refers to the host's channels: no copy, no allocation)
    const bool hasSc = getBusCount (true) > 1 && getChannelCountOfBus (true, 1) > 0;
    auto sc = hasSc ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();

    inputMeter.push (main.getArrayOfReadPointers(), channels, total);
    preAnalyzer.push (main.getArrayOfReadPointers(), channels, total);
    if (refs.scExt.getBool() && hasSc)
        sidechainAnalyzer.push (sc.getArrayOfReadPointers(), juce::jmin (2, sc.getNumChannels()), total);
    else
        sidechainAnalyzer.push (main.getArrayOfReadPointers(), channels, total);

    for (int start = 0; start < total; start += kChunk)
    {
        const int n = juce::jmin (kChunk, total - start);
        float* ptrs[2] = { main.getWritePointer (0, start), channels > 1 ? main.getWritePointer (1, start) : nullptr };
        juce::AudioBuffer<float> chunk (ptrs, channels, n);

        juce::AudioBuffer<float> scChunk;
        if (hasSc)
        {
            float* sp[2] = { sc.getWritePointer (0, start), sc.getNumChannels() > 1 ? sc.getWritePointer (1, start) : nullptr };
            scChunk.setDataToReferTo (sp, juce::jmin (2, sc.getNumChannels()), n);
        }

        updateEngineSettings();
        engine.process (chunk, hasSc ? &scChunk : nullptr, n);
    }

    outputMeter.push (main.getArrayOfReadPointers(), channels, total);
    postAnalyzer.push (main.getArrayOfReadPointers(), channels, total);
}

//==============================================================================
std::unique_ptr<juce::Component> StCompProcessor::createStripView()
{
    return std::make_unique<stcomp::CompStrip> (*this);
}

int StCompProcessor::getStripWidth (int height) const
{
    return stcomp::CompStrip::widthFor (height);
}

juce::AudioProcessorEditor* StCompProcessor::createEditor()
{
    return new StCompEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StCompProcessor();
}
