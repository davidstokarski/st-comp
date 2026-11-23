#include "FactoryPresets.h"
#include "Params.h"

namespace stcomp
{

namespace
{
    using Values = std::vector<std::pair<juce::String, float>>;

    /** A style with the most common controls, then extra overrides. */
    Values preset (Style s, float threshold, float ratio, float attack, float release,
                   std::initializer_list<std::pair<juce::String, float>> more = {})
    {
        Values v { { ids::style, (float) (int) s }, { ids::threshold, threshold }, { ids::ratio, ratio },
                   { ids::attack, attack }, { ids::release, release } };
        for (auto& p : more) v.push_back (p);
        return v;
    }

    float ch (Character c) { return (float) (int) c; }
    float mode (ChannelMode m) { return (float) (int) m; }
}

void registerFactoryPresets (st::PresetManager& pm)
{
    using namespace ids;
    auto add = [&pm] (const char* name, const char* category, const Values& v) { pm.addFactoryPreset (name, category, v); };

    add ("Init", "Basics", {});
    add ("Gentle Leveller", "Basics", preset (Style::clean, -20.0f, 2.0f, 15.0f, 150.0f, { { knee, 12.0f }, { autoGain, 1.0f } }));
    add ("Peak Catcher", "Basics", preset (Style::clean, -8.0f, 20.0f, 0.1f, 60.0f, { { knee, 2.0f }, { lookahead, 2.0f } }));

    // mix bus / mastering
    add ("Mix Bus Glue", "Mix Bus", preset (Style::glue, -16.0f, 2.0f, 30.0f, 200.0f,
                                            { { knee, 6.0f }, { autoRelease, 1.0f }, { range, 6.0f }, { scLowCut, 90.0f }, { autoGain, 1.0f } }));
    add ("Mastering Touch", "Mix Bus", preset (Style::mastering, -14.0f, 1.5f, 20.0f, 250.0f,
                                               { { knee, 10.0f }, { range, 4.0f }, { lookahead, 5.0f }, { scLowCut, 60.0f }, { autoGain, 1.0f } }));
    add ("Tube Bus Warmth", "Mix Bus", preset (Style::variMu, -18.0f, 2.0f, 25.0f, 300.0f,
                                               { { character, ch (Character::tube) }, { drive, 15.0f }, { autoGain, 1.0f } }));
    add ("M/S Width Control", "Mix Bus", preset (Style::mastering, -24.0f, 3.0f, 10.0f, 200.0f,
                                                 { { channelMode, mode (ChannelMode::side) }, { range, 6.0f }, { autoGain, 0.0f } }));

    // vocals
    add ("Vocal Upfront", "Vocals", preset (Style::vocal, -22.0f, 3.5f, 5.0f, 120.0f, { { autoGain, 1.0f }, { scLowCut, 120.0f } }));
    add ("Vocal Opto Smooth", "Vocals", preset (Style::opto, -24.0f, 3.0f, 10.0f, 500.0f, { { autoGain, 1.0f } }));
    add ("Vocal LA-76 Chain", "Vocals", preset (Style::la76, -26.0f, 4.0f, 0.8f, 80.0f,
                                                { { character, ch (Character::tube) }, { drive, 12.0f }, { autoGain, 1.0f } }));
    add ("De-Ess (Side-Chain Bell)", "Vocals", preset (Style::clean, -30.0f, 6.0f, 0.5f, 60.0f,
                                                       { { scBellFreq, 6500.0f }, { scBellGain, 18.0f }, { scBellQ, 2.0f },
                                                         { scLowCut, 3000.0f }, { knee, 3.0f }, { range, 8.0f } }));

    // drums
    add ("Drum Bus Glue", "Drums", preset (Style::glue, -18.0f, 4.0f, 10.0f, 200.0f,
                                           { { autoRelease, 1.0f }, { scLowCut, 90.0f }, { range, 12.0f }, { autoGain, 1.0f } }));
    add ("Kick Punch", "Drums", preset (Style::punch, -20.0f, 4.0f, 12.0f, 90.0f, { { knee, 3.0f }, { autoGain, 1.0f } }));
    add ("Snare Crack", "Drums", preset (Style::fet, -24.0f, 4.0f, 1.5f, 70.0f, { { knee, 3.0f }, { autoGain, 1.0f } }));
    add ("Room Smash", "Drums", preset (Style::fetAll, -30.0f, 20.0f, 0.1f, 60.0f,
                                        { { character, ch (Character::diode) }, { drive, 25.0f }, { mix, 60.0f }, { autoGain, 1.0f } }));
    add ("Parallel Crush", "Drums", preset (Style::fetAll, -34.0f, 20.0f, 0.05f, 50.0f,
                                            { { character, ch (Character::tape) }, { drive, 35.0f }, { mix, 35.0f }, { autoGain, 1.0f } }));

    // bass / instruments
    add ("Bass Even", "Bass", preset (Style::opto, -22.0f, 4.0f, 10.0f, 300.0f, { { autoGain, 1.0f } }));
    add ("Bass Grip", "Bass", preset (Style::diode, -20.0f, 4.0f, 3.0f, 120.0f,
                                      { { character, ch (Character::transformer) }, { drive, 25.0f }, { autoGain, 1.0f } }));
    add ("Acoustic Guitar", "Instruments", preset (Style::opto, -20.0f, 2.5f, 10.0f, 400.0f, { { scLowCut, 100.0f }, { autoGain, 1.0f } }));
    add ("Piano Sustain", "Instruments", preset (Style::variMu, -24.0f, 3.0f, 20.0f, 500.0f, { { autoGain, 1.0f } }));
    add ("Strings Bloom", "Instruments", preset (Style::upward, -36.0f, 2.0f, 10.0f, 150.0f, { { range, 10.0f } }));

    // creative
    add ("Sidechain Pump (External)", "Creative", preset (Style::pump, -30.0f, 8.0f, 0.5f, 180.0f,
                                                          { { scExternal, 1.0f }, { range, 18.0f }, { knee, 0.0f } }));
    add ("Self Pump", "Creative", preset (Style::pump, -26.0f, 10.0f, 1.0f, 220.0f, { { scLowCut, 20.0f }, { range, 14.0f } }));
    add ("Upward Detail", "Creative", preset (Style::upward, -30.0f, 3.0f, 5.0f, 120.0f, { { range, 12.0f } }));
    add ("Bright Grit", "Creative", preset (Style::fet, -22.0f, 6.0f, 0.5f, 100.0f,
                                            { { character, ch (Character::bright) }, { drive, 40.0f }, { autoGain, 1.0f } }));
}

} // namespace stcomp
