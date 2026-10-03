#pragma once
#include <cmath>

namespace entropy::thermo
{
// 界面公式层的艺术标定常数：
// - T = 298.15 K 是真实物理锚点（室温存放），不是时间轴；
// - ΔH / Ea 是设计取值（kJ/mol 量级），仅用于让公式数值随拖动产生有意义的响应，
//   不是对真实介质的热化学测量；
// - ΔS 上限取 2ΔH/T，因此 ΔG = ΔH − T·ΔS 恰好在熵 0.5（时间轴中点）穿过零，
//   过零点标注 SPONTANEOUS；
// - t½ 直接取该载体的时间尺度（与 timeline::spanMs 一致），用于展示 Arrhenius 速率方程。
inline constexpr double ambientKelvin = 298.15;
inline constexpr double gasConstantJ = 8.314462618;

struct System
{
    double deltaH = 0.0;            // kJ/mol，载体“焓”
    double deltaSMax = 0.0;         // kJ/(mol·K)，熵 1 时的 ΔS 上限
    double activationEnergy = 0.0;  // kJ/mol，Arrhenius 活化能
    double halfLifeSeconds = 0.0;   // 半衰期（秒），等于载体时间尺度
    double rateConstant = 0.0;      // k = ln2 / t½，s⁻¹
};

inline System systemFor(int carrier) noexcept
{
    constexpr double yearSeconds = 31556952.0;
    const auto clamped = carrier < 0 ? 0 : (carrier > 3 ? 3 : carrier);
    constexpr std::array<double, 4> spans { 60 * yearSeconds, 100 * yearSeconds, 86400.0, 40 * yearSeconds };
    constexpr std::array<double, 4> enthalpies { 36.0, 55.0, 12.0, 28.0 };
    constexpr std::array<double, 4> barriers { 50.0, 55.0, 30.0, 48.0 };
    System system;
    system.deltaH = enthalpies[static_cast<size_t>(clamped)];
    system.deltaSMax = 2.0 * system.deltaH / ambientKelvin;
    system.activationEnergy = barriers[static_cast<size_t>(clamped)];
    system.halfLifeSeconds = spans[static_cast<size_t>(clamped)];
    system.rateConstant = std::log(2.0) / system.halfLifeSeconds;
    return system;
}
}
