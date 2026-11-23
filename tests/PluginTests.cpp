// ST Comp whole-plugin tests: every style's curve and timing, lookahead, hold, nulls, latency, channel modes, the
// side chain (filter, listen, external input), upward compression, processor stress, state, presets, the
// multiview module, and editor snapshots (--snapshot <dir>).
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Strip.h"
#include "ui/Display.h"
#if JUCE_MAC
 #include <AudioToolbox/AudioToolbox.h>
#endif

using namespace stcomp;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}

void setParam (StCompProcessor& p, const juce::String& id, float plain)
{
    auto& prm = p.param (id);
    prm.setValueNotifyingHost (prm.convertTo0to1 (plain));
}

/** Music-ish test signal: drum-like bursts over a sustained tone and a noise floor. */
float runSignal (StCompProcessor& p, int blocks, int blockSize, bool& finite, int channels = 2, float level = 0.3f, float floor = 0.2f)
{
    juce::AudioBuffer<float> buf (channels, blockSize);
    static juce::Random rng (1);
    static int64_t clock = 0;   // keeps the pattern going across calls
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const int64_t n = clock + b * blockSize + i;
                const int k = (int) (n % 12000);
                const float hit = k < 2400 ? std::exp (-(float) k / 500.0f) : 0.0f;
                const float tone = 0.35f * std::sin ((float) (n % 48000) * 0.0172f) * (0.6f + 0.4f * std::sin ((float) (n % 96000) * 0.000065f));
                buf.setSample (c, i, level * ((rng.nextFloat() * 2.0f - 1.0f) * (floor + hit) + 0.5f * std::sin ((float) n * 0.01f) * hit + tone));
            }
        p.processBlock (buf, midi);
        for (int c = 0; c < juce::jmin (2, channels); ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                peak = juce::jmax (peak, std::abs (v));
            }
    }
    clock += (int64_t) blocks * blockSize;
    return peak;
}

CompSettings baseSettings (stcomp::Style s)
{
    CompSettings c;
    c.style = (int) s;
    c.thresholdDb = -24.0f;
    c.ratio = 4.0f;
    c.kneeDb = 6.0f;
    c.rangeDb = 60.0f;
    c.attackMs = 5.0f;
    c.releaseMs = 100.0f;
    return c;
}

/** Runs the compressor on a constant-magnitude signal (square wave) and returns the settled gain (dB). */
float settledGain (Compressor& c, float levelDb, int samples)
{
    std::vector<float> det (64);
    const float a = st::dsp::dbToGain (levelDb);
    float g = 0.0f;
    int done = 0;
    while (done < samples)
    {
        const int n = juce::jmin (64, samples - done);
        for (int i = 0; i < n; ++i) det[(size_t) i] = ((done + i) & 1) ? a : -a;
        const float* ptrs[2] = { det.data(), det.data() };
        c.computeGain (ptrs, 2, n);
        g = c.getGainDb (0)[n - 1];
        done += n;
    }
    return g;
}

const char* styleName (stcomp::Style s) { return getStyleInfo ((int) s).name; }
bool isLinearStyle (stcomp::Style s) { return s == stcomp::Style::clean || s == stcomp::Style::glue || s == stcomp::Style::mastering || s == stcomp::Style::punch
                                       || s == stcomp::Style::pump || s == stcomp::Style::fet || s == stcomp::Style::diode; }
}

//==============================================================================
static void testCurves()
{
    std::cout << "Styles: steady state on the static curve\n";
    for (int si = 0; si < kNumStyles; ++si)
    {
        const auto s = (stcomp::Style) si;
        const bool up = getStyleInfo (si).upward;
        for (bool autoRel : { false, true })
            for (float level : { -48.0f, -36.0f, -27.0f, -18.0f, -6.0f })
            {
                Compressor c;
                c.prepare (48000.0, 64);
                auto cs = baseSettings (s);
                cs.autoRelease = autoRel;
                c.setSettings (cs);
                const float g = settledGain (c, level, 48000 * 4);
                const float want = Compressor::staticGainDb (level, cs);
                check (std::abs (g - want) < 0.6f, juce::String (styleName (s)) + (autoRel ? " auto" : "") + " at " + juce::String (level)
                                                   + " dB: settled " + juce::String (g, 2) + " vs curve " + juce::String (want, 2));
            }

        auto cs = baseSettings (s);
        if (up)
        {
            // upward: raises below the threshold, leaves above alone, never lifts silence
            const float below = Compressor::staticGainDb (-40.0f, cs), above = Compressor::staticGainDb (-6.0f, cs);
            check (below > 6.0f, "Upward raises quiet levels (" + juce::String (below, 2) + " dB at -40)");
            check (std::abs (above) < 0.01f, "Upward leaves loud levels alone");
            check (Compressor::staticGainDb (-110.0f, cs) < 0.05f, "Upward leaves silence silent");
            check (std::abs ((Compressor::staticGainDb (-40.0f, cs) - Compressor::staticGainDb (-34.0f, cs)) / 6.0f - 0.75f) < 0.03f,
                   "Upward 4:1 slope below the knee");
            cs.rangeDb = 6.0f;
            check (Compressor::staticGainDb (-50.0f, cs) < 6.01f && Compressor::staticGainDb (-50.0f, cs) > 5.0f, "Upward: range limits the lift");
            check (Compressor::autoMakeupDb (baseSettings (s)) < 0.0f, "Upward auto gain takes level back");
            continue;
        }

        check (std::abs (Compressor::staticGainDb (-50.0f, cs)) < 0.01f, juce::String (styleName (s)) + ": unity below threshold");
        const float g12 = Compressor::staticGainDb (-12.0f, cs), g0 = Compressor::staticGainDb (0.0f, cs);
        const float slope = (g0 - g12) / 12.0f;
        if (s == stcomp::Style::fetAll)
            check (g0 < g12 - 9.0f, "FET All over-compresses (louder input, more than 1:1 reduction)");
        else if (isLinearStyle (s))
            check (std::abs (slope + 0.75f) < 0.03f, juce::String (styleName (s)) + ": 4:1 slope (" + juce::String (slope, 3) + ")");
        else
            check (slope < -0.4f && slope > -0.95f, juce::String (styleName (s)) + ": programme-dependent slope (" + juce::String (slope, 3) + ")");

        cs.rangeDb = 6.0f;
        check (Compressor::staticGainDb (0.0f, cs) > -6.01f && Compressor::staticGainDb (0.0f, cs) < -5.0f, juce::String (styleName (s)) + ": range limits");
        check (Compressor::autoMakeupDb (baseSettings (s)) > 1.0f && Compressor::autoMakeupDb (baseSettings (s)) < 15.0f,
               juce::String (styleName (s)) + ": auto make-up in a sensible range (" + juce::String (Compressor::autoMakeupDb (baseSettings (s)), 2) + " dB)");
    }

    // LA-76: two stages. The opto leveller works gently from the threshold; above threshold + 6 dB the FET joins
    // and the curve gets steeper.
    {
        auto la = baseSettings (stcomp::Style::la76);
        const float gLo = Compressor::staticGainDb (-21.0f, la);
        const float lowSlope = (Compressor::staticGainDb (-20.0f, la) - Compressor::staticGainDb (-24.0f, la)) / 4.0f;
        const float highSlope = (Compressor::staticGainDb (0.0f, la) - Compressor::staticGainDb (-6.0f, la)) / 6.0f;
        check (gLo < -0.2f, "LA-76: the leveller works just above the threshold (" + juce::String (gLo, 2) + ")");
        check (highSlope < lowSlope - 0.2f, "LA-76: the FET stage steepens the curve on peaks (" + juce::String (lowSlope, 2)
                                            + " -> " + juce::String (highSlope, 2) + " dB/dB)");
    }

    // infinite ratio on a feed-forward style is a brickwall above the knee
    {
        auto cs = baseSettings (stcomp::Style::clean);
        cs.ratio = 100.0f;
        cs.kneeDb = 0.0f;
        check (std::abs (Compressor::staticGainDb (-6.0f, cs) + 18.0f) < 0.01f, "Clean: infinite ratio holds the threshold");
    }
}

static void testTiming()
{
    std::cout << "Styles: timing, hold, release types\n";
    auto timeTo = [] (stcomp::Style s, float attackMs, float fraction)
    {
        Compressor c;
        c.prepare (48000.0, 64);
        auto cs = baseSettings (s);
        cs.attackMs = attackMs;
        c.setSettings (cs);
        const float target = Compressor::staticGainDb (-6.0f, cs);
        std::vector<float> det (1, st::dsp::dbToGain (-6.0f));
        for (int i = 0; i < 48000; ++i)
        {
            const float* ptrs[2] = { det.data(), det.data() };
            det[0] = (i & 1) ? std::abs (det[0]) : -std::abs (det[0]);
            c.computeGain (ptrs, 2, 1);
            if (c.getGainDb (0)[0] <= target * fraction) return (float) i / 48.0f;
        }
        return 1000.0f;
    };
    const float fet = timeTo (stcomp::Style::fet, 0.05f, 0.8f), opto = timeTo (stcomp::Style::opto, 0.05f, 0.8f), clean = timeTo (stcomp::Style::clean, 10.0f, 0.8f);
    check (fet < 1.0f, "FET grabs within 1 ms (" + juce::String (fet, 2) + " ms)");
    check (opto > 2.0f, "Opto can't be instant (" + juce::String (opto, 2) + " ms)");
    check (clean > 8.0f && clean < 40.0f, "Clean 10 ms attack (" + juce::String (clean, 2) + " ms to 80%)");

    // recovery time (ms) from a settled burst until the gain is back above `untilDb`
    auto recovery = [] (CompSettings cs, int burstSamples, float untilDb)
    {
        Compressor c;
        c.prepare (48000.0, 64);
        c.setSettings (cs);
        settledGain (c, -6.0f, burstSamples);
        int n = 0;
        while (n < 48000 * 20)
        {
            if (settledGain (c, -80.0f, 16) > untilDb) break;
            n += 16;
        }
        return (float) n / 48.0f;
    };

    // Punch: constant-rate release (release = time to recover 10 dB)
    {
        Compressor c;
        c.prepare (48000.0, 64);
        auto cs = baseSettings (stcomp::Style::punch);
        cs.releaseMs = 100.0f;
        cs.ratio = 100.0f;
        c.setSettings (cs);
        const float g0 = settledGain (c, -6.0f, 48000);
        const float g1 = settledGain (c, -80.0f, 4800);   // 100 ms later
        check (g0 < -15.0f && std::abs ((g1 - g0) - 10.0f) < 1.5f, "Punch recovers ~10 dB per release time (" + juce::String (g1 - g0, 2) + " dB)");
    }

    // Auto release: a short burst recovers faster than sustained compression
    {
        auto cs = baseSettings (stcomp::Style::glue);
        cs.autoRelease = true;
        cs.attackMs = 1.0f;
        const float shortHit = recovery (cs, 480, -1.0f), longHit = recovery (cs, 48000 * 3, -1.0f);
        check (longHit > shortHit * 1.5f, "Auto release: sustained compression recovers slower (" + juce::String (shortHit, 0) + " vs " + juce::String (longHit, 0) + " ms)");
    }

    // Opto: the tail lengthens after long compression
    {
        auto cs = baseSettings (stcomp::Style::opto);
        cs.releaseMs = 400.0f;
        const float a = recovery (cs, 4800, -1.0f), b = recovery (cs, 48000 * 5, -1.0f);
        check (b > a * 1.3f, "Opto memory: longer tail after long compression (" + juce::String (a, 0) + " vs " + juce::String (b, 0) + " ms)");
    }

    // Hold: gain reduction waits before releasing
    {
        auto cs = baseSettings (stcomp::Style::clean);
        cs.attackMs = 0.5f;
        cs.releaseMs = 50.0f;
        const float without = recovery (cs, 4800, -1.0f);
        cs.holdMs = 200.0f;
        const float with = recovery (cs, 4800, -1.0f);
        check (std::abs ((with - without) - 200.0f) < 15.0f, "Hold delays the release by the hold time (" + juce::String (without, 0) + " -> " + juce::String (with, 0) + " ms)");
    }

    // Pump: the release accelerates (slow while deep, fast near 0 dB); a classic release decelerates
    {
        auto shapeRatio = [&] (stcomp::Style s)
        {
            auto cs = baseSettings (s);
            cs.ratio = 100.0f;
            cs.releaseMs = 100.0f;
            cs.attackMs = 0.5f;
            const float toHalf = recovery (cs, 9600, -9.0f), toZero = recovery (cs, 9600, -0.5f);
            return toHalf / juce::jmax (1.0f, toZero - toHalf);
        };
        const float pump = shapeRatio (stcomp::Style::pump), classic = shapeRatio (stcomp::Style::clean);
        check (pump > classic * 2.0f, "Pump release accelerates (first half / rest: " + juce::String (pump, 2) + " vs classic " + juce::String (classic, 2) + ")");
    }
}

/** Feeds the engine a block-wise signal and returns the processed output (both channels). */
static std::vector<std::array<float, 2>> runEngine (Engine& e, int total, const std::function<std::array<float, 2> (int)>& in,
                                                    const std::function<std::array<float, 2> (int)>& sc = {})
{
    std::vector<std::array<float, 2>> out;
    juce::AudioBuffer<float> buf (2, 64), side (2, 64);
    for (int start = 0; start < total; start += 64)
    {
        const int n = juce::jmin (64, total - start);
        for (int i = 0; i < n; ++i)
        {
            const auto v = in (start + i);
            buf.setSample (0, i, v[0]);
            buf.setSample (1, i, v[1]);
            if (sc) { const auto s = sc (start + i); side.setSample (0, i, s[0]); side.setSample (1, i, s[1]); }
        }
        e.process (buf, sc ? &side : nullptr, n);
        for (int i = 0; i < n; ++i) out.push_back ({ buf.getSample (0, i), buf.getSample (1, i) });
    }
    return out;
}

static void testEngine()
{
    std::cout << "Engine: nulls, latency, lookahead, channel modes, side chain, upward\n";
    const double sr = 48000.0;

    // ---- bypass and Mix 0% null against the input delayed by the latency (every oversampling, with lookahead)
    for (int q = 0; q < 4; ++q)
        for (float la : { 0.0f, 5.0f })
            for (int mode = 0; mode < 2; ++mode)
            {
                Engine e;
                e.setQuality (q);
                e.prepare (sr, 64, 2);
                EngineSettings s;
                s.comp = baseSettings (stcomp::Style::fetAll);
                s.character = (int) Character::tube;
                s.drive = 0.5f;
                s.lookaheadMs = la;
                s.bypass = mode == 0;
                s.mix = mode == 0 ? 1.0f : 0.0f;
                e.setSettings (s);
                e.reset();
                const int L = e.getLatencySamples();
                check (L == e.latencyFor (q, la), "latencyFor matches the running latency");
                juce::Random rng (3);
                std::vector<float> in;
                auto out = runEngine (e, 64 * 200, [&] (int) { const float v = (rng.nextFloat() * 2.0f - 1.0f) * 0.5f; in.push_back (v); return std::array<float, 2> { v, v }; });
                float maxErr = 0.0f;
                for (int i = 640; i < (int) out.size(); ++i)
                    if (i - L >= 0) maxErr = juce::jmax (maxErr, std::abs (out[(size_t) i][0] - in[(size_t) (i - L)]));
                check (maxErr < 1.0e-5f, juce::String (mode == 0 ? "Bypass" : "Mix 0%") + " nulls (oversampling " + juce::String (q) + ", lookahead "
                                         + juce::String (la) + " ms, err " + juce::String (maxErr, 8) + ", latency " + juce::String (L) + ")");
            }

    // ---- lookahead: a sudden peak is caught before it arrives
    auto stepPeak = [&] (float lookaheadMs)
    {
        Engine e;
        e.prepare (sr, 64, 2);
        EngineSettings s;
        s.comp = baseSettings (stcomp::Style::clean);
        s.comp.thresholdDb = -20.0f;
        s.comp.ratio = 100.0f;
        s.comp.kneeDb = 0.0f;
        s.comp.attackMs = lookaheadMs > 0.0f ? lookaheadMs * 0.3f : 1.0f;
        s.lookaheadMs = lookaheadMs;
        e.setSettings (s);
        e.reset();
        const int step = 4800;
        auto out = runEngine (e, 9600, [&] (int i) { const float v = i >= step ? 0.9f * ((i & 1) ? 1.0f : -1.0f) : 0.0f; return std::array<float, 2> { v, v }; });
        float peak = 0.0f;
        for (auto& o : out) peak = juce::jmax (peak, std::abs (o[0]));
        return st::dsp::gainToDb (peak);
    };
    const float noLa = stepPeak (0.0f), withLa = stepPeak (5.0f);
    check (noLa > -6.0f && withLa < noLa - 8.0f, "Lookahead catches the step (" + juce::String (noLa, 1) + " -> " + juce::String (withLa, 1) + " dBFS)");

    // ---- channel modes: only the chosen channels are detected and processed
    auto modeOut = [&] (ChannelMode m, std::function<std::array<float, 2> (int)> in)
    {
        Engine e;
        e.prepare (sr, 64, 2);
        EngineSettings s;
        s.comp = baseSettings (stcomp::Style::clean);
        s.comp.thresholdDb = -30.0f;
        s.channelMode = (int) m;
        e.setSettings (s);
        e.reset();
        auto out = runEngine (e, 48000, in);
        std::array<float, 2> peak {};
        for (size_t i = 24000; i < out.size(); ++i)
            for (int c = 0; c < 2; ++c) peak[(size_t) c] = juce::jmax (peak[(size_t) c], std::abs (out[i][(size_t) c]));
        return peak;
    };
    auto sine = [] (int i) { return 0.8f * std::sin ((float) i * 0.05f); };
    {
        auto side = modeOut (ChannelMode::mid, [&] (int i) { return std::array<float, 2> { sine (i), -sine (i) }; });
        check (std::abs (side[0] - 0.8f) < 0.01f, "Mid mode leaves a pure side signal alone (" + juce::String (side[0], 3) + ")");
        auto mid = modeOut (ChannelMode::side, [&] (int i) { return std::array<float, 2> { sine (i), sine (i) }; });
        check (std::abs (mid[0] - 0.8f) < 0.01f, "Side mode leaves a pure mid signal alone");
        auto left = modeOut (ChannelMode::left, [&] (int i) { return std::array<float, 2> { sine (i), sine (i) }; });
        check (left[0] < 0.4f && std::abs (left[1] - 0.8f) < 0.01f, "Left mode compresses the left only (" + juce::String (left[0], 3) + ", " + juce::String (left[1], 3) + ")");
        auto ms = modeOut (ChannelMode::midSide, [&] (int i) { return std::array<float, 2> { sine (i), sine (i) }; });
        check (ms[0] < 0.4f && std::abs (ms[0] - ms[1]) < 0.01f, "Mid/Side mode compresses a mid signal on both sides equally");
    }

    // ---- side-chain filter: response, and a low cut keeps bass from driving the detector
    {
        SidechainSettings s;
        s.lowCutHz = 200.0f;
        s.bellHz = 3000.0f;
        s.bellGainDb = 12.0f;
        s.bellQ = 1.0f;
        check (SidechainEq::magnitudeDb (40.0f, s, sr) < -24.0f, "SC low cut: 24 dB/oct below the corner");
        check (std::abs (SidechainEq::magnitudeDb (3000.0f, s, sr) - 12.0f) < 0.5f, "SC bell gain at its frequency");
        check (std::abs (SidechainEq::magnitudeDb (800.0f, s, sr)) < 3.0f, "SC filter near flat in between");
        s.highCutHz = 5000.0f;
        check (SidechainEq::magnitudeDb (16000.0f, s, sr) < -12.0f, "SC high cut above the corner");

        auto bassGr = [&] (float lowCut)
        {
            Engine e;
            e.prepare (sr, 64, 2);
            EngineSettings es;
            es.comp = baseSettings (stcomp::Style::clean);
            es.comp.thresholdDb = -30.0f;
            es.sidechain.lowCutHz = lowCut;
            e.setSettings (es);
            e.reset();
            runEngine (e, 24000, [&] (int i) { const float v = 0.7f * std::sin ((float) i * 2.0f * juce::MathConstants<float>::pi * 50.0f / 48000.0f); return std::array<float, 2> { v, v }; });
            return e.grDb.load();
        };
        const float off = bassGr (20.0f), on = bassGr (400.0f);
        check (off < -10.0f && on > off + 10.0f, "SC low cut stops bass from driving the compressor (" + juce::String (off, 1) + " -> " + juce::String (on, 1) + " dB)");
    }

    // ---- listen: the output is what the detector hears
    {
        Engine e;
        e.prepare (sr, 64, 2);
        EngineSettings es;
        es.listen = true;
        es.sidechain.lowCutHz = 1000.0f;
        e.setSettings (es);
        e.reset();
        auto out = runEngine (e, 24000, [&] (int i) { const float v = 0.7f * std::sin ((float) i * 2.0f * juce::MathConstants<float>::pi * 50.0f / 48000.0f); return std::array<float, 2> { v, v }; });
        float peak = 0.0f;
        for (size_t i = 12000; i < out.size(); ++i) peak = juce::jmax (peak, std::abs (out[i][0]));
        check (peak < 0.01f, "Listen plays the filtered side chain (50 Hz through a 1 kHz low cut: " + juce::String (peak, 4) + ")");
    }

    // ---- external side chain ducks a quiet signal
    {
        Engine e;
        e.prepare (sr, 64, 2);
        EngineSettings es;
        es.comp = baseSettings (stcomp::Style::pump);
        es.comp.thresholdDb = -30.0f;
        es.externalSidechain = true;
        e.setSettings (es);
        e.reset();
        runEngine (e, 24000, [&] (int i) { const float v = 0.05f * std::sin ((float) i * 0.1f); return std::array<float, 2> { v, v }; },
                   [&] (int i) { const float v = 0.8f * std::sin ((float) i * 0.05f); return std::array<float, 2> { v, v }; });
        check (e.grDb.load() < -10.0f, "External side chain ducks the main signal (" + juce::String (e.grDb.load(), 1) + " dB)");
    }

    // ---- upward: quiet passages come up, silence stays silent
    {
        auto run = [&] (float level)
        {
            Engine e;
            e.prepare (sr, 64, 2);
            EngineSettings es;
            es.comp = baseSettings (stcomp::Style::upward);
            es.comp.thresholdDb = -20.0f;
            es.comp.ratio = 2.0f;
            es.comp.rangeDb = 12.0f;
            e.setSettings (es);
            e.reset();
            auto out = runEngine (e, 48000, [&] (int i) { const float v = level * std::sin ((float) i * 0.05f); return std::array<float, 2> { v, v }; });
            float peak = 0.0f;
            for (size_t i = 24000; i < out.size(); ++i) peak = juce::jmax (peak, std::abs (out[i][0]));
            return st::dsp::gainToDb (peak) - st::dsp::gainToDb (level);
        };
        const float quiet = run (0.01f), silent = run (1.0e-6f);
        check (quiet > 6.0f, "Upward lifts a quiet signal (" + juce::String (quiet, 2) + " dB)");
        check (silent < 0.5f, "Upward doesn't lift silence (" + juce::String (silent, 2) + " dB)");
    }
}

static void testProcessor()
{
    std::cout << "Processor: stress, latency, state, presets, side chain\n";
    StCompProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    bool finite = true;

    for (int q = 0; q < 4; ++q)
    {
        setParam (p, ids::quality, (float) q);
        setParam (p, ids::lookahead, q == 2 ? 7.5f : 0.0f);
        p.applyPendingChanges();
        runSignal (p, 1, 512, finite);
        check (p.getLatencySamples() == p.getEngine().getLatencySamples(), "latency reported, oversampling " + juce::String (q)
               + " (" + juce::String (p.getLatencySamples()) + " vs " + juce::String (p.getEngine().getLatencySamples()) + ")");
        for (int s = 0; s < kNumStyles; ++s)
        {
            setParam (p, ids::style, (float) s);
            setParam (p, ids::character, (float) ((s + q) % 6));
            setParam (p, ids::drive, (float) ((s * 17 + q * 31) % 100));
            setParam (p, ids::channelMode, (float) ((s + q * 2) % 6));
            setParam (p, ids::hold, s % 3 == 0 ? 80.0f : 0.0f);
            setParam (p, ids::autoRelease, (float) (s % 2));
            setParam (p, ids::autoGain, 1.0f);
            setParam (p, ids::inputGain, 12.0f);
            const float peak = runSignal (p, 10, 512, finite);
            check (peak < 8.0f, "bounded: q=" + juce::String (q) + " style=" + getStyleInfo (s).name + " peak=" + juce::String (peak));
        }
    }
    setParam (p, ids::inputGain, 0.0f);
    setParam (p, ids::lookahead, 0.0f);
    setParam (p, ids::quality, 0.0f);
    p.applyPendingChanges();
    check (finite, "all output finite");

    for (int bs : { 1, 7, 63, 64, 65, 333, 2048 })
    {
        p.prepareToPlay (44100.0, bs);
        const float peak = runSignal (p, juce::jmax (4, 4096 / bs), bs, finite);
        check (finite && peak < 8.0f, "block size " + juce::String (bs));
    }
    p.prepareToPlay (48000.0, 512);

    // mono layout
    {
        StCompProcessor m;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::mono());
        layout.inputBuses.add (juce::AudioChannelSet::disabled());
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        check (m.setBusesLayout (layout), "mono layout accepted");
        m.prepareToPlay (48000.0, 512);
        setParam (m, ids::style, (float) stcomp::Style::la76);
        setParam (m, ids::channelMode, (float) ChannelMode::side);
        const float peak = runSignal (m, 20, 512, finite, 1);
        check (finite && peak < 8.0f, "mono runs (any channel mode)");
    }

    // compression actually happens and is metered
    setParam (p, ids::style, (float) stcomp::Style::fet);
    setParam (p, ids::threshold, -30.0f);
    runSignal (p, 40, 512, finite, 2, 0.9f);
    {
        float deepest = 0.0f;
        const auto w = p.getEngine().history.getWritten();
        for (uint32_t i = w > 200 ? w - 200 : 0; i < w; ++i) deepest = juce::jmin (deepest, p.getEngine().history.read (i).grDb);
        check (deepest < -3.0f, "FET compresses loud passages and the history shows it (" + juce::String (deepest, 1) + " dB)");
    }

    // state round trip
    setParam (p, ids::ratio, 7.5f);
    setParam (p, ids::style, (float) stcomp::Style::variMu);
    setParam (p, ids::scBellGain, 9.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    StCompProcessor p2;
    p2.setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (p2.apvts.getRawParameterValue (ids::ratio)->load() - 7.5f) < 0.05f, "state: ratio");
    check (p2.getStyle() == (int) stcomp::Style::variMu, "state: style");
    check (std::abs (p2.getSidechainSettings().bellGainDb - 9.0f) < 0.05f, "state: side-chain bell");

    // presets load and run
    for (int i = 0; i < (int) p.presets.getPresets().size(); ++i)
    {
        p.presets.loadPreset (i);
        const float peak = runSignal (p, 6, 512, finite);
        check (finite && peak < 8.0f, "preset " + p.presets.getPresets()[(size_t) i].name);
    }
    p.presets.loadPreset (0);

    // external side chain through the processor's buses
    {
        StCompProcessor sc;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        check (sc.setBusesLayout (layout), "side-chain layout accepted");
        sc.setRateAndBufferSizeDetails (48000.0, 512);
        sc.prepareToPlay (48000.0, 512);
        setParam (sc, ids::scExternal, 1.0f);
        setParam (sc, ids::threshold, -30.0f);
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        float deepest = 0.0f;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                buf.setSample (0, i, 0.01f * std::sin ((float) i * 0.1f));
                buf.setSample (1, i, 0.01f * std::sin ((float) i * 0.1f));
                buf.setSample (2, i, 0.8f * std::sin ((float) i * 0.05f));
                buf.setSample (3, i, 0.8f * std::sin ((float) i * 0.05f));
            }
            sc.processBlock (buf, midi);
            deepest = juce::jmin (deepest, sc.getEngine().grDb.load());
        }
        check (deepest < -6.0f, "a loud side chain compresses a quiet main signal (" + juce::String (deepest, 1) + " dB)");
    }

    // CPU estimate: LA-76 (two feedback stages), 8x oversampling, drive, lookahead
    {
        setParam (p, ids::style, (float) stcomp::Style::la76);
        setParam (p, ids::character, (float) Character::bright);
        setParam (p, ids::drive, 40.0f);
        setParam (p, ids::quality, 3.0f);
        setParam (p, ids::lookahead, 5.0f);
        p.applyPendingChanges();
        const int blocks = 400;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        runSignal (p, blocks, 512, finite);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "CPU (LA-76, 8x, Bright drive, lookahead): " << juce::String (100.0 * ms / (blocks * 512 / 48.0), 2) << "% of one core\n";
        setParam (p, ids::quality, 0.0f);
        setParam (p, ids::style, (float) stcomp::Style::clean);
        setParam (p, ids::character, 0.0f);
        p.applyPendingChanges();
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        runSignal (p, blocks, 512, finite);
        const auto ms1 = juce::Time::getMillisecondCounterHiRes() - t1;
        std::cout << "CPU (Clean, no oversampling): " << juce::String (100.0 * ms1 / (blocks * 512 / 48.0), 2) << "% of one core\n";
    }
}

#if JUCE_MAC
namespace
{
    AudioUnit loadStAu (OSType subType)
    {
        AudioComponentDescription desc {};
        desc.componentType = kAudioUnitType_Effect;
        desc.componentSubType = subType;
        desc.componentManufacturer = 'StAu';
        AudioComponent comp = AudioComponentFindNext (nullptr, &desc);
        if (comp == nullptr) return nullptr;
        AudioUnit au = nullptr;
        if (AudioComponentInstanceNew (comp, &au) != noErr) return nullptr;
        return au;
    }
    void setAuTrack (AudioUnit au, const char* track)
    {
        CFStringRef name = juce::String (track).toCFString();
        AudioUnitSetProperty (au, kAudioUnitProperty_ContextName, kAudioUnitScope_Global, 0, &name, sizeof (name));
        CFRelease (name);
    }
}
#endif

/** The editor (every popover opens / closes), and ST Comp's multiview module. */
static void testEditorAndMultiView()
{
    std::cout << "Editor and multiview\n";
    auto& reg = st::InstanceRegistry::get();
    auto vocal = std::make_unique<StCompProcessor>();
    auto bus = std::make_unique<StCompProcessor>();
    juce::AudioProcessor::TrackProperties tv, tb;
    tv.name = juce::String ("Lead Vocal");
    tb.name = juce::String ("Mix Bus");
    vocal->updateTrackProperties (tv);
    bus->updateTrackProperties (tb);
#if JUCE_MAC
    AudioUnit eq = loadStAu ('Steq');   // ST EQ on the vocal track: shown next to ST Comp
    if (eq != nullptr) setAuTrack (eq, "Lead Vocal");
#endif
    reg.flush();

    std::unique_ptr<juce::AudioProcessorEditor> ed (vocal->createEditor());
    ed->setSize (StCompEditor::designWidth, StCompEditor::designHeight);
    auto* e = dynamic_cast<StCompEditor*> (ed.get());
    check (e->getControlPanel().getNumKnobs() == 12, "control panel: twelve knobs");
    e->openSidechain();
    check (e->getSidechainPopover() != nullptr, "side-chain popover opens");
    {
        juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        juce::Graphics g (img);
        ed->paintEntireComponent (g, true);
    }

    e->openMultiView();
    auto* mv = e->getMultiView();
    mv->refresh();
    check (mv->getNumRows() >= 2, "multiview rows (" + juce::String (mv->getNumRows()) + ")");
#if JUCE_MAC
    if (eq != nullptr)
    {
        bool together = false;
        for (int r = 0; r < mv->getNumRows(); ++r)
        {
            const auto kinds = mv->getRowKinds (r);
            together |= kinds.size() == 2 && kinds[0] == "ST Comp" && kinds[1] == "ST EQ";
        }
        check (together, "the vocal row shows ST Comp first, then the track's ST EQ");
    }
#endif
    auto* strip = dynamic_cast<CompStrip*> (mv->getModuleComponent (bus->getInstanceUid()));
    check (strip != nullptr && strip->getNumKnobs() == 6, "ST Comp module: six knobs");
    if (strip != nullptr)
    {
        auto* k = strip->getKnob (0);
        const float before = bus->param (ids::threshold).getValue();
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto c = k->getLocalBounds().getCentre().toFloat();
        auto ev = [&] (juce::Point<float> pos, juce::ModifierKeys m, bool dragged)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (src, pos, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, k, k, now, c, now, 1, dragged);
        };
        const juce::ModifierKeys lb (juce::ModifierKeys::leftButtonModifier);
        k->mouseDown (ev (c, lb, false));
        for (int i = 1; i <= 10; ++i) k->mouseDrag (ev (c.translated (0.0f, -6.0f * (float) i), lb, true));
        k->mouseUp (ev (c.translated (0.0f, -60.0f), {}, true));
        check (bus->param (ids::threshold).getValue() > before + 0.05f, "editing the module edits that instance");
    }
    {
        juce::Image img (juce::Image::ARGB, mv->getWidth(), mv->getHeight(), true);
        juce::Graphics g (img);
        mv->paintEntireComponent (g, true);
    }
    const int rows = mv->getNumRows();
    bus.reset();
    reg.flush();
    check (mv->getNumRows() == rows - 1, "a removed instance leaves the multiview");
#if JUCE_MAC
    if (eq != nullptr) AudioComponentInstanceDispose (eq);
    reg.flush();
#endif
    ed.reset();
}

static int runTests()
{
    juce::ScopedJuceInitialiser_GUI gui;
    testCurves();
    testTiming();
    testEngine();
    testProcessor();
    testEditorAndMultiView();
    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILURES") << "\n";
    return failures == 0 ? 0 : 1;
}

//==============================================================================
namespace
{
/** Renders the editor after letting its timers run for a while (needs a running message loop). */
struct Snapshotter : juce::Timer
{
    struct Job { std::function<void()> setup; juce::String name; float scale = 1.0f;
                 std::function<void (juce::Component&)> afterOpen; int ticks = 70; };
    StCompProcessor& p;
    juce::File dir;
    std::vector<Job> jobs;
    size_t index = 0;
    int ticks = 0;
    bool opened = false;
    std::unique_ptr<juce::AudioProcessorEditor> ed;

    Snapshotter (StCompProcessor& proc, juce::File d) : p (proc), dir (d) {}

    void timerCallback() override
    {
        if (ed == nullptr)
        {
            if (index >= jobs.size()) { stopTimer(); juce::JUCEApplicationBase::quit(); return; }
            p.presets.loadPreset (0);
            p.uiState.setProperty ("sidechainOpen", false, nullptr);
            if (jobs[index].setup) jobs[index].setup();
            p.applyPendingChanges();
            bool f = true;
            runSignal (p, 220, 1024, f, 2, 0.55f, 0.04f);   // ~4.7 s, so the level history is full
            ed.reset (p.createEditor());
            ed->setSize (juce::roundToInt ((float) StCompEditor::designWidth * jobs[index].scale),
                         juce::roundToInt ((float) StCompEditor::designHeight * jobs[index].scale));
            ticks = 0;
            opened = false;
        }

        bool f = true;
        runSignal (p, 4, 256, f, 2, 0.55f, 0.04f);

        if (! opened && ticks > 6 && jobs[index].afterOpen) { jobs[index].afterOpen (*ed); opened = true; }

        if (++ticks > jobs[index].ticks)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto file = dir.getChildFile (jobs[index].name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (img, out);
            std::cout << "wrote " << file.getFullPathName() << "\n";
            ed.reset();
            ++index;
        }
    }
};

juce::File snapshotDir;

struct SnapshotApp : juce::JUCEApplicationBase
{
    std::unique_ptr<StCompProcessor> proc;
    std::vector<std::unique_ptr<StCompProcessor>> others;
    std::unique_ptr<Snapshotter> snap;
#if JUCE_MAC
    std::vector<AudioUnit> units;
#endif

    const juce::String getApplicationName() override { return "StCompSnapshots"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}
    void shutdown() override
    {
        snap.reset();
#if JUCE_MAC
        for (auto u : units) AudioComponentInstanceDispose (u);
#endif
        others.clear();
        proc.reset();
    }

    void initialise (const juce::String&) override
    {
        proc = std::make_unique<StCompProcessor>();
        auto& p = *proc;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        snapshotDir.createDirectory();

        auto preset = [&p] (const juce::String& name)
        {
            for (int i = 0; i < (int) p.presets.getPresets().size(); ++i)
                if (p.presets.getPresets()[(size_t) i].name == name) { p.presets.loadPreset (i); return; }
            std::cout << "no preset " << name << "\n";
        };

        snap = std::make_unique<Snapshotter> (p, snapshotDir);
        snap->jobs.push_back ({ [&p] { setParam (p, ids::threshold, -26.0f); }, "01_clean", 1.0f, {} });
        snap->jobs.push_back ({ [preset] { preset ("Snare Crack"); }, "02_fet", 1.0f, {} });
        snap->jobs.push_back ({ [preset] { preset ("Vocal LA-76 Chain"); }, "03_la76", 1.0f, {} });
        snap->jobs.push_back ({ [preset] { preset ("Bass Even"); }, "04_opto", 1.0f, {} });
        snap->jobs.push_back ({ [preset, &p] { preset ("Upward Detail"); setParam (p, ids::threshold, -14.0f); }, "05_upward", 1.0f, {} });
        snap->jobs.push_back ({ [preset] { preset ("De-Ess (Side-Chain Bell)"); }, "06_sidechain", 1.0f,
                                [] (juce::Component& ed) { if (auto* e = dynamic_cast<StCompEditor*> (&ed)) e->openSidechain(); }, 90 });
        snap->jobs.push_back ({ [preset] { preset ("Tube Bus Warmth"); }, "07_varimu_small", 0.75f, {} });
        snap->jobs.push_back ({ [preset] { preset ("Room Smash"); }, "08_fetall", 1.0f, {} });

        // the multiview: a few tracks with ST Comp (and ST EQ / ST Sat where installed)
        snap->jobs.push_back ({ [this, &p, preset]
        {
            juce::AudioProcessor::TrackProperties tp;
            tp.name = juce::String ("Lead Vocal");
            p.updateTrackProperties (tp);
            preset ("Vocal Upfront");
            const std::pair<const char*, const char*> tracks[] = { { "Mix Bus", "Mix Bus Glue" }, { "Bass", "Bass Grip" },
                                                                   { "Drums", "Drum Bus Glue" }, { "Piano", "Piano Sustain" } };
            for (auto& [name, presetName] : tracks)
            {
                auto o = std::make_unique<StCompProcessor>();
                o->setPlayConfigDetails (2, 2, 48000.0, 512);
                o->prepareToPlay (48000.0, 512);
                for (int i = 0; i < (int) o->presets.getPresets().size(); ++i)
                    if (o->presets.getPresets()[(size_t) i].name == juce::String (presetName)) o->presets.loadPreset (i);
                juce::AudioProcessor::TrackProperties t;
                t.name = juce::String (name);
                o->updateTrackProperties (t);
                bool f = true;
                runSignal (*o, 300, 512, f, 2, 0.9f, 0.04f);
                others.push_back (std::move (o));
            }
#if JUCE_MAC
            for (auto [sub, track] : { std::pair<OSType, const char*> { 'Steq', "Lead Vocal" }, { 'Stst', "Bass" }, { 'Steq', "Drums" } })
                if (auto au = loadStAu (sub)) { setAuTrack (au, track); units.push_back (au); }
#endif
            st::InstanceRegistry::get().flush();
        }, "09_multiview", 1.0f, [] (juce::Component& ed)
        {
            if (auto* e = dynamic_cast<StCompEditor*> (&ed)) { e->openMultiView(); e->getMultiView()->refresh(); }
        }, 90 });
        snap->startTimerHz (60);
    }
};

juce::JUCEApplicationBase* createSnapshotApp() { return new SnapshotApp(); }
/** Records the editor as PNG frames at 30 fps while audio runs in real time (for the website loop):
    StCompPluginTests --video <dir>, then ffmpeg -framerate 30 -i <dir>/%04d.jpg ... */
struct Recorder : juce::Timer
{
    StCompProcessor& p;
    juce::File dir;
    std::unique_ptr<juce::AudioProcessorEditor> ed;
    int tick = 0, frame = 0;
    static constexpr int totalFrames = 30 * 18;

    Recorder (StCompProcessor& proc, juce::File d) : p (proc), dir (d)
    {
        ed.reset (p.createEditor());
        ed->setSize (juce::roundToInt (StCompEditor::designWidth * 0.75f), juce::roundToInt (StCompEditor::designHeight * 0.75f));
        if (auto* e = dynamic_cast<StCompEditor*> (ed.get())) e->driveFramesManually();
    }

    void timerCallback() override
    {
        bool f = true;
        runSignal (p, 1, 800, f, 2, 0.55f, 0.04f);   // 60 ticks per second x 800 samples = real time at 48 kHz
        if (auto* e = dynamic_cast<StCompEditor*> (ed.get())) e->stepFrame (1.0 / 60.0);
        const float t = (float) tick / 60.0f;
        ++tick;

        // the script: a style every 3 seconds, a threshold sweep, the side chain opening near the end
        auto at = [t] (float sec) { return std::abs (t - sec) < 0.5f / 60.0f; };
        if (at (3.0f))  { setParam (p, ids::style, (float) stcomp::Style::fet); setParam (p, ids::attack, 1.0f); }
        if (at (6.0f))  { setParam (p, ids::style, (float) stcomp::Style::la76); setParam (p, ids::character, (float) Character::tube); }
        if (at (9.0f))  { setParam (p, ids::style, (float) stcomp::Style::opto); setParam (p, ids::release, 400.0f); }
        if (at (12.0f)) { setParam (p, ids::style, (float) stcomp::Style::upward); setParam (p, ids::threshold, -14.0f); setParam (p, ids::range, 12.0f); }
        if (t > 1.0f && t < 2.6f) setParam (p, ids::threshold, -18.0f - 10.0f * std::sin ((t - 1.0f) / 1.6f * juce::MathConstants<float>::pi));
        if (at (14.5f)) { setParam (p, ids::style, (float) stcomp::Style::clean); setParam (p, ids::threshold, -28.0f); setParam (p, ids::scBellGain, 15.0f);
                          setParam (p, ids::scBellFreq, 5000.0f); setParam (p, ids::scLowCut, 1500.0f);
                          if (auto* e = dynamic_cast<StCompEditor*> (ed.get())) e->openSidechain(); }

        if (tick % 2 != 0 || tick < 90) return;   // 30 fps, after the history has filled a little
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto file = dir.getChildFile (juce::String (frame).paddedLeft ('0', 4) + ".jpg");
        juce::FileOutputStream out (file);
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality (0.95f);
        jpeg.writeImageToStream (img, out);
        if (++frame >= totalFrames) { stopTimer(); ed.reset(); juce::JUCEApplicationBase::quit(); }
    }
};

struct VideoApp : juce::JUCEApplicationBase
{
    std::unique_ptr<StCompProcessor> proc;
    std::unique_ptr<Recorder> rec;
    const juce::String getApplicationName() override { return "StCompVideo"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}
    void shutdown() override { rec.reset(); proc.reset(); }
    void initialise (const juce::String&) override
    {
        proc = std::make_unique<StCompProcessor>();
        proc->setPlayConfigDetails (2, 2, 48000.0, 800);
        proc->prepareToPlay (48000.0, 800);
        setParam (*proc, ids::threshold, -18.0f);
        setParam (*proc, ids::autoGain, 1.0f);
        snapshotDir.createDirectory();
        bool f = true;
        runSignal (*proc, 120, 800, f, 2, 0.55f, 0.04f);
        rec = std::make_unique<Recorder> (*proc, snapshotDir);
        rec->startTimerHz (60);
    }
};

juce::JUCEApplicationBase* createVideoApp() { return new VideoApp(); }

}

int runUseCaseVideos (int argc, char** argv);   // UseCases.cpp

int main (int argc, char** argv)
{
    juce::StringArray args (argv + 1, argc - 1);
    if (args.contains ("--usecases"))
        return runUseCaseVideos (argc, argv);
    if (args.contains ("--video"))
    {
        snapshotDir = juce::File (args[args.indexOf ("--video") + 1]);
        juce::JUCEApplicationBase::createInstance = &createVideoApp;
        return juce::JUCEApplicationBase::main (argc, (const char**) argv);
    }
    if (args.contains ("--snapshot"))
    {
        snapshotDir = juce::File (args[args.indexOf ("--snapshot") + 1]);
        juce::JUCEApplicationBase::createInstance = &createSnapshotApp;
        return juce::JUCEApplicationBase::main (argc, (const char**) argv);
    }
    return runTests();
}
