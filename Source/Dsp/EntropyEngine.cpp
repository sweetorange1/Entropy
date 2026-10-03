#include "EntropyEngine.h"
#include <algorithm>
#include <cmath>

namespace entropy
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
float dbGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
float interpolate(float a, float b, float t) noexcept { return a + t * (b - a); }
}

void EntropyEngine::Ramp::set(float v, int samples) noexcept
{
    if (v == target)
        return;
    target = v;
    remaining = std::max(1, samples);
    step = (target - current) / static_cast<float>(remaining);
}

float EntropyEngine::Ramp::next() noexcept
{
    if (remaining > 0)
    {
        current += step;
        if (--remaining == 0)
            current = target;
    }
    return current;
}

float EntropyEngine::Lane::uniform() noexcept
{
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return static_cast<float>(random >> 8) * (1.0f / 16777216.0f);
}

void EntropyEngine::prepare(double sampleRate, int channels)
{
    rate = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 384000.0) : 48000.0;
    preparedChannels = std::clamp(channels, 1, 2);
    latency = static_cast<int>(std::lround(rate * 0.010));
    rampSamples = static_cast<int>(rate * 0.040);
    envelopeDecay = std::exp(-1.0f / static_cast<float>(rate * 0.12));
    gapSlew = 1.0f - std::exp(-1.0f / static_cast<float>(rate * 0.0015));
    dcPole = std::exp(-2.0f * pi * 12.0f / static_cast<float>(rate));
    for (auto& channel : history)
        channel.assign(static_cast<size_t>(std::ceil(rate * 1.6)) + 8, 0.0f);
    prepared = true;
    reset();
}

void EntropyEngine::reset() noexcept
{
    for (auto& channel : history)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (int i = 0; i < carrierCount; ++i)
    {
        lanes[static_cast<size_t>(i)] = Lane {};
        lanes[static_cast<size_t>(i)].random = 0x1234abcdU + static_cast<std::uint32_t>(i) * 0x9e3779b9U;
        weights[static_cast<size_t>(i)].snap(controls.carrier == i ? 1.0f : 0.0f);
    }
    age.snap(controls.entropy);
    generations.snap(static_cast<float>(controls.generations));
    for (int c = 0; c < carrierCount; ++c)
        for (int row = 0; row < 5; ++row)
            intensity[static_cast<size_t>(c)][static_cast<size_t>(row)].snap(
                controls.intensity[static_cast<size_t>(c * 5 + row)]);
    mix.snap(controls.mix);
    inputGain.snap(dbGain(controls.inputDb));
    outputGain.snap(dbGain(controls.outputDb));
    bypass.snap(controls.bypass ? 1.0f : 0.0f);
    writePosition = controlClock = 0;
    phase = 0.0;
    envelope = inPeak = outPeak = 0.0f;
}

void EntropyEngine::setControls(const Controls& values) noexcept
{
    controls.carrier = std::clamp(values.carrier, 0, 3);
    controls.entropy = finiteClamp(values.entropy, 0.0f, 1.0f);
    controls.mix = finiteClamp(values.mix, 0.0f, 1.0f, 1.0f);
    controls.inputDb = finiteClamp(values.inputDb, -24.0f, 12.0f);
    controls.outputDb = finiteClamp(values.outputDb, -24.0f, 12.0f);
    controls.generations = std::clamp(values.generations, 1, 8);
    controls.bypass = values.bypass;
    for (int c = 0; c < carrierCount; ++c)
        for (int row = 0; row < 5; ++row)
        {
            controls.intensity[static_cast<size_t>(c * 5 + row)] =
                finiteClamp(values.intensity[static_cast<size_t>(c * 5 + row)], 0.0f, 1.0f, 1.0f);
            intensity[static_cast<size_t>(c)][static_cast<size_t>(row)].set(
                controls.intensity[static_cast<size_t>(c * 5 + row)], rampSamples);
        }
    age.set(controls.entropy, rampSamples);
    generations.set(static_cast<float>(controls.generations), rampSamples);
    mix.set(controls.mix, rampSamples);
    inputGain.set(dbGain(controls.inputDb), rampSamples);
    outputGain.set(dbGain(controls.outputDb), rampSamples);
    for (int i = 0; i < carrierCount; ++i)
        weights[static_cast<size_t>(i)].set(controls.carrier == i ? 1.0f : 0.0f, rampSamples);
}

float EntropyEngine::read(int channel, float delaySamples) const noexcept
{
    const auto& buffer = history[static_cast<size_t>(channel)];
    const int size = static_cast<int>(buffer.size());
    const float delay = std::clamp(delaySamples, 1.0f, static_cast<float>(size - 2));
    float position = static_cast<float>(writePosition) - delay;
    if (position < 0.0f)
        position += static_cast<float>(size);
    const int a = static_cast<int>(position);
    const int b = a + 1 == size ? 0 : a + 1;
    return interpolate(buffer[static_cast<size_t>(a)], buffer[static_cast<size_t>(b)], position - static_cast<float>(a));
}

std::array<float, 2> EntropyEngine::processLane(int index, int channels, float gain, float gate,
                                               float wow, float flutter, float warp) noexcept
{
    auto& lane = lanes[static_cast<size_t>(index)];
    const auto& r = lane.recipe;
    const float sr = static_cast<float>(rate);
    const float randomValue = lane.uniform();
    if (lane.gapRemaining == 0 && randomValue < r.gapRate / sr)
        lane.gapRemaining = std::max(1, static_cast<int>(r.gapMs * (0.45f + lane.uniform()) * 0.001f * sr));
    const float gapTarget = lane.gapRemaining > 0 ? 1.0f - r.gapDepth : 1.0f;
    lane.gapGain += gapSlew * (gapTarget - lane.gapGain);
    if (lane.gapRemaining > 0)
        --lane.gapRemaining;
    if (lane.uniform() < r.eventRate / sr)
    {
        if (index == 1)
            lane.impulse = (2.0f * lane.uniform() - 1.0f) * r.eventLevel;
        if (index == 3)
            lane.holdRemaining = 2 + static_cast<int>(lane.uniform() * 5.0f);
    }
    if (index == 3 && lane.birdieRemaining == 0 && lane.uniform() < r.birdieRate / sr)
    {
        lane.birdieDuration = std::max(1, static_cast<int>((0.008f + 0.016f * lane.uniform()) * sr));
        lane.birdieRemaining = lane.birdieDuration;
        lane.birdiePhase = 0.0f;
        lane.birdieAmp = 0.09f + 0.09f * lane.uniform();
        lane.birdieStart = 1200.0f + 1500.0f * lane.uniform();
        lane.birdieEnd = 5200.0f + 3200.0f * lane.uniform();
    }
    lane.impulse *= 1.0f - 1200.0f / sr;
    float birdie = 0.0f;
    if (index == 3 && lane.birdieRemaining > 0)
    {
        const float progress = 1.0f - static_cast<float>(lane.birdieRemaining) / static_cast<float>(lane.birdieDuration);
        const float frequency = lane.birdieStart + (lane.birdieEnd - lane.birdieStart) * progress;
        lane.birdiePhase += 2.0f * pi * frequency / sr;
        birdie = std::sin(lane.birdiePhase) * lane.birdieAmp * std::sin(pi * progress);
        --lane.birdieRemaining;
    }
    std::array<float, 2> result {};
    for (int ch = 0; ch < channels; ++ch)
    {
        const size_t c = static_cast<size_t>(ch);
        const float white = 2.0f * lane.uniform() - 1.0f;
        const float pitch = index == 1 ? warp : wow;
        const float flutterValue = index == 3 ? white : flutter;
        const float azimuth = (ch == 0 ? -1.0f : 1.0f) * r.driftMs * wow;
        const float modulation = r.wowMs * pitch + r.flutterMs * flutterValue + azimuth;
        const float delay = static_cast<float>(latency) + modulation * 0.001f * sr;
        float x = read(ch, delay) * gain;
        if (index == 3 && lane.holdRemaining > 0)
            x = interpolate(x, lane.previous[c], r.eventLevel);
        lane.previous[c] = x;

        float filtered = x;
        const int sections = index == 2 ? 3 : 1;
        for (int section = 0; section < sections; ++section)
        {
            const auto& b = lane.coefficients[static_cast<size_t>(section)];
            auto& z1 = lane.filter[c][static_cast<size_t>(section * 2)];
            auto& z2 = lane.filter[c][static_cast<size_t>(section * 2 + 1)];
            const float output = b[0] * filtered + z1;
            z1 = b[1] * filtered - b[3] * output + z2;
            z2 = b[2] * filtered - b[4] * output;
            filtered = output;
        }
        float y = interpolate(x, filtered, r.filterAmount);
        if (r.saturation > 0.0f)
        {
            const float drive = 1.0f + r.saturation;
            const float bias = index == 0 ? 0.07f * r.saturation : 0.0f;
            const float distorted = (std::tanh(y * drive + bias) - std::tanh(bias)) / std::sqrt(drive);
            const float blocked = distorted - lane.dcIn[c] + dcPole * lane.dcOut[c];
            lane.dcIn[c] = distorted;
            lane.dcOut[c] = blocked;
            y = interpolate(y, blocked, std::min(1.0f, r.saturation));
        }
        if (index == 2)
        {
            // The 10 ms lookahead permits energy 2-5 ms ahead of the aligned main transient.
            const float pre = (read(ch, static_cast<float>(latency) - sr * 0.002f)
                             + read(ch, static_cast<float>(latency) - sr * 0.0035f)
                             + read(ch, static_cast<float>(latency) - sr * 0.005f)) * gain / 3.0f;
            y = interpolate(y, 0.65f * filtered + 0.35f * pre, r.smear);
            const float comb = read(ch, static_cast<float>(latency) + sr * (0.0017f + 0.0003f * wow)) * gain;
            y += r.residue * (x - filtered) * (0.5f * flutter + 0.15f * comb);
        }
        if (index == 0)
            y += r.echo * read(ch, static_cast<float>(latency) + 1.5f * sr) * gain;
        lane.pink[c] = 0.985f * lane.pink[c] + 0.015f * white;
        const float texture = index == 0 ? 0.68f * white + 2.0f * lane.pink[c] : white;
        y *= lane.gapGain;
        // 黑胶 click 与光盘 birdie 都是音频自身的表面/读错效果，不随 SURFACE 缩放；
        // hiss/dust 底噪仍按 SURFACE 缩放。事件与噪声都随输入尾音淡出。
        y += gate * (birdie + r.noise * texture + lane.impulse * (0.65f + 0.35f * white));
        result[c] = y;
    }
    if (lane.holdRemaining > 0)
        --lane.holdRemaining;
    if (channels == 2)
    {
        const float mid = (result[0] + result[1]) * 0.5f;
        const float side = (result[0] - result[1]) * 0.5f * r.width;
        result = { mid + side, mid - side };
    }
    return result;
}

void EntropyEngine::process(float* const* data, int numChannels, int numSamples, bool hostBypassed) noexcept
{
    inPeak = outPeak = 0.0f;
    if (!prepared || data == nullptr || numSamples <= 0 || numChannels <= 0)
        return;
    const int count = std::min({ numChannels, preparedChannels, 2 });
    bypass.set(hostBypassed || controls.bypass ? 1.0f : 0.0f, std::max(1, rampSamples / 4));
    const float sr = static_cast<float>(rate);
    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < count; ++ch)
        {
            const float x = finiteClamp(data[ch][i], -32.0f, 32.0f);
            history[static_cast<size_t>(ch)][static_cast<size_t>(writePosition)] = x;
            peak = std::max(peak, std::abs(x));
        }
        inPeak = std::max(inPeak, peak);
        envelope = std::max(peak, envelope * envelopeDecay);
        const float gate = std::clamp((envelope - 0.000001f) * 3000.0f, 0.0f, 1.0f);
        const float e = age.next();
        const float generationValue = generations.next();
        if (controlClock == 0)
        {
            for (int c = 0; c < carrierCount; ++c)
            {
                auto& lane = lanes[static_cast<size_t>(c)];
                lane.recipe = makeRecipe(static_cast<Carrier>(c), e, generationValue);
                std::array<float, 5> laneIntensity {};
                for (int row = 0; row < 5; ++row)
                    laneIntensity[static_cast<size_t>(row)] =
                        intensity[static_cast<size_t>(c)][static_cast<size_t>(row)].next();
                applyModuleIntensity(lane.recipe, static_cast<Carrier>(c), laneIntensity.data());
                applyModuleBypass(lane.recipe, static_cast<Carrier>(c), (moduleMask >> (c * 5)) & 0x1fu);
                const float target = std::min(lane.recipe.cutoffHz, sr * 0.45f);
                lane.cutoff += (1.0f - std::exp(-16.0f / (sr * 0.01f))) * (target - lane.cutoff);
                const float omega = 2.0f * pi * std::min(lane.cutoff, sr * 0.45f) / sr;
                const float cosine = std::cos(omega), sine = std::sin(omega);
                constexpr std::array<float, 3> q { 0.51763809f, 0.70710678f, 1.93185165f };
                for (int section = 0; section < (c == 2 ? 3 : 1); ++section)
                {
                    const float alpha = sine / (2.0f * (c == 2 ? q[static_cast<size_t>(section)] : 0.70710678f));
                    const float a0 = 1.0f + alpha;
                    lane.coefficients[static_cast<size_t>(section)] = {
                        (1.0f - cosine) * 0.5f / a0, (1.0f - cosine) / a0,
                        (1.0f - cosine) * 0.5f / a0, -2.0f * cosine / a0, (1.0f - alpha) / a0
                    };
                }
            }
        }
        controlClock = (controlClock + 1) % 16;
        const float in = inputGain.next(), out = outputGain.next();
        const float amount = mix.next(), bypassAmount = bypass.next();
        const float wow = static_cast<float>(0.78 * std::sin(2.0 * pi * 0.83 * phase) + 0.22 * std::sin(2.0 * pi * 1.37 * phase));
        const float flutter = static_cast<float>(std::sin(2.0 * pi * 27.3 * phase));
        const float warp = static_cast<float>(0.82 * std::sin(2.0 * pi * (5.0 / 9.0) * phase) + 0.18 * std::sin(4.0 * pi * (5.0 / 9.0) * phase));
        std::array<float, 2> wet {};
        for (int c = 0; c < carrierCount; ++c)
        {
            const float weight = weights[static_cast<size_t>(c)].next();
            const auto frame = processLane(c, count, in, gate, wow, flutter, warp);
            for (int ch = 0; ch < count; ++ch)
                wet[static_cast<size_t>(ch)] += frame[static_cast<size_t>(ch)] * weight;
        }
        for (int ch = 0; ch < count; ++ch)
        {
            const float raw = read(ch, static_cast<float>(latency));
            const float processed = interpolate(raw * in, wet[static_cast<size_t>(ch)], amount) * out;
            const float y = interpolate(processed, raw, bypassAmount);
            data[ch][i] = std::isfinite(y) ? y : 0.0f;
            outPeak = std::max(outPeak, std::abs(data[ch][i]));
        }
        if (++writePosition == static_cast<int>(history[0].size()))
            writePosition = 0;
        phase += 1.0 / rate;
    }
}
}
