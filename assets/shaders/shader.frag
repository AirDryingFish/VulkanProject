#version 450

const float PI = 3.14159265359;
const int MAX_POINT_LIGHTS = 16;
const float MAX_REFLECTION_LOD = 4.0;

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
layout(location = 4) in vec4 fragTangent;
layout(location = 5) in vec2 fragTexCoord1;

layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 ambientLight;
    ivec4 lightCounts;
    vec4 renderParams;

    PointLight pointLights[MAX_POINT_LIGHTS];

} frame;

layout(push_constant) uniform DrawPushConstants
{
    mat4 model;
    vec4 baseColorFactor;
    // x: metallic
    // y: roughness
    // z: AO
    // w: reserved
    vec4 materialFactors;
    vec4 emissiveFactor;
    uvec4 textureInfo;
} draw;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicMap;
layout(set = 1, binding = 3) uniform sampler2D roughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;
layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

vec3 getNormalFromNormalMap()
{
    // 当前所有材质仍然使用 UV0
    vec2 normalUv = fragTexCoord;
    vec3 tangentSpaceNormal = texture(normalMap, normalUv).xyz * 2.0 - 1.0;
    vec3 n = normalize(fragNormal);
    // 路径 1：glTF primitive 自带有效 tangent
    if (draw.textureInfo.y != 0u)
    {
        vec3 t = fragTangent.xyz;
        // 插值后再次正交化
        t -= n * dot(n, t);
        float tangentLength = length(t);
        if (tangentLength < 0.0001 || abs(fragTangent.w) < 0.5)
        {
            return n;
        }
        t /= tangentLength;
        float handedness = fragTangent.w < 0.0 ? -1.0 : 1.0;
        vec3 b = normalize(cross(n, t)) * handedness;
        mat3 tbn = mat3(t, b, n);
        return normalize(tbn * tangentSpaceNormal);
    }
    // 没有 tangent，通过位置与 UV 导数恢复 T/B
    else
    {
        vec3 q1 =
            dFdx(fragWorldPos);

        vec3 q2 =
            dFdy(fragWorldPos);

        vec2 st1 =
            dFdx(normalUv);

        vec2 st2 =
            dFdy(normalUv);

        float uvDeterminant =
            st1.x * st2.y -
            st1.y * st2.x;

        // 没有 UV 或 UV 退化时，禁用 normal map 影响。
        if (abs(uvDeterminant) < 0.00000001)
        {
            return n;
        }

        vec3 tangentRaw =
            (
                q1 * st2.y -
                q2 * st1.y
            ) /
            uvDeterminant;

        vec3 bitangentRaw =
            (
                -q1 * st2.x +
                q2 * st1.x
            ) /
            uvDeterminant;

        vec3 t =
            tangentRaw -
            n *
            dot(n, tangentRaw);

        float tangentLength =
            length(t);

        if (tangentLength < 0.0001)
        {
            return n;
        }

        t /= tangentLength;

        vec3 crossNT =
            cross(n, t);

        float crossLength =
            length(crossNT);

        if (crossLength < 0.0001)
        {
            return n;
        }

        float handedness =
            dot(crossNT, bitangentRaw) < 0.0
                ? -1.0
                : 1.0;

        vec3 b =
            crossNT /
            crossLength *
            handedness;

        mat3 tbn =
            mat3(t, b, n);

        return normalize(
            tbn *
            tangentSpaceNormal);
    }
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1 - F0) * pow(clamp(1 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 environmentDirection(vec3 worldDirection)
{
    return vec3(worldDirection.x, worldDirection.z, worldDirection.y);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

void main()
{
    float metallic = clamp(texture(metallicMap, fragTexCoord).r * draw.materialFactors.x, 0.0, 1.0);
    float roughness = clamp(texture(roughnessMap, fragTexCoord).r * draw.materialFactors.y, 0.04, 1.0);
    float ao = clamp(texture(aoMap, fragTexCoord).r * draw.materialFactors.z, 0.0, 1.0);

    vec3 albedo = fragColor * texture(albedoMap, fragTexCoord).rgb * draw.baseColorFactor.rgb;
    vec3 normal = getNormalFromNormalMap();
    vec3 viewDir = normalize(frame.cameraPosition.xyz - fragWorldPos);
    float iblIntensity = max(frame.renderParams.x, 0.0);
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < frame.lightCounts.x; i++)
    {
        PointLight light = frame.pointLights[i];
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
        vec3 halfDir = normalize(lightDir + viewDir);

        vec3 F = fresnelSchlick(max(dot(halfDir, viewDir), 0.0), F0);

        float NDF = DistributionGGX(normal, halfDir, roughness);
        float G = GeometrySmith(normal, viewDir, lightDir, roughness);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(normal, viewDir), 0.0) * max(dot(normal, lightDir), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        float NdotL = max(dot(normal, lightDir), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    vec3 ambient = frame.ambientLight.rgb * frame.ambientLight.a * albedo * ao;
    vec3 F = fresnelSchlickRoughness(max(dot(normal, viewDir), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 irradiance = texture(irradianceMap, environmentDirection(normal)).rgb;
    vec3 diffuse = irradiance * albedo;

    vec3 reflectionDir = reflect(-viewDir, normal);

    vec3 prefilteredColor = textureLod(prefilterMap, environmentDirection(reflectionDir), roughness * MAX_REFLECTION_LOD).rgb;
    float NdotV = max(dot(normal, viewDir), 0.0);
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ibl = (kD * diffuse + specular) * ao * iblIntensity;

    vec3 emissive = texture(emissiveMap, fragTexCoord).rgb * draw.emissiveFactor.rgb;

    vec3 color = ambient + ibl + Lo + emissive;
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}
