#include "Timeline.h"
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace entropy::timeline
{
namespace
{
using Calendar = std::array<int, 6>;
#if JUCE_WINDOWS
constexpr juce::int64 epochOffset = 116444736000000000LL;

bool localCalendar(juce::int64 ms, Calendar& fields)
{
    ULARGE_INTEGER ticks {};
    ticks.QuadPart = static_cast<ULONGLONG>(ms * 10000 + epochOffset);
    const FILETIME file { ticks.LowPart, ticks.HighPart };
    SYSTEMTIME utc {}, local {};
    if (!FileTimeToSystemTime(&file, &utc) || !SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local))
        return false;
    fields = { local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond };
    return true;
}

bool utcFromLocal(const Calendar& fields, juce::int64& ms)
{
    SYSTEMTIME local {}, utc {};
    local.wYear = static_cast<WORD>(fields[0]);
    local.wMonth = static_cast<WORD>(fields[1]);
    local.wDay = static_cast<WORD>(fields[2]);
    local.wHour = static_cast<WORD>(fields[3]);
    local.wMinute = static_cast<WORD>(fields[4]);
    local.wSecond = static_cast<WORD>(fields[5]);
    FILETIME file {};
    if (!TzSpecificLocalTimeToSystemTimeEx(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &file))
        return false;
    ULARGE_INTEGER ticks {};
    ticks.LowPart = file.dwLowDateTime;
    ticks.HighPart = file.dwHighDateTime;
    ms = (static_cast<juce::int64>(ticks.QuadPart) - epochOffset) / 10000;
    return true;
}
#else
bool localCalendar(juce::int64 ms, Calendar& fields)
{
    const juce::Time time(ms);
    fields = { time.getYear(), time.getMonth() + 1, time.getDayOfMonth(), time.getHours(), time.getMinutes(), time.getSeconds() };
    return true;
}

bool utcFromLocal(const Calendar& fields, juce::int64& ms)
{
    ms = juce::Time(fields[0], fields[1] - 1, fields[2], fields[3], fields[4], fields[5], 0, true).toMilliseconds();
    return true;
}
#endif
constexpr std::array<const char*, 12> months { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
juce::String pad(int value, int length = 2) { return juce::String(value).paddedLeft('0', length); }
}

juce::String localDate(juce::int64 milliseconds)
{
    Calendar c {};
    if (!localCalendar(milliseconds, c)) return "Invalid date";
    return pad(c[2]) + " " + months[static_cast<size_t>(c[1] - 1)] + " " + pad(c[0], 4)
         + " " + pad(c[3]) + ":" + pad(c[4]) + ":" + pad(c[5]);
}

juce::String tickLabel(juce::int64 milliseconds, bool includeTime)
{
    Calendar c {};
    if (!localCalendar(milliseconds, c)) return "--";
    return includeTime ? pad(c[1]) + "/" + pad(c[2]) + " " + pad(c[3]) + ":" + pad(c[4])
                       : pad(c[0], 4) + "/" + pad(c[1]);
}

bool parseLocalDate(const juce::String& text, juce::int64& result)
{
    auto normalised = text.trim();
    for (const auto* suffix : { "年", "月", "日", "时", "分", "秒" })
        normalised = normalised.replace(juce::String::fromUTF8(suffix), " ");
    normalised = normalised.replaceCharacters("-:", "  ");
    const auto fields = juce::StringArray::fromTokens(normalised, " ", "");
    juce::StringArray parts;
    for (const auto& field : fields)
        if (field.isNotEmpty()) parts.add(field);
    if (parts.size() != 6) return false;
    if (parts[1].containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"))
    {
        int month = -1;
        for (int i = 0; i < 12; ++i)
            if (parts[1].equalsIgnoreCase(months[static_cast<size_t>(i)])) month = i + 1;
        if (month < 1) return false;
        const auto dayText = parts[0];
        parts.set(0, parts[2]);
        parts.set(1, pad(month));
        parts.set(2, dayText);
    }
    if (parts[0].length() != 4) return false;
    Calendar values {};
    for (int i = 0; i < 6; ++i)
    {
        if (!parts[i].containsOnly("0123456789") || (i > 0 && parts[i].length() != 2)) return false;
        values[static_cast<size_t>(i)] = parts[i].getIntValue();
    }
    const auto [y, m, d, h, minute, s] = values;
    if (y < 1900 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31 || h > 23 || minute > 59 || s > 59)
        return false;
    constexpr std::array<int, 12> monthDays { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    if (d > monthDays[static_cast<size_t>(m - 1)] + (m == 2 && leap ? 1 : 0)) return false;
    juce::int64 milliseconds = 0;
    Calendar roundTrip {};
    if (!utcFromLocal(values, milliseconds) || milliseconds < earliest || milliseconds > latest
        || !localCalendar(milliseconds, roundTrip) || roundTrip != values)
        return false;
    result = milliseconds;
    return true;
}
}
