#pragma once

struct GLFWwindow;
class Camera;

// 将 GLFW/ImGui 输入解释为 Camera 操作，相机数学本身无需依赖窗口或 UI。
// 不持有窗口；切换场景时须在窗口仍有效时 reset()，清除拖动并恢复光标。
class CameraController
{
public:
    void update(Camera& camera, GLFWwindow* window, float deltaTime);
    void addScroll(double verticalOffset);
    void reset(GLFWwindow* window);

private:
    enum class MouseMode
    {
        None,
        Look,
        Pan,
    };

    // 这些是跨帧的输入状态，不属于场景，也不能由 UI 直接修改。
    MouseMode mouseMode_ = MouseMode::None;
    bool firstMouse_ = true;
    float lastMouseX_ = 0.0f;
    float lastMouseY_ = 0.0f;
    float scrollOffset_ = 0.0f;
};
