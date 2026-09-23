#include "TriangleApplication.hpp"

#include <cstdlib>
#include <charconv>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

int main(int argc, char** argv)
{
    try
    {
        ScenePreset preset = ScenePreset::ShadowPlayground;
        std::uint32_t frameLimit = 0;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument = argv[i];
            if (argument == "--help" || argument == "-h")
            {
                std::cout << "Usage: vulkan [--scene ID] [--frames N] [--list-scenes]\n"
                          << "Default scene: shadow-playground. --frames exits normally after N rendered frames.\n";
                return EXIT_SUCCESS;
            }
            if (argument == "--list-scenes")
            {
                for (const auto& info : scenePresets)
                {
                    std::cout << info.id << " : " << info.label << '\n';
                    if (info.unsupportedReason[0] != '\0')
                        std::cout << "  unavailable: " << info.unsupportedReason << '\n';
                }
                return EXIT_SUCCESS;
            }
            if (argument == "--scene" && i + 1 < argc)
            {
                const auto requested = findScenePreset(argv[++i]);
                if (!requested)
                    throw std::invalid_argument("Unknown scene ID: " + std::string(argv[i]) + ". Use --list-scenes.");
                preset = *requested;
            }
            else if (argument == "--frames" && i + 1 < argc)
            {
                const std::string_view value = argv[++i];
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frameLimit);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || frameLimit == 0)
                    throw std::invalid_argument("--frames requires a positive integer");
            }
            else
                throw std::invalid_argument("Unknown or incomplete option: " + std::string(argument) + ". Use --help.");
        }
        TriangleApplication app;
        app.run(preset, frameLimit);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
