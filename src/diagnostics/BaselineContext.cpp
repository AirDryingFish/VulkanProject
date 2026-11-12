#include "diagnostics/BaselineContext.hpp"
#include "core/AppConfig.hpp"
#include "render/ShaderTypes.hpp"
#include "scene/Camera.hpp"
#include "scene/Scene.hpp"
#include "vulkan/context/VulkanContext.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

std::string buildBaselineContext(const BaselineContextView& view)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << std::setprecision(std::numeric_limits<float>::max_digits10);

    const auto write = [&](const std::string& key, const auto& value) { out << key << '=' << value << '\n'; };

    const auto writeVec3 = [&](const std::string& key, const glm::vec3& value)
    { out << key << '=' << value.x << ' ' << value.y << ' ' << value.z << '\n'; };

    const auto writeVec4 = [&](const std::string& key, const glm::vec4& value)
    { out << key << '=' << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w << '\n'; };

    const auto writeMatrix = [&](const std::string& key, const glm::mat4& matrix)
    {
        out << key << '=';

        // GLM 按列保存矩阵。
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                out << matrix[column][row] << ' ';
            }
        }

        out << '\n';
    };

    // 构建和设备环境。
    write("project_version", VULKAN_PROJECT_VERSION);
    write("build_type", VULKAN_PROJECT_BUILD_TYPE);
    write("validation_requested", enableValidationLayers);
    write("matrix_layout", "column-major");
    write("environment.hdr_configured_path", std::quoted(SKYBOX_HDR_PATH));

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(view.context.physicalDevice(), &properties);

    write("gpu_name", std::quoted(properties.deviceName));
    write("gpu_vendor_id", properties.vendorID);
    write("gpu_device_id", properties.deviceID);
    write("driver_version_raw", properties.driverVersion);

    const auto& caps = view.context.capabilities();

    write("api_header_raw", caps.headerVersion);
    write("api_loader_raw", caps.loaderVersion);
    write("api_instance_raw", caps.instanceVersion);
    write("api_device_reported_raw", caps.physicalDeviceVersion);
    write("api_device_effective_raw", caps.deviceApiVersion);
    write("sync2_supported", caps.synchronization2Supported);
    write("sync2_enabled", caps.synchronization2Enabled);
    write("dynamic_rendering_supported", caps.dynamicRenderingSupported);
    write("dynamic_rendering_enabled", caps.dynamicRenderingEnabled);

    // 相机状态。
    writeVec3("camera.position", view.camera.position);
    writeVec3("camera.front", view.camera.front);
    writeVec3("camera.up", view.camera.up);
    write("camera.yaw", view.camera.yaw);
    write("camera.pitch", view.camera.pitch);
    write("camera.near", view.camera.nearPlane);
    write("camera.far", view.camera.farPlane);
    write("camera.fov_degrees", view.camera.fieldOfViewDegrees);

    const auto extent = view.extent;
    const float aspect = static_cast<float>(extent.width) / extent.height;

    writeMatrix("camera.view", view.camera.view());
    writeMatrix("camera.projection", view.camera.projection(aspect));

    // 灯光编辑输入。
    // UBO 更新阶段仍会执行方向归一化、兜底和范围约束。
    write("directional.enabled", view.scene.directionalLight.enabled);
    writeVec3("directional.direction_input", view.scene.directionalLight.direction);
    writeVec3("directional.color_input", view.scene.directionalLight.color);
    write("directional.intensity_input", view.scene.directionalLight.intensity);

    writeVec3("ambient.color", view.scene.ambientLightColor);
    write("ambient.intensity", view.scene.ambientLightIntensity);
    write("ibl.intensity", view.scene.iblIntensity);

    if (view.activeScenePreset == ScenePreset::ManyLights)
    {
        write("many_lights.generator", "lcg32-v1");
        write("many_lights.seed", view.manyLightsSettings.seed);
        write("many_lights.generated_count", view.manyLightsSettings.count);
        write("many_lights.generated_range", view.manyLightsSettings.range);
    }

    write("ui.point_light_markers", view.showPointLightMarkers);

    write("point_lights.listed_count", view.scene.pointLights.size());
    write("point_lights.uploaded_count", std::count_if(view.scene.pointLights.begin(), view.scene.pointLights.end(),
                                                       [](const PointLight& light) { return light.enabled; }));

    write("point_lights.capacity", MAX_POINT_LIGHTS);

    for (std::size_t i = 0; i < view.scene.pointLights.size(); ++i)
    {
        const auto& light = view.scene.pointLights[i];
        const std::string prefix = "point_light." + std::to_string(i) + ".";

        writeVec3(prefix + "position", light.position);
        writeVec3(prefix + "color_input", light.color);
        write(prefix + "intensity_input", light.intensity);
        write(prefix + "range_input", light.range);
        write(prefix + "enabled", light.enabled);
    }

    // 物体变换与材质参数。
    // 这里记录的是场景对象总数，不是实际 draw call 数。
    write("objects.count", view.scene.objects.size());

    for (std::size_t i = 0; i < view.scene.objects.size(); ++i)
    {
        const auto& object = view.scene.objects[i];
        const std::string prefix = "object." + std::to_string(i) + ".";

        write(prefix + "name", std::quoted(object.name));
        write(prefix + "source", static_cast<std::uint32_t>(object.source));
        write(prefix + "source_path", std::quoted(object.sourcePath));
        writeMatrix(prefix + "model", object.modelMatrix());
        write(prefix + "auto_rotate", object.autoRotate);

        if (!object.material)
        {
            continue;
        }

        const auto& material = *object.material;

        write(prefix + "material", std::quoted(material.name));
        write(prefix + "alpha_mode", static_cast<std::uint32_t>(material.alphaMode));
        write(prefix + "alpha_cutoff", material.alphaCutoff);
        write(prefix + "double_sided", material.doubleSided);
        writeVec4(prefix + "base_color", material.baseColorFactor);
        write(prefix + "metallic", material.metallicFactor);
        write(prefix + "roughness", material.roughnessFactor);
        write(prefix + "ao_strength", material.occlusionStrength);
        write(prefix + "normal_scale", material.normalScale);
        writeVec3(prefix + "emissive", material.emissiveFactor);
    }

    return out.str();
}
