// 让 Forward 和 G-buffer 使用同一套材质求值
#ifndef PROJECT_MATERIAL_GLSL
#define PROJECT_MATERIAL_GLSL

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec4 fragTangent;
layout(location = 5) in vec2 fragTexCoord1;

layout(push_constant) uniform DrawPushConstant
{
    mat4 model;
    vec4 baseColorFactor;
    vec4 materialFactors; // metallic, roughness, AO strength, normalScale
    vec4 emissiveFactor; // rgb: emissive factor, w: alpha cutoff
    // x: 纹理 UV set bit mask
    // y: 当前 Mesh 是否有有效 vertex tangent
    // z: 0 = OPAQUE, 1 = MASK
    // w: shadow draw 使用 cascade index；scene draw 保持 0
    uvec4 textureInfo;
} draw;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;


// 根据纹理槽位决定这张纹理使用 uv0 还是 uv1
vec2 materialUv(uint slot)
{
    // 检查 draw.textureInfo.x 的第 slot 位是不是 1
    return (draw.textureInfo.x & (1u << slot)) != 0u ? fragTexCoord1 : fragTexCoord;
}

// 返回世界空间法线
vec3 getNormalFromNormalMap()
{
    vec2 uv = materialUv(1u);
    vec3 mapped = texture(normalMap, uv).xyz * 2.0 - 1.0;
    mapped.xy *= draw.materialFactors.w;

    float lengthSquared = dot(mapped, mapped);
    mapped = lengthSquared > 1e-8 ? mapped * inversesqrt(lengthSquared) : vec3(0.0, 0.0, 1.0);

    vec3 n = normalize(fragNormal);
    vec3 t;
    vec3 b;

    // 如果有 tangent，那么就用模型自带的 tangent
    if (draw.textureInfo.y != 0u)
    {
        // 模型顶点上正确的 tangent，经过变换和光栅化插值，到片元阶段时不一定仍然与
        t = fragTangent.xyz - n * dot(n, fragTangent.xyz);
        float tangentLength = length(t);
        if (tangentLength < 0.0001 || abs(fragTangent.w) < 0.5)
        {
            return n;
        }

        t /= tangentLength;
        b = normalize(cross(n, t)) * (fragTangent.w < 0.0 ? -1.0 : 1.0);
    }

    // 没有 tangent，使用模型算出来的
    else
    {
        // 在屏幕上横向或纵向移动一点，对应的物体表面世界位置如何变化
        vec3 q1 = dFdx(fragWorldPos);
        vec3 q2 = dFdy(fragWorldPos);
        // 在屏幕上横向或纵向移动一点，对应的 uv 坐标如何变化
        vec2 st1 = dFdx(uv);
        vec2 st2 = dFdy(uv);
        float determinant = st1.x * st2.y - st1.y * st2.x;;
        if (abs(determinant) < 1e-8)
        {
            return n;
        }

        vec3 tangentRaw = (q1 * st2.y - q2 * st1.y) / determinant;
        vec3 bitangentRaw = (-q1 * st2.x + q2 * st1.x) / determinant;

        t = tangentRaw - n * dot(tangentRaw, n);
        float tangetLength = length(t);
        if (tangetLength < 0.0001)
        {
            return n;
        }

        t /= tangetLength;

        vec3 crossNT = cross(n, t);
        float crossLength = length(crossNT);
        if (crossLength < 0.0001)
        {
            return n;
        }

        float handedness = dot(crossNT, bitangentRaw) < 0.0 ? -1.0 : 1.0;
        b = crossNT / crossLength * handedness;
    }

    return normalize(mat3(t, b, n) * mapped);
}

vec4 sampleBaseColor()
{
    return texture(albedoMap, materialUv(0u)) * draw.baseColorFactor * fragColor;
}

bool materialAlphaRejected(vec4 baseColor)
{
    return draw.textureInfo.z == 1u && baseColor.a < draw.emissiveFactor.w;
}

struct MaterialSample
{
    vec4 baseColor;
    vec3 normal;
    float metallic;
    float roughness;
    float ao;
    vec3 emissive;
    bool alphaRejected;
};

// 求出当前片元的完整材质属性，打包成 MaterialSample 返回
MaterialSample evalueteMaterial(vec4 baseColor)
{
    MaterialSample result;
    result.baseColor = baseColor;
    result.alphaRejected = materialAlphaRejected(baseColor);

    vec4 materialRoughness = texture(metallicRoughnessMap, materialUv(2u));
    result.metallic = clamp(materialRoughness.b * draw.materialFactors.x, 0.0, 1.0);
    result.roughness = clamp(materialRoughness.g * draw.materialFactors.y, 0.04, 1.0);
    result.ao = mix(1.0, texture(aoMap, materialUv(3u)).r, clamp(draw.materialFactors.z, 0.0, 1.0));

    result.normal = getNormalFromNormalMap();
    if (!gl_FrontFacing)
    {
        result.normal = -result.normal;
    }

    result.emissive = texture(emissiveMap, materialUv(4u)).rgb * draw.emissiveFactor.rgb;
    return result;
}


#endif