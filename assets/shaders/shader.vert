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
layout(location = 4) in vec4 inTangent;
layout(location = 5) in vec2 inTexCoord1;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec3 fragNormal;
layout(location = 4) out vec4 fragTangent;
layout(location = 5) out vec2 fragTexCoord1;

layout(std140, set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 ambientLight;
    ivec4 lightCounts;
    vec4 renderParams;

    PointLight pointLights[MAX_POINT_LIGHTS];

} ubo;

layout(push_constant) uniform ModelPushConstants
{
    mat4 model;
    vec4 baseColorFactor;
    vec4 materialFactors;
    vec4 emissiveFactor;
    uvec4 textureInfo;
} pushConstants;

void main() {
    vec4 worldPosition = pushConstants.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPosition;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragTexCoord1 = inTexCoord1;
    fragWorldPos = worldPosition.xyz;
    mat3 modelLinear = mat3(pushConstants.model);
    vec3 worldNormal = normalize(transpose(inverse(mat3(pushConstants.model))) * inNormal);
    fragNormal = worldNormal;
    fragTangent = vec4(0.0);

    // 有 tangent from asset
    if (pushConstants.textureInfo.y != 0u)
    {
        vec3 worldTangent = modelLinear * inTangent.xyz;
        // 重新让 T 与 N 垂直
        worldTangent -= worldNormal * dot(worldNormal, worldTangent);
        float tangentLength = length(worldTangent);
        if (tangentLength > 0.0001)
        {
            worldTangent /= tangentLength;
            // modelSign 判断 model 变换有没有将坐标系“镜像反转”
            float modelSign = determinant(modelLinear) < 0.0 ? -1.0 : 1.0;
            fragTangent = vec4(worldTangent, inTangent.w * modelSign);
        }

    }

}
