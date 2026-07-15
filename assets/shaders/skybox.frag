#version 450

layout(binding = 1) uniform samplerCube skyboxSampler;

layout(location = 0) in vec3 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 hdrColor = texture(skyboxSampler, fragTexCoord).rgb;
    vec3 color = hdrColor / (hdrColor + vec3(1.0));
    outColor = vec4(color, 1.0);
}
