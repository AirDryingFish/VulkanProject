#pragma once

#include <glm/glm.hpp>

// Camera geometry and tunable parameters are independent of the window and UI.
// The editor uses a right-handed, Z-up world and Vulkan's 0..1 depth range.
class Camera
{
public:
    Camera();

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

    void lookAt(const glm::vec3& eye, const glm::vec3& target);
    void rotate(float yawDeltaDegrees, float pitchDeltaDegrees);
    void pan(float horizontalPixels, float verticalPixels, float speed);

    glm::vec3 right() const;
    glm::vec3 viewUp() const;
    glm::mat4 view() const;
    glm::mat4 projection(float aspectRatio) const;
};
