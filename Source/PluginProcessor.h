#pragma once
#include <JuceHeader.h>
#include "Parameters.h"
#include "Dsp/EntropyEngine.h"
#include "Timeline.h"

class EntropyAudioProcessor final : public juce::AudioProcessor,
                                    private juce::AudioProcessorParameter::Listener
{
public:
    explicit EntropyAudioProcessor(const std::atomic<juce::int64>* clockForTesting = nullptr);
    ~EntropyAudioProcessor() override;
    const juce::String getName() const override { return JucePlugin_Name; }
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    bool restoreState(const void*, int);
    entropy::Controls currentControls() const noexcept;
    entropy::timeline::Snapshot timelineSnapshot() const noexcept;
    void selectDate(juce::int64 utcMilliseconds);
    void beginDateGesture();
    void endDateGesture();
    bool isModuleBypassed(int carrier, int row) const noexcept;
    void setModuleBypass(int carrier, int row, bool bypassed);
    void resetModuleBypass() noexcept;
    float moduleIntensityValue(int carrier, int row) const noexcept;
    void setModuleIntensity(int carrier, int row, float value);
    void resetModuleSettings();
    void setParameter(const char* id, float plainValue);
    void loadFactoryPreset(int);
    int matchingFactoryPreset() const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float> inputLevel { 0.0f }, outputLevel { 0.0f };
    std::atomic<int> editorWidth { 960 };

private:
    void parameterValueChanged(int, float) override;
    void parameterGestureChanged(int, bool) override {}
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool);
    std::array<std::atomic<float>*, 7> raw {};
    entropy::timeline::Clock wallClock;
    std::atomic<juce::int64> selectedDate { 0 };
    std::atomic<std::uint32_t> moduleBypass { 0 };
    std::array<std::atomic<float>, 20> moduleIntensity {};
    entropy::EntropyEngine engine;
    bool ready = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EntropyAudioProcessor)
};
