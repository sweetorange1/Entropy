#pragma once
#include "PluginProcessor.h"
#include "UI/ScientificLookAndFeel.h"
#include "UI/TimelineControl.h"
#include "UI/PresetPanel.h"

class EntropyAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EntropyAudioProcessorEditor(EntropyAudioProcessor&);
    ~EntropyAudioProcessorEditor() override;
    void advanceAnimationForTesting(int ticks) { for (int i = 0; i < ticks; ++i) timerCallback(); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    static float intensityFromBarPosition(float scaledX) noexcept { return juce::jlimit(0.0f, 1.0f, (scaledX - 696.0f) / 232.0f); }
    static const char* moduleLabel(int carrier, int row) noexcept;

private:
    juce::Rectangle<float> websiteBounds() const;
    juce::Rectangle<float> formulaBounds() const;
    juce::Rectangle<float> prevPresetBounds() const;
    juce::Rectangle<float> nextPresetBounds() const;
    juce::Rectangle<float> bypassBounds() const;
    juce::Rectangle<float> descriptorRowBounds(int row) const;
    juce::Rectangle<float> descriptorResetBounds() const;
    int descriptorRowAt(juce::Point<float> position) const;
    void timerCallback() override;
    void showArchive();
    void hideArchive();
    void choosePresetFile(bool save);
    void saveSnapshot(const juce::File&);
    void stepPreset(int delta);
    enum class DescriptorIcon { bandwidth, transport, events, dropout, stereo, surface, bitrate, smear, birdie };
    void drawSpecimen(juce::Graphics&, const entropy::Controls&, int fromCarrier, float transition);
    void drawCarrierTexture(juce::Graphics&, const entropy::Controls&, int fromCarrier, float transition);
    void drawDescriptorIcon(juce::Graphics&, DescriptorIcon, juce::Rectangle<float>, float amount, juce::Colour, bool active);
    void drawDescriptors(juce::Graphics&, const entropy::Controls&, juce::Colour accent);
    void beginKnobEdit(int index);
    void commitKnobEdit();
    void cancelKnobEdit();
    void beginTimelineEdit();
    void commitTimelineEdit();
    void cancelTimelineEdit();
    void text(juce::Graphics&, const juce::String&, float x, float y, float w, float h,
              float size, juce::Colour colour, juce::Justification align = juce::Justification::centredLeft);
    juce::Rectangle<int> scaled(int x, int y, int w, int h) const;

    EntropyAudioProcessor& processor;
    entropy::ui::ScientificLookAndFeel look;
    entropy::ui::NumericSlider inputSlider, mixSlider, outputSlider;
    entropy::ui::KnobInputEditor knobInput, timelineInput;
    entropy::ui::TimelineControl timelineControl;
    juce::Label selectedDateLabel;
    std::array<juce::Slider*, 3> knobSliders {};
    std::array<const char*, 3> knobIds {};
    std::array<bool, 3> knobPercent {};
    int editingSlider = -1;
    int dragRow = -1;
    float dragStartX = 0.0f, dragStartIntensity = 1.0f;
    bool dragMoved = false;
    int carrierFrom = 0, carrierTarget = 0;
    float carrierTransition = 1.0f;
    juce::Colour displayAccent = entropy::ui::carrierColour(0);
    std::array<juce::TextButton, 4> carrierButtons;
    juce::TextButton generation { "GEN 01" };
    entropy::ui::PresetPanel presetPanel;
    bool websiteHovered = false, formulaHovered = false, prevHovered = false,
         nextHovered = false, bypassHovered = false, descriptorResetHovered = false;
    int descriptorHover = -1;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> attachments;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::TooltipWindow tooltip { this, 650 };
    float scale = 1.0f, animation = 0.0f, meterIn = 0.0f, meterOut = 0.0f;
    juce::String status;
    int statusTicks = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EntropyAudioProcessorEditor)
};
