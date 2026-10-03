#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Thermodynamics.h"
#include "../Source/UI/TimelineControl.h"
#include "../Source/UI/PresetPanel.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
juce::MemoryBlock binary(const juce::ValueTree& tree)
{
    juce::MemoryBlock result;
    if (auto xml = tree.createXml())
        juce::AudioProcessor::copyXmlToBinary(*xml, result);
    return result;
}
void verifyAudio(EntropyAudioProcessor& processor)
{
    processor.prepareToPlay(48000.0, 64);
    require(processor.getLatencySamples() == 480, "Processor latency is wrong");
    juce::AudioBuffer<float> buffer(2, 8192);
    juce::MidiBuffer midi;
    buffer.clear();
    buffer.setSample(0, 0, 0.1f);
    buffer.setSample(1, 0, -0.1f);
    processor.processBlock(buffer, midi);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            require(std::isfinite(buffer.getSample(ch, i)), "Invalid restored audio");
    processor.releaseResources();
}

void timelineTests()
{
    namespace tl = entropy::timeline;
    const juce::int64 initialNow = juce::Time(2026, 9, 1, 12, 34, 56).toMilliseconds();
    std::atomic<juce::int64> clock { initialNow };
    EntropyAudioProcessor processor(&clock);
    for (int carrier = 0; carrier < 4; ++carrier)
    {
        processor.setParameter(entropy::ids::carrier, static_cast<float>(carrier));
        processor.setParameter(entropy::ids::entropy, 0.25f);
        const auto selected = processor.timelineSnapshot().selected;
        require(std::abs(processor.currentControls().entropy - 0.25f) < 0.000001f, "Carrier time calibration mismatch");
        const float nominal = processor.parameters.getRawParameterValue(entropy::ids::entropy)->load();
        clock.store(initialNow + tl::spanMs(carrier) / 4);
        require(processor.timelineSnapshot().selected == selected, "Clock changed the selected date");
        require(std::abs(processor.currentControls().entropy - 0.5f) < 0.000001f, "Headless clock ageing failed");
        require(processor.parameters.getRawParameterValue(entropy::ids::entropy)->load() == nominal,
                "Clock ageing wrote host automation");
        verifyAudio(processor);
        clock.store(initialNow);
        processor.setParameter(entropy::ids::entropy, 1.0f);
        const auto oldestDate = processor.timelineSnapshot().selected;
        clock.store(initialNow + tl::year);
        require(processor.currentControls().entropy == 1.0f && processor.timelineSnapshot().selected == oldestDate,
                "Oldest endpoint must remain capped after another real year");
        clock.store(initialNow);
    }

    processor.setParameter(entropy::ids::carrier, 2);
    const juce::int64 exactDate = initialNow - tl::day / 3 - 1234;
    processor.beginDateGesture(); processor.selectDate(exactDate); processor.endDateGesture();
    require(processor.timelineSnapshot().selected == exactDate, "Date precision lost to parameter quantisation");
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    clock.store(initialNow + tl::day / 6);
    EntropyAudioProcessor restored(&clock);
    require(restored.restoreState(saved.getData(), static_cast<int>(saved.getSize())), "Date snapshot did not restore");
    require(restored.timelineSnapshot().selected == exactDate, "Restoring reset the absolute date");
    require(restored.currentControls().entropy > 0.5f, "Time while project was closed was lost");
    clock.store(exactDate - 1000);
    require(restored.currentControls().entropy == 0.0f && restored.timelineSnapshot().selected == exactDate,
            "Clock rollback corrupted date or produced negative entropy");
    clock.store(initialNow);
    restored.setParameter(entropy::ids::carrier, 0);
    require(restored.timelineSnapshot().selected == exactDate && restored.currentControls().entropy < 0.001f,
            "Carrier switch re-anchored the date");
    restored.setParameter(entropy::ids::carrier, 2);
    auto* hostParameter = restored.parameters.getParameter(entropy::ids::entropy);
    hostParameter->setValueNotifyingHost(hostParameter->convertTo0to1(0.75f));
    require(std::abs(restored.currentControls().entropy - 0.75f) < 0.000001f, "Direct host automation did not set the date");
    restored.setParameter(entropy::ids::carrier, 0);
    hostParameter->setValueNotifyingHost(hostParameter->convertTo0to1(0.25f));
    restored.setParameter(entropy::ids::carrier, 2);
    require(restored.currentControls().entropy == 1.0f, "Old date did not saturate shorter scale");
    hostParameter->setValueNotifyingHost(hostParameter->convertTo0to1(0.25f));
    require(std::abs(restored.currentControls().entropy - 0.25f) < 0.000001f, "Repeated host value was lost to APVTS deduplication");
    restored.loadFactoryPreset(12);
    const auto presetDate = restored.timelineSnapshot().selected;
    clock.store(initialNow + 10000);
    restored.loadFactoryPreset(12);
    require(restored.timelineSnapshot().selected == presetDate + 10000, "Reloading identical preset did not re-anchor");
    clock.store(initialNow);

    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    require(xml != nullptr, "Date state is not XML binary");
    const auto state = juce::ValueTree::fromXml(*xml);
    auto oldState = state.createCopy();
    oldState.setProperty("schemaVersion", 1, nullptr);
    oldState.removeProperty("selectedUtcMs", nullptr);
    oldState.removeProperty("timelineVersion", nullptr);
    oldState.removeProperty("moduleBypass", nullptr);
    oldState.removeProperty("moduleIntensity", nullptr);
    juce::ValueTree legacyNoise("PARAM");
    legacyNoise.setProperty("id", "noise", nullptr);
    legacyNoise.setProperty("value", 0.4f, nullptr);
    oldState.getChildWithName("Parameters").addChild(legacyNoise, -1, nullptr);
    const auto oldBinary = binary(oldState);
    EntropyAudioProcessor migrated(&clock);
    require(migrated.restoreState(oldBinary.getData(), static_cast<int>(oldBinary.getSize())), "Legacy state did not migrate");
    const float oldAmount = processor.parameters.getRawParameterValue(entropy::ids::entropy)->load();
    require(std::abs(migrated.currentControls().entropy - oldAmount) < 0.000001f, "Legacy entropy was not preserved on migration");
    require(std::abs(migrated.moduleIntensityValue(0, 4) - 0.4f) < 0.000001f
            && migrated.moduleIntensityValue(2, 4) == 1.0f, "v1 noise did not migrate to surface rows");
    const auto migratedDate = migrated.timelineSnapshot().selected;
    juce::MemoryBlock migratedState;
    migrated.getStateInformation(migratedState);
    clock.store(initialNow + 5000);
    require(migrated.restoreState(migratedState.getData(), static_cast<int>(migratedState.getSize()))
        && migrated.timelineSnapshot().selected == migratedDate, "Migrated date was migrated twice");
    clock.store(initialNow);

    for (int test = 0; test < 6; ++test)
    {
        auto invalidState = state.createCopy();
        if (test == 0) invalidState.removeProperty("selectedUtcMs", nullptr);
        if (test == 1) invalidState.setProperty("selectedUtcMs", "123abc", nullptr);
        if (test == 2) invalidState.setProperty("selectedUtcMs", "9223372036854775808", nullptr);
        if (test == 3) invalidState.setProperty("timelineVersion", 99, nullptr);
        if (test == 4) invalidState.setProperty("selectedUtcMs", tl::earliest - 1, nullptr);
        if (test == 5) invalidState.addChild(invalidState.getChildWithName("Parameters").createCopy(), -1, nullptr);
        juce::MemoryBlock before, after;
        restored.getStateInformation(before);
        const auto invalid = binary(invalidState);
        require(!restored.restoreState(invalid.getData(), static_cast<int>(invalid.getSize())), "Malformed timeline accepted");
        restored.getStateInformation(after);
        require(before == after, "Rejected state changed parameters or date metadata");
    }

    juce::int64 parsed = 0;
    const auto dateText = juce::String::fromUTF8("1996年02月29日23时59分58秒");
    require(tl::parseLocalDate(dateText, parsed) && tl::localDate(parsed) == "29 Feb 1996 23:59:58", "Legacy date to English display failed");
    const auto leapDate = parsed;
    require(tl::parseLocalDate(tl::localDate(leapDate), parsed) && parsed == leapDate, "English date editing roundtrip failed");
    require(tl::parseLocalDate("29 feb 1996 23:59:58", parsed) && parsed == leapDate, "Case-insensitive month parsing failed");
    require(!tl::parseLocalDate("29 Xxx 1996 23:59:58", parsed), "Unknown English month accepted");
    require(!tl::parseLocalDate("29 Feb 1997 23:59:58", parsed), "Invalid English leap date accepted");
    require(tl::parseLocalDate("1926-10-01 12:34:56", parsed) && tl::tickLabel(parsed, false) == "1926/10",
            "Pre-1970 date was not supported");
    require(!tl::parseLocalDate("1997-02-29 12:00:00", parsed), "Invalid leap date accepted");
    require(!tl::parseLocalDate("2026-10-01 24:00:00", parsed), "Invalid hour accepted");
    require(!tl::parseLocalDate("2026-10-01 12:00:00 trailing", parsed), "Trailing date garbage accepted");
    require(tl::localDate(initialNow + 1000) != tl::localDate(initialNow), "Local clock does not show seconds");

    entropy::ui::TimelineControl control;
    control.setSize(578, 54);
    restored.setParameter(entropy::ids::carrier, 0);
    restored.setParameter(entropy::ids::entropy, 0.5f);
    control.setSnapshot(restored.timelineSnapshot());
    int starts = 0, ends = 0;
    control.onGestureBegin = [&] { ++starts; restored.beginDateGesture(); };
    control.onGestureEnd = [&] { ++ends; restored.endDateGesture(); };
    control.onSelectPosition = [&](double position)
    {
        const auto snapshot = restored.timelineSnapshot();
        restored.selectDate(tl::dateAt(static_cast<float>(1.0 - position), snapshot.now, snapshot.carrier));
        control.setSnapshot(restored.timelineSnapshot());
    };
    control.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    require(restored.currentControls().entropy > 0.5f, "Left arrow must increase age and disorder");
    control.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    require(std::abs(restored.currentControls().entropy - 0.5f) < 0.000001f, "Right arrow must decrease age");
    control.keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
    require(restored.currentControls().entropy == 1.0f && control.getPosition() == 0.0, "Oldest endpoint is wrong");
    control.keyPressed(juce::KeyPress(juce::KeyPress::endKey));
    require(restored.timelineSnapshot().selected == initialNow && control.getPosition() == 1.0, "Now endpoint is wrong");
    require(starts == 4 && ends == 4, "Timeline gestures are not paired");
    const auto mouseEvent = [&](int clicks, bool dragged, float x)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x, 17 },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            &control, &control, juce::Time(initialNow), { 10, 17 }, juce::Time(initialNow), clicks, dragged);
    };
    bool edited = false;
    control.onEditDate = [&] { edited = true; };
    const auto click = mouseEvent(1, false, 10);
    const auto doubleClick = mouseEvent(2, false, 10);
    control.mouseDown(click); control.mouseUp(click);
    control.mouseDown(doubleClick); control.mouseDoubleClick(doubleClick); control.mouseUp(doubleClick);
    require(edited && starts == 4 && ends == 4 && restored.timelineSnapshot().selected == initialNow,
            "Double-click editing changed date before confirmation");
    control.mouseDown(click);
    control.mouseDrag(mouseEvent(1, true, 100));
    require(restored.currentControls().entropy > 0.8f, "Timeline drag direction is wrong");
    control.mouseUp(click);
    require(starts == 5 && ends == 5, "Mouse drag gesture was not paired");
    std::cout << "PASS carrier timescales / headless ageing / exact dates / host automation / legacy migration / date validation / reversed ruler\n";
}

void presetPanelTests()
{
    std::atomic<juce::int64> clock { juce::Time(2026, 9, 1, 12, 34, 56).toMilliseconds() };
    EntropyAudioProcessor processor(&clock);
    for (int width : { 720, 960, 1920 })
    {
        processor.loadFactoryPreset(2);
        processor.editorWidth.store(width);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        entropy::ui::PresetPanel* panel = nullptr;
        for (auto* child : editor->getChildren())
        {
            if (auto* button = dynamic_cast<juce::TextButton*>(child))
                require(button->getButtonText() != "?", "Help button is still present");
            if (auto* candidate = dynamic_cast<entropy::ui::PresetPanel*>(child)) panel = candidate;
        }
        require(panel != nullptr && !panel->isVisible(), "Custom preset panel missing or initially open");
        const float scale = static_cast<float>(width) / 960.0f;
        const auto clickPanel = [&](float x, float y)
        {
            const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), { x * scale, y * scale },
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                panel, panel, juce::Time(clock.load()), { x * scale, y * scale }, juce::Time(clock.load()), 1, false);
            panel->mouseDown(event);
        };
        const auto clickEditor = [&](float x, float y)
        {
            const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), { x * scale, y * scale },
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                editor.get(), editor.get(), juce::Time(clock.load()), { x * scale, y * scale }, juce::Time(clock.load()), 1, false);
            editor->mouseDown(event);
        };
        clickEditor(480, 26);
        require(panel->isVisible() && panel->getBounds() == editor->getLocalBounds(), "Formula area failed to show overlay");
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("Entropy-preview-presets-" + juce::String(width) + ".png");
        auto stream = file.createOutputStream();
        require(image.isValid() && stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk()
            && juce::PNGImageFormat().writeImageToStream(image, *stream), "Preset panel preview failed");
        const auto date = processor.timelineSnapshot().selected;
        clickPanel(40, 400);
        require(!panel->isVisible() && processor.timelineSnapshot().selected == date, "Outside click did not dismiss safely");
        for (int index = 0; index < 20; ++index)
        {
            clickEditor(480, 26);
            clickPanel(100.0f + 200.0f * static_cast<float>(index / 5), 145.0f + 26.0f * static_cast<float>(index % 5));
            require(!panel->isVisible() && processor.matchingFactoryPreset() == index, "Preset hit testing or loading failed");
        }
        clickEditor(480, 26);
        panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        require(!panel->isVisible(), "Escape did not dismiss archive");
        processor.loadFactoryPreset(0);
        clickEditor(480, 26);
        panel->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
        panel->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        require(!panel->isVisible() && processor.matchingFactoryPreset() == 1, "Keyboard preset selection failed");
        bool saveCalled = false, openCalled = false;
        panel->onSave = [&] { saveCalled = true; panel->setVisible(false); };
        panel->onOpen = [&] { openCalled = true; panel->setVisible(false); };
        clickEditor(480, 26); clickPanel(100, 294);
        clickEditor(480, 26); clickPanel(260, 294);
        require(saveCalled && openCalled, "Snapshot actions were not preserved");
        clickEditor(480, 26); clickPanel(835, 294);
        require(!panel->isVisible(), "Close action failed");
        // 箭头按钮位置与 PluginEditor::formulaBounds 的固定预设簇一致：
        // 簇宽 = 最长名称（含 "Custom / Archive"）+ 26，以 480 居中，
        // 按钮中心在簇两侧外 18 处，与当前预设名称长度无关。
        const auto arrowX = [](bool next)
        {
            const juce::Font font(juce::FontOptions(17.0f));
            float w = juce::GlyphArrangement::getStringWidth(font, "Custom / Archive");
            for (const auto& p : entropy::factoryPresets())
                w = juce::jmax(w, juce::GlyphArrangement::getStringWidth(font, p.name));
            w = juce::jmax(84.0f, w + 26.0f);
            return (480.0f - w * 0.5f) + (next ? w + 18.0f : -18.0f);
        };
        clickEditor(arrowX(false), 26);
        require(processor.matchingFactoryPreset() == 0, "Previous arrow did not step preset");
        clickEditor(arrowX(true), 26);
        require(processor.matchingFactoryPreset() == 1, "Next arrow did not step preset");
    }
    std::cout << "PASS custom preset overlay / centred formula cluster / arrows / all 20 items / three scales / dismissal / keyboard / snapshot actions\n";
}

void knobEditorTests()
{
    std::atomic<juce::int64> clock { juce::Time(2026, 9, 1, 12, 34, 56).toMilliseconds() };
    EntropyAudioProcessor processor(&clock);
    processor.editorWidth.store(960);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    entropy::ui::NumericSlider* input = nullptr;
    entropy::ui::NumericSlider* mix = nullptr;
    entropy::ui::KnobInputEditor* knob = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* slider = dynamic_cast<entropy::ui::NumericSlider*>(child))
        {
            if (child->getName() == "Input") input = slider;
            if (child->getName() == "Mix") mix = slider;
        }
        if (auto* candidate = dynamic_cast<entropy::ui::KnobInputEditor*>(child))
            if (child->getName() != "Timeline percent input") knob = candidate;
    }
    require(input != nullptr && mix != nullptr && knob != nullptr && !knob->isVisible(),
            "Custom knob editor missing or initially visible");
    const auto doubleClick = [&](juce::Component* component)
    {
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), { 40, 590 },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            component, component, juce::Time(clock.load()), { 40, 590 }, juce::Time(clock.load()), 2, false);
        static_cast<entropy::ui::NumericSlider*>(component)->mouseDoubleClick(event);
    };
    const float before = processor.parameters.getRawParameterValue(entropy::ids::input)->load();
    doubleClick(input);
    require(knob->isVisible() && knob->getText().isNotEmpty(), "Custom editor did not open on double click");
    require(processor.parameters.getRawParameterValue(entropy::ids::input)->load() == before,
            "Opening the editor wrote the parameter");
    knob->setText("3.5", juce::sendNotification);
    knob->onReturnKey();
    require(!knob->isVisible(), "Editor did not close on commit");
    require(std::abs(processor.parameters.getRawParameterValue(entropy::ids::input)->load() - 3.5f) < 0.001f,
            "dB knob commit failed");
    doubleClick(mix);
    knob->setText("25", juce::sendNotification);
    knob->onReturnKey();
    require(std::abs(processor.parameters.getRawParameterValue(entropy::ids::mix)->load() - 0.25f) < 0.0005f,
            "Percent knob commit failed");
    doubleClick(mix);
    knob->setText("99", juce::sendNotification);
    knob->onEscapeKey();
    require(!knob->isVisible() && std::abs(processor.parameters.getRawParameterValue(entropy::ids::mix)->load() - 0.25f) < 0.0005f,
            "Escape cancel failed");
    std::cout << "PASS custom knob editor open / dB commit / percent commit / escape cancel\n";
}

void timelineInputTests()
{
    std::atomic<juce::int64> clock { juce::Time(2026, 9, 1, 12, 34, 56).toMilliseconds() };
    EntropyAudioProcessor processor(&clock);
    processor.setParameter(entropy::ids::carrier, 0);
    processor.setParameter(entropy::ids::entropy, 0.5f);
    processor.editorWidth.store(960);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    entropy::ui::TimelineControl* timeline = nullptr;
    entropy::ui::KnobInputEditor* percentInput = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* candidate = dynamic_cast<entropy::ui::TimelineControl*>(child)) timeline = candidate;
        if (child->getName() == "Timeline percent input")
            if (auto* candidate = dynamic_cast<entropy::ui::KnobInputEditor*>(child)) percentInput = candidate;
    }
    require(timeline != nullptr && percentInput != nullptr && !percentInput->isVisible(),
            "Timeline percent editor missing or initially visible");
    const auto doubleClick = [&]()
    {
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), { 290, 40 },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            timeline, timeline, juce::Time(clock.load()), { 290, 40 }, juce::Time(clock.load()), 2, false);
        timeline->mouseDoubleClick(event);
    };
    doubleClick();
    require(percentInput->isVisible() && percentInput->getText() == "50", "Double-click did not open the 0-100 editor");
    percentInput->setText("75", juce::sendNotification);
    percentInput->onReturnKey();
    require(!percentInput->isVisible(), "Timeline editor did not close on commit");
    require(std::abs(processor.currentControls().entropy - 0.75f) < 0.000001f, "Timeline percent commit failed");
    doubleClick();
    percentInput->setText("25.5", juce::sendNotification);
    percentInput->onEscapeKey();
    require(!percentInput->isVisible() && std::abs(processor.currentControls().entropy - 0.75f) < 0.000001f,
            "Escape must cancel the timeline edit");
    doubleClick();
    percentInput->setText("abc", juce::sendNotification);
    percentInput->onReturnKey();
    require(!percentInput->isVisible() && std::abs(processor.currentControls().entropy - 0.75f) < 0.000001f,
            "Invalid text must not move the date");
    doubleClick();
    percentInput->setText("120", juce::sendNotification);
    percentInput->onReturnKey();
    require(std::abs(processor.currentControls().entropy - 1.0f) < 0.000001f, "Out-of-range percent must clamp");
    doubleClick();
    require(percentInput->getText() == "100", "Clamped value must be shown on the next open");
    percentInput->onEscapeKey();
    std::cout << "PASS timeline 0-100 quick edit / commit / escape / invalid text / clamping\n";
}

void thermodynamicsTests()
{
    namespace tl = entropy::timeline;
    for (int carrier = 0; carrier < 4; ++carrier)
    {
        const auto system = entropy::thermo::systemFor(carrier);
        require(std::abs(system.deltaH - entropy::thermo::ambientKelvin * 0.5 * system.deltaSMax) < 0.000001,
                "ΔG must cross zero exactly at entropy 0.5");
        require(std::abs(system.halfLifeSeconds - tl::spanMs(carrier) / 1000.0) < 0.5,
                "Arrhenius half-life must equal the carrier time span");
        require(std::isfinite(system.rateConstant) && system.rateConstant > 0.0, "Rate constant must be positive");
        require(system.activationEnergy > 0.0 && system.deltaH > 0.0, "Thermodynamic constants must be positive");
    }
    std::cout << "PASS thermodynamic constants / ΔG zero crossing at 0.5 / half-life equals time span\n";
}

void moduleBypassTests()
{
    std::atomic<juce::int64> clock { juce::Time(2026, 9, 1, 12, 34, 56).toMilliseconds() };
    EntropyAudioProcessor processor(&clock);
    processor.setModuleBypass(1, 3, true);
    processor.setModuleBypass(2, 0, true);
    require(processor.isModuleBypassed(1, 3) && processor.isModuleBypassed(2, 0), "Module bypass bits did not set");
    require(!processor.isModuleBypassed(0, 0) && !processor.isModuleBypassed(1, 2), "Module bypass leaked to other rows");
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    EntropyAudioProcessor restored(&clock);
    require(restored.restoreState(saved.getData(), static_cast<int>(saved.getSize())), "Bypass state did not restore");
    require(restored.isModuleBypassed(1, 3) && restored.isModuleBypassed(2, 0) && !restored.isModuleBypassed(0, 0),
            "Bypass mask roundtrip mismatch");
    restored.setModuleBypass(1, 3, false);
    require(!restored.isModuleBypassed(1, 3) && restored.isModuleBypassed(2, 0), "Bypass clear touched other bits");
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    const auto state = juce::ValueTree::fromXml(*xml);
    auto invalidState = state.createCopy();
    invalidState.setProperty("moduleBypass", 0x100000, nullptr);
    const auto invalid = binary(invalidState);
    juce::MemoryBlock before, after;
    restored.getStateInformation(before);
    require(!restored.restoreState(invalid.getData(), static_cast<int>(invalid.getSize())), "Out-of-range mask accepted");
    restored.getStateInformation(after);
    require(before == after, "Rejected mask changed state");
    processor.setModuleBypass(0, 0, false);
    processor.editorWidth.store(960);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    const auto click = [&](float x, float y)
    {
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), { x, y },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            editor.get(), editor.get(), juce::Time(clock.load()), { x, y }, juce::Time(clock.load()), 1, false);
        editor->mouseDown(event);
        editor->mouseUp(event);
    };
    const int carrierNow = processor.currentControls().carrier;
    click(700, 220);
    require(processor.isModuleBypassed(carrierNow, 0), "Descriptor click did not bypass");
    click(700, 220);
    require(!processor.isModuleBypassed(carrierNow, 0), "Descriptor click did not re-enable");
    const auto drag = [&](float x0, float x1)
    {
        const juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(), { x0, 220 },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            editor.get(), editor.get(), juce::Time(clock.load()), { x0, 220 }, juce::Time(clock.load()), 1, false);
        const juce::MouseEvent move(juce::Desktop::getInstance().getMainMouseSource(), { x1, 220 },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            editor.get(), editor.get(), juce::Time(clock.load()), { x0, 220 }, juce::Time(clock.load()), 1, true);
        editor->mouseDown(down);
        editor->mouseDrag(move);
        editor->mouseUp(move);
    };
    drag(700.0f, 790.0f);
    const float draggedIntensity = processor.moduleIntensityValue(carrierNow, 0);
    require(draggedIntensity > 0.3f && draggedIntensity < 0.5f, "Descriptor bar drag did not set intensity");
    require(!processor.isModuleBypassed(carrierNow, 0), "Intensity drag must not toggle bypass");
    click(700, 220);
    require(processor.isModuleBypassed(carrierNow, 0) && std::abs(processor.moduleIntensityValue(carrierNow, 0) - draggedIntensity) < 0.000001f,
            "Click after drag did not toggle bypass or changed intensity");
    click(700, 220);
    processor.setModuleIntensity(1, 3, 0.25f);
    processor.setModuleIntensity(3, 4, 0.0f);
    juce::MemoryBlock intensitySaved;
    processor.getStateInformation(intensitySaved);
    EntropyAudioProcessor intensityRestored(&clock);
    require(intensityRestored.restoreState(intensitySaved.getData(), static_cast<int>(intensitySaved.getSize())),
            "Intensity state did not restore");
    require(std::abs(intensityRestored.moduleIntensityValue(1, 3) - 0.25f) < 0.000001f
            && intensityRestored.moduleIntensityValue(3, 4) == 0.0f
            && std::abs(intensityRestored.moduleIntensityValue(0, 0) - processor.moduleIntensityValue(0, 0)) < 0.000001f,
            "Intensity roundtrip mismatch");
    auto intensityXml = juce::AudioProcessor::getXmlFromBinary(intensitySaved.getData(), static_cast<int>(intensitySaved.getSize()));
    const auto intensityState = juce::ValueTree::fromXml(*intensityXml);
    for (int test = 0; test < 3; ++test)
    {
        auto invalidIntensityState = intensityState.createCopy();
        if (test == 0) invalidIntensityState.setProperty("moduleIntensity", "0.5 0.5", nullptr);
        if (test == 1)
        {
            juce::String outOfRange;
            for (int i = 0; i < 20; ++i)
                outOfRange += juce::String(i == 3 ? "1.2" : "0.5") + juce::String(i == 19 ? "" : " ");
            invalidIntensityState.setProperty("moduleIntensity", outOfRange, nullptr);
        }
        if (test == 2) invalidIntensityState.setProperty("moduleIntensity", "zero one two", nullptr);
        juce::MemoryBlock beforeTamper, afterTamper;
        intensityRestored.getStateInformation(beforeTamper);
        const auto tampered = binary(invalidIntensityState);
        require(!intensityRestored.restoreState(tampered.getData(), static_cast<int>(tampered.getSize())), "Malformed intensity accepted");
        intensityRestored.getStateInformation(afterTamper);
        require(beforeTamper == afterTamper, "Rejected intensity changed state");
    }
    // v2 迁移：旧 noise 参数写入各载体 SURFACE 行，流媒体 SMEAR 行保持满强度。
    auto legacyState = intensityState.createCopy();
    legacyState.setProperty("schemaVersion", 2, nullptr);
    legacyState.removeProperty("moduleIntensity", nullptr);
    auto legacyParams = legacyState.getChildWithName("Parameters");
    juce::ValueTree noiseParam("PARAM");
    noiseParam.setProperty("id", "noise", nullptr);
    noiseParam.setProperty("value", 0.3f, nullptr);
    legacyParams.addChild(noiseParam, -1, nullptr);
    const auto legacyBinary = binary(legacyState);
    EntropyAudioProcessor migrated(&clock);
    require(migrated.restoreState(legacyBinary.getData(), static_cast<int>(legacyBinary.getSize())), "v2 state did not migrate");
    for (int carrier : { 0, 1, 3 })
        require(std::abs(migrated.moduleIntensityValue(carrier, 4) - 0.3f) < 0.000001f, "v2 noise did not migrate to SURFACE rows");
    require(migrated.moduleIntensityValue(2, 4) == 1.0f, "v2 migration must not touch the streaming smear row");
    for (int carrier = 0; carrier < 4; ++carrier)
        for (int row = 0; row < 5; ++row)
            processor.setModuleBypass(carrier, row, true);
    processor.setModuleIntensity(2, 1, 0.4f);
    click(905, 178);
    for (int carrier = 0; carrier < 4; ++carrier)
        for (int row = 0; row < 5; ++row)
            require(!processor.isModuleBypassed(carrier, row), "Reset button did not restore all effects");
    const auto defaults = entropy::defaultIntensity();
    for (int i = 0; i < 20; ++i)
        require(std::abs(processor.moduleIntensityValue(i / 5, i % 5) - defaults[static_cast<size_t>(i)]) < 0.000001f,
                "Reset button did not restore default intensities");
    std::cout << "PASS module bypass bits / state roundtrip / tamper rejection / descriptor click vs drag / intensity state / v2 migration / reset all\n";
}
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        timelineTests();
        thermodynamicsTests();
        presetPanelTests();
        knobEditorTests();
        timelineInputTests();
        moduleBypassTests();
        std::atomic<juce::int64> testClock { juce::Time(2026, 9, 1, 12, 34, 56).toMilliseconds() };
        EntropyAudioProcessor original(&testClock);
        const auto presetDefaults = entropy::defaultIntensity();
        for (int preset = 0; preset < 20; ++preset)
        {
            original.loadFactoryPreset(preset);
            const auto& definition = entropy::factoryPresets()[static_cast<size_t>(preset)];
            for (int row = 0; row < 5; ++row)
                require(std::abs(original.moduleIntensityValue(definition.carrier, row)
                        - definition.intensity[static_cast<size_t>(row)]) < 0.000001f,
                        "Preset did not apply its intensity profile");
            for (int carrier = 0; carrier < 4; ++carrier)
                if (carrier != definition.carrier)
                    for (int row = 0; row < 5; ++row)
                        require(std::abs(original.moduleIntensityValue(carrier, row)
                                - presetDefaults[static_cast<size_t>(carrier * 5 + row)]) < 0.000001f,
                                "Preset touched another carrier's rows");
            original.editorWidth.store(1200);
            juce::MemoryBlock saved;
            original.getStateInformation(saved);
            EntropyAudioProcessor restored(&testClock);
            require(restored.restoreState(saved.getData(), static_cast<int>(saved.getSize())), "State rejected a valid snapshot");
            for (const auto* id : entropy::ids::all)
                require(std::abs(original.parameters.getRawParameterValue(id)->load() - restored.parameters.getRawParameterValue(id)->load()) < 0.00001f,
                        "Parameter roundtrip mismatch");
            require(restored.matchingFactoryPreset() == preset, "Factory identity did not roundtrip");
            require(restored.editorWidth.load() == 1200, "Editor size was not restored");
            for (int i = 0; i < 20; ++i)
                require(std::abs(original.moduleIntensityValue(i / 5, i % 5) - restored.moduleIntensityValue(i / 5, i % 5)) < 0.000001f,
                        "Intensity did not roundtrip");
            verifyAudio(restored);
        }
        original.loadFactoryPreset(0);
        original.setModuleIntensity(0, 1, 0.35f);
        require(original.matchingFactoryPreset() == -1, "Custom intensity must break factory identity");
        original.resetModuleSettings();
        require(original.matchingFactoryPreset() == 0, "Reset must restore factory identity");
        std::cout << "PASS 20 presets / state roundtrip / headless restoration / oversized host blocks / intensity identity\n";
        juce::MemoryBlock valid;
        original.getStateInformation(valid);
        auto xml = juce::AudioProcessor::getXmlFromBinary(valid.getData(), static_cast<int>(valid.getSize()));
        require(xml != nullptr, "Invalid state encoding");
        const auto validTree = juce::ValueTree::fromXml(*xml);
        const float before = original.currentControls().entropy;
        for (int test = 0; test < 7; ++test)
        {
            auto tree = validTree.createCopy();
            auto params = tree.getChildWithName("Parameters");
            if (test == 0) tree.setProperty("schemaVersion", 999, nullptr);
            if (test == 1) params.removeChild(0, nullptr);
            if (test == 2) params.getChild(0).setProperty("value", "nan", nullptr);
            if (test == 3) params.getChild(0).setProperty("value", "1oops", nullptr);
            if (test == 4) params.getChild(0).setProperty("value", 999, nullptr);
            if (test == 5) params.getChild(0).setProperty("id", "unknown", nullptr);
            if (test == 6) params.getChild(0).setProperty("value", 1.5, nullptr);
            const auto invalid = binary(tree);
            require(!original.restoreState(invalid.getData(), static_cast<int>(invalid.getSize())), "Malformed state was accepted");
            require(original.currentControls().entropy == before, "Failed restoration mutated parameters");
        }
        require(!original.restoreState(nullptr, 0), "Empty state accepted");
        require(!original.restoreState(valid.getData(), 4), "Truncated state accepted");
        std::cout << "PASS malformed / truncated / future-version state rejection\n";
        original.loadFactoryPreset(2);
        original.editorWidth.store(960);
        for (int pass = 0; pass < 3; ++pass)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(original.createEditor());
            require(editor != nullptr && editor->getWidth() == 960, "Editor construction failed");
            if (auto* entropyEditor = dynamic_cast<EntropyAudioProcessorEditor*>(editor.get()))
                entropyEditor->advanceAnimationForTesting(20);
            if (pass == 0)
            {
                auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true);
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("Entropy-preview.png");
                auto stream = file.createOutputStream();
                require(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk()
                    && juce::PNGImageFormat().writeImageToStream(image, *stream), "UI preview could not be written");
            }
        }
        for (int carrier = 0; carrier < 4; ++carrier)
            for (int width : { 720, 960, 1920 })
            {
                original.loadFactoryPreset(carrier * 5 + 2);
                original.editorWidth.store(width);
                const auto controlsBeforePainting = original.currentControls();
                for (float age : { 0.0f, 0.46f, 1.0f })
                {
                    original.setParameter(entropy::ids::entropy, age);
                    std::unique_ptr<juce::AudioProcessorEditor> editor(original.createEditor());
                    require(editor->getWidth() == width && editor->getHeight() * 3 == width * 2, "Scaled editor bounds are wrong");
                    if (auto* entropyEditor = dynamic_cast<EntropyAudioProcessorEditor*>(editor.get()))
                        entropyEditor->advanceAnimationForTesting(20);
                    const float ageBeforePainting = original.currentControls().entropy;
                    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true);
                    require(image.isValid() && image.getWidth() == width, "Carrier preview rendering failed");
                    if (width == 960 && age == 0.46f)
                    {
                        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(
                            "Entropy-preview-" + juce::String(entropy::carrierName(carrier)) + ".png");
                        auto stream = file.createOutputStream();
                        require(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk()
                            && juce::PNGImageFormat().writeImageToStream(image, *stream), "Carrier preview could not be saved");
                    }
                    require(original.currentControls().carrier == carrier && original.currentControls().entropy == ageBeforePainting,
                            "Painting changed carrier parameters");
                }
                require(original.currentControls().mix == controlsBeforePainting.mix
                        && original.moduleIntensityValue(carrier, 4) == controlsBeforePainting.intensity[static_cast<size_t>(carrier * 5 + 4)],
                        "Painting changed mix or module intensities");
            }
        std::cout << "PASS editor lifecycle / four carrier previews / three UI scales / entropy endpoints\nEntropy state tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
