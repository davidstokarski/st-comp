#include "Params.h"

namespace stcomp
{

using namespace st::params;

juce::StringArray getChannelModeNames() { return { "Stereo", "Mid/Side", "Mid", "Side", "Left", "Right" }; }
juce::StringArray getCharacterNames()   { return { "Clean", "Tube", "Diode", "Tape", "Transformer", "Bright" }; }

namespace
{
    const juce::String infinity (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));

    juce::AudioParameterFloatAttributes offBelow (float offAt)
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([offAt] (float v, int) { return v <= offAt ? juce::String ("Off") : formatHz (v); })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.containsIgnoreCase ("off") ? 0.0f : parseHz (s); })
            .withLabel ("Hz");
    }

    juce::AudioParameterFloatAttributes offAbove (float offAt)
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([offAt] (float v, int) { return v >= offAt ? juce::String ("Off") : formatHz (v); })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.containsIgnoreCase ("off") ? 20000.0f : parseHz (s); })
            .withLabel ("Hz");
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto rangeAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return v >= limits::rangeUnlimited ? infinity : formatDb (v, 1, true, false); })
        .withValueFromStringFunction ([] (const juce::String& s) { return s.containsIgnoreCase ("inf") ? limits::rangeMax : std::abs (parseDb (s)); })
        .withLabel ("dB");

    auto qAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1); })
        .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); });

    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("comp", "Compressor", " | ");
        g->addChild (makeChoice (ids::style, "Style", getStyleNames(), (int) Style::clean));
        g->addChild (makeFloat (ids::threshold, "Threshold", linear (limits::thresholdMin, limits::thresholdMax, 0.01f), -18.0f, dbAttributes()));
        g->addChild (makeFloat (ids::ratio, "Ratio", skewed (1.0f, 100.0f, 6.0f, 0.01f), 4.0f, ratioAttributes()));
        g->addChild (makeFloat (ids::knee, "Knee", linear (0.0f, 36.0f, 0.01f), 6.0f, dbAttributes()));
        g->addChild (makeFloat (ids::range, "Range", linear (0.0f, limits::rangeMax, 0.01f), limits::rangeMax, rangeAttributes));
        g->addChild (makeFloat (ids::attack, "Attack", skewed (0.01f, 250.0f, 5.0f, 0.001f), 10.0f, msAttributes()));
        g->addChild (makeFloat (ids::release, "Release", skewed (5.0f, 2500.0f, 150.0f, 0.01f), 100.0f, msAttributes()));
        g->addChild (makeBool (ids::autoRelease, "Auto Release", false));
        g->addChild (makeFloat (ids::hold, "Hold", skewed (0.0f, 500.0f, 40.0f, 0.01f), 0.0f, msAttributes()));
        g->addChild (makeFloat (ids::lookahead, "Lookahead", linear (0.0f, limits::lookaheadMaxMs, 0.01f), 0.0f, msAttributes()));
        layout.add (std::move (g));
    }
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("sc", "Side Chain", " | ");
        g->addChild (makeFloat (ids::stereoLink, "Stereo Link", linear (0.0f, 100.0f, 0.01f), 100.0f, percentAttributes()));
        g->addChild (makeChoice (ids::channelMode, "Channels", getChannelModeNames(), (int) ChannelMode::stereo));
        g->addChild (makeBool (ids::scExternal, "External Side Chain", false));
        g->addChild (makeBool (ids::scListen, "Side-Chain Listen", false));
        g->addChild (makeFloat (ids::scGain, "Side-Chain Gain", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
        g->addChild (makeFloat (ids::scLowCut, "Side-Chain Low Cut", logarithmic (20.0f, 2000.0f), 20.0f, offBelow (limits::lowCutOff)));
        g->addChild (makeFloat (ids::scHighCut, "Side-Chain High Cut", logarithmic (1000.0f, 20000.0f), 20000.0f, offAbove (limits::highCutOff)));
        g->addChild (makeFloat (ids::scBellFreq, "Side-Chain Bell Freq", logarithmic (40.0f, 16000.0f), 2500.0f, hzAttributes()));
        g->addChild (makeFloat (ids::scBellGain, "Side-Chain Bell Gain", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
        g->addChild (makeFloat (ids::scBellQ, "Side-Chain Bell Q", logarithmic (0.2f, 8.0f), 1.0f, qAttributes));
        layout.add (std::move (g));
    }
    {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("output", "Output", " | ");
        g->addChild (makeFloat (ids::inputGain, "Input Gain", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
        g->addChild (makeChoice (ids::character, "Character", getCharacterNames(), (int) Character::clean));
        g->addChild (makeFloat (ids::drive, "Drive", linear (0.0f, 100.0f, 0.01f), 30.0f, percentAttributes()));
        g->addChild (makeFloat (ids::mix, "Mix", linear (0.0f, 100.0f, 0.01f), 100.0f, percentAttributes()));
        g->addChild (makeFloat (ids::outputGain, "Output Gain", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
        g->addChild (makeBool (ids::autoGain, "Auto Gain", false));
        g->addChild (makeChoice (ids::quality, "Oversampling", { "Off", "2x", "4x", "8x" }, 0));
        g->addChild (makeBool (ids::bypass, "Bypass", false));
        layout.add (std::move (g));
    }
    return layout;
}

} // namespace stcomp
