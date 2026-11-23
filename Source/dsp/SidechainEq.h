#pragma once
#include "../Params.h"

namespace stcomp
{

struct SidechainSettings
{
    float gainDb = 0.0f;
    float lowCutHz = 20.0f;       // <= limits::lowCutOff = off (24 dB/oct)
    float highCutHz = 20000.0f;   // >= limits::highCutOff = off (12 dB/oct)
    float bellHz = 2500.0f;
    float bellGainDb = 0.0f;
    float bellQ = 1.0f;
};

/** What the detector hears: side-chain level, a 24 dB/oct low cut, a bell and a 12 dB/oct high cut.
    Coefficients are recomputed only when a setting moves (cheap, audio thread). */
class SidechainEq
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setSettings (const SidechainSettings&) noexcept;
    void process (float* const* data, int numCh, int n) noexcept;

    /** Response (dB) of the side-chain filter at `hz` (UI, tests; any thread). */
    static float magnitudeDb (float hz, const SidechainSettings&, double sampleRate) noexcept;

    static bool lowCutOn (const SidechainSettings& s) noexcept  { return s.lowCutHz > limits::lowCutOff; }
    static bool highCutOn (const SidechainSettings& s) noexcept { return s.highCutHz < limits::highCutOff; }
    static bool bellOn (const SidechainSettings& s) noexcept    { return std::abs (s.bellGainDb) > 0.05f; }

private:
    double fs = 48000.0;
    SidechainSettings settings, applied;
    bool dirty = true;
    std::array<st::dsp::Biquad, 2> lowCut;
    st::dsp::Biquad bell, highCut;
    float gain = 1.0f;

    static void design (const SidechainSettings&, double fs, st::dsp::BiquadCoeffs (&lc)[2], st::dsp::BiquadCoeffs& bell,
                        st::dsp::BiquadCoeffs& hc) noexcept;
};

} // namespace stcomp
