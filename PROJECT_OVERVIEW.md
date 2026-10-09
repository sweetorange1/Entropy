# Entropy 项目全景简介（参数与开发导航）

> 面向作者、AI 助手和维护者：集中说明“现在做到了什么、参数叫什么、声音如何产生、下一步改哪里”。
> 本文以当前源码为准，不把灵感文档里的目标功能当作已实现功能。
> 对应版本：**1.0.4**；更新日期：**2026-10-04**；工程：`I:\理科系列\Entropy`。
> 本版核心变化：悬停提示全面修订——原 7 处长句提示简化为短句；补齐右侧 5 条退化描述符（按载体动态变化）、顶栏预设簇、箭头、BYPASS、RESET、网址等此前缺失的提示。完整对照见 §6.6。
> 上版核心变化（1.0.0）：物理化学公式层上线——时间轴标注 σ = dS/dt ≥ 0；右上角融合 Gibbs–Helmholtz 签名公式与随拖动实时计算的 ΔG = ΔH − T·ΔS（T = 298 K 室温锚点，ΔG 在熵 0.5 过零并显示 SPONTANEOUS）；模型视图标题 S = k ln W 与微观状态计数 W ≈ 10^23·S；卡片底部 Arrhenius 速率方程与每载体 Ea、t½（等于时间尺度）；SURFACE 行标注 dS ≥ δQ/T。常数见 `Source/Thermodynamics.h`（艺术标定，t½ 与 ΔG 过零点自洽）。
> 后续提出需求时，推荐使用“**载体 + 参数字段 + 想要的听感/数值变化**”，例如“让 TAPE 的 `wowMs` 在熵 0.4 后才开始增加”。

## 目录

- [1. 项目定位与当前边界](#1-项目定位与当前边界)
- [2. 用户参数速查：界面、宿主、代码名称对照](#2-用户参数速查界面宿主代码名称对照)
- [3. 内部退化描述符 Recipe 字典](#3-内部退化描述符-recipe-字典)
- [4. 四载体当前映射曲线](#4-四载体当前映射曲线)
- [5. DSP 链路、算法与时间常数](#5-dsp-链路算法与时间常数)
- [6. 界面布局、交互与描述符显示](#6-界面布局交互与描述符显示)
- [7. 工厂预设与状态持久化](#7-工厂预设与状态持久化)
- [8. 文件结构与关键接口](#8-文件结构与关键接口)
- [9. 构建、产物与验证](#9-构建产物与验证)
- [10. 与灵感文档的差异及待实现功能](#10-与灵感文档的差异及待实现功能)
- [11. 下一轮需求如何点名与定位](#11-下一轮需求如何点名与定位)

## 1. 项目定位与当前边界

### 1.1 产品概念

**Entropy** 是理科系列第二款作品，一款以“载体随时间失序”为隐喻的音频退化效果器。主交互是“选择载体 → 沿时间轴选择日期 → 日期与当前时钟的距离映射到退化 DSP”。时间越古老，混乱程度越高；0–1 Entropy 保留为内部 DSP 量与兼容宿主自动化接口。

- `Organic Chemistry` 提供系列视觉和交互参考；`CRTLoss`、`Pupon` 提供效果器工程设计参考。
- 当前 Entropy 是重新组织的独立工程，未直接搬入这些产品的音频引擎、更新服务或遥测。
- 灵感来源：`C:/Users/echotcxu/WorkBuddy/2026-09-29-11-15-16/理科系列02-物理化学-熵-DSP声音设计.md`。
- 当前为**四载体可试听基础版**，不是完成全部灵感机制的发布终版。
- Entropy 是归一化创作控制量，不是以 J/K 表示的热力学熵；界面不计算真实 Shannon 熵，也不模拟材料的完整物理过程。
- 用户已确认采用约 **10 ms 固定报告延迟**，优先保证干湿与旁路的基准时序一致。

### 1.2 产品身份与技术栈

| 项目 | 当前值 |
| --- | --- |
| 产品名 / CMake 主目标 | `ChemE-Entropy`（自 1.0.1 起加系列前缀；CMake target 仍为 `Entropy`，产物目录 `Entropy_artefacts` 不变） |
| 版本 | `1.0.4`，来自 `CMakeLists.txt` 的 `project()` |
| 厂商 | `iisaacbeats.cn` |
| Manufacturer Code / Plugin Code | `Isac` / `CE02`（旧为 `Entr`；更换后 VST3 UID 不同，新旧插件在宿主中共存，不覆盖旧安装） |
| Bundle ID | `cn.iisaacbeats.ChemEEntropy`（旧为 `cn.iisaacbeats.Entropy`） |
| 分类 | 效果器，VST3 `Fx / Distortion`；AU `kAudioUnitType_Effect` |
| 格式 | Windows：VST3、Standalone；Apple 配置额外包含 AU |
| 总线 | mono → mono 或 stereo → stereo；默认立体声，无侧链 |
| MIDI | 不接收、不输出，不是 MIDI 效果器 |
| 语言 / 框架 | C++17 / JUCE 8.0.12 |
| 构建 | CMake ≥ 3.22；Windows 使用 Ninja + MSVC x64 |
| JUCE 模块 | `juce_audio_utils`、`juce_dsp`；自研 DSP 核心本身不依赖 JUCE |
| UI | `juce::Graphics` 自绘；无外部字体/图片资源依赖 |

**身份和参数 ID 应保持稳定**，尤其在分发给用户、保存宿主工程之后；不要为了显示名称更好看而顺手改插件身份或参数 ID。

### 1.3 载体命名对照

| 界面名 | 宿主选项 | `Controls::carrier` | `Carrier` 枚举 | 已实现重点 |
| --- | --- | ---: | --- | --- |
| TAPE | Tape | 0 | `Carrier::tape` | 低通、wow/flutter、声道延迟漂移、饱和、hiss、dropout、弱后回声 |
| VINYL | Vinyl | 1 | `Carrier::vinyl` | 低通磨损、偏心 wow、失真、随机 click/crackle |
| STREAM | Streaming | 2 | `Carrier::streaming` | 码率档位映射、代际损失、声场收窄、前向抽头 smear、残留调制、丢包式陷落 |
| PHASE | Optical / Phase | 3 | `Carrier::optical` | 随机时钟抖动、短保持/插值错误、陷落、噪声、误纠鸟鸣（不再静音） |

PHASE 是光盘载体的界面名称，**不是第五种载体**，也还不是可操作的退火/淬火系统。

## 2. 用户参数速查：界面、宿主、代码名称对照

### 2.1 已注册的 7 个 APVTS 参数

定义入口：`Source/Parameters.cpp` → `entropy::createParameterLayout()`；常量入口：`Source/Parameters.h` → `entropy::ids`；音频传值结构：`Source/Dsp/Recipes.h` → `Controls`。

下表的范围是**参数原始值**，不是宿主通用的 0–1 归一化坐标。

| 界面显示/入口 | 宿主参数名 | 稳定 ID | `Controls` 字段 | 类型、范围、步长 | 默认值 | 作用与处理位置 |
| --- | --- | --- | --- | --- | --- | --- |
| TAPE / VINYL / STREAM / PHASE | Carrier | `carrier` | `carrier` | Choice：0–3，整数 | 0 / Tape | 所有载体；控制四路输出混合权重 |
| CARRIER TIMELINE / 日期 | Entropy（兼容自动化） | `entropy` | `entropy`（有效值） | 宿主仍为 Float 0–1、步长 0.0001；日期独立存为 int64 | 新实例日期为当前时刻 | UI 选日期；有效值由实时时间差计算，不直接使用 APVTS 静态值 |
| 各行效果条 | （不注册） | — | `Controls::intensity[20]` | 内部 Float：0–1，每载体 5 行 | SURFACE 行 0.6，其余 1.0 | 缩放该行退化；不是宿主参数，随状态保存，拖动/快照管理 |
| MIX | Mix | `mix` | `mix` | Float：0–1，步长 0.001 | 1 / 100% | 延迟对齐后的线性干湿混合 |
| INPUT | Input | `input` | `inputDb` | Float：−24 至 +12 dB，步长 0.1 dB | 0 dB | 湿路输入及干路输入增益；不是仅失真 drive |
| OUTPUT | Output | `output` | `outputDb` | Float：−24 至 +12 dB，步长 0.1 dB | 0 dB | 干湿混合之后的增益，旁路时绕过 |
| STORE / GEN 01 … 08 | Transcode generations | `generations` | `generations` | Int：1–8，步长 1 | 1 | STREAM 为转码代数；TAPE/VINYL/PHASE 为储存环境（1=理想储存，8=极端恶劣），放大该载体的相应退化 |
| BYPASS | Bypass | `bypass` | `bypass` | Bool：false / true | false | 平滑切到延迟后的原始输入；也提供给宿主标准旁路接口 |

- 插件主界面不再显示 Entropy 的数字或百分比；日期格式统一为英文 `DD Mon YYYY HH:MM:SS`，例如 `01 Oct 2026 12:34:56`，使用本地时区和固定英文月份。支持该格式直接编辑，也兼容 ISO 和旧中文输入。宿主仍保留原 Entropy 百分比；`mix` 仍显示百分比。日期手输精确到秒，不受宿主 Entropy 的 0.0001 步长限制。
- 用户参数均进入状态保存并支持宿主自动化；`generations` 在其他载体隐藏，但参数仍存在并保存。
- `Controls::generations` 是整数；引擎里的 `generations` 是独立 `Ramp`，允许切换时平滑经过小数值。
- 旋钮由 `SliderAttachment` 绑定；旁路由 `ButtonAttachment` 绑定；载体、代数和工厂预设通过 `setParameter()` 通知宿主并包围 change gesture。

### 2.2 容易混淆的同名与端点

| 名称/操作 | 准确含义 |
| --- | --- |
| `Controls::intensity[i]` | 第 i 行强度（`i = carrier × 5 + row`），0–1 缩放该行配方；SURFACE 行默认 0.6，其余 1.0；仅磁带/黑胶/光盘有 SURFACE 行（流媒体第 5 行是 SMEAR，默认 1.0） |
| `Recipe::noise` | 配方产生的基础噪声幅度（已乘该行强度），不是用户参数，通常远小于 1 |
| `Recipe::eventLevel` | 黑胶脉冲幅度，或光盘短保持的插值权重；含义依载体不同 |
| `Recipe::saturation` | 内部非线性强度；和用户 INPUT dB 不是同一个参数 |
| `Recipe::bitrate` | 模型档位标签，不是独立宿主参数或真实音频码率 |
| `Recipe::width` | 全频段 M/S 的 Side 乘数；不是界面宽度 `editorWidth` |
| Entropy = 0 | 退化贡献归零；在增益为 0 dB 时接近延迟后的透明直通，不代表没有延迟 |
| MIX = 0 | 仅干路，但 INPUT、OUTPUT 增益仍生效；不等同 BYPASS |
| SURFACE 行强度 = 0 | 去掉 hiss/dust 底噪；黑胶 click 爆音仍发声（属 DISCONTINUITIES 行）；其他行退化不受影响 |
| BYPASS = true | 淡出全部处理及增益，保留约 10 ms 延迟；内部四路仍继续运行 |
| PHASE + Entropy = 1 | 湿路充满插值咔哒、丢读陷落与误纠鸟鸣，**不会静音**（静音反而是信息被抹平、熵更低的状态）；若 MIX < 100% 或正在旁路，仍以干路为主 |

精确透传的讨论限于正常有限输入：引擎还会将非有限输入替换为零，将异常大输入限制在 ±32；并非对任意浮点比特模式的无条件 bit-perfect 透传。

### 2.3 绝对日期、载体时间尺度与实时老化

时间模型入口：`Source/Timeline.h` / `Timeline.cpp`；交互入口：`Source/UI/TimelineControl.h`；日期真值：`EntropyAudioProcessor::selectedDate`。

| 载体 | 从最旧端到当前的跨度 | 设计语义 |
| --- | --- | --- |
| TAPE | 60 年 | 磁带数十年退磁、材料老化与运输系统退化的艺术刻度 |
| VINYL | 100 年 | 更长的保存/反复播放磨损历史；不是只靠静置就必然磨损 |
| STREAM | 24 小时 | 将持续传输/转码/网络损伤过程压缩为时间轴；不是数字文件自然老化 |
| PHASE | 40 年 | 光学介质氧化/染料退化、读错增多的艺术刻度；不区分各种盘片实际寿命 |

以上是**可调整的声音设计校准，不是实测介质寿命**，也不保证“过完此期限必然损坏”。常量 `timeline::year` 使用 365.2425 天，跨度是固定秒数而非逐年扣减日历年份；修改跨度见 `spanMs()` / `spanLabel()`，并同步 `mappingVersion` 的兼容策略。

设选定 UTC 时刻为 \(T\)，本机当前 UTC 时刻为 \(N\)，当前载体跨度为 \(D_c\)：

\[
e_{effective}=\operatorname{clamp}((N-T)/D_c,0,1),\qquad x_{axis}=1-e_{effective}
\]

- 选择日期后保持 \(T\) 不变；刷新 UI、关闭编辑器、保存/重开工程均不把它变为“永远距今多少年”。系统时间推进时，有效 Entropy 增加直至 1；**已经拖到最左端后，再过去一年仍为 1，不会继续增加配方强度**。随机噪声/事件的持续变化不等于继续增强；MIX、行强度、增益和代数也不会随时间自动增加。
- 切换载体保持时间轴比例（有效 Entropy）不变，按新跨度重新锚定日期；时间只是包装——例如在 TAPE 拉到最左，切到 VINYL 仍停最左，日期随之改到 100 年前。
- 比最旧端还古老的日期保留原值，滑块贴左端并提示 BEYOND SCALE；系统时钟回拨导致日期暂时在未来时，保持该日期但有效值钳为 0；用户手输未来日期会被拒绝。
- `timeline::Clock` 在专用线程每约 100 ms 缓存本机时钟；音频线程只读原子值，不做日历转换、系统时间查询、文件操作或 UI 工作。没有 Editor 也能老化。
- `localDate()` 用本机时区显示，状态保存 UTC 毫秒。Windows 使用原生 FILETIME 时区转换，支持 1970 年以前；本地夏令时不存在的日期时间由往返校验拒绝，重复小时按系统转换选择。
- UI 的 NOW 时钟和标尺随时间更新；上方、时间轴右上方的选定日期保持固定。格式精确到秒，内部保存毫秒。
- 宿主显式写入 `entropy` 时按当时载体跨度重新推算日期；直接参数 Listener 接收包括重复值在内的通知。日期写入和恢复用 `DateWriteScope` 标记自身同步通知，避免再次反算丢失日期精度。
- 自然老化不回写宿主参数、不制造自动化事件。因此 APVTS 的名义 Entropy 可以不同于 `currentControls().entropy` 的实时有效值。重复播放/离线渲染也使用墙上时钟，不保证不同现实时间的导出逐样本相同。
- 工厂预设继续以内部 Entropy 深度定义，加载时创建相应的新日期；预设名中的年份不直接用作选定年份。文件快照则恢复保存的固定日期。

## 3. 内部退化描述符 Recipe 字典

`Recipe` 是 `makeRecipe()` 的输出。下面 **19 个字段均未独立注册为宿主参数**；它们由载体、Entropy 和代数计算，适合直接点名调整曲线。

“初始值”指 `Recipe` 结构默认值；不等于每个载体在所有熵位置的取值。未被某载体覆盖的字段保留初始值。

| 字段 | 初始值 | 单位/语义 | 当前使用载体与 DSP 作用 |
| --- | ---: | --- | --- |
| `cutoffHz` | 20000 | Hz，目标低通截止 | 四载体；实际截止会平滑并限制到采样率的 0.45 倍 |
| `filterAmount` | 0 | 0–1，原信号与低通输出的混合系数 | 四载体；未到 1 时仍有未滤波分量 |
| `wowMs` | 0 | ms，慢速延迟调制深度 | TAPE、VINYL；不是音分或百分比音高深度 |
| `flutterMs` | 0 | ms，快速/随机延迟调制深度 | TAPE 用正弦；PHASE 用逐声道随机值 |
| `driftMs` | 0 | ms，左右反向附加延迟深度 | TAPE；与慢速 wow 共用调制信号 |
| `saturation` | 0 | 无量纲非线性强度 | TAPE、VINYL；影响 drive、混合深度；TAPE 还影响偏置 |
| `noise` | 0 | 加性噪声的线性幅度 | TAPE、VINYL、PHASE；已乘 SURFACE 行强度，再乘输入包络门 |
| `eventRate` | 0 | 每秒目标触发率 | VINYL 脉冲、PHASE 短保持；按每采样概率判定 |
| `eventLevel` | 0 | 脉冲幅度/插值权重 | VINYL 控制 clicks 大小；PHASE 控制当前样本向历史值靠拢的程度 |
| `gapRate` | 0 | 每秒目标陷落触发率 | TAPE、STREAM、PHASE；已有陷落未结束时不重复触发 |
| `gapDepth` | 0 | 0–1，陷落深度 | 陷落期间目标增益为 `1 - gapDepth`，有平滑边沿 |
| `gapMs` | 0 | ms，陷落基础长度 | 实际随机时长约为该值的 0.45–1.45 倍 |
| `width` | 1 | Side 乘数 | TAPE、STREAM 收窄；1 保持原宽度，0 合成单声道 |
| `smear` | 0 | 混合权重 | STREAM，将正常路径混向滤波信号与提前抽头组成的 smear 信号 |
| `echo` | 0 | 线性幅度 | TAPE，相对主延迟再晚 1.5 s 的输入抽头；不是反馈混响 |
| `residue` | 0 | 残留调制强度 | STREAM，高频差值乘以调制信号与短延迟信号的组合 |
| `birdieRate` | 0 | 每秒目标触发率 | PHASE，误纠鸟鸣（短促扫频哨音）的事件率，替代旧的整体静音 |
| `bitrate` | 320 | kb/s，派生档位标签 | STREAM 显示用；真正滤波由同档位的 `cutoffHz` 决定 |
| `correctionStage` | 0 | 0–3，派生阶段标签 | PHASE 显示用；不直接切换纠错算法，事件仍由各自连续曲线驱动 |

## 4. 四载体当前映射曲线

主入口：`Source/Dsp/Recipes.cpp` → `makeRecipe()`。修改本节曲线会同时影响 DSP 配方和右侧目标描述符显示；调制频率、事件算法、滤波结构则不在这里。

### 4.1 通用符号

设 \(e\) 为限制到 0–1 的 Entropy；\(g\) 为 1–8 的代数（音频内部为平滑值）；\(\ell=(g-1)/7\)。STREAM 用 \(\ell\) 作代际损失；TAPE/VINYL/PHASE 用同一公式的 \(s=(g-1)/7\) 作“储存环境恶劣程度”（1 = 理想储存，8 = 极端恶劣），仅放大已有退化（随 \(e\) 起效，不改变零熵端的无损语义）。以下表格以 \(O_a\) 代表 `onset(e, a)`：

\[
x=\operatorname{clamp}\left(\frac{e-a}{1-a},0,1\right),\qquad O_a=x^2(3-2x)
\]

即：Entropy 不超过阈值 \(a\) 时为 0，之后平滑增大，到 Entropy = 1 时为 1。表格右列为 **Entropy = 1 的配方目标**，不是测量值，也未乘行强度/MIX 等用户参数。

**熵增的两条退化原则**（时间轴语义的依据，改曲线时须遵守）：

1. **事件类退化按频率增长**：灰尘爆音、dropout、读错、丢包等离散事件，随时间/熵增加的是**发生频率**（`eventRate` / `gapRate`），单次事件的强度（`eventLevel` / `gapDepth`）与物理上颗粒大小、磁粉脱落面积有关，基本恒定或只缓慢上升，不能随时间越变越响。
2. **连续类退化按深度增长**：高频损失、本底噪声、音高漂移、饱和、声场收窄等持续过程，随时间/熵增加的是**程度**（`cutoffHz` / `noise` / `wowMs` / `saturation` / `width`），单调加深直至封顶。

### 4.2 TAPE：连续退化

| 字段 | 当前公式 | Entropy = 1 |
| --- | --- | ---: |
| `cutoffHz` | \(20000\times0.18^e\) | 3600 Hz |
| `filterAmount` | \(O_0\) | 1 |
| `wowMs` | \(2.1O_{0.22}\) | 2.1 ms |
| `flutterMs` | \(0.10O_{0.43}\) | 0.10 ms |
| `driftMs` | \(0.30O_{0.38}\) | 0.30 ms |
| `saturation` | \(2.8O_{0.12}\) | 2.8 |
| `noise` | \(0.032e^2\) | 0.032 |
| `gapRate` | \(5O_{0.27}\) | 5 /s |
| `gapDepth` | \(0.72O_{0.25}\) | 0.72，目标剩余增益 0.28 |
| `gapMs` | \(8+30e\) | 38 ms |
| `width` | \(1-0.62O_{0.4}\) | 0.38 |
| `echo` | \(0.0178O_{0.35}\) | 0.0178，约 −35 dB |

注意：磁带 dropout 使用 `gapRate`，不是 `eventRate`；本模式没有附加点击脉冲。当前截止终点是 3.6 kHz，不是灵感卡片里的 6 kHz。储存环境 \(s\) 使 `cutoffHz` 指数项加 \(0.5s\)、wow/flutter/drift 乘 \(1+0.4s\)、`noise` 乘 \(1+1.5s\)、`gapRate` 乘 \(1+1.2s\)。

### 4.3 VINYL：事件型退化

| 字段 | 当前公式 | Entropy = 1 |
| --- | --- | ---: |
| `cutoffHz` | \(20000\times0.23^e\) | 4600 Hz |
| `filterAmount` | \(O_{0.10}\) | 1 |
| `wowMs` | \(2.5O_{0.45}\) | 2.5 ms |
| `saturation` | \(3.6O_{0.28}\) | 3.6 |
| `noise` | \(0.020O_{0.30}\) | 0.020 |
| `eventRate` | \(0.6e+70e^2\) | 70.6 /s |
| `eventLevel` | \(0.10+0.08e\) | 0.18 |

- click 和 crackle 目前由同一个短脉冲事件模型产生；**密度随熵快速上升（0.6e+70e²），幅度基本恒定（0.10–0.18）**，符合“灰尘变多、单颗粒响度不变”的物理直觉。爆音不随 SURFACE 行强度缩放（仅随输入尾音淡出），SURFACE 行只控制 hiss/dust 噪声床。没有独立 click/pop/crackle 三套参数。
- 这里没有 `gapRate`、独立 scratch、rumble、内圈位置或跳针实现；`width` 保持 1。储存环境 \(s\) 使 `wowMs` 乘 \(1+0.4s\)（翘曲）、`noise` 乘 \(1+1.5s\)（灰尘霉变）、`eventRate` 乘 \(1+1.2s\)（爆音频率）。

### 4.4 STREAM：码率阶梯与代际损失

档位索引为 \(\min(5,\lfloor6e\rfloor)\)。下表区间左闭右开，最后一档包含 1：

| Entropy 区间 | `bitrate` | 基础截止 \(C\) |
| --- | ---: | ---: |
| 0 至 1/6 | 320 kb/s | 20000 Hz |
| 1/6 至 2/6 | 256 kb/s | 19000 Hz |
| 2/6 至 3/6 | 192 kb/s | 17500 Hz |
| 3/6 至 4/6 | 128 kb/s | 16000 Hz |
| 4/6 至 5/6 | 96 kb/s | 13500 Hz |
| 5/6 至 1 | 64 kb/s | 11000 Hz |

UI 在 Entropy < 0.001 时将码率文字显示为 LOSSLESS；这时结构里的 `bitrate` 仍为 320。真正零退化端点是 Entropy = 0。

| 字段 | 当前公式 | Entropy = 1，代数 1 → 8 |
| --- | --- | --- |
| `cutoffHz` | \(C(1-0.72\ell e)\) | 11000 → 3080 Hz |
| `filterAmount` | \(O_0\) | 1 |
| `width` | \(1-O_{0.10}(0.80+0.20\ell)\) | 0.20 → 0 |
| `smear` | \(O_{0.28}(0.24+0.52\ell)\) | 0.24 → 0.76 |
| `residue` | \(O_{0.40}(0.20+0.48\ell)\) | 0.20 → 0.68 |
| `gapRate` | \((2+4\ell)O_{0.60}\) | 2 → 6 /s |
| `gapDepth` | \(O_{0.45}\) | 1 |
| `gapMs` | \(50+250e\) | 300 ms |

代际语义（v1.0.1 调整）：GEN 只放大已有退化（各项均乘 \(e\)），不改变零熵端的无损语义。连续类按深度增长（高频截止、声场塌缩、涂抹、残留的 ℓ 系数加大），事件类按频率增长（`gapRate` 随代数从 2/s 升至 6/s）；单次陷落的时长与深度仍只由熵决定，不随代数变深。

本模式没有加性本底噪声和独立事件脉冲；STREAM 的第 5 行是 TRANSIENT SMEAR（默认强度 1.0），不是 SURFACE 行。代数只改变上述配方系数，没有真的将信号重复转码或反复通过多代引擎。

### 4.5 PHASE：纠错失败的近似

| 字段 | 当前公式 | Entropy = 1 |
| --- | --- | ---: |
| `cutoffHz` | \(20000(1-0.68O_{0.4})\) | 6400 Hz |
| `filterAmount` | \(O_{0.35}\) | 1 |
| `flutterMs` | \(0.013O_{0.05}\) | 0.013 ms |
| `noise` | \(0.017O_{0.50}\) | 0.017（最终还会被失效因子静音） |
| `eventRate` | \(45O_{0.25}\) | 45 /s |
| `eventLevel` | \(O_{0.25}\) | 1 |
| `gapRate` | \(9O_{0.50}\) | 9 /s |
| `gapDepth` | \(O_{0.40}\) | 1 |
| `gapMs` | \(3+160O_{0.5}\) | 163 ms |
| `birdieRate` | \(7O_{0.30}\) | 7 /s |

`correctionStage` 标签：Entropy < 0.28 为 0 / CORRECTABLE；0.28–0.58 为 1 / INTERPOLATION；0.58–0.93 为 2 / BIRDIES；≥0.93 为 3 / ERROR STORM。

**标签阈值不等同事件激活阈值**：例如插值事件在 Entropy > 0.25 就可能发生；最高档也不再静音，而是鸟鸣与丢读事件密集交织的杂乱状态。这不是完整 CIRC 纠错状态机。储存环境 \(s\) 使 `noise` 乘 \(1+1.2s\)、`eventRate` 与 `birdieRate` 乘 \(1+1.2s\)、`gapRate` 乘 \(1+1.0s\)。

## 5. DSP 链路、算法与时间常数

### 5.1 真实信号流

入口为 `EntropyAudioProcessor::processBlock()` / `processBlockBypassed()` → `process()` → `EntropyEngine::setControls()` → `EntropyEngine::process()`。

1. 防御非法输入、写入两声道环形历史缓冲；计算输入峰值和纹理门控包络。
2. 推进 Entropy/代数平滑器；每 16 样本刷新四份配方和低通系数。
3. **四个载体 lane 每个采样都运行**，即使某路权重为 0；每路独占滤波、随机和事件状态。
4. 每个 `processLane()`：调制延迟读取并乘 INPUT → PHASE 短保持 → 低通/并联滤波混合 → 饱和/DC 阻断 → STREAM smear/residue 或 TAPE 后回声 → gap 增益 → 加性事件（PHASE 误纠鸟鸣、黑胶 click、噪声床）→ M/S 宽度。
5. 将四路输出按平滑权重相加得到湿声。切换载体是约 40 ms 交叉淡化，而不是重建/重置引擎。
6. 与固定延迟干声做线性 MIX，乘 OUTPUT；最后按约 10 ms 旁路权重切回延迟后的原始输入。
7. 输出非有限值保护、输出峰值采集。

设 \(d\) 为固定延迟的原始输入、\(w\) 为已含 INPUT 的载体混合湿声，\(G_i,G_o\) 为输入/输出线性增益，\(m\) 为 MIX，\(b\) 为旁路淡化值：

\[
y_p=((1-m)dG_i+mw)G_o,\qquad y=(1-b)y_p+bd
\]

这不是等功率混合。固定延迟对齐解决的是基准时序；音高调制、IIR 相位和前/后抽头仍会造成有意的相位/时间差，不能据此承诺任意干湿比例都完全同相。

### 5.2 算法细节与可修改常量

| 模块/代码名 | 当前实现与常量 | 修改入口 |
| --- | --- | --- |
| 固定延迟 `latency` | `round(sampleRate × 0.010)`；48 kHz 时 480 样本 | `EntropyEngine::prepare()`，Processor 上报 |
| 历史缓冲 `history` | 每声道 `ceil(sampleRate × 1.6) + 8` 个 float；仅 prepare 分配 | `prepare()`、`read()` |
| 普通平滑 `Ramp` | 约 40 ms；Entropy、MIX、增益、载体权重、代数、20 个行强度；增益在线性幅度域平滑 | `Ramp::set()`、`setControls()` |
| 旁路平滑 `bypass` | 普通平滑长度的 1/4，约 10 ms；宿主旁路与参数旁路取 OR | `process()` |
| 配方刷新 `controlClock` | 每 16 样本更新，不依赖宿主 block 切分 | `process()` |
| 截止平滑 `Lane::cutoff` | 约 10 ms 一阶平滑；实际截止上限 `0.45 × sampleRate` | `process()` |
| 低通 `coefficients` / `filter` | TAPE/VINYL/PHASE：二阶 Butterworth；STREAM：三节构成六阶 Butterworth，Q 约 0.517638、0.707107、1.931852 | `process()`、`processLane()` |
| 慢漂 `wow` | 0.83 Hz 与 1.37 Hz 正弦，权重 0.78 / 0.22；不是随机游走 | `process()` |
| 快抖 `flutter` | 27.3 Hz 正弦；PHASE 不用该正弦做 jitter，而用随机值 | `process()`、`processLane()` |
| 偏心 `warp` | 基频 5/9 Hz（33⅓ rpm）及二次谐波，权重 0.82 / 0.18 | `process()` |
| 声道漂移 `azimuth` | L/R 对 `driftMs × wow` 取相反符号；没有独立全通网络或随机声道增益 | `processLane()` |
| 延迟插值 | 环形缓冲相邻两个采样线性插值；没有高阶插值 | `read()` |
| 失真 `drive` / `bias` | `drive = 1 + saturation`；TAPE 偏置 `0.07 × saturation`，VINYL 偏置为 0；tanh 输出除以 `sqrt(drive)` | `processLane()` |
| DC 阻断 | 非线性之后，极点对应约 12 Hz；再按 `min(1, saturation)` 混回 | `prepare()`、`processLane()` |
| STREAM `pre` | 相对固定延迟提前 2、3.5、5 ms 的三个原始输入抽头平均；与 65% 滤波信号、35% 提前抽头组合后按 `smear` 混合 | `processLane()` |
| STREAM `comb` / `residue` | 短抽头延迟为基准后 `1.7 + 0.3 × wow` ms；高频差值乘以 `0.5 × flutter + 0.15 × comb`，再乘 `residue` | `processLane()` |
| TAPE 后回声 | 原始输入抽头位于基准后 1.5 s，乘 INPUT 与 `echo`；无反馈 | `processLane()` |
| gap 边沿 `gapSlew` | 约 1.5 ms 一阶平滑；结束后恢复到 1 | `prepare()`、`processLane()` |
| VINYL `impulse` | 随机正/负脉冲，幅度由 `eventLevel` 决定，每采样乘 `1 - 1200/sampleRate` 衰减，再受噪声调制 | `processLane()` |
| PHASE `holdRemaining` | 每次读错事件持续 2–6 个采样；当前值向上一处理值插值；不是跳轨 | `processLane()` |
| 噪声 `pink` / `texture` | TAPE 为 `0.68 × white + 2 × pink`；`pink` 是 `0.985/0.015` 单极低通噪声近似，不是严格 1/f 粉噪；其他载体用白噪 | `processLane()` |
| 纹理门 `envelope` / `gate` | 输入峰值快速跟随、120 ms 指数衰减；`gate = clamp((envelope - 0.000001) × 3000, 0, 1)` | `prepare()`、`process()` |
| 随机数 `Lane::uniform()` | 实例内 xorshift32；每个 lane 有固定种子，reset 恢复种子 | `reset()`、`Lane::uniform()` |

补充边界：

- 输入包络门只控制**加性纹理**，不是主信号噪声门；它读取 INPUT trim 之前的输入，所以调 INPUT 不等于同比例调噪声底。
- `gapRate/sampleRate` 是空闲时每采样触发概率，已有 gap 不重触发，因此实际每秒 gap 数可能小于目标值；`gapDepth = 1` 的平滑陷落不保证整个区间严格为零。
- VINYL 和 PHASE 的事件计时在同一 lane 内共享，两声道的部分随机纹理/抖动各自取值；不能将事件描述为完全独立的双声道泊松源。
- STREAM 低通支路为六阶，但 `filterAmount` 会保留原信号，提前抽头还会混入未低通信号；**整个效果不是严格砖墙滤波器**。
- 当前 smear 没有瞬态检测器；`comb` 是参与乘法的短抽头，不是具有反馈尾音的梳状谐振器。
- 不含过采样、抗混叠失真专用算法、响度补偿或 0 dBFS 输出限制器；高频/大输入可能产生混叠或超过 0 dBFS。
- `prepare()` 内将采样率限制到 8–384 kHz；不能外推宣称任意采样率都已支持或验证。
- 固定种子支持相同条件下重现；不同插件实例并未主动使用不同种子。快照不保存随机状态或延迟历史，加载快照也不重置它们。

## 6. 界面布局、交互与描述符显示

### 6.1 主界面与操作

入口：`Source/PluginEditor.cpp` → `EntropyAudioProcessorEditor`；控件样式：`Source/UI/ScientificLookAndFeel.h`。

| 区域/控件 | 当前交互/含义 |
| --- | --- |
| 顶部官网、版本 | 左上 `iisaacbeats.cn` 加浅灰动态版本号，字号 15 / 11.5、与 Organic Chemistry 相同；顶栏高 52，文字按 (52−15)/2=17 顶距垂直居中；网址悬停变灰/下划线/手形，左键打开 `https://iisaacbeats.cn`；版本不可点击；无 SCIENCE SERIES / 02 与问号按钮 |
| 预设切换区 | 顶栏水平居中的纯文字簇 `< 名称 >`，无圆角边框；名称悬停/面板打开时浅蓝圆角底加下划线，箭头悬停变蓝；点击名称展开 `PresetPanel` 自绘白色细线卡片（4 列各 5 项，蓝色选中条与悬停底色，含 Save/Open snapshot）；与 Organic Chemistry 的分子预设簇交互一致 |
| 预设覆盖层 | 点击条目应用并关闭；点击非条目区域、Close 或 Esc 关闭且不穿透；方向键/Tab 导航、Enter/Space 选择；布局随窗口缩放，不再使用默认级联菜单 |
| `<` / `>` | 在 20 个工厂预设间循环；自定义状态以当前载体第一项为基准再向前/后一步；纯文字绘制，命中区随名称宽度变化 |
| BYPASS | 右上纯文字，悬停或激活时加下划线，激活用强调色；切换标准旁路参数并随宿主同步 |
| 标题带 | 顶栏下方左侧两行：眉题 `02 / PHYSICAL CHEMISTRY`（9.5px muted）+ 大标题 `Entropy`（27px ink），与 Transcription 同格式；载体按钮移至标题带下方（y 122-158），卡片顶边 116→164（高 322→274），卡片头部下移、粒子区行距压缩（24→14，圆阵纵比 0.58→0.30），卡片底部、纹理带、时间轴、旋钮与右侧描述符区不变 |
| 四载体按钮 | 切换 `carrier`，保持时间轴比例（有效 Entropy）不变，按新跨度重新锚定日期；切换带动约 0.3 s 过渡：强调色平滑插值，粒子阵型补间，图样带为两段式擦除（旧带先整体左移淡出，新带再从右侧进入，任一时刻只显示一个状态），时间轴/旋钮/描述符同步过渡 |
| MODEL VIEW | 75 点按载体排布的有序/失序动画：TAPE 5 条平行横线（磁迹）、VINYL 透视音槽弧带（低视角唱片的音槽：上方弧更长更平缓、下方弧更短更弯）、STREAM 5 条并行波浪线（6 周期、行间相位错开）、PHASE 5 个同心椭圆环；相邻点连线勾勒形态，载体切换时新旧阵型按 eased 补间；下方有磁带盒/唱片/数据流/光盘及对应纹理，不是音频分析 |
| CARRIER TIMELINE | 左旧右今；41 个标尺刻度、5 个主日期标签；拖动或方向键选择，Home/End 选最旧/现在，Shift+方向键精调；保留键盘焦点但不绘制圆角聚焦框。标题行与方向引导文字已按系列风格精简移除 |
| 热力学公式层 | 五处纯展示公式（斜体，`Source/Thermodynamics.h` 艺术标定）：① 时间轴右端 `σ = dS/dt ≥ 0`；② 右上签名 `∂(ΔG/T)/∂T = −ΔH/T²` 与活公式 `ΔG = ΔH − T·ΔS`（T = 298 K 室温常数，ΔS 来自时间轴，ΔG 在熵 0.5 过零，过零后左上显示 `SPONTANEOUS`）；③ 模型视图标题 `S = k ln W` 与 `W ≈ 10^(23·S)` 微观状态计数；④ 卡片底部 `k = A·e^(−Ea/RT)` 及每载体 `Ea`、`t½`（恰等于该载体时间尺度）；⑤ SURFACE 行 `dS ≥ δQ/T`（噪声床 = 热流）。公式字符串必须经 `juce::String::fromUTF8`，字体用含 ∂Δσδ≥≈²·½ 字形的默认 UI 斜体 |
| 日期显示 | 右上（`CARRIER DATE / LOCAL TIME` 下方）显示选定日期，NOW 行显示当前时钟，统一为英文 `DD Mon YYYY HH:MM:SS`；双击右上日期可直接按原显示格式精确编辑，兼容 ISO/旧中文输入，Enter 提交、Esc 取消 |
| 时间轴快捷输入 | 双击时间轴或按 Enter 弹出 0–100 数值框（0 = 现在/最右，100 = 最古老/混乱最高），输入数字即按时间尺度换算为日期；越界钳制到 0–100，非法文本取消；点击别处提交、Esc 取消 |
| 右侧数值区 | 标签、突出数值、几何图标和细进度线；图标表达带宽/波动/断裂/声场/噪声等意义；细线上有强度手柄，**按住拖动即可设置该行强度（0–100%）** |
| INPUT / MIX / OUTPUT | 旋钮样式与 Organic Chemistry 反应剖面（ADSR）控制器同款：5 段仪表刻度、强调色值弧、浅色主体与同色指针；纵向拖动，向上增大；双击打开自绘白色圆角数值框，dB 与百分比独立换算、框旁显示单位；回车/点击别处提交，Esc 取消；滚轮禁用 |
| STORE / GEN | 四载体均显示；点击 1→2→…→8→1；STREAM 显示 `GEN 01–08`（转码代数），其余显示 `环境名 x/8`（TAPE：VAULT / CLIMATE / CABINET / DRAWER / ATTIC / GARAGE / BASEMENT / MAGNET；VINYL：RACK / SLEEVE / PAPER / STACKED / ATTIC / GARAGE / DAMP / SUN；PHASE：CASE / CABINET / DESK / LOOSE / SILL / CAR / DAMP / SUN）；不通过载体切换清零 |
| IN / OUT | 输入 trim 前 / 最终输出后的采样峰值，合并声道；不是 RMS、LUFS 或 true peak |
| 右下角缩放 | 默认 960×640，固定 3:2；720×480 至 1920×1280，即 75%–200% |

UI 以 **30 Hz** 刷新。旋钮全量程拖动距离约 `160 × uiScale` 像素；使用 `NumericSlider::mouseDoubleClick()` 开启编辑。

配色：白底，正文 `#292D2E`，次级文字 `#858983`，线条 `#E5E7E0`，卡片 `#F7F8F4`；TAPE `#AF614C`、VINYL `#687B54`、STREAM `#2A6FB0`、PHASE `#807396`。

### 6.2 右侧描述符：显示名到字段

入口：`drawDescriptors()`。它从当前用户参数重新调用 `makeRecipe()`，再对显示副本调用 `applyModuleIntensity()` 乘各行强度，展示的是**强度缩放后的目标配方**，不是引擎平滑后的实时系数或音频测量。条形最终限制到 0–1；条上的竖手柄表示该行强度位置。

| 显示名 | 模式 | 文字来源 | 条形含义 |
| --- | --- | --- | --- |
| BANDWIDTH | 全部 | `cutoffHz / 1000` kHz（已乘强度） | `1 - cutoffHz / 20000` |
| TRANSPORT | 除 STREAM 外 | `wowMs + flutterMs` ms（已乘强度），不包含 `driftMs` | 上述和除以 2.6 |
| BITRATE MODEL | STREAM | `bitrate` kb/s；Entropy < 0.001 或强度 0 显示 LOSSLESS | `强度 × (1 - bitrate / 320)` |
| DISCONTINUITIES | 全部 | `eventRate + gapRate` /s（已乘强度） | 上述和除以 65；不是实际发生次数 |
| STEREO ORDER | 除 PHASE 外 | `width × 100` %（已乘强度） | `1 - width`；数字越低，退化条越长 |
| MISCORRECTION | PHASE | `correctionStage` 阶段文字（CORRECTABLE / INTERPOLATION / BIRDIES / ERROR STORM） | `强度 × correctionStage / 3` |
| SURFACE RESIDUE | TAPE/VINYL/PHASE | `noise / surfaceNoiseMax × 100` %（已乘强度） | 同上归一化值；不是测得的噪声电平 |
| TRANSIENT SMEAR | STREAM | `smear × 100` %（已乘强度） | `smear` |

右上阶段标签由 `degradationStage()` 给出：0–0.02 PRISTINE，0.02–0.25 TRACES，0.25–0.5 WEATHERED，0.5–0.75 UNSTABLE，0.75–0.94 DECAY，≥0.94 COLLAPSE；前述区间左闭右开。

这些文字不会因为 MIX=0、旁路或无输入而自动变成“无退化”；BANDWIDTH 也未按实际采样率做显示钳位。它们用于解释模型，不代表当前输出客观状态。

**图标语义**（`drawDescriptorIcon()`，共 9 种，旁路时变灰并盖白罩）：

| 图标 | 视觉 | 用于 |
| --- | --- | --- |
| bandwidth | 坐标轴 + 向左提前下弯的滚降曲线 | 所有 BANDWIDTH |
| transport | 虚线参考线 + 围绕它起伏的抖动 | TAPE/VINYL/PHASE 的 TRANSPORT |
| events | 平稳基线 + 突出单脉冲 | VINYL/PHASE 的 DISCONTINUITIES（click/crackle、插值咔哒） |
| dropout | 两段波形之间缺失的间隙 | TAPE/STREAM 的 DISCONTINUITIES（dropout、丢包） |
| stereo | 两声道圆点向中线塌缩 + 虚线中线 | 所有 STEREO ORDER |
| surface | 散布颗粒随退化变大变显 | 所有 SURFACE RESIDUE |
| bitrate | 从左到右逐级下降的阶梯 | STREAM BITRATE MODEL |
| smear | 本体脉冲 + 前方淡影 | STREAM TRANSIENT SMEAR |
| birdie | 三声频率快速上升的扫频哨音 | PHASE MISCORRECTION |

### 6.3 模块旁路（点击描述符行）

每行同时支持两种交互：**按住拖动强度条**（竖手柄或细线区域）设置该行强度 0–100%，0 表示该退化完全移除、100% 表示按时间轴满强度，中间值线性缩放；**单击（未拖动）切换旁路**，旁路后图标变灰、数值显示 `OFF`、进度条变灰，再点恢复。拖动与点击的区分在 `mouseUp()` 中按位移阈值（约 4 缩放像素）判定。

- 强度真源为 `EntropyAudioProcessor::moduleIntensity`（20 个无锁原子，索引 `carrier × 5 + row`），默认 SURFACE 行 0.6、其余 1.0；不是宿主参数，随状态保存，约 40 ms 平滑生效。配方刷新时 `EntropyEngine::process()` 先对每路配方调用 `Recipes.cpp::applyModuleIntensity()`（字段线性缩放；`width` 按 `1 + (width - 1) × 强度` 插值；`cutoffHz` 从 20 kHz 向配方值插值），再调用 `applyModuleBypass()` 归零旁路字段。
- 旁路掩码真源为 `EntropyAudioProcessor::moduleBypass`（20 位原子），音频线程每个块读一次。全部五行旁路后，各载体还原为接近熵 0 的干净配方（黑胶的 STEREO ORDER 恒为 100%，旁路它无可闻变化）。

| 行 | TAPE | VINYL | STREAM | PHASE |
| --- | --- | --- | --- | --- |
| 0 BANDWIDTH | 高频滚降 + 饱和 | 高频滚降 + 饱和 | 滤波并联混合（`filterAmount`） | 高频滚降 |
| 1 TRANSPORT / BITRATE | wow/flutter/漂移 | 偏心 wow | 码率档位截止（恢复 20 kHz） | 随机抖动 |
| 2 DISCONTINUITIES | dropout + 后回声 | click/crackle | 丢包陷落 | 插值事件 + 静音陷落 |
| 3 STEREO / CORRECTION | 声场收窄 | 声场（无变化） | 声场收窄 | 误纠鸟鸣（`birdieRate`） |
| 4 SURFACE / SMEAR | hiss | 灰尘底噪 hiss / dust | smear + 残留 | hiss |

映射实现见 `Recipes.cpp::applyModuleBypass()`。旁路与强度都不是宿主参数（不进入自动化），但随状态保存：`EntropyState` 根节点的 `moduleBypass` 整数（0–0xFFFFF）与 `moduleIntensity` 空格分隔的 20 个浮点；非法值整体拒绝。工厂预设加载会把强度重置为默认值，但不重置旁路掩码。

标题右侧有 **RESET** 按钮：任何效果被旁路或强度偏离默认时变为可点的深灰（悬停蓝色加下划线），点击一次恢复全部 20 个旁路位和默认强度；无变化时呈浅灰不可点。实现为 `resetModuleSettings()`。

### 6.4 四种模式五行效果的含义

这是用户视角的“每行代表什么”。行号 0–4 对应右侧从上到下；同名行在不同载体里的物理含义不同，括号内为 DSP 配方字段。

| 行 | TAPE | VINYL | STREAM | PHASE |
| --- | --- | --- | --- | --- |
| 0 BANDWIDTH 带宽 | 磁畴退磁导致短波长（高频）先失稳：低通滚降 + 磁带饱和谐波（`cutoffHz`/`filterAmount`/`saturation`） | 纹路与内圈磨损：高频衰减 + 内圈奇次失真（同上） | 有损编码丢弃高频：低通并联混合（`filterAmount`） | 介质氧化/误读让高频先糊（`cutoffHz`/`filterAmount`） |
| 1 TRANSPORT 走带 / BITRATE 码率 | 带速不稳：wow（0.83/1.37 Hz）+ flutter（27.3 Hz）+ 声道方位漂移（`wowMs`/`flutterMs`/`driftMs`） | 盘面偏心/翘曲：0.55 Hz 音高慢摆（`wowMs`） | 码率阶梯：Entropy 派生 320→64 kb/s 的砖墙截止（`cutoffHz` 档位） | 采样时钟抖动：逐样本随机时基（`flutterMs` 用随机值） |
| 2 DISCONTINUITIES 断续 | dropout：磁粉脱落使信号瞬间陷落，随时间频率变高、单次陷落短暂（`gapRate`↑ / `gapDepth`≈0.72 / `gapMs`≈38ms），外加 print-through 后回声（`echo`） | **灰尘/划痕撞击产生的 click/crackle 爆音**：存放越久灰尘越多，爆音**频率**显著上升（`eventRate` 至约 70/s），单次爆音幅度基本恒定（`eventLevel` 0.10→0.18，不随 SURFACE 行缩放）；它是音频自身的表面撞击效果，不是 noise | 丢包/缓冲欠载：随机静音陷落（`gap*`） | 插值咔哒 + 静音/跳读陷落（`event*` + `gap*`） |
| 3 STEREO ORDER 声场 / MISCORRECTION 误纠 | 磁头方位角误差让定位漂移：声场收窄（`width`） | 无立体声退化，恒为 100%（行存在但旁路无可闻变化） | 低码率声道合并：声场收窄（`width`） | **误纠鸟鸣 birdie**：光盘长期存放后反射层氧化/染料退化，CIRC 把读错数据“纠正”成错误值，产生短促扫频哨音（8–24ms，约 1.2–8.4kHz 扫频）；随时间增长的是**鸟鸣频率**（`birdieRate` 至约 7/s），不再静音 |
| 4 SURFACE RESIDUE 表面残留 / SMEAR 涂抹 | 带基 hiss 底噪（`noise`） | **静置/存放产生的灰尘底噪 hiss/dust**（`noise`），随 SURFACE 行强度缩放（默认 60%），与第 2 行的突发 click（不随该行）区分 | 编码预回声：瞬态前 smear + 金属残留（`smear`/`residue`） | 反复擦写后的噪声底抬升（`noise`） |

> 黑胶的“灰尘”在界面分两处：持续的灰尘底噪属于第 4 行 SURFACE RESIDUE（HISS / DUST）；灰尘颗粒撞击针尖产生的突发 click/crackle 属于第 2 行 DISCONTINUITIES。二者旁路互不影响。

### 6.5 峰值计量

Processor 用原子最大值累积 UI 两次读取之间的峰值；UI 通过 `exchange(0)` 取走后，以 `max(新峰值, 上次显示值 × 0.86)` 衰减显示。读数下限 −60 dBFS，低于/等于下限显示 `--`，大于 0 dBFS 显示红色；**红色不是自动限幅**。

编辑器关闭时累积器仍可能保留峰值，重开会显示一次此前峰值；这不是持久化状态。

### 6.6 悬停提示对照表（v1.0.1 修订）⭐

机制：`juce::TooltipWindow`（成员 `tooltip`，约 0.65 s 延迟，白底细线样式由 `ScientificLookAndFeel` 配置）。JUCE 8 的 TooltipWindow 只查询鼠标正下方组件（`TooltipClient`），不向父组件遍历，因此：子控件（时间轴、日期、旋钮、载体按钮、GEN）用 `setTooltip()`；编辑器自绘区域（顶栏、右侧描述符行）由 `EntropyAudioProcessorEditor::getTooltip()` 按实时鼠标坐标返回。提示文本全部为纯 ASCII 短句；按住鼠标拖动时提示自动隐藏。

| 区域 / 控件 | 提示内容（v1.0.1 起） | 备注 |
| --- | --- | --- |
| 网址 `iisaacbeats.cn` | `Visit iisaacbeats.cn` | 新增；`PluginEditor::getTooltip()` |
| 顶栏预设名称 | `Open preset list` | 新增；同上 |
| `<` 箭头 | `Previous preset` | 新增；同上 |
| `>` 箭头 | `Next preset` | 新增；同上 |
| BYPASS | `Bypass all processing` | 新增；同上 |
| RESET | `Restore all effect defaults` | 新增；同上 |
| 右侧 5 条描述符行 | 第 1 行：该载体该行的退化含义；第 2 行：`click to bypass - drag to adjust`（含义见下方 4×5 表） | 新增；`descriptorEffectTooltip()` 表 + `getTooltip()` 按当前载体动态拼接 |
| CARRIER TIMELINE | `Drag to pick a date - double-click to type 0-100` | 简化（原为三句长文）；`TimelineControl` 构造 |
| 右上选定日期 | `Double-click to edit the date` | 简化（原含日期格式示例长句） |
| INPUT 旋钮 | `Input trim before degradation` | 简化（去掉拖拽/双击操作说明） |
| MIX 旋钮 | `Dry/wet mix` | 简化（去掉延迟说明） |
| OUTPUT 旋钮 | `Output trim` | 简化（去掉限幅器说明） |
| TAPE 按钮 | `Magnetic tape - decays over 60 years` | 重写（原为机制术语短语）；`carrierMechanism()` |
| VINYL 按钮 | `Vinyl record - wears over 100 years` | 同上 |
| STREAM 按钮 | `Digital stream - degrades over 24 hours` | 同上 |
| PHASE 按钮 | `Optical disc - degrades over 40 years` | 同上 |
| GEN 按钮（仅 STREAM） | `Transcoding generations - click to cycle` | 简化 |

描述符行第 1 行文案（`descriptorEffectTooltip(carrier, row)`，`Source/PluginEditor.cpp` 匿名命名空间）：

| 行 | TAPE | VINYL | STREAM | PHASE |
| --- | --- | --- | --- | --- |
| 0 BANDWIDTH | High-frequency loss + saturation | High-frequency loss + groove distortion | Lossy high-frequency cut | High-frequency loss |
| 1 TRANSPORT / BITRATE | Wow, flutter, azimuth drift | Eccentric pitch wobble | Bitrate ladder 320-64 kb/s | Clock jitter |
| 2 DISCONTINUITIES | Dropouts + print-through echo | Dust clicks and crackles | Packet-loss dropouts | Read-error dropouts |
| 3 STEREO / MISCORRECTION | Stereo width collapse | Stereo width (no degradation) | Stereo width collapse | Mis-corrected birdie tones |
| 4 SURFACE / SMEAR | Tape hiss | Dust hiss | Pre-echo smear | Disc noise floor |

## 7. 工厂预设与状态持久化

### 7.1 20 个工厂快照

数据定义：`Source/Parameters.cpp` → `factoryPresets()`。下表索引为代码使用的 0-based 索引，不是宿主 program 编号。每预设包含该载体的**五行强度组合**（行序：0 带宽、1 走带/码率、2 断续、3 声场/误纠、4 表面残留/SMEAR），加载时先复位全部 20 行默认再套用这五行。

每次加载还统一设置：`mix=1`、`input=0 dB`、`output=0 dB`、`bypass=false`；因此加载预设会退出旁路，也会重置用户增益和干湿。

| 索引 | 载体 | 精确预设名 | Entropy（百分比） | Generations | 五行强度 [0,1,2,3,4] |
| ---: | --- | --- | --- | ---: | --- |
| 0 | TAPE | Fresh stock | 0.00（0%） | 1 | 100 / 100 / 100 / 100 / 60 |
| 1 | TAPE | 2001 / Warm archive | 0.42（42%） | 1 | 85 / 70 / 55 / 90 / 50 |
| 2 | TAPE | 1989 / Fading domains | 0.62（62%） | 1 | 100 / 45 / 75 / 70 / 75 |
| 3 | TAPE | 1978 / Wandering transport | 0.80（80%） | 1 | 60 / 100 / 50 / 80 / 40 |
| 4 | TAPE | 1968 / Oxide memory | 0.97（97%） | 1 | 100 / 90 / 100 / 100 / 80 |
| 5 | VINYL | First pressing | 0.00（0%） | 1 | 100 / 100 / 100 / 100 / 60 |
| 6 | VINYL | 1989 / Paper sleeve | 0.37（37%） | 1 | 75 / 50 / 50 / 100 / 70 |
| 7 | VINYL | 1973 / Dust in the groove | 0.53（53%） | 1 | 85 / 35 / 100 / 100 / 60 |
| 8 | VINYL | 1961 / Off-centre | 0.65（65%） | 1 | 55 / 100 / 35 / 100 / 45 |
| 9 | VINYL | 1929 / Worn to noise | 0.97（97%） | 1 | 100 / 75 / 90 / 100 / 90 |
| 10 | STREAM | Lossless reference | 0.00（0%） | 1 | 100 / 100 / 100 / 100 / 60 |
| 11 | STREAM | Early web / 192k | 0.33（33%） | 1 | 90 / 80 / 40 / 85 / 70 |
| 12 | STREAM | 2001 / 128k | 0.52（52%） | 2 | 100 / 90 / 60 / 80 / 80 |
| 13 | STREAM | Generation seven | 0.79（79%） | 7 | 95 / 90 / 70 / 100 / 100 |
| 14 | STREAM | Buffer exhausted | 0.97（97%） | 8 | 100 / 100 / 100 / 100 / 90 |
| 15 | PHASE | Crystalline | 0.00（0%） | 1 | 100 / 100 / 100 / 100 / 60 |
| 16 | PHASE | 2016 / Correctable | 0.25（25%） | 1 | 85 / 50 / 50 / 40 / 60 |
| 17 | PHASE | 2008 / Interpolation | 0.45（45%） | 1 | 90 / 60 / 100 / 50 / 50 |
| 18 | PHASE | 1997 / Unreadable sectors | 0.72（72%） | 1 | 80 / 50 / 90 / 100 / 50 |
| 19 | PHASE | 1986 / Amorphous | 1.00（100%） | 1 | 100 / 100 / 100 / 100 / 80 |

名称中的年份与“现在 ≈ 2026”下熵值在该载体时间尺度（TAPE 60 年 / VINYL 100 年 / STREAM 24 小时 / PHASE 40 年）映射出的日期大致一致，是设计命名，不代表对指定年份真实设备的测量或标定；现实时间流逝后日期会漂移。崭新参考（0/5/10/15）使用默认强度。

`matchingFactoryPreset()` 按当前参数反向识别名称：载体、熵、代数与本载体的五行强度（容差 0.001）须同时匹配，其余 15 行须为默认；少量浮点误差容许匹配，不匹配时显示 Custom / Archive。没有额外保存“最后预设索引”。宿主 `getNumPrograms()` 仍返回 1，**上述 20 项不是 20 个宿主 programs**。

### 7.2 用户文件与宿主状态

- 扩展名：`.entropypreset`；Archive → Save snapshot / Open snapshot；默认从用户文档目录选择位置，没有独立的用户预设库索引或自动目录扫描。
- 文件与宿主工程复用同一套状态：`EntropyState` 根节点 → `schemaVersion=3`、`timelineVersion=1`、`selectedUtcMs`、`moduleBypass`、`moduleIntensity`、`editorWidth`、`Parameters` 子树。
- 格式为 JUCE `copyXmlToBinary()` 编码，不是裸 XML；保存 7 个宿主参数、绝对 UTC 日期、20 位模块旁路掩码、20 个行强度及窗口宽度。
- **不保存**延迟缓冲、滤波状态、包络、随机数位置、事件计时、动画、计量器或临时提示；不保证恢复原运行现场的逐样本音频轨迹。
- `restoreState()` 可在没有 Editor 时使用；v2/v3 直接恢复固定日期，关闭工程期间经过的真实时间计入老化；不重置 DSP 历史。
- v1/v2 兼容：旧快照携带的 `noise` 参数（8 参数时代）被剥离并迁移为磁带/黑胶/光盘的 SURFACE 行强度（流媒体 SMEAR 行保持 1.0）；v1 另按旧 carrier/entropy 建立日期。重新保存为 v3 后不会再次迁移。
- 保存使用 `TemporaryFile` 后替换目标；对扩展名修正后发生的额外覆盖风险再次确认。

`restoreState()` 先整体检查再提交：拒绝空/截断/超过 1 MiB 数据、错误根节点、未知版本、重复 Parameters 树、非 PARAM 子节点、参数个数不符、ID 缺失/重复、非法或越界数字、离散参数小数值；v2/v3 还验证时间映射版本、完整 int64 时间戳及允许日期范围；v3 额外验证 `moduleIntensity` 恰好 20 个 0–1 数值。解析使用与 locale 无关的 `std::from_chars`。

窗口宽度默认 960，限制在 720–1920；超界宽度被钳位。这不是完整 XML schema/签名机制。旧版插件不能读取 v3；未来新增参数或修改时间映射仍须继续维护版本兼容。

## 8. 文件结构与关键接口

### 8.1 文件职责

| 文件 | 职责 | 常见修改 |
| --- | --- | --- |
| `CMakeLists.txt` | 版本、JUCE 依赖、插件身份、格式、测试目标 | 改版本/平台/构建目标 |
| `build.ps1` | Windows 工具链发现、配置、编译和测试 | 构建参数/本地 JUCE 路径 |
| `.gitignore` | 排除构建产物和本地文件 | 当前 `*.entropypreset` 也被忽略，提交示例快照前需留意 |
| `Source/Parameters.h` / `Parameters.cpp` | 稳定 ID、参数范围/显示、工厂预设表 | 新增用户参数、改默认值/预设 |
| `Source/Timeline.h` / `Timeline.cpp` | `spanMs()` / `dateAt()` / `amountAt()` / `localDate()` / `parseLocalDate()`，独立时钟缓存 | 改载体跨度、日期转换或格式 |
| `Source/UI/TimelineControl.h` | 日期标尺、反向映射、鼠标/键盘/自动化手势 | 改时间轴交互、刻度和视觉 |
| `Source/UI/PresetPanel.h` | 四列自绘预设卡片、命中测试、悬停/选中、键盘导航和快照入口 | 改预设展开层样式与交互 |
| `Source/Dsp/Recipes.h` / `Recipes.cpp` | `Controls`、`Recipe`、退化映射与标签 | 改阈值、曲线、目标强度 |
| `Source/Dsp/EntropyEngine.h` / `EntropyEngine.cpp` | DSP 生命周期、延迟、滤波、事件、随机、平滑、混音 | 改处理算法、时序、调制常量 |
| `Source/PluginProcessor.h` / `PluginProcessor.cpp` | JUCE 生命周期、总线、APVTS、状态、预设应用、峰值发布 | 宿主接口、状态兼容、参数传递 |
| `Source/PluginEditor.h` / `PluginEditor.cpp` | 布局、描述符、模型动画、菜单、文件对话框、悬停提示（`getTooltip()` 与 `descriptorEffectTooltip()`，见 §6.6） | 界面文字/交互/显示逻辑 |
| `Source/UI/ScientificLookAndFeel.h` | 色板、自绘按钮/滑块、数值编辑入口 | 系列风格、旋钮绘制 |
| `Tests/DspTests.cpp` | 独立 DSP 回归 | 音频端点、自动化、块大小等行为 |
| `Tests/StateTests.cpp` | JUCE 状态与 UI 生命周期回归 | 预设/恢复/Editor 行为 |
| `PROJECT_OVERVIEW.md` | 当前参数和开发导航 | 参数/曲线变更后同步维护 |

独立库目标叫 `EntropyDSP`。当前并没有 10 个独立 `juce::dsp::ProcessorBase` 模块类，也没有独立 `PresetService` / `StateCodec` 文件；不要按尚未实现的建议架构寻找文件。

### 8.2 关键符号导航

| 符号 | 职责/数据流 |
| --- | --- |
| `EntropyAudioProcessor::currentControls()` | 普通参数读 APVTS；Entropy 从日期与缓存墙钟计算；组装实时有效 `Controls` |
| `timelineSnapshot()` / `selectDate()` | 获取当前时刻/选定日期/载体；提交固定日期并同步兼容宿主参数 |
| `parameterValueChanged()` | 处理宿主显式 Entropy 通知并重新锚定日期 |
| `beginDateGesture()` / `endDateGesture()` | 日期拖动与编辑的宿主自动化手势 |
| `EntropyAudioProcessor::prepareToPlay()` | 把当前控制传给引擎、分配准备、上报延迟 |
| `EntropyAudioProcessor::process()` | 清理多余声道/MIDI、设置无 denormal 作用域、调用引擎、累积峰值 |
| `EntropyAudioProcessor::setParameter()` | UI/预设单参数写入，转换归一化值并通知宿主 |
| `EntropyAudioProcessor::loadFactoryPreset()` | 写入预设值及公共默认值，逐参数通知宿主 |
| `EntropyAudioProcessor::getStateInformation()` / `restoreState()` | 状态编码与校验恢复 |
| `EntropyEngine::prepare()` / `reset()` | 缓冲准备/清空、种子及平滑初始值初始化 |
| `EntropyEngine::setControls()` | 防御性限制参数、设置平滑目标，不分配资源 |
| `EntropyEngine::process()` | 逐样本调度、配方/滤波控制周期、混音/旁路 |
| `EntropyEngine::processLane()` | 单载体共享 DSP 积木链 |
| `makeRecipe()` | 载体 + Entropy + 代数 → `Recipe` |
| `drawSpecimen()` / `drawDescriptors()` | 模型动画/配方目标显示 |
| `timerCallback()` | 30 Hz 同步参数、预设名、窗口宽度、动画与计量器 |
| `showArchive()` / `choosePresetFile()` / `saveSnapshot()` | 工厂菜单与文件存取 |

音频线程不依赖界面存活；普通参数以 APVTS 为真源，时间控制以 `selectedDate` 和墙钟为真源，派生有效 Entropy。当前原子读取是逐字段快照，**不是多参数事务**；加载工厂预设也是逐参数写入。有效退化程度随现实时间变化后，工厂名可能转为 Custom，这是按当前声音状态匹配的结果。

实时约束：现有处理路径使用固定成员状态和 prepare 阶段分配的缓存，音频回调不做文件操作或阻塞锁。新增处理时继续避免回调内扩容、联网、日志、UI 操作和资源重建；新增复杂算法需另设实时安全交接方案。

## 9. 构建、产物与验证

### 9.1 Windows 构建入口

在 PowerShell 中使用：`powershell -NoProfile -ExecutionPolicy Bypass -File "I:\理科系列\Entropy\build.ps1"`。

此脚本构建的是 **Entropy**，不要运行 `I:\理科系列\Organic Chemistry\build.ps1` 来验证本项目。脚本调用 CMake 配置、Release 构建与 CTest，全部成功后打印 `BUILD_OK`。

| 脚本参数 | 默认/意义 |
| --- | --- |
| `-JuceSource` | 默认读取环境变量 `ENTROPY_JUCE_SOURCE`；可指定本地 JUCE 源码 |
| `-BuildDirectory` | 默认工程下 `cmake-build-ninja` |
| `-Jobs` | 默认 4，构建并行任务数 |
| `-DspOnly` | 关闭插件目标，只构建独立 DSP 与其测试；不执行 StateTests |

- 自动发现 Visual Studio x64 C++ 工具链和 Windows SDK；需要 CMake 和 Ninja。
- 未指定 JUCE 路径且本机存在参考工程源码时，复用系列文件夹内 `Organic Chemistry\cmake-build-release-visual-studio\_deps\juce-src`（相对 `build.ps1` 所在位置解析），通过 `FETCHCONTENT_SOURCE_DIR_JUCE` 指定；不复制或修改旧插件源码。
- 没有本地源码覆盖时，CMake 按 `GIT_TAG 8.0.12` 获取 JUCE。指定本地路径会覆盖远端版本选择，迁移机器后需自行核对该源码版本。
- CMake 开关：`ENTROPY_BUILD_PLUGIN=ON`、`ENTROPY_BUILD_TESTS=ON`、`ENTROPY_COPY_PLUGIN_AFTER_BUILD=OFF`；脚本固定开启测试、关闭安装，`-DspOnly` 控制插件开关。
- Apple 配置声明 macOS 11.0 起、`x86_64;arm64` 架构；目前没有对应平台的成功构建验证记录。

### 9.2 默认产物

| 项目 | 路径 |
| --- | --- |
| VST3 bundle | `I:\理科系列\Entropy\cmake-build-ninja\Entropy_artefacts\Release\VST3\Entropy.vst3` |
| Standalone | `I:\理科系列\Entropy\cmake-build-ninja\Entropy_artefacts\Release\Standalone\Entropy.exe` |
| 构建日志 | `I:\理科系列\Entropy\build.log` |
| 测试记录 | `I:\理科系列\Entropy\cmake-build-ninja\Testing\Temporary\LastTest.log` |
| UI 快照 | `I:\理科系列\Entropy\cmake-build-ninja\Entropy-preview.png`（StateTests 在工作目录生成） |

开发构建不会自动安装插件；独立安装器脚本（原 `build_installer.bat` / `build_installer_mac.sh` / `entropy_installer.iss`）已删除，本插件只随系列大安装包分发（`i:/理科系列/build_installer.bat` → `dist/iisaacbeats_ScienceSeries_Setup_<ver>_x64.exe`）。签名、公证或自动更新发布流程仍未建立。

### 9.3 已验证与未验证

1.0.0 Windows VST3 / Standalone 已重新构建并输出 `BUILD_OK`；`EntropyDSPTests` 和 `EntropyStateTests` 两项通过。覆盖内容除前述外，新增热力学公式层一致性校验（ΔG 恰在熵 0.5 过零、t½ 等于载体时间尺度、速率常数为正）与公式字符串 UTF-8 渲染验证。公式数值为艺术标定（T = 298 K 为真实室温锚点），不代表真实热化学测量；预览测试使用固定测试时钟，图片中的 NOW 并非拍摄时的实时系统时间。

1.0.1（悬停提示修订）Windows VST3 / Standalone 已构建并输出 `BUILD_OK`；`EntropyDSPTests` 与 `EntropyStateTests` 两项通过。本轮为纯 UI 文案与提示改动（见 §6.6），未改动 DSP；悬停提示的视觉观感尚未做截图验收。本版本随后进行**全系列改名**：显示名 `ChemE-Entropy`、Bundle ID `cn.iisaacbeats.ChemEEntropy`、Plugin Code `CE02`，改名后重新构建 `BUILD_OK` 并纳入系列安装包（与旧插件共存，不卸载旧安装）。

同日后续修订（仍为 1.0.1）：① 模型视图粒子按载体差异化排布（VINYL 螺旋、STREAM 波浪，见 §6.1），截图已检查；② STREAM 代际加强（§4.4 公式更新），`EntropyDSPTests` 的曲线单调性、旁路还原、强度缩放语义全部仍通过；GEN 1（loss=0）配方与旧版完全一致。系列安装包版本定为 1.2.0。

**1.0.2**：模型视图排布再调整——VINYL 由螺旋/灰尘盘改为 5 条横贯全宽的上凸音槽弧带，STREAM 由单根波浪线改为 5 条并行波浪线（与 TAPE 同构、行间相位错开），四种载体的排布均占满整个粒子显示区；截图（VINYL/STREAM）已检查，`EntropyDSPTests` 与 `EntropyStateTests` 通过。

**1.0.3 / 1.0.4**：VINYL 音槽弧带两轮迭代——1.0.3 尝试同心圆弧（圆心近、上长下短），小半径下弧带收缩成拱顶团块，弃用；1.0.4 改为**透视音槽带**（真实唱片照片中同心音槽的低视角形态）：五条弧各自参数化（半径 380→132、矢高 55→12），上方弧更长更平缓、下方弧更短更弯，跨度 392/321/252/183/109 设计像素逐级收窄；截图已检查，`EntropyDSPTests` 与 `EntropyStateTests` 通过。

**1.0.4 同日后续修订（强调色饱和度）**：`carrierColour` 四载体强调色整体提升饱和度，参考 Organic Chemistry 的 CPK 元素配色让白底上的彩色更鲜明——TAPE `#af614c→#b05a44`、VINYL `#687b54→#6e9a52`、STREAM `#2a6fb0→#3a78c2`、PHASE `#807396→#8764a6`，色相不变。该函数是界面所有载体彩色的单一来源，故按钮、时间轴、旋钮、粒子动画、载体图标与描述符区同步生效。构建 `BUILD_OK`，`EntropyDSPTests` 与 `EntropyStateTests` 通过。

| 测试 | 当前覆盖 |
| --- | --- |
| `EntropyDSPTests` | 四载体曲线/可听信号变化；Entropy=0、MIX=0、插件/宿主旁路的延迟对齐；mono/stereo；44.1/48/96/192 kHz 端点测试 |
| `EntropyDSPTests` | 1/511/18000 样本分块一致性；自动化压力、非有限输入、零长度、重复 prepare；压力测试另含 8 kHz |
| `EntropyDSPTests` | 光盘误纠鸟鸣端点（可听不静音、旁路第四行可去除鸟鸣）、静音尾部衰减；处理期间所监测的 `new/new[]` 分配为零 |
| `EntropyStateTests` | 20 个工厂预设参数/名称/窗口宽度往返；无 Editor 恢复；480 样本报告延迟；超出 prepare block 大小的处理 |
| `EntropyStateTests` | 畸形/截断/未来版本状态拒绝；重复 Editor 创建销毁；四载体与菜单多缩放 PNG 渲染 |
| `EntropyStateTests` | 英文日期解析/显示、旧格式兼容；最左端再过一年仍封顶；预设面板 20 项选择、外部点击/Esc/Close 关闭、键盘导航与存取入口 |
| `EntropyStateTests` | 模块旁路位与 20 个行强度的设置/状态往返、非法掩码与非法强度拒绝、描述符行“单击旁路 vs 拖动强度”手势区分、RESET 恢复旁路与默认强度、v1/v2 noise 迁移到 SURFACE 行；`EntropyDSPTests` 另验证全旁路配方还原为熵 0 干净值、STREAM 全旁路透明输出、行强度零/半/满与 NaN 回退语义、黑胶爆音“频率上升且幅度恒定”、SURFACE 行强度 0 时爆音仍发声（与噪声解耦）及磁带 dropout 短暂性 |

未完成：DAW 实机试听/兼容性矩阵、自动化听感验收、插件验证器认证、真实 codec/硬件对比、CPU 长时性能基准、所有分配 API 的实时检测、macOS/AU 构建与宿主测试。状态测试验证参数恢复与有限音频输出，**不验证 DSP 历史逐样本恢复**。

## 10. 与灵感文档的差异及待实现功能

| 灵感目标 | 当前状态/限制 |
| --- | --- |
| 10 个可复用 DSP 积木 | 主要处理集中在 `processLane()`；不是 10 个独立可插拔类 |
| 温度/时间等物理化学宏 | 已有绝对日期时间轴与实时老化；没有独立 Temperature / Order 参数，宿主保留 7 个 ID |
| TAPE 完整物理退化 | 已有基础链；慢漂为正弦组合，没有随机游走/scrape flutter 或完整磁滞模型 |
| TAPE print-through 前/后回声 | 只有基准后 1.5 s 的弱抽头；没有长前回声，也未模拟卷径变化 |
| VINYL scratch、rumble、内圈磨损追踪 | 尚未；当前为统一低通/饱和与脉冲纹理，无播放位置或划痕计数参数 |
| STREAM 真实编码与 generation loss | 参数化听感近似，无 MP3/AAC/Opus 编解码或心理声学掩蔽模型 |
| STREAM 独立码率选择器 | 当前由 Entropy 派生标签；还不是用户可独立选择的参数 |
| STREAM 瞬态检测预回声 | 有真实提前抽头，但没有瞬态检测和自适应 smear |
| STREAM 分频段立体声退化、swirl/ring | 目前全频 M/S；残留为高频差值调制，无完整频谱处理或反馈谐振尾音 |
| PHASE 完整 CIRC、birdie、跳轨 | 已实现简单扫频鸟鸣（`birdieRate`）与短保持/插值、陷落近似；不是完整 CIRC 纠错状态机，也没有播放位置跳变 |
| Annealing / Quench、擦写次数 | 尚未实现；PHASE 名称/动画不代表这些控件已存在 |
| 随机化、Undo/Redo、A/B、Reset 控件 | 尚未实现，不要与引擎内部 `reset()` 混淆 |
| 发布设施 | 仅随系列大安装包分发（iisaacbeats Science Series）；无签名/公证、在线授权、自动更新或遥测 |

## 11. 下一轮需求如何点名与定位

### 11.1 推荐描述方式

| 可以直接这样提需求 | 意味着修改什么 | 首要位置 |
| --- | --- | --- |
| “TAPE 的 `wowMs` 在 Entropy 0.4 后才起效，上限改成 1.5 ms” | 改激活阈值与深度曲线，不改 wow 频率 | `Recipes.cpp` → `makeRecipe()` 的 tape 分支 |
| “TAPE 的 flutter 太快，把 27.3 Hz 调低” | 改调制频率；当前 STREAM residue 也复用该 flutter 信号，需明确是否拆开 | `EntropyEngine.cpp` → `process()` |
| “VINYL 的 `eventRate` 最高降到 20/s，`eventLevel` 保持不变” | 减少 click/crackle 密度，不减单个事件强度 | `makeRecipe()` 的 vinyl 分支 |
| “VINYL 脉冲太短，改 `impulse` 衰减” | 改脉冲形状/时间，而不是事件率 | `EntropyEngine::processLane()` |
| “SURFACE 行默认降到 30%” | 改 `defaultIntensity()` 中三个 SURFACE 行默认；同时决定工厂预设/状态迁移是否同步 | `Recipes.h` → `defaultIntensity()`、`resetModuleSettings()`、状态迁移 |
| “只降低 TAPE 底噪，不影响 VINYL clicks” | 改 TAPE 的 `Recipe::noise`，或只调 TAPE 的 SURFACE 行强度 | tape 配方 / `moduleIntensity` |
| “把某行强度注册为宿主自动化参数” | 新增宿主 ID、附件与状态兼容；当前 20 个强度只内部保存 | 参数布局、Processor、Editor、状态兼容、测试 |
| “把 STREAM 的 `bitrate` 做成独立参数” | 新增宿主 ID、用户控制与状态；明确和 Entropy 的优先关系 | 参数布局、`Controls`、Processor、配方、Editor、状态兼容、测试 |
| “STREAM 的 `smear` 少一点，保留码率阶梯” | 改 smear 系数，不一定改 `cutoffHz` | streaming 配方；如改算法则进 `processLane()` |
| “PHASE 鸟鸣太密/太少” | 改 `birdieRate` 曲线（`7·O₀.₃₀`）；鸟鸣时长、扫频范围和幅度在 `EntropyEngine::processLane()` 的触发块里调整 | optical 配方与端点测试 |
| “让 BYPASS 切换更慢” | 改内部 `bypass` Ramp 时长，不改用户参数类型 | `EntropyEngine::process()` |
| “SURFACE RESIDUE 要显示真实噪声电平” | 新增音频统计/可视化数据流，不只是替换文字 | Engine/Processor 计量 + `drawDescriptors()` |
| “把 MIX 旋钮换成新的物理化学名字” | 如果仅换显示文案，保留 `mix` 稳定 ID 和声音行为 | Editor / 参数显示名 |

### 11.2 维护约定

1. **先区分参数层级**：用户参数用 ID（如 `mix`）；行强度用 `moduleIntensity[i]`（`i = carrier × 5 + row`，非宿主参数）；配方量用 `Recipe::noise`；算法状态/常量用符号（如 `gapSlew`、`warp`）。需要“独立控制”时明确是新增旋钮/自动化参数，还是只调整内置曲线。
2. **单位写清**：`wowMs` 是延迟毫秒，不是 cents；`eventRate` 是目标事件率；`gapDepth` 是衰减深度；`input`/`output` 才是 dB。
3. 改配方时同步本文件的公式/阈值；改默认值时检查结构默认、参数布局、工厂加载与名称匹配是否一致。
4. 新增公开参数需同步 `ids::all`、参数布局、Processor `raw` 读取、`Controls`、DSP、UI、预设、状态兼容和测试；现有固定 7 参数校验不能原封不动沿用。已删除的 `noise` ID 不复用成不同含义。
5. 当前 `schemaVersion=3` / `timelineVersion=1`，已支持 v1/v2 快照迁移（含 noise 剥离）；修改日期跨度、强度默认或新增参数时须继续维护版本兼容。既有 ID 不重命名、不复用成不同含义。
6. 载体数和工厂预设数量目前有固定数组/菜单循环；增加第五种载体或第 21 个预设需要同步 Processor、Engine、Editor 和测试，不能只在表末尾追加一行。新增/修改预设必须同时给出五行强度组合（`FactoryPreset::intensity`），并保持 `matchingFactoryPreset()` 的按组合识别语义与 §7.1 表同步。
7. 修改 DSP 后使用本项目 `build.ps1` 构建并执行回归；测试通过后仍需试听验证。只改本文档不需要重新编译。
8. 不删除 `.codebuddy` 项目数据；不修改参考插件的源码/构建产物来“顺便修复”本项目。
