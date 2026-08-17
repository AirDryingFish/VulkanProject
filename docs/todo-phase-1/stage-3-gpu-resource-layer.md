# 阶段 3：通用 GPU 资源层实施指南

本文对应[总览中的阶段 3](overview.md#阶段-3通用-gpu-资源层)。评估基线为 2026-08-17 的当前工作树，最近提交为 `1754cd0 Encapsulate swapchain core rebuild`。

阶段 1 已建立 move-only RAII 资源和幂等清理，阶段 2 已把 `VulkanContext`、`Swapchain` 与 `Renderer` 的主要职责从 `TriangleApplication` 中拆出。阶段 3 不继续扩大模块拆分，而是在现有边界上统一“资源是什么、如何创建、如何上传、由谁释放”。

本文按适合学习的顺序组织。建议每完成一个小节就编译、运行并提交，不要一次性照抄所有接口。

---

## 1. 当前项目评估

### 1.1 已经具备的基础

当前代码不是从零开始建设资源层，以下基础可以直接保留：

- `AllocatedBuffer` 和 `AllocatedImage` 已经是 move-only RAII 类型；
- 两个类型的析构函数都会调用 `reset()`，移动赋值前也会释放旧资源；
- `UniqueShaderModule` 已限制 shader module 的生命周期；
- `VulkanContext` 集中持有 instance、device、queue 与 VMA allocator，并提供 buffer、image 和 shader module 的创建入口；
- `Renderer` 已拥有帧同步对象、command buffer、上传 command pool/fence 和 `immediateSubmit()`；
- `Renderer::retireBuffer()` 已表达“等对应帧 fence 完成后才能释放”的语义；
- `Swapchain` 已拥有 swapchain image view、颜色/深度 image 和 framebuffer；
- 测试中已有编译期断言，检查资源包装类型不可复制且可无异常移动。

因此 Stage 3 不应推翻现有设计。正确方向是逐步增强这些类型，并减少业务函数重复填写 Vulkan 创建结构体。

### 1.2 当前主要缺口

#### 资源不能完整描述自身

`AllocatedBuffer` 目前只保存 allocator、buffer、allocation 和映射地址，不能直接回答 buffer 的 size、usage 和创建时要求的内存属性。

`AllocatedImage` 虽然记录了 `mipLevels`，但没有统一记录 extent、format、usage、array layer 和 sample count。后续上传、生成 mip 和创建 view 仍依赖调用处的平行变量。

#### 创建接口参数过长

`VulkanContext::createImage()` 依次接收宽、高、mip 数、采样数、格式、tiling、usage、内存属性、layer 和 flags。多个同类型参数靠位置区分，容易写错，也不利于增加 debug name。

#### 上传动作仍然碎片化

上传已通过 `Renderer::immediateSubmit()` 使用独立 command pool 和 fence，这是正确基础；但目前仍存在：

- 一个 mesh 的 vertex buffer 和 index buffer 分别提交并等待；
- texture 的 layout transition、copy 和 mipmap 生成可能形成多次提交；
- `copyBuffer()`、`transitionImageLayout()`、`copyBufferToImage()`、`generateMipmaps()` 仍主要是 `TriangleApplication` 成员；
- 同步上传每次等待 fence，资源越多，提交和等待次数越多。

Stage 3 先减少不必要的同步提交，不立刻实现后台线程或异步 transfer queue。

#### sampler 与 IBL 创建代码重复

普通纹理、skybox、irradiance、prefilter 和 BRDF LUT 都直接填写 `VkSamplerCreateInfo`。IBL 的三个离屏流程还分别创建单颜色附件 render pass、framebuffer 和无顶点输入 pipeline。

这些代码可以共享一部分，但不能强行压成一个“万能 pipeline builder”。三个 IBL pass 的 descriptor、push constant、shader 和 dependency 并不完全相同。

#### 仍有裸 handle 是合理的

`TriangleApplication` 仍持有场景资源、sampler、descriptor 和 IBL 中间对象；部分对象仍由主 `DeletionQueue` 清理。这不代表 Stage 2 失败，也不要求 Stage 3 一次清空所有裸 handle。

Stage 3 只处理能形成稳定窄接口的共性。Scene、Material、EnvironmentLighting 的所有权迁移属于后续阶段。

### 1.3 阶段判断

从代码结构看，项目可以进入 Stage 3。进入前只需保留一份 Stage 2 运行基线：Debug/Release 构建、测试、正常渲染、窗口缩放/最小化恢复，以及 Validation Layer 无生命周期错误。

如果这些运行项尚未在当前提交上确认，先做第 5 节；不需要继续修改 Stage 2 架构。

---

## 2. Stage 3 的目标与边界

### 2.1 本阶段目标

完成后，项目应满足：

1. buffer 和 image 包装类型能描述自身的关键创建信息；
2. 创建参数由有名字的 description struct 表达，不再主要依赖长参数列表；
3. `AllocatedBuffer` / `AllocatedImage` 演进为 `GpuBuffer` / `GpuImage`；
4. vertex/index 数据可以在一次同步上传提交中完成；
5. texture 的 transition、copy、mipmap 命令能在同一 command buffer 中连续记录；
6. sampler 使用统一的 `SamplerDesc` 和明确所有者；
7. IBL 只提取真正重复的单颜色 render pass、单附件 framebuffer 和 pipeline 公共片段；
8. 每一步都保持现有渲染结果、同步语义和销毁顺序。

### 2.2 本阶段不做什么

- 不拆 `Scene`、`Material`、`EnvironmentLighting` 大模块；
- 不引入 bindless descriptor 或 render graph；
- 不实现后台上传线程或 dedicated transfer queue；
- 不抽象“所有 Vulkan handle 通用 RAII 模板”；
- 不重写 PBR、IBL 算法或 shader；
- 不为了统一而改变 render pass dependency、barrier 或 layout transition；
- 不要求删除整个 `DeletionQueue`。

这些约束让每次提交只验证一个概念。

---

## 3. 必须一直成立的规则

### 3.1 所有权规则

- `GpuBuffer` 和 `GpuImage` 不可复制，只能移动；
- 包装类型只销毁自己明确拥有的 handle；
- borrowed handle 不进入 RAII 包装对象；
- VMA buffer/image 必须在 `VulkanContext` 的 allocator 销毁前释放；
- `GpuImage` 若拥有 default image view，必须先销毁 view，再销毁 image；
- cubemap face view、mip view 等辅助 view 不应偷偷塞入只承诺拥有一个 view 的 `GpuImage`；
- `reset()` 必须可重复调用；
- 移动后的源对象必须回到完整空状态，包括 metadata。

### 3.2 上传规则

- staging 与 destination 资源必须存活到上传 fence 完成；
- batch 任一步抛异常时，已创建资源依靠 RAII 回收；
- command-recording helper 只记录命令，不在内部再次调用 `immediateSubmit()`；
- 同步 batch 只等待一次 fence，不能在每条 copy 后分别等待；
- 未设计 queue family ownership transfer 前，不切换 dedicated transfer queue。

### 3.3 元数据与 helper 规则

- metadata 来自真正用于创建 Vulkan 对象的 description；
- metadata 不代替 Vulkan 同步状态；
- debug name 指针只在创建期间使用，除非包装类型明确拥有 `std::string`；
- `reset()` 后 metadata 也恢复默认空值；
- helper 必须暴露会影响正确性的 Vulkan 参数；
- 两处代码只有表面相似但同步语义不同，就保留差异；
- 先找完全相同的结构，再提取 helper，不预先设计万能 builder。

---

## 4. 推荐提交顺序

| 顺序 | 建议提交内容 | 主要学习点 |
|---|---|---|
| 0 | 记录 Stage 2 运行基线 | 区分旧问题和新回归 |
| 1 | 为现有资源类型增加 metadata | RAII 对象也描述资源 |
| 2 | 重命名为 `GpuBuffer` / `GpuImage` | 机械重命名与行为改动分离 |
| 3 | 增加 `BufferDesc` / `ImageDesc` | 用字段名消除长参数列表 |
| 4 | 迁移所有创建调用并删除旧重载 | 小步迁移与编译器检查 |
| 5 | 分离“记录上传命令”和“提交命令” | command buffer 与 queue submit 层次 |
| 6 | 一次提交上传 vertex/index | batch 和 staging 生命周期 |
| 7 | 一次提交完成 texture 上传 | image barrier 与命令顺序 |
| 8 | 引入 `SamplerDesc` 和 sampler RAII | 小型 Vulkan 对象的所有权 |
| 9 | 提取 IBL 的窄 helper | 复用与过度抽象的边界 |
| 10 | Validation、故障注入和收尾 | 用证据判断阶段完成 |

不要在第一个 commit 同时重命名类型和重写上传系统，否则编译错误与运行错误会混在一起。

---

## 5. 第 0 步：固定 Stage 2 基线

```powershell
git status --short
git log -5 --oneline

cmake --preset windows-mingw-debug
cmake --build --preset windows-mingw-debug --parallel
ctest --test-dir build/windows-mingw-debug --output-on-failure

cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release --parallel
```

当前 `CMakePresets.json` 只有 configure/build preset，没有 test preset，所以测试通过 `--test-dir` 指向对应构建目录。

手动运行至少检查：

- 主模型、skybox、IBL 和 ImGui 正常；
- 连续缩放窗口；
- 最小化后恢复；
- 关闭时没有 Validation Layer 生命周期报错；
- 没有 device lost 或 fence/command buffer 状态错误。

把结果写进 commit message 或个人学习记录，后续出现黑屏时才能定位是哪一步引入的。

---

## 6. 第 1 步：先增加 metadata，不急着改名

第一步继续使用 `AllocatedBuffer` / `AllocatedImage` 名称，只增加它们描述自身的能力。

### 6.1 Buffer metadata

在 `VulkanResources.hpp` 中增加访问器与成员：

```cpp
VkDeviceSize size() const noexcept { return size_; }
VkBufferUsageFlags usage() const noexcept { return usage_; }
VkMemoryPropertyFlags requiredMemoryProperties() const noexcept
{
    return requiredMemoryProperties_;
}

VkDeviceSize size_ = 0;
VkBufferUsageFlags usage_ = 0;
VkMemoryPropertyFlags requiredMemoryProperties_ = 0;
```

构造函数接收这些值，由 `VulkanContext::createBuffer()` 在成功创建后传入。

字段表示调用者提出的内存要求，不一定等于 VMA 最终选中 memory type 的全部属性，因此名称不要含糊地暗示它是查询结果。

### 6.2 Image metadata

建议增加：

```cpp
VkExtent3D extent() const noexcept { return extent_; }
VkFormat format() const noexcept { return format_; }
VkImageUsageFlags usage() const noexcept { return usage_; }
uint32_t mipLevels() const noexcept { return mipLevels_; }
uint32_t arrayLayers() const noexcept { return arrayLayers_; }
VkSampleCountFlagBits samples() const noexcept { return samples_; }
```

私有成员的默认值：

```cpp
VkExtent3D extent_{0, 0, 0};
VkFormat format_ = VK_FORMAT_UNDEFINED;
VkImageUsageFlags usage_ = 0;
uint32_t mipLevels_ = 0;
uint32_t arrayLayers_ = 0;
VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
```

资源有效性仍由 `image() != VK_NULL_HANDLE` 或显式 `operator bool()` 判断，不要只看 metadata。

### 6.3 修改 move 与 `reset()`

移动构造和移动赋值必须移动所有 metadata，并清空源对象。标量字段适合用 `std::exchange`：

```cpp
size_ = std::exchange(other.size_, 0);
usage_ = std::exchange(other.usage_, 0);
```

`reset()` 不应因 handle 为空而跳过 metadata 清理。组织成“有资源就释放，无论如何都清状态”：

```cpp
void AllocatedBuffer::reset() noexcept
{
    if (buffer_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE)
    {
        // unmap and destroy
    }

    allocator_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    mappedData_ = nullptr;
    size_ = 0;
    usage_ = 0;
    requiredMemoryProperties_ = 0;
}
```

以实际成员名为准，不要机械照抄示例。

### 6.4 本步验收

- 所有构造位置都传入真实 metadata；
- move 后源对象的 handle 和 metadata 都为空；
- 重复 `reset()` 不崩溃；
- 编译、测试和画面均无变化。

建议 commit：

```text
Record GPU resource metadata
```

---

## 7. 第 2 步：重命名为 `GpuBuffer` 与 `GpuImage`

`AllocatedBuffer` 描述“通过 VMA 分配过”，`GpuBuffer` 更直接表达“拥有 GPU buffer 的资源对象”。重命名应是机械改动，不改变行为。

操作顺序：

1. 在 `VulkanResources.hpp/.cpp` 重命名类、构造/析构和方法限定名；
2. 修改 `VulkanContext` 返回类型；
3. 修改 `Swapchain`、`Renderer`、`TriangleApplication`、IBL、Skybox 的成员与局部变量；
4. 更新测试中的 `static_assert`；
5. 搜索旧名称并编译 Debug/Release。

```powershell
rg "AllocatedBuffer|AllocatedImage" src tests docs
```

迁移中可以短暂建立 type alias，但同一个或下一个 commit 就删除。长期同时存在两个名称会让人误以为它们是不同所有权类型。

建议 commit：

```text
Rename allocated resources to GPU resources
```

---

## 8. 第 3～4 步：用 description struct 统一创建接口

### 8.1 定义 descriptions

可以先放在 `VulkanResources.hpp`，以后文件确实过大时再拆。

```cpp
struct BufferDesc
{
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    VkMemoryPropertyFlags requiredMemoryProperties = 0;
    const char *debugName = nullptr;
};

struct ImageDesc
{
    VkExtent3D extent{0, 0, 1};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags usage = 0;
    VkMemoryPropertyFlags requiredMemoryProperties =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkImageCreateFlags flags = 0;
    const char *debugName = nullptr;
};
```

这个版本沿用项目当前的 memory property 抽象，改动最小。以后若要完整暴露 VMA allocation policy，再单独演进。

### 8.2 新创建接口

```cpp
GpuBuffer createBuffer(const BufferDesc &desc) const;
GpuImage createImage(const ImageDesc &desc) const;
```

创建入口至少验证：

- allocator 已初始化；
- buffer size 和 usage 非零；
- image 的 extent 分量、mipLevels 和 arrayLayers 非零；
- format 不是 `VK_FORMAT_UNDEFINED`；
- image usage 非零。

创建成功后，若 `debugName` 非空且 debug utils 可用，调用已有 `setDebugName()`。不要把 `const char *` 原样存进资源对象，因为调用者字符串可能先结束生命周期。

### 8.3 逐模块迁移

先保留旧接口，让旧接口构造 desc 并转发到新接口。然后按以下顺序迁移：

1. `Buffers.cpp`；
2. `ImageResources.cpp`；
3. `Skybox.cpp`；
4. `IBL.cpp`；
5. `Swapchain.cpp`；
6. 其他搜索结果。

每迁移一个模块就编译一次，全部完成后删除旧重载，让编译器找出遗漏。

C++17 没有标准 designated initializer，调用处建议逐字段赋值：

```cpp
BufferDesc desc{};
desc.size = bufferSize;
desc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
desc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
desc.debugName = "scene vertex buffer";

GpuBuffer vertexBuffer = context.createBuffer(desc);
```

本步完成后检查：

```powershell
rg "createBuffer\(" src
rg "createImage\(" src
rg "setDebugName" src
```

旧长参数重载应删除；metadata 与 desc 一致；关键资源在调试器中有名称；swapchain 重建仍正常。

建议 commit：

```text
Add description-based GPU resource creation
Migrate GPU resource creation to descriptions
```

---

## 9. 立即释放与延迟释放

Stage 3 不要把“延迟释放”塞进 `GpuBuffer` 自己。

### 9.1 立即释放

`GpuBuffer::reset()` 和 `GpuImage::reset()` 表示调用者已经保证 GPU 不再使用资源，可以现在释放。

典型场景：

- 初始化阶段的同步上传已等待 fence；
- 程序退出前已 `vkDeviceWaitIdle()`；
- 创建中途失败，资源还没有提交给 GPU 使用。

### 9.2 延迟释放

`Renderer::retireBuffer(GpuBuffer &&)` 表示把所有权交给掌握 frame fence 的对象，由它在安全时刻销毁。这比 `buffer.destroyLater(renderer)` 更清晰，因为资源类型不需要知道 Renderer、当前帧或 fence。

只有出现“运行时替换 image，而旧 image 仍可能被已提交帧使用”的真实调用点时，再增加 `retireImage()`。不要只为接口对称制造未验证路径。

### 9.3 `DeletionQueue` 仍可存在

主 `DeletionQueue` 可以暂时管理尚未包装的 sampler、descriptor pool、IBL framebuffer 等对象。每当一个对象获得清楚的 RAII owner，就删除对应 callback。

目标是逐项减少队列职责，不是先删队列再寻找资源放在哪里。

---

## 10. 第 5 步：分离 record 与 submit

统一上传前，先让底层 helper 只接受一个已有的 `VkCommandBuffer`。

### 10.1 建议的低层接口

```cpp
void recordBufferCopy(
    VkCommandBuffer commandBuffer,
    VkBuffer source,
    VkBuffer destination,
    VkDeviceSize size);

void recordImageTransition(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels,
    uint32_t layerCount);

void recordBufferToImageCopy(
    VkCommandBuffer commandBuffer,
    VkBuffer source,
    VkImage destination,
    VkExtent3D extent,
    uint32_t layerCount);

void recordGenerateMipmaps(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkFormat format,
    VkExtent2D baseExtent,
    uint32_t mipLevels,
    uint32_t layerCount);
```

它们可以先做 `Renderer` 私有函数，也可以放进单独 `UploadCommands` 文件。判断标准是：函数不依赖 `TriangleApplication` 状态。

### 10.2 高层只提交一次

```cpp
renderer.immediateSubmit([&](VkCommandBuffer commandBuffer)
{
    recordImageTransition(commandBuffer, /* ... */);
    recordBufferToImageCopy(commandBuffer, /* ... */);
    recordGenerateMipmaps(commandBuffer, /* ... */);
});
```

三组命令按顺序进入同一 command buffer，最后只有一次 `vkQueueSubmit()` 和一次 fence 等待。

不要在 callback 里调用内部又执行 `immediateSubmit()` 的旧 helper。这会形成嵌套提交，甚至在同一个 upload fence 上等待自己。`record` 前缀就是在提醒调用者：函数只记录，不提交。

本步搜索：

```powershell
rg "immediateSubmit" src
rg "record(Buffer|Image|Generate)" src
```

逐个解释每个 `immediateSubmit()` 是否代表一整个上传事务。

建议 commit：

```text
Separate upload recording from submission
```

---

## 11. 第 6 步：批量上传 vertex/index buffer

先实现同步 batch，不直接设计跨帧异步 `UploadManager`。

### 11.1 请求类型

```cpp
struct BufferUploadRequest
{
    const void *data = nullptr;
    VkDeviceSize size = 0;
    VkBufferUsageFlags destinationUsage = 0;
    const char *debugName = nullptr;
};
```

明确约定：`data` 只需在同步调用返回前有效，接口不会保存该指针。

### 11.2 同步接口

```cpp
std::vector<GpuBuffer> uploadBuffers(
    const std::vector<BufferUploadRequest> &requests);
```

它可以先属于 `Renderer`，因为当前 Renderer 已拥有 upload context，并能通过 `VulkanContext` 创建资源。以后上传系统真正需要独立时再移动。

### 11.3 实现顺序

1. 检查 requests 非空；
2. 验证每个请求的 data、size 和 destinationUsage；
3. 为每个请求创建 host-visible staging buffer；
4. map 并 `memcpy`；
5. 创建 device-local destination buffer，usage 自动追加 `VK_BUFFER_USAGE_TRANSFER_DST_BIT`；
6. 调用一次 `immediateSubmit()`；
7. callback 遍历请求，记录每组 `vkCmdCopyBuffer()`；
8. `immediateSubmit()` 返回后 fence 已完成；
9. staging vector 离开作用域并安全销毁；
10. 返回 destination vector。

局部容器直接持有 `GpuBuffer`，不要用裸 `VkBuffer` 表达所有权。

### 11.4 异常安全推演

若创建第二个 destination 时失败：

- staging vector 自动释放已有 staging；
- destination vector 自动释放已创建 destination；
- 尚未提交 GPU 命令，因此可以立即释放；
- 调用者原有成员尚未被覆盖。

初始化期间可在成功返回后移动到成员。若运行时替换正在使用的旧 buffer，应先将旧对象交给 `retireBuffer()`。

确认 `createObjectBuffers()` 使用 batch 后，搜索旧 `createVertexBuffer()`、`createIndexBuffer()` 和单独 `copyBuffer()`。无调用就删除，不保留两套上传实现。

### 11.5 本步验收

- vertex/index 只有一次 `immediateSubmit()`；
- staging 在 fence 完成前没有析构；
- 顶点和索引画面不变；
- 空 data 或 size 为 0 会给出明确错误；
- Validation Layer 无 usage、copy range 或生命周期错误。

建议 commit：

```text
Batch synchronous buffer uploads
```

---

## 12. 第 7 步：合并 texture 上传事务

texture 比 buffer 多一个难点：layout 是命令顺序的一部分。

### 12.1 典型事务

在一次 `immediateSubmit()` 中依次记录：

1. `VK_IMAGE_LAYOUT_UNDEFINED` → `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`；
2. staging buffer → mip 0 image copy；
3. 每层 mip 的 blit 与 barrier；
4. 所有 mip 最终进入 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`。

同一个 queue 和 command buffer 会按记录顺序执行，但 barrier 仍必须正确；不能因为只提交一次就删除 barrier。

### 12.2 迁移顺序

先改普通 2D texture，再改 skybox cubemap，最后处理 IBL 中真正属于同类上传的部分。cubemap 的 layer count、subresource range 和 view type 不同，不能偷偷使用 2D 默认值。

上传代码应优先从 `GpuImage` 获取 `format()`、`extent()`、`mipLevels()` 和 `arrayLayers()`，避免调用者平行保存一套可能失真的参数。

生成 mipmap 前继续用 `vkGetPhysicalDeviceFormatProperties()` 检查 format 是否支持 linear filtering blit。统一 helper 不能删掉这项验证。

### 12.3 本步验收

- 一张普通 texture 初始化只有一次上传 submit；
- mip 内容正确；
- cubemap 六个 face 的方向和内容正确；
- Validation Layer 无 oldLayout、stage/access mask 或 subresource range 错误；
- staging 资源在 fence 完成后才释放。

建议 commit：

```text
Consolidate texture upload submissions
```

---

## 13. 第 8 步：统一 sampler 创建和所有权

### 13.1 `SamplerDesc`

```cpp
struct SamplerDesc
{
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float mipLodBias = 0.0F;
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0F;
    bool compareEnable = false;
    VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS;
    float minLod = 0.0F;
    float maxLod = 0.0F;
    VkBorderColor borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    bool unnormalizedCoordinates = false;
    const char *debugName = nullptr;
};
```

它覆盖当前常见差异，又让 Vulkan 行为保持可见。

### 13.2 专用 RAII 类型

```cpp
class UniqueSampler
{
public:
    UniqueSampler() = default;
    UniqueSampler(VkDevice device, VkSampler sampler) noexcept;
    ~UniqueSampler();

    UniqueSampler(const UniqueSampler &) = delete;
    UniqueSampler &operator=(const UniqueSampler &) = delete;
    UniqueSampler(UniqueSampler &&other) noexcept;
    UniqueSampler &operator=(UniqueSampler &&other) noexcept;

    VkSampler get() const noexcept { return sampler_; }
    void reset() noexcept;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};
```

由 `VulkanContext` 提供：

```cpp
UniqueSampler createSampler(const SamplerDesc &desc) const;
```

descriptor 写入时使用 `sampler.get()`。迁移一个 sampler 后立即删除它原来注册到 `mainDeletionQueue` 的销毁 callback，避免双重销毁。

若启用 anisotropy，确认 device feature 已启用，并对 `maxSamplerAnisotropy` 做限制或明确报错；不要在 helper 内为所有 sampler 静默开启。

迁移顺序建议为普通 texture、skybox、irradiance、prefilter、BRDF LUT：

```powershell
rg "VkSamplerCreateInfo|vkCreateSampler|vkDestroySampler" src
```

最终创建/销毁应集中在资源层实现中。

建议 commit：

```text
Add owned sampler resources
```

---

## 14. 第 9 步：提取 IBL 的窄 helper

### 14.1 单颜色附件 render pass

不要写固定 dependency 的无参数函数，因为 irradiance/prefilter 与 BRDF LUT 当前 dependency 不完全相同。

```cpp
struct SingleColorRenderPassDesc
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDependency dependency{};
};

VkRenderPass createSingleColorRenderPass(
    VkDevice device,
    const SingleColorRenderPassDesc &desc);
```

helper 只统一创建。若返回裸 handle，调用者仍须立刻交给明确 owner 或 cleanup 路径。

### 14.2 单附件 framebuffer

```cpp
VkFramebuffer createSingleAttachmentFramebuffer(
    VkDevice device,
    VkRenderPass renderPass,
    VkImageView attachment,
    VkExtent2D extent,
    uint32_t layers = 1);
```

不要在此 helper 内创建 image view。face/mip view 的 subresource 范围属于具体 IBL 算法。

### 14.3 无顶点输入 pipeline

先并排比较三段 pipeline，只把完全相同的 vertex input、input assembly、rasterization、multisample 和 dynamic state 初始化提成小函数。

不要直接复用 `Renderer` 主 pipeline 的私有配置：主 pass 的 descriptor layout、depth、blend、push constant 与 IBL 离屏 pass 的假设不同。

如果确有稳定共同接口，可考虑：

```cpp
struct OffscreenPipelineDesc
{
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    bool depthTestEnable = false;
};
```

只有它准确覆盖多个调用点且没有大量布尔开关时才保留，否则只提取更小的 state helper。

### 14.4 生命周期与验收

继续用 `UniqueShaderModule` 将 shader module 生命周期限制在 pipeline 创建期间；shader module 不需要与 pipeline 同寿命，也不进入主 `DeletionQueue`。

本阶段不迁移整个 IBL 模块所有权，只消除创建样板。验证：

```powershell
rg "vkCreateRenderPass|vkCreateFramebuffer|vkCreateGraphicsPipelines" src/IBL.cpp
```

结果不必为零；剩余代码应只表达各 pass 真正不同的参数与算法。运行确认 irradiance、prefilter、BRDF LUT 和主场景外观均与基线一致。

建议拆成两个 commit：

```text
Share single-color render target helpers
Reduce duplicated IBL pipeline setup
```

---

## 15. P2：image layout 调试状态

这是调试辅助，不是同步真相来源，应放在 P0/P1 完成后。

`GpuImage` 可以增加仅用于 debug build 的 layout 字段。中央 transition recording helper 在记录转换时检查调用者传入的 `oldLayout`，并更新调试值。

但 CPU 字段不知道 GPU 实际执行到哪里，也不能完整表达：

- 不同 mip/layer 各自的 layout；
- render pass implicit transition；
- 多 queue 并发；
- 已记录但尚未提交的命令；
- 已提交但尚未执行的命令。

因此它不能替代 barrier。只有所有显式 transition 已走中央入口、且暂时只跟踪整个 image 时才启用；无法准确跟踪的 implicit transition 应标记 unknown 或跳过检查，不能伪造确定状态。

建议 commit：

```text
Track image layouts for debug validation
```

---

## 16. 测试与故障注入

### 16.1 编译期测试

继续保留并更新类型名：

```cpp
static_assert(!std::is_copy_constructible_v<GpuBuffer>);
static_assert(!std::is_copy_assignable_v<GpuBuffer>);
static_assert(std::is_nothrow_move_constructible_v<GpuBuffer>);
static_assert(std::is_nothrow_move_assignable_v<GpuBuffer>);
```

对 `GpuImage`、`UniqueSampler` 和其他新增 owner 做相同检查。

### 16.2 不连接真实 Vulkan 的测试

适合测试：

- 默认对象 handle 为空；
- 默认 metadata 是约定空值；
- description validation 对 0 size、空 usage、undefined format 报错；
- `SamplerDesc` 默认值符合项目预期。

不要为了测试 move 而把伪造的非空 Vulkan/VMA handle 塞进对象，析构会调用真实销毁函数。没有合适依赖注入时，完整 move/析构行为交给真实 Vulkan 集成运行和 Validation Layer 覆盖。

### 16.3 故障注入点

逐个临时加入异常，验证后撤销：

1. 第一个 staging buffer 创建后；
2. 全部 staging 创建后、destination 创建前；
3. 第一个 destination 创建后；
4. texture image 创建后、view 创建前；
5. sampler 创建后、descriptor 更新前；
6. IBL render pass 创建后、framebuffer 创建中途；
7. swapchain 重建的新 image 创建后。

每次检查：

- 没有 double destroy；
- 没有 allocator 销毁后再销毁资源；
- 没有遗留 mapped allocation；
- 已提交资源不会提前析构；
- 失败后应用能按当前异常策略退出或恢复。

### 16.4 验证上传次数

学习阶段可以临时给 `immediateSubmit()` 加 debug counter 或日志，确认：

- mesh vertex/index 从两次提交降为一次；
- 单张普通 texture 的 transition/copy/mipmap 合为一次；
- 真正需要独立完成边界的事务仍保持独立。

不要只看 `vkQueueSubmit` 总次数，因为正常 `endFrame()` 每帧本来就需要渲染提交。

---

## 17. 每步通用验证流程

每个 commit 前执行：

```powershell
git diff --check
cmake --build --preset windows-mingw-debug --parallel
ctest --test-dir build/windows-mingw-debug --output-on-failure
```

关键里程碑再执行：

```powershell
cmake --build --preset windows-mingw-release --parallel
```

至少在以下步骤后手动运行：

- metadata + rename；
- desc API 全部迁移；
- buffer batch；
- texture batch；
- sampler 迁移；
- IBL helper。

常用搜索：

```powershell
rg "AllocatedBuffer|AllocatedImage" src tests
rg "createBuffer\(|createImage\(" src
rg "immediateSubmit" src
rg "VkSamplerCreateInfo|vkCreateSampler|vkDestroySampler" src
rg "vkCreateRenderPass|vkCreateFramebuffer" src/IBL.cpp
rg "mainDeletionQueue" src
```

不是所有搜索结果都必须归零，但你应能逐条解释剩余结果为什么合理。

---

## 18. Stage 3 完成标准

### P0：必须完成

- [ ] 当前 Stage 2 运行基线已记录；
- [ ] `GpuBuffer` 记录 size、usage 和明确命名的内存请求信息；
- [ ] `GpuImage` 记录 extent、format、usage、mips、layers 和 samples；
- [ ] 两种资源不可复制、可无异常移动、可幂等 reset；
- [ ] move 和 reset 同步处理全部 metadata；
- [ ] 创建接口主要使用 `BufferDesc` / `ImageDesc`；
- [ ] 旧长参数创建重载已删除；
- [ ] vertex/index 使用一次同步 batch；
- [ ] texture 的 transition/copy/mipmap 可在一次上传事务中记录；
- [ ] staging 资源活到 fence 完成；
- [ ] Debug 构建和测试通过；
- [ ] Validation Layer 无新增错误。

### P1：本阶段建议完成

- [ ] sampler 使用统一 description；
- [ ] sampler 有明确 RAII owner；
- [ ] 已迁移 sampler 不再同时注册 DeletionQueue callback；
- [ ] IBL 共享单颜色 render pass helper；
- [ ] IBL 共享单附件 framebuffer helper；
- [ ] IBL pipeline 只提取真正相同的 state；
- [ ] shader module 生命周期仍限制在 pipeline 创建期间；
- [ ] Release 构建和 resize/minimize/restore 测试通过；
- [ ] 关键失败点经过至少一轮故障注入。

### P2：可延后

- [ ] image layout debug state；
- [ ] 辅助 image view 的专用 move-only wrapper；
- [ ] 更通用的同步 `UploadBatch` 对象；
- [ ] 更完整的资源 debug name 覆盖；
- [ ] 上传统计或性能标记。

以下项目明确不作为 Stage 3 完成条件：删除全部 `DeletionQueue`、把所有 handle 包装成同一个模板、后台异步上传、dedicated transfer queue，以及 Scene/Material/IBL 的最终模块拆分。

---

## 19. 你现在应从哪里开始

下一次实际手敲只完成以下一小步：

1. 在 `AllocatedBuffer` 增加 size、usage 和内存请求 metadata；
2. 在 `AllocatedImage` 增加 extent、format、usage、arrayLayers 和 samples；
3. 修改构造、move 和 `reset()`；
4. 修改 `VulkanContext::createBuffer/createImage()` 的返回构造；
5. 编译并运行；暂时不重命名，也不改上传。

完成并验证后再进入第 7 节重命名。这样如果出现问题，范围只在资源状态转移，不会与几十个名称替换或上传同步问题混合。

完成这一步后，你应该能回答：

- 为什么 metadata 必须跟随 move；
- 为什么 `reset()` 在 handle 已空时仍可能需要清 metadata；
- 创建请求的 memory properties 与 VMA 最终选择的 memory type 有什么区别；
- 为什么 debug name 指针不应默认存进资源对象；
- 为什么资源包装层不应自己决定延迟到哪一帧释放。

能清楚回答这些问题，再继续下一 commit。
