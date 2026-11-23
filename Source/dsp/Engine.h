#pragma once
#include "Compressor.h"
#include "Character.h"
#include "SidechainEq.h"

namespace stcomp
{

struct EngineSettings
{
    CompSettings comp;
    float inputDb = 0.0f, outputDb = 0.0f;
    bool autoGain = false;
    int character = (int) Character::clean;
    float drive = 0.0f;          // 0..1
    float mix = 1.0f;            // 0..1
    float lookaheadMs = 0.0f;
    int channelMode = (int) ChannelMode::stereo;
    SidechainSettings sidechain;
    bool externalSidechain = false;
    bool listen = false;         // output what the detector hears
    bool bypass = false;
};

/** ST Comp's whole signal path, independent of AudioProcessor (offline testable):

      input gain -> [M/S encode] -> detector: main or side-chain input -> side-chain level / EQ -> Compressor gain
      main -> lookahead delay -> gain on the processed channels -> [M/S decode] -> oversample -> style colour +
      character drive -> downsample -> auto / make-up gain -> Mix with the (latency-aligned) dry signal
      -> output gain -> side-chain listen -> bypass cross-fade

    Latency = lookahead + oversampling. prepare() / setQuality() on the message thread; process() in chunks of
    <= maxBlock on the audio thread (lookahead changes are allocation-free: the delays are sized for the maximum). */
class Engine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    /** 0 = off, 1 = 2x, 2 = 4x, 3 = 8x. Re-prepares: message thread, processing suspended. */
    void setQuality (int index);
    int getQuality() const noexcept { return quality; }
    int getLatencySamples() const noexcept { return latency; }
    /** The latency a quality / lookahead combination has at the prepared sample rate (any thread). */
    int latencyFor (int qualityIndex, float lookaheadMs) const noexcept;

    void setSettings (const EngineSettings&) noexcept;
    const EngineSettings& getSettings() const noexcept { return settings; }

    /** Processes `main` in place (n <= maxBlockSize). `sidechain` may be null or have 0 channels. */
    void process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sidechain, int n) noexcept;

    // Meters (any thread)
    std::atomic<float> grDb { 0.0f };          // strongest gain change of the last block (< 0 reduction, > 0 upward)
    std::atomic<float> detectorDb { -120.0f }; // detector peak of the last block
    std::atomic<float> makeupDb { 0.0f };      // current make-up gain (auto)
    st::LevelHistory history;

    const Compressor& getCompressor() const noexcept { return comp; }

private:
    EngineSettings settings;
    Compressor comp;
    CharacterStage character;
    SidechainEq scEq;
    st::dsp::Oversampler oversampler;
    st::dsp::DelayLine lookahead, rawDelay;
    st::dsp::LinearSmoother inGain, outGain, makeup, mixAmount, bypassAmount, listenAmount;

    juce::AudioBuffer<float> raw, detector, listenBuf;
    std::vector<float> inGainRamp;
    double fs = 48000.0;
    int maxBlock = 64, channels = 2, quality = 0, latency = 0, lookaheadSamples = 0, maxLookahead = 0;
    float lastColour = 0.0f;
    bool prepared = false;

    // history frame accumulation
    int frameLength = 192, frameCount = 0;
    float frameIn = 0.0f, frameOut = 0.0f, frameGr = 0.0f, frameDet = -120.0f;

    void updateLatency() noexcept;
};

} // namespace stcomp
