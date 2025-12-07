#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

#include <vulkan/vulkan.h>

#include "display2d.h"
#include "glyph.h"
#include "engine.h"
#include "light.h"
#include "model.h"
#include "utils.h"
#include "video.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace
{
// Look for the sample video in the current directory (files were moved up).
const std::filesystem::path kVideoPath = std::filesystem::path("P1090533_main8_hevc_fast.mkv");
constexpr uint32_t kScrubberWidth = 512;
constexpr uint32_t kScrubberHeight = 64;

struct VideoPlaybackState
{
    video::VideoDecoder decoder;
    Primitive* videoPrimitive = nullptr;
    video::VideoColorInfo colorInfo;
    std::deque<video::DecodedFrame> pendingFrames;
    video::DecodedFrame stagingFrame;
    bool playbackClockInitialized = false;
    double basePtsSeconds = 0.0;
    double lastFramePtsSeconds = 0.0;
    double lastDisplayedSeconds = 0.0;
    std::chrono::steady_clock::time_point lastFrameRenderTime{};
};

struct ScrubberPushConstants
{
    glm::vec2 resolution;
    float progress;
    float isPlaying;
};

static_assert(sizeof(ScrubberPushConstants) == 16, "Scrubber push constant size must be 16 bytes");

struct ScrubberCompute
{
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBuffer outputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory outputMemory = VK_NULL_HANDLE;
    void* mappedData = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    VkDeviceSize bufferSize = 0;
};

void destroyScrubberCompute(ScrubberCompute& compute);

// Scroll handling for rectangle sizing
static double g_scrollDelta = 0.0;
static void onScroll(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset)
{
    g_scrollDelta += yoffset;
}

std::vector<Vertex> buildOverlayQuad(float width, float height)
{
    const float halfWidth = width * 0.5f;
    const float halfHeight = height * 0.5f;
    return {
        {{-halfWidth, -halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{halfWidth, -halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{halfWidth, halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-halfWidth, -halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{halfWidth, halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-halfWidth, halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
}

bool initializeVideoPlayback(const std::filesystem::path& videoPath,
                             Engine* engine,
                             VideoPlaybackState& state,
                             double& durationSeconds)
{
    if (!std::filesystem::exists(videoPath))
    {
        std::cerr << "[Video2D] Missing video file: " << videoPath << std::endl;
        return false;
    }

    video::DecoderInitParams params{};
    if (!video::initializeVideoDecoder(videoPath, state.decoder, params))
    {
        std::cerr << "[Video2D] Failed to initialize decoder" << std::endl;
        return false;
    }

    durationSeconds = 0.0;
    if (state.decoder.formatCtx && state.decoder.formatCtx->duration > 0)
    {
        durationSeconds = static_cast<double>(state.decoder.formatCtx->duration) / static_cast<double>(AV_TIME_BASE);
    }

    auto quadVertices = video::buildVideoQuadVertices(
        static_cast<float>(state.decoder.width),
        static_cast<float>(state.decoder.height));

    auto videoModel = std::make_unique<Model>(quadVertices, engine);
    if (videoModel->meshes.empty() || videoModel->meshes[0].primitives.empty())
    {
        std::cerr << "[Video2D] Failed to create quad primitive" << std::endl;
        video::cleanupVideoDecoder(state.decoder);
        return false;
    }

    state.videoPrimitive = videoModel->meshes[0].primitives[0].get();
    state.colorInfo = video::deriveVideoColorInfo(state.decoder);
    state.videoPrimitive->setYuvColorMetadata(
        static_cast<uint32_t>(state.colorInfo.colorSpace),
        static_cast<uint32_t>(state.colorInfo.colorRange));
    state.videoPrimitive->enableTextureDoubleBuffering();

    std::vector<uint8_t> initialFrame(static_cast<size_t>(state.decoder.bufferSize), 0);
    if (state.decoder.outputFormat == PrimitiveYuvFormat::NV12)
    {
        const size_t yBytes = state.decoder.yPlaneBytes;
        if (yBytes > 0 && yBytes <= initialFrame.size())
        {
            std::fill(initialFrame.begin(), initialFrame.begin() + yBytes, 0x80);
            std::fill(initialFrame.begin() + yBytes, initialFrame.end(), 0x80);
        }
        state.videoPrimitive->updateTextureFromNV12(
            initialFrame.data(),
            initialFrame.size(),
            state.decoder.width,
            state.decoder.height);
    }
    else
    {
        const size_t yBytes = state.decoder.yPlaneBytes;
        const size_t uvBytes = state.decoder.uvPlaneBytes;
        const bool sixteenBit = state.decoder.bytesPerComponent > 1;
        if (sixteenBit)
        {
            const uint32_t bitDepth = state.decoder.bitDepth > 0 ? state.decoder.bitDepth : 8;
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
        const uint8_t* yPlane = initialFrame.data();
        const uint8_t* uvPlane = initialFrame.data() + yBytes;
        state.videoPrimitive->updateTextureFromPlanarYuv(
            yPlane,
            yBytes,
            state.decoder.width,
            state.decoder.height,
            uvPlane,
            uvBytes,
            state.decoder.chromaWidth,
            state.decoder.chromaHeight,
            sixteenBit,
            state.decoder.outputFormat);
    }

    videoModel->resizeToUnitBox();
    engine->addModel(std::move(videoModel));

    if (!video::startAsyncDecoding(state.decoder, 12))
    {
        std::cerr << "[Video2D] Failed to start async decoder" << std::endl;
        video::cleanupVideoDecoder(state.decoder);
        return false;
    }

    state.stagingFrame.buffer.reserve(static_cast<size_t>(state.decoder.bufferSize));
    state.pendingFrames.clear();
    state.playbackClockInitialized = false;
    state.lastDisplayedSeconds = 0.0;
    return true;
}

void pumpDecodedFrames(VideoPlaybackState& state)
{
    constexpr size_t kMaxPendingFrames = 6;
    while (state.pendingFrames.size() < kMaxPendingFrames &&
           video::acquireDecodedFrame(state.decoder, state.stagingFrame))
    {
        state.pendingFrames.emplace_back(std::move(state.stagingFrame));
        state.stagingFrame = video::DecodedFrame{};
        state.stagingFrame.buffer.reserve(static_cast<size_t>(state.decoder.bufferSize));
    }
}

void uploadDecodedFrame(Primitive* primitive,
                        const video::VideoDecoder& decoder,
                        const video::DecodedFrame& frame)
{
    if (!primitive)
    {
        return;
    }

    if (decoder.outputFormat == PrimitiveYuvFormat::NV12)
    {
        primitive->updateTextureFromNV12(
            frame.buffer.data(),
            frame.buffer.size(),
            decoder.width,
            decoder.height);
    }
    else
    {
        const uint8_t* yPlane = frame.buffer.data();
        const uint8_t* uvPlane = yPlane + decoder.yPlaneBytes;
        primitive->updateTextureFromPlanarYuv(
            yPlane,
            decoder.yPlaneBytes,
            decoder.width,
            decoder.height,
            uvPlane,
            decoder.uvPlaneBytes,
            decoder.chromaWidth,
            decoder.chromaHeight,
            decoder.bytesPerComponent > 1,
            decoder.outputFormat);
    }
}

double advancePlayback(VideoPlaybackState& state, bool playing)
{
    if (!state.videoPrimitive)
    {
        return 0.0;
    }

    pumpDecodedFrames(state);

    if (!playing)
    {
        return state.lastDisplayedSeconds;
    }

    if (state.pendingFrames.empty())
    {
        if (state.decoder.finished.load() && !state.decoder.threadRunning.load())
        {
            std::cout << "[Video2D] End of video reached" << std::endl;
        }
        return state.lastDisplayedSeconds;
    }

    auto currentTime = std::chrono::steady_clock::now();
    auto& nextFrame = state.pendingFrames.front();

    if (!state.playbackClockInitialized)
    {
        state.playbackClockInitialized = true;
        state.basePtsSeconds = nextFrame.ptsSeconds;
        state.lastFramePtsSeconds = nextFrame.ptsSeconds;
        state.lastFrameRenderTime = currentTime;
    }

    double frameDelta = nextFrame.ptsSeconds - state.lastFramePtsSeconds;
    if (frameDelta < 1e-6)
    {
        frameDelta = 1.0 / std::max(30.0, state.decoder.fps);
    }

    auto targetTime = state.lastFrameRenderTime + std::chrono::duration<double>(frameDelta);
    if (currentTime + std::chrono::milliseconds(1) < targetTime)
    {
        return state.lastDisplayedSeconds;
    }

    auto frame = std::move(nextFrame);
    state.pendingFrames.pop_front();

    uploadDecodedFrame(state.videoPrimitive, state.decoder, frame);

    state.lastFramePtsSeconds = frame.ptsSeconds;
    state.lastFrameRenderTime = currentTime;
    state.lastDisplayedSeconds = std::max(0.0, state.lastFramePtsSeconds - state.basePtsSeconds);
    return state.lastDisplayedSeconds;
}

bool initializeScrubberCompute(Engine* engine, ScrubberCompute& compute, uint32_t width, uint32_t height)
{
    compute.device = engine->logicalDevice;
    compute.queue = engine->graphicsQueue;
    compute.width = width;
    compute.height = height;
    // Each pixel is a uvec4 in the compute shader (16 bytes)
    compute.bufferSize = static_cast<VkDeviceSize>(width) * height * sizeof(uint32_t) * 4;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(compute.device, &layoutInfo, nullptr, &compute.descriptorSetLayout) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create compute descriptor set layout" << std::endl;
        return false;
    }

    std::vector<char> shaderCode;
    try
    {
        shaderCode = readSPIRVFile("shaders/scrubber.comp.spv");
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[Video2D] " << ex.what() << std::endl;
        return false;
    }

    VkShaderModule shaderModule = engine->createShaderModule(shaderCode);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ScrubberPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compute.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(compute.device, &pipelineLayoutInfo, nullptr, &compute.pipelineLayout) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create compute pipeline layout" << std::endl;
        vkDestroyShaderModule(compute.device, shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = compute.pipelineLayout;

    if (vkCreateComputePipelines(compute.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compute.pipeline) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create compute pipeline" << std::endl;
        vkDestroyShaderModule(compute.device, shaderModule, nullptr);
        destroyScrubberCompute(compute);
        return false;
    }

    vkDestroyShaderModule(compute.device, shaderModule, nullptr);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(compute.device, &poolInfo, nullptr, &compute.descriptorPool) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create descriptor pool" << std::endl;
        destroyScrubberCompute(compute);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = compute.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &compute.descriptorSetLayout;

    if (vkAllocateDescriptorSets(compute.device, &allocInfo, &compute.descriptorSet) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to allocate descriptor set" << std::endl;
        destroyScrubberCompute(compute);
        return false;
    }

    engine->createBuffer(
        compute.bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        compute.outputBuffer,
        compute.outputMemory);

    vkMapMemory(compute.device, compute.outputMemory, 0, compute.bufferSize, 0, &compute.mappedData);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = compute.outputBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = compute.bufferSize;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = compute.descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(compute.device, 1, &write, 0, nullptr);

    VkCommandPoolCreateInfo poolCreateInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCreateInfo.queueFamilyIndex = engine->graphicsQueueFamilyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(compute.device, &poolCreateInfo, nullptr, &compute.commandPool) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create command pool" << std::endl;
        destroyScrubberCompute(compute);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = compute.commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(compute.device, &cmdAllocInfo, &compute.commandBuffer) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to allocate command buffer" << std::endl;
        destroyScrubberCompute(compute);
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(compute.device, &fenceInfo, nullptr, &compute.fence) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create fence" << std::endl;
        destroyScrubberCompute(compute);
        return false;
    }

    return true;
}

void destroyScrubberCompute(ScrubberCompute& compute)
{
    if (compute.fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(compute.device, compute.fence, nullptr);
        compute.fence = VK_NULL_HANDLE;
    }
    if (compute.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(compute.device, compute.commandPool, nullptr);
        compute.commandPool = VK_NULL_HANDLE;
    }
    if (compute.outputBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(compute.device, compute.outputBuffer, nullptr);
        compute.outputBuffer = VK_NULL_HANDLE;
    }
    if (compute.outputMemory != VK_NULL_HANDLE)
    {
        vkUnmapMemory(compute.device, compute.outputMemory);
        vkFreeMemory(compute.device, compute.outputMemory, nullptr);
        compute.outputMemory = VK_NULL_HANDLE;
    }
    if (compute.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(compute.device, compute.descriptorPool, nullptr);
        compute.descriptorPool = VK_NULL_HANDLE;
    }
    if (compute.pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(compute.device, compute.pipeline, nullptr);
        compute.pipeline = VK_NULL_HANDLE;
    }
    if (compute.pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(compute.device, compute.pipelineLayout, nullptr);
        compute.pipelineLayout = VK_NULL_HANDLE;
    }
    if (compute.descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(compute.device, compute.descriptorSetLayout, nullptr);
        compute.descriptorSetLayout = VK_NULL_HANDLE;
    }
}

void runScrubberCompute(Engine* engine,
                        ScrubberCompute& compute,
                        const ScrubberPushConstants& push)
{
    vkResetCommandBuffer(compute.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(compute.commandBuffer, &beginInfo);

    vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
    vkCmdBindDescriptorSets(compute.commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            compute.pipelineLayout,
                            0,
                            1,
                            &compute.descriptorSet,
                            0,
                            nullptr);
    vkCmdPushConstants(compute.commandBuffer,
                       compute.pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(ScrubberPushConstants),
                       &push);

    const uint32_t groupX = (compute.width + 15) / 16;
    const uint32_t groupY = (compute.height + 15) / 16;
    vkCmdDispatch(compute.commandBuffer, groupX, groupY, 1);

    vkEndCommandBuffer(compute.commandBuffer);

    vkResetFences(compute.device, 1, &compute.fence);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &compute.commandBuffer;

    vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, compute.fence);
    vkWaitForFences(compute.device, 1, &compute.fence, VK_TRUE, UINT64_MAX);
}

bool cursorInScrubber(double x, double y, int windowWidth, int windowHeight)
{
    // Use the actual scrubber pixel size to align hit testing with the overlay quad
    const double scrubberWidth = static_cast<double>(kScrubberWidth);
    const double scrubberHeight = static_cast<double>(kScrubberHeight);
    const double margin = 20.0; // small bottom padding
    const double left = (static_cast<double>(windowWidth) - scrubberWidth) * 0.5;
    const double right = left + scrubberWidth;
    const double top = static_cast<double>(windowHeight) - scrubberHeight - margin;
    const double bottom = top + scrubberHeight;
    return x >= left && x <= right && y >= top && y <= bottom;
}
}

int main()
{
    Engine* engine = new Engine();
    Display2D* display = new Display2D(engine, 1280, 720, "Motive Video 2D");
    const float screenWidth = static_cast<float>(std::max(1, display->width));
    const float screenHeight = static_cast<float>(std::max(1, display->height));
    glfwSetScrollCallback(display->window, onScroll);

    Light sceneLight(glm::vec3(0.0f, 0.0f, 1.0f),
                     glm::vec3(0.1f),
                     glm::vec3(0.9f));
    sceneLight.setDiffuse(glm::vec3(1.0f, 0.95f, 0.9f));
    engine->setLight(sceneLight);

    VideoPlaybackState playbackState;
    double videoDurationSeconds = 0.0;
    if (!initializeVideoPlayback(kVideoPath, engine, playbackState, videoDurationSeconds))
    {
        delete engine;
        return 1;
    }

    auto overlayVertices = buildOverlayQuad(1.0f, 1.0f);
    auto overlayModel = std::make_unique<Model>(overlayVertices, engine);
    if (overlayModel->meshes.empty() || overlayModel->meshes[0].primitives.empty())
    {
        std::cerr << "[Video2D] Failed to build overlay quad" << std::endl;
        video::cleanupVideoDecoder(playbackState.decoder);
        delete engine;
        return 1;
    }

    Primitive* overlayPrimitive = overlayModel->meshes[0].primitives[0].get();
    const float scrubberWidth = static_cast<float>(kScrubberWidth);
    const float scrubberHeight = static_cast<float>(kScrubberHeight);
    const float bottomMargin = 20.0f;
    overlayPrimitive->transform = glm::mat4(1.0f);
    overlayPrimitive->transform = glm::translate(overlayPrimitive->transform, glm::vec3(0.0f, 0.0f, 0.02f));
    // Enable double buffering for overlay texture to ensure descriptor set updates
    overlayPrimitive->enableTextureDoubleBuffering();
    engine->addModel(std::move(overlayModel));

    ScrubberCompute scrubberCompute{};
    if (!initializeScrubberCompute(engine, scrubberCompute, kScrubberWidth, kScrubberHeight))
    {
        video::cleanupVideoDecoder(playbackState.decoder);
        delete engine;
        return 1;
    }

    bool playing = true;
    bool spaceHeld = false;
    bool mouseHeld = false;
    std::vector<uint8_t> overlayPixels;
    std::vector<uint8_t> scrubberPixels;
    auto fpsLastSample = std::chrono::steady_clock::now();
    int fpsFrameCounter = 0;
    float currentFps = 0.0f;
    // Rectangle state
    float rectHeight = screenHeight;
    float rectWidth = rectHeight * (9.0f / 16.0f);
    glm::vec2 rectCenter(screenWidth * 0.5f, screenHeight * 0.5f);

    while (!display->shouldClose())
    {
        bool spaceDown = glfwGetKey(display->window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceHeld)
        {
            playing = !playing;
        }
        spaceHeld = spaceDown;

        if (glfwGetKey(display->window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(display->window, GLFW_TRUE);
        }

        int mouseState = glfwGetMouseButton(display->window, GLFW_MOUSE_BUTTON_LEFT);
        if (mouseState == GLFW_PRESS && !mouseHeld)
        {
            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(display->window, &cursorX, &cursorY);
            if (cursorInScrubber(cursorX, cursorY, display->width, display->height))
            {
                playing = !playing;
            }
            else
            {
                rectCenter = glm::vec2(static_cast<float>(cursorX), static_cast<float>(cursorY));
            }
        }
        mouseHeld = (mouseState == GLFW_PRESS);

        // Scroll to resize rectangle
        double scrollDelta = g_scrollDelta;
        g_scrollDelta = 0.0;
        if (std::abs(scrollDelta) > 0.0)
        {
            float scale = 1.0f + static_cast<float>(scrollDelta) * 0.05f;
            rectHeight = std::clamp(rectHeight * scale, 50.0f, screenHeight);
            rectWidth = rectHeight * (9.0f / 16.0f);
        }

        double playbackSeconds = advancePlayback(playbackState, playing);
        double normalizedProgress = videoDurationSeconds > 0.0
                                        ? std::clamp(playbackSeconds / videoDurationSeconds, 0.0, 1.0)
                                        : 0.0;
        ScrubberPushConstants push{
            glm::vec2(static_cast<float>(kScrubberWidth), static_cast<float>(kScrubberHeight)),
            static_cast<float>(normalizedProgress),
            playing ? 1.0f : 0.0f};
        runScrubberCompute(engine, scrubberCompute, push);

        // Convert scrubber compute output (uvec4 buffer) to tightly packed RGBA8
        const size_t scrubberPixelCount = static_cast<size_t>(scrubberCompute.width) * scrubberCompute.height;
        scrubberPixels.resize(scrubberPixelCount * 4);
        const uint32_t* scrubberSrc = reinterpret_cast<const uint32_t*>(scrubberCompute.mappedData);
        for (size_t i = 0; i < scrubberPixelCount; ++i)
        {
            scrubberPixels[i * 4 + 0] = static_cast<uint8_t>(scrubberSrc[i * 4 + 0]);
            scrubberPixels[i * 4 + 1] = static_cast<uint8_t>(scrubberSrc[i * 4 + 1]);
            scrubberPixels[i * 4 + 2] = static_cast<uint8_t>(scrubberSrc[i * 4 + 2]);
            scrubberPixels[i * 4 + 3] = static_cast<uint8_t>(scrubberSrc[i * 4 + 3]);
        }

        // FPS tracking
        fpsFrameCounter++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - fpsLastSample).count();
        if (elapsed >= 500)
        {
            currentFps = static_cast<float>(fpsFrameCounter) * 1000.0f / static_cast<float>(elapsed);
            fpsFrameCounter = 0;
            fpsLastSample = now;
        }

        // Build overlay texture combining scrubber + FPS bitmap
        glyph::OverlayBitmap fpsBitmap = glyph::buildFrameRateOverlay(
            static_cast<uint32_t>(screenWidth),
            static_cast<uint32_t>(screenHeight),
            currentFps);

        // Debug: print FPS and bitmap dimensions
        static int debugFrameCount = 0;
        if (debugFrameCount++ % 60 == 0) {
            std::cout << "[FPS Overlay] FPS: " << currentFps 
                      << ", bitmap: " << fpsBitmap.width << "x" << fpsBitmap.height
                      << ", offset: " << fpsBitmap.offsetX << "," << fpsBitmap.offsetY << std::endl;
        }

        const uint32_t overlayWidth = static_cast<uint32_t>(screenWidth);
        const uint32_t overlayHeight = static_cast<uint32_t>(screenHeight);
        const uint32_t overlayBytes = overlayWidth * overlayHeight * 4;
        if (overlayPixels.size() < overlayBytes)
        {
            overlayPixels.resize(overlayBytes, 0);
        }
        // TEST: Fill entire overlay with semi-transparent green to see if overlay works
        for (size_t i = 0; i < overlayBytes; i += 4) {
            overlayPixels[i + 0] = 0;     // R
            overlayPixels[i + 1] = 255;   // G
            overlayPixels[i + 2] = 0;     // B
            overlayPixels[i + 3] = 128;   // A (50% transparent)
        }

        // Copy scrubber output into overlay buffer at the bottom
        const uint32_t scrubberYOffset = overlayHeight > kScrubberHeight ? (overlayHeight - kScrubberHeight - 20) : 0;
        const uint32_t scrubberXOffset = (overlayWidth > kScrubberWidth) ? (overlayWidth - kScrubberWidth) / 2 : 0;
        for (uint32_t row = 0; row < kScrubberHeight; ++row)
        {
            uint32_t dstRow = scrubberYOffset + row;
            uint32_t dstIndex = (dstRow * overlayWidth + scrubberXOffset) * 4;
            uint32_t srcIndex = row * kScrubberWidth * 4;
            std::memcpy(&overlayPixels[dstIndex], scrubberPixels.data() + srcIndex, kScrubberWidth * 4);
        }

        // Blend FPS bitmap using its computed offset (top-left with margin)
        if (fpsBitmap.width > 0 && fpsBitmap.height > 0 && fpsBitmap.pixels.size() >= fpsBitmap.width * fpsBitmap.height * 4)
        {
            const uint32_t fpsX = fpsBitmap.offsetX;
            const uint32_t fpsY = fpsBitmap.offsetY;
            for (uint32_t y = 0; y < fpsBitmap.height; ++y)
            {
                if (fpsY + y >= overlayHeight)
                    break;
                const uint8_t* srcRow = fpsBitmap.pixels.data() + static_cast<size_t>(y * fpsBitmap.width * 4);
                uint8_t* dstRow = overlayPixels.data() + static_cast<size_t>((fpsY + y) * overlayWidth * 4);
                for (uint32_t x = 0; x < fpsBitmap.width; ++x)
                {
                    if (fpsX + x >= overlayWidth)
                        break;
                    const uint8_t* srcPx = srcRow + x * 4;
                    uint8_t* dstPx = dstRow + (fpsX + x) * 4;
                    float srcA = srcPx[3] / 255.0f;
                    for (int c = 0; c < 4; ++c)
                    {
                        float dst = dstPx[c] / 255.0f;
                        float src = srcPx[c] / 255.0f;
                        float out = src * srcA + dst * (1.0f - srcA);
                        dstPx[c] = static_cast<uint8_t>(std::clamp(out, 0.0f, 1.0f) * 255.0f);
                    }
                }
            }
        }

        // Draw the 9x16 rectangle (semi-transparent)
        float halfW = rectWidth * 0.5f;
        float halfH = rectHeight * 0.5f;
        int x0 = static_cast<int>(std::round(rectCenter.x - halfW));
        int y0 = static_cast<int>(std::round(rectCenter.y - halfH));
        int x1 = static_cast<int>(std::round(rectCenter.x + halfW));
        int y1 = static_cast<int>(std::round(rectCenter.y + halfH));
        x0 = std::clamp(x0, 0, static_cast<int>(overlayWidth) - 1);
        y0 = std::clamp(y0, 0, static_cast<int>(overlayHeight) - 1);
        x1 = std::clamp(x1, x0 + 1, static_cast<int>(overlayWidth));
        y1 = std::clamp(y1, y0 + 1, static_cast<int>(overlayHeight));

        const uint8_t rectColor[4] = {255, 80, 60, 255};
        const uint8_t outlineColor[4] = {0, 0, 0, 255};
        const int outlineThickness = 4;
        for (int y = y0; y < y1; ++y)
        {
            uint8_t* rowPtr = overlayPixels.data() + static_cast<size_t>(y * overlayWidth * 4);
            for (int x = x0; x < x1; ++x)
            {
                uint8_t* dst = rowPtr + x * 4;
                const uint8_t* srcCol = rectColor;
                // Draw outline near the border
                if (x - x0 < outlineThickness || x1 - x <= outlineThickness ||
                    y - y0 < outlineThickness || y1 - y <= outlineThickness)
                {
                    srcCol = outlineColor;
                }
                // Alpha blend rect over existing overlay pixel
                float srcA = srcCol[3] / 255.0f;
                for (int c = 0; c < 4; ++c)
                {
                    float dstC = dst[c] / 255.0f;
                    float srcC = srcCol[c] / 255.0f;
                    float out = srcC * srcA + dstC * (1.0f - srcA);
                    dst[c] = static_cast<uint8_t>(std::clamp(out, 0.0f, 1.0f) * 255.0f);
                }
            }
        }

        // Debug pixel in the center to ensure overlay is visible
        const uint32_t centerX = overlayWidth / 2;
        const uint32_t centerY = overlayHeight / 2;
        size_t centerIdx = static_cast<size_t>((centerY * overlayWidth + centerX) * 4);
        if (centerIdx + 3 < overlayPixels.size())
        {
            overlayPixels[centerIdx + 0] = 0;
            overlayPixels[centerIdx + 1] = 255;
            overlayPixels[centerIdx + 2] = 255;
            overlayPixels[centerIdx + 3] = 255;
        }

        // Upload combined overlay texture
        overlayPrimitive->updateTextureFromPixelData(
            overlayPixels.data(),
            overlayBytes,
            overlayWidth,
            overlayHeight,
            VK_FORMAT_R8G8B8A8_UNORM);

        VkExtent2D overlayExtent{overlayWidth, overlayHeight};
        VkOffset2D overlayOffset{0, 0};

        display->renderFrame(playbackState.videoPrimitive,
                             overlayPrimitive,
                             playbackState.colorInfo,
                             static_cast<uint32_t>(playbackState.decoder.width),
                             static_cast<uint32_t>(playbackState.decoder.height),
                             overlayExtent,
                             overlayOffset);
        display->pollEvents();
    }

    destroyScrubberCompute(scrubberCompute);
    video::cleanupVideoDecoder(playbackState.decoder);
    delete display;
    delete engine;
    return 0;
}
