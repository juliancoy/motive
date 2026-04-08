#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <functional>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>

#include <glm/glm.hpp>

class Engine;
class Model;

namespace motive {

// High-performance parallel asset loading system
// 
// Architecture:
// 1. Phase 1 (Parallel): File I/O + Parse (CPU-bound)
// 2. Phase 2 (Main Thread): GPU Resource Creation (Vulkan-bound)
//
// This maximizes throughput by keeping all CPU cores busy during file loading
// while ensuring Vulkan calls happen on the main thread.

class ParallelAssetLoader {
public:
    struct LoadOptions {
        glm::vec3 translation;
        glm::vec3 rotation;  // degrees
        glm::vec3 scale;
        bool resizeToUnitBox;
        bool meshConsolidation;
        bool visible;
        int targetSlot;  // -1 = append to models list
        
        LoadOptions() 
            : translation(0.0f)
            , rotation(0.0f)
            , scale(1.0f)
            , resizeToUnitBox(true)
            , meshConsolidation(true)
            , visible(true)
            , targetSlot(-1)
        {}
    };

    struct PendingModel {
        std::string path;
        LoadOptions options;
        std::vector<uint8_t> fileData;  // Loaded file bytes
        std::string fileExtension;
        bool fileLoaded = false;
        std::chrono::steady_clock::time_point loadStartTime;
    };

    struct LoadResult {
        std::string path;
        std::unique_ptr<Model> model;
        std::string error;
        bool success = false;
        int targetSlot = -1;
        int64_t loadTimeMs = 0;
    };

    using ProgressCallback = std::function<void(int completed, int total, const std::string& currentFile)>;
    using CompleteCallback = std::function<void(std::vector<LoadResult> results)>;

    explicit ParallelAssetLoader(size_t maxThreads = 0);  // 0 = hardware concurrency
    ~ParallelAssetLoader();

    // Non-copyable
    ParallelAssetLoader(const ParallelAssetLoader&) = delete;
    ParallelAssetLoader& operator=(const ParallelAssetLoader&) = delete;

    // Load multiple models
    void loadModels(Engine* engine,
                    const std::vector<std::pair<std::string, LoadOptions>>& requests,
                    ProgressCallback progress = nullptr,
                    CompleteCallback complete = nullptr);

    // Load single model (convenience)
    void loadModel(Engine* engine,
                   const std::string& path,
                   const LoadOptions& options = LoadOptions(),
                   std::function<void(LoadResult)> callback = nullptr);

    // Process GPU uploads - call this from main thread every frame
    void processGpuUploads(Engine* engine);

    // Check if any loads are pending or in progress
    bool isBusy() const;
    
    // Get current status
    size_t getPendingFileLoads() const;
    size_t getPendingGpuUploads() const;
    size_t getCompletedCount() const;

    // Cancel all pending operations
    void cancel();

private:
    void fileLoadWorker(Engine* engine);
    void startFileLoadThreads(Engine* engine);
    
    std::unique_ptr<Model> createModelFromData(Engine* engine, PendingModel& pending);

    std::vector<std::thread> fileLoadThreads_;
    std::queue<std::unique_ptr<PendingModel>> pendingFileLoads_;
    std::vector<std::unique_ptr<PendingModel>> readyForGpu_;
    std::vector<LoadResult> completedResults_;
    
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<size_t> totalRequests_{0};
    std::atomic<size_t> completedCount_{0};
    
    ProgressCallback progressCallback_;
    CompleteCallback completeCallback_;
    
    size_t maxThreads_;
};

// Global instance for easy access
ParallelAssetLoader& getAssetLoader();

} // namespace motive
