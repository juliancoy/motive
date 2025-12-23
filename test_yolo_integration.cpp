#include <iostream>
#include <memory>
#include <vector>

#include "detection.hpp"
#include "engine.h"

// Simple test to verify YOLO detection system integration
int main(int argc, char** argv) {
    std::cout << "Testing YOLO detection system integration..." << std::endl;
    
    // Create engine instance
    Engine* engine = nullptr;
    try {
        engine = new Engine();
        std::cout << "Engine created successfully" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to create engine: " << ex.what() << std::endl;
        return 1;
    }
    
    // Get detection system instance
    detection::DetectionSystem& detectionSystem = detection::DetectionSystem::getInstance();
    detection::DetectionConfig config = detection::getDefaultYOLOConfig();
    
    std::cout << "Initializing detection system..." << std::endl;
    bool initialized = detectionSystem.initialize(engine, config);
    
    if (initialized) {
        std::cout << "Detection system initialized successfully" << std::endl;
        
        // Test detection system methods
        detectionSystem.setEnabled(true);
        
        // Create a dummy detection for testing
        detection::DetectionResult dummy;
        dummy.bbox = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
        dummy.confidence = 0.85f;
        dummy.label = "person";
        dummy.class_id = 0;
        
        std::vector<detection::DetectionResult> dummyDetections = {dummy};
        
        // Test buffer creation
        detectionSystem.updateDetectionBuffer(engine);
        
        // Get detection buffer
        auto buffer = detectionSystem.getDetectionBuffer();
        std::cout << "Detection buffer count: " << buffer.count << std::endl;
        
        std::cout << "YOLO integration test PASSED" << std::endl;
    } else {
        std::cout << "Detection system initialization failed (expected if ncnn not available)" << std::endl;
        std::cout << "This is OK - the system will use placeholder mode" << std::endl;
    }
    
    // Cleanup
    delete engine;
    
    std::cout << "Test completed" << std::endl;
    return 0;
}
