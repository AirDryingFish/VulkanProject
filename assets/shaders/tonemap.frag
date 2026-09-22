#version 450

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PostPushConstants
{
    vec4 params;
    ivec4 modes;
} post;

void main()
{
    vec3 hdr = max(textureLod(hdrScene, fragUv, 0.0).rgb, vec3(0.0));

    vec3 displayLinear;
    if (post.modes.y != 0)
    {
        displayLinear = hdr;
    }
    else
    {
        displayLinear = hdr / (hdr + vec3(1.0));
    }
    // shader 输出的是 display-linear RGB。后续会要求swapchain使用sRGB附件，由附件写入执行sRGB编码
    // 所以这里不再写 pow(color, 1.0 / 2.2)
    outColor = vec4(displayLinear, 1.0);
}