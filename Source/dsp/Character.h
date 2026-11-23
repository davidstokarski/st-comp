#pragma once
#include "../Params.h"

namespace stcomp
{

/** Post-compression colour, run at the oversampled rate:
    1. style colour: the harmonics the selected style's circuit adds, growing with gain reduction
       (y = x + c (f(x) - x), tiny at 0 dB GR; even = tube, odd = symmetric, diode = asymmetric bridge),
    2. Character + Drive: Tube (asymmetric, even), Diode (harder, asymmetric), Tape (soft, symmetric, HF roll-off
       with drive), Transformer (lows driven harder, then restored), Bright (highs driven harder, a little left
       on). The drive is normalised so a -6 dBFS peak keeps its level: more drive = denser, louder body with held
       peaks. Clean = no drive stage.
    A DC blocker follows (asymmetric curves add DC). Audio thread only after prepare(). */
class CharacterStage
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    /** colourFrom / colourTo: style colour amount ramped across the block. */
    void process (float* const* data, int numCh, int n, Harmonics harmonics, float colourFrom, float colourTo,
                  int character, float drive01) noexcept;

    /** Level compensation (dB) Auto Gain applies for a drive amount (the drive adds body / loudness). */
    static float driveCompensationDb (int character, float drive01) noexcept
    {
        return character == (int) Character::clean ? 0.0f : -0.45f * juce::jlimit (0.0f, 1.0f, drive01) * 24.0f;
    }

    /** Static curve of the Drive stage for one character (for tests / UI). */
    static float shape (int character, float x) noexcept;
    /** The style-colour waveshaper. */
    static float harmonicShape (Harmonics, float x) noexcept;

private:
    double fs = 48000.0;
    st::dsp::OnePole tapeLp, xfmrPre, xfmrPost;
    st::dsp::Biquad brightPre, brightPost;
    st::dsp::DcBlocker dc;
    float lastTapeHz = -1.0f;
};

} // namespace stcomp
