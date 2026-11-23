#include "Engine.h"

namespace stcomp
{

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (1, maxBlockSize);
    channels = juce::jlimit (1, 2, numChannels);

    oversampler.prepare (fs, maxBlock, channels);
    oversampler.setFactorIndex (quality);

    int maxOs = 0;
    for (int q = 0; q < 4; ++q) maxOs = juce::jmax (maxOs, oversampler.getLatencySamples (q));
    maxLookahead = (int) std::ceil ((double) limits::lookaheadMaxMs * 0.001 * fs);
    lookahead.prepare (channels, maxLookahead + 1);
    rawDelay.prepare (channels, maxLookahead + maxOs + 1);

    comp.prepare (fs, maxBlock);
    character.prepare (fs * (double) st::dsp::Oversampler::factorForIndex (quality), channels);
    scEq.prepare (fs, channels);

    raw.setSize (channels, maxBlock, false, true, true);
    detector.setSize (channels, maxBlock, false, true, true);
    listenBuf.setSize (channels, maxBlock, false, true, true);
    inGainRamp.assign ((size_t) maxBlock, 1.0f);

    for (auto* s : { &inGain, &outGain, &makeup, &mixAmount, &bypassAmount, &listenAmount })
        s->prepare (fs, 20.0);

    frameLength = juce::jmax (16, juce::roundToInt (fs * 0.004));
    history.setFrameRate (fs / frameLength);
    prepared = true;
    setSettings (settings);
    reset();
}

void Engine::reset() noexcept
{
    comp.reset();
    character.reset();
    scEq.reset();
    oversampler.reset();
    lookahead.reset();
    rawDelay.reset();
    inGain.setCurrentAndTarget (st::dsp::dbToGain (settings.inputDb));
    outGain.setCurrentAndTarget (st::dsp::dbToGain (settings.outputDb));
    mixAmount.setCurrentAndTarget (settings.mix);
    bypassAmount.setCurrentAndTarget (settings.bypass ? 1.0f : 0.0f);
    listenAmount.setCurrentAndTarget (settings.listen ? 1.0f : 0.0f);
    makeup.setCurrentAndTarget (st::dsp::dbToGain (makeupDb.load()));
    lastColour = 0.0f;
    frameCount = 0;
    frameIn = frameOut = frameGr = 0.0f;
    frameDet = -120.0f;
}

int Engine::latencyFor (int q, float lookaheadMs) const noexcept
{
    const int la = juce::jlimit (0, maxLookahead, (int) std::lround ((double) juce::jmax (0.0f, lookaheadMs) * 0.001 * fs));
    return la + oversampler.getLatencySamples (juce::jlimit (0, 3, q));
}

void Engine::updateLatency() noexcept
{
    lookaheadSamples = juce::jlimit (0, maxLookahead, (int) std::lround ((double) juce::jmax (0.0f, settings.lookaheadMs) * 0.001 * fs));
    latency = lookaheadSamples + oversampler.getLatencySamples (quality);
    lookahead.setDelay (lookaheadSamples);
    rawDelay.setDelay (latency);
}

void Engine::setQuality (int index)
{
    quality = juce::jlimit (0, 3, index);
    if (! prepared) return;
    oversampler.setFactorIndex (quality);
    character.prepare (fs * (double) st::dsp::Oversampler::factorForIndex (quality), channels);
    oversampler.reset();
    lookahead.reset();
    rawDelay.reset();
    updateLatency();
}

void Engine::setSettings (const EngineSettings& s) noexcept
{
    settings = s;
    comp.setSettings (s.comp);
    scEq.setSettings (s.sidechain);
    inGain.setTarget (st::dsp::dbToGain (s.inputDb));
    outGain.setTarget (st::dsp::dbToGain (s.outputDb));
    mixAmount.setTarget (juce::jlimit (0.0f, 1.0f, s.mix));
    bypassAmount.setTarget (s.bypass ? 1.0f : 0.0f);
    listenAmount.setTarget (s.listen ? 1.0f : 0.0f);

    float mk = 0.0f;
    if (s.autoGain)
        mk = Compressor::autoMakeupDb (s.comp) + CharacterStage::driveCompensationDb (s.character, s.drive);
    makeupDb.store (mk, std::memory_order_relaxed);
    makeup.setTarget (st::dsp::dbToGain (mk));
    if (prepared) updateLatency();
}

void Engine::process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sidechain, int n) noexcept
{
    n = juce::jmin (n, maxBlock);
    const int numCh = juce::jmin (channels, main.getNumChannels());
    if (n <= 0 || numCh <= 0) return;
    float* d[2] = { main.getWritePointer (0), numCh > 1 ? main.getWritePointer (1) : nullptr };

    // ---- keep the raw input (bypass / mix dry), latency aligned; then the input gain
    for (int c = 0; c < numCh; ++c) raw.copyFrom (c, 0, main, c, 0, n);
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainRamp[(size_t) i] = inGain.getNext();
        for (int c = 0; c < numCh; ++c) d[c][i] *= g;
    }
    for (int c = 0; c < numCh; ++c) rawDelay.processChannel (c, raw.getWritePointer (c), n);
    rawDelay.advance (n);

    float inPeak = 0.0f;
    for (int c = 0; c < numCh; ++c) inPeak = juce::jmax (inPeak, main.getMagnitude (c, 0, n));

    // ---- detector: the main signal or the side-chain input, through the side-chain level / EQ
    const bool ext = settings.externalSidechain && sidechain != nullptr && sidechain->getNumChannels() > 0
                     && sidechain->getNumSamples() >= n;
    for (int c = 0; c < numCh; ++c)
    {
        if (ext) detector.copyFrom (c, 0, *sidechain, juce::jmin (c, sidechain->getNumChannels() - 1), 0, n);
        else detector.copyFrom (c, 0, main, c, 0, n);
    }
    float* det[2] = { detector.getWritePointer (0), numCh > 1 ? detector.getWritePointer (1) : nullptr };
    scEq.process (det, numCh, n);
    const bool listening = settings.listen || listenAmount.isSmoothing() || listenAmount.getCurrent() > 0.0f;
    if (listening)
        for (int c = 0; c < numCh; ++c) listenBuf.copyFrom (c, 0, detector, c, 0, n);

    // ---- channel mode: which channels are detected and processed (M/S modes work on mid / side)
    const auto mode = (ChannelMode) juce::jlimit (0, 5, settings.channelMode);
    const bool stereo = numCh == 2;
    const bool ms = stereo && (mode == ChannelMode::midSide || mode == ChannelMode::mid || mode == ChannelMode::side);
    int first = 0, count = numCh;
    if (stereo && (mode == ChannelMode::mid || mode == ChannelMode::left)) count = 1;
    if (stereo && (mode == ChannelMode::side || mode == ChannelMode::right)) { first = 1; count = 1; }
    if (ms)
    {
        st::dsp::encodeMidSide (d[0], d[1], n);
        st::dsp::encodeMidSide (det[0], det[1], n);
    }

    // ---- lookahead: the audio is late, the detector isn't
    for (int c = 0; c < numCh; ++c) lookahead.processChannel (c, d[c], n);
    lookahead.advance (n);

    // ---- gain computer + ballistics (base rate), applied before the colour stage
    const float* detPtrs[2] = { det[first], count > 1 ? det[1] : nullptr };
    comp.computeGain (detPtrs, count, n);
    float grStrong = 0.0f, grSum = 0.0f;
    for (int k = 0; k < count; ++k)
    {
        float* x = d[first + k];
        const float* g = comp.getGainDb (k);
        for (int i = 0; i < n; ++i)
        {
            x[i] *= st::dsp::dbToGain (g[i]);
            grStrong = st::LevelHistory::strongerGain (grStrong, g[i]);
            grSum += g[i];
        }
    }
    const float grMean = grSum / (float) (n * count);
    if (ms) st::dsp::decodeMidSide (d[0], d[1], n);

    // ---- oversampled colour: the style's harmonics + character drive (always through the oversampler, so the
    //      latency never changes)
    {
        const auto& info = getStyleInfo (settings.comp.style);
        const float colour = Compressor::colourAmount (settings.comp.style, grMean);
        juce::dsp::AudioBlock<float> block (d, (size_t) numCh, (size_t) n);
        auto up = oversampler.processUp (block);
        float* upPtrs[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
        character.process (upPtrs, numCh, (int) up.getNumSamples(), info.harmonics, lastColour, colour,
                           settings.character, settings.drive);
        oversampler.processDown (block);
        lastColour = colour;
    }

    // ---- make-up, mix, output, listen, bypass
    float outPeak = 0.0f;
    {
        const float* rw[2] = { raw.getReadPointer (0), numCh > 1 ? raw.getReadPointer (1) : nullptr };
        const float* ls[2] = { listenBuf.getReadPointer (0), numCh > 1 ? listenBuf.getReadPointer (1) : nullptr };
        for (int i = 0; i < n; ++i)
        {
            const float mk = makeup.getNext(), mx = mixAmount.getNext(), og = outGain.getNext(),
                        bp = bypassAmount.getNext(), li = listenAmount.getNext();
            const float ig = inGainRamp[(size_t) i];   // the Mix dry signal has the input gain too
            for (int c = 0; c < numCh; ++c)
            {
                const float dry = rw[c][i] * ig;
                float y = (dry + mx * (d[c][i] * mk - dry)) * og;
                if (listening) y += li * (ls[c][i] - y);
                y += bp * (rw[c][i] - y);
                d[c][i] = y;
                outPeak = juce::jmax (outPeak, std::abs (y));
            }
        }
    }

    // ---- meters + history
    grDb.store (grStrong, std::memory_order_relaxed);
    detectorDb.store (comp.getDetectorDb(), std::memory_order_relaxed);
    frameIn = juce::jmax (frameIn, inPeak);
    frameOut = juce::jmax (frameOut, outPeak);
    frameGr = st::LevelHistory::strongerGain (frameGr, grStrong);
    frameDet = juce::jmax (frameDet, comp.getDetectorDb());
    frameCount += n;
    while (frameCount >= frameLength)
    {
        frameCount -= frameLength;
        history.push ({ st::dsp::gainToDb (frameIn, -120.0f), st::dsp::gainToDb (frameOut, -120.0f), frameGr, frameDet });
        frameIn = frameOut = frameGr = 0.0f;
        frameDet = -120.0f;
    }
}

} // namespace stcomp
