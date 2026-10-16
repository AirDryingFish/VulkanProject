#include "scene/Scene.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool close(float actual, float expected)
    {
        return std::abs(actual - expected) < 1e-4f;
    }

    void testModelTransformOrder()
    {
        SceneObject object;
        object.transform.position = {10.0f, 20.0f, 30.0f};
        object.assetTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
        object.assetTransform = glm::rotate(object.assetTransform, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        object.transform.rotation = glm::vec3(90.0f);
        object.autoRotation = 90.0f;
        object.transform.scale = {2.0f, 3.0f, 4.0f};

        const glm::vec4 world = object.modelMatrix() * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
        // Local point: scale -> auto Z -> user Z/Y/X -> asset -> user position.
        require(close(world.x, 13.0f) && close(world.y, 34.0f) && close(world.z, 27.0f) && close(world.w, 1.0f),
                "model matrix must preserve user, asset, Euler, animation and scale composition order");
    }

    void testAnimation()
    {
        Scene scene;
        scene.objects.resize(3);
        scene.objects[0].autoRotation = 17.0f;
        scene.objects[1].autoRotate = true;
        scene.objects[1].autoRotation = 350.0f;
        scene.objects[1].autoRotateSpeed = 90.0f;
        scene.objects[2].autoRotate = true;
        scene.objects[2].autoRotation = -350.0f;
        scene.objects[2].autoRotateSpeed = -90.0f;

        scene.updateAnimations(0.5f);
        require(close(scene.objects[0].autoRotation, 17.0f), "disabled animation must not advance");
        require(close(scene.objects[1].autoRotation, 35.0f), "positive animation must use delta time and wrap");
        require(close(scene.objects[2].autoRotation, -35.0f), "negative animation must use delta time and wrap");

        scene.objects[1].autoRotation = 315.0f;
        scene.updateAnimations(0.5f);
        require(close(scene.objects[1].autoRotation, 360.0f), "exact revolution must preserve existing wrap boundary");
    }

    void testMissingBounds()
    {
        Scene scene;
        scene.objects.emplace_back();
        glm::vec3 minimum{}, maximum{};
        bool rejected = false;
        try
        {
            scene.bounds(minimum, maximum);
        }
        catch (const std::runtime_error& error)
        {
            rejected = std::string(error.what()) == "Scene contains no bounded geometry";
        }
        require(rejected, "scene without bounded meshes must reject preset fitting");
    }
}

int main()
{
    try
    {
        testModelTransformOrder();
        testAnimation();
        testMissingBounds();
        std::cout << "Scene tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
