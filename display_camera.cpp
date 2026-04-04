#include <algorithm>
#include <iostream>
#include <memory>

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "primitive.h"

void Display::addCamera(Camera* camera)
{
    if (!camera)
    {
        return;
    }
    camera->setWindow(window);
    camera->setFullscreenViewportEnabled(true);
    cameras.push_back(camera);
    updateCameraViewports();
    if (graphicsPipelines[static_cast<size_t>(PrimitiveCullMode::Back)] != VK_NULL_HANDLE)
    {
        camera->allocateDescriptorSet();
    }
}

void Display::updateCameraViewports()
{
    if (cameras.empty())
    {
        return;
    }

    float screenWidth = std::max(1.0f, static_cast<float>(width));
    float screenHeight = std::max(1.0f, static_cast<float>(height));
    glm::vec2 screenCenter(screenWidth * 0.5f, screenHeight * 0.5f);

    for (auto* camera : cameras)
    {
        if (camera)
        {
            if (!camera->isFullscreenViewportEnabled())
            {
                continue;
            }
            float viewportWidth = std::max(1.0f, screenWidth * camera->getFullscreenPercentX());
            float viewportHeight = std::max(1.0f, screenHeight * camera->getFullscreenPercentY());
            camera->setViewport(screenCenter.x, screenCenter.y, viewportWidth, viewportHeight);
        }
    }
}

Camera* Display::findCameraByName(const std::string& name) const
{
    for (auto* camera : cameras)
    {
        if (camera && camera->getCameraName() == name)
        {
            return camera;
        }
    }
    return nullptr;
}

Camera* Display::createCamera(const std::string& name, const glm::vec3& initialPos, const glm::vec2& initialRot)
{
    auto newCamera = std::make_unique<Camera>(engine, this, initialPos, initialRot);
    Camera* cameraPtr = newCamera.get();

    if (!name.empty())
    {
        cameraPtr->setCameraName(name);
    }

    ownedCameras.push_back(std::move(newCamera));

    cameraPtr->setWindow(window);
    cameraPtr->setFullscreenViewportEnabled(true);
    cameras.push_back(cameraPtr);

    updateCameraViewports();

    if (graphicsPipelines[static_cast<size_t>(PrimitiveCullMode::Back)] != VK_NULL_HANDLE)
    {
        cameraPtr->allocateDescriptorSet();
    }

    std::cout << "[Display] Created camera '" << cameraPtr->getCameraName()
              << "' at (" << initialPos.x << ", " << initialPos.y << ", " << initialPos.z << ")" << std::endl;

    return cameraPtr;
}

void Display::removeCamera(Camera* camera)
{
    if (!camera)
    {
        return;
    }

    auto it = std::find(cameras.begin(), cameras.end(), camera);
    if (it != cameras.end())
    {
        cameras.erase(it);
    }

    auto ownedIt = std::find_if(ownedCameras.begin(), ownedCameras.end(),
        [camera](const std::unique_ptr<Camera>& ptr) { return ptr.get() == camera; });
    if (ownedIt != ownedCameras.end())
    {
        ownedCameras.erase(ownedIt);
    }

    updateCameraViewports();
}
