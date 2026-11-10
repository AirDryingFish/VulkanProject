#ifndef PROJECT_LIGHTING_GLSL
#define PROJECT_LIGHTING_GLSL

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;

struct PointLight
{
    vec4 position;
    vec4 color;
    vec4 params;
};

layout(std140, set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 ambientLight;
    ivec4 lightCounts;
    vec4 renderParams;

    vec4 directionalDirectionEnabled;
    vec4 directionalColorIntensity;

    mat4 lightViewProjection;
    vec4 shadowParams;
    // x: shadows enabled 是否启用阴影
    // y: shadow projection view 是否显示阴影投影调试视图
    // z: PCF enabled 是否启用 PCF 阴影过滤
    // w: DebugView 当前 DebugView 模式编号
    ivec4 shadowFlags;

    mat4 cascadeLightViewProjection[4];
    vec4 cascadeSplits;
    vec4 cascadeTexelWorldSize;
    vec4 cascadeReceiverBias;
    uvec4 cascadeInfo;

    vec4 cascadeBlendStarts;

} frame;


layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;
layout(set = 0, binding = 4) uniform sampler2DArrayShadow shadowMap;
// binding 5: shadow preview
layout(std430, set = 0, binding = 6) readonly buffer PointLightBuffer
{
    PointLight pointLights[];
} lightBuffer;

// specular 的 反射率 F 项
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1 - F0) * pow(clamp(1 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 environmentDirection(vec3 worldDirection)
{
    return vec3(worldDirection.x, worldDirection.z, worldDirection.y);
}

// specular 的 微表面分布 D 项
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

// specular 的 微表面遮挡 G 项
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// 计算一个直接光源对当前像素产生的 PBR 光照贡献
// 计算直接光照的入口函数： Diffuse + specular
vec3 evaluateDirectLight(
    vec3 normal,
    vec3 viewDir,
    vec3 lightDir,
    vec3 albedo,
    float metallic,
    float roughness,
    vec3 F0,        // 垂直入射时的基础反射率
    vec3 radiance   // 光源颜色 * 强度
)
{
    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.0);
    // NdotL = 0 => 擦着表面
    // NdotL < 0 => 光在表面背后
    if (NdotL <= 0.0 || NdotV <= 0.0)
    {
        return vec3(0.0);
    }

    vec3 halfVector = lightDir + viewDir;
    float halfLengthSquared = dot(halfVector, halfVector);
    if (halfLengthSquared < 1e-8)
    {
        return vec3(0.0);
    }
    vec3 halfDir = halfVector * inversesqrt(halfLengthSquared);

    // Fresnel项：在当前观察角度下，有多少光被表面镜面反射
    vec3 F = fresnelSchlick(max(dot(halfDir, viewDir), 0.0), F0);
    // Normal Distribution Function: 有多少微表面的法向朝向 H
    float NDF = DistributionGGX(normal, halfDir, roughness);
    // Geometry / Shadowing-Masking: 模拟微表面之前互相遮挡的问题
    float G = GeometrySmith(normal, viewDir, lightDir, roughness);
    // 完整的 Cook-Torrance specular BRDF:
    vec3 specular = NDF * G * F / (4.0 * NdotV * NdotL + 0.0001);
    // diffuse部分:
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// 根据 fragment 的 view space 距离选择正确的 cascade
int selectShadowCascade(vec3 worldPos)
{
    float viewDepth = -(frame.view * vec4(worldPos, 1.0)).z;

    if (viewDepth <= 0.0 || viewDepth > frame.cascadeSplits.w)
    {
        return -1;
    }

    // Single mode
    if (frame.cascadeInfo.y == 0u)
    {
        return 0;
    }

    int cascadeCount = int(min(frame.cascadeInfo.x, 4u));

    for (int i = 0; i < cascadeCount; ++i)
    {
        if (viewDepth <= frame.cascadeSplits[i])
        {
            return i;
        }
    }
    return -1;
}

float shadowCascadeVisibility(vec3 worldPosition, int cascadeIndex)
{
    vec4 lightClip = frame.cascadeLightViewProjection[cascadeIndex] * vec4(worldPosition, 1.0);

    if (lightClip.w <= 0.0)
    {
        return 1.0;
    }
    vec3 lightNdc = lightClip.xyz / lightClip.w;
    vec2 lightUv = lightNdc.xy * 0.5 + 0.5;
    bool outside = any(lessThan(lightUv, vec2(0.0))) || // lessThan 返回一个bvec2，any 只要有一个是 true 就返回 true
                   any(greaterThan(lightUv, vec2(1.0))) ||
                   lightNdc.z < 0.0 ||
                   lightNdc.z > 1.0;
    if (outside)
    {
        return 1.0;
    }
    // frame.shadowParams.z: 阴影比较使用的 bias
    float referenceDepth = lightNdc.z - frame.cascadeReceiverBias[cascadeIndex];

    // 未开启 pcf
    if (frame.shadowFlags.z == 0)
    {
        // return textureLod(shadowMap, vec3(lightUv, referenceDepth), 0.0);
        return textureGrad(shadowMap, vec4(lightUv, float(cascadeIndex), referenceDepth), vec2(0.0), vec2(0.0));
    }

    vec2 texelSize = frame.shadowParams.xy;
    float visibilitySum = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 sampleUv = lightUv + vec2(float(x), float(y)) * texelSize;

            // 要指定采样哪一级阴影，以及换用 Array 阴影采样其支持的函数。
            // vec4(U, V, 选择哪一层阴影贴图, 比较深度)
            // 第三第四个为参数为 UV 沿着 X/Y 方向的变化率
            visibilitySum += textureGrad(shadowMap, vec4(sampleUv, float(cascadeIndex), referenceDepth), vec2(0.0), vec2(0.0));
        }
    }
    return visibilitySum /= 9.0;
}

float directionalShadowVisibility(vec3 worldPosition)
{
    if (frame.shadowFlags.x == 0)
    {
        return 1.0;
    }

    int cascadeIndex = selectShadowCascade(worldPosition);
    if (cascadeIndex < 0)
    {
        return 1.0;
    }

    float currectVisibility = shadowCascadeVisibility(worldPosition, cascadeIndex);

    int cascadeCount = int(min(frame.cascadeInfo.x, 4u));

    if (frame.cascadeInfo.y == 0u || cascadeIndex + 1 >= cascadeCount)
    {
        return currectVisibility;
    }

    float blendStart = frame.cascadeBlendStarts[cascadeIndex];
    float blendEnd = frame.cascadeSplits[cascadeIndex];
    if (blendStart >= blendEnd)
    {
        return currectVisibility;
    }

    float viewDepth = -(frame.view * vec4(worldPosition, 1.0)).z;
    if (viewDepth <= blendStart)
    {
        return currectVisibility;
    }

    float nextVisibility = shadowCascadeVisibility(worldPosition, cascadeIndex + 1);

    float weight = clamp((viewDepth - blendStart) / (blendEnd - blendStart), 0.0, 1.0);

    return mix(currectVisibility, nextVisibility, weight);
}


vec3 evaluateSurfaceLighting(
    vec3 worldPosition,
    vec3 normal,
    vec3 albedo,
    float metallic,
    float roughness,
    float ao,
    vec3 emissive)
{
    vec3 viewDir = normalize(frame.cameraPosition.xyz - worldPosition);
    float iblIntensity = max(frame.renderParams.x, 0.0);
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);

    int lightCount = clamp(frame.lightCounts.x, 0, lightBuffer.pointLights.length());
    for (int i = 0; i < lightCount; i++)
    {
        PointLight light = lightBuffer.pointLights[i];
        if (light.params.y < 0.5)
        {
            continue;
        }

        vec3 lightVector = light.position.xyz - worldPosition;
        float distanceToLight = length(lightVector);
        vec3 lightDir = lightVector / max(distanceToLight, 0.0001);
        float range = max(light.params.x, 0.0001);
        float attenuation = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
        attenuation *= attenuation;

        vec3 radiance = light.color.rgb * light.color.a * attenuation;
        Lo += evaluateDirectLight(
            normal,
            viewDir,
            lightDir,
            albedo,
            metallic,
            roughness,
            F0,
            radiance
        );
    }

    if (frame.directionalDirectionEnabled.w > 0.5)
    {
        vec3 lightDir = -frame.directionalDirectionEnabled.xyz;
        vec3 radiance = frame.directionalColorIntensity.rgb * frame.directionalColorIntensity.w;
        float visibility = directionalShadowVisibility(worldPosition);
        Lo += visibility * evaluateDirectLight(
            normal,
            viewDir,
            lightDir,
            albedo,
            metallic,
            roughness,
            F0,
            radiance
        );
    }

    vec3 ambient = frame.ambientLight.rgb * frame.ambientLight.a * albedo * ao;
    vec3 F = fresnelSchlickRoughness(max(dot(normal, viewDir), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 irradiance = textureLod(irradianceMap, environmentDirection(normal), 0.0).rgb;
    vec3 diffuse = irradiance * albedo;

    vec3 reflectionDir = reflect(-viewDir, normal);

    vec3 prefilteredColor = textureLod(prefilterMap, environmentDirection(reflectionDir), roughness * MAX_REFLECTION_LOD).rgb;
    float NdotV = max(dot(normal, viewDir), 0.0);
    vec2 brdf = textureLod(brdfLUT, vec2(NdotV, roughness), 0.0).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ibl = (kD * diffuse + specular) * ao * iblIntensity;

    return ambient + ibl + Lo + emissive;

}

#endif