#version 450

const int MAX_POINT_LIGHTS = 16;

struct PointLight
{
    vec4 position;
    vec4 color;
    vec4 params;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec3 fragNormal;

layout(binding = 0) uniform UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 ambientLight;

    ivec4 lightCounts;

    vec4 materialAlbedo;
    vec4 materialParams;

    PointLight pointLights[MAX_POINT_LIGHTS];

} ubo;

layout(push_constant) uniform ModelPushConstants
{
    mat4 model;
} pushConstants;

void main() {
    vec4 worldPosition = pushConstants.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPosition;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragWorldPos = worldPosition.xyz;
    fragNormal = normalize(transpose(inverse(mat3(pushConstants.model))) * inNormal);
}
