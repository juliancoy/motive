#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include <vulkan/vulkan.h>

#include "command_buffer_submit_tracker.h"

class FrameSyncState
{
public:
    size_t currentFrame() const
    {
        return currentFrame_;
    }

    template <typename FenceProvider>
    void waitForCurrentFrameFence(VkDevice device, FenceProvider&& fenceProvider) const
    {
        const VkFence fence = fenceProvider(currentFrame_);
        if (fence == VK_NULL_HANDLE)
        {
            return;
        }
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    }

    template <typename FenceProvider>
    void waitForReusableCommandBuffer(VkDevice device, FenceProvider&& fenceProvider) const
    {
        commandBufferSubmitTracker.waitForLastSubmit(device, std::forward<FenceProvider>(fenceProvider));
    }

    template <typename FenceProvider>
    void resetCurrentFrameFence(VkDevice device, FenceProvider&& fenceProvider) const
    {
        const VkFence fence = fenceProvider(currentFrame_);
        if (fence == VK_NULL_HANDLE)
        {
            return;
        }
        vkResetFences(device, 1, &fence);
    }

    void markSubmitted()
    {
        commandBufferSubmitTracker.markSubmitted(currentFrame_);
    }

    void advance(uint32_t maxFramesInFlight)
    {
        if (maxFramesInFlight == 0)
        {
            currentFrame_ = 0;
            return;
        }
        currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight;
    }

    void reset()
    {
        currentFrame_ = 0;
        commandBufferSubmitTracker.clear();
    }

private:
    size_t currentFrame_ = 0;
    CommandBufferSubmitTracker commandBufferSubmitTracker;
};
