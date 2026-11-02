# Stage 1 → Stage 2：CSM 收口、配置归档与 Buffer 内存契约手敲指南

对应 [Stage 2 准入边界](stage-2-deferred-tiled-lighting.md#13-准入与收口边界)。核对日期：2026-11-02，基线为 `289fbc5` 加当前未提交的 CSM 诊断/采集改动。

本文供你逐组手敲，下面的新接口与代码均为**待实现步骤**，没有代你修改生产代码，也没有宣称专项验收已通过。顺序为：**先排除内存契约问题 → 固定并归档条件 → 验 CSM 画质与生命周期 → 采性能 → 接 G-buffer**。

目标是保住当前 Forward/CSM 基线。继续使用两飞行帧、现有 fence/提交 ID、120 次提交预热和 300 行配对；不重做 CSM、不引入 JSON 依赖、场景保存系统或完整 Readback 管线。

## 第 0 步：保存修改前的证据

**目的：留下可回退的代码和运行参照，避免修了 Buffer 后才发现无法解释前后的差异。**

在项目根目录执行；示例目录名可换成本次实际日期/实验编号：

```sh
mkdir -p docs/validation/2026-11-02-csm
git rev-parse HEAD > docs/validation/2026-11-02-csm/commit.txt
git status --short > docs/validation/2026-11-02-csm/worktree-status.txt
git diff HEAD --binary > docs/validation/2026-11-02-csm/tracked-working-tree.patch
cmake --build --preset linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
./build/linux-debug/vulkan --scene shadow-playground --frames 120 \
    > docs/validation/2026-11-02-csm/before-debug.log 2>&1
```

这里用 `git diff HEAD` 同时包含相对 HEAD 的暂存/未暂存修改，但仍不含未跟踪文件；当前至少还要复制 `src/render/ShadowFrameSummary.hpp` 到记录目录，并在说明中记下原路径。源码中的注释、构建成功或这 120 帧都不能代替视觉验收。保存一张默认 Lit 图，记录 framebuffer extent/实际 Scene samples。

**完成点：** 能明确找到“改前源码是什么、改前运行是否正常”。不需要先完成全部截图。

## 第 1 步：把 Buffer 的请求属性与实际属性分开

**目的：先查清 VMA 实际分配了哪类内存，避免把代码里的 `HOST_COHERENT` 请求当成分配结果。**

当前 `VulkanContext::createBuffer()` 根据 HOST_VISIBLE 选择分配倾向，但没有设置 `allocInfo.requiredFlags`；`GpuBuffer::requiredMemoryProperties()` 返回的只是请求值。这是确定的接口缺口，**不是已经证明本机发生了缓存故障**。

修改 [VulkanResources.hpp](../../src/vulkan/VulkanResources.hpp)，在 `GpuBuffer` 保留原字段，再新增：

```cpp
// private
VkMemoryPropertyFlags memoryProperties_ = 0;

// public
VkMemoryPropertyFlags memoryProperties() const noexcept;
```

给现有有参构造函数的声明/定义追加最后一个参数：

```cpp
VkMemoryPropertyFlags actualMemoryProperties
```

在初始化列表添加 `memoryProperties_(actualMemoryProperties)`。在 [VulkanResources.cpp](../../src/vulkan/VulkanResources.cpp) 补齐：

```cpp
VkMemoryPropertyFlags GpuBuffer::memoryProperties() const noexcept
{
    return memoryProperties_;
}

// reset() 末尾与其他属性一起清零
memoryProperties_ = 0;

// moveFrom() 与其他字段一起转移
memoryProperties_ = std::exchange(other.memoryProperties_, 0);
```

上面最后两行分别放进现有方法，不能直接放在全局。新增字段必须随 RAII 所有权一起 move/reset。

在 [VulkanContext.cpp](../../src/vulkan/VulkanContext.cpp) 的 `vmaCreateBuffer()` 成功之后查询：

```cpp
VkMemoryPropertyFlags actualMemoryProperties = 0;
vmaGetAllocationMemoryProperties(
    allocator_, rawAllocation, &actualMemoryProperties);
```

在该函数最后的 `GpuBuffer(...)` 构造调用中追加 `actualMemoryProperties`。这是当前这类有参构造的调用点。

先临时打印 UBO/staging 的请求 flags 与实际 flags；用 `actual & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` 和 `actual & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` 解释结果，不靠十六进制数字猜含义。先编译运行一次，记录修改分配策略之前的实际结果。

**完成点：** 请求与实际结果可以分别查询；move 后新对象保留属性，旧对象清零。若本机实际 coherent，记录这一事实，不写“已复现非 coherent 故障”。

## 第 2 步：落实 requiredFlags

**目的：让调用者写下的“必需属性”成为 VMA 必须满足的约束。**

在 `VulkanContext::createBuffer()` 初始化分配信息处增加一行，原 host/device 分支继续保留：

```cpp
VmaAllocationCreateInfo allocInfo{};
allocInfo.requiredFlags = desc.requiredMemoryProperties;

if (desc.requiredMemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
{
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
}
else
{
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
}
```

`AUTO_PREFER_HOST/DEVICE` 是倾向，`requiredFlags` 是硬要求。原 mesh destination 的 DEVICE_LOCAL 也会真正成为硬要求；不满足时让现有 `VK_CHECK(vmaCreateBuffer(...))` 报错，不静默改变语义。

仍在 `VulkanContext::createBuffer()` 内，将断言放在 `vmaGetAllocationMemoryProperties(...)` **之后**，直接使用刚查询到的变量：

```cpp
assert((actualMemoryProperties & desc.requiredMemoryProperties) ==
       desc.requiredMemoryProperties);
```

`createBuffer()` 这里没有名为 `buffer` 的局部对象，不能调用 `buffer.memoryProperties()`。不要在刚拿到裸 VMA allocation 后为了诊断抛异常而忘记销毁它。当前四处 host buffer 仍请求 HOST_VISIBLE | HOST_COHERENT 时，到这里便已形成一种正确但受限的 coherent-only 策略。

**完成点：** 原场景正常运行，实际 flags 包含全部 requested flags。不支持某要求时明确分配失败。

## 第 3 步：在 GpuBuffer 封装缓存维护

**目的：允许上层写入非 coherent 映射内存，同时把 VMA allocation 的细节保留在资源封装内。**

推荐继续补完整上传契约；这比只强制 coherent 更灵活。VMA 明确指出 map/unmap 不会自动执行 flush/invalidate，coherent 分配上的缓存维护调用会成为无操作。[VMA 内存映射说明](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html)

在 `GpuBuffer` 的 public 中声明：

```cpp
VkResult flush(VkDeviceSize offset = 0,
               VkDeviceSize byteSize = VK_WHOLE_SIZE) noexcept;
VkResult invalidate(VkDeviceSize offset = 0,
                    VkDeviceSize byteSize = VK_WHOLE_SIZE) noexcept;
```

在 `.cpp` 定义：

```cpp
VkResult GpuBuffer::flush(VkDeviceSize offset, VkDeviceSize byteSize) noexcept
{
    assert(allocator_ != nullptr && allocation_ != nullptr);
    assert(mappedData_ != nullptr);
    assert(offset <= size_);
    assert(byteSize == VK_WHOLE_SIZE || byteSize <= size_ - offset);
    return vmaFlushAllocation(allocator_, allocation_, offset, byteSize);
}

VkResult GpuBuffer::invalidate(VkDeviceSize offset, VkDeviceSize byteSize) noexcept
{
    assert(allocator_ != nullptr && allocation_ != nullptr);
    assert(mappedData_ != nullptr);
    assert(offset <= size_);
    assert(byteSize == VK_WHOLE_SIZE || byteSize <= size_ - offset);
    return vmaInvalidateAllocation(allocator_, allocation_, offset, byteSize);
}
```

这些接口的前置条件是 buffer 有效且已映射，范围在 buffer 内；这里沿用项目的 assert 风格。offset 相对于 allocation 开头，**不要再加 allocation 的设备内存偏移**；VMA 会处理 `nonCoherentAtomSize` 对齐，调用者不再做第二套对齐。

当前没有 GPU→CPU 读回消费者，`invalidate()` 可以封装好，但本轮不必制造调用点。它不会等 GPU，不能用它代替 fence。

**完成点：** 声明、定义与调用签名一致，当前构建通过；对 coherent 分配调用 flush 不出错。

## 第 4 步：在全部四处 CPU 写入后接 flush

**目的：让每次 CPU 数据写入在提交给 GPU 前完成缓存维护，避免只修了 UBO 而漏掉资源上传。**

先保留 coherent 请求接好调用，确认能运行；然后将这四个 host descriptor 的 `requiredMemoryProperties` 改成仅 HOST_VISIBLE。顺序写入的 VMA flag 继续保留，DEVICE_LOCAL destination 不改。

| 当前位置 | 放在 memcpy 后的调用 | 必须早于 |
|---|---|---|
| `src/app/Buffers.cpp`，`Application::updateUniformBuffer()` | `VK_CHECK(uniformBuffers[currentImage].flush(0, sizeof(ubo)));` | 本帧 queue submit |
| `src/render/Renderer.cpp`，`Renderer::uploadBuffers()` | `VK_CHECK(stagingBuffer.flush(0, request.size));` | unmap 与 upload submit |
| `src/app/ImageResources.cpp`，`Application::uploadTexture2D()` | `VK_CHECK(stagingBuffer.flush(0, imageSize));` | unmap 与 upload submit |
| `src/render/EnvironmentSkybox.cpp`，`Environment::createSkyboxImage()` | `VK_CHECK(stagingBuffer.flush(0, pixels.imageSize()));` | unmap 与 upload submit |

三处 staging 的顺序均为：

```cpp
VK_CHECK(stagingBuffer.map(&data));
std::memcpy(data, source, byteCount); // 保留该处原有实参
VK_CHECK(stagingBuffer.flush(0, stagingBuffer.size()));
stagingBuffer.unmap();
```

表中给了精确 byte size；上面的整 buffer 写法适用于当前三处“整份 staging 完整填充”。不要把占位 `source/byteCount` 原样粘贴。

UBO 继续持久映射，仅在现有 `memcpy(...,&ubo,sizeof(ubo))` 后加 flush。`currentImage` 在这里实际是 **frame slot**；不要换成交换链 imageIndex。保留 `beginFrame()` 等该槽 fence 的顺序。

更新两类旧注释：“请求 coherent 就无需 flush”“unmap 后 GPU 自动可见”都不能作为新契约。无需在四个调用点分别判断 coherent，VMA 内部已有判断。

flush 管 CPU cache，GPU barrier 管设备访问依赖，fence 管完成/复用，这三者分别核查。不要把 flush 放到只接 raw VkBuffer 的 `UploadCommands`；也不为修缓存把普通每帧路径改成 `vkDeviceWaitIdle()`。

**完成点：** 四处都接好；重载纹理模型和环境图仍正常。仅取消 coherent 要求不会强迫 VMA 选非 coherent 内存，本机没选到时应记“非 coherent 设备路径待验收”。

## 第 5 步：对 Buffer 改动做局部回归

**目的：在 CSM 画质检查前确认上传和帧槽写入没有退化，使后续视觉问题有清楚的定位边界。**

```sh
cmake --build --preset linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
./build/linux-debug/vulkan --scene shadow-playground --frames 120
./build/linux-debug/vulkan --scene damaged-helmet --frames 120
cmake --build --preset linux-release --parallel
ctest --test-dir build/linux-release --output-on-failure
```

再交互执行一次 `Material Lab → Helmet → Shadow Playground`，确认 mesh、纹理、天空盒均正常；记录两个 UBO 槽的实际 flags。现有六项 CTest 不会自动验证 VMA 的非 coherent 硬件路径；Validation 无报错也不能证明 host 写入已 flush，必须检查实际分配属性和四处调用。

已有 `immediateSubmit()` 返回前等 upload fence，可继续保证临时 staging 的生命周期。它不等于已证明所有 GPU 消费者的可见性；另外检查 copy 后到 vertex/index read 的设备依赖，若缺失单独补同步，不用 flush 代替它。[Khronos 同步示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)

未来 Tiled readback 再扩 Readback 分配意图与 HOST_ACCESS_RANDOM；当前 G-buffer 的 GPU 写/采样本身不要求 CPU Readback，因此不把该扩展拖进本轮前置工作。

**完成点：** 记录“代码路径已接好”和“当前设备实际覆盖了什么”，不把本机 coherent 成功扩写成所有设备已验证。

## 第 6 步：修正格式标签，补最小验收控件

**目的：让归档记录真实格式，并使距离/分割/覆盖条件可以明确设置。**

先在 [Diagnostics.cpp](../../src/ui/Diagnostics.cpp) 修正一行：

```cpp
case VK_FORMAT_D16_UNORM:
    return "D16_UNORM";
```

当前 Shadows 面板有 mode/blend/PCF/bias/preview，没有 distance/lambda/Single box 控件。在 [EditorUI.cpp](../../src/ui/EditorUI.cpp) 的 Shadows tab 增加独立区块：

```cpp
ImGui::SeparatorText("Shadow coverage");
ImGui::DragFloat("Shadow distance", &shadows.shadowDistance,
                 0.5f, std::min(camera.nearPlane + 0.01f, camera.farPlane), camera.farPlane,
                 "%.2f", ImGuiSliderFlags_AlwaysClamp);
ImGui::SliderFloat("Split lambda", &shadows.splitLambda,
                   0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
ImGui::DragFloat3("Single center", &shadows.center.x, 0.1f);
ImGui::DragFloat("Single half extent", &shadows.halfExtent,
                 0.1f, 0.1f, 10000.0f, "%.2f",
                 ImGuiSliderFlags_AlwaysClamp);
```

将 lambda 控件在 Single 时置灰，Single box 在 Cascaded 时置灰；distance 是两模式共同接收远界的输入，实际 receiver far 查看 Diagnostics。不要在这里重建阴影图，它们的分辨率/层数没有改变。

还需在 `updateUniformBuffer()` 内、第一次使用 shadowDistance 之前统一合法化，而非仅靠滑块限制。相机本身仍由已有 Camera/clipping 检查保证有限的 `0 < near < far`，在此基础上补：

```cpp
// 与当前 Camera clipping UI 的最小间隔一致，避免把四个 split 挤在相邻浮点数内。
const float minimumShadowDistance =
    std::min(camera.nearPlane + 0.01f, camera.farPlane);
if (!std::isfinite(shadows.shadowDistance))
    shadows.shadowDistance = camera.farPlane;
shadows.shadowDistance =
    std::max(shadows.shadowDistance, minimumShadowDistance);
const float effectiveShadowDistance =
    std::min(camera.farPlane, shadows.shadowDistance);
```

用这段替换原 `effectiveShadowDistance` 定义，不再重复声明。较大的请求距离仍允许由 far 截断；near 调高导致请求失效时抬高请求下界，并让本次快照记录修正后的值。新增 `near` 提高、`far` 缩小、NaN/Inf distance 三个边界检查，确认不会把无效区间送进 `calculateShadowCascadeFrame()`。本例沿用当前相机控件 near≤10、far≥near+0.01 的尺度范围；以后扩展极端尺度时需再验证分割浮点精度，不能仅用一个 ULP 的间隔承载四个 split。

采集 UI 的 Start 已限制 Auto Rotate；相机/灯光/材质仍要人工固定。为了同次运行恢复相机，可在 `Application` 增加 `std::optional<Camera> csmAuditCamera`，在 Camera tab 增加两个按钮：

```cpp
if (ImGui::Button("Remember audit camera"))
    csmAuditCamera = camera;
ImGui::BeginDisabled(!csmAuditCamera.has_value());
if (ImGui::Button("Restore audit camera") && csmAuditCamera)
{
    diagnostics.baseline.cancel("Capture cancelled: audit camera restored.");
    diagnostics.startRequested = false;
    camera = *csmAuditCamera;
    cameraController.reset(window);
}
ImGui::EndDisabled();
```

这是运行内书签，不是磁盘场景存档。加载新预设后清空书签，避免误恢复另一场景的相机；跨运行恢复使用已记录的 position/target 与 `camera.lookAt()`，不要只改 yaw 却遗漏 front。

**完成点：** 可以固定相机、distance/lambda/Single box；修改受跟踪阴影参数会取消采集。格式标签与 VkFormat 数字相符。

## 第 7 步：冻结采集上下文，禁止导出时读取当前 UI

**目的：把“这份数据当时怎样采的”随采集保存，避免采完后改了界面，导出却写入新配置。**

已有 `BaselineCapture::start(firstId, config)` 会复制 `FrameTimingConfig`，成功提交后才调用的时机正确。保留它；补一份字符串形式的冻结上下文即可，不必新增 JSON 库或场景存档系统。

在 [BaselineCapture.hpp](../../src/diagnostics/BaselineCapture.hpp) 添加成员并扩展签名：

```cpp
std::string frozenContext;

void start(std::uint64_t firstId,
           const FrameTimingConfig& capturedConfig,
           std::string capturedContext = {})
{
    *this = BaselineCapture{};
    firstSubmissionId = firstId;
    config = capturedConfig;
    frozenContext = std::move(capturedContext);
    samples.reserve(targetSamples);
    running = true;
}
```

增加 `<utility>`。默认空参数用于保留现有纯计时测试，生产入口必须传上下文。

在 `Application.hpp/.cpp` 声明并实现 `std::string buildBaselineContext() const`。用 `std::ostringstream`、`std::locale::classic()` 和 `std::setprecision(std::numeric_limits<float>::max_digits10)` 写纯文本 `key=value`。至少逐项记录：

| 内容 | 当前可用来源 |
|---|---|
| 相机 position/front/up、yaw/pitch、near/far/FOV、view/proj | `camera` 与本帧 swapchain extent；矩阵说明 GLM 列序 |
| 方向光 enabled/原方向/color/intensity，ambient/IBL | `scene.directionalLight`、`scene.ambientLight*`、`scene.iblIntensity`；注明方向归一化/零向量兜底规则 |
| 全部 point lights | `scene.pointLights` 的 position/color/intensity/range/enabled |
| 对象与材质状态 | 对象名/sourcePath/modelMatrix/autoRotate；材质名、alphaMode/cutoff/doubleSided、baseColor/M/R/AO/normalScale/emissive factors、纹理资源名/UV |
| 设备/构建/API | `vkGetPhysicalDeviceProperties(context.physicalDevice(),...)`、`context.capabilities()`、`VULKAN_PROJECT_ENABLE_VALIDATION`、构建配置记录 |
| 资源来源/场景版本 | 预设 ID、固定资产版本；commit/dirty patch、GPU driver、VSync/限帧及文件名在外部 run-info 中补齐 |

字符串值用 `std::quoted()` 输出，数字保留足够精度；这是供人复现实验的文本，不能宣称是已实现的完整场景反序列化格式。driverVersion 可原样记录为整数，不直接套用 API 版本解码公式。

在 [Application.cpp](../../src/app/Application.cpp) 的 `updateUniformBuffer()` 后、`recordFrame()` 前，为本次 Start 请求建立局部快照：

```cpp
std::string requestedCaptureContext;
if (diagnostics.startRequested)
    requestedCaptureContext = buildBaselineContext();
```

在现有成功提交分支把它传入：

```cpp
diagnostics.baseline.start(
    diagnostics.latestCpuTiming.renderer.submissionId,
    diagnostics.latestCpuTiming.renderer.config,
    std::move(requestedCaptureContext));
```

保持 `consume(oldGpu) → 更新/录制/成功提交 → start → rememberCpu` 的顺序。Exporter 只能读 `baseline.config/frozenContext/samples`，不能再读 live camera、scene 或当前阴影设置。

**完成点：** Start 时形成副本，之后修改调用者变量不会改变这份记录。冻结起点不等于整个采集中自动锁住相机/灯光，当前仍需人工不编辑。

## 第 8 步：把 CSV 与配置成套导出

**目的：保持原 16 列数值 CSV 可用，同时让每份 CSV 有可查的持久化配置。**

在 `BaselineCapture.hpp` 保持 `saveCsv()` 原格式，新增：

```cpp
bool saveMetadata(const char* path) const;
bool saveBundle(const char* csvPath) const;
```

`saveMetadata()` 先检查 `complete`，再打开文件；使用 classic locale 与 `max_digits10`（需要 `<limits>`），写入：

```text
schema_version=1
warmup_first_submission=...
first_measured_submission=...
last_measured_submission=...
warmup_submissions=120
target_samples=300
actual_samples=300
dropped_samples=...
[frame_config]
...逐项输出冻结的 config...
[capture_context]
...输出 frozenContext...
```

其中 first/last measured 从 `samples.front()/back()` 取，不能用 firstId+120/+419 推算，因为允许丢样。枚举同时保存名称与原始整数，未知值写 Unknown+整数，不伪装成 Single/D32。

`[frame_config]` 必须覆盖 `FrameTimingConfig` 的全部字段，按以下组逐一输出，不以导出时的 UI 替代：

- sceneId、extent、实际 sceneSamples。
- shadowEnabled/mode/preview/previewLayer、PCF、constantBias/slopeBias、Single receiver bias、CSM world receiver bias、blendFraction。
- shadowDistance、lambda、depthMargin、lightNear、Single center/halfExtent。
- shadowFrame 的 activeCascadeCount、cameraNearDistance、receiverFarDistance、cameraFovDegrees、四个最终 receiverBiasNormalized。
- shadowTarget 的真实 extent/format/arrayLayers；**有效 cascade 数与分配层数分开**。
- uiDrawn、bypassToneMapping、EV/toneMapper、debugView、debugDepthNear/Far。

可在类内先写一个小型输出函数减少手误：

```cpp
const auto writeVec4 = [&](const char* key, const glm::vec4& value)
{
    file << key << '=' << value.x << ' ' << value.y << ' '
         << value.z << ' ' << value.w << '\n';
};
file << "scene_id=" << std::quoted(config.sceneId) << '\n';
file << "shadow_mode_value=" << static_cast<std::uint32_t>(config.shadowMode) << '\n';
file << "active_cascades=" << config.shadowFrame.activeCascadeCount << '\n';
file << "allocated_shadow_layers=" << config.shadowTarget.arrayLayers << '\n';
file << "shadow_format_value=" << static_cast<std::int32_t>(config.shadowTarget.format) << '\n';
writeVec4("receiver_bias_normalized", config.shadowFrame.receiverBiasNormalized);
```

示例展示输出方式，不代表只输出这几项就算完成；按上方清单逐字段补全。最终 `file.flush(); return file.good();`，写失败保留采集数据以便重试。

`saveBundle()` 可保持简单，不声称两个文件有原子事务：

```cpp
bool saveBundle(const char* csvPath) const
{
    if (!complete || csvPath == nullptr || csvPath[0] == '\0')
        return false;
    if (!saveCsv(csvPath))
        return false;
    const std::string metadataPath = std::string(csvPath) + ".config.txt";
    return saveMetadata(metadataPath.c_str());
}
```

UI 的 Export 按钮改调 `saveBundle()`，成功提示“两文件已保存”；失败提示“导出未完整完成，可能已有部分文件，可重试”，不能只写 CSV saved。实验目录使用新的、不覆盖旧证据的路径；父目录先创建。

**完成点：** `single.csv + single.csv.config.txt` 成套存在，所有数值能解释，两个文件都成功才报告完整成功。

## 第 9 步：只补会保护归档契约的测试

**目的：防止最危险的“导出成功但配置属于另一帧/另一模式”，不重复测试已经覆盖的 16 列 CSV。**

在 [BaselineCaptureTests.cpp](../../tests/BaselineCaptureTests.cpp) 保留现有 `testCsv/testConfigChangeCancels/testCancelAndRestart`，复用现有 `TemporaryDirectory` 与 `completeCapture()`，新增：

| 新用例 | 必须断言 |
|---|---|
| 完整导出 | 300 行/16 列仍不变；侧车存在，首/末有效 ID 等于真实 samples，所有字段可查 |
| 起点冻结 | start 后修改传入 config/context 原变量，导出仍为旧值；完成后修改 live 状态也不影响 |
| Single/CSM 区分 | Single 的 active=1、allocated=4；CSM 的 active=4、allocated=4；不可把两者混为一项 |
| 格式 | D16 名称和值正确，D32 同理；不能复用之前错误 UI 标签 |
| 不完整采集 | `saveBundle` 返回 false，两个文件都不创建 |
| 部分失败与重试 | 预先创建同名 `.config.txt` 目录使侧车写入失败，返回 false，complete/samples/config/context 不变；换路径可成功 |

先写断言、运行看到新接口/行为不满足，再实现上述导出逻辑并通过。不要删除原 CSV 失败/重试测试来换取通过。context 是纯文本，检查带空格/中文/引号的资源名仍完整保留。

```sh
cmake --build --preset linux-debug --target baseline_capture_tests
ctest --test-dir build/linux-debug -R baseline_capture_tests --output-on-failure
```

**完成点：** 新增用例保护冻结、模式、格式、失败恢复；原采集逻辑仍通过。

## 第 10 步：先验静态画面和覆盖范围

**目的：区分阴影精度改善与“CSM 少画了一部分接收区域”，建立公平的 Single/CSM 参照。**

加载 Shadow Playground 并 Reset，停止动画，固定窗口与相机，按 Remember audit camera 保存起点。默认相机约为 `(14,-22,14)` 看向 `(0,15,0)`，光传播方向为 `(-0.6,0.7,-1)`；实际配置仍以快照为准。Reset 不会保证把全部 CSM 参数恢复默认，因此显式设置 mode/distance/lambda/blend/PCF/bias。

- 显示 `Shadow cascades`：确认由近到远的级别顺序；远于有效 shadow distance 的表面不应假装仍属于第四级。
- 打开 `Show shadow depth`，依次检查 0/1/2/3；相同投射物在不同矩阵下的覆盖应能解释，不是四张完全相同的图。
- 回 Lit，关 preview，先 PCF off，再 on。观察 Near box、门框、斜坡、Far box 的边缘与接触处。
- Single 与 CSM 使用同一相机/光照/near/far/接收物；记录 Single center/halfExtent，确认 Single 中同一批接收物与影子也在覆盖内。

`min(camera.farPlane, shadowDistance)` 是当前 Single/CSM 共同的接收远界，不会自动使 Single 的手动光源正交盒与四个视锥段覆盖相同。若要严格同投影覆盖，另做全 `[near,far]` 拟合的单图实验；本轮不能拿缩小 shadowDistance 或漏画 Far box 当作 CSM 提升。

**完成点：** 保存同机位 Single/CSM 截图及配置，能够指出近景改善，并能解释远端覆盖。此时还没有证明运动稳定。

## 第 11 步：分别验运动、接缝、bias、相机外 caster 与材质

**目的：一次改变一个因素，定位 CSM 最常见的跳影、漏影与材质退化。**

| 子项 | 具体操作 | 观察/通过点 |
|---|---|---|
| 稳定性 | 相同起点，降低 Camera 的移动速度；先只慢平移，再只慢旋转，录制短视频 | 不能出现整块阴影周期性跳动；允许真实透视/遮挡变化，记录仍存在的亚像素闪烁 |
| 接缝 | 先用 cascade 颜色找到边界，再回 Lit；固定 PCF/bias，比较 blend=0 与 0.15/0.25，慢移通过同一边界 | 无明显亮/暗带、缺口或突然切换；过渡不能靠极大 blur 遮掩 |
| Bias | 固定其他条件，constant/slope/receiver 每次只改一个；近箱接触处与斜坡都检查 | acne 与悬浮取舍可解释；Single receiver 是深度单位，CSM receiver 是世界单位，不要求输入数字相同 |
| Mask | 用实际 Sponza 镂空叶片/栏网，观察主画面孔洞和落地影子，Single/CSM、PCF off/on 对照 | 不变成实心矩形阴影；深度预览中的孔洞语义相符 |
| 双面 | 对实际薄面从正背两侧看，并改变光方向照射背面 | 材质与影子不因背面绕序整片消失；普通封闭 box 不足以证明双面正确 |

当前 Inspector 没有 alphaMode/cutoff/doubleSided 编辑控件，不要寻找不存在的复选框。Sponza 有同时 MASK+doubleSided 的材质；若混合因素难定位，单独准备薄片 fixture，再按现有导入链验证。Blend 不属于当前支持范围。

**相机外 caster 的一个具体实验：** Reset Shadow Playground，选 Near box，将 Inspector Position 改为 `(12,-4,20)`，保持原 `.75` 缩放。按默认光方向，它向 z=0 地面的投影中心约为 `(0,10,0)`；默认机位下 caster 应在上方画面之外，而接收地面仍在 50 的范围内。先实际确认这一空间关系，再观察 Lit 与末级 depth，慢转相机让 caster 进入/离开画面。

期望影子连续存在，不能因 caster 离开相机视锥就消失。这个例子检查整场景 caster 深度范围；不要删掉物体后再把正常消失的影子称作漏影。做完 Reset，恢复基准机位与参数再采性能。

**完成点：** 每项各有“条件、操作、截图/视频、结果/待办”，发现问题先修相应模块；不把 CPU 数学测试通过当作视频已经通过。

## 第 12 步：启用同步验证并做生命周期回归

**目的：在附件数量增加前检查现有多层阴影、帧槽与资源重建边界。**

Linux 的现行 Validation Layers 可通过 `VK_VALIDATION_VALIDATE_SYNC=1` 开启同步验证；同时确保程序确实启用 `VK_LAYER_KHRONOS_validation`，确认本机版本/配置未忽略该设置。旧版 SDK 可用 vkconfig 的对应设置。[Khronos SyncVal 用法](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/syncval_usage.md)

```sh
VK_VALIDATION_VALIDATE_SYNC=1 \
./build/linux-debug/vulkan --scene shadow-playground \
    > docs/validation/2026-11-02-csm/sync-validation.log 2>&1
```

在这次交互运行中顺序做：

- Single/CSM 来回切换，PCF/blend/preview 层反复改。
- 窗口 1920×1080 → 1280×720 → 恢复；记录实际 framebuffer extent，别只记窗口逻辑尺寸。
- 真正最小化数秒再恢复，确认跳帧/恢复正常；不能用 resize 代替此项。
- Shadow Playground → Material Lab → Helmet → Sponza → 返回；场景切换失败时仍保留可用旧场景。
- 在可丢弃的构建目录资源副本中模拟一次缺失模型/纹理，记录明确失败提示与清理；恢复文件，不能改源资产冒充测试。
- 正常退出，检查销毁阶段；本轮未做的启动中途失败/其他平台测试明确写待验收。

搜索 `Validation Error`、`VUID-`、`SYNC-HAZARD` 并阅读全文，不能把所有 loader 信息都当错误，也不能把无 VUID 当作没有同步错误。SyncVal 有覆盖限制，尤其不会完整追踪 host 内存访问，因此第 1–5 步不能省。

**完成点：** 无未解释的布局/同步/生命周期错误；未覆盖的失败点、工具与平台如实标记。

## 第 13 步：最后采集性能，并检查导出文件

**目的：让性能差异对应已验收的同一批条件，而不是边调参数边看 FPS。**

使用 Release，Validation 设置保持一致，回到同一个记录机位和对象状态：Lit、关闭 depth preview、所有 Auto Rotate 关闭、相同 UI 布局、EV、toneMapper、分辨率与实际 samples。不要在采集中移动相机/灯光/物体或编辑材质。

按现有 `Diagnostics → Capture → Start capture`：等待 120 次提交预热和 300 行有效 CPU/GPU 配对，再导出。分别保存：

```text
single.csv                 + single.csv.config.txt
csm-blend0.csv             + csm-blend0.csv.config.txt
csm-blend015.csv           + csm-blend015.csv.config.txt
```

各组只改变文件名声明的条件；如果修改 PCF/bias/覆盖范围，另开一组并记录。丢样后等到有效样本满 300，不用 300 次 UI 刷新冒充 300 样本。

检查：16 列、300 数据行、CPU/GPU submission ID 一致且唯一递增、耗时有限非负；配置里的 mode、有效级数、实际层数、格式、距离、bias 与截图对应。首末样本 ID 不要求连续，允许真实丢样并记录 dropped 数。

重点比较 Shadow/Scene、CPU Update/Record 与实际 draws/triangles，不只看 FPS。默认 Shadow Playground 14 个有效对象、无额外剔除时，Single 预期约 14 个 shadow mesh draw，CSM 约 56；最终以实际统计解释，天空盒/UI 不算 mesh draw。

当前两模式都分配两槽×四层阴影。若实际 D32/2048²，名义 payload 都是 `2×4×2048×2048×4 = 128 MiB`，不是 Single 32 MiB 对 CSM 128 MiB；D16 则都为 64 MiB。分别保存 payload 与 Refresh VMA statistics 的 allocation/block 数据，二者都不叫实际驻留显存。

**完成点：** 所有 CSV 均有对应 sidecar、run-info、截图/路线说明；结果支持你对成本与画质的解释，不填预期加速比例。

## 第 14 步：保存关键帧并决定是否接 G-buffer

**目的：用 GPU 工具确认附件、层、绘制与采样确实符合代码契约，建立下一阶段可依赖的参照。**

在可用的 RenderDoc/平台工具中抓一帧，检查四个 shadow layer 的 clear/draw/矩阵与实际输入、Scene 的数组 shadow descriptor、HDR→Post。Single 仅 layer0 有物体绘制、其他层仍分配/清理，不能据预览空白层推断它们没有占内存。保存 capture 文件和简短输入/输出说明；工具不可用就注明待验收。

接入 G-buffer 的判定：

- [ ] Buffer 请求属性落实，实际属性可查，四处 CPU 写入缓存契约明确。
- [ ] CSM 静态/运动/边界/caster/Mask/双面专项无未解释退化。
- [ ] resize、最小化恢复、场景/模式切换及退出检查有记录；已发现的同步/生命周期错误已处理。
- [ ] Single/CSM 固定条件原始数据与配置可追溯，实际内存形状没有误报。
- [ ] 已保存关键帧或明确工具待验收边界，未将未跑平台写成通过。

满足本机主路径正确性与可复现条件后，再做 [Stage 2 B：MRT/单采样 G-buffer](stage-2-deferred-tiled-lighting.md#6-提交-bmrt单采样资源与-geometry)。现有验收尚有失败项时，先解决对应失败，再增加新 GPU 附件。

建议每完成一个独立代码小组便保存一次可运行提交；运行记录只勾实际执行项。**第一次手敲只做第 1–2 步：分清请求/实际属性并落实 requiredFlags。**
