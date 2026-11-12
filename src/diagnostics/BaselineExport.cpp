#include "diagnostics/BaselineCapture.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>

bool BaselineCapture::saveCsv(const char* path) const
{
    if (!complete || samples.empty() || path == nullptr || path[0] == '\0')
    {
        return false;
    }

    std::ofstream file(path);
    if (!file)
    {
        return false;
    }

    file.imbue(std::locale::classic());
    file << std::setprecision(9);

    file << "submission_id,frame_slot";

    for (const char* column : gpuPassCsvColumns)
    {
        file << ',' << column;
    }

    file << ",cpu_submission_id,"
            "cpu_wait_ms,cpu_acquire_ms,cpu_update_ms,"
            "cpu_record_ms,cpu_submit_ms,cpu_present_ms,"
            "scene_draws,scene_triangles,"
            "shadow_draws,shadow_triangles\n";

    for (const BaselineSample& sample : samples)
    {
        file << sample.submissionId << ',' << sample.frameSlot;

        for (std::uint32_t i = 0; i < gpuPassCount; ++i)
        {
            // 即使没执行，也必须保留这一列。
            file << ',';

            if ((sample.gpuPassMask & (std::uint32_t{1} << i)) != 0)
            {
                file << sample.milliseconds[i];
            }
        }

        file << ',' << sample.cpu.submissionId;

        for (double milliseconds : sample.cpu.milliseconds)
        {
            file << ',' << milliseconds;
        }

        const DrawStatistics& draws = sample.cpu.draws;

        file << ',' << draws.sceneDraws << ',' << draws.sceneTriangles << ',' << draws.shadowDraws << ','
             << draws.shadowTriangles << '\n';
    }

    file.flush();
    return file.good();
}

bool BaselineCapture::saveMetadata(const char* path) const
{
    if (!complete || samples.empty() || path == nullptr || path[0] == '\0')
    {
        return false;
    }

    std::ofstream file(path);
    if (!file)
    {
        return false;
    }

    file.imbue(std::locale::classic());
    file << std::boolalpha << std::setprecision(std::numeric_limits<float>::max_digits10);

    // 普通标量：key=value
    const auto write = [&](const char* key, const auto& value) { file << key << '=' << value << '\n'; };

    // 三维向量：key=x y z
    const auto writeVec3 = [&](const char* key, const glm::vec3& value)
    { file << key << '=' << value.x << ' ' << value.y << ' ' << value.z << '\n'; };

    // 四维向量：key=x y z w
    const auto writeVec4 = [&](const char* key, const glm::vec4& value)
    { file << key << '=' << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w << '\n'; };

    // 采集身份和样本数量。
    write("schema_version", 3);
    write("warmup_first_submission_id", firstSubmissionId);
    write("first_measured_submission_id", samples.front().submissionId);
    write("last_measured_submission_id", samples.back().submissionId);
    write("warmup_submissions", warmupSubmissions);
    write("target_samples", targetSamples);
    write("actual_samples", samples.size());
    write("dropped_samples", droppedSamples);

    // 场景与实际渲染目标配置。
    write("scene_id", std::quoted(config.sceneId));
    write("width", config.extent.width);
    write("height", config.extent.height);
    write("scene_samples", static_cast<std::uint32_t>(config.sceneSamples));

    write("render_path", config.renderPath == RenderPath::Deferred ? "Deferred" : "Forward");
    write("light_culling_mode", config.lightCullingMode == LightCullingMode::Tiled ? "Tiled" : "None");

    write("light_culling_mode_value", static_cast<std::uint32_t>(config.lightCullingMode));

    write("gpu_pass_mask", expectedGpuPassMask(config));

    write("gpu_frame_scope", "Shadow through PostAndUi, including barriers; "
                             "excludes CPU waits and present");
    write("gbuffer_geometry", config.gBufferGeometry);
    write("gbuffer_samples", config.gBufferGeometry ? 1 : 0);
    write("gbuffer_view", static_cast<std::int32_t>(config.gBufferView));

    // 阴影模式及输入参数。
    const char* modeName = "Unknown";
    if (config.shadowMode == ShadowMode::Single)
    {
        modeName = "Single";
    }
    else if (config.shadowMode == ShadowMode::Cascaded)
    {
        modeName = "Cascaded";
    }

    write("shadow_mode", modeName);
    write("shadow_mode_value", static_cast<std::uint32_t>(config.shadowMode));
    write("shadow_enabled", config.shadowEnabled);
    write("shadow_preview", config.shadowPreview);
    write("shadow_preview_layer", config.shadowPreviewLayer);
    write("shadow_pcf_enabled", config.shadowPcfEnabled);
    write("shadow_blend_fraction", config.shadowBlendFraction);
    write("shadow_constant_bias", config.shadowConstantBias);
    write("shadow_slope_bias", config.shadowSlopeBias);
    write("single_receiver_bias", config.shadowReceiverBiasNormalized);
    write("csm_receiver_bias_world", config.shadowReceiverBiasWorld);
    write("shadow_distance", config.shadowDistance);
    write("shadow_split_lambda", config.shadowSplitLambda);
    write("shadow_depth_margin", config.shadowDepthMargin);
    write("shadow_csm_light_near", config.shadowCsmLightNear);
    writeVec3("single_center", config.shadowSingleCenter);
    write("single_half_extent", config.shadowSingleHalfExtent);

    // 本次 UBO 使用的实际结果。
    write("active_cascades", config.shadowFrame.activeCascadeCount);
    write("camera_near", config.shadowFrame.cameraNearDistance);
    write("receiver_far", config.shadowFrame.receiverFarDistance);
    write("camera_fov_degrees", config.shadowFrame.cameraFovDegrees);
    writeVec4("cascade_receiver_bias", config.shadowFrame.receiverBiasNormalized);

    // 实际分配的阴影图形状。
    write("shadow_width", config.shadowTarget.extent.width);
    write("shadow_height", config.shadowTarget.extent.height);
    write("allocated_shadow_layers", config.shadowTarget.arrayLayers);
    write("shadow_format_value", static_cast<std::int32_t>(config.shadowTarget.format));

    const char* formatName = "Unknown";
    if (config.shadowTarget.format == VK_FORMAT_D32_SFLOAT)
    {
        formatName = "D32_SFLOAT";
    }
    else if (config.shadowTarget.format == VK_FORMAT_D16_UNORM)
    {
        formatName = "D16_UNORM";
    }
    write("shadow_format", formatName);

    // 显示设置也会影响对照条件。
    write("ui_drawn", config.uiDrawn);
    write("bypass_tone_mapping", config.bypassToneMapping);
    write("exposure_ev", config.exposureEv);
    write("tone_mapper_value", static_cast<std::uint32_t>(config.toneMapper));
    write("debug_view_value", static_cast<std::int32_t>(config.debugView));
    write("debug_depth_near", config.debugDepthNear);
    write("debug_depth_far", config.debugDepthFar);

    write("context_available", !contextText.empty());

    file << "\n[capture_context]\n";
    file << contextText;

    file.flush();
    if (!file.good())
    {
        return false;
    }

    file.close();
    return !file.fail();
}

bool BaselineCapture::saveBundle(const char* csvPath) const
{
    if (!complete || samples.empty() || csvPath == nullptr || csvPath[0] == '\0')
    {
        return false;
    }

    if (!saveCsv(csvPath))
    {
        return false;
    }

    const std::string metadataPath = std::string(csvPath) + ".config.txt";

    return saveMetadata(metadataPath.c_str());
}
