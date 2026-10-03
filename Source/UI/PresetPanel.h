#pragma once
#include "ScientificLookAndFeel.h"
#include "../Parameters.h"

namespace entropy::ui
{
class PresetPanel final : public juce::Component
{
public:
    std::function<void(int)> onChosen;
    std::function<void()> onDismiss, onSave, onOpen;

    PresetPanel()
    {
        setName("Preset archive panel");
        setWantsKeyboardFocus(true);
        setVisible(false);
    }
    void open(int selected)
    {
        current = selected;
        focused = selected >= 0 ? selected : 0;
        hovered = -1;
        setVisible(true);
        toFront(false);
        if (isShowing()) grabKeyboardFocus();
        repaint();
    }
    void setCurrentIndex(int index)
    {
        if (current != index) { current = index; repaint(); }
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::white.withAlpha(0.25f));
        g.addTransform(juce::AffineTransform::scale(uiScale()));
        const auto card = cardBounds();
        for (int layer = 3; layer > 0; --layer)
        {
            g.setColour(juce::Colours::black.withAlpha(0.018f));
            g.fillRoundedRectangle(card.expanded(static_cast<float>(layer) * 2).translated(0, 2), 6);
        }
        g.setColour(juce::Colours::white);
        g.fillRoundedRectangle(card, 5);
        g.setColour(juce::Colour(0xffe2e2dc));
        g.drawRoundedRectangle(card, 5, 1);
        drawText(g, "PRESETS", { 81, 70, 200, 23 }, 11, ink);
        drawText(g, "20 archives", { 722, 70, 157, 23 }, 10.5f, muted, juce::Justification::centredRight);
        g.setColour(line);
        g.drawLine(80, 100, 880, 100, 1);
        for (int carrier = 0; carrier < 4; ++carrier)
        {
            const float x = 80.0f + 200.0f * static_cast<float>(carrier);
            drawText(g, carrierName(carrier), { x + 8, 107, 180, 22 }, 10, carrierColour(carrier));
            if (carrier > 0)
            {
                g.setColour(line);
                g.drawLine(x, 108, x, 261, 1);
            }
        }
        const auto& presets = factoryPresets();
        for (int i = 0; i < 20; ++i)
        {
            const auto bounds = itemBounds(i);
            const bool highlighted = hovered == i || (hasKeyboardFocus(false) && focused == i);
            if (highlighted)
            {
                g.setColour(juce::Colour(0xfff3f7fb));
                g.fillRoundedRectangle(bounds, 3);
            }
            if (current == i)
            {
                g.setColour(blue);
                g.fillRoundedRectangle(bounds.getX(), bounds.getY() + 5, 2, bounds.getHeight() - 10, 1);
            }
            drawText(g, presets[static_cast<size_t>(i)].name, bounds.reduced(8, 0), 12.5f,
                     highlighted || current == i ? blue : ink);
        }
        g.setColour(line);
        g.drawLine(80, 273, 880, 273, 1);
        constexpr std::array<const char*, 3> actions { "Save snapshot...", "Open snapshot...", "Close" };
        for (int i = 20; i < 23; ++i)
        {
            const bool highlighted = hovered == i || (hasKeyboardFocus(false) && focused == i);
            const auto bounds = itemBounds(i);
            if (highlighted)
            {
                g.setColour(juce::Colour(0xfff3f7fb));
                g.fillRoundedRectangle(bounds, 3);
            }
            drawText(g, actions[static_cast<size_t>(i - 20)], bounds.reduced(8, 0), 11.5f,
                     highlighted ? blue : muted, i == 22 ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }
    }
    void mouseMove(const juce::MouseEvent& event) override
    {
        hovered = itemAt(event.position);
        setMouseCursor(hovered >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override { hovered = -1; repaint(); }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isLeftButtonDown()) activate(itemAt(event.position));
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.isKeyCode(juce::KeyPress::escapeKey)) { activate(-1); return true; }
        if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey))
        {
            activate(focused); return true;
        }
        int delta = 0;
        if (key.isKeyCode(juce::KeyPress::downKey)) delta = 1;
        else if (key.isKeyCode(juce::KeyPress::upKey)) delta = -1;
        else if (key.isKeyCode(juce::KeyPress::rightKey)) delta = focused < 20 ? 5 : 1;
        else if (key.isKeyCode(juce::KeyPress::leftKey)) delta = focused < 20 ? -5 : -1;
        else if (key.isKeyCode(juce::KeyPress::tabKey)) delta = key.getModifiers().isShiftDown() ? -1 : 1;
        else return false;
        focused = (focused + delta + 23) % 23;
        repaint();
        return true;
    }
private:
    float uiScale() const noexcept { return static_cast<float>(getWidth()) / 960.0f; }
    static juce::Rectangle<float> cardBounds() { return { 64, 60, 832, 261 }; }
    static juce::Rectangle<float> itemBounds(int index)
    {
        if (index < 20)
            return { 84.0f + 200.0f * static_cast<float>(index / 5),
                     132.0f + 26.0f * static_cast<float>(index % 5), 192, 26 };
        if (index == 20) return { 80, 282, 152, 27 };
        if (index == 21) return { 238, 282, 152, 27 };
        return { 799, 282, 81, 27 };
    }
    int itemAt(juce::Point<float> point) const
    {
        if (getWidth() <= 0) return -1;
        point /= uiScale();
        for (int i = 0; i < 23; ++i)
            if (itemBounds(i).contains(point)) return i;
        return -1;
    }
    static void drawText(juce::Graphics& g, const juce::String& value, juce::Rectangle<float> bounds,
                         float size, juce::Colour colour, juce::Justification align = juce::Justification::centredLeft)
    {
        g.setColour(colour);
        g.setFont(juce::Font(juce::FontOptions(size)));
        g.drawText(value, bounds, align);
    }
    void activate(int index)
    {
        if (index >= 0 && index < 20) { if (onChosen) onChosen(index); }
        else if (index == 20) { if (onSave) onSave(); }
        else if (index == 21) { if (onOpen) onOpen(); }
        else if (onDismiss) onDismiss();
    }
    int current = -1, hovered = -1, focused = 0;
};
}
