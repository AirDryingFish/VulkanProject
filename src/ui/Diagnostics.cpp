#include "app/Application.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include <imgui.h>

namespace
{
constexpr std::array<const char*, gpuPassCount> gpuPassNames = {
    "Shadow", "Scene", "Post + UI"};

constexpr std::array<const char*, baselineCpuMetricCount> cpuMetricNames = {
    "Fence wait", "Acquire", "Update", "Record", "Submit", "Present"};

constexpr ImGuiTableFlags metricTableFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
    ImGuiTableFlags_SizingStretchProp;

bool beginMetricTable(const char* id, const char* valueHeader = nullptr)
{
    if (!ImGui::BeginTable(id, 2, metricTableFlags))
    {
        return false;
    }

    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(valueHeader ? valueHeader : "Value",
                            ImGuiTableColumnFlags_WidthStretch, 1.0f);
    if (valueHeader)
    {
        ImGui::TableHeadersRow();
    }
    return true;
}

// Leave the cursor in the value column so callers can choose its format.
void metricRow(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextWrapped("%s", label);
    ImGui::TableSetColumnIndex(1);
}

const char* debugViewName(DebugView view)
{
    switch (view)
    {
    case DebugView::Lit: return "Lit";
    case DebugView::Albedo: return "Albedo";
    case DebugView::WorldNormal: return "World normal";
    case DebugView::Metallic: return "Metallic";
    case DebugView::Roughness: return "Roughness";
    case DebugView::LinearDepth: return "Linear depth";
    case DebugView::HdrLuminance: return "HDR luminance";
    case DebugView::ShadowProjection: return "Shadow projection";
    }
    return "Unknown";
}

void drawTimingConfig(const char* id, const FrameTimingConfig& config)
{
    if (!beginMetricTable(id))
    {
        return;
    }

    metricRow("Scene");
    ImGui::TextWrapped("%s", config.sceneId.c_str());
    metricRow("Extent");
    ImGui::Text("%u x %u", config.extent.width, config.extent.height);
    metricRow("Scene MSAA");
    ImGui::Text("%ux", static_cast<unsigned int>(config.sceneSamples));
    metricRow("Shadows");
    ImGui::TextUnformatted(config.shadowEnabled ? "Draw enabled" : "Clear only");
    metricRow("Depth preview");
    ImGui::TextUnformatted(config.shadowPreview ? "On" : "Off");
    metricRow("UI rendered");
    ImGui::TextUnformatted(config.uiDrawn ? "Yes" : "No");
    metricRow("Exposure EV");
    ImGui::Text("%.2f", config.exposureEv);
    metricRow("Tone mapper");
    ImGui::TextWrapped("%s", config.toneMapper == ToneMapper::Reinhard
        ? "Reinhard" : "ACES fitted (Narkowicz)");
    metricRow("Tone mapping bypass");
    ImGui::TextUnformatted(config.bypassToneMapping ? "On" : "Off");
    metricRow("Debug view");
    ImGui::TextUnformatted(debugViewName(config.debugView));
    metricRow("Depth display range");
    ImGui::Text("%.3f .. %.3f", config.debugDepthNear, config.debugDepthFar);
    ImGui::EndTable();
}

template<std::size_t Count, typename GetValue>
void drawBaselineStatistics(const char* id,
                            const std::array<const char*, Count>& names,
                            const std::vector<BaselineSample>& samples,
                            GetValue getValue)
{
    const bool compact = ImGui::GetContentRegionAvail().x < 360.0f;
    if (!ImGui::BeginTable(id, compact ? 2 : 4, metricTableFlags))
    {
        return;
    }

    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn(compact ? "Mean / range" : "Mean ms",
                            ImGuiTableColumnFlags_WidthStretch, 1.0f);
    if (!compact)
    {
        ImGui::TableSetupColumn("Min ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Max ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    }
    ImGui::TableHeadersRow();

    for (std::size_t i = 0; i < Count; ++i)
    {
        double sum = 0.0;
        double minimum = getValue(samples.front(), i);
        double maximum = minimum;
        for (const BaselineSample& sample : samples)
        {
            const double value = getValue(sample, i);
            sum += value;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }

        metricRow(names[i]);
        ImGui::Text("%.3f", sum / static_cast<double>(samples.size()));
        if (compact)
        {
            ImGui::TextWrapped("%.3f - %.3f", minimum, maximum);
        }
        else
        {
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", minimum);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", maximum);
        }
    }
    ImGui::EndTable();
}
} // namespace

void Application::drawDiagnostics()
{
    if (!ImGui::BeginTabBar("DiagnosticsSections", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        return;
    }

    if (ImGui::BeginTabItem("Live"))
    {
        if (ImGui::BeginChild("DiagnosticsLive", ImVec2(0.0f, 0.0f)))
        {
            drawDiagnosticsLive();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Baseline"))
    {
        if (ImGui::BeginChild("DiagnosticsBaseline", ImVec2(0.0f, 0.0f)))
        {
            drawDiagnosticsBaseline();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Resources"))
    {
        if (ImGui::BeginChild("DiagnosticsResources", ImVec2(0.0f, 0.0f)))
        {
            drawDiagnosticsResources();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void Application::drawDiagnosticsLive()
{
    const CpuFrameTimingSnapshot& cpu = diagnostics.latestCpuTiming;
    const GpuTimingSnapshot& gpu = renderer.gpuTimingSnapshot();

    ImGui::SeparatorText("Current renderer");
    if (beginMetricTable("CurrentRenderer"))
    {
        const float framerate = ImGui::GetIO().Framerate;
        metricRow("FPS");
        ImGui::Text("%.1f", framerate);
        metricRow("Frame interval");
        if (framerate > 0.0f)
        {
            ImGui::TextWrapped("%.3f ms (FPS estimate)", 1000.0f / framerate);
        }
        else
        {
            ImGui::TextUnformatted("N/A");
        }
        metricRow("Scene");
        ImGui::TextWrapped("%s", scenePresetInfo(activeScenePreset).id);
        metricRow("Extent");
        ImGui::Text("%u x %u", swapchain.extent().width, swapchain.extent().height);
        metricRow("Scene MSAA");
        ImGui::Text("%ux", static_cast<unsigned int>(renderer.sceneSamples()));
        metricRow("Swapchain images");
        ImGui::Text("%zu", swapchain.imageCount());
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextWrapped("CPU shows the latest submission; GPU shows a completed submission. Their IDs may differ.");
    ImGui::SeparatorText("GPU timing");
    if (!gpu.supported)
    {
        ImGui::TextWrapped("Unavailable: the graphics queue has no timestamp support.");
    }
    else if (!gpu.hasSample)
    {
        ImGui::TextWrapped("Pending: waiting for a completed submission.");
    }
    else
    {
        ImGui::TextWrapped("Completed submission: %llu | Frame slot: %u",
            static_cast<unsigned long long>(gpu.submissionId), gpu.frameSlot);
        if (beginMetricTable("LiveGpuTiming", "GPU ms / state"))
        {
            for (std::size_t i = 0; i < gpuPassNames.size(); ++i)
            {
                metricRow(gpuPassNames[i]);
                const GpuPassTiming& timing = gpu.passes[i];
                switch (timing.state)
                {
                case GpuTimingState::Ready:
                    ImGui::Text("%.3f", timing.milliseconds);
                    break;
                case GpuTimingState::NotExecuted:
                    ImGui::TextWrapped("Not executed");
                    break;
                case GpuTimingState::NotReady:
                    ImGui::TextWrapped("N/A: query not ready");
                    break;
                case GpuTimingState::Unavailable:
                    ImGui::TextUnformatted("Unavailable");
                    break;
                case GpuTimingState::Pending:
                    ImGui::TextUnformatted("Pending");
                    break;
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("CPU timing");
    if (!cpu.renderer.valid)
    {
        ImGui::TextWrapped("Pending: no submitted CPU sample.");
    }
    else
    {
        ImGui::TextWrapped("Latest submission: %llu | Frame slot: %u",
            static_cast<unsigned long long>(cpu.renderer.submissionId), cpu.renderer.frameSlot);
        const std::array<double, baselineCpuMetricCount> milliseconds = {
            cpu.renderer.fenceWaitMs, cpu.renderer.acquireMs, cpu.updateMs,
            cpu.recordMs, cpu.renderer.submitMs, cpu.renderer.presentMs};
        if (beginMetricTable("LiveCpuTiming", "CPU ms"))
        {
            for (std::size_t i = 0; i < cpuMetricNames.size(); ++i)
            {
                metricRow(cpuMetricNames[i]);
                ImGui::Text("%.3f", milliseconds[i]);
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Submitted geometry");
        if (ImGui::BeginTable("LiveDrawStatistics", 3, metricTableFlags))
        {
            ImGui::TableSetupColumn("Mesh pass");
            ImGui::TableSetupColumn("Draws");
            ImGui::TableSetupColumn("Triangles");
            ImGui::TableHeadersRow();
            const DrawStatistics& draws = cpu.renderer.draws;
            metricRow("Scene");
            ImGui::Text("%llu", static_cast<unsigned long long>(draws.sceneDraws));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%llu", static_cast<unsigned long long>(draws.sceneTriangles));
            metricRow("Shadow");
            ImGui::Text("%llu", static_cast<unsigned long long>(draws.shadowDraws));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%llu", static_cast<unsigned long long>(draws.shadowTriangles));
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Sample configuration"))
    {
        if (gpu.hasSample && ImGui::TreeNode("Completed GPU submission"))
        {
            drawTimingConfig("LiveGpuConfig", gpu.config);
            ImGui::TreePop();
        }
        if (cpu.renderer.valid && ImGui::TreeNode("Latest CPU submission"))
        {
            drawTimingConfig("LiveCpuConfig", cpu.renderer.config);
            ImGui::TreePop();
        }
    }
}

void Application::drawDiagnosticsBaseline()
{
    const bool hasAnimatedObjects = std::any_of(scene.objects.begin(), scene.objects.end(),
        [](const SceneObject& object) { return object.autoRotate; });
    const bool canStart = renderer.gpuTimingSnapshot().supported &&
        display.debugView == DebugView::Lit && !pendingScenePreset.has_value() &&
        !hasAnimatedObjects && !diagnostics.baseline.running && !diagnostics.startRequested;

    ImGui::SeparatorText("Capture");
    ImGui::BeginDisabled(!canStart);
    if (ImGui::Button("Start capture"))
    {
        diagnostics.startRequested = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!diagnostics.baseline.running && !diagnostics.startRequested);
    if (ImGui::Button("Cancel"))
    {
        diagnostics.startRequested = false;
        diagnostics.baseline.cancel("Capture cancelled by user.");
    }
    ImGui::EndDisabled();

    if (diagnostics.startRequested)
    {
        ImGui::TextUnformatted("Starting capture...");
    }
    else if (diagnostics.baseline.running)
    {
        ImGui::TextUnformatted(diagnostics.baseline.warmupDone < BaselineCapture::warmupSubmissions
            ? "Warming up..." : "Collecting paired CPU / GPU samples...");
    }
    else if (!diagnostics.baseline.message.empty())
    {
        ImGui::TextWrapped("%s", diagnostics.baseline.message.c_str());
    }
    else
    {
        ImGui::TextUnformatted("Ready to capture.");
    }

    if (!renderer.gpuTimingSnapshot().supported)
    {
        ImGui::TextWrapped("GPU timestamps are unavailable on this device.");
    }
    else if (display.debugView != DebugView::Lit)
    {
        ImGui::TextWrapped("Select Lit in Display to start a baseline.");
    }
    else if (hasAnimatedObjects)
    {
        ImGui::TextWrapped("Turn off object auto-rotation to start a baseline.");
    }
    else if (pendingScenePreset.has_value())
    {
        ImGui::TextWrapped("Waiting for the scene change.");
    }

    char progress[64];
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                         ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::TextUnformatted("Warmup submissions");
    std::snprintf(progress, sizeof(progress), "%u / %u", diagnostics.baseline.warmupDone,
                  BaselineCapture::warmupSubmissions);
    ImGui::ProgressBar(static_cast<float>(diagnostics.baseline.warmupDone) /
        BaselineCapture::warmupSubmissions, ImVec2(-1.0f, 0.0f), progress);
    ImGui::TextUnformatted("Valid samples");
    std::snprintf(progress, sizeof(progress), "%zu / %u", diagnostics.baseline.samples.size(),
                  BaselineCapture::targetSamples);
    ImGui::ProgressBar(static_cast<float>(diagnostics.baseline.samples.size()) /
        BaselineCapture::targetSamples, ImVec2(-1.0f, 0.0f), progress);
    ImGui::PopStyleColor();
    ImGui::Text("Dropped samples: %u", diagnostics.baseline.droppedSamples);

    ImGui::Spacing();
    ImGui::SeparatorText("Export");
    ImGui::BeginDisabled(!diagnostics.baseline.complete);
    ImGui::TextUnformatted("CSV path");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##BaselineCsvPath", diagnostics.csvPath, sizeof(diagnostics.csvPath)))
    {
        diagnostics.baseline.exportMessage.clear();
    }
    if (ImGui::Button("Export CSV"))
    {
        diagnostics.baseline.exportMessage = diagnostics.baseline.saveCsv(diagnostics.csvPath)
            ? "CSV saved." : "CSV write failed.";
    }
    ImGui::EndDisabled();
    if (!diagnostics.baseline.exportMessage.empty())
    {
        const float contentRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const float messageRight = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x +
            ImGui::CalcTextSize(diagnostics.baseline.exportMessage.c_str()).x;
        if (messageRight <= contentRight)
        {
            ImGui::SameLine();
        }
        ImGui::TextWrapped("%s", diagnostics.baseline.exportMessage.c_str());
    }
    if (!diagnostics.baseline.complete)
    {
        ImGui::TextWrapped("Export becomes available when the capture is complete.");
    }

    if (diagnostics.baseline.complete && !diagnostics.baseline.samples.empty())
    {
        ImGui::Spacing();
        ImGui::SeparatorText("GPU statistics (ms)");
        drawBaselineStatistics("BaselineGpuStatistics", gpuPassNames, diagnostics.baseline.samples,
            [](const BaselineSample& sample, std::size_t i) { return sample.milliseconds[i]; });
        ImGui::SeparatorText("CPU statistics (ms)");
        drawBaselineStatistics("BaselineCpuStatistics", cpuMetricNames, diagnostics.baseline.samples,
            [](const BaselineSample& sample, std::size_t i) { return sample.cpu.milliseconds[i]; });
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Capture details"))
    {
        ImGui::TextWrapped("Keep the camera, lighting, materials and UI fixed while capturing. Editing these is not automatically blocked.");
        if (diagnostics.baseline.firstSubmissionId != 0)
        {
            ImGui::TextWrapped("First warmup submission: %llu",
                static_cast<unsigned long long>(diagnostics.baseline.firstSubmissionId));
            ImGui::TextWrapped("Configuration saved at capture start:");
            drawTimingConfig("BaselineConfig", diagnostics.baseline.config);
        }
    }
}

void Application::drawDiagnosticsResources()
{
    ImGui::SeparatorText("Current scene");
    if (beginMetricTable("CurrentResources"))
    {
        metricRow("Scene");
        ImGui::TextWrapped("%s", scenePresetInfo(activeScenePreset).id);
        metricRow("Objects");
        ImGui::Text("%zu", scene.objects.size());
        metricRow("Point lights");
        ImGui::Text("%zu / %u", scene.pointLights.size(), MAX_POINT_LIGHTS);
        metricRow("Materials");
        ImGui::Text("%zu", materialLibrary.size());
        metricRow("Texture entries");
        ImGui::Text("%zu", textureLibrary.size());
        metricRow("Material slots");
        ImGui::Text("%u / %u", static_cast<unsigned int>(allocatedMaterialSetCount),
                    static_cast<unsigned int>(maxMaterialCount));
        metricRow("Shadow resolution");
        ImGui::Text("%u x %u", directionalShadowResolution, directionalShadowResolution);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Render target payload");
    const RenderTargetPayloadEstimate targets = renderer.renderTargetPayloadEstimate();
    if (!targets.available)
    {
        ImGui::TextUnformatted("Unavailable");
    }
    else if (!targets.complete)
    {
        ImGui::TextWrapped("N/A: unsupported image format or shape.");
    }
    else if (beginMetricTable("RenderTargetPayload", "MiB"))
    {
        const auto showBytes = [](const char* label, std::uint64_t bytes)
        {
            metricRow(label);
            ImGui::Text("%.2f", static_cast<double>(bytes) / (1024.0 * 1024.0));
        };
        showBytes("HDR color", targets.hdrColorBytes);
        showBytes("MSAA color", targets.msaaColorBytes);
        showBytes("Scene depth", targets.sceneDepthBytes);
        showBytes("Shadow depth", targets.shadowDepthBytes);
        showBytes("Total", targets.totalBytes);
        ImGui::EndTable();
    }
    ImGui::TextWrapped("Nominal pixel data across all live frame slots. Actual memory allocations may include padding.");

    ImGui::Spacing();
    ImGui::SeparatorText("VMA memory snapshot");
    if (ImGui::Button("Refresh VMA statistics") && context.allocator() != nullptr)
    {
        vmaCalculateStatistics(context.allocator(), &diagnostics.cachedVmaStatistics);
        diagnostics.vmaStatisticsSceneId = scenePresetInfo(activeScenePreset).id;
        diagnostics.hasVmaStatistics = true;
    }

    if (diagnostics.hasVmaStatistics)
    {
        ImGui::TextWrapped("Snapshot scene: %s", diagnostics.vmaStatisticsSceneId.c_str());
        if (diagnostics.vmaStatisticsSceneId != scenePresetInfo(activeScenePreset).id)
        {
            ImGui::TextWrapped("The scene has changed since this snapshot. Refresh to update it.");
        }
        if (beginMetricTable("VmaStatistics"))
        {
            const VmaStatistics& stats = diagnostics.cachedVmaStatistics.total.statistics;
            constexpr double bytesPerMiB = 1024.0 * 1024.0;
            metricRow("Allocation MiB");
            ImGui::Text("%.2f", static_cast<double>(stats.allocationBytes) / bytesPerMiB);
            metricRow("Block MiB");
            ImGui::Text("%.2f", static_cast<double>(stats.blockBytes) / bytesPerMiB);
            metricRow("Allocations");
            ImGui::Text("%u", stats.allocationCount);
            metricRow("Blocks");
            ImGui::Text("%u", stats.blockCount);
            ImGui::EndTable();
        }
        ImGui::TextWrapped("VMA allocation statistics are collected on request; they do not measure GPU memory residency.");
    }
    else
    {
        ImGui::TextWrapped("No snapshot collected. Refresh to inspect allocated memory and backing blocks.");
    }
}
