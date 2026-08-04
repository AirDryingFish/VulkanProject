# IBL Implementation

本文说明当前工程中 image-based lighting 的实现方式。HDR skybox 会预计算为一张低频 irradiance cubemap 和一张带 mip 链的 prefiltered environment cubemap，分别为漫反射和镜面反射提供环境光照。

## 资源流

当前流程是：

```text
textures/pbr/newport_loft.hdr
    -> skybox HDR cubemap
        |-> irradiance cubemap（漫反射 IBL）-----------|
        |-> prefiltered environment cubemap（镜面 IBL）--|-> PBR fragment shader
```

`newport_loft.hdr` 是等距柱状投影 HDR 图。工程启动时先在 `Skybox.cpp` 中用 `stbi_loadf` 读取 HDR 像素，并在 CPU 端转换成 `VK_FORMAT_R16G16B16A16_SFLOAT` 的 skybox cubemap。这个 cubemap 既用于显示天空盒，也作为 irradiance 和 prefiltered environment 两项 IBL 预计算的输入环境图。

## 为什么需要 irradiance cubemap

漫反射 IBL 不能只沿法线方向采样一次环境图：

```glsl
sampleEnvironment(N)
```

这样会把 HDR 的高频细节直接投到漫反射上，metallic 或视角变化时容易出现突变感。正确的 diffuse IBL 需要计算以表面法线 `N` 为中心的半球积分：

```text
irradiance(N) = integral over hemisphere(environment(wi) * cos(theta))
```

这个积分每个 fragment 实时计算太贵，所以当前实现把它预计算到一张低分辨率 cubemap 中。渲染模型时只需要：

```glsl
vec3 irradiance = texture(irradianceMap, N).rgb;
```

## 启动时只计算一次

Diffuse IBL 的预计算入口在：

```cpp
TriangleApplication::InitVulkan()
```

初始化顺序中，skybox 创建完成后调用：

```cpp
createSkyboxImage();
createSkyboxSampler();
createIrradianceResources();
createPrefilterResources();
```

`createIrradianceResources()` 和 `createPrefilterResources()` 都位于 `src/IBL.cpp`。两项预计算只在 Vulkan 初始化阶段执行一次，不在每帧 command buffer 中执行。

## Irradiance 资源

`src/IBL.cpp` 中创建了一张 32x32 的 cubemap：

```cpp
constexpr uint32_t irradianceDimension = 32;
constexpr VkFormat irradianceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
```

image usage 是：

```cpp
VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
```

原因是它先作为离屏渲染的 color attachment，被写入卷积结果；之后作为 shader sampled image，被 PBR shader 采样。

这张 image 有两类 view：

```text
VK_IMAGE_VIEW_TYPE_CUBE
```

用于运行时在 PBR shader 中采样。

```text
6 个 VK_IMAGE_VIEW_TYPE_2D
```

每个 2D view 对应 cubemap 的一个 face，用来创建 6 个 framebuffer。离屏渲染时每次写入一个 face。

## 离屏卷积渲染

`renderIrradianceCubemap()` 使用 `immediateSubmit()` 提交一次性 command buffer。它会对 6 个 cubemap face 各渲染一次 cube：

```cpp
for (uint32_t face = 0; face < irradianceFramebuffers.size(); face++)
{
    vkCmdBeginRenderPass(... irradianceFramebuffers[face] ...);
    vkCmdBindPipeline(... irradiancePipeline ...);
    vkCmdBindDescriptorSets(... irradianceDescriptorSet ...);
    vkCmdPushConstants(... captureProjection * captureViews[face] ...);
    vkCmdDraw(commandBuffer, 36, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}
```

每个 face 使用不同的 view matrix，看向：

```text
+X, -X, +Y, -Y, +Z, -Z
```

渲染完成后，irradiance image 从：

```cpp
VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
```

转换到：

```cpp
VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
```

这样后续每帧就可以安全采样。

## 卷积 Shader

卷积 shader 是：

```text
shaders/irradiance.vert
shaders/irradiance.frag
```

`irradiance.vert` 不使用 vertex buffer，而是通过 `gl_VertexIndex` 生成 cube 的 36 个顶点。每次渲染一个 face 时，通过 push constant 传入该 face 的 `viewProjection`。

`irradiance.frag` 接收 cube 的 local position，并把它当成当前 cubemap texel 对应的法线方向：

```glsl
vec3 normal = normalize(localPos);
```

然后围绕该 normal 构造半球切线空间：

```glsl
vec3 up = abs(normal.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
vec3 right = normalize(cross(up, normal));
up = normalize(cross(normal, right));
```

再用 `phi` 和 `theta` 遍历半球方向：

```glsl
for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
{
    for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
    {
        vec3 tangentSample = vec3(
            sin(theta) * cos(phi),
            sin(theta) * sin(phi),
            cos(theta));

        vec3 sampleVec = tangentSample.x * right
                       + tangentSample.y * up
                       + tangentSample.z * normal;

        irradiance += texture(environmentMap, sampleVec).rgb
                    * cos(theta)
                    * sin(theta);
        sampleCount += 1.0;
    }
}

irradiance = PI * irradiance / sampleCount;
```

这里：

```text
cos(theta)
```

表示 Lambert 漫反射中斜入射光贡献更弱。

```text
sin(theta)
```

用于补偿球坐标采样时不同纬度对应面积不同。

`sampleDelta` 当前是：

```glsl
float sampleDelta = 0.05;
```

值越小，预计算越慢但结果更平滑；值越大，启动更快但误差更明显。

## Prefiltered environment 资源

镜面 IBL 需要让材质 roughness 影响环境反射的模糊程度。当前实现创建了一张 128x128、包含 5 个 mip level 的 cubemap：

```cpp
constexpr uint32_t prefilterDimension = 128;
constexpr VkFormat prefilterFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
static constexpr uint32_t prefilterMipLevels = 5;
```

每个 mip level 对应一个 roughness 值：

```cpp
roughness = float(mip) / float(prefilterMipLevels - 1);
```

`renderPrefilterCubemap()` 会遍历 5 个 mip level 和 6 个 cubemap face，共执行 30 次离屏渲染。`prefilter.frag` 使用 Hammersley 序列和 GGX importance sampling，每个 texel 采样 1024 次原始环境图，并根据采样立体角选择输入环境图的 mip level。生成完成后，整张 cubemap 转换为 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`，供 PBR shader 采样。

## Descriptor 绑定

模型材质 descriptor 现在绑定 8 个 image sampler：

```text
binding 1: albedoMap
binding 2: normalMap
binding 3: metallicMap
binding 4: roughnessMap
binding 5: aoMap
binding 6: environmentMap
binding 7: irradianceMap
binding 8: prefilterMap
```

`binding 6` 是原始 HDR environment cubemap。irradiance 和 prefilter 两条预计算管线会通过各自 descriptor 的 `binding 0` 读取同一张 cubemap。

`binding 7` 是启动时生成的 irradiance cubemap，用于 diffuse IBL。

`binding 8` 是启动时生成的 prefiltered environment cubemap，用于 roughness-dependent specular IBL。

## PBR Shader 中的使用

运行时 PBR shader 位于 `shaders/shader.frag`。diffuse IBL 现在使用：

```glsl
vec3 irradiance = texture(irradianceMap, environmentDirection(normal)).rgb;
vec3 diffuse = irradiance * albedo;
```

然后结合 Fresnel 得到 diffuse/specular 能量分配：

```glsl
vec3 F = fresnelSchlickRoughness(max(dot(normal, viewDir), 0.0), F0, roughness);
vec3 kS = F;
vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
```

镜面 IBL 根据 roughness 选择 prefiltered cubemap 的 mip level：

```glsl
vec3 reflectionDir = reflect(-viewDir, normal);
vec3 prefilteredColor = textureLod(
    prefilterMap,
    environmentDirection(reflectionDir),
    roughness * MAX_REFLECTION_LOD).rgb;
vec3 specular = prefilteredColor * F;
```

最终环境光贡献是：

```glsl
vec3 ibl = (kD * diffuse + specular) * ao * iblIntensity;
```

其中 `iblIntensity` 通过 `materialParams.w` 从 CPU 传入，可在 ImGui 的 `IBL Intensity` 调整。

## 当前限制

当前实现已经完成 diffuse IBL 的 irradiance cubemap，以及 roughness-dependent specular IBL 使用的 prefiltered environment cubemap。

镜面项目前直接使用 `prefilteredColor * F`，还没有乘上 split-sum 近似中的 BRDF 积分结果。后续需要生成一张以 `NdotV` 和 roughness 为坐标的 2D BRDF LUT，并在运行时组合为类似下面的形式：

```glsl
vec2 brdf = texture(brdfLut, vec2(NdotV, roughness)).rg;
vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);
```

完整 PBR IBL 的当前进度是：

```text
1. irradiance cubemap       已完成，用于 diffuse IBL
2. prefiltered cubemap      已完成，用于 roughness-dependent specular IBL
3. BRDF LUT                 待实现，用于 split-sum specular BRDF
```
