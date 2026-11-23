#include "Styles.h"

namespace stcomp
{

namespace
{
    // ---- stage builders --------------------------------------------------------------------------------------
    StageSpec feedForward (float rmsBlend, float rmsMs)
    {
        StageSpec s;
        s.active = true;
        s.rmsBlend = rmsBlend;
        s.rmsMs = rmsMs;
        return s;
    }

    StageSpec feedBack (float rmsBlend, float rmsMs)
    {
        auto s = feedForward (rmsBlend, rmsMs);
        s.feedback = true;
        return s;
    }

    StageSpec rising (StageSpec s, float floor, float width)
    {
        s.curve = Curve::rising;
        s.curveFloor = floor;
        s.curveWidth = width;
        return s;
    }

    StyleInfo style (const char* name, const char* character, const char* detail, Family f, StageSpec a,
                     Harmonics h, float colourBase, float colourPerDb, float colourMax, StageSpec b = {})
    {
        StyleInfo i { name, character, detail, f, { a, b } };
        i.harmonics = h;
        i.colourBase = colourBase;
        i.colourPerDb = colourPerDb;
        i.colourMax = colourMax;
        return i;
    }

    // ---- the styles ------------------------------------------------------------------------------------------
    // Research notes (why each design behaves the way it does) are in the README, "The styles".
    std::vector<StyleInfo> makeStyles()
    {
        std::vector<StyleInfo> v;

        // Clean: the reference. Peak detector, the curve exactly as set, no colour at all.
        v.push_back (style ("Clean", "Transparent, precise, does exactly what the knobs say",
                            "Feed-forward digital compressor with a peak detector and the exact curve you set. "
                            "No harmonics, no programme dependence: the reference everything else is measured against.",
                            Family::modern, feedForward (0.0f, 1.0f), Harmonics::none, 0.0f, 0.0f, 0.0f));

        // Glue: feed-forward VCA bus compressor. Blended peak / RMS, Auto Release for the two-stage release.
        {
            auto s = feedForward (0.5f, 5.0f);
            v.push_back (style ("Glue", "Bus VCA glue, transients intact",
                                "Feed-forward VCA with a blended peak / RMS detector, the classic bus design. Low ratios, "
                                "slower attacks and Auto Release glue a mix without dulling it.",
                                Family::modern, s, Harmonics::odd, 0.01f, 0.003f, 0.08f));
        }

        // Mastering: slow RMS-weighted detector, programme-dependent release, smoothed gain, no colour.
        {
            auto s = feedForward (0.75f, 15.0f);
            s.kneeAdd = 4.0f;
            s.release = Release::dual;
            s.gainSmoothMs = 3.0f;
            s.minAttackMs = 0.5f;
            v.push_back (style ("Mastering", "Gentle, inaudible, low distortion",
                                "RMS-weighted detector, a softer knee, a programme-dependent release that is always on "
                                "and a smoothed gain trace: less modulation distortion, no colour. For the stereo bus.",
                                Family::modern, s, Harmonics::none, 0.0f, 0.0f, 0.0f));
        }

        // Vocal: fast catch with a ratio that firms up as the voice gets louder; smooth programme release.
        {
            auto s = rising (feedForward (0.4f, 4.0f), 0.6f, 12.0f);
            s.kneeAdd = 6.0f;
            s.release = Release::dual;
            s.gainSmoothMs = 1.0f;
            v.push_back (style ("Vocal", "Upfront, even, never grabby",
                                "Mostly-peak detector with a wide knee and a ratio that firms up the louder the voice "
                                "gets, plus a programme-dependent release: consonants stay, levels stay put.",
                                Family::modern, s, Harmonics::even, 0.01f, 0.004f, 0.08f));
        }

        // Punch: true RMS VCA, programme-dependent attack and a constant-rate release.
        {
            auto s = feedForward (1.0f, 8.0f);
            s.programmeAttack = true;
            s.release = Release::linear;
            v.push_back (style ("Punch", "Tight RMS punch, steady lows",
                                "Feed-forward VCA with a true RMS detector, programme-dependent attack (bigger jumps are "
                                "caught faster) and a constant-rate release in dB per second. Drums, bass, anything that thumps.",
                                Family::modern, s, Harmonics::odd, 0.02f, 0.005f, 0.12f));
        }

        // Pump: accelerating release so the gain swells back audibly.
        {
            auto s = feedForward (0.3f, 3.0f);
            s.release = Release::pump;
            v.push_back (style ("Pump", "Audible, rhythmic breathing",
                                "Peak-leaning detector and an accelerating release: the gain holds while compression is "
                                "deep and snaps back near 0 dB, so the swell is part of the sound. Side-chain it from a kick.",
                                Family::modern, s, Harmonics::odd, 0.02f, 0.004f, 0.12f));
        }

        // Upward: raises everything below the threshold instead of lowering what's above.
        {
            auto s = feedForward (0.6f, 10.0f);
            auto i = style ("Upward", "Brings up detail and tails",
                            "Upward compression: levels below the threshold are raised (Ratio sets how much, Range how far), "
                            "levels above it are left alone. Silence stays silent: there is no gain below -80 dBFS.",
                            Family::modern, s, Harmonics::none, 0.0f, 0.0f, 0.0f);
            i.upward = true;
            v.push_back (i);
        }

        // FET: fast feedback FET, harmonics growing with gain reduction.
        {
            auto s = feedBack (0.0f, 1.0f);
            s.minAttackMs = 0.02f;
            v.push_back (style ("FET", "Fast, bright, aggressive bite",
                                "Feedback FET design: attack down to 20 microseconds, a harder knee at high ratios and "
                                "harmonics that grow with gain reduction. Grabs transients and adds edge.",
                                Family::vintage, s, Harmonics::even, 0.05f, 0.012f, 0.4f));
        }

        // FET All: every ratio engaged.
        {
            auto s = feedBack (0.0f, 1.0f);
            s.curve = Curve::overshoot;
            s.kneeScale = 0.5f;
            s.minAttackMs = 0.1f;
            s.releaseScale = 0.6f;
            v.push_back (style ("FET All", "Smashed, distorted, pumping",
                                "The FET with every ratio engaged: the curve over-compresses (louder input gets quieter), "
                                "release speeds up and distortion rises sharply. Room mics, parallel smash.",
                                Family::vintage, s, Harmonics::even, 0.16f, 0.02f, 0.6f));
        }

        // Opto: optical feedback cell.
        {
            auto s = rising (feedBack (0.6f, 10.0f), 0.55f, 18.0f);
            s.kneeAdd = 8.0f;
            s.minAttackMs = 2.0f;
            s.release = Release::opto;
            v.push_back (style ("Opto", "Smooth, slow, programme-dependent",
                                "Optical feedback cell: soft wide knee, a ratio that grows with level, ~10 ms average "
                                "attack and a two-stage release whose tail lengthens the longer it has been compressing.",
                                Family::vintage, s, Harmonics::even, 0.04f, 0.006f, 0.25f));
        }

        // LA-76: optical leveller into a FET peak stage (the classic chain in one box).
        {
            auto opto = rising (feedBack (0.6f, 10.0f), 0.55f, 18.0f);
            opto.kneeAdd = 8.0f;
            opto.ratioPower = 0.5f;           // the leveller takes the square root of the ratio...
            opto.ratioMin = 1.5f;
            opto.ratioMax = 4.0f;
            opto.fixedAttackMs = 10.0f;       // ...with its own optical timing
            opto.fixedReleaseMs = 400.0f;
            opto.release = Release::opto;

            auto fet = feedBack (0.0f, 1.0f);
            fet.thresholdOffsetDb = 6.0f;     // the FET only touches what pokes 6 dB above the threshold
            fet.minAttackMs = 0.02f;

            v.push_back (style ("LA-76", "Levelled body, caught peaks",
                                "Two stages in series, like the classic opto-into-FET chain: an optical leveller (square "
                                "root of the ratio, its own slow timing) evens out the body, then a FET at the threshold "
                                "+6 dB catches the peaks with the Attack and Release you set.",
                                Family::vintage, opto, Harmonics::even, 0.05f, 0.01f, 0.35f, fet));
        }

        // Vari-Mu: variable-mu tube.
        {
            auto s = rising (feedBack (0.6f, 6.0f), 0.25f, 24.0f);
            s.kneeAdd = 14.0f;
            s.minAttackMs = 1.0f;
            s.gainSmoothMs = 1.0f;
            v.push_back (style ("Vari-Mu", "Warm tube density",
                                "Variable-mu tube feedback compressor: the ratio rises gradually with gain reduction "
                                "(very soft knee) and the tube adds even harmonics as it works. Mix bus, vocals, strings.",
                                Family::vintage, s, Harmonics::even, 0.06f, 0.014f, 0.4f));
        }

        // Diode: diode-bridge feedback compressor.
        {
            auto s = feedBack (0.3f, 3.0f);
            s.kneeAdd = 4.0f;
            s.minAttackMs = 0.1f;
            s.release = Release::dual;
            v.push_back (style ("Diode", "Gritty, thick, forward",
                                "Diode-bridge feedback design: medium-fast, a programme-dependent release and the odd, "
                                "slightly asymmetric grit of the bridge. Thickens drums, bass and guitars.",
                                Family::vintage, s, Harmonics::diode, 0.05f, 0.012f, 0.4f));
        }

        jassert ((int) v.size() == kNumStyles);
        return v;
    }
}

const StyleInfo& getStyleInfo (int s)
{
    static const auto styles = makeStyles();
    return styles[(size_t) juce::jlimit (0, kNumStyles - 1, s)];
}

juce::StringArray getStyleNames()
{
    juce::StringArray s;
    for (int i = 0; i < kNumStyles; ++i) s.add (getStyleInfo (i).name);
    return s;
}

juce::PopupMenu buildStyleMenu (int current)
{
    juce::PopupMenu m;
    for (auto [family, title] : { std::pair<Family, const char*> { Family::modern, "Modern" }, { Family::vintage, "Vintage" } })
    {
        m.addSectionHeader (title);
        for (int i = 0; i < kNumStyles; ++i)
        {
            const auto& s = getStyleInfo (i);
            if (s.family != family) continue;
            m.addItem (juce::PopupMenu::Item (juce::String (s.name) + juce::String (juce::CharPointer_UTF8 ("  \xe2\x80\x94  ")) + s.character)
                           .setID (i + 1).setTicked (current == i));
        }
    }
    return m;
}

} // namespace stcomp
