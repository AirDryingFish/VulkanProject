#version 450

layout(binding = 1) uniform samplerCube skyboxSampler;

layout(location = 0) in vec3 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 hdrColor = texture(skyboxSampler, fragTexCoord).rgb;
    outColor = vec4(hdrColor, 1.0);
}
