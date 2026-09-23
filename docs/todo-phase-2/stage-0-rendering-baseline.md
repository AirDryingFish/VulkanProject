# 阶段 0：可测量、可观察的渲染基线实施指南

本文对应 [Phase 2 大纲](overview.md) 的阶段 0，接续已有 glTF/PBR、方向光阴影与 HDR 渲染链路。
面向手敲学习：按下面的小组逐步完成，每组写清目的、改动位置、观察方法和完成标准。

评估日期：2026-09-23。源码基线：`98fd00f`（`Support mask material`）及当前工作区。
本文是实施计划；新增的数据类型、字段和接口均为建议，尚未写入源码。本次只核对源码与文档，
未执行构建、图形交互或性能测量，所有功能与运行验收保持待完成。

## 1. 本阶段的目的与范围

**目的：在升级 Vulkan 和加入 CSM/Deferred 前，能够说清画面是否正确、CPU/GPU 时间花在哪里。**

本阶段交付三部分：

1. **测量**：Shadow、Scene、Post + UI 的 GPU 时间，明确范围的 CPU 时间、绘制计数和资源统计。
2. **观察**：曝光与 tone mapper 控制，以及深度、法线、材质和 HDR 亮度视图。
3. **基线**：固定场景与设置下的截图、捕帧说明和性能记录，供阶段 0.5 及后续算法对照。

先使用当前 Vulkan 1.0 的 query 和同步接口。版本协商、Synchronization2、Dynamic Rendering
留到阶段 0.5；Bloom 留到 A-2，TAA/GTAO/CSM/Deferred 不在本阶段实现。
不要求先建立自动化测试框架；每组保留必要的构建和手动观察，未执行的检查标为待验收。

## 2. 当前代码与接入位置

| 当前情况 | 代码位置 | 本阶段的接入方向 |
|---|---|---|
| 每个飞行帧已有 command pool/buffer、fence 和延迟释放列表 | [Renderer.hpp](../../src/Renderer.hpp) 的 `FrameContext` | 同一所有者管理 query pool 与提交状态 |
| `beginFrame()` 等待当前槽位 fence，再 acquire | [Renderer.cpp](../../src/Renderer.cpp) | 等待成功后读回该槽位上次提交的查询结果 |
| `recordFrame()` 顺序录制 Shadow、HDR Scene、Present | 同上 | 在实际 Pass 边界写入 GPU timestamp |
| 阴影关闭仅跳过绘制，仍执行深度清除 Pass | [ShadowMapping.cpp](../../src/ShadowMapping.cpp) 的 `recordShadowPass()` | 统计实际清除成本，不能把开关关闭直接视为 Pass 未执行 |
| Present 包含 tone mapping、阴影预览与 ImGui | `Renderer::recordFrame()` | UI 名称使用 `Post + UI`，避免误称纯 tone mapping 时间 |
| Diagnostics 仅显示 FPS 与 `1000 / FPS` 等信息 | [ImGuiLayer.cpp](../../src/ImGuiLayer.cpp) | 增加最近一次完成的 GPU 结果和明确状态 |
| 应用在 beginFrame 后构建 UI，再生成绘制快照与提交 | [TriangleApplication.cpp](../../src/TriangleApplication.cpp) | GPU 显示过去已完成的帧，CPU 当帧结果下一次 UI 再展示 |
| Post push constants 已预留 EV 与 tone mapper 字段 | [RenderTypes.hpp](../../src/RenderTypes.hpp)、[tonemap.frag](../../assets/shaders/tonemap.frag) | 使用已有参数空间，不扩充已满 128 字节的 Draw push constants |
| 逐帧 UBO 与材质 shader 已有阴影调试入口 | [VulkanTypes.hpp](../../src/VulkanTypes.hpp)、[Buffers.cpp](../../src/Buffers.cpp)、[shader.frag](../../assets/shaders/shader.frag) | 扩展明确的 Debug View 模式，核对 CPU/GLSL 布局 |

第一轮计时直接放在 Renderer 的现有边界内，不先搭建通用 Profiler 或 Render Graph。
后续若文件过大再拆分实现；新增 `.cpp` 时才需要同步 [CMakeLists.txt](../../CMakeLists.txt)。

## 3. 手敲分组与推进顺序

| 小组 | 目的 | 修改重点 | 完成后能观察什么 |
|---|---|---|---|
| A：计时数据与资源 | 明确所有权和设备能力 | RenderTypes、Renderer 初始化/销毁 | 能力日志、每帧 query pool；暂不显示数值 |
| B：GPU 写入与读回 | 接通完整提交生命周期 | beginFrame / recordFrame / endFrame | 最近一次已完成提交的三组耗时 |
| C：Diagnostics 展示 | 正确解释统计值 | Renderer 只读快照、ImGui | ms、数据来源与 N/A 原因 |
| D：CPU/绘制/资源统计 | 区分瓶颈类型 | 应用更新、命令录制、提交和资源统计 | CPU 范围耗时、draw/triangle、分配开销 |
| E：曝光与 ACES fitted | 建立一致的显示条件 | 应用设置、RenderFrameData、Post shader | EV 与 tone mapper 切换，UI 不受影响 |
| F：Debug Views | 分开检查几何、材质与光照 | UBO、材质 shader、Post、ImGui | Depth/Normal/Albedo/Metallic/Roughness/HDR luminance |
| G：固定基线记录 | 为后续改造提供证据 | 现有场景预设与记录 | 可复现的配置、截图、捕帧与测量结果 |

**A–C 是第一轮目标。** 每组可以继续拆成更小的手敲提交；仅声明结构或分配 query pool，
不能标记“GPU 计时完成”。D–G 完成后才形成完整 Stage 0 基线。

## 4. 先约定测量含义

| 指标 | 测量范围 | 不应解释成 |
|---|---|---|
| Frame interval / FPS | 应用观察到的帧间隔，可能含等待与限帧影响 | CPU 工作量或 GPU 独立执行时间 |
| CPU update / record / submit | 各自明确的主机代码区间 | GPU 执行命令所需时间 |
| Shadow GPU ms | 完整 Shadow RenderPass，包括清除和实际绘制 | 只有阴影物体绘制成本 |
| Scene GPU ms | HDR Scene 的开始至结束，包括天空盒、模型与 MSAA resolve | 单个模型或材质 shader 的时间 |
| Post + UI GPU ms | Present RenderPass，包括 tone mapping、可选深度预览和 ImGui | `vkQueuePresentKHR`、显示扫描或纯后处理时间 |

GPU 时间来自命令流中的两个 timestamp。第一版使用 Pass 开始前的 `TOP_OF_PIPE` 与结束后的
`BOTTOM_OF_PIPE`，作为一致的粗粒度观察范围。流水线重叠、队列等待和实现的时间戳落点会影响
结果，三个区间之和不能直接命名为 GPU 总帧时间，也不能直接换算成 FPS。
写入语义参见 [vkCmdWriteTimestamp](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp.html)。

如果之后需要整帧 GPU 区间，应另加一对覆盖整个录制范围的 query，单独说明是否包含等待。
本轮先固定三组、六个槽位；不引入跨队列或 CPU/GPU 时钟校准。

## 5. 小组 A：计时契约、设备能力与资源

**目的：让查询结果有明确的来源、有效性和释放时机。**

### 5.1 建议的数据划分

以下名字是拟新增设计，不是已有接口：

| 数据 | 所有者 | 内容 |
|---|---|---|
| `GpuPass` | RenderTypes | 固定 Shadow、Scene、PostAndUi 三项及数量 |
| `GpuPassTiming` / `GpuTimingSnapshot` | RenderTypes，Renderer 保存最近结果 | 各 Pass 的有效状态、ms、提交序号及本次测量配置 |
| 时间戳能力 | Renderer | supported、graphics queue family、`timestampValidBits`、`timestampPeriod` |
| 每帧查询资源 | `FrameContext` | `VkQueryPool`，默认 `VK_NULL_HANDLE` |
| 本次录制记录 | `FrameContext` | 哪些 Pass 的两端 query 确实写入、对应设置快照 |
| 已提交待读记录 | `FrameContext` | pending、提交序号、已提交的 Pass mask 与配置 |

UI 只读取结果快照，不持有 query pool，也不调用 Vulkan 查询函数。
状态至少区分 `unavailable`（设备不支持）、`pending`（尚无完成数据）、`not executed`
和 `ready`；实际得到的 `0.000 ms` 可以是有效结果，不能用数值是否为零推断状态。

### 5.2 固定 query 索引

每个飞行帧独立创建一个 `VK_QUERY_TYPE_TIMESTAMP` pool，`queryCount = 6`：

| Pass | 开始 | 结束 |
|---|---:|---:|
| Shadow | 0 | 1 |
| Scene | 2 | 3 |
| Post + UI | 4 | 5 |

这些槽位属于 `token.frameIndex`，与 swapchain 的 `token.imageIndex` 无关。
查询结果按上次成功提交保存的 Pass mask 解读，不能用当前 UI 开关判断旧查询是否有效。

### 5.3 手敲顺序

1. 在 RenderTypes 定义显示所需类型，在 Renderer/FrameContext 声明上述状态；所有句柄置空、pending 初始为 false。
2. 在 `Renderer::initialize()` 创建每帧资源前，查询 `context.queueFamilies().graphicsFamily` 对应的队列族属性。
3. 读取该队列族的 `timestampValidBits`，再从物理设备 properties/limits 读取 `timestampPeriod`，后者单位为 ns/tick。
4. `timestampValidBits == 0` 时关闭计时路径并说明原因，继续渲染；所有创建、录制和读回操作受能力状态保护。
5. 支持时在现有帧资源循环创建 query pool，可用已有 debug-name 接口标明 frame slot。
6. 在 `shutdown()` 帧循环销毁非空 pool、清空句柄和状态，覆盖部分初始化失败的清理。

队列能力以实际执行命令的 graphics family 为准，不能只根据 GPU 型号或 API 版本推断。
参见 [VkQueueFamilyProperties](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyProperties.html)。

普通 resize 不改变飞行帧数量，不需要重建 query pool。销毁仍需保证 GPU 不再使用资源：当前
应用 `cleanup()` 会先等待设备空闲，Renderer 独立 shutdown 的调用方也需满足相同前提。

完成标准：

- [ ] 支持能力与换算参数可观察；不支持时正常渲染并显示不可用原因。
- [ ] 每个飞行帧有自己的六个槽位，初始化异常和正常退出都不会漏释放。
- [ ] 首帧不读取尚未使用的 query，资源创建完成不冒充已经测得耗时。

## 6. 小组 B：录制、提交与延迟读回

**目的：利用已有 fence 安全读回过去的 GPU 工作，不为统计额外阻塞每一帧。**

### 6.1 生命周期

```text
beginFrame(frame slot N)
  → 等待该槽位 renderFence
  → 若有已提交待读结果：读取、发布快照、消费 pending
  → acquire swapchain image
  → 重置 command pool

recordFrame(N)
  → begin command buffer
  → vkCmdResetQueryPool(N, 0, 6)
  → 写入三组实际 Pass 的边界 timestamp
  → end command buffer

endFrame(N)
  → reset fence
  → vkQueueSubmit 成功
  → 保存提交序号、Pass mask、配置，标记 pending
  → present

下一次复用槽位 N：再次等待 fence，再读取这一份结果
```

`pending` 表示确实成功提交过，不能在录制完成时提前置位。
当前两帧并行下，UI 看到的是过去的完成结果；无需为显示“当前帧”强制等待刚提交的工作。

### 6.2 精确修改位置

1. **`beginFrame()`**：在 `vkWaitForFences` 成功后、`vkAcquireNextImageKHR` 前调用拟新增的收集函数。
2. **`recordFrame()`**：在 `vkBeginCommandBuffer` 后、`recordShadowPass()` 前录制 `vkCmdResetQueryPool`，并清空本次录制 mask。
3. **Shadow**：在 `recordShadowPass()` 调用前后写 0/1。当前阴影开关关闭时仍写入，因为函数仍清深度并结束 Pass。
4. **Scene**：在 HDR `vkCmdBeginRenderPass` 前写 2，在对应 `vkCmdEndRenderPass` 后写 3。
5. **Post + UI**：在 Present `vkCmdBeginRenderPass` 前写 4，在 ImGui 之后、对应 `vkCmdEndRenderPass` 后写 5。
6. **`endFrame()`**：`vkQueueSubmit` 确认成功后立即发布本槽位的待读状态，再调用 Present。

某个 Pass 以后若完全不录制，就不写它的 query，并在提交记录中明确标为未执行。
当前 Shadow 的 `clear-only` 状态应有真实耗时；它和“完全未执行”不同。

重置 command pool 不会重置 query 状态；本阶段用命令缓冲里的 `vkCmdResetQueryPool`，
放在 RenderPass 外。不要直接引入需要其他版本/feature 的 host query reset。
参见 [query reset 约束](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdResetQueryPool.html)。

### 6.3 读取与单位转换

首版按提交 mask 对每个已执行 Pass 读取两个 query，使用 `vkGetQueryPoolResults`：

- 输出使用两个 `uint64_t`；`dataSize = 2 * sizeof(uint64_t)`，`stride = sizeof(uint64_t)`。
- flags 使用 `VK_QUERY_RESULT_64_BIT`；已等待 fence，不加 `WAIT_BIT`，也不使用 timestamp 不支持的 `PARTIAL_BIT`。
- `VK_SUCCESS` 才计算结果；`VK_NOT_READY` 显示 N/A 并诊断对应的 mask/写入范围，不忙等、不读取未定义数据。
- 当前设计中，完成 fence 后已写入的 query 应当可用；其他 Vulkan 错误按现有错误路径处理。
- 结果消费后清 pending，避免 acquire 失败后重复发布同一份数据；异常样本可丢弃并计数。

不要先把 `VK_NOT_READY` 交给只接受成功结果的 `VK_CHECK`。若以后改用 availability，需同时
修改输出结构和 stride，不能在旧数组布局上只添加 flag。
参见 [query 结果与 availability 布局](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html)。

换算公式：

```text
deltaTicks = (endTick - beginTick) 在 timestampValidBits 范围内取模
gpuMs = double(deltaTicks) * double(timestampPeriod) / 1,000,000
```

差值用无符号 64 位计算。有效位不足 64 时按有效位 mask 截断；64 位情况单独处理，不能执行
`1ULL << 64`。这要求测量区间短于计数器完整回绕周期；极端停顿不作为正常性能样本。

### 6.4 提交与重建边界

| 情况 | 应有行为 |
|---|---|
| 初始化 fence 已 signaled，但尚未提交过 | pending=false，不读 query |
| Acquire 返回 out-of-date | 已先收集旧结果；无新录制/提交，不创建新的 pending |
| 录制中抛错或提交失败 | 不发布新 pending；沿用退出/异常清理，不等待无人 signal 的已重置 fence |
| Submit 成功，Present 返回 out-of-date/suboptimal | 保留 pending，GPU 工作确实已经提交 |
| 普通 resize/场景切换 | pool 可保留；旧结果带原配置或被明确丢弃，不能误标为新场景/新尺寸的样本 |
| 最小化、没有新提交 | 保留上次结果并标注来源，不伪造一个耗时为零的新帧 |

完成标准：

- [ ] 三个区间持续产生有效结果，首次启动与窗口重建没有未初始化查询错误。
- [ ] 没有新增每帧 `vkDeviceWaitIdle`、即时 query 等待或忙轮询。
- [ ] 已录制、已提交、已完成三个状态分开，错误路径不会等待未提交的 fence。

## 7. 小组 C：Diagnostics 展示

**目的：让用户看到可解释的数字，而不是把无效数据当作零毫秒。**

手敲顺序：

1. Renderer 提供最近一次结果的只读 getter；ImGui 在现有 Diagnostics 中读取，不拥有底层句柄。
2. 显示 `Shadow GPU ms`、`Scene GPU ms`、`Post + UI GPU ms`，无结果时显示状态和原因。
3. 展示提交序号及 frame slot；增加采集时的 extent、scene MSAA 和阴影模式，说明结果有延迟。
4. 将现有 `Frame Time` 明确命名为帧间隔；CPU/GPU 数值使用独立标签。
5. 可对 UI 数值做小窗口平滑，但保留原始样本；只有新完成的提交才进入窗口，不能每次刷新重复累计。

记录 mask 与阴影开关等元数据时使用录制该帧的配置。场景/尺寸变化时清空平滑窗口，避免把
不同负载混在一起；若使用配置版本号，旧版本的晚到结果不能重新混入新窗口。

完成标准：

- [ ] 启动时先显示 pending，收到结果后显示 ms；不支持计时的设备显示 unavailable。
- [ ] 阴影关闭时可以显示 `clear-only` 及其耗时；纯跳过 Pass 才显示 not executed。
- [ ] UI 明确 Post 包含 ImGui/阴影预览，移动窗口和切换场景不会让旧值伪装成新配置结果。

## 8. 小组 D：CPU、绘制量与资源统计

**目的：区分 CPU 录制/提交、GPU 渲染与资源规模，为后续批处理和算法优化提供依据。**

### CPU 区间

使用 `std::chrono::steady_clock`，按以下位置分开记录：

- 等待/Acquire：在 Renderer 内分别围住 fence 等待与 Acquire，避免混入 CPU update。
- Update：应用输入、UI、UBO 更新与绘制快照构建，明确起止位置。
- Record：围住 `renderer.recordFrame()`，表示主机录制成本。
- Submit：只围住 `vkQueueSubmit`；Present 主机调用另记，不能把整个 `endFrame()` 都标为 Submit。

这些 CPU 区间可能包含驱动阻塞，它们不代表 GPU 执行时间。UI 生成早于当帧录制/提交，
因此完整 CPU 结果在下一次 UI 中显示，并注明与延迟 GPU 样本的来源关系。

### 绘制与资源

1. 在实际录制绘制命令的分支累加计数，分别统计 Scene mesh 与 Shadow 的 draw 和提交三角形。
2. 当前三角形列表可按 `indexCount / 3 * instanceCount` 统计；这是提交量，不是可见或最终着色的三角形数量。
3. 天空盒、全屏三角形、阴影预览与 ImGui 单独注明统计范围；不把场景对象数当作总 draw 数。
4. 在 Diagnostics 按需/低频更新 VMA 分配统计；区别 allocation bytes、block bytes 与估算的主要 render target 开销。
5. 共享 Mesh/纹理按唯一 GPU 资源计算，重复实例不重复计入分配；预算与驻留显存不冒充同一指标。

VMA 的详细统计可用于人工刷新，避免在测量热点频繁遍历分配。参见
[VMA Statistics](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/statistics.html)。

完成标准：

- [ ] CPU 等待、Update、Record、Submit/Present 和 GPU 时间标签明确。
- [ ] 阴影关闭时 Shadow draw 计数为零，Shadow clear-only 的 GPU 耗时仍可有效。
- [ ] 删除/添加对象后的计数符合实际绘制，资源共享不会导致统计翻倍。

## 9. 小组 E：Exposure 与 ACES fitted

**目的：固定算法比较的显示条件，让 HDR 场景统一通过 Post 输出。**

手敲顺序：

1. 在应用设置中增加 EV 和 tone mapper 选择，默认 `EV = 0`、Reinhard，保持现有画面。
2. 通过 RenderFrameData 传入 Renderer，填充已有 `PostPushConstants.params.x` 与 `modes.x`。
3. 在 Post shader 中先做 `exposed = hdr * exp2(EV)`，再选择 Reinhard 或明确命名的 ACES fitted 近似。
4. 固定参考曲线、输入色彩空间与额外曝光系数；记录实现来源，不把简单拟合写成完整 ACES 流程。
5. 数据调试模式跳过曝光与 tone mapping，但保持输出编码约定；ImGui 与阴影预览继续在显示端绘制。

可从 [Narkowicz 的拟合曲线](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)
开始，界面命名为 `ACES fitted (Narkowicz)`，避免与其他拟合版本混淆。

完成标准：

- [ ] EV 增加 1 时，tone mapping 前的线性亮度乘 2；不要求最终截图像素值也乘 2。
- [ ] `EV = 0 + Reinhard` 与原有默认显示一致，切换曲线不会改变 HDR 场景的光照计算。
- [ ] sRGB 编码只执行一次，UI/数据调试视图不受曝光变化影响；各模式无 NaN/Inf。

## 10. 小组 F：Debug Views

**目的：分别观察几何、材质和光照输入，避免仅凭最终截图判断算法问题。**

第一版继续使用前向路径，材质类调试色在 shader 中产生；无需提前创建 G-buffer。

| 视图 | 建议第一版含义 | 观察重点 |
|---|---|---|
| Albedo | 纹理、factor 与顶点色组合后的线性 Base Color RGB | 纹理色彩空间、材质参数与 UV |
| World Normal | 着色实际使用的法线，包括 normal map 与双面处理；映射到显示颜色 | 法线方向、切线基、双面语义 |
| Metallic / Roughness | 着色实际使用的标量值，标注 roughness 的最小值限制 | glTF 的 B/G 通道及 factor |
| Depth | 视空间线性距离的可视化，注明显示归一化范围；原始设备深度另命名 | 近远裁剪与深度分布 |
| HDR luminance | 曝光/tone mapping 前 HDR 的亮度，采用注明范围的对数灰度或伪彩色 | 高亮与动态范围 |

手敲顺序：

1. 增加一个明确的 Debug View 枚举和 UI 选择，约定它与现有 Shadow Projection 模式的优先级。
2. 通过逐帧 UBO 传递材质调试模式；如扩展 UBO，同步所有消费它的 GLSL 定义与 CPU 布局断言。
3. 保留 Alpha Mask discard，再输出 Albedo/Normal/Metallic/Roughness 等调试色，不能让镂空处变实心。
4. Depth 首版可由片元世界坐标和 view 矩阵计算视空间距离，明确它不是在采样现有 depth attachment。
5. HDR luminance 在 Post 中读取原始 HDR 颜色后计算；对应模式绕过曝光与 tone mapper。
6. 数据模式明确处理天空背景与 clear color；颜色仅用于观察，显示编码后的截图不能直接当原始数值读回。

当前 HDR depth 可能是 MSAA 附件且没有采样用途。不要为了 Depth 预览直接采样它；若后续要
真正采样深度，需要另外处理 usage、store、descriptor、layout、采样数及深度 resolve 契约。
Draw push constants 已经是 128 字节，本组不继续往里面追加字段。

完成标准：

- [ ] 各视图有清楚含义与范围，改变 EV/tone mapper 不改变数据视图。
- [ ] Alpha Mask、双面材质和天空背景正确；正常 Lit 模式可以恢复。
- [ ] HDR luminance 不使用已经压缩过的显示颜色，法线视图与实际着色使用同一法线。

修改 Shader 后需运行现有编译脚本，并确认新的 SPIR-V 已进入实际运行目录。CMake 现有资源复制
挂在链接后的 POST_BUILD，单独改 Shader 不一定触发它。可显式用 `cmake -E copy_directory`
同步 `assets` 到对应 `build/<preset>/assets`，避免观察到旧 Shader。

## 11. 小组 G：固定基线与手动观察

**目的：让后续 API 迁移和渲染算法具有可重复的对照条件。**

推荐先用项目已有场景；资源缺失时按 [场景预设说明](../scene-presets.md) 处理：

| 场景 ID | 主要用途 |
|---|---|
| `shadow-playground` | 阴影开关、PCF、clear-only、Shadow GPU 成本 |
| `material-lab` | Metallic/Roughness/Normal、曝光与 tone mapping |
| `damaged-helmet` | 纹理、法线、AO/Emissive 材质输入 |
| `sponza` | 较多绘制、Alpha Mask、双面材质和室内外观察 |

每份记录固定 GPU/驱动、构建模式、Validation、extent、实际 Scene MSAA、阴影分辨率与 PCF、
灯光/IBL、相机、EV、tone mapper、debug 模式及 UI/预览开关。GPU 样本携带原配置或采集版本号。

实施顺序：

1. 保存阶段 0 改动前的默认场景截图与设置；计时接通后补第一份测量。
2. 在固定配置下预热，例如 120 个成功提交，再收集后续 300 个有效完成样本；样本数可调整但必须记录。
3. 统计原始样本的均值及波动范围，配置变化时重新预热，不对平滑后的 UI 数值再次求“原始均值”。
4. 性能比较优先同一 Release 配置，Debug/Validation 结果单独记录；注明 VSync/限帧，不仅比较 FPS。
5. 用 RenderDoc 或实际平台可用的捕帧工具检查一次 Shadow → HDR Scene → Present 的资源与绘制。
6. 运行 resize、最小化恢复、场景切换、导入/删除对象和正常退出的观察清单。

Windows 下可使用已有命令检查启动和退出；以下是待执行示例，不是已经通过的结果：

```powershell
cmake --build --preset windows-mingw-debug --parallel
.\build\windows-mingw-debug\vulkan.exe --list-scenes
.\build\windows-mingw-debug\vulkan.exe --scene shadow-playground --frames 120
```

`--frames` 只控制成功绘制次数与退出，不会自动导出计时或完成交互验收。基线统计应在实际收集到
完成样本后记录；最终几帧尚未读回的 query 不计入样本数量。Linux/macOS 使用各自 preset 与启动方式。

记录模板：

| 场景/固定配置 | 有效样本数 | Shadow ms | Scene ms | Post + UI ms | CPU Record/Submit ms | Scene/Shadow draw | 分配开销 | 状态 |
|---|---|---|---|---|---|---|---|---|
| 待填写 | 待测 | 待测 | 待测 | 待测 | 待测 | 待测 | 待测 | 待验收 |

关闭阴影主要影响 Shadow 绘制，缩小窗口主要影响屏幕分辨率相关工作；不预先要求某个固定加速
比例。如果耗时变化与预期不一致，先检查采集范围、配置与负载，再解释硬件或驱动影响。

## 12. 阶段完成标准与下一步

- [ ] A–C：三组 GPU 计时、状态和延迟读回完整；未执行与有效零耗时不混淆。
- [ ] D：CPU/绘制/资源统计含义明确，能够区分等待、录制、提交与 GPU 工作。
- [ ] E：曝光和 ACES fitted 可切换，默认 Reinhard 可作旧画面对照。
- [ ] F：基础 Debug Views 能检查材质、法线、深度和 HDR 输入。
- [ ] G：至少保存材质、阴影和复杂场景的固定基线；缺失资源或不可用工具明确记录，不冒充验证。
- [ ] 已执行的构建/运行检查有结果，未执行项保持待验收，没有已知 query 生命周期或同步错误。

具备这些基线后进入阶段 0.5：版本与能力管理 → Synchronization2 → Dynamic Rendering。
迁移期间使用本阶段的同一场景、计时范围和显示设置，比较正确性与成本。

## 13. 下一次实际手敲范围

**先做小组 A：计时类型、能力查询、每帧 query pool 与清理。**

下一组只涉及 RenderTypes、Renderer 的类型声明与初始化/销毁；完成后应能编译运行并看到能力
日志，画面保持原状。之后再按小组 B 接通记录/提交/读回，最后用小组 C 加入 Diagnostics。
尚未产生 GPU 结果时显示 pending 即可，不填入估计耗时。

## 14. 参考资料

- [Khronos Timestamp Queries 示例](https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html)：理解 query 的创建、写入和换算；本项目使用已有帧 fence 延迟读回，不照搬即时等待。
- [vkCmdWriteTimestamp](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp.html)：写入阶段与有效队列要求。
- [vkCmdResetQueryPool](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdResetQueryPool.html)：重置位置与执行约束。
- [vkGetQueryPoolResults](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html)：结果格式、状态与 availability。
- [Phase 2 大纲](overview.md)：阶段 0.5、CSM/Deferred、光栅专题及 ReSTIR 的后续依赖。
