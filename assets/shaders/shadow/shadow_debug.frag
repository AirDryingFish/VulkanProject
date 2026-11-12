#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 5) uniform sampler2DArray shadowDepthMap;

layout(push_constant) uniform PostPushConstants
{
    vec4 params;
    ivec4 modes;
} post;


void main()
{
    // post.modes.w 选择数组层，最后的 0.0 选择 mip 0。
    float depth = textureLod(shadowDepthMap, vec3(fragUv, float(post.modes.w)), 0.0).r;
    outColor = vec4(vec3(depth), 1.0);
}