#include "PluginEditor.h"
#include "Thermodynamics.h"
#include <cmath>

namespace
{
// 公式统一使用斜体默认 UI 字体（Segoe UI 含 ∂Δσδ≥≈²·½ 全部字形），
// 字符串一律经 fromUTF8 显式解码，避免窄字面量按本地代码页解释。
juce::Font formulaFont(float size)
{
    return juce::Font(juce::FontOptions(size).withStyle("Italic"));
}

// 预设簇固定宽度：所有预设名与 "Custom / Archive" 的最大宽度 + 26 内边距。
// 名称区域不再随当前名称伸缩，左右箭头按钮因此保持不动，连续切换预设时
// 点击位置稳定。仅在首次调用时计算一次（函数内 static 的 magic static）。
float presetClusterWidth()
{
    const juce::Font font(juce::FontOptions(17.0f));
    float w = juce::GlyphArrangement::getStringWidth(font, "Custom / Archive");
    for (const auto& p : entropy::factoryPresets())
        w = juce::jmax(w, juce::GlyphArrangement::getStringWidth(font, p.name));
    return juce::jmax(84.0f, w + 26.0f);
}

// 右侧五条退化描述符的悬停提示：每载体每行一句话说明该退化是什么。
// 布局与 §6.4 的行语义表一致（0 带宽、1 走带/码率、2 断续、3 声场/误纠、4 表面/涂抹）。
const char* descriptorEffectTooltip(int carrier, int row) noexcept
{
    constexpr std::array<const char*, 20> tips {
        // TAPE
        "High-frequency loss + saturation", "Wow, flutter, azimuth drift", "Dropouts + print-through echo",
        "Stereo width collapse", "Tape hiss",
        // VINYL
        "High-frequency loss + groove distortion", "Eccentric pitch wobble", "Dust clicks and crackles",
        "Stereo width (no degradation)", "Dust hiss",
        // STREAM
        "Lossy high-frequency cut", "Bitrate ladder 320-64 kb/s", "Packet-loss dropouts",
        "Stereo width collapse", "Pre-echo smear",
        // PHASE
        "High-frequency loss", "Clock jitter", "Read-error dropouts",
        "Mis-corrected birdie tones", "Disc noise floor"
    };
    const int c = std::clamp(carrier, 0, 3);
    const int r = std::clamp(row, 0, 4);
    return tips[static_cast<size_t>(c * 5 + r)];
}
}

using entropy::ui::ink;
using entropy::ui::muted;
using entropy::ui::paper;
using entropy::ui::blue;
using entropy::ui::carrierColour;

const char* EntropyAudioProcessorEditor::moduleLabel(int carrier, int row) noexcept
{
    constexpr std::array<const char*, 5> common { "BANDWIDTH", "TRANSPORT", "DISCONTINUITIES", "STEREO ORDER", "SURFACE RESIDUE" };
    constexpr std::array<const char*, 5> streaming { "BANDWIDTH", "BITRATE MODEL", "DISCONTINUITIES", "STEREO ORDER", "TRANSIENT SMEAR" };
    const int r = std::clamp(row, 0, 4);
    if (carrier == 2)
        return streaming[static_cast<size_t>(r)];
    if (carrier == 3 && r == 3)
        return "MISCORRECTION";
    return common[static_cast<size_t>(r)];
}

EntropyAudioProcessorEditor::EntropyAudioProcessorEditor(EntropyAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p)
{
    setLookAndFeel(&look);
    setOpaque(true);
    std::array<entropy::ui::NumericSlider*, 3> sliders { &inputSlider, &mixSlider, &outputSlider };
    constexpr std::array<const char*, 3> ids { entropy::ids::input, entropy::ids::mix, entropy::ids::output };
    constexpr std::array<const char*, 3> names { "Input", "Mix", "Output" };
    constexpr std::array<bool, 3> percent { false, true, false };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& slider = *sliders[i];
        slider.setName(names[i]);
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 22);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setScrollWheelEnabled(false);
        addAndMakeVisible(slider);
        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, ids[i], slider);
        knobSliders[i] = &slider;
        knobIds[i] = ids[i];
        knobPercent[i] = percent[i];
        slider.onKnobEdit = [this, index = static_cast<int>(i)] { beginKnobEdit(index); };
    }
    addChildComponent(knobInput);
    knobInput.onReturnKey = [this] { commitKnobEdit(); };
    knobInput.onEscapeKey = [this] { cancelKnobEdit(); };
    knobInput.onFocusLost = [this] { commitKnobEdit(); };
    addChildComponent(timelineInput);
    timelineInput.setName("Timeline percent input");
    timelineInput.onReturnKey = [this] { commitTimelineEdit(); };
    timelineInput.onEscapeKey = [this] { cancelTimelineEdit(); };
    timelineInput.onFocusLost = [this] { commitTimelineEdit(); };
    timelineControl.onGestureBegin = [this] { processor.beginDateGesture(); };
    timelineControl.onGestureEnd = [this] { processor.endDateGesture(); };
    timelineControl.onSelectPosition = [this](double position)
    {
        const auto snapshot = processor.timelineSnapshot();
        processor.selectDate(entropy::timeline::dateAt(static_cast<float>(1.0 - position), snapshot.now, snapshot.carrier));
        timelineControl.setSnapshot(processor.timelineSnapshot());
        selectedDateLabel.setText(entropy::timeline::localDate(processor.timelineSnapshot().selected), juce::dontSendNotification);
        repaint();
    };
    timelineControl.onEditDate = [this] { beginTimelineEdit(); };
    addAndMakeVisible(timelineControl);
    selectedDateLabel.setName("Selected local date");
    selectedDateLabel.setEditable(false, true, false);
    selectedDateLabel.setJustificationType(juce::Justification::centredRight);
    selectedDateLabel.setColour(juce::Label::textColourId, ink);
    selectedDateLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    selectedDateLabel.setTooltip("Double-click to edit the date");
    selectedDateLabel.onTextChange = [this]
    {
        juce::int64 date = 0;
        if (entropy::timeline::parseLocalDate(selectedDateLabel.getText(), date) && date <= processor.timelineSnapshot().now)
        {
            processor.beginDateGesture();
            processor.selectDate(date);
            processor.endDateGesture();
        }
        else
        {
            status = "Invalid local date, nonexistent DST time, or future date.";
            statusTicks = 180;
        }
        selectedDateLabel.setText(entropy::timeline::localDate(processor.timelineSnapshot().selected), juce::dontSendNotification);
        timelineControl.setSnapshot(processor.timelineSnapshot());
        repaint();
    };
    addAndMakeVisible(selectedDateLabel);
    inputSlider.setTooltip("Input trim before degradation");
    mixSlider.setTooltip("Dry/wet mix");
    outputSlider.setTooltip("Output trim");
    for (int i = 0; i < 4; ++i)
    {
        auto& button = carrierButtons[static_cast<size_t>(i)];
        button.setButtonText(entropy::carrierName(i));
        button.setTooltip(entropy::carrierMechanism(i));
        button.onClick = [this, i] { processor.setParameter(entropy::ids::carrier, static_cast<float>(i)); };
        addAndMakeVisible(button);
    }
    addAndMakeVisible(generation);
    generation.onClick = [this]
    {
        const auto c = processor.currentControls();
        processor.setParameter(entropy::ids::generations, static_cast<float>(c.generations % 8 + 1));
    };
    generation.setTooltip("Transcoding generations - click to cycle");
    presetPanel.onChosen = [this](int index)
    {
        hideArchive();
        processor.loadFactoryPreset(index);
    };
    presetPanel.onDismiss = [this] { hideArchive(); };
    presetPanel.onSave = [this] { hideArchive(); choosePresetFile(true); };
    presetPanel.onOpen = [this] { hideArchive(); choosePresetFile(false); };
    addChildComponent(presetPanel);
    const int width = juce::jlimit(720, 1920, processor.editorWidth.load());
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio(1.5);
    setResizeLimits(720, 480, 1920, 1280);
    setSize(width, juce::roundToInt(static_cast<float>(width) / 1.5f));
    timerCallback();
    startTimerHz(30);
}

EntropyAudioProcessorEditor::~EntropyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

juce::Rectangle<int> EntropyAudioProcessorEditor::scaled(int x, int y, int w, int h) const
{
    return { juce::roundToInt(static_cast<float>(x) * scale), juce::roundToInt(static_cast<float>(y) * scale),
             juce::roundToInt(static_cast<float>(w) * scale), juce::roundToInt(static_cast<float>(h) * scale) };
}

void EntropyAudioProcessorEditor::resized()
{
    scale = static_cast<float>(getWidth()) / 960.0f;
    look.scale = scale;
    processor.editorWidth.store(getWidth());
    presetPanel.setBounds(getLocalBounds());
    selectedDateLabel.setFont(juce::Font(juce::FontOptions(17.0f * scale)));
    for (int i = 0; i < 4; ++i)
        carrierButtons[static_cast<size_t>(i)].setBounds(scaled(32 + i * 147, 122, 137, 36));
    timelineControl.setBounds(scaled(32, 452, 578, 80));
    selectedDateLabel.setBounds(scaled(650, 92, 278, 30));
    inputSlider.setBounds(scaled(128, 566, 80, 68));
    mixSlider.setBounds(scaled(272, 566, 80, 68));
    outputSlider.setBounds(scaled(416, 566, 80, 68));
    generation.setBounds(scaled(650, 482, 114, 30));
    for (auto* slider : { &inputSlider, &mixSlider, &outputSlider })
    {
        slider->setMouseDragSensitivity(juce::roundToInt(160.0f * scale));
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false,
                               juce::roundToInt(78.0f * scale), juce::roundToInt(22.0f * scale));
    }
}

void EntropyAudioProcessorEditor::text(juce::Graphics& g, const juce::String& value, float x, float y,
                                      float w, float h, float size, juce::Colour colour, juce::Justification align)
{
    g.setColour(colour);
    g.setFont(juce::Font(juce::FontOptions(size)));
    g.drawText(value, juce::Rectangle<float>(x, y, w, h), align);
}

juce::Rectangle<float> EntropyAudioProcessorEditor::websiteBounds() const
{
    // 顶栏高 52、字体 15：文字中心对齐到顶栏中线（与 Organic Chemistry 46/15 同比例），
    // 因此顶部边距取 (52 - 15)/2 ≈ 17，保证字号与竖直位置视觉一致。
    const juce::Font font(juce::FontOptions(15.0f));
    return { 16.0f, 17.0f, juce::GlyphArrangement::getStringWidth(font, "iisaacbeats.cn"), font.getHeight() };
}

juce::Rectangle<float> EntropyAudioProcessorEditor::formulaBounds() const
{
    // 宽度固定为「最长名称 + 内边距」，以 480 居中。prev/next 按钮紧贴此
    // 固定区域两侧，切换预设时不再随名称长度移动。
    static const float w = presetClusterWidth();
    return { 480.0f - w * 0.5f, 13.0f, w, 26.0f };
}

juce::Rectangle<float> EntropyAudioProcessorEditor::prevPresetBounds() const
{
    const auto f = formulaBounds();
    return { f.getX() - 27.0f, f.getY(), 18.0f, f.getHeight() };
}

juce::Rectangle<float> EntropyAudioProcessorEditor::nextPresetBounds() const
{
    const auto f = formulaBounds();
    return { f.getRight() + 9.0f, f.getY(), 18.0f, f.getHeight() };
}

juce::Rectangle<float> EntropyAudioProcessorEditor::bypassBounds() const
{
    const juce::Font font(juce::FontOptions(12.5f));
    const float w = juce::GlyphArrangement::getStringWidth(font, "BYPASS") + 16.0f;
    return { 946.0f - w, 15.0f, w, 22.0f };
}

juce::Rectangle<float> EntropyAudioProcessorEditor::descriptorRowBounds(int row) const
{
    return { 646.0f, 202.0f + static_cast<float>(row) * 53.0f, 282.0f, 48.0f };
}

juce::Rectangle<float> EntropyAudioProcessorEditor::descriptorResetBounds() const
{
    const juce::Font font(juce::FontOptions(10.5f));
    const float w = juce::GlyphArrangement::getStringWidth(font, "RESET") + 12.0f;
    return { 928.0f - w, 168.0f, w, 22.0f };
}

int EntropyAudioProcessorEditor::descriptorRowAt(juce::Point<float> position) const
{
    for (int row = 0; row < 5; ++row)
        if (descriptorRowBounds(row).contains(position))
            return row;
    return -1;
}

void EntropyAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    const auto p = event.position / scale;
    const bool website = websiteBounds().contains(p);
    const bool formula = !presetPanel.isVisible() && formulaBounds().contains(p);
    const bool previous = !presetPanel.isVisible() && prevPresetBounds().contains(p);
    const bool next = !presetPanel.isVisible() && nextPresetBounds().contains(p);
    const bool bypassed = !presetPanel.isVisible() && bypassBounds().contains(p);
    const bool reset = !presetPanel.isVisible() && descriptorResetBounds().contains(p);
    const int row = presetPanel.isVisible() ? -1 : descriptorRowAt(p);
    const bool interactive = website || formula || previous || next || bypassed || reset || row >= 0;
    if (websiteHovered != website || formulaHovered != formula || prevHovered != previous
        || nextHovered != next || bypassHovered != bypassed || descriptorResetHovered != reset
        || descriptorHover != row)
    {
        websiteHovered = website;
        formulaHovered = formula;
        prevHovered = previous;
        nextHovered = next;
        bypassHovered = bypassed;
        descriptorResetHovered = reset;
        descriptorHover = row;
        setMouseCursor(interactive ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EntropyAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    websiteHovered = formulaHovered = prevHovered = nextHovered = bypassHovered = descriptorResetHovered = false;
    descriptorHover = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

juce::String EntropyAudioProcessorEditor::getTooltip()
{
    // 预设面板是全屏遮罩层，打开时顶栏提示一律隐藏。
    if (presetPanel.isVisible())
        return {};

    const auto p = getMouseXYRelative().toFloat() / scale;
    if (websiteBounds().contains(p))
        return "Visit iisaacbeats.cn";
    if (formulaBounds().contains(p))
        return "Open preset list";
    if (prevPresetBounds().contains(p))
        return "Previous preset";
    if (nextPresetBounds().contains(p))
        return "Next preset";
    if (bypassBounds().contains(p))
        return "Bypass all processing";
    if (descriptorResetBounds().contains(p))
        return "Restore all effect defaults";
    if (const int row = descriptorRowAt(p); row >= 0)
    {
        const int carrier = processor.currentControls().carrier;
        return juce::String(descriptorEffectTooltip(carrier, row)) + "\nclick to bypass - drag to adjust";
    }
    return {};
}

void EntropyAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    commitKnobEdit();
    commitTimelineEdit();
    if (!event.mods.isLeftButtonDown() || presetPanel.isVisible())
        return;
    const auto p = event.position / scale;
    if (prevPresetBounds().contains(p)) { stepPreset(-1); return; }
    if (nextPresetBounds().contains(p)) { stepPreset(1); return; }
    if (formulaBounds().contains(p)) { showArchive(); return; }
    if (bypassBounds().contains(p))
    {
        const auto c = processor.currentControls();
        processor.setParameter(entropy::ids::bypass, c.bypass ? 0.0f : 1.0f);
        return;
    }
    if (descriptorResetBounds().contains(p))
    {
        processor.resetModuleSettings();
        status = "All effects restored.";
        statusTicks = 150;
        repaint();
        return;
    }
    const int row = descriptorRowAt(p);
    if (row >= 0)
    {
        // 按下不立即切换旁路：先记录潜在拖动起点，单击（未拖动）在 mouseUp 中切换。
        const auto c = processor.currentControls();
        dragRow = row;
        dragStartX = p.x;
        dragStartIntensity = processor.moduleIntensityValue(c.carrier, row);
        dragMoved = false;
        return;
    }
    if (websiteBounds().contains(p))
        juce::URL("https://iisaacbeats.cn").launchInDefaultBrowser();
}

void EntropyAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (dragRow < 0)
        return;
    const auto p = event.position / scale;
    if (!dragMoved && std::abs(p.x - dragStartX) > 4.0f)
        dragMoved = true;
    if (!dragMoved)
        return;
    const auto c = processor.currentControls();
    const float value = intensityFromBarPosition(p.x);
    processor.setModuleIntensity(c.carrier, dragRow, value);
    status = juce::String(moduleLabel(c.carrier, dragRow)) + "  " + juce::String(juce::roundToInt(value * 100.0f)) + " %";
    statusTicks = 20;
    repaint();
}

void EntropyAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    if (dragRow < 0)
        return;
    const int row = dragRow;
    dragRow = -1;
    if (!dragMoved)
    {
        const auto c = processor.currentControls();
        const bool bypassed = processor.isModuleBypassed(c.carrier, row);
        processor.setModuleBypass(c.carrier, row, !bypassed);
        status = bypassed ? "Effect enabled." : "Effect bypassed.";
        statusTicks = 150;
        repaint();
    }
}

void EntropyAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::white);
    g.addTransform(juce::AffineTransform::scale(scale));
    const auto c = processor.currentControls();
    const auto accent = displayAccent;
    g.setColour(entropy::ui::line);
    g.drawHorizontalLine(52, 0.0f, 960.0f);
    // 标题带（与 Transcription 同格式）：小号眉题 + 27px 大标题，位于顶栏下方左侧。
    text(g, "02 / PHYSICAL CHEMISTRY", 32, 66, 240, 17, 9.5f, muted);
    g.setColour(entropy::ui::ink);
    g.setFont(juce::Font(juce::FontOptions(27.0f)));
    g.drawText("Entropy", juce::Rectangle<float>(30, 84, 260, 33), juce::Justification::centredLeft, false);
    const auto website = websiteBounds();
    const auto websiteColour = websiteHovered ? juce::Colour(0xff666666) : juce::Colours::black;
    g.setColour(websiteColour);
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawText("iisaacbeats.cn", website, juce::Justification::topLeft, false);
    const juce::Rectangle<float> versionBounds(website.getRight() + 8.0f, website.getY(), 100.0f, website.getHeight());
    g.setColour(juce::Colour(0xffa8a8a2));
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText("v" + juce::String(JucePlugin_VersionString), versionBounds, juce::Justification::centredLeft, false);
    if (websiteHovered)
    {
        g.setColour(websiteColour);
        g.drawLine(website.getX(), website.getBottom(), website.getRight(), website.getBottom(), 1.0f);
    }
    {
        const int index = processor.matchingFactoryPreset();
        const juce::String name = index >= 0 ? entropy::factoryPresets()[static_cast<size_t>(index)].name : "Custom / Archive";
        const auto formula = formulaBounds();
        const bool formulaActive = formulaHovered || presetPanel.isVisible();
        if (formulaActive)
        {
            g.setColour(juce::Colour(0x142a6fb0));
            g.fillRoundedRectangle(formula, 4.0f);
        }
        g.setColour(formulaActive ? juce::Colour(0xff2a6fb0) : ink);
        g.setFont(juce::Font(juce::FontOptions(17.0f)));
        g.drawText(name, formula, juce::Justification::centred, false);
        if (formulaActive)
        {
            g.setColour(juce::Colour(0x552a6fb0));
            g.drawLine(formula.getX() + 8.0f, formula.getBottom() - 2.0f,
                       formula.getRight() - 8.0f, formula.getBottom() - 2.0f, 1.0f);
        }
        g.setFont(juce::Font(juce::FontOptions(14.5f)));
        g.setColour(prevHovered ? juce::Colour(0xff2a6fb0) : juce::Colour(0xffc0c0ba));
        g.drawText("<", prevPresetBounds(), juce::Justification::centred, false);
        g.setColour(nextHovered ? juce::Colour(0xff2a6fb0) : juce::Colour(0xffc0c0ba));
        g.drawText(">", nextPresetBounds(), juce::Justification::centred, false);
    }
    {
        const auto bounds = bypassBounds();
        const auto colour = bypassHovered ? juce::Colour(0xff666660)
                                          : (c.bypass ? accent : juce::Colour(0xffb0b0aa));
        g.setColour(colour);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText("BYPASS", bounds, juce::Justification::centred, false);
        if (bypassHovered || c.bypass)
            g.drawLine(bounds.getX() + 2.0f, bounds.getBottom() - 2.0f,
                       bounds.getRight() - 2.0f, bounds.getBottom() - 2.0f, 1.0f);
    }
    const auto time = processor.timelineSnapshot();
    {
        // Gibbs–Helmholtz 签名 + 活的 ΔG = ΔH − T·ΔS：T 为室温常数，ΔS 来自时间轴。
        const auto system = entropy::thermo::systemFor(c.carrier);
        const float deltaS = c.entropy * static_cast<float>(system.deltaSMax) * 1000.0f;                       // J/(mol·K)
        const float deltaG = static_cast<float>(system.deltaH) * (1.0f - 2.0f * c.entropy);                   // kJ/mol
        g.setFont(formulaFont(11.0f));
        g.setColour(ink.withAlpha(0.82f));
        g.drawText(juce::String::fromUTF8("∂(ΔG/T)/∂T = −ΔH/T²   ΔG = ΔH − T·ΔS"),
                   juce::Rectangle<float>(650, 60, 278, 16), juce::Justification::centredRight, false);
        g.setFont(juce::Font(juce::FontOptions(9.5f)));
        g.setColour(muted);
        const juce::String values = juce::String::fromUTF8("T 298 K · ΔS ") + juce::String(deltaS, 0)
            + juce::String::fromUTF8(" J/K · ΔG ") + juce::String(deltaG, 1)
            + juce::String::fromUTF8(" kJ/mol");
        g.drawText(values, juce::Rectangle<float>(650, 78, 278, 14), juce::Justification::centredRight, false);
        if (c.entropy >= 0.5f)
        {
            g.setFont(formulaFont(9.5f));
            g.setColour(accent);
            g.drawText("SPONTANEOUS", juce::Rectangle<float>(650, 78, 278, 14), juce::Justification::centredLeft, false);
        }
    }
    text(g, "NOW  " + entropy::timeline::localDate(time.now), 650, 124, 278, 17, 10.5f, muted, juce::Justification::centredRight);
    const bool outsideRange = time.selected < time.now - entropy::timeline::spanMs(time.carrier);
    text(g, juce::String(entropy::timeline::spanLabel(c.carrier)) + " / "
         + (outsideRange ? "BEYOND SCALE" : entropy::degradationStage(c.entropy)), 650, 142, 278, 16, 10, accent,
         juce::Justification::centredRight);
    g.setColour(entropy::ui::line);
    g.drawVerticalLine(630, 60.0f, 620.0f);
    drawSpecimen(g, c, carrierFrom, carrierTransition);
    drawDescriptors(g, c, accent);
    g.setColour(entropy::ui::line);
    g.drawHorizontalLine(542, 32.0f, 610.0f);
    constexpr std::array<const char*, 3> labels { "INPUT", "MIX", "OUTPUT" };
    for (int i = 0; i < 3; ++i)
        text(g, labels[static_cast<size_t>(i)], static_cast<float>(106 + i * 144), 550, 124, 16, 10, muted, juce::Justification::centred);
    text(g, "SIGNAL / dBFS", 650, 552, 240, 18, 10, muted);
    const std::array<float, 2> peaks { meterIn, meterOut };
    for (int i = 0; i < 2; ++i)
    {
        const float y = 584.0f + static_cast<float>(i) * 28.0f;
        const float db = juce::Decibels::gainToDecibels(peaks[static_cast<size_t>(i)], -60.0f);
        text(g, i == 0 ? "IN" : "OUT", 650, y - 5, 32, 17, 9, muted);
        g.setColour(entropy::ui::line);
        g.fillRect(687.0f, y + 2.0f, 182.0f, 3.0f);
        g.setColour(db > 0.0f ? juce::Colour(0xffbd5046) : accent);
        g.fillRect(687.0f, y + 2.0f, 182.0f * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f), 3.0f);
        text(g, db <= -60.0f ? "--" : juce::String(db, 1), 877, y - 6, 52, 18, 10, db > 0.0f ? juce::Colour(0xffbd5046) : muted,
             juce::Justification::centredRight);
    }
    if (editingSlider >= 0 && knobInput.isVisible())
    {
        const auto eb = knobInput.getBounds();
        const float s = scale;
        g.setColour(juce::Colour(0xff6e6e68));
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawText(knobPercent[static_cast<size_t>(editingSlider)] ? "%" : "dB",
                   juce::Rectangle<float>(static_cast<float>(eb.getRight()) / s + 3.0f,
                                          static_cast<float>(eb.getY()) / s,
                                          30.0f, static_cast<float>(eb.getHeight()) / s),
                   juce::Justification::centredLeft, false);
    }
    if (timelineInput.isVisible())
    {
        const auto eb = timelineInput.getBounds();
        const float s = scale;
        g.setColour(juce::Colour(0xff6e6e68));
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawText("%", juce::Rectangle<float>(static_cast<float>(eb.getRight()) / s + 3.0f,
                                               static_cast<float>(eb.getY()) / s,
                                               30.0f, static_cast<float>(eb.getHeight()) / s),
                   juce::Justification::centredLeft, false);
    }
}

void EntropyAudioProcessorEditor::drawSpecimen(juce::Graphics& g, const entropy::Controls& c,
                                               int fromCarrier, float transition)
{
    const float eased = transition * transition * (3.0f - 2.0f * transition);
    const auto accent = displayAccent;
    g.setColour(paper);
    g.fillRoundedRectangle(32, 164, 578, 274, 4);
    g.setColour(entropy::ui::line);
    g.drawRoundedRectangle(32, 164, 578, 274, 4, 1);
    text(g, "0" + juce::String(fromCarrier + 1) + " / " + entropy::carrierName(fromCarrier),
         49.0f - 10.0f * eased, 178, 240, 18, 11, accent.withAlpha(1.0f - eased));
    text(g, "0" + juce::String(c.carrier + 1) + " / " + entropy::carrierName(c.carrier),
         49.0f + 10.0f * (1.0f - eased), 178, 240, 18, 11, accent.withAlpha(0.18f + 0.82f * eased));
    g.setFont(formulaFont(10.5f));
    g.setColour(muted);
    g.drawText("S = k ln W", juce::Rectangle<float>(448, 178, 144, 16), juce::Justification::centredRight, false);
    {
        // 微观状态数随熵爆炸：W = 10^(23·S)，熵 0 时 W = 1。
        const float exponent = 23.0f * c.entropy;
        const juce::String microstates = exponent < 0.05f ? juce::String("W = 1")
            : juce::String::fromUTF8("W ≈ 10^") + juce::String(exponent, 1);
        g.setFont(juce::Font(juce::FontOptions(8.5f)));
        g.setColour(muted.withAlpha(0.9f));
        g.drawText(microstates, juce::Rectangle<float>(448, 195, 144, 12), juce::Justification::centredRight, false);
    }
    {
        const juce::Graphics::ScopedSaveState particleState(g);
        g.reduceClipRegion(49, 201, 544, 72);
        constexpr int columns = 15, rows = 5;
        // 每种载体一种排布（相邻 index 的连线自动勾勒排布形态）：
        //   TAPE   磁迹——5 条平行横线
        //   VINYL  唱片纹路——单根内卷螺旋（每圈 15 点，共 5 圈）
        //   STREAM 数据流——横向波浪线（6 个周期）
        //   PHASE  光盘轨道——5 个同心椭圆环
        // 载体切换时 fromCarrier 与当前载体的坐标按 eased 补间，动画自动成立。
        const auto pointFor = [this, &c, columns](int index, int carrier)
        {
            const float id = static_cast<float>(index);
            const float jitter = c.entropy * (4.0f + 18.0f * c.entropy);
            const int x = index % columns;
            const int y = index / columns;
            float px, py;
            switch (carrier)
            {
                case 1:   // VINYL：音槽弧带——低视角唱片的透视音槽：下方弧（近处）更短更弯，
                {         // 上方弧（远处）更长更平缓，即真实唱片照片中同心音槽的透视形态
                    const float frac = static_cast<float>(x) / 14.0f;
                    const float radius = 380.0f - static_cast<float>(y) * 62.0f;
                    const float yCentre = 210.0f + static_cast<float>(y) * 12.0f;
                    const float sagitta = juce::jmin(55.0f, 270.0f - yCentre);
                    const float halfSpan = std::sqrt(2.0f * radius * sagitta - sagitta * sagitta);
                    const float arcX = (frac * 2.0f - 1.0f) * halfSpan;
                    px = 319.0f + arcX;
                    py = yCentre + radius - std::sqrt(radius * radius - arcX * arcX);
                    break;
                }
                case 2:   // STREAM：5 条并行波浪线（与 TAPE 同构），行间相位错开
                {
                    px = 70.0f + static_cast<float>(x) * 35.5f;
                    const float frac = static_cast<float>(x) / 14.0f;
                    py = 208.0f + static_cast<float>(y) * 14.0f
                        + 5.0f * std::sin(frac * juce::MathConstants<float>::twoPi * 6.0f
                                          + static_cast<float>(y) * 0.7f);
                    break;
                }
                case 3:   // PHASE 同心椭圆环
                {
                    const float angle = static_cast<float>(x) / 15.0f * juce::MathConstants<float>::twoPi;
                    const float radius = 24.0f + static_cast<float>(y) * 19.0f;
                    px = 319.0f + std::cos(angle) * radius * 2.6f;
                    py = 232.0f + std::sin(angle) * radius * 0.30f;
                    break;
                }
                default:  // TAPE 平行横线
                {
                    px = 70.0f + static_cast<float>(x) * 35.5f;
                    py = 207.0f + static_cast<float>(y) * 14.0f;
                    break;
                }
            }
            return juce::Point<float> {
                px + std::sin(id * 13.13f + animation * 0.5f) * jitter,
                py + std::cos(id * 8.71f + animation * 0.4f) * jitter * 0.30f
            };
        };
        std::array<juce::Point<float>, columns * rows> points;
        for (int i = 0; i < columns * rows; ++i)
        {
            const auto a = pointFor(i, fromCarrier);
            const auto b = pointFor(i, c.carrier);
            points[static_cast<size_t>(i)] = { a.x + (b.x - a.x) * eased, a.y + (b.y - a.y) * eased };
        }
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x)
            {
                const int index = y * columns + x;
                const auto point = points[static_cast<size_t>(index)];
                if (x < columns - 1)
                {
                    g.setColour(accent.withAlpha((1.0f - c.entropy) * 0.20f + 0.04f));
                    g.drawLine(juce::Line<float>(point, points[static_cast<size_t>(index + 1)]), 0.8f);
                }
                const float strength = 0.35f + 0.65f * std::abs(std::sin(static_cast<float>(index) * 1.71f));
                g.setColour(accent.withAlpha(strength));
                const float radius = 1.6f + c.entropy * strength * 1.4f;
                g.fillEllipse(point.x - radius, point.y - radius, radius * 2.0f, radius * 2.0f);
            }
    }
    drawCarrierTexture(g, c, fromCarrier, transition);
    {
        // Arrhenius 速率方程：解释该载体的时间尺度（t½ = 时间跨度）。
        const auto system = entropy::thermo::systemFor(c.carrier);
        const juce::String halfLife = c.carrier == 2
            ? juce::String("24 h") : juce::String(juce::roundToInt(system.halfLifeSeconds / 31556952.0)) + juce::String(" a");
        g.setFont(formulaFont(10.0f));
        g.setColour(muted);
        g.drawText(juce::String::fromUTF8("k = A·e^(−Ea/RT)"), juce::Rectangle<float>(49, 412, 260, 15), juce::Justification::centredLeft, false);
        g.setFont(juce::Font(juce::FontOptions(9.5f)));
        g.drawText("Ea " + juce::String(system.activationEnergy, 0) + juce::String::fromUTF8(" kJ/mol · t½ ") + halfLife,
                   juce::Rectangle<float>(300, 412, 292, 15), juce::Justification::centredRight, false);
    }
}

void EntropyAudioProcessorEditor::drawCarrierTexture(juce::Graphics& g, const entropy::Controls& controls,
                                                     int fromCarrier, float transition)
{
    const float eased = transition * transition * (3.0f - 2.0f * transition);
    const juce::Graphics::ScopedSaveState carrierState(g);
    g.setColour(entropy::ui::line);
    g.drawHorizontalLine(288, 49.0f, 593.0f);
    const auto paintBand = [this, &g](juce::Graphics& target, const entropy::Controls& c, float opacity, float offsetX)
    {
        const juce::Graphics::ScopedSaveState bandState(target);
        target.reduceClipRegion(49, 296, 544, 112);
        target.addTransform(juce::AffineTransform::translation(49.0f - offsetX, 318.0f));
        target.setOpacity(opacity);
        const auto accent = carrierColour(c.carrier);
    constexpr std::array<const char*, 4> names { "MAGNETIC TAPE", "VINYL RECORD", "DIGITAL STREAM", "OPTICAL DISC" };
    text(g, names[static_cast<size_t>(c.carrier)], 134, 0, 260, 19, 11, ink);
    g.setColour(accent.withAlpha(0.045f));
    g.fillRoundedRectangle(128, 24, 416, 46, 3);
    g.setColour(accent.withAlpha(0.75f));
    if (c.carrier == 0)
    {
        g.drawRoundedRectangle(4, 5, 104, 62, 5, 1.4f);
        g.drawRoundedRectangle(14, 20, 84, 25, 12, 1.0f);
        g.drawLine(20, 13, 92, 13, 1.0f);
        for (float centre : { 32.0f, 80.0f })
        {
            g.drawEllipse(centre - 10, 23, 20, 20, 1.2f);
            g.drawEllipse(centre - 3, 30, 6, 6, 1.0f);
            for (int spoke = 0; spoke < 6; ++spoke)
            {
                const float angle = static_cast<float>(spoke) * juce::MathConstants<float>::twoPi / 6.0f;
                g.drawLine(centre + 5 * std::cos(angle), 33 + 5 * std::sin(angle),
                           centre + 9 * std::cos(angle), 33 + 9 * std::sin(angle), 1.0f);
            }
        }
        juce::Path head;
        head.startNewSubPath(24, 66); head.lineTo(30, 51); head.lineTo(82, 51); head.lineTo(88, 66);
        g.strokePath(head, juce::PathStrokeType(1.1f));
        g.drawLine(43, 57, 69, 57, 1.0f);
        for (float x : { 11.0f, 101.0f })
            g.fillEllipse(x - 1.5f, 58, 3, 3);
    }
    else if (c.carrier == 1 || c.carrier == 3)
    {
        const float cx = 52.0f, cy = 36.0f;
        g.setColour(accent.withAlpha(c.carrier == 1 ? 0.10f : 0.045f));
        g.fillEllipse(cx - 31, cy - 31, 62, 62);
        g.setColour(accent.withAlpha(0.75f));
        g.drawEllipse(cx - 31, cy - 31, 62, 62, 1.4f);
        g.drawEllipse(cx - 10, cy - 10, 20, 20, 1.1f);
        g.fillEllipse(cx - 2, cy - 2, 4, 4);
        if (c.carrier == 1)
        {
            g.setColour(accent.withAlpha(0.35f));
            for (float radius : { 15.0f, 19.0f, 23.0f, 27.0f })
                g.drawEllipse(cx - radius, cy - radius, radius * 2, radius * 2, 0.8f);
            g.setColour(accent.withAlpha(0.8f));
            juce::Path arm;
            arm.startNewSubPath(99, 12); arm.lineTo(99, 41); arm.lineTo(81, 56);
            g.strokePath(arm, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawEllipse(96, 7, 6, 6, 1.1f);
            g.drawLine(79, 54, 85, 60, 3);
        }
        else
        {
            for (int sector = 0; sector < 3; ++sector)
            {
                const float angle = 0.35f + static_cast<float>(sector) * juce::MathConstants<float>::twoPi / 3.0f;
                juce::Path reflection;
                reflection.addPieSegment(cx - 29, cy - 29, 58, 58, angle, angle + 0.47f, 0.38f);
                g.setColour(accent.withAlpha(0.14f));
                g.fillPath(reflection);
            }
            g.setColour(accent.withAlpha(0.75f));
            g.drawEllipse(cx - 14, cy - 14, 28, 28, 0.8f);
            g.drawLine(75, 51, 99, 62, 1.0f);
            g.drawRoundedRectangle(95, 59, 12, 8, 2, 1.1f);
        }
    }
    else
    {
        juce::Path cloud;
        cloud.startNewSubPath(23, 43);
        cloud.cubicTo(5, 43, 5, 21, 23, 20);
        cloud.cubicTo(26, 0, 57, 0, 62, 20);
        cloud.cubicTo(82, 14, 98, 43, 75, 43);
        cloud.closeSubPath();
        g.strokePath(cloud, juce::PathStrokeType(1.5f));
        juce::Path play;
        play.addTriangle(40, 20, 40, 35, 53, 27.5f);
        g.setColour(accent.withAlpha(0.35f));
        g.fillPath(play);
        for (int packet = 0; packet < 3; ++packet)
        {
            const float x = 22.0f + static_cast<float>(packet) * 24.0f;
            g.setColour(accent.withAlpha(0.7f));
            g.drawLine(x + 6, 46, x + 6, 54, 1.0f);
            g.drawRoundedRectangle(x, 56, 12, 9, 2, 1.1f);
        }
    }

    const juce::Graphics::ScopedSaveState textureState(g);
    g.reduceClipRegion(134, 27, 404, 40);
    if (c.carrier == 1)
    {
        for (int groove = 0; groove < 7; ++groove)
        {
            juce::Path path;
            const float y = 28.0f + static_cast<float>(groove) * 6.0f;
            path.startNewSubPath(134, y);
            for (int step = 1; step <= 60; ++step)
            {
                const float x = static_cast<float>(step) / 60.0f;
                path.lineTo(134 + x * 404, y + std::sin(x * 7.0f + static_cast<float>(groove) * 0.4f) * (1 + c.entropy * 2));
            }
            g.setColour(accent.withAlpha(0.25f + static_cast<float>(groove % 2) * 0.18f));
            g.strokePath(path, juce::PathStrokeType(0.9f));
        }
    }
    else
    {
        const int columns = c.carrier == 2 ? 17 : 27;
        const float spacing = c.carrier == 2 ? 24.0f : 15.0f;
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < columns; ++column)
            {
                const float mark = 0.5f + 0.5f * std::sin(static_cast<float>(column * 7 + row * 13));
                const bool faded = mark < c.entropy * 0.7f;
                const float x = 134.0f + static_cast<float>(column) * spacing;
                const float y = 30.0f + static_cast<float>(row) * 13.0f;
                g.setColour(accent.withAlpha(faded ? 0.12f : 0.48f));
                if (c.carrier == 2)
                {
                    g.drawRoundedRectangle(x, y, 17, 8, 1.5f, 0.9f);
                    if (!faded)
                        g.fillRect(x + 3, y + 3, 11.0f, 2.0f);
                }
                else if (c.carrier == 3)
                    g.fillRoundedRectangle(x, y + 2, 4 + mark * 7, 4, 2);
                else
                {
                    g.fillRect(x, y + 1, 10.0f, 6.0f);
                    g.setColour(paper.withAlpha(0.8f));
                    g.drawLine(x + (column % 2 == 0 ? 3.0f : 7.0f), y + 2,
                               x + (column % 2 == 0 ? 3.0f : 7.0f), y + 6, 0.8f);
                }
            }
    }
    };
    if (fromCarrier != controls.carrier && eased < 1.0f)
    {
        constexpr float bandWidth = 560.0f;
        const float first = juce::jlimit(0.0f, 1.0f, eased / 0.5f);
        const float second = juce::jlimit(0.0f, 1.0f, (eased - 0.5f) / 0.5f);
        auto oldBand = controls;
        oldBand.carrier = fromCarrier;
        paintBand(g, oldBand, 1.0f - first, bandWidth * first);
        paintBand(g, controls, second, -bandWidth * (1.0f - second));
    }
    else
        paintBand(g, controls, 1.0f, 0.0f);
}

void EntropyAudioProcessorEditor::drawDescriptorIcon(juce::Graphics& g, DescriptorIcon icon,
                                                     juce::Rectangle<float> bounds, float amount, juce::Colour accent, bool active)
{
    const juce::Graphics::ScopedSaveState iconState(g);
    g.addTransform(juce::AffineTransform::scale(bounds.getWidth() / 32.0f, bounds.getHeight() / 32.0f)
                       .translated(bounds.getX(), bounds.getY()));
    const float level = juce::jlimit(0.0f, 1.0f, amount);
    g.setColour(accent.withAlpha(0.065f));
    g.fillRoundedRectangle(0, 0, 32, 32, 6);
    const float alpha = active ? 0.85f : 0.32f;
    const auto colour = [&](float a) { return (active ? accent : muted).withAlpha(a); };
    juce::Path path;
    switch (icon)
    {
        case DescriptorIcon::bandwidth:
        {
            // 高频损失：坐标轴 + 随退化向左提前的滚降曲线
            g.setColour(colour(0.25f));
            g.drawLine(6, 24, 27, 24, 1.0f);
            g.drawLine(6, 24, 6, 6, 1.0f);
            const float knee = 15.0f - 6.0f * level;
            path.startNewSubPath(6, 8); path.lineTo(knee, 8);
            path.cubicTo(knee + 4.0f, 8, knee, 23, 27, 23);
            break;
        }
        case DescriptorIcon::transport:
        {
            // 走带不稳：虚线参考 + 围绕它起伏的抖动
            g.setColour(colour(0.22f));
            for (float x = 6; x < 27; x += 4)
                g.drawLine(x, 16, x + 2, 16, 1.0f);
            const float depth = 2.0f + 5.0f * level;
            for (int i = 0; i <= 32; ++i)
            {
                const float t = static_cast<float>(i) / 32.0f;
                const float y = 16 + std::sin(t * juce::MathConstants<float>::twoPi * 1.5f) * depth;
                if (i == 0) path.startNewSubPath(5, y);
                else path.lineTo(5 + t * 22, y);
            }
            break;
        }
        case DescriptorIcon::events:
        {
            // 突发爆音：平稳基线 + 尖锐单脉冲
            g.setColour(colour(0.3f));
            g.drawLine(6, 22, 27, 22, 1.0f);
            const float height = 5.0f + 9.0f * level;
            path.startNewSubPath(6, 22); path.lineTo(12, 22); path.lineTo(13.4f, 22 - height);
            path.lineTo(14.8f, 22); path.lineTo(27, 22);
            break;
        }
        case DescriptorIcon::dropout:
        {
            // 信号断续：两段波形之间缺失的间隙
            const float gap = 3.0f + 9.0f * level;
            const float centre = 16.0f;
            const float half = gap * 0.5f;
            for (int i = 0; i <= 14; ++i)
            {
                const float t = static_cast<float>(i) / 14.0f;
                const float x = 6.0f + (centre - half - 6.0f) * t;
                const float y = 16.0f + std::sin(t * juce::MathConstants<float>::twoPi) * 3.0f;
                if (i == 0) path.startNewSubPath(x, y);
                else path.lineTo(x, y);
            }
            for (int i = 0; i <= 14; ++i)
            {
                const float t = static_cast<float>(i) / 14.0f;
                const float x = centre + half + (26.0f - centre - half) * t;
                const float y = 16.0f + std::sin((t + 1.0f) * juce::MathConstants<float>::twoPi) * 3.0f;
                if (i == 0) path.startNewSubPath(x, y);
                else path.lineTo(x, y);
            }
            g.setColour(colour(0.3f));
            g.drawLine(centre - half, 10, centre - half, 16, 1.0f);
            g.drawLine(centre + half, 22, centre + half, 16, 1.0f);
            break;
        }
        case DescriptorIcon::stereo:
        {
            // 声场塌缩：两声道圆点向中线靠拢
            const float offset = 7.5f - 4.5f * level;
            g.setColour(colour(0.22f));
            for (float y = 8; y < 25; y += 5)
                g.drawLine(16, y, 16, y + 2.5f, 1.0f);
            g.setColour(colour(alpha));
            g.fillEllipse(11.0f - offset - 2.5f, 11, 5, 5);
            g.fillEllipse(11.0f + offset - 2.5f, 11, 5, 5);
            break;
        }
        case DescriptorIcon::surface:
        {
            // 表面尘噪：散布颗粒随退化变大变显
            for (int i = 0; i < 9; ++i)
            {
                const float px = 9.0f + static_cast<float>(i % 3) * 7.0f;
                const float py = 10.0f + static_cast<float>(i / 3) * 6.0f;
                const float variation = 0.5f + 0.5f * std::abs(std::sin(static_cast<float>(i) * 2.7f));
                const float radius = 0.9f + variation * (1.2f + level * 1.2f);
                g.setColour(colour(0.55f + 0.3f * variation));
                g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);
            }
            break;
        }
        case DescriptorIcon::bitrate:
        {
            // 码率阶梯：从左到右逐级下降
            const float startX = 6.0f, stepW = 4.2f;
            const float top = 9.0f;
            for (int i = 0; i < 5; ++i)
            {
                const float x = startX + static_cast<float>(i) * stepW;
                const float y = top + static_cast<float>(i) * 3.2f;
                const bool faded = static_cast<float>(i) >= (1.0f - level) * 5.0f - 0.001f;
                g.setColour(colour(faded ? 0.22f : alpha));
                g.drawLine(x, y, x + stepW - 0.8f, y, 1.6f);
                if (i < 4)
                    g.drawLine(x + stepW - 0.8f, y, x + stepW - 0.8f, y + 3.2f, 1.0f);
            }
            break;
        }
        case DescriptorIcon::smear:
        {
            // 瞬态涂抹：前方淡影 + 本体脉冲
            const float spread = 4.0f + 7.0f * level;
            juce::Path ghost;
            ghost.startNewSubPath(9, 23); ghost.lineTo(13, 8 + spread * 0.3f); ghost.lineTo(17, 23); ghost.closeSubPath();
            g.setColour(colour(0.3f));
            g.strokePath(ghost, juce::PathStrokeType(1.0f));
            path.startNewSubPath(18, 24); path.lineTo(22, 8); path.lineTo(26, 24); path.closeSubPath();
            g.setColour(colour(0.38f));
            g.fillPath(path);
            path.clear();
            break;
        }
        case DescriptorIcon::birdie:
        {
            // 误纠鸟鸣：三声频率快速上升的扫频哨音
            for (int bird = 0; bird < 3; ++bird)
            {
                const float offsetY = -4.0f + static_cast<float>(bird) * 4.0f;
                juce::Path chirp;
                for (int i = 0; i <= 28; ++i)
                {
                    const float t = static_cast<float>(i) / 28.0f;
                    const float x = 5.0f + t * 22.0f;
                    const float y = 16.0f + offsetY * 0.45f
                        - std::sin(t * 3.14159f * (1.2f + 3.6f * t * t)) * (1.5f + 3.5f * level);
                    if (i == 0) chirp.startNewSubPath(x, y);
                    else chirp.lineTo(x, y);
                }
                g.setColour(colour(0.3f + 0.55f * (1.0f - static_cast<float>(bird) * 0.25f)));
                g.strokePath(chirp, juce::PathStrokeType(1.0f));
            }
            break;
        }
    }
    g.setColour(colour(alpha));
    g.strokePath(path, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (!active)
    {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.fillRoundedRectangle(0, 0, 32, 32, 6);
    }
}

void EntropyAudioProcessorEditor::drawDescriptors(juce::Graphics& g, const entropy::Controls& c, juce::Colour accent)
{
    const auto carrier = static_cast<entropy::Carrier>(c.carrier);
    const auto r = entropy::makeRecipe(carrier, c.entropy, static_cast<float>(c.generations));
    std::array<float, 5> levels {};
    for (int row = 0; row < 5; ++row)
        levels[static_cast<size_t>(row)] = processor.moduleIntensityValue(c.carrier, row);
    auto display = r;
    entropy::applyModuleIntensity(display, carrier, levels.data());
    text(g, "DEGRADATION DESCRIPTORS", 650, 168, 280, 24, 11, ink);
    bool anyBypassed = false;
    for (int row = 0; row < 5; ++row)
        anyBypassed = anyBypassed || processor.isModuleBypassed(c.carrier, row);
    {
        const auto bounds = descriptorResetBounds();
        const auto colour = descriptorResetHovered ? blue : (anyBypassed ? muted : juce::Colour(0xffd5d6d1));
        g.setColour(colour);
        g.setFont(juce::Font(juce::FontOptions(10.5f)));
        g.drawText("RESET", bounds, juce::Justification::centred, false);
        if (descriptorResetHovered)
            g.drawLine(bounds.getX() + 2.0f, bounds.getBottom() - 2.0f,
                       bounds.getRight() - 2.0f, bounds.getBottom() - 2.0f, 1.0f);
    }
    const float surfaceLevel = c.carrier == 2 ? 0.0f : display.noise / entropy::surfaceNoiseMax(carrier);
    struct Row { juce::String label, value; float amount; DescriptorIcon icon; };
    std::array<Row, 5> rows {{
        { moduleLabel(c.carrier, 0), juce::String(display.cutoffHz / 1000.0f, 1) + " kHz", 1.0f - display.cutoffHz / 20000.0f, DescriptorIcon::bandwidth },
        { moduleLabel(c.carrier, 1), juce::String(display.wowMs + display.flutterMs, 2) + " ms", (display.wowMs + display.flutterMs) / 2.6f, DescriptorIcon::transport },
        { moduleLabel(c.carrier, 2), juce::String(display.eventRate + display.gapRate, 1) + " /s", (display.eventRate + display.gapRate) / 65.0f, DescriptorIcon::events },
        { moduleLabel(c.carrier, 3), juce::String(juce::roundToInt(display.width * 100.0f)) + " %", 1.0f - display.width, DescriptorIcon::stereo },
        { moduleLabel(c.carrier, 4), juce::String(juce::roundToInt(surfaceLevel * 100.0f)) + " %", surfaceLevel, DescriptorIcon::surface }
    }};
    if (c.carrier == 0 || c.carrier == 2)
        rows[2].icon = DescriptorIcon::dropout;
    if (c.carrier == 2)
    {
        const bool lossless = c.entropy < 0.001f || levels[1] <= 0.001f;
        rows[1] = { "BITRATE MODEL", lossless ? "LOSSLESS" : juce::String(r.bitrate) + " kb/s",
                    levels[1] * (1.0f - static_cast<float>(r.bitrate) / 320.0f), DescriptorIcon::bitrate };
        rows[4] = { "TRANSIENT SMEAR", juce::String(display.smear * 100.0f, 1) + " %", display.smear, DescriptorIcon::smear };
    }
    if (c.carrier == 3)
    {
        constexpr std::array<const char*, 4> stages { "CORRECTABLE", "INTERPOLATION", "BIRDIES", "ERROR STORM" };
        rows[3] = { "MISCORRECTION", stages[static_cast<size_t>(r.correctionStage)],
                    levels[3] * static_cast<float>(r.correctionStage) / 3.0f, DescriptorIcon::birdie };
    }
    for (int i = 0; i < 5; ++i)
    {
        const auto& row = rows[static_cast<size_t>(i)];
        const float y = 204.0f + static_cast<float>(i) * 53.0f;
        const bool active = !processor.isModuleBypassed(c.carrier, i);
        const bool dragged = dragRow == i && dragMoved;
        if (descriptorHover == i || dragged)
        {
            g.setColour(accent.withAlpha(dragged ? 0.11f : 0.07f));
            g.fillRoundedRectangle(descriptorRowBounds(i).withY(y - 2.0f), 3.0f);
        }
        drawDescriptorIcon(g, row.icon, { 650, y + 4, 32, 32 }, row.amount, accent, active);
        text(g, row.label, 696, y, 232, 15, 10, active ? muted : juce::Colour(0xffc2c4bf));
        if (i == 4 && c.carrier != 2)
        {
            g.setFont(formulaFont(8.5f));
            g.setColour((active ? muted : juce::Colour(0xffc2c4bf)).withAlpha(0.9f));
            g.drawText(juce::String::fromUTF8("dS ≥ δQ/T"), juce::Rectangle<float>(696, y, 232, 13), juce::Justification::centredRight, false);
        }
        text(g, active ? row.value : "OFF", 696, y + 16, 232, 23, 15, active ? ink : juce::Colour(0xffc2c4bf));
        g.setColour(entropy::ui::line);
        g.fillRect(696.0f, y + 42.0f, 232.0f, 2.0f);
        g.setColour(active ? accent.withAlpha(0.7f) : juce::Colour(0xffc2c4bf));
        g.fillRect(696.0f, y + 42.0f, 232.0f * juce::jlimit(0.0f, 1.0f, row.amount), 2.0f);
        const float handleX = 696.0f + 232.0f * juce::jlimit(0.0f, 1.0f, levels[static_cast<size_t>(i)]);
        g.setColour((descriptorHover == i || dragged) ? accent : accent.withAlpha(0.45f));
        g.fillRect(handleX - 1.0f, y + 37.0f, 2.0f, 12.0f);
    }
    if (c.carrier == 2)
        text(g, "TRANSCODE PASSES", 776, 483, 153, 28, 9, muted, juce::Justification::centredRight);
    else
        text(g, c.carrier == 0 ? "CONTINUOUS DECAY" : c.carrier == 1 ? "STOCHASTIC SURFACE EVENTS" : "MISCORRECTION ARTEFACTS",
             776, 483, 153, 28, 9, muted, juce::Justification::centredRight);
}

void EntropyAudioProcessorEditor::timerCallback()
{
    const auto time = processor.timelineSnapshot();
    timelineControl.setSnapshot(time);
    if (!selectedDateLabel.isBeingEdited())
        selectedDateLabel.setText(entropy::timeline::localDate(time.selected), juce::dontSendNotification);
    const auto c = processor.currentControls();
    if (c.carrier != carrierTarget)
    {
        carrierFrom = carrierTransition < 0.5f ? carrierFrom : carrierTarget;
        carrierTarget = c.carrier;
        carrierTransition = 0.0f;
    }
    if (carrierTransition < 1.0f)
        carrierTransition = juce::jmin(1.0f, carrierTransition + 0.055f);
    const float eased = carrierTransition * carrierTransition * (3.0f - 2.0f * carrierTransition);
    displayAccent = entropy::ui::blendColour(carrierColour(carrierFrom), carrierColour(carrierTarget), eased);
    look.accent = displayAccent;
    timelineControl.setAccent(displayAccent);
    animation += 0.035f + 0.07f * c.entropy;
    meterIn = juce::jmax(processor.inputLevel.exchange(0.0f, std::memory_order_relaxed), meterIn * 0.86f);
    meterOut = juce::jmax(processor.outputLevel.exchange(0.0f, std::memory_order_relaxed), meterOut * 0.86f);
    for (int i = 0; i < 4; ++i)
        carrierButtons[static_cast<size_t>(i)].setToggleState(c.carrier == i, juce::dontSendNotification);
    generation.setVisible(true);
    generation.setButtonText(entropy::storageName(c.carrier, c.generations)
        + (c.carrier == 2 ? juce::String()
                           : juce::String(" ") + juce::String(c.generations) + "/8"));
    generation.setTooltip(entropy::storageTooltip(c.carrier));
    const int index = processor.matchingFactoryPreset();
    presetPanel.setCurrentIndex(index);
    if (statusTicks > 0)
        --statusTicks;
    const int savedWidth = processor.editorWidth.load();
    if (savedWidth != getWidth())
        setSize(savedWidth, juce::roundToInt(static_cast<float>(savedWidth) / 1.5f));
    repaint();
}

void EntropyAudioProcessorEditor::stepPreset(int delta)
{
    int index = processor.matchingFactoryPreset();
    if (index < 0)
        index = processor.currentControls().carrier * 5;
    processor.loadFactoryPreset((index + delta + 20) % 20);
}

void EntropyAudioProcessorEditor::showArchive()
{
    if (presetPanel.isVisible()) { hideArchive(); return; }
    presetPanel.setBounds(getLocalBounds());
    presetPanel.open(processor.matchingFactoryPreset());
    repaint();
}

void EntropyAudioProcessorEditor::hideArchive()
{
    presetPanel.setVisible(false);
    repaint();
}

void EntropyAudioProcessorEditor::choosePresetFile(bool save)
{
    fileChooser = std::make_unique<juce::FileChooser>(save ? "Save Entropy snapshot" : "Open Entropy snapshot",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.entropypreset"), "*.entropypreset");
    const int chooserFlags = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting
                                  : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    const juce::Component::SafePointer<EntropyAudioProcessorEditor> safe(this);
    fileChooser->launchAsync(chooserFlags, [safe, save](const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;
        auto file = chooser.getResult();
        if (file == juce::File())
            return;
        bool ok = false;
        if (save)
        {
            const auto target = file.withFileExtension("entropypreset");
            if (target != file && target.exists())
            {
                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("Replace snapshot?").withMessage(target.getFullPathName())
                    .withButton("Replace").withButton("Cancel").withAssociatedComponent(safe.getComponent()),
                    [safe, target](int result)
                    {
                        if (safe != nullptr && result == 1) safe->saveSnapshot(target);
                    });
            }
            else
                safe->saveSnapshot(target);
            return;
        }
        if (file.getSize() <= 1024 * 1024)
        {
            juce::MemoryBlock state;
            ok = file.loadFileAsData(state) && safe->processor.restoreState(state.getData(), static_cast<int>(state.getSize()));
        }
        safe->status = ok ? "Snapshot restored." : "Snapshot failed: invalid file, unsupported version, or inaccessible path.";
        safe->statusTicks = 180;
    });
}

void EntropyAudioProcessorEditor::saveSnapshot(const juce::File& file)
{
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    juce::TemporaryFile temporary(file);
    const bool ok = temporary.getFile().replaceWithData(state.getData(), state.getSize()) && temporary.overwriteTargetFileWithTemporary();
    status = ok ? "Snapshot saved." : "Snapshot failed: unable to write this path.";
    statusTicks = 180;
}

void EntropyAudioProcessorEditor::beginKnobEdit(int index)
{
    if (!juce::isPositiveAndBelow(index, 4))
        return;
    editingSlider = index;
    const float raw = processor.parameters.getRawParameterValue(knobIds[static_cast<size_t>(index)])->load();
    const float display = knobPercent[static_cast<size_t>(index)] ? raw * 100.0f : raw;
    knobInput.setText(juce::String(display, 1));
    const auto area = knobSliders[static_cast<size_t>(index)]->getBounds();
    const int w = juce::roundToInt(84.0f * scale);
    const int h = juce::roundToInt(22.0f * scale);
    knobInput.setBounds(area.getCentreX() - w / 2, area.getBottom() - h, w, h);
    knobInput.setVisible(true);
    knobInput.toFront(false);
    knobInput.grabKeyboardFocus();
    knobInput.selectAll();
    repaint();
}

void EntropyAudioProcessorEditor::commitKnobEdit()
{
    if (editingSlider < 0)
        return;
    const int index = editingSlider;
    editingSlider = -1;
    float value = knobInput.getText().getFloatValue();
    if (knobPercent[static_cast<size_t>(index)])
        value /= 100.0f;
    const auto* parameter = processor.parameters.getParameter(knobIds[static_cast<size_t>(index)]);
    const auto range = parameter->getNormalisableRange();
    value = juce::jlimit(range.start, range.end, value);
    knobInput.setVisible(false);
    processor.setParameter(knobIds[static_cast<size_t>(index)], value);
    repaint();
}

void EntropyAudioProcessorEditor::cancelKnobEdit()
{
    if (editingSlider < 0)
        return;
    editingSlider = -1;
    knobInput.setVisible(false);
    repaint();
}

void EntropyAudioProcessorEditor::beginTimelineEdit()
{
    const auto snapshot = processor.timelineSnapshot();
    timelineInput.setText(juce::String(juce::roundToInt(snapshot.amount() * 100.0f)));
    const auto area = timelineControl.getBounds();
    const int w = juce::roundToInt(120.0f * scale);
    const int h = juce::roundToInt(34.0f * scale);
    timelineInput.setBounds(area.getCentreX() - w / 2, area.getCentreY() - h / 2, w, h);
    timelineInput.setVisible(true);
    timelineInput.toFront(false);
    timelineInput.grabKeyboardFocus();
    timelineInput.selectAll();
    repaint();
}

void EntropyAudioProcessorEditor::commitTimelineEdit()
{
    if (!timelineInput.isVisible())
        return;
    const auto text = timelineInput.getText().trim();
    const float value = text.getFloatValue();
    const bool valid = text.isNotEmpty() && text.containsOnly("0123456789.")
        && std::isfinite(value);
    timelineInput.setVisible(false);
    if (valid)
    {
        const auto snapshot = processor.timelineSnapshot();
        processor.beginDateGesture();
        processor.selectDate(entropy::timeline::dateAt(juce::jlimit(0.0f, 1.0f, value / 100.0f),
                                                       snapshot.now, snapshot.carrier));
        processor.endDateGesture();
        timelineControl.setSnapshot(processor.timelineSnapshot());
    }
    repaint();
}

void EntropyAudioProcessorEditor::cancelTimelineEdit()
{
    timelineInput.setVisible(false);
    repaint();
}
