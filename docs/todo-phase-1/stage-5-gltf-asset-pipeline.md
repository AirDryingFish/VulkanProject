# 阶段 5：glTF 2.0 资产管线实施指南

本文对应[开发路线图中的阶段 5](overview.md#阶段-5gltf-20-资产管线)。本文在 2026-08-29 以
`8bf1c83 Finish Stage 4 manual validation` 为评估基线。

Stage 4 已经把 SceneObject、Mesh、Material、Texture 和 descriptor 边界建立起来。Stage 5
不再继续扩展 OBJ loader，而是让工程能够读取 glTF 2.0 的场景、primitive、PBR 材质、纹理和
Node 变换，并复用现有 GPU 上传和资源生命周期路径。

本文按学习顺序拆分提交。第一批提交只解析和检查 CPU 数据；确认 accessor、primitive 和 Node
关系正确后，才逐步接入 GPU Buffer、纹理、descriptor 和 shader。不要在第一个提交里同时加载
glTF、增加 tangent、修改 Material descriptor 和引入 Node 层级。

---

## 1. 当前工程进度评估

### 1.1 构建与版本基线

在本指南编写时已经确认：

- 工作树干净，`HEAD` 与 `origin/main` 都是 `8bf1c83`；
- `windows-mingw-debug` 构建通过；
- `windows-mingw-release` 构建通过；
- `git diff --check` 通过；
- CTest 仍报告 `No tests were found`；
- Stage 4 已有手动验收提交，但仓库没有独立的验收日志文件。

自动测试仍不是初版功能的硬阻塞项，但 Stage 5 的 accessor 解码非常适合逐步加入纯 CPU 测试。
即使暂时不接测试框架，也应保留一个 `gltf_inspect` 命令行工具作为可重复检查入口。

### 1.2 Stage 4 完成判断

Stage 4 的 P0 条件已经满足：

- `Mesh` 独占 vertex/index `GpuBuffer`，并保存 count 和 local bounds；
- `SceneObject` 只持有 `MeshHandle`、`MaterialHandle`、`Transform` 和高层状态；
- Mesh cache 使用 `weak_ptr`，最后一个运行时引用通过 frame retirement 释放；
- Texture/Material library、默认纹理和 Emissive 已接入；
- Frame、Material、Skybox descriptor layout 已分离；
- PBR pipeline 使用 set 0 Frame、set 1 Material；
- Material factor 使用每 draw push constant；
- 启动场景中的两个 Sphere 共享 Mesh、使用不同 Material；
- Debug 与 Release 构建通过。

因此可以进入 Stage 5。以下 Stage 4 P1 建议项仍未实现，但不阻塞 glTF：

- 显式 `Duplicate Object` 操作；
- 显式 `Make Material Instance` 操作；
- UI 显示稳定的 Mesh key/name，而不仅是引用数和顶点统计。

不要为了“清空所有 P1”暂停 glTF 基础解析。可以在 Stage 5 引入稳定的 glTF primitive key 时顺手
补 Mesh key 展示；对象复制和 Material instance 可以继续作为独立小提交。

### 1.3 已经可以复用的能力

Stage 5 应直接复用：

- `MeshBuildData` 的 CPU vertex/index/bounds 表达；
- `TriangleApplication::createMesh()` 的批量 vertex/index 上传；
- `MeshHandle` 与 weak Mesh cache；
- `TextureResource`、`Material` 和 app-lifetime library；
- `createTextureImageFromFile()` 中的 image 创建、上传、mipmap 和 view 流程；
- Material descriptor 的六槽绑定路径；
- `SceneObject -> RenderObjectView` 的每帧快照边界；
- picking 对 Mesh local bounds 和 object matrix 的组合；
- `releaseMesh()` 与 Renderer frame retirement。

不应重写 VMA RAII、upload context、IBL pipeline、OBJ loader 或 Renderer 帧同步。

### 1.4 当前结构无法直接表达 glTF 的部分

| 当前结构 | glTF 需要 | Stage 5 的处理 |
|---|---|---|
| 一个 `SceneObject` 对应一个 Mesh 和一个 Material | 一个 glTF Mesh 可有多个 primitive，每个 primitive 可有不同 Material | 初版把每个 primitive 建成独立 Mesh/SceneObject，并保留共同 Node 变换 |
| `Vertex` 没有 tangent | glTF 可提供 `TANGENT` vec4 | 增加 tangent，缺失时生成或使用明确 fallback |
| GPU index 固定为 `uint32_t` | accessor 可为 uint8/uint16/uint32 | CPU 解码后统一转换为 `uint32_t`，暂不增加多种 VkIndexType |
| Metallic 与 Roughness 是两张独立图 | glTF 将 Roughness 放 G、Metallic 放 B | 迁移为 packed Metallic-Roughness 纹理 |
| 每个 Material 只使用一套 UV | 每个 textureInfo 可选择 `texCoord` | 至少支持 TEXCOORD_0/1，并把各槽选择传给 shader |
| 应用统一拥有一个 sampler | glTF texture 引用 image + sampler | P0 只接受缺省 sampler 语义；P1 再完整映射并缓存参数组合 |
| `Transform` 是 position/Euler/scale | glTF Node 使用 TRS（rotation 是 quaternion）或 matrix | CPU importer 保存 local matrix，遍历时计算 world matrix |
| 当前世界以 Z-up 使用 | glTF 使用 Y-up 约定 | 只在场景根应用一次坐标转换矩阵 |
| import UI 只接受 OBJ | 需要 `.gltf` 与 `.glb` | 增加独立 glTF import 操作和事务式错误报告 |

### 1.5 Stage 5 的核心难点

Stage 5 的难点不是 JSON 语法，而是以下映射：

```text
buffer + bufferView + accessor
            ↓
正确考虑 offset / stride / component type / normalized / sparse
            ↓
统一的 CPU vertex/index 数据

scene roots
    ↓
Node local transform
    ↓ parent * local
Node world transform
    ↓
一个 Node 引用的每个 Mesh primitive
    ↓
MeshHandle + MaterialHandle + RenderObjectView
```

任意一步索引错位都可能产生“能编译但模型炸裂”的结果，因此 CPU 数据检查必须早于 GPU 上传。

---

## 2. Stage 5 的目标与边界

### 2.1 P0 目标

完成后应满足：

1. 使用一个明确的 glTF 2.0 parser 读取 `.gltf` 和 `.glb`；
2. 支持外部 buffer、GLB buffer 和 data URI；
3. 正确读取 position、normal、TEXCOORD_0/1、COLOR_0 和 tangent；
4. 正确读取 indexed 与 non-indexed primitive；
5. 输入 uint8/uint16/uint32 index 后统一生成 CPU `uint32_t` index；
6. 支持一个 glTF Mesh 中多个 triangle primitive；
7. 多个 Node 引用同一个 glTF Mesh 时共享 GPU Mesh；
8. 支持 Node hierarchy、非奇异且非镜像的 TRS/matrix 和 local/world transform；
9. 支持外部图片、data URI 图片和 GLB bufferView 图片；
10. Base Color/Emissive 使用 sRGB，其他 PBR 数据使用线性格式；
11. 支持 Base Color、Metallic-Roughness、Normal、Occlusion、Emissive；
12. texture 与 factor 同时生效；
13. 缺失 texture/material 时复用或新建符合 glTF identity 语义的默认资源；
14. 失败导入不会向当前 scene/library 发布半成品；
15. OBJ 入口继续可用，但 glTF 成为主要资产入口；
16. 能加载至少一个多 Node、多 primitive、多 Material 的参考场景；
17. Debug/Release 构建通过，手动运行无新增 Validation 错误。

### 2.2 本阶段暂不做什么

- 不做 Skin、joint palette 和骨骼动画；
- 不做 Morph target；
- 不做 animation channel/sampler；
- 不做 Draco、Meshopt 或其他几何压缩扩展；
- 不做 KTX2/Basis Universal；
- 不做后台线程、异步 transfer queue 或 streaming；
- 不做通用 Asset Manager、generation handle 或热重载；
- 不做 bindless descriptor；
- 不做场景保存和 glTF 导出；
- 不在 P0 支持 line、point、triangle strip/fan primitive；
- 不在 P0 支持 alpha blend、alpha mask 和 double-sided pipeline variant；
- 不一次性实现全部 glTF extension。

遇到未支持功能时必须返回带上下文的错误或 warning，不能静默画错。

### 2.3 P1/P2 延伸

P1 建议：

- glTF sampler wrap/filter 完整映射与去重；
- 生成缺失 tangent；
- 纯 CPU accessor/import 测试；
- glTF import summary 和资源统计 UI；
- 纹理与 glTF asset cache；
- 导入的场景根对象整体 Transform。

P2 延期：

- Alpha mode 与 double-sided；
- KHR_texture_transform；
- Skin、Morph、Animation；
- KTX2/BasisU、Draco、Meshopt；
- 异步资产管线。

---

## 3. 必须一直成立的规则

### 3.1 Parser 与 Renderer 边界

- glTF parser 只产生 CPU 数据，不接触 `VkDevice`、VMA 或 command buffer；
- accessor 解码失败时不得创建 GPU Buffer；
- CPU 解析完成并通过结构校验后，应用层才上传资源；
- Renderer 继续只接收 `RenderObjectView`，不读取 fastgltf 类型；
- 不允许 fastgltf 类型扩散到 `Renderer.hpp`、shader 接口或 Vulkan 资源封装。

### 3.2 Accessor 规则

- 不手写 `buffer + bufferView + accessor` 的裸指针算术来绕过 parser accessor 工具；
- 必须处理 accessor 与 bufferView 的 byte offset；
- 必须处理 interleaved `byteStride`；
- 必须验证 accessor type、component type 和 count；
- 必须尊重 normalized integer attribute；
- non-indexed primitive 必须显式生成顺序 index；
- 输入 index 统一转换为 `uint32_t`，Renderer 暂时继续使用 `VK_INDEX_TYPE_UINT32`；
- POSITION 缺失、count 不一致或 index 越界必须导入失败；
- sparse accessor 通过 fastgltf accessor 工具处理，不假设 bufferView 永远存在。

### 3.3 Primitive 与 Mesh 规则

- glTF `mesh` 与现有 GPU `Mesh` 不是同一个粒度；
- Stage 5 初版中，一个 glTF primitive 对应一个现有 `Mesh`；
- 一个 glTF Mesh 的多个 primitive 生成多个 `MeshHandle`；
- 多个 Node 引用同一 glTF Mesh 时，必须复用这些 primitive MeshHandle；
- primitive cache key 至少包含规范化资产路径、mesh index 和 primitive index；
- bounds 从 POSITION accessor 或解码后的 position 计算，不能使用 Node world bounds 代替 local bounds；
- 非 triangle primitive 返回清晰的 unsupported mode 信息。

建议 key：

```text
gltf:/absolute/normalized/path/model.glb#mesh=3/primitive=1
```

当前 `getOrCreateMesh(MeshSource, path)` 会自行调用 OBJ/Cube/Sphere 构建函数并立即写全局 cache，
不能直接用于已解码的 glTF primitive。新增一个能够接收稳定 key 与 `MeshBuildData` 的 staged 路径：

```text
先只读 lock 全局 weak cache
        ↓ miss
在本次 import transaction 的局部 map 中查找/创建 MeshHandle
        ↓
createMesh(decodedPrimitive)，但暂不写全局 cache
        ↓ 全部导入成功
发布 weak cache entry 与 SceneObject
```

不要让 helper 在事务中途偷偷写 `meshCache`。来源类型也必须明确：增加 `MeshSource::Gltf`，或将
`SceneObject` 的资产来源类型与内置 Mesh 构建枚举拆开；不能让 glTF 对象默认伪装成 OBJ。

### 3.4 Material 与 Texture 规则

- Base Color 和 Emissive image 使用 sRGB VkFormat；
- Metallic-Roughness、Normal 和 Occlusion 使用 UNORM/线性 VkFormat；
- glTF Metallic-Roughness 的 G 通道是 Roughness，B 通道是 Metallic；
- Occlusion 通常读取 R 通道；
- Normal texture scale、Occlusion strength 和所有 factor 不能被静默丢弃；
- Occlusion strength 使用 `mix(1.0, sampledOcclusion, strength)`，不是简单的
  `sampledOcclusion * strength`；
- 同一个 glTF image 若同时用于 sRGB 和线性用途，应按 `(image identity, color space)` 分开缓存；
- descriptor set 不能比 image view 和 sampler 活得更久；
- 运行时不原地更新 pending frame 正在使用的 Material descriptor；
- 导入新材质时创建新的 Material/descriptor set；
- descriptor pool 容量必须在上传前预检。

### 3.5 Node 与坐标规则

- Node 的 matrix 与 TRS 二选一，不能重复相乘；
- glTF rotation 使用 quaternion，不先转换成 Euler 再参与层级计算；
- world matrix 固定为 `parentWorld * local`；
- 只从选中的 glTF scene roots 开始遍历；
- 检查非法 child index 和循环引用；
- 当前工程是 Z-up，glTF Y-up 到 Z-up 的转换只应用一次；
- 推荐根转换为绕 X 轴 +90°，并用参考资产确认朝向；
- 该根旋转 determinant 为正，不应额外反转 winding；但 Node 的 scale/matrix 仍可能产生镜像；
- P0 检查 `determinant(mat3(assetTransform))`：接近 0 的奇异变换，以及小于 0 的镜像变换，
  都以包含 node index/name 的 unsupported error 拒绝；后者要等到有 front-face/winding variant 后再支持；
- glTF UV 原点在图片左上、T 轴向下；当前 stb 原始行序加 Vulkan 上传路径的基线是不翻 glTF UV、
  也不翻 image。参考纹理只用于确认没有意外翻转，不能用来“选择”规范；不要复用 OBJ loader 的
  `1.0f - v`。

### 3.6 导入事务

完整导入分三段：

```text
parse + validate
        ↓
构建所有 CPU import data，统计资源需求
        ↓
预检 descriptor/material/texture/mesh 数量
        ↓
上传到局部临时资源集合
        ↓ 全部成功
一次性发布到 cache/library/sceneObjects
```

如果失败：

- 当前 scene 保持不变；
- 临时 RAII GPU 资源正常析构；
- UI 显示资产路径、对象类型/index 和错误原因；
- 不留下指向已析构 image 的 descriptor；
- 不吞掉 warning。

---

## 4. Parser 选择

### 4.1 推荐 fastgltf

本阶段推荐使用 `fastgltf`，原因是：

- 当前工程是 C++17，fastgltf 本身也是 C++17；
- 当前 vcpkg baseline 已包含 `fastgltf 0.9.0` port；
- 提供 accessor、sparse accessor 和 Node transform 工具；
- parser 不负责图片解码，正好复用工程已有的 stb 和 Vulkan image 上传路径；
- `.gltf` 与 `.glb` 可以走同一个入口；
- 使用 typed/optional 数据，适合学习 glTF 对象间的索引关系。

官方资料：

- [fastgltf 0.9 文档](https://fastgltf.readthedocs.io/v0.9.x/)
- [fastgltf Accessor tools](https://fastgltf.readthedocs.io/v0.9.x/tools.html)
- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)

不要同时保留 fastgltf 和 tinygltf 两套 importer。若后续确实更换库，应保持自有 CPU import data
接口稳定，只替换 parser adapter。

### 4.2 固定版本意识

项目使用 vcpkg builtin baseline，因此手敲代码时应以实际安装的 fastgltf 版本文档为准，不要直接
复制 `main` 分支最新 API。Stage 5 开始时记录：

```text
vcpkg baseline
fastgltf port version
编译器版本
使用的官方 sample asset commit
```

---

## 5. 目标数据流与 CPU 类型

### 5.1 数据流

```text
.gltf / .glb
    ↓ fastgltf parse
fastgltf::Asset
    ↓ GltfLoader（只做 CPU 转换）
GltfImportData
├── decoded images
├── material definitions
├── primitive build data
├── glTF mesh -> primitive indices
└── nodes / roots / local transforms
    ↓ TriangleApplication import transaction
TextureHandle / MaterialHandle / MeshHandle
    ↓ Node hierarchy traversal
SceneObject instances
    ↓ existing per-frame snapshot
RenderObjectView
```

### 5.2 建议的 CPU 中间类型

不要把 `fastgltf::Asset` 长期保存在 Renderer。先建立项目自己的窄数据模型，例如：

```cpp
struct DecodedImageData
{
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba8;
};

struct GltfTextureRef
{
    std::size_t imageIndex = 0;
    std::optional<std::size_t> samplerIndex;
    std::uint32_t texCoord = 0;
};

struct GltfSamplerData
{
    // 保存 glTF 枚举原值；nullopt 表示规范未指定，由 importer 应用缺省语义。
    std::optional<std::int32_t> magFilter;
    std::optional<std::int32_t> minFilter;
    std::int32_t wrapS = 10497; // REPEAT
    std::int32_t wrapT = 10497; // REPEAT
};

struct GltfMaterialData
{
    std::string name;
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    glm::vec3 emissiveFactor{0.0f};

    std::optional<GltfTextureRef> baseColor;
    std::optional<GltfTextureRef> metallicRoughness;
    std::optional<GltfTextureRef> normal;
    std::optional<GltfTextureRef> occlusion;
    std::optional<GltfTextureRef> emissive;
};

struct GltfPrimitiveData
{
    std::string key;
    MeshBuildData mesh;
    std::optional<std::size_t> materialIndex;
};

struct GltfMeshData
{
    std::string name;
    std::vector<std::size_t> primitiveIndices;
};

struct GltfNodeData
{
    std::string name;
    glm::mat4 localTransform{1.0f};
    std::optional<std::size_t> meshIndex;
    std::vector<std::size_t> children;
};

struct GltfImportData
{
    std::filesystem::path sourcePath;
    std::vector<GltfSamplerData> samplers;
    std::vector<DecodedImageData> images;
    std::vector<GltfMaterialData> materials;
    std::vector<GltfPrimitiveData> primitives;
    std::vector<GltfMeshData> meshes;
    std::vector<GltfNodeData> nodes;
    std::vector<std::size_t> sceneRoots;
};
```

类型名称可以调整，但边界必须保持：这些类型不拥有 Vulkan handle。

### 5.3 静态 Node flattening

Stage 5 P0 不做动画，可以在导入发布阶段遍历 hierarchy，计算每个 Node 的 world matrix，并为
Node 引用 Mesh 的每个 primitive 创建一个 `SceneObject`。建议给 `SceneObject` 增加：

```cpp
glm::mat4 assetTransform{1.0f};
```

最终模型矩阵：

```text
gltfWorld(root)  = local(root)
gltfWorld(child) = gltfWorld(parent) * local(child)
assetTransform  = rootConversion * gltfWorld(node)

model = T(userPosition)
      * assetTransform
      * R(userRotation)
      * R(autoRotation)
      * S(userScale)
```

`rootConversion` 只进入 `assetTransform` 一次。这个乘序让对象 UI 的 rotation/scale 围绕该 primitive
的 Node local origin 工作，不会把 Node 在 glTF 场景中的平移一起缩放或绕资产原点旋转。内置几何的
`assetTransform` 保持 identity，因此与当前 `getObjectMatrix()` 行为一致。若以后要把整份导入场景
作为一个整体移动/旋转，应增加共享 import-root transform，而不是给每个 primitive 重复套一层
`userModel * assetTransform`。

这允许继续复用现有 draw、picking 和 Material 选择。多个 primitive 会在 UI 中显示为多个对象，
名称可使用 `NodeName / MeshName / Primitive N`。

该方案只适用于静态导入。未来做 animation/skin 时，需要保留 scene instance 与实时 Node pose，
不能继续只在导入时烘焙 world matrix。

---

## 6. 推荐提交顺序

| 顺序 | 建议提交 | 主要学习点 |
|---|---|---|
| 0 | `Record Stage 5 glTF baseline` | 固定参考资产、构建与对比方法 |
| 1 | `Integrate fastgltf parse inspection` | 依赖、parse/validate、错误报告，不接 GPU |
| 2 | `Decode glTF accessors` | offset、stride、type、normalized、sparse、index 转换 |
| 3 | `Import glTF primitive geometry` | tangent、单 primitive、现有 Mesh 上传路径 |
| 4 | `Share glTF mesh primitives` | 多 primitive、稳定 key、Node 间共享 |
| 5 | `Import glTF images and samplers` | URI/embedded/GLB image、色彩空间、mip、cache |
| 6 | `Map glTF PBR materials` | packed MR、factor、UV set、descriptor/shader 同步 |
| 7 | `Instantiate glTF scene nodes` | hierarchy、local/world matrix、坐标转换 |
| 8 | `Integrate glTF scene import UI` | 事务式发布、错误提示、OBJ 共存 |
| 9 | `Finish Stage 5 validation` | 参考资产矩阵、Validation、Release、验收记录 |

每个提交只解决一类问题。特别是提交 2 不创建 GPU Buffer，提交 5 不改 PBR shader，提交 6 才
一起修改 Material/descriptor/shader。

---

## 7. 第 0 步：固定参考基线

### 7.1 建立测试资产清单

从 Khronos glTF Sample Assets 选择小而明确的资产，不要第一步就使用大型 Sponza：

| 资产类型 | 用途 |
|---|---|
| 最小 Triangle/Box | parser 与单 primitive |
| non-indexed Triangle | 自动生成 index |
| interleaved Box | accessor byteStride |
| uint8/uint16/uint32 index assets | index component type 转换 |
| 多 primitive/Mesh asset | primitive 与 Material 对应 |
| 多 Node hierarchy asset | parent/local/world transform |
| DamagedHelmet 或 WaterBottle | packed MR、Normal、AO、Emissive、GLB |
| UV/texture coordinate test | UV 朝向与 TEXCOORD_0/1 |

在 `assets/models/gltf/README.md` 记录每个资产的：

- 来源链接；
- upstream commit；
- license；
- 具体测试点；
- 是否进入 Git LFS。

### 7.2 保存参考画面

使用 Khronos Sample Viewer 或另一个明确支持 glTF 2.0 metallic-roughness 的查看器，记录：

- 相机大致角度；
- 环境贴图；
- 是否开启 punctual lights；
- tone mapping/exposure；
- 截图日期。

“外观基本一致”不等于逐像素一致，但 UV、法线方向、金属度通道和 emissive 不应明显错误。

### 7.3 验证 Stage 4 基线

```powershell
git diff --check
cmake --build --preset windows-mingw-debug --parallel
cmake --build --preset windows-mingw-release --parallel
.\build\windows-mingw-debug\vulkan.exe
```

确认两个默认 Sphere 共享 Mesh、材质不同、删除与窗口恢复无 Validation 错误。

---

## 8. 第 1 步：集成 parser，但只做 inspection

### 8.1 vcpkg 与 CMake

在 `vcpkg.json` dependencies 中增加：

```json
"fastgltf"
```

在 CMake 中增加：

```cmake
find_package(fastgltf CONFIG REQUIRED)
```

先创建独立命令行工具，不修改 Renderer：

```cmake
add_executable(gltf_inspect
    tools/GltfInspect.cpp
)

target_link_libraries(gltf_inspect PRIVATE
    fastgltf::fastgltf
)
```

如果 target 名与实际安装包不一致，以 vcpkg 安装输出和生成的 config 为准，不要猜测。

### 8.2 `gltf_inspect` 只输出结构

第一版工具只做：

1. 接收一个 `.gltf` 或 `.glb` 路径；
2. `GltfDataBuffer::FromPath()`；
3. `Parser::loadGltf()` 自动识别 JSON/GLB；
4. 检查每一个 `Expected` 的 error；
5. 调用 `fastgltf::validate()` 做结构校验；
6. 输出 scene/node/mesh/primitive/material/image/texture/buffer/accessor 数量；
7. 输出默认 scene index和每个 Node 的 name/children/mesh index；
8. 返回非零 exit code 表示失败。

代码骨架以 fastgltf 0.9 文档为准：

```cpp
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include <filesystem>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: gltf_inspect <asset.gltf|asset.glb>\n";
        return 2;
    }

    const std::filesystem::path path =
        std::filesystem::absolute(argv[1]).lexically_normal();

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None)
    {
        std::cerr << "failed to read glTF file: "
                  << fastgltf::getErrorMessage(data.error()) << '\n';
        return 1;
    }

    fastgltf::Parser parser;
    auto loaded = parser.loadGltf(
        data.get(),
        path.parent_path(),
        fastgltf::Options::None);

    if (loaded.error() != fastgltf::Error::None)
    {
        std::cerr << "failed to parse glTF asset: "
                  << fastgltf::getErrorMessage(loaded.error()) << '\n';
        return 1;
    }

    const fastgltf::Asset &asset = loaded.get();

    const fastgltf::Error validationError = fastgltf::validate(asset);
    if (validationError != fastgltf::Error::None)
    {
        std::cerr << "failed to validate glTF asset: "
                  << fastgltf::getErrorMessage(validationError) << '\n';
        return 1;
    }

    std::cout << "scenes: " << asset.scenes.size() << '\n';
    std::cout << "nodes: " << asset.nodes.size() << '\n';
    std::cout << "meshes: " << asset.meshes.size() << '\n';
    std::cout << "materials: " << asset.materials.size() << '\n';
    std::cout << "images: " << asset.images.size() << '\n';
    std::cout << "accessors: " << asset.accessors.size() << '\n';
    return 0;
}
```

先让工具编译运行，再逐项增加输出。不要照抄未安装版本的错误字符串 helper。

### 8.3 验证

```powershell
cmake --preset windows-mingw-debug
cmake --build --preset windows-mingw-debug --parallel
.\build\windows-mingw-debug\gltf_inspect.exe <你的资产路径>
.\build\windows-mingw-debug\vulkan.exe
```

这一提交不应产生任何新 Vulkan object，也不应修改 shader。

建议提交：

```text
Integrate fastgltf parse inspection
```

---

## 9. 第 2 步：解码 accessor 到 CPU 数据

### 9.1 新增 CPU loader

建议新增：

```text
src/GltfImportTypes.hpp
src/GltfLoader.hpp
src/GltfLoader.cpp
```

把这些文件加入 `vulkan` target，并让主程序也链接 parser：

```cmake
target_sources(vulkan PRIVATE
    src/GltfLoader.cpp
    src/GltfLoader.hpp
    src/GltfImportTypes.hpp
)

target_link_libraries(vulkan PRIVATE
    fastgltf::fastgltf
)
```

`gltf_inspect` 后续可以直接复用 `GltfLoader.cpp`，或在这一提交提取一个很小的 `gltf_import`
静态库供两个 executable 链接；只选一种组织方式，不要复制两份 accessor 解码实现。

接口先返回 CPU 数据：

```cpp
GltfImportData loadGltfCpuData(const std::filesystem::path &path);
```

不要传入 `TriangleApplication`、`Renderer` 或 `VulkanContext`。

### 9.2 使用 accessor tools

包含：

```cpp
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
```

对每个 triangle primitive：

1. 找 `POSITION`，验证 Vec3 和 count；
2. 可选读取 `NORMAL`；
3. 可选读取 `TEXCOORD_0`、`TEXCOORD_1`；
4. 可选读取 `COLOR_0` Vec3/Vec4；
5. 可选读取 `TANGENT` Vec4；
6. 读取 indices accessor，或生成 `0..vertexCount-1`；
7. 所有输入 index 转为 `uint32_t`；
8. 验证最大 index 小于 vertex count；
9. 计算 local bounds；
10. 记录 material index；
11. 拒绝暂不支持的 primitive mode。

缺失 `NORMAL` 时不能给所有顶点写一个任意方向。对 triangle primitive 计算 flat normal；indexed
primitive 为得到真正逐面的 flat normal，需要按三角形拆点并重建顺序 index。此时忽略文件中即使存在的
`TANGENT`。退化三角形要报包含 mesh/primitive/triangle index 的错误，不能让归一化产生 NaN。

UV 在 attribute 层可默认写零，但只有没有 Material slot 请求该 UV set 时才允许继续。`COLOR_0`
缺失时必须写白色。当前 fragment shader 会把 vertex color 乘入 Base Color，若保留 value-initialized
黑色，绝大多数 glTF 会被画黑。存在 `COLOR_0` 时使用 accessor 工具读取 Vec3/Vec4 及 normalized
integer；P0 的 OPAQUE 路径至少保留 RGB，alpha 留给后续 alpha mode。

“可选读取”表示 accessor 在文件格式层面可缺失，不代表可以忽略 Material 的要求。若某个 texture
slot 指定 `texCoord = 1`，对应 primitive 就必须提供 `TEXCOORD_1`；否则用包含 material、mesh、
primitive index 的错误终止导入。`texCoord > 1` 在 P0 中同样明确报 unsupported，不能悄悄改成 0。

第 2 步只解码 accessor，因此 CPU importer 先明确使用：

```cpp
const auto options = fastgltf::Options::LoadExternalBuffers;
```

GLB buffer 和 data URI 在 fastgltf 0.9 中会进入可读 source；外部 `.bin` 必须启用
`LoadExternalBuffers`，不要再添加已经不需要的 `LoadGLBBuffers`。第 5 步开始处理图片时，再把
`LoadExternalImages` OR 进同一组选项，让外部、data URI 与嵌入图片统一从内存字节解码；在
`Asset` 生命周期结束前复制到自己的 `DecodedImageData`。

fastgltf 0.9 默认 accessor adapter 的正常 owning source 是 `sources::Array`；`sources::Vector` 主要供
exporter 使用，`sources::ByteView` 不拥有内存。若以后改为 `ByteView` 或自定义 adapter，backing bytes
必须一直活到 accessor/image 转换完成，不能把它描述成 parser 自动接管。

### 9.3 不要错误地 memcpy `Vertex`

glTF attribute 常常位于不同 accessor，甚至使用 interleaved stride。正确流程是先默认初始化
`vertices.resize(positionCount)`，再按 attribute 分别写字段。不能假设 glTF 的内存布局等于项目
`Vertex`。

### 9.4 CPU 输出检查

让 `gltf_inspect` 增加：

```text
mesh 0 primitive 0
  vertices: 24
  indices: 36
  has normals: yes
  uv sets: 1
  has tangents: no
  material: 0
  bounds min/max: ...
```

此提交仍不上传 GPU。

建议提交：

```text
Decode glTF accessors
```

---

## 10. 第 3 步：接入 tangent 与第一个 primitive

### 10.1 扩展 Vertex

给 `Vertex` 增加：

```cpp
// w == 0 只表示“没有有效 tangent”；合法 glTF tangent.w 为 +1 或 -1。
glm::vec4 tangent{1.0f, 0.0f, 0.0f, 0.0f};
glm::vec2 texcoord1{0.0f};
```

同步修改：

- vertex binding/attribute descriptions；
- `Vertex::operator==` 与 `std::hash<Vertex>`；
- Cube/Sphere/OBJ 顶点构建默认值；
- PBR vertex shader input/output；
- fragment shader TBN 构建；
- shader SPIR-V。

同时给 `MeshBuildData` 与 `Mesh` 增加 `bool hasTangents`，它是整个 primitive 的元数据。赋值链必须
完整写出来：loader 设置 `MeshBuildData::hasTangents`，现有 `createMesh()` 显式复制到
`Mesh::hasTangents`，`drawFrame()` 再写入 `RenderObjectView` 的 `textureInfo.y`，例如
`0 = derivative fallback`、`1 = vertex tangent`。当前 `createMesh()` 是逐字段复制，不会自动传播新成员；
shader 也不能从随便填入的默认 tangent 猜测路径。

glTF tangent 的 xyz 是 tangent 方向，w 用于恢复 bitangent handedness。normal 用 model 线性部分的
inverse-transpose，tangent 则用 model 的线性部分，并在 world space 重新正交化：

```glsl
mat3 M = mat3(pushConstants.model);
vec3 N = normalize(transpose(inverse(M)) * inNormal);
vec3 T = M * inTangent.xyz;
T = normalize(T - N * dot(N, T));
float modelSign = determinant(M) < 0.0 ? -1.0 : 1.0;
vec3 B = normalize(cross(N, T)) * inTangent.w * modelSign;
```

若 primitive 缺 tangent：

- P0 由 `hasTangents == false` 明确选择 derivative TBN fallback；
- P1 应为拥有 UV 和 normal 的 triangle 生成 tangent；
- 无 UV 时使用几何 normal，并禁用 normal map 影响。

当前 fallback 的 `b = -cross(n, t)` 与 OBJ 的 V 翻转耦合，不能直接用于不翻 UV 的 glTF。fallback
必须使用 normal texture 实际选择的同一套 UV，并由导数恢复两个方向，例如先计算：

```glsl
float det = st1.x * st2.y - st1.y * st2.x;
vec3 Traw = (q1 * st2.y - q2 * st1.y) / det;
vec3 Braw = (-q1 * st2.x + q2 * st1.x) / det;
```

先拒绝接近零的 `det`，再把 `Traw` 对 N 做 Gram-Schmidt；用
`dot(cross(N, T), Braw)` 的符号恢复 B 的 handedness。不要硬编码正号或负号，也不要给缺失 tangent
的顶点随便写同一个方向后仍宣称 normal map 正确。

### 10.2 先只上传一个 primitive

第一次 GPU 接入限定：

- 一个 Node；
- 一个 triangle primitive；
- 默认 Material；
- 不加载 glTF image；
- 通过现有 `createMesh()` 上传；
- bounds、picking、删除路径继续工作。

验证 geometry 正确后再做多 primitive。

建议提交：

```text
Import glTF primitive geometry
```

---

## 11. 第 4 步：多 primitive 与 Mesh 共享

### 11.1 一个 primitive 一个 GPU Mesh

不要把 glTF Mesh 的所有 primitive 强行合并成一个现有 `Mesh`，因为它们可以有不同 Material、
attribute 集合和 topology。初版为每个 primitive 创建一个 MeshHandle。

建立二维映射：

```text
gltf mesh index
    └── primitive index
            ├── MeshHandle
            └── material index
```

### 11.2 稳定 cache key

key 使用：

```text
canonical-ish asset path + mesh index + primitive index
```

同一 glTF Mesh 被多个 Node 引用时只上传一次。cache 继续保存 `weak_ptr<Mesh>`，SceneObject 保存
强引用。可以在这一步给 `Mesh` 增加 debug `name/key`，补齐 Stage 4 的 UI 建议项。

用于 `gltf mesh index -> primitive handles` 的强引用映射只能存在于当前导入事务。成功发布后，
持久强引用仍只来自 SceneObject，cache 仍是 weak reference；函数返回前清空 staged handles。否则
当前 `releaseMesh()` 的 `use_count() == 1` 约定会失效，最后一个 SceneObject 删除时 Buffer 不会进入
frame retirement。

失败事务中的新 Mesh 可以直接由局部 RAII handle 析构，是因为它们只完成同步 upload、从未发布、
也从未进入任何 draw command buffer。已经发布并参与渲染的 Mesh 仍必须逐个经过 `releaseMesh()`；
批量删除 glTF 对象也不能直接 `sceneObjects.erase()` 后让 shared_ptr 普通析构。

Windows 下 `absolute().lexically_normal()` 不统一路径大小写、symlink 或短路径。初版可保持与 OBJ
一致；如果改用 `weakly_canonical()`，必须处理文件不存在和 filesystem error。

### 11.3 删除语义不变

每个由 primitive 生成的 SceneObject 删除时仍调用 `releaseMesh()`。删除一个 Node 实例不能销毁
其他 Node 正在共享的 primitive Mesh。

建议提交：

```text
Share glTF mesh primitives
```

---

## 12. 第 5 步：图片、纹理与 sampler

### 12.1 先拆 decode 与 upload

将现有“从文件加载 + 创建 GPU image”拆为两个概念：

```text
decode image bytes -> RGBA8 CPU data
upload RGBA8 -> GpuImage + mip chain + image view
```

建议接口：

```cpp
DecodedImageData decodeImage(
    const std::byte *bytes,
    std::size_t byteCount,
    const std::string &debugName);

GpuImage uploadTexture2D(
    const DecodedImageData &image,
    VkFormat format,
    const std::string &debugName);
```

这样 URI 图片、data URI 和 GLB bufferView 图片都能复用同一个 upload 路径。

### 12.2 glTF image source

处理：

- 外部 URI；
- data URI；
- GLB 中的 bufferView；
- PNG/JPEG MIME type；
- 路径相对 glTF 文件目录解析；
- `..` 规范化与越界/读取错误。

本指南选择在这一提交启用 `fastgltf::Options::LoadExternalImages`，把支持的 URI/data URI image
加载为 owning source，再和 GLB bufferView image 一起交给 `stbi_load_from_memory()`。把解码结果复制
进 `DecodedImageData` 后才能销毁临时 `fastgltf::Asset`。不再另外写一套外部文件 image reader。
fastgltf 只提供 image 数据来源，不替工程选择 VkFormat 或创建 Vulkan image。

### 12.3 色彩空间 cache key

建议 image cache key：

```text
asset path + image index + srgb/linear
```

同一 image 被 Base Color 和 Normal 同时引用时，需要两个不同 VkFormat 资源，不能只凭 image index
复用一个 `GpuImage`。

### 12.4 Sampler

glTF sampler 需要映射：

- magFilter；
- minFilter 与 mipmap mode；
- wrapS / wrapT；
- 缺失 sampler 的默认行为。

建议用 sampler 参数生成稳定 key 并缓存 `GpuSampler`。P0 的精确边界是：保存 glTF sampler index，
正确建立“缺失 sampler”的默认 repeat/filter SamplerHandle；若资产显式要求当前尚未映射的
wrap/filter 组合，返回带 texture/sampler index 的 unsupported error，不能只警告后静默换成全局
sampler。P1 再完整映射并缓存所有基础 glTF sampler 参数。

解析 `fastgltf::Asset` 时把每个 sampler 的 mag/min/wrapS/wrapT 复制进
`GltfImportData::samplers`；`GltfTextureRef::samplerIndex` 只索引这张自有表。不能只保存 index 后销毁
Asset，否则 P0 无法判断它是否为可接受的缺省组合，P1 也失去了创建 VkSampler 所需的参数。

当前 Material slot 只有 `TextureHandle`，descriptor writer 也固定使用全局 `textureSampler`，因此
只建立 sampler cache 仍不能让 glTF sampler 生效。建议逐步引入：

```cpp
struct SamplerResource
{
    std::string name;
    GpuSampler sampler;
};

using SamplerHandle = std::shared_ptr<SamplerResource>;

struct MaterialTextureSlot
{
    TextureHandle texture;
    SamplerHandle sampler;
    std::uint32_t texCoord = 0;
};
```

上面是提交 6 完成 packed Material 迁移后的目标结构。提交 5 时当前 Material 仍有六个 slot
（Metallic 与 Roughness 分离），因此这里只建立 `SamplerResource`、参数 key/cache 和 CPU glTF
sampler 映射，不修改 Material slot 或 descriptor。提交 6 再一次性把六槽迁移为五个
`MaterialTextureSlot`，让 descriptor write 从各 slot 同时取得 image view 与 sampler。应用持有
app-lifetime `samplerLibrary`；缺失 glTF sampler 的 slot 引用默认 sampler。P0 的所有已接受 slot
可以指向同一个默认 SamplerHandle；遇到非缺省参数则按上段拒绝，P1 再完整启用映射。

此提交只让 TextureResource 正确建立，不修改 PBR material shader。

建议提交：

```text
Import glTF images and samplers
```

---

## 13. 第 6 步：映射 glTF PBR Material

### 13.1 Packed Metallic-Roughness

当前 shader 从两张独立 R 通道纹理读取 metallic 与 roughness。glTF 使用一张 packed image：

```text
R: unused for metallic-roughness
G: roughness
B: metallic
A: unused
```

推荐把 `Material` 迁移为一个 `metallicRoughnessTexture`，并同步修改：

- 默认 Material；
- Rusted Iron 兼容路径；
- Material descriptor layout/write；
- descriptor pool image 数量；
- fragment shader；
- `materialImageDescriptorCount`；
- SPIR-V。

这次迁移完成后用下面的搜索做清理检查，不能留下新旧两套死资源：

```powershell
rg "metallicTexture|roughnessTexture|defaultMetallicTexture|defaultRoughnessTexture|materialImageDescriptorCount" src assets/shaders
```

删除或替换旧 `Material::metallicTexture/roughnessTexture`、不再使用的默认纹理、descriptor null-check/
write、cleanup/header 成员以及旧 shader sampling；搜索结果中只应留下你明确保留的兼容构建输入或
新的 packed 名称。

现有 Rusted Iron 两张独立灰度图片可以在 CPU 合成为一张 RGBA packed image。不要为了兼容旧资产
长期保留两套 shader binding。

迁移后的 set 1 固定为：

```text
binding 0  Base Color
binding 1  Normal
binding 2  packed Metallic-Roughness
binding 3  Occlusion
binding 4  Emissive
```

`materialImageDescriptorCount` 改为 5，descriptor pool 的 Material image 容量改为
`5 * maxMaterialCount`。C++ layout/write、fragment shader binding 和下面 UV mask bit 0～4 使用完全
相同的槽顺序，不能只改 count 后沿用旧数组下标。

### 13.2 Material 映射

| glTF | 项目 Material |
|---|---|
| baseColorTexture/factor | sRGB texture + baseColorFactor |
| metallicRoughnessTexture | linear packed texture |
| metallicFactor | metallicFactor |
| roughnessFactor | roughnessFactor |
| normalTexture/scale | linear texture + normalScale |
| occlusionTexture/strength | linear texture + ao/occlusion strength |
| emissiveTexture/factor | sRGB texture + emissiveFactor |

`Material` 本身必须新增默认值为 `1.0f` 的 `normalScale`，并把现有 `aoFactor` 重命名/替换为默认
`1.0f` 的 `occlusionStrength`；不要同时保留两个含义重叠的字段。同步修改默认/Rusted Iron material、
ImGui label、每帧 push constant 组装和 shader。`Material` 还要保存各 `MaterialTextureSlot` 的 UV set
信息，不能只把这些字段留在临时 `GltfMaterialData` 中。

先按 glTF material index 各创建一次 `MaterialHandle` 和 descriptor set，并保存
`gltfMaterialIndex -> MaterialHandle` 表。所有 primitive/Node 对同一 index 共享这个 handle，不能按
SceneObject 重复分配 descriptor；没有 material 的 primitive 统一引用规范 default Material。

缺失槽必须使用符合 glTF 乘法语义的默认像素：

| Slot | 默认像素 | 原因 |
|---|---|---|
| Base Color | white sRGB | 保留 baseColorFactor |
| Normal | flat normal linear | 不扰动几何法线 |
| Metallic-Roughness | linear `{255,255,255,255}` | G/B 都为 1，保留两个 factor |
| Occlusion | white linear | sampled occlusion 为 1，不削弱环境项 |
| Emissive | white sRGB | 保留 emissiveFactor；factor 默认为 0，因此仍不自发光 |

Stage 4 的黑色默认 Emissive 会把非零 emissiveFactor 也乘成零，不能直接用于 glTF 缺失 Emissive
texture 的语义。进入 packed Material 迁移时应将默认 Emissive 改为白色，并依靠默认 factor=0
保持现有材质不发光。glTF 没有 Material 时创建或复用规范默认材质，而不是强制使用 Rusted Iron。

### 13.3 TEXCOORD_0/1

Vertex shader 输出两套 UV。每个 Material texture slot 记录 `texCoord`。由于 Vulkan 保证的 push
constant 最小空间只有 128 bytes，而当前结构已经使用 112 bytes，建议最后 16 bytes 增加：

```cpp
alignas(16) glm::uvec4 textureInfo{};
```

`textureInfo.x` 使用 bit mask 表示五个 PBR slot 选择 UV0 或 UV1；`textureInfo.y` 表示当前 Mesh
是否拥有有效 vertex tangent（0 走 derivative fallback，1 走 vertex TBN）；z/w 预留。最终
`DrawPushConstants` 恰好 128 bytes，必须更新 offset/size static_assert 和 GLSL block。

CPU 侧断言 `textureInfo` offset 为 112、结构大小为 128；`shader.vert` 与 `shader.frag` 两个 push
constant block 都在末尾追加同一个 `uvec4`。pipeline range 和 `vkCmdPushConstants()` 继续使用完整
`sizeof(DrawPushConstants)`，并保留现有设备 `maxPushConstantsSize` 检查。

当前 `materialFactors.w` 是预留 float，用于 `normalScale`；已有 `materialFactors.z` 改为
`occlusionStrength`。`Material::normalScale` 与 push constant 的默认值都必须是 `1.0f`，并在每帧
snapshot 中显式写入，不能沿用当前的 `w = 0.0f`。shader 中应使用：

```glsl
vec3 tangentNormal = texture(normalMap, normalUv).xyz * 2.0 - 1.0;
tangentNormal.xy *= normalScale;
tangentNormal = normalize(tangentNormal);

float occlusion = mix(1.0, texture(occlusionMap, uv).r, occlusionStrength);
```

`normalScale` 只缩放 tangent-space normal 的 X/Y，之后再归一化；不能缩放整个 XYZ。不要继续使用
当前 `sample * aoFactor` 的语义，也不要再额外扩张 push constant。可以在 Material
创建时由五个 slot 的 `texCoord` 预计算一个 `std::uint32_t textureUvMask`，并在
`TriangleApplication::drawFrame()` 组装 snapshot 时写到 `view.pushConstants.textureInfo.x`；
Renderer 不负责解释 Material。

暂不支持大于 1 的 texCoord 时必须报告，不要静默改成 0。

### 13.4 Descriptor 原子提交

这是 Stage 5 中风险最高的 shader/descriptor 提交。开始前确保工作树干净。C++ binding、pipeline
layout、descriptor pool、GLSL 和 SPIR-V 必须一起修改。

不要把 descriptor rollback 推迟到第 8 步 UI。提交 6 第一次批量创建 glTF Material 时就启用
`VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`，先在事务局部批量分配并写完本次全部 sets；
只有所有资源成功后才发布 MaterialHandle。发布前失败时释放本批 sets，并按真实分配数恢复容量统计。

验证：

```powershell
.\scripts\compile-shaders.bat
git diff --check
cmake --build --preset windows-mingw-debug --parallel
cmake --build --preset windows-mingw-release --parallel
```

建议提交：

```text
Map glTF PBR materials
```

---

## 14. 第 7 步：Node hierarchy 与 Scene 实例化

### 14.1 Local matrix

对于每个 Node：

- 若提供 matrix，直接转换为 `glm::mat4`；
- 否则按 glTF 规定从 translation、quaternion rotation、scale 生成 local matrix；
- 缺失分量使用规范默认值；
- 不转成 Euler 后再组合。

优先使用 fastgltf 0.9 的 `getTransformMatrix()`/scene traversal 工具。若为了学习而手动转换，必须
记住 glTF quaternion 数组是 XYZW，而 `glm::quat` 的四参数构造顺序是 WXYZ；matrix 数组按 glTF/
GLM 的 column-major 约定复制。两条路径选一条并用同一参考 Node 测试，不要再额外转置或重复组合 TRS。

### 14.2 World matrix

从默认 scene roots 深度优先或广度优先遍历：

```text
gltfWorld(root)  = local(root)
gltfWorld(child) = gltfWorld(parent) * local(child)
assetTransform  = rootConversion * gltfWorld(node)
```

如果资产没有 default scene，采用明确策略：选择 scene 0，或要求 UI 选择；不要遍历所有 Node 导致
未属于当前 scene 的对象也被实例化。

计算 `assetTransform` 后检查 3x3 线性部分 determinant。绝对值接近零时 normal matrix 不可逆；小于
零时当前固定 CCW/back-face-culling pipeline 会把镜像对象剔除。Stage 5 P0 对两者都返回包含 node
index/name 的错误，不静默改 winding，也不只改 tangent handedness 后假装已完整支持。

### 14.3 发布 SceneObject

每个拥有 mesh 的 Node，对其每个 primitive 创建一个 SceneObject：

```text
SceneObject.mesh         = primitive MeshHandle
SceneObject.material     = primitive MaterialHandle
SceneObject.assetTransform = rootConversion * gltfWorld(node)
SceneObject.sourcePath   = glTF path
SceneObject.name         = node/mesh/primitive debug name
```

多个 Node 引用同一个 mesh 时，SceneObject 不同、Transform 不同、MeshHandle 相同。同一 glTF
material index 也复用同一个 MaterialHandle；无 material 的 primitive 复用规范 default Material。

### 14.4 Picking

`getObjectMatrix()` 按第 5.3 节的乘序返回
`T(userPosition) * assetTransform * R(userRotation) * R(autoRotation) * S(userScale)`。picking 仍使用
Mesh local bounds，并通过这个完整 model matrix 转到 world AABB。不要在 importer 中预先把 position
烘焙进 vertex，否则 Mesh 无法在多个 Node 间共享。

建议提交：

```text
Instantiate glTF scene nodes
```

---

## 15. 第 8 步：UI 与导入事务

### 15.1 独立 glTF 入口

保留 OBJ 调试入口，新增：

```text
glTF/GLB Path
Import glTF Scene
Last import summary
Last import warning/error
```

不要让 `.obj`、`.gltf`、`.glb` 共用一个模糊的 `MeshSource::Obj` 分支。

### 15.2 预检

上传前统计：

- 新 Mesh primitive 数量；
- 新 image（区分色彩空间）数量；
- 新 sampler 数量；
- 新 Material 数量；
- 新 SceneObject 数量；
- descriptor pool 剩余容量。

超过 `maxMaterialCount` 时在创建任何 descriptor 前失败。Stage 5 初版可以继续固定上限，但 UI
必须显示容量和错误。

### 15.3 一次性发布

先在局部 vector 中持有所有新 handle 和 SceneObject。全部上传和 descriptor 创建成功后，再一次性
追加到 `textureLibrary`、`samplerLibrary`/sampler cache、`materialLibrary`、mesh cache 和
`sceneObjects`。P0 若只接受缺省 sampler，通常只复用已有默认 handle；P1 新建的 sampler 同样必须
进入这次原子发布。

`8bf1c83` 基线的 descriptor pool 不支持逐个释放 set，因此仅靠 `materialLibrary.size()` 预检不能
保证失败回滚。提交 6 应已采用下面的策略 1；若尚未完成，不得进入本节的全场景发布。可选策略为：

1. 给 pool 增加 `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`，批量分配本次导入的全部
   Material sets；发布前失败时统一 `vkFreeDescriptorSets()`；或
2. 为每个导入资产创建独立 Material descriptor pool，失败时销毁局部 pool，成功后让资产记录
   持有该 pool。

推荐初版采用策略 1，并单独记录实际已分配的 Material set 数量。rollback free 只允许发生在这些
set 尚未发布、从未被 command buffer 引用时；已参与渲染的 descriptor 仍不能立即 free。

如果现有 helper 会立即写入全局 library，应先增加“不发布”的底层 helper，而不是靠失败后手工
pop_back 回滚。

建议提交：

```text
Integrate glTF scene import UI
```

---

## 16. 手动与可重复验证

### 16.1 每个提交

```powershell
git diff --check
cmake --build --preset windows-mingw-debug --parallel
.\build\windows-mingw-debug\vulkan.exe
```

涉及 parser/CPU import：

```powershell
.\build\windows-mingw-debug\gltf_inspect.exe <asset>
```

涉及 shader/descriptor：

```powershell
.\scripts\compile-shaders.bat
cmake --build --preset windows-mingw-release --parallel
```

### 16.2 资产矩阵

至少验证：

1. `.gltf` + 外部 `.bin`；
2. `.gltf` + data URI；
3. `.glb`；
4. indexed uint8；
5. indexed uint16；
6. indexed uint32；
7. non-indexed primitive；
8. interleaved accessor；
9. normalized integer attribute；
10. sparse accessor；
11. 缺失 normal 时生成 flat normal，缺失 UV/tangent 时走明确 fallback；
12. 多 primitive + 多 Material；
13. 多 Node 引用同一个 Mesh；
14. 三层以上 Node hierarchy；
15. rotated parent + translated child + non-uniform positive scale；
16. singular/negative-determinant Node 返回预期 unsupported error；
17. Base Color、packed MR、Normal、AO、Emissive；
18. sRGB/linear 对照；
19. TEXCOORD_0/1；
20. 缺失纹理、缺失 Material 和缺省 sampler；
21. 显式非缺省 sampler 在 P0 返回预期 limitation error；
22. 非法文件和 unsupported extension 的错误路径。

### 16.3 运行时生命周期

1. 导入同一个 glTF 两次，确认 primitive Mesh 是否按设计共享；
2. 删除一个共享实例，其他实例继续渲染；
3. 删除最后引用，等待数帧无 Buffer lifetime Validation 错误；
4. 导入失败后原场景不变；
5. 连续成功/失败导入，descriptor 容量统计正确；
6. resize、最小化、恢复；
7. picking/gizmo 使用正确 Node world transform；
8. 正常退出无 descriptor/image/sampler/buffer 错误。

### 16.4 参考查看器对比

分别检查：

- 模型朝向和 winding；
- UV 是否上下颠倒；
- Normal map 凹凸方向；
- Metallic 与 Roughness 是否通道互换；
- Emissive 是否不受直接光照影响；
- AO 只按设计影响环境项；
- 多 Node 相对位置；
- 多 primitive 是否缺失或材质串位。

将结果写入 Stage 5 验收记录，而不是只用提交标题表示“测过”。

---

## 17. 建议加入的 CPU 测试

Stage 5 是开始补轻量测试的合适时机。即使不测试 Vulkan，也可以测试：

- non-indexed primitive 生成 index；
- uint8/uint16/uint32 转换；
- interleaved accessor；
- normalized attribute；
- sparse accessor；
- 缺失 normal 时拆点并生成 flat normal；
- 缺失 tangent 时记录 `hasTangents == false`；
- bounds；
- Node parent * local；
- matrix 与 TRS 二选一；
- singular/negative determinant Node 拒绝路径；
- packed MR 通道解释；
- texture color-space 分类；
- invalid index/material/child 引用；
- import failure 不发布结果。

测试目标只链接 CPU loader 与 fastgltf，不创建窗口和 Vulkan instance。

---

## 18. 常见错误

### 18.1 把 glTF Mesh 当成一个现有 Mesh

结果：多个 primitive 的 Material 丢失或 index range 混乱。初版应一个 primitive 对应一个 GPU
Mesh，再在 glTF mesh/node 层组合。

### 18.2 忽略 accessor stride/offset

结果：简单 Triangle 正常，interleaved asset 顶点爆炸。使用 accessor tools，不做错误的连续 memcpy。

### 18.3 只支持 uint32 input index

大量 glTF 使用 uint16。正确做法是读取规范允许的 component type，再统一扩展为 CPU uint32。

### 18.4 对 non-indexed primitive 调用空 index upload

当前 `Mesh` 要求有效 index buffer。应生成顺序 index，而不是让 primitive 消失。

### 18.5 Metallic/Roughness 通道读反

glTF packed MR 是 G=Roughness、B=Metallic，不是 R/R，也不是 G/B 相反。

### 18.6 所有图片都用 sRGB

Normal/MR/AO 进入 sRGB 解码后数值会改变，材质外观明显错误。

### 18.7 同一 image 不区分用途缓存

同一个 image index 可能被不同色彩空间用途引用。cache key 必须包含格式/色彩空间。

### 18.8 重复翻转 UV 或坐标

OBJ 的 UV 修正不能直接复制到 glTF；根坐标转换也只能应用一次。使用带文字/方向标记的纹理验证。

### 18.9 把 Node 变换烘焙进共享 vertex

这样多个 Node 引用同一个 Mesh 时会互相冲突，也破坏 local bounds。Node transform 应留在实例层。

### 18.10 matrix 和 TRS 同时相乘

glTF Node 两者互斥。同时使用会让 transform 重复。

### 18.11 parser 类型进入 Renderer

Renderer 若持有 fastgltf Node/Material，未来换 parser 或支持程序生成资产时会被第三方库绑死。

### 18.12 导入中途直接写全局 library

后半段失败会留下半成品、descriptor 泄漏或不可见资源。先局部构建，最后一次性发布。

### 18.13 第一提交直接加载 DamagedHelmet

大型 PBR 资产同时涉及 GLB、image、tangent、packed MR 和 Node。黑屏时无法定位。先从 parse-only
和最小 geometry asset 开始。

---

## 19. Stage 5 完成标准

### P0：初版必须完成

- [ ] fastgltf 依赖与 parse-only inspection 工具已建立；
- [ ] `.gltf` 和 `.glb` 都能解析；
- [ ] external/embedded/GLB buffer 能读取；
- [ ] accessor offset、stride、type、normalized 和 sparse 通过工具正确处理；
- [ ] indexed 与 non-indexed triangle primitive 都能导入；
- [ ] uint8/uint16/uint32 index 输入统一转换为 GPU uint32 路径；
- [ ] position、normal、UV、COLOR_0 和 tangent 已接入；
- [ ] 缺失 normal 会生成 flat normal；缺失 tangent 会通过 Mesh flag 选择 derivative fallback；
- [ ] TEXCOORD_0/1 可按 Material texture slot 选择，缺失请求的 UV set 会明确失败；
- [ ] 多 primitive 可使用不同 Material；
- [ ] 多 Node 引用同一 Mesh 时共享 GPU Mesh；
- [ ] Node hierarchy 和非奇异、非镜像 local/world matrix 正确；镜像/奇异 Node 明确拒绝；
- [ ] `.gltf` URI、data URI 和 GLB image 能解码；
- [ ] Base Color/Emissive 与线性数据纹理使用正确 VkFormat；
- [ ] packed Metallic-Roughness 正确读取 G/B；
- [ ] Base Color、Normal、MR、AO、Emissive texture/factor 生效；
- [ ] 缺失 texture/material 使用正确默认资源；
- [ ] 缺省 sampler 语义正确；非缺省 sampler 在 P0 明确拒绝而非静默替换；
- [ ] 导入前预检 Material descriptor set 与 combined-image descriptor 的剩余容量；
- [ ] 导入失败不污染现有 scene/library；
- [ ] OBJ 调试入口仍可用；
- [ ] Debug/Release 构建通过；
- [ ] 多 Node、多 primitive、多 Material 场景通过手动验证；
- [ ] 无新增 Vulkan Validation 错误。

### P1：建议完成

- [ ] glTF sampler wrap/filter/mipmap 完整映射并缓存；
- [ ] 为缺失 tangent 的 triangle 生成 tangent，取代 derivative fallback；
- [ ] Mesh UI 显示稳定 glTF primitive key；
- [ ] texture/mesh 的可选预算上限与完整资源统计已加入预检/UI；
- [ ] `gltf_inspect` 输出完整 import summary；
- [ ] 至少加入 accessor 与 Node transform CPU 测试；
- [ ] resize/minimize/restore 与重复导入验收通过；
- [ ] 与 Khronos Sample Viewer 外观基本一致；
- [ ] 验收结果保存在文档中。

### 延期项

- [ ] Alpha mask/blend；
- [ ] Double-sided pipeline variant；
- [ ] KHR_texture_transform；
- [ ] KTX2/Basis Universal；
- [ ] Draco/Meshopt；
- [ ] Skin 与骨骼动画；
- [ ] Morph target；
- [ ] Animation channel；
- [ ] 后台线程、streaming 和异步 transfer；
- [ ] 通用 Asset Manager 与热重载。

---

## 20. 第一次实际手敲任务

Stage 5 的第一个生产提交只做 parser inspection：

1. 在 `vcpkg.json` 增加 `fastgltf`；
2. 在 CMake 增加 `find_package(fastgltf CONFIG REQUIRED)`；
3. 新建独立 `gltf_inspect` target；
4. 读取一个最小 `.gltf` 和一个最小 `.glb`；
5. 检查所有 `Expected` error；
6. 对成功解析的 `Asset` 调用 `fastgltf::validate()`；
7. 输出 scene/node/mesh/primitive/material/image/accessor 数量；
8. 对无效路径或无效资产返回非零 exit code；
9. Debug 构建并继续启动原 Vulkan 程序；
10. 提交 `Integrate fastgltf parse inspection`。

第一次提交明确禁止：

- 修改 `Vertex`；
- 创建 Mesh Buffer；
- 解码图片；
- 修改 Material；
- 修改 descriptor/shader；
- 把 fastgltf 类型放进 Renderer；
- 一开始导入大型完整场景。

完成后先检查工具输出是否与 glTF JSON/参考查看器一致，再进入 accessor 解码提交。
