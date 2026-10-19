#pragma once

#include "scene/Lighting.hpp"
#include "scene/SceneTypes.hpp"

#include <vector>

// Scene 持有场景对象与灯光，并负责它们的空间计算和动画。
// GPU 上传、等待和预设替换事务仍由 Application 组织，避免把同步与回滚藏进数据修改。
class Scene
{
public:
    std::vector<SceneObject> objects;
    std::vector<PointLight> pointLights;
    DirectionalLight directionalLight;
    glm::vec3 ambientLightColor{1.0f};
    float ambientLightIntensity = 0.0f;
    float iblIntensity = 1.0f;

    // 每次场景更新调用一次；不能在阴影/主场景等每个 pass 里重复推进动画。
    void updateAnimations(float deltaTime);
    // 合并物体的世界空间 AABB；没有有效几何或出现非有限坐标时抛异常，交给预设事务回滚。
    void bounds(glm::vec3& minimum, glm::vec3& maximum) const;
};
