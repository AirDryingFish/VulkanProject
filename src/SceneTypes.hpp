#pragma once

#include "Mesh.hpp"
#include "Material.hpp"

#include <glm/glm.hpp>
#include <string>

struct Transform
{
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};
};

struct SceneObject
{
    std::string name;
    MeshSource source = MeshSource::Obj;
    std::string sourcePath;

    MeshHandle mesh;
    MaterialHandle material;

    // 从资产节点中计算出的变换
    glm::mat4 assetTransform{1.0f};

    // 用户在界面中调整的变换
    Transform transform;

    bool autoRotate = false;
    float autoRotation = 0.0f;
    float autoRotateSpeed = 90.0f;
};
