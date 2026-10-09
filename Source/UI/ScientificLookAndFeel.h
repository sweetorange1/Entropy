#pragma once
#include <JuceHeader.h>
#include <functional>

namespace entropy::ui
{
inline const juce::Colour ink { 0xff292d2e };
inline const juce::Colour muted { 0xff858983 };
inline const juce::Colour line { 0xffe5e7e0 };
inline const juce::Colour paper { 0xfff7f8f4 };
inline const juce::Colour blue { 0xff2a6fb0 };
inline juce::Colour carrierColour(int index)
{
    // 参考 Organic Chemistry 的 CPK 元素配色，提高饱和度，让四载体在白底上更鲜明。
    constexpr std::array<juce::uint32, 4> colours { 0xffb05a44, 0xff6e9a52, 0xff3a78c2, 0xff8764a6 };
    return juce::Colour(colours[static_cast<size_t>(juce::jlimit(0, 3, index))]);
}

inline juce::Colour blendColour(juce::Colour from, juce::Colour to, float t) noexcept
{
    const float u = juce::jlimit(0.0f, 1.0f, t);
    return juce::Colour::fromFloatRGBA(
        from.getFloatRed() + (to.getFloatRed() - from.getFloatRed()) * u,
        from.getFloatGreen() + (to.getFloatGreen() - from.getFloatGreen()) * u,
        from.getFloatBlue() + (to.getFloatBlue() - from.getFloatBlue()) * u,
        from.getFloatAlpha() + (to.getFloatAlpha() - from.getFloatAlpha()) * u);
}

class ScientificLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    float scale = 1.0f;
    juce::Colour accent = carrierColour(0);
    ScientificLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::highlightColourId, blue.withAlpha(0.18f));
        setColour(juce::TextEditor::focusedOutlineColourId, blue);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colours::white);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, paper);
        setColour(juce::PopupMenu::highlightedTextColourId, blue);
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colours::white);
        setColour(juce::TooltipWindow::textColourId, ink);
        setColour(juce::TooltipWindow::outlineColourId, line);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::Font(juce::FontOptions(12.0f * scale)); }
    juce::Font getLabelFont(juce::Label&) override { return juce::Font(juce::FontOptions(12.0f * scale)); }
    juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions(13.0f * scale)); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool hover, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f * scale);
        const bool active = button.getToggleState();
        g.setColour(active ? accent.withAlpha(0.10f) : (hover || down ? paper : juce::Colours::white));
        g.fillRoundedRectangle(bounds, 3.0f * scale);
        g.setColour(active ? accent.withAlpha(0.4f) : line);
        g.drawRoundedRectangle(bounds, 3.0f * scale, scale);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        g.setColour((button.getToggleState() ? accent : ink).withAlpha(button.isEnabled() ? 1.0f : 0.35f));
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.drawText(button.getButtonText(), button.getLocalBounds().reduced(4), juce::Justification::centred);
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float value,
                          float start, float end, juce::Slider& slider) override
    {
        // 与 Organic Chemistry 反应剖面（ADSR）旋钮同款：5 段仪表刻度、
        // 强调色值弧、浅色圆形主体与同色指针，无满程轨道弧。
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height)).reduced(8.0f * scale);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f * 0.9f;
        const auto centre = bounds.getCentre();
        const float angle = start + value * (end - start);
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        g.setColour(juce::Colour(0xffd8d8d1).withAlpha(alpha));
        for (int j = 0; j <= 4; ++j)
        {
            const float a = start + (static_cast<float>(j) / 4.0f) * (end - start);
            const float r1 = radius + 4.0f * scale, r2 = radius + 7.0f * scale;
            g.drawLine(centre.x + std::sin(a) * r1, centre.y - std::cos(a) * r1,
                       centre.x + std::sin(a) * r2, centre.y - std::cos(a) * r2, scale);
        }
        juce::Path arc;
        constexpr int steps = 24;
        for (int j = 0; j <= steps; ++j)
        {
            const float t = static_cast<float>(j) / static_cast<float>(steps);
            const float a = start + (angle - start) * t;
            const float ax = centre.x + std::sin(a) * (radius + 2.0f * scale);
            const float ay = centre.y - std::cos(a) * (radius + 2.0f * scale);
            if (j == 0) arc.startNewSubPath(ax, ay);
            else arc.lineTo(ax, ay);
        }
        g.setColour((slider.isEnabled() ? accent : muted).withAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(2.0f * scale));
        g.setColour(juce::Colour(0xfff7f7f3).withAlpha(alpha));
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
        g.setColour(juce::Colour(0xffd2d2cb).withAlpha(alpha));
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), scale);
        g.setColour((slider.isEnabled() ? accent : muted).withAlpha(alpha));
        g.drawLine(centre.x, centre.y, centre.x + std::sin(angle) * (radius - 2.5f * scale),
                   centre.y - std::cos(angle) * (radius - 2.5f * scale), 2.0f * scale);
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                          float, float, const juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float centre = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        g.setColour(line);
        g.fillRect(static_cast<float>(x), centre - scale, static_cast<float>(width), 2.0f * scale);
        g.setColour(accent);
        g.fillRect(static_cast<float>(x), centre - scale, juce::jmax(0.0f, position - static_cast<float>(x)), 2.0f * scale);
        g.setColour(juce::Colours::white);
        g.fillEllipse(position - 7.0f * scale, centre - 7.0f * scale, 14.0f * scale, 14.0f * scale);
        g.setColour(accent);
        g.drawEllipse(position - 7.0f * scale, centre - 7.0f * scale, 14.0f * scale, 14.0f * scale, 1.5f * scale);
    }
};

class KnobEditorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) override
    {
        g.setColour(juce::Colours::white);
        g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 4.0f);
    }
    void drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor&) override
    {
        g.setColour(juce::Colour(0xffc9c9c2));
        g.drawRoundedRectangle(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 4.0f, 1.0f);
    }
};

class KnobInputEditor final : public juce::TextEditor
{
public:
    KnobInputEditor()
    {
        setLookAndFeel(&laf);
        setJustification(juce::Justification::centred);
        setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
        setColour(juce::TextEditor::textColourId, juce::Colour(0xff2a2a28));
        setColour(juce::TextEditor::highlightColourId, juce::Colour(0x332a6fb0));
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        setColour(juce::CaretComponent::caretColourId, juce::Colour(0xff2a2a28));
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentWhite);
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff2a6fb0));
        setFont(juce::Font(juce::FontOptions(12.0f)));
        setInputRestrictions(0, "0123456789.-");
        setSelectAllWhenFocused(true);
    }
private:
    KnobEditorLookAndFeel laf;
};

class NumericSlider final : public juce::Slider
{
public:
    std::function<void()> onKnobEdit;
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (onKnobEdit) onKnobEdit();
        else showTextBox();
    }
};
}
