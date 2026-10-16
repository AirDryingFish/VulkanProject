#pragma once

struct GLFWwindow;
class Camera;

// Window input and cursor ownership stay out of the camera's scene math.
// No window is retained: reset while it is alive when changing scenes.
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

    MouseMode mouseMode_ = MouseMode::None;
    bool firstMouse_ = true;
    float lastMouseX_ = 0.0f;
    float lastMouseY_ = 0.0f;
    float scrollOffset_ = 0.0f;
};
