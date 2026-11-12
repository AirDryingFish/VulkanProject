#version 450

// 复用 UniformBufferObject 的前两个矩阵
// view 位于 offset 0，proj 位于 offset 64
layout(std140, set = 0, binding = 0) uniform PreviewFrame
{
    mat4 view;
    mat4 proj;
} frame;

layout(set = 1, binding = 0) uniform sampler2D gNormal;
layout(set = 1, binding = 1) uniform sampler2D gBaseColor;
layout(set = 1, binding = 2) uniform sampler2D gMaterial;
layout(set = 1, binding = 3) uniform sampler2D gEmissive;
layout(set = 1, binding = 4) uniform sampler2D gDepth;

layout(push_constant) uniform GBufferPreviewPushConstants
{
    vec4 params;
    ivec4 modes;
} preview;

layout(location = 0) out vec4 outColor;

vec3 toneMapEmissive(vec3 value)
{
    // 压缩颜色
    vec3 x = max(value, vec3(0.0)) * exp2(preview.params.z);

    // ACES 风格拟合曲线
    if (preview.modes.y == 1)
    {
        return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3(0.0), vec3(1.0));
    }
    // Reinhard 曲线
    return x / (x + vec3(1.0));
}

void main()
{
    // Present 和 G-buffer 尺寸相同。使用同一个整数像素位置读取，不进行插值也不额外翻转 Y
    // 这是 framebuffer 的值，对应实际“像素”值
    ivec2 pixel = ivec2(gl_FragCoord.xy);

    vec4 normalData = texelFetch(gNormal, pixel, 0);
    int mode = preview.modes.x;

    if (normalData.a < 0.5)
    {
        // LinearDepth 为全白背景，其他 case 为全黑
        vec3 background = mode == 7 ? vec3(1.0) : vec3(0.0);
        outColor = vec4(background, 1.0);
        return;
    }

    vec3 result = vec3(0.0);

    // worldNormal
    if (mode == 1)
    {
        // 法线从 [-1, 1] 映射到 [0, 1]
        result = clamp(normalData.xyz * 0.5 + 0.5, vec3(0.0), vec3(1.0));
    }

    else if (mode == 2)
    {
        result = texelFetch(gBaseColor, pixel, 0).rgb;
    }
    else if (mode >= 3 && mode <= 5)
    {
        // material.r = metallic
        // material.g = roughness
        // material.b = AO
        vec3 material = texelFetch(gMaterial, pixel, 0).rgb;
        result = vec3(material[mode - 3]);
    }

    else if (mode == 6)
    {
        vec3 emissive = texelFetch(gEmissive, pixel, 0).rgb;
        result = toneMapEmissive(emissive);
    }

    else if (mode == 7)
    {
        float depth = texelFetch(gDepth, pixel, 0).r;

        float viewDepth = frame.proj[3][2] / (depth + frame.proj[2][2]);
        float gray = clamp((viewDepth - preview.params.x) / max(preview.params.y - preview.params.x, 1e-4), 0.0, 1.0);

        result = vec3(gray);
    }

    outColor = vec4(result, 1.0);

}
