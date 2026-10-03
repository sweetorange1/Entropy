#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <charconv>

namespace
{
thread_local const EntropyAudioProcessor* dateWriteOwner = nullptr;
class DateWriteScope
{
public:
    explicit DateWriteScope(const EntropyAudioProcessor* owner) : previous(dateWriteOwner) { dateWriteOwner = owner; }
    ~DateWriteScope() { dateWriteOwner = previous; }
private:
    const EntropyAudioProcessor* previous;
};

void accumulatePeak(std::atomic<float>& meter, float peak) noexcept
{
    static_assert(std::atomic<float>::is_always_lock_free);
    float previous = meter.load(std::memory_order_relaxed);
    while (previous < peak && !meter.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
}
}

EntropyAudioProcessor::EntropyAudioProcessor(const std::atomic<juce::int64>* clockForTesting)
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", entropy::createParameterLayout()), wallClock(clockForTesting)
{
    for (size_t i = 0; i < raw.size(); ++i)
        raw[i] = parameters.getRawParameterValue(entropy::ids::all[i]);
    const auto defaults = entropy::defaultIntensity();
    for (size_t i = 0; i < moduleIntensity.size(); ++i)
        moduleIntensity[i].store(defaults[i], std::memory_order_relaxed);
    selectedDate.store(wallClock.now());
    parameters.getParameter(entropy::ids::entropy)->addListener(this);
}

EntropyAudioProcessor::~EntropyAudioProcessor()
{
    parameters.getParameter(entropy::ids::entropy)->removeListener(this);
}

void EntropyAudioProcessor::parameterValueChanged(int, float value)
{
    if (dateWriteOwner != this)
    {
        const auto carrier = static_cast<int>(entropy::finiteClamp(raw[0]->load(), 0.0f, 3.0f));
        selectedDate.store(entropy::timeline::dateAt(value, wallClock.now(), carrier));
    }
}

entropy::timeline::Snapshot EntropyAudioProcessor::timelineSnapshot() const noexcept
{
    return { wallClock.now(), selectedDate.load(),
             static_cast<int>(entropy::finiteClamp(raw[0]->load(), 0.0f, 3.0f)) };
}

void EntropyAudioProcessor::beginDateGesture() { parameters.getParameter(entropy::ids::entropy)->beginChangeGesture(); }
void EntropyAudioProcessor::endDateGesture() { parameters.getParameter(entropy::ids::entropy)->endChangeGesture(); }

bool EntropyAudioProcessor::isModuleBypassed(int carrier, int row) const noexcept
{
    const int bit = carrier * 5 + row;
    if (bit < 0 || bit >= 20)
        return false;
    return (moduleBypass.load(std::memory_order_relaxed) >> bit) & 1u;
}

void EntropyAudioProcessor::setModuleBypass(int carrier, int row, bool bypassed)
{
    const int bit = carrier * 5 + row;
    if (bit < 0 || bit >= 20)
        return;
    const std::uint32_t flag = 1u << bit;
    if (bypassed)
        moduleBypass.fetch_or(flag, std::memory_order_relaxed);
    else
        moduleBypass.fetch_and(~flag, std::memory_order_relaxed);
}

void EntropyAudioProcessor::resetModuleBypass() noexcept
{
    moduleBypass.store(0, std::memory_order_relaxed);
}

float EntropyAudioProcessor::moduleIntensityValue(int carrier, int row) const noexcept
{
    const int index = carrier * 5 + row;
    if (index < 0 || index >= 20)
        return 1.0f;
    return moduleIntensity[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

void EntropyAudioProcessor::setModuleIntensity(int carrier, int row, float value)
{
    const int index = carrier * 5 + row;
    if (index < 0 || index >= 20)
        return;
    moduleIntensity[static_cast<size_t>(index)].store(
        entropy::finiteClamp(value, 0.0f, 1.0f, 1.0f), std::memory_order_relaxed);
}

void EntropyAudioProcessor::resetModuleSettings()
{
    moduleBypass.store(0, std::memory_order_relaxed);
    const auto defaults = entropy::defaultIntensity();
    for (size_t i = 0; i < moduleIntensity.size(); ++i)
        moduleIntensity[i].store(defaults[i], std::memory_order_relaxed);
}

void EntropyAudioProcessor::selectDate(juce::int64 milliseconds)
{
    const auto snapshot = timelineSnapshot();
    const auto date = std::clamp(milliseconds, entropy::timeline::earliest, snapshot.now);
    selectedDate.store(date);
    const DateWriteScope scope(this);
    auto* parameter = parameters.getParameter(entropy::ids::entropy);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(entropy::timeline::amountAt(date, snapshot.now, snapshot.carrier)));
}

entropy::Controls EntropyAudioProcessor::currentControls() const noexcept
{
    const auto snapshot = timelineSnapshot();
    entropy::Controls c;
    c.carrier = snapshot.carrier;
    c.entropy = snapshot.amount();
    c.mix = raw[2]->load();
    c.inputDb = raw[3]->load();
    c.outputDb = raw[4]->load();
    c.generations = static_cast<int>(entropy::finiteClamp(raw[5]->load(), 1.0f, 8.0f, 1.0f));
    c.bypass = raw[6]->load() >= 0.5f;
    for (size_t i = 0; i < moduleIntensity.size(); ++i)
        c.intensity[i] = moduleIntensity[i].load(std::memory_order_relaxed);
    return c;
}

void EntropyAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine.setControls(currentControls());
    engine.prepare(sampleRate, getTotalNumInputChannels());
    setLatencySamples(engine.latencySamples());
    ready = true;
}

void EntropyAudioProcessor::releaseResources()
{
    ready = false;
    inputLevel.store(0.0f);
    outputLevel.store(0.0f);
}

void EntropyAudioProcessor::reset()
{
    engine.setControls(currentControls());
    engine.reset();
}

bool EntropyAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto out = layout.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && out == layout.getMainInputChannelSet();
}

void EntropyAudioProcessor::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool hostBypassed)
{
    const juce::ScopedNoDenormals noDenormals;
    midi.clear();
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
    if (!ready)
    {
        buffer.clear();
        return;
    }
    engine.setControls(currentControls());
    engine.setModuleBypass(moduleBypass.load(std::memory_order_relaxed));
    engine.process(buffer.getArrayOfWritePointers(), juce::jmin(buffer.getNumChannels(), getTotalNumInputChannels()),
                   buffer.getNumSamples(), hostBypassed);
    accumulatePeak(inputLevel, engine.inputPeak());
    accumulatePeak(outputLevel, engine.outputPeak());
}

void EntropyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) { process(buffer, midi, false); }
void EntropyAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) { process(buffer, midi, true); }
juce::AudioProcessorParameter* EntropyAudioProcessor::getBypassParameter() const { return parameters.getParameter(entropy::ids::bypass); }
juce::AudioProcessorEditor* EntropyAudioProcessor::createEditor() { return new EntropyAudioProcessorEditor(*this); }

void EntropyAudioProcessor::setParameter(const char* id, float plainValue)
{
    if (auto* parameter = parameters.getParameter(id))
    {
        parameter->beginChangeGesture();
        if (juce::String(id) == entropy::ids::entropy)
        {
            const auto snapshot = timelineSnapshot();
            selectDate(entropy::timeline::dateAt(plainValue, snapshot.now, snapshot.carrier));
        }
        else
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
        parameter->endChangeGesture();
    }
}

void EntropyAudioProcessor::loadFactoryPreset(int index)
{
    const auto& presets = entropy::factoryPresets();
    if (!juce::isPositiveAndBelow(index, static_cast<int>(presets.size())))
        return;
    const auto& preset = presets[static_cast<size_t>(index)];
    setParameter(entropy::ids::carrier, static_cast<float>(preset.carrier));
    setParameter(entropy::ids::entropy, preset.entropy);
    setParameter(entropy::ids::generations, static_cast<float>(preset.generations));
    const auto defaults = entropy::defaultIntensity();
    for (size_t i = 0; i < moduleIntensity.size(); ++i)
        moduleIntensity[i].store(defaults[i], std::memory_order_relaxed);
    for (int row = 0; row < 5; ++row)
        moduleIntensity[static_cast<size_t>(preset.carrier * 5 + row)].store(
            preset.intensity[static_cast<size_t>(row)], std::memory_order_relaxed);
    setParameter(entropy::ids::mix, 1.0f);
    setParameter(entropy::ids::input, 0.0f);
    setParameter(entropy::ids::output, 0.0f);
    setParameter(entropy::ids::bypass, 0.0f);
}

int EntropyAudioProcessor::matchingFactoryPreset() const noexcept
{
    const auto c = currentControls();
    if (std::abs(c.mix - 1.0f) > 0.001f
        || std::abs(c.inputDb) > 0.05f || std::abs(c.outputDb) > 0.05f || c.bypass)
        return -1;
    const auto defaults = entropy::defaultIntensity();
    const auto& presets = entropy::factoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& preset = presets[i];
        if (preset.carrier != c.carrier || preset.generations != c.generations
            || std::abs(preset.entropy - c.entropy) >= 0.0002f)
            continue;
        bool matches = true;
        for (int index = 0; index < 20 && matches; ++index)
        {
            const float expected = index / 5 == preset.carrier
                ? preset.intensity[static_cast<size_t>(index % 5)]
                : defaults[static_cast<size_t>(index)];
            if (std::abs(c.intensity[static_cast<size_t>(index)] - expected) > 0.001f)
                matches = false;
        }
        if (matches)
            return static_cast<int>(i);
    }
    return -1;
}

void EntropyAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    juce::ValueTree state("EntropyState");
    state.setProperty("schemaVersion", 3, nullptr);
    state.setProperty("timelineVersion", entropy::timeline::mappingVersion, nullptr);
    state.setProperty("selectedUtcMs", selectedDate.load(), nullptr);
    state.setProperty("moduleBypass", static_cast<int>(moduleBypass.load(std::memory_order_relaxed)), nullptr);
    juce::String intensityText;
    for (size_t i = 0; i < moduleIntensity.size(); ++i)
        intensityText += juce::String(i == 0 ? "" : " ") + juce::String(moduleIntensity[i].load(std::memory_order_relaxed));
    state.setProperty("moduleIntensity", intensityText, nullptr);
    state.setProperty("editorWidth", editorWidth.load(), nullptr);
    state.addChild(parameters.copyState(), -1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

bool EntropyAudioProcessor::restoreState(const void* data, int size)
{
    if (data == nullptr || size <= 0 || size > 1024 * 1024)
        return false;
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName("EntropyState"))
        return false;
    const auto state = juce::ValueTree::fromXml(*xml);
    const auto version = state.getProperty("schemaVersion").toString();
    if (version != "1" && version != "2" && version != "3")
        return false;
    juce::int64 restoredDate = 0;
    std::uint32_t restoredMask = 0;
    auto restoredIntensity = entropy::defaultIntensity();
    if (version == "2" || version == "3")
    {
        if (state.getProperty("timelineVersion").toString() != juce::String(entropy::timeline::mappingVersion))
            return false;
        if (state.hasProperty("moduleBypass"))
        {
            const auto maskText = state.getProperty("moduleBypass").toString();
            const auto* maskStart = maskText.toRawUTF8();
            const auto* maskEnd = maskStart + maskText.getNumBytesAsUTF8();
            unsigned long long parsedMask = 0;
            const auto maskParsed = std::from_chars(maskStart, maskEnd, parsedMask);
            if (maskText.isEmpty() || maskParsed.ec != std::errc() || maskParsed.ptr != maskEnd
                || parsedMask > 0xfffffu)
                return false;
            restoredMask = static_cast<std::uint32_t>(parsedMask);
        }
        const auto number = state.getProperty("selectedUtcMs").toString();
        const auto* start = number.toRawUTF8();
        const auto* end = start + number.getNumBytesAsUTF8();
        const auto parsed = std::from_chars(start, end, restoredDate);
        if (number.isEmpty() || parsed.ec != std::errc() || parsed.ptr != end
            || restoredDate < entropy::timeline::earliest || restoredDate > entropy::timeline::latest)
            return false;
    }
    if (version == "3")
    {
        juce::StringArray tokens;
        tokens.addTokens(state.getProperty("moduleIntensity").toString(), " ", "");
        if (tokens.size() != 20)
            return false;
        for (int i = 0; i < 20; ++i)
        {
            const auto number = tokens[i].trim();
            const auto* start = number.toRawUTF8();
            const auto* end = start + number.getNumBytesAsUTF8();
            float value = 0.0f;
            const auto parsed = std::from_chars(start, end, value);
            if (number.isEmpty() || parsed.ec != std::errc() || parsed.ptr != end
                || !std::isfinite(value) || value < 0.0f || value > 1.0f)
                return false;
            restoredIntensity[static_cast<size_t>(i)] = value;
        }
    }
    const auto values = state.getChildWithName("Parameters");
    if (!values.isValid())
        return false;
    int parameterTrees = 0;
    for (const auto& child : state)
        if (child.hasType("Parameters")) ++parameterTrees;
    if (parameterTrees != 1)
        return false;
    juce::ValueTree noiseChild;
    for (const auto& child : values)
        if (child.getProperty("id").toString() == "noise")
            noiseChild = child;
    if (values.getNumChildren() != static_cast<int>(raw.size()) + (noiseChild.isValid() ? 1 : 0))
        return false;
    float restoredNoise = 0.6f;
    if (noiseChild.isValid())
    {
        if (!noiseChild.hasType("PARAM"))
            return false;
        const auto number = noiseChild.getProperty("value").toString().trim();
        const auto* start = number.toRawUTF8();
        const auto* end = start + number.getNumBytesAsUTF8();
        float value = 0.0f;
        const auto parsed = std::from_chars(start, end, value);
        if (number.isEmpty() || parsed.ec != std::errc() || parsed.ptr != end
            || !std::isfinite(value) || value < 0.0f || value > 1.0f)
            return false;
        restoredNoise = value;
    }
    int restoredCarrier = 0;
    float restoredAmount = 0.0f;
    for (const auto* id : entropy::ids::all)
    {
        int matches = 0;
        for (const auto& child : values)
        {
            if (child.getProperty("id").toString() != id)
                continue;
            ++matches;
            const auto number = child.getProperty("value").toString().trim();
            const auto* start = number.toRawUTF8();
            const auto* end = start + number.getNumBytesAsUTF8();
            float value = 0.0f;
            const auto parsed = std::from_chars(start, end, value);
            const auto range = parameters.getParameter(id)->getNormalisableRange();
            if (number.isEmpty() || parsed.ec != std::errc() || parsed.ptr != end || !std::isfinite(value)
                || value < range.start || value > range.end)
                return false;
            if (!child.hasType("PARAM"))
                return false;
            if ((juce::String(id) == entropy::ids::carrier || juce::String(id) == entropy::ids::generations
                 || juce::String(id) == entropy::ids::bypass) && value != std::floor(value))
                return false;
            if (juce::String(id) == entropy::ids::carrier) restoredCarrier = static_cast<int>(value);
            if (juce::String(id) == entropy::ids::entropy) restoredAmount = value;
        }
        if (matches != 1)
            return false;
    }
    if (version == "1")
        restoredDate = entropy::timeline::dateAt(restoredAmount, wallClock.now(), restoredCarrier);
    if (version == "1" || version == "2")
        for (int carrier : { 0, 1, 3 })
            restoredIntensity[static_cast<size_t>(carrier * 5 + 4)] = restoredNoise;
    juce::ValueTree cleaned = values;
    if (noiseChild.isValid())
    {
        cleaned = values.createCopy();
        cleaned.removeChild(cleaned.getChildWithProperty("id", "noise"), nullptr);
    }
    const DateWriteScope scope(this);
    parameters.replaceState(cleaned);
    selectedDate.store(restoredDate);
    moduleBypass.store(restoredMask, std::memory_order_relaxed);
    for (size_t i = 0; i < moduleIntensity.size(); ++i)
        moduleIntensity[i].store(restoredIntensity[i], std::memory_order_relaxed);
    editorWidth.store(juce::jlimit(720, 1920, static_cast<int>(state.getProperty("editorWidth", 960))));
    return true;
}

void EntropyAudioProcessor::setStateInformation(const void* data, int size) { restoreState(data, size); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EntropyAudioProcessor(); }
