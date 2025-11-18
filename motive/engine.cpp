#include "engine.h"
#include "model.h"
#include <../tinygltf/tiny_gltf.h>
#include <array>
#include <fstream>

Engine::Engine()
    : renderDevice(),
      instance(renderDevice.getInstance()),
      logicalDevice(renderDevice.getLogicalDevice()),
      physicalDevice(renderDevice.getPhysicalDevice()),
      props(renderDevice.getDeviceProperties()),
      features(renderDevice.getDeviceFeatures()),
      memProperties(renderDevice.getMemoryProperties()),
      descriptorSetLayout(renderDevice.getDescriptorSetLayout()),
      primitiveDescriptorSetLayout(renderDevice.getPrimitiveDescriptorSetLayout()),
      descriptorPool(renderDevice.getDescriptorPool()),
      commandPool(renderDevice.getCommandPool()),
      graphicsQueueFamilyIndex(renderDevice.getGraphicsQueueFamilyIndex()),
      graphicsQueue(renderDevice.getGraphicsQueue())
{
    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    renderDevice.initialize();
}

void Engine::nameVulkanObject(uint64_t handle, VkObjectType type, const char* name) {
    renderDevice.nameVulkanObject(handle, type, name);
}

VkCommandBuffer Engine::beginSingleTimeCommands()
{
    return renderDevice.beginSingleTimeCommands();
}

void Engine::endSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    renderDevice.endSingleTimeCommands(commandBuffer);
}

void Engine::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkCommandPool commandPool, VkQueue graphicsQueue)
{
    renderDevice.copyBuffer(srcBuffer, dstBuffer, size, commandPool, graphicsQueue);
}

void Engine::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                          VkBuffer &buffer, VkDeviceMemory &bufferMemory)
{
    renderDevice.createBuffer(size, usage, properties, buffer, bufferMemory);
}

uint32_t Engine::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    return renderDevice.findMemoryType(typeFilter, properties);
}

VkShaderModule Engine::createShaderModule(const std::vector<char> &code)
{
    return renderDevice.createShaderModule(code);
}

void Engine::createDescriptorSetLayouts()
{
    renderDevice.createDescriptorSetLayouts();
}

Engine::~Engine()
{
    if (logicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(logicalDevice);
    }

    models.clear();

    for (auto &display : displays)
    {
        delete display;
    }
    displays.clear();

    renderDevice.shutdown();
    glfwTerminate();
}

void Engine::addModel(std::unique_ptr<Model> model)
{
    if (!model)
    {
        throw std::runtime_error("Attempted to add null model to engine");
    }
    models.push_back(std::move(model));
}

Display *Engine::createWindow(int width, int height, const char *title)
{
    Display *display = new Display(this, width, height, title);
    displays.push_back(display);
    return display;
}

void Engine::renderLoop()
{
    for (auto &display : displays)
    {
        while (!glfwWindowShouldClose(display->window))
        {
            display->render();
        }
    }
}

std::vector<char> readSPIRVFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open SPIR-V file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize % 4 != 0)
    {
        throw std::runtime_error("SPIR-V file size not multiple of 4: " + filename);
    }

    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    if (fileSize >= 4)
    {
        uint32_t magic = *reinterpret_cast<uint32_t *>(buffer.data());
        if (magic != 0x07230203)
        {
            throw std::runtime_error("Invalid SPIR-V magic number in file: " + filename);
        }
    }

    return buffer;
}

// C interface for Python
Engine *engine;

extern "C"
{
    void *create_engine()
    {
        engine = new Engine();
        return engine;
    }

    void destroy_engine(void *engine)
    {
        delete static_cast<Engine *>(engine);
    }

    void create_engine_window(void *engine, int width, int height, const char *title)
    {
        static_cast<Engine *>(engine)->createWindow(width, height, title);
    }

    void render(Engine *engine)
    {
        if (!engine)
            return;

        // Render frame
        engine->renderLoop();
    }
}
