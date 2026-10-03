#pragma once
#include <array>
#include <cstdint>

namespace entropy
{
enum class Carrier { tape, vinyl, streaming, optical };
constexpr int carrierCount = 4;

// 每载体 5 行效果强度；仅磁带/黑胶/光盘的 SURFACE 行（hiss 底噪）默认 0.6，
// 与旧版全局 Surface noise 默认值保持一致，其余行默认满强度。
inline std::array<float, 20> defaultIntensity() noexcept
{
    std::array<float, 20> values;
    values.fill(1.0f);
    for (int carrier : { 0, 1, 3 })
        values[static_cast<size_t>(carrier * 5 + 4)] = 0.6f;
    return values;
}

struct Controls
{
    int carrier = 0;
    float entropy = 0.0f;
    float mix = 1.0f;
    float inputDb = 0.0f;
    float outputDb = 0.0f;
    int generations = 1;
    bool bypass = false;
    std::array<float, 20> intensity = defaultIntensity();
};

struct Recipe
{
    float cutoffHz = 20000.0f;
    float filterAmount = 0.0f;
    float wowMs = 0.0f;
    float flutterMs = 0.0f;
    float driftMs = 0.0f;
    float saturation = 0.0f;
    float noise = 0.0f;
    float eventRate = 0.0f;
    float eventLevel = 0.0f;
    float gapRate = 0.0f;
    float gapDepth = 0.0f;
    float gapMs = 0.0f;
    float width = 1.0f;
    float smear = 0.0f;
    float echo = 0.0f;
    float residue = 0.0f;
    float birdieRate = 0.0f;
    int bitrate = 320;
    int correctionStage = 0;
};

Recipe makeRecipe(Carrier, float entropy, float generations = 1.0f) noexcept;
void applyModuleIntensity(Recipe&, Carrier, const float* intensity) noexcept;
void applyModuleBypass(Recipe&, Carrier, std::uint32_t mask) noexcept;
float surfaceNoiseMax(Carrier) noexcept;
const char* carrierName(int) noexcept;
const char* carrierMechanism(int) noexcept;
const char* degradationStage(float) noexcept;
float finiteClamp(float value, float low, float high, float fallback = 0.0f) noexcept;
}
