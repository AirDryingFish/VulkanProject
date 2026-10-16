#include "scene/Camera.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool close(float actual, float expected, float tolerance = 1e-5f)
    {
        return std::abs(actual - expected) < tolerance;
    }

    void testViewAndOrientation()
    {
        Camera camera;
        const glm::vec3 eye(0.0f, -5.0f, 2.0f);
        const glm::vec3 target(0.0f, 0.0f, 2.0f);
        camera.lookAt(eye, target);

        require(close(camera.yaw, 90.0f), "lookAt must synchronize yaw");
        require(close(camera.pitch, 0.0f), "lookAt must synchronize pitch");
        const glm::vec4 eyeInView = camera.view() * glm::vec4(eye, 1.0f);
        const glm::vec4 targetInView = camera.view() * glm::vec4(target, 1.0f);
        require(glm::length(glm::vec3(eyeInView)) < 1e-5f, "view must map eye to origin");
        require(close(targetInView.x, 0.0f) && close(targetInView.y, 0.0f) && close(targetInView.z, -5.0f),
                "camera must look along negative view-space Z");

        camera.rotate(-90.0f, 0.0f);
        require(close(camera.front.x, 1.0f) && close(camera.front.y, 0.0f), "rotation must use Z-up yaw");
        camera.rotate(0.0f, 200.0f);
        require(close(camera.pitch, 89.0f), "look input must clamp pitch before reaching the pole");
        require(close(glm::length(camera.front), 1.0f), "rotation must preserve a unit forward direction");
    }

    void testProjection()
    {
        Camera camera;
        camera.nearPlane = 0.5f;
        camera.farPlane = 250.0f;
        const glm::mat4 projection = camera.projection(16.0f / 9.0f);
        const glm::vec4 nearClip = projection * glm::vec4(0.0f, 0.0f, -camera.nearPlane, 1.0f);
        const glm::vec4 farClip = projection * glm::vec4(0.0f, 0.0f, -camera.farPlane, 1.0f);
        require(close(nearClip.z / nearClip.w, 0.0f), "Vulkan near depth must be zero");
        require(close(farClip.z / farClip.w, 1.0f), "Vulkan far depth must be one");
        require(projection[1][1] < 0.0f, "projection must invert Y exactly once for Vulkan");

        bool rejected = false;
        try
        {
            (void)camera.projection(0.0f);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "zero aspect ratio must be rejected");
    }

    void testPanAndVerticalView()
    {
        Camera camera;
        camera.lookAt({0.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
        camera.pan(10.0f, 20.0f, 0.01f);
        require(close(camera.position.x, -0.1f) && close(camera.position.y, -5.0f) && close(camera.position.z, -0.2f),
                "pan must move along the camera screen axes");

        camera.lookAt({0.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 0.0f});
        const glm::mat4 verticalView = camera.view();
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                require(std::isfinite(verticalView[column][row]), "vertical lookAt must retain a finite view basis");

        bool rejected = false;
        try
        {
            camera.lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "coincident eye and target must be rejected");
    }
}

int main()
{
    try
    {
        testViewAndOrientation();
        testProjection();
        testPanAndVerticalView();
        std::cout << "Camera tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
