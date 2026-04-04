#pragma once

#include "viewport_host_widget.h"

namespace motive::ui {

class ViewportRuntime;

class ViewportAssetLoader
{
public:
    static bool loadModelIntoEngine(ViewportRuntime& runtime, const ViewportHostWidget::SceneItem& item);
    static bool loadModelIntoEngineSlot(ViewportRuntime& runtime, int sceneIndex, const ViewportHostWidget::SceneItem& item);
    static void ensureModelSlot(ViewportRuntime& runtime, int sceneIndex);
};

}  // namespace motive::ui
