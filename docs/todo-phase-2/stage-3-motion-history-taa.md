# 阶段 3：Motion Vectors、提交历史与基础 TAA 手敲指南

本文接续 [Phase 2 大纲](overview.md#7-阶段-3时域数据与完整基础-taa) 与 [Stage 2 指南](stage-2-deferred-tiled-lighting.md)。核对日期：2026-11-12；源码基线：`30b26ae`，写文档前工作区干净。文件位置以当前分目录架构为准，不使用旧教程中的平铺路径。

**可以进入 Stage 3 的功能开发。** 当前已有单采样 G-buffer、Deferred 全灯光照、Compute Tiled、CSM/IBL、线性 HDR 和独立后处理入口。本阶段按“稳定身份与提交快照 → motion → history → jitter 和 TAA resolve”推进。首先做第 5 节 A1，只修改四个现有源码文件；不先接大批历史图像。

本文是开发设计与手敲顺序，**没有把下文的 Stage 3 功能写入生产源码**。首版 TAA 服务于 **Deferred + Lit、同分辨率、不透明与 Mask**。Forward 保留现有 MSAA 对照；透明、蒙皮、顶点变形、运动模糊、超分辨率、GTAO、SSR 和 ReSTIR 不在本阶段同时实现。

## 1. 当前起点与准入判断

| 当前实现 | 对 Stage 3 的用途 | 本阶段仍需增加 |
|---|---|---|
| `render/deferred/DeferredRendering.cpp`：四颜色附件与 D32，全部 1x | 当前帧表面与深度输入 | motion、稳定表面身份、Temporal Geometry 变体 |
| `DeferredLighting.cpp`、`TiledLighting.cpp` | 生成可采样的线性 HDR；支持全灯/Tiled 对照 | Lighting 后的 temporal resolve |
| `render/frame/SceneFrameBuilder.cpp` | 把有效对象转为本次绘制快照 | 稳定 object/material ID；上一成功提交的模型变换 |
| `SceneUniforms.cpp`、`Camera::projection()` | 统一当前相机、深度重建和光照参数 | 同一帧完整的 jittered/unjittered 矩阵契约 |
| `FrameSubmission.cpp`：两个飞行帧、提交 ID、按槽等待 fence | 保留上传与提交边界 | 在 submit 成功点提交 CPU 历史和 GPU history 索引 |
| `render/post/PostProcessing.cpp` | HDR 采样、曝光、tone mapping、Present | 在本次确实产生 TAA 输出时选择该 HDR 输入 |
| `scene/SceneTypes.hpp`、`Material.hpp` | 共享网格、材质与模型变换 | 跨删除、扩容、导入仍稳定的身份 |
| `diagnostics/FrameTiming.hpp` | 7 个 GPU 区间、14 个 query、模式快照 | 独立 Temporal Resolve 区间与 temporal 配置 |

Stage 2 的旧文档保留规划时的四 MRT、三段计时和 16 灯起点；不能用它判断当前实现是否缺失。当前主 UBO 已是 **656 B**，Deferred UBO 为 **160 B**，点光在独立 SSBO 中，容量为 **1024**。

上一轮已完成 Debug 构建、20 个 shader 编译，以及 Forward 和 Deferred + Tiled 的短帧运行。专项画质、长时间切换和性能归档仍应按实际完成情况记录，不因源码重组而自动视为全部验收。已有基线采样测试夹具把 Forward 未执行的 Deferred 区间标为 Ready，原 HEAD 可复现；它不阻止 A 组功能开发，G 组扩展计时时应同步修正夹具。

## 2. 本阶段选定的实现路线

```text
Shadow（不抖动）
    ↓
Temporal Geometry（抖动后的场景投影，输出 G-buffer + motion + key）
    ↓
[Compute Tiled] → Deferred Lighting（同一套抖动矩阵）
    ↓
Temporal Resolve（线性 HDR；输出非抖动显示网格的 history）
    ↓
曝光 / tone mapping → 阴影预览覆盖层 → ImGui → Present
```

- 首版使用 **Dynamic Rendering 的全屏图形 pass** 做 resolve，复用现有 fragment sampling 路径；不同时增加异步 Compute、Timeline Semaphore 或 Render Graph。
- 相邻成功 temporal 提交共享两套 GPU history。UBO、对象 SSBO 和描述符按 frame slot 隔离；history 不按 slot 定义上一帧。
- 原四 MRT G-buffer 保留。TAA 使用另外的六 MRT 管线变体；新增附件或格式不可用时只报告 TAA unavailable，原 Deferred 继续可用。
- history 存曝光与 tone mapping 之前的线性 HDR。曝光调整不需要重新缩放历史；不引入 pre-exposure。UI 与阴影预览覆盖层不写入 history。
- 第一次只做无 jitter 重投影，坐标与同步正确后再开启 jitter；静态平均不能作为整个 Stage 3 的完成标准。

这些是本项目本阶段的选择，不是 TAA 唯一实现方式。

## 3. 模块归属与文件位置

新增功能进入 `src/render/temporal/` 和 `assets/shaders/temporal/`，不继续往 render 或 shaders 根目录堆文件。

| 位置 | 职责 |
|---|---|
| `scene/SceneTypes.hpp`、`Scene.hpp/.cpp` | 对象身份、统一对象发布入口；不持有渲染历史 |
| `scene/Material.hpp`、`app/resources/MaterialDescriptors.cpp` | 材质身份；分配与发布边界保持在应用资源库 |
| `render/temporal/TransformHistory.hpp/.cpp`（拟新增） | 最近成功提交的 CPU 相机/模型快照与失效状态，不操作 Vulkan |
| `render/temporal/TemporalTypes.hpp`（拟新增） | CPU/GLSL 布局、temporal 设置与本次录制计划 |
| `render/temporal/TemporalGeometry.cpp`（拟新增） | 对象 motion 数据上传、独立 descriptor set、六 MRT Geometry 变体 |
| `render/temporal/TemporalRendering.cpp`（拟新增） | history 创建/释放、resolve 描述符与管线、barrier 和录制 |
| `render/temporal/TemporalMath.hpp/.cpp`（拟新增） | Halton jitter、clip-space 平移及必要的重投影数学 |
| `render/frame/SceneFrameBuilder.cpp`、`SceneUniforms.cpp` | 生成一次相机/对象快照，让 Geometry、Lighting、Tiled 使用一致输入 |
| `render/frame/FrameSubmission.cpp` | 编排 temporal pass；submit 成功后提交历史状态 |
| `render/post/PostProcessing.cpp` | 按本次输出标志选择原 HDR 或 temporal HDR |
| `ui/panels/DisplayPanel.cpp`、`EditorUI.hpp`、`app/EditorBridge.cpp` | 设置、调试显示和 camera-cut 通知，不持有 history 图像 |
| `diagnostics/FrameTiming.hpp`、`BaselineCapture.cpp`、`BaselineExport.cpp` | 计时区间、配置变化、CSV/config 与采样状态 |
| `assets/shaders/temporal/`（拟新增） | temporal Geometry、resolve、预览与公共 temporal GLSL 布局 |

GPU 图像与管线仍由 Renderer 管理和释放；CPU 提交快照独立成 `TransformHistory`。不为每种 history 图像再创建一套通用框架。新增 `.cpp` 显式加入 CMake；已有 shader 脚本递归编译 stage，新增 GLSL include 会被入口 shader 包含。

## 4. 开发分组与每组可见结果

| 分组 | 目的 | 做完后能看到什么 |
|---|---|---|
| A1 稳定对象身份 | 删除或新增对象后不把别人的历史配给当前对象 | 原画面保持；对象 ID 不随 vector 下标变化 |
| A2 材质身份与提交历史 | 建立上一成功提交的相机/对象状态 | CPU 可确认首帧、新对象、未提交帧和回滚的历史语义 |
| B Motion 与表面 key | 覆盖相机和刚体对象运动 | 静止速度为零；移动物体和相机的 motion 可预览 |
| C History 与无 jitter 重投影 | 建立安全的跨帧图像读写 | 相机慢移时能重投影，初帧/失效走 seed |
| D 历史拒绝 | 拒绝遮挡显露、不同表面和不可信深度 | 移动物体后面不会长期残留旧颜色 |
| E Jitter 与非抖动输出网格 | 让子像素采样与重投影使用一致坐标 | 静止细线有采样变化，画面不整体跟着 jitter 漂移 |
| F 完整基础 resolve | 邻域裁剪、响应权重、天空重投影 | 静态收敛，运动时控制拖影与模糊 |
| G 设置、失效与诊断收口 | 给出可用功能与可复现对照 | 明确开关、motion/rejection/age 视图、耗时与归档 |

每组完成相关构建和短运行，再继续下一组。当前优先功能，不新建自动化框架，不把手敲流程拆成大量测试任务。

## 5. A1：现在先手敲稳定对象身份

**本组只修改四个文件：`SceneTypes.hpp`、`Scene.hpp`、`Scene.cpp`、`app/scene/Model.cpp`。不修改 shader、descriptor、push constants 或 CMake。**

### 5.1 声明对象 ID

**目的：把对象身份与数组位置分开。两个物体即使共享 Mesh/Material，也必须有不同身份。**

打开 [src/scene/SceneTypes.hpp](../../src/scene/SceneTypes.hpp)。在 includes 中加入：

```cpp
#include <cstdint>
```

在 `struct Transform` 前加入：

```cpp
using SceneObjectId = std::uint64_t;
inline constexpr SceneObjectId invalidSceneObjectId = 0;
```

在 `struct SceneObject` 的第一个成员位置加入：

```cpp
SceneObjectId id = invalidSceneObjectId;
```

保留其余现有字段和 `modelMatrix()`。0 表示尚未正式发布的暂存对象；这里只增加 CPU 身份，之后把 64 位 ID 拆为两个 `uint32` 上传，不要求 Vulkan 的 `shaderInt64`。

### 5.2 在 Scene 中集中发布新对象

**目的：普通添加和 glTF 批量导入使用同一个身份分配入口；场景替换与失败回滚只交换对象列表，不重置计数器。**

打开 [src/scene/Scene.hpp](../../src/scene/Scene.hpp)。在 `updateAnimations()` 声明前加入：

```cpp
// 发布新对象并分配新身份；移动或删除列表中的其他对象不改变已有 ID。
void appendObject(SceneObject object);
```

在类末尾、最后一个 `};` 之前加入：

```cpp
private:
    SceneObjectId nextObjectId_ = 1;
```

当前预设事务使用 `scene.objects.swap(...)`，而不是替换整个 Scene，因此该计数器不会被准备/回滚过程重置。后续若增加“替换整个 Scene”的入口，必须同时改变 temporal generation 或保留此分配器；不能只重置 ID 再沿用旧历史。

### 5.3 实现发布入口

**目的：只有正式进入场景的对象才取得新身份，既保留暂存导入流程，也不在动画更新阶段重复分配。**

打开 [src/scene/Scene.cpp](../../src/scene/Scene.cpp)。在标准库 includes 中加入：

```cpp
#include <cassert>
#include <utility>
```

在 `void Scene::updateAnimations(float deltaTime)` 定义前加入：

```cpp
void Scene::appendObject(SceneObject object)
{
    assert(nextObjectId_ != invalidSceneObjectId);

    object.id = nextObjectId_;
    objects.push_back(std::move(object));
    ++nextObjectId_;
}
```

同一个 Scene 的 64 位计数器持续递增，0 保留；断言表达未耗尽的内部不变量。原有 glTF 批量导入已经在发布前完成 `reserve()` 并要求对象移动不抛异常，保留这些准备工作，不给 `appendObject()` 再加逐个 `reserve()` 或每帧重新编号。

### 5.4 把两处正式发布改为新入口

**目的：覆盖普通对象和 glTF primitive 实例，避免只改按钮添加、导入对象仍全部是 ID 0。**

打开 [src/app/scene/Model.cpp](../../src/app/scene/Model.cpp)。找到 `Application::addMeshObject()` 中：

```cpp
scene.objects.push_back(std::move(object));
```

替换为：

```cpp
scene.appendObject(std::move(object));
```

继续找到 `Application::addGltfMeshObjects()` 末尾的发布循环：

```cpp
for (SceneObject& object : stagedObjects)
{
    scene.objects.push_back(std::move(object));
}
```

替换为：

```cpp
for (SceneObject& object : stagedObjects)
{
    scene.appendObject(std::move(object));
}
```

**不要替换前面的 `stagedObjects.push_back(...)`。** 那是准备阶段，尚未发布；也不要把预设回滚的 `swap()` 改成 append，否则旧对象会被重新编号。Inspector 删除仍保留现有 `erase()` 和延迟 GPU 资源回收。

### 5.5 构建与预期

**目的：确认新入口覆盖现有发布流程，同时保持原渲染链可用。**

```bash
cmake --build --preset linux-debug --parallel 2
./build/linux-debug/vulkan --scene shadow-playground --frames 8
```

这一组完成后画面应保持现状。删除列表中的一个对象，其他对象的 `id` 不变；新增对象得到后续 ID。此时还没有 motion 或 TAA 画面；接下来做 A2，把身份传到实际绘制快照并建立提交历史。

## 6. A2：身份、相机快照与上一成功提交

**目的：让 previous model 和 previous camera 对应上一张真正提交的画面，而不是当前 frame slot 两次之前的数据。**

### 6.1 补齐材质身份与绘制快照

- `scene/Material.hpp` 增加 `MaterialId = std::uint64_t` 和 0 初值的 `id`。应用资源库在新材质正式分配/发布时给出单调递增的 ID；不得用描述符句柄、指针地址、名称或材质数组位置代替。
- `app/resources/MaterialDescriptors.cpp::allocateMaterialDescriptorSets()` 是已有新材质发布边界。身份分配不能破坏整批分配失败回滚；失败消耗几个 ID 没有关系，旧 ID 不复用。
- `app/scene/ScenePresets.cpp` 当前逐项恢复六个内建材质参数，保留这种方式，不覆盖稳定 `id` 和 `descriptorSet`。默认值副本在身份分配前建立；将来若改为整份 Material 赋值，必须显式保留身份与描述符。新建材质和编辑现有材质是不同操作。
- `RenderObjectView` 增加 `SceneObjectId objectId` 与 `MaterialId materialId`；`buildRenderObjects()` 直接复制对象和材质的稳定 ID。
- GPU 当前数组索引只负责 SSBO 寻址，跨帧仍以 object ID 查找。无效对象会被 builder 跳过，所以不能依赖输出 vector 的位置。

材质参数/贴图编辑与材质替换请求 history reset。初版不建立复杂逐材质内容哈希系统；普通刚体运动不 reset，而由 motion 处理。

### 6.2 CPU 历史接口

在拟新增 `render/temporal/TransformHistory.hpp/.cpp` 中明确以下接口：

```cpp
struct PreviousObjectTransform
{
    glm::mat4 model{1.0f};
    bool valid = false;
};

struct FrameTransformSnapshot
{
    glm::mat4 view{1.0f};
    glm::mat4 viewProjectionUnjittered{1.0f};
    glm::vec2 jitterUv{0.0f};
    std::unordered_map<SceneObjectId, glm::mat4> models;
};

class TransformHistory
{
public:
    bool valid() const noexcept;
    PreviousObjectTransform previousObject(SceneObjectId id,
                                           const glm::mat4& currentModel) const noexcept;
    const FrameTransformSnapshot& previous() const noexcept;
    void invalidate() noexcept;
    void commit(std::uint64_t submissionId, FrameTransformSnapshot&& snapshot) noexcept;

private:
    FrameTransformSnapshot previous_;
    std::uint64_t submissionId_ = 0;
    bool valid_ = false;
};
```

`previous()` 只在 `valid()` 为真时调用；无历史时 previous matrices 回填 current matrices。新对象回填 current model，**同时保留 `valid=false`**，否则会错误借用原位置的旧背景颜色。

快照在录制前从实际 `RenderObjectView` 复制，提前完成 map 分配。`commit()` 只移动/交换已准备的容器与矩阵，不在 queue submit 成功后再次分配或抛异常。不得保存 `RenderFrameData::objects` 指针：它目前借用 Application 的局部 vector，只承诺 `recordFrame()` 期间有效。

### 6.3 提交点必须放对

在 `FrameSubmission.cpp::endFrame()` 中，精确顺序是：

```text
vkQueueSubmit / vkQueueSubmit2
    ↓
VK_CHECK_RESULT(submitResult, ...)
    ↓
生成 submissionId
    ↓
commit 本次 CPU 快照、history 输出索引与 jitter 序号
    ↓
vkQueuePresentKHR
```

本次实际生成 temporal 输出时，才推进 GPU history 索引与 jitter 序号；Forward 或暂停积累的调试提交不能翻转它们。acquire Skip/OutOfDate、只录制未提交、submit 失败都不推进。Present 随后返回 `RecreateSwapchain` 时，本次 GPU 工作已经真实存在，仍属于成功提交；随后重建再显式 invalidate。不能把 Application 里的 `endStatus == Ready` 分支当作提交确认。

删除对象后，下次成功提交用当前快照替换整份上一快照，自然去掉已删除项。预设准备失败继续保留旧 ID 和旧历史；只在成功发布新场景之后请求 reset，即使重载同一 preset 也一样。

## 7. B：Motion、表面 key 与 GPU 数据

**目的：为每个当前可见表面找到上一成功提交中的同一个局部顶点，覆盖相机与刚体对象运动。**

### 7.1 保持现有 128 B push constants

当前 `DrawPushConstants` 已正好 128 B，不追加 `previousModel`。`textureInfo.w` 在 Shadow 中还保存 cascade index，不拿它同时承担对象索引。

采用独立的 **Temporal Geometry set 2**：binding 0 为 temporal UBO，binding 1 为只读对象 SSBO。原 frame set 0 的 binding 0～6、材质 set 1 均保持语义。虽然 frame binding 7 起空闲，本阶段不继续扩大应用 frame 描述符的职责。

Temporal Geometry 的 pipeline layout 是 `[frame, material, temporalGeometry]`。Deferred Lighting 中 set 2 的 skybox 属于另一条 pipeline layout，不与这里冲突。切换管线时按相应 layout 重新绑定需要的 set，保持 push constant range 一致。

对象数据放在 `render/temporal/TemporalTypes.hpp`：

```cpp
struct alignas(16) TemporalObjectData
{
    glm::mat4 previousModel{1.0f};
    // object ID low/high，material ID low/high；GLSL 只使用 32 位 uint。
    glm::uvec4 surfaceKey{0u};
    // x: previous object valid，其余保留。
    glm::uvec4 flags{0u};
};

static_assert(offsetof(TemporalObjectData, surfaceKey) == 64);
static_assert(offsetof(TemporalObjectData, flags) == 80);
static_assert(sizeof(TemporalObjectData) == 96);
```

Temporal draw 把数组索引放入直接 `vkCmdDrawIndexed()` 的 `firstInstance`，vertex shader 用 `gl_InstanceIndex` 读取。原 Shadow/Forward draw 保持各自语义；不用因此引入 indirect draw 或 `shaderInt64`。

SSBO/UBO 按帧槽分配，等该槽 fence 后更新并 flush。对象容量按真实绘制数量增长，扩容和 descriptor 重写只影响已安全复用的槽；检查实际字节数不超过 `maxStorageBufferRange`，不固定猜测一个对象上限。

### 7.2 固定 temporal UBO

Geometry 和 resolve 共用以下字段，放在 `TemporalTypes.hpp` 与 `temporal/temporal_frame.glsl`；用 set/binding 宏让两个入口复用同一 GLSL 块定义。

| 字段 | 偏移 | 用途 |
|---|---:|---|
| `mat4 currentView` | 0 | 当前线性 view depth |
| `mat4 currentViewProjectionUnjittered` | 64 | 排除 jitter 的当前位置 |
| `mat4 previousViewProjectionUnjittered` | 128 | 上一提交位置、天空方向投影 |
| `mat4 previousView` | 192 | 同一表面的预期上一 view depth |
| `mat4 inverseCurrentViewProjectionJittered` | 256 | 从实际 G-buffer 深度重建 |
| `vec4 cameraPosition` | 320 | 当前世界相机位置 |
| `vec4 jitterUv` | 336 | xy=current jitter UV，zw=previous jitter UV |
| `vec4 resolveParams` | 352 | history weight、绝对深度阈值、相对阈值、法线余弦阈值 |
| `uvec4 frameInfo` | 368 | width、height、history valid、jitter sample index |

std140 总计 **384 B**，C++ 对每个偏移、对齐和总大小做 `static_assert`。当前 frame UBO 656 B 和 Deferred UBO 160 B 不直接追加这些字段。

### 7.3 两个新增 Geometry 附件

| 附件 | 格式 | 内容与 clear |
|---|---|---|
| `motion` | `R16G16B16A16_SFLOAT` | xy=UV 位移；z=预期上一帧正线性 view depth；w=对象历史有效位；clear=0 |
| `surfaceKey` | `R32G32B32A32_UINT` | object ID low/high、material ID low/high；clear 全 0 |

保留既有四颜色附件和 D32，Temporal Geometry 变体一共六个颜色输出。新增图像属于每帧槽的 Temporal 输入目标，不改变原 `gBufferColorFormats_[4]` 的含义。

创建前检查：颜色输出上限至少 6、相关 descriptor/SSBO/UBO 限制、每种格式的 `COLOR_ATTACHMENT | SAMPLED`、单采样/尺寸组合与整数输出支持。整数附件关闭 blending，使用正确的 uint clear 和整数 shader output。TAA 不支持时保留四 MRT Deferred，不让 `gBufferGeometryAvailable()` 一起失效。

新增 temporal 专用 vertex/fragment 入口；已有 `shared/mesh.vert` 同时被 Forward 与 G-buffer 使用，不能直接往其中加必须绑定的 temporal SSBO。把真正共享的几何与材质处理抽成小型 GLSL include，两个入口复用法线、切线、UV、Mask 和双面逻辑，避免维护两份材质算法。

### 7.4 Motion 方向与深度

本文固定：

```glsl
vec2 velocityUv = previousUvUnjittered - currentUvUnjittered;
```

这是“当前表面在上一帧哪里”的 UV 位移，不是每秒物理速度。相机和对象都静止时应为 0，即使未来启用了 jitter。该方向也符合 [GPUOpen 的 current-to-previous motion 约定](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/#providing-motion-vectors)，本文自己确定 UV 单位与输出网格，不照搬其像素单位 API。

vertex 中分别计算：

```glsl
vec4 local = vec4(inPosition, 1.0);
vec4 currentWorld = currentModel * local;
vec4 previousWorld = previousModel * local;
vec4 currentClip0 = currentViewProjectionUnjittered * currentWorld;
vec4 previousClip0 = previousViewProjectionUnjittered * previousWorld;
float expectedPreviousDepth = -(previousView * previousWorld).z;
```

`currentClip0`、`previousClip0` 用普通 smooth vec4 varying，片元中各自除 W 后再求 UV。不能把顶点处已除 W 的 previous NDC 用 `noperspective` 插值，代替透视正确的表面对应。

新对象、previous W 非正或历史缺失时 motion.w=0；已有对象也要检查坐标有限、previous clip 深度和 UV 范围。Mask discard 必须同时影响颜色、motion、key 和 depth。历史 key 用 flat 整数 varying。

## 8. C：History、无 jitter 重投影与输出接入

**目的：先在不引入采样扰动的情况下，把相邻提交的颜色和表面信息正确连接起来。**

### 8.1 资源与索引

全局保存两套 `TemporalHistoryTarget`，每套包含：

| 图像 | 格式 | 采样 |
|---|---|---|
| color | RGBA16F | clamp-to-edge，LINEAR；已验证格式支持过滤 |
| linear depth | R32F | NEAREST / texelFetch |
| world normal + surface mask | RGBA16F | NEAREST / texelFetch |
| surface key | RGBA32_UINT | usampler，NEAREST / texelFetch |
| age | R16_UINT | usampler，NEAREST / texelFetch |

resolve 同时输出这五个 MRT，使 color、depth、normal、key、age 属于同一次提交。它们都需要 `COLOR_ATTACHMENT | SAMPLED`；用于初始化清零的图像另加 `TRANSFER_DST`。检查所有实际 usage 组合，不能只查格式名称。

不要直接复制当前 D32 到 R32F：现有 G-buffer 不含 `TRANSFER_SRC`，两个格式也不是这样转换。resolve 用实际选中输入 texel 的中心 `uvRep=(pixelRep+0.5)/extent`，与该 texel 的 D32 一同乘 `inverseCurrentViewProjectionJittered`，再用 currentView 计算线性深度并写 R32F。不能用输出 q 配抖动深度；选择了邻域深度时，也不能继续用中心输入坐标逆投影。

读写计划在录制前固定：

```text
read = 最近成功 temporal 提交写出的 history
write = 另一套 history
```

`frameIndex` 只选择当前 UBO/SSBO/descriptor；`imageIndex` 只选择交换链图像。没有 temporal 输出的提交不把某张旧图像标成新历史。

### 8.2 描述符与 seed

resolve 使用自己的 set 0：binding 0～4 分别为 current HDR、motion、current depth、current normal、current key；5～9 为 previous color/depth/normal/key/age；10 为当前 temporal UBO。Geometry 的 UBO/对象 set 由另一个 layout 管理。

描述符按 `[frameSlot][historyReadIndex]` 固定分配，或只在对应 slot fence 完成后更新。Tonemap 读取本次 history write color 的 set 使用现有 Post layout；不要每帧重写一个全局、仍可能在途的 set。

首次创建后一次性初始化两套 history，并转到正确只读布局；在 `Renderer::initialize()` 内创建时使用已就绪的 `uploadContext_.immediateSubmit()`，不能调用尚受 `initialized_` 检查限制的公共 `Renderer::immediateSubmit()`。本文采用初始化有效图像的方案。**`historyValid=false` 不允许绑定空句柄、未定义 layout 或虚假的 SHADER_READ_ONLY 图像。**

seed 输出当前颜色、当前元数据与 age=1。后续 camera cut 只让逻辑历史失效，保持真实 layout 跟踪；不需要每次 reset 都清图或等待整个设备。

### 8.3 本文统一使用非抖动的输出网格

history 和最终 resolve 输出从开始就定义在非抖动显示网格 `q`：

```glsl
vec2 q = (vec2(outputPixel) + 0.5) / vec2(extent);
vec2 currentInputUv = q + currentJitterUv;
vec2 previousHistoryUv = q + velocityUv;
```

C 组 jitter 仍为 0，因此先验证普通重投影。E 组开启 jitter 后，当前 HDR/深度/法线/motion/key 在 `currentInputUv` 周围重建或选择代表表面，previous history 仍按 `q + velocityUv` 查询。

**这里不再加 `previousJitterUv-currentJitterUv`。** 那个补偿属于“history 也保存在抖动 raster 网格”的另一套表示；若把它混入本文的非抖动输出，会重复补偿并导致静止画面漂移。

### 8.4 接入当前帧编排

在 `FrameSubmission.cpp::recordFrame()` 的 `recordDeferredLightingPass()` 完成后、`beginPresentPass()` 之前录制 resolve。只有本次实际生成 temporal 输出，才向 `recordToneMapping()` 提供 temporal HDR set；否则仍读取现有 Forward/Deferred HDR。

ImGui、阴影深度覆盖层和 G-buffer 预览保持 Present 阶段的现有归属。调试视图不累积其可视化颜色。

### 8.5 初始化、目标重建与释放

- `Renderer::initialize()`：在原 G-buffer/Deferred 资源准备好后创建 temporal 布局、UBO/SSBO、motion/key、history 和描述符；内部清图使用已准备好的 UploadContext。创建中途失败进入已有 shutdown 路径。
- `recreateHdrTargets()`：保留调用方已等待 GPU、无活动帧的前提。原尺寸资源释放前处理旧 temporal 目标与其描述符引用；原 G-buffer/Deferred 重建后，再按新 extent 创建 motion/key、两套 history，重写所有 resolve 和 temporal tonemap set，最后 invalidate。尺寸无关管线/布局可保留。不能只重建旧目标而留下指向旧 view 的 temporal set。
- `Renderer::shutdown()`：先释放 temporal 描述符、管线/布局与图像/buffer，再释放其依赖的旧 Deferred/G-buffer 与共享 layout；保持设备和 UploadContext 活到依赖者关闭。
- `requestTemporalReset()` 只处理逻辑历史；它既不替代尺寸重建，也不偷偷等待整个 GPU。

## 9. History 在两个飞行帧中的同步

**目的：让 CPU 可以准备下一帧，同时保证相邻提交读取与后续覆盖共享 history 时没有 RAW/WAR 冒险。**

所有 temporal 工作保留在当前 graphics queue。对每张 history 图像跟踪实际 layout 与最近访问，不用每帧 `vkDeviceWaitIdle()` 代替依赖。

| 依赖 | source | destination |
|---|---|---|
| Geometry motion/key 写后被 resolve 采样 | COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_WRITE | FRAGMENT_SHADER / SHADER_SAMPLED_READ |
| G-buffer depth 写后被 resolve 读取 | EARLY+LATE_FRAGMENT_TESTS / DEPTH_STENCIL_ATTACHMENT_WRITE | FRAGMENT_SHADER / SHADER_SAMPLED_READ |
| Lighting HDR 写后被 resolve 读取 | COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_WRITE | FRAGMENT_SHADER / SHADER_SAMPLED_READ |
| history 输出被 Tonemap/下一提交读取 | COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_WRITE | FRAGMENT_SHADER / SHADER_SAMPLED_READ |
| 已采样 history 下一次作为 MRT 覆写 | FRAGMENT_SHADER / 执行依赖 | COLOR_ATTACHMENT_OUTPUT / COLOR_ATTACHMENT_WRITE，配合只读→附件布局转换 |

写→读同时保证可见性和 layout；读→覆写的 WAR 仍需要执行依赖和 layout 转换，不能因为不保留旧内容就省略。具体 Vulkan 屏障模式见 [Khronos Synchronization Examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。

例如提交 N 写 H0，N+1 读 H0/写 H1，N+2 读 H1/写 H0。等待 N+2 使用的 slot fence 只能证明该槽旧提交完成，不能证明 N+1 已读完 H0；必须有跨命令缓冲的读→覆写依赖。

今后如果换 Compute resolve，需要把消费阶段改为 COMPUTE 并验证 STORAGE 能力；当前 HDR barrier 只有 fragment destination，不能直接插一个 dispatch。异步队列还需要 semaphore 与可能的 queue ownership transfer，均不纳入首版。

## 10. D：拒绝不可信历史

**目的：让新显露的表面使用当前颜色，避免把前景、背景或不同物体的旧颜色混进来。**

先按当前 surface mask 分流：背景进入第 12.2 节的天空分支，**不经过下面的对象 motion 有效性检查**。以下顺序仅针对表面；失败即 current-only、age=1：

1. 全局 history valid、motion.w、previous clip 与 UV 合法。
2. previous UV 在有效 texel 范围内；不能先 clamp 到边缘，再把越界历史当有效。
3. 当前和历史的 surface/background mask 相同。
4. object/material key 完全相同；整数 key 不做线性过滤。
5. 历史线性深度接近 motion.z 的“该表面预计上一 view depth”。
6. 世界空间法线方向相容；初版使用保守的当前/历史法线点积，快速旋转可多拒绝。

深度判断示例：

```glsl
float tolerance = max(depthAbsoluteThreshold,
                      depthRelativeThreshold * expectedPreviousDepth);
bool depthValid = abs(historyLinearDepth - expectedPreviousDepth) <= tolerance;
```

不能直接用 current D32 减 previous D32；相机运动时空间不同。移动对象也不能仅拿 `previousView * currentWorldPosition` 预测上一深度，必须使用 previousModel 对应的上一世界位置。

初始可调值采用绝对阈值 0.02 世界单位、相对阈值 0.01、normal dot 0.85；它们是调试起点，受场景尺度、法线贴图与 motion 深度半精度误差影响，应随配置归档，不当作通用正确阈值。

current 的 motion/depth/normal/key 必须来自同一个代表表面。需要边缘 motion dilation 时，在当前 3×3 中选最近有效深度对应的整套数据，不能只扩 velocity 却继续使用中心像素的 key 或 expected depth。

先根据 nearest 元数据检查 history；采样 history color 的双线性 footprint 若跨到不同 key/深度，丢弃不匹配 tap 并重新归一化，或保守拒绝这次历史。只验证中心 metadata、却无条件混合跨边界的四个颜色 tap，仍会造成轮廓渗色。

## 11. E：Jitter 必须贯穿完整投影链

**目的：改变每次提交的子像素覆盖，而不是把最终图像简单来回平移。**

Camera 继续返回非抖动 `P0`，不让相机数学依赖 TAA。在渲染层使用 8 个样本的 Halton(2,3) 序列：

```cpp
jitterPixels = glm::vec2(halton(sample + 1, 2), halton(sample + 1, 3)) - 0.5f;
jitterUv = jitterPixels / glm::vec2(extent.width, extent.height);
```

样本索引仅随成功 temporal 提交推进；reset 从样本 0 开始。以最终 Vulkan clip 空间的右/下为正，构造：

```cpp
glm::mat4 jitterTransform{1.0f};
jitterTransform[3][0] = 2.0f * jitterUv.x;
jitterTransform[3][1] = 2.0f * jitterUv.y;
const glm::mat4 projectionJittered = jitterTransform * projectionUnjittered;
```

当前 Camera 已采用 RH_ZO、Y 翻转、正高度 viewport，因此 UV=`ndc*0.5+0.5`；这里不额外翻 Y，深度不改成 OpenGL 的 `[-1,1]`。

必须从同一份本帧矩阵派生：

| 消费者 | 使用的投影 |
|---|---|
| Geometry 的实际 gl_Position | jittered |
| frame UBO 的 proj | jittered |
| Deferred inverseProjection / inverseViewProjection | 对应同一 jittered projection |
| Tiled frustum 与深度重建 | 对应同一 jittered projection |
| 天空当前射线 | 对应同一 jittered projection |
| motion 的 current/previous clip | 各自 unjittered |
| CSM 拟合、拾取与 gizmo | unjittered |

`SceneUniforms` 现在一次性生成 frame/Deferred 数据，应把 jitter 输入纳入这次生成流程，不能生成完只改 frame.proj，却保留旧 inverseProjection。CSM 保持非抖动拟合，同时增加一个像素量级的保守 receiver guard band，使 jitter 后的边缘仍被覆盖；Tiled 同样需要保守边界，防止 tile 边缘丢灯。

resolve 在 `q + currentJitterUv` 重建当前颜色。首版可使用明确、归一化的小型 tent filter，邻域裁剪使用同一区域；不把 current HDR 直接按 q 取样再叠加一套历史 jitter 差值。检查静态棋盘与细线：应有采样收敛，不应出现整屏周期性位移。

## 12. F：完整基础 TAA resolve 与天空

**目的：减少闪烁，同时抑制错误或过时颜色造成的拖影。**

### 12.1 邻域与混合

对当前重建位置周围 3×3 的线性 HDR 建立颜色范围。首版选 YCoCg 的邻域 min/max，裁剪历史后再转回线性 RGB；shader 中记录究竟启用 clamp 还是 clip，不用一个含糊的开关指代不同算法。

先实现可关闭的 AABB clamp；后续需要更少颜色偏移时，在同一处改为射线/AABB clip 或 variance clip，不同时维护多套未经比较的实现。邻域裁剪的参考实现可看 [Playdead TemporalReprojection.shader](https://github.com/playdeadgames/temporal/blob/master/Assets/Shaders/TemporalReprojection.shader)，本文不照搬其 Unity 矩阵与坐标宏。

拒绝通过时：

```glsl
float historyWeight = min(configuredHistoryWeight,
                          float(historyAge) / float(historyAge + 1u));
vec3 result = mix(currentColor, clippedHistoryColor, historyWeight);
```

初始 history weight 上限取 0.9，age 上限取 32；新历史 age=1，失败重置为 1。再根据像素运动量和当前/历史亮度差降低历史权重，让快速运动与灯光变化更及时；不能靠把历史权重始终调到接近 1 掩盖闪烁。

motion 仅代表几何对应，不会自动解释镜面高光、光源变化、法线贴图高频或材质编辑；这些情况需要颜色裁剪、响应权重或显式失效。

### 12.2 背景单独处理

C/D 组可让背景 current-only，以便先定位表面重投影。完整基础 TAA 加上天空的旋转重投影：

- 从当前相机与实际逆矩阵恢复世界射线；使用非抖动输出 q 所对应的当前输入位置。
- 用上一未抖动 view-projection 投影 `vec4(worldDirection, 0)`，忽略相机平移。
- 在通用表面 rejection 之前进入此分支；不检查背景清零的 motion.w，也不使用对象 key、表面法线或有限线性深度判断。
- 只检查全局 history valid、方向投影有限、previous W>0、UV 范围与历史 background mask。有限 RH_ZO 投影下，方向向量投影的 NDC Z 可能大于 1，不能套用表面 clip Z∈[0,1] 的条件。
- 当前环境采样有 `(x,z,y)` 的 cubemap 方向转换；用于相机投影的仍是原世界方向，不把 swizzle 后的采样方向拿去投影。

背景/表面相互遮挡时立即拒绝。环境图更换时 reset；天空不需要伪造一个“非常远的模型”来借用表面 motion。

## 13. G：失效、设置与诊断

**目的：将前面各组收成可使用、可解释的功能，避免“源码有了但哪些条件可用不清楚”。**

### 13.1 失效入口

Renderer 提供单一的 `requestTemporalReset(TemporalResetReason reason)`。它改变逻辑有效性和 jitter 序号，不隐藏资源重建或 GPU 全局等待。

| 事件 | 处理 |
|---|---|
| 首帧、新建/重建 history | invalid，下一次输出 seed |
| resize、最小化恢复后的目标重建 | 先沿现有路径等待/重建，再 invalid |
| 成功切换或重载场景 | invalid，即使 preset 名字相同 |
| 场景切换失败回滚 | 保留旧历史；准备阶段不提前 reset |
| 测试相机跳转、near/far/FOV 改变 | 在本帧 uniform/motion 生成前 invalid |
| 正常相机移动、对象平移旋转 | 保留历史，依靠 motion 与 rejection |
| TAA/jitter 开关、Forward/Deferred、调试/预览切换 | invalid；禁用阶段 jitter=0 |
| 材质/贴图编辑、环境图替换 | invalid，保留稳定 ID |
| 灯光快速编辑 | 降低权重；首版也可在编辑事件请求 reset |
| 曝光/tone mapper 改变 | 线性 HDR 历史保持，配置采集按显示条件取消 |

通过 `EditorActions` 增加 camera-cut/material-change 通知，`EditorBridge` 转给 Renderer。不要在 `Camera::lookAt()` 内自动 reset：预设准备阶段会修改相机副本，事务可能失败。`CameraController::reset()` 目前只重置输入，不是渲染历史提交事件。

UI 仅在 Deferred、Lit、无 G-buffer 预览、无阴影深度覆盖视图、能力可用时启用 TAA。不可用时显示实际原因，不静默切换算法。设置至少包括 TAA、jitter、history weight、clipping、深度/法线阈值及手动 reset。

### 13.2 调试视图

增加 temporal 独立调试模式，不把其颜色送回 history：

- motion UV 转像素的方向/幅度；静态为零。
- reprojected history，观察对应位置。
- rejection reason：无历史、越界、身份、深度、法线、背景不一致。
- history age，观察稳定区域增长与显露区域重置。

调试视图可以继续生成 motion 和检查 history，但不能积累“调试可视化颜色”。要么只读最近完整 Lit history，要么明确 seed/暂停积累策略；不要因为切换预览而把旧 debug 颜色带回 Lit。

### 13.3 计时、配置与载荷

追加 `GpuPass::TemporalResolve = 7`，保持原 0～6 编号；Count 变为 8、query 数变为 16。只在本次实际执行 resolve 时置位，未执行显示 NotExecuted。Frame GPU 覆盖总命令区间，不与分段耗时相加冒充总时间。

同步扩展 pass 名称/CSV 列、expected mask、BaselineCapture 的状态校验、Temporal 参数快照、配置比较与导出 schema。当前 schema 为 3，加入这些字段后升级到 4。修正旧测试夹具时按 expected mask 设置 Ready/NotExecuted，不能继续将每个区间一律标 Ready。

固定配置归档包含：TAA 实际启用、jitter 样本数与约定、history weight、clipping、阈值、reset generation、实际图像格式、分辨率、RenderPath、culling 模式与构建/设备信息。jitter 当前样本序号和 history age 是运行状态，不能加入“每变一次就取消采集”的配置比较；正常采样推进不应取消基线。

本方案的名义额外载荷：每帧槽 motion 8 B + key 16 B，两槽共 48 B/pixel；两套 history 每套 color 8 + depth 4 + normal 8 + key 16 + age 2，共 76 B/pixel；合计 **124 B/pixel，1920×1080 约 245.2 MiB**，不含 buffer、驱动对齐和现有 G-buffer。将其纳入 RenderTargetPayloadEstimate，VMA 统计另列；这是为明确身份与调试选择的首版格式，后续再凭实测压缩 normal/key，而非称作实际驻留显存。

## 14. 功能完成标准

以下为开发过程中的人工功能收口，不要求先增加测试框架：

- [ ] 静止相机/物体的 motion 为零；仅移动相机、仅移动物体、两者同时运动均对应正确。
- [ ] 新增、删除、导入、vector 扩容与预设回滚不混淆对象身份。
- [ ] history 对应上一成功提交；acquire Skip 不推进，Present OutOfDate 后能重建恢复。
- [ ] 首帧、重建、camera cut、材质编辑与模式切换走正确 seed/reset。
- [ ] 深度、法线、身份与越界拒绝有效；移动遮挡物后方不持续残留旧颜色。
- [ ] jitter 开关独立可观察；静止细线不会随序列整体漂移，重建与 tile 灯列表无错位/闪黑。
- [ ] 邻域裁剪和权重能独立对照；说明细线、远纹理、镜面高光的改善与仍存在的模糊。
- [ ] 天空旋转重投影正确，相机平移不让无限远背景产生错误位移。
- [ ] UI、阴影预览、G-buffer/debug 颜色不污染线性 HDR history。
- [ ] 两帧在途下 history 的 RAW/WAR、layout、descriptor 生命周期无已知错误。
- [ ] 无 TAA、无 jitter 累积、完整 TAA 在同尺寸/机位/材质/曝光条件下可比较，成本和额外载荷可查看。

最小静态累积完成后可以继续开发，但不能将它勾成完整 Stage 3。完成本阶段再进入大纲的 GTAO/Bloom 等专题；它们可复用提交历史与重投影约定，各自管理算法历史，不直接把 TAA 成品颜色当作所有时域算法的历史。
