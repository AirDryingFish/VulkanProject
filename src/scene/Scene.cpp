#include "scene/Scene.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

glm::mat4 SceneObject::modelMatrix() const
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position);
    model = model * assetTransform;
    model = glm::rotate(model, glm::radians(transform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(transform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(transform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, glm::radians(autoRotation), glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, transform.scale);
}

void Scene::updateAnimations(float deltaTime)
{
    for (SceneObject& object : objects)
    {
        if (!object.autoRotate)
            continue;

        object.autoRotation += object.autoRotateSpeed * deltaTime;
        if (object.autoRotation > 360.0f || object.autoRotation < -360.0f)
            object.autoRotation = std::fmod(object.autoRotation, 360.0f);
    }
}

void Scene::bounds(glm::vec3& minimum, glm::vec3& maximum) const
{
    minimum = glm::vec3(std::numeric_limits<float>::max());
    maximum = glm::vec3(std::numeric_limits<float>::lowest());
    bool found = false;
    for (const SceneObject& object : objects)
    {
        if (!object.mesh || !object.mesh->boundsValid)
            continue;

        const glm::mat4 model = object.modelMatrix();
        const Mesh& mesh = *object.mesh;
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 local{
                (corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z};
            const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
            if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
                throw std::runtime_error("Scene contains non-finite world bounds");

            minimum = glm::min(minimum, world);
            maximum = glm::max(maximum, world);
            found = true;
        }
    }
    if (!found)
        throw std::runtime_error("Scene contains no bounded geometry");
}
