#include "app/CameraController.hpp"
#include "scene/Camera.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

void CameraController::update(Camera& camera, GLFWwindow* window, float deltaTime)
{
    const ImGuiIO& io = ImGui::GetIO();
    // UI 接管鼠标后立即结束相机拖动，避免编辑控件时仍转动视角或保持锁定光标。
    if (io.WantCaptureMouse && mouseMode_ != MouseMode::None)
    {
        mouseMode_ = MouseMode::None;
        firstMouse_ = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    const bool fast = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    const float multiplier = fast ? camera.fastMultiplier : 1.0f;
    // 键盘提供“持续按住”的状态，移动量需乘帧间秒数，才能保持每秒速度稳定。
    const float speed = camera.moveSpeed * deltaTime * multiplier;

    if (!io.WantCaptureMouse)
        camera.position += camera.front * scrollOffset_ * camera.scrollSpeed;
    // 即使 UI 捕获也清空增量，避免离开 UI 后才补执行旧的滚轮移动。
    scrollOffset_ = 0.0f;

    // 鼠标与键盘的捕获分别判断；在文本框输入 WASDQE 时不能同时移动相机。
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
        // 旋转/平移切换会改变光标模式；旧鼠标坐标不能用于新模式的第一帧。
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
        // 首帧只建立基准，使下方差值为零，避免开始拖动时视角突然跳变。
        lastMouseX_ = static_cast<float>(mouseX);
        lastMouseY_ = static_cast<float>(mouseY);
        firstMouse_ = false;
    }

    // 鼠标坐标 Y 向下递增，这里反向取差让抬鼠标得到正 pitch。
    // 差值已经是本帧移动的像素数，不再乘 deltaTime，否则手感会随帧率改变。
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
    // GLFW 回调只累计事件，实际位移统一在 update() 中执行，保持输入更新入口一致。
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
