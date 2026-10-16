#include "app/CameraController.hpp"
#include "scene/Camera.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

void CameraController::update(Camera& camera, GLFWwindow* window, float deltaTime)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && mouseMode_ != MouseMode::None)
    {
        mouseMode_ = MouseMode::None;
        firstMouse_ = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    const bool fast = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    const float multiplier = fast ? camera.fastMultiplier : 1.0f;
    const float speed = camera.moveSpeed * deltaTime * multiplier;

    if (!io.WantCaptureMouse)
        camera.position += camera.front * scrollOffset_ * camera.scrollSpeed;
    // A wheel event consumed by UI must not move the camera on a later frame.
    scrollOffset_ = 0.0f;

    if (!io.WantCaptureKeyboard)
    {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.position += camera.front * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.position -= camera.front * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.position -= camera.right() * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.position += camera.right() * speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            camera.position += camera.up * speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            camera.position -= camera.up * speed;
    }

    MouseMode desiredMode = MouseMode::None;
    if (!io.WantCaptureMouse)
    {
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
            desiredMode = MouseMode::Look;
        else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
            desiredMode = MouseMode::Pan;
    }

    if (desiredMode != mouseMode_)
    {
        mouseMode_ = desiredMode;
        firstMouse_ = true;
        glfwSetInputMode(window, GLFW_CURSOR,
                        mouseMode_ == MouseMode::Look ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    if (mouseMode_ == MouseMode::None)
        return;

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    if (firstMouse_)
    {
        lastMouseX_ = static_cast<float>(mouseX);
        lastMouseY_ = static_cast<float>(mouseY);
        firstMouse_ = false;
    }

    const float xOffset = static_cast<float>(mouseX) - lastMouseX_;
    const float yOffset = lastMouseY_ - static_cast<float>(mouseY);
    lastMouseX_ = static_cast<float>(mouseX);
    lastMouseY_ = static_cast<float>(mouseY);

    if (mouseMode_ == MouseMode::Look)
        camera.rotate(-xOffset * camera.mouseSensitivity, yOffset * camera.mouseSensitivity);
    else
        camera.pan(xOffset, yOffset, camera.panSpeed * multiplier);
}

void CameraController::addScroll(double verticalOffset)
{
    if (ImGui::GetCurrentContext() != nullptr && !ImGui::GetIO().WantCaptureMouse)
        scrollOffset_ += static_cast<float>(verticalOffset);
}

void CameraController::reset(GLFWwindow* window)
{
    mouseMode_ = MouseMode::None;
    firstMouse_ = true;
    lastMouseX_ = 0.0f;
    lastMouseY_ = 0.0f;
    scrollOffset_ = 0.0f;
    if (window != nullptr)
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}
