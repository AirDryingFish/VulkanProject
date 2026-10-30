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

    void testKnownShadowXYAndSnapping()
    {
        const auto cornersView = calculateFrustumSliceCorners(
            glm::mat4(1.0f), 90.0f, 1.0f, 1.0f, 3.0f);

        const glm::vec3 direction(0.0f, 0.0f, -1.0f);

        const auto base = fitShadowCascadeXY(
            cornersView, glm::mat4(1.0f), direction, 16);

        const float expectedRadius = std::sqrt(19.0f);

        require(
            glm::length(
                base.centerWorld - glm::vec3(0, 0, -2)) < 1e-5f,
            "Unexpected slice center");

        require(
            std::abs(base.radius - expectedRadius) < 1e-5f,
            "Unexpected slice radius");

        require(
            std::abs(
                base.halfExtent -
                expectedRadius * 4.0f / 3.0f) < 1e-5f,
            "Unexpected guarded extent");

        require(
            std::abs(
                base.texelWorldSize -
                expectedRadius / 6.0f) < 1e-5f,
            "Unexpected world texel size");

        const float texel = base.texelWorldSize;

        glm::mat4 inverseView(1.0f);
        inverseView[3].x = 0.2f * texel;

        const auto smallMove = fitShadowCascadeXY(
            cornersView, inverseView, direction, 16);

        require(
            glm::length(
                smallMove.snappedCenterLightSpace) < 1e-5f,
            "Move within this grid cell must keep snapped center");

        inverseView[3].x = 0.6f * texel;

        const auto nextCell = fitShadowCascadeXY(
            cornersView, inverseView, direction, 16);

        require(
            glm::length(
                nextCell.snappedCenterLightSpace -
                glm::vec2(texel, 0)) < 1e-5f,
            "Crossing the rounding boundary must move one texel");
    }

    void testShadowXYCoverage()
    {
        const auto cornersView = calculateFrustumSliceCorners(
            glm::mat4(1.0f),
            60.0f, 16.0f / 9.0f, 1.0f, 30.0f);

        const std::array<glm::vec3, 3> directions{{{-1.0f, -1.0f, -2.0f},
                                                   {0.0f, 0.0f, -1.0f},
                                                   {0.0f, 0.0f, 1.0f}}};

        const std::array<glm::vec3, 3> eyes{{{0.0f, -5.0f, 2.0f},
                                             {3.0f, -2.0f, 4.0f},
                                             {0.0f, 0.0f, 5.0f}}};

        for (const auto &direction : directions)
        {
            const auto reference = fitShadowCascadeXY(
                cornersView, glm::mat4(1.0f), direction, 2048);

            for (const auto &eye : eyes)
            {
                Camera camera;
                camera.lookAt(eye, glm::vec3(0.0f));

                const glm::mat4 inverseView =
                    glm::inverse(camera.view());

                const auto fit = fitShadowCascadeXY(
                    cornersView, inverseView, direction, 2048);

                require(
                    fit.radius == reference.radius &&
                        fit.halfExtent == reference.halfExtent &&
                        fit.texelWorldSize == reference.texelWorldSize,
                    "Camera pose must not change XY size");

                const glm::vec3 lightForward = glm::vec3(
                    fit.lightRotation *
                    glm::vec4(glm::normalize(direction), 0.0f));

                require(
                    glm::length(
                        lightForward -
                        glm::vec3(0, 0, -1)) < 1e-5f,
                    "Light propagation must map to negative light Z");

                const glm::vec2 grid =
                    fit.snappedCenterLightSpace /
                    fit.texelWorldSize;

                require(
                    glm::length(
                        grid - glm::round(grid)) < 1e-3f,
                    "Shadow center must lie on texel grid");

                for (const auto &cornerView : cornersView)
                {
                    const glm::vec4 world =
                        inverseView * glm::vec4(cornerView, 1.0f);

                    const glm::vec2 delta =
                        glm::vec2(fit.lightRotation * world) -
                        fit.snappedCenterLightSpace;

                    const float limit =
                        fit.halfExtent - fit.texelWorldSize;

                    require(
                        std::abs(delta.x) <= limit + 1e-4f &&
                            std::abs(delta.y) <= limit + 1e-4f,
                        "All corners must retain a PCF texel of margin");
                }
            }
        }
    }

    void testInvalidShadowXY()
    {
        const auto cornersView = calculateFrustumSliceCorners(
            glm::mat4(1.0f), 60.0f, 1.0f, 1.0f, 10.0f);

        const auto expectRejected =
            [&](const FrustumSliceCorners &corners,
                const glm::vec3 &direction,
                std::uint32_t resolution)
        {
            bool rejected = false;

            try
            {
                (void)fitShadowCascadeXY(
                    corners,
                    glm::mat4(1.0f),
                    direction,
                    resolution);
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }

            require(
                rejected,
                "Invalid shadow XY input must be rejected");
        };

        expectRejected(cornersView, {0, 0, -1}, 4);
        expectRejected(cornersView, {0, 0, 0}, 2048);
        expectRejected(FrustumSliceCorners{}, {0, 0, -1}, 2048);
    }

    glm::vec3 shadowNdc(
        const glm::mat4 &matrix,
        const glm::vec3 &world)
    {
        const glm::vec4 clip =
            matrix * glm::vec4(world, 1.0f);

        require(
            std::isfinite(clip.w) && clip.w > 0.0f,
            "Shadow clip W must be positive and finite");

        return glm::vec3(clip) / clip.w;
    }

    void testKnownShadowMatrices()
    {
        ShadowCascadeXY xy{};
        xy.halfExtent = 4.0f;
        xy.snappedCenterLightSpace = {2.0f, -3.0f};

        const auto matrices = calculateShadowCascadeMatrices(
            xy,
            {-2.0f, -8.0f, -10.0f},
            {8.0f, 4.0f, 6.0f},
            2.0f,
            1.0f);

        require(
            matrices.nearPlane == 1.0f &&
                matrices.farPlane == 21.0f,
            "Unexpected light depth interval");

        const glm::vec3 originInView(
            matrices.view * glm::vec4(0, 0, 0, 1));

        require(
            glm::length(
                originInView - glm::vec3(-2, 3, -9)) < 1e-5f,
            "Unexpected light view translation");

        const auto closest =
            shadowNdc(matrices.viewProjection, {2, -3, 6});

        const auto farthest =
            shadowNdc(matrices.viewProjection, {2, -3, -10});

        require(
            std::abs(closest.z - 0.1f) < 1e-5f &&
                std::abs(farthest.z - 0.9f) < 1e-5f,
            "Scene endpoints must retain depth margin");

        require(
            std::abs(shadowNdc(
                         matrices.viewProjection, {6, -3, 6})
                         .x -
                     1.0f) < 1e-5f,
            "Right edge must map to positive NDC X");

        require(
            std::abs(shadowNdc(
                         matrices.viewProjection, {2, 1, 6})
                         .y +
                     1.0f) < 1e-5f,
            "Upper edge must include one Vulkan Y flip");
    }

    void testShadowMatrixCoverage()
    {
        const glm::vec3 minimum(-2.0f, -3.0f, -4.0f);
        const glm::vec3 maximum(5.0f, 7.0f, 11.0f);

        const auto cornersView = calculateFrustumSliceCorners(
            glm::mat4(1.0f),
            60.0f, 16.0f / 9.0f, 1.0f, 30.0f);

        Camera camera;
        camera.lookAt(
            {3.0f, -5.0f, 2.0f},
            {0.0f, 0.0f, 0.0f});

        const glm::mat4 inverseView =
            glm::inverse(camera.view());

        const std::array<glm::vec3, 3> directions{{{1.0f, -2.0f, -3.0f},
                                                   {0.0f, 0.0f, -1.0f},
                                                   {0.0f, 0.0f, 1.0f}}};

        for (std::size_t d = 0; d < directions.size(); ++d)
        {
            const auto xy = fitShadowCascadeXY(
                cornersView, inverseView, directions[d], 2048);

            const auto matrices = calculateShadowCascadeMatrices(
                xy, minimum, maximum, 1.0f, 0.5f);

            if (d == 0)
            {
                require(
                    std::abs(
                        matrices.farPlane -
                        (72.0f / std::sqrt(14.0f) + 2.5f)) < 1e-4f,
                    "Oblique light must include all eight AABB corners");
            }

            for (std::size_t i = 0; i < 8; ++i)
            {
                const glm::vec3 world(
                    (i & 1) ? maximum.x : minimum.x,
                    (i & 2) ? maximum.y : minimum.y,
                    (i & 4) ? maximum.z : minimum.z);

                const auto ndc =
                    shadowNdc(matrices.viewProjection, world);

                require(
                    ndc.z >= -1e-4f && ndc.z <= 1.0001f,
                    "Scene caster depth must remain within light range");
            }

            for (const auto &corner : cornersView)
            {
                const glm::vec3 world(
                    inverseView * glm::vec4(corner, 1.0f));

                const auto ndc =
                    shadowNdc(matrices.viewProjection, world);

                require(
                    std::abs(ndc.x) <= 1.0001f &&
                        std::abs(ndc.y) <= 1.0001f,
                    "Slice corners must remain within shadow XY");
            }

            const glm::vec4 anchorWorld =
                glm::inverse(xy.lightRotation) *
                glm::vec4(
                    xy.snappedCenterLightSpace, 0.0f, 1.0f);

            require(
                glm::length(glm::vec2(shadowNdc(
                    matrices.viewProjection,
                    glm::vec3(anchorWorld)))) < 1e-4f,
                "Snapped center must map to NDC XY origin");
        }
    }

    void testInvalidShadowMatrices()
    {
        ShadowCascadeXY xy{};
        xy.halfExtent = 4.0f;

        const auto expectInvalid =
            [&](const glm::vec3 &minimum,
                const glm::vec3 &maximum,
                float margin,
                float nearPlane)
        {
            bool rejected = false;

            try
            {
                (void)calculateShadowCascadeMatrices(
                    xy, minimum, maximum, margin, nearPlane);
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }

            require(
                rejected,
                "Invalid shadow matrix input must be rejected");
        };

        expectInvalid({0, 0, 0}, {1, 1, 1}, -1.0f, 0.1f);
        expectInvalid({0, 0, 0}, {1, 1, 1}, 1.0f, 0.0f);
        expectInvalid({2, 0, 0}, {1, 1, 1}, 1.0f, 0.1f);

        const float nan =
            std::numeric_limits<float>::quiet_NaN();

        expectInvalid({nan, 0, 0}, {1, 1, 1}, 1.0f, 0.1f);

        // near+1 在 float 中舍入回 near。
        bool rejected = false;

        try
        {
            (void)calculateShadowCascadeMatrices(
                xy,
                {0, 0, 0},
                {1, 1, 1},
                0.0f,
                16777216.0f);
        }
        catch (const std::range_error &)
        {
            rejected = true;
        }

        require(
            rejected,
            "Collapsed float depth interval must be rejected");
    }

    ShadowCascadeInput makeCascadeFrameInput()
    {
        ShadowCascadeInput input{};
        input.fovDegrees = 90.0f;
        input.aspectRatio = 1.0f;
        input.cameraNear = 1.0f;
        input.cameraFar = 100.0f;
        input.shadowDistance = 17.0f;
        input.splitLambda = 0.0f;
        input.lightDirection = {0.0f, 0.0f, -1.0f};
        input.sceneMinimum = glm::vec3(-20.0f);
        input.sceneMaximum = glm::vec3(20.0f);
        input.resolution = 16;
        input.depthMargin = 1.0f;
        input.lightNear = 0.5f;
        input.receiverBiasWorld = 0.042f;
        return input;
    }

    void testKnownCascadeFrame()
    {
        const auto frame =
            calculateShadowCascadeFrame(makeCascadeFrameInput());

        const std::array<float, 4> nearDistances{1, 5, 9, 13};
        const std::array<float, 4> farDistances{5, 9, 13, 17};
        const std::array<float, 4> centerDepths{3, 7, 11, 15};
        const std::array<float, 4> radiusSquared{54, 166, 342, 582};

        for (std::size_t i = 0; i < frame.cascades.size(); ++i)
        {
            const auto &cascade = frame.cascades[i];

            require(
                cascade.nearDistance == nearDistances[i] &&
                    cascade.farDistance == farDistances[i],
                "Cascade intervals must advance without gaps");

            require(
                glm::length(
                    cascade.xy.centerWorld -
                    glm::vec3(0, 0, -centerDepths[i])) < 1e-4f,
                "Cascade must use its own near and far boundaries");

            require(
                std::abs(
                    cascade.xy.radius -
                    std::sqrt(radiusSquared[i])) < 1e-4f,
                "Cascade sphere must match its interval");

            require(
                cascade.matrices.nearPlane == 0.5f &&
                    cascade.matrices.farPlane == 42.5f,
                "Light depth range must use scene bounds");

            require(
                std::abs(cascade.receiverBias - 0.001f) < 1e-6f,
                "World bias must use light depth span");

            const auto ndc = shadowNdc(
                cascade.matrices.viewProjection,
                {farDistances[i], 0, -farDistances[i]});

            const float expectedX =
                farDistances[i] /
                (std::sqrt(radiusSquared[i]) * 4.0f / 3.0f);

            require(
                std::abs(ndc.x - expectedX) < 1e-4f,
                "Each cascade matrix must match its own XY fit");
        }
    }

    void testCascadeFrameLimitsAndMotion()
    {
        const auto input = makeCascadeFrameInput();
        const std::array<float, 4> expectedFar{3, 5, 7, 9};

        auto cameraLimited = input;
        cameraLimited.cameraFar = 9.0f;

        auto distanceLimited = input;
        distanceLimited.shadowDistance = 9.0f;

        const auto a = calculateShadowCascadeFrame(cameraLimited);
        const auto b = calculateShadowCascadeFrame(distanceLimited);

        for (std::size_t i = 0; i < expectedFar.size(); ++i)
        {
            require(
                a.cascades[i].farDistance == expectedFar[i] &&
                    b.cascades[i].farDistance == expectedFar[i],
                "Effective far must honor camera and shadow limits");
        }

        const auto original = calculateShadowCascadeFrame(input);

        auto movedInput = input;
        const glm::vec3 translation(2.0f, -3.0f, 4.0f);
        movedInput.inverseView[3] = glm::vec4(translation, 1.0f);
        movedInput.receiverBiasWorld = 0.0f;

        const auto moved = calculateShadowCascadeFrame(movedInput);

        for (std::size_t i = 0; i < moved.cascades.size(); ++i)
        {
            const auto &before = original.cascades[i];
            const auto &after = moved.cascades[i];

            require(
                glm::length(
                    after.xy.centerWorld -
                    before.xy.centerWorld -
                    translation) < 1e-4f,
                "All cascade centers must follow camera translation");

            require(
                after.xy.radius == before.xy.radius &&
                    after.xy.halfExtent == before.xy.halfExtent &&
                    after.xy.texelWorldSize == before.xy.texelWorldSize,
                "Translation must preserve all cascade sizes");

            require(
                after.receiverBias == 0.0f,
                "Zero world bias must remain zero");
        }
    }

    void testInvalidCascadeFrame()
    {
        const auto expectInvalid =
            [](const ShadowCascadeInput &input)
        {
            bool rejected = false;

            try
            {
                (void)calculateShadowCascadeFrame(input);
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }

            require(
                rejected,
                "Invalid cascade frame input must be rejected");
        };

        const float nan =
            std::numeric_limits<float>::quiet_NaN();
        const float inf =
            std::numeric_limits<float>::infinity();

        auto input = makeCascadeFrameInput();
        input.cameraFar = nan;
        expectInvalid(input);

        input = makeCascadeFrameInput();
        input.shadowDistance = nan;
        expectInvalid(input);

        input = makeCascadeFrameInput();
        input.shadowDistance = input.cameraNear;
        expectInvalid(input);

        input = makeCascadeFrameInput();
        input.cameraFar = input.cameraNear;
        expectInvalid(input);

        input = makeCascadeFrameInput();
        input.receiverBiasWorld = -0.01f;
        expectInvalid(input);

        input = makeCascadeFrameInput();
        input.receiverBiasWorld = inf;
        expectInvalid(input);

        input = makeCascadeFrameInput();
        input.resolution = 0;
        expectInvalid(input);
    }

    void testCascadeBlendFrame()
    {
        auto input = makeCascadeFrameInput();
        input.blendFraction = 0.25f;
        const auto frame = calculateShadowCascadeFrame(input);
        const std::array<float, 4> fitNear{1, 4, 8, 12};
        const std::array<float, 4> starts{4, 8, 12, 17};
        const std::array<float, 4> nearDistances{1, 5, 9, 13};
        const std::array<float, 4> farDistances{5, 9, 13, 17};
        const std::array<float, 4> radiusSquared{54, 168.25f, 344.25f, 584.25f};
        for (std::size_t i = 0; i < frame.cascades.size(); ++i)
        {
            const auto &cascade = frame.cascades[i];
            require(cascade.nearDistance == nearDistances[i] &&
                        cascade.farDistance == farDistances[i] &&
                        cascade.fitNearDistance == fitNear[i] &&
                        cascade.blendStartDistance == starts[i],
                    "Nominal and overlapping intervals must match the known frame");
            require(std::abs(cascade.xy.radius - std::sqrt(radiusSquared[i])) < 1e-4f,
                    "Cascade fit must use the expanded interval");
        }

        input.lightDirection = {-1.0f, -2.0f, -3.0f};
        input.sceneMinimum = glm::vec3(-100.0f);
        input.sceneMaximum = glm::vec3(100.0f);
        Camera camera;
        camera.lookAt({3, -2, 4}, {0, 1, 0});
        input.inverseView = glm::inverse(camera.view());
        const auto moved = calculateShadowCascadeFrame(input);
        for (std::size_t i = 0; i + 1 < moved.cascades.size(); ++i)
        {
            const auto &current = moved.cascades[i];
            const auto &next = moved.cascades[i + 1];
            const auto corners = calculateFrustumSliceCorners(
                input.inverseView, input.fovDegrees, input.aspectRatio,
                current.blendStartDistance, current.farDistance);
            for (const auto &corner : corners)
            {
                for (const auto *cascade : {&current, &next})
                {
                    const auto ndc = shadowNdc(cascade->matrices.viewProjection, corner);
                    require(std::abs(ndc.x) <= 1.0001f && std::abs(ndc.y) <= 1.0001f &&
                                ndc.z >= -1e-4f && ndc.z <= 1.0001f,
                            "Both cascades must cover the blend-band corners");
                }
            }
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

        testKnownShadowXYAndSnapping();
        testShadowXYCoverage();
        testInvalidShadowXY();

        testKnownShadowMatrices();
        testShadowMatrixCoverage();
        testInvalidShadowMatrices();

        testKnownCascadeFrame();
        testCascadeFrameLimitsAndMotion();
        testInvalidCascadeFrame();

        testCascadeBlendFrame();

        std::cout << "Shadow cascade math tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}