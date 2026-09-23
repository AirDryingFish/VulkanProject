# Phase 2：从高级光栅渲染到 ReSTIR

本文接续 [Phase 1 开发路线图](../todo-phase-1/overview.md)，作为后续手敲开发的大纲。

规划日期：2026-09-23。源码评估基线：`98fd00f`（`Support mask material`）。
本文记录目标与依赖关系，不表示下面的功能已经实现，也不替代运行验收记录。

项目目标包含引擎图形工程师求职展示：逐步积累渲染算法、GPU 数据与同步管理、性能分析和技术
表达能力。每个重点功能应有可运行的对照、可解释的取舍和实际测量结果；新增任务均保持待完成状态。

分阶段手敲指南：

- [阶段 0：可测量、可观察的渲染基线](stage-0-rendering-baseline.md)

## 1. 当前起点与 Phase 1 收尾边界

当前基础前向 PBR 渲染器已具备进入新阶段的条件，但 Phase 1 原路线图尚未全部完成。

| 能力 | 当前情况 | Phase 2 如何复用 |
|---|---|---|
| VulkanContext / Swapchain / Renderer、VMA 资源封装 | 核心代码已有 | 继续扩展资源与 pass，保持所有权明确 |
| glTF、Mesh 共享、材质、节点变换 | 主链路已有，已补双面与 Alpha Mask | 作为光栅和光追的共同场景输入 |
| 方向光阴影、PCF、深度预览 | 已有 | 扩展成 CSM，并作为光追阴影的对照 |
| HDR Scene → Post → Present → ImGui | 已接通 | Deferred 和光追继续输出线性 HDR |
| 曝光、ACES fitted | 尚未实现，当前只有固定 Reinhard | 纳入阶段 0 的显示基线 |
| 材质与深度 Debug Views | 目前主要是阴影相关视图 | 补齐后检查 G-buffer、重投影和光追命中 |
| GPU timestamp / pass 耗时 | 尚未实现 | 作为本阶段第一个手敲任务 |
| Bloom、FXAA | 尚未完成 | Bloom 纳入 A-2 显示收尾；FXAA 选做，主要抗锯齿目标为 TAA |
| 多光源剔除、屏幕空间效果与绘制优化 | 尚未建立本大纲中的进阶路径 | 纳入阶段 2、A-1、A-3、A-4，逐项实现与测量 |
| Vulkan API / 能力管理 | 当前请求 Vulkan 1.0，设备初始化主要使用旧式 features | 阶段 0.5 建立版本协商与 Features2，接入 Synchronization2 / Dynamic Rendering |
| 光追能力 | 尚未建立 RT feature 路径 | 阶段 4 复用能力框架，补齐 BDA、加速结构、Ray Query 及依赖 |

Phase 1 中的旧复选框和历史实施顺序保留为原始记录，不能直接代表当前进度。迁移任务时应区分：

- **实现已有、待验收**：不能因为代码存在就标记为全部验证通过。
- **尚未实现、明确延期**：FXAA 和未纳入本大纲的后处理专题保留为选做，不以此阻塞主线。
- **转入 Phase 2**：CSM、基础 GPU 测量、曝光/ACES、Bloom、所需调试视图及 Vulkan 能力管理与现代化。

Phase 1 的 Stage 8/9 不必整章做完才进入本阶段。GPU 测量提前到阶段 0，版本与能力管理、
Synchronization2、Dynamic Rendering 纳入阶段 0.5。Timeline Semaphore、Bindless 和 Render Graph
在出现对应需求时引入，不与本次 API 现代化绑成一次整体重构。

## 2. 总目标与阶段依赖

**总目标：构建可分析、可优化的现代实时渲染器，在同一套场景、材质与 HDR 输出基础上，比较
不同算法的正确性、画质和 CPU/GPU 成本，并逐步进入 ReSTIR。**

推荐学习顺序：

```text
阶段 0：可测量、可观察的基础基线
    ↓
阶段 0.5：版本与能力管理 → Synchronization2 → Dynamic Rendering
    ↓
阶段 1：CSM
    ↓
阶段 2：Deferred / G-buffer / Tiled 或 Clustered 多光源剔除
    ↓
阶段 3：Motion Vectors / History / 完整基础 TAA
    ↓
A 基础收尾：A-1 GTAO + A-2 Bloom + A-3 CPU 剔除与实例化
    ├── A 进阶：A-3 GPU 剔除 / Indirect / Hi-Z、A-4 SSR、A-5 选做专题
    │           可穿插推进，不阻塞阶段 4
    ↓
阶段 4：BLAS / TLAS / Ray Query
    ↓
阶段 5：Monte Carlo 直接光照参考路径
    ↓
阶段 6：ReSTIR DI
    ↓
阶段 7：间接光、ReSTIR GI / PT（后续研究目标）
```

这是学习顺序，不是全部算法的硬依赖：CSM 并不是光追的前提，Deferred 也不是 ReSTIR
唯一可用的表面数据来源。保留这些步骤，是为了逐步建立阴影、多附件、时域数据和采样的经验。

为保持已有学习记录的连续性，保留阶段 0–7 编号，在 0 与 1 之间插入阶段 0.5；新增光栅专题
使用 A-1 至 A-5 标识。0.5 是项目选定的工程学习步骤，CSM/Deferred 本身不强制要求这些新版 API。
A-1/A-2/A-3 基础部分可在各自输入就绪后穿插完成，无需机械等待前一个专题全部结束。

| 交付范围 | 包含内容 | 完成目标 |
|---|---|---|
| **里程碑 A 基础：现代光栅渲染器** | 阶段 0、0.5、1–3、A-1、A-2、A-3 基础部分 | Vulkan 能力管理与现代化、可测量的 Forward/Deferred、稳定 CSM、多光源剔除、TAA、GTAO、HDR 后处理、CPU 剔除与实例化 |
| **里程碑 A 进阶：作品集专题** | A-3 GPU 路径、A-4、A-5 中选择 1–2 项深入 | 优先 GPU 剔除与间接绘制、SSR；Hi-Z 与体积雾等按专题继续扩展 |
| **里程碑 B：混合渲染与 ReSTIR DI** | 阶段 4–6 | 加速结构、光线可见性、随机直接光照基线及蓄水池复用 |
| **后续研究** | 阶段 7 | 间接光、ReSTIR GI / PT，分别研究和验收 |

A 基础可独立作为作品交付，之后即可进入 B；A 进阶与阶段 7 不阻塞 B。
不要求实现所有 AO/阴影/抗锯齿变体，也不把 Mesh Shader、虚拟几何或完整商业引擎架构作为本阶段门槛。

## 3. 阶段 0：建立可比较的渲染基线

**目的：每次增加算法后，都能说明画面改变了什么、时间花在哪里。**

实施细节见 [Stage 0 手敲指南](stage-0-rendering-baseline.md)，按 A–G 分组推进；先完成 A–C 的 GPU 计时。
当前 Post 统计包括 tone mapping、阴影预览和 ImGui，界面应标为 `Post + UI`。

### 开发任务

- [ ] 给 Shadow、Scene、Post pass 增加 GPU timestamp，ImGui 显示各 pass 毫秒数。
- [ ] 查询时间戳支持能力和 `timestampValidBits`，用 `timestampPeriod` 换算；不支持时显示 unavailable。
- [ ] 每个飞行帧管理自己的 query 区间，等待对应 fence 后读取和复用，避免立即等查询结果导致停顿。
- [ ] 区分帧间隔、CPU 提交耗时和 GPU pass 耗时；`1 / FPS` 不命名为 CPU 或 GPU 独立耗时。
- [ ] 增加 draw call、triangle 的基础统计，记录分辨率、MSAA、阴影分辨率与当前渲染模式。
- [ ] 区分对象数、实例数、提交的绘制条目和实际 API 调用数；多 Pass 的三角形统计注明统计范围。
- [ ] 记录主要 buffer/image 的分配开销；区分资源估算、分配器统计与设备内存预算，不混称实际驻留显存。
- [ ] 接入 Exposure EV；保留 Reinhard，并增加明确命名的 ACES fitted 近似。
- [ ] 保证场景先输出线性 HDR，再进行曝光、tone mapping 和一次 sRGB 编码；UI 不受曝光影响。
- [ ] 增加 Depth、World Normal、Albedo、Metallic、Roughness、HDR luminance 调试显示。
- [ ] 固定 Sponza、材质球和阴影实验场景的相机、灯光与窗口尺寸，保存对照条件。
- [ ] 随功能加入多光源、重复实例、运动物体场景；保留随机种子与相机路线，便于复现。
- [ ] 使用 RenderDoc 或可用的 GPU 分析工具记录一次关键帧，说明主要 pass 的输入、输出和瓶颈。
- [ ] 记录 Debug/Release、Validation、resize、最小化恢复、场景切换和退出结果。

### 完成标准

同一场景和设置下可以查看 Shadow / Scene / Post 的时间；调试图能区分几何、材质、光照与
显示问题。已有渲染链路无已知的资源生命周期或同步错误。

本组不要求建立大型自动化测试框架；没有执行的运行检查明确记录为“待验收”。

## 4. 阶段 0.5：Vulkan 能力管理与现代化

**目的：建立明确的版本、扩展和 feature 契约，并在新增大量 Pass 前更新同步与渲染附件的表达方式。**

### 版本目标与范围

- **首选开发目标为 Vulkan 1.3**；最低支持版本在核查 Windows/Linux/macOS 目标设备后明确记录。
- SDK/头文件版本、Loader 支持版本、设备 API 版本与已启用 feature 分别记录；改 `apiVersion` 不会自动启用功能。
- 能力查询与启用管理属于必做；Synchronization2、Dynamic Rendering 纳入本项目主线，分组迁移。
- 若目标设备需要较低版本兼容，评估对应 KHR 扩展及全部依赖；只对实际实现并验证的组合声明支持。
- 必选能力缺失时提供明确原因；选做能力不可用时保留基础渲染。是否维护旧 RenderPass 路径由目标设备决定。
- Vulkan 1.4 及后续特性按具体用途评估，不为追版本号一次开启所有新 feature，也不预先承诺跨平台可用。

核心版本与扩展路径参考 [Khronos 版本说明](https://docs.vulkan.org/guide/latest/versions.html)；
新功能需要显式查询和启用，参见 [Features 启用说明](https://docs.vulkan.org/guide/latest/enabling_features.html)。

### 第一组：版本协商与能力记录

**目的：先统一设备初始化和依赖配置，保留现有渲染方式作为迁移基线。**

手敲顺序：

1. 核对 SDK/头文件和 Loader；通过可用的 `vkEnumerateInstanceVersion` 查询 Loader 上限，入口不存在时按 1.0 处理。
2. 明确应用请求的 Instance 版本，选卡时再查询物理设备版本、扩展、feature、queue 和所需格式/limit。
3. 建立能力记录，分开保存“设备支持”和“应用启用”；区分 Instance 版本、Device 版本及实际设备使用级别。
4. 用 `VkPhysicalDeviceFeatures2` 与适用的 `pNext` 链查询，另建只开启所需功能的设备创建链。
5. 选择核心或扩展路径，匹配扩展名、依赖、feature 结构和函数入口；避免重复挂入同一能力的核心/扩展结构。
6. 统一 Context、VMA、ImGui 的版本配置，保留现有 anisotropy 与平台 portability 需求。
7. 明确 Shader 编译目标和所需 GLSL/SPIR-V 能力，两套编译脚本使用与支持范围一致的 `--target-env`。

当前接入位置：

| 位置 | 要处理的内容 |
|---|---|
| [TriangleApplication.cpp](../../src/TriangleApplication.cpp)、[AppConfig.hpp](../../src/AppConfig.hpp) | 版本请求与必选/可选扩展策略，替换分散的硬编码 |
| [VulkanContext.hpp](../../src/VulkanContext.hpp)、[VulkanContext.cpp](../../src/VulkanContext.cpp) | Loader/设备协商、Features2、核心/扩展入口、能力记录与 VMA 配置 |
| [ImGuiLayer.cpp](../../src/ImGuiLayer.cpp) | 统一 API 版本；第三组迁移时再配置 Dynamic Rendering 模式与附件格式 |
| [compile-shaders.bat](../../scripts/compile-shaders.bat)、[compile-shaders.sh](../../scripts/compile-shaders.sh) | 明确编译目标；新增 shader 能力不能超出选定设备路径的支持范围 |

完成标准：

- [ ] 日志区分请求版本、Loader/设备支持版本、有效运行路径及启用功能，不把 SDK 版本当运行能力。
- [ ] 查询到但未使用的 feature 不会被全部照搬启用；不满足必选能力时报告具体缺项。
- [ ] VMA 使用与实际设备路径一致的配置，ImGui 不再独立硬编码 1.0。
- [ ] 原有场景在初始化改动后仍可运行；分别记录已验证的平台，未执行的平台保持待验收。

若将 `VkPhysicalDeviceFeatures2` 挂入设备创建链，`pEnabledFeatures` 应置空，基础功能放入该结构。
现有 Shader 不必为了版本升级使用更高阶的语言特性，已兼容的旧 SPIR-V 可以继续使用。

### 第二组：Synchronization2

**目的：让每次资源读写、layout 转换和提交等待的同步范围更清楚，方便后续 Compute 与光追扩展。**

手敲顺序：

1. 查询并启用 `synchronization2`，按第一组选择使用核心入口或 KHR 扩展入口。
2. 从一条明确的上传/采样依赖开始，引入 `VkDependencyInfo`、`VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2` 和 `vkCmdPipelineBarrier2`。
3. 分批整理 Shadow → Scene、HDR → Post、上传 → 首次使用的 stage/access/layout；需要时迁移旧 RenderPass 的依赖表达。
4. 使用 `vkQueueSubmit2` 描述提交，核对 wait/signal 的 stage 范围，先保留现有每帧 fence 和 WSI binary semaphore。
5. 需要统一时间戳接口时迁移到 `vkCmdWriteTimestamp2`，保留阶段 0 的采集区间与读取规则。

完成标准：

- [ ] 每组改动都能说明生产者、消费者及所需同步范围，没有仅替换函数名而漏掉依赖。
- [ ] 上传、Shadow/HDR/Post、resize、场景切换与退出经过运行检查；开启可用的同步验证检查。
- [ ] GPU 时间统计仍有效；先比较迁移前后的正确性，不预设更换 API 本身会带来加速。

主要修改入口是 [UploadCommands.cpp](../../src/UploadCommands.cpp)、[Renderer.cpp](../../src/Renderer.cpp)
及具体 Pass。Synchronization2 不自动推导同步，也不要求同时实现 Timeline Semaphore 或异步队列。
迁移规则参考 [Khronos Synchronization2](https://docs.vulkan.org/guide/latest/extensions/VK_KHR_synchronization2.html)。

### 第三组：Dynamic Rendering

**目的：以明确的附件和 Pipeline 格式配置扩展后续 CSM/G-buffer，减少 RenderPass/Framebuffer 对象管理。**

手敲顺序：

1. 查询并启用 `dynamicRendering`；给 GraphicsPipeline 配置增加颜色/深度/模板附件格式，支持 `VkPipelineRenderingCreateInfo`。
2. 先迁移较简单的 Post/Present 链路，以 `VkRenderingInfo`、`VkRenderingAttachmentInfo` 及 Begin/EndRendering 指定附件。
3. 当前 tone mapping、阴影调试叠加和 ImGui 处于同一个 Present RenderPass；迁移时同步调整这些 Pipeline 与 ImGui 后端模式、格式和采样数。
4. 用显式 barrier 处理原来由 RenderPass initial/final layout 与依赖表达的职责，包括 swapchain acquire/present 边界。
5. 再迁移 HDR Scene 与 Shadow；保留 load/store、clear、MSAA/resolve、深度和采样数语义。
6. 更新资源重建和销毁流程，只移除已迁移 Pass 对应的 framebuffer/render pass；IBL 预计算路径可单独后续迁移。

完成标准：

- [ ] Post/Present、HDR Scene、Shadow 的主帧路径完成迁移，画面与阶段 0 基线一致。
- [ ] Pipeline 附件格式/数量、采样数和实际渲染附件匹配；ImGui、阴影调试和 MSAA resolve 正常。
- [ ] Resize、最小化恢复与资源释放有效；新增 CSM/Deferred 默认沿用本组附件与同步约定。
- [ ] 记录暂留的旧式 Pass 或兼容路径，迁移中没有提前销毁仍被使用的对象。

主要修改入口为 [GraphicsPipeline.cpp](../../src/GraphicsPipeline.cpp)、[PostProcessing.cpp](../../src/PostProcessing.cpp)
以及 Renderer/ImGuiLayer。Dynamic Rendering 仍需要显式同步和 layout 管理；参考
[Khronos 示例](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html)。

### 后续特性的引入时机

| 能力 | 安排 | 实施边界与观察目标 |
|---|---|---|
| Timeline Semaphore（1.2 核心或对应 KHR 扩展） | 上传、多次提交或跨队列依赖复杂时选做 | 定义递增值与资源回收时机，从一个提交链路迁移；遵循 WSI 规则保留 acquire/present 所需 binary semaphore |
| Descriptor Indexing / Bindless（1.2 的相关能力或 EXT 路径） | A-3 大量材质提交或阶段 4 任意命中材质访问需要时 | 仅启用实际使用的索引/数组 feature，建立纹理索引、容量、槽位生命周期与回退；不是所有 indirect draw 的前提 |
| Draw Indirect Count（1.2 核心或对应 KHR 扩展） | A-3 GPU 压缩绘制列表需要时选做 | 固定命令数版本先可用，再比较 GPU 生成 count 的收益 |
| Buffer Device Address（1.2 核心或对应 KHR 扩展） | 阶段 4 所选加速结构路径必做 | 同时处理 feature、buffer usage、VMA/内存分配标志与地址生命周期 |
| Acceleration Structure / Ray Query 及依赖 | 阶段 4、里程碑 B 必做 | 单独查询并启用扩展与 feature；API 版本达到 1.3 也不能假定支持光追 |
| Extended Dynamic State、1.4 及后续特性 | 解决具体问题时选做 | 例如减少特定状态组合产生的 Pipeline；以适用性、成本和目标设备支持决定 |

Timeline Semaphore 的提交组织参考 [官方示例](https://docs.vulkan.org/samples/latest/samples/extensions/timeline_semaphore/README.html)。
Render Graph 是本项目的架构选择，不是升级 Vulkan 必须同时实现的 API 特性。

## 5. 阶段 1：Cascaded Shadow Maps

**目的：解决单张方向光阴影图覆盖大场景时，近处细节不足的问题。**

### 手敲顺序

1. 增加 cascade 配置，第一版固定四级；确定相机视距与 shadow distance 的含义。
2. 在相机视空间计算分割距离，支持均匀/对数混合的 split 参数。
3. 求各级视锥角点，计算 light view/projection，并保留可能投影到接收区域的遮挡物范围。
4. 创建每帧的 depth texture array 及每级 image view，用阶段 0.5 的 Dynamic Rendering 逐级指定深度附件并录制；仅保留的旧式兼容路径使用 framebuffer。
5. 为各级写入不同矩阵，在主着色中按视空间深度选择 cascade 并执行 PCF。
6. 加入 cascade 着色预览和每级深度图预览。
7. 使用稳定投影尺寸和 texel snapping 减少抖动；增加每级 bias 与分界过渡。

### 完成标准

- [ ] 近景阴影精度优于同等单图覆盖范围的基线。
- [ ] 相机平移、旋转时没有明显跳动，分级边界无明显接缝。
- [ ] Alpha Mask 和双面材质保持现有阴影语义，建筑/植被不退化为实心轮廓。
- [ ] 可以切换单张阴影与 CSM，对比画面、GPU 时间和内存开销。

第一版不同时加入 layered rendering、几何 shader、GPU culling 或点光源 cubemap 阴影。

## 6. 阶段 2：Deferred Rendering 与多光源剔除

**目的：建立可复用的表面数据，再通过缩小每像素需要处理的光源集合，研究多光源场景的性能。**

### 手敲顺序

1. 增加 Forward / Deferred 模式开关，保留 Forward 作为参考路径。
2. 明确第一版 G-buffer 契约：深度、世界空间法线、Base Color、Metallic / Roughness / AO、Emissive。
3. 明确各附件格式、通道、颜色空间与使用阶段；第一版采用单采样，不直接沿用前向 MSAA。
4. Geometry pass 按阶段 0.5 的 Dynamic Rendering 配置多个 G-buffer 附件；写入法线贴图后的 shading normal，并明确几何法线是否另存。
5. Lighting pass 从深度和逆矩阵重建位置，计算方向光、点光源、CSM 与 IBL，输出 HDR。
6. 对背景/无几何像素作明确处理，保留天空盒与现有 Post / Present 链路。
7. 提供逐附件可视化，与 Forward 的材质和光照结果对照。
8. 建立可扩展的 GPU light buffer，明确位置、颜色、强度、衰减与影响范围；先支持点光源。
9. 保留逐像素遍历全部光源的基线，创建可重复的多光源场景，逐步增加光源数量与重叠程度。
10. 选择 Tiled 或 Clustered 一种剔除方案；学习版默认先实现 Compute Tiled，再按测量需要扩展。
11. 建立 tile/cluster 光源列表，处理计数、存储容量、同步与溢出，Lighting pass 只遍历关联光源。
12. 增加光源数量热图和剔除耗时统计，与朴素路径比较总成本；聚光灯在点光源路径稳定后补充。

### 完成标准

- [ ] 深度重建符合项目 Vulkan `[0,1]` 深度与 viewport / projection 约定。
- [ ] Forward / Deferred 的法线、材质和直接光照一致，差异能由附件精度或抗锯齿方式解释。
- [ ] Opaque 与 Alpha Mask 正常；透明 Blend 保留为后续 Forward pass，不强塞入普通 G-buffer。
- [ ] Resize 后图像/view、descriptor、渲染附件及 Pipeline 格式配置全部有效；显示各 pass 时间与 G-buffer 内存开销。
- [ ] 剔除开启/关闭时，同一光源范围与衰减定义下的光照一致，没有漏灯或边界突变。
- [ ] 光源列表溢出可见且有正确性回退，不越界写入，也不静默丢弃光源。
- [ ] 对不同光源数量和屏幕覆盖率记录剔除、光照及总 GPU 时间，解释性能转折点。
- [ ] 能说明 Tiled 的二维屏幕分块与 Clustered 增加深度分层的差异，完成一种即可收口。

Deferred 不保证所有场景更快。本阶段将一种光源剔除方案纳入必做范围，实际收益由数据说明。
多光源比较时固定阴影光源数量，第一版不要求为每盏灯生成阴影图。Clustered 可服务于 Deferred，
无需为它再写一套几何管线。第一版列表只承诺覆盖不透明/Mask 表面；后续透明路径需重新评估
依赖不透明深度的剔除范围。使用阶段 0.5 的能力框架确认 Compute 队列、格式和所需 feature，
扩展同步约定以覆盖 G-buffer 写入、剔除列表写入和 Lighting 读取，不等到光追阶段才处理。
不要同时实施 Visibility Buffer、Bindless 与 Render Graph 重构。

## 7. 阶段 3：时域数据与完整基础 TAA

**目的：完成可独立演示的时域抗锯齿，同时为低采样数降噪和 ReSTIR 建立可靠的跨帧对应关系。**

### 手敲顺序

1. 保存当前/上一帧 view-projection 与对象变换，引入稳定的对象/材质身份。
2. 定义 motion vector 的方向、单位和坐标空间，覆盖相机运动与对象运动。
3. 保存 history depth、normal、color 等必要图像，提供 motion/reprojection 调试视图。
4. 通过越界、深度、法线和身份检查拒绝失效历史，处理新暴露表面。
5. 先用最小 temporal accumulation 验证重投影，再加入投影 jitter，约定速度是否包含 jitter 差值。
6. 实现完整基础 TAA resolve：历史采样、邻域统计、history clamp/clip 与可调混合权重。
7. 明确线性 HDR 的历史存储与曝光约定；若引入 pre-exposure，历史重用时需补偿曝光变化。
8. 对比细线、远处纹理、镜面高光和移动轮廓，调整闪烁、清晰度与拖影之间的取舍。
9. 在 resize、相机跳转、场景切换、渲染模式变化时显式清空历史。

### 完成标准

- [ ] 静止场景可收敛，相机与对象运动时没有明显历史错位或长期拖影。
- [ ] 同分辨率下保留无 TAA 对照，录制静止与运动结果，说明改善与仍存在的模糊/闪烁。
- [ ] Jitter 开关、历史裁剪和历史权重可独立观察；新显露表面不会持续使用旧物体颜色。
- [ ] 可观察 history rejection、history age 和 motion vector。
- [ ] 区分上一张已渲染图像与“当前 frame slot 的旧内容”；历史索引不使用 swapchain imageIndex。
- [ ] 历史读写有明确同步，不能因为使用 ping-pong 数组就假设多帧并行下天然安全。

仅分配 history 或实现静态平均不算本阶段完成。基础 TAA 属于 A 的交付范围；超分辨率、复杂
透明材质响应和高级重建策略继续选做。AO、SSR、ReSTIR 可复用重投影约定，但分别管理各自的
历史内容与拒绝条件；不能直接把最终 TAA 颜色当作它们的算法历史。

## 8. 里程碑 A 的光栅补充专题

### A-1：屏幕空间环境遮蔽（基础必做）

**目的：表现墙角、柱脚和接触区域的间接光遮蔽，学习深度采样、重建与去噪。**

前置输入：阶段 2 的深度/法线；时域版本使用阶段 3 的重投影基础。

手敲顺序：

1. 用简单 SSAO 验证位置重建、法线方向和采样半径；半径使用明确的视空间单位。
2. 以 GTAO 作为深入实现目标，明确所选论文/参考实现的近似与质量参数，无需实现所有 AO 变体。
3. 加入背景、屏幕边缘和深度不连续处理，提供原始 AO 调试图。
4. 实现深度/法线引导的边缘保持滤波；半分辨率版本需要相应的上采样处理。
5. 使用独立 AO history 做时域稳定，复用速度与失效判断，观察运动物体边缘与新显露表面。
6. 合成到合适的间接漫反射项；明确与 glTF 材质 AO 的组合策略，避免重复过度遮蔽。

完成标准：

- [ ] AO 关闭时恢复原有间接光结果；直接光和 Emissive 不被统一乘黑。
- [ ] 原始、空间滤波、时域结果可分别显示，墙角之外没有明显跨边缘漏黑或长期拖影。
- [ ] 比较采样数、半径和分辨率的画质/耗时，记录细薄物体与屏幕外信息缺失等限制。
- [ ] 至少交付一个完整 GTAO 方案及其对照；SSAO 保留为学习基线，无需独立追求全部质量优化。

AO 不提供间接反弹光；镜面遮蔽如需加入，应使用独立、明确的近似，不能默认复用同一乘数。

### A-2：HDR 显示与 Bloom 收尾（基础必做）

**目的：完成可控的显示链路，保证算法比较使用一致曝光，并改善高亮区域的呈现。**

前置输入：现有 HDR 链路与阶段 0 的曝光/ACES；沿用阶段 3 的 TAA 输出约定。

手敲顺序：

1. 复用阶段 0 的 Exposure EV、Reinhard、ACES fitted 开关，固定对照图的曝光与显示变换。
2. 明确初版顺序：线性 HDR 场景 → TAA resolve → Bloom → 曝光/tone mapping → 输出编码 → UI。
3. 在 tone mapping 前建立 Bloom 的高亮提取、逐级降采样/上采样与合成，约定阈值和强度单位。
4. 增加各级 Bloom 预览，检查 resize、奇数尺寸和接近最小窗口尺寸时的资源重建。
5. 自动曝光作为选做：亮度统计、目标曝光、时间适应；拍摄算法对照时锁定曝光。

完成标准：

- [ ] Bloom 可独立关闭；亮区有可控扩散，暗部不会因合成错误整体泛灰。
- [ ] UI 不参与 Bloom 和曝光；用于 TAA 的场景历史不被 Bloom/UI 污染。
- [ ] 颜色编码只执行一次；各级资源开销与 GPU 时间可观察。
- [ ] 明确 ACES fitted 是近似；不将其标为完整 ACES 色彩管理流程。

FXAA、景深和运动模糊保留选做，不作为 A 基础或 B 的完成门槛。

### A-3：可见性、实例化与 GPU 绘制（基础必做 + 重点进阶）

**目的：减少无效几何工作与重复 CPU 提交，建立可解释的场景数据和绘制优化路径。**

基础手敲顺序：

1. 为 Mesh/实例维护包围盒或包围球，正确处理变换、非均匀缩放与更新。
2. 实现 CPU 视锥剔除，保留全量绘制开关，显示可见/被剔除实例与包围体。
3. 按兼容 Mesh、Material 和 Pipeline 分组；Mask、双面等状态必须正确区分。
4. 建立 GPU 每实例变换与对象身份数据，让同组实例通过 instanced draw 绘制。
5. 固定重复实例场景，比较逐对象、剔除、实例化三种配置的 CPU 提交成本与 GPU 时间。

基础完成标准：

- [ ] 与全量绘制相比没有误剔除；跨视锥边界、相机近裁剪面与对象移动时结果正确。
- [ ] 实例化后拾取、材质、法线和 motion vector 身份保持一致。
- [ ] 相机可见集合与 CSM 阴影投射物集合分别处理，屏幕外投射物仍能产生正确阴影。
- [ ] 统计实例、批次与 API 调用，解释批处理受材质/网格差异限制的原因。

进阶手敲顺序：

1. 把可见性判定移至 Compute，建立可见实例列表和 indirect draw 参数，保留 CPU 对照。
2. 按实际用法查询 indirect 相关 feature/limit；第一版可使用固定命令数量和零实例数，无需立即依赖 GPU draw count。
3. 明确 Compute 写入到间接参数读取、顶点阶段实例读取的同步，管理每帧缓冲和容量。
4. 增加 Hi-Z 深度金字塔及保守遮挡剔除，约定普通/reversed depth 对应的归约和比较方向。
5. 明确深度来源与时序；使用上一帧深度时处理相机跳转、物体运动和新显露表面的回退。

进阶完成标准：

- [ ] GPU 与 CPU 路径可对照；对象删除、容量变化和场景切换时不读取失效数据。
- [ ] Hi-Z 开关不会造成可见物体持续消失；显示遮挡判断与被剔除对象，记录保守性和代价。
- [ ] 分别报告剔除成本、剩余绘制成本和整帧收益，不用“剔除数量更多”代替性能结论。

原有逐对象 model push constant 需要演进为可索引的实例数据，不能直接靠一个 indirect 调用
覆盖所有材质绑定。先按兼容批次组织；Bindless、全场景几何合并与 Mesh Shader 按需要另行引入。
GPU 剔除、Indirect 和 Hi-Z 属于 A 进阶，不阻塞阶段 4。

### A-4：屏幕空间反射 SSR（优先选做）

**目的：研究可见表面之间的镜面反射，并为后续光追反射建立画质与成本对照。**

前置输入：深度、法线、粗糙度、线性 HDR 场景颜色；时域优化依赖阶段 3。

手敲顺序：

1. 从简单光滑表面开始，实现视空间反射方向、屏幕投影与基础 ray marching。
2. 处理厚度、背面、自交和屏幕边界，显示命中位置、步数与失败原因。
3. 可在基础追踪正确后引入层级深度加速；与遮挡剔除共享设施时重新核对所需 min/max 语义。
4. 明确粗糙度的处理与首版支持范围；未可靠命中的区域回退到现有 specular IBL。
5. 以置信度组合 SSR 与 IBL，避免对同一镜面贡献重复叠加；使用独立时域历史处理闪烁。

完成标准：

- [ ] 相机移动、反射物离开屏幕和新显露区域有可解释的回退，不持续显示旧反射。
- [ ] 反射读取和合成使用明确的源/目标资源，不在同一图像上无保护地边采样边覆盖。
- [ ] 能对照 IBL-only、基础 SSR 与优化 SSR，记录步数、命中率、GPU 耗时和已知限制。

先支持可控的反射材质与场景；粗糙随机反射、反射探针和专用降噪可继续深化，不必一次做完。

### A-5：其他展示专题（选做）

根据兴趣和目标岗位选择，已有 A 进阶专题尚未收口时不同时展开全部方向。

| 专题与目的 | 最小实施顺序 | 完成标准 |
|---|---|---|
| 体积雾：展示散射与空间氛围 | 有限范围单次散射 → 阴影采样 → 低分辨率积分 → 时域稳定 | 密度/相函数/步数可控，运动时无长期拖影，记录成本 |
| PCSS：研究接触硬化软阴影 | blocker search → penumbra 估计 → 可变范围滤波 | 与 PCF 对照不同光源尺寸和遮挡距离，说明漏光与性能限制 |
| 透明渲染：补齐材质表达 | Forward Blend → 排序和深度约定 → 可选 Weighted Blended OIT | 多层透明可演示，说明近似误差及与多光源/TAA 的接口 |

## 9. 阶段 4：硬件光追基础与 Ray Query

**目的：让渲染器能查询场景中的真实几何遮挡，先实现可与 CSM 比较的光追阴影。**

### 手敲顺序

1. 复用阶段 0.5 的 capability 查询与功能开关，补齐 RT 依赖，记录支持但未启用/不可用的原因。
2. 接入 buffer device address、加速结构相关 buffer usage、内存分配与 VMA 配置；若保留较低版本路径，核对其扩展依赖。
3. 根据实际支持启用 `VK_KHR_acceleration_structure`、`VK_KHR_ray_query` 及其依赖，匹配函数入口与 Ray Query shader 编译目标。
4. 为唯一 Mesh 创建 BLAS，为场景对象实例创建 TLAS；复用现有 Mesh / SceneObject 的划分。
5. 区分静态构建、实例变换更新和几何变更重建；管理 scratch 对齐、构建同步与延迟释放。
6. 先对 Opaque 简单场景实现方向光硬阴影，验证基本遮挡关系。
7. 建立 instance / primitive → GPU geometry / material 的查找关系，支持命中处的 UV、法线和纹理读取。
8. 使用上述查找实现 Alpha Mask 命中候选过滤和双面语义，再对照复杂材质场景。

### 完成标准

- [ ] 光追阴影与已知简单几何遮挡关系一致，可与 CSM 切换比较。
- [ ] 共享 Mesh 复用 BLAS；对象移动、删除和场景替换后 TLAS 与资源生命周期正确。
- [ ] Sponza 的镂空材质不会在光追中重新变成实心遮挡。
- [ ] 不支持 RT 的设备仍能启动光栅路径，并说明不可用原因。

先使用 Ray Query，不要求立即实现 `VK_KHR_ray_tracing_pipeline` 与 Shader Binding Table。
若之后的路径追踪需要独立 ray generation / hit / miss shader，再单独增加这条路径。
Ray Query 与完整 RT pipeline 都需要加速结构，但两者不是必须同时实现。

普通前向绘制逐对象绑定的 Material descriptor，不能自动解决任意光线命中处的材质访问。
可以先用受控的小材质集合实现，再在明确需求下引入 GPU material table / descriptor indexing。
GPU 场景数据还需约定 index type、vertex stride、属性偏移和地址生命周期；通过命中重心坐标
插值 UV0/1、normal、tangent。Mask 几何不能统一标记为 Opaque，应在候选命中时按正确 UV
读取 alpha、判断 cutoff，再决定是否确认交点。

## 10. 阶段 5：随机直接光照基线

**目的：先建立正确、可累积的 Monte Carlo 估计器，再用 ReSTIR 提升采样效率。**

### 手敲顺序

1. 明确辐射度、BRDF、几何项、采样 PDF 与可见性在估计器中的职责。
2. 增加可控的面光源测试场景；再将发光三角形建立为可采样的 light buffer。
3. 实现一致的随机数序列和均匀采样基线，再加入按面积/功率等分布的重要性采样。
4. 正确处理选择光源与采样光源表面的概率，以及面积 PDF / 立体角 PDF 的转换。
5. 使用 Ray Query 计算样本可见性，保留未降噪 HDR 输出。
6. 增加静态相机高样本/长时间累积参考图，与低样本结果对比。

### 完成标准

- [ ] 增加样本数后结果向同一均值收敛，改变采样分布不会无故改变整体亮度。
- [ ] 可以分开观察直接光、可见性和噪声，统计每像素候选数、光线数和 GPU 时间。
- [ ] 比较时使用相同的曝光与 tone mapper；数值误差尽量在线性 HDR 中计算。
- [ ] 高样本参考与低样本路径使用相同的材质、光源与可见性定义。

本阶段先处理直接光。阶段 2 的光源列表用于缩小确定性光照的遍历范围，此处需另外建立随机
采样与 PDF 的契约；演示 ReSTIR 时使用多个实际有贡献的候选光源，避免只用一盏方向光评估。
发光材质显示为亮色，也不等于它已经进入光源采样或照亮其他表面。

## 11. 阶段 6：ReSTIR DI

**目的：用蓄水池选择与时空复用，在大量光源下以较少可见性查询获得更好的直接光照估计。**

### 手敲顺序

1. 确定采用的论文/参考实现与估计器版本，固定数据和 PDF 约定。
2. 定义 reservoir：选中样本、权重和、候选计数、最终归一化所需信息。
3. 实现初始候选生成与 weighted reservoir sampling，先关闭所有复用。
4. 在正确目标函数、提议分布和归一化下完成最终着色，对比阶段 5 基线。
5. 接入 temporal reuse，处理重投影、光源身份变化、历史年龄与失效条件。
6. 接入 spatial reuse，检查邻域表面相容性，并在接收表面重新评估样本贡献。
7. 明确 visibility 查询与复用策略，记录采用的偏差/无偏条件，不把简单平均当 reservoir 合并。
8. 加入 reservoir age、样本来源、权重、拒绝原因等调试视图。
9. 分别评估原始估计和重建/降噪后的显示效果。

### 完成标准

- [ ] 初始采样、时域复用、空间复用可独立开关并测量。
- [ ] 与相同预算的随机基线比较噪声和耗时，与高样本参考比较亮度和误差。
- [ ] 移动相机、物体与光源，以及显露/遮挡变化时，不出现长期错误历史。
- [ ] 明确低采样噪声、估计器偏差与降噪伪影的区别，不只以“看起来更平滑”判断正确。

ReSTIR DI 不是整套光追管线，也不自动提供间接光或完整降噪。RTXDI 可作为参考，但手敲学习时
应先解释各步骤的输入输出，再决定采用自写教学实现还是集成库。

## 12. 阶段 7：间接光与 ReSTIR GI / PT

**目的：在直接光基础上研究多次反弹和更复杂的路径复用。此阶段属于后续目标。**

- [ ] 先实现可累积的一次间接反弹，再建立多反弹路径追踪参考。
- [ ] 完善 BSDF 采样、next-event estimation、MIS 与路径吞吐量等基础。
- [ ] 扩展命中材质读取，处理自交、法线方向和粗糙镜面等问题。
- [ ] 加入适合随机照明的时域/空间重建，保留原始估计便于分析。
- [ ] 单独研究 ReSTIR GI 的样本表达、复用条件与重连权重。
- [ ] 再评估 ReSTIR PT；不能把 DI reservoir 直接复制后宣称支持路径复用。

完成标志是间接光能与参考解比较，并能解释噪声、偏差和运动伪影；不是仅在场景中出现一块反光。

## 13. 架构和平台约定

- **保留可切换的算法路径**：Forward、Deferred、CSM、Ray Query 共享场景与材质契约，避免各自维护资产副本。
- **保持 HDR 输出边界**：光照算法输出线性 HDR，曝光与显示转换集中在后处理端。
- **明确每帧资源与历史资源**：两者的索引、复用时机和销毁条件分别记录。
- **Compute 按需引入**：需要时增加 compute pipeline 和 dispatch 管理；同步明确到 pass 读写，不先引入异步队列。
- **GPU 数据契约明确**：light、instance、material 的布局、索引、容量与失效规则写清楚；光栅批处理和光追命中共享一致语义。
- **能力查询贯穿开发**：阶段 0.5 建立统一框架，Compute、Indirect、descriptor indexing 与 RT 按使用时机扩展，区分必选能力与可降级的选做能力。
- **同步与渲染对象分别迁移**：Synchronization2 不自动建立依赖，Dynamic Rendering 不自动转换 layout；逐 Pass 迁移并保留实际验证过的兼容范围。
- **Render Graph 由需求推动**：出现大量重复的资源依赖和 barrier 管理后，再从显式 pass 表演进。
- **跨平台按能力启用**：Windows / Linux / macOS 保留光栅基线；RT 是否可用取决于实际 GPU、驱动和 Vulkan 实现。
- **不假设 MoltenVK 能力恒定**：查询运行时扩展和 feature；在真正验证前，不承诺某个系统一定支持或一定不支持 RT。
- **场景数据语义一致**：坐标系、实例矩阵、材质色彩空间、Alpha Mask、双面与法线规则在光栅和光追中保持一致。

## 14. 作品集与性能证据

**目的：让完成的功能能够复现、讲解和评估，形成与实际代码一致的求职展示材料。**

每个重点算法随实现逐步补齐以下记录，不要求在功能开发前搭建大型自动化框架：

- [ ] 开启/关闭或基线/优化的对照截图；涉及稳定性的功能保留固定路线运动视频。
- [ ] 至少一个中间结果视图，例如 cascade、光源密度、AO、motion vector、剔除或 reservoir。
- [ ] 固定 GPU/驱动、构建模式、分辨率、场景、相机、质量参数及光源配置的耗时表。
- [ ] 测量先预热，再记录固定区间的平均值和波动；注明 Validation、VSync/帧率上限与采集方式。
- [ ] 同时报告新增 pass 成本、受益 pass 成本、总 GPU 时间，以及必要的 CPU/内存数据。
- [ ] 一份短技术说明：问题、算法输入输出、取舍、遇到的伪影、修复方法与已知限制。

性能记录可使用以下模板；实际运行前保持“待测”，不填入预期收益：

| 固定场景与配置 | 算法路径 | CPU 提交 ms | 关键 Pass GPU ms | 总 GPU ms | 资源分配开销 | 状态 |
|---|---|---|---|---|---|---|
| 待填写 | 基线 | 待测 | 待测 | 待测 | 待测 | 待验收 |
| 与基线相同 | 优化 | 待测 | 待测 | 待测 | 待测 | 待验收 |

简历中的“实现”“优化”“支持某平台”对应已完成且验证过的范围。参考或集成第三方代码时记录
来源与自己的工作；算法数量、效果截图和性能数据各自说明不同能力，不互相替代。

## 15. 每组手敲任务的组织方式

每次只完成一个可解释的小目标，并记录：

1. **目的**：解决哪个现有问题，或为哪一步准备输入。
2. **修改位置**：涉及的文件、结构和调用顺序。
3. **资源契约**：格式、usage、descriptor binding、layout、所有者与索引方式。
4. **观察方法**：应出现的图像/计数/调试视图，以及算法关闭后的参考结果。
5. **完成标准**：构建、运行、同步、画面与性能的实际结果；未执行的检查标记待验收。

一次完整算法切换可能涉及多组手敲，但中间准备态不能被标成完整功能已验收。
优先为数学/索引边界加入有意义的小测试，不要求为了改一个 UI 按钮建立测试框架。

## 16. 下一次实际手敲任务

**首先增加 Shadow / Scene / Post 的 GPU 时间统计。**

具体从 [Stage 0 手敲指南](stage-0-rendering-baseline.md) 的小组 A 开始：先做计时类型、能力查询与
每帧 query pool，再用 B/C 接通时间戳、读回和 UI。下面五项是第一轮完整目标，可以分组完成。

首组仅做以下内容：

1. 查询当前 graphics queue family 的 timestamp 能力和设备 `timestampPeriod`。
2. 为各飞行帧分配足够的 timestamp query 槽位。
3. 在三个 pass 的开始/结束插入时间戳；未执行的 pass 显示 N/A。
4. 在对应 fence 完成后读取结果，不在刚提交时阻塞等待。
5. ImGui 显示每个 pass 的 GPU ms，并记录固定场景下的第一份基线。

这一组完成后按以下顺序继续，每一组均保留可运行的结果：

1. 完成阶段 0 的显示/调试基线。
2. 阶段 0.5 第一组：版本协商、能力记录、VMA/ImGui/Shader 配置统一。
3. 阶段 0.5 第二组：Synchronization2。
4. 阶段 0.5 第三组：Dynamic Rendering，先 Post/Present，再 Scene/Shadow。
5. 阶段 1 四级 CSM，再进入阶段 2 Deferred 与多光源剔除。

GPU 测量不依赖先升级 API；Timeline、Bindless 与 RT 留在各自需要的阶段。后续独立实施指南
按实际进度添加，本大纲不预设尚不存在的函数或文件已经实现。

## 17. 参考资料

- GTAO 的遮蔽模型与滤波思路：[Activision 技术报告](https://research.activision.com/publications/2020-03/practical-real-time-strategies-for-accurate-indirect-occlusion)。
- TAA 与重投影的实践参考：[Playdead INSIDE 技术分享](https://blog.playdead.com/articles/inside_presentations/inside_publications.html)。
- 层级深度、SSR 与时空降噪的工程参考：[AMD FidelityFX SSSR](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/)。
- Vulkan RT 的扩展、加速结构与同步要求：[Khronos Ray Tracing Guide](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html)。
- BLAS/TLAS 与实例更新的实现参考：[Khronos Ray Tracing Extended Sample](https://docs.vulkan.org/samples/latest/samples/extensions/ray_tracing_extended/README.html)。
- ReSTIR DI 的候选、时域与空间重采样基础：[Bitterli 等，2020](https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct)。
- 工程集成与算法接口参考：[RTXDI Integration](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md)。

参考代码用于核对算法契约，不替代本项目的资源生命周期、材质数据和跨平台能力检查。
