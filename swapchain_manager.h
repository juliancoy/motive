#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <memory>

class Engine;
class VmaAllocatorManager;

// Manages swapchain, render pass, framebuffers, and MSAA/depth attachments
class SwapchainManager
{
public:
    SwapchainManager();
    ~SwapchainManager();

    // Non-copyable
    SwapchainManager(const SwapchainManager&) = delete;
    SwapchainManager& operator=(const SwapchainManager&) = delete;
    // Movable
    SwapchainManager(SwapchainManager&& other) noexcept;
    SwapchainManager& operator=(SwapchainManager&& other) noexcept;

    void initialize(Engine* engine, VkSurfaceKHR surface, int width, int height);
    void shutdown();
    bool isInitialized() const { return engine_ != nullptr; }

    // Swapchain operations
    void createSwapchain();
    void recreateSwapchain(int width, int height);
    void cleanupSwapchainResources();
    
    // Accessors
    VkSwapchainKHR getSwapchain() const { return swapchain_; }
    VkRenderPass getRenderPass() const { return renderPass_; }
    VkImage getSwapchainImage(uint32_t index) const {
        return index < images_.size() ? images_[index] : VK_NULL_HANDLE;
    }
    VkFramebuffer getFramebuffer(uint32_t index) const { 
        return index < framebuffers_.size() ? framebuffers_[index] : VK_NULL_HANDLE; 
    }
    uint32_t getFramebufferCount() const { return static_cast<uint32_t>(framebuffers_.size()); }
    
    VkSemaphore getImageAvailableSemaphore(uint32_t frame) const {
        return frame < imageAvailableSemaphores_.size() ? imageAvailableSemaphores_[frame] : VK_NULL_HANDLE;
    }
    VkSemaphore getRenderFinishedSemaphore(uint32_t frame) const {
        return frame < renderFinishedSemaphores_.size() ? renderFinishedSemaphores_[frame] : VK_NULL_HANDLE;
    }
    VkFence getInFlightFence(uint32_t frame) const {
        return frame < inFlightFences_.size() ? inFlightFences_[frame] : VK_NULL_HANDLE;
    }
    
    void setImageInFlight(uint32_t imageIndex, VkFence fence);
    VkFence getImageInFlight(uint32_t imageIndex) const {
        return imageIndex < imagesInFlight_.size() ? imagesInFlight_[imageIndex] : VK_NULL_HANDLE;
    }

    // Image format and extent
    VkFormat getImageFormat() const { return imageFormat_; }
    VkExtent2D getExtent() const { return extent_; }
    int getWidth() const { return static_cast<int>(extent_.width); }
    int getHeight() const { return static_cast<int>(extent_.height); }
    uint32_t getMaxFramesInFlight() const { return MAX_FRAMES_IN_FLIGHT; }

    // Frame synchronization
    uint32_t acquireNextImage(uint32_t currentFrame, uint32_t* imageIndex);
    VkResult presentImage(uint32_t imageIndex, uint32_t currentFrame);

    // MSAA
    VkSampleCountFlagBits getMsaaSamples() const { return msaaSamples_; }
    void setMsaaSamples(VkSampleCountFlagBits samples) { msaaSamples_ = samples; }

private:
    void createSyncObjects();
    void createRenderPass();
    void createFramebuffers();
    void createMsaaResources();
    void createDepthResources();
    void transitionAttachmentLayouts();

    Engine* engine_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    
    // Swapchain
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    VkFormat imageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ = {};
    
    // Render pass
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    
    // Framebuffers
    std::vector<VkFramebuffer> framebuffers_;
    
    // MSAA color attachment
    VkImage msaaColorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaColorMemory_ = VK_NULL_HANDLE;
    VkImageView msaaColorView_ = VK_NULL_HANDLE;
    
    // Depth attachment
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    
    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    std::vector<VkFence> imagesInFlight_;
    
    // Command pool/buffer for swapchain operations
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
};
