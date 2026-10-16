#pragma once

#include "scene/Lighting.hpp"
#include "scene/SceneTypes.hpp"

#include <vector>

// Scene content owns its objects and lighting. GPU upload and preset replacement
// remain application operations so their synchronization and rollback stay explicit.
class Scene
{
public:
    std::vector<SceneObject> objects;
    std::vector<PointLight> pointLights;
    DirectionalLight directionalLight;
    glm::vec3 ambientLightColor{1.0f};
    float ambientLightIntensity = 0.0f;
    float iblIntensity = 1.0f;

    void updateAnimations(float deltaTime);
    void bounds(glm::vec3& minimum, glm::vec3& maximum) const;
};
