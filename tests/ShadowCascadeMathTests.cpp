#include "render/ShadowCascades.hpp"
#include "scene/Camera.hpp"

#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    template <class Error>
    void requireRejected(float n, float f, float lambda)
    {
        try
        {
            (void)calculateCascadeSplits(n, f, lambda);
        }
        catch (const Error &)
        {
            return;
        }

        throw std::runtime_error(
            "Expected cascade split rejection");
    }

    void testKnownSplits()
    {
        const std::array<float, 3> lambdas{0.0f, 0.5f, 1.0f};

        const std::array<std::array<float, 4>, 3> expected{{{25.75f, 50.5f, 75.25f, 100.0f},
                                                            {14.456139f, 30.25f, 53.436389f, 100.0f},
                                                            {3.162278f, 10.0f, 31.622777f, 100.0f}}};

        for (std::size_t c = 0; c < lambdas.size(); ++c)
        {
            const auto splits =
                calculateCascadeSplits(1.0f, 100.0f, lambdas[c]);

            float previous = 1.0f;

            for (std::size_t i = 0; i < splits.size(); ++i)
            {
                require(
                    std::abs(splits[i] - expected[c][i]) < 1e-4f,
                    "Known cascade split differs");

                require(
                    std::isfinite(splits[i]) && splits[i] > previous,
                    "Splits must be finite and strictly increasing");

                previous = splits[i];
            }

            require(
                splits.back() == 100.0f,
                "Last split must equal shadowFar");
        }
    }

    void testInvalidInputs()
    {
        const float nan =
            std::numeric_limits<float>::quiet_NaN();
        const float inf =
            std::numeric_limits<float>::infinity();

        const std::array<std::array<float, 3>, 12> inputs{{{0.0f, 100.0f, 0.5f},
                                                           {-1.0f, 100.0f, 0.5f},
                                                           {1.0f, 1.0f, 0.5f},
                                                           {2.0f, 1.0f, 0.5f},
                                                           {1.0f, 100.0f, -0.1f},
                                                           {1.0f, 100.0f, 1.1f},
                                                           {nan, 100.0f, 0.5f},
                                                           {1.0f, nan, 0.5f},
                                                           {1.0f, 100.0f, nan},
                                                           {inf, 100.0f, 0.5f},
                                                           {1.0f, inf, 0.5f},
                                                           {1.0f, 100.0f, inf}}};

        for (const auto &input : inputs)
        {
            requireRejected<std::invalid_argument>(
                input[0], input[1], input[2]);
        }
    }

    void testRepresentation()
    {
        const auto wide =
            calculateCascadeSplits(1e-30f, 1e30f, 1.0f);

        float previous = 1e-30f;

        for (float split : wide)
        {
            require(
                std::isfinite(split) && split > previous,
                "Wide range must remain finite and increasing");

            previous = split;
        }

        require(
            wide.back() == 1e30f,
            "Wide range must preserve far boundary");

        // 两个相邻 float 之间放不下四个递增边界。
        const float next = std::nextafter(1.0f, 2.0f);
        requireRejected<std::range_error>(1.0f, next, 0.5f);
    }

    void testKnownFrustumSlice()
    {
        const auto corners = calculateFrustumSliceCorners(
            glm::mat4(1.0f), 90.0f, 2.0f, 1.0f, 3.0f);

        const FrustumSliceCorners expected{{{-2.0f, -1.0f, -1.0f}, {2.0f, -1.0f, -1.0f}, {2.0f, 1.0f, -1.0f}, {-2.0f, 1.0f, -1.0f},

                                            {-6.0f, -3.0f, -3.0f},
                                            {6.0f, -3.0f, -3.0f},
                                            {6.0f, 3.0f, -3.0f},
                                            {-6.0f, 3.0f, -3.0f}}};

        for (std::size_t i = 0; i < corners.size(); ++i)
        {
            require(
                glm::length(corners[i] - expected[i]) < 1e-4f,
                "Known frustum corner differs");
        }
    }

    void testCascadeReprojection()
    {
        const std::array<glm::vec3, 3> eyes{{{0.0f, 0.0f, 0.0f},
                                             {3.0f, -5.0f, 2.0f},
                                             {0.0f, 0.0f, 5.0f}}};

        const std::array<glm::vec3, 3> targets{{{0.0f, 1.0f, 0.0f},
                                                {0.0f, 0.0f, 1.0f},
                                                {0.0f, 0.0f, 0.0f}}};

        // Camera::projection() 已翻转 Y。
        const std::array<glm::vec2, 4> expectedNdc{{{-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}}};

        for (std::size_t pose = 0; pose < eyes.size(); ++pose)
            for (float fov : {45.0f, 90.0f})
                for (float aspect : {16.0f / 9.0f, 0.75f})
                {
                    Camera camera;
                    camera.lookAt(eyes[pose], targets[pose]);
                    camera.nearPlane = 1.0f;
                    camera.farPlane = 100.0f;
                    camera.fieldOfViewDegrees = fov;

                    const glm::mat4 view = camera.view();
                    const glm::mat4 inverseView = glm::inverse(view);
                    const glm::mat4 projection = camera.projection(aspect);

                    const auto splits = calculateCascadeSplits(
                        camera.nearPlane, 60.0f, 0.5f);

                    float nearDistance = camera.nearPlane;
                    FrustumSliceCorners previous{};

                    for (std::size_t cascade = 0;
                         cascade < splits.size();
                         ++cascade)
                    {
                        const auto corners = calculateFrustumSliceCorners(
                            inverseView,
                            fov,
                            aspect,
                            nearDistance,
                            splits[cascade]);

                        for (std::size_t i = 0; i < corners.size(); ++i)
                        {
                            const float distance =
                                i < 4 ? nearDistance : splits[cascade];

                            const glm::vec4 inView =
                                view * glm::vec4(corners[i], 1.0f);

                            require(
                                std::abs(-inView.z - distance) < 1e-3f,
                                "Corner has incorrect view distance");

                            const glm::vec4 clip = projection * inView;

                            require(
                                clip.w > 0.0f,
                                "Corner must be in front of camera");

                            const glm::vec3 ndc =
                                glm::vec3(clip) / clip.w;

                            require(
                                glm::length(
                                    glm::vec2(ndc) - expectedNdc[i % 4]) < 1e-3f,
                                "Corner must project to the expected screen edge");

                            require(
                                ndc.z >= -1e-4f && ndc.z <= 1.0001f,
                                "Corner depth must remain within camera range");

                            const glm::vec4 reference =
                                projection *
                                glm::vec4(0.0f, 0.0f, -distance, 1.0f);

                            require(
                                std::abs(
                                    ndc.z - reference.z / reference.w) < 1e-4f,
                                "Corner depth must match its camera distance");
                        }

                        if (cascade > 0)
                        {
                            for (std::size_t i = 0; i < 4; ++i)
                            {
                                require(
                                    glm::length(
                                        previous[i + 4] - corners[i]) < 1e-4f,
                                    "Adjacent cascade faces must coincide");
                            }
                        }

                        previous = corners;
                        nearDistance = splits[cascade];
                    }
                }
    }

    void testInvalidFrustumSlices()
    {
        const float nan =
            std::numeric_limits<float>::quiet_NaN();

        // 每项依次为 FOV、aspect、近距离、远距离。
        const std::array<std::array<float, 4>, 7> inputs{{{0.0f, 1.0f, 1.0f, 10.0f},
                                                          {180.0f, 1.0f, 1.0f, 10.0f},
                                                          {nan, 1.0f, 1.0f, 10.0f},
                                                          {60.0f, 0.0f, 1.0f, 10.0f},
                                                          {60.0f, 1.0f, 0.0f, 10.0f},
                                                          {60.0f, 1.0f, 2.0f, 1.0f},
                                                          {60.0f, 1.0f, 1.0f, 1.0f}}};

        for (const auto &input : inputs)
        {
            bool rejected = false;

            try
            {
                (void)calculateFrustumSliceCorners(
                    glm::mat4(1.0f),
                    input[0],
                    input[1],
                    input[2],
                    input[3]);
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }

            require(
                rejected,
                "Invalid frustum slice must be rejected");
        }
    }
}

int main()
{
    try
    {
        testKnownSplits();
        testInvalidInputs();
        testRepresentation();

        testKnownFrustumSlice();
        testCascadeReprojection();
        testInvalidFrustumSlices();

        std::cout << "Shadow cascade math tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}