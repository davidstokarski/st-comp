#include "Compressor.h"

namespace stcomp
{

namespace
{
    inline float smoothstep01 (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }

    /** Polynomial smooth minimum (exactly min() further than k away from the crossover). */
    inline float smoothMin (float a, float b, float k) noexcept
    {
        const float h = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * (b - a) / k);
        return b + (a - b) * h - k * h * (1.0f - h);
    }

    /** Overshoot through a quadratic soft knee of width `knee` (dB, >= 0). */
    inline float kneeOver (float over, float knee) noexcept
    {
        if (knee <= 0.01f) return juce::jmax (0.0f, over);
        const float h = knee * 0.5f;
        if (over <= -h) return 0.0f;
        if (over >= h) return over;
        const float t = over + h;
        return t * t / (2.0f * knee);
    }

    /** Gain change for a knee-shaped overshoot `o` and slope `b` with the stage's curve shape. */
    inline float shape (const StageCurve& c, float o, float b) noexcept
    {
        switch (c.curve)
        {
            case Curve::rising:    return b * o * (c.floor + (1.0f - c.floor) * smoothstep01 (o / c.width));
            case Curve::overshoot: return b * (1.18f * juce::jmin (o, 15.0f) + juce::jmax (0.0f, o - 15.0f));
            case Curve::linear:
            default:               return b * o;
        }
    }

    /** Upward compression stops raising the level as the detector approaches silence (no noise pumped up). */
    inline float upwardFloor (float levelDb) noexcept { return smoothstep01 ((levelDb + 92.0f) / 22.0f); }
}

//==============================================================================
StageCurve StageCurve::resolve (const StageSpec& s, const CompSettings& c, bool upward) noexcept
{
    StageCurve r;
    r.feedback = s.feedback && ! upward;
    r.upward = upward;
    r.threshold = c.thresholdDb + s.thresholdOffsetDb;
    r.knee = juce::jmax (0.0f, c.kneeDb * s.kneeScale + s.kneeAdd);
    r.curve = s.curve;
    r.floor = s.curveFloor;
    r.width = juce::jmax (1.0f, s.curveWidth);

    const bool infinite = c.ratio >= limits::ratioInfinite && s.ratioPower >= 0.999f && s.ratioMax >= limits::ratioInfinite;
    const float ratio = juce::jlimit (s.ratioMin, juce::jmin (s.ratioMax, 100.0f), std::pow (juce::jmax (1.0f, c.ratio), s.ratioPower));
    r.slope = infinite ? -1.0f : 1.0f / juce::jmax (1.0f, ratio) - 1.0f;
    return r;
}

float StageCurve::target (float levelDb) const noexcept
{
    if (upward)
        return -slope * kneeOver (threshold - levelDb, knee) * upwardFloor (levelDb);

    if (! feedback)
        return shape (*this, kneeOver (levelDb - threshold, knee), slope);

    // Feedback stages detect their own output y. With a slope b' = b / (1 + b) on y, the steady state
    // (gr = b' (x + gr - T)) has the slope b on the input, i.e. the ratio the knob says. Feedback can't reach
    // infinity: the ratio is capped (~25:1), like the hardware.
    const float b = juce::jmax (-0.96f, slope);
    return shape (*this, kneeOver (levelDb - threshold, knee), b / (1.0f + b));
}

float StageCurve::steadyState (float inDb) const noexcept
{
    if (! feedback) return target (inDb);

    // h = target (x + h). q(h) = h - target (x + h) increases monotonically (target' <= 0): bisection finds it.
    float lo = -100.0f, hi = 0.0f;
    for (int i = 0; i < 28; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        if (mid - target (inDb + mid) > 0.0f) hi = mid; else lo = mid;
    }
    return 0.5f * (lo + hi);
}

//==============================================================================
float Compressor::limitRange (float gr, float rangeDb) noexcept
{
    if (rangeDb >= limits::rangeUnlimited) return gr;
    const float r = juce::jmax (0.0f, rangeDb);
    if (r <= 0.0f) return 0.0f;
    return gr >= 0.0f ? smoothMin (gr, r, 1.5f) : -smoothMin (-gr, r, 1.5f);
}

float Compressor::staticGainDb (float inDb, const CompSettings& s) noexcept
{
    const auto& info = getStyleInfo (s.style);
    float total = 0.0f, x = inDb;
    for (const auto& spec : info.stages)
    {
        if (! spec.active) continue;
        const float g = StageCurve::resolve (spec, s, info.upward).steadyState (x);
        total += g;
        x += g;   // the next stage hears this one's output
    }
    return limitRange (total, s.rangeDb);
}

float Compressor::autoMakeupDb (const CompSettings& s) noexcept
{
    // Downward: half the reduction a loud passage (-6 dBFS detector) would get, close to the loudness lost on
    // typical material without chasing the programme. Upward: take back half of what a quiet passage gains.
    if (getStyleInfo (s.style).upward)
        return juce::jlimit (-24.0f, 0.0f, -0.5f * staticGainDb (-30.0f, s));
    return juce::jlimit (0.0f, 24.0f, -0.5f * staticGainDb (-6.0f, s));
}

float Compressor::colourAmount (int style, float grDb) noexcept
{
    const auto& i = getStyleInfo (style);
    if (i.harmonics == Harmonics::none) return 0.0f;
    return juce::jmin (i.colourMax, i.colourBase + i.colourPerDb * std::abs (grDb));
}

//==============================================================================
void Compressor::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& g : gains) g.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    setSettings (settings);
    reset();
}

void Compressor::reset() noexcept
{
    for (auto& s : state) s = StageState {};
    detectorDb = -120.0f;
}

float Compressor::coeff (float ms) const noexcept
{
    return ms <= 0.0f ? 1.0f : (float) (1.0 - std::exp (-1.0 / ((double) ms * 0.001 * fs)));
}

void Compressor::setSettings (const CompSettings& s) noexcept
{
    const bool styleChanged = s.style != settings.style;
    settings = s;
    const auto& info = getStyleInfo (s.style);
    numStages = 0;
    for (int i = 0; i < kMaxStages; ++i)
    {
        const auto& spec = info.stages[(size_t) i];
        if (! spec.active) break;
        curves[(size_t) i] = StageCurve::resolve (spec, s, info.upward);

        const float attack = spec.fixedAttackMs >= 0.0f ? spec.fixedAttackMs : juce::jmax (spec.minAttackMs, s.attackMs);
        const float release = (spec.fixedReleaseMs >= 0.0f ? spec.fixedReleaseMs : s.releaseMs) * spec.releaseScale;
        auto& c = coeffs[(size_t) i];
        c.att = coeff (attack);
        c.rel = coeff (release);
        c.rms = coeff (spec.rmsMs);
        c.releaseMs = release;
        // programme-dependent release: a fast stage recovers short peaks, a slow stage holds sustained compression
        c.fastRel = coeff (juce::jmax (10.0f, release * 0.35f));
        c.slowAtt = coeff (juce::jlimit (150.0f, 1500.0f, release * 10.0f));
        c.slowRel = coeff (release * 3.0f);
        // opto: fast part of the release (about half the reduction) and the slower attack of the slow part
        c.optoFastRel = coeff (juce::jlimit (40.0f, 90.0f, release * 0.06f));
        c.optoSlowAtt = coeff (attack * 3.0f);
        c.smooth = coeff (spec.gainSmoothMs);
        // linear release: `release` = time to recover 10 dB
        c.ratePerSample = (float) (10.0 / ((double) juce::jmax (1.0f, release) * 0.001 * fs));
        ++numStages;
    }
    holdSamples = (int) std::lround ((double) juce::jmax (0.0f, s.holdMs) * 0.001 * fs);

    // a different style has different state meanings (feedback gains, opto memory): start clean, no jump in level
    // matters less than a stuck feedback loop
    if (styleChanged)
        for (auto& st : state) { st.prevGain.fill (1.0f); st.optoMemory = 0.0f; }
}

void Compressor::computeGain (const float* const* detector, int numCh, int n) noexcept
{
    numCh = juce::jlimit (1, kMaxChannels, numCh);
    n = juce::jmin (n, (int) gains[0].size());
    const auto& info = getStyleInfo (settings.style);
    const float link = juce::jlimit (0.0f, 1.0f, settings.stereoLink);
    const float dt = (float) (1.0 / fs);
    const bool userAuto = settings.autoRelease;

    // opto tails lengthen with how long / hard the cell has been lit (memory, per block)
    std::array<float, kMaxStages> optoSlowRel {};
    for (int s = 0; s < numStages; ++s)
        optoSlowRel[(size_t) s] = coeff (coeffs[(size_t) s].releaseMs * (1.0f + state[(size_t) s].optoMemory * (userAuto ? 2.0f : 1.0f)));

    float peakIn = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        std::array<float, kMaxChannels> in {}, total {};
        for (int c = 0; c < numCh; ++c)
        {
            in[(size_t) c] = detector[c][i];
            peakIn = juce::jmax (peakIn, std::abs (in[(size_t) c]));
        }

        for (int s = 0; s < numStages; ++s)
        {
            const auto& spec = info.stages[(size_t) s];
            const auto& curve = curves[(size_t) s];
            const auto& k = coeffs[(size_t) s];
            auto& st = state[(size_t) s];

            // ---- detector: peak / RMS blend per channel, then the stereo link
            std::array<float, kMaxChannels> lin {};
            float maxLin = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                float x = in[(size_t) c];
                if (curve.feedback) x *= st.prevGain[(size_t) c];
                float& m = st.meanSq[(size_t) c];
                m += k.rms * (x * x - m);
                if (m < 1.0e-20f) m = 0.0f;
                const float a = std::abs (x);
                lin[(size_t) c] = a + spec.rmsBlend * (std::sqrt (m) - a);
                maxLin = juce::jmax (maxLin, lin[(size_t) c]);
            }

            float deepest = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const auto ci = (size_t) c;
                const float l = lin[ci] + link * (maxLin - lin[ci]);
                const float t = curve.target (st::dsp::gainToDb (l, -120.0f));
                float& g = st.g[ci];
                const bool attacking = t < g;

                // ---- hold: after gain moves down, wait before letting it come back up
                if (attacking) st.hold[ci] = holdSamples;
                else if (st.hold[ci] > 0)
                {
                    --st.hold[ci];
                    total[ci] += st.out[ci];
                    in[ci] *= st.prevGain[ci];
                    deepest = st::LevelHistory::strongerGain (deepest, st.out[ci]);
                    continue;
                }

                float out;
                const bool dual = spec.release == Release::dual
                                  || (userAuto && (spec.release == Release::classic || spec.release == Release::linear));
                if (spec.release == Release::opto)
                {
                    g += (attacking ? k.att : k.optoFastRel) * (t - g);
                    float& o = st.opto[ci];
                    o += (t < o ? k.optoSlowAtt : optoSlowRel[(size_t) s]) * (t - o);
                    out = 0.5f * (g + o);
                }
                else
                {
                    if (attacking)
                    {
                        if (spec.programmeAttack)
                        {
                            const float kk = juce::jlimit (0.5f, 4.0f, (g - t) / 10.0f);
                            g += (1.0f - std::pow (1.0f - k.att, kk)) * (t - g);
                        }
                        else g += k.att * (t - g);
                    }
                    else if (dual)                             g += k.fastRel * (t - g);
                    else if (spec.release == Release::linear) g = juce::jmin (t, g + k.ratePerSample);
                    else if (spec.release == Release::pump)
                    {
                        // accelerating release: slow while deep, fast near 0 dB
                        const float speed = juce::jlimit (0.3f, 4.0f, 3.0f / (std::abs (g) + 0.75f));
                        g += juce::jmin (1.0f, k.rel * speed) * (t - g);
                    }
                    else g += k.rel * (t - g);

                    out = g;
                    if (dual)
                    {
                        float& sl = st.slow[ci];
                        sl += (t < sl ? k.slowAtt : k.slowRel) * (t - sl);
                        out = juce::jmin (g, sl);
                    }
                }

                if (k.smooth < 1.0f)
                {
                    float& sm = st.smooth[ci];
                    sm += k.smooth * (out - sm);
                    out = sm;
                }
                if (std::abs (out) < 1.0e-6f) out = 0.0f;

                st.out[ci] = out;
                st.prevGain[ci] = st::dsp::dbToGain (out);
                in[ci] *= st.prevGain[ci];          // the next stage hears this stage's output
                total[ci] += out;
                deepest = st::LevelHistory::strongerGain (deepest, out);
            }

            if (spec.release == Release::opto)
            {
                const float depth = std::abs (deepest);
                st.optoMemory += dt * (depth > 2.0f ? depth / 12.0f : 0.0f) - dt * st.optoMemory / 4.0f;
                st.optoMemory = juce::jlimit (0.0f, 3.0f, st.optoMemory);
            }
        }

        for (int c = 0; c < numCh; ++c)
            gains[(size_t) c][(size_t) i] = limitRange (total[(size_t) c], settings.rangeDb);
    }

    for (int c = numCh; c < kMaxChannels; ++c)
        std::copy (gains[0].begin(), gains[0].begin() + n, gains[(size_t) c].begin());
    detectorDb = st::dsp::gainToDb (peakIn, -120.0f);
}

} // namespace stcomp
