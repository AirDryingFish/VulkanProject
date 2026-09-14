# 阶段 6：方向光与阴影实施指南

本文对应 [开发路线图](overview.md) 的 Stage 6。目标是在现有点光源、IBL 和 glTF 静态 PBR 场景上增加一盏方向光，以及一套可调试、无跨帧资源冲突的 shadow map。

评估日期：2026-09-14。Stage 5 功能基线：`39416b0`（`Complete gltf upload and display`）；Linux loader 修复基线：`5ccc85c`（`Fix link error`）。本文是后续手敲指南，文中的 Stage 6 新接口、字段和 shader 尚未实现。

## 1. Stage 5 是否已经完成

结论：**核心功能链路已经实现，可以准备 Stage 6；尚不能把 Stage 5 的全部验收项目标为完成。** 区分“代码存在”“运行验证通过”和“完整验收通过”，不要按 commit 标题打勾。

| 项目 | 当前代码证据 | 本次结论 |
|---|---|---|
| glTF/GLB、accessor、图片解码 | `src/GltfLoader.cpp`、`src/ImageDecode.cpp` | 已有实现 |
| 材质五槽、packed MR、UV 选择和 sampler | `src/Material.hpp`、`src/ImageResources.cpp`、`assets/shaders/shader.frag` | 已有实现 |
| GPU 图片/mesh 复用、材质绑定 | `src/Model.cpp::addGltfMeshObjects()` | 已接入代码路径 |
| scene 选择、Node 层级、Y-up→Z-up | `src/Model.cpp` 的 Node 遍历；loader 保存 `localTransform` | 已有实现，仍需多节点参考场景验收 |
| 奇异/镜像矩阵、缺失 UV、动画/skin/morph 限制 | importer 的显式检查 | 已有拒绝路径 |
| 导入事务、descriptor 容量 | `GltfMaterialUpload`、`ensureMaterialDescriptorCapacity()`、批量分配和回滚 | 已有实现，故障路径仍需实测 |
| fixture 测试 | `tests/GltfLoaderTests.cpp` | 覆盖 Triangle、无索引、Box/Interleaved、Sparse、外部/data URI 图片；没有完整 Node/材质/交互覆盖 |
| Debug/Release 构建 | 初次检查均在 vcpkg 配置阶段因系统工具缺失失败；随后已有可执行文件及 loader 修复 | 初次失败作为历史记录保留；当前提交的完整构建与测试结果仍需记录 |
| Linux Vulkan loader | `5ccc85c` 增加 `BUILD_RPATH`；当前 Debug 的 RUNPATH 和实际库路径已检查 | Debug 优先加载系统 loader；Release 当前也解析到系统 loader，但未观察到 RUNPATH，需重建确认 |
| Validation、resize、重复导入、参考外观 | 已诊断 surface 创建问题，系统 loader 下曾验证 surface 创建成功 | 不等于完整渲染/交互验收，仍待记录 |

### 1.1 先恢复可复现构建

首次检查时，`nativefiledialog-extended` 引入的依赖安装过程在 `libxcrypt` 构建中报告缺少系统工具。新环境遇到同一问题时，在 Ubuntu 上安装下列软件包；已经安装则无需重复处理：

```bash
sudo apt update
sudo apt install autoconf autoconf-archive automake libtool
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
cmake --preset linux-release
cmake --build --preset linux-release --parallel
```

这不是已确认的 C++ 编译错误。完成依赖安装后可能暴露其他环境或代码问题，以实际日志为准。不要把旧 build 目录里的可执行文件当作当前提交已通过的证据，也不要为绕过错误随意更新 vcpkg baseline。

### 1.2 修复 Linux Vulkan loader 的运行时搜索路径

依赖构建通过后，曾出现：

```text
glfwCreateWindowSurface(...) returned VK_ERROR_EXTENSION_NOT_PRESENT (-7)
```

已定位原因：CMake 的 `Vulkan_LIBRARY` 虽然指向系统 loader，但 Debug 可执行文件原来的 RUNPATH 优先指向 vcpkg 的 `debug/lib`，运行时加载了该目录的同名 `libvulkan.so.1`。这份 loader 的 `BUILD_WSI_XLIB_SUPPORT`、`BUILD_WSI_XCB_SUPPORT` 和 `BUILD_WSI_WAYLAND_SUPPORT` 均为 OFF；GLFW 没有得到创建 X11 surface 所需的扩展。日志里的 gfxstream 提示在切换 loader、surface 创建成功时仍然出现，不能把它直接判定为本次失败原因。

修复需要同时处理链接与运行时搜索：保留现有 `find_library(VULKAN_PROJECT_SYSTEM_VULKAN_LOADER ...)` 和 `Vulkan_LIBRARY` 设置，在 `target_link_libraries(vulkan ...)` 后设置目标属性。当前 `5ccc85c` 已包含以下代码，**不要重复添加**：

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    get_filename_component(
        VULKAN_PROJECT_SYSTEM_VULKAN_DIR
        "${VULKAN_PROJECT_SYSTEM_VULKAN_LOADER}"
        DIRECTORY
    )

    set_property(
        TARGET vulkan
        PROPERTY BUILD_RPATH
        "${VULKAN_PROJECT_SYSTEM_VULKAN_DIR}"
    )
endif()
```

`BUILD_RPATH` 为 build tree 的程序增加运行时库搜索目录，并保留 CMake 自动生成的依赖路径。这里从找到的系统 loader 提取目录，不把机器架构写死。最终顺序以生成的 ELF 为准。[CMake BUILD_RPATH 文档](https://cmake.org/cmake/help/latest/prop_tgt/BUILD_RPATH.html)

修改后依次重新配置和链接两个 preset，然后检查：

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel
cmake --preset linux-release
cmake --build --preset linux-release --parallel

readelf -d build/linux-debug/vulkan | rg 'RPATH|RUNPATH'
readelf -d build/linux-release/vulkan | rg 'RPATH|RUNPATH'
env -u LD_LIBRARY_PATH ldd build/linux-debug/vulkan | rg 'libvulkan'
env -u LD_LIBRARY_PATH ldd build/linux-release/vulkan | rg 'libvulkan'
```

当前 Debug 已观察到 `/usr/lib/x86_64-linux-gnu` 位于 vcpkg 目录之前，移除 `LD_LIBRARY_PATH` 后仍解析到系统 `libvulkan.so.1`。Release 当前解析到 `/lib/x86_64-linux-gnu/libvulkan.so.1`，但 `readelf` 未显示 RPATH/RUNPATH；这个结果只能证明该文件当前使用系统 loader，不能证明它已重建并带上新属性。

验收时两个程序都不应解析到 `vcpkg_installed/.../libvulkan.so.1`。Ubuntu 上 `/lib/x86_64-linux-gnu` 与 `/usr/lib/x86_64-linux-gnu` 可能指向同一系统库。接着直接运行并检查完整启动、绘制和退出：

```bash
env -u LD_LIBRARY_PATH ./build/linux-debug/vulkan
env -u LD_LIBRARY_PATH ./build/linux-release/vulkan
```

临时诊断也可以使用 `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./build/linux-debug/vulkan`，但它只证明切换搜索路径有效，不代替持久修复验收。`LD_LIBRARY_PATH` 通常优先于 RUNPATH；不要在 shell 配置中长期插入 vcpkg 的 loader 路径，不要删除 vcpkg 的库或因此重装显卡驱动。未来安装/打包时单独设计 `INSTALL_RPATH`，本配置针对 build 目录直接运行。

### 1.3 保留当前对象变换约定

当前 `Buffers.cpp::getObjectMatrix()` 大致计算：

```text
T_user * assetTransform * R_user * R_auto * S_user
```

这与当前 Stage 5 指南第 5.3、14.4 节一致：用户 rotation/scale 围绕 primitive 的 Node local origin 工作，不会把 Node 原有平移一起缩放或绕资产原点旋转。不要误改成另一种语义：

```text
userTransform * assetTransform
= T_user * R_user * R_auto * S_user * assetTransform
```

两者在有旋转/非均匀缩放的节点上并不等价。Stage 6 不改变现有编辑语义；若以后整体变换一次导入的场景，应单独设计共享 import-root。用带旋转父节点的资产验证旋转、缩放、拾取和 Gizmo；不要只用单位矩阵的 Box 验收。Stage 6 两个 pass 必须使用同一个最终 model 矩阵。

### 1.4 进入阴影开发前的最小门槛

- [ ] 当前提交 Debug/Release 构建通过，CPU tests 通过。
- [ ] 两个 preset 无需临时 `LD_LIBRARY_PATH` 即可使用系统 Vulkan loader，正常创建窗口 surface。
- [ ] Box、DamagedHelmet 和一个多 Node/多 primitive 场景能导入；记录外观和资源计数。
- [ ] 按上述已有变换语义验收；主绘制与拾取一致。
- [ ] 重复导入、删除、错误路径和容量超限后，原场景仍可绘制。
- [ ] resize、最小化恢复和正常退出没有新增 Validation 错误。
- [ ] 补齐 `DamagedHelmet` 的来源、revision 和许可证记录；文件存在不等于来源记录完整。

将结果写到 Stage 5 验收记录。Stage 5 的 P1（例如生成 tangent、更完整统计）以及动画、透明、压缩纹理等延期项不必全部完成才开始 Stage 6。Stage 6 首个“无阴影方向光”提交可在基线恢复后先做；多 pass 阴影应在上述门槛通过后进行。

## 2. 范围与可见成果

按三个可见里程碑推进：

1. 关闭点光源后，一盏方向光能照亮模型；改变方向能移动明暗分界。
2. Cube/模型能在地面上投下硬阴影，并随物体移动。
3. 3×3 PCF、depth bias 和深度预览可用，开关阴影不改变其他光照项。

P0：一盏方向光、固定正交视域、固定 2048×2048 shadow map、单采样深度、硬阴影、3×3 PCF、bias、深度预览、跨帧同步和清理。

延期：CSM、点光源阴影、透明/alpha-mask 投影、动态分辨率、摄像机视锥自动拟合、稳定化 texel snapping、Render Graph、异步队列。HDR 主目标和后处理属于 Stage 7，本阶段保留现有 tone mapping。

## 3. 当前工程的接入点

| 文件 | 本阶段职责 |
|---|---|
| `VulkanTypes.hpp` | 方向光 CPU 参数、Frame UBO 字段及布局断言 |
| `TriangleApplication.hpp`、`ImGuiLayer.cpp` | 方向光/阴影配置、UI、基准场景 |
| `Buffers.cpp::updateUniformBuffer()` | 方向归一化、lightVP、每帧 UBO 上传 |
| `RenderTypes.hpp` | shadow pass 开关、bias 等绘制快照；保持 128 字节 draw push constants |
| `Renderer.hpp`、`Renderer.cpp` | 阴影资源所有权、每帧录制顺序和销毁 |
| 新建 `ShadowMapping.cpp` | Renderer 的 shadow image/render pass/framebuffer/pipeline 实现 |
| `GraphicsPipeline.cpp` | Frame layout 增加阴影 binding；主 pipeline 继续使用原有 RenderPass |
| `Descriptors.cpp` | 每帧阴影描述符与 pool 容量 |
| 新建 `assets/shaders/shadow.vert` | depth-only 几何绘制 |
| `assets/shaders/shader.frag` | 方向光 BRDF、shadow visibility、PCF |
| 新建 `shadow_debug.vert/.frag` | 在主 pass 内绘制小窗口深度预览 |
| `AppConfig.hpp`、`CMakeLists.txt`、shader 编译脚本 | shader 路径、源文件与 SPIR-V 构建 |

不要把 fastgltf 类型放入 Renderer。它继续消费 `RenderObjectView` 的 buffer、index count 和最终 model 矩阵。

## 4. 提交 1：先让方向光照亮模型

提交名：`Add directional light to PBR shading`

### 4.1 CPU 配置

在 `VulkanTypes.hpp` 的 `PointLight` 附近加入：

```cpp
struct DirectionalLight
{
    glm::vec3 direction{-1.0f, -1.0f, -2.0f};
    glm::vec3 color{1.0f};
    float intensity = 3.0f;
    bool enabled = true;
};
```

约定 `direction` 为光线传播方向（光源→场景），shader 中表面指向光源的 `L = -normalize(direction)`。方向光不做距离衰减。CPU 拒绝非有限方向；长度过小时保留上一有效方向或使用固定有效默认值，不能 `normalize(0)`。

在应用里持有 `DirectionalLight directionalLight;`。UI 提供 enabled、方向、颜色、非负 intensity。不要把 CPU `bool` 直接复制到 std140。

### 4.2 UBO 追加字段

当前 UBO 为 960 字节，`pointLights` 从 offset 192 开始。先在数组后追加：

```cpp
alignas(16) glm::vec4 directionalDirectionEnabled{};
alignas(16) glm::vec4 directionalColorIntensity{};
```

XYZ 分别为归一化方向/线性颜色，W 分别为 enabled（0/1）和强度。新增 offset 为 960、976，总大小 992。增加 `offsetof` 和 `sizeof` 断言，保留旧字段偏移。

同步修改实际参与主绘制的 `shader.vert` 与 `shader.frag` std140 block；`skybox.vert` 只读取原有 view/proj 前缀，可保留前缀声明。UBO 分配、memcpy 和 descriptor range 均使用更新后的 `sizeof(UniformBufferObject)`。编译脚本并不编译旧 `shader_rast.*`，不要误改成另一条渲染路径。

### 4.3 Shader 与第一次验收

复用当前 GGX distribution、Smith geometry 和 Fresnel 公式，方向光使用相同 metallic/roughness/normal。建议提取公共的直接光 BRDF 函数，让点光源循环与方向光调用它，不复制出会分叉的两套 PBR。

先实现 `LoDirectional = BRDF * color * intensity * max(dot(N,L),0)`。enabled=false 时贡献为零。IBL、AO、Emissive 保持原来的意义。

手动验证：关闭点光源、ambient 和 IBL；调整方向，确认明暗分界移动；关闭方向光后仅剩 Emissive。打开 IBL，确认两者能叠加。本提交不创建阴影资源。

## 5. 提交 2：固定视域与光空间矩阵

提交名：`Define directional shadow projection`

先选一个固定测试区域，例如世界原点附近、半宽 10、光源距离 20、near=0.1、far=50；这些是实验初值，不是任意资产的保证范围。正交视域外定义为“未覆盖，按可见处理”。

```cpp
// 已检查 direction 非零且有限，halfExtent/near/far 合法。
const glm::vec3 d = glm::normalize(directionalLight.direction);
const glm::vec3 target{0.0f};
const glm::vec3 eye = target - d * 20.0f;
const glm::vec3 up = std::abs(glm::dot(d, glm::vec3(0, 0, 1))) > 0.99f
    ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
const glm::mat4 lightView = glm::lookAtRH(eye, target, up);
glm::mat4 lightProj = glm::orthoRH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 50.0f);
lightProj[1][1] *= -1.0f;
const glm::mat4 lightVP = lightProj * lightView;
```

显式包含 GLM 的 matrix transform/clip space 头文件。当前应用采用 Vulkan Z∈[0,1] 和投影 Y 翻转；shadow pass 采用相同约定、正 viewport height，采样时 XY 映射 `ndc.xy * 0.5 + 0.5`，Z 不再乘 0.5 加 0.5，也不再次翻转 V。

在提交 1 的 UBO 后追加：

```cpp
alignas(16) glm::mat4 lightViewProjection{1.0f}; // offset 992
alignas(16) glm::vec4 shadowParams{};          // offset 1056
alignas(16) glm::ivec4 shadowFlags{};          // offset 1072
```

最终大小 1088。`shadowParams` 约定 x/y=1/分辨率、z=receiver bias、w=预留；`shadowFlags` 约定 x=enabled、y=深度预览、z=PCF（0/1）、w=预留。constant/slope raster bias 放到 `RenderFrameData` 的阴影快照，不混入这两个 vec4。`DrawPushConstants` 保持 128 字节，model 仍位于 offset 0。

矩阵在 `beginFrame()` 成功、对应帧 fence 已等待后计算并写入当前帧 UBO。lightVP 与两遍绘制使用同一帧场景快照，不能 shadow pass 再推进一次 autoRotation。

## 6. 提交 3：创建每帧阴影资源

提交名：`Create per-frame directional shadow targets`

### 6.1 所有权

沿用 Renderer 的 `FrameContext`，为每个 frame slot 分配一份深度 image 和 framebuffer：

```cpp
struct ShadowTarget
{
    GpuImage depth;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};
// Renderer 成员：
std::array<ShadowTarget, MAX_FRAMES_IN_FLIGHT> shadowTargets_{};
VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
GpuSampler shadowCompareSampler_;
GpuSampler shadowPreviewSampler_;
```

取资源用 `FrameToken::frameIndex`，不是 swapchain image index。每个 frame slot 的上次读写完成后才复用其 shadow image。第一版不把所有在飞帧绑到同一张可写 shadow image。

Renderer 暴露只读 view/sampler 查询方法给 `Descriptors.cpp`，检查索引范围。应用不复制资源所有权。固定分辨率 shadow targets 不随窗口 extent 重建；暂不提供运行时改分辨率，避免额外的 descriptor 更新与延迟销毁问题。

### 6.2 格式与资源参数

优先检查 `VK_FORMAT_D32_SFLOAT`，备选 `VK_FORMAT_D16_UNORM`；要求 optimal tiling 同时支持 depth attachment 和 sampled image，并检查使用的 API/设备是否支持深度比较采样。不假定 swapchain depth 格式天然适合 shadow sampling。失败时返回清楚的 unsupported error。[格式能力说明](https://docs.vulkan.org/spec/latest/chapters/formats.html)

Image：2048×2048×1、mip=1、layer=1、sample=`VK_SAMPLE_COUNT_1_BIT`、usage=`DEPTH_STENCIL_ATTACHMENT_BIT | SAMPLED_BIT`。view 明确使用 `VK_IMAGE_ASPECT_DEPTH_BIT`，不要依赖 helper 默认 color aspect。复用 `GpuImage`/VMA，不新增另一种图像所有权封装。

comparison sampler：NEAREST min/mag/mipmap、compareEnable=true、compareOp=LESS_OR_EQUAL、CLAMP_TO_BORDER、FLOAT_OPAQUE_WHITE、minLod=maxLod=0、anisotropy=false。先用 NEAREST 保证一次查询是一个比较，再显式实现 3×3 PCF。

preview sampler：同样参数但 compareEnable=false。深度预览读原始 R 通道，不能用 comparison sampler 代替。[比较采样说明](https://docs.vulkan.org/spec/latest/chapters/textures.html)

### 6.3 RenderPass

仅一个 depth attachment：CLEAR、STORE；stencil DONT_CARE；clear depth=1。subpass 的 colorAttachmentCount=0，depth layout 为 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`。initialLayout=`UNDEFINED`（每次丢弃旧内容），finalLayout=`DEPTH_STENCIL_READ_ONLY_OPTIMAL`。

不要把 `UNDEFINED` 理解为无需同步。按当前 legacy RenderPass API 添加两条 dependency，第一版不用 BY_REGION 限制阴影读取范围：

| dependency | src stage/access | dst stage/access |
|---|---|---|
| EXTERNAL → shadow subpass | FRAGMENT_SHADER / SHADER_READ | EARLY + LATE_FRAGMENT_TESTS / DEPTH_STENCIL_ATTACHMENT_READ + WRITE |
| shadow subpass → EXTERNAL | EARLY + LATE_FRAGMENT_TESTS / DEPTH_STENCIL_ATTACHMENT_WRITE | FRAGMENT_SHADER / SHADER_READ |

上一帧 slot 已由 fence 保证完成；上表还表达了 pass 间的读写与 layout transition 顺序。另一种做法是显式 barrier，但不要与自动 transition 混用出冲突的 oldLayout。本指南选择 RenderPass dependency。官方例子使用 synchronization2，当前项目采用对应 legacy stage/access mask，不为阴影顺便升级同步 API。[深度写入到采样的同步例子](https://docs.vulkan.org/guide/latest/synchronization_examples.html)

## 7. 提交 4：录制 depth-only pass

提交名：`Render scene geometry into directional shadow maps`

当前 `GraphicsPipelineConfig` 固定两个 shader stage、一个 color attachment、主 renderPass 和主 MSAA。不能原样拿来创建阴影 pipeline。第一版在 `ShadowMapping.cpp` 写专用创建函数，复用 shader module helper 即可。

参数：仅 vertex stage；Vertex binding stride 保持现有 `sizeof(Vertex)`，attribute 只需 location 0 POSITION；triangle list；sample=1；color attachment count=0；depth test/write=true；LESS_OR_EQUAL；第一版 cullMode=NONE，避免错误地靠 front-face culling 掩盖薄片问题；动态 viewport、scissor、depth bias。

`shadowPipelineLayout_` 使用同一个 Frame set layout（set 0），push constant range 仅 vertex、offset=0、size=64。shadow shader 的 UBO 前缀与主 UBO 精确一致，或显式声明经过核对的 std140 offset；不要只写一个 lightVP 放在 offset 0。

`shadow.vert` 的核心：

```glsl
layout(location = 0) in vec3 inPosition;
layout(push_constant) uniform ShadowDraw { mat4 model; } draw;
// frame.lightViewProjection 来自 offset 992 的 Frame UBO 字段。
void main()
{
    gl_Position = frame.lightViewProjection * draw.model * vec4(inPosition, 1.0);
}
```

在 `Renderer::recordFrame()` 的主 render pass 之前录制 shadow pass：清深度→绑定 shadow pipeline 和当前帧 set 0→设置 2048 viewport/scissor/bias→遍历同一 `RenderObjectView` 数组，绑定 vertex/index、推送 `object.pushConstants.model`、`vkCmdDrawIndexed()`→结束 pass。

不要绘制 Skybox、ImGui 或纯光源可视化几何。初版所有不透明场景 mesh 都投影；为特殊对象增加显式 `castsShadow` 时，把布尔值放到绘制快照，而不是让 Renderer 查询 SceneObject。

进入主 pass 后重新设置 swapchain viewport/scissor、主 pipeline、descriptor 与完整 draw push constants。前一个 pass 的动态状态不会自动恢复。

首帧与阴影关闭时仍执行空的 clear pass（关闭时不画 mesh），确保深度为 1 且进入可采样 layout；shadowFlags.x=false 让主 shader 返回 visibility=1。这版开关跳过阴影 draw，但仍有 clear pass 的固定成本。

验收：用 RenderDoc 或后续深度预览观察物体轮廓；若只有全白/全黑，先查 lightVP、裁剪、position stride 和 depth clear，不先调 PCF。

## 8. 提交 5：Frame descriptor 与硬阴影

提交名：`Sample directional shadows in PBR shading`

### 8.1 Descriptor 对应关系

| set/binding | 内容 |
|---|---|
| 0/0 | Frame UBO |
| 0/1、0/2、0/3 | irradiance、prefilter、BRDF LUT（原有） |
| 0/4 | 本帧 shadow depth + comparison sampler |
| 0/5 | 本帧 shadow depth + non-comparison preview sampler |
| 1/0～4 | 五槽 Material，不变 |

本提交同时预留并写入 preview binding，`frameImageDescriptorCount` 从 3 改为 5，Frame layout 一共 6 个 binding。更新 descriptor write 数组与 pool 容量：`frameCount * (frameImageDescriptorCount + skyboxImageDescriptorCount) + maxMaterialCount * materialImageDescriptorCount`。检查所有硬编码 `3`/`4`；只改常量不会自动填好两个新增 imageInfo。

Frame set[i] 同时引用 UBO[i] 和 shadowTargets_[i]。两条阴影 descriptor 都声明 `DEPTH_STENCIL_READ_ONLY_OPTIMAL`。预先写 descriptor 可以，但实际采样时图像必须已经处于声明的 layout。[Descriptor layout 要求](https://docs.vulkan.org/spec/latest/chapters/descriptors.html)

### 8.2 先采样一次

```glsl
layout(set = 0, binding = 4) uniform sampler2DShadow shadowMap;

float directionalVisibility(vec3 worldPosition)
{
    if (frame.shadowFlags.x == 0) return 1.0;
    vec4 clip = frame.lightViewProjection * vec4(worldPosition, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (ndc.z < 0.0 || ndc.z > 1.0 ||
        any(lessThan(uv, vec2(0.0))) ||
        any(greaterThan(uv, vec2(1.0)))) return 1.0;
    float referenceDepth = ndc.z - frame.shadowParams.z;
    return texture(shadowMap, vec3(uv, referenceDepth));
}
```

只对方向光直接光照乘 visibility：`Lo = LoPoint + visibility * LoDirectional`。不要把阴影乘到最终颜色、IBL、ambient 或 emissive 上。表面即使处于太阳阴影中，也可能被环境光和点光源照亮。

阶段验收场景：一个大而薄的 Cube 作地面，一个抬高的 Cube，一个 Sphere，以及一个 glTF 模型。地面位于模型下方，所有物体在固定 shadow volume 内，关闭 autoRotate 先验证静止结果。确认移动物体时阴影同步移动；直接使用主 pass 的最终 model，不能在 shadow pass 重算或漏掉 assetTransform。

## 9. 提交 6：bias、PCF 和深度预览

提交名：`Add shadow bias controls and depth preview`

### 9.1 Bias

pipeline `depthBiasEnable=true`，动态状态包含 DEPTH_BIAS；每次绑定阴影 pipeline 后调用 `vkCmdSetDepthBias(constant, 0.0f, slope)`。clamp 固定为零，避免依赖 depthBiasClamp feature。

实验初值：constant=1.25、slope=1.75、receiverBias=0.0005。它们不是通用正确值；格式、投影视域和深度范围会改变效果。先 receiverBias=0，仅调 raster bias，必要时再加小的 receiver bias，避免两者同时过量导致悬浮。

UI 分别显示这三项。receiver bias 是归一化光空间深度单位，不是世界坐标米数。不能通过把 roughness 调大、关闭法线贴图来掩盖 shadow acne。

### 9.2 3×3 PCF

在已有裁剪检查后，把单次比较改成 9 次比较求平均；offset 使用 `frame.shadowParams.xy`，不要使用窗口尺寸：

```glsl
float visibility = 0.0;
for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
        visibility += texture(shadowMap,
            vec3(uv + vec2(x, y) * frame.shadowParams.xy, referenceDepth));
return visibility / 9.0;
```

用 `shadowFlags.z` 在单次比较和 PCF 之间切换。比较采样返回可见度，PCF 平均的是比较结果，不是先平均深度再比较。采样器维持 NEAREST，避免把硬件过滤与 3×3 核叠加后无法解释软化范围。

### 9.3 深度预览

使用 `shadow_debug.vert/.frag`：fullscreen triangle，通过 `gl_VertexIndex` 生成三个顶点；fragment 从 set 0/binding 5 的 `sampler2D` 读取 `.r`，输出灰度。正交深度与光空间距离线性相关，可直接显示 0～1，初版不套用透视深度线性化公式。

在现有主 render pass 的场景/天空绘制之后、ImGui 之前，设置右下角小 viewport/scissor 画三角形。debug pipeline 使用主 renderPass、主 MSAA、depthTest=false、depthWrite=false、cullMode=NONE；绘制后恢复窗口 viewport/scissor。这样暂不引入 ImGui AddTexture/RemoveTexture 的 descriptor 所有权问题。

预览也使用当前 frame slot 的 set 0，而不是上一帧深度。resize 只改变预览矩形与主 framebuffer，不销毁 shadow image。

## 10. 生命周期与初始化顺序

建议顺序：Renderer 创建 Frame layout → 主 pass/pipeline 与帧上下文 → 阴影 pass、targets、samplers、pipeline → 应用创建 UBO/pool → 写 Frame descriptors → 正常绘制。深度预览 pipeline 依赖主 renderPass，应与主 pipeline 同步重建。

正常绘制：等待当前 frame fence → 更新当前 UBO → shadow pass → 主 pass（方向光采样）→ 深度预览 → ImGui → submit/present。不要每帧 `vkDeviceWaitIdle()`，不要每帧走 `immediateSubmit()` 单独提交阴影。

清理：停止提交并等待设备/帧完成 → 释放引用阴影资源的应用 descriptor pool → 销毁 shadow/debug pipeline、pipeline layout → 销毁每个 shadow framebuffer → reset 深度 image/view → reset sampler → 销毁 shadow renderPass → 继续原 Renderer/context 清理。保证初始化中途失败也能走同一路径，所有裸句柄初始化为空并销毁后清空。

阴影 target 固定尺寸，不加入 Swapchain 的 framebuffer 数组。窗口 surface format 改变时，debug pipeline 需要随主 pass 重建，shadow depth pass 不依赖 surface format。改变 shadow resolution 属于后续任务：等待在用帧、事务式创建新 targets、更新所有引用它的 descriptors，再释放旧资源。

## 11. 每个提交的构建与验收

新增 `ShadowMapping.cpp` 时加入 CMake 的 `vulkan` target；新 shader 路径在 `AppConfig.hpp` 使用现有 `assetPath()`。同步 Linux/macOS 的 `scripts/compile-shaders.sh` 和 Windows 的 `.bat`，例如增加 `shadow.vert → shadow.vert.spv`、debug 两个 shader；只有创建了对应源码后才增加编译命令。

```bash
sh scripts/compile-shaders.sh
cmake --build --preset linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
./build/linux-debug/vulkan
cmake --build --preset linux-release --parallel
git diff --check
```

不要求每个参数改动都新增测试文件。CPU lightVP 测试只覆盖有意义的边界：零方向、方向平行 up、near/far、已知点的投影范围。图像同步与阴影质量必须靠运行时验证，CPU tests 不能替代它们。

| 操作 | 期望 |
|---|---|
| 关闭方向光 | 方向光直接贡献为零，点光源/IBL/Emissive 不受影响 |
| 关闭阴影 | 方向光仍照亮模型，visibility=1 |
| 移动物体/改变方向 | 阴影与几何和光方向同步 |
| 移动相机但不动物体 | 固定 lightVP 下 shadow map 不跟着相机漂移 |
| 方向接近 Z 轴 | 自动选择有效 up，无 NaN/突然消失 |
| 物体离开 shadow volume | 明确按未覆盖处理，不重复平铺阴影 |
| 开关 PCF、调 bias | 能观察锯齿/acne/peter-panning 的可解释变化 |
| 多帧、resize/minimize/restore | 无 depth read/write hazard、失效 descriptor 或 framebuffer 错误 |
| 删除投影对象、重复导入 glTF | 阴影与主场景使用同一份有效绘制快照 |
| 初始化失败、正常退出 | 无重复销毁/仍在使用资源的 Validation 错误 |

## 12. 完成标准与常见故障

- [ ] 单方向光参数与直接 PBR 生效，默认方向约定一致。
- [ ] 每帧独立 depth target，frameIndex 与 descriptor/UBO 一致。
- [ ] depth-only pass、最终 model、光空间投影和读取 layout 正确。
- [ ] 不透明模型与地面可投射/接收阴影，光照开关语义正确。
- [ ] PCF、两种 raster bias、receiver bias 和原始深度预览可用。
- [ ] Stage 5 资产导入和现有点光源/IBL 未回归。
- [ ] Debug/Release、Validation 和窗口交互验收有记录。

| 现象 | 优先检查 |
|---|---|
| 全白深度 | 没有 draw、视域之外、position stride、裁剪和深度写入 |
| 全黑阴影 | 深度 clear、比较方向、Z 被错误映射两次 |
| 阴影上下颠倒 | 投影 Y 翻转与采样 V 是否重复处理 |
| 物体和阴影分离 | 两个 pass 的 model/assetTransform 或帧快照不同 |
| 随机闪烁 | 多帧共用目标、frameIndex/imageIndex 混用、读写依赖遗漏 |
| 阴影区域连自发光都变暗 | visibility 错乘到最终颜色 |
| bias 很大仍有问题 | 先看投影范围和深度精度，不继续盲加 bias |

验收记录模板：日期、commit、GPU/驱动、Debug/Release、场景/参数、Validation 结果、关闭阴影/硬阴影/PCF/深度预览截图、窗口交互、已知限制。没有实测的行保持待验收。

## 13. 下一次实际手敲任务

恢复第 1 节构建基线后，**只执行提交 1：方向光，无阴影**。修改 `VulkanTypes.hpp`、`TriangleApplication.hpp`、`Buffers.cpp`、`ImGuiLayer.cpp`、主 vertex/fragment UBO 与 fragment 光照计算，更新 SPIR-V。目标是当场看到方向控制明暗变化，不先写一个没有调用方的通用 Shadow Manager。

方向光显示正确后，再按提交 2～6 接入阴影。遇到黑屏时回到上一个可见里程碑排查，不同时改坐标约定、深度比较与光照公式。
