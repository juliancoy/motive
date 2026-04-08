#include "asset_loader.h"

#include "viewport_internal_utils.h"
#include "viewport_runtime.h"

#include "engine.h"
#include "model.h"
#include "parallel_asset_loader.h"

#include <iostream>
#include <chrono>
#include <thread>

namespace motive::ui {

bool ViewportAssetLoader::loadModelIntoEngine(ViewportRuntime& runtime, const ViewportHostWidget::SceneItem& item)
{
    if (!runtime.engine() || item.sourcePath.isEmpty() || !detail::isRenderableAsset(item.sourcePath))
    {
        return false;
    }

    // Use parallel loading if enabled
    if (runtime.engine()->isParallelModelLoadingEnabled())
    {
        auto start = std::chrono::steady_clock::now();
        
        std::vector<std::pair<std::string, ParallelAssetLoader::LoadOptions>> requests;
        ParallelAssetLoader::LoadOptions opts;
        opts.translation = glm::vec3(item.translation.x(), item.translation.y(), item.translation.z());
        opts.rotation = glm::vec3(item.rotation.x(), item.rotation.y(), item.rotation.z());
        opts.scale = glm::vec3(item.scale.x(), item.scale.y(), item.scale.z());
        opts.meshConsolidation = item.meshConsolidationEnabled;
        opts.visible = item.visible;
        
        requests.push_back({item.sourcePath.toStdString(), opts});
        
        std::atomic<bool> loadComplete{false};
        ParallelAssetLoader::LoadResult result;
        
        getAssetLoader().loadModels(runtime.engine(), requests,
            nullptr,  // No progress callback for single model
            [&result, &loadComplete](std::vector<ParallelAssetLoader::LoadResult> results) {
                if (!results.empty()) {
                    result = std::move(results[0]);
                }
                loadComplete = true;
            });
        
        // Process GPU uploads until complete (blocking for now)
        while (!loadComplete) {
            getAssetLoader().processGpuUploads(runtime.engine());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        if (result.success && result.model) {
            std::cout << "[ViewportAssetLoader] Model loaded in " << ms << "ms (parallel)" << std::endl;
            
            // Apply paint override if needed
            if (item.paintOverrideEnabled) {
                result.model->setPaintOverride(true, glm::vec3(
                    item.paintOverrideColor.x(), 
                    item.paintOverrideColor.y(), 
                    item.paintOverrideColor.z()));
            }
            
            runtime.engine()->addModel(std::move(result.model));
            return true;
        } else {
            std::cerr << "[ViewportAssetLoader] Parallel load failed: " << result.error << std::endl;
            // Fall through to synchronous loading
        }
    }
    
    // Synchronous loading (fallback or when parallel is disabled)
    auto model = std::make_unique<Model>(item.sourcePath.toStdString(), runtime.engine(), item.meshConsolidationEnabled);
    model->resizeToUnitBox();
    model->setSceneTransform(glm::vec3(item.translation.x(), item.translation.y(), item.translation.z()),
                             glm::vec3(item.rotation.x(), item.rotation.y(), item.rotation.z()),
                             glm::vec3(item.scale.x(), item.scale.y(), item.scale.z()));
    model->setPaintOverride(item.paintOverrideEnabled,
                            glm::vec3(item.paintOverrideColor.x(), item.paintOverrideColor.y(), item.paintOverrideColor.z()));
    model->visible = item.visible;
    runtime.engine()->addModel(std::move(model));
    return true;
}

void ViewportAssetLoader::ensureModelSlot(ViewportRuntime& runtime, int sceneIndex)
{
    if (!runtime.engine() || sceneIndex < 0)
    {
        return;
    }
    if (sceneIndex >= static_cast<int>(runtime.engine()->models.size()))
    {
        runtime.engine()->models.resize(static_cast<size_t>(sceneIndex) + 1);
    }
}

bool ViewportAssetLoader::loadModelIntoEngineSlot(ViewportRuntime& runtime, int sceneIndex, const ViewportHostWidget::SceneItem& item)
{
    if (!runtime.engine() || sceneIndex < 0 || item.sourcePath.isEmpty() || !detail::isRenderableAsset(item.sourcePath))
    {
        return false;
    }

    // Use parallel loading if enabled
    if (runtime.engine()->isParallelModelLoadingEnabled())
    {
        auto start = std::chrono::steady_clock::now();
        
        std::vector<std::pair<std::string, ParallelAssetLoader::LoadOptions>> requests;
        ParallelAssetLoader::LoadOptions opts;
        opts.translation = glm::vec3(item.translation.x(), item.translation.y(), item.translation.z());
        opts.rotation = glm::vec3(item.rotation.x(), item.rotation.y(), item.rotation.z());
        opts.scale = glm::vec3(item.scale.x(), item.scale.y(), item.scale.z());
        opts.meshConsolidation = item.meshConsolidationEnabled;
        opts.visible = item.visible;
        opts.targetSlot = sceneIndex;
        
        requests.push_back({item.sourcePath.toStdString(), opts});
        
        std::atomic<bool> loadComplete{false};
        ParallelAssetLoader::LoadResult result;
        
        getAssetLoader().loadModels(runtime.engine(), requests,
            nullptr,
            [&result, &loadComplete](std::vector<ParallelAssetLoader::LoadResult> results) {
                if (!results.empty()) {
                    result = std::move(results[0]);
                }
                loadComplete = true;
            });
        
        // Process GPU uploads until complete
        while (!loadComplete) {
            getAssetLoader().processGpuUploads(runtime.engine());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        if (result.success && result.model) {
            std::cout << "[ViewportAssetLoader] Model slot " << sceneIndex << " loaded in " << ms << "ms (parallel)" << std::endl;
            
            if (item.paintOverrideEnabled) {
                result.model->setPaintOverride(true, glm::vec3(
                    item.paintOverrideColor.x(), 
                    item.paintOverrideColor.y(), 
                    item.paintOverrideColor.z()));
            }
            
            ensureModelSlot(runtime, sceneIndex);
            runtime.engine()->models[static_cast<size_t>(sceneIndex)] = std::move(result.model);
            return true;
        } else {
            std::cerr << "[ViewportAssetLoader] Parallel load failed for slot " << sceneIndex << ": " << result.error << std::endl;
            // Fall through to synchronous loading
        }
    }
    
    // Synchronous loading
    auto model = std::make_unique<Model>(item.sourcePath.toStdString(), runtime.engine(), item.meshConsolidationEnabled);
    model->resizeToUnitBox();
    model->setSceneTransform(glm::vec3(item.translation.x(), item.translation.y(), item.translation.z()),
                             glm::vec3(item.rotation.x(), item.rotation.y(), item.rotation.z()),
                             glm::vec3(item.scale.x(), item.scale.y(), item.scale.z()));
    model->setPaintOverride(item.paintOverrideEnabled,
                            glm::vec3(item.paintOverrideColor.x(), item.paintOverrideColor.y(), item.paintOverrideColor.z()));
    model->visible = item.visible;

    ensureModelSlot(runtime, sceneIndex);
    runtime.engine()->models[static_cast<size_t>(sceneIndex)] = std::move(model);
    return true;
}

// Batch load multiple models in parallel
bool ViewportAssetLoader::loadModelsIntoEngineBatch(
    ViewportRuntime& runtime,
    const std::vector<std::pair<int, ViewportHostWidget::SceneItem>>& items,
    std::function<void(int completed, int total)> progressCallback)
{
    if (!runtime.engine() || items.empty())
    {
        return false;
    }
    
    // Only use parallel loading if enabled
    if (!runtime.engine()->isParallelModelLoadingEnabled())
    {
        // Fall back to sequential loading with timing
        auto seqStart = std::chrono::high_resolution_clock::now();
        int completed = 0;
        for (const auto& [index, item] : items)
        {
            auto itemStart = std::chrono::high_resolution_clock::now();
            if (index >= 0)
            {
                loadModelIntoEngineSlot(runtime, index, item);
            }
            else
            {
                loadModelIntoEngine(runtime, item);
            }
            auto itemEnd = std::chrono::high_resolution_clock::now();
            auto itemMs = std::chrono::duration_cast<std::chrono::milliseconds>(itemEnd - itemStart).count();
            std::cout << "[ViewportAssetLoader] Sequential: " << item.name.toStdString() << " took " << itemMs << "ms" << std::endl;
            
            completed++;
            if (progressCallback)
            {
                progressCallback(completed, static_cast<int>(items.size()));
            }
        }
        auto seqEnd = std::chrono::high_resolution_clock::now();
        auto seqTotalMs = std::chrono::duration_cast<std::chrono::milliseconds>(seqEnd - seqStart).count();
        std::cout << "[ViewportAssetLoader] SEQUENTIAL total: " << items.size() << " models in " << seqTotalMs << "ms" << std::endl;
        return true;
    }
    
    // Parallel batch loading with detailed timing
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::pair<std::string, ParallelAssetLoader::LoadOptions>> requests;
    requests.reserve(items.size());
    
    for (const auto& [index, item] : items)
    {
        ParallelAssetLoader::LoadOptions opts;
        opts.translation = glm::vec3(item.translation.x(), item.translation.y(), item.translation.z());
        opts.rotation = glm::vec3(item.rotation.x(), item.rotation.y(), item.rotation.z());
        opts.scale = glm::vec3(item.scale.x(), item.scale.y(), item.scale.z());
        opts.meshConsolidation = item.meshConsolidationEnabled;
        opts.visible = item.visible;
        opts.targetSlot = index;
        
        requests.push_back({item.sourcePath.toStdString(), opts});
    }
    
    std::atomic<bool> loadComplete{false};
    std::vector<ParallelAssetLoader::LoadResult> results;
    
    auto queueStart = std::chrono::high_resolution_clock::now();
    getAssetLoader().loadModels(runtime.engine(), requests,
        [&progressCallback](int completed, int total, const std::string&) {
            if (progressCallback)
            {
                progressCallback(completed, total);
            }
        },
        [&results, &loadComplete](std::vector<ParallelAssetLoader::LoadResult> loaded) {
            results = std::move(loaded);
            loadComplete = true;
        });
    auto queueEnd = std::chrono::high_resolution_clock::now();
    auto queueMs = std::chrono::duration_cast<std::chrono::milliseconds>(queueEnd - queueStart).count();
    std::cout << "[ViewportAssetLoader] Parallel: queued " << items.size() << " models in " << queueMs << "ms" << std::endl;
    
    // Process GPU uploads until complete
    auto gpuStart = std::chrono::high_resolution_clock::now();
    int gpuIterations = 0;
    while (!loadComplete) {
        getAssetLoader().processGpuUploads(runtime.engine());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        gpuIterations++;
    }
    auto gpuEnd = std::chrono::high_resolution_clock::now();
    auto gpuMs = std::chrono::duration_cast<std::chrono::milliseconds>(gpuEnd - gpuStart).count();
    std::cout << "[ViewportAssetLoader] Parallel: GPU uploads took " << gpuMs << "ms (" << gpuIterations << " iterations)" << std::endl;
    
    auto end = std::chrono::high_resolution_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Place models in their slots
    int successCount = 0;
    for (auto& result : results)
    {
        if (result.success && result.model)
        {
            // Find the original item to apply paint override
            for (const auto& [index, item] : items)
            {
                if (item.sourcePath.toStdString() == result.path)
                {
                    if (item.paintOverrideEnabled)
                    {
                        result.model->setPaintOverride(true, glm::vec3(
                            item.paintOverrideColor.x(),
                            item.paintOverrideColor.y(),
                            item.paintOverrideColor.z()));
                    }
                    break;
                }
            }
            
            if (result.targetSlot >= 0)
            {
                ensureModelSlot(runtime, result.targetSlot);
                runtime.engine()->models[static_cast<size_t>(result.targetSlot)] = std::move(result.model);
            }
            else
            {
                runtime.engine()->addModel(std::move(result.model));
            }
            successCount++;
        }
    }
    
    std::cout << "[ViewportAssetLoader] PARALLEL total: " << successCount << "/" << items.size() 
              << " models in " << totalMs << "ms (file I/O: " << queueMs << "ms, GPU: " << gpuMs << "ms)" << std::endl;
    
    return successCount > 0;
}

}  // namespace motive::ui
