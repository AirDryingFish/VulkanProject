#include "diagnostics/BaselineCapture.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint64_t firstSubmission = 100;

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void requireNear(double actual, double expected, const std::string& message)
{
    require(std::isfinite(actual) && std::abs(actual - expected) < 1e-7, message);
}

std::uint32_t slotFor(std::uint64_t id)
{
    return static_cast<std::uint32_t>(id % MAX_FRAMES_IN_FLIGHT);
}

FrameTimingConfig captureConfig()
{
    FrameTimingConfig config{};
    config.sceneId = "material-lab";
    config.extent = {1280, 720};
    config.sceneSamples = VK_SAMPLE_COUNT_4_BIT;
    config.shadowEnabled = true;
    config.uiDrawn = true;
    return config;
}

GpuTimingSnapshot gpuSample(std::uint64_t id, const FrameTimingConfig& config = captureConfig())
{
    GpuTimingSnapshot gpu{};
    gpu.supported = true;
    gpu.hasSample = true;
    gpu.submissionId = id;
    gpu.frameSlot = slotFor(id);
    gpu.config = config;
    for (std::uint32_t i = 0; i < gpuPassCount; ++i)
    {
        gpu.passes[i].state = GpuTimingState::Ready;
        gpu.passes[i].milliseconds = static_cast<double>(id) / 1024.0 + i / 8.0;
    }
    return gpu;
}

CpuFrameTimingSnapshot cpuSample(std::uint64_t id)
{
    CpuFrameTimingSnapshot cpu{};
    cpu.renderer.valid = true;
    cpu.renderer.submissionId = id;
    cpu.renderer.frameSlot = slotFor(id);
    cpu.renderer.config = captureConfig();
    cpu.renderer.fenceWaitMs = id + 0.125;
    cpu.renderer.acquireMs = id + 0.250;
    cpu.updateMs = id + 0.375;
    cpu.recordMs = id + 0.500;
    cpu.renderer.submitMs = id + 0.625;
    cpu.renderer.presentMs = id + 0.750;
    cpu.renderer.draws = {id + 1, (id + 1) * 7, id / 2, (id / 2) * 3};
    return cpu;
}

std::array<double*, baselineCpuMetricCount> cpuMetrics(CpuFrameTimingSnapshot& cpu)
{
    return {&cpu.renderer.fenceWaitMs, &cpu.renderer.acquireMs, &cpu.updateMs,
            &cpu.recordMs, &cpu.renderer.submitMs, &cpu.renderer.presentMs};
}

BaselineCapture warmedCapture()
{
    BaselineCapture capture;
    capture.start(firstSubmission, captureConfig());
    for (std::uint64_t offset = 0; offset < BaselineCapture::warmupSubmissions; ++offset)
        capture.consume(gpuSample(firstSubmission + offset));
    require(capture.running && !capture.complete, "warmup must not complete capture");
    require(capture.warmupDone == BaselineCapture::warmupSubmissions,
            "warmup must count successful submission IDs");
    require(capture.samples.empty() && capture.droppedSamples == 0,
            "warmup needs no paired CPU record and must not add or drop samples");
    return capture;
}

std::uint64_t firstMeasuredSubmission()
{
    return firstSubmission + BaselineCapture::warmupSubmissions;
}

BaselineCapture completeCapture()
{
    BaselineCapture capture;
    capture.start(firstSubmission, captureConfig());
    capture.rememberCpu(cpuSample(firstSubmission));
    const std::uint64_t lastMeasured = firstMeasuredSubmission() + BaselineCapture::targetSamples - 1;

    // Match the application: consume the old slot immediately after beginFrame,
    // then register this frame's CPU result after endFrame and the start branch.
    for (std::uint64_t current = firstSubmission + 1;
         current <= lastMeasured + MAX_FRAMES_IN_FLIGHT; ++current)
    {
        capture.consume(gpuSample(current - MAX_FRAMES_IN_FLIGHT));
        capture.rememberCpu(cpuSample(current));
    }

    require(capture.complete && !capture.running, "120 warmup + 300 matched samples must complete");
    require(capture.samples.size() == BaselineCapture::targetSamples && capture.droppedSamples == 0,
            "matched slot reuse must not lose or duplicate samples");
    require(capture.warmupDone == BaselineCapture::warmupSubmissions, "warmup progress must stay capped");
    for (std::size_t i = 0; i < capture.samples.size(); ++i)
    {
        const BaselineSample& sample = capture.samples[i];
        const std::uint64_t expectedId = firstMeasuredSubmission() + i;
        require(sample.submissionId == expectedId && sample.cpu.submissionId == expectedId,
                "CPU and GPU must refer to the same submission");
        require(sample.frameSlot == slotFor(expectedId), "sample must keep the original frame slot");
        require(sample.cpu.draws.sceneDraws == expectedId + 1 &&
                sample.cpu.draws.sceneTriangles == (expectedId + 1) * 7 &&
                sample.cpu.draws.shadowDraws == expectedId / 2 &&
                sample.cpu.draws.shadowTriangles == (expectedId / 2) * 3,
                "draw counts must come from the paired CPU submission");
        for (std::size_t metric = 0; metric < baselineCpuMetricCount; ++metric)
            requireNear(sample.cpu.milliseconds[metric], expectedId + (metric + 1) / 8.0,
                        "all six CPU metrics must keep their order and submission");
    }
    return capture;
}

void testOldAndRepeatedGpu()
{
    BaselineCapture capture;
    capture.start(firstSubmission, captureConfig());
    GpuTimingSnapshot old = gpuSample(firstSubmission - 1);
    old.config.sceneId = "previous-scene";
    capture.consume(old);
    capture.consume(old);
    require(capture.running && capture.warmupDone == 0 && capture.droppedSamples == 0,
            "pre-start GPU results must be ignored before checking config or CPU history");
    GpuTimingSnapshot noSample{};
    capture.consume(noSample);
    require(capture.warmupDone == 0, "pending GPU snapshot must not advance warmup");

    capture = warmedCapture();
    const auto id = firstMeasuredSubmission();
    capture.rememberCpu(cpuSample(id));
    capture.consume(gpuSample(id));
    capture.consume(gpuSample(id));
    capture.consume(gpuSample(id - 1));
    require(capture.samples.size() == 1 && capture.droppedSamples == 0,
            "repeated or older GPU IDs must not duplicate samples or drops");
}

void testMissingAndMismatchedCpu()
{
    const auto id = firstMeasuredSubmission();
    BaselineCapture missing = warmedCapture();
    missing.consume(gpuSample(id));
    missing.consume(gpuSample(id));
    require(missing.samples.empty() && missing.droppedSamples == 1,
            "missing CPU must drop exactly once, without filling in zero times");

    BaselineCapture mismatch = warmedCapture();
    mismatch.rememberCpu(cpuSample(id + MAX_FRAMES_IN_FLIGHT));
    mismatch.consume(gpuSample(id));
    require(mismatch.samples.empty() && mismatch.droppedSamples == 1,
            "a newer CPU record in the same slot must not match an older GPU ID");

    BaselineCapture invalid = warmedCapture();
    auto cpu = cpuSample(id);
    cpu.renderer.valid = false;
    invalid.rememberCpu(cpu);
    invalid.consume(gpuSample(id));
    require(invalid.samples.empty() && invalid.droppedSamples == 1,
            "invalid CPU snapshots must not enter the pairing cache");

    BaselineCapture bounds = warmedCapture();
    cpu = cpuSample(id);
    cpu.renderer.frameSlot = static_cast<std::uint32_t>(MAX_FRAMES_IN_FLIGHT);
    bounds.rememberCpu(cpu);
    auto gpu = gpuSample(id);
    gpu.frameSlot = cpu.renderer.frameSlot;
    bounds.consume(gpu);
    require(bounds.samples.empty() && bounds.droppedSamples == 1,
            "out-of-range CPU and GPU slots must be handled without indexing the cache");
}

void testInvalidTimings()
{
    const auto id = firstMeasuredSubmission();
    const std::array<double, 4> invalidValues = {
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), -0.001};
    for (double invalidValue : invalidValues)
    {
        for (std::size_t metric = 0; metric < baselineCpuMetricCount; ++metric)
        {
            BaselineCapture capture = warmedCapture();
            auto cpu = cpuSample(id);
            *cpuMetrics(cpu)[metric] = invalidValue;
            capture.rememberCpu(cpu);
            capture.consume(gpuSample(id));
            require(capture.samples.empty() && capture.droppedSamples == 1,
                    "every CPU metric must reject nonfinite and negative times");
        }
        for (std::uint32_t pass = 0; pass < gpuPassCount; ++pass)
        {
            BaselineCapture capture = warmedCapture();
            capture.rememberCpu(cpuSample(id));
            auto gpu = gpuSample(id);
            gpu.passes[pass].milliseconds = invalidValue;
            capture.consume(gpu);
            require(capture.samples.empty() && capture.droppedSamples == 1,
                    "every GPU pass must reject nonfinite and negative times");
        }
    }

    const std::array<GpuTimingState, 4> invalidStates = {
        GpuTimingState::Unavailable, GpuTimingState::Pending,
        GpuTimingState::NotExecuted, GpuTimingState::NotReady};
    for (GpuTimingState state : invalidStates)
    {
        for (std::uint32_t pass = 0; pass < gpuPassCount; ++pass)
        {
            BaselineCapture capture = warmedCapture();
            capture.rememberCpu(cpuSample(id));
            auto gpu = gpuSample(id);
            gpu.passes[pass].state = state;
            capture.consume(gpu);
            require(capture.samples.empty() && capture.droppedSamples == 1,
                    "only Ready GPU pass states may enter a baseline");
        }
    }
}

void testZeroTimings()
{
    BaselineCapture capture = warmedCapture();
    const auto id = firstMeasuredSubmission();
    auto cpu = cpuSample(id);
    for (double* metric : cpuMetrics(cpu))
        *metric = 0.0;
    auto gpu = gpuSample(id);
    for (auto& pass : gpu.passes)
        pass.milliseconds = 0.0;
    capture.rememberCpu(cpu);
    capture.consume(gpu);
    require(capture.samples.size() == 1 && capture.droppedSamples == 0,
            "valid zero times must be retained instead of treated as missing data");
}

void testConfigChangeCancels()
{
    using ChangeConfig = void (*)(FrameTimingConfig&);
    const std::array<ChangeConfig, 5> changes = {{
        [](FrameTimingConfig& config) { config.sceneId = "shadow-playground"; },
        [](FrameTimingConfig& config) { ++config.extent.width; },
        [](FrameTimingConfig& config) { config.exposureEv = 1.0f; },
        [](FrameTimingConfig& config) { config.toneMapper = ToneMapper::AcesFittedNarkowicz; },
        [](FrameTimingConfig& config) { config.shadowEnabled = !config.shadowEnabled; }
    }};
    const auto id = firstMeasuredSubmission();
    for (ChangeConfig change : changes)
    {
        BaselineCapture capture = warmedCapture();
        capture.rememberCpu(cpuSample(id));
        auto gpu = gpuSample(id);
        change(gpu.config);
        capture.consume(gpu);
        require(!capture.running && !capture.complete && capture.samples.empty(),
                "changing a captured render config must cancel, rather than mix measurements");
        require(capture.droppedSamples == 0 && !capture.message.empty(),
                "config cancellation needs a reason and is distinct from a dropped sample");
    }
}

void testCancelAndRestart()
{
    BaselineCapture capture = warmedCapture();
    const auto id = firstMeasuredSubmission();
    capture.rememberCpu(cpuSample(id));
    capture.consume(gpuSample(id));
    capture.rememberCpu(cpuSample(id + 1));
    const auto cacheBeforeCancel = capture.cpuByFrameSlot;
    capture.cancel("Window resized.");
    capture.consume(gpuSample(id + 1));
    capture.rememberCpu(cpuSample(id + 2));
    require(!capture.running && !capture.complete && capture.samples.size() == 1,
            "cancel must stop both CPU registration and GPU sample collection");
    require(capture.message == "Window resized.", "cancel must preserve its reason");
    for (std::size_t slot = 0; slot < cacheBeforeCancel.size(); ++slot)
        require(capture.cpuByFrameSlot[slot].submissionId == cacheBeforeCancel[slot].submissionId,
                "cancelled capture must stop registering new CPU submissions");

    capture.exportMessage = "Previous export result.";
    capture.start(1000, captureConfig());
    require(capture.running && !capture.complete && capture.samples.empty() &&
            capture.warmupDone == 0 && capture.droppedSamples == 0 &&
            capture.firstSubmissionId == 1000 && capture.lastSeenSubmissionId == 0 &&
            capture.message.empty() && capture.exportMessage.empty(),
            "restart must reset previous progress, results and status");
    for (const auto& cached : capture.cpuByFrameSlot)
        require(!cached.valid && cached.submissionId == 0, "restart must clear every CPU cache slot");
    capture.consume(gpuSample(id + 1));
    require(capture.running && capture.warmupDone == 0 && capture.droppedSamples == 0,
            "a late GPU result from the cancelled capture must not affect the restarted capture");
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned int attempt = 0; attempt < 100; ++attempt)
        {
            path_ = std::filesystem::temp_directory_path() /
                ("vulkan-baseline-tests-" + std::to_string(token) + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
                return;
            if (error)
                throw std::runtime_error("Cannot create test directory: " + error.message());
        }
        throw std::runtime_error("Cannot find an unused temporary test directory");
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::vector<std::vector<std::string>> readCsv(const std::filesystem::path& path)
{
    std::ifstream file(path);
    require(file.is_open(), "exported CSV must be readable");
    std::vector<std::vector<std::string>> rows;
    std::string line;
    while (std::getline(file, line))
    {
        std::vector<std::string> row;
        std::istringstream fields(line);
        std::string field;
        while (std::getline(fields, field, ','))
            row.push_back(field);
        rows.push_back(std::move(row));
    }
    require(!file.bad(), "reading CSV must finish without an I/O failure");
    return rows;
}

void testCsv(const BaselineCapture& capture)
{
    TemporaryDirectory temporary;
    const auto csvPath = temporary.path() / "baseline.csv";
    const std::string csvName = csvPath.string();
    require(capture.saveCsv(csvName.c_str()), "complete capture must export a CSV");
    const auto rows = readCsv(csvPath);
    const std::array<const char*, 16> expectedHeader = {{
        "submission_id", "frame_slot", "shadow_ms", "scene_ms", "post_ui_ms", "cpu_submission_id",
        "cpu_wait_ms", "cpu_acquire_ms", "cpu_update_ms", "cpu_record_ms", "cpu_submit_ms", "cpu_present_ms",
        "scene_draws", "scene_triangles", "shadow_draws", "shadow_triangles"
    }};
    require(rows.size() == BaselineCapture::targetSamples + 1, "CSV must have a header and 300 data rows");
    for (const auto& row : rows)
        require(row.size() == expectedHeader.size(), "every CSV row must have exactly 16 columns");
    for (std::size_t column = 0; column < expectedHeader.size(); ++column)
        require(rows.front()[column] == expectedHeader[column], "CSV column names and order must be stable");

    for (std::size_t i = 1; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        const std::uint64_t id = firstMeasuredSubmission() + i - 1;
        require(std::stoull(row[0]) == id && row[0] == row[5],
                "CSV must export matching CPU and GPU submission IDs for every row");
        require(std::stoul(row[1]) == slotFor(id), "CSV frame slot must match the measured submission");
        for (std::size_t pass = 0; pass < gpuPassCount; ++pass)
            requireNear(std::stod(row[2 + pass]), id / 1024.0 + pass / 8.0,
                        "CSV GPU times must match their submission and pass");
        for (std::size_t metric = 0; metric < baselineCpuMetricCount; ++metric)
            requireNear(std::stod(row[6 + metric]), id + (metric + 1) / 8.0,
                        "CSV CPU metric order must match Wait/Acquire/Update/Record/Submit/Present");
        require(std::stoull(row[12]) == id + 1 && std::stoull(row[13]) == (id + 1) * 7 &&
                std::stoull(row[14]) == id / 2 && std::stoull(row[15]) == (id / 2) * 3,
                "CSV draw columns must match their paired CPU submission");
    }

    const auto invalidPath = (temporary.path() / "missing-parent" / "baseline.csv").string();
    require(!capture.saveCsv(invalidPath.c_str()), "export to a nonexistent parent must report failure");
    require(capture.complete && capture.samples.size() == BaselineCapture::targetSamples &&
            capture.samples.front().submissionId == firstMeasuredSubmission() &&
            capture.samples.back().submissionId == firstMeasuredSubmission() + BaselineCapture::targetSamples - 1,
            "failed export must preserve the completed capture for retry");
    const auto retryPath = (temporary.path() / "retry.csv").string();
    require(capture.saveCsv(retryPath.c_str()), "a failed export must remain retryable");

    BaselineCapture incomplete = warmedCapture();
    const auto prematurePath = temporary.path() / "premature.csv";
    const auto prematureName = prematurePath.string();
    require(!incomplete.saveCsv(prematureName.c_str()) && !std::filesystem::exists(prematurePath),
            "incomplete capture must not create an export file");
}
}

int main()
{
    try
    {
        const BaselineCapture completed = completeCapture();
        testOldAndRepeatedGpu();
        testMissingAndMismatchedCpu();
        testInvalidTimings();
        testZeroTimings();
        testConfigChangeCancels();
        testCancelAndRestart();
        testCsv(completed);
        std::cout << "Baseline capture tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Baseline capture tests failed: " << error.what() << '\n';
        return 1;
    }
}
