#include "scene/Camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    bool finite(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
}

Camera::Camera()
{
    lookAt(position, glm::vec3(0.0f));
}

void Camera::lookAt(const glm::vec3& eye, const glm::vec3& target)
{
    const glm::vec3 direction = target - eye;
    const float lengthSquared = glm::dot(direction, direction);
    if (!finite(eye) || !finite(target) || !std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
        throw std::invalid_argument("Camera requires distinct, finite eye and target positions");

    position = eye;
    up = {0.0f, 0.0f, 1.0f};
    front = glm::normalize(direction);
    yaw = glm::degrees(std::atan2(front.y, front.x));
    pitch = glm::degrees(std::asin(std::clamp(front.z, -1.0f, 1.0f)));
}

void Camera::rotate(float yawDeltaDegrees, float pitchDeltaDegrees)
{
    yaw += yawDeltaDegrees;
    pitch = std::clamp(pitch + pitchDeltaDegrees, -89.0f, 89.0f);

    const float yawRadians = glm::radians(yaw);
    const float pitchRadians = glm::radians(pitch);
    front = glm::normalize(glm::vec3(
        std::cos(pitchRadians) * std::cos(yawRadians),
        std::cos(pitchRadians) * std::sin(yawRadians),
        std::sin(pitchRadians)));
}

glm::vec3 Camera::right() const
{
    glm::vec3 horizontal = glm::cross(front, up);
    if (glm::dot(horizontal, horizontal) < 1e-8f)
        horizontal = glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::normalize(horizontal);
}

glm::vec3 Camera::viewUp() const
{
    return glm::normalize(glm::cross(right(), front));
}

void Camera::pan(float horizontalPixels, float verticalPixels, float speed)
{
    position -= right() * horizontalPixels * speed;
    position -= viewUp() * verticalPixels * speed;
}

glm::mat4 Camera::view() const
{
    return glm::lookAtRH(position, position + front, viewUp());
}

glm::mat4 Camera::projection(float aspectRatio) const
{
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0f ||
        !std::isfinite(nearPlane) || nearPlane <= 0.0f ||
        !std::isfinite(farPlane) || farPlane <= nearPlane ||
        !std::isfinite(fieldOfViewDegrees) || fieldOfViewDegrees <= 0.0f || fieldOfViewDegrees >= 180.0f)
        throw std::invalid_argument("Camera requires a positive aspect ratio and valid perspective planes and field of view");

    glm::mat4 result = glm::perspectiveRH_ZO(glm::radians(fieldOfViewDegrees), aspectRatio, nearPlane, farPlane);
    result[1][1] *= -1.0f;
    return result;
}
