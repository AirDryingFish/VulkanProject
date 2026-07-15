#version 450

const int MAX_POINT_LIGHTS = 16;

struct PointLight
{
    vec4 position;
    vec4 color;
    vec4 params;
};

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 ambientLight;
    ivec4 lightCounts;
    PointLight pointLights[MAX_POINT_LIGHTS];
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

void main()
{
    vec3 baseColor = fragColor * texture(texSampler, fragTexCoord).rgb;
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    vec3 lighting = baseColor * ubo.ambientLight.rgb * ubo.ambientLight.a;
    float shininess = 64.0;
    float specularStrength = 0.35;

    for (int i = 0; i < ubo.lightCounts.x; i++)
    {
        PointLight light = ubo.pointLights[i];
        if (light.params.y < 0.5)
        {
            continue;
        }

        vec3 lightVector = light.position.xyz - fragWorldPos;
        float distanceToLight = length(lightVector);
        vec3 lightDir = lightVector / max(distanceToLight, 0.0001);
        float range = max(light.params.x, 0.0001);
        float attenuation = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
        attenuation *= attenuation;

        vec3 radiance = light.color.rgb * light.color.a * attenuation;
        float diffuse = max(dot(normal, lightDir), 0.0);
        vec3 halfDir = normalize(lightDir + viewDir);
        float specular = pow(max(dot(normal, halfDir), 0.0), shininess);

        lighting += baseColor * diffuse * radiance;
        lighting += specularStrength * specular * radiance;
    }

    outColor = vec4(lighting, 1.0);
}
