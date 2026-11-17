#include "shared/material.glsl"

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outBaseColor;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;

#ifdef TEMPORAL_GEOMETRY
layout(location = 6) in vec4 temporalCurrentClip;
layout(location = 7) in vec4 temporalPreviousClip;
layout(location = 8) in float temporalPreviousDepth;
// flat 表示这个变量在光栅化时不插值。一个三角形覆盖所有 frag，都会收到同一顶点提供的值
layout(location = 9) flat in uvec4 temporalSurfaceKey;
layout(location = 10) flat in uint temporalPreviousValid;

layout(location = 4) out vec4 outMotion;
layout(location = 5) out uvec4 outSurfaceKey;

void writeTemporalOutputs()
{
    outMotion = vec4(0.0);
    outSurfaceKey = temporalSurfaceKey;

    // 新对象、不可投影的点和超出半精度范围的深度不能使用历史。
    if (temporalPreviousValid == 0u ||
        temporalCurrentClip.w <= 1e-5 || temporalPreviousClip.w <= 1e-5 ||
        isnan(temporalPreviousDepth) || isinf(temporalPreviousDepth) ||
        temporalPreviousDepth <= 0.0 || temporalPreviousDepth > 65504.0 ||
        any(isnan(temporalCurrentClip)) || any(isinf(temporalCurrentClip)) ||
        any(isnan(temporalPreviousClip)) || any(isinf(temporalPreviousClip)))
    {
        return;
    }

    vec2 currentUv = temporalCurrentClip.xy / temporalCurrentClip.w * 0.5 + 0.5;
    vec3 previousNdc = temporalPreviousClip.xyz / temporalPreviousClip.w;
    vec2 previousUv = previousNdc.xy * 0.5 + 0.5;
    vec2 motionUv = previousUv - currentUv;

    if (any(lessThan(currentUv, vec2(0.0))) || any(greaterThan(currentUv, vec2(1.0))) ||
        any(lessThan(previousUv, vec2(0.0))) || any(greaterThan(previousUv, vec2(1.0))) ||
        previousNdc.z < 0.0 || previousNdc.z > 1.0 ||
        any(isnan(motionUv)) || any(isinf(motionUv)))
    {
        return;
    }

    outMotion = vec4(motionUv, temporalPreviousDepth, 1.0);
}
#endif

void main()
{
    MaterialSample material = evalueteMaterial(sampleBaseColor());

    // 材质采样和导数计算完成后再裁剪
    if (material.alphaRejected)
    {
        discard;
    }

    // outNormal.a = 1 表示有效表面，附件清零后背景的 alpha 为 0，后续 lighting 可以据此区分背景
    outNormal = vec4(material.normal, 1.0);
    outBaseColor = vec4(material.baseColor.rgb, 1.0);
    outMaterial = vec4(material.metallic, material.roughness, material.ao, 1.0);
    outEmissive = vec4(material.emissive, 1.0);

    #ifdef TEMPORAL_GEOMETRY
        writeTemporalOutputs();
    #endif
}