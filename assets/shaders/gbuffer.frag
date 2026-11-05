#version 450
#extension GL_GOOGLE_include_directive : require

#include "material.glsl"

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outBaseColor;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;

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
}