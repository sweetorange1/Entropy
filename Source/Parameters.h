#pragma once
#include <JuceHeader.h>
#include "Dsp/Recipes.h"

namespace entropy
{
namespace ids
{
inline constexpr auto carrier = "carrier";
inline constexpr auto entropy = "entropy";
inline constexpr auto mix = "mix";
inline constexpr auto input = "input";
inline constexpr auto output = "output";
inline constexpr auto generations = "generations";
inline constexpr auto bypass = "bypass";
inline constexpr std::array<const char*, 7> all { carrier, entropy, mix, input, output, generations, bypass };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
struct FactoryPreset
{
    const char* name;
    int carrier;
    float entropy;
    int generations;
    std::array<float, 5> intensity;
};
const std::array<FactoryPreset, 20>& factoryPresets();
}
