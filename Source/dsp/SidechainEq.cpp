#include "SidechainEq.h"

namespace stcomp
{

namespace
{
    constexpr double kButterworthQ[2] = { 0.54119610, 1.30656296 };   // 4th-order Butterworth sections
}

void SidechainEq::design (const SidechainSettings& s, double sr, st::dsp::BiquadCoeffs (&lc)[2], st::dsp::BiquadCoeffs& b,
                          st::dsp::BiquadCoeffs& hc) noexcept
{
    using st::dsp::BiquadCoeffs;
    using st::dsp::BiquadType;
    const double nyq = sr * 0.45;
    for (int i = 0; i < 2; ++i)
        lc[i] = BiquadCoeffs::design (BiquadType::highPass, sr, juce::jlimit (10.0, nyq, (double) s.lowCutHz), kButterworthQ[i]);
    b = BiquadCoeffs::design (BiquadType::peak, sr, juce::jlimit (10.0, nyq, (double) s.bellHz), juce::jlimit (0.1, 20.0, (double) s.bellQ), s.bellGainDb);
    hc = BiquadCoeffs::design (BiquadType::lowPass, sr, juce::jlimit (100.0, nyq, (double) s.highCutHz), 0.7071);
}

void SidechainEq::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& f : lowCut) f.prepare (numChannels);
    bell.prepare (numChannels);
    highCut.prepare (numChannels);
    dirty = true;
    setSettings (settings);
}

void SidechainEq::reset() noexcept
{
    for (auto& f : lowCut) f.reset();
    bell.reset();
    highCut.reset();
}

void SidechainEq::setSettings (const SidechainSettings& s) noexcept
{
    settings = s;
    gain = st::dsp::dbToGain (s.gainDb);
    const bool changed = dirty || std::abs (s.lowCutHz - applied.lowCutHz) > 1.0e-3f || std::abs (s.highCutHz - applied.highCutHz) > 1.0e-2f
                         || std::abs (s.bellHz - applied.bellHz) > 1.0e-3f || std::abs (s.bellGainDb - applied.bellGainDb) > 1.0e-4f
                         || std::abs (s.bellQ - applied.bellQ) > 1.0e-5f;
    if (! changed) return;
    st::dsp::BiquadCoeffs lc[2], b, hc;
    design (s, fs, lc, b, hc);
    lowCut[0].setCoefficients (lc[0]);
    lowCut[1].setCoefficients (lc[1]);
    bell.setCoefficients (b);
    highCut.setCoefficients (hc);
    applied = s;
    dirty = false;
}

void SidechainEq::process (float* const* data, int numCh, int n) noexcept
{
    numCh = juce::jlimit (1, kMaxChannels, numCh);
    const bool lc = lowCutOn (settings), hc = highCutOn (settings), pk = bellOn (settings);
    const bool level = std::abs (gain - 1.0f) > 1.0e-6f;
    if (! lc && ! hc && ! pk && ! level) return;
    for (int c = 0; c < numCh; ++c)
    {
        float* d = data[c];
        for (int i = 0; i < n; ++i)
        {
            float x = d[i] * gain;
            if (lc) x = lowCut[1].processSample (c, lowCut[0].processSample (c, x));
            if (pk) x = bell.processSample (c, x);
            if (hc) x = highCut.processSample (c, x);
            d[i] = x;
        }
    }
}

float SidechainEq::magnitudeDb (float hz, const SidechainSettings& s, double sr) noexcept
{
    st::dsp::BiquadCoeffs lc[2], b, hc;
    design (s, sr, lc, b, hc);
    double mag = st::dsp::dbToGain (s.gainDb);
    if (lowCutOn (s)) mag *= lc[0].magnitude (hz, sr) * lc[1].magnitude (hz, sr);
    if (bellOn (s)) mag *= b.magnitude (hz, sr);
    if (highCutOn (s)) mag *= hc.magnitude (hz, sr);
    return st::dsp::gainToDb ((float) mag, -120.0f);
}

} // namespace stcomp
