# 阶段 1：Cascaded Shadow Maps 手敲指南

本文接续 [Phase 2 大纲](overview.md#5-阶段-1cascaded-shadow-maps)。源码核对基线为 `4655e94`，编写日期为 2026-10-19。

**状态：实现计划，尚未实施。下一项主线是阶段 0.5，完成其版本协商、Synchronization2 和主帧路径 Dynamic Rendering 后，再按本文接入 CSM。**

CSM 算法本身不要求 Vulkan 1.3；这里遵循项目选定的学习顺序，避免一边增加四级阴影，一边重写附件与同步。前置步骤见 [阶段 0.5 手敲指南](stage-0-5-vulkan-modernization.md)。可以先阅读本文或做独立 CPU 数学练习，但不能将练习通过等同于 CSM 已接入。

## 1. 本轮要得到什么

第一版固定四个 cascade，保留单张方向光阴影作为画质和耗时参照。主场景仍是现有 Forward PBR，不同时加入几何着色器、multiview、GPU culling、Render Graph 或点光源阴影。

要能解释完整链路：

```text
相机 near / shadow distance
    -> 四段正的视空间距离
    -> 每段接收区域的八个角点
    -> 四套稳定的 light view/projection
    -> 当前 frame slot 的四层深度图
    -> 根据表面的视空间距离选层、比较深度、执行 PCF
    -> 调试图、采集配置、GPU 时间和资源开销
```

CSM 给近处接收区域分配更多阴影 texel；它不保证阴影没有所有锯齿，也不保证比单图快。划分视锥、分别生成深度图、着色时选择对应级，是算法的基本结构。[Microsoft CSM 说明](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)

## 2. 先认清已有代码和拟新增内容

以下是本次源码基线的情况。阶段 0.5 完成后，应重新检查附件录制函数的位置；不要把表中的当前 RenderPass 接口当成之后仍然存在的前提。

| 当前入口 | 已有职责 | 本阶段拟调整内容 |
|---|---|---|
| [RenderSettings.hpp](../../src/render/RenderSettings.hpp) | 单图开关、PCF、bias、中心和半宽 | 增加 Single/CSM、shadow distance、split lambda、稳定化和过渡设置 |
| [Camera.cpp](../../src/scene/Camera.cpp) | Z-up 相机、RH_ZO 投影、统一 Y 翻转 | 复用 view、near/far、FOV，不把 cascade 状态塞回 Camera |
| [Buffers.cpp](../../src/app/Buffers.cpp) | 在安全帧槽写 UBO，计算一套光源矩阵 | 调用纯 CPU cascade 计算，填入四套矩阵和 split 数据 |
| [ShaderTypes.hpp](../../src/render/ShaderTypes.hpp) | 1088 字节 Frame UBO、128 字节 draw push constants | 明确扩展 UBO 契约，保持 draw push constants 的 128 字节大小 |
| [ShadowMapping.cpp](../../src/render/ShadowMapping.cpp) | 每槽一张单层深度图、比较/预览 sampler、单图绘制 | 深度数组、每层附件 view、逐级录制、逐层预览 |
| [Renderer.cpp](../../src/render/Renderer.cpp) | Shadow/Scene/Post + UI 三段计时和实际 draw 统计 | Shadow 区间包住所有 cascade；记录本次提交真实设置 |
| [Descriptors.cpp](../../src/app/Descriptors.cpp) | set 0 的三个 IBL 和两个阴影采样绑定 | 将阴影两个绑定的 view 改为数组 view，保持比较/原始读取分工 |
| [shader.frag](../../assets/shaders/shader.frag) | 单图比较和 3×3 PCF | 按正视空间距离选级、数组采样、过渡和 cascade 着色 |
| [shadow.vert](../../assets/shaders/shadow.vert)、[shadow.frag](../../assets/shaders/shadow.frag) | 模型变换，以及保持 UV/alpha 的 Mask 深度绘制 | 顶点阶段选择本级矩阵，继续保留 Mask 和双面语义 |
| [shadow_debug.frag](../../assets/shaders/shadow_debug.frag) | 原始单图深度预览 | 预览用户选择的数组层 |
| [FrameTiming.hpp](../../src/diagnostics/FrameTiming.hpp)、[BaselineCapture.hpp](../../src/diagnostics/BaselineCapture.hpp) | 跟随提交保存配置、按 ID 配对采集 | 增加 CSM 配置，防止开关/参数变化后继续混采 |
| [PostProcessing.cpp](../../src/render/PostProcessing.cpp) | 渲染目标 payload 估算 | 当前拒绝 `arrayLayers != 1`；必须扩展，否则数组图会显示 N/A |

拟新增的 `ShadowCascades.hpp/.cpp`、`ShadowCascadeFrame`、`calculateCascadeSplits()` 等名称均为本文建议，**当前源码没有这些接口**。每组先建立数据约定，再接调用者，不需要重命名整个 Renderer。

## 3. 先写下数据和坐标契约

| 项目 | 第一版约定 | 为什么这样定 |
|---|---|---|
| 数量 | 固定四级；Single 有效级数为 1，CSM 为 4 | 先验证算法，不引入可变数组和 shader 编译变体 |
| 分割范围 | `n = camera.nearPlane`，`f = min(camera.farPlane, shadowDistance)`，要求 `0 < n < f` | 主相机可看得更远，但阴影可以只覆盖其中一段 |
| split 值 | 四个正的视空间远边界；最后一个为 `f` | 和 `-(view * worldPosition).z` 直接比较，不用非线性深度或到相机的欧氏距离 |
| 光方向 | 沿用现有方向光的光线传播方向，归一化后建立右手 light view | 避免把“朝向光源”和“光线方向”混用 |
| 深度 | 普通 Z，near→0、far→1，clear=1，比较沿用 LESS_OR_EQUAL | 本阶段不加入 reversed-Z；receiver bias 仍从比较参考深度中减去 |
| 资源 | 每个飞行帧槽独占一张 2D depth array，4 层、1 mip、1 sample | 帧槽 fence 保证安全复用；不按交换链图像数量分配 |
| 渲染 | 每次只绑定一层的 2D view；循环 1 或 4 次 | 不要求 shader 写 `gl_Layer`，也不要求 multiview feature |
| 采样 | 整体数组 view + 比较 sampler；预览用同一数组 view + 非比较 sampler | 比较值与原始深度的用途不同，不能混用 |
| 远端行为 | 超过 `f` 返回完全受光；收尾时可增加末级淡出 | 阴影覆盖范围是可解释的有限预算，不能越界选层 |

第一版建议始终分配四层，Single 只使用 layer 0；这样切换模式不需要重建资源。**这两种模式的实际阴影内存相同。** 画质/绘制成本的 A/B 可以直接做；若要测“一层和四层的真实分配差异”，最后再增加安全边界重建，或使用旧单图基线记录，不能把未使用的三层当成没有分配。

## 4. 提交 A：只计算四个 split，不碰 GPU

**目的：先验证距离分配，排除资源和 shader 干扰。这就是第一轮实际手敲范围。**

修改位置：拟新增 `src/render/ShadowCascades.hpp/.cpp`、`tests/ShadowCascadeMathTests.cpp`，并将数学测试加入 CMake。数学模块只依赖 C++/GLM；不要包含 Application、VulkanContext 或 GPU 资源头。

1. 在新头文件写固定数量和函数声明：

   ```cpp
   inline constexpr std::size_t shadowCascadeCount = 4;

   std::array<float, shadowCascadeCount> calculateCascadeSplits(
       float nearPlane, float shadowFar, float lambda);
   ```

   补上 `<array>`、`<cstddef>`。后面设置/UI/GPU 布局也使用这一数量约定，不到处散落字面量 `4`。

2. 实现时先拒绝非有限参数、`nearPlane <= 0`、`shadowFar <= nearPlane`、`lambda` 不在 `[0,1]` 的输入。数学层拒绝坏输入；UI 层之后再负责限制输入范围。

3. 对 `i=1..4` 计算：

   ```text
   t       = i / 4
   uniform = n + (f - n) * t
   log     = n * pow(f / n, t)
   split[i - 1] = (1 - lambda) * uniform + lambda * log
   ```

   使用 `double` 中间值，检查转换回 float 后仍有限、严格递增；最后一项明确写回 `f`。这里 `lambda=0` 是均匀划分，`lambda=1` 是对数划分，初始 UI 值可取 `0.5`，之后根据场景对比，而非宣称某个参数通用最优。混合划分见 [NVIDIA GPU Gems 3，第 10 章](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus)。

4. 先写与具体实现无关的边界测试：`n=1,f=100` 时，均匀结果约为 `25.75,50.5,75.25,100`；对数结果约为 `3.16228,10,31.62278,100`。再检查 lambda 中间值、严格递增、最后为 f，以及无效范围被拒绝。

5. CMake 新增 `shadow_cascade_math_tests` 目标，只编译数学模块和测试、链接 GLM。不要为了四个距离启动 Vulkan。运行该目标及 CTest，保存一次提交：

   ```bash
   cmake --build --preset linux-debug --target shadow_cascade_math_tests
   ctest --test-dir build/linux-debug -R shadow_cascade_math_tests --output-on-failure
   ```

**验收：** 所有数学输入输出与上述契约一致，原程序尚未改变画面。到这里停下来理解结果，再做下一组；本组不需要四张阴影图。

## 5. 提交 B：求视锥八角点，并在 CPU 检查

**目的：把“距离区间”变成真正的世界空间接收区域。**

修改位置：继续扩展拟新增的 `ShadowCascades` 模块与数学测试；之后可在 [Diagnostics.cpp](../../src/ui/Diagnostics.cpp) 显示四个距离和角点范围。

1. 每级名义区间为 `[n,split[0]]`、`[split[0],split[1]]` 等。先不加边界混合。
2. 用现有垂直 FOV 和 aspect 构造相机视空间角点。距离为 `d` 的平面上：

   ```text
   halfY = d * tan(verticalFov / 2)
   halfX = aspect * halfY
   corners = (±halfX, ±halfY, -d)
   ```

   近/远平面各四点，乘 `inverse(camera.view())` 得到世界角点。不要把世界 Z 当相机深度；项目的世界 Z 朝上，相机前方则是视空间 -Z。
3. 用 `camera.projection(aspect) * camera.view()` 回投影检查：八角点的 XY 应落在视锥边缘，深度应在相机的 `[0,1]` 内。`Camera::projection()` 已翻转 Y，不再加一次。等价的反投影方法应使用 NDC Z=0/1，再转换距离；不能把米制 split 直接塞进 NDC Z。
4. 测试相机平移、旋转、不同 aspect/FOV。对应角点的 `-viewZ` 应等于本级近/远距离；任意相机姿态都应保持有效。

可先输出 split、每级中心/半径和线框到现有 ImGui foreground draw list，明确颜色与 cascade 编号。线框只是观察工具，数学测试仍负责检查角点距离和范围。

**验收：** 四个接收区域与相机一起移动，分界沿相机深度而非世界高度排列，尚不更改阴影资源。

## 6. 提交 C：构造稳定光源矩阵，保留遮挡物范围

**目的：让每级有合理覆盖范围，并避免视角一转投影尺寸就收缩/膨胀。**

拟新增 CPU 输出可命名为 `ShadowCascadeFrame`，保存四个矩阵、split、每级世界 texel 尺寸和用于调试的包围区域。输入使用普通 Camera/GLM 参数和场景 AABB，不持有 Renderer 或 Image。

### C1. 先固定 XY 尺寸和网格原点

1. 对每个视锥切片取八角点平均中心 `C`，半径 `r=max(length(corner-C))`。在固定 FOV/aspect/split 条件下，先在视空间求半径并复用，避免用每帧旋转后的紧 AABB 尺寸决定缩放。
2. 用归一化光方向建立**仅旋转、以世界原点为参考**的 light basis `R`。方向接近世界 Z 时切换 up 参考轴，避免叉乘为零。不要每帧先 `lookAt(C-...,C)` 再对 light-space 的零中心做 snapping；那样网格原点仍会跟着相机移动。
3. 预留 PCF 和 snapping 的边缘余量。首版 3×3 PCF 可取 `g=2` 个 texel 的 guard：

   ```text
   H = r / (1 - 2*g/resolution)       // 要求 resolution > 2*g
   texelWorldSize = 2*H / resolution
   centerLS = R * vec4(C, 1)
   snappedXY = round(centerLS.xy / texelWorldSize) * texelWorldSize
   ```

   这是本文选择的保守包围球实现；guard 同时覆盖最近网格点舍入和 PCF 邻域，不是任意滤波核的通用常数。改变 PCF 半径时必须重新评估余量。

稳定范围与按 texel 量化位置是两项工作：只 snapping 位置，不能消除投影缩放带来的变化。相关原理见 [Microsoft：移动光源投影的 texel-sized increments](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps#moving-the-light-in-texel-sized-increments)。

### C2. Z 范围必须能容纳遮挡物

**接收区域在相机里，不表示所有投影到它的遮挡物也在相机里。** 第一版不做 caster 剔除，仍向每级提交全部物体；但如果 light near/far 已裁掉它们，提交再多也不会产生阴影。

先采用保守实现：将 `scene.bounds()` 得到的世界 AABB 八角点变换到 `R`，用其 Z 范围确定光源深度范围并加可记录的 margin。后续才按每级 XY 棱柱筛选物体/裁剪场景范围以收紧 Z；不能直接拿相机切片的近/远距离当 light near/far。仅按视锥 Z 拟合会裁掉部分 caster；整场景 Z 较保守但可能降低精度。[Microsoft：Near/Far 拟合与 caster 裁切](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps#calculating-the-near-plane-and-far-plane)

右手视图中可以这样检查符号，而非直接把 minZ/maxZ 传入 GLM：

```text
eyeZ = sceneMaxZ + margin + lightNear
V = translate(-snappedX, -snappedY, -eyeZ) * R
lightFar = eyeZ - sceneMinZ + margin
P = orthoRH_ZO(-H, H, -H, H, lightNear, lightFar)
P[1][1] *= -1
lightViewProjection = P * V
```

这里是对称 XY 正交投影，因此只翻转 `P[1][1]` 即可；若以后把中心移进非对称投影范围，Y 翻转还必须涵盖对应平移项，不能机械照抄。输入的 AABB、margin、near/far、矩阵全部检查有限且范围有效。

**验收：** 测试切片角点被 XY 范围包住、场景 caster 深度进入 `[0,1]`、固定参数旋转不改变半径、snapped 坐标是 texelWorldSize 的整数倍。在固定光方向/切片尺寸下，投影的 XY 网格只按 texel 步进；这不保证运动光源、Z 范围变化或任何观察路线完全无闪烁。增加“相机外高物体向可见地面投影”的运行用例，不能只测视锥里的立方体。

## 7. 提交 D：先接 CPU/GLSL 数据布局

**目的：让每次提交使用一致的矩阵、距离和模式，先保持 Single 显示正确。**

修改位置：`RenderSettings.hpp`、拟新增数学模块、`Buffers.cpp`、`ShaderTypes.hpp`，以及实际读取 Frame UBO 的 shader。

1. 增加 `ShadowMode { Single, Cascaded }` 和独立的 CSM 参数；`shadowDistance` 不等于 `camera.farPlane`。初始仍选 Single。每次填当前帧 UBO 时只计算一次 cascade 数据，不在每个 draw 重算。
2. 保留当前 UBO 前缀，在尾部追加拟定字段：

   | 拟新增 GPU 字段 | GLSL / C++ 对应 | 单位/含义 |
   |---|---|---|
   | `cascadeLightViewProjection[4]` | `mat4[4]` / `std::array<glm::mat4,4>` | 世界→各级光源裁剪空间 |
   | `cascadeSplits` | `vec4` | 四个正视空间距离，最后为 shadow far |
   | `cascadeTexelWorldSize` | `vec4` | 每级世界单位/texel，供调试和 bias 标定 |
   | `cascadeReceiverBias` | `vec4` | 四级深度比较 bias，已转为各自归一化深度单位 |
   | `cascadeInfo` | `uvec4` | x 有效级数 1/4，y 模式，z 预览层，w 预留 |

   `shadowParams.xy` 继续保存 UV texel 尺寸；过渡比例可为原 `shadowParams.w` 定义新语义。现有单图矩阵暂时保留，Single 时将相同矩阵写入数组第 0 项，避免同一次改动删除旧布局、变更采样和排查四级矩阵。
3. 以本次 1088 字节 UBO 为前提，追加字段的预期偏移分别是 `1088、1344、1360、1376、1392`，总大小 `1408`。**这是拟定布局，不是当前大小。** 加 `offsetof/sizeof` 断言，并在阶段 0.5 后按实际结构重算。`std140` 的向量/数组规则必须对应；C++ 断言不能替代 shader 端核对。[Khronos Shader Memory Layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html)
4. 同步更新 `shader.vert`、`shader.frag`、`shadow.vert` 中完整 UBO 声明，搜索其他使用者。只读取 view/proj 前缀的 skybox 不一定要复制整个结构，但不能误以为 GLSL 存在 C++ 那样自动共享的声明。UBO 分配大小和 descriptor range 均使用新的 `sizeof`，检查设备范围限制。
5. **不要再给 128 字节 `DrawPushConstants` 追加四个矩阵或一个独立字段。** 首版拟定 shadow 专用语义：复制 `object.pushConstants` 到局部值，把 `textureInfo.w` 写成当前 cascade index，再推送这份副本。主场景仍保持 w=0；shadow 顶点阶段读取数组中对应矩阵。x/y/z 的材质语义和 `shadow.frag` 的 alpha cutoff 保留。

**验收：** 新布局的 shader 全部编译，Single 画面和旧基线一致，无 descriptor range/UBO 布局错误；四级资源尚未接入时不宣称 CSM 可用。

## 8. 提交 E：每槽四层 depth array 与 view

**目的：明确资源所有权，以及“写一层、读整个数组”的区别。**

修改位置：阶段 0.5 后的 `ShadowMapping.cpp`、`Renderer.hpp`、`Descriptors.cpp`、`VulkanContext` 的 view 创建帮助函数及资源统计。

1. 每个 `ShadowTarget` 保持一张 `GpuImage`：extent=`R×R×1`、`arrayLayers=4`、mip=1、sample=1、depth attachment + sampled usage。沿用已支持的 D32/D16 选择，核对设备允许的格式用途、extent 和 array layer 数；PCF 首版继续 nearest 比较采样，不擅自打开格式未支持的线性过滤。
2. 为采样创建一个 `VK_IMAGE_VIEW_TYPE_2D_ARRAY`，范围为 mip 0、layer 0..3。为附件另外创建四个 `VK_IMAGE_VIEW_TYPE_2D`，每个 `baseArrayLayer=i,layerCount=1`。view 类型和子资源范围约定见 [Khronos VkImageViewCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)。
3. 当前 `VulkanContext::createImageView()` 能指定 layerCount，但 **baseArrayLayer 固定为 0**；直接循环调用旧函数会得到四个都指向第 0 层的 view。先给帮助函数增加明确的 base layer 参数/描述结构，保持已有调用默认行为，再创建逐层 view。
4. 整体数组 view 可以由 `GpuImage::setView()` 持有；额外四个附件 view 由 `ShadowTarget`/Renderer 显式持有并销毁。不要把同一个句柄交给两个独立所有者。GPU 完成使用后再销毁；数组 view、附件 view、image 的清理范围必须齐全，初始化失败也要回收已成功创建的部分。
5. set 0 现有两个阴影 combined-image-sampler 绑定分别换成“整体数组 view + compare sampler”和“整体数组 view + raw sampler”。这里一张数组图仍是一个描述符，**不是四个 descriptor**；只有采用 descriptor array 时才是另一套计数/索引约定。frame set 的五个图片 binding 不因四层而自动变成八个。
6. 修改 `estimateImagePayload()`：首版仍限定 mip=1、2D、已知格式，但允许多层并乘真实 `arrayLayers()`。按实际各帧目标求和，不再额外乘一次帧数。给一层、四层和未知格式补相应检查，避免资源面板退回 N/A。

**验收：** 图像元数据为 4 layers；四个附件 view 的 base layer 互不相同，整体 view 覆盖四层；无泄漏/重复销毁/不兼容 view 错误，Resources 正确显示分配形状与估算。

## 9. 提交 F：用 Dynamic Rendering 逐级录制

**目的：每层拿到自己的深度，保留原本的材质遮挡规则。**

修改位置：阶段 0.5 完成后的 `recordShadowPass()`、shadow pipeline 配置和 `shadow.vert`。本次基线仍有 `createShadowRenderPass()`/`createShadowFramebuffers()`，但本文主路径不在这套旧接口上新增四份 framebuffer。

1. Shadow pipeline 保留深度格式、单采样、无颜色附件、动态 viewport/scissor/bias。`VkPipelineRenderingCreateInfo` 中的格式必须和实际数组层格式相同。
2. 一次 Shadow 计时开始后，按有效数量循环。每次 `VkRenderingInfo` 设置 `viewMask=0,layerCount=1`，深度附件绑定对应的单层 view，clear=1、store=STORE；开始渲染，设置本级 bias 和索引，画完后结束。本版不使用多层一次绘制。[Khronos VkRenderingInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderingInfo.html)
3. 先对每级画全部有效物体，不做新 caster culling。继续选择原单面/双面 pipeline，绑定 set 1 的材质和完整 draw push 副本；`shadow.frag` 仍读取 base color alpha、顶点 alpha、所选 UV 和 cutoff。**不能为“深度 pass 简化”而删除 Mask fragment shader。**
4. 按阶段 0.5 的 Sync2 约定显式处理数组子资源：

   | 边界 | 生产者/消费者 | 需要表达的内容 |
   |---|---|---|
   | 复用前 | 上次 fragment 采样 → 当前深度附件使用 | 当前槽 fence 已完成；正确管理已有 layout，转换到 depth attachment 可用状态 |
   | 写入后 | early/late fragment tests 深度写 → Scene fragment 采样 | depth-stencil attachment write → shader sampled read，转为约定的 read-only layout |
   | 预览 | 同一深度结果 → Post 中的原始深度采样 | 也必须包含在可读范围内，descriptor 中的 layout 与实际一致 |

   明确 `baseArrayLayer/layerCount`。可以批量转换整个数组，也可以逐层转换，但不能仅转换 layer 0 后采样其他层。首次使用允许丢弃旧内容；之后若使用 `UNDEFINED`，必须说明是在覆盖并丢弃，不能靠它替代执行同步。[Khronos VkImageMemoryBarrier2](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier2.html)
5. 若 Single 只写 layer 0，整体采样 view 涉及的层也应按约定建立可读 layout，预览层限制为 0，未初始化的其他层不采样。转到 CSM 后各层先 clear 再画，不能展示上一模式遗留数据。
6. 原有 Shadow 时间戳包住全部级和相关转换；不要循环四次复用同一对 query slot。实际每次 `vkCmdDrawIndexed` 才累加 shadow draws/triangles。没有剔除且 14 个有效物体时，Single 预计 14 次、CSM 预计 56 次，不能仍显示对象数 14。

**验收：** 分别预览四层能看到不同覆盖范围；RenderDoc/可用工具中的附件层、clear、draw 数和矩阵对应；Single 仍可显示正确阴影，Mask 镂空和双面物体没有退化。

## 10. 提交 G：选择 cascade、PCF 和调试显示

**目的：先证明表面使用了正确的层，再调画质。**

修改位置：`shader.frag`、`shadow_debug.frag`、`RenderSettings.hpp`、`EditorUI.cpp`，及预览所需的少量 per-frame 数据传递。

1. 在片元世界位置上计算 `d=-(frame.view*vec4(worldPosition,1)).z`。两模式先共同处理 `d > shadowFar` 时返回 1；范围内 Single 固定 index=0，CSM 选第一个满足 `d <= split[i]` 的级。先显示四种固定颜色，观察近/中/远分区；不能以 `gl_FragCoord.z` 或 `length(worldPosition-cameraPosition)` 代替。
2. 使用本级矩阵得到 `lightNdc`。只有 XY 做 `uv=ndc.xy*0.5+0.5`，Z 保持 `[0,1]`，不得再 `z*0.5+0.5`。XY 越界或 Z 越界先返回 1，便于暴露覆盖问题。
3. shadow 比较声明改为 `sampler2DArrayShadow`，坐标是 `vec4(uv, float(index), referenceDepth)`；raw 预览改为 `sampler2DArray`，坐标是 `vec3(uv,float(previewLayer))`。数组层不是归一化 UV，也不是 mip。
4. **不能机械保留原来的比较 `textureLod()` 调用。** GLSL 的 `sampler2DArrayShadow` 没有对应的该重载。首版固定 1 mip、nearest、关闭 anisotropy 时，可以用下面的显式梯度比较，PCF 的九次 tap 各调用一次：

   ```glsl
   textureGrad(shadowMap,
       vec4(sampleUv, float(cascadeIndex), referenceDepth),
       vec2(0.0), vec2(0.0));
   ```

   这样避免在分级分支内依赖隐式导数；零梯度在本版 sampler 的 LOD 范围被约束到 mip 0。以后增加 mip/filter 时要重新设计梯度，不能把零梯度当通用滤波方案。重载核对见 [Khronos textureGrad](https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/gl4/textureGrad.xml)、[textureLod](https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/gl4/textureLod.xml)。
5. 保留原 3×3 PCF，UV offset 仍为 `(x,y)/resolution`；分辨率相同不表示四级滤波覆盖的世界宽度相同。预览用非比较采样读取原始 depth；正交深度本来线性对应本级 light near/far，但不同级的灰度不代表相同世界距离。
6. 增加 cascade 着色和 preview layer 控件，传递必须跟随当前帧。现有 `PostPushConstants` 的 `modes.w` 是可考虑的预览层位置，但目前没有此语义；使用前同步更新 `recordShadowPreview()` 的 push 与预览 shader，不能只改 UI。调试着色应绕过正常曝光/tone mapping，性能采集仍使用 Lit、关闭预览。

**验收：** 四色边界和 CPU split 一致，四层可独立预览，近处阴影比同覆盖范围的单图更细；保留 Albedo/Normal 等已有数据视图。若出现“只有 clear color 在变”，先检查片元分支是否写 outColor 并 return，以及 CPU 模式是否确实传入。

## 11. 提交 H：收尾 bias、接缝和稳定性

**目的：把“能分四层”收敛为相机移动时可用的阴影。**

1. 给各级保留可解释的 receiver bias。若用世界单位设置 `bWorld`，正交深度下可按 `bNormalized=bWorld/(lightFar-lightNear)` 转换；后者才是和 lightNdc.z 比较的量。可用世界 texel 尺寸作为调参参考，但不能把它直接当 raster constant bias。Raster constant/slope bias 继续按 Vulkan 的语义分别处理，不混称米制偏移。
2. 先观察 zero/小 bias，再记录抑制 acne 与避免悬浮之间的取舍；不能用巨大 bias 掩盖错误矩阵或 caster 裁切。
3. 增加 split 附近的过渡带。假设级 i 的末段 `[split[i]-w,split[i]]` 需要和 i+1 混合，**i+1 的拟合接收区必须向前覆盖这段**，否则第二次查询可能出界。先扩展角点计算的近边界、重新求稳定半径，再在 shader 混合两个 visibility，而不是混合原始深度或两个矩阵。[Microsoft：Cascade blending](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps#blend-between-cascades)
4. 每次只改一个因素：PCF、bias、过渡比例、稳定化开关；相机慢平移/旋转时分别观察。snapping 开启后投影可以按一 texel 步进，不应承诺静态截图就证明“完全稳定”。光方向改变和 FOV/aspect/split 改变会重建网格，单独记录。
5. 最后可增加 shadow distance 末端淡出；它解决远端突然消失，不能替代级间重叠。

**验收：** 记录同一条慢移动相机路线，近景细节提升、明显接缝消失、没有因相机外 caster 被裁掉而突然漏影。Mask 与双面用例单独验收；BLEND 仍按现有导入能力处理，本阶段不承诺新增透明阴影。

## 12. 提交 I：把 CSM 纳入采集和内存记录

**目的：让下一次算法变化仍可公平比较，不把不同配置的数据拼在一起。**

修改位置：`FrameTiming.hpp`、`RenderFrameData`、`Renderer::recordFrame()` 的配置快照、`BaselineCapture::matchesConfig()`、Diagnostics 与 payload 估算。

1. 在**录制/提交对应的配置副本**中增加 shadow mode、有效 cascade 数、shadow resolution、shadow distance、lambda、稳定化/过渡参数、PCF 和每级 bias。Single 的中心/半宽、caster margin/策略也记入；预览层变化应能识别。不能等 GPU 结果回来再读此刻 UI。
2. `matchesConfig()` 同步比较新增字段。采集中修改这些设置应取消；若更早在 UI 取消也可以，但仍保留快照检查。当前采集不完整跟踪相机姿态/物体/灯光/材质，继续人工固定，不能声称所有条件已自动锁定。
3. 保留 submissionId 配对、120 次提交预热和 300 行有效样本。CSV 至少通过旁置的配置记录或新增 metadata/配置文件明确 Single/CSM 条件；只靠文件名不能完整表达参数。若扩 CSV 列，保持原数值列和验证逻辑的一致性。
4. Shadow 记录总四级成本，Scene 反映采样成本；CPU Update/Record 也对比。首版不强求每级独立 GPU query；若以后增加，需同时扩大每槽 query 布局和读取逻辑，不能覆盖原六个位置。
5. 按实际格式计算名义 payload。以 D32、2048²、两飞行帧为例：旧单层为 32 MiB，四层为 128 MiB；D16 各减半。它们不是设备驻留显存。若本版始终分配四层，Single/CSM 的这项实际估算都应是 128 MiB，VMA allocation/block 另记。
6. 若确实需要运行时一层/四层内存 A/B，再单独增加“安全边界重建”提交：在无 active frame 时等待相关帧完成，建立新 image/view、更新所有槽 descriptor 并撤销旧资源；失败时保留可用旧资源。不要在 Shadows 面板内直接 reset 正在用的 image。

建议保存以下记录，空白项保持待验收：

| 条件 | Single | CSM 4 |
|---|---|---|
| commit、平台/GPU/driver、Debug/Release/Validation | 待记录 | 待记录 |
| 场景、相机/灯光、分辨率/MSAA、UI 状态 | 待记录 | 与左侧固定一致 |
| shadow distance、分辨率/实际 layers、PCF/bias | 待记录 | 待记录 |
| lambda、稳定化、过渡、caster 范围 | 不适用项明确标记 | 待记录 |
| Shadow / Scene / Post + UI GPU 均值及原始样本 | 待采集 | 待采集 |
| CPU Update / Record、实际 shadow draws/triangles | 待采集 | 待采集 |
| shadow payload、VMA allocation/block | 待记录 | 待记录 |
| 固定截图、慢移动视频/路线、关键帧 capture | 待保存 | 待保存 |

比较时固定两模式相同的 near/shadow far 和接收物体，记录 Single 的中心/半宽。旧 Single 是手动正交盒，CSM 是视锥分段拟合，不能把缩短 shadow distance 或裁掉部分接收物体当成 CSM 的精度收益；若要求严格控制整体投影覆盖，再补一套对整个 `[n,f]` 视锥统一拟合的单图对照，并单独标记，保留原 Single 参考。

先用 Shadow Playground；再用材质/Mask/双面用例检查语义，最后选 Sponza 等较大场景看成本和覆盖。当前 registry 将 Sponza 列为可选预设，**不代表本文已运行并完成整个场景验收**；不能照旧文档推断它必定不可用，也不能以“能选到”代替实测。

## 13. 收口与下一步

只有实际执行后才勾选：

- [ ] 阶段 0.5 主帧迁移完成，并保留正确性/生命周期回归记录。
- [ ] split、视锥角点、稳定范围和 snapping 的 CPU 测试通过。
- [ ] CPU/GLSL 布局、depth array、逐层 view 和 barrier 的子资源范围一致。
- [ ] Single/CSM 可切换；四级预览、cascade 着色和 Lit 结果匹配。
- [ ] 近景精度改善，慢平移/旋转无明显抖动或边界接缝；相机外 caster、Mask、双面用例通过。
- [ ] 对照采集配置完整，原始 CPU/GPU 样本配对，GPU 时间和实际内存形状均有记录。
- [ ] resize、最小化/恢复、场景切换、模式切换、启动失败清理和退出按实际平台运行检查，无未解释的 Validation/同步错误。
- [ ] 保存一次关键帧工具检查；没有执行的工具/平台仍注明待验收。

本阶段完成后再按 [大纲的阶段 2](overview.md#6-阶段-2deferred-rendering-与多光源剔除) 进入 Deferred/G-buffer。现在的第一步仍是完成阶段 0.5；轮到本文时，从 **提交 A 的四个 split 和纯 CPU 测试** 开始，不一次敲完九组。
