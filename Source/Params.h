#pragma once
#include "Styles.h"

namespace stcomp
{

/** Parameter IDs. Permanent: never rename them after a release (sessions and presets depend on them). */
namespace ids
{
    inline const juce::String style         = "style";
    inline const juce::String inputGain     = "input_gain";
    inline const juce::String threshold     = "threshold";
    inline const juce::String ratio         = "ratio";
    inline const juce::String knee          = "knee";
    inline const juce::String range         = "range";
    inline const juce::String attack        = "attack";
    inline const juce::String release       = "release";
    inline const juce::String autoRelease   = "auto_release";
    inline const juce::String hold          = "hold";
    inline const juce::String lookahead     = "lookahead";
    inline const juce::String character     = "character";
    inline const juce::String drive         = "drive";
    inline const juce::String mix           = "mix";
    inline const juce::String outputGain    = "output_gain";
    inline const juce::String autoGain      = "auto_gain";
    inline const juce::String stereoLink    = "stereo_link";
    inline const juce::String channelMode   = "channel_mode";
    inline const juce::String scExternal    = "sc_external";
    inline const juce::String scListen      = "sc_listen";
    inline const juce::String scGain        = "sc_gain";
    inline const juce::String scLowCut      = "sc_low_cut";
    inline const juce::String scHighCut     = "sc_high_cut";
    inline const juce::String scBellFreq    = "sc_bell_freq";
    inline const juce::String scBellGain    = "sc_bell_gain";
    inline const juce::String scBellQ       = "sc_bell_q";
    inline const juce::String quality       = "oversampling";
    inline const juce::String bypass        = "bypass";
}

/** Limits shared by the DSP, the UI and the tests. */
namespace limits
{
    constexpr float thresholdMin = -60.0f, thresholdMax = 0.0f;
    constexpr float ratioInfinite = 99.5f;        // ratio >= this = infinity
    constexpr float rangeMax = 60.0f;
    constexpr float rangeUnlimited = 59.5f;       // range >= this = no limit
    constexpr float lookaheadMaxMs = 20.0f;
    constexpr float lowCutOff = 20.5f;            // side-chain low cut <= this = off
    constexpr float highCutOff = 19500.0f;        // side-chain high cut >= this = off
}

/** Channel processing modes (`channel_mode`). Order is permanent. */
enum class ChannelMode { stereo, midSide, mid, side, left, right };
juce::StringArray getChannelModeNames();

/** Character choices (`character`). Order is permanent. */
enum class Character { clean, tube, diode, tape, transformer, bright };
juce::StringArray getCharacterNames();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace stcomp
