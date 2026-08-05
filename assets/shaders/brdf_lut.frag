#version 450

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 1024u;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// 把一个整数bits的二进制位反转，再映射到[0, 1)区间，这样可以得到比普通随机数更均匀的采样点
float RadicalInverseVdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u)
         | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u)
         | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u)
         | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u)
         | ((bits & 0xFF00FF00u) >> 8u);

    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint index, uint count)
{
    return vec2(float(index) / float(count), RadicalInverseVdc(index));
}

// 采样GGX分布的半向量H，返回noraml空间下的光线采样向量
vec3 ImportanceSampleGGX(vec2 Xi, vec3 normal, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;

    // 把Z轴(0, 0, 1)为法线的向量H转换到normal为法线的空间中
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + normal * H.z;
    return normalize(sampleVec);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
     float alpha = roughness * roughness;
     // IBL 情况下使用的k，与直接光照中的k不同
     float k = alpha * 0.5;
     return NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec2 IntegrateBRDF(float NdotV, float roughness)
{
     vec3 normal = vec3(0.0, 0.0, 1.0);
     vec3 viewDir = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

     float scale = 0.0;
     float bias = 0.0;

     for (uint i = 0u; i < SAMPLE_COUNT; ++i)
     {
          vec2 Xi = Hammersley(i, SAMPLE_COUNT);
          vec3 halfway = ImportanceSampleGGX(Xi, normal, roughness);

          vec3 lightDir = normalize(2.0 * dot(viewDir, halfway) * halfway - viewDir);

          float NdotL = max(lightDir.z, 0.0);
          float NdotH = max(halfway.z, 0.0);
          float VdotH = max(dot(viewDir, halfway), 0.0);

          // 只有当光线在法线半球内时才进行采样
          if (NdotL > 0.0)
          {
               float geometry = GeometrySmith(NdotV, NdotL, roughness);
               // 当前采样方向在去掉GGX采样概率后，对最终BRDF积分应当贡献的可见性权重
               float visibility = geometry * VdotH / (NdotH * NdotV);

               float fresnel = pow(1.0 - VdotH, 5.0);
               scale += (1.0 - fresnel) * visibility;
               bias += fresnel * visibility;
          }
     }
     return vec2(scale, bias) / float(SAMPLE_COUNT);
}

void main()
{
     float NdotV = max(fragUV.x, 0.001);
     float roughness = max(fragUV.y, 0.001);
     
     vec2 integratedBRDF = IntegrateBRDF(NdotV, roughness);
     outColor = vec4(integratedBRDF, 0.0, 1.0);
}