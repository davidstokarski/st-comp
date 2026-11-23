#pragma once
#include <st_shell/st_shell.h>

namespace stcomp
{

/** The compression styles. The order is permanent: it is the `style` parameter's value. */
enum class Style { clean, glue, mastering, vocal, punch, pump, upward, fet, fetAll, opto, la76, variMu, diode };
constexpr int kNumStyles = 13;

enum class Family { modern, vintage };

/** Shape of the gain curve above the knee. */
enum class Curve
{
    linear,      // the ratio as set
    rising,      // the ratio grows with the overshoot (optical cells, variable-mu tubes)
    overshoot    // over-compresses past the threshold, then settles (FET with every ratio engaged)
};

/** How a stage lets go of gain reduction. */
enum class Release
{
    classic,     // exponential; Auto Release adds a slow second stage (programme dependent)
    dual,        // always programme dependent (fast + slow stage), Auto Release or not
    linear,      // constant rate in dB per second (release = time to recover 10 dB)
    opto,        // two-stage optical release whose tail lengthens with how long the cell has been lit
    pump         // accelerating: holds while deep, snaps back near 0 dB (audible, musical pumping)
};

/** Harmonics a style's circuit adds as it works (grow with gain reduction). */
enum class Harmonics { none, even, odd, diode };

/** One gain stage. Most styles have one; LA-76 runs an optical leveller into a FET peak stage.
    Every number here is relative to the user's controls so all styles share one set of knobs. */
struct StageSpec
{
    bool active = false;
    bool feedback = false;          // detects its own output (vintage designs) instead of the input
    float rmsBlend = 0.0f;          // detector: 0 = peak, 1 = RMS
    float rmsMs = 5.0f;             // RMS window
    float kneeAdd = 0.0f;           // added to the Knee control (dB)
    float kneeScale = 1.0f;         // Knee control multiplier
    Curve curve = Curve::linear;
    float curveFloor = 1.0f;        // rising: share of the ratio at the knee (0..1)
    float curveWidth = 18.0f;       // rising: overshoot (dB) where the full ratio is reached
    float thresholdOffsetDb = 0.0f; // stage threshold = Threshold + offset
    float ratioPower = 1.0f;        // stage ratio = Ratio ^ power, clamped to [ratioMin, ratioMax]
    float ratioMin = 1.0f, ratioMax = 100.0f;
    float minAttackMs = 0.01f;      // fastest attack the design allows
    float fixedAttackMs = -1.0f;    // >= 0: ignores the Attack control
    float fixedReleaseMs = -1.0f;   // >= 0: ignores the Release control
    float releaseScale = 1.0f;
    Release release = Release::classic;
    bool programmeAttack = false;   // bigger jumps are caught faster
    float gainSmoothMs = 0.0f;      // extra smoothing of the gain trace (less modulation distortion)
};

struct StyleInfo
{
    const char* name;               // "FET"
    const char* character;          // one line: what it sounds like
    const char* detail;             // tooltip: how it works
    Family family;
    std::array<StageSpec, 2> stages;
    bool upward = false;            // raises quiet parts instead of lowering loud ones
    Harmonics harmonics = Harmonics::none;
    float colourBase = 0.0f, colourPerDb = 0.0f, colourMax = 0.0f;
};

const StyleInfo& getStyleInfo (int style);
juce::StringArray getStyleNames();

/** A categorised menu of the styles (item IDs = style + 1) for the Style dropdown. */
juce::PopupMenu buildStyleMenu (int currentStyle);

} // namespace stcomp
