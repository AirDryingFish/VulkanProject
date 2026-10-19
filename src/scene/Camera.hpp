#pragma once

#include <glm/glm.hpp>

// Camera 只描述相机几何和可调参数；窗口事件、按键与鼠标模式由 CameraController 处理。
// 世界坐标采用右手系、Z 轴朝上；投影显式使用 Vulkan 的 [0, 1] 深度范围。
class Camera
{
public:
    Camera();

    // front 是单位视线方向，up 是世界向上参考；倾斜视角的屏幕向上由 viewUp() 计算。
    glm::vec3 position{2.0f, 2.0f, 2.0f};
    glm::vec3 front{};
    glm::vec3 up{0.0f, 0.0f, 1.0f};
    float yaw = -135.0f;
    float pitch = -35.264f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float fieldOfViewDegrees = 45.0f;

    float moveSpeed = 3.0f;
    float fastMultiplier = 3.0f;
    float scrollSpeed = 1.0f;
    float panSpeed = 0.01f;
    float mouseSensitivity = 0.12f;

    // 改变朝向时用这两个方法同步 front 与 yaw/pitch，避免输入接管时角度不一致。
    void lookAt(const glm::vec3& eye, const glm::vec3& target);
    void rotate(float yawDeltaDegrees, float pitchDeltaDegrees);
    void pan(float horizontalPixels, float verticalPixels, float speed);

    glm::vec3 right() const;
    glm::vec3 viewUp() const;
    glm::mat4 view() const;
    // 返回的矩阵已翻转 Y；场景绘制、拾取和编辑器覆盖层均直接使用，不能重复翻转。
    glm::mat4 projection(float aspectRatio) const;
};
