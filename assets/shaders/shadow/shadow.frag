#version 450

layout(location = 0) in float fragAlpha;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec2 fragTexCoord1;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;

layout(push_constant) uniform DrawPushConstants
{
    mat4 model;
    vec4 baseColorFactor;
    vec4 materialFactors;
    vec4 emissiveFactor;
    uvec4 textureInfo;
} draw;

// 这个 shader 不输出颜色，只做：丢弃的片元不写阴影深度
void main()
{
    // mask 材质
    if (draw.textureInfo.z == 1u)
    {
        vec2 uv = (draw.textureInfo.x & 1u) != 0u ? fragTexCoord1 :fragTexCoord;

        float alpha = texture(albedoMap, uv).a * draw.baseColorFactor.a * fragAlpha;
        if (alpha < draw.emissiveFactor.w)
        {
            discard;
        }
    }
}