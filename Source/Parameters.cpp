#include "Parameters.h"

namespace entropy
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto percentage = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return juce::String(v * 100.0f, 1) + " %"; })
        .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue() * 0.01f; });
    const auto decibels = juce::AudioParameterFloatAttributes().withLabel("dB")
        .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " dB"; });
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { ids::carrier, 1 }, "Carrier",
        juce::StringArray { "Tape", "Vinyl", "Streaming", "Optical / Phase" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids::entropy, 1 }, "Entropy",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.0f, percentage));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids::mix, 1 }, "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f, percentage));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids::input, 1 }, "Input",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, decibels));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids::output, 1 }, "Output",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, decibels));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { ids::generations, 1 }, "Transcode generations", 1, 8, 1));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids::bypass, 1 }, "Bypass", false));
    return layout;
}

const std::array<FactoryPreset, 20>& factoryPresets()
{
    // 每载体 5 个：崭新参考 → 轻度/中度/重度特征 → 彻底失序。
    // entropy 与该载体的时间尺度对齐（TAPE 60 年、VINYL 100 年、STREAM 24 小时、PHASE 40 年），
    // 名称中的年份与“现在 ≈ 2026”下的对应日期大致一致，是设计命名而非标定。
    // intensity 为该载体五行强度：行 0 带宽、行 1 走带/码率、行 2 断续、
    // 行 3 声场/误纠、行 4 表面残留/SMEAR。崭新参考使用默认强度。
    static const std::array<FactoryPreset, 20> presets {{
        { "Fresh stock", 0, 0.0f, 1, { 1.0f, 1.0f, 1.0f, 1.0f, 0.6f } },
        { "2001 / Warm archive", 0, 0.42f, 1, { 0.85f, 0.70f, 0.55f, 0.90f, 0.50f } },
        { "1989 / Fading domains", 0, 0.62f, 1, { 1.0f, 0.45f, 0.75f, 0.70f, 0.75f } },
        { "1978 / Wandering transport", 0, 0.80f, 1, { 0.60f, 1.0f, 0.50f, 0.80f, 0.40f } },
        { "1968 / Oxide memory", 0, 0.97f, 1, { 1.0f, 0.90f, 1.0f, 1.0f, 0.80f } },
        { "First pressing", 1, 0.0f, 1, { 1.0f, 1.0f, 1.0f, 1.0f, 0.6f } },
        { "1989 / Paper sleeve", 1, 0.37f, 1, { 0.75f, 0.50f, 0.50f, 1.0f, 0.70f } },
        { "1973 / Dust in the groove", 1, 0.53f, 1, { 0.85f, 0.35f, 1.0f, 1.0f, 0.60f } },
        { "1961 / Off-centre", 1, 0.65f, 1, { 0.55f, 1.0f, 0.35f, 1.0f, 0.45f } },
        { "1929 / Worn to noise", 1, 0.97f, 1, { 1.0f, 0.75f, 0.90f, 1.0f, 0.90f } },
        { "Lossless reference", 2, 0.0f, 1, { 1.0f, 1.0f, 1.0f, 1.0f, 0.6f } },
        { "Early web / 192k", 2, 0.33f, 1, { 0.90f, 0.80f, 0.40f, 0.85f, 0.70f } },
        { "2001 / 128k", 2, 0.52f, 2, { 1.0f, 0.90f, 0.60f, 0.80f, 0.80f } },
        { "Generation seven", 2, 0.79f, 7, { 0.95f, 0.90f, 0.70f, 1.0f, 1.0f } },
        { "Buffer exhausted", 2, 0.97f, 8, { 1.0f, 1.0f, 1.0f, 1.0f, 0.90f } },
        { "Crystalline", 3, 0.0f, 1, { 1.0f, 1.0f, 1.0f, 1.0f, 0.6f } },
        { "2016 / Correctable", 3, 0.25f, 1, { 0.85f, 0.50f, 0.50f, 0.40f, 0.60f } },
        { "2008 / Interpolation", 3, 0.45f, 1, { 0.90f, 0.60f, 1.0f, 0.50f, 0.50f } },
        { "1997 / Unreadable sectors", 3, 0.72f, 1, { 0.80f, 0.50f, 0.90f, 1.0f, 0.50f } },
        { "1986 / Amorphous", 3, 1.0f, 1, { 1.0f, 1.0f, 1.0f, 1.0f, 0.80f } }
    }};
    return presets;
}
}
