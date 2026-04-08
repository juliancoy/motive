#pragma once

#include "host_widget.h"
#include <vector>
#include <utility>
#include <functional>

namespace motive::ui {

class ViewportRuntime;

class ViewportAssetLoader
{
public:
    static bool loadModelIntoEngine(ViewportRuntime& runtime, const ViewportHostWidget::SceneItem& item);
    static bool loadModelIntoEngineSlot(ViewportRuntime& runtime, int sceneIndex, const ViewportHostWidget::SceneItem& item);
    static void ensureModelSlot(ViewportRuntime& runtime, int sceneIndex);
    
    // Batch load multiple models with parallel loading support
    static bool loadModelsIntoEngineBatch(
        ViewportRuntime& runtime,
        const std::vector<std::pair<int, ViewportHostWidget::SceneItem>>& items,
        std::function<void(int completed, int total)> progressCallback = nullptr);
};

}  // namespace motive::ui
