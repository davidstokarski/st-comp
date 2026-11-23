#include "Character.h"

namespace stcomp
{

namespace
{
    constexpr float kTubeBias = 0.3f;
    const float kTubeNorm = 1.0f / (1.0f - std::tanh (kTubeBias) * std::tanh (kTubeBias));   // 1 / sech^2(bias)

    /** Asymmetric tube-like curve, unity slope at 0, no DC at 0. */
    inline float tube (float x) noexcept { return (std::tanh (x + kTubeBias) - std::tanh (kTubeBias)) * kTubeNorm; }
    /** Diode bridge: exponential knee, the negative side clips harder (unity slope at 0 on both sides). */
    inline float diode (float x) noexcept { return x >= 0.0f ? 1.0f - std::exp (-x) : -(1.0f - std::exp (1.5f * x)) / 1.5f; }
    inline float tapeCurve (float x) noexcept { return x / std::pow (1.0f + std::pow (std::abs (x), 2.2f), 1.0f / 2.2f); }
}

float CharacterStage::shape (int character, float x) noexcept
{
    switch ((Character) character)
    {
        case Character::tube:   return tube (x);
        case Character::diode:  return diode (x);
        case Character::tape:   return tapeCurve (x);
        case Character::clean:  return x;
        case Character::transformer:
        case Character::bright:
        default:                return std::tanh (x);
    }
}

float CharacterStage::harmonicShape (Harmonics h, float x) noexcept
{
    switch (h)
    {
        case Harmonics::even:  return tube (x);
        case Harmonics::odd:   return std::tanh (x);
        case Harmonics::diode: return diode (x);
        case Harmonics::none:
        default:               return x;
    }
}

void CharacterStage::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    tapeLp.setType (st::dsp::OnePoleType::lowPass);
    tapeLp.prepare (fs, numChannels);
    tapeLp.setCutoff (juce::jmin (20000.0, fs * 0.45));
    lastTapeHz = -1.0f;
    for (auto* f : { &xfmrPre, &xfmrPost })
    {
        f->setType (st::dsp::OnePoleType::lowPass);
        f->prepare (fs, numChannels);
        f->setCutoff (150.0);
    }
    const double brightHz = juce::jmin (3500.0, fs * 0.4);
    brightPre.prepare (numChannels);
    brightPost.prepare (numChannels);
    brightPre.setParameters (st::dsp::BiquadType::highShelf, fs, brightHz, 0.7071, 9.0);
    brightPost.setParameters (st::dsp::BiquadType::highShelf, fs, brightHz, 0.7071, -6.0);
    dc.prepare (fs, numChannels, 8.0);
    reset();
}

void CharacterStage::reset() noexcept
{
    tapeLp.reset();
    xfmrPre.reset();
    xfmrPost.reset();
    brightPre.reset();
    brightPost.reset();
    dc.reset();
}

void CharacterStage::process (float* const* data, int numCh, int n, Harmonics harmonics, float colourFrom, float colourTo,
                              int character, float drive01) noexcept
{
    numCh = juce::jlimit (1, kMaxChannels, numCh);
    drive01 = juce::jlimit (0.0f, 1.0f, drive01);
    const auto ch = (Character) juce::jlimit (0, 5, character);
    const bool driving = ch != Character::clean && drive01 > 0.0005f;
    const bool colouring = harmonics != Harmonics::none && (colourFrom > 1.0e-5f || colourTo > 1.0e-5f);
    if (! driving && ! colouring) return;

    // Drive: g into the curve, normalised so a 0.5 (-6 dBFS) peak keeps its level; blended in over the first 1/6
    const float g = st::dsp::dbToGain (drive01 * 24.0f);
    constexpr float ref = 0.5f;
    const float norm = ref / juce::jmax (1.0e-3f, shape ((int) ch, g * ref));
    const float wet = juce::jmin (1.0f, drive01 * 6.0f);

    if (ch == Character::tape && driving)
    {
        const float hz = (float) juce::jmin (fs * 0.45, 20000.0 - 12000.0 * (double) drive01);
        if (std::abs (hz - lastTapeHz) > 1.0f) { tapeLp.setCutoff (hz); lastTapeHz = hz; }
    }

    const float step = n > 1 ? (colourTo - colourFrom) / (float) n : 0.0f;
    for (int c = 0; c < numCh; ++c)
    {
        float* d = data[c];
        float colour = colourFrom;
        for (int i = 0; i < n; ++i, colour += step)
        {
            float x = d[i];

            // 1. style colour
            if (colouring) x += colour * (harmonicShape (harmonics, x) - x);

            // 2. character drive
            if (driving)
            {
                float y;
                switch (ch)
                {
                    case Character::tube:  y = tube (g * x) * norm; break;
                    case Character::diode: y = diode (g * x) * norm; break;
                    case Character::tape:  y = tapeLp.processSample (c, tapeCurve (g * x) * norm); break;
                    case Character::transformer:
                    {
                        // lows driven ~6 dB harder, then the same amount taken back off
                        const float pre = x + xfmrPre.processSample (c, x);
                        const float s = std::tanh (g * pre) * norm;
                        y = s - 0.5f * xfmrPost.processSample (c, s);
                        break;
                    }
                    case Character::bright:
                    {
                        // highs driven 9 dB harder, 6 dB taken back: brighter harmonics and a little air
                        const float s = std::tanh (g * brightPre.processSample (c, x)) * norm;
                        y = brightPost.processSample (c, s);
                        break;
                    }
                    case Character::clean:
                    default: y = x; break;
                }
                x += wet * (y - x);
            }

            d[i] = dc.processSample (c, x);
        }
    }
}

} // namespace stcomp
