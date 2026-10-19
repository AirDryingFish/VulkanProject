#include "scene/Scene.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

glm::mat4 SceneObject::modelMatrix() const
{
    // GLM 使用列向量：M = T用户 * M资产 * Rx * Ry * Rz * R自动 * S，作用于顶点时从右往左。
    // 保留导入节点的资产变换，再叠加用户平移；调整乘法顺序会改变旋转中心和缩放所在的坐标系。
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
        // 自动动画单独累计，不改 Inspector 中的手动 Euler 角；取余防止角度长期增长损失精度。
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
        // 旋转后原 AABB 的 min/max 两点不再是世界包围盒极值，必须变换全部八个角。
        // corner 的低三位分别选择 X/Y/Z 的 min 或 max；再逐点合并得到保守的世界 AABB。
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 local{
                (corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z};
            const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
            // 拒绝坏变换，防止 NaN/无穷传播到包围盒、相机拟合和阴影覆盖范围。
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
