#pragma once
#include "ScientificLookAndFeel.h"
#include "../Timeline.h"

namespace entropy::ui
{
class TimelineControl final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    std::function<void(double)> onSelectPosition;
    std::function<void()> onGestureBegin, onGestureEnd, onEditDate;

    TimelineControl()
    {
        setName("Carrier date timeline");
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Drag to pick a date - double-click to type 0-100");
    }
    ~TimelineControl() override { finishGesture(); }

    void setSnapshot(timeline::Snapshot value)
    {
        snapshot = value;
        repaint();
    }
    void setAccent(juce::Colour value)
    {
        accentOverride = value;
        repaint();
    }
    bool isDragging() const noexcept { return dragging; }
    double getPosition() const noexcept { return snapshot.position(); }

    void paint(juce::Graphics& g) override
    {
        const float scale = static_cast<float>(getWidth()) / 578.0f;
        g.addTransform(juce::AffineTransform::scale(scale));
        const auto accent = accentOverride.isTransparent() ? carrierColour(snapshot.carrier) : accentOverride;
        constexpr float left = 10, right = 568;
        const float height = static_cast<float>(getHeight()) / scale;
        const float y = juce::jmax(17.0f, height * 0.40f);
        const float thumb = left + static_cast<float>(snapshot.position()) * (right - left);
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.17f), left, y,
                                              accent.withAlpha(0.015f), right, y, false));
        g.fillRoundedRectangle(left, y - 12, right - left, 12, 2);
        g.setColour(line);
        g.drawLine(left, y, right, y, 1.2f);
        g.setColour(accent.withAlpha(0.65f));
        g.drawLine(thumb, y, right, y, 1.5f);
        for (int tick = 0; tick <= 40; ++tick)
        {
            const float position = static_cast<float>(tick) / 40.0f;
            const float x = left + position * (right - left);
            const bool major = tick % 10 == 0;
            g.setColour(major ? muted : line);
            g.drawLine(x, y + 3, x, y + (major ? 11.0f : 7.0f), major ? 1.0f : 0.8f);
            if (major)
            {
                const auto date = timeline::dateAt(1.0f - position, snapshot.now, snapshot.carrier);
                const auto label = tick == 40 ? juce::String("NOW") : timeline::tickLabel(date, snapshot.carrier == 2);
                const float start = tick == 0 ? left : tick == 40 ? right - 92 : x - 46;
                g.setColour(ink.withAlpha(0.75f));
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText(label, juce::Rectangle<float>(start, y + 16, 92, 16),
                    tick == 0 ? juce::Justification::centredLeft : tick == 40 ? juce::Justification::centredRight : juce::Justification::centred);
            }
        }
        g.setColour(juce::Colours::white);
        g.fillEllipse(thumb - 6, y - 6, 12, 12);
        g.setColour(accent);
        g.drawEllipse(thumb - 6, y - 6, 12, 12, 1.5f);
        g.drawLine(thumb, y - 15, thumb, y - 9, 1.3f);
        juce::Font sigma(juce::FontOptions(9.5f).withStyle("Italic"));
        g.setFont(sigma);
        g.setColour(muted.withAlpha(0.85f));
        g.drawText(juce::String::fromUTF8("σ = dS/dt ≥ 0"), juce::Rectangle<float>(right - 165, y - 21, 165, 15),
                   juce::Justification::centredRight, false);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        stopTimer();
        pressed = event.mods.isLeftButtonDown() && event.getNumberOfClicks() == 1;
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!pressed || !event.mouseWasDraggedSinceMouseDown()) return;
        if (!dragging)
        {
            dragging = true;
            if (onGestureBegin) onGestureBegin();
        }
        if (onSelectPosition) onSelectPosition(mousePosition(event));
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (dragging) finishGesture();
        else if (pressed)
        {
            pendingPosition = mousePosition(event);
            startTimer(juce::MouseEvent::getDoubleClickTimeout() + 20);
        }
        pressed = false;
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        stopTimer();
        pressed = false;
        finishGesture();
        if (onEditDate) onEditDate();
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        stopTimer();
        const double step = key.getModifiers().isShiftDown() ? 0.001 : 0.01;
        auto position = snapshot.position();
        if (key.isKeyCode(juce::KeyPress::leftKey)) position -= step;
        else if (key.isKeyCode(juce::KeyPress::rightKey)) position += step;
        else if (key.isKeyCode(juce::KeyPress::homeKey)) position = 0;
        else if (key.isKeyCode(juce::KeyPress::endKey)) position = 1;
        else if (key.isKeyCode(juce::KeyPress::returnKey)) { if (onEditDate) onEditDate(); return true; }
        else return false;
        if (onGestureBegin) onGestureBegin();
        if (onSelectPosition) onSelectPosition(juce::jlimit(0.0, 1.0, position));
        if (onGestureEnd) onGestureEnd();
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { stopTimer(); pressed = false; finishGesture(); repaint(); }
private:
    void timerCallback() override
    {
        stopTimer();
        if (onGestureBegin) onGestureBegin();
        if (onSelectPosition) onSelectPosition(pendingPosition);
        if (onGestureEnd) onGestureEnd();
    }
    double mousePosition(const juce::MouseEvent& event) const
    {
        const double scale = static_cast<double>(getWidth()) / 578.0;
        return juce::jlimit(0.0, 1.0, (static_cast<double>(event.position.x) / scale - 10.0) / 558.0);
    }
    void finishGesture()
    {
        if (!dragging) return;
        dragging = false;
        if (onGestureEnd) onGestureEnd();
    }
    timeline::Snapshot snapshot;
    double pendingPosition = 1.0;
    juce::Colour accentOverride = juce::Colours::transparentBlack;
    bool dragging = false, pressed = false;
};
}
