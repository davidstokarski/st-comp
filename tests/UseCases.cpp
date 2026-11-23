// Use-case videos of the real editor (website), on the shell's video kit (st_shell/testing/st_video.h): a
// synthesized mix plays through the processor while a scripted pointer works the UI.
//   StCompPluginTests --usecases <dir> [scene ...]     (--video renders the looping demo clip's frames)
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <st_shell/testing/st_video.h>

using st::video::Director;
using st::video::Music;
using st::video::Scene;

namespace
{
void loadPreset (juce::AudioProcessor& ap, const juce::String& name)
{
    auto& p = dynamic_cast<StCompProcessor&> (ap);
    for (int i = 0; i < (int) p.presets.getPresets().size(); ++i)
        if (p.presets.getPresets()[(size_t) i].name == name) p.presets.loadPreset (i);
}

juce::Point<float> knob (Director& d, const juce::String& id) { return d.controlCentre (id); }

std::vector<Scene> scenes()
{
    std::vector<Scene> v;

    // 1. Even out a vocal that jumps between loud and soft phrases
    v.push_back ({ "vocal-level", 15.0, [] (juce::AudioProcessor& p, Music& m)
    {
        loadPreset (p, "Vocal Upfront");
        st::video::setParam (p, stcomp::ids::threshold, -12.0f);
        m.vocal = 1.0f; m.sib = 0.4f; m.pad = 0.3f; m.dynamics = 1.0f;
    }, [] (Director& dir)
    {
        dir.wait (1.4);
        dir.move ([&dir] { return knob (dir, stcomp::ids::threshold); }, 1.0);
        dir.dragBy ({ 0.0f, 60.0f }, 1.6);                         // threshold down: the loud phrases come down
        dir.wait (0.8);
        dir.move ([&dir] { return knob (dir, stcomp::ids::ratio); }, 0.9);
        dir.dragBy ({ 0.0f, -35.0f }, 1.0);                        // a little more ratio
        dir.move ([&dir] { return knob (dir, stcomp::ids::release); }, 0.9);
        dir.dragBy ({ 0.0f, -30.0f }, 1.0);                        // slower release: smoother
        dir.wait (3.0);
    } });

    // 2. Glue a drum bus, then blend the squashed signal back in (parallel)
    v.push_back ({ "drum-glue", 15.0, [] (juce::AudioProcessor& p, Music& m)
    {
        loadPreset (p, "Drum Bus Glue");
        m.drums = 1.0f; m.rooms = 0.5f; m.bass = 0.7f;
    }, [] (Director& dir)
    {
        dir.wait (1.4);
        dir.move ([&dir] { return knob (dir, stcomp::ids::threshold); }, 1.0);
        dir.dragBy ({ 0.0f, 55.0f }, 1.4);
        dir.move ([&dir] { return knob (dir, stcomp::ids::attack); }, 0.9);
        dir.dragBy ({ 0.0f, -35.0f }, 1.0);                        // let the transients through
        dir.move ([&dir] { return knob (dir, stcomp::ids::mix); }, 1.0);
        dir.dragBy ({ 0.0f, 50.0f }, 1.2);                         // parallel: blend with the dry kit
        dir.wait (3.2);
    } });

    // 3. A de-esser: the side chain hears the "s" band louder, so only the sibilance is compressed
    v.push_back ({ "deess", 15.0, [] (juce::AudioProcessor& p, Music& m)
    {
        loadPreset (p, "De-Ess (Side-Chain Bell)");
        st::video::setParam (p, stcomp::ids::scBellFreq, 3200.0f);
        m.vocal = 1.0f; m.sib = 1.6f; m.pad = 0.3f; m.bass = 0.25f;
    }, [] (Director& dir)
    {
        dir.wait (1.4);
        dir.move ([&dir] { return dir.centreOf (dir.findType<juce::Button> ([] (juce::Button& b) { return b.getButtonText() == "Side Chain"; })); }, 1.0);
        dir.click();                                               // open the side-chain filter
        dir.wait (0.7);
        dir.move ([&dir] { return knob (dir, stcomp::ids::scBellFreq); }, 1.0);
        dir.dragBy ({ 0.0f, -34.0f }, 1.4);                        // move the bell up onto the "s" (~7 kHz)
        dir.move ([&dir] { return knob (dir, stcomp::ids::scBellGain); }, 0.9);
        dir.dragBy ({ 0.0f, -10.0f }, 0.8);                        // and make it hear them a little louder
        dir.wait (3.6);
    } });
    return v;
}
}

int runUseCaseVideos (int argc, char** argv);
int runUseCaseVideos (int argc, char** argv)
{
    return st::video::run (argc, argv, "st-comp", [] { return std::make_unique<StCompProcessor>(); }, scenes(), "--usecases");
}
