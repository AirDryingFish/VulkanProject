#include "render/ShadowCascades.hpp"

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
}

int main()
{
    try
    {
        testKnownSplits();
        testInvalidInputs();
        testRepresentation();

        std::cout << "Shadow cascade math tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}