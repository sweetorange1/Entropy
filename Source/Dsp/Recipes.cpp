#include "Recipes.h"
#include <algorithm>
#include <cmath>

namespace entropy
{
float finiteClamp(float value, float low, float high, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

namespace
{
float onset(float e, float start) noexcept
{
    const float x = std::clamp((e - start) / (1.0f - start), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
}

void applyModuleIntensity(Recipe& r, Carrier carrier, const float* intensity) noexcept
{
    const auto level = [&](int row)
    {
        const float value = intensity[row];
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f;
    };
    // width 从 1 塌缩到配方值，因此按 (1-x) 部分插值，其余字段均为线性缩放。
    const auto narrow = [](float value, float amount) { return 1.0f + (value - 1.0f) * amount; };
    switch (carrier)
    {
        case Carrier::tape:
        {
            const float i0 = level(0), i1 = level(1), i2 = level(2), i3 = level(3), i4 = level(4);
            r.cutoffHz = 20000.0f + (r.cutoffHz - 20000.0f) * i0;
            r.filterAmount *= i0; r.saturation *= i0;
            r.wowMs *= i1; r.flutterMs *= i1; r.driftMs *= i1;
            r.gapRate *= i2; r.gapDepth *= i2; r.gapMs *= i2; r.echo *= i2;
            r.width = narrow(r.width, i3);
            r.noise *= i4;
            break;
        }
        case Carrier::vinyl:
        {
            const float i0 = level(0), i1 = level(1), i2 = level(2), i3 = level(3), i4 = level(4);
            r.cutoffHz = 20000.0f + (r.cutoffHz - 20000.0f) * i0;
            r.filterAmount *= i0; r.saturation *= i0;
            r.wowMs *= i1;
            r.eventRate *= i2; r.eventLevel *= i2;
            r.width = narrow(r.width, i3);
            r.noise *= i4;
            break;
        }
        case Carrier::streaming:
        {
            const float i0 = level(0), i1 = level(1), i2 = level(2), i3 = level(3), i4 = level(4);
            r.filterAmount *= i0;
            r.cutoffHz = 20000.0f + (r.cutoffHz - 20000.0f) * i1;
            r.gapRate *= i2; r.gapDepth *= i2; r.gapMs *= i2;
            r.width = narrow(r.width, i3);
            r.smear *= i4; r.residue *= i4;
            break;
        }
        case Carrier::optical:
        {
            const float i0 = level(0), i1 = level(1), i2 = level(2), i3 = level(3), i4 = level(4);
            r.cutoffHz = 20000.0f + (r.cutoffHz - 20000.0f) * i0;
            r.filterAmount *= i0;
            r.flutterMs *= i1;
            r.eventRate *= i2; r.eventLevel *= i2; r.gapRate *= i2; r.gapDepth *= i2; r.gapMs *= i2;
            r.birdieRate *= i3;
            r.noise *= i4;
            break;
        }
    }
}

float surfaceNoiseMax(Carrier carrier) noexcept
{
    constexpr std::array<float, 4> maxima { 0.032f, 0.020f, 0.017f, 0.017f };
    return maxima[static_cast<size_t>(std::clamp(static_cast<int>(carrier), 0, 3))];
}

void applyModuleBypass(Recipe& r, Carrier carrier, std::uint32_t mask) noexcept
{
    const auto bypassed = [&](int row) { return (mask >> row) & 1u; };
    switch (carrier)
    {
        case Carrier::tape:
            if (bypassed(0)) { r.cutoffHz = 20000.0f; r.filterAmount = 0.0f; r.saturation = 0.0f; }
            if (bypassed(1)) { r.wowMs = 0.0f; r.flutterMs = 0.0f; r.driftMs = 0.0f; }
            if (bypassed(2)) { r.gapRate = 0.0f; r.gapDepth = 0.0f; r.gapMs = 0.0f; r.echo = 0.0f; }
            if (bypassed(3)) r.width = 1.0f;
            if (bypassed(4)) r.noise = 0.0f;
            break;
        case Carrier::vinyl:
            if (bypassed(0)) { r.cutoffHz = 20000.0f; r.filterAmount = 0.0f; r.saturation = 0.0f; }
            if (bypassed(1)) r.wowMs = 0.0f;
            if (bypassed(2)) { r.eventRate = 0.0f; r.eventLevel = 0.0f; }
            if (bypassed(3)) r.width = 1.0f;
            if (bypassed(4)) r.noise = 0.0f;
            break;
        case Carrier::streaming:
            if (bypassed(0)) r.filterAmount = 0.0f;
            if (bypassed(1)) r.cutoffHz = 20000.0f;
            if (bypassed(2)) { r.gapRate = 0.0f; r.gapDepth = 0.0f; r.gapMs = 0.0f; }
            if (bypassed(3)) r.width = 1.0f;
            if (bypassed(4)) { r.smear = 0.0f; r.residue = 0.0f; }
            break;
        case Carrier::optical:
            if (bypassed(0)) { r.cutoffHz = 20000.0f; r.filterAmount = 0.0f; }
            if (bypassed(1)) r.flutterMs = 0.0f;
            if (bypassed(2)) { r.eventRate = 0.0f; r.eventLevel = 0.0f; r.gapRate = 0.0f; r.gapDepth = 0.0f; r.gapMs = 0.0f; }
            if (bypassed(3)) r.birdieRate = 0.0f;
            if (bypassed(4)) r.noise = 0.0f;
            break;
    }
}

Recipe makeRecipe(Carrier carrier, float value, float generations) noexcept
{
    const float e = finiteClamp(value, 0.0f, 1.0f);
    Recipe r;
    switch (carrier)
    {
        case Carrier::tape:
        {
            // 储存环境（高温/高湿/杂散磁场）s ∈ [0,1]：退磁加速 → 高频损失更陡；
            // 磁层水解粘连 → 走带不稳；磁粉脱落 → dropout 更频繁；氧化 → 底噪抬升。
            const float s = (finiteClamp(generations, 1.0f, 8.0f, 1.0f) - 1.0f) / 7.0f;
            r.cutoffHz = 20000.0f * std::pow(0.18f, e * (1.0f + 0.5f * s));
            r.filterAmount = onset(e, 0.0f);
            r.wowMs = 2.1f * onset(e, 0.22f) * (1.0f + 0.4f * s);
            r.flutterMs = 0.10f * onset(e, 0.43f) * (1.0f + 0.4f * s);
            r.driftMs = 0.30f * onset(e, 0.38f) * (1.0f + 0.4f * s);
            r.saturation = 2.8f * onset(e, 0.12f);
            r.noise = 0.032f * e * e * (1.0f + 1.5f * s);
            r.gapRate = 5.0f * onset(e, 0.27f) * (1.0f + 1.2f * s);
            r.gapDepth = 0.72f * onset(e, 0.25f);
            r.gapMs = 8.0f + 30.0f * e;
            r.width = 1.0f - 0.62f * onset(e, 0.4f);
            r.echo = 0.0178f * onset(e, 0.35f);
            break;
        }
        case Carrier::vinyl:
        {
            // 熵增原则：灰尘随时间“变多”，表现为爆音频率上升；单颗粒的爆音幅度
            // 与颗粒大小有关、基本恒定，不应随时间越变越响。
            // 储存环境（灰尘/霉变/翘曲）s ∈ [0,1]：灰尘霉变 → 底噪 + 爆音频率；
            // 翘曲 → 偏心 wow 加深。
            const float s = (finiteClamp(generations, 1.0f, 8.0f, 1.0f) - 1.0f) / 7.0f;
            r.cutoffHz = 20000.0f * std::pow(0.23f, e);
            r.filterAmount = onset(e, 0.10f);
            r.wowMs = 2.5f * onset(e, 0.45f) * (1.0f + 0.4f * s);
            r.saturation = 3.6f * onset(e, 0.28f);
            r.noise = 0.020f * onset(e, 0.30f) * (1.0f + 1.5f * s);
            r.eventRate = (0.6f * e + 70.0f * e * e) * (1.0f + 1.2f * s);
            r.eventLevel = 0.10f + 0.08f * e;
            break;
        }
        case Carrier::streaming:
        {
            const int step = std::min(5, static_cast<int>(e * 6.0f));
            constexpr std::array<int, 6> rates { 320, 256, 192, 128, 96, 64 };
            constexpr std::array<float, 6> cuts { 20000, 19000, 17500, 16000, 13500, 11000 };
            // 代际损失 loss ∈ [0,1]：每次重复转码都在前一代的损失上再叠一层，
            // 因此 GEN 只放大已有退化（乘 e），不改变零熵端的无损语义。
            // 连续类按深度增长：高频截止、声场塌缩、预回声涂抹、金属残留；
            // 事件类按频率增长：低码率流的丢包陷落更频繁。
            const float loss = (finiteClamp(generations, 1.0f, 8.0f, 1.0f) - 1.0f) / 7.0f;
            r.bitrate = rates[static_cast<size_t>(step)];
            r.cutoffHz = cuts[static_cast<size_t>(step)] * (1.0f - 0.72f * loss * e);
            r.filterAmount = onset(e, 0.0f);
            r.width = 1.0f - onset(e, 0.10f) * (0.80f + 0.20f * loss);
            r.smear = onset(e, 0.28f) * (0.24f + 0.52f * loss);
            r.residue = onset(e, 0.40f) * (0.20f + 0.48f * loss);
            r.gapRate = (2.0f + 4.0f * loss) * onset(e, 0.60f);
            r.gapDepth = onset(e, 0.45f);
            r.gapMs = 50.0f + 250.0f * e;
            break;
        }
        case Carrier::optical:
        {
            // 储存环境（光照/高温/氧化/划痕）s ∈ [0,1]：染料退化与反射层氧化 →
            // 读错与误纠鸟鸣更频繁；划痕 → 陷落更多；底噪抬升。
            const float s = (finiteClamp(generations, 1.0f, 8.0f, 1.0f) - 1.0f) / 7.0f;
            r.cutoffHz = 20000.0f * (1.0f - 0.68f * onset(e, 0.4f));
            r.filterAmount = onset(e, 0.35f);
            r.flutterMs = 0.013f * onset(e, 0.05f);
            r.noise = 0.017f * onset(e, 0.50f) * (1.0f + 1.2f * s);
            r.eventRate = 45.0f * onset(e, 0.25f) * (1.0f + 1.2f * s);
            r.eventLevel = onset(e, 0.25f);
            r.gapRate = 9.0f * onset(e, 0.50f) * (1.0f + 1.0f * s);
            r.gapDepth = onset(e, 0.40f);
            r.gapMs = 3.0f + 160.0f * onset(e, 0.5f);
            r.correctionStage = e < 0.28f ? 0 : (e < 0.58f ? 1 : (e < 0.93f ? 2 : 3));
            r.birdieRate = 7.0f * onset(e, 0.30f) * (1.0f + 1.2f * s);
            break;
        }
    }
    return r;
}

const char* carrierName(int index) noexcept
{
    constexpr std::array<const char*, 4> names { "TAPE", "VINYL", "STREAM", "PHASE" };
    return names[static_cast<size_t>(std::clamp(index, 0, 3))];
}

const char* carrierMechanism(int index) noexcept
{
    constexpr std::array<const char*, 4> names {
        "Magnetic tape - decays over 60 years",
        "Vinyl record - wears over 100 years",
        "Digital stream - degrades over 24 hours",
        "Optical disc - degrades over 40 years"
    };
    return names[static_cast<size_t>(std::clamp(index, 0, 3))];
}

const char* storageName(int carrier, int level) noexcept
{
    // 8 个等级：从理想储存到极端恶劣。STREAM 保持转码代数语义（GEN 01–08）。
    constexpr std::array<std::array<const char*, 8>, 4> names {{
        { "VAULT", "CLIMATE", "CABINET", "DRAWER", "ATTIC", "GARAGE", "BASEMENT", "MAGNET" },
        { "RACK", "SLEEVE", "PAPER", "STACKED", "ATTIC", "GARAGE", "DAMP", "SUN" },
        { "GEN 01", "GEN 02", "GEN 03", "GEN 04", "GEN 05", "GEN 06", "GEN 07", "GEN 08" },
        { "CASE", "CABINET", "DESK", "LOOSE", "SILL", "CAR", "DAMP", "SUN" }
    }};
    const int c = std::clamp(carrier, 0, 3);
    const int l = std::clamp(level, 1, 8) - 1;
    return names[static_cast<size_t>(c)][static_cast<size_t>(l)];
}

const char* storageTooltip(int carrier) noexcept
{
    constexpr std::array<const char*, 4> tips {
        "Storage conditions - heat, humidity and stray fields",
        "Storage conditions - dust, mould and warping",
        "Transcoding generations - click to cycle",
        "Storage conditions - light, heat and oxidation"
    };
    return tips[static_cast<size_t>(std::clamp(carrier, 0, 3))];
}

const char* degradationStage(float e) noexcept
{
    return e < 0.02f ? "PRISTINE" : e < 0.25f ? "TRACES" : e < 0.5f ? "WEATHERED" : e < 0.75f ? "UNSTABLE" : e < 0.94f ? "DECAY" : "COLLAPSE";
}
}
