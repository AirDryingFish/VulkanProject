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
    Transform transform;

    bool autoRotate = false;
    float autoRotation = 0.0f;
    float autoRotateSpeed = 90.0f;
};
