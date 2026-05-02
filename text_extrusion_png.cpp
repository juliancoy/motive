#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "light.h"
#include "model.h"
#include "text_mesh_extrusion.h"
#include "text_rendering.h"

namespace {

struct Args
{
    std::string text = "What";
    std::string output = "/tmp/motive_extrusion_text.png";
    std::string fontPath;
    int pixelHeight = 132;
    float extrudeDepth = 0.22f;
    int canvasWidth = 1920;
    int canvasHeight = 1080;
    int meshSupersample = 3;
    float bevelScale = 1.0f;
};

bool parseArgs(int argc, char** argv, Args& args)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> const char*
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << flag << std::endl;
                return nullptr;
            }
            return argv[++i];
        };

        if (a == "--text")
        {
            const char* v = need("--text");
            if (!v) return false;
            args.text = v;
        }
        else if (a == "--out")
        {
            const char* v = need("--out");
            if (!v) return false;
            args.output = v;
        }
        else if (a == "--font")
        {
            const char* v = need("--font");
            if (!v) return false;
            args.fontPath = v;
        }
        else if (a == "--pixel-height")
        {
            const char* v = need("--pixel-height");
            if (!v) return false;
            args.pixelHeight = std::max(8, std::stoi(v));
        }
        else if (a == "--extrude-depth")
        {
            const char* v = need("--extrude-depth");
            if (!v) return false;
            args.extrudeDepth = std::max(0.02f, std::stof(v));
        }
        else if (a == "--canvas-width")
        {
            const char* v = need("--canvas-width");
            if (!v) return false;
            args.canvasWidth = std::max(64, std::stoi(v));
        }
        else if (a == "--canvas-height")
        {
            const char* v = need("--canvas-height");
            if (!v) return false;
            args.canvasHeight = std::max(64, std::stoi(v));
        }
        else if (a == "--mesh-supersample")
        {
            const char* v = need("--mesh-supersample");
            if (!v) return false;
            args.meshSupersample = std::clamp(std::stoi(v), 1, 6);
        }
        else if (a == "--bevel-scale")
        {
            const char* v = need("--bevel-scale");
            if (!v) return false;
            args.bevelScale = std::clamp(std::stof(v), 0.0f, 2.0f);
        }
        else
        {
            std::cerr << "Unknown argument: " << a << std::endl;
            return false;
        }
    }
    return true;
}

void transitionImageLayout(Engine& engine,
                           VkImage image,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout)
{
    VkCommandBuffer cb = engine.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else
    {
        throw std::runtime_error("Unsupported image layout transition");
    }

    vkCmdPipelineBarrier(cb,
                         srcStage,
                         dstStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    engine.endSingleTimeCommands(cb);
}

bool captureSwapchainImageToPng(Engine& engine, Display& display, const std::string& outPath)
{
    VkExtent2D extent = display.getSwapchainExtent();
    if (extent.width == 0 || extent.height == 0)
    {
        return false;
    }

    const uint32_t imageIndex = display.getLastRenderedImageIndex();
    VkImage swapImage = display.getSwapchainManager().getSwapchainImage(imageIndex);
    if (swapImage == VK_NULL_HANDLE)
    {
        return false;
    }

    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
    VkBuffer readbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
    engine.createBuffer(bufferSize,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        readbackBuffer,
                        readbackMemory);

    transitionImageLayout(engine, swapImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    engine.copyImageToBuffer(swapImage,
                             readbackBuffer,
                             extent.width,
                             extent.height,
                             display.getSwapchainImageFormat(),
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transitionImageLayout(engine, swapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    std::vector<uint8_t> rgba(static_cast<size_t>(bufferSize), 0u);
    void* mapped = nullptr;
    vkMapMemory(engine.logicalDevice, readbackMemory, 0, bufferSize, 0, &mapped);
    std::memcpy(rgba.data(), mapped, static_cast<size_t>(bufferSize));
    vkUnmapMemory(engine.logicalDevice, readbackMemory);

    if (display.getSwapchainImageFormat() == VK_FORMAT_B8G8R8A8_UNORM ||
        display.getSwapchainImageFormat() == VK_FORMAT_B8G8R8A8_SRGB)
    {
        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        {
            std::swap(rgba[i + 0], rgba[i + 2]);
        }
    }

    QImage image(static_cast<int>(extent.width), static_cast<int>(extent.height), QImage::Format_RGBA8888);
    if (image.bytesPerLine() != static_cast<int>(extent.width * 4u))
    {
        vkDestroyBuffer(engine.logicalDevice, readbackBuffer, nullptr);
        vkFreeMemory(engine.logicalDevice, readbackMemory, nullptr);
        return false;
    }

    std::memcpy(image.bits(), rgba.data(), rgba.size());

    vkDestroyBuffer(engine.logicalDevice, readbackBuffer, nullptr);
    vkFreeMemory(engine.logicalDevice, readbackMemory, nullptr);

    return image.save(QString::fromStdString(outPath), "PNG");
}

}  // namespace

int main(int argc, char** argv)
{
    Args args;
    if (!parseArgs(argc, argv, args))
    {
        return 1;
    }

    try
    {
        Engine engine;
        Display* display = engine.createWindow(args.canvasWidth,
                                               args.canvasHeight,
                                               "text_extrusion_png",
                                               false,
                                               false,
                                               true);
        display->setBackgroundColor(0.27f, 0.35f, 0.77f);
        display->setCustomOverlayBitmap(glyph::OverlayBitmap{}, true);

        Light sceneLight(glm::vec3(0.35f, -0.25f, 0.9f),
                         glm::vec3(0.35f),
                         glm::vec3(1.1f, 1.05f, 1.0f));
        engine.setLight(sceneLight);

        Camera* camera = display->createCamera("capture_cam",
                                               glm::vec3(0.0f, 0.03f, 4.8f),
                                               glm::vec2(glm::radians(0.0f), glm::radians(-1.0f)));
        if (!camera)
        {
            throw std::runtime_error("Failed to create camera");
        }
        camera->setOrthographicProjection(4.1f, 2.3f, 0.1f, 100.0f);
        display->setActiveCamera(camera);

        motive::text::ExtrudedTextOptions textOptions;
        textOptions.pixelHeight = static_cast<uint32_t>(std::max(8, args.pixelHeight));
        textOptions.meshSupersample = static_cast<uint32_t>(args.meshSupersample);
        textOptions.font.fontPath = args.fontPath;
        textOptions.font.bold = true;
        textOptions.depth = args.extrudeDepth;
        textOptions.bevelScale = args.bevelScale;

        std::vector<Vertex> vertices = motive::text::buildExtrudedTextVerticesFromText(args.text, textOptions);
        if (vertices.empty())
        {
            throw std::runtime_error("Failed to build extruded text mesh");
        }
        glm::vec3 vmin(std::numeric_limits<float>::max());
        glm::vec3 vmax(std::numeric_limits<float>::lowest());
        for (const Vertex& v : vertices)
        {
            vmin = glm::min(vmin, v.pos);
            vmax = glm::max(vmax, v.pos);
        }
        std::cout << "[TextExtrusion] vertex bounds min=(" << vmin.x << "," << vmin.y << "," << vmin.z
                  << ") max=(" << vmax.x << "," << vmax.y << "," << vmax.z << ")\n";

        auto model = std::make_unique<Model>(vertices, &engine);
        model->name = "text_extrusion_vulkan";
        if (model->meshes.empty() || model->meshes.front().primitives.empty())
        {
            throw std::runtime_error("Invalid model primitive state");
        }

        motive::text::applyExtrudedTextMaterial(*model, glm::vec3(0.97f, 0.97f, 0.97f), true, true);

        model->setSceneTransform(glm::vec3(0.0f, -0.04f, 0.0f),
                                 glm::vec3(-12.0f, 20.0f, 0.0f),
                                 glm::vec3(0.94f, 0.94f, 0.94f));

        engine.addModel(std::move(model));

        display->render();
        vkDeviceWaitIdle(engine.logicalDevice);

        if (!captureSwapchainImageToPng(engine, *display, args.output))
        {
            throw std::runtime_error("Failed to capture swapchain image to PNG");
        }

        std::cout << "Wrote " << args.output << " (" << args.canvasWidth << "x" << args.canvasHeight
                  << ") via Vulkan engine render path" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "text_extrusion_png failed: " << e.what() << std::endl;
        return 2;
    }

    return 0;
}
