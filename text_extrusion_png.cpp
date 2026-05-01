#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "model.h"
#include "primitive.h"
#include "text_mesh_extrusion.h"
#include "text_rendering.h"
 #include "light.h"

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
    image = image.mirrored(true, true);

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
                                               glm::vec3(0.0f, 0.0f, 3.0f),
                                               glm::vec2(glm::radians(0.0f), glm::radians(-2.0f)));
        if (!camera)
        {
            throw std::runtime_error("Failed to create camera");
        }
        display->setActiveCamera(camera);

        motive::text::FontRenderOptions fontOptions;
        fontOptions.fontPath = args.fontPath;
        fontOptions.bold = true;

        motive::text::TextOverlayStyle style;
        style.drawBackground = false;
        style.drawOutline = false;
        style.drawShadow = false;
        style.textColor = 0xFFFFFFFFu;

        const uint32_t pxHeight = static_cast<uint32_t>(std::max(8, args.pixelHeight));
        const size_t charCount = std::max<size_t>(args.text.size(), 1u);
        const uint32_t overlayWidth = static_cast<uint32_t>(std::clamp<size_t>(charCount * static_cast<size_t>(pxHeight) * 2u,
                                                                                1024u,
                                                                                4096u));
        const uint32_t overlayHeight = static_cast<uint32_t>(std::clamp<uint32_t>(pxHeight * 4u,
                                                                                   256u,
                                                                                   1024u));

        const motive::text::OverlayBitmap bitmap = motive::text::buildStyledTextOverlay(
            overlayWidth,
            overlayHeight,
            args.text,
            pxHeight,
            fontOptions,
            style);
        if (bitmap.pixels.empty() || bitmap.width == 0 || bitmap.height == 0)
        {
            throw std::runtime_error("Failed to build text overlay bitmap");
        }

        std::vector<Vertex> vertices = motive::text::buildExtrudedTextVertices(bitmap, args.extrudeDepth);
        if (vertices.empty())
        {
            throw std::runtime_error("Failed to build extruded text mesh");
        }

        auto model = std::make_unique<Model>(vertices, &engine);
        model->name = "text_extrusion_vulkan";
        if (model->meshes.empty() || model->meshes.front().primitives.empty())
        {
            throw std::runtime_error("Invalid model primitive state");
        }

        Primitive* primitive = model->meshes.front().primitives.front().get();
        primitive->updateTextureFromPixelData(bitmap.pixels.data(),
                                              bitmap.pixels.size(),
                                              bitmap.width,
                                              bitmap.height,
                                              VK_FORMAT_R8G8B8A8_UNORM);
        primitive->alphaMode = PrimitiveAlphaMode::Blend;
        primitive->cullMode = PrimitiveCullMode::Disabled;
        primitive->depthTestEnabled = true;
        primitive->depthWriteEnabled = true;
        primitive->forceAlphaOne = true;

        model->setSceneTransform(glm::vec3(0.0f, -0.1f, 0.0f),
                                 glm::vec3(-14.0f, -24.0f, -6.0f),
                                 glm::vec3(2.35f, 2.35f, 2.35f));

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
