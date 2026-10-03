#pragma once
#include "Recipes.h"
#include <array>
#include <cstdint>
#include <vector>

namespace entropy
{
class EntropyEngine
{
public:
    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setControls(const Controls&) noexcept;
    void setModuleBypass(std::uint32_t mask) noexcept { moduleMask = mask; }
    void process(float* const* channels, int numChannels, int numSamples, bool hostBypassed = false) noexcept;
    int latencySamples() const noexcept { return latency; }
    float inputPeak() const noexcept { return inPeak; }
    float outputPeak() const noexcept { return outPeak; }

private:
    struct Ramp
    {
        float current = 0.0f, target = 0.0f, step = 0.0f;
        int remaining = 0;
        void snap(float v) noexcept { current = target = v; step = 0; remaining = 0; }
        void set(float v, int samples) noexcept;
        float next() noexcept;
    };
    struct Lane
    {
        Recipe recipe;
        std::array<std::array<float, 6>, 2> filter {};
        std::array<std::array<float, 5>, 3> coefficients {};
        std::array<float, 2> pink {}, dcIn {}, dcOut {}, previous {};
        float cutoff = 20000.0f;
        float gapGain = 1.0f, impulse = 0.0f;
        float birdiePhase = 0.0f, birdieAmp = 0.0f, birdieStart = 0.0f, birdieEnd = 0.0f;
        int gapRemaining = 0, holdRemaining = 0, birdieRemaining = 0, birdieDuration = 1;
        std::uint32_t random = 1;
        float uniform() noexcept;
    };
    float read(int channel, float delaySamples) const noexcept;
    std::array<float, 2> processLane(int carrier, int channels, float gain, float gate,
                                    float wow, float flutter, float warp) noexcept;
    double rate = 48000.0;
    int preparedChannels = 2, latency = 480, writePosition = 0, controlClock = 0, rampSamples = 1920;
    std::array<std::vector<float>, 2> history;
    std::array<Lane, 4> lanes;
    std::array<Ramp, 4> weights;
    std::array<std::array<Ramp, 5>, 4> intensity;
    Ramp age, mix, inputGain, outputGain, bypass, generations;
    std::uint32_t moduleMask = 0;
    Controls controls;
    double phase = 0.0;
    float envelope = 0.0f, envelopeDecay = 0.0f, gapSlew = 0.0f, dcPole = 0.0f;
    float inPeak = 0.0f, outPeak = 0.0f;
    bool prepared = false;
};
}
