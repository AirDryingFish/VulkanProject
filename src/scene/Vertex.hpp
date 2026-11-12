#pragma once

#include <cstddef>
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

struct Vertex
{
    glm::vec3 pos{0.0f};
    glm::vec4 color{1.0f};
    glm::vec2 texcoord{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};

    // tangent.xyz 是切线方向
    // tangent.w = +1 或 -1 表示 bitangent handedness
    // tangent.w = 0 表示当前顶点没有有效 tangent
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 0.0f};

    // 第二套纹理坐标对应 gltf TEXCOORD_1
    glm::vec2 texcoord1{0.0f};

    bool operator==(const Vertex& other) const
    {
        return pos == other.pos && color == other.color && texcoord == other.texcoord && normal == other.normal &&
               tangent == other.tangent && texcoord1 == other.texcoord1;
    }
};

namespace std
{
template <>
// 哈希字段与 Vertex::operator== 保持一致，供 CPU 网格去重使用。
struct hash<Vertex>
{
    size_t operator()(Vertex const& vertex) const
    {
        size_t seed = 0;

        auto combine = [&seed](size_t value) { seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u); };

        combine(hash<glm::vec3>()(vertex.pos));
        combine(hash<glm::vec4>()(vertex.color));
        combine(hash<glm::vec2>()(vertex.texcoord));
        combine(hash<glm::vec3>()(vertex.normal));
        combine(hash<glm::vec4>()(vertex.tangent));
        combine(hash<glm::vec2>()(vertex.texcoord1));

        return seed;
    }
};
} // namespace std
