# 阶段 7：HDR 主渲染目标与后处理实施指南

本文对应 [开发路线图](overview.md) 的 Stage 7，接续 [Stage 6：方向光与阴影](stage-6-directional-light-shadows.md)。面向手敲学习：每次只完成一个能编译、能解释、能观察的提交，不一次性改完所有 pass。

评估日期：2026-09-20。代码基线：`08a3a2f`（`Optimized imgui layout`）；阴影、PCF 与深度预览基线：`2290222`。本文中的 Stage 7 类型和接口是实施建议，**不是已经实现的 API**。本次仅检查源码和本地依赖头文件，没有重新执行构建、图形交互或 Validation 验收。

## 1. 当前项目在哪里

| 内容 | 当前代码证据 | 判断 |
|---|---|---|
| 方向光、阴影、bias、PCF | `Buffers.cpp`、`ShadowMapping.cpp`、`shader.frag` | 功能链路已存在；质量和同步仍需运行验收 |
| 原始阴影深度预览 | `recordShadowPreview()`、`shadow_debug.vert/.frag` | 已有；迁移后应留在显示端，不经过曝光/Bloom |
| ImGui 分组 | `ImGuiLayer.cpp::drawImGui()` | Scene / Inspector / Lighting / Shadows / Camera / Diagnostics |
| 场景渲染目标 | `Renderer::createRenderPass()`、`Swapchain::createFramebuffers()` | MSAA color、depth、swapchain resolve 三附件；不是离屏 HDR 场景目标 |
| 模型 tone mapping | `shader.frag::main()` 末尾 | `color / (color + 1)` 在材质 shader 内执行 |
| 天空盒 tone mapping | `skybox.frag::main()` | 单独执行同样的 Reinhard 映射 |
| Swapchain 格式 | `Swapchain::chooseSurfaceFormat()` | 优先 `B8G8R8A8_SRGB + SRGB_NONLINEAR`，不支持时返回列表首项，不能假定永远 sRGB |
| MSAA | `VulkanContext::getMaxUsableSampleCount()` | 查询的是设备级 color/depth 上限，尚未核对新 HDR 格式对应的 sampleCounts |
| Resize | `TriangleApplication::recreateSwapChain()` | 重建 swapchain/framebuffer；format 变化直接报错；尚无离屏资源和 post descriptor 重建 |
| shader 资产同步 | `scripts/compile-shaders.*`、CMake `POST_BUILD` | 单独编译 shader 不保证触发运行目录复制，已发生过读到旧 SPIR-V/缺新文件的问题 |

结论：**Stage 6 核心代码具备进入下一阶段的基础，但不能据此把全部验收勾成完成。** 先记录一次硬阴影、PCF、深度预览、resize/minimize/restore、glTF 回归和正常退出结果。有黑屏、非法 descriptor 或资源在用销毁错误时，先修复，不用 HDR 改造掩盖它们。

## 2. 这一阶段解决什么，不解决什么

当前虽然已经加载 HDR 环境图，但场景在各 shader 内提前压缩亮度，再写交换链。真正要建立的是：**整个场景先保存线性、可大于 1 的辐亮度，再统一做后处理和显示转换。** HDR 中间目标不等于开启显示器 HDR10；本阶段最终仍输出 SDR sRGB。

分两个验收门槛，避免把“接通 HDR”误报成“完整 Stage 7”：

- **Gate A：HDR 基础链路。** 离屏 HDR、正确的 MSAA/resolve、统一曝光与 tone mapping、单次 sRGB 编码、UI 不受曝光影响、resize 与释放安全。
- **Gate B：路线图后处理与调试。** 可独立开关的 Bloom、FXAA，以及 Albedo / World normal / Metallic / Roughness / AO / Irradiance / Prefilter mip / BRDF LUT / Shadow map / HDR luminance 调试视图。

TAA、SSAO、CSM、动态阴影分辨率、Render Graph、异步计算、自动曝光、HDR10 输出不在本轮。不要为了未来可能有十种 pass 先写通用框架。

## 3. 目标链路和所有权

Gate A：

```text
等待当前 frame fence / acquire
  → 更新 UI、当前帧 UBO 与绘制快照
  → Shadow pass：写当前 frame 的 shadow depth
  → HDR scene pass：天空盒 + 模型 → 当前 frame 的 hdrColor
  → Present pass：曝光 + tone mapping → swapchain image
       → 阴影/其他调试叠图 → ImGui
  → submit / present
```

启用 MSAA 时，scene pass 先写 multisample color，再 resolve 到 **单采样 HDR 图**，不是 resolve 到交换链。

| 资源 | 所有者 | 索引 | 是否随窗口尺寸重建 |
|---|---|---|---|
| HDR color、可选 MSAA color、scene depth、scene framebuffer | Renderer | `token.frameIndex` | 是 |
| post descriptor set、Bloom/FXAA 中间图 | Renderer | `token.frameIndex` | 图变化后更新 descriptor；不可更新正在使用的 set |
| swapchain image/view、present framebuffer、renderFinished semaphore | Swapchain | `token.imageIndex` | 是 |
| shadow targets、shadow framebuffer、shadow descriptors | 维持当前 Renderer / 应用分工 | `token.frameIndex` | 固定分辨率，普通 resize 不重建 |
| Frame/Material/Skybox sets | 应用现有 descriptor pool | 当前帧 / 材质 | 不因 HDR 尺寸变化整体重建 |

不要让多个并行帧共用一张 HDR image/depth；不要用 `imageIndex` 去访问长度为 `MAX_FRAMES_IN_FLIGHT` 的数组。

### 3.1 文件分工

- `Renderer.hpp`：新增 scene/present pass、HDR target、post layout/pool/sets/pipeline、resize 接口。
- 建议新增 `src/PostProcessing.cpp`：HDR targets、后处理 descriptor、pass 创建和命令录制；加入 CMake `vulkan` target。
- `GraphicsPipeline.cpp`：让 pipeline 显式选择 render pass 与 samples。
- `Swapchain.hpp/.cpp`：最终只管理交换链和单附件 present framebuffer，移除旧 scene color/depth 的所有权。
- `RenderTypes.hpp`：新增 CPU 后处理设置及独立 post push constants；不挤占已有 128 字节 `DrawPushConstants`。
- `TriangleApplication.cpp`：设置快照、初始化和 resize 编排；不直接维护 Renderer 的 HDR image。
- `ImGuiLayer.cpp`：增加 Rendering 标签，ImGui Vulkan backend 改到 present pass。
- `assets/shaders/`：`fullscreen.vert`、`tonemap.frag`；Gate B 再加 Bloom、FXAA、debug shader。
- `AppConfig.hpp`、`scripts/compile-shaders.sh/.bat`：只注册已存在的 shader。

## 4. 提交 1：让 pipeline 显式指定输出目标

提交名：`Make graphics pipeline targets explicit`

**目的：先拆开当前 helper 对主 render pass 和全局 MSAA 的隐式依赖，画面应保持不变。**

在 `Renderer::GraphicsPipelineConfig` 增加：

```cpp
VkRenderPass renderPass = VK_NULL_HANDLE;
VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
```

修改 `createGraphicsPipelineFromConfig()`：

```cpp
// 检查 config.renderPass，而不是只检查 renderPass_。
multisampling.rasterizationSamples = config.samples;
pipelineInfo.renderPass = config.renderPass;
```

在当前 scene、skybox、shadow-preview 三个调用方都显式填写原来的 `renderPass_` 和 `context_->msaaSamples()`，保持行为不变。depth-only shadow pipeline 是独立创建函数，不要强行合并进这个 helper。

本提交不删除任何 tone mapping，不改 Swapchain 附件。验收：主画面、阴影预览、ImGui 与修改前一致，Debug 可编译。

## 5. 提交 2：创建每帧 HDR 资源

提交名：`Create per-frame HDR scene targets`

**目的：先完成正确的资源格式、采样数、所有权与异常清理，再接入实际绘制。** 此时允许资源暂未使用，画面不变；下一提交完成路由切换。

建议结构：

```cpp
struct HdrFrameTarget
{
    GpuImage hdrColor;  // 始终单采样；后处理读取此图
    GpuImage msaaColor; // samples > 1 时才创建
    GpuImage depth;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

std::array<HdrFrameTarget, MAX_FRAMES_IN_FLIGHT> hdrTargets_{};
VkFormat hdrFormat_ = VK_FORMAT_UNDEFINED;
VkFormat sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
VkSampleCountFlagBits sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;
VkRenderPass sceneRenderPass_ = VK_NULL_HANDLE;
VkRenderPass presentRenderPass_ = VK_NULL_HANDLE;
GpuSampler postSampler_;
```

裸句柄置空，失败路径和正常 shutdown 使用同一套幂等清理函数。数组成员含 `GpuImage` 不代表 framebuffer 会自动释放；创建半途失败时仍要逐个释放已创建 framebuffer。

### 5.1 HDR 格式和 MSAA 查询

优先 `VK_FORMAT_R16G16B16A16_SFLOAT`，需要：

- optimal tiling 支持 `COLOR_ATTACHMENT_BIT`、`SAMPLED_IMAGE_BIT`；本指南使用线性 post sampler，另检查 `SAMPLED_IMAGE_FILTER_LINEAR_BIT`。
- 用 `vkGetPhysicalDeviceImageFormatProperties()` 核对实际 type/tiling/usage/flags、extent 和 sampleCounts。
- 对 MSAA color 的 `COLOR_ATTACHMENT` usage，以及 depth 格式的 `DEPTH_STENCIL_ATTACHMENT` usage，分别查询支持的 samples；与设备 framebuffer 限制取交集。
- HDR 单采样图需要 `COLOR_ATTACHMENT | SAMPLED`，不能使用 transient attachment usage；MSAA 图本身无需 `SAMPLED`。
- 最终 scene samples 不高于现有 `context_->msaaSamples()`。可选 4× 作为初始上限减少显存，但必须打印真实选择，不再把设备最大值叫作“当前 scene MSAA”。
- 不支持预期 HDR 格式时明确报错并带上 format/usage；不要静默退回 8-bit UNORM。之后再明确设计浮点备选格式。

当前输出 `MSAA samples: 8` 只来自设备级限制，不能证明 RGBA16F 也支持 8×。[Vulkan image format 查询](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceImageFormatProperties.html)

以 1920×1080 为例，一张 RGBA16F 约 15.8 MiB（不含分配开销）；8× color 约 126.6 MiB，两帧再加 depth/resolve 并不便宜。不要无条件选设备最大 samples。

### 5.2 必须同时支持 1× 和 MSAA

| scene samples | framebuffer 附件顺序 | resolve |
|---|---|---|
| 1× | `hdrColor`, `depth` | `pResolveAttachments = nullptr` |
| >1× | `msaaColor`, `depth`, `hdrColor` | attachment 2，单采样 |

1× 时不要继续生成“三附件 + 单采样 resolve source”的组合。

- HDR 图：extent = swapchain extent，mipLevels=1，arrayLayers=1，DEVICE_LOCAL。
- `hdrColor`：最终 `SHADER_READ_ONLY_OPTIMAL`，storeOp=STORE；直接渲染时 loadOp=CLEAR，作为 resolve 时 loadOp=DONT_CARE。
- `msaaColor`：loadOp=CLEAR，storeOp=DONT_CARE，最终 `COLOR_ATTACHMENT_OPTIMAL`。
- depth：与 scene samples 一致，loadOp=CLEAR、storeOp=DONT_CARE；不是 shadow depth。
- scene clear values 与 attachment 索引保持一致，颜色/深度仍在 0/1。
- post sampler：普通 sampler，LINEAR、CLAMP_TO_EDGE、LOD 0、关闭比较和 anisotropy。

### 5.3 Scene pass 的同步

使用现有 Vulkan 1.0 render pass/dependency 接口；本阶段不同时迁移到 synchronization2/dynamic rendering。

- 输出依赖：`COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_WRITE` → `FRAGMENT_SHADER / SHADER_READ`，覆盖 resolve 写入到后处理采样。
- 入口依赖：之前 fragment 采样 → 本次 color/depth 附件访问；dst 包含 color output、early/late fragment tests 和对应附件 read/write access。
- 跨像素采样/Bloom 不依赖仅 framebuffer-local 的顺序；使用完整区域依赖，不默认加 `BY_REGION`。
- 当前 frame fence 确保重复使用该 frame 的图像前，之前提交已结束。`initialLayout=UNDEFINED` 表示丢弃内容，不能替代同步。
- 采用 render pass finalLayout + external dependency 后，不再重复写一套冲突的 layout barrier。

官方例子给出了 color write → fragment sampled read 的阶段和访问关系；文档中的 synchronization2 示例需翻译为项目现有 flags，而不是直接引入新 API。[Vulkan 同步例子](https://docs.vulkan.org/guide/latest/synchronization_examples.html)

## 6. 提交 3：接通 HDR → 显示链路

提交名：`Render the scene through an HDR post-process pass`

这是一次完整的纵向切换：**scene 输出、present pass、descriptor、ImGui、resize 必须一起接通**。可以在本地分小步编译，但中间态不能当作可运行提交。

### 6.1 Present pass 与 Swapchain

- Present pass 只有一个 color attachment：swapchain format、sample=1、loadOp=CLEAR、storeOp=STORE、initial=UNDEFINED、final=PRESENT_SRC_KHR。
- 没有 depth、没有 resolve；对应 fullscreen pipeline `depthTest=false`、`depthWrite=false`、`useVertexInput=false`、`cullMode=NONE`。
- 保留 swapchain acquire 的 semaphore 等待与 `COLOR_ATTACHMENT_OUTPUT` 目标阶段；present pass 的入口依赖也要覆盖 color output。不要把 shadow/HDR 图当作 present image。
- `Swapchain::createFramebuffers(presentRenderPass)` 改为每个 framebuffer 只有 `imageViews_[imageIndex]`。
- 删除 Swapchain 内旧 `colorImage_`、`depthImage_`、`createAttachments()` 及相关创建/清理；它们已由每帧 HDR target 接替。
- 用 `sceneRenderPass()` / `presentRenderPass()` 明确命名，替换含糊的 `renderer.renderPass()` 调用点，不保留会让调用方猜用途的别名。
- Swapchain 增加只读 `colorSpace()` 和记录实际 surface 最小图片数的接口，供能力日志、颜色空间检查与 ImGui 初始化使用；`MAX_FRAMES_IN_FLIGHT` 不是 surface 的 `minImageCount`。

### 6.2 Post descriptor 独立于 Frame/Material

第一版 post layout 只需要 `set 0 / binding 0 = combined image sampler`，指向 `hdrTargets_[frameIndex].hdrColor`。

- Renderer 自己创建 post descriptor pool、layout、每帧 set、pipeline layout；不要继续往应用的 Frame set 塞 HDR、Bloom、FXAA 所有中间图。
- Gate A pool：`MAX_FRAMES_IN_FLIGHT` 个 set、同数量 combined image sampler。
- 创建目标后写入每帧 descriptor；descriptor 的 `imageLayout=SHADER_READ_ONLY_OPTIMAL` 不是实际布局转换。
- 在**写入 descriptor 的同一个 frame 循环**中选择该帧 view，避免此前 shadow descriptor 的“先循环覆盖同一个 imageInfo，再写全部 set”错误。
- resize 只在等待 GPU 后更新 sets。不要每帧分配/释放 descriptor。

### 6.3 第一个后处理保持 Reinhard

`fullscreen.vert` 复用 `shadow_debug.vert` 的大三角形方法，不复制 mesh。UV 与 positive viewport 一致，不无理由翻转 V。

现有 `showShadowProjection` 会让 scene shader 提前输出光空间坐标。Gate A 就必须保留这个数据调试语义：在 `RenderFrameData` 增加 `bypassToneMapping`，由当前帧的 `showShadowProjection` 填写，启用该模式时不绘制 HDR skybox。先引入第 7 节的 32 字节 `PostPushConstants` 和对应 fragment push range，初始化为零，仅将 `modes.y` 设为该 bypass 标志；曝光和映射选择留到下一提交。UI 更新放在本帧 UBO 和快照生成之前，确保两个模式一致。

新增 `tonemap.frag`，第一版核心（文件还需 `#version 450`）：

```glsl
layout(set = 0, binding = 0) uniform sampler2D hdrScene;
layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PostPushConstants
{
    vec4 params;
    ivec4 modes;
} post;

void main()
{
    vec3 hdr = max(textureLod(hdrScene, fragUv, 0.0).rgb, vec3(0.0));
    vec3 displayLinear = post.modes.y != 0
        ? hdr
        : hdr / (hdr + vec3(1.0));
    outColor = vec4(displayLinear, 1.0);
}
```

与此同时：

- `shader.frag`：保留 `ambient + ibl + Lo + emissive`，删除末尾 Reinhard，输出线性 HDR。
- `skybox.frag`：直接输出 HDR cubemap 采样值，删除自身 Reinhard。
- 阴影仅衰减方向光贡献的逻辑保持不变。
- clearColor 明确作为 scene 的线性颜色使用；它也会被 tone map。不要再把它当成最终屏幕值。

不能只删除材质 tone mapping 却漏掉天空盒，也不能两处旧映射都保留后再加一次 post 映射。

### 6.4 ImGui 和阴影预览搬到显示端

`Renderer::recordFrame()`：shadow → HDR scene（skybox+objects）→ present（fullscreen+preview+ImGui）。每个 pass 显式设置正确 extent、viewport/scissor、pipeline、sets；不要假设动态状态自动恢复。

所有 pipeline 的目标：

| Pipeline | Render pass | Samples |
|---|---|---|
| scene / skybox | sceneRenderPass | sceneSamples |
| shadow | shadowRenderPass | 1 |
| tonemap / shadow preview | presentRenderPass | 1 |
| ImGui backend | presentRenderPass | 1 |

`ImGuiLayer.cpp::initImGui()` 必须改为：

```cpp
initInfo.PipelineInfoMain.RenderPass = renderer.presentRenderPass();
initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
```

本地依赖是 ImGui **1.92.8**，不要照旧教程填写已迁移的顶层 `RenderPass/MSAASamples` 字段。UI、坐标轴和阴影预览不参加 scene tone mapping、Bloom、FXAA；保持原有 UI 颜色解释，本阶段不顺便重做 UI 色彩校准。

### 6.5 Resize、格式变化与释放是本提交的一部分

当前 `Swapchain::rebuildCore()` 会 waitIdle 后 shutdown/reinitialize；不是事务式替换旧 swapchain。第一版保持“重建失败就安全停止”的策略，不宣称失败后仍能继续渲染。

按以下顺序设计 `TriangleApplication::recreateSwapChain()`：

1. 检查窗口非零 extent；最小化返回 Deferred，不创建 0×0 图。
2. 等待设备空闲，仅 resize/重建/退出允许，不加入正常每帧路径。
3. 删除或注销会指向旧尺寸图的 debug UI descriptor（若已注册）；不缓存旧 view。
4. 保存旧 format/colorSpace/imageCount，执行 `swapchain.rebuildCore()`；若非 Ready 不提交绘制。
5. 格式不变时复用兼容的 present render pass/pipeline；格式变化时重建 present pass、tonemap/preview pipeline，并同步更新 ImGui Vulkan backend pipeline。
6. 创建新 extent 的整套 HDR targets，重写所有 post sets 后释放旧 targets；也可先销毁旧资源再创建，但失败必须停止渲染，不能沿用已失效 set。Gate B 增加后，这个集合还必须包含 Bloom down/up、LDR 和尺寸相关 debug preview 的全部 image/framebuffer/descriptor/UI texture registration，不能只改 HDR。
7. 用 present pass 创建 swapchain 单附件 framebuffers。更新 renderer 缓存 extent。
8. ImGui imageCount/minImageCount 也核对实际 swapchain。`SetMinImageCount()` 只处理它负责的最小数量，不等价于更新所有初始化信息。
9. 成功后清除 resize 请求，下一帧恢复；shadow targets 不因窗口尺寸改变而重建。

可为 format/imageCount 变化选择“等待后仅重建 Vulkan backend，保留 ImGui context 和 GLFW backend”，使用实际 RenderPass、samples、imageCount 初始化。先拆出 backend 初始化辅助函数和初始化状态，失败时不要再次 Shutdown 一个尚未初始化的 backend。不要反复调用整个 `initImGui()` 向 `mainDeletionQueue` 注册多份 shutdown lambda。仅格式变化且 counts 不变时，本地 backend 也提供 `ImGui_ImplVulkan_CreateMainPipeline()`。重启 backend 前注销应用注册的所有 debug texture，之后重新注册（包括 BRDF LUT），清空旧 UI texture ID，不只处理尺寸变化的图片。

Gate A 先严格接受 `B8G8R8A8_SRGB` 或 `R8G8B8A8_SRGB` 且 colorSpace 为 `SRGB_NONLINEAR`；不匹配明确报错，替换原来不受控的 `formats.at(0)` fallback。UNORM/HDR10 fallback 留待显式设计，不能黑盒套一个 gamma。

清理顺序：waitIdle → 注销 UI 纹理并关闭 ImGui Vulkan backend → 销毁引用离屏图的 post pool/sets → 销毁 framebuffers（包括 Swapchain 的 present framebuffers）→ reset 图像 → 销毁 pipeline/layout/render pass（遵守实际引用关系）→ Swapchain/Context 最后释放。现有应用 pool 在 `mainDeletionQueue.flush()` 中释放，Renderer 新增资源由自身 shutdown 负责，不注册捕获旧句柄的重复 lambda。

验收：窗口可见、模型/天空盒亮度基本保持；MSAA 边缘可能因“先 resolve 线性 HDR、后 tone map”的顺序变化而不同，这是合理差异。resize/退出无 Validation 错误；主 scene color 的捕获值能够大于 1，而非已经压缩后的 LDR。

## 7. 提交 4：统一曝光、ACES fitted 与颜色空间

提交名：`Add exposure and tone mapping controls`

**目的：用同一套显示参数控制所有场景光照，而不是修改材质或光源来模拟曝光。**

建议 CPU 参数：

```cpp
enum class ToneMapper : std::int32_t { Reinhard = 0, AcesFitted = 1 };

struct PostProcessSettings
{
    float exposureEV = 0.0f;
    ToneMapper toneMapper = ToneMapper::Reinhard;
};

struct alignas(16) PostPushConstants
{
    glm::vec4 params{0.0f}; // x: exposureEV; yzw 后续使用
    glm::ivec4 modes{0};    // x: tone mapper; y: debug bypass
};
static_assert(offsetof(PostPushConstants, modes) == 16);
static_assert(sizeof(PostPushConstants) == 32);
```

应用持有 settings，放进 `RenderFrameData` 快照；Renderer 填 post push constant。`PostPushConstants` 在提交 3 已用于光空间调试 bypass，本提交启用其中的曝光和 tone mapper 字段。独立 post pipeline layout 仅声明 fragment stage 的 32 字节 range，GLSL 对应 vec4+ivec4。不要扩大已有 128 字节 DrawPushConstants。

曝光使用 `hdr *= exp2(exposureEV)`，UI `-8..8 EV`；EV=0 不变，+1 亮度翻倍，-1 减半。不是 `hdr *= exposureEV`。

ACES 选用 Narkowicz 常用拟合曲线并明确标注 **ACES fitted approximation**，它不是完整的 ACES 色彩管理流程：

```glsl
vec3 acesFitted(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) /
                 (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
```

曝光前限制负值；大量溢出/NaN 应诊断输入，不用 clamp 假装修复。拟合来源和局限见 [作者说明](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)。

### 7.1 色彩职责表

| 数据 | 解释 | 是否 tone map / sRGB 编码 |
|---|---|---|
| Base Color、Emissive 的 sRGB 图片 | 采样时自动解码为线性 | 不再手动 `pow(..., 2.2)` |
| Normal、MR、AO、BRDF LUT、depth | 数据，不是 sRGB 颜色 | 不做 sRGB 解码 |
| HDR scene / HDR 环境 / Bloom | 线性、可大于 1 | 不提前 tone map/gamma |
| Gate A post shader 输出 | tone-mapped display-linear RGB | 写 sRGB attachment，由硬件编码 |
| ImGui / debug overlay | 显示端合成 | 不接受场景曝光/Bloom/FXAA |

`SRGB_NONLINEAR` colorSpace 本身不使 UNORM image 自动编码；负责 attachment 编码的是 `_SRGB` 格式。当前支持矩阵已限制 sRGB attachment，所以 post shader **不要再 `pow(color, 1/2.2)`**。[Vulkan framebuffer 颜色转换](https://docs.vulkan.org/spec/latest/chapters/framebuffer.html)

验证：线性灰色 0.18 在 sRGB 存储约为 0.461（以已绕过 tone mapping 的测试色为例）；不要期望普通截图直接读到线性 0.18。EV 改变模型和天空盒，但不改变 UI/原始深度预览。Reinhard 与 ACES 切换不改变 HDR 场景图中的原始值。

## 8. 提交 5：让 Debug Views 能解释画面

提交名：`Add material and HDR debug views`

**目的：区分材质输入、光照结果、后处理结果，避免靠最终画面猜测。** 不要求引入 G-buffer。

建议 `DebugView` 枚举：Lit、Albedo、WorldNormal、Metallic、Roughness、AO、Irradiance、Prefilter、BrdfLut、ShadowDepth、HdrLuminance。UI 放在 Rendering/Diagnostics，现有 Shadows 的深度预览保留。

- 物体属性模式可复用 Frame UBO 的空闲 `renderParams.y` 保存小整数 mode（CPU 显式转 float，GLSL 显式转 int）；`renderParams.z` 保存 prefilter LOD，x 仍是 IBL intensity。三个 scene/shadow shader 的 UBO 声明保持布局一致。
- Post 快照同步同一 mode，决定是否绕过 exposure/tone mapping/Bloom/FXAA；不要 UI mode 变了而 UBO 还是前一帧。把当前 `drawFrame()` 的 UI 编辑放在 `updateUniformBuffer()` 和 renderData 构建之前，继续在 frame fence 之后更新，统一一帧的参数快照。
- 现有 `showShadowProjection` 暂时保留：启用时优先于新增 DebugView，并在 UI 提示覆盖关系；它也属于绕过后处理的数据模式。不让两个独立开关同时决定相互矛盾的着色分支。
- Albedo：纹理×顶点色×factor 的线性值；World normal：明确显示 normal map 后用于光照的世界法线，映射 `N*0.5+0.5`。
- Metallic/Roughness/AO：显示参与着色的标量灰度；所有物体数据模式（包括 Albedo、WorldNormal、光空间坐标）都隐藏 skybox 或使用中性清屏，避免把 HDR 天空盒当数据图。
- Irradiance/Prefilter：沿世界法线/反射方向采样现有 cubemap；它们仍是 HDR 辐亮度，允许独立注明的预览曝光或统一 tone mapping，不要与标量 bypass 混淆。Prefilter mip 滑条限制在实际 mip 范围。
- BRDF LUT：独立二维预览，RG 分量可显示到颜色或分别灰度；不是 PBR 渲染后的一块截图。
- HDR luminance：对 Lit HDR 使用 `dot(rgb, vec3(0.2126, 0.7152, 0.0722))`，做明确范围的 log2/false-color 显示，不把 >1 全部 clamp 成白色；记录刻度。
- 复用现有 shadow depth preview，无需重复造 shadow image。

Debug bypass 只跳过曝光、tone mapping、Bloom、FXAA，**不跳过正确的显示编码**。Gate A 输出 display-linear 到 sRGB attachment；第 10 节改为 UNORM LDR 后，数据模式也遵守 linear-to-sRGB 写中间图、present 时解码再由 sRGB attachment 编码的相同约定。

### 8.1 本项目的 ImGui 图片 API 注意点

本地 `imgui_impl_vulkan.h` 1.92.8 的正式接口是：

```cpp
VkDescriptorSet ImGui_ImplVulkan_AddTexture(
    VkImageView image_view, VkImageLayout image_layout);
void ImGui_ImplVulkan_RemoveTexture(VkDescriptorSet descriptor_set);
```

它注册的是 **SAMPLED_IMAGE** descriptor；旧三参数 overload 里的 sampler 会被忽略。不要把教程中的 combined sampler set 直接当成这里的 UI texture。用返回的 set 按本地 `ImTextureID/ImTextureRef` 接口构造 `ImGui::Image` 参数；编译时以本地头文件为准。现有 ImGui pool 已有 sampled-image 和 sampler 容量。[ImGui 1.92.8 backend 接口](https://github.com/ocornut/imgui/blob/v1.92.8/backends/imgui_impl_vulkan.h)

BRDF LUT 是持久资源，可以初始化后注册一次。每帧 HDR/Bloom 调试图需要为每个 frame 的 view 分别注册并选择当前帧，不每帧重新 AddTexture；resize 等待后 RemoveTexture → 销毁旧 view → 注册新 view。若要额外曝光、通道选择或 false-color，先由自己的 debug pipeline 写预览图，再交给 ImGui，不能指望默认 UI shader 自动理解 HDR。

验收：数据模式可解释，UI 和 raw shadow preview 不受曝光影响；阶段 7 路线图要求的每个 debug view 都记录实际结果。

## 9. 提交 6：Bloom（先亮部提取，再多级过滤）

提交名建议分开：`Extract HDR bloom highlights`、`Add bloom downsample and upsample passes`。

**目的：让超过亮度阈值的区域向周围扩散；它不是把整张最终画面模糊。**

先 bright-pass：

1. 在 tone mapping **之前**读取当前帧 HDR；阈值定义为场景线性亮度，不随曝光改变，以便学习和验收。
2. `brightness = max(r,g,b)`；第一版可用 `color * max(brightness-threshold, 0) / max(brightness, 1e-5)`，后续 soft knee 独立增加。
3. 输出半分辨率 RGBA16F 到独立图片，提供亮部预览；不要把 UI 和原始 debug overlay 提取进去。

然后过滤链：每帧分配最多 5 层 downsample 图，每边 `max(1, previous/2)`，到 1×1 停止；另分配对应的 upsample 输出，避免同一 draw 采样又写同一 subresource。每级明确 extent、texelSize、framebuffer、input descriptor、layout 和 read/write dependency。

- 下采样先用归一化 2×2 box kernel，所有权重和为 1。
- 上采样可用 3×3 tent kernel（权重归一化），并与该层 downsample 结果加权合成，例如 `0.5*down + 0.5*upsampledCoarser`，避免每增加一级能量就无解释地放大。
- 最终 `combinedHdr = sceneHdr + bloomStrength * bloom`，再曝光、tone map。
- 关闭 Bloom 时跳过其 draws，但 post shader 也必须不读取尚未写入的 Bloom 图。可绑定初始化到黑色/read-only 的持久 fallback，并设置强度为 0；**不要仅把强度设 0，却仍采样 undefined image**。
- 同样检查 ImGui/调试预览：关闭 Bloom 或刚 resize 时，不显示尚未生成的 Bloom 纹理，改用黑色 fallback/禁用预览；若要求关闭合成仍能看 Bloom 中间结果，则必须继续执行生成这些结果的 pass。
- descriptors 预先按 frame × pass 计数分配，resize 更新；不在帧循环分配。不将 Bloom 中间图混入 Frame/Material sets。

若 post shader 增加 binding 1（Bloom），创建 layout/pool/sets 和 shader 要在同一提交中匹配；所有关闭分支也要有合法绑定。

验收：自发光或高强度光照能扩散；普通暗区不会整体泛白；曝光后 Bloom 与场景一致变化；关闭时恢复 Gate A 输出；预览能看见 bright-pass/down/up 各层内容；1×1、小窗口、resize 没有非法尺寸。

## 10. 提交 7：FXAA 与最终显示 pass

提交名：`Add optional FXAA before UI composition`

**目的：对 tone-mapped 场景做屏幕空间抗锯齿，UI 不参加过滤。** FXAA 不修复 shadow map 的覆盖密度，也不能代替阴影 PCF。

增加 FXAA 后不能在同一交换链图里边写边采样，需要中间 LDR 目标。选定一个可验证的色彩约定：

```text
线性 HDR + Bloom
  → 曝光 / tone mapping
  → 显式 linear-to-sRGB
  → 每帧 R8G8B8A8_UNORM 的 display-encoded LDR 图
  → FXAA（或 bypass），读取 display-encoded RGB / luminance
  → sRGB-to-linear
  → sRGB swapchain attachment 自动编码
  → 原始 debug overlay + ImGui
```

这里 LDR 图故意用 **UNORM** 保存已经编码的值，采样时不自动解码，符合所选 FXAA 输入约定；最后先解码再交给 sRGB attachment，不能把已编码 RGB 直接再写 `_SRGB` 导致重复编码。使用标准分段 sRGB 函数，而非把 `pow(2.2)` 当成精确转换。Gate A 的 tone mapper 原先直接写 display-linear 到交换链，迁移时要同步改其目标与输出约定。

按 [NVIDIA FXAA 白皮书](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf) 选定算法版本并保留来源/许可证说明：采样中心和邻域亮度 → 局部对比阈值 early-out → 估计边缘方向 → 沿边缘采样/混合。不要把普通 3×3 box blur 命名为 FXAA。不同版本对 RGB/luma packing 有要求，按选定版本写死契约并验证。

- MSAA 保留为 scene 几何抗锯齿；FXAA 默认关闭，两者允许独立比较。
- `invResolution` 使用 **LDR 输入图**尺寸，不用阴影图尺寸。
- FXAA 关闭仍走同一份 LDR present-copy 路径，保证切换只影响过滤，不改变曝光/gamma。
- 最终 present pipeline、ImGui 和 debug overlay 都是 1×。新增 LDR pass 与 Bloom pass 不需要 depth。
- 所有普通后处理使用 CLAMP_TO_EDGE，避免边缘卷到另一侧。

验收：斜边锯齿减轻，文字/UI 保持清晰；无全屏亮度跳变、上下翻转或边缘环绕；静止时关闭 FXAA 基本恢复原有 tone-mapped 图，允许 8-bit LDR 量化误差。

## 11. 构建、调试与验收记录

每个新 shader 同步两个编译脚本和 AppConfig 路径；源码已存在后才注册。新 cpp 加入 CMake target。当前构建系统尚未自动跟踪 shader 编译依赖，手敲阶段执行：

```bash
sh scripts/compile-shaders.sh
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel
cmake -E copy_directory assets build/linux-debug/assets
ctest --test-dir build/linux-debug --output-on-failure
./build/linux-debug/vulkan

cmake --preset linux-release
cmake --build --preset linux-release --parallel
cmake -E copy_directory assets build/linux-release/assets
ctest --test-dir build/linux-release --output-on-failure
./build/linux-release/vulkan
git diff --check
```

现有 glTF CPU 测试通过不等于 GPU 后处理正确；Release 默认关闭 Validation。Debug 要额外开启同步验证（通过 Vulkan Configurator 或已安装 layer 的配置），不能只看普通日志没有 VUID 就宣称已查过所有 hazard。

| 验收 | 期望 |
|---|---|
| capture scene HDR | 格式为浮点，亮部值可 >1，未提前 tone map |
| EV -1 / 0 / +1 | 输入 tone mapper 的亮度按 0.5 / 1 / 2 变化，UI 不变 |
| 模型与天空盒 | 使用同一显示变换，无天空盒重复映射 |
| scene samples=1 与支持的 MSAA | 均可创建/绘制，resolve 分支合法 |
| color-space 灰阶 | 无重复 gamma；非支持的 surface format 明确拒绝 |
| 开关阴影/PCF | Stage 6 功能不回归 |
| glTF 导入/删除、点光源/IBL/Emissive | 原有资源与材质流程保持正常 |
| 多帧运行、快速 resize、最小化恢复 | 无跨帧 image hazard、旧 descriptor、错误 framebuffer |
| Bloom/FXAA 分别开关 | 彼此独立，UI 与 debug overlay 不参与效果 |
| format/imageCount 变化 | present pipelines / ImGui 一致更新，或明确报错安全退出 |
| 初始化部分失败、正常关闭 | 所有新资源可清理，不重复销毁，无 in-use 错误 |

记录模板：日期、commit、GPU/驱动、HDR format、scene samples、extent、swapchain format/colorSpace、Debug/Release、Validation/sync validation、测试场景、截图/捕获、已知限制。没执行的项目写“待验收”。

### 11.1 完成标准

- [ ] Gate A：per-frame HDR + 正确 resolve/1× 分支 + 单采样 present 已接通。
- [ ] Gate A：Exposure/Reinhard/ACES fitted 和 sRGB 职责明确且验证。
- [ ] Gate A：UI/阴影深度预览在后处理之后，resize 和 shutdown 通过。
- [ ] Gate B：Bloom bright-pass/downsample/upsample 可调、可关闭。
- [ ] Gate B：FXAA 可独立关闭，色彩不随开关跳变。
- [ ] Gate B：所有路线图 debug views 有实际结果，包含 BRDF LUT 的 ImGui 预览。
- [ ] Stage 5/6 回归、Debug/Release、Validation/同步验证和窗口交互有记录。

Gate A 完成可以提交稳定 HDR 基线，但不能把 Bloom/FXAA/全部 Debug Views 一并勾选。Gate B 完成后再进入 Stage 8 性能与可观测性。

## 12. 下一次实际手敲任务

**只从提交 1 开始：给 GraphicsPipelineConfig 增加 renderPass/samples，更新三个调用方，让画面保持不变。**

先理解“pipeline 必须匹配它真正绘制的附件”，再进入资源分配。不要现在就删除 `shader.frag` 和 `skybox.frag` 的 tone mapping；等 HDR→post→present 的纵向提交能完整接通时再一起迁移。
