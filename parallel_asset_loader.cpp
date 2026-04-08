#include "parallel_asset_loader.h"

#include "engine.h"
#include "model.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace motive {

ParallelAssetLoader::ParallelAssetLoader(size_t maxThreads)
    : maxThreads_(maxThreads == 0 ? std::thread::hardware_concurrency() : maxThreads)
{
}

ParallelAssetLoader::~ParallelAssetLoader()
{
    cancel();
}

void ParallelAssetLoader::cancel()
{
    cancelRequested_ = true;
    condition_.notify_all();
    
    for (auto& thread : fileLoadThreads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    fileLoadThreads_.clear();
    
    running_ = false;
}

bool ParallelAssetLoader::isBusy() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !pendingFileLoads_.empty() || !readyForGpu_.empty() || completedCount_ < totalRequests_;
}

size_t ParallelAssetLoader::getPendingFileLoads() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingFileLoads_.size();
}

size_t ParallelAssetLoader::getPendingGpuUploads() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return readyForGpu_.size();
}

size_t ParallelAssetLoader::getCompletedCount() const
{
    return completedCount_.load();
}

void ParallelAssetLoader::fileLoadWorker(Engine* engine)
{
    while (!cancelRequested_)
    {
        std::unique_ptr<PendingModel> pending;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { 
                return cancelRequested_ || !pendingFileLoads_.empty(); 
            });
            
            if (cancelRequested_)
            {
                return;
            }
            
            if (!pendingFileLoads_.empty())
            {
                pending = std::move(pendingFileLoads_.front());
                pendingFileLoads_.pop();
            }
        }
        
        if (pending)
        {
            auto start = std::chrono::steady_clock::now();
            
            // Load file data
            try
            {
                if (std::filesystem::exists(pending->path))
                {
                    std::ifstream file(pending->path, std::ios::binary | std::ios::ate);
                    if (file.is_open())
                    {
                        auto size = file.tellg();
                        file.seekg(0, std::ios::beg);
                        pending->fileData.resize(size);
                        file.read(reinterpret_cast<char*>(pending->fileData.data()), size);
                        pending->fileLoaded = true;
                        
                        // Extract extension
                        pending->fileExtension = std::filesystem::path(pending->path).extension().string();
                        std::transform(pending->fileExtension.begin(), pending->fileExtension.end(), 
                                      pending->fileExtension.begin(), ::tolower);
                        
                        auto end = std::chrono::steady_clock::now();
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                        
                        std::cout << "[ParallelLoader] File loaded: " << pending->path 
                                  << " (" << pending->fileData.size() / 1024 << " KB, " << ms << "ms)"
                                  << std::endl;
                    }
                    else
                    {
                        std::cerr << "[ParallelLoader] Failed to open: " << pending->path << std::endl;
                    }
                }
                else
                {
                    std::cerr << "[ParallelLoader] File not found: " << pending->path << std::endl;
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "[ParallelLoader] Exception loading " << pending->path 
                          << ": " << e.what() << std::endl;
            }
            
            // Move to GPU upload queue
            {
                std::lock_guard<std::mutex> lock(mutex_);
                readyForGpu_.push_back(std::move(pending));
            }
        }
    }
}

void ParallelAssetLoader::startFileLoadThreads(Engine* engine)
{
    if (running_)
    {
        return;
    }
    
    running_ = true;
    cancelRequested_ = false;
    
    for (size_t i = 0; i < maxThreads_; ++i)
    {
        fileLoadThreads_.emplace_back(&ParallelAssetLoader::fileLoadWorker, this, engine);
    }
    
    std::cout << "[ParallelLoader] Started " << maxThreads_ << " file load threads" << std::endl;
}

std::unique_ptr<Model> ParallelAssetLoader::createModelFromData(Engine* engine, PendingModel& pending)
{
    // For now, we still use the Model constructor which does everything
    // In a future optimization, we could:
    // 1. Parse GLTF/FBX from pending.fileData in parallel
    // 2. Create Model with pre-parsed data on main thread
    
    auto model = std::make_unique<Model>(pending.path, engine, pending.options.meshConsolidation);
    
    if (model && pending.options.resizeToUnitBox)
    {
        model->resizeToUnitBox();
    }
    
    if (model)
    {
        model->setSceneTransform(pending.options.translation, 
                                 pending.options.rotation, 
                                 pending.options.scale);
        model->visible = pending.options.visible;
    }
    
    return model;
}

void ParallelAssetLoader::loadModels(Engine* engine,
                                       const std::vector<std::pair<std::string, LoadOptions>>& requests,
                                       ProgressCallback progress,
                                       CompleteCallback complete)
{
    progressCallback_ = std::move(progress);
    completeCallback_ = std::move(complete);
    
    totalRequests_ = requests.size();
    completedCount_ = 0;
    completedResults_.clear();
    completedResults_.reserve(requests.size());
    
    // Add all requests to pending queue
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [path, options] : requests)
        {
            auto pending = std::make_unique<PendingModel>();
            pending->path = path;
            pending->options = options;
            pending->loadStartTime = std::chrono::steady_clock::now();
            pendingFileLoads_.push(std::move(pending));
        }
    }
    
    // Start worker threads
    startFileLoadThreads(engine);
    
    std::cout << "[ParallelLoader] Queued " << requests.size() << " models for loading" << std::endl;
}

void ParallelAssetLoader::loadModel(Engine* engine,
                                      const std::string& path,
                                      const LoadOptions& options,
                                      std::function<void(LoadResult)> callback)
{
    loadModels(engine, {{path, options}}, 
               nullptr,  // No progress callback for single load
               [callback](std::vector<LoadResult> results) {
                   if (!results.empty() && callback)
                   {
                       callback(std::move(results[0]));
                   }
               });
}

void ParallelAssetLoader::processGpuUploads(Engine* engine)
{
    if (!running_ && completedCount_ >= totalRequests_)
    {
        return;
    }
    
    // Get all models ready for GPU upload
    std::vector<std::unique_ptr<PendingModel>> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready = std::move(readyForGpu_);
        readyForGpu_.clear();
    }
    
    // Process GPU uploads on main thread
    for (auto& pending : ready)
    {
        if (cancelRequested_)
        {
            break;
        }
        
        LoadResult result;
        result.path = pending->path;
        result.targetSlot = pending->options.targetSlot;
        
        auto gpuStart = std::chrono::steady_clock::now();
        
        try
        {
            if (pending->fileLoaded)
            {
                result.model = createModelFromData(engine, *pending);
                result.success = (result.model != nullptr);
            }
            else
            {
                result.error = "File failed to load";
            }
        }
        catch (const std::exception& e)
        {
            result.error = e.what();
            std::cerr << "[ParallelLoader] GPU upload failed for " << pending->path 
                      << ": " << e.what() << std::endl;
        }
        
        auto gpuEnd = std::chrono::steady_clock::now();
        result.loadTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(gpuEnd - pending->loadStartTime).count();
        
        completedResults_.push_back(std::move(result));
        completedCount_++;
        
        if (progressCallback_)
        {
            progressCallback_(static_cast<int>(completedCount_), 
                             static_cast<int>(totalRequests_), 
                             pending->path);
        }
    }
    
    // Check if all done
    if (completedCount_ >= totalRequests_ && completeCallback_)
    {
        // Stop file load threads
        cancelRequested_ = true;
        condition_.notify_all();
        
        for (auto& thread : fileLoadThreads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        fileLoadThreads_.clear();
        running_ = false;
        
        // Call completion callback
        completeCallback_(std::move(completedResults_));
        completeCallback_ = nullptr;
        progressCallback_ = nullptr;
    }
}

// ============================================================================
// Global instance
// ============================================================================

ParallelAssetLoader& getAssetLoader()
{
    static ParallelAssetLoader loader;
    return loader;
}

} // namespace motive
