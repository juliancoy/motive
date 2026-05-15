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
    
    // Initialize BufferManager with VMA support
    bufferManager.initialize(&renderDevice, instance, physicalDevice, logicalDevice);
    
    lightManager.initialize(logicalDevice, graphicsQueueFamilyIndex, memProperties);
    
    // Initialize physics world using factory
    physicsWorld = motive::PhysicsFactory::createWorld(physicsSettings.engineType);
    if (physicsWorld) {
        physicsWorld->initialize();
        physicsWorld->setGravity(physicsSettings.gravity);
    }
}

void Engine::nameVulkanObject(uint64_t handle, VkObjectType type, const char* name) {
    renderDevice.nameVulkanObject(handle, type, name);
}

VkCommandBuffer Engine::beginSingleTimeCommands()
{
    if (bufferManager.isInBatchUpload())
    {
        if (bufferManager.getActiveBatchCommandBuffer() == VK_NULL_HANDLE)
        {
            bufferManager.setActiveBatchCommandBuffer(renderDevice.beginSingleTimeCommands());
        }
        return bufferManager.getActiveBatchCommandBuffer();
    }
    return renderDevice.beginSingleTimeCommands();
}

void Engine::endSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    if (bufferManager.isInBatchUpload() && commandBuffer == bufferManager.getActiveBatchCommandBuffer())
    {
        return;
    }
    renderDevice.endSingleTimeCommands(commandBuffer);
}

VkResult Engine::allocateDescriptorSet(VkDescriptorPool pool,
                                       VkDescriptorSetLayout layout,
                                       VkDescriptorSet& outSet)
{
    return renderDevice.allocateDescriptorSet(pool, layout, outSet);
}

VkResult Engine::freeDescriptorSet(VkDescriptorPool pool, VkDescriptorSet descriptorSet)
{
    return renderDevice.freeDescriptorSet(pool, descriptorSet);
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

    // Shutdown physics before models are destroyed
    physicsWorld.reset();
    
    models.clear();

    for (auto &display : displays)
    {
        delete display;
    }
    displays.clear();
    lightManager.shutdown();
    bufferManager.shutdown();

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

Display *Engine::createWindow(int width, int height, const char *title, bool disableCulling, bool use2DPipeline, bool embeddedMode)
{
    Display *display = new Display(this, width, height, title, disableCulling, use2DPipeline, embeddedMode);
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

void Engine::updatePhysics(float deltaTime)
{
    if (physicsWorld) {
        physicsWorld->stepSimulation(deltaTime, physicsSettings.maxSubSteps);
        if (physicsSettings.autoSync) {
            physicsWorld->syncAllTransforms();
        }
    }
}

void Engine::syncPhysicsToModels()
{
    if (physicsWorld) {
        physicsWorld->syncAllTransforms();
    }
}

void Engine::setPhysicsEngine(motive::PhysicsEngineType type)
{
    if (physicsSettings.engineType == type) return;
    
    // Store old bodies info if needed
    // For now, we just recreate the world
    physicsWorld.reset();
    
    physicsSettings.engineType = type;
    physicsWorld = motive::PhysicsFactory::createWorld(type);
    
    if (physicsWorld) {
        physicsWorld->initialize();
        physicsWorld->setGravity(physicsSettings.gravity);
    }
    
    std::cout << "[Engine] Switched physics engine to: " << motive::PhysicsFactory::getBackendName(type) << std::endl;
}

void Engine::applyPhysicsSettings(const motive::PhysicsSettings& settings)
{
    const bool engineChanged = physicsSettings.engineType != settings.engineType;

    if (engineChanged)
    {
        setPhysicsEngine(settings.engineType);
    }

    physicsSettings = settings;

    if (physicsWorld)
    {
        physicsWorld->setGravity(physicsSettings.gravity);
        physicsWorld->setDebugDrawEnabled(physicsSettings.debugDraw);
    }
}

// Image to buffer copy for frame capture
void Engine::copyImageToBuffer(VkImage srcImage, VkBuffer dstBuffer, 
                              uint32_t width, uint32_t height, 
                              VkFormat format, VkImageLayout srcImageLayout) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    
    // Determine bytes per pixel based on format
    uint32_t bytesPerPixel = 4; // Default for RGBA8
    if (format == VK_FORMAT_R8_UNORM) {
        bytesPerPixel = 1;
    } else if (format == VK_FORMAT_R8G8_UNORM) {
        bytesPerPixel = 2;
    } else if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM) {
        bytesPerPixel = 4;
    }
    
    // Setup buffer copy region
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // Tightly packed
    region.bufferImageHeight = 0; // Tightly packed
    
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    
    // Copy image to buffer
    vkCmdCopyImageToBuffer(
        commandBuffer,
        srcImage,
        srcImageLayout,
        dstBuffer,
        1,
        &region
    );
    
    endSingleTimeCommands(commandBuffer);
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
