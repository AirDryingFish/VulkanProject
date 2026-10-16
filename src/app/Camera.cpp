#include "app/TriangleApplication.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

void TriangleApplication::processCameraInput(float deltaTime)
{
    constexpr int cameraMouseNone = 0;
    constexpr int cameraMouseLook = 1;
    constexpr int cameraMousePan = 2;

    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse && cameraControlMode != cameraMouseNone)
    {
        cameraControlMode = cameraMouseNone;
        firstMouse = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    const bool rightMousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool middleMousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;

    float speed = cameraMoveSpeed * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    {
        speed *= cameraFastMultiplier;
    }

    if (!io.WantCaptureMouse && cameraScrollOffset != 0.0f)
    {
        cameraPos += cameraFront * cameraScrollOffset * cameraScrollSpeed;
        cameraScrollOffset = 0.0f;
    }

    const glm::vec3 cameraRight = glm::normalize(glm::cross(cameraFront, cameraUp));
    const glm::vec3 cameraViewUp = glm::normalize(glm::cross(cameraRight, cameraFront));
    if (!io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    {
        cameraPos += cameraFront * speed;
    }
    if (!io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {
        cameraPos -= cameraFront * speed;
    }
    if (!io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    {
        cameraPos -= cameraRight * speed;
    }
    if (!io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    {
        cameraPos += cameraRight * speed;
    }
    if (!io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
    {
        cameraPos += cameraUp * speed;
    }
    if (!io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
    {
        cameraPos -= cameraUp * speed;
    }

    int desiredControlMode = cameraMouseNone;
    if (!io.WantCaptureMouse)
    {
        if (rightMousePressed)
        {
            desiredControlMode = cameraMouseLook;
        }
        else if (middleMousePressed)
        {
            desiredControlMode = cameraMousePan;
        }
    }

    if (desiredControlMode == cameraMouseNone)
    {
        if (cameraControlMode != cameraMouseNone)
        {
            cameraControlMode = cameraMouseNone;
            firstMouse = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        cameraTarget = cameraPos + cameraFront;
        return;
    }

    if (cameraControlMode != desiredControlMode)
    {
        cameraControlMode = desiredControlMode;
        firstMouse = true;
        glfwSetInputMode(window, GLFW_CURSOR, desiredControlMode == cameraMouseLook ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (firstMouse)
    {
        lastMouseX = static_cast<float>(mouseX);
        lastMouseY = static_cast<float>(mouseY);
        firstMouse = false;
    }

    const float xOffset = static_cast<float>(mouseX) - lastMouseX;
    const float yOffset = lastMouseY - static_cast<float>(mouseY);
    lastMouseX = static_cast<float>(mouseX);
    lastMouseY = static_cast<float>(mouseY);

    if (cameraControlMode == cameraMouseLook)
    {
        cameraYaw += -xOffset * mouseSensitivity;
        cameraPitch += yOffset * mouseSensitivity;
        cameraPitch = std::clamp(cameraPitch, -89.0f, 89.0f);

        const float yaw = glm::radians(cameraYaw);
        const float pitch = glm::radians(cameraPitch);
        cameraFront = glm::normalize(glm::vec3(
            std::cos(pitch) * std::cos(yaw),
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch)));
    }
    else if (cameraControlMode == cameraMousePan)
    {
        float panSpeed = cameraPanSpeed;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        {
            panSpeed *= cameraFastMultiplier;
        }

        cameraPos -= cameraRight * xOffset * panSpeed;
        cameraPos -= cameraViewUp * yOffset * panSpeed;
    }

    cameraTarget = cameraPos + cameraFront;
}

void TriangleApplication::scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    auto app = reinterpret_cast<TriangleApplication *>(glfwGetWindowUserPointer(window));
    if (app != nullptr && !ImGui::GetIO().WantCaptureMouse)
    {
        app->cameraScrollOffset += static_cast<float>(yoffset);
    }
}
