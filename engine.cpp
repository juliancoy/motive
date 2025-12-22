#include "engine.h"
#include "model.h"
#include <../tinygltf/tiny_gltf.h>
#include <array>

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
      graphicsQueue(renderDevice.getGraphicsQueue()),
      videoQueueFamilyIndex(renderDevice.getVideoQueueFamilyIndex()),
      videoQueue(renderDevice.getVideoQueue()),
      videoDecodeQueueFamilyIndex(renderDevice.getVideoDecodeQueueFamilyIndex()),
      videoDecodeQueue(renderDevice.getVideoDecodeQueue()),
      videoEncodeQueueFamilyIndex(renderDevice.getVideoEncodeQueueFamilyIndex()),
      videoEncodeQueue(renderDevice.getVideoEncodeQueue())
{
    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    renderDevice.initialize();
    msaaSampleCount = queryMaxUsableSampleCount();
    createLightResources();
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
    destroyLightResources();

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

Display *Engine::createWindow(int width, int height, const char *title, bool disableCulling, bool use2DPipeline)
{
    Display *display = new Display(this, width, height, title, disableCulling, use2DPipeline);
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

void Engine::createLightResources()
{
    VkDeviceSize bufferSize = sizeof(LightUBOData);
    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 lightUBO,
                 lightUBOMemory);

    if (vkMapMemory(logicalDevice, lightUBOMemory, 0, bufferSize, 0, &lightUBOMapped) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to map light uniform buffer.");
    }

    currentLight = Light();
    updateLightBuffer();
}

void Engine::destroyLightResources()
{
    if (lightUBOMapped)
    {
        vkUnmapMemory(logicalDevice, lightUBOMemory);
        lightUBOMapped = nullptr;
    }
    if (lightUBO != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(logicalDevice, lightUBO, nullptr);
        lightUBO = VK_NULL_HANDLE;
    }
    if (lightUBOMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(logicalDevice, lightUBOMemory, nullptr);
        lightUBOMemory = VK_NULL_HANDLE;
    }
}

void Engine::updateLightBuffer()
{
    if (!lightUBOMapped)
    {
        return;
    }

    LightUBOData ubo{};
    ubo.direction = glm::vec4(currentLight.direction, 0.0f);
    ubo.ambient = glm::vec4(currentLight.ambient, 0.0f);
    ubo.diffuse = glm::vec4(currentLight.diffuse, 0.0f);

    std::memcpy(lightUBOMapped, &ubo, sizeof(ubo));
}

void Engine::setLight(const Light &light)
{
    currentLight = light;
    updateLightBuffer();
}

void Engine::setMsaaSampleCount(VkSampleCountFlagBits requested)
{
    msaaSampleCount = clampSampleCount(requested);
}

VkSampleCountFlagBits Engine::queryMaxUsableSampleCount() const
{
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts &
                                props.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_8_BIT)
        return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT)
        return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT)
        return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

VkSampleCountFlagBits Engine::clampSampleCount(VkSampleCountFlagBits requested) const
{
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts &
                                props.limits.framebufferDepthSampleCounts;

    std::array<VkSampleCountFlagBits, 7> order = {
        VK_SAMPLE_COUNT_64_BIT,
        VK_SAMPLE_COUNT_32_BIT,
        VK_SAMPLE_COUNT_16_BIT,
        VK_SAMPLE_COUNT_8_BIT,
        VK_SAMPLE_COUNT_4_BIT,
        VK_SAMPLE_COUNT_2_BIT,
        VK_SAMPLE_COUNT_1_BIT};

    uint32_t requestedValue = static_cast<uint32_t>(requested);
    for (auto candidate : order)
    {
        uint32_t candidateValue = static_cast<uint32_t>(candidate);
        if (candidateValue > requestedValue)
            continue;
        if (counts & candidate)
            return candidate;
    }

    // Fallback to highest available if request was invalid
    for (auto candidate : order)
    {
        if (counts & candidate)
            return candidate;
    }
    return VK_SAMPLE_COUNT_1_BIT;
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
