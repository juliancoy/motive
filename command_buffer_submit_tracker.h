#pragma once

#include <cstddef>

#include <vulkan/vulkan.h>

class CommandBufferSubmitTracker
{
public:
    template <typename FenceProvider>
    void waitForLastSubmit(VkDevice device, FenceProvider&& fenceProvider) const
    {
        if (!hasPendingSubmit_)
        {
            return;
        }
        const VkFence fence = fenceProvider(lastSubmitFrame_);
        if (fence == VK_NULL_HANDLE)
        {
            return;
        }
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    }

    void markSubmitted(size_t frameIndex)
    {
        hasPendingSubmit_ = true;
        lastSubmitFrame_ = frameIndex;
    }

    void clear()
    {
        hasPendingSubmit_ = false;
        lastSubmitFrame_ = 0;
    }

private:
    bool hasPendingSubmit_ = false;
    size_t lastSubmitFrame_ = 0;
};
