#include "vv_engine_device_context.h"

EngineDeviceContextAdapter::EngineDeviceContextAdapter(Engine& engineRef)
    : engine(engineRef)
{
    // Mirror Engine-owned handles.
    instance = engine.instance;
    physicalDevice = engine.physicalDevice;
    device = engine.logicalDevice;
    graphicsQueueFamilyIndex = engine.getGraphicsQueueFamilyIndex();
    videoDecodeQueueFamilyIndex = engine.getVideoDecodeQueueFamilyIndex();
    videoEncodeQueueFamilyIndex = engine.getVideoEncodeQueueFamilyIndex();
    graphicsQueue = engine.getGraphicsQueue();
    videoDecodeQueue = engine.getVideoDecodeQueue();
    videoEncodeQueue = engine.getVideoEncodeQueue();
}
