#version 450
#extension GL_GOOGLE_include_directive : require

#define DEFERRED_TILED_LIGHTING
#include "tile_layout.glsl"
#include "lighting.glsl"

layout(set = 1, binding = 0) uniform sampler2D gNormal;
layout(set = 1, binding = 1) uniform sampler2D gBaseColor;
layout(set = 1, binding = 2) uniform sampler2D gMaterial;
layout(set = 1, binding = 3) uniform sampler2D gEmissive;
layout(set = 1, binding = 4) uniform sampler2D gDepth;

layout(set = 2, binding = 1) uniform samplerCube skyboxSampler;

layout(push_constant) uniform DeferredLightingPushConstants
{
    vec4 clearColor;
} deferredPush;

layout(location = 0) out vec4 outColor;

// 让 Deferred lighting 为当前像素选择灯列表
// out offset: 输出当前 tile 的索引区域起点
// inout lightCount: 输入全部灯数，成功时修改成 tile 内的灯数
bool selectTileLightList(out uint offset, inout int lightCount)
{
    offset = 0u;
    // 不开启 tile 模式
    if (deferredFrame.tileParams.z != 1u)
    {
        return false;
    }

    // 获取当前像素坐标对于的 tile
    uvec2 tile = uvec2(gl_FragCoord.xy / TILE_SIZE);
    uvec2 grid = deferredFrame.extentAndTiles.zw;
    if (any(greaterThanEqual(tile, grid)))
    {
        return false;
    }

    uint tileId = tile.y * grid.x + tile.x;
    if (tileId >= uint(tileHeaders.headers.length()))
    {
        return false;
    }

    uvec4 header = tileHeaders.headers[tileId];
    if (header.y != 0u || header.x > TILE_LIGHT_CAPACITY || header.x > uint(lightCount))
    {
        return false;
    }

    uint indexCount = uint(tileIndices.indices.length());
    if (tileId >= indexCount / TILE_LIGHT_CAPACITY)
    {
        return false;
    }

    offset = tileId * TILE_LIGHT_CAPACITY;
    lightCount = int(header.x);
    return true;
}

// iteration: tile 列表里的第几个位置
uint tileLightIndex(uint offset, uint iteration)
{
    return tileIndices.indices[offset + iteration];
}

// 生成表面调试颜色：需要调试显示时，把结果写入 result 并返回 true
bool surfaceDebugColor(
    vec3 worldPosition,
    vec3 normal,
    vec3 albedo,
    float metallic,
    float roughness,
    out vec3 result
)
{
    int debugMode = frame.shadowFlags.w;
    if (debugMode == 8)
    {
        int cascadeIndex = selectShadowCascade(worldPosition);
        const vec3 colors[4] = vec3[4](
            vec3(1.0, 0.15, 0.15),
            vec3(0.15, 1.0, 0.15),
            vec3(0.15, 0.3, 0.1),
            vec3(1.0, 0.85, 0.15)
        );
        result = cascadeIndex < 0 ? vec3(1.0) : colors[cascadeIndex];
        return true;
    }

    if (frame.shadowFlags.y != 0)
    {
        int cascadeIndex = selectShadowCascade(worldPosition);
        if (cascadeIndex < 0)
        {
            result = vec3(1.0);
            return true;
        }

        vec4 lightClip = frame.cascadeLightViewProjection[cascadeIndex] * vec4(worldPosition, 1.0);
        if (lightClip.w <= 0.0)
        {
            result = vec3(1.0, 0.0, 1.0);
            return true;
        }

        vec3 lightNdc = lightClip.xyz / lightClip.w;
        vec2 lightUv = lightNdc.xy * 0.5 + 0.5;
        bool inside = all(greaterThanEqual(lightUv, vec2(0.0))) && all(lessThan(lightUv, vec2(1.0))) && lightNdc.z >= 0.0 && lightNdc.z <= 1.0;
        result = inside ? vec3(lightUv, lightNdc.z) : vec3(1.0, 0.0, 1.0);

        return true;
    }

    if (debugMode < 1 || debugMode > 5)
    {
        result = vec3(0.0);
        return false;
    }

    if (debugMode == 1)
    {
        result = albedo;
    }
    else if (debugMode == 2)
    {
        result = normal * 0.5 + 0.5;
    }
    else if (debugMode == 3)
    {
        result = vec3(metallic);
    }
    else if (debugMode == 4)
    {
        result = vec3(roughness);
    }
    else if (debugMode == 5)
    {
        float viewDepth = -(frame.view * vec4(worldPosition, 1.0)).z;
        float gray = clamp((viewDepth - frame.renderParams.y) / max(frame.renderParams.z - frame.renderParams.y, 1e-4), 0.0, 1.0);
        result = vec3(gray);
    }
    return true;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    // 整数像素索引 -> 像素中心 -> UV -> NDCxy
    vec2 ndc = (vec2(pixel) + vec2(0.5)) / vec2(deferredFrame.extentAndTiles.xy) * 2.0 - 1.0;

    // 选取 ndc 远平面上的位置
    vec4 viewFar = deferredFrame.inverseProjection * vec4(ndc, 1.0, 1.0);
    // 计算 “从相机穿过这个像素” 的世界空间方向，用于天空盒
    // 相机旋转矩阵通常是正交矩阵，转置相当于取逆
    vec3 worldDirection = transpose(mat3(frame.view)) * viewFar.xyz;
    vec3 cubeDirection = environmentDirection(worldDirection);

    vec3 cubeDx = dFdx(cubeDirection);
    vec3 cubeDy = dFdy(cubeDirection);

    // 当前像素没有模型表面的情况，alpha 是有效标记不是透明度
    vec4 normalData = texelFetch(gNormal, pixel, 0);
    if (normalData.a < 0.5)
    {
        // 保留天空盒背景
        if (frame.shadowFlags.w == 0 || frame.shadowFlags.w == 6)
        {
            vec3 hdr = textureGrad(skyboxSampler, cubeDirection, cubeDx, cubeDy).rgb;
            outColor = vec4(hdr, 1.0);
        }
        else
        {
            outColor = deferredPush.clearColor;
        }
        return;
    }

    float depth = texelFetch(gDepth, pixel, 0).r;
    // 世界空间的齐次坐标
    vec4 worldH = deferredFrame.inverseViewProjection * vec4(ndc, depth, 1.0);
    // 实际的三维位置
    vec3 worldPosition = worldH.xyz / worldH.w;

    vec3 normal = normalize(normalData.xyz);
    vec3 albedo = texelFetch(gBaseColor, pixel, 0).rgb;
    vec3 material = texelFetch(gMaterial, pixel, 0).rgb;
    material = clamp(material, vec3(0.0, 0.04, 0.0), vec3(1.0));
    vec3 emissive = texelFetch(gEmissive, pixel, 0).rgb;

    vec3 debugColor;
    if (surfaceDebugColor(worldPosition, normal, albedo, material.r, material.g, debugColor))
    {
        outColor = vec4(debugColor, 1.0);
        return;
    }

    vec3 hdr = evaluateSurfaceLighting(worldPosition, normal, albedo, material.r, material.g, material.b, emissive);
    outColor = vec4(hdr, 1.0);
}
