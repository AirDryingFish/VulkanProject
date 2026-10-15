#version 450

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PostPushConstants
{
    vec4 params;
    ivec4 modes;
} post;

// Narkowicz ACES filmic fit, 2016.
// Linear RGB input and output.
// No additional input exposure scale.
vec3 acesFittedNarkowicz(vec3 x)
{
    vec3 numerator = x * (2.51 * x + 0.03);
    vec3 denominator = x * (2.43 * x + 0.59) + 0.14;

    return clamp(numerator / denominator, vec3(0.0), vec3(1.0));
}

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
        vec3 exposed = hdr * exp2(post.params.x);
        if (post.modes.x == 1)
        {
            displayLinear = acesFittedNarkowicz(exposed);
        }
        else
        {
            displayLinear = exposed / (exposed + vec3(1.0));
        }
    }
    // shader 输出的是 display-linear RGB。后续会要求swapchain使用sRGB附件，由附件写入执行sRGB编码
    // 所以这里不再写 pow(color, 1.0 / 2.2)
    outColor = vec4(displayLinear, 1.0);
}