#include "detection.hpp"

#include <chrono>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

// Disable ncnn for now due to Vulkan header conflicts
// #ifdef NCNN_AVAILABLE
// #include "net.h"
// #include "cpu.h"
// #endif

#include "engine.h"
#include "utils.h"
#include "decode.h"  // For VideoResources definition

namespace detection {

// YOLODetector implementation
YOLODetector::YOLODetector() = default;

YOLODetector::~YOLODetector() {
    // Cleanup if needed
}

bool YOLODetector::initialize(Engine* engine, const DetectionConfig& config) {
    if (initialized_) {
        std::cerr << "[Detection] Detector already initialized" << std::endl;
        return true;
    }
    
    config_ = config;
    engine_ = engine;
    
    // Placeholder implementation when ncnn is not available
    std::cout << "[Detection] YOLO detector initialized (placeholder - ncnn not available)" << std::endl;
    std::cout << "[Detection] Input size: " << config_.input_width << "x" << config_.input_height << std::endl;
    std::cout << "[Detection] Using " << (config_.use_gpu ? "GPU (Vulkan)" : "CPU") << " for inference" << std::endl;
    
    initialized_ = true;
    return true;
}

std::vector<DetectionResult> YOLODetector::detect(const uint8_t* rgba_data, int width, int height) {
    std::vector<DetectionResult> results;
    
    if (!initialized_ || !rgba_data || width <= 0 || height <= 0) {
        return results;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // ncnn not available at compile time
    results = getSimulatedDetections(width, height);
    
    // Apply NMS
    applyNMS(results, config_.nms_threshold);
    
    // Limit number of detections
    if (results.size() > static_cast<size_t>(config_.max_detections)) {
        results.resize(config_.max_detections);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    last_inference_time_ms_ = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    
    return results;
}

std::vector<DetectionResult> YOLODetector::detectFromVulkanImage(Engine* engine, VkImage image, 
                                                                 VkFormat format, int width, int height) {
    // TODO: Implement efficient GPU-to-GPU inference without CPU readback
    // For now, fall back to CPU readback
    std::cerr << "[Detection] detectFromVulkanImage not implemented yet, falling back to CPU path" << std::endl;
    
    // Capture image from GPU to CPU
    std::vector<uint8_t> rgba_data(width * height * 4);
    // TODO: Implement GPU readback
    // engine->readImageToBuffer(image, format, width, height, rgba_data.data());
    
    return detect(rgba_data.data(), width, height);
}

// Helper function to get simulated detections when ncnn is not available
std::vector<DetectionResult> YOLODetector::getSimulatedDetections(int width, int height) {
    std::vector<DetectionResult> results;
    
    if (width > 0 && height > 0) {
        if (config_.model_type == "pose") {
            // Generate simulated pose keypoints (as small bounding boxes)
            // COCO keypoints: 17 keypoints
            const std::vector<std::string> keypoint_labels = {
                "nose", "left_eye", "right_eye", "left_ear", "right_ear",
                "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
                "left_wrist", "right_wrist", "left_hip", "right_hip",
                "left_knee", "right_knee", "left_ankle", "right_ankle"
            };
            
            // Simulate a person in the center with keypoints
            float person_center_x = 0.5f;
            float person_center_y = 0.5f;
            float person_width = 0.3f;
            float person_height = 0.6f;
            
            // Add person bounding box
            results.emplace_back(
                glm::vec4(person_center_x - person_width/2, person_center_y - person_height/2, 
                         person_width, person_height),
                0.95f,
                0,
                "person"
            );
            
            // Add keypoints as small bounding boxes
            for (int i = 0; i < 17; i++) {
                // Calculate keypoint position based on skeleton
                float kx, ky;
                
                // Simple layout: face at top, shoulders, elbows, wrists, hips, knees, ankles
                if (i == 0) { // nose
                    kx = person_center_x;
                    ky = person_center_y - person_height * 0.4f;
                } else if (i == 1) { // left eye
                    kx = person_center_x - person_width * 0.05f;
                    ky = person_center_y - person_height * 0.42f;
                } else if (i == 2) { // right eye
                    kx = person_center_x + person_width * 0.05f;
                    ky = person_center_y - person_height * 0.42f;
                } else if (i == 3) { // left ear
                    kx = person_center_x - person_width * 0.1f;
                    ky = person_center_y - person_height * 0.4f;
                } else if (i == 4) { // right ear
                    kx = person_center_x + person_width * 0.1f;
                    ky = person_center_y - person_height * 0.4f;
                } else if (i == 5) { // left shoulder
                    kx = person_center_x - person_width * 0.2f;
                    ky = person_center_y - person_height * 0.2f;
                } else if (i == 6) { // right shoulder
                    kx = person_center_x + person_width * 0.2f;
                    ky = person_center_y - person_height * 0.2f;
                } else if (i == 7) { // left elbow
                    kx = person_center_x - person_width * 0.25f;
                    ky = person_center_y;
                } else if (i == 8) { // right elbow
                    kx = person_center_x + person_width * 0.25f;
                    ky = person_center_y;
                } else if (i == 9) { // left wrist
                    kx = person_center_x - person_width * 0.2f;
                    ky = person_center_y + person_height * 0.2f;
                } else if (i == 10) { // right wrist
                    kx = person_center_x + person_width * 0.2f;
                    ky = person_center_y + person_height * 0.2f;
                } else if (i == 11) { // left hip
                    kx = person_center_x - person_width * 0.15f;
                    ky = person_center_y + person_height * 0.1f;
                } else if (i == 12) { // right hip
                    kx = person_center_x + person_width * 0.15f;
                    ky = person_center_y + person_height * 0.1f;
                } else if (i == 13) { // left knee
                    kx = person_center_x - person_width * 0.1f;
                    ky = person_center_y + person_height * 0.3f;
                } else if (i == 14) { // right knee
                    kx = person_center_x + person_width * 0.1f;
                    ky = person_center_y + person_height * 0.3f;
                } else if (i == 15) { // left ankle
                    kx = person_center_x - person_width * 0.05f;
                    ky = person_center_y + person_height * 0.45f;
                } else { // right ankle (i == 16)
                    kx = person_center_x + person_width * 0.05f;
                    ky = person_center_y + person_height * 0.45f;
                }
                
                // Keypoint as small bounding box (circle)
                float keypoint_size = 0.02f; // 2% of image size
                results.emplace_back(
                    glm::vec4(kx - keypoint_size/2, ky - keypoint_size/2, 
                             keypoint_size, keypoint_size),
                    0.8f + (i * 0.01f), // Varying confidence
                    i + 100, // Use IDs > 100 for keypoints
                    keypoint_labels[i]
                );
            }
        } else {
            // Regular object detection
            results.emplace_back(
                glm::vec4(0.3f, 0.3f, 0.2f, 0.3f),  // x, y, w, h
                0.85f,  // confidence
                0,      // person
                "person"
            );
            results.emplace_back(
                glm::vec4(0.6f, 0.4f, 0.15f, 0.2f),
                0.72f,
                2,      // car
                "car"
            );
            results.emplace_back(
                glm::vec4(0.1f, 0.1f, 0.1f, 0.15f),
                0.68f,
                56,     // chair
                "chair"
            );
        }
    }
    
    return results;
}


void YOLODetector::applyNMS(std::vector<DetectionResult>& detections, float threshold) {
    if (detections.empty()) return;
    
    // Sort by confidence (descending)
    std::sort(detections.begin(), detections.end(),
              [](const DetectionResult& a, const DetectionResult& b) {
                  return a.confidence > b.confidence;
              });
    
    std::vector<DetectionResult> filtered;
    std::vector<bool> suppressed(detections.size(), false);
    
    for (size_t i = 0; i < detections.size(); ++i) {
        if (suppressed[i]) continue;
        
        filtered.push_back(detections[i]);
        
        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (suppressed[j]) continue;
            
            // Calculate IoU
            const auto& bbox1 = detections[i].bbox;
            const auto& bbox2 = detections[j].bbox;
            
            float x1 = std::max(bbox1.x, bbox2.x);
            float y1 = std::max(bbox1.y, bbox2.y);
            float x2 = std::min(bbox1.x + bbox1.z, bbox2.x + bbox2.z);
            float y2 = std::min(bbox1.y + bbox1.w, bbox2.y + bbox2.w);
            
            float intersection = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
            float area1 = bbox1.z * bbox1.w;
            float area2 = bbox2.z * bbox2.w;
            float iou = intersection / (area1 + area2 - intersection);
            
            if (iou > threshold) {
                suppressed[j] = true;
            }
        }
    }
    
    detections = std::move(filtered);
}

bool YOLODetector::loadClassLabels(const std::string& label_path) {
    // TODO: Implement label loading from file
    return false;
}

// DetectionBuffer implementation
bool DetectionBuffer::create(Engine* engine, DetectionBuffer& det_buffer, size_t max_detections) {
    if (!engine || max_detections == 0) {
        return false;
    }
    
    size_t buffer_size = max_detections * sizeof(GPUDetection);
    
    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = buffer_size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(engine->logicalDevice, &bufferInfo, nullptr, &det_buffer.buffer) != VK_SUCCESS) {
        std::cerr << "[Detection] Failed to create detection buffer" << std::endl;
        return false;
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(engine->logicalDevice, det_buffer.buffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits, 
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &det_buffer.memory) != VK_SUCCESS) {
        std::cerr << "[Detection] Failed to allocate detection buffer memory" << std::endl;
        vkDestroyBuffer(engine->logicalDevice, det_buffer.buffer, nullptr);
        det_buffer.buffer = VK_NULL_HANDLE;
        return false;
    }
    
    vkBindBufferMemory(engine->logicalDevice, det_buffer.buffer, det_buffer.memory, 0);
    
    // Setup descriptor
    det_buffer.descriptor.buffer = det_buffer.buffer;
    det_buffer.descriptor.offset = 0;
    det_buffer.descriptor.range = buffer_size;
    
    det_buffer.capacity = max_detections;
    det_buffer.count = 0;
    
    return true;
}

bool DetectionBuffer::update(Engine* engine, DetectionBuffer& det_buffer, 
                            const std::vector<DetectionResult>& detections) {
    if (!engine || !det_buffer.buffer) {
        return false;
    }
    
    // Limit to buffer capacity
    size_t num_detections = std::min(detections.size(), det_buffer.capacity);
    det_buffer.count = num_detections;
    
    if (num_detections == 0) {
        // Clear buffer by updating with zero-sized data
        // We'll just leave the buffer as is since count is 0
        return true;
    }
    
    // Convert to GPU format
    std::vector<GPUDetection> gpu_detections(num_detections);
    
    // Color palette for different classes
    static const glm::vec4 colors[] = {
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),   // red
        glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),   // green
        glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),   // blue
        glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),   // yellow
        glm::vec4(1.0f, 0.0f, 1.0f, 1.0f),   // magenta
        glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),   // cyan
        glm::vec4(1.0f, 0.5f, 0.0f, 1.0f),   // orange
        glm::vec4(0.5f, 0.0f, 1.0f, 1.0f),   // purple
    };
    const int num_colors = sizeof(colors) / sizeof(colors[0]);
    
    for (size_t i = 0; i < num_detections; ++i) {
        const auto& det = detections[i];
        auto& gpu_det = gpu_detections[i];
        
        gpu_det.bbox = det.bbox;
        gpu_det.confidence = det.confidence;
        gpu_det.class_id = det.class_id;
        
        // Assign color based on class ID
        int color_idx = det.class_id % num_colors;
        gpu_det.color = colors[color_idx];
        
        // Make bounding box semi-transparent
        gpu_det.color.a = 0.3f;
    }
    
    // Update buffer using staging buffer
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    
    VkDeviceSize bufferSize = num_detections * sizeof(GPUDetection);
    
    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(engine->logicalDevice, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        std::cerr << "[Detection] Failed to create staging buffer" << std::endl;
        return false;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(engine->logicalDevice, stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        std::cerr << "[Detection] Failed to allocate staging buffer memory" << std::endl;
        vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
        return false;
    }
    
    vkBindBufferMemory(engine->logicalDevice, stagingBuffer, stagingMemory, 0);
    
    // Copy data to staging buffer
    void* data;
    vkMapMemory(engine->logicalDevice, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, gpu_detections.data(), bufferSize);
    vkUnmapMemory(engine->logicalDevice, stagingMemory);
    
    // Copy from staging to device buffer
    VkCommandBuffer commandBuffer = engine->beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, det_buffer.buffer, 1, &copyRegion);
    
    engine->endSingleTimeCommands(commandBuffer);
    
    // Cleanup
    vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingMemory, nullptr);
    
    return true;
}

void DetectionBuffer::destroy(Engine* engine, DetectionBuffer& det_buffer) {
    if (!engine) return;
    
    if (det_buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(engine->logicalDevice, det_buffer.buffer, nullptr);
        det_buffer.buffer = VK_NULL_HANDLE;
    }
    
    if (det_buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(engine->logicalDevice, det_buffer.memory, nullptr);
        det_buffer.memory = VK_NULL_HANDLE;
    }
    
    det_buffer.capacity = 0;
    det_buffer.count = 0;
    det_buffer.descriptor = {};
}

// DetectionSystem implementation
DetectionSystem::~DetectionSystem() {
    if (initialized_ && engine_) {
        DetectionBuffer::destroy(engine_, detection_buffer_);
    }
}

DetectionSystem& DetectionSystem::getInstance() {
    static DetectionSystem instance;
    return instance;
}

bool DetectionSystem::initialize(Engine* engine, const DetectionConfig& config) {
    if (initialized_) {
        return true;
    }
    
    engine_ = engine;
    
    // Initialize detector
    if (!detector_.initialize(engine, config)) {
        std::cerr << "[Detection] Failed to initialize YOLO detector" << std::endl;
        return false;
    }
    
    // Create detection buffer
    if (!DetectionBuffer::create(engine, detection_buffer_, config.max_detections)) {
        std::cerr << "[Detection] Failed to create detection buffer" << std::endl;
        return false;
    }
    
    initialized_ = true;
    std::cout << "[Detection] Detection system initialized successfully" << std::endl;
    
    return true;
}

std::vector<DetectionResult> DetectionSystem::processFrame(Engine* engine, VideoResources* video) {
    if (!initialized_ || !enabled_) {
        return std::vector<DetectionResult>();
    }
    
    // Update video resources if provided
    if (video) {
        video_resources_ = video;
    }
    
    // If we have video resources, capture frame from GPU
    if (video_resources_) {
        int width = 0, height = 0;
        auto frame_data = captureFrameFromGPU(engine, width, height);
        
        if (!frame_data.empty() && width > 0 && height > 0) {
            // Run detection on captured frame
            current_detections_ = detector_.detect(frame_data.data(), width, height);
        } else {
            // Fall back to simulated detections
            current_detections_ = detector_.getSimulatedDetections(640, 480);
        }
    } else {
        // No video resources, use simulated detections
        current_detections_ = detector_.getSimulatedDetections(640, 480);
    }
    
    return current_detections_;
}

bool DetectionSystem::updateDetectionBuffer(Engine* engine) {
    if (!initialized_) {
        return false;
    }
    
    return DetectionBuffer::update(engine, detection_buffer_, current_detections_);
}

std::vector<uint8_t> DetectionSystem::captureFrameFromGPU(Engine* engine, int& width, int& height) {
    width = 0;
    height = 0;
    
    if (!engine || !video_resources_) {
        std::cerr << "[Detection] No engine or video resources for frame capture" << std::endl;
        return std::vector<uint8_t>();
    }
    
    // Get video dimensions from VideoImageSet
    width = static_cast<int>(video_resources_->descriptors.width);
    height = static_cast<int>(video_resources_->descriptors.height);
    
    if (width <= 0 || height <= 0) {
        std::cerr << "[Detection] Invalid video dimensions: " << width << "x" << height << std::endl;
        return std::vector<uint8_t>();
    }
    
    // For now, we'll capture the luma (Y) plane and convert to RGBA
    // The luma image is stored in video_resources_->lumaImage
    VkImage lumaImage = video_resources_->lumaImage.image;
    VkFormat lumaFormat = video_resources_->lumaImage.format;
    
    if (lumaImage == VK_NULL_HANDLE) {
        std::cerr << "[Detection] No luma image available for capture" << std::endl;
        return std::vector<uint8_t>();
    }
    
    // Create a staging buffer for the luma image
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    
    VkDeviceSize bufferSize = width * height;  // Y plane is 8-bit per pixel
    
    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(engine->logicalDevice, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        std::cerr << "[Detection] Failed to create staging buffer for frame capture" << std::endl;
        return std::vector<uint8_t>();
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(engine->logicalDevice, stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        std::cerr << "[Detection] Failed to allocate staging buffer memory" << std::endl;
        vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
        return std::vector<uint8_t>();
    }
    
    vkBindBufferMemory(engine->logicalDevice, stagingBuffer, stagingMemory, 0);
    
    // Copy luma image to staging buffer
    engine->copyImageToBuffer(lumaImage, stagingBuffer, width, height, lumaFormat);
    
    // Map memory and copy data
    void* mappedData = nullptr;
    vkMapMemory(engine->logicalDevice, stagingMemory, 0, bufferSize, 0, &mappedData);
    
    // Convert Y plane to RGBA (simple grayscale conversion)
    std::vector<uint8_t> rgba_data(width * height * 4);
    const uint8_t* y_data = static_cast<const uint8_t*>(mappedData);
    
    for (int i = 0; i < width * height; ++i) {
        uint8_t y = y_data[i];
        // Simple Y to RGB conversion (grayscale)
        rgba_data[i * 4 + 0] = y;     // R
        rgba_data[i * 4 + 1] = y;     // G
        rgba_data[i * 4 + 2] = y;     // B
        rgba_data[i * 4 + 3] = 255;   // A
    }
    
    vkUnmapMemory(engine->logicalDevice, stagingMemory);
    
    // Cleanup
    vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingMemory, nullptr);
    
    std::cout << "[Detection] Captured frame from GPU: " << width << "x" << height << std::endl;
    
    return rgba_data;
}

} // namespace detection
