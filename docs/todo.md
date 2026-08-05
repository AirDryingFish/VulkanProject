# VulkanProject 开发路线图

本文档记录 VulkanProject 从当前 PBR Renderer 继续演进为可扩展 Vulkan
渲染引擎的开发计划。任务按依赖关系和风险排序，而不是单纯按照视觉效果排序。

## 状态标记

- `[x]`：已经完成
- `[ ]`：尚未开始或尚未完成
- `[P0]`：当前最高优先级，会影响稳定性或后续所有模块
- `[P1]`：近期核心功能
- `[P2]`：基础稳定后再做
- `[P3]`：长期演进方向

## 当前能力基线

- [x] C++17、CMake、Ninja 和 vcpkg 跨平台构建
- [x] Windows、Linux 和 Apple Silicon macOS 配置
- [x] Vulkan Instance、Validation Layers、Device 和 VMA Allocator
- [x] Swapchain 创建、重建、MSAA 和深度缓冲
- [x] 多帧同步、每帧 Fence 和 Command Pool
- [x] Immediate Submit 上传上下文
- [x] Main、Swapchain 和 Frame Deletion Queue
- [x] OBJ 模型加载与内置 Cube/Sphere
- [x] 多场景对象、对象删除、拾取和 Transform Gizmo
- [x] PBR metallic-roughness 材质
- [x] HDR Skybox
- [x] Diffuse irradiance cubemap
- [x] Specular prefiltered environment cubemap
- [x] Split-sum BRDF LUT
- [x] ImGui 场景与材质调试界面

---

## 阶段 0：完成当前 IBL 里程碑

目标：保证 BRDF LUT 提交不仅能够运行，而且代码、文档和验证结果彼此一致。

### 文档

- [ ] `[P0]` 更新 `docs/diffuse-ibl.md`
  - 将 BRDF LUT 从“待实现”改为“已完成”
  - 记录 LUT 尺寸与格式
  - 记录 LUT 的两个通道含义：Fresnel scale 和 bias
  - 记录 LUT 坐标：`x = NdotV`、`y = roughness`
  - 记录最终镜面 IBL 公式
  - 说明 BRDF LUT 不包含 HDR 环境信息，可以被不同环境贴图复用
- [ ] `[P1]` 为 BRDF LUT 增加一张调试截图或可视化说明

### 验证

- [ ] `[P0]` Debug 构建无编译错误
- [ ] `[P0]` Validation Layers 下启动和退出无错误
- [ ] `[P0]` 关闭所有点光源，只保留 IBL 进行视觉验证
- [ ] `[P0]` 测试 roughness 从 `0.04` 到 `1.0`
- [ ] `[P0]` 测试 metallic 为 `0.0` 和 `1.0`
- [ ] `[P0]` 检查球体掠射角处的 Fresnel 响应
- [ ] `[P1]` 添加 ImGui BRDF LUT 预览窗口

### 完成标准

- 文档不再把 BRDF LUT 标记为未完成。
- Debug 和 Release 都能正常启动。
- Validation Layers 没有 descriptor、image layout 或资源销毁错误。
- 开关 BRDF LUT 前后能观察到合理、可解释的镜面 IBL 差异。

---

## 阶段 1：资源生命周期与异常安全

目标：无论程序正常关闭、初始化中途失败，还是运行时资源被替换，都能够安全释放
已经创建的 Vulkan、VMA、ImGui 和 GLFW 资源。

### 1.1 初始化与清理

- [ ] `[P0]` 检查 `glfwInit()` 返回值
- [ ] `[P0]` 检查 `glfwCreateWindow()` 返回值
- [ ] `[P0]` 让 `Cleanup()` 支持部分初始化
  - 销毁前检查 Instance、Device、Allocator、Window 等句柄
  - 未创建的资源不能进入 Vulkan 销毁函数
- [ ] `[P0]` 让 `Cleanup()` 支持重复调用
  - 增加 `cleanedUp` 状态，或在销毁后清空所有句柄
  - 避免析构函数与显式清理产生 double free
- [ ] `[P0]` 初始化抛异常时仍执行清理
  - 可以使用析构函数、scope guard 或更细粒度 RAII
- [ ] `[P0]` 将设备空闲等待纳入可靠的清理路径
  - 仅在有效 Device 上调用
  - 处理 `VK_ERROR_DEVICE_LOST` 时不能依赖无限等待

### 1.2 DeletionQueue

- [ ] `[P0]` 将 `push_back(function)` 改为 `push_back(std::move(function))`
- [ ] `[P1]` 将 `deletors` 重命名为 `deletions` 或 `callbacks`
- [ ] `[P1]` 增加 `empty()` 和 `size()` 调试接口
- [ ] `[P1]` 明确回调必须为不可抛异常的销毁操作
- [ ] `[P1]` 记录三类队列的使用约定
  - Main：Device 生命周期内长期存在的资源
  - Swapchain：依赖 Swapchain extent/image 的资源
  - Frame：仍可能被 GPU 使用、需要延迟销毁的动态资源
- [ ] `[P1]` 检查队列 flush 顺序是否符合 Vulkan 对象依赖关系

### 1.3 动态资源安全

- [ ] `[P0]` 验证删除 SceneObject 时旧 Buffer 不会仍被 GPU 使用
- [ ] `[P0]` 验证 rebuild mesh 时不再调用全局 `vkDeviceWaitIdle()`
- [ ] `[P1]` 为延迟销毁添加资源名称或调试标签
- [ ] `[P1]` 明确 Frame deletion queue 在对应 Fence 之后 flush
- [ ] `[P1]` 测试连续添加、删除和替换模型

### 1.4 测试场景

- [ ] `[P0]` 连续缩放窗口并触发 Swapchain 重建
- [ ] `[P0]` 最小化窗口后恢复
- [ ] `[P0]` 连续导入模型
- [ ] `[P0]` 连续删除当前选中对象
- [ ] `[P0]` 在初始化不同阶段主动制造失败，检查是否泄漏或崩溃
- [ ] `[P1]` 为 DeletionQueue 添加 LIFO、空 flush 和重复 flush 单元测试

### 完成标准

- 正常退出和初始化失败均不会 double free。
- 常见交互压力测试下没有 Validation Layer 错误。
- 所有资源都能明确归入 Main、Swapchain 或 Frame 生命周期。

---

## 阶段 2：拆分 TriangleApplication

目标：让 `TriangleApplication` 只负责程序流程，不再直接拥有全部 Vulkan 和场景细节。

### 2.1 VulkanContext

- [ ] `[P1]` 创建 `VulkanContext` 类型
- [ ] `[P1]` 移入以下资源和逻辑
  - `VkInstance`
  - Debug Messenger
  - `VkSurfaceKHR`
  - `VkPhysicalDevice`
  - `VkDevice`
  - Graphics/Present Queue
  - Queue family 查询
  - `VmaAllocator`
- [ ] `[P1]` 禁止复制 `VulkanContext`
- [ ] `[P1]` 明确析构顺序
- [ ] `[P1]` 为 Vulkan 对象设置 Debug Name 的公共接口

### 2.2 Swapchain

- [ ] `[P1]` 创建 `Swapchain` 类型
- [ ] `[P1]` 移入以下资源和逻辑
  - Swapchain handle
  - Images 和 image views
  - Surface format、present mode 和 extent
  - Depth/MSAA image
  - Framebuffers
  - Present semaphores
  - 创建、销毁和重建
- [ ] `[P1]` 让重建操作返回是否成功或是否需要继续等待窗口恢复
- [ ] `[P1]` 保持 Swapchain 资源与永久资源的生命周期边界清晰

### 2.3 Renderer

- [ ] `[P1]` 创建 `Renderer` 类型
- [ ] `[P1]` 移入 `FrameContext`
- [ ] `[P1]` 移入 `drawFrame()`
- [ ] `[P1]` 移入 command buffer 录制
- [ ] `[P1]` 移入 render pass 和 graphics pipeline 管理
- [ ] `[P1]` 提供 `beginFrame()` / `endFrame()` 或等价接口
- [ ] `[P2]` 允许 Renderer 在没有 ImGui 的情况下运行

### 2.4 拆分策略

- [ ] 每次只移动一个模块
- [ ] 每次移动后编译并运行 Validation Layers
- [ ] 不在同一提交中同时重写同步、资源管理和渲染结构
- [ ] 保留小而清晰的提交，便于 bisect 和回退

### 完成标准

- `TriangleApplication` 主要只包含初始化顺序、主循环和高层模块协调。
- VulkanContext、Swapchain 和 Renderer 可以独立理解其资源所有权。
- 拆分前后的画面输出一致。

---

## 阶段 3：通用 GPU 资源层

目标：减少裸 Vulkan handle 在业务代码中的传播，同时保留 Vulkan 行为的可见性。

### 3.1 Buffer 与 Image

- [ ] `[P1]` 将 `AllocatedBuffer` 演进为可移动、不可复制的 `GpuBuffer`
- [ ] `[P1]` 将 `AllocatedImage` 演进为可移动、不可复制的 `GpuImage`
- [ ] `[P1]` 记录 Buffer/Image 的大小、格式、usage 和 mip 数量
- [ ] `[P1]` 销毁后清空 handle
- [ ] `[P1]` 支持显式立即释放和延迟释放
- [ ] `[P2]` 添加 Image layout 调试状态

### 3.2 创建辅助函数

- [ ] `[P1]` 提取常见 Sampler 创建逻辑
- [ ] `[P1]` 提取单颜色附件 RenderPass 创建逻辑
- [ ] `[P1]` 提取单附件 Framebuffer 创建逻辑
- [ ] `[P2]` 提取无顶点输入的离屏 Pipeline 创建逻辑
- [ ] `[P2]` 建立 `PipelineConfig`，但避免万能配置对象
- [ ] `[P2]` 将 Shader Module 生命周期限制在 Pipeline 创建过程中

### 3.3 上传系统

- [ ] `[P1]` 为 staging buffer 上传建立统一接口
- [ ] `[P1]` 避免每个 Mesh 分别等待上传 Fence
- [ ] `[P2]` 支持一次提交上传多个 Buffer/Image
- [ ] `[P2]` 支持异步上传队列或批量上传任务

### 完成标准

- 新增纹理或 Buffer 不再需要复制整段 Vulkan 创建样板代码。
- 资源类型能明确表达所有权，不能被意外复制。
- IBL 三条离屏管线共享适量 helper，但仍保留各自清晰流程。

---

## 阶段 4：Scene、Mesh 与 Material 解耦

目标：从“每个对象直接持有 Buffer、全场景共享一套材质”升级为真正的场景资源模型。

### 4.1 Mesh

- [ ] `[P1]` 创建 `Mesh` 类型
- [ ] `[P1]` Mesh 持有 vertex/index buffer 和 index count
- [ ] `[P1]` Mesh 持有 local-space bounds
- [ ] `[P1]` 多个 SceneObject 可以引用同一个 Mesh
- [ ] `[P1]` Cube、Sphere 和导入模型进入相同 Mesh 资源路径

### 4.2 Material

- [ ] `[P1]` 创建 `Material` 类型
- [ ] `[P1]` 每个 Material 支持
  - Base color texture/factor
  - Normal texture
  - Metallic-roughness texture/factor
  - AO texture/factor
  - Emissive texture/factor
- [ ] `[P1]` Material 拥有或引用自己的 descriptor set
- [ ] `[P1]` 每个 SceneObject 可以选择不同 Material
- [ ] `[P1]` 为缺失纹理建立默认白色、黑色和法线纹理
- [ ] `[P2]` 支持 alpha mask 和 alpha blend
- [ ] `[P2]` 支持 double-sided material

### 4.3 SceneObject

- [ ] `[P1]` SceneObject 只保存高层数据
  - Name
  - Transform
  - Mesh handle
  - Material handle
  - Selection state
- [ ] `[P1]` 删除对象时只释放引用，不一定立即销毁共享 Mesh/Material
- [ ] `[P1]` 将 Transform 独立为结构体
- [ ] `[P2]` 建立父子节点关系

### 4.4 Descriptor 结构

- [ ] `[P1]` 为 Skybox 建立独立 descriptor set layout
- [ ] `[P1]` 分离 Frame descriptor 与 Material descriptor
- [ ] `[P1]` Frame descriptor 保存 Camera、Lights 和全局 IBL
- [ ] `[P1]` Material descriptor 保存材质纹理
- [ ] `[P2]` 减少每次绘制重复绑定的 descriptor

### 完成标准

- 两个对象可以共享 Mesh、使用不同 Material。
- Skybox 不再填充未使用的 PBR descriptor。
- 更换材质不需要重建 Mesh buffer。

---

## 阶段 5：glTF 2.0 资产管线

目标：使用与 metallic-roughness PBR 工作流匹配的现代资产格式。

### 5.1 基础加载

- [ ] `[P1]` 选择并集成 glTF 加载库，例如 fastgltf 或 tinygltf
- [ ] `[P1]` 加载 glTF/GLB Buffer 和 accessor
- [ ] `[P1]` 支持多个 Mesh primitive
- [ ] `[P1]` 支持 vertex position、normal、UV 和 tangent
- [ ] `[P1]` 支持 uint16/uint32 index
- [ ] `[P1]` 支持 Node 层级与 local/global transform

### 5.2 PBR Material

- [ ] `[P1]` Base Color
- [ ] `[P1]` Metallic-Roughness
- [ ] `[P1]` Normal
- [ ] `[P1]` Occlusion
- [ ] `[P1]` Emissive
- [ ] `[P1]` Texture coordinate set
- [ ] `[P1]` Factor 与纹理共同作用
- [ ] `[P2]` Alpha mode
- [ ] `[P2]` Double-sided

### 5.3 纹理规范

- [ ] `[P1]` Base Color 和 Emissive 使用 sRGB 格式
- [ ] `[P1]` Normal、Metallic、Roughness 和 AO 使用线性格式
- [ ] `[P1]` 生成或加载完整 mip 链
- [ ] `[P2]` 支持 KTX2/Basis Universal
- [ ] `[P2]` 建立纹理缓存，避免重复加载同一文件

### 5.4 后续能力

- [ ] `[P2]` Skin 与骨骼动画
- [ ] `[P2]` Morph target
- [ ] `[P2]` Animation channels

### 完成标准

- 能正确加载一个包含多个节点和多个材质的 glTF 场景。
- 与 glTF 参考查看器的材质外观基本一致。
- OBJ 保留为简单调试格式，但不再作为主要资产管线。

---

## 阶段 6：方向光与阴影

目标：在现有点光源和 IBL 基础上加入第一个完整阴影系统。

### 6.1 Directional Light

- [ ] `[P1]` 添加方向、颜色、强度和 enabled 状态
- [ ] `[P1]` 在 ImGui 中编辑方向光
- [ ] `[P1]` 在 PBR shader 中加入方向光直接光照

### 6.2 Shadow Map

- [ ] `[P1]` 创建 depth-only shadow image
- [ ] `[P1]` 创建 shadow image view 和 comparison sampler
- [ ] `[P1]` 创建 depth-only render pass
- [ ] `[P1]` 创建 shadow framebuffer
- [ ] `[P1]` 创建 depth-only pipeline 和 shader
- [ ] `[P1]` 计算 light view/projection matrix
- [ ] `[P1]` 在主 PBR pass 中采样 shadow map

### 6.3 阴影质量

- [ ] `[P1]` 添加 constant depth bias
- [ ] `[P1]` 添加 slope-scaled depth bias
- [ ] `[P1]` 实现 3x3 PCF
- [ ] `[P1]` 增加 Shadow Map 调试预览
- [ ] `[P2]` 稳定方向光投影，减少移动时闪烁
- [ ] `[P2]` Cascaded Shadow Maps
- [ ] `[P3]` Point light cubemap shadow

### 完成标准

- 场景对象能正确投射和接收方向光阴影。
- 没有明显 shadow acne 或 peter-panning。
- 可以在 ImGui 中关闭阴影并比较性能和画面。

---

## 阶段 7：HDR 主渲染目标与后处理

目标：不再直接把主场景渲染到 Swapchain，建立可扩展的后处理链。

### 7.1 HDR Scene Pass

- [ ] `[P2]` 创建浮点 HDR color image
- [ ] `[P2]` 主 PBR pass 输出到 HDR image
- [ ] `[P2]` 创建 fullscreen post-process pipeline
- [ ] `[P2]` 后处理 pass 输出到 Swapchain

### 7.2 色调映射和曝光

- [ ] `[P2]` 支持可调 Exposure
- [ ] `[P2]` 实现 ACES tone mapping
- [ ] `[P2]` 检查 Swapchain sRGB 与 shader gamma 的职责边界
- [ ] `[P2]` 避免重复 gamma correction

### 7.3 后处理效果

- [ ] `[P2]` Bloom bright-pass
- [ ] `[P2]` Downsample/upsample blur chain
- [ ] `[P2]` FXAA
- [ ] `[P3]` TAA
- [ ] `[P3]` SSAO

### 7.4 Debug Views

- [ ] `[P1]` Albedo
- [ ] `[P1]` World normal
- [ ] `[P1]` Metallic
- [ ] `[P1]` Roughness
- [ ] `[P1]` AO
- [ ] `[P1]` Irradiance
- [ ] `[P1]` Prefilter mip
- [ ] `[P1]` BRDF LUT
- [ ] `[P1]` Shadow map
- [ ] `[P2]` HDR luminance

### 完成标准

- 主场景以 HDR 精度渲染。
- Tone mapping 和颜色空间处理有明确、可验证的流程。
- 后处理效果可以独立开关。

---

## 阶段 8：性能与可观测性

目标：在有足够复杂场景之后，用测量数据指导优化。

### 8.1 CPU/GPU 测量

- [ ] `[P2]` ImGui 显示 CPU frame time
- [ ] `[P2]` 使用 Vulkan timestamp query 测量各 pass GPU 时间
- [ ] `[P2]` 统计 draw call、triangle、pipeline switch 数量
- [ ] `[P2]` 统计 Buffer/Image 显存使用量
- [ ] `[P2]` 为 Vulkan 对象添加 Debug Name
- [ ] `[P2]` 支持 RenderDoc 帧捕获工作流

### 8.2 Draw 提交优化

- [ ] `[P2]` 按 Pipeline 和 Material 排序对象
- [ ] `[P2]` 减少 descriptor set 切换
- [ ] `[P2]` 相同 Mesh 使用 instanced rendering
- [ ] `[P2]` 将每对象数据迁移到 dynamic UBO 或 SSBO
- [ ] `[P2]` CPU frustum culling
- [ ] `[P3]` GPU-driven culling
- [ ] `[P3]` Indirect drawing

### 8.3 资源性能

- [ ] `[P2]` 批量 staging upload
- [ ] `[P2]` Pipeline cache
- [ ] `[P2]` Descriptor allocator
- [ ] `[P2]` 纹理和 Mesh 缓存
- [ ] `[P3]` 后台资源加载
- [ ] `[P3]` 资源流式加载

### 完成标准

- 每项优化都有优化前后的 CPU/GPU 测量结果。
- 场景规模提升时能确定瓶颈位于 CPU、GPU、带宽还是同步。

---

## 阶段 9：现代 Vulkan 与长期架构

这些任务依赖前面模块边界已经稳定，不应过早开始。

### Vulkan API 现代化

- [ ] `[P3]` 评估升级到 Vulkan 1.3
- [ ] `[P3]` 使用 Dynamic Rendering
- [ ] `[P3]` 使用 Synchronization2
- [ ] `[P3]` 使用 Timeline Semaphore
- [ ] `[P3]` 使用 Extended Dynamic State

### Descriptor 与资源绑定

- [ ] `[P3]` Descriptor Indexing
- [ ] `[P3]` Bindless textures
- [ ] `[P3]` 全局纹理数组与 Material texture index
- [ ] `[P3]` 检查 MoltenVK 和目标 GPU 的兼容性与限制

### Render Graph

- [ ] `[P3]` 只有出现多个相互依赖的 pass 后再设计 Render Graph
- [ ] `[P3]` 描述 pass 的读写资源
- [ ] `[P3]` 自动推导 image layout transition
- [ ] `[P3]` 自动排序 pass
- [ ] `[P3]` 管理 transient image 生命周期与复用

### 完成标准

- 新架构解决的是已经测量或明确存在的问题。
- 不为追求 API 新颖性牺牲跨平台兼容性和代码可读性。

---

## 推荐执行顺序

严格建议按照以下顺序推进：

```text
当前 IBL 文档与验证
    ↓
资源生命周期与异常安全
    ↓
拆分 VulkanContext / Swapchain / Renderer
    ↓
GPU 资源封装
    ↓
Mesh / Material / Scene 解耦
    ↓
glTF 资产加载
    ↓
方向光 Shadow Map
    ↓
HDR 后处理
    ↓
基于测量的性能优化
    ↓
Dynamic Rendering / Bindless / Render Graph
```

## 当前最近任务

下一批提交建议控制在以下范围：

1. 更新 BRDF LUT 文档状态。
2. 完成 BRDF LUT 的 Debug/Release 和 Validation 验证。
3. 让初始化异常时也能安全清理资源。
4. 完善 DeletionQueue 的移动语义与测试。
5. 开始抽取 VulkanContext，但暂时不要同时重写 Renderer。

在这五项完成之前，暂缓加入新的大型视觉效果。这样后续 Material、glTF 和 Shadow
Map 都能建立在更可靠的资源所有权和模块边界之上。
