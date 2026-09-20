#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 5) uniform sampler2D shadowDepthMap;

void main()
{
    float depth = textureLod(shadowDepthMap, fragUv, 0.0).r;
    outColor = vec4(vec3(depth), 1.0);
}