# 阶段 1：资源生命周期与异常安全实施指南

本文对应[开发路线图](overview.md)中的“阶段 1：资源生命周期与异常安全”。它不是一份
可以直接粘贴的最终补丁，而是一条适合逐步手敲、编译、运行和理解的实现路径。

本阶段只解决资源所有权和清理问题，不拆分 `TriangleApplication`，不引入完整 RAII
封装，也不同时重写 Renderer。完成以后，再进入 VulkanContext、Swapchain 和 Renderer
拆分会安全得多。

## 1. 先理解当前问题

当前正常运行路径是：

```text
InitWindow
    -> InitVulkan
        -> MainLoop
            -> vkDeviceWaitIdle
                -> Cleanup
```

这条路径能工作，但异常会从中间跳到 `main.cpp` 的 `catch`，跳过 `Cleanup()`：

```text
InitWindow
    -> InitVulkan
        -> 某个 VK_CHECK 抛出异常
            -> main.cpp catch
                -> 程序返回，已经创建的资源没有统一清理
```

此外，当前 `Cleanup()` 默认所有初始化步骤都已成功，无条件使用 `device`、`allocator`、
`swapChain`、`instance` 和 `window`。如果初始化只完成了一半，销毁函数可能收到空句柄，
或者某个 deletion callback 使用已经失效的父对象。

要建立的核心不变量是：

1. 每个资源从创建成功的那一刻起，就必须有明确的销毁路径。
2. 子对象一定先于父对象销毁。
3. GPU 不再使用资源后，CPU 才能销毁它。
4. `Cleanup()` 可以在任意初始化阶段调用，也可以重复调用。
5. 清理路径不能抛异常。

## 2. 资源依赖顺序

先不要急着改代码。把下面的依赖关系和当前成员对应起来：

```text
GLFW initialized
└── GLFWwindow ──────────────┐
                             ├── VkSurfaceKHR
VkInstance ─────────────────┘
├── Debug Messenger
└── VkPhysicalDevice
    └── VkDevice

VkInstance + VkPhysicalDevice + VkDevice
└── VmaAllocator
    ├── AllocatedBuffer
    └── AllocatedImage

VkDevice
├── Swapchain
│   ├── swapchain image views
│   ├── depth/MSAA images
│   ├── framebuffers
│   └── present semaphores
├── pipelines / render passes / descriptor pools
├── frame command pools / fences / semaphores
└── ImGui Vulkan backend
```

销毁顺序大体是这张图的反方向。尤其注意：

- VMA 创建的 Buffer/Image 必须在 `vmaDestroyAllocator()` 之前销毁。
- 所有 Device 子对象必须在 `vkDestroyDevice()` 之前销毁。
- Surface 必须在 Instance 之前销毁。
- ImGui Vulkan backend 必须在 Device 和 descriptor pool 之前关闭。
- Swapchain framebuffer 必须在它引用的 image view、render pass 之前销毁。

当前三类队列的职责应固定为：

| 队列 | 放入的资源 | 何时 flush |
| --- | --- | --- |
| Main | 生命周期覆盖整个 Device 的资源 | Device 销毁前，且 GPU 已停止使用资源后 |
| Swapchain | 依赖 swapchain image/extent 的资源 | 重建或最终退出时，在旧 Swapchain 销毁前 |
| Frame | 运行时被替换、仍可能被在途帧引用的资源 | 对应 Frame Fence 成功等待之后 |

## 3. 推荐的提交顺序

建议把阶段 1 拆成六个小提交。每个提交都先完成 Debug 构建，再启动 Validation Layers
验证，不要攒到最后一次性调试。

```text
1. GLFW 创建失败检查
2. DeletionQueue 语义和测试
3. Cleanup 幂等化与析构兜底
4. 创建过程的局部回滚
5. 动态资源延迟销毁约束
6. Swapchain 和故障注入压力测试
```

---

## 4. 第一步：检查 GLFW 初始化

### 修改位置

- `src/TriangleApplication.hpp`
- `src/TriangleApplication.cpp`

### 需要增加的状态

在 `TriangleApplication` 中增加一个状态，用来区分“GLFW 从未初始化”和“GLFW 已经
初始化，但 Vulkan 尚未初始化”：

```cpp
bool glfwInitialized = false;
```

不要通过 `window != nullptr` 推断 GLFW 是否初始化，因为 `glfwInit()` 成功而
`glfwCreateWindow()` 失败时，window 仍然是空的，但依然需要 `glfwTerminate()`。

### 修改 `InitWindow()`

按以下顺序手敲：

```cpp
if (glfwInit() != GLFW_TRUE)
{
    throw std::runtime_error("failed to initialize GLFW");
}
glfwInitialized = true;

// window hints...

window = glfwCreateWindow(...);
if (window == nullptr)
{
    throw std::runtime_error("failed to create GLFW window");
}

// 只有 window 有效后才能设置 user pointer 和 callbacks。
```

### 为什么这样改

GLFW 的函数不使用 `VkResult`，所以不能套用 `VK_CHECK`。如果 window 创建失败后仍调用
`glfwSetWindowUserPointer(window, ...)`，会把空指针传给 GLFW。显式抛出异常可以让错误
进入后面统一的析构清理路径。

### 本步验证

1. 正常启动和退出。
2. 临时让创建窗口的宽度或平台条件失败，确认异常信息清楚。
3. 确认失败时没有继续执行 Vulkan Instance 创建。

---

## 5. 第二步：整理 DeletionQueue

### 修改位置

- `src/DeletionQueue.hpp`
- 新增一个很小的测试源文件，例如 `tests/DeletionQueueTests.cpp`
- `CMakeLists.txt`

### 修改目标

当前 `pushFunction(std::function<void()>&&)` 收到右值后又执行复制：

```cpp
deletors.push_back(function);
```

改为移动，并修正拼写：

```cpp
callbacks.push_back(std::move(function));
```

记得包含 `<utility>`。同时增加只读调试接口：

```cpp
bool empty() const noexcept;
std::size_t size() const noexcept;
```

可以保留 `pushFunction` 这个名字以减少本阶段改动，也可以统一改为 `push`。不要只改
声明而漏掉工程里所有调用点。

### `flush()` 的约定

`flush()` 必须按 LIFO 执行：最后创建的对象通常依赖更早创建的对象，因此要先销毁。
执行完成后清空容器；对空队列调用 `flush()` 也必须安全。

建议把 `flush()` 声明为 `noexcept`，但这要求所有 callback 都是纯销毁操作，不能在
callback 中使用 `VK_CHECK` 或主动抛异常。原因是析构期间再次抛异常可能导致
`std::terminate()`。

一种适合当前阶段的接口轮廓是：

```cpp
struct DeletionQueue
{
    std::deque<std::function<void()>> callbacks;

    void pushFunction(std::function<void()> function);
    void flush() noexcept;
    bool empty() const noexcept;
    std::size_t size() const noexcept;
};
```

参数按值接收再 move，调用者传 lambda 时只需要一次构造，接口也比只接收右值引用更
容易使用。你也可以保留右值引用版本，重点是入队时必须 move。

### 最小单元测试

本阶段不需要引入大型测试框架。可以创建一个返回非零值表示失败的小 executable，并
通过 CTest 运行。至少覆盖：

1. 新队列为 empty，size 为 0。
2. 三个 callback 按 `1, 2, 3` 入队，flush 后执行顺序为 `3, 2, 1`。
3. flush 后重新变为空。
4. 空队列重复 flush 不崩溃。
5. 已 flush 的队列再次 flush 不会重复执行 callback。

在 CMake 中使用 `include(CTest)`、`add_executable(...)` 和 `add_test(...)` 即可。

### 为什么先改队列

下一步会让析构函数依赖这些队列。如果队列的重复 flush、顺序或异常约定还不明确，
就很难判断 Cleanup 是否真正幂等。

---

## 6. 第三步：让 Cleanup 支持部分初始化和重复调用

### 修改位置

- `src/TriangleApplication.hpp`
- `src/TriangleApplication.cpp`
- `main.cpp` 只需检查行为，通常不必改

### 6.1 增加析构函数和清理状态

在 public 区域声明：

```cpp
~TriangleApplication() noexcept;
```

增加状态：

```cpp
bool cleanedUp = false;
```

析构函数只调用 `Cleanup()`。保留 `run()` 末尾的显式 `Cleanup()` 也可以，因为
`Cleanup()` 将被改成幂等；这样正常退出时立即释放，析构时的第二次调用只是空操作。

### 6.2 清理函数不能抛异常

把声明改为：

```cpp
void Cleanup() noexcept;
```

开头先阻止重复和重入：

```cpp
if (cleanedUp)
{
    return;
}
cleanedUp = true;
rendererReady = false;
```

把 `cleanedUp = true` 放在开头，是为了即使后续某个第三方 shutdown 行为异常，也不会
在析构路径再次尝试销毁同一批对象。

### 6.3 安全等待 Device

只有 `device != VK_NULL_HANDLE` 时才能调用 `vkDeviceWaitIdle()`。清理函数中不要使用
会抛异常的 `VK_CHECK`：

```cpp
if (device != VK_NULL_HANDLE)
{
    const VkResult result = vkDeviceWaitIdle(device);
    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
    {
        // 记录错误，但继续尽最大努力释放 CPU/Vulkan 对象。
    }
}
```

`VK_ERROR_DEVICE_LOST` 的含义是不能再依赖 GPU 正常完成工作，但仍应继续调用 Vulkan
销毁函数释放主机侧对象。不要在错误路径无限重复等待。

`MainLoop()` 末尾已有一次 `vkDeviceWaitIdle()`。为了让“等待属于清理职责”这个规则更
明确，可以删除 MainLoop 中的等待，只保留 Cleanup 的安全等待；也可以暂时保留，但
必须保证 Cleanup 自己不依赖 MainLoop 已经执行。

### 6.4 按依赖顺序清理

建议按照以下结构重新组织 `Cleanup()`，不要无条件调用任何父对象相关函数：

```text
1. 标记 renderer 不可再绘制
2. 如果 Device 有效，等待 Device idle
3. 如果 Device 有效，清理 Swapchain 子对象和 Swapchain
4. 清理仍在 sceneObjects 中的 Buffer
5. flush 所有 Frame deletion queues
6. flush Main deletion queue
7. 销毁 VmaAllocator
8. 销毁 VkDevice
9. 销毁 Debug Messenger
10. 销毁 Surface
11. 销毁 Instance
12. 销毁 GLFWwindow
13. 如果 GLFW 初始化过，调用 glfwTerminate
```

为什么 Frame queue 要在 Allocator 之前 flush：其中保存的是延迟执行的
`vmaDestroyBuffer()`。为什么 Main queue 要在 Device 之前 flush：其中保存了 Pipeline、
RenderPass、Image、Sampler、CommandPool、ImGui backend 等 Device 子对象。

每次直接销毁 member handle 后立刻清空：

```cpp
vkDestroyDevice(device, nullptr);
device = VK_NULL_HANDLE;
```

VMA 和 GLFW 状态也同样清空：

```cpp
vmaDestroyAllocator(allocator);
allocator = nullptr;

glfwDestroyWindow(window);
window = nullptr;
```

对于 `cleanupSwapChain()`，至少增加以下保护：

```cpp
if (device == VK_NULL_HANDLE)
{
    return;
}

swapChainDeletionQueue.flush();
if (swapChain != VK_NULL_HANDLE)
{
    vkDestroySwapchainKHR(device, swapChain, nullptr);
    swapChain = VK_NULL_HANDLE;
}
```

### 6.5 为什么析构兜底有效

`TriangleApplication app;` 位于 `main()` 的栈上。即使 `app.run()` 抛异常并进入 catch，
函数返回时依然会调用 `app` 的析构函数。于是初始化成功到哪一步，Cleanup 就清理到哪
一步。

注意：如果 `TriangleApplication` 的构造函数本身抛异常，对象析构函数不会运行。当前
构造函数没有创建 Vulkan 资源，因此本阶段不要把初始化逻辑搬进构造函数。

### 本步验证

1. 正常退出，确认显式 Cleanup 和析构的第二次 Cleanup 不会 double free。
2. 临时在 `InitWindow()` 后抛异常。
3. 临时在 Instance、Device、Allocator、Swapchain 创建后分别抛异常。
4. 每个位置都检查进程退出、Validation 输出和是否崩溃。

---

## 7. 第四步：处理“创建到一半”的局部回滚

只检查成员句柄仍然不够。一个创建函数内部可能已经创建第一个对象，却在创建第二个
对象时抛异常，而 deletion callback 直到函数末尾才注册。

当前 `createUploadContext()` 就是一个学习样例：

```text
创建 command pool
    -> 分配 command buffer
        -> 创建 fence
            -> 最后才注册一个统一 deletion callback
```

如果 fence 创建失败，command pool 已经存在，但统一 callback 尚未入队。

### 推荐做法：创建成功后立即登记

每次 Vulkan 创建调用成功后，立即把对应销毁操作加入 Main queue：

```text
vkCreateCommandPool 成功
    -> 立即登记 vkDestroyCommandPool
vkCreateFence 成功
    -> 立即登记 vkDestroyFence
```

由于 Main queue 是 LIFO，Fence 会先销毁，CommandPool 后销毁，顺序自然正确。
CommandBuffer 不需要单独 free；销毁其 CommandPool 会释放它。

登记 callback 时应优先按值捕获刚创建的 handle，而不是在执行时重新读取可能已经变化的
member：

```cpp
const VkFence fence = uploadContext.fence;
mainDeletionQueue.pushFunction([this, fence]() noexcept {
    vkDestroyFence(device, fence, nullptr);
});
```

这一点对以后会重建或替换的资源尤其重要。按值捕获保存的是“本次创建的对象”；捕获
`this` 后再读取 `uploadContext.fence`，读到的可能已经是下一次创建的新对象。

逐个检查这些“一个函数创建多个对象”的位置：

- `createUploadContext()`
- `createFrameContexts()`
- `initImGui()`
- `createIrradianceResources()`
- `createPrefilterResources()`
- `createBRDFLUTResources()`
- `createTextureImage()` 及 staging upload 路径

### ImGui 的特殊处理

当前 ImGui 的三个层次必须分别考虑：

```text
ImGui::CreateContext
ImGui_ImplGlfw_InitForVulkan
ImGui_ImplVulkan_Init
```

检查 `ImGui_ImplGlfw_InitForVulkan()` 的 bool 返回值。每层成功后立即登记对应 shutdown，
依靠 LIFO 得到以下退出顺序：

```text
ImGui_ImplVulkan_Shutdown
ImGui_ImplGlfw_Shutdown
ImGui::DestroyContext
```

不要在 Vulkan backend 尚未成功时调用它的 Shutdown，也不要在 backend 还活着时先销毁
ImGui context 或 Device。

### 另一种做法：局部 scope guard

你也可以在创建函数内部使用 scope guard：函数尚未完成时由 guard 回滚，完成后把资源
正式转交给成员和 DeletionQueue。这个方式更接近以后要学习的 RAII，但本阶段不要同时
引入一套庞大的通用封装。先选一两个函数实践即可。

### 本步验证

在每个多资源创建函数的第二或第三步后临时抛异常，确认之前创建的对象仍会被销毁。
每次只保留一个故障点，验证后删除，避免忘记测试宏导致正常构建无法启动。

---

## 8. 第五步：理解并约束动态资源延迟销毁

当前对象删除按钮和 `rebuildMesh()` 已经调用 `destroyBufferDeferred()`，这是正确方向，
不需要重新实现一套机制。

### 为什么不能立即销毁

CPU 从 `sceneObjects` 擦除对象，并不表示 GPU 已经执行完之前录制的 command buffer：

```text
CPU：删除对象 -> 想销毁 Buffer
GPU：上一帧可能仍在执行 vkCmdDrawIndexed，并引用该 Buffer
```

立即调用 `vmaDestroyBuffer()` 会造成 use-after-free。Validation Layers 有时能发现，
但不能依赖它捕获所有时序问题。

### 当前 Frame queue 为什么安全

`drawFrame()` 在当前 Frame Fence 成功等待后先 flush 该 frame 的 deletion queue。删除操作
发生在本帧录制新 command buffer 之前，旧 Buffer 被放入当前 frame queue。等这个 frame
下一次被复用时，它的 Fence 已经证明本次提交完成；同一 graphics queue 上更早提交的帧
也按队列顺序完成，因此旧 Buffer 可以安全释放。

需要把这个使用前提写进 `destroyBufferDeferred()` 附近的注释：

- 只能在渲染线程调用。
- 调用点位于当前 frame fence 已等待之后、本帧提交之前。
- callback 必须在 Allocator 销毁前 flush。

### 检查句柄转移

`destroyBufferDeferred()` 应继续保持这个顺序：

```cpp
AllocatedBuffer oldBuffer = buffer;
buffer = {};
// 把 oldBuffer 捕获到 callback 中。
```

清空原对象很重要，它把“所有权”从 member 转移到 callback，避免 Cleanup 或其他代码再次
销毁同一 handle。

### `destroySceneObject()` 的职责

当前函数在最终 Cleanup 中使用，而 Cleanup 已先等待 Device idle，因此可以立即销毁。
建议通过命名或注释明确区分：

- `destroySceneObjectNow()`：只允许 GPU idle 后使用。
- `destroySceneObjectDeferred()`：运行时删除使用。

也可以暂时不重命名，但不要让 ImGui 删除路径重新调用立即销毁版本。

### 本步验证

1. 连续添加并删除对象，至少循环几十次。
2. 开启自动旋转和相机移动时连续删除。
3. 连续切换 Cube、Sphere 和 OBJ，触发 rebuild。
4. 确认没有 `vkDeviceWaitIdle()` 被加入运行时 rebuild/delete 路径。
5. 确认 Validation 没有 “buffer in use” 或无效 handle 报错。

---

## 9. 第六步：Swapchain 与窗口边界测试

当前 `recreateSwapChain()` 已经等待所有 Frame Fence，并额外等待 Present Queue idle，然后
才销毁旧 Swapchain。这是因为 Frame Fence 只覆盖 graphics submission，不证明 present
queue 已经停止使用旧 image。

### 最小化窗口

当前代码在 framebuffer 尺寸为零时循环 `glfwWaitEvents()`。增加关闭窗口判断，避免用户在
最小化期间关闭窗口后仍停留在等待循环：

```cpp
while (width == 0 || height == 0)
{
    if (glfwWindowShouldClose(window))
    {
        return;
    }
    glfwWaitEvents();
    glfwGetFramebufferSize(window, &width, &height);
}
```

以后阶段 2 可以把 `recreateSwapChain()` 改成返回状态；本阶段先保持改动小。

### Swapchain 清理检查

确认 `swapChainDeletionQueue` 的 LIFO 顺序满足：

```text
framebuffer
-> depth/MSAA resources
-> image views
-> present semaphores
-> VkSwapchainKHR
```

实际顺序由“创建后何时入队”决定，不能只看 Cleanup 中的一行 `flush()`。使用 RenderDoc
或 Validation 时，为对象增加名称会更容易定位，但 Debug Name 可以作为本阶段 P1 收尾，
不应阻塞异常安全 P0。

### 压力测试清单

- 连续拖动窗口边缘改变大小。
- 快速最大化、还原。
- 最小化几秒后恢复。
- 最小化状态直接关闭窗口。
- resize 同时旋转相机、增删模型。
- 连续导入有效模型。
- 导入不存在或损坏的 OBJ，确认错误不会破坏已有场景。

每次记录：是否崩溃、是否卡死、Validation 消息、最后一次成功操作。

---

## 10. 故障注入方法

只测试正常路径无法证明异常安全。最简单的学习方式是在初始化流程中临时插入：

```cpp
throw std::runtime_error("intentional failure after allocator creation");
```

推荐依次放在：

1. GLFW 初始化成功后。
2. Window 创建成功后。
3. Instance 创建成功后。
4. Device 创建成功后。
5. Allocator 创建成功后。
6. Swapchain 创建成功后。
7. ImGui GLFW backend 成功后。
8. 第一张 IBL image 创建后。

每验证一个位置就移动这行，不要同时放多个。提交前用 `rg "intentional failure"` 确认临时
故障点已经删除。

如果想保留长期故障注入，可以以后增加仅 Debug 编译启用的 checkpoint；本阶段先手动
注入更容易看懂控制流。

## 11. 每个提交的验证命令

Linux 上至少执行：

```sh
cmake --build --preset linux-debug
cmake --build --preset linux-release
ctest --test-dir build/linux-debug --output-on-failure
```

然后运行 Debug 版本观察 Validation Layers。Release 构建验证宏、优化和无 Validation
配置下也能编译；资源生命周期的主要诊断仍以 Debug + Validation 为准。

如果你在 macOS 或 Windows 上开发，把 preset 换成对应平台名称。跨平台验证不要求每个
小提交都在三台机器完成，但阶段结束前应至少覆盖日常开发平台，并在合并前补其他目标
平台的构建。

## 12. 阶段完成检查表

### P0：必须完成

- [ ] 检查 `glfwInit()` 和 `glfwCreateWindow()` 返回值。
- [ ] `Cleanup()` 可以处理部分初始化。
- [ ] `Cleanup()` 可以重复调用且不 double free。
- [ ] 初始化异常由析构路径触发清理。
- [ ] 清理只在有效 Device 上等待，并处理 `VK_ERROR_DEVICE_LOST`。
- [ ] 多资源创建函数不会在中途失败时泄漏先前创建的对象。
- [ ] 运行时删除/替换 Buffer 使用 Frame deletion queue。
- [ ] Frame deletion queue 在对应 Fence 等待成功后 flush。
- [ ] resize、最小化、恢复、导入、删除压力测试无 Validation 错误。

### P1：建议随阶段完成

- [ ] DeletionQueue 使用 move 入队并修正 `deletors` 拼写。
- [ ] DeletionQueue 提供 `empty()` 和 `size()`。
- [ ] DeletionQueue 的 LIFO、空 flush、重复 flush 测试通过。
- [ ] 三类 deletion queue 的职责在代码注释中可见。
- [ ] 动态资源销毁接口名称能区分立即和延迟销毁。
- [ ] 关键 Vulkan 对象有便于 Validation/RenderDoc 定位的 Debug Name。

## 13. 完成后你应该能回答的问题

不要只以“程序没有崩溃”作为学习完成标准。你应该能用自己的话回答：

1. 为什么 C++ 对象析构可以覆盖 `InitVulkan()` 中途抛异常的路径？
2. 为什么 Cleanup 不能使用会抛异常的 `VK_CHECK`？
3. 为什么 VMA Allocator 必须晚于所有 AllocatedBuffer/Image 销毁？
4. 为什么 DeletionQueue 使用 LIFO 而不是 FIFO？
5. 为什么等待 graphics Fence 不能完全替代等待 Present Queue？
6. 为什么从 `std::vector<SceneObject>` 擦除对象后不能立即销毁 GPU Buffer？
7. 为什么“创建函数末尾统一登记销毁”仍可能在中途失败时泄漏？
8. 为什么清空 handle 有助于幂等清理，但不能替代正确的依赖顺序？

能回答这些问题，并通过故障注入与压力测试，才算真正完成阶段 1。
