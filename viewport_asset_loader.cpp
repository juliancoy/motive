#include "viewport_asset_loader.h"

#include "viewport_internal_utils.h"
#include "viewport_runtime.h"

#include "engine.h"
#include "model.h"

namespace motive::ui {

bool ViewportAssetLoader::loadModelIntoEngine(ViewportRuntime& runtime, const ViewportHostWidget::SceneItem& item)
{
    if (!runtime.engine() || item.sourcePath.isEmpty() || !detail::isRenderableAsset(item.sourcePath))
    {
        return false;
    }

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

}  // namespace motive::ui
