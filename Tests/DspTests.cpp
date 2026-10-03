#include "Dsp/EntropyEngine.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
thread_local bool trackAllocations = false;
std::atomic<int> allocations { 0 };
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
using Audio = std::array<std::vector<float>, 2>;
Audio source(int samples, double rate)
{
    Audio audio;
    for (int ch = 0; ch < 2; ++ch)
    {
        audio[static_cast<size_t>(ch)].resize(static_cast<size_t>(samples));
        for (int i = 0; i < samples; ++i)
            audio[static_cast<size_t>(ch)][static_cast<size_t>(i)] = static_cast<float>(0.18 * std::sin(i * 0.077 + ch * 1.7)
                + 0.07 * std::sin(i * 2.0 * 3.141592653589793 * 7500.0 / rate));
    }
    return audio;
}
Audio render(entropy::Controls c, const Audio& input, double rate, int channels, int block,
             bool hostBypass = false, std::uint32_t mask = 0)
{
    entropy::EntropyEngine engine;
    engine.setControls(c);
    engine.prepare(rate, channels);
    engine.setModuleBypass(mask);
    require(engine.latencySamples() == static_cast<int>(std::lround(rate * 0.01)), "Incorrect reported latency");
    Audio result = input;
    const int size = static_cast<int>(result[0].size());
    for (int offset = 0; offset < size; offset += block)
    {
        std::array<float*, 2> pointers { result[0].data() + offset, result[1].data() + offset };
        trackAllocations = true;
        engine.process(pointers.data(), channels, std::min(block, size - offset), hostBypass);
        trackAllocations = false;
    }
    require(allocations.load() == 0, "Allocation detected in DSP callback");
    return result;
}
void identityTests()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            const int frames = static_cast<int>(sr * 0.09);
            const auto input = source(frames, sr);
            for (int carrier = 0; carrier < 4; ++carrier)
                for (int mode = 0; mode < 4; ++mode)
                {
                    entropy::Controls c;
                    c.carrier = carrier;
                    c.entropy = mode == 0 ? 0.0f : 0.9f;
                    c.mix = mode == 1 ? 0.0f : 1.0f;
                    c.bypass = mode == 2;
                    if (mode >= 2) { c.inputDb = 7.0f; c.outputDb = -9.0f; }
                    const auto output = render(c, input, sr, channels, 127, mode == 3);
                    const int delay = static_cast<int>(std::lround(sr * 0.01));
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = mode == 3 ? delay * 2 : 0; i < frames; ++i)
                        {
                            const float expected = i < delay ? 0.0f : input[static_cast<size_t>(ch)][static_cast<size_t>(i - delay)];
                            require(std::abs(output[static_cast<size_t>(ch)][static_cast<size_t>(i)] - expected) < 0.000002f,
                                    "Pristine, dry or bypass is not latency-aligned transparent");
                        }
                }
        }
    std::cout << "PASS pristine / dry / plugin bypass / host bypass, mono-stereo, 44.1-192 kHz\n";
}
void partitionTests()
{
    const auto input = source(18000, 48000.0);
    for (int carrier = 0; carrier < 4; ++carrier)
    {
        entropy::Controls c;
        c.carrier = carrier;
        c.entropy = 0.86f;
        c.generations = 7;
        const auto a = render(c, input, 48000, 2, 1);
        const auto b = render(c, input, 48000, 2, 511);
        const auto large = render(c, input, 48000, 2, 18000);
        double difference = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < a[0].size(); ++i)
            {
                require(a[static_cast<size_t>(ch)][i] == b[static_cast<size_t>(ch)][i]
                    && b[static_cast<size_t>(ch)][i] == large[static_cast<size_t>(ch)][i], "DSP depends on block partition");
                require(std::isfinite(a[static_cast<size_t>(ch)][i]) && std::abs(a[static_cast<size_t>(ch)][i]) < 4.0f,
                        "Nonfinite or excessive output");
                if (i >= 480)
                    difference += std::abs(a[static_cast<size_t>(ch)][i] - input[static_cast<size_t>(ch)][i - 480]);
            }
        require(difference > 10.0, "Carrier does not change the sound");
    }
    std::cout << "PASS four audible recipes / deterministic events / block partition invariance\n";
}
void stressTests()
{
    for (double sr : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        entropy::EntropyEngine engine;
        engine.prepare(sr, 2);
        std::array<std::array<float, 257>, 2> data {};
        std::array<float*, 2> pointers { data[0].data(), data[1].data() };
        for (int block = 0; block < 700; ++block)
        {
            entropy::Controls c;
            c.carrier = (block / 5) % 4;
            c.entropy = static_cast<float>(block % 101) / 100.0f;
            c.mix = block % 13 == 0 ? 0.0f : 1.0f;
            c.intensity[static_cast<size_t>(block % 20)] = static_cast<float>(block % 7) / 6.0f;
            c.inputDb = block % 2 == 0 ? 12.0f : -24.0f;
            c.outputDb = -6.0f;
            c.generations = block % 8 + 1;
            c.bypass = block % 11 == 0;
            if (block == 10) c.entropy = std::numeric_limits<float>::quiet_NaN();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 257; ++i)
                    data[static_cast<size_t>(ch)][static_cast<size_t>(i)] = static_cast<float>(0.15 * std::sin((block * 257 + i) * 0.08));
            if (block == 15) data[0][12] = std::numeric_limits<float>::infinity();
            trackAllocations = true;
            engine.setControls(c);
            engine.process(pointers.data(), 2, block % 19 == 0 ? 0 : 257);
            trackAllocations = false;
            for (const auto& channel : data)
                for (float value : channel)
                    require(std::isfinite(value) && std::abs(value) < 16.0f, "Automation destabilised DSP");
        }
        engine.reset();
        engine.prepare(sr, 1);
        engine.process(nullptr, 0, 0);
    }
    require(allocations.load() == 0, "Allocation detected under automation");
    std::cout << "PASS automation / nonfinite guards / zero blocks / repeated prepare / no callback allocations\n";
}
void recipeTests()
{
    for (int carrier = 0; carrier < 4; ++carrier)
    {
        float previousCut = 20001.0f;
        for (int step = 0; step <= 100; ++step)
        {
            const auto r = entropy::makeRecipe(static_cast<entropy::Carrier>(carrier), static_cast<float>(step) * 0.01f, 4);
            require(r.cutoffHz <= previousCut + 0.01f, "Bandwidth trajectory is not monotonic");
            require(r.width >= 0.0f && r.width <= 1.0f, "Invalid stereo trajectory");
            previousCut = r.cutoffHz;
        }
    }
    {
        const auto vinylHalf = entropy::makeRecipe(entropy::Carrier::vinyl, 0.5f, 1);
        const auto vinylFull = entropy::makeRecipe(entropy::Carrier::vinyl, 1.0f, 1);
        require(vinylFull.eventRate > 60.0f && vinylFull.eventLevel > 0.09f && vinylFull.eventLevel < 0.25f,
                "Vinyl dust must raise click frequency with a clear but bounded click level");
        require(vinylHalf.eventLevel >= vinylFull.eventLevel * 0.3f,
                "Vinyl click amplitude must stay nearly constant over time");
        const auto tapeFull = entropy::makeRecipe(entropy::Carrier::tape, 1.0f, 1);
        require(tapeFull.gapDepth <= 0.75f && tapeFull.gapMs <= 40.0f, "Tape dropout must stay brief");
    }
    auto input = source(10000, 48000);
    entropy::Controls optical;
    optical.carrier = 3;
    optical.entropy = 1.0f;
    const auto output = render(optical, input, 48000, 2, 128);
    float energy = 0.0f;
    for (const auto& channel : output)
        for (float value : channel)
        {
            require(std::isfinite(value), "Nonfinite optical output");
            energy += std::abs(value);
        }
    require(energy > 0.5f, "Optical endpoint must stay audible instead of muting");
    const auto noBirdies = render(optical, input, 48000, 2, 128, false, 1u << 18);
    double difference = 0.0;
    for (size_t i = 480; i < input[0].size(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            difference += std::abs(output[static_cast<size_t>(ch)][i] - noBirdies[static_cast<size_t>(ch)][i]);
    require(difference > 0.02, "Bypassing miscorrection must remove the birdie artefacts");
    for (int carrier = 0; carrier < 4; ++carrier)
    {
        auto tail = source(144000, 48000);
        for (auto& channel : tail)
            std::fill(channel.begin() + 1000, channel.end(), 0.0f);
        entropy::Controls c;
        c.carrier = carrier; c.entropy = 0.9f;
        const auto faded = render(c, tail, 48000, 2, 512);
        for (const auto& channel : faded)
            for (size_t i = 130000; i < channel.size(); ++i)
                require(std::abs(channel[i]) < 0.000001f, "Idle noise does not decay to silence");
    }
    std::cout << "PASS monotonic recipes / optical birdie endpoint / idle silence\n";
}

void moduleBypassTests()
{
    for (int carrier = 0; carrier < 4; ++carrier)
    {
        auto recipe = entropy::makeRecipe(static_cast<entropy::Carrier>(carrier), 1.0f, 7);
        entropy::applyModuleBypass(recipe, static_cast<entropy::Carrier>(carrier), 0x1fu);
        require(recipe.cutoffHz == 20000.0f && recipe.filterAmount == 0.0f, "Bypass left HF loss");
        require(recipe.wowMs == 0.0f && recipe.flutterMs == 0.0f && recipe.driftMs == 0.0f, "Bypass left pitch drift");
        require(recipe.saturation == 0.0f, "Bypass left saturation");
        require(recipe.noise == 0.0f, "Bypass left surface noise");
        require(recipe.eventRate == 0.0f && recipe.eventLevel == 0.0f, "Bypass left impulse events");
        require(recipe.gapRate == 0.0f && recipe.gapDepth == 0.0f && recipe.gapMs == 0.0f, "Bypass left dropouts");
        require(recipe.width == 1.0f, "Bypass left stereo collapse");
        require(recipe.smear == 0.0f && recipe.echo == 0.0f && recipe.residue == 0.0f && recipe.birdieRate == 0.0f,
                "Bypass left residue, echo or birdies");
        auto partial = entropy::makeRecipe(static_cast<entropy::Carrier>(carrier), 1.0f, 7);
        entropy::applyModuleBypass(partial, static_cast<entropy::Carrier>(carrier), 0x0u);
        const auto pristine = entropy::makeRecipe(static_cast<entropy::Carrier>(carrier), 0.0f, 7);
        require(partial.cutoffHz < 20000.0f, "Empty mask accidentally restored the bandwidth");
        require(recipe.cutoffHz == pristine.cutoffHz && recipe.filterAmount == pristine.filterAmount
                && recipe.width == pristine.width && recipe.smear == pristine.smear
                && recipe.noise == pristine.noise && recipe.birdieRate == pristine.birdieRate,
                "Full bypass must equal the pristine entropy-zero recipe");
    }
    const auto input = source(20000, 48000.0);
    entropy::Controls streaming;
    streaming.carrier = 2;
    streaming.entropy = 1.0f;
    streaming.generations = 8;
    const auto output = render(streaming, input, 48000, 2, 127, false, 0x1fu << 10);
    const int delay = static_cast<int>(std::lround(48000.0 * 0.01));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = delay; i < 20000; ++i)
            require(std::abs(output[static_cast<size_t>(ch)][static_cast<size_t>(i)]
                - input[static_cast<size_t>(ch)][static_cast<size_t>(i - delay)]) < 0.000002f,
                    "Fully bypassed streaming must be transparent");
    std::cout << "PASS module bypass masks / fully bypassed streaming transparency\n";
}

void vinylClickIndependenceTests()
{
    // SURFACE 行强度 = 0 时黑胶 click 仍应存在：爆音是音频自身的表面撞击效果，
    // 不属于 hiss/dust 噪声床，不应被 SURFACE 行缩放。
    const auto input = source(22050, 48000.0);
    entropy::Controls vinyl;
    vinyl.carrier = 1;
    vinyl.entropy = 0.8f;
    vinyl.mix = 1.0f;
    vinyl.intensity[static_cast<size_t>(1 * 5 + 4)] = 0.0f;
    const auto withClicks = render(vinyl, input, 48000, 2, 127, false, 0);
    const auto withoutClicks = render(vinyl, input, 48000, 2, 127, false, 1u << 7);
    double clickEnergy = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 480; i < input[0].size(); ++i)
        {
            const auto a = withClicks[static_cast<size_t>(ch)][i];
            const auto b = withoutClicks[static_cast<size_t>(ch)][i];
            require(std::isfinite(a) && std::isfinite(b), "Nonfinite vinyl output");
            clickEnergy += std::abs(a - b);
        }
    require(clickEnergy > 0.05, "Vinyl clicks disappeared when the SURFACE row was zero");
    std::cout << "PASS vinyl clicks independent of the SURFACE row intensity\n";
}

void intensityTests()
{
    // 强度 0 应完全移除对应行退化（等价 pristine）；强度 1 保持配方；0.5 线性折半。
    for (int carrier = 0; carrier < 4; ++carrier)
    {
        const auto c = static_cast<entropy::Carrier>(carrier);
        const auto full = entropy::makeRecipe(c, 1.0f, 7);
        const auto pristine = entropy::makeRecipe(c, 0.0f, 7);
        std::array<float, 5> zeroLevels {};
        auto zero = full;
        entropy::applyModuleIntensity(zero, c, zeroLevels.data());
        // gapMs 在 gapRate 为 0 时不产生任何声音，零强度时归零与 pristine 的基准时长不同是预期的；
        // eventLevel 同理：黑胶/光盘的脉冲幅度存在基准值，脉冲率为 0 时不产生声音。
        require(zero.cutoffHz == pristine.cutoffHz && zero.filterAmount == pristine.filterAmount
                && zero.wowMs == pristine.wowMs && zero.flutterMs == pristine.flutterMs
                && zero.driftMs == pristine.driftMs && zero.saturation == pristine.saturation
                && zero.noise == pristine.noise && zero.eventRate == pristine.eventRate
                && zero.gapRate == pristine.gapRate
                && zero.gapDepth == pristine.gapDepth
                && zero.width == pristine.width && zero.smear == pristine.smear
                && zero.echo == pristine.echo && zero.residue == pristine.residue
                && zero.birdieRate == pristine.birdieRate,
                (std::string("Zero intensity must equal the pristine recipe, carrier ") + std::to_string(carrier)).c_str());
        std::array<float, 5> oneLevels { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        auto one = full;
        entropy::applyModuleIntensity(one, c, oneLevels.data());
        const auto close = [](float a, float b)
        { return std::abs(a - b) <= 0.000001f * std::max(1.0f, std::abs(b)); };
        require(close(one.cutoffHz, full.cutoffHz) && close(one.width, full.width) && close(one.noise, full.noise)
                && close(one.gapRate, full.gapRate) && close(one.smear, full.smear) && close(one.birdieRate, full.birdieRate),
                "Full intensity must keep the recipe");
        std::array<float, 5> halfLevels { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
        auto half = full;
        entropy::applyModuleIntensity(half, c, halfLevels.data());
        require(half.cutoffHz > full.cutoffHz && half.cutoffHz < pristine.cutoffHz,
                "Half intensity bandwidth is not between endpoints");
        if (full.width < 1.0f)
            require(half.width > full.width && half.width < 1.0f, "Half intensity stereo is not between endpoints");
        if (full.noise > 0.0f)
            require(std::abs(half.noise - full.noise * 0.5f) < 0.000001f, "Half intensity noise is not halved");
        if (full.gapRate > 0.0f)
            require(std::abs(half.gapRate - full.gapRate * 0.5f) < 0.000001f, "Half intensity gaps are not halved");
        if (full.smear > 0.0f)
            require(std::abs(half.smear - full.smear * 0.5f) < 0.000001f, "Half intensity smear is not halved");
        if (full.birdieRate > 0.0f)
            require(std::abs(half.birdieRate - full.birdieRate * 0.5f) < 0.000001f, "Half intensity birdies are not halved");
        auto tampered = full;
        const std::array<float, 5> badLevels { std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN() };
        entropy::applyModuleIntensity(tampered, c, badLevels.data());
        require(close(tampered.cutoffHz, full.cutoffHz) && close(tampered.filterAmount, full.filterAmount)
                && close(tampered.noise, full.noise) && close(tampered.birdieRate, full.birdieRate)
                && close(tampered.width, full.width) && close(tampered.gapRate, full.gapRate),
                (std::string("Nonfinite intensity must fall back to full strength, carrier ") + std::to_string(carrier)).c_str());
    }
    {
        // 磁带第二行把 gap 与 echo 一起缩放；流媒体第四行把 smear 与 residue 一起缩放。
        const auto tape = entropy::makeRecipe(entropy::Carrier::tape, 1.0f, 1);
        std::array<float, 5> levels { 1.0f, 1.0f, 0.25f, 1.0f, 1.0f };
        auto scaled = tape;
        entropy::applyModuleIntensity(scaled, entropy::Carrier::tape, levels.data());
        require(std::abs(scaled.gapDepth - tape.gapDepth * 0.25f) < 0.000001f
                && std::abs(scaled.echo - tape.echo * 0.25f) < 0.000001f,
                "Tape discontinuity row must scale dropouts and echo together");
        const auto streaming = entropy::makeRecipe(entropy::Carrier::streaming, 1.0f, 8);
        levels = { 1.0f, 1.0f, 1.0f, 1.0f, 0.2f };
        auto streamScaled = streaming;
        entropy::applyModuleIntensity(streamScaled, entropy::Carrier::streaming, levels.data());
        require(std::abs(streamScaled.smear - streaming.smear * 0.2f) < 0.000001f
                && std::abs(streamScaled.residue - streaming.residue * 0.2f) < 0.000001f,
                "Streaming smear row must scale smear and residue together");
    }
    require(std::abs(entropy::surfaceNoiseMax(entropy::Carrier::tape) - 0.032f) < 0.000001f
            && entropy::surfaceNoiseMax(entropy::Carrier::vinyl) < entropy::surfaceNoiseMax(entropy::Carrier::tape),
            "Surface noise normalisation maxima are wrong");
    std::cout << "PASS per-row intensity scaling / pristine at zero / halves / fallback / grouped rows\n";
}
}

void* operator new(std::size_t size)
{
    if (trackAllocations) ++allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    try
    {
        recipeTests();
        identityTests();
        partitionTests();
        stressTests();
        moduleBypassTests();
        vinylClickIndependenceTests();
        intensityTests();
        std::cout << "Entropy DSP tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        trackAllocations = false;
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
