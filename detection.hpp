#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

// Disable ncnn for now due to Vulkan header conflicts
// #ifdef NCNN_AVAILABLE
// #include "net.h"
// #include "cpu.h"
// #endif

class Engine;

// Forward declaration
struct VideoResources;

namespace detection {

// Detection result structure
struct DetectionResult {
    glm::vec4 bbox;        // x, y, width, height (normalized 0-1)
    float confidence;      // confidence score 0-1
    int class_id;          // class identifier
    std::string label;     // class label (optional)
    
    DetectionResult(glm::vec4 bbox = glm::vec4(0.0f), float conf = 0.0f, int cls = -1, const std::string& lbl = "")
        : bbox(bbox), confidence(conf), class_id(cls), label(lbl) {}
};

// Detection system configuration
struct DetectionConfig {
    // Model paths
    std::string param_path;
    std::string bin_path;
    
    // Input size for the model
    int input_width = 416;
    int input_height = 416;
    
    // Confidence threshold
    float confidence_threshold = 0.5f;
    
    // NMS threshold
    float nms_threshold = 0.4f;
    
    // Use GPU (Vulkan) for inference
    bool use_gpu = true;
    
    // Maximum number of detections to return
    int max_detections = 100;
    
    // Class labels (if available)
    std::vector<std::string> class_labels;
    
    // Model type: "detection" or "pose"
    std::string model_type = "detection";
    
    // Pose-specific configuration
    struct PoseConfig {
        int num_keypoints = 17;  // COCO keypoints
        float keypoint_threshold = 0.5f;
        std::vector<std::string> keypoint_labels = {
            "nose", "left_eye", "right_eye", "left_ear", "right_ear",
            "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
            "left_wrist", "right_wrist", "left_hip", "right_hip",
            "left_knee", "right_knee", "left_ankle", "right_ankle"
        };
        std::vector<std::pair<int, int>> skeleton = {
            {0, 1}, {0, 2}, {1, 3}, {2, 4},  // face
            {5, 6}, {5, 7}, {7, 9}, {6, 8}, {8, 10},  // upper body
            {5, 11}, {6, 12}, {11, 13}, {13, 15}, {12, 14}, {14, 16}  // lower body
        };
    } pose_config;
};

// Main detection system class
class YOLODetector {
public:
    YOLODetector();
    ~YOLODetector();
    
    // Initialize the detector with configuration
    bool initialize(Engine* engine, const DetectionConfig& config);
    
    // Process a frame (RGBA8 data, width x height)
    std::vector<DetectionResult> detect(const uint8_t* rgba_data, int width, int height);
    
    // Process a frame from Vulkan image (more efficient, avoids CPU readback)
    std::vector<DetectionResult> detectFromVulkanImage(Engine* engine, VkImage image, 
                                                       VkFormat format, int width, int height);
    
    // Get current configuration
    const DetectionConfig& getConfig() const { return config_; }
    
    // Check if detector is initialized
    bool isInitialized() const { return initialized_; }
    
    // Get last inference time in milliseconds
    float getLastInferenceTime() const { return last_inference_time_ms_; }
    
    // Get simulated detections for testing
    std::vector<DetectionResult> getSimulatedDetections(int width, int height);
    
private:
    bool initialized_ = false;
    DetectionConfig config_;
    Engine* engine_ = nullptr;
    float last_inference_time_ms_ = 0.0f;
    
    // Non-maximum suppression
    void applyNMS(std::vector<DetectionResult>& detections, float threshold);
    
    // Load class labels from file
    bool loadClassLabels(const std::string& label_path);
};

// Detection buffer for GPU (storage buffer for shader)
struct DetectionBuffer {
    struct GPUDetection {
        glm::vec4 bbox;        // x, y, width, height (normalized 0-1)
        glm::vec4 color;       // rgba color for visualization
        float confidence;      // confidence score
        int class_id;          // class identifier
        int padding[2];        // padding for alignment
    };
    
    static_assert(sizeof(GPUDetection) == 48, "GPUDetection size must be 48 bytes for alignment");
    
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorBufferInfo descriptor{};
    size_t capacity = 0;  // Maximum number of detections
    size_t count = 0;     // Current number of detections
    
    // Create detection buffer
    static bool create(Engine* engine, DetectionBuffer& det_buffer, size_t max_detections);
    
    // Update detection buffer with new results
    static bool update(Engine* engine, DetectionBuffer& det_buffer, 
                       const std::vector<DetectionResult>& detections);
    
    // Destroy detection buffer
    static void destroy(Engine* engine, DetectionBuffer& det_buffer);
};

// Detection system manager (singleton-like)
class DetectionSystem {
public:
    static DetectionSystem& getInstance();
    
    // Initialize detection system
    bool initialize(Engine* engine, const DetectionConfig& config = DetectionConfig());
    
    // Process current video frame (captures from GPU if possible)
    std::vector<DetectionResult> processFrame(Engine* engine, VideoResources* video = nullptr);
    
    // Get current detection results
    const std::vector<DetectionResult>& getCurrentDetections() const { return current_detections_; }
    
    // Get GPU detection buffer
    const DetectionBuffer& getDetectionBuffer() const { return detection_buffer_; }
    
    // Update detection buffer with current results
    bool updateDetectionBuffer(Engine* engine);
    
    // Set video resources for frame capture
    void setVideoResources(VideoResources* video) { video_resources_ = video; }
    
    // Check if detection is enabled
    bool isEnabled() const { return enabled_; }
    
    // Enable/disable detection
    void setEnabled(bool enabled) { enabled_ = enabled; }
    
    // Toggle detection
    void toggle() { enabled_ = !enabled_; }
    
    // Get last inference time
    float getLastInferenceTime() const { return detector_.getLastInferenceTime(); }
    
private:
    DetectionSystem() = default;
    ~DetectionSystem();
    
    DetectionSystem(const DetectionSystem&) = delete;
    DetectionSystem& operator=(const DetectionSystem&) = delete;
    
    YOLODetector detector_;
    DetectionBuffer detection_buffer_;
    std::vector<DetectionResult> current_detections_;
    bool initialized_ = false;
    bool enabled_ = false;
    Engine* engine_ = nullptr;
    VideoResources* video_resources_ = nullptr;
    
    // Capture current video frame from GPU
    std::vector<uint8_t> captureFrameFromGPU(Engine* engine, int& width, int& height);
};

// Default YOLO configuration (using YOLO11m model)
inline DetectionConfig getDefaultYOLOConfig() {
    DetectionConfig config;
    config.param_path = "models/yolo11m_ncnn_model/model.ncnn.param";
    config.bin_path = "models/yolo11m_ncnn_model/model.ncnn.bin";
    config.input_width = 640;
    config.input_height = 640;
    config.confidence_threshold = 0.25f;
    config.nms_threshold = 0.45f;
    config.use_gpu = true;
    config.max_detections = 100;
    config.model_type = "detection";
    
    // YOLO COCO class labels (80 classes)
    config.class_labels = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
        "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
        "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
        "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
        "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
        "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair",
        "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
        "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator",
        "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
    };
    
    return config;
}

// Default YOLO pose configuration (using YOLO11m-pose model)
inline DetectionConfig getDefaultPoseConfig() {
    DetectionConfig config;
    config.param_path = "models/yolo11m-pose_ncnn_model/model.ncnn.param";
    config.bin_path = "models/yolo11m-pose_ncnn_model/model.ncnn.bin";
    config.input_width = 640;
    config.input_height = 640;
    config.confidence_threshold = 0.25f;
    config.nms_threshold = 0.45f;
    config.use_gpu = true;
    config.max_detections = 100;
    config.model_type = "pose";
    
    // Pose model only detects people
    config.class_labels = {"person"};
    
    return config;
}

} // namespace detection
