#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"
#include "material.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
    // 主画面执行 alpha 裁剪，把 alpha 值小于 cutoff 的丢弃。大于的当作不透明片元绘制
    vec4 baseColor = sampleBaseColor();
    bool alphaRejected = materialAlphaRejected(baseColor);

    if (frame.shadowFlags.w == 8)
    {
        if (alphaRejected)
        {
            discard;
        }

        int cascadeIndex = selectShadowCascade(fragWorldPos);

        if (cascadeIndex < 0)
        {
            outColor = vec4(1.0, 1.0, 1.0, 1.0);
            return;
        }

        const vec3 colors[4] = vec3[4](
        vec3(1.0, 0.15, 0.15), // 红色
        vec3(0.15, 1.0, 0.15), // 绿色
        vec3(0.15, 0.3, 1.0),  // 蓝色
        vec3(1.0, 0.85, 0.15)  // 黄色
        );

        outColor = vec4(colors[cascadeIndex], 1.0);
        return;
    }

    // -- 用颜色显示光空间坐标 (debug use) --
    if (frame.shadowFlags.y != 0)
    {
        if (alphaRejected)
        {
            discard;
        }

        int cascadeIndex = selectShadowCascade(fragWorldPos);
        if (cascadeIndex < 0)
        {
            outColor = vec4(1.0, 1.0, 1.0, 1.0);
            return;
        }

        vec4 lightClip = frame.cascadeLightViewProjection[cascadeIndex] * vec4(fragWorldPos, 1.0);
        if (lightClip.w <= 0.0) // 点在相机后方
        {
            outColor = vec4(1.0, 0.0, 1.0, 1.0); // 把这个像素画成红色
            return;
        }

        vec3 lightNdc = lightClip.xyz / lightClip.w; // vulkan ndc 坐标系范围 x[-1, 1] y[-1, 1] z[0, 1]
        vec2 lightUv = lightNdc.xy * 0.5 + 0.5; // uv: [0, 1]

        bool inside =
            all(greaterThanEqual(lightUv, vec2(0.0))) &&
            all(lessThanEqual(lightUv, vec2(1.0))) &&
            lightNdc.z >= 0.0 &&
            lightNdc.z <= 1.0;
        if (!inside)
        {
            outColor = vec4(1.0, 0.0, 1.0, 1.0);
            return;
        }

        outColor = vec4(lightUv, lightNdc.z, 1.0);
        return;
    }

    MaterialSample material = evalueteMaterial(baseColor);
    float metallic = material.metallic;
    float roughness = material.roughness;
    float ao = material.ao;
    vec3 albedo = material.baseColor.rgb;
    vec3 normal = material.normal;

    // Lit = 0,
    // Albedo = 1,
    // WorldNormal = 2,
    // Metallic = 3,
    // Roughness = 4,
    // LinearDepth = 5,
    // HdrLuminance = 6,
    // ShadowProjection = 7
    int debugMode = frame.shadowFlags.w;
    if (debugMode >= 1 && debugMode <= 5)
    {
        if (alphaRejected)
        {
            discard;
        }

        vec3 debugColor;
        if (debugMode == 1)
        {
            debugColor = albedo;
        }
        else if (debugMode == 2)
        {
            debugColor = normal * 0.5 + 0.5;
        }
        else if (debugMode == 3)
        {
            debugColor = vec3(metallic);
        }
        else if (debugMode == 4)
        {
            debugColor = vec3(roughness);
        }
        else
        {
            // 这里相机的坐标系中，相机正方向上深度为负数，取负号得到正深度值
            float viewDepth = -(frame.view * vec4(fragWorldPos, 1.0)).z;
            float nearDepth = frame.renderParams.y;
            float farDepth = frame.renderParams.z;

            float depth01 = clamp((viewDepth - nearDepth) / max(farDepth - nearDepth, 1e-4), 0.0, 1.0);

            debugColor = vec3(depth01);
        }
        outColor = vec4(debugColor, 1.0);
        return;
    }

    vec3 color = evaluateSurfaceLighting(
        fragWorldPos,
        normal,
        albedo,
        metallic,
        roughness,
        ao,
        material.emissive);

    if (alphaRejected)
    {
        discard;
    }

    outColor = vec4(color, 1.0);
}
