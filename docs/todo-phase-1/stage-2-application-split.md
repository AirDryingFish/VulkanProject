# 阶段 2：拆分 TriangleApplication 实施指南

本文对应[开发路线图](overview.md)中的“阶段 2：拆分 TriangleApplication”。本文基于
2026-08-07、提交 `4979e0b` 之后的项目结构编写，默认阶段 1 的 Buffer、Image、临时
ShaderModule RAII、可靠 Cleanup 和 Frame 延迟销毁已经存在。

本阶段的目标不是继续增加画面效果，也不是一次性设计最终引擎架构，而是把当前
`TriangleApplication` 中已经存在的职责划分成三个可以独立理解的模块：

```text
TriangleApplication
├── VulkanContext
├── Renderer
└── Swapchain
```

完成后，`TriangleApplication` 仍可以暂时拥有场景、相机、ImGui 控件和资产内容；这些会在
后续 Scene、Material 和资产系统阶段继续拆分。本阶段优先消除 Vulkan 父对象、Swapchain
资源和逐帧同步全部混在一个类里的问题。

## 1. 当前项目评估

### 1.1 已经具备的基础

阶段 1 已经解决了拆分类之前最危险的问题：

- `AllocatedBuffer` 和 `AllocatedImage` 是 move-only RAII 类型。
- `UniqueShaderModule` 能覆盖临时 ShaderModule 的异常路径。
- `Cleanup()` 支持部分初始化和重复调用。
- 运行时替换 Buffer 会移动到对应 Frame 的 `retiredBuffers`。
- Frame Fence 成功等待后才销毁 retired Buffer。
- Swapchain 重建会等待所有 Graphics Fence，并等待 Present Queue idle。
- Debug 和 Release 构建、DeletionQueue 单元测试已经存在。

因此现在可以移动所有权，而不必在拆分过程中重新发明资源销毁策略。

### 1.2 当前只是“按文件分类”，还不是模块

当前已经有这些文件：

```text
VulkanInstance.cpp
VulkanDevice.cpp
VulkanAllocator.cpp
SwapChain.cpp
Renderer.cpp
GraphicsPipeline.cpp
CommandBuffers.cpp
```

但其中的函数仍然全部是：

```cpp
TriangleApplication::someFunction()
```

所有文件都包含 `TriangleApplication.hpp`，并可直接读取几乎全部状态。因此当前结构只是把
一个大类的实现分散到多个 `.cpp`，没有形成以下边界：

- 谁拥有 Instance、Device 和 Allocator。
- 谁拥有 Swapchain image、attachment 和 framebuffer。
- 谁拥有 Frame Fence、CommandPool 和逐帧状态机。
- 谁可以销毁哪些对象。
- 哪个模块必须比另一个模块活得更久。

### 1.3 当前主要耦合点

#### VulkanContext 相关状态分散

下面这些成员都位于 `TriangleApplication`：

```text
VkInstance
VkDebugUtilsMessengerEXT
VkSurfaceKHR
VkPhysicalDevice
VkDevice
VkQueue graphicsQueue
VkQueue presentQueue
VmaAllocator
```

创建逻辑分布在 `VulkanInstance.cpp`、`VulkanDevice.cpp` 和 `VulkanAllocator.cpp`，最终销毁
却集中在 `TriangleApplication::Cleanup()`。

#### Swapchain 所有权横跨多个文件

当前 Swapchain 相关资源并不只在 `SwapChain.cpp`：

| 资源或逻辑 | 当前位置 |
| --- | --- |
| Swapchain handle、images、extent、format | `SwapChain.cpp` / `TriangleApplication.hpp` |
| Present semaphores | `Renderer.cpp` |
| Depth/MSAA image | `ImageResources.cpp` |
| Framebuffers | `GraphicsPipeline.cpp` |
| 重建触发 | `Renderer.cpp` |
| 最终清理 | `SwapChain.cpp` / `TriangleApplication.cpp` |

这会让“Swapchain 重建到底重建哪些东西”只能通过全局搜索理解。

#### Renderer 同时依赖所有高层状态

当前 `drawFrame()` 同时执行：

```text
等待 Fence
释放 retired Buffer
处理窗口和 Swapchain 状态
获取 Swapchain image
处理相机输入
更新 Uniform Buffer
开始 ImGui 帧
执行场景选择
录制命令
提交 Graphics Queue
提交 Present Queue
推进 currentFrame
```

其中只有同步、命令录制和 Queue 提交属于 Renderer。相机、场景编辑和 ImGui UI 构建仍应由
应用层协调。

#### Main DeletionQueue 跨越模块边界

当前 Main queue 同时保存 FrameContext、Pipeline、Sampler、IBL、DescriptorPool 和 ImGui
backend 的销毁 callback。拆分后不能简单把整个 queue 移给某一个模块，否则新的模块仍会
替其他模块销毁资源。

本阶段应该让每个模块管理自己的资源。暂时没有 RAII 包装的原始 Handle，可以由模块内部
DeletionQueue 管理，但 callback 不能跨模块销毁别人的对象。

## 2. 本阶段要建立的不变量

拆分完成后必须满足：

1. `VulkanContext` 不依赖 Swapchain、Renderer、场景或 ImGui。
2. `Swapchain` 和 `Renderer` 可以读取 Context 的只读 Handle，但不拥有 Context。
3. `Renderer` 不拥有场景；它只消费一帧的渲染输入。
4. `Swapchain` 不负责创建 Renderer 的 Pipeline 或 RenderPass。
5. Framebuffer 一定先于它引用的 RenderPass 和 ImageView 销毁。
6. 所有 VMA Buffer/Image 一定先于 `VulkanContext` 的 Allocator 销毁。
7. `TriangleApplication` 只协调模块初始化、帧循环、重建和关闭顺序。
8. 不通过 `Renderer(TriangleApplication&)` 或 `friend class` 绕过边界。
9. 每移动一个模块后，画面、同步策略和 shader 行为保持不变。

目标拥有和借用关系：

```text
TriangleApplication
├── owns GLFWwindow
├── owns VulkanContext
│   └── owns Instance / Surface / Device / Queues / VmaAllocator
├── owns Swapchain
│   └── borrows VulkanContext and GLFWwindow
└── owns Renderer
    └── borrows VulkanContext and Swapchain
```

`TriangleApplication` 只负责协调。借用对象不负责销毁被借用对象；`VulkanContext` 和 Window
必须比借用它们的模块活得更久。Framebuffer/RenderPass 的特殊跨模块销毁顺序在 7.3 节单独
处理。

## 3. 明确本阶段不做什么

为避免拆分类时同时改变渲染结果，本阶段不做：

- 不改 Fence、Semaphore 和 present semaphore 的同步算法。
- 不引入 Dynamic Rendering。
- 不加入 Bindless、Render Graph 或多线程录制。
- 不重写 PBR、IBL、BRDF LUT shader。
- 不重新设计 Scene、Material 或模型导入格式。
- 不把所有 Vulkan Handle 一次性包装成最终通用 RAII 模板。
- 不为了“减少 getter”而让模块互相成为 friend。

如果拆分过程中发现现有同步 Bug，单独提交修复，再继续移动代码，不要把行为修复和大范围
文件移动混成一个提交。

## 4. 推荐提交顺序

建议拆成以下提交：

```text
1. 增加共享类型和模块骨架，不移动行为
2. 抽取 VulkanContext
3. 将 VMA 创建辅助函数迁入 VulkanContext
4. 抽取 Swapchain core、image view 和 present semaphore
5. 迁移 Swapchain attachment 和 framebuffer
6. 抽取 FrameContext 和 Renderer 帧协议
7. 迁移主 RenderPass、Pipeline 和 command recording
8. 收紧 TriangleApplication 接口并删除旧成员
9. Validation、重建和故障注入验收
```

每个提交都至少执行：

```sh
cmake --build --preset linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
```

涉及所有权或清理顺序的提交还要补 Release 构建。

## 5. 第一步：准备共享类型和模块骨架

### 修改位置

- 新增 `src/VulkanContext.hpp/.cpp`
- 新增 `src/Swapchain.hpp/.cpp`
- 新增 `src/Renderer.hpp/.cpp`
- 可选新增 `src/RenderTypes.hpp`
- 更新 `CMakeLists.txt`

先只创建空骨架并加入构建，不立即删除旧函数。这样可以把“文件是否正确加入工程”和“行为
迁移是否正确”分开验证。

### 不要继续使用容易冲突的文件名

当前已有 `SwapChain.cpp` 和 `Renderer.cpp`。推荐在迁移期间使用一致的新名字：

```text
Swapchain.hpp
Swapchain.cpp
Renderer.hpp
Renderer.cpp
```

可以先把旧 `SwapChain.cpp` 重命名为 `SwapchainLegacy.cpp`，或者先新建
`SwapchainModule.cpp`。不要让大小写不同的 `SwapChain.cpp` 和 `Swapchain.cpp` 长期共存，
这在大小写不敏感文件系统上会出问题。

### 提前移出嵌套类型

`FrameContext` 当前位于 `TriangleApplication.hpp` 顶层，可以直接迁入 `Renderer.hpp`。
`SceneObject` 当前是 `TriangleApplication` 的 private 嵌套类型，Renderer 不应该为了录制命令
而包含整个应用类。

推荐新增窄的逐帧输入，而不是让 Renderer 保存 `TriangleApplication*`：

```cpp
struct RenderObjectView
{
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    glm::mat4 model{1.0f};
};

struct RenderFrameData
{
    const std::vector<RenderObjectView>* objects = nullptr;
    VkDescriptorSet sceneDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;
    ImDrawData* imguiDrawData = nullptr;
};
```

应用层从 `SceneObject` 生成只读 view，Renderer 不获得场景编辑权限。第一版如果暂时直接
传递 `const std::vector<SceneObject>&`，也应把 `SceneObject` 移到独立头文件，不能反向包含
`TriangleApplication.hpp`。

## 6. 第二步：抽取 VulkanContext

### 6.1 所有权范围

`VulkanContext` 应拥有：

```text
VkInstance
VkDebugUtilsMessengerEXT
VkSurfaceKHR
VkPhysicalDevice
VkDevice
Graphics Queue
Present Queue
Graphics/Present Queue family indices
VmaAllocator
MSAA sample count
```

建议同时迁入：

- Validation layer 和 instance extension 检查。
- PhysicalDevice 选择和适用性判断。
- Device extension 检查。
- Queue family 查询。
- Swapchain support 查询。
- format 能力查询和最大 MSAA 查询。
- Debug Name 公共接口。

`GLFWwindow` 仍由 Application 拥有，Context 只在初始化时借用它创建 Surface。

### 6.2 推荐接口

当前顶层 Vulkan Handle 还没有全部 RAII 化。为保证 `initialize()` 中途抛异常时仍能清理，
本阶段推荐“两阶段初始化”，不要直接在可能抛异常的构造函数中连续创建原始 Handle：

```cpp
struct VulkanContextConfig
{
    const char* applicationName = "VulkanProject";
    uint32_t apiVersion = VK_API_VERSION_1_0;
    bool enableValidation = false;
};

class VulkanContext final
{
public:
    VulkanContext() noexcept = default;
    ~VulkanContext() noexcept;

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    void initialize(
        GLFWwindow* window,
        const VulkanContextConfig& config);

    void shutdown() noexcept;

    VkInstance instance() const noexcept;
    VkSurfaceKHR surface() const noexcept;
    VkPhysicalDevice physicalDevice() const noexcept;
    VkDevice device() const noexcept;
    VkQueue graphicsQueue() const noexcept;
    VkQueue presentQueue() const noexcept;
    VmaAllocator allocator() const noexcept;
    const QueueFamilyIndices& queueFamilies() const noexcept;
    VkSampleCountFlagBits msaaSamples() const noexcept;

    SwapChainSupportDetails querySwapchainSupport() const;
    VkResult waitIdle() const noexcept;

    void setDebugName(
        VkObjectType objectType,
        uint64_t handle,
        const char* name) const noexcept;

private:
    // 创建辅助函数和 Handle……
};
```

Application 中可以使用：

```cpp
std::optional<VulkanContext> context;

context.emplace();
context->initialize(window, config);
```

如果 `initialize()` 抛异常，已经构造完成的 Context 会在 Application 清理时执行
`shutdown()`。`initialize()` 本身也建议：

```cpp
try
{
    createInstance();
    setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
    createAllocator();
}
catch (...)
{
    shutdown();
    throw;
}
```

### 6.3 Context shutdown 顺序

Context 只能在所有外部 Device/VMA 子对象已经销毁后 shutdown：

```text
等待有效 Device idle
    ↓
销毁 VmaAllocator
    ↓
销毁 VkDevice
    ↓
销毁 Surface
    ↓
销毁 Debug Messenger
    ↓
销毁 Instance
```

`shutdown()` 必须 `noexcept`、幂等，并在销毁后清空 Handle。不要在其中使用会抛异常的
`VK_CHECK`。等待时仅在 `device_ != VK_NULL_HANDLE` 时调用 `vkDeviceWaitIdle()`；如果返回
`VK_ERROR_DEVICE_LOST`，不能再假设 GPU 会正常完成工作，但仍要继续调用 Vulkan 销毁函数
释放主机侧对象。其他等待错误只记录一次并继续尽最大努力清理，不要在错误路径重复等待。

### 6.4 Handle getter 的边界

本阶段允许只读 getter，因为 Vulkan API 需要原始 Handle。但不要提供：

```cpp
VkDevice& device();
VmaAllocator& allocator();
```

外部模块不应能替换 Context 的父对象。返回 Handle 值即可。

### 6.5 将资源创建入口迁入 Context

当前 `createBuffer()`、`createImage()` 和 `createShaderModule()` 仍是
`TriangleApplication` 成员，但它们只依赖 Device/Allocator。建议在 Context 核心迁移稳定后
再移动：

```cpp
AllocatedBuffer createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties) const;

AllocatedImage createImage(
    const ImageCreateDesc& desc) const;

UniqueShaderModule createShaderModule(
    const std::vector<char>& code) const;
```

这样 Application、Swapchain 和 Renderer 不需要各自读取 `allocator()` 来创建 VMA 资源。

### 6.6 Debug Name

`setDebugName()` 应在函数不可用时静默返回，不能让 Debug Name 影响 Release 行为：

```cpp
void VulkanContext::setDebugName(
    VkObjectType type,
    uint64_t handle,
    const char* name) const noexcept
{
    if (setDebugUtilsObjectName == nullptr ||
        handle == 0 ||
        name == nullptr)
    {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    setDebugUtilsObjectName(device_, &info);
}
```

不要使用会抛异常的检查宏处理 Debug Name 失败。

### 6.7 VulkanContext 迁移完成条件

- `TriangleApplication` 不再声明顶层 Vulkan Handle。
- `VulkanInstance.cpp`、`VulkanDevice.cpp`、`VulkanAllocator.cpp` 不再包含
  `TriangleApplication.hpp`。
- Context 可以在 Instance、Surface、Device、Allocator 任一步失败后安全析构。
- Context 禁止复制和移动。
- Debug/Release 构建和正常启动通过。

## 7. 第三步：抽取 Swapchain

### 7.1 Swapchain 应拥有的资源

按照 overview 的边界，Swapchain 最终拥有：

```text
VkSwapchainKHR
Swapchain images（非拥有 Handle）
Swapchain image views
VkFormat / VkColorSpaceKHR / VkPresentModeKHR
VkExtent2D
MSAA color image
Depth image
Framebuffers
每个 Swapchain image 对应的 render-finished semaphore
```

其中 `vkGetSwapchainImagesKHR()` 返回的 Image 由 Swapchain 实现拥有，应用只保存非拥有
Handle；ImageView、Framebuffer、Semaphore、Depth/MSAA image 由 `Swapchain` 真正拥有。

### 7.2 推荐接口

```cpp
enum class SwapchainBuildStatus
{
    Ready,
    Deferred,
    WindowClosed,
    SurfaceLost,
};

class Swapchain final
{
public:
    Swapchain() noexcept = default;
    ~Swapchain() noexcept;

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    SwapchainBuildStatus initializeCore(
        VulkanContext& context,
        GLFWwindow* window);

    void createFramebuffers(VkRenderPass renderPass);

    SwapchainBuildStatus rebuildCore();
    void destroyFramebuffersAndAttachments() noexcept;
    void shutdown() noexcept;

    VkSwapchainKHR get() const noexcept;
    VkFormat format() const noexcept;
    VkExtent2D extent() const noexcept;
    std::size_t imageCount() const noexcept;
    VkFramebuffer framebuffer(uint32_t imageIndex) const;
    VkSemaphore renderFinishedSemaphore(uint32_t imageIndex) const;
};
```

`initializeCore()` 先创建 Swapchain、images 和 image views。Renderer 根据 format 创建兼容的
RenderPass，随后调用 `createFramebuffers(renderPass)`。这个两段式流程是为了解决
Framebuffer 与 RenderPass 的依赖，而不是让 Swapchain 反向拥有 Renderer。

### 7.3 必须显式解决 RenderPass 循环依赖

当前依赖是：

```text
Renderer RenderPass 依赖 Swapchain format
Swapchain Framebuffer 依赖 Renderer RenderPass
```

不要通过让 Swapchain 调用 `renderer.createRenderPass()` 解决，这会形成双向依赖。正确初始
创建顺序：

```text
Swapchain.initializeCore()
    ↓ 提供 format
Renderer.initialize(format, msaaSamples)
    ↓ 提供 renderPass
Swapchain.createFramebuffers(renderPass)
```

销毁顺序必须反向：

```text
Swapchain 销毁 Framebuffer
    ↓
Renderer 销毁 Pipeline / RenderPass
    ↓
Swapchain 销毁 attachment / image view / swapchain
```

为了减少最终清理的阶段数，也可以让 `Swapchain::shutdown()` 一次销毁 framebuffer、
attachment 和 core，但它必须发生在 Renderer 的 RenderPass 析构之前。

### 7.4 重建状态不要在模块内无限等待

当前 `recreateSwapChain()` 在尺寸为零时循环 `glfwWaitEvents()`。拆分后建议只检查一次并返回
状态：

```cpp
SwapchainBuildStatus Swapchain::rebuildCore()
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);

    if (glfwWindowShouldClose(window_))
    {
        return SwapchainBuildStatus::WindowClosed;
    }

    if (width == 0 || height == 0)
    {
        return SwapchainBuildStatus::Deferred;
    }

    // 安全重建……
    return SwapchainBuildStatus::Ready;
}
```

Application 收到 `Deferred` 后继续等待窗口事件，但 Renderer 和 Swapchain 都不会进入自己的
无限循环。

### 7.5 重建协调顺序

Application 负责协调，不让 Swapchain 直接依赖 Renderer：

```text
Renderer.waitForAllFrames()
    ↓
等待 Present Queue idle
    ↓
Swapchain.destroyFramebuffersAndAttachments()
    ↓
Swapchain.rebuildCore()
    ↓
如果 format 改变：Renderer.rebuildSwapchainPipelines(newFormat)
    ↓
Swapchain.createFramebuffers(Renderer.renderPass())
```

如果 format 没变，主 RenderPass/Pipeline 可以暂时复用；第一版为了保持实现简单，也可以每次
重建 Renderer 的 Swapchain-dependent resources，但必须保证旧 Framebuffer 先销毁。

### 7.6 Swapchain queue 的去向

当前 `swapChainDeletionQueue` 应迁入 `Swapchain`，或者被明确的 RAII/vector 清理替代。
迁移后 callback 只能销毁 Swapchain 自己的 ImageView、Framebuffer 和 Semaphore，不能销毁
Renderer Pipeline。

### 7.7 Swapchain 迁移完成条件

- `TriangleApplication` 不再拥有 Swapchain handle、images、views、extent、format。
- Depth/MSAA image、Framebuffer 和 present semaphore 全部由 Swapchain 管理。
- resize、最小化和关闭窗口通过返回状态协调，没有模块内部无限等待。
- 重建前仍然同时覆盖 Graphics Fence 和 Present Queue 使用期。
- Swapchain 可以重复重建和幂等 shutdown。

## 8. 第四步：抽取 Renderer

### 8.1 Renderer 应拥有的状态

```text
FrameContext 数组
currentFrame
UploadContext（可先迁入，后续再拆 UploadContext）
主 RenderPass
DescriptorSetLayout / PipelineLayout
主 Graphics Pipeline
Skybox Pipeline
逐帧录制和 Queue submit/present 协议
Renderer 自己的长期删除队列
```

`retiredBuffers` 跟随 FrameContext 迁入 Renderer，因为它的释放时刻由 Frame Fence 决定。

Renderer 不应拥有：

- GLFWwindow
- VulkanContext
- Swapchain
- SceneObject 容器
- 相机输入状态
- ImGui UI 状态
- 模型导入路径

它可以保存 Context/Swapchain 的非拥有指针，但它们必须比 Renderer 活得更久。

### 8.2 不要直接整体搬运 drawFrame()

当前 `drawFrame()` 混合了应用逻辑。建议先切成帧协议：

```cpp
enum class FrameStatus
{
    Ready,
    Skipped,
    RebuildSwapchain,
    WindowClosed,
};

struct FrameToken
{
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
};

struct BeginFrameResult
{
    FrameStatus status = FrameStatus::Skipped;
    FrameToken frame{};
};

class Renderer
{
public:
    BeginFrameResult beginFrame();
    void recordFrame(
        const FrameToken& frame,
        const RenderFrameData& data);
    FrameStatus endFrame(const FrameToken& frame);

    void waitForAllFrames();
    void retireBuffer(AllocatedBuffer&& buffer);
};
```

Application 主循环变成：

```cpp
const BeginFrameResult begin = renderer.beginFrame();

if (begin.status == FrameStatus::RebuildSwapchain)
{
    rebuildSwapchainModules();
    return;
}

if (begin.status != FrameStatus::Ready)
{
    return;
}

processCameraInput(deltaTime);
updateUniformBuffer(begin.frame.frameIndex, deltaTime);
buildImGui();

RenderFrameData data = buildRenderFrameData(begin.frame.frameIndex);
renderer.recordFrame(begin.frame, data);

if (renderer.endFrame(begin.frame) == FrameStatus::RebuildSwapchain)
{
    rebuildSwapchainModules();
}
```

这样 Application 决定“这一帧画什么”，Renderer 决定“如何安全提交这一帧”。

### 8.3 beginFrame() 的职责

`beginFrame()` 应负责：

1. 等待当前 Frame Fence。
2. 清空当前 Frame 的 `retiredBuffers`。
3. 获取 Swapchain image。
4. 处理 `VK_ERROR_OUT_OF_DATE_KHR`。
5. reset CommandPool。
6. 返回 frameIndex、imageIndex 和 CommandBuffer。

在成功提交前不要提前推进 `currentFrame`。

### 8.4 endFrame() 的职责

`endFrame()` 应负责：

1. 结束 CommandBuffer（如果 recordFrame 没有完成）。
2. reset 当前 Frame Fence。
3. `vkQueueSubmit()`。
4. 使用 `Swapchain::renderFinishedSemaphore(imageIndex)` present。
5. 将 OUT_OF_DATE/SUBOPTIMAL 转换为 `FrameStatus::RebuildSwapchain`。
6. 只有提交路径完成后才推进 `currentFrame`。

### 8.5 帧状态必须防止错误调用顺序

当前 `frameInProgress` 和手动 `finishFrame()` 容易在新增 return 分支时遗漏。Renderer 内部建议
维护：

```cpp
bool frameInProgress_ = false;
```

并在 Debug 构建断言：

```text
beginFrame 不能连续成功两次
recordFrame 只能使用当前 token
endFrame 只能结束当前 token
shutdown 前不能存在未完成帧
```

以后可以用 move-only `FrameToken`/scope guard 加强协议，本阶段先保持改动可控。

### 8.6 ImGui 可选化

不要让 Renderer 调用 `ImGui::NewFrame()` 或绘制 UI 控件。Application 构建 ImGui，Renderer
只接收最终的 `ImDrawData*`：

```cpp
if (data.imguiDrawData != nullptr)
{
    ImGui_ImplVulkan_RenderDrawData(
        data.imguiDrawData,
        commandBuffer);
}
```

这样 Renderer 在传入 `nullptr` 时就能无 ImGui 运行，也不会依赖 Application 的 UI 状态。
如果 `RenderTypes.hpp` 只保存该指针，可以前置声明 `struct ImDrawData;`，不必为了一个非拥有
指针把完整 ImGui 头文件传播给所有模块。

### 8.7 Pipeline 与 Swapchain 的边界

Renderer 拥有主 RenderPass/Pipeline，因为它们描述渲染过程；Swapchain 拥有 Framebuffer，
因为它由 Swapchain image、Depth/MSAA attachment 组成。

Renderer 应提供只读：

```cpp
VkRenderPass renderPass() const noexcept;
```

仅供 Swapchain 创建兼容 Framebuffer。Swapchain 不保存 Renderer 指针。

### 8.8 IBL 和资产资源的处理

本阶段不要一边移动 Renderer 一边重写约 1000 行 IBL 算法。推荐：

- IBL image、sampler、descriptor 和预计算流程暂时保持现有行为。
- 将其调用所需的 Device/Allocator 改为通过 Context 获取。
- command recording 只通过 `RenderFrameData` 获得 IBL descriptor set 或 image view。
- Stage 2 结束后再抽取 `EnvironmentLighting`，不要把它永久塞进 Renderer。

但 `TriangleApplication` 不应继续被 Renderer 反向引用。即使 IBL 暂时留在 Application，也要
通过窄输入接口传给 Renderer。

### 8.9 Renderer 迁移完成条件

- `FrameContext`、`UploadContext` 和 `currentFrame` 不再属于 Application。
- `Renderer.cpp` 不包含 `TriangleApplication.hpp`。
- Renderer 没有 `TriangleApplication*` 或 friend 访问。
- Fence、retired Buffer、acquire、submit、present 的顺序与拆分前一致。
- 主 RenderPass/Pipeline 的销毁由 Renderer 负责。
- 传入空 ImGui draw data 时仍可录制场景。

## 9. 初始化与关闭顺序

### 9.1 推荐初始化顺序

```text
初始化 GLFW
    ↓
创建 Window
    ↓
VulkanContext.initialize(window)
    ↓
Swapchain.initializeCore(context, window)
    ↓
Renderer.initialize(context, swapchain.format())
    ↓
Swapchain.createFramebuffers(renderer.renderPass())
    ↓
初始化 ImGui backend
    ↓
创建纹理、IBL、Scene 和 descriptor
```

Context 必须最先创建、最后销毁。

### 9.2 推荐关闭顺序

```text
标记 Renderer 不再接受新帧
    ↓
安全等待 Device idle
    ↓
关闭 ImGui Vulkan/GLFW backend
    ↓
销毁场景、纹理、IBL 和 descriptor
    ↓
清空 Renderer retired Buffer
    ↓
Swapchain 销毁 Framebuffer
    ↓
Renderer 销毁 Pipeline / RenderPass / FrameContext
    ↓
Swapchain 销毁 attachment / image view / semaphore / handle
    ↓
VulkanContext 销毁 Allocator / Device / Surface / Instance
    ↓
销毁 GLFWwindow
    ↓
glfwTerminate
```

特别注意：C++ 析构函数体在成员析构之前运行。若 Window 仍是 raw pointer，不能在
`TriangleApplication` 析构函数体中先销毁 Window、再期待 Context 成员稍后销毁 Surface。

本阶段可以继续使用显式、幂等 `Cleanup()`，按上述顺序对 `std::optional` 模块调用
`reset()`：

```cpp
swapchain.reset();
renderer.reset();
context.reset();
```

实际顺序要考虑 Framebuffer 引用 RenderPass：如果 `Swapchain::shutdown()` 会一次销毁全部
Swapchain 资源，应先 reset Swapchain，再 reset Renderer；如果采用分阶段 shutdown，则严格
按 9.2 的顺序调用。

不要仅依赖成员声明顺序掩盖跨模块依赖，建议 Debug 构建中为 shutdown 前置条件增加断言。

## 10. Window callback 的边界

当前 callback 通过 Window user pointer 直接调用 `TriangleApplication::drawFrame()`。拆分后仍可
让 user pointer 指向 Application，因为 resize、refresh 和输入是平台事件，由 Application
协调更合理。

推荐 callback 只更新状态：

```cpp
void TriangleApplication::framebufferResizeCallback(
    GLFWwindow* window,
    int,
    int)
{
    auto* app = static_cast<TriangleApplication*>(
        glfwGetWindowUserPointer(window));

    if (app != nullptr)
    {
        app->swapchainRebuildRequested = true;
    }
}
```

不推荐在 refresh callback 中重入 `Renderer::beginFrame()`。窗口 refresh、主循环和 ImGui
callback 同时触发绘制会让帧状态机更难保证。Stage 2 可以保留现有行为作为过渡，但完成前
应让所有绘制通过一个高层入口。

## 11. 如何避免拆分时的大爆炸

### 11.1 使用临时只读访问器

移动 Context 后，大量旧代码仍需要 `device`。可以短期在 Application 增加：

```cpp
VkDevice device() const noexcept
{
    return context->device();
}
```

但只用于一次迁移提交，并在调用点稳定后删除。不要同时保留旧 `device` 成员和 Context
device，这会产生两个事实来源。

### 11.2 不复制 Vulkan Handle 的所有权

可以复制 Handle 值用于一次 API 调用，但不能让两个模块都认为自己负责销毁它。例如：

```cpp
VkRenderPass Swapchain::renderPass; // 不应存在
```

Swapchain 只在 `createFramebuffers(renderPass)` 调用期间借用 RenderPass Handle。

### 11.3 每次迁移后立刻删除旧成员

某个资源迁入新模块后：

1. 更新全部调用点。
2. 删除 Application 中的旧成员。
3. 搜索旧名字确认没有第二份状态。
4. 构建并运行。

不要让旧成员和新成员并存多个提交。

### 11.4 不使用全局单例

不要把 Context 改成：

```cpp
VulkanContext::Get().device();
```

单例会隐藏依赖，使测试、多个窗口和销毁顺序更难处理。使用构造参数、初始化参数或明确的
非拥有引用。

## 12. 测试和故障注入

### 12.1 每个模块的最小测试重点

#### VulkanContext

- 默认构造后 shutdown 安全。
- shutdown 重复调用安全。
- 禁止复制和移动的 `static_assert`。
- Instance、Surface、Device、Allocator 后分别抛异常，确认安全退出。

#### Swapchain

- 默认构造后 shutdown 安全。
- 零尺寸返回 `Deferred`，不无限循环。
- Window closing 返回 `WindowClosed`。
- 连续重建时旧 Framebuffer/ImageView/Semaphore 不残留。
- 重建后 image 数量改变时 present semaphore 数量同步改变。

#### Renderer

- `FrameContext` 数量始终为 `MAX_FRAMES_IN_FLIGHT`。
- Fence 等待成功后才清空 retired Buffer。
- OUT_OF_DATE/SUBOPTIMAL 转换为重建状态。
- 未成功 begin 时不能 end。
- 空 `ImDrawData*` 不调用 ImGui Vulkan backend。

### 12.2 故障注入位置

建议依次在以下位置临时抛异常：

```text
VulkanContext 创建 Surface 后
VulkanContext 创建 Device 后
VulkanContext 创建 Allocator 后
Swapchain 创建 handle 后
Swapchain 创建一半 image view 后
Renderer 创建第一个 FrameContext 后
Renderer 创建 RenderPass 后
Swapchain 创建一半 framebuffer 后
```

每次确认进程退出、Validation 输出和资源清理，再移动故障点。

### 12.3 交互压力测试

- 连续 resize 30 秒。
- 快速最大化和还原。
- 最小化后恢复。
- 最小化状态直接关闭。
- resize 同时移动相机。
- resize 同时添加、删除和 rebuild 模型。
- 连续导入有效和损坏的 OBJ。
- 禁用 ImGui 路径运行场景渲染。

### 12.4 构建命令

```sh
cmake --build --preset linux-debug --parallel
cmake --build --preset linux-release --parallel
ctest --test-dir build/linux-debug --output-on-failure
ctest --test-dir build/linux-release --output-on-failure
```

阶段结束前至少在日常开发平台运行 Debug + Validation。其他平台至少保证构建，窗口和
MoltenVK/Win32 特有问题可在合并前补测。

## 13. 推荐的代码审查搜索

迁移 Context 后：

```sh
rg "TriangleApplication::(CreateInstance|createSurface|pickPhysicalDevice|createLogicalDevice|createAllocator)" src
rg "VkInstance instance|VkDevice device|VmaAllocator allocator" src/TriangleApplication.hpp
```

迁移 Swapchain 后：

```sh
rg "swapChain|swapChainImages|swapChainImageViews|swapChainExtent|swapChainImageFormat" src/TriangleApplication.hpp
rg "TriangleApplication::(createSwapChain|cleanupSwapChain|recreateSwapChain)" src
```

迁移 Renderer 后：

```sh
rg "FrameContext|UploadContext|currentFrame|frameInProgress" src/TriangleApplication.hpp
rg "TriangleApplication::(drawFrame|recordCommandBuffer|createFrameContexts)" src
rg '#include "TriangleApplication.hpp"' src/Renderer.cpp src/VulkanContext.cpp src/Swapchain.cpp
```

最终搜索不应命中对应的旧成员或旧成员函数。

## 14. 阶段完成检查表

### P0：模块边界和正确性

- [ ] `VulkanContext` 拥有 Instance、Surface、PhysicalDevice、Device、Queues 和 Allocator。
- [ ] `VulkanContext` 禁止复制和移动，并支持部分初始化失败。
- [ ] 所有 VMA/Device 子对象在 Context shutdown 前销毁。
- [ ] `Swapchain` 拥有 handle、images/views、attachments、framebuffers 和 present semaphores。
- [ ] Swapchain 重建通过明确状态处理零尺寸和窗口关闭。
- [ ] Framebuffer 先于 Renderer RenderPass 销毁。
- [ ] `Renderer` 拥有 FrameContext、帧同步、command recording 和主 Pipeline/RenderPass。
- [ ] Renderer 不持有或反向访问 `TriangleApplication`。
- [ ] Frame retired Buffer 仍在对应 Fence 成功等待后释放。
- [ ] Graphics Fence 和 Present Queue 的重建同步语义没有退化。
- [ ] `TriangleApplication` 只协调模块和高层场景/UI 流程。
- [ ] 拆分前后画面一致，Validation 无新增错误。

### P1：接口质量

- [ ] Context、Swapchain、Renderer 均有清晰的 non-copyable 声明。
- [ ] 原始 Handle 只通过只读 getter 暴露。
- [ ] 每个模块的 shutdown 都幂等且 `noexcept`。
- [ ] DeletionQueue callback 不跨模块销毁资源。
- [ ] 关键 Vulkan 对象通过 Context 设置 Debug Name。
- [ ] Renderer 使用明确的 begin/record/end 帧协议。
- [ ] Window callback 不直接重入底层渲染状态机。
- [ ] Renderer 在 `ImDrawData* == nullptr` 时可以运行。
- [ ] 新模块头文件不包含 `TriangleApplication.hpp`。

## 15. 完成后应该能回答的问题

1. 为什么 Context 必须比所有 VMA Buffer/Image 活得更久？
2. 为什么构造函数中途抛异常时，类自身析构函数不会运行？
3. 为什么本阶段对顶层原始 Handle 使用 `initialize()` + `shutdown()`？
4. 为什么 Swapchain Framebuffer 和 Renderer RenderPass 会形成跨模块依赖？
5. 为什么 Swapchain 不应该保存 Renderer 指针来解决这个依赖？
6. 为什么 Graphics Fence 不能证明 Present Queue 已经停止使用旧 Swapchain image？
7. 为什么 Renderer 不应该直接读取 `TriangleApplication` 的 private 场景状态？
8. 为什么 begin/end frame 应返回状态，而不是在内部直接重建全部模块？
9. 为什么 resize callback 最好只记录请求，而不是直接重入 drawFrame？
10. 为什么把实现移动到多个 `.cpp` 不等于真正完成模块拆分？

能够回答这些问题，并通过故障注入、Validation 和交互压力测试后，才算真正完成阶段 2。
