# 阶段 2：Deferred Rendering 与 Tiled 多光源剔除手敲指南

本文接续 [Phase 2 大纲](overview.md#6-阶段-2deferred-rendering-与多光源剔除) 和 [Stage 1：CSM](stage-1-cascaded-shadow-maps.md)。
核对日期：2026-11-02；源码基线：`289fbc5`（`Add csm diagnostics`）**加当前工作区未提交的 CSM 诊断、Buffer 属性/flush、配置归档和验收 UI 改动**，包括新增的 `ShadowFrameSummary.hpp`。只检出该 commit 不能完整复现本次核对。

**结论：建议有条件进入 Stage 2。Stage 0.5 与 Stage 1 的核心实现已具备，允许开始本指南 A 组；Stage 1 的专项画质、生命周期和基线归档尚未全部收口。Stage 2 本文仍是开发计划，没有实现 Deferred 或 Tiled。**

## 1. 是否需要做，当前是否可以开始

### 1.1 为什么仍建议做 Stage 2

项目目标是可分析的现代光栅渲染器，并逐步进入 ReSTIR。Stage 2 同时提供可复用的深度/法线/材质输入、多附件与 Compute 同步经验，以及大量光源下的可测量对照，符合现有学习路线。
Deferred 不是 ReSTIR 的算法硬前提；若目标改为尽快做 Ray Query，可以重新安排顺序。但按当前路线，不建议跳过，也不把“只有 G-buffer 画面”当作本阶段完成：**一种多光源剔除是必做范围。**

### 1.2 本次核对到的实际进展

| 部分 | 当前实现与证据 | 仍不能据此宣称的内容 |
|---|---|---|
| Stage 0 基线 | 每槽 query、三段 GPU 时间、六项 CPU 时间、draw/triangle、payload/VMA、120 次提交预热及 300 行同 ID 配对；CSV 与 schema 2 配置旁置文件成套导出 | 已有 `gpu-baseline.csv` 是一组采集，不能自动证明同机位 Single/CSM 的 A/B 已完成 |
| Stage 0.5 | Loader/Instance/Device 版本记录、Features2、支持/启用区分；当前设备开启 Synchronization2 和 Dynamic Rendering，主帧 Present/Scene/Shadow 已迁移 | 不是所有较低版本设备、旧路径或其他平台均已验收；IBL 烘焙仍可保留旧 RenderPass |
| Stage 1 数学 | split、视锥角点、稳定 XY、texel snapping、场景 caster 深度范围、世界 bias 转换、混合带扩大拟合 | 数学测试不能替代运动画面、Mask/双面和光照一致性观察 |
| Stage 1 GPU | 每飞行帧四层深度数组及逐层 view、Single/CSM、PCF、blend、级别着色与逐层预览 | 默认 `blendFraction=0`；仅存在混合代码不能证明无接缝 |
| Stage 1 诊断 | 提交快照已包含实际 cascade 数、near/receiver far/FOV、最终 bias、图像 extent/format/layers，并参与配置变化取消；开始采集时冻结相机/灯光/对象/设备上下文，支持配置持久化；已加入 coverage 与测试机位控件 | 已有 `captures/csm-20261102/validation.log`，本次检索无 Validation Error/VUID/SYNC-HAZARD；尚未看到填写后的专项验收记录，日志不代替视觉观察 |
| Buffer 契约 | requiredFlags 已透传；查询并保存实际内存属性；UBO、网格 staging、纹理 staging、天空盒 staging 的 CPU 写入后均调用 flush | 尚不能据此声称在非 coherent 设备上完成了视觉验收；当前无 host readback 消费者，未来读回另补 invalidate 与完成等待 |
| Stage 2 输入 | 可共享的场景、材质、CSM、IBL、HDR/Post 已有；现有点光源仍是 UBO 内最多 16 盏 | 尚无 MRT G-buffer、Deferred Lighting、可扩展 light SSBO、Compute Tiled 或其统计 |

2026-11-02 实际验证范围：

| 检查 | 结果 | 边界 |
|---|---|---|
| `cmake --build --preset linux-debug -j 2` | 通过，当前构建已是最新 | 不等于运行验收 |
| `cmake --build --preset linux-release -j 2` | 通过 | 配置阶段仍有 Vulkan loader 的 RPATH 搜索路径警告；本次 Release 可正常运行 |
| 两个配置分别执行 CTest | 各 6/6 通过，包含 CSM 数学与采集测试 | CPU/资产测试，不是图形截图测试 |
| 16 个现有 shader 在临时目录编译 | 全部通过，与运行资源中的 SPIR-V 一致 | 使用现有脚本的 `--target-env=vulkan1.0`，没有改源码资源 |
| Debug/Release：`--scene shadow-playground --frames 30` | 均正常退出；Debug 日志未见 Validation Error/VUID | 普通 Validation；未声称已开启同步验证，也未做模式切换与视觉判定 |

运行设备为 NVIDIA GeForce RTX 4080 SUPER；Loader 1.3.275，应用请求/设备使用级别为 Vulkan 1.3，设备报告 API 1.4.312。**设备 API 版本不是驱动版本。** 本次窗口为 1920×1080，实际 HDR Scene 为 4x MSAA，两个帧槽均分配 2048²×4 层阴影。日志中的设备 MSAA 上限 8x 不能当作本次 Scene 实际值。

### 1.3 准入与收口边界

逐步手敲、每步目的、Buffer 修正代码和 CSM 实验操作见 [CSM 收口与配置归档指南](stage-1-csm-handoff.md)。

这次建议是允许下一阶段的最小增量，不是把 Stage 1 原验收清单自动勾完：

- **现在可以做 A 组：** 固定契约、重建数学与 CPU 检查，不改变现有主渲染链。
- **在 B 组接入新 GPU 附件前完成一次 Stage 1 收口：** Single/CSM 同条件截图和慢平移/旋转、四层与级别视图、相机外 caster、Mask/双面；resize、最小化恢复、场景/模式切换及退出。若发现未解释的同步、资源生命周期或材质退化，先解决再扩主链。
- **保留可复现旧路径：** 归档两模式原始 CSV 及旁置配置，记录相机/灯光、commit 与 dirty diff、构建/Validation、GPU/driver、分辨率/实际采样数、阴影参数/实际图像形状；至少完成一次关键帧检查。未执行的工具或平台继续注明待验收，不能据此对外宣称 Stage 1 全验收。
- **已完成的准备项：** D16 格式标签已经修正；Buffer 的 requiredFlags、实际属性查询和四处 CPU 写入 flush 已落地；CSV/config 与开始采集时的上下文已能成套保存。这些项无需重新阻止 A 组。
- **后续读回契约：** 当前没有 GPU→CPU 读回消费者，不为开始 A 组额外添加空用的 invalidate。D/E 真正引入统计读回时，再补齐完成等待和非 coherent invalidate；CPU flush 不替代 GPU 设备依赖。

当前未发现必须重做 CSM 架构才能开始 A 组的问题。上述收口没有被本次 30 帧冒烟代替。

## 2. 本轮交付、方案与边界

按现有大纲选用：**保留 Forward → 单采样 Deferred 全光源基线 → Compute Tiled Deferred**。

| 方案 | 取舍 | 本轮安排 |
|---|---|---|
| Deferred + Compute Tiled | 深度/材质可直接复用；二维 tile 易观察，深度跨度大时有较多假阳性 | 推荐并作为下文默认，不是用户已逐项确认的参数 |
| Deferred + Clustered | 增加 Z 分层，适合更复杂深度分布；索引与调参范围更大 | 按实测需要后续扩展，不要求两种都做 |
| Forward+ | 可保留前向材质/MSAA，复用 tile 光源列表 | 后续可复用本阶段列表；本轮仍交付路线图要求的 G-buffer/Deferred |

必做：Opaque/Mask、法线贴图与双面语义，方向光/CSM、点光源/IBL/Emissive、背景与 HDR/Post；可扩展 light buffer、全灯基线、Tiled 列表及溢出正确性回退；附件视图、热图、计时、内存与固定条件对照。

本轮不同时实现 Blend、聚光灯、点光阴影、MSAA Deferred、TAA/history、GTAO、Bloom、实例化/GPU culling、Bindless、Render Graph 或异步 Compute。Blend 仍按当前导入器的不支持策略报错，未来另建 Forward 透明 pass；本阶段基于不透明深度的列表不能直接承诺覆盖透明物体。

```text
Forward： Shadow → Forward Scene（保留原 MSAA）→ HDR → Post + UI → Present
Deferred：Shadow → Geometry（1x G-buffer）→ [Compute Tiled] → Lighting（1x HDR）
                                                               ↓
                                                        Post + UI → Present
```

## 3. 对照当前代码，改动从哪里开始

下表中“拟新增”是建议职责与名称，不表示源码已有这些文件或函数。

| 当前入口 | 已有约束 | 本阶段工作 |
|---|---|---|
| [RenderSettings.hpp](../../src/render/RenderSettings.hpp)、[RenderTypes.hpp](../../src/render/RenderTypes.hpp) | 当前无 Forward/Deferred 模式 | 拟增 `RenderPath { Forward, Deferred }`、`LightCullingMode { None, Tiled }`，配置随提交复制 |
| [Renderer.hpp](../../src/render/Renderer.hpp)、[GraphicsPipeline.cpp](../../src/render/GraphicsPipeline.cpp) | `colorAttachmentFormat` 单值，颜色 blend/rendering 附件数为 1 | 改为格式列表及等长 blend 状态；零色 Shadow、单色原管线仍有效 |
| [PostProcessing.cpp](../../src/render/PostProcessing.cpp) | HDR/depth 按 `sceneSamples_` 创建；depth 只有 attachment usage | 拆清 Forward 与 Deferred 活跃目标，新增 1x 可采样 G-buffer depth，保持 Post 输入为 1x HDR |
| [Renderer.cpp](../../src/render/Renderer.cpp) | `recordFrame()` 中 Shadow 后直接进入 Forward Scene | 按模式分发，保留 `beginFrame/recordFrame/endFrame`、token 与 fence 契约 |
| [ShaderTypes.hpp](../../src/render/ShaderTypes.hpp)、[Buffers.cpp](../../src/app/Buffers.cpp) | 当前 UBO 1424 字节，无重建逆矩阵；draw push 为 128 字节 | 拟增单独 `DeferredFrameUniforms`，随后迁出 UBO 的点光源数组；同步全部 GLSL 布局 |
| [Descriptors.cpp](../../src/app/Descriptors.cpp)、`GraphicsPipeline.cpp` | frame set 0 的 binding 0 为 UBO、1–5 为图像；材质 set 1 | 新 light SSBO 与 Deferred 输入 set；逐类型重算 pool 数量/`maxSets`，材质 set 保持语义 |
| [shader.frag](../../assets/shaders/shader.frag) | 材质解码、BRDF、CSM、IBL 与显示分支在一个 shader 中 | 抽共享材质/光照函数，拟增 `gbuffer.frag`、`deferred_lighting.frag`；避免两份 BRDF 漂移 |
| [Lighting.hpp](../../src/scene/Lighting.hpp)、[ScenePresets.cpp](../../src/app/ScenePresets.cpp) | `MAX_POINT_LIGHTS=16`，现有预设没有大量灯 | 拟增固定种子多光源预设，使用每槽 light SSBO |
| [VulkanContext.cpp](../../src/vulkan/VulkanContext.cpp) | 当前选 GRAPHICS family，未明确检查 COMPUTE flag | 查询所选 family 的 Compute/limit；本阶段在同一 queue 提交 |
| [VulkanResources.hpp](../../src/vulkan/VulkanResources.hpp)、[VulkanResources.cpp](../../src/vulkan/VulkanResources.cpp)、`VulkanContext.cpp` | Buffer 已有 map/unmap/flush、必选属性透传与实际属性记录 | 新 light/header buffer 按真实读写意图分配；统计读回出现时再补等待/invalidate |
| [FrameTiming.hpp](../../src/diagnostics/FrameTiming.hpp)、[BaselineCapture.hpp](../../src/diagnostics/BaselineCapture.hpp)、[Diagnostics.cpp](../../src/ui/Diagnostics.cpp) | 三个 pass、六个 query、固定 CSV 列与有效性检查 | 同步扩展 pass、总区间、模式/列表配置、CSV、内存、测试 |
| [CMakeLists.txt](../../CMakeLists.txt)、两套 `compile-shaders` 脚本 | 不自动发现新增 C++ 或 shader | 显式加入新文件、`.comp` 及 shader include 路径，构建后核对运行资源 |

建议将附件/Geometry/Lighting 管理放入拟新增 `src/render/DeferredRendering.cpp`，Tiled 资源与 dispatch 放入 `src/render/TiledLighting.cpp`，CPU 契约放入小型头文件/数学单元；沿用现有 Renderer 的所有权，不趁机重写应用架构。

## 4. 先固定坐标、材质与附件契约

### 4.1 坐标与重建

延续 `Camera::projection()`：右手 view、前方为 `-Z`，Vulkan NDC 深度 `[0,1]`；**投影已经翻转 Y，viewport 使用正高度**。首版没有 jitter/reversed-Z。

对尺寸为 `extent` 的附件，pixel 是整数像素坐标，取 texel 中心：

```glsl
vec2 uv = (vec2(pixel) + 0.5) / vec2(extent);
vec2 ndcXY = uv * 2.0 - 1.0;
float depth = texelFetch(gDepth, pixel, 0).r;
vec4 worldH = inverseViewProjection * vec4(ndcXY, depth, 1.0);
vec3 worldPosition = worldH.xyz / worldH.w;
```

`inverseViewProjection = inverse(proj * view)`，与生成这张 depth 的**同一帧槽、同一矩阵**对应；这里不要再翻 Y，也不要把 depth 改成 `2*depth-1`。正视空间距离为 `-(view * vec4(worldPosition,1)).z`，CSM 选级继续用它，不能用 depth texel 直接比较 split。

拟增 `DeferredFrameUniforms`：两个 `mat4`（`inverseViewProjection`、`inverseProjection`），一个 `uvec4 extentAndTiles`（width/height/tileCountX/tileCountY），一个 `uvec4 tileParams`（tileSize/maxLightsPerTile/cullingMode/debugMode）。std140 偏移为 0/64/128/144，总计 160 字节，CPU 做 `static_assert`；现有 frame UBO 提供 view、相机、光照与 CSM。

### 4.2 第一版 G-buffer

所有附件按飞行帧槽分配，分辨率等于实际 framebuffer extent，均为 1x、1 mip。下面格式是首版选择；创建前查询实际 format feature 与 image-format usage/sample 支持，不支持则禁用 Deferred 并明确原因，保留 Forward。

| 附件 | 首选格式 | 通道与读写约定 | clear |
|---|---|---|---|
| `gDepth` | `VK_FORMAT_D32_SFLOAT` | depth attachment + sampled；只采 depth aspect，NEAREST，不用比较 sampler | depth=1 |
| `gNormal` | `VK_FORMAT_R16G16B16A16_SFLOAT` | xyz=世界空间 shading normal，alpha=有效表面 1；无表面为 0 | 全 0 |
| `gBaseColor` | `VK_FORMAT_R8G8B8A8_UNORM` | rgb=已解码为线性的 Base Color × vertex color × factor；alpha 预留 | 全 0 |
| `gMaterial` | `VK_FORMAT_R8G8B8A8_UNORM` | r=metallic，g=roughness，b=材质 AO，a 预留 | 全 0 |
| `gEmissive` | `VK_FORMAT_R16G16B16A16_SFLOAT` | rgb=线性 emissive × factor，alpha 预留 | 全 0 |

四个颜色附件都需要 `COLOR_ATTACHMENT | SAMPLED` usage；depth 需要 `DEPTH_STENCIL_ATTACHMENT | SAMPLED`。所有 G-buffer 读取用相同整数 pixel 的 `texelFetch`，不跨边缘线性混合材质或法线。至少核查 `maxColorAttachments`、`maxFragmentOutputAttachments`、descriptor/UBO/SSBO 限制及具体格式能力，不能只复用 `findDepthFormat()` 的 attachment-only 结果。依据见 [Khronos 格式能力定义](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatFeatureFlagBits.html) 与 [设备限制](https://docs.vulkan.org/spec/latest/chapters/limits.html)。

不单独存世界坐标和几何法线。先保留法线贴图、tangent/UV 选择、normalScale、`gl_FrontFacing` 背面翻转；几何法线若后续算法确有需要再加。Roughness 下限沿用现有 0.04，AO 沿用 `mix(1, sampledAO, strength)`，只按已有语义影响环境项。

Base Color 和 Emissive 的源纹理仍按既有 sRGB 纹理路径解码，线性 G-buffer 不重复做 gamma。UNORM 的线性色彩量化会影响暗部；先用附件误差解释对照，确需精度再升级格式并重新记录内存，不能在 Lighting 偷补一次 sRGB 解码。

Mask 在写任何 MRT/depth 前按同一 alphaCutoff `discard`，双面使用对应无 cull pipeline。普通 G-buffer 不处理 Blend。

## 5. 提交 A：坐标、布局、深度重建与 tile 索引手敲

**现在可以开始这一组。** Buffer 必选/实际属性、四处 flush 和 CSV/config 导出已经落地；本组建立 CPU 契约，不要求等待所有 CSM 人工观察记录齐全。接入 B 组 GPU 附件前，仍须按第 1.3 节处理 CSM 收口中发现的问题。

本组只修改下面六个文件；其中三个是新文件。以下代码是**给你手敲的示例**，尚未写入仓库生产源码。示例已在临时目录用当前 Camera 与 GLM 编译并运行测试。

| 文件 | 操作 | 本组职责 |
|---|---|---|
| `src/render/RenderSettings.hpp` | 修改 | 声明模式枚举 |
| `src/render/ShaderTypes.hpp` | 修改 | 声明 160 字节独立整帧布局 |
| `src/render/DeferredMath.hpp` | 新建 | 声明深度重建与 tile 数学接口 |
| `tests/DeferredMathTests.cpp` | 新建 | 固定已知坐标、像素中心、索引与异常输入的检查 |
| `src/render/DeferredMath.cpp` | 新建 | 实现上述数学接口 |
| `CMakeLists.txt` | 修改 | 注册源码与第七个测试目标 |

本组沿用固定契约：RH view 的前方为 -Z；Vulkan depth=[0,1]；projection 已翻 Y；viewport 正高度；16×16 tile、行优先索引。所有测试均为 CPU 数学/布局检查，不是 GPU 深度量化或画质验收。模式只声明，不增加未实现功能的 UI 开关。

### 5.1 先固定模式名称和 CPU 二进制布局

**目的：让后续帧配置、shader 与资源使用同一套名称和字段偏移，避免一边写 G-buffer 一边临时改接口。**

打开 `src/render/RenderSettings.hpp`，在 includes 之后、现有 `enum class ToneMapper` 之前插入：

```cpp
enum class RenderPath : std::uint32_t
{
    Forward = 0,
    Deferred = 1
};

enum class LightCullingMode : std::uint32_t
{
    None = 0,
    Tiled = 1
};
```

打开 `src/render/ShaderTypes.hpp`，找到：

```cpp
static_assert(sizeof(UniformBufferObject) == 1424);
```

在这行后、`DrawPushConstants` 定义前插入：

```cpp
// Deferred 的独立整帧布局；本组只声明，后续接入时再创建、填充和绑定 UBO。
struct alignas(16) DeferredFrameUniforms
{
    alignas(16) glm::mat4 inverseViewProjection{1.0f};
    alignas(16) glm::mat4 inverseProjection{1.0f};
    // width, height, tileCountX, tileCountY
    alignas(16) glm::uvec4 extentAndTiles{0u};
    // tileSize, maxLightsPerTile, cullingMode, debugMode
    alignas(16) glm::uvec4 tileParams{16u, 128u, 0u, 0u};
};

static_assert(offsetof(DeferredFrameUniforms, inverseViewProjection) == 0);
static_assert(offsetof(DeferredFrameUniforms, inverseProjection) == 64);
static_assert(offsetof(DeferredFrameUniforms, extentAndTiles) == 128);
static_assert(offsetof(DeferredFrameUniforms, tileParams) == 144);
static_assert(alignof(DeferredFrameUniforms) == 16);
static_assert(sizeof(DeferredFrameUniforms) == 160);
```

此处是新结构；不要将它的字段塞进旧 `UniformBufferObject`。只有在后续创建/更新/绑定 Deferred UBO 时，才把实际矩阵、extent 与 tile 参数填进去；默认单位矩阵不会产生可用的 Deferred 画面。160 字节断言只证明 C++ 一侧，GLSL 接入时仍须核对相同 std140 布局。

### 5.2 新建数学接口头文件

**目的：把坐标反算和屏幕分块从 Renderer 的 Vulkan 操作中独立出来，先用实际 Camera 验证。**

新建 `src/render/DeferredMath.hpp`，完整写入：

```cpp
#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace deferred
{
inline constexpr std::uint32_t tileSize = 16;

struct TileGrid
{
    std::uint32_t tilesX = 0;
    std::uint32_t tilesY = 0;
    std::uint64_t count = 0;
};

// 正高度 viewport；投影矩阵已翻 Y。这里不再翻转。
glm::vec2 pixelCenterNdc(
    std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height);

// depth 保持 Vulkan [0, 1]，矩阵必须来自同一帧的 inverse(proj * view)。
glm::vec3 reconstructWorldPosition(
    const glm::mat4& inverseViewProjection,
    const glm::vec2& ndcXY, float depth);

// 任一 extent 为零时返回空网格；这里只计算数量，不分配 GPU 资源。
TileGrid makeTileGrid(std::uint32_t width, std::uint32_t height);

// 行优先索引；保留原 extent 参数，以拒绝尾部不完整 tile 内的越界像素。
std::uint64_t tileIndexForPixel(
    std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height);
}
```

`makeTileGrid()` 对零 extent 返回全零；任何具体像素查询都要求非零 extent 且坐标在范围内。这里固定 tileSize=16，不增加任意 tile size 配置。64 位数量仅保证 CPU 算术；未来 GPU 分配/dispatch 仍须检查实际设备限制及字节数乘法。

### 5.3 先写判定正确性的测试

**目的：用独立已知值发现 Y/depth 错误，再检查相机变化与像素边界，避免只做自洽的矩阵往返。**

新建 `tests/DeferredMathTests.cpp`，完整写入以下代码。此时尚未实现函数，先继续 5.4 和 5.5，统一在 5.6 构建：

```cpp
#include "render/DeferredMath.hpp"
#include "render/RenderSettings.hpp"
#include "render/ShaderTypes.hpp"
#include "scene/Camera.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template<class Error, class Action>
void requireThrows(Action action)
{
    try { action(); }
    catch (const Error&) { return; }
    throw std::runtime_error("Expected input rejection");
}

void requirePosition(const glm::vec3& actual, const glm::vec3& expected)
{
    const float tolerance = 1e-4f * std::max(1.0f, glm::length(expected));
    require(glm::length(actual - expected) <= tolerance,
            "Reconstructed world position differs");
}

void testKnownPosition()
{
    Camera camera;
    camera.lookAt({0, 0, 0}, {0, 1, 0});
    camera.nearPlane = 1.0f;
    camera.farPlane = 9.0f;
    camera.fieldOfViewDegrees = 90.0f;
    const glm::mat4 vp = camera.projection(2.0f) * camera.view();

    // 独立已知值：世界点 (-4,2,2) -> NDC (-1,-1,0.5625)。
    const glm::vec4 clip = vp * glm::vec4(-4, 2, 2, 1);
    require(glm::length(glm::vec3(clip) / clip.w -
                        glm::vec3(-1, -1, 0.5625f)) < 1e-5f,
            "Camera convention differs");
    requirePosition(deferred::reconstructWorldPosition(
        glm::inverse(vp), {-1, -1}, 0.5625f), {-4, 2, 2});
}

void testRoundTrips()
{
    const std::array<glm::vec2, 5> corners{{
        {0, 0}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    }};
    for (int pose = 0; pose < 2; ++pose)
    {
        Camera camera;
        if (pose == 0)
            camera.lookAt({0, -5, 2}, {0, 0, 2});
        else
            camera.lookAt({14, -22, 14}, {0, 15, 0});
        camera.nearPlane = 1.0f;
        camera.farPlane = 100.0f;
        camera.fieldOfViewDegrees = 60.0f;

        for (float aspect : {1.0f, 16.0f / 9.0f, 9.0f / 16.0f})
        {
            const glm::mat4 view = camera.view();
            const glm::mat4 vp = camera.projection(aspect) * view;
            const glm::mat4 inverseVP = glm::inverse(vp);
            for (float distance : {1.01f, 7.0f, 99.0f})
                for (const glm::vec2& corner : corners)
                {
                    const float halfY = distance *
                        std::tan(glm::radians(camera.fieldOfViewDegrees) * 0.5f);
                    const glm::vec3 world = camera.position +
                        camera.front * distance +
                        camera.right() * (corner.x * halfY * aspect) +
                        camera.viewUp() * (corner.y * halfY);
                    const glm::vec4 clip = vp * glm::vec4(world, 1.0f);
                    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    const glm::vec3 restored =
                        deferred::reconstructWorldPosition(
                            inverseVP, glm::vec2(ndc), ndc.z);
                    requirePosition(restored, world);
                    const float viewDistance =
                        -(view * glm::vec4(restored, 1.0f)).z;
                    require(std::abs(viewDistance - distance) <= 1e-4f * distance,
                            "Positive view distance differs");
                }
        }
    }
}

void testPixelCenters()
{
    require(glm::length(deferred::pixelCenterNdc(0, 0, 2, 2) -
                        glm::vec2(-0.5f, -0.5f)) < 1e-6f,
            "Pixel center must include half a texel without another Y flip");

    Camera camera;
    camera.lookAt({0, 0, 0}, {0, 1, 0});
    camera.nearPlane = 1.0f;
    camera.farPlane = 100.0f;
    camera.fieldOfViewDegrees = 90.0f;
    for (glm::uvec2 extent : {glm::uvec2(1, 1), glm::uvec2(16, 17),
                              glm::uvec2(1919, 1079)})
    {
        const float aspect = float(extent.x) / float(extent.y);
        const glm::mat4 inverseVP =
            glm::inverse(camera.projection(aspect) * camera.view());
        for (glm::uvec2 pixel : {glm::uvec2(0, 0), extent - glm::uvec2(1),
                                 extent / 2u})
        {
            const glm::vec2 ndc = deferred::pixelCenterNdc(
                pixel.x, pixel.y, extent.x, extent.y);
            // FOV90：near面距离1、far面距离100。由屏幕射线独立计算世界点。
            for (float depth : {0.0f, 1.0f})
            {
                const float distance = depth == 0.0f ? 1.0f : 100.0f;
                requirePosition(deferred::reconstructWorldPosition(
                    inverseVP, ndc, depth),
                    {ndc.x * aspect * distance, distance, -ndc.y * distance});
            }
        }
    }
}

void testTiles()
{
    for (std::uint32_t size : {1u, 16u, 17u})
    {
        const auto grid = deferred::makeTileGrid(size, size);
        const std::uint32_t expected = size == 17 ? 2 : 1;
        require(grid.tilesX == expected && grid.tilesY == expected &&
                grid.count == std::uint64_t{expected} * expected,
                "Small tile grid differs");
    }
    const auto grid = deferred::makeTileGrid(1919, 1079);
    require(grid.tilesX == 120 && grid.tilesY == 68 && grid.count == 8160,
            "Non-divisible extent must round up");
    require(deferred::tileIndexForPixel(15, 0, 17, 17) == 0 &&
            deferred::tileIndexForPixel(16, 0, 17, 17) == 1 &&
            deferred::tileIndexForPixel(0, 16, 17, 17) == 2 &&
            deferred::tileIndexForPixel(16, 16, 17, 17) == 3 &&
            deferred::tileIndexForPixel(1918, 1078, 1919, 1079) == 8159,
            "Tile indexing must be row-major");

    for (glm::uvec2 extent : {glm::uvec2(0, 17), glm::uvec2(17, 0)})
    {
        const auto empty = deferred::makeTileGrid(extent.x, extent.y);
        require(empty.tilesX == 0 && empty.tilesY == 0 && empty.count == 0,
                "Zero extent must produce an empty grid");
    }
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    const auto huge = deferred::makeTileGrid(maximum, maximum);
    require(huge.tilesX == (1u << 28) && huge.tilesY == (1u << 28) &&
            huge.count == (std::uint64_t{1} << 56) &&
            deferred::tileIndexForPixel(maximum - 1, maximum - 1,
                maximum, maximum) == huge.count - 1,
            "Tile arithmetic must not overflow uint32");
}

void testInvalidInputs()
{
    requireThrows<std::invalid_argument>([] {
        (void)deferred::pixelCenterNdc(0, 0, 0, 1);
    });
    requireThrows<std::out_of_range>([] {
        (void)deferred::pixelCenterNdc(17, 0, 17, 17);
    });
    requireThrows<std::out_of_range>([] {
        (void)deferred::tileIndexForPixel(17, 16, 17, 17);
    });
    requireThrows<std::invalid_argument>([] {
        (void)deferred::tileIndexForPixel(0, 0, 1, 0);
    });
    for (float depth : {-0.01f, 1.01f,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity()})
        requireThrows<std::invalid_argument>([=] {
            (void)deferred::reconstructWorldPosition(
                glm::mat4(1.0f), {0, 0}, depth);
        });
    requireThrows<std::out_of_range>([] {
        (void)deferred::tileIndexForPixel(16, 17, 17, 17);
    });
    requireThrows<std::invalid_argument>([] {
        (void)deferred::reconstructWorldPosition(glm::mat4(1.0f),
            {std::numeric_limits<float>::quiet_NaN(), 0}, 0.5f);
    });
    requireThrows<std::invalid_argument>([] {
        glm::mat4 matrix(1.0f);
        matrix[0][0] = std::numeric_limits<float>::infinity();
        (void)deferred::reconstructWorldPosition(matrix, {0, 0}, 0.5f);
    });
    requireThrows<std::range_error>([] {
        (void)deferred::reconstructWorldPosition(
            glm::mat4(0.0f), {0, 0}, 0.5f);
    });
    requireThrows<std::range_error>([] {
        glm::mat4 matrix(1.0f);
        matrix[0][0] = std::numeric_limits<float>::max();
        (void)deferred::reconstructWorldPosition(matrix, {2, 0}, 0.5f);
    });
    glm::mat4 distant(1.0f);
    distant[3][3] = 1e-8f;
    require(deferred::reconstructWorldPosition(distant, {1, 0}, 0.5f).x > 1e7f,
            "Small nonzero W must not be rejected by an arbitrary epsilon");
}

void reportWideDepthPrecision()
{
    Camera camera;
    camera.nearPlane = 0.1f;
    camera.farPlane = 1000.0f;
    camera.lookAt({0, 0, 0}, {0, 1, 0});
    const glm::mat4 vp = camera.projection(1.0f) * camera.view();
    const glm::vec3 world(0, 990, 0);
    const glm::vec4 clip = vp * glm::vec4(world, 1);
    const glm::vec3 restored = deferred::reconstructWorldPosition(
        glm::inverse(vp), {0, 0}, clip.z / clip.w);
    std::cout << "Float precision diagnostic (near=0.1, far=1000): error="
              << glm::length(restored - world) << '\n';
}
}

int main()
{
    try
    {
        testKnownPosition();
        testRoundTrips();
        testPixelCenters();
        testTiles();
        testInvalidInputs();
        reportWideDepthPrecision();
        std::cout << "Deferred math tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

已知点使用相机原点朝 +Y、FOV90、aspect2、near1/far9：世界点 (-4,2,2) 对应 NDC (-1,-1,0.5625)。常规往返用 near1/far100 和严格相对容差。像素中心测试用解析屏幕射线；没有将连续投影坐标取整后仍要求精确回到原点。

宽深度 near0.1/far1000 单独输出 float 误差诊断：本机该示例的误差约 0.777344。**这不是断言通过的精度标准，也不是 GPU 画质结论。** 不要把常规 `1e-4` 容差机械套在全部 near/far 范围，更不要只为让压力用例通过而放宽所有测试。

### 5.4 实现重建与索引

**目的：落实本组契约：只做一次齐次除法，保留 Vulkan 深度，避免 tile 向上取整和索引乘法溢出。**

新建 `src/render/DeferredMath.cpp`，完整写入：

```cpp
#include "render/DeferredMath.hpp"

#include <cmath>
#include <stdexcept>

namespace deferred
{
namespace
{
void validatePixel(std::uint32_t x, std::uint32_t y,
                   std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
        throw std::invalid_argument("Pixel requires nonzero extent");
    if (x >= width || y >= height)
        throw std::out_of_range("Pixel is outside extent");
}
}

glm::vec2 pixelCenterNdc(
    std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height)
{
    validatePixel(x, y, width, height);
    return {
        (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f,
        (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f
    };
}

glm::vec3 reconstructWorldPosition(
    const glm::mat4& inverseViewProjection,
    const glm::vec2& ndcXY, float depth)
{
    if (!std::isfinite(ndcXY.x) || !std::isfinite(ndcXY.y) ||
        !std::isfinite(depth) || depth < 0.0f || depth > 1.0f)
        throw std::invalid_argument("Invalid NDC or Vulkan depth");

    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(inverseViewProjection[column][row]))
                throw std::invalid_argument("Inverse matrix must be finite");

    const glm::vec4 worldH =
        inverseViewProjection * glm::vec4(ndcXY, depth, 1.0f);
    if (!std::isfinite(worldH.w) || worldH.w == 0.0f)
        throw std::range_error("Invalid homogeneous W");

    const glm::vec3 world = glm::vec3(worldH) / worldH.w;
    if (!std::isfinite(world.x) || !std::isfinite(world.y) ||
        !std::isfinite(world.z))
        throw std::range_error("Reconstructed position must be finite");
    return world;
}

TileGrid makeTileGrid(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
        return {};

    TileGrid grid{};
    // 不写 (width + tileSize - 1)，避免 uint32 加法溢出。
    grid.tilesX = width / tileSize + (width % tileSize != 0);
    grid.tilesY = height / tileSize + (height % tileSize != 0);
    grid.count = std::uint64_t{grid.tilesX} * grid.tilesY;
    return grid;
}

std::uint64_t tileIndexForPixel(
    std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height)
{
    validatePixel(x, y, width, height);
    const TileGrid grid = makeTileGrid(width, height);
    return std::uint64_t{y / tileSize} * grid.tilesX + x / tileSize;
}
}
```

重建的核心就是：

```cpp
inverseViewProjection * glm::vec4(ndcXY, depth, 1.0f)
```

这里没有第二次 Y 翻转，也没有 `2*depth-1`。矩阵必须是生成该张 depth 的同帧 `inverse(proj * view)`；CPU helper 不替调用方证明这一点。没有把 `abs(w)<1e-6` 当作统一非法条件，合法远点可能有很小的非零 w。

### 5.5 把新文件加入构建

**目的：让新代码进入主程序和 CTest，避免文件已经手敲但工程没有编译它。**

打开根目录 `CMakeLists.txt`，做两处修改。

第一处，在 `add_executable(vulkan ...)` 的 `# Frame recording and render passes` 源码组中，紧接 `src/render/ShaderTypes.hpp` 加入：

```cmake
    src/render/DeferredMath.hpp
    src/render/DeferredMath.cpp
```

第二处，在现有 `if(BUILD_TESTING)` 内，紧接 `shadow_cascade_math_tests` 的 `add_test(...)` 后、`add_executable(gltf_loader_tests ...)` 前，插入：

```cmake
    add_executable(deferred_math_tests
        tests/DeferredMathTests.cpp
        src/render/DeferredMath.cpp
        src/scene/Camera.cpp
    )
    target_include_directories(deferred_math_tests PRIVATE src)
    target_link_libraries(deferred_math_tests PRIVATE glm::glm)
    add_test(NAME deferred_math_tests COMMAND deferred_math_tests)
```

该目标只依赖 GLM 和 Camera，不需要链接 Vulkan 或启动窗口。Camera 已显式使用 RH_ZO，不必给这个目标补全局 depth 宏。

### 5.6 构建并检查这一组的完成点

**目的：确认新数学契约可用、旧测试仍通过，现有 Forward 渲染没有被破坏。**

在项目根目录依次执行；一条失败时先处理该条，不继续用后面的成功覆盖它：

```bash
cmake --preset linux-debug -DBUILD_TESTING=ON
cmake --build --preset linux-debug --target deferred_math_tests -j 2
ctest --test-dir build/linux-debug -R '^deferred_math_tests$' --output-on-failure
cmake --build --preset linux-debug -j 2
ctest --test-dir build/linux-debug --output-on-failure
./build/linux-debug/vulkan --scene shadow-playground --frames 30
```

新测试应为 1/1 通过，完整测试应由原 6 项增加到 **7/7**。需要看精度诊断原始输出时执行：

```bash
./build/linux-debug/deferred_math_tests
```

本组不需要编译新 shader。程序画面仍走当前 Forward；没有 Deferred 切换按钮是预期结果，不代表漏做 UI。

**本组完成点：** 六个文件按上述接入；160 字节断言与七项测试通过；Forward 启动/退出正常。接下来是 B 组的 MRT 管线与单采样 G-buffer；先核对第 1.3 节剩余 CSM 证据及未解释问题，再接新 GPU 资源。

## 6. 提交 B：MRT、单采样资源与 Geometry

**目的：先看到可靠的表面数据，暂不做光照和剔除。** 主要位置：`Renderer.hpp`、`GraphicsPipeline.cpp`、`PostProcessing.cpp`、拟增 `DeferredRendering.cpp`、`gbuffer.frag`。

1. 将 Pipeline 的单个颜色格式改为列表，颜色 blend 状态与 `VkPipelineRenderingCreateInfo` 使用同一数量和顺序；已有单色/零色路径仍测试。G-buffer 的四个 blend attachment 全部关闭 blend，samples=1。
2. 每槽创建完整 `GBufferTarget`（四颜色 + depth），检查能力、初始化失败清理与显式销毁；不要把现有 4x Scene depth 当可直接采样的 1x depth。
3. Geometry 复用 RenderObjectView、材质 descriptor、128 字节 push constants 与 Opaque/Mask/单双面选择；材质求值从 Forward 抽共享函数，先保持原输出不变。
4. 在独立 begin/end rendering 中按 location 0–3 写 MRT；clear、store、layout、viewport/scissor 与实际 extent 匹配，所有后续要采样的附件都 STORE。
5. 增加附件查看路径，将选中附件转换为 1x HDR 后进入已有 Post；数据视图沿用 bypass tone mapping 规则，不能把 raw normal 当作 Lit 色彩处理。

首版在每槽独立保留 `ForwardTarget` 与 `DeferredTarget`，后者包含 G-buffer 及 1x Lighting HDR；不让 Deferred 的样本数改写 Forward 的 `sceneSamples_`。Post 按本槽记录的 render path 选择对应 HDR descriptor（分别分配两套，避免更新仍在使用的 set），内存统计包含两套实际分配。后续若只保留活跃目标，在安全重建边界再优化。

**观察/验收：** 地面和球体的法线/深度正确；Helmet 法线贴图/Emissive、材质球 M/R、Mask 孔洞及双面背面与 Forward 一致；resize 后五个 view 与 descriptor 全部更新。未做的视觉用例保持未勾选。

## 7. 提交 C：Deferred Lighting，先逐像素全灯

**目的：形成可与 Forward 对照的完整画面。** 新增 `deferred_lighting.frag`，复用 `fullscreen.vert`；扩展帧数据、descriptor 与录制分支。

建议 descriptor 契约如下；它是拟新增接口，Geometry 的 set 1 继续是材质，Lighting/Compute 使用自己的 pipeline layout，不错误绑定材质 set：

| set/binding | 内容 | 使用阶段 |
|---|---|---|
| set 0 / 0–5 | 复用 frame UBO、IBL、CSM/preview 原绑定 | 按实际读取；后续 Compute 若读 view，UBO 加 Compute 可见 |
| set 0 / 6 | 后续 D 组 light SSBO | Fragment / Compute |
| set 1 / 0–4 | depth / normal / base color / material / emissive 的非比较采样 | Fragment；depth、normal 另对 Compute 可见 |
| set 1 / 5 | `DeferredFrameUniforms` UBO | Fragment / Compute |
| set 1 / 6、7 | 后续 E/F 组 tile headers、light indices SSBO | Fragment / Compute |
| set 1 / 8 | 与现有 Skybox 同源的 environment cubemap | Fragment |

D/E 组之前可以分步加入后续绑定；若提前定义，则写入合法的占位资源，不留下会被访问的未初始化 descriptor。Deferred 输入与 tile 资源由 Renderer 管理；light buffer 由 Application 按槽更新，沿用现有帧资源所有权。布局、pool 与 shader 必须一起修改。

1. 每槽写入匹配 G-buffer 的逆矩阵，重建 worldPosition；读取/归一化 shading normal，对量化后的 M/R/AO 重施相同 clamp（roughness 至少 0.04），复用同一 BRDF、点光衰减、方向光、CSM visibility、IBL/AO/Emissive。
2. 第一版仍使用当前最多 16 灯的数据，先排除 SSBO/剔除变量。CSM 选择/混合函数改为接受 worldPosition，两条着色路径共享；不得在 Deferred 另写一套 bias 或 split 规则。
3. `gNormal.a==0` 表示无表面：Lit/HDR luminance 输出同源天空盒，方向由相机及逆矩阵求得，不带相机平移；表面/附件 Debug View 输出明确背景色。天空盒不写 G-buffer，不参与 tile 深度范围。
4. Lighting 为无顶点输入、无深度测试/写入、单个 HDR 颜色附件、samples=1，完全写入每个像素；之后复用 Post/Present/ImGui 和阴影预览。
5. 暴露可用的 Forward/Deferred 开关；将实际路径/采样数随提交快照记录。Forward 可保留原 MSAA，Deferred 永远报告实际 1x。
6. 为公平对照新增 `forceForwardSingleSample` 请求设置，默认 false 保持现有 4x/2x/1x 能力选择；true 时选择经过查询支持的 1x。UI 只排队请求，在无 active frame 且所有在途帧完成后重建 Forward color/depth/resolve 目标、scene/skybox pipeline 和相关 descriptor；失败保留原配置。分别保存请求和实际 samples，切换取消当前采集，Deferred 不受该设置影响。

**验收：** 固定相机/灯光、先关闭阴影/IBL 逐项对照，再开启方向光、CSM、IBL。比较内部表面区域与线性 HDR，边缘 4x/1x 差异单列；严格比较时另建可用的 Forward 1x 配置，不把 Deferred 设成 4x。显示阶段仍只有一次 sRGB 编码。

## 8. 提交 D：GPU light buffer 与可重复全灯基线

**目的：先解除 16 灯限制，保留正确性参照。** 修改 `Lighting.hpp`、`ShaderTypes.hpp`、`Buffers.cpp`、`Descriptors.cpp`、相关 shader、Scene Preset/UI。

**D/E 的前置小步：落实 Buffer 映射内存契约。** 扩展 `BufferDesc` 的 host access 意图（SequentialWrite / Readback），将 `requiredMemoryProperties` 写入 `VmaAllocationCreateInfo::requiredFlags`；Readback 采用 `VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT`，不一律按 sequential write 分配。查询并保存实际 allocation 的 memory property flags，不把请求值当结果。
在 `GpuBuffer` 增加 allocation 相对范围的 `flush(offset,size)` / `invalidate(offset,size)`，内部用 `vmaFlushAllocation` / `vmaInvalidateAllocation` 并检查错误；coherent 类型可走 VMA 的无操作路径，非 coherent 类型也必须正确。回归已有 UBO/staging 映射写入、失败清理和上传，不只验证新 SSBO。VMA 负责 atom 对齐；不能用 unmap 代替 flush。依据见 [VMA Memory Mapping](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html)。

1. 沿用现有 `GpuPointLight` 的三个 vec4、48 字节 stride：`position.xyz` 世界位置；`color.rgb/w` 颜色/强度；`params.x/y` range/enabled。std430 SSBO 与 CPU 的 stride、offset 做断言。不在此时换成另一种物理衰减。
2. 每帧槽持有自己的映射 light SSBO，在该槽 fence 完成后写入；仅打包 enabled 的灯，`frame.lightCounts.x` 是实际打包数。range 继续按现有 `max(range, 0.0001)` 求有效范围，有限非负强度/颜色在上传前检查。
3. 初版分配 1024 灯容量，活动数可为 0–1024，按 `maxStorageBufferRange` 等设备限制确认可用；超过显式容量时清楚拒绝，不用 `min(count,16)` 静默截断。后续动态增长独立处理，不在 GPU 使用中替换 buffer/descriptor。
4. Forward 和 Deferred None 都改从同一 SSBO 遍历全部活动点光源；迁出 UBO 旧数组后同步 vertex/fragment/shadow/skybox 等所有共享布局与静态断言。不能只改两个主 fragment shader。
5. 沿用点光衰减：`attenuation = clamp(1-distance/range,0,1)^2`，range 之外贡献为 0。Tiled 的球半径必须用同一个有效 range；方向光单独计算，不进入点光列表。
6. 拟增 `many-lights` 预设：固定 seed `0x5EED`、确定性生成算法、场景尺度/相机/灯高，默认静止；测试 0/1/16/64/256/1024 灯及小/中/大覆盖范围。显式修改 `ScenePresets.hpp` 注册表、CLI、文档与预设测试；不改变已有场景默认灯光。

**验收：** Forward/Deferred None 对相同活动光源集合给出一致光照；第 17 盏灯确实有贡献；0 灯、disabled、range 边界和容量上限有明确结果。先归档全灯基线，再增加 Compute。

## 9. 提交 E/F：Compute Tiled 与可靠的溢出回退

### E：先建立队列、列表和全包含正确性版本

首版在现有 graphics queue 上录制 dispatch，必须查询该 family 含 `VK_QUEUE_COMPUTE_BIT`，检查 local size/workgroup invocations、shared memory、storage buffer 与 dispatch limits。没有能力时禁用 Tiled，Deferred None 仍可用；Stage 2 全收口仍需要在支持设备验证 Tiled。

建议 16×16 像素/tile，`local_size_x=16, local_size_y=16`，一 workgroup 对应一个 tile。若所选设备无法满足此配置，首版明确禁用该模式，不默默改变 CPU/GPU 索引。

```text
tileCountX = ceil(width / 16)，tileCountY = ceil(height / 16)
tileId = tileY * tileCountX + tileX
每 tile 固定 128 个 uint 索引：base = tileId * 128
TileHeader = uvec4(storedCount, overflowFlag, reserved, reserved)，stride=16
```

每槽分配 `tileCountX*tileCountY` 个 header 和其 128 倍索引，VkDeviceSize 做乘法溢出、范围与设备限制检查。第一版不用全局 append allocator，不增加 compact/prefix sum。

先让 Compute 将所有活动灯视为候选，验证 pipeline、descriptor、写入/读取与溢出；这一步成本可能更高，不宣称剔除已完成。每次 dispatch 重写所有 tile 的 header，索引只读到 storedCount；0 灯/空 tile 也写有效 header，不依赖上帧内容。

候选写入规则：shared 候选计数用原子操作取得 slot；仅 `slot<128` 写入，结束时 `storedCount=min(candidateCount,128)`，超限置 overflow。**overflow tile 的 Lighting 完整遍历 light SSBO，而不是只用截断列表**；这样不漏灯、不重复计算，也不越界。可视化 overflow，并在同槽 fence 后读回统计，避免每帧同步等待。

### F：保守剔除，再逐步缩小候选集

1. 一线程处理 tile 内一个像素，用 `gNormal.a` 判有效；越界/背景不参与正视空间深度 min/max。所有线程仍经过 workgroup barrier，不能让边缘线程提前 return 造成分歧 barrier。
2. 用 inverseProjection、当前投影 Y 约定和 tile 的像素边界构造四侧 view-space 平面，朝向向内且法线归一化；右/下边缘 clamp 到实际 extent。深度只取有效表面，得到正距离 `[minDepth,maxDepth]`；无有效像素的 tile 输出 count=0/overflow=0。
3. 将点光源中心用本帧 view 变换到 view space，用影响球测试四侧平面与深度 slab。保留相切、跨 near plane、包含相机及边界灯；不以球中心在屏幕外/相机后就整灯剔除。
4. 可先只做四侧平面保守测试，再加深度 slab；每加一个收紧条件都与 None 比较。深度跨度大的 tile 允许假阳性，不能出现影响当前不透明表面的假阴性。
5. Lighting 由整数 pixel 求 tileId，非 overflow 遍历 storedCount 索引，再用同一逐灯公式；overflow 遍历全部灯。候选顺序可能不同，比较允许浮点累加舍入差异，不允许成块漏灯。

**验收：** 强制将每 tile 容量降为 1 或放置 >128 盏重叠灯，观察 overflow 可见且结果与 None 相同；同时检查 0 灯、空 tile、非整除分辨率、灯球与平面相切、快速移动灯/相机。首版只覆盖当前 Opaque/Mask 表面，不拿此列表给后续透明 pass 直接用。

算法思想参考 [AMD TiledLighting11](https://github.com/GPUOpen-LibrariesAndSDKs/TiledLighting11)；它演示 Forward+ 与 Tiled Deferred。这里只借鉴列表/保守剔除，Vulkan 资源与同步按本项目契约实现。

## 10. 所有组共用的资源与同步表

以下采用 Synchronization2，所有生产/消费都在同一 queue；图像/缓冲的实际 subresource/range 必须覆盖本次访问，queue family 使用 IGNORED。Dynamic Rendering 不替代这些依赖。

| 资源与依赖 | 生产者 stage/access | 消费者 stage/access / layout |
|---|---|---|
| G-buffer 四颜色写 → Lighting，normal 另供 Cull | COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_WRITE | FRAGMENT_SHADER，normal 另含 COMPUTE_SHADER / SHADER_SAMPLED_READ；COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL |
| G-buffer depth 写 → Cull/Lighting | EARLY_FRAGMENT_TESTS + LATE_FRAGMENT_TESTS / DEPTH_STENCIL_ATTACHMENT_WRITE | COMPUTE_SHADER + FRAGMENT_SHADER / SHADER_SAMPLED_READ；DEPTH_STENCIL_ATTACHMENT_OPTIMAL → DEPTH_STENCIL_READ_ONLY_OPTIMAL |
| Cull headers/indices 写 → Lighting 与后续 host 统计 | COMPUTE_SHADER / SHADER_STORAGE_WRITE | FRAGMENT_SHADER / SHADER_STORAGE_READ；若映射读回还需 HOST / HOST_READ，且等本槽 fence |
| Lighting HDR 写 → Post 采样 | COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_WRITE | FRAGMENT_SHADER / SHADER_SAMPLED_READ；COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL |
| 槽位下一轮重新写入附件/列表 | 上次实际 sampled/storage read | 本次相应 color/depth/storage write；按布局跟踪处理复用，不能省掉写前依赖 |

Shadow → Lighting 沿用四层采样依赖，必要时扩展消费阶段；light/uniform 的 host 写发生在槽 fence 后，非 coherent 映射按 VMA 规则 flush。设备写入的非 coherent host 读回须 invalidate；不能把 CPU 读取映射内存当作已有 fence/barrier。

首版可将小型 tile header buffer 分配为 host-visible，以 fence 后统计 overflow/候选分布；记录其 VMA 分配成本。若改 device-local + staging readback，另加 transfer usage、Compute→Transfer→Host 依赖，不照搬表中的直接 host 路径。

首次使用从 UNDEFINED 进入附件；后续从实际旧布局转换。若丢弃内容而使用 oldLayout=UNDEFINED，仍要确保先前读取已结束，不能用“不要旧内容”替代执行依赖。同步规则参考 [Khronos Synchronization Examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。

## 11. 提交 G：调试、计时、采集与内存

**目的：能解释正确性和总成本，不只展示热图。**

1. 保留已有 DebugView 数值，扩展 G-buffer validity/AO/Emissive、重建位置误差检查、tile 光源数和 overflow 视图；热图提供固定图例，overflow 独立颜色，表面视图的背景明确。
2. 建议 pass 枚举为 Shadow、Scene（仅 Forward）、Geometry、LightCull、Lighting、PostAndUi，加独立总区间 `FrameGpu`：六对 pass query + 一对总 query，每槽共 14 个位置。总区间从本帧首个资源 barrier 前到最后 Present 转换后，标签说明包含范围。
3. Forward 的 Geometry/Cull/Lighting 标为 NotExecuted；Deferred 的 Scene 标为 NotExecuted；None 模式的 Cull 标为 NotExecuted。实际 clear-only 的 Shadow 仍为 Ready。扩大 query pool、reset/readback、mask、UI、CSV 和测试，不覆盖原 query 索引。
4. `FrameGpu` 是 GPU 命令区间，不包括 CPU fence/acquire/present 等待；若比较各子区间之和，标注“pass 区间之和”。不通过三段或五段简单相加冒充完整 GPU 帧耗时，避免与嵌套总区间重复相加。
5. 保留 submissionId 配对、120 次提交预热、300 有效样本；`BaselineCapture` 从“所有固定 pass 必须 Ready”改为“按当前模式所需 pass + 总区间验证”，NotExecuted 不造 0。扩展计时数据结构，移除固定三列循环/硬编码。
6. 快照与取消条件增加 render path、实际 samples/格式/extent、剔除模式、tile 尺寸/容量、活动灯数、预设 seed/参数或光源 revision。对象/材质/相机/灯光的未自动跟踪项仍人工固定；本阶段不顺带开发场景保存系统。
7. 为每份 CSV 保存同名配置旁置文件（首版可人工填写，但必填项不能为空），包括第 1.3 节条件及灯光分布/数量、G-buffer 格式、tile/overflow、显示/曝光/UI。当前 CSV 缺 metadata 的问题不能延续到最终性能报告。

首选格式 G-buffer 名义 payload 为 `8+4+4+8+4=28 bytes/pixel/frame-slot`。以 1920×1080、两槽为例约 **110.74 MiB**，另算 HDR、Shadow 与 tile/light buffer。16×16 分块为 120×68=8160 tiles，每槽 header 为 130560 B、indices 为 4177920 B；均乘实际槽数后再汇总。

内存按**实际分配**求和。初次接入为降低切换风险可以同时保留两套目标，但必须把空闲 Forward MSAA/HDR/depth 也计入；最终 A/B 可改为无 active frame、等待在途帧后重建活跃目标，不在 UI 回调中直接销毁。Single/CSM 当前都分配四层，同样据实记录；payload、VMA allocation/block 与设备预算分别命名。

## 12. 提交 H：运行验收与性能对照

每组先构建、运行有意义的 CPU 测试和对应 GPU 用例；新增/共享 shader 有变动时编译两平台脚本对应项，并同步构建资源。以下命令用于最终 Linux 回归，其他平台只记录实际执行结果：

```sh
./scripts/compile-shaders.sh
cmake --build --preset linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
./build/linux-debug/vulkan --scene shadow-playground --frames 120
cmake --build --preset linux-release --parallel
ctest --test-dir build/linux-release --output-on-failure
```

CLI 的 120 帧不是采集，也不会自动切模式；Forward/Deferred/Tiled、热图、overflow、窗口操作等仍按实际入口运行并记录。普通 Validation 与同步验证设置分开记。

| 用例 | 固定条件与观察 | 通过标准 |
|---|---|---|
| 坐标/附件 | near/far、角落、不同 aspect、1920×1080 与 1919×1079 | 重建方向/深度正确，无重复 Y 翻转或边缘越界 |
| 材质/背景 | Material Lab、Helmet、实际 Mask/双面资产，逐项开 CSM/IBL | Forward/Deferred 内部表面匹配；孔洞/背面/天空盒语义一致 |
| 多光源正确性 | 0/1/17/64/256/1024 灯、范围边界、相机内灯球、None/Tiled | 没有漏灯；超容量有明确报错，列表溢出正确回退 |
| 生命周期 | 两帧槽连续运行，反复切渲染模式/预设、resize、最小化恢复、退出/部分初始化失败 | descriptor、view、buffer 无悬空，无未解释的 Validation/同步错误 |
| 公平对照 | 相同场景/相机/灯集合/曝光/UI/阴影数，注明实际 samples | 单独解释抗锯齿和附件量化差异，不能改变灯光半径再声称剔除提速 |
| 性能 | 不同灯数 × 小/中/大覆盖率，None 对 Tiled，先预热再 300 行 | 记录 Cull、Lighting、FrameGpu、CPU Update/Record、draw、payload/VMA、overflow；允许低灯数负收益 |

报告至少比较 Forward 全灯、Deferred None、Deferred Tiled。Forward 可额外报原 MSAA 结果；算法成本的严格 A/B 使用相同 1x 条件。若保留备用路径资源，注明两模式内存为何没有随切换下降。Tiled 列表只服务当前不透明表面；Clustered 加 Z 分层可减少深度跨度引起的假阳性，但是否继续做由实测决定。

## 13. 收口清单与下一次最小任务

下面均为**未来验收项**，仅实际完成后勾选：

- [ ] 第 1.3 节 Stage 1 收口记录已补，未执行的平台/工具单列，未把冒烟结果写成完整视觉验收。
- [ ] Forward 保留，Deferred 使用 1x；MRT/格式/descriptor、坐标与 CPU/GLSL 布局一致。
- [ ] Opaque/Mask、单双面、normal map、CSM、IBL、Emissive、背景与 Post 的语义通过对照。
- [ ] 可扩展 SSBO 支持 >16 盏灯，保留完整全灯基线，容量/启用/range 定义一致。
- [ ] 一种 Compute Tiled 完成，边缘/空 tile、相切/近面/相机内灯球正确；overflow 可见并完整回退。
- [ ] Buffer 必选/实际内存属性与 flush/invalidate 契约已落实；写入→采样、Compute→Fragment、槽位复用和 host 读回同步正确，无新增逐帧等待。
- [ ] 附件视图、热图、各 pass/总 GPU 区间、配置取消、ID 配对、CSV/旁置配置及实际分配统计齐全。
- [ ] 不同灯数/覆盖率的画质、原始性能样本与关键帧说明已保存，能解释提速或负收益。
- [ ] resize、最小化恢复、场景/模式切换、失败清理和退出有实际检查结果。

收口后进入 Stage 3 的 motion vectors/history/TAA。深度、法线与表面数据可复用，但本阶段输出**不是历史资源**：没有 previous transform、jitter 或重投影有效性，不能提前宣称 TAA 输入链完整。

**下一次只手敲 A：深度重建与 tile 索引的 CPU 契约。** 当前窗口图像与 Forward 路径保持可运行；之后先完成 Stage 1 收口，再做 B 的 MRT/单采样 G-buffer，不一次加入 Lighting、SSBO 和 Compute。
