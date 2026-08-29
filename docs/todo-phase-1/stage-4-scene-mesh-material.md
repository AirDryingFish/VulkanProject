# 阶段 4：Scene、Mesh 与 Material 解耦实施指南

本文对应[开发路线图中的阶段 4](overview.md#阶段-4scenemesh-与-material-解耦)。评估基线为
2026-08-21 的 `ba561eb Clarify dynamic viewport state`。

阶段 1～3 已经建立资源生命周期、应用模块边界和通用 GPU 资源层。阶段 4 不继续抽象
Vulkan 创建函数，而是解决场景数据模型仍然混在 `TriangleApplication` 中的问题：目前每个
`SceneObject` 直接拥有 vertex/index buffer，所有对象共享一套全局 PBR 纹理和材质参数，
Skybox 还使用包含大量无关 binding 的主场景 descriptor layout。

当前处于初版开发阶段，自动化测试和故障注入按开发者选择暂时延期。本阶段仍要求每个提交
能够通过 Debug 构建，并至少手动启动一次；关键 descriptor/shader 提交还要进行 Release 构建
和 Validation Layer 检查。

---

## 1. 当前工程进度评估

### 1.1 构建与版本基线

当前工作树在评估时保持干净，并已确认：

- `linux-debug` 构建通过；
- `linux-release` 构建通过；
- 当前没有启用的 CTest 测试目标，`ctest` 会报告 `No tests were found`；
- 自动测试、故障注入、完整窗口压力测试暂不阻塞初版功能开发。

这意味着当前适合开始 Stage 4 的生产代码演进，但不能把“编译成功”等同于 descriptor
绑定、资源释放和画面结果已经全部验证。

### 1.2 Stage 1～3 的实际状态

| 阶段 | 当前判断 | 已具备的关键能力 | 延期或仍需人工确认 |
|---|---|---|---|
| Stage 1 资源生命周期 | 核心实现已完成 | move-only RAII、幂等 cleanup、DeletionQueue、帧后延迟 Buffer 销毁 | 自动测试与故障注入 |
| Stage 2 应用拆分 | 核心实现已完成 | `VulkanContext`、`Swapchain`、`Renderer` 已形成明确模块 | 更细的 Scene/Material 拆分本来就属于后续阶段 |
| Stage 3 GPU 资源层 | 生产代码已完成 | `GpuBuffer`/`GpuImage` metadata、Desc API、批量 Buffer 上传、单次纹理事务、Sampler RAII、IBL helper | image layout debug state、自动测试和完整验收 |
| Stage 4 Scene 资源模型 | 尚未开始 | 已有多对象、Transform 编辑、拾取和统一 CPU Mesh 构建路径作为基础 | Mesh 共享、Material、descriptor 分组尚未建立 |

Stage 3 当前不需要继续扩展 `immediateSubmit()` 或万能 pipeline builder。Stage 4 应直接使用
现有资源层，不重新设计它。

### 1.3 已经可以复用的 Scene 基础

当前工程并非没有场景系统，以下代码可以保留并迁移：

- `sceneObjects` 已支持多个 Cube、Sphere 和 OBJ 对象；
- `MeshBuildData` 已统一表达 CPU 顶点、索引和 local-space bounds；
- Cube、Sphere、OBJ 最终都会走 `createObjectBuffers()`；
- vertex/index 已通过 `Renderer::uploadBuffers()` 在一次同步事务中上传；
- 每个对象已有 position、rotation、scale、自动旋转状态；
- picking 已使用对象 local bounds 计算 world AABB；
- `RenderObjectView` 已把应用对象转换成 Renderer 只读的裸 Vulkan handle 视图；
- 删除对象时会把 Buffer 移入对应 `FrameContext` 的退休队列，等待 fence 后再析构。

因此 Stage 4 的重点是重新组织所有权和引用关系，不是重写 OBJ loader、拾取算法或上传命令。

### 1.4 当前 Mesh 耦合

`SceneObject` 目前同时包含：

- 名称和资产来源；
- vertex/index GPU Buffer；
- vertex/index count；
- local bounds；
- Transform；
- 自动旋转状态。

直接结果是：

1. 创建两个 Cube 会重复生成并上传两套完全相同的 Buffer；
2. 删除对象必须手工、成对调用 `destroyBufferDeferred()`；
3. picking、ImGui 和 render view 都从 `SceneObject` 读取 Mesh 细节；
4. `SceneObject` 不能只表达“场景中的一个实例”；
5. 后续 glTF 中一个 Mesh 被多个 Node 引用时没有自然映射。

当前还保留了一组已经不参与主流程的旧状态：

- `TriangleApplication::vertices` / `indices`；
- 顶层 `vertexBuffer` / `indexBuffer`；
- `modelLocalBoundsMin` / `modelLocalBoundsMax` / `modelBoundsValid`；
- `loadModel()`、`computeModelBounds()`、`rebuildMesh()`；
- `Buffers.cpp` 中旧的两次上传实现注释。

这些旧状态会干扰 Mesh 迁移，应该在 Stage 4 的第一个清理提交中删除，而不是继续兼容。

### 1.5 当前 Material 耦合

现在只有一套全局材质：

- `textureImage`、`normalImage`、`metallicImage`、`roughnessImage`、`aoImage`；
- `materialAlbedo`、`materialMetallic`、`materialRoughness`、`materialAo`；
- 所有 `SceneObject` 使用同一个 `descriptorSets[frameIndex]`；
- 材质 factor 被放在每帧 `UniformBufferObject` 中。

因此两个对象即使拥有不同 Mesh，也不能选择不同纹理或 factor。修改 ImGui 材质参数会同时
影响整个场景。

### 1.6 当前 Descriptor 耦合

主 descriptor set layout 目前包含：

```text
binding 0  UniformBufferObject
binding 1  Albedo
binding 2  Normal
binding 3  Metallic
binding 4  Roughness
binding 5  AO
binding 6  Environment cubemap
binding 7  Irradiance cubemap
binding 8  Prefilter cubemap
binding 9  BRDF LUT
```

同一个 layout 同时给主 PBR pipeline 和 Skybox pipeline 使用。Skybox 实际只需要 frame
camera 数据和一个 cubemap，却仍然填写 normal、metallic、roughness、AO、IBL 等无关
descriptor。

这也是 Stage 4 必须按顺序推进的原因：Material 不只是一个 C++ struct，它最终会改变
shader set/binding、pipeline layout、descriptor pool、`RenderObjectView` 和 draw loop。

### 1.7 阶段判断

当前工程已经具备进入 Stage 4 的前置条件。推荐推进顺序是：

```text
清理旧 Mesh 状态
    ↓
建立 Mesh 类型（先不共享）
    ↓
建立 Transform / SceneObject 类型
    ↓
引入共享 Mesh 引用和安全释放
    ↓
建立 Texture / Material CPU 模型
    ↓
先让 Skybox descriptor 独立
    ↓
拆 Frame / Material descriptor
    ↓
让不同对象选择不同 Material
```

不要一开始同时引入 `Mesh`、`Material`、新 shader binding 和 descriptor pool。否则黑屏时很难
判断是 C++ 所有权、pipeline layout 还是 GLSL binding 出错。

---

## 2. Stage 4 的目标与边界

### 2.1 本阶段目标

完成后应满足：

1. `Mesh` 独占 vertex/index Buffer，并记录 count 与 local bounds；
2. `SceneObject` 只保存高层实例数据，不直接拥有 Vulkan Buffer；
3. 两个 `SceneObject` 可以共享同一个 Mesh；
4. `Transform` 成为独立数据结构；
5. `Material` 表达 PBR texture slot 和 factor；
6. 两个对象可以共享 Mesh、使用不同 Material；
7. Frame descriptor 保存 camera、lights 和全局 IBL；
8. Material descriptor 只保存材质纹理；
9. Skybox 使用自己的 descriptor layout，不再填写无关 PBR descriptor；
10. 替换 Material 不重新创建 Mesh Buffer。

### 2.2 本阶段不做什么

- 不接入 glTF；
- 不支持一个 Mesh 内多个 primitive/submesh；
- 不加入 tangent attribute；
- 不建立 Scene Node 父子层级；
- 不做 bindless descriptor；
- 不做后台资产线程和异步 transfer queue；
- 不实现通用 ECS；
- 不实现材质热重载；
- 不实现 alpha mask、alpha blend 和 double-sided pipeline variant；
- 不为了共享资源引入万能句柄模板或完整 Asset Manager；
- 不在本阶段强制补自动化测试。

这些内容属于 Stage 5 或更后的架构演进。Stage 4 只建立足以承载它们的资源关系。

---

## 3. 必须一直成立的规则

### 3.1 Mesh 所有权

- 一个 `GpuBuffer` 只能由一个 `Mesh` 拥有；
- `SceneObject` 只能引用 Mesh，不能复制 Buffer；
- 运行时最后一个 Mesh 引用释放前，Buffer 必须进入 frame retirement 路径；
- 应用关闭时已经先 `waitIdle()`，此时可以直接销毁剩余 Mesh；
- `RenderObjectView` 中的 `VkBuffer` 只是借用，不拥有资源；
- Mesh 的 local bounds 与 Mesh 一起共享，不能在每个对象中复制维护。

### 3.2 Material 与 Texture 所有权

- `Material` 可以共享 Texture；
- descriptor set 不能比它引用的 image view 和 sampler 活得更久；
- 当前阶段 Texture library 保持资源到应用关闭，避免运行时纹理回收问题与 descriptor 拆分混在一起；
- Material factor 是 CPU 数据，每次 draw 通过 push constant 传入；
- 已被 pending command buffer 使用的 descriptor set 不应原地更新；更换纹理时优先创建新 Material/descriptor set。

### 3.3 Descriptor 规则

- PBR pipeline 的 set 0 固定表示 Frame/Scene 全局数据；
- PBR pipeline 的 set 1 固定表示 Material；
- Skybox 使用自己独立的 set 0，其中只包含 Frame UBO 和 skybox cubemap；
- C++ binding、GLSL `set`/`binding` 和 pipeline layout 顺序必须在同一个提交中同步修改；
- descriptor set layout 不兼容时必须重新绑定，不能依赖上一条 pipeline 遗留的绑定状态；
- shader 修改后必须重新生成 `.spv`。

### 3.4 提交边界

每个提交只解决一种问题：

- 数据结构迁移不改 shader；
- Mesh 共享不改 Material；
- Skybox descriptor 独立不改 PBR shader；
- Frame/Material descriptor 拆分才同时修改 CPU layout 和 PBR shader；
- UI 多材质选择放在 GPU 绑定已经稳定之后。

---

## 4. 目标结构

本阶段不需要独立的 `Scene` 大类就能先建立正确关系：

```text
TriangleApplication
├── Mesh cache (weak references)
├── Texture library (strong references, app lifetime)
├── Material library (shared Material handles)
└── sceneObjects
    └── SceneObject
        ├── name / selection-related data
        ├── Transform
        ├── shared Mesh handle
        └── shared Material handle

Mesh
├── GpuBuffer vertexBuffer
├── GpuBuffer indexBuffer
├── vertexCount / indexCount
└── local bounds

Material
├── texture handles
├── PBR factors
└── VkDescriptorSet (pool-owned borrowed handle)
```

Renderer 仍然只接收每帧快照：

```text
SceneObject + Mesh + Material
            ↓ application builds a view
RenderObjectView
├── borrowed VkBuffer handles
├── borrowed material descriptor set
└── per-draw push constants
            ↓
Renderer::recordFrame()
```

Renderer 不需要拥有 `SceneObject`、`Mesh` 或 `Material`。

---

## 5. 推荐提交顺序

| 顺序 | 建议提交 | 主要学习点 |
|---|---|---|
| 0 | `Remove legacy single-mesh state` | 先删除旧路径，减少迁移噪声 |
| 1 | `Introduce owned Mesh resources` | 组合资源与实例数据，但暂不共享 |
| 2 | `Extract scene object and transform types` | 高层场景数据边界 |
| 3 | `Share meshes between scene objects` | shared ownership 与 GPU 延迟释放 |
| 4 | `Introduce texture and material resources` | CPU 资源模型与默认材质 |
| 5 | `Give skybox an independent descriptor layout` | pipeline layout 与 descriptor 兼容性 |
| 6 | `Split frame and material descriptors` | set 分组、push constant、shader 接口 |
| 7 | `Support per-object material selection` | 证明 Mesh 与 Material 真正解耦 |
| 8 | `Finish Stage 4 manual validation` | 初版阶段的手动验收记录 |

---

## 6. 第 0 步：删除旧的单 Mesh 状态

先通过搜索确认这些名称没有参与当前初始化主路径：

```bash
rg "loadModel\(|computeModelBounds\(|rebuildMesh\(" src
rg "modelLocalBounds|modelBoundsValid|\bvertices\b|\bindices\b" src
```

删除：

- `TriangleApplication::loadModel()`；
- `TriangleApplication::computeModelBounds()`；
- 未使用的 `rebuildMesh()`；
- 顶层 `vertices`、`indices`、`vertexBuffer`、`indexBuffer`；
- 顶层 `modelLocalBoundsMin`、`modelLocalBoundsMax`、`modelBoundsValid`；
- cleanup 中对应的旧 Buffer reset；
- `Buffers.cpp` 中已经被 `uploadBuffers()` 替代的大段注释代码。

不要删除 `MeshBuildData::vertices/indices`，它们仍是 CPU 构建和上传的输入。

验证：

```bash
git diff --check
cmake --build --preset linux-debug --parallel
```

建议提交：

```text
Remove legacy single-mesh state
```

---

## 7. 第 1 步：引入 Mesh，但暂时不共享

### 7.1 新增 `Mesh.hpp`

先把 `MeshSource`、`MeshBuildData` 和 GPU Mesh 放到业务类型头文件中：

```cpp
#pragma once

#include "VulkanResources.hpp"
#include "VulkanTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

enum class MeshSource
{
    Obj,
    Cube,
    Sphere,
};

struct MeshBuildData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    bool boundsValid = false;
};

struct Mesh
{
    GpuBuffer vertexBuffer;
    GpuBuffer indexBuffer;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    bool boundsValid = false;

    bool valid() const noexcept
    {
        return vertexBuffer &&
               indexBuffer &&
               indexCount > 0;
    }
};
```

`Mesh` 因为包含 move-only `GpuBuffer`，自然不可复制、可以移动，不需要再手写删除 copy。

### 7.2 让 SceneObject 先直接拥有 Mesh

第一步只做组合，不立即上 `shared_ptr`：

```cpp
struct SceneObject
{
    std::string name;
    MeshSource source = MeshSource::Obj;
    std::string sourcePath;

    Mesh mesh;

    // Transform 和动画字段暂时保持原样
};
```

将：

```cpp
void createObjectBuffers(
    SceneObject &object,
    const MeshBuildData &meshData);
```

改为更清晰的返回值接口：

```cpp
Mesh createMesh(const MeshBuildData &meshData);
```

`createMesh()` 内仍然调用一次 `renderer.uploadBuffers(requests)`，然后把返回 Buffer 和 metadata
写入局部 `Mesh`，最后移动返回。这样异常发生时局部资源自动清理，不会留下半初始化
`SceneObject`。

调用处变成：

```cpp
SceneObject object{};
object.mesh = createMesh(meshData);
```

draw、picking、ImGui 和删除路径分别改为读取：

```cpp
object.mesh.vertexBuffer
object.mesh.indexBuffer
object.mesh.indexCount
object.mesh.boundsMin
object.mesh.boundsMax
```

删除时仍保持原有延迟释放语义：

```cpp
destroyBufferDeferred(object.mesh.indexBuffer);
destroyBufferDeferred(object.mesh.vertexBuffer);
```

此提交完成后行为必须与迁移前完全一致，只是资源被归入 `Mesh`。

建议提交：

```text
Introduce owned Mesh resources
```

---

## 8. 第 2 步：提取 Transform 和 SceneObject

新增 `SceneTypes.hpp`：

```cpp
#pragma once

#include "Mesh.hpp"

#include <string>

struct Transform
{
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};
};

struct SceneObject
{
    std::string name;
    MeshSource source = MeshSource::Obj;
    std::string sourcePath;

    Mesh mesh;
    Transform transform;

    bool autoRotate = false;
    float autoRotation = 0.0f;
    float autoRotateSpeed = 90.0f;
};
```

将 `SceneObject` 从 `TriangleApplication` 私有嵌套类型移出后，`Model.cpp`、`Picking.cpp`、
`ImGuiLayer.cpp` 不再依赖 `TriangleApplication` 的私有类型定义。

把：

```cpp
object.position
object.rotation
object.scale
```

机械替换为：

```cpp
object.transform.position
object.transform.rotation
object.transform.scale
```

此提交不要同时引入 Material 或 shared ownership。

建议提交：

```text
Extract scene object and transform types
```

---

## 9. 第 3 步：让多个对象共享 Mesh

### 9.1 引入明确的 handle

在 `Mesh.hpp` 中增加：

```cpp
#include <memory>

using MeshHandle = std::shared_ptr<Mesh>;
```

将 `SceneObject::mesh` 改为：

```cpp
MeshHandle mesh;
```

使用 `shared_ptr` 的目的只是表达多个对象共享同一个 Mesh，不是让 Renderer 或 Vulkan API
接收 `shared_ptr`。

### 9.2 使用 weak cache

当前阶段可以在 `TriangleApplication` 中先放一个窄缓存：

```cpp
std::unordered_map<std::string, std::weak_ptr<Mesh>> meshCache;
```

key 必须稳定：

- Cube 使用 `builtin:cube`；
- Sphere 使用 `builtin:sphere`；
- OBJ 使用规范化后的资产路径，例如 `obj:/absolute/path/model.obj`。

获取接口可以是：

```cpp
MeshHandle getOrCreateMesh(
    MeshSource source,
    const std::string &path);
```

流程：

```text
计算 key
    ↓
cache 中 weak_ptr.lock() 成功？──是──> 直接共享现有 Mesh
    │
    否
    ↓
buildMeshData()
    ↓
createMesh()
    ↓
make_shared<Mesh>(move(mesh))
    ↓
写入 weak cache
```

cache 使用 `weak_ptr`，避免 cache 自己永久阻止最后一个 Mesh 引用释放。

### 9.3 最后一个引用的 GPU 安全释放

不能直接：

```cpp
object.mesh.reset();
```

因为最后一个 `shared_ptr` reset 会立即析构 `Mesh`，而前一帧可能仍在使用它的 Buffer。

当前范围内所有强引用都来自 `SceneObject`，因此统一建立：

```cpp
void TriangleApplication::releaseMesh(
    MeshHandle &mesh)
{
    if (!mesh)
    {
        return;
    }

    if (mesh.use_count() == 1)
    {
        destroyBufferDeferred(mesh->indexBuffer);
        destroyBufferDeferred(mesh->vertexBuffer);
    }

    mesh.reset();
}
```

规则：

- 删除一个共享 Mesh 的对象时，其他对象仍持有强引用，不退休 Buffer；
- 删除最后一个对象时，把两个 Buffer 移入 frame retirement；
- `Mesh` 随后只析构两个空 Buffer；
- cleanup 已经先 `waitIdle()`，可以直接清空 `sceneObjects`，不需要 frame retirement；
- 后续若增加新的强引用拥有者，必须重新审视 `use_count() == 1` 的约定。

增加一个“Duplicate Selected”或连续创建两个 Cube 的临时 UI 操作，确认两者的
`mesh.get()` 相同，但 Transform 独立。

建议提交：

```text
Share meshes between scene objects
```

---

## 10. 第 4 步：建立 Texture 与 Material 数据模型

### 10.1 TextureResource

本阶段 sampler 可以继续由应用统一拥有，Texture 先只拥有 image：

```cpp
struct TextureResource
{
    std::string name;
    GpuImage image;
};

using TextureHandle = std::shared_ptr<TextureResource>;
```

应用持有一个强引用库，确保 descriptor 引用的 image view 活到 descriptor pool 销毁之后：

```cpp
std::vector<TextureHandle> textureLibrary;
```

不要在此步骤实现运行时纹理卸载。

### 10.2 Material

当前项目继续保留 metallic 和 roughness 两张独立纹理，等 Stage 5 glTF 再处理组合纹理：

```cpp
struct Material
{
    std::string name;

    TextureHandle baseColorTexture;
    TextureHandle normalTexture;
    TextureHandle metallicTexture;
    TextureHandle roughnessTexture;
    TextureHandle aoTexture;
    TextureHandle emissiveTexture;

    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float aoFactor = 1.0f;
    glm::vec3 emissiveFactor{0.0f};

    // descriptor pool 拥有实际 descriptor set，此处只是借用 handle
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

using MaterialHandle = std::shared_ptr<Material>;
```

给 `SceneObject` 增加：

```cpp
MaterialHandle material;
```

### 10.3 缺失纹理

建立 1×1 默认纹理：

| Slot | 默认像素 | 含义 |
|---|---|---|
| Base color | `{255,255,255,255}` | 不改变 base color factor |
| Normal | `{128,128,255,255}` | 平坦法线 |
| Metallic | `{0,0,0,255}` | 默认非金属 |
| Roughness | `{255,255,255,255}` | 默认粗糙 |
| AO | `{255,255,255,255}` | 不削弱环境光 |
| Emissive | `{0,0,0,255}` | 默认不自发光 |

Base color 和 Emissive 使用 sRGB 格式，Normal/Metallic/Roughness/AO 使用线性格式。

先创建一个 `defaultMaterial`，引用当前 rusted-iron 五张纹理和默认黑色 Emissive。所有现有
对象都引用它。此提交只建立 CPU 模型，暂时仍可使用旧的全局 descriptor，保证数据结构迁移
和 GPU descriptor 迁移分开。

建议提交：

```text
Introduce texture and material resources
```

---

## 11. 第 5 步：先让 Skybox descriptor 独立

这是 descriptor 重构的低风险切入点，因为当前 Skybox shader 已经只使用：

```text
binding 0  frame UBO
binding 1  cubemap
```

### 11.1 Renderer 分离 layout 所有权

将当前共享成员逐步拆为：

```cpp
VkDescriptorSetLayout sceneDescriptorSetLayout_;
VkDescriptorSetLayout skyboxDescriptorSetLayout_;

VkPipelineLayout scenePipelineLayout_;
VkPipelineLayout skyboxPipelineLayout_;
```

Renderer 初始化顺序应明确为：

```text
render pass
descriptor set layouts
pipeline layouts
graphics pipelines
frame/upload contexts
```

不要再让 `createGraphicsPipelineFromConfig()` 在内部发现 layout 为空时偷偷创建共享
pipeline layout。`GraphicsPipelineConfig` 应显式接收：

```cpp
VkPipelineLayout layout = VK_NULL_HANDLE;
```

### 11.2 Skybox descriptor set

创建只有两个 binding 的 Skybox layout，并让 `createSkyboxDescriptorSets()` 只写 UBO 和
skybox cubemap。不要再填充 normal、metallic、AO、irradiance 等无关 image info。

主 PBR descriptor 在这个提交里保持原状。

验证重点：

- Skybox 正常显示；
- 主模型不变；
- Validation Layer 没有 layout compatibility 或未写 descriptor 错误；
- `rg "createSkyboxDescriptorSets" -n src/Descriptors.cpp` 中只剩两个 write。

建议提交：

```text
Give skybox an independent descriptor layout
```

---

## 12. 第 6 步：拆分 Frame 与 Material descriptor

这是 Stage 4 风险最高的提交。开始前必须先提交或暂存其他无关修改。

### 12.1 目标 set 结构

PBR pipeline：

```text
set 0: Frame
    binding 0  UniformBufferObject
    binding 1  Irradiance cubemap
    binding 2  Prefilter cubemap
    binding 3  BRDF LUT

set 1: Material
    binding 0  Base color
    binding 1  Normal
    binding 2  Metallic
    binding 3  Roughness
    binding 4  AO
    binding 5  Emissive
```

当前 fragment shader 中 `environmentMap` 只被未调用的辅助函数使用，实际 PBR 结果不依赖它。
Stage 4 可以从主 PBR set 删除该 binding；Skybox cubemap 留在 Skybox 自己的 set 中。

### 12.2 Frame UBO 不再保存 Material

从 `UniformBufferObject` 删除：

```cpp
model
materialAlbedo
materialParams
```

`model` 已由 push constant 提供。Frame UBO 保留：

```text
view / projection
camera
ambient light
light count / point lights
global render parameters（例如 IBL intensity）
```

`iblIntensity` 是场景级参数，不是材质参数，可以放入新的 `renderParams.x`。

### 12.3 使用 per-draw push constant 传 Material factor

在 CPU 与 GLSL 中保持完全相同的布局：

```cpp
struct DrawPushConstants
{
    glm::mat4 model{1.0f};

    alignas(16) glm::vec4 baseColorFactor{1.0f};

    // x metallic, y roughness, z AO, w reserved
    alignas(16) glm::vec4 materialFactors{1.0f};

    alignas(16) glm::vec4 emissiveFactor{0.0f};
};

static_assert(sizeof(DrawPushConstants) <= 128);
```

64 + 16 + 16 + 16 = 112 bytes，低于 Vulkan 保证支持的最小 128-byte push constant 空间。
仍应在物理设备能力中确认 `maxPushConstantsSize` 不小于该结构大小。

pipeline push constant range 改为 vertex + fragment：

```cpp
range.stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT |
    VK_SHADER_STAGE_FRAGMENT_BIT;
range.size = sizeof(DrawPushConstants);
```

### 12.4 修改 RenderObjectView

`RenderObjectView` 继续借用 Buffer，并增加当前 draw 的 Material：

```cpp
struct RenderObjectView
{
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE;
    DrawPushConstants pushConstants{};
};
```

应用层从 `SceneObject`、`Mesh` 和 `Material` 组装这个 view。Renderer 不读取 shared pointer。

### 12.5 修改 PBR shader

GLSL 必须显式写 set：

```glsl
layout(set = 0, binding = 0) uniform UniformBufferObject
{
    // Frame data
} frame;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

layout(set = 1, binding = 0) uniform sampler2D baseColorMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicMap;
layout(set = 1, binding = 3) uniform sampler2D roughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;
```

vertex 和 fragment shader 的 push constant block 字段顺序必须相同。Fragment 最终加入：

```glsl
vec3 emissive =
    texture(emissiveMap, fragTexCoord).rgb *
    draw.emissiveFactor.rgb;

vec3 color = ambient + ibl + Lo + emissive;
```

修改后执行：

```bash
./scripts/compile-shaders.sh
```

shader 源码和生成的 `.spv` 必须一起提交。

### 12.6 Renderer 绑定顺序

Skybox 使用自己的 pipeline layout 和 descriptor set。

主 PBR draw：

```text
bind PBR pipeline
bind frame descriptor at set 0（每帧一次）
for each object
    bind material descriptor at set 1
    push DrawPushConstants
    bind vertex/index buffers
    draw indexed
```

初版可以每个对象都绑定 Material。正确后再用 `lastMaterialSet` 跳过相邻对象的重复绑定。

### 12.7 Descriptor pool

Frame set 仍按 `MAX_FRAMES_IN_FLIGHT` 分配；Material set 不包含 UBO，因此每个 Material 只需
一个 descriptor set。初版可以给 Material pool 预留明确上限，例如 128 个 Material，并让
Material library 保持它们到应用关闭。

不要在 pending frame 中调用 `vkFreeDescriptorSets()`。运行时材质回收可以后续再做。

建议提交：

```text
Split frame and material descriptors
```

---

## 13. 第 7 步：证明每对象 Material 生效

只有数据结构存在还不算解耦完成。至少创建：

- 一个使用当前 rusted-iron texture 的 Material；
- 一个复用相同 Texture、但 base color/metallic/roughness factor 不同的 Material；
- 两个共享同一个 Sphere 或 Cube Mesh 的 SceneObject；
- 两个对象分别引用不同 Material。

ImGui 至少显示：

```text
Selected object
Mesh name/key
Mesh reference count（仅调试）
Material name
Base color factor
Metallic / Roughness / AO
Emissive factor
```

当用户调整 factor 时，只修改选中对象的 Material。如果多个对象共享同一个 Material，修改会
同时影响它们；若需要独立修改，应先复制 Material CPU 数据并分配新的 descriptor set。

推荐增加显式操作：

```text
Duplicate Object          共享 Mesh 和 Material
Make Material Instance    共享 Texture，但复制 Material factor/descriptor
```

这样可以清楚观察三层关系：

```text
Object instance 不同
Mesh 可以相同
Material 可以相同或不同
Texture 可以继续共享
```

建议提交：

```text
Support per-object material selection
```

---

## 14. 手动验证流程

自动测试目前延期，因此每个提交至少执行：

```bash
git diff --check
cmake --build --preset linux-debug --parallel
./build/linux-debug/vulkan
```

涉及 shader、descriptor 或 pipeline layout 的提交还执行：

```bash
./scripts/compile-shaders.sh
cmake --build --preset linux-release --parallel
```

手动检查：

1. 添加两个 Cube，确认它们共享 Mesh Buffer；
2. 移动、旋转、缩放其中一个，不影响另一个；
3. 两个对象选择不同 Material，画面确实不同；
4. 删除其中一个共享 Mesh 的对象，另一个继续正常渲染；
5. 删除最后一个引用，等待数帧后无 Validation 错误；
6. 连续添加和删除 Sphere/Cube/OBJ；
7. picking 和 gizmo 仍使用正确 bounds/Transform；
8. Skybox 正常显示且不再依赖 PBR descriptor；
9. resize、最小化、恢复后场景仍正常；
10. 正常退出没有 descriptor/image/buffer 生命周期错误。

常用搜索：

```bash
rg "object\.(vertexBuffer|indexBuffer|indexCount|localBounds)" src
rg "materialAlbedo|materialMetallic|materialRoughness|materialAo" src
rg "descriptorSetLayout|PipelineLayout" src/Renderer.* src/GraphicsPipeline.cpp
rg "layout\(.*set.*binding" assets/shaders
rg "destroyBufferDeferred" src
```

Stage 4 结束时，第一条搜索应只出现在迁移说明或不相关视图中；全局 Material factor 应被删除，
Buffer 延迟销毁应集中在 Mesh 释放路径。

---

## 15. 常见错误

### 15.1 直接 reset 最后一个 Mesh shared_ptr

结果：Buffer 可能在上一帧仍被 GPU 使用时立即析构。运行时删除必须经过统一 Mesh release
函数，并在最后一个引用时退休 Buffer。

### 15.2 cache 使用 shared_ptr

如果 cache 永久保存强引用，删除所有 SceneObject 后 Mesh 永远不会触发最后引用释放。当前
设计使用 `weak_ptr`，Texture library 才使用 app-lifetime 强引用。

### 15.3 只改 C++ binding，不改 GLSL

Descriptor set layout、pipeline layout 和 shader 接口是一个整体。任意一侧遗漏都会产生
Validation 错误或黑屏。

### 15.4 修改 shader 后忘记生成 SPIR-V

程序运行的是 `.spv`，不是 `.vert/.frag` 源文件。每次 shader 改动后运行编译脚本并一起提交。

### 15.5 在 UBO 和 push constant 中重复保留材质参数

迁移完成后 Material factor 只应来自 `DrawPushConstants`。同时保留两份会造成 UI 修改和 shader
读取来源不一致。

### 15.6 给 Skybox 继续填写完整 PBR descriptor

这会让“独立 Skybox layout”只停留在命名层面。Skybox descriptor set 应只包含它真正使用的
Frame UBO 和 cubemap。

### 15.7 过早做 Asset Manager

Stage 4 只需要窄的 Mesh weak cache、Texture/Material library。generation handle、异步加载、
热重载和磁盘缓存会掩盖当前真正要学习的引用与 descriptor 边界。

---

## 16. Stage 4 完成标准

> 2026-08-29 状态复核：以 `8bf1c83 Finish Stage 4 manual validation` 为基线，下面的 P0 生产代码、
> Debug/Release 构建和提交序列均已落地，已满足进入 Stage 5 的条件。仓库提交记录表明已做手动验收，
> 但本次只读复核没有独立观察运行画面和 Validation 输出；开始 Stage 5 前仍按
> [Stage 5 第 7.3 节](stage-5-gltf-asset-pipeline.md#73-验证-stage-4-基线)再跑一次本机启动检查。
> 下列未勾选项保留为可重复验收模板，不表示 P0 仍未实现。

### P0：初版必须完成

- [ ] 已删除旧单 Mesh 全局状态和死代码；
- [ ] `Mesh` 独占 vertex/index `GpuBuffer`；
- [ ] `Mesh` 记录 count 与 local bounds；
- [ ] `Transform` 和 `SceneObject` 已从 `TriangleApplication` 私有嵌套数据中抽离；
- [ ] `SceneObject` 不再直接拥有 Buffer；
- [ ] 两个 SceneObject 可以共享 Mesh；
- [ ] 最后一个 Mesh 引用运行时释放会经过 frame retirement；
- [ ] Material 已表达纹理 slot 与 PBR factor；
- [ ] 每个 SceneObject 可以选择 Material；
- [ ] Frame descriptor 与 Material descriptor 已分离；
- [ ] Skybox 使用独立 descriptor layout；
- [ ] 不同对象可以共享 Mesh、显示不同 Material；
- [ ] Debug 构建通过且手动运行无新增 Validation 错误。

### P1：本阶段建议完成

- [ ] 默认白色、黑色和平坦法线纹理已建立；
- [ ] Emissive texture/factor 已接入 shader；
- [ ] Mesh cache 对内置 Mesh 和 OBJ 使用稳定 key；
- [ ] UI 能显示对象的 Mesh 与 Material；
- [ ] 可以显式复制对象并共享 Mesh；
- [ ] 可以创建独立 Material instance；
- [ ] Release 构建通过；
- [ ] resize/minimize/restore 手动验证通过。

### 延期项

- [ ] 自动化资源类型测试；
- [ ] descriptor/Material 单元测试；
- [ ] 故障注入；
- [ ] Material descriptor 的运行时回收；
- [ ] image layout debug state；
- [ ] glTF、多 primitive、Node 层级；
- [ ] alpha、double-sided 和 pipeline variant。

延期项必须保留在文档中，不能因为初版暂不执行就被误记为已经完成。

---

## 17. 第一次实际手敲任务

Stage 4 的第一个提交只做第 0 步：

1. 删除未使用的 `loadModel()`、`computeModelBounds()`、`rebuildMesh()`；
2. 删除对应的旧全局 vertices/indices/Buffer/bounds 成员；
3. 删除 cleanup 中旧 Buffer reset；
4. 删除 `Buffers.cpp` 中旧两次上传实现的注释；
5. Debug 编译并运行；
6. 提交 `Remove legacy single-mesh state`。

不要在第一个提交中创建 `Mesh.hpp`。先确认清理没有改变画面，再进入第二个提交，能够显著降低
后续机械迁移时的干扰。
