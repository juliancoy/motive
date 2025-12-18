#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <deque>
#include <chrono>
#include <iomanip>
#include <array>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan.h>
#include "engine.h"
#include "display.h"
#include "camera.h"
#include "model.h"
#include "utils.h"
#include "light.h"
#include "video.h"
#include "glyph.h"

namespace
{
    const std::filesystem::path kVideoPath = std::filesystem::path("..") / "P1090533_main8_hevc_fast.mkv";

    enum class VideoInstanceLayout
    {
        OctagonStack,
        ColumnSquares,
        XZGrid,
        Obelisk
    };

    constexpr VideoInstanceLayout kDefaultVideoLayout = VideoInstanceLayout::XZGrid;
    constexpr uint32_t kDefaultColumns = 4;
    constexpr uint32_t kDefaultRows = 4;
    constexpr uint32_t kDefaultOctagonSegments = 8;
    constexpr uint32_t kGridCountX = 128;
    constexpr uint32_t kGridCountZ = 64;
    constexpr uint32_t kObeliskOctagonRows = 8;

    uint32_t prepareInstances(Primitive& primitive, uint32_t requestedCount)
    {
        const uint32_t capacity = static_cast<uint32_t>(primitive.instanceOffsets.size());
        const uint32_t actual = std::min(requestedCount, capacity);
        primitive.instanceCount = actual;
        primitive.instanceOffsets.fill(glm::vec3(0.0f));
        primitive.instanceRotations.fill(glm::vec4(0.0f));
        return actual;
    }

    void setInstanceTransform(Primitive& primitive,
                              uint32_t index,
                              const glm::vec3& offset,
                              const glm::vec3& rotation,
                              float rotationFlag = 0.0f)
    {
        if (index >= primitive.instanceOffsets.size())
        {
            return;
        }
        primitive.instanceOffsets[index] = offset;
        primitive.instanceRotations[index] = glm::vec4(rotation, rotationFlag);
    }

    void layoutOctagonStack(Primitive& primitive,
                            uint32_t rows,
                            uint32_t quadsPerRow,
                            float ringRadius,
                            float rowSpacing)
    {
        const uint32_t actualInstances = prepareInstances(primitive, rows * quadsPerRow);
        if (actualInstances == 0 || rows == 0 || quadsPerRow == 0)
        {
            return;
        }

        const float rowCenter = (static_cast<float>(rows) - 1.0f) * 0.5f;
        uint32_t instanceIndex = 0;
        for (uint32_t row = 0; row < rows && instanceIndex < actualInstances; ++row)
        {
            const float rowHeight = (static_cast<float>(row) - rowCenter) * rowSpacing;
            for (uint32_t quad = 0; quad < quadsPerRow && instanceIndex < actualInstances; ++quad)
            {
                const float angleDegrees = (360.0f / static_cast<float>(quadsPerRow)) * static_cast<float>(quad);
                const float angleRadians = glm::radians(angleDegrees);
                const float xOffset = std::cos(angleRadians) * ringRadius;
                const float zOffset = std::sin(angleRadians) * ringRadius;
                setInstanceTransform(primitive,
                                     instanceIndex++,
                                     glm::vec3(xOffset, rowHeight, zOffset),
                                     glm::vec3(0.0f, angleRadians, 0.0f));
            }
        }

        primitive.markInstanceTransformsDirty();
    }

    void layoutColumnSquares(Primitive& primitive,
                             uint32_t columns,
                             float columnSpacing,
                             float lateralExtent,
                             float verticalSpacing)
    {
        constexpr uint32_t kInstancesPerColumn = 4;
        const uint32_t actualInstances = prepareInstances(primitive, columns * kInstancesPerColumn);
        if (actualInstances == 0 || columns == 0)
        {
            return;
        }

        const float columnCenter = (static_cast<float>(columns) - 1.0f) * 0.5f;
        const std::array<glm::vec3, kInstancesPerColumn> baseOffsets = {
            glm::vec3(-lateralExtent, -verticalSpacing * 0.5f, -lateralExtent),
            glm::vec3(lateralExtent, -verticalSpacing * 0.5f, lateralExtent),
            glm::vec3(-lateralExtent, verticalSpacing * 0.5f, lateralExtent),
            glm::vec3(lateralExtent, verticalSpacing * 0.5f, -lateralExtent)};

        uint32_t instanceIndex = 0;
        for (uint32_t column = 0; column < columns && instanceIndex < actualInstances; ++column)
        {
            const float baseX = (static_cast<float>(column) - columnCenter) * columnSpacing;
            const float yaw = glm::half_pi<float>() * static_cast<float>(column % 4u);
            const float cosYaw = std::cos(yaw);
            const float sinYaw = std::sin(yaw);

            for (const auto& offset : baseOffsets)
            {
                if (instanceIndex >= actualInstances)
                {
                    break;
                }

                glm::vec3 rotated = offset;
                const float rotatedX = offset.x * cosYaw - offset.z * sinYaw;
                const float rotatedZ = offset.x * sinYaw + offset.z * cosYaw;
                rotated.x = rotatedX + baseX;
                rotated.z = rotatedZ;
                setInstanceTransform(primitive,
                                     instanceIndex++,
                                     rotated,
                                     glm::vec3(0.0f, yaw, 0.0f));
            }
        }
        primitive.markInstanceTransformsDirty();
    }

    void layoutXZGrid(Primitive& primitive,
                      uint32_t countX,
                      uint32_t countZ,
                      float spacingX,
                      float spacingZ)
    {
        const uint32_t requestedTotal = countX * countZ;
        const uint32_t actualInstances = prepareInstances(primitive, requestedTotal);
        if (actualInstances == 0 || countX == 0 || countZ == 0)
        {
            return;
        }

        uint32_t actualCountX = std::min(countX, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(actualInstances)))));
        actualCountX = std::max(actualCountX, 1u);
        uint32_t actualCountZ = (actualInstances + actualCountX - 1u) / actualCountX;
        actualCountZ = std::min(actualCountZ, countZ);
        actualCountZ = std::max(actualCountZ, 1u);
        if (actualCountX * actualCountZ < actualInstances)
        {
            actualCountZ = (actualInstances + actualCountX - 1u) / actualCountX;
        }

        const float halfX = (static_cast<float>(actualCountX) - 1.0f) * 0.5f;
        const float halfZ = (static_cast<float>(actualCountZ) - 1.0f) * 0.5f;
        uint32_t instanceIndex = 0;
        for (uint32_t z = 0; z < actualCountZ && instanceIndex < actualInstances; ++z)
        {
            for (uint32_t x = 0; x < actualCountX && instanceIndex < actualInstances; ++x)
            {
                const float xOffset = (static_cast<float>(x) - halfX) * spacingX;
                const float zOffset = (static_cast<float>(z) - halfZ) * spacingZ;
                setInstanceTransform(primitive,
                                     instanceIndex++,
                                     glm::vec3(xOffset, 0.0f, zOffset),
                                     glm::vec3(0.0f));
            }
        }
        primitive.markInstanceTransformsDirty();
    }

    void layoutObelisk(Primitive& primitive,
                       uint32_t octagonRows,
                       float panelWidth,
                       float panelHeight)
    {
        constexpr uint32_t kSegmentsPerOctagon = 8;
        const uint32_t capInstanceCount = kSegmentsPerOctagon * 2;
        const uint32_t requestedInstances = octagonRows * kSegmentsPerOctagon + capInstanceCount;
        const uint32_t actualInstances = prepareInstances(primitive, requestedInstances);
        if (actualInstances == 0)
        {
            return;
        }

        const uint32_t rowInstanceCapacity = actualInstances > capInstanceCount ? actualInstances - capInstanceCount : actualInstances;
        const uint32_t desiredRowInstances = std::min(octagonRows * kSegmentsPerOctagon, rowInstanceCapacity);
        const uint32_t actualRows = std::max(1u, std::min(octagonRows, (desiredRowInstances + kSegmentsPerOctagon - 1u) / kSegmentsPerOctagon));
        const float width = std::max(panelWidth, 0.001f);
        const float height = std::max(panelHeight, 0.001f);
        const float angleHalf = glm::pi<float>() / static_cast<float>(kSegmentsPerOctagon);
        float ringRadius = (width * 0.5f) / std::tan(angleHalf);
        ringRadius = std::max(ringRadius, width * 0.5f);
        const float rowCenter = (static_cast<float>(actualRows) - 1.0f) * 0.5f;
        uint32_t instanceIndex = 0;
        const float angleStep = glm::two_pi<float>() / static_cast<float>(kSegmentsPerOctagon);
        const float cosStep = std::cos(angleStep);
        const float sinStep = std::sin(angleStep);

        for (uint32_t row = 0; row < actualRows && instanceIndex < desiredRowInstances; ++row)
        {
            const float rowHeight = (static_cast<float>(row) - rowCenter) * height;
            glm::vec2 radialVec(1.0f, 0.0f);
            float currentAngle = 0.0f;

            for (uint32_t segment = 0; segment < kSegmentsPerOctagon && instanceIndex < desiredRowInstances; ++segment)
            {
                glm::vec3 offset = glm::vec3(radialVec.x * ringRadius,
                                             rowHeight,
                                             radialVec.y * ringRadius);
                const float yaw = glm::half_pi<float>() - currentAngle;
                setInstanceTransform(primitive,
                                     instanceIndex++,
                                     offset,
                                     glm::vec3(0.0f, yaw, 0.0f));

                // Rotate the radial vector using the previous instance's data to keep seams watertight.
                glm::vec2 rotated(radialVec.x * cosStep - radialVec.y * sinStep,
                                  radialVec.x * sinStep + radialVec.y * cosStep);
                radialVec = rotated;
                currentAngle += angleStep;
            }
        }

        auto appendCapRing = [&](float capY, float normalYSign)
        {
            if (instanceIndex + kSegmentsPerOctagon > actualInstances)
            {
                return;
            }

            glm::vec2 radialVec(1.0f, 0.0f);
            for (uint32_t segment = 0; segment < kSegmentsPerOctagon && instanceIndex < actualInstances; ++segment)
            {
                glm::vec3 offset = glm::vec3(radialVec.x * ringRadius,
                                             capY,
                                             radialVec.y * ringRadius);
                float flag = normalYSign > 0.0f ? 1.0f : -1.0f;
                setInstanceTransform(primitive,
                                     instanceIndex++,
                                     offset,
                                     glm::vec3(0.0f),
                                     flag);

                glm::vec2 rotated(radialVec.x * cosStep - radialVec.y * sinStep,
                                  radialVec.x * sinStep + radialVec.y * cosStep);
                radialVec = rotated;
            }
        };

        const float topCapBaseY = ((static_cast<float>(actualRows - 1)) - rowCenter) * height + height * 0.5f;
        appendCapRing(topCapBaseY, 1.0f);
        const float bottomCapBaseY = (0.0f - rowCenter) * height - height * 0.5f;
        appendCapRing(bottomCapBaseY, -1.0f);

        primitive.markInstanceTransformsDirty();
    }

    struct CommandLineOptions
    {
        bool loadGltf = false;
        bool testDecode = false;
        bool forceGridLayout = false;
        bool forceObeliskLayout = false;
        bool disableCulling = false;
        std::filesystem::path gltfPath;
    };

    CommandLineOptions parseCommandLineArgs(int argc, char *argv[])
    {
        CommandLineOptions options{};
        const std::string gltfFlag = "--gltf";
        const std::string gltfEqualsPrefix = "--gltf=";

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == gltfFlag)
            {
                options.loadGltf = true;
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    options.gltfPath = argv[++i];
                }
            }
            else if (arg == "--test-decode")
            {
                options.testDecode = true;
            }
            else if (arg == "--grid")
            {
                options.forceGridLayout = true;
            }
            else if (arg == "--obelisk")
            {
                options.forceObeliskLayout = true;
            }
            else if (arg == "--disable-cullinh")
            {
                options.disableCulling = true;
            }
            else if (arg.rfind(gltfEqualsPrefix, 0) == 0)
            {
                options.loadGltf = true;
                options.gltfPath = arg.substr(gltfEqualsPrefix.size());
            }
        }

        return options;
    }

    struct VideoPlaybackState {
        video::VideoDecoder decoder;
        Primitive* videoPrimitive = nullptr;
        bool initialized = false;
        video::VideoColorInfo colorInfo;
        video::Nv12Overlay fpsOverlay;
        bool overlayValid = false;
        size_t framesSinceOverlayUpdate = 0;
        std::chrono::steady_clock::time_point overlayTimerStart;
        std::deque<video::DecodedFrame> pendingFrames;
        video::DecodedFrame stagingFrame;
        std::chrono::steady_clock::time_point playbackStartTime;
        bool playbackClockInitialized = false;
        double basePresentationPts = 0.0;
    };

    std::unique_ptr<VideoPlaybackState> videoState;

    struct DecodeBenchmarkResult
    {
        video::DecodeImplementation implementation = video::DecodeImplementation::Software;
        std::string name;
        bool success = false;
        size_t framesDecoded = 0;
        double elapsedSeconds = 0.0;
        double decodeFps = 0.0;
        double videoSecondsDecoded = 0.0;
    };

    std::string decodeImplementationLabel(video::DecodeImplementation implementation)
    {
    switch (implementation)
    {
    case video::DecodeImplementation::Vulkan:
        return "Vulkan";
        case video::DecodeImplementation::Software:
        default:
            return "Software (CPU)";
        }
    }

    void logAvailableHardwareDevices()
    {
        auto devices = video::listAvailableHardwareDevices();
        if (devices.empty())
        {
            std::cout << "[Video] FFmpeg reports no hardware device types." << std::endl;
            return;
        }
        std::cout << "[Video] FFmpeg hardware device types: ";
        for (size_t i = 0; i < devices.size(); ++i)
        {
            std::cout << devices[i];
            if (i + 1 < devices.size())
            {
                std::cout << ", ";
            }
        }
        std::cout << std::endl;
    }

    void logVulkanVideoDecodeExtensions()
    {
        const std::array<const char*, 6> videoExtensions = {
            "VK_KHR_video_queue",
            "VK_KHR_video_decode_queue",
            "VK_KHR_video_decode_h264",
            "VK_KHR_video_decode_h265",
            "VK_NV_video_decode_h264",
            "VK_NV_video_decode_h265"};

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "MotiveDecodeProbe";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Motive";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;

        VkInstance instance = VK_NULL_HANDLE;
        VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if (result != VK_SUCCESS)
        {
            std::cout << "[Video] Vulkan instance creation failed (" << result
                      << "); skipping video extension probe." << std::endl;
            return;
        }

        uint32_t deviceCount = 0;
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (result != VK_SUCCESS || deviceCount == 0)
        {
            std::cout << "[Video] No Vulkan physical devices found; skipping video extension probe." << std::endl;
            vkDestroyInstance(instance, nullptr);
            return;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        std::cout << "[Video] Vulkan video decode extension probe:" << std::endl;
        for (VkPhysicalDevice device : devices)
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(device, &props);

            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> extProps(extCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, extProps.data());

            std::vector<std::string> available;
            std::vector<std::string> missing;
            for (const char* extension : videoExtensions)
            {
                bool found = false;
                for (const auto& prop : extProps)
                {
                    if (std::strcmp(prop.extensionName, extension) == 0)
                    {
                        found = true;
                        break;
                    }
                }
                if (found)
                {
                    available.emplace_back(extension);
                }
                else
                {
                    missing.emplace_back(extension);
                }
            }

            std::cout << "  [Video] Device '" << props.deviceName << "': ";
            if (!available.empty())
            {
                std::cout << "has ";
                for (size_t i = 0; i < available.size(); ++i)
                {
                    std::cout << available[i];
                    if (i + 1 < available.size())
                    {
                        std::cout << ", ";
                    }
                }
            }
            else
            {
                std::cout << "no target extensions";
            }
            if (!missing.empty())
            {
                std::cout << "; missing ";
                for (size_t i = 0; i < missing.size(); ++i)
                {
                    std::cout << missing[i];
                    if (i + 1 < missing.size())
                    {
                        std::cout << ", ";
                    }
                }
            }
            std::cout << std::endl;
        }

        vkDestroyInstance(instance, nullptr);
    }

    DecodeBenchmarkResult runDecodeBenchmarkForMode(const std::filesystem::path& videoPath,
                                                    video::DecodeImplementation implementation)
    {
        DecodeBenchmarkResult result;
        result.implementation = implementation;
        result.name = decodeImplementationLabel(implementation);

        video::DecoderInitParams params;
        params.implementation = implementation;

        video::VideoDecoder decoder;
        if (!video::initializeVideoDecoder(videoPath, decoder, params))
        {
            return result;
        }

        result.name = decoder.implementationName;
        const double targetSeconds = 10.0;
        const double fps = decoder.fps > 0.0 ? decoder.fps : 30.0;
        const size_t targetFrames = static_cast<size_t>(fps * targetSeconds);
        video::DecodedFrame decodedFrame;
        size_t framesDecoded = 0;
        auto start = std::chrono::steady_clock::now();

        while (framesDecoded < targetFrames)
        {
            if (!video::decodeNextFrame(decoder, decodedFrame, /*copyFrameBuffer=*/true))
            {
                break;
            }
            ++framesDecoded;
        }

        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        result.framesDecoded = framesDecoded;
        result.elapsedSeconds = elapsed.count();
        result.decodeFps = elapsed.count() > 0.0 ? static_cast<double>(framesDecoded) / elapsed.count() : 0.0;
        result.videoSecondsDecoded = fps > 0.0 ? static_cast<double>(framesDecoded) / fps : 0.0;
        result.success = framesDecoded > 0;

        if (framesDecoded < targetFrames)
        {
            std::cout << "[Video] " << result.name
                      << " benchmark reached the end of file before target duration." << std::endl;
        }

        video::cleanupVideoDecoder(decoder);
        return result;
    }

    bool runDecodeBenchmark(const std::filesystem::path& videoPath)
    {
        if (!std::filesystem::exists(videoPath))
        {
            std::cerr << "[Video] Benchmark file not found: " << videoPath << std::endl;
            return false;
        }

        std::vector<DecodeBenchmarkResult> results;
        logAvailableHardwareDevices();
        logVulkanVideoDecodeExtensions();
        const std::array<video::DecodeImplementation, 2> implementations = {
            video::DecodeImplementation::Software,
            video::DecodeImplementation::Vulkan};

        for (auto implementation : implementations)
        {
            auto result = runDecodeBenchmarkForMode(videoPath, implementation);
            if (result.success)
            {
                std::cout << "[Video] " << result.name << " decoded " << result.framesDecoded
                          << " frames (" << result.videoSecondsDecoded << "s of video) in "
                          << result.elapsedSeconds << "s => " << result.decodeFps << " FPS"
                          << std::endl;
            }
            else
            {
                std::cout << "[Video] Skipping " << result.name
                          << " benchmark (not available)." << std::endl;
            }
            results.push_back(std::move(result));
        }

        const DecodeBenchmarkResult* bestResult = nullptr;
        const DecodeBenchmarkResult* cpuResult = nullptr;
        for (const auto& result : results)
        {
            if (!result.success)
            {
                continue;
            }

            if (result.implementation == video::DecodeImplementation::Software)
            {
                cpuResult = &result;
            }

            if (!bestResult || result.decodeFps > bestResult->decodeFps)
            {
                bestResult = &result;
            }
        }

        if (!bestResult)
        {
            std::cerr << "[Video] No decode benchmarks completed successfully." << std::endl;
            return false;
        }

        const auto oldFlags = std::cout.flags();
        const auto oldPrecision = std::cout.precision();
        std::cout << "[Video] Fastest backend: " << bestResult->name << " @ "
                  << std::fixed << std::setprecision(2) << bestResult->decodeFps << " FPS";
        if (cpuResult && cpuResult != bestResult && cpuResult->decodeFps > 0.0)
        {
            const double multiple = bestResult->decodeFps / cpuResult->decodeFps;
            std::cout << " (" << std::setprecision(2) << multiple << "x vs CPU)";
        }
        std::cout << std::endl;
        std::cout.flags(oldFlags);
        std::cout.precision(oldPrecision);
        return true;
    }

    void updateVideoOverlay(VideoPlaybackState& state, float fps)
    {
        if (state.decoder.width == 0 || state.decoder.height == 0)
        {
            return;
        }
        glyph::OverlayBitmap bitmap = glyph::buildLabeledOverlay(
            static_cast<uint32_t>(state.decoder.width),
            static_cast<uint32_t>(state.decoder.height),
            "VID",
            fps);
        state.fpsOverlay = video::convertOverlayToNv12(bitmap, state.colorInfo);
        state.overlayValid = state.fpsOverlay.isValid();
    }

    void updateVideoFrame()
    {
        if (!videoState || !videoState->initialized || !videoState->videoPrimitive)
        {
            return;
        }

        constexpr size_t kMaxPendingFrames = 3;
        while (videoState->pendingFrames.size() < kMaxPendingFrames &&
               video::acquireDecodedFrame(videoState->decoder, videoState->stagingFrame))
        {
            videoState->framesSinceOverlayUpdate++;
            auto overlayNow = std::chrono::steady_clock::now();
            auto overlayElapsed = overlayNow - videoState->overlayTimerStart;
            if (overlayElapsed >= std::chrono::milliseconds(250))
            {
                const double seconds = std::chrono::duration<double>(overlayElapsed).count();
                const float fpsValue = seconds > 0.0f ?
                    static_cast<float>(videoState->framesSinceOverlayUpdate / seconds) : 0.0f;
                updateVideoOverlay(*videoState, fpsValue);
                videoState->framesSinceOverlayUpdate = 0;
                videoState->overlayTimerStart = overlayNow;
            }

            videoState->pendingFrames.emplace_back(std::move(videoState->stagingFrame));
        }

        if (videoState->pendingFrames.empty())
        {
            if (videoState->decoder.finished.load() && !videoState->decoder.threadRunning.load())
            {
                std::cout << "[Video] End of video reached" << std::endl;
                videoState->initialized = false;
            }
            return;
        }

        auto currentTime = std::chrono::steady_clock::now();
        auto& nextFrame = videoState->pendingFrames.front();
        if (!videoState->playbackClockInitialized)
        {
            videoState->playbackClockInitialized = true;
            videoState->basePresentationPts = nextFrame.ptsSeconds;
            videoState->playbackStartTime = currentTime;
        }

        const double relativePts = std::max(0.0, nextFrame.ptsSeconds - videoState->basePresentationPts);
        const auto targetTime = videoState->playbackStartTime + std::chrono::duration<double>(relativePts);
        if (currentTime + std::chrono::milliseconds(1) < targetTime)
        {
            return;
        }

        video::DecodedFrame frame = std::move(nextFrame);
        videoState->pendingFrames.pop_front();

        if (videoState->overlayValid)
        {
            video::applyOverlayToDecodedFrame(frame.buffer,
                                              videoState->decoder,
                                              videoState->fpsOverlay);
        }

        if (videoState->decoder.outputFormat == PrimitiveYuvFormat::NV12)
        {
            videoState->videoPrimitive->updateTextureFromNV12(
                frame.buffer.data(),
                frame.buffer.size(),
                videoState->decoder.width,
                videoState->decoder.height);
        }
        else
        {
            const uint8_t* yPlane = frame.buffer.data();
            const uint8_t* uvPlane = yPlane + videoState->decoder.yPlaneBytes;
            videoState->videoPrimitive->updateTextureFromPlanarYuv(
                yPlane,
                videoState->decoder.yPlaneBytes,
                videoState->decoder.width,
                videoState->decoder.height,
                uvPlane,
                videoState->decoder.uvPlaneBytes,
                videoState->decoder.chromaWidth,
                videoState->decoder.chromaHeight,
                videoState->decoder.bytesPerComponent > 1,
                videoState->decoder.outputFormat);
        }

    }

    void cleanupVideoPlayback()
    {
        if (videoState)
        {
            video::cleanupVideoDecoder(videoState->decoder);
            videoState.reset();
        }
    }
}

int main(int argc, char *argv[])
{
    const auto options = parseCommandLineArgs(argc, argv);
    const bool obeliskShowcaseMode = options.forceObeliskLayout && options.disableCulling;
    if (options.testDecode)
    {
        return runDecodeBenchmark(kVideoPath) ? 0 : 1;
    }
    Engine *engine = new Engine();

    Display *display = engine->createWindow(1280, 720, "FFmpeg Video Preview", options.disableCulling);

    Light sceneLight(glm::vec3(0.0f, 0.0f, 1.0f),
                     glm::vec3(0.1f),
                     glm::vec3(0.9f));
    if (obeliskShowcaseMode)
    {
        sceneLight.setAmbient(glm::vec3(1.0f));
        sceneLight.setDiffuse(glm::vec3(0.0f));
    }
    else
    {
        sceneLight.setDiffuse(glm::vec3(1.0f, 0.95f, 0.9f));
    }
    engine->setLight(sceneLight);

    glm::vec3 defaultCameraPos(0.0f, 0.0f, 3.0f);
    glm::vec2 defaultCameraRotation(glm::radians(0.0f), 0.0f);
    auto *primaryCamera = new Camera(engine, display, defaultCameraPos, defaultCameraRotation);
    display->addCamera(primaryCamera);

    if (options.loadGltf)
    {
        std::filesystem::path gltfPath = options.gltfPath.empty() ? std::filesystem::path("the_utah_teapot.glb") : options.gltfPath;
        if (!std::filesystem::exists(gltfPath))
        {
            std::cerr << "[GLTF] File not found: " << gltfPath << std::endl;
            delete engine;
            return 1;
        }

        try
        {
            auto gltfModel = std::make_unique<Model>(gltfPath.string(), engine);
            gltfModel->resizeToUnitBox();
            gltfModel->rotate(-90.0f, 0.0f, 0.0f); // Adjust orientation if needed
            engine->addModel(std::move(gltfModel));
        }
        catch (const std::exception &ex)
        {
            std::cerr << "[GLTF] Failed to load " << gltfPath << ": " << ex.what() << std::endl;
            delete engine;
            return 1;
        }
    }
    else
    {
        primaryCamera->cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
        if (!std::filesystem::exists(kVideoPath))
        {
            std::cerr << "[Video] Hardcoded path " << kVideoPath << " does not exist." << std::endl;
            return false;
        }

        videoState = std::make_unique<VideoPlaybackState>();

        video::DecoderInitParams playbackParams{};
        if (!video::initializeVideoDecoder(kVideoPath, videoState->decoder, playbackParams))
        {
            std::cerr << "[Video] Failed to initialize video decoder" << std::endl;
            videoState.reset();
            return false;
        }

        std::cout << "[Video] Video dimensions: " << videoState->decoder.width << "x" << videoState->decoder.height 
                  << ", FPS: " << videoState->decoder.fps << std::endl;

        // Create video quad geometry
        auto quadVertices = video::buildVideoQuadVertices(
            static_cast<float>(videoState->decoder.width), 
            static_cast<float>(videoState->decoder.height)
        );
        
        auto videoModel = std::make_unique<Model>(quadVertices, engine);
        
        // Get the primitive that will display the video
        if (videoModel->meshes.empty() || videoModel->meshes[0].primitives.empty())
        {
            std::cerr << "[Video] Failed to construct quad primitive." << std::endl;
            video::cleanupVideoDecoder(videoState->decoder);
            videoState.reset();
            return false;
        }

        videoState->videoPrimitive = videoModel->meshes[0].primitives[0].get();
        videoState->colorInfo = video::deriveVideoColorInfo(videoState->decoder);
        videoState->videoPrimitive->setYuvColorMetadata(
            static_cast<uint32_t>(videoState->colorInfo.colorSpace),
            static_cast<uint32_t>(videoState->colorInfo.colorRange));
        videoState->videoPrimitive->enableTextureDoubleBuffering();

        // Enable instancing to reuse the decoded frame across a variety of spatial arrangements.
        const float longestVideoSide = static_cast<float>(std::max(videoState->decoder.width, videoState->decoder.height));
        const float normalizedWidth = static_cast<float>(videoState->decoder.width) / longestVideoSide;
        const float normalizedHeight = static_cast<float>(videoState->decoder.height) / longestVideoSide;
        const float quadExtent = std::max(normalizedWidth, normalizedHeight);

        VideoInstanceLayout selectedLayout = kDefaultVideoLayout;
        if (options.forceObeliskLayout)
        {
            selectedLayout = VideoInstanceLayout::Obelisk;
        }
        else if (options.forceGridLayout)
        {
            selectedLayout = VideoInstanceLayout::XZGrid;
        }

        switch (selectedLayout)
        {
        case VideoInstanceLayout::OctagonStack:
            layoutOctagonStack(*videoState->videoPrimitive,
                               kDefaultRows,
                               kDefaultOctagonSegments,
                               quadExtent * 1.75f,
                               quadExtent * 2.0f);
            break;
        case VideoInstanceLayout::Obelisk:
            layoutObelisk(*videoState->videoPrimitive,
                          kObeliskOctagonRows,
                          normalizedWidth,
                          normalizedHeight);
            break;
        case VideoInstanceLayout::ColumnSquares:
            layoutColumnSquares(*videoState->videoPrimitive,
                                kDefaultColumns,
                                quadExtent * 2.5f,
                                quadExtent * 0.75f,
                                quadExtent * 1.5f);
            break;
        case VideoInstanceLayout::XZGrid:
        default:
            layoutXZGrid(*videoState->videoPrimitive,
                         kGridCountX,
                         kGridCountZ,
                         quadExtent * 1.2f,
                         quadExtent * 1.2f);
            break;
        }
        
        std::vector<uint8_t> initialFrame(static_cast<size_t>(videoState->decoder.bufferSize), 0);
        if (videoState->decoder.outputFormat == PrimitiveYuvFormat::NV12)
        {
            const size_t yBytes = videoState->decoder.yPlaneBytes;
            if (yBytes > 0 && yBytes <= initialFrame.size())
            {
                std::fill(initialFrame.begin(), initialFrame.begin() + yBytes, 0x80);
                std::fill(initialFrame.begin() + yBytes, initialFrame.end(), 0x80);
            }
            videoState->videoPrimitive->updateTextureFromNV12(
                initialFrame.data(),
                initialFrame.size(),
                videoState->decoder.width,
                videoState->decoder.height);
        }
        else
        {
            const size_t yBytes = videoState->decoder.yPlaneBytes;
            const size_t uvBytes = videoState->decoder.uvPlaneBytes;
            const bool sixteenBit = videoState->decoder.bytesPerComponent > 1;
        if (sixteenBit)
        {
            const uint32_t bitDepth = videoState->decoder.bitDepth > 0 ? videoState->decoder.bitDepth : 8;
            const uint32_t shift = bitDepth >= 16 ? 0u : 16u - bitDepth;
            const uint16_t baseValue = static_cast<uint16_t>(1u << (bitDepth > 0 ? bitDepth - 1 : 7));
            const uint16_t fillValue = static_cast<uint16_t>(baseValue << shift);
            if (yBytes >= sizeof(uint16_t))
            {
                uint16_t* yDst = reinterpret_cast<uint16_t*>(initialFrame.data());
                std::fill(yDst, yDst + (yBytes / sizeof(uint16_t)), fillValue);
            }
            if (uvBytes >= sizeof(uint16_t))
            {
                uint16_t* uvDst = reinterpret_cast<uint16_t*>(initialFrame.data() + yBytes);
                std::fill(uvDst, uvDst + (uvBytes / sizeof(uint16_t)), fillValue);
            }
        }
            else
            {
                if (yBytes > 0 && yBytes <= initialFrame.size())
                {
                    std::fill(initialFrame.begin(), initialFrame.begin() + yBytes, 0x80);
                }
                if (uvBytes > 0 && yBytes + uvBytes <= initialFrame.size())
                {
                    std::fill(initialFrame.begin() + yBytes, initialFrame.begin() + yBytes + uvBytes, 0x80);
                }
            }
            videoState->videoPrimitive->updateTextureFromPlanarYuv(
                initialFrame.data(),
                yBytes,
                videoState->decoder.width,
                videoState->decoder.height,
                initialFrame.data() + yBytes,
                uvBytes,
                videoState->decoder.chromaWidth,
                videoState->decoder.chromaHeight,
                sixteenBit,
                videoState->decoder.outputFormat);
        }

        videoState->videoPrimitive->transform = glm::mat4(1.0f);
        videoModel->resizeToUnitBox();
        engine->addModel(std::move(videoModel));

        if (!video::startAsyncDecoding(videoState->decoder, 12))
        {
            std::cerr << "[Video] Failed to start async decoder" << std::endl;
            video::cleanupVideoDecoder(videoState->decoder);
            videoState.reset();
            return false;
        }

        // Set up timing for video playback
        videoState->initialized = true;
        videoState->overlayTimerStart = std::chrono::steady_clock::now();
        videoState->framesSinceOverlayUpdate = 0;
        videoState->overlayValid = false;
        videoState->pendingFrames.clear();
        videoState->stagingFrame = video::DecodedFrame{};
        if (videoState->decoder.bufferSize > 0)
        {
            videoState->stagingFrame.buffer.reserve(static_cast<size_t>(videoState->decoder.bufferSize));
        }
        videoState->playbackClockInitialized = false;
        videoState->basePresentationPts = 0.0;
        videoState->playbackStartTime = std::chrono::steady_clock::now();
        updateVideoOverlay(*videoState, 0.0f);

        std::cout << "[Video] Video playback initialized successfully" << std::endl;
    }

    // Custom render loop for video playback
    for (auto &display : engine->displays)
    {
        while (!glfwWindowShouldClose(display->window))
        {
            updateVideoFrame();
            display->render();
        }
    }

    cleanupVideoPlayback();
    delete engine;
    return 0;
}
