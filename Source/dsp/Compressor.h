#pragma once
#include "../Params.h"

namespace stcomp
{

/** The user's compressor controls (updated per chunk on the audio thread; cheap). */
struct CompSettings
{
    int style = (int) Style::clean;
    float thresholdDb = -18.0f;
    float ratio = 4.0f;          // >= limits::ratioInfinite = infinity
    float kneeDb = 6.0f;
    float rangeDb = 60.0f;       // >= limits::rangeUnlimited = no limit
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float holdMs = 0.0f;
    bool autoRelease = false;
    float stereoLink = 1.0f;     // 0..1
};

/** A style's stage curve resolved against the user's controls (threshold, slope, knee, shape). */
struct StageCurve
{
    bool feedback = false, upward = false;
    float threshold = -18.0f;
    float slope = -0.75f;        // dB of gain change per dB of overshoot: 1/R - 1 (<= 0)
    float knee = 6.0f;
    Curve curve = Curve::linear;
    float floor = 1.0f, width = 18.0f;

    static StageCurve resolve (const StageSpec&, const CompSettings&, bool upward) noexcept;
    /** Gain change (dB) the stage aims for at detector level `levelDb` (its input for feed-forward stages, its
        own output for feedback stages). Downward: <= 0; upward: >= 0. */
    float target (float levelDb) const noexcept;
    /** Steady-state gain change for a steady stage input `inDb` (feedback stages solved for their fixed point). */
    float steadyState (float inDb) const noexcept;
};

/** ST Comp's gain computer: every style in one class, described by data (Styles.h: up to two stages in series,
    each feed-forward or feedback, with its detector, curve and release behaviour), so a style is a table entry,
    not a code path.

    detector -> per stage: level (peak / RMS blend, stereo link) -> target gain (curve) -> ballistics (attack,
    hold, release type, smoothing) -> sum of the stages -> range limit. Everything is in the dB domain on the gain
    change (gr <= 0 for downward styles, >= 0 for Upward).

    The static curve (staticGainDb) is what the knee display draws. Feedback stages detect their own output; their
    curve is built so the steady state lands on the same curve as the knob settings say (tests check it).

    Threading: prepare() on the message thread, the rest on the audio thread (no allocation). */
class Compressor
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxStages = 2;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setSettings (const CompSettings&) noexcept;
    const CompSettings& getSettings() const noexcept { return settings; }

    /** Computes the gain change (dB) for n samples from `detector` (numCh channels) into getGainDb(ch).
        Feedback stages use the detector times their own previous gain (they hear the compressed signal). */
    void computeGain (const float* const* detector, int numCh, int n) noexcept;
    const float* getGainDb (int ch) const noexcept { return gains[(size_t) juce::jlimit (0, kMaxChannels - 1, ch)].data(); }

    /** Static transfer: gain change (dB) for a steady detector level `inDb`. Any thread. */
    static float staticGainDb (float inDb, const CompSettings&) noexcept;
    /** Programme-independent make-up for Auto Gain (dB; negative for Upward). */
    static float autoMakeupDb (const CompSettings&) noexcept;
    /** The style's own colour (circuit harmonics) for a gain change of grDb: 0..~0.6. */
    static float colourAmount (int style, float grDb) noexcept;
    /** Limits a total gain change to the range (soft corner). */
    static float limitRange (float gainDb, float rangeDb) noexcept;

    /** Highest detector level of the last block (dB), for the knee display. */
    float getDetectorDb() const noexcept { return detectorDb; }

private:
    struct StageState
    {
        std::array<float, kMaxChannels> meanSq {}, g {}, slow {}, opto {}, smooth {}, out {}, prevGain { 1.0f, 1.0f };
        std::array<int, kMaxChannels> hold {};
        float optoMemory = 0.0f;
    };
    struct StageCoeffs
    {
        float att = 1.0f, rel = 1.0f, rms = 1.0f, fastRel = 1.0f, slowAtt = 1.0f, slowRel = 1.0f,
              optoFastRel = 1.0f, optoSlowAtt = 1.0f, smooth = 1.0f, ratePerSample = 0.0f, releaseMs = 100.0f;
    };

    CompSettings settings;
    double fs = 48000.0;
    int numStages = 1, holdSamples = 0;
    std::array<StageCurve, kMaxStages> curves;
    std::array<StageCoeffs, kMaxStages> coeffs;
    std::array<StageState, kMaxStages> state;
    float detectorDb = -120.0f;
    std::array<std::vector<float>, kMaxChannels> gains;

    float coeff (float ms) const noexcept;
};

} // namespace stcomp
