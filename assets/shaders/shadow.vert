#version 450

const int MAX_POINT_LIGHTS = 16;

struct PointLight
{
    vec4 position;
    vec4 color;
    vec4 params;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 5) in vec2 inTexCoord1;

layout(location = 0) out float fragAlpha;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec2 fragTexCoord1;

layout(std140, set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 ambientLight;
    ivec4 lightCounts;
    vec4 renderParams;

    PointLight pointLights[MAX_POINT_LIGHTS];

    vec4 directionalDirectionEnabled;
    vec4 directionalColorIntensity;

    mat4 lightViewProjection;
    vec4 shadowParams;
    ivec4 shadowFlags;

} ubo;

layout(push_constant) uniform DrawPushConstants
{
    mat4 model;
    vec4 baseColorFactor;
    vec4 materialFactors;
    vec4 emissiveFactor;
    uvec4 textureInfo;
} draw;

// 记录光源看到的最近表面（渲染深度图）
void main(){
    // inPosition: 模型自己的局部坐标
    // model * inPosition: 模型的世界坐标
    // lightViewProjection * model * inPosition: 光源裁剪空间坐标
    gl_Position = ubo.lightViewProjection * draw.model * vec4(inPosition, 1.0);

    fragAlpha = inColor.a;
    fragTexCoord = inTexCoord;
    fragTexCoord1 = inTexCoord1;
}