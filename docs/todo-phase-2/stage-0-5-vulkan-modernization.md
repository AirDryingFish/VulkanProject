# 阶段 0.5：Vulkan 能力管理与现代化手敲指南

本文接续 [Stage 0 渲染基线](stage-0-rendering-baseline.md)，对应 [Phase 2 大纲](overview.md) 的阶段 0.5。
核对日期：2026-10-19；源码基线：`4655e94`（`Add comment`）。

**当前应该先做本阶段，再实施 [Stage 1：CSM](stage-1-cascaded-shadow-maps.md)。**
Stage 0 的计时、Diagnostics、显示调试和 CSV 采集主功能已经存在；固定硬件基线、截图和捕帧证据仍需补齐。
本文是待实施指南，没有修改渲染代码，也不表示新版 API 路径已经通过运行验收。
文中的新类型、字段和接口均是拟新增设计；现有接口另行注明。

## 1. 为什么在 CSM 前安排这一阶段

现在仍是 Vulkan 1.0 初始化、旧式 barrier/submit 和 RenderPass/Framebuffer 主路径。
CSM 随后会增加多层深度附件，Deferred 还会增加多个 G-buffer 附件；先整理能力、同步和附件表达，
可以让后续算法沿用同一套约定。

这属于本项目的学习顺序；CSM 算法本身不依赖 Vulkan 1.3，也可以用旧式 RenderPass 实现。
本阶段的成功标准是初始化契约清楚、画面与基线一致、同步正确，不预设更换 API 会自动加速。

```text
已有 Stage 0：观察画面 + CPU/GPU 时间 + 绘制/资源统计
    ↓
A：版本与能力“看得见”
    ↓
B/C：协商版本、查询 feature、只启用实际需要的能力
    ↓
D：迁移 barrier → E：迁移 submit
    ↓
F：迁移 Post/Present + ImGui
    ↓
G：迁移 HDR Scene → H：迁移单图 Shadow
    ↓
I：回归、归档 → Stage 1：四级 CSM
```

首轮保持现有 graphics/present queue、两个飞行帧、fence 和 WSI binary semaphore。
Timeline Semaphore、Bindless、Render Graph、异步 Compute 和 RT 不与这次迁移绑定。

## 2. 对照当前代码，具体缺什么

| 当前落点 | 已有行为 | 本阶段要补的内容 |
|---|---|---|
| [Application.cpp](../../src/app/Application.cpp) `initializeRendering()` | 请求 `VK_API_VERSION_1_0` | 统一应用的版本请求策略 |
| [VulkanContext.hpp](../../src/vulkan/VulkanContext.hpp) `VulkanContextConfig` | 单一 `apiVersion`；默认 1.0 | 区分请求、支持、实际使用版本与启用能力 |
| [VulkanContext.cpp](../../src/vulkan/VulkanContext.cpp) | 未协商 Loader；旧式 features 查询和 `pEnabledFeatures` | Loader 查询、候选设备能力记录、Features2 查询/启用链 |
| 同上 `createAllocator()` | VMA 已读取 `apiVersion_`，并非另有一处硬编码 | 让该值来自选定设备的有效 API 路径，而非直接复制 Instance 请求 |
| [EditorUI.cpp](../../src/ui/EditorUI.cpp) `initImGui()` | ImGui API 版本独立写死 1.0，绑定 Present RenderPass | 版本来源统一；Present 迁移时启用动态渲染模式 |
| [UploadCommands.cpp](../../src/vulkan/UploadCommands.cpp) | `VkImageMemoryBarrier` / `vkCmdPipelineBarrier` | 分批迁移到 Barrier2，保留 mip/layer 范围与生产消费关系 |
| [Renderer.cpp](../../src/render/Renderer.cpp) | `VkSubmitInfo` / `vkQueueSubmit`、三个旧式渲染 Pass | Submit2、显式附件、布局转换与依赖 |
| [GraphicsPipeline.cpp](../../src/render/GraphicsPipeline.cpp) | 创建 helper 强制要求非空 `renderPass` | 支持明确的动态附件格式契约 |
| [PostProcessing.cpp](../../src/render/PostProcessing.cpp)、[ShadowMapping.cpp](../../src/render/ShadowMapping.cpp) | 初末布局、external dependency、framebuffer 承担部分管理 | 将主路径上的职责迁移为显式 barrier 和 rendering attachment |
| [Swapchain.hpp](../../src/vulkan/Swapchain.hpp) | image/view 私有，目前只有 framebuffer 访问接口 | 按本次真实用途增加受边界检查的 image/view 只读访问 |
| [compile-shaders.sh](../../scripts/compile-shaders.sh)、[compile-shaders.bat](../../scripts/compile-shaders.bat) | 未显式设置 `--target-env` | 两个平台脚本使用一致的、实际支持的编译目标 |

不先重命名 `Application` 或再次拆一轮目录。能力与入口归 `vulkan/`，渲染附件与 Pass 归 `render/`，
UI 只展示只读能力信息；需要新的 `.cpp` 时再更新 [CMakeLists.txt](../../CMakeLists.txt)。

## 3. 先认清四种版本与两种 feature 状态

| 名称 | 回答的问题 | 来源/用途 |
|---|---|---|
| 头文件版本 | 编译器认识哪些类型、函数声明？ | `VK_HEADER_VERSION_COMPLETE`；不能代表运行时支持 |
| Loader 支持版本 | 本机 Loader 可以建立什么版本的 Instance？ | 动态查询 `vkEnumerateInstanceVersion` |
| Instance 请求版本 | 应用这次创建 Instance 使用什么版本约定？ | `VkApplicationInfo::apiVersion` |
| 物理设备 API 版本 | 选中的 GPU/驱动支持什么设备核心版本？ | `VkPhysicalDeviceProperties::apiVersion` |
| 本项目设备有效版本 | 本次设备路径实际使用到哪个核心版本？ | 项目策略取请求与设备支持的共同范围；交给 VMA 等使用 |
| supported / enabled | 设备支持，还是已经在本次 `vkCreateDevice` 启用？ | 分开记录；支持不等于启用 |

本项目首选 Vulkan 1.3 主路径。若目标设备需要更低版本，先检查 KHR 路径及其依赖，再决定实现范围。
**仅把某个宏改成 1.3，不会自动开启 `synchronization2` 或 `dynamicRendering`。**
版本迁移规则见 [Khronos 版本指南](https://docs.vulkan.org/guide/latest/versions.html)。

先收集下面的目标平台表，再决定最低版本；未运行的平台不写“已支持新版路径”。

| 平台/GPU/驱动 | Loader / Device API | Sync2 支持路径 | Dynamic Rendering 支持路径 | 本次已验证 |
|---|---|---|---|---|
| 当前开发机：待查询 | 待填写 | 待填写 | 待填写 | 待执行 |
| Windows 目标机 | 待填写 | 待填写 | 待填写 | 待执行 |
| macOS / MoltenVK 目标机 | 待填写 | 待填写 | 待填写 | 待执行 |

开发时先打通一条已确认支持的路径。较低版本兼容路径可以后续单独实现和验收，不能静默回退版本后
仍调用未启用的新入口。现有 Git 基线可作画面对照；保留历史提交不等于已有运行时兼容后端。

## 4. 小组 A：先记录能力，保持原有渲染

**目的：先回答“我实际运行在什么设备和版本上”，避免初始化、同步和附件同时变化后难以定位问题。**

第一组只触及 Context、少量配置/只读展示，不修改 Pass、shader 或资源布局。

| 手敲步骤 | 具体动作 | 这一步的目的 |
|---|---|---|
| A1 | 在 `VulkanContext.hpp` 附近定义拟新增的能力值对象；内容稳定后可移入 `vulkan/VulkanCapabilities.hpp` | 给运行能力一个所有者，避免 UI 自己查询 Vulkan |
| A2 | 记录头文件版本、Loader 版本、当前 Instance 请求版本 | 分开编译环境和运行环境 |
| A3 | 选卡时记录每个候选的设备名、API、queue、必需扩展及拒绝原因 | 解释为什么选中/拒绝某张卡，而非只报 `failed to find a suitable GPU` |
| A4 | 本组继续使用原有 feature/渲染路径，新增只读 getter 或启动日志 | 先形成可运行的小提交；Features2 放到下一组 |
| A5 | 构建并启动 Box / Shadow Playground，检查画面与退出 | 确认观测代码没有改变原有渲染 |

能力对象建议只保存普通值、枚举和错误说明，不长期保存连接好的 `pNext` 裸指针链。
例如字段可分成 `loaderVersion`、`instanceVersion`、`physicalDeviceVersion`、`deviceApiVersion`，
以及每项能力的 supported / enabled / selectedPath；这些名字是设计建议，不是当前接口。

Loader 查询的最小写法可以先手敲在 `createInstance()` 之前：

```cpp
std::uint32_t loaderVersion = VK_API_VERSION_1_0;
const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
    vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
if (enumerateVersion != nullptr)
    VK_CHECK(enumerateVersion(&loaderVersion));
```

入口不存在时按 Loader 1.0 处理；入口存在但调用失败时走错误路径，不把失败吞成 1.0。
查询规则见 [vkEnumerateInstanceVersion](https://docs.vulkan.org/refpages/latest/refpages/source/vkEnumerateInstanceVersion.html)。

本组完成标准：日志能区分上述版本，渲染仍走旧路径；不能因为日志显示设备支持 1.3 就声称现代化完成。

能力记录可以先做，但进入 D 组实际迁移前，至少为当前单图阴影路径保存一套固定设置、截图和新 CSV。
具体元数据见 [Stage 0 的归档要求](stage-0-rendering-baseline.md#13-下一次实际手敲范围)；
不要等改完新路径，再凭记忆补写旧画面或旧性能。

## 5. 小组 B：协商版本，分开查询与启用

**目的：创建 device 之前就确定合法路径，不把设备“支持的所有 feature”直接全部开启。**

1. **确定 Instance 版本。** 将应用目标与 Loader 能力比较，记录实际请求；如果不满足项目要求，
   报告缺项。先保证可使用核心 Features2（Instance 1.1+），或实现并启用对应的
   `VK_KHR_get_physical_device_properties2` 路径，不能在原封不动的 1.0 Instance 上盲调核心入口。
2. **为每个候选查询能力。** 保留已有 queue、swapchain、surface format/present mode、anisotropy 检查，
   再查询所选核心/扩展路径需要的 feature。不要选完卡才发现所需能力缺失却没有拒绝原因。
3. **选择核心或 KHR 路径。** 1.3 主路径可查询 `VkPhysicalDeviceVulkan13Features`；低版本按实际支持的
   扩展使用相应 feature 结构并核对依赖。这里先只关心 Sync2 和 Dynamic Rendering。
4. **重新建立启用链。** 查询结构保存支持结果；另建全零启用结构，只设置本组实际需要的布尔值。
   B 组可以先把旧的 anisotropy 启用迁到 Features2，下一组再开启新增功能。
5. **保证链与入口有效。** `pNext` 指向的对象必须活到调用结束；同一种能力不重复挂入聚合核心结构和
   单独扩展结构。若把 Features2 放入 `VkDeviceCreateInfo::pNext`，令 `pEnabledFeatures = nullptr`。

下面仅展示已确认使用 **1.3 核心路径** 时的启用结构形状，省略已有 queue/extension 等创建参数：

```cpp
VkPhysicalDeviceVulkan13Features enabled13{};
enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
// 下一组确认 supported 后，才设置 synchronization2 / dynamicRendering。

VkPhysicalDeviceFeatures2 enabled{};
enabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
enabled.features.samplerAnisotropy = VK_TRUE;
enabled.pNext = &enabled13;

deviceInfo.pNext = &enabled;
deviceInfo.pEnabledFeatures = nullptr;
```

查询/启用方式见 [Features 指南](https://docs.vulkan.org/guide/latest/enabling_features.html)，
聚合结构字段见 [VkPhysicalDeviceVulkan13Features](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan13Features.html)。
这是 C++17 示例，不照抄采用 C++20 designated initializer 的外部示例。

本组完成标准：保留当前画面；必选 feature 缺失时有明确错误；supported 与 enabled 在日志中可区分。

## 6. 小组 C：启用新增能力，统一依赖配置

**目的：后续调用的新函数与 device 创建时启用的能力、外部库配置完全对应。**

| 手敲步骤 | 具体动作 | 目的/完成标准 |
|---|---|---|
| C1 | 在 B 组选定的链中显式启用已支持的 `synchronization2`、`dynamicRendering` | 先具备调用资格，暂不改变旧 Pass |
| C2 | 核心路径使用匹配入口；KHR 路径启用扩展及其依赖，加载并检查 KHR 入口 | 函数指针非空不能代替 feature 已启用；扩展名列表也不能代替 feature 位 |
| C3 | 保留 GLFW 所需 Instance 扩展、Validation、portability enumeration/subset 规则 | 升级时不破坏已有窗口与 MoltenVK 初始化 |
| C4 | VMA 使用本项目的设备有效 API 版本；ImGui `ApiVersion` 使用实际 Instance 请求版本 | 两者用途不同，不应机械地给所有字段写同一个版本常量 |
| C5 | 在两个 shader 编译脚本显式固定一致的 `--target-env`，重新编译并同步运行资源 | 输出 SPIR-V 不超出宣称支持的运行路径 |
| C6 | 只读记录实际路径及启用项，再跑现有场景 | 新功能已启用，但旧 RenderPass 路径应仍可工作 |

纯 1.3 主路径可以选择 `--target-env=vulkan1.3`；若还要让同一套 SPIR-V 服务较低版本路径，
应保留兼容的较低目标或分别生成，并写清选择。现有 shader 不必为了升级版本使用新语言特性。

VMA 的 `vulkanApiVersion` 不得高于 Instance 请求与所选物理设备的支持范围，见
[VmaAllocatorCreateInfo](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/struct_vma_allocator_create_info.html)。
当前本机依赖为 ImGui 1.92.8；应以锁定的依赖实际头文件/实现为准，不要求为了本阶段升级 ImGui。

本组完成标准：能明确打印“支持什么、启用了什么、调用哪条路径”，且尚未迁移的旧 Pass 正常。

## 7. 小组 D：先迁移一条明确的 barrier

**目的：把同步理解为具体资源的生产者与消费者，而不是全局替换函数名字。**

从 `UploadCommands.cpp` 的纹理上传开始：

1. **先写依赖表。** 以“复制写入纹理 → fragment shader 采样”为第一条；核对 image、mip、layer 与 aspect。
2. **改写为 `VkImageMemoryBarrier2`。** 将源/目标 stage 和 access 写在同一结构中，保持原有布局语义，
   queue family 当前仍使用 `VK_QUEUE_FAMILY_IGNORED`。
3. **用 `VkDependencyInfo` 包装，再调用 `vkCmdPipelineBarrier2`。** 此步不迁移提交、不改上传资源所有权。
4. **补 mipmap 路径。** 逐级处理 transfer dst/src 与采样布局，覆盖最后一级、单 mip 和 cubemap 多 layer。
5. **观察贴图和 IBL。** Box/Helmet 的材质纹理、天空盒和环境预计算都应正常，再迁移下一条依赖。

| 资源用途 | 生产者 stage / access | 消费者 stage / access |
|---|---|---|
| 上传纹理 | `COPY` / `TRANSFER_WRITE` | `FRAGMENT_SHADER` / `SHADER_SAMPLED_READ` |
| HDR 写入/resolve 后采样 | `COLOR_ATTACHMENT_OUTPUT` / `COLOR_ATTACHMENT_WRITE` | `FRAGMENT_SHADER` / `SHADER_SAMPLED_READ` |
| Shadow 深度写入后采样 | `EARLY_FRAGMENT_TESTS | LATE_FRAGMENT_TESTS` / `DEPTH_STENCIL_ATTACHMENT_WRITE` | `FRAGMENT_SHADER` / `SHADER_SAMPLED_READ` |

表内均指 `VK_PIPELINE_STAGE_2_*` / `VK_ACCESS_2_*`；实际 barrier 还需填写 image 和 subresource range。
其中 `COPY` 只覆盖复制写入的 mip；当前 mipmap 链使用 `vkCmdBlitImage`，相关生产者/消费者应使用
`BLIT`，或首版保守使用 `ALL_TRANSFER`，不能把旧 `TRANSFER` 一律替换成 `COPY`。
后两条在迁移对应动态 Pass 时落实。保留旧 Pass 期间，其 RenderPass 依赖仍有效，不把同一条转换重复录两遍。
复用渲染目标时还要处理“上次读取/写入 → 本次写入”的依赖；`oldLayout = UNDEFINED` 只表示丢弃内容，
不能用来代替仍然需要的执行同步。写完一条就说明它保护哪次访问。

API 对应关系见 [Synchronization2 指南](https://docs.vulkan.org/guide/latest/extensions/VK_KHR_synchronization2.html)，
资源依赖写法参见 [官方同步示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。

本组完成标准：纹理、mipmap、环境烘焙正常；没有新增每帧等待；同步验证可用时检查相应路径。

## 8. 小组 E：迁移提交，保留帧槽契约

**目的：更换提交表达方式，同时保持 Stage 0 已建立的资源复用和计时配对。**

1. **先迁移 `immediateSubmit()`。** 用 `VkCommandBufferSubmitInfo`、`VkSubmitInfo2` 表达同一条同步上传；
   继续等待原 fence，避免临时 staging 提前析构。
2. **再迁移 `endFrame()`。** 为 acquire 的 `imageAvailable` 和呈现的 `renderFinished` 分别填写
   `VkSemaphoreSubmitInfo`；它们仍是 binary semaphore，`value` 不承担 timeline 计数。
3. **保留 WSI 等待范围。** 当前交换链首次用于颜色附件，可使用 `COLOR_ATTACHMENT_OUTPUT` 等待；
   后续显式进入附件布局的 barrier 必须和这个等待范围衔接。signal 首版用 `ALL_COMMANDS` 保持原提交结束语义。
4. **保留 fence/ID 顺序。** 仍在紧邻真正提交前 reset fence；只有 Submit2 成功才递增 submission ID、发布 query pending；
   Present 要求重建时不能撤销已经存在的 GPU 提交。
5. **保留索引区别。** `frameIndex` 管理帧槽的 command buffer/fence/UBO；`imageIndex` 管理交换链图像与呈现信号量。
6. **计时迁移单独做。** 如改用 `vkCmdWriteTimestamp2`，先保留三个区间、六个 query 与原始测量含义；
   不把 barrier 的 TOP/BOTTOM 迁移规则机械套到计时点，也不顺手重定义统计范围。

呈现信号量的复用规则见 [Swapchain Semaphore Reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)。
`beginFrame()` 依旧先等槽 fence、消费旧 GPU 结果，再覆写本槽数据；无需为 Submit2 引入 Timeline Semaphore。

本组完成标准：三个 GPU 指标、CPU 六项指标、draw 计数、120 次预热和 300 行同 ID CSV 配对仍正常；
resize 导致 acquire/present 重建时不死等，不把提交主机耗时当作 GPU 耗时。

## 9. 小组 F：先迁移 Post/Present，与 ImGui 一起完成

**目的：先从单颜色、单采样、没有深度附件的 Pass 学习 Dynamic Rendering。**

### F1：让 Pipeline 知道附件格式

在 `Renderer::GraphicsPipelineConfig` 拟增加动态渲染路径与颜色/深度/模板格式描述。
`createGraphicsPipelineFromConfig()` 暂时同时接受旧式 renderPass 或动态格式，不能直接删除非空检查后不补契约。

动态分支挂入 `VkPipelineRenderingCreateInfo`，设置 `pipelineInfo.renderPass = VK_NULL_HANDLE`。
Post 的颜色格式使用真实 `swapchain.format()`，颜色附件数量为 1，深度/模板未使用时为 `VK_FORMAT_UNDEFINED`，
采样数仍为 1。pipeline 创建结构中的格式数组需活到创建调用结束；首版不引入 multiview。
这种格式声明和实际附件分离的方式见 [Dynamic Rendering 示例](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html)。

### F2：获得本次图像，表达布局和附件

1. **补最小访问接口。** 给 Swapchain 增加按 `imageIndex` 取 `VkImage` / `VkImageView` 的只读接口。
   它们现在确实被显式 barrier/attachment 使用；不要暴露可修改容器或混用帧槽编号。
2. **准备呈现前状态。** acquire 后将本图像转为 `COLOR_ATTACHMENT_OPTIMAL`。首用与复用状态明确记录，
   若决定每帧丢弃旧内容也要保留合法的 acquire 等待和执行依赖；重建后重新初始化状态。
3. **填写颜色附件。** `VkRenderingAttachmentInfo` 指向本图像 view，布局为颜色附件，`loadOp = CLEAR`、
   `storeOp = STORE`，clear value 沿用现有显示设置。
4. **包围原有绘制。** 用 Begin/EndRendering 包住 tone mapping、可选阴影预览和 ImGui，保持原顺序。
   Post 的 timestamp 仍包围同一逻辑区域，记录本轮是否把显式边界 barrier 包含在计时内。
5. **准备呈现。** EndRendering 后显式转到 `PRESENT_SRC_KHR`，再由呈现信号量衔接 Present；
   不把颜色附件的写入布局直接交给 Present。

只替换 Begin/End 函数会漏掉原 RenderPass 的布局和依赖。暂时不扩大当前
`recreateSwapchain()` 对格式/图像数量变化的支持范围；若另行支持，需一起重建相关 pipeline/ImGui。

### F3：同步迁移阴影预览与 ImGui Pipeline

阴影预览 pipeline 目前也绑定 Present RenderPass，必须改成同一动态颜色格式。
`EditorUI.cpp::initImGui()` 以当前 ImGui 1.92.8 接口填写：

- `ApiVersion`：实际 Instance 请求版本。
- `UseDynamicRendering = true`。
- `PipelineInfoMain.RenderPass = VK_NULL_HANDLE`。
- `PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT`。
- `PipelineInfoMain.PipelineRenderingCreateInfo`：有效 sType、一个 swapchain 颜色格式、无深度/模板。

不要使用旧教程中的顶层 `ColorAttachmentFormat` 或顶层 `PipelineRenderingCreateInfo`。
本次核对的后端会先取核心 Begin/EndRendering 入口，再尝试 KHR；不能仅根据头文件中旧的扩展注释，
额外宣称 1.3 核心路径必须启用 KHR 扩展。接口与入口加载以
[锁定版本头文件](https://github.com/ocornut/imgui/blob/v1.92.8/backends/imgui_impl_vulkan.h) 和
[后端实现](https://github.com/ocornut/imgui/blob/v1.92.8/backends/imgui_impl_vulkan.cpp) 为准。

本组最后才移除已无消费者的 Present framebuffer/render pass 创建、访问与清理调用。
Scene/Shadow/IBL 仍需要旧式对象，不执行全项目批量删除。

完成标准：正常画面、所有显示模式、阴影深度预览、字体/UI、resize/恢复与退出均有观察结果；
UI 仍不受场景曝光影响，swapchain 的 sRGB 输出约定保持一致。

## 10. 小组 G：迁移 HDR Scene，保留 MSAA/resolve

**目的：在已跑通的动态渲染基础上，再处理多采样颜色、深度与 resolve。**

| 手敲步骤 | 动作 | 目的 |
|---|---|---|
| G1 | 主场景和 skybox pipeline 改用 HDR/depth 格式与真实 `sceneSamples_` | pipeline 与实际附件一致，不能沿用 Post 的单采样配置 |
| G2 | 在每帧槽的 HDR、MSAA、depth 图像上建立进入渲染所需的布局/依赖 | 动态渲染不会替代这些转换 |
| G3 | 1x 时直接写 `hdrColor`；MSAA 时写 `msaaColor`，在颜色附件配置中 resolve 到 1x `hdrColor` | 保留旧链路的最终采样输入，不能只把采样数字段改成 1 |
| G4 | 颜色/深度清除、load/store 沿用实际用途；没有启用模板操作时不凭格式盲挂模板附件 | 避免无意保留垃圾内容或丢掉后续要采样的数据 |
| G5 | EndRendering 后将最终 `hdrColor` 写入/resolve 同步到 Post 的 fragment sampled read | 画面正确也要有实际的内存可见性依赖 |
| G6 | 更新重建/清理和估算，再移除主 Scene 旧式 framebuffer/render pass | resize 时仍能重建对应槽位资源 |

本项目 HDR 格式的首版 MSAA resolve 可采用平均解析；resolve 的 image view/layout/mode 配置参见
[VkRenderingAttachmentInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderingAttachmentInfo.html)。
深度仍只用于本阶段场景深度测试，不在本组追加深度 resolve 或时域数据。

完成标准：核对设备实际选定的采样数，在可用的 1x/MSAA 路径检查模型边缘、天空盒、深度遮挡、
HDR 亮度视图、Post 输入和资源开销；不能把一次单采样运行当作 MSAA 已验证。

## 11. 小组 H：迁移单图 Shadow，给 CSM 留下明确模板

**目的：先把现有一张方向光阴影的附件/同步表达改正确，再扩成四级。**

1. **先改 pipeline。** 阴影为零颜色附件、一个实际深度格式、单采样；保留 Opaque/Mask 和单面/双面选择。
   `createShadowPipeline()` 当前还有独立的旧式 pipeline 创建代码，不能只改通用 helper。
2. **再改录制。** 每个帧槽的 shadow depth view 用作动态深度附件；继续 clear depth 到 1、按现有比较约定绘制。
3. **补写入/采样依赖。** 进入附件前处理上一轮的使用，结束后从早/晚深度写入同步到主场景及预览采样；
   descriptor 的 layout 声明与真正转换后的 layout 一致。
4. **保留 clear-only。** 关闭方向光阴影绘制时仍按原逻辑清图；Shadow draw 为 0、GPU 区间仍有效，
   不把它误写成 NotExecuted。
5. **移除该 Pass 的旧对象。** 无消费者后再删 Shadow framebuffer/render pass；保留常驻深度图与采样器。
   Environment IBL 预计算仍可暂留旧式 RenderPass，另列范围即可。

完成标准：固定相机下阴影、PCF、bias、Mask 镂空、双面材质和原始深度预览与基线可对照；
不在这一组同时加入 cascade 数组、split、稳定化或新的阴影算法。

## 12. 小组 I：怎样验收，何时开始 Stage 1

每组都保留一个能运行的小提交。Linux 的基础检查使用仓库已有命令：

```sh
cmake --build --preset linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
./build/linux-debug/vulkan --scene shadow-playground
```

shader 有改动时额外执行 `./scripts/compile-shaders.sh` 并同步构建目录资源；Windows/macOS 用各自 preset，
未运行的平台保持待验收。普通 Validation 不等于已经开启 synchronization validation，记录实际设置。

- [ ] A–C：版本、支持/启用状态、核心/KHR 路径、VMA/ImGui/shader 目标有一致且可解释的记录。
- [ ] D/E：上传与提交完成迁移；旧有 fence、帧槽、ID、延迟回收和查询生命周期未退化。
- [ ] F–H：Post/Present、HDR Scene、单图 Shadow 主帧路径完成动态渲染；暂留 IBL 等路径明确列出。
- [ ] Box、Material Lab、Shadow Playground、Helmet 画面和 Debug Views 有对照；复杂 Mask/双面场景实际运行后再勾选。
- [ ] resize、真正的最小化/恢复、场景切换、导入/删除、退出有结果；无界面软件环境不能替代所有原生窗口行为。
- [ ] CPU/GPU 计时与 300 行同 ID CSV 有效；新旧路径的计时边界差异已注明，不直接比较不同范围的数值。
- [ ] 固定硬件的 Release 配置、截图、CSV、驱动信息和捕帧说明归档；没有填入预期加速数字。

满足主路径正确性与基线可复现条件后，按 [Stage 1 指南](stage-1-cascaded-shadow-maps.md) 实施四级 CSM。
阶段 1 的纯 CPU 数学可以预先学习，但不把预习或写好文档算作 CSM 已完成。

## 13. 下一次只手敲什么

**先做小组 A：能力记录与 Loader 版本查询。**

第一次只完成能力值对象、Loader 查询、选卡版本日志与只读访问；保留原 1.0 渲染行为。
能逐项解释“头文件 / Loader / Instance / Device”，并正常启动、退出，就是这次小任务的完成点。
之后再做 B 的版本协商与 Features2，不直接把所有 `1_0` 搜索替换为 `1_3`。
