#include "scene_controller.h"

#include "asset_loader.h"
#include "viewport_internal_utils.h"
#include "viewport_runtime.h"

#include "engine.h"
#include "model.h"

#include <QDebug>
#include <QFileInfo>
#include <vulkan/vulkan.h>

namespace motive::ui {

ViewportSceneController::ViewportSceneController(ViewportRuntime& runtime)
    : m_runtime(runtime)
{
}

void ViewportSceneController::loadAssetFromPath(const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }

    if (m_runtime.engine())
    {
        m_runtime.engine()->models.clear();
    }
    m_sceneEntries.clear();
    m_pendingSceneEntries.clear();
    m_pendingSceneEntries.push_back(ViewportHostWidget::SceneItem{
        QFileInfo(path).completeBaseName(),
        QFileInfo(path).absoluteFilePath(),
        m_meshConsolidationEnabled,
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(-90.0f, 0.0f, 0.0f),
        QVector3D(1.0f, 1.0f, 1.0f),
        false,
        QVector3D(1.0f, 0.0f, 1.0f),
        QString(),
        true,
        true,
        1.0f,
        true,
        QStringLiteral("AnimationOnly"),  // animationPhysicsCoupling
        true,  // useGravity
        QVector3D(0.0f, 0.0f, 0.0f)  // customGravity
    });
    m_currentAssetPath = path;
    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }
    addAssetToScene(path);
    m_pendingSceneEntries.clear();
}

void ViewportSceneController::loadSceneFromItems(const QList<ViewportHostWidget::SceneItem>& items)
{
    if (m_runtime.engine())
    {
        m_runtime.engine()->models.clear();
    }
    m_sceneEntries.clear();
    m_pendingSceneEntries.clear();

    for (const auto& item : items)
    {
        m_pendingSceneEntries.push_back(item);
    }
    m_currentAssetPath = items.isEmpty() ? QString() : items.back().sourcePath;

    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }

    restorePendingEntries();
}

void ViewportSceneController::addAssetToScene(const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }

    m_currentAssetPath = path;
    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }

    try
    {
        ViewportHostWidget::SceneItem entry{
            QFileInfo(path).completeBaseName(),
            QFileInfo(path).absoluteFilePath(),
            m_meshConsolidationEnabled,
            QVector3D(static_cast<float>(m_runtime.engine()->models.size()) * 1.6f, 0.0f, 0.0f),
            QVector3D(-90.0f, 0.0f, 0.0f),
            QVector3D(1.0f, 1.0f, 1.0f),
            false,
            QVector3D(1.0f, 0.0f, 1.0f),
            QString(),
            true,
            true,
            1.0f,
            true
        };
        ViewportAssetLoader::loadModelIntoEngine(m_runtime, entry);
        if (!m_runtime.engine()->models.empty() && m_runtime.engine()->models.back() && entry.activeAnimationClip.isEmpty())
        {
            const auto& clips = m_runtime.engine()->models.back()->animationClips;
            if (!clips.empty())
            {
                entry.activeAnimationClip = QString::fromStdString(clips.front().name);
            }
        }
        m_sceneEntries.push_back(entry);
    }
    catch (const std::exception& ex)
    {
        qWarning() << "[ViewportHost] Failed to add scene asset:" << path << ex.what();
    }
}

QString ViewportSceneController::currentAssetPath() const
{
    return m_currentAssetPath;
}

QList<ViewportHostWidget::SceneItem> ViewportSceneController::sceneItems() const
{
    QList<ViewportHostWidget::SceneItem> items = m_sceneEntries;
    for (const auto& entry : m_pendingSceneEntries)
    {
        items.push_back(entry);
    }
    return items;
}

bool ViewportSceneController::meshConsolidationEnabled() const
{
    return m_meshConsolidationEnabled;
}

void ViewportSceneController::setMeshConsolidationEnabled(bool enabled)
{
    if (m_meshConsolidationEnabled == enabled)
    {
        return;
    }

    m_meshConsolidationEnabled = enabled;
    const QList<ViewportHostWidget::SceneItem> items = sceneItems();

    if (m_runtime.engine())
    {
        m_runtime.engine()->models.clear();
    }
    m_sceneEntries.clear();
    m_pendingSceneEntries.clear();

    for (const auto& item : items)
    {
        m_pendingSceneEntries.push_back(item);
    }

    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }

    restorePendingEntries();
}

void ViewportSceneController::updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].translation = translation;
    m_sceneEntries[index].rotation = rotation;
    m_sceneEntries[index].scale = scale;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        auto& model = m_runtime.engine()->models[static_cast<size_t>(index)];
        model->setSceneTransform(glm::vec3(translation.x(), translation.y(), translation.z()),
                                 glm::vec3(rotation.x(), rotation.y(), rotation.z()),
                                 glm::vec3(scale.x(), scale.y(), scale.z()));
        if (auto* body = model->getPhysicsBody())
        {
            body->syncTransformToPhysics();
        }
    }
}

void ViewportSceneController::setSceneItemMeshConsolidationEnabled(int index, bool enabled)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    if (m_sceneEntries[index].meshConsolidationEnabled == enabled)
    {
        return;
    }

    m_sceneEntries[index].meshConsolidationEnabled = enabled;
    if (!m_runtime.engine())
    {
        return;
    }

    ViewportAssetLoader::ensureModelSlot(m_runtime, index);
    if (index < static_cast<int>(m_runtime.engine()->models.size()) && m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        if (m_runtime.engine()->logicalDevice != VK_NULL_HANDLE)
        {
            vkQueueWaitIdle(m_runtime.engine()->getGraphicsQueue());
            vkDeviceWaitIdle(m_runtime.engine()->logicalDevice);
        }
        m_runtime.engine()->models[static_cast<size_t>(index)].reset();
    }

    if (m_sceneEntries[index].visible)
    {
        try
        {
            ViewportAssetLoader::loadModelIntoEngineSlot(m_runtime, index, m_sceneEntries[index]);
            if (m_runtime.engine()->models[static_cast<size_t>(index)] && m_sceneEntries[index].activeAnimationClip.isEmpty())
            {
                const auto& clips = m_runtime.engine()->models[static_cast<size_t>(index)]->animationClips;
                if (!clips.empty())
                {
                    m_sceneEntries[index].activeAnimationClip = QString::fromStdString(clips.front().name);
                }
            }
        }
        catch (const std::exception& ex)
        {
            qWarning() << "[ViewportHost] Failed to reload scene asset after load-parameter change:" << m_sceneEntries[index].sourcePath << ex.what();
        }
    }
}

void ViewportSceneController::updateSceneItemPaintOverride(int index, bool enabled, const QVector3D& color)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].paintOverrideEnabled = enabled;
    m_sceneEntries[index].paintOverrideColor = color;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->setPaintOverride(enabled, glm::vec3(color.x(), color.y(), color.z()));
    }
}

void ViewportSceneController::updateSceneItemAnimationState(int index, const QString& activeClip, bool playing, bool loop, float speed)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    m_sceneEntries[index].activeAnimationClip = activeClip;
    m_sceneEntries[index].animationPlaying = playing;
    m_sceneEntries[index].animationLoop = loop;
    m_sceneEntries[index].animationSpeed = speed;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->setAnimationPlaybackState(activeClip.toStdString(), playing, loop, speed);
    }
}

void ViewportSceneController::updateSceneItemAnimationPhysicsCoupling(int index, const QString& couplingMode)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    m_sceneEntries[index].animationPhysicsCoupling = couplingMode;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->setAnimationPhysicsCoupling(couplingMode.toStdString());
    }
}

void ViewportSceneController::updateSceneItemPhysicsGravity(int index, bool useGravity, const QVector3D& customGravity)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    m_sceneEntries[index].useGravity = useGravity;
    m_sceneEntries[index].customGravity = customGravity;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        auto& model = m_runtime.engine()->models[static_cast<size_t>(index)];
        model->setUseGravity(useGravity);
        model->setCustomGravity(glm::vec3(customGravity.x(), customGravity.y(), customGravity.z()));
        
        // If physics body already exists, update it
        if (auto* body = model->getPhysicsBody())
        {
            body->setUseGravity(useGravity);
            if (customGravity.x() != 0.0f || customGravity.y() != 0.0f || customGravity.z() != 0.0f)
            {
                body->setCustomGravity(glm::vec3(customGravity.x(), customGravity.y(), customGravity.z()));
            }
        }
    }
}

void ViewportSceneController::renameSceneItem(int index, const QString& name)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
    {
        m_sceneEntries[index].name = trimmed;
    }
}

void ViewportSceneController::setSceneItemVisible(int index, bool visible)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].visible = visible;
    if (!m_runtime.engine())
    {
        return;
    }

    ViewportAssetLoader::ensureModelSlot(m_runtime, index);
    if (visible && !m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        try
        {
            ViewportAssetLoader::loadModelIntoEngineSlot(m_runtime, index, m_sceneEntries[index]);
            if (m_runtime.engine()->models[static_cast<size_t>(index)] && m_sceneEntries[index].activeAnimationClip.isEmpty())
            {
                const auto& clips = m_runtime.engine()->models[static_cast<size_t>(index)]->animationClips;
                if (!clips.empty())
                {
                    m_sceneEntries[index].activeAnimationClip = QString::fromStdString(clips.front().name);
                }
            }
        }
        catch (const std::exception& ex)
        {
            qWarning() << "[ViewportHost] Failed to lazy-load hidden scene asset:" << m_sceneEntries[index].sourcePath << ex.what();
            m_sceneEntries[index].visible = false;
        }
    }
    if (index < static_cast<int>(m_runtime.engine()->models.size()) && m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->visible = m_sceneEntries[index].visible;
    }
}

void ViewportSceneController::deleteSceneItem(int index)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries.removeAt(index);
    if (m_runtime.engine() && index < static_cast<int>(m_runtime.engine()->models.size()))
    {
        if (m_runtime.engine()->logicalDevice != VK_NULL_HANDLE)
        {
            vkQueueWaitIdle(m_runtime.engine()->getGraphicsQueue());
            vkDeviceWaitIdle(m_runtime.engine()->logicalDevice);
        }
        m_runtime.engine()->models.erase(m_runtime.engine()->models.begin() + index);
    }

    if (m_sceneEntries.isEmpty())
    {
        m_currentAssetPath.clear();
    }
    else if (index - 1 >= 0)
    {
        m_currentAssetPath = m_sceneEntries[index - 1].sourcePath;
    }
    else
    {
        m_currentAssetPath = m_sceneEntries.front().sourcePath;
    }
}

QList<ViewportHostWidget::SceneItem>& ViewportSceneController::loadedEntries()
{
    return m_sceneEntries;
}

const QList<ViewportHostWidget::SceneItem>& ViewportSceneController::loadedEntries() const
{
    return m_sceneEntries;
}

QList<ViewportHostWidget::SceneItem>& ViewportSceneController::pendingEntries()
{
    return m_pendingSceneEntries;
}

const QList<ViewportHostWidget::SceneItem>& ViewportSceneController::pendingEntries() const
{
    return m_pendingSceneEntries;
}

void ViewportSceneController::restorePendingEntries()
{
    const QList<ViewportHostWidget::SceneItem> pendingEntries = m_pendingSceneEntries;
    m_pendingSceneEntries.clear();

    // Track total loading time
    auto totalStart = std::chrono::high_resolution_clock::now();
    
    // Use batch loading for multiple entries when parallel loading is enabled
    // TEMPORARILY DISABLED - testing crash fix
    if (false && pendingEntries.size() > 1 && 
        m_runtime.engine() && 
        m_runtime.engine()->isParallelModelLoadingEnabled())
    {
        std::vector<std::pair<int, ViewportHostWidget::SceneItem>> items;
        items.reserve(pendingEntries.size());
        
        int startIndex = m_sceneEntries.size();
        for (int i = 0; i < pendingEntries.size(); ++i)
        {
            items.push_back({startIndex + i, pendingEntries[i]});
        }
        
        qDebug() << "[ViewportSceneController] Batch loading" << items.size() << "models with parallel loader";
        
        // Load all models in parallel
        ViewportAssetLoader::loadModelsIntoEngineBatch(m_runtime, items,
            [](int completed, int total) {
                qDebug() << "[ViewportSceneController] Loading progress:" << completed << "/" << total;
            });
        
        // Update scene entries with animation clip info
        for (int i = 0; i < pendingEntries.size(); ++i)
        {
            auto entry = pendingEntries[i];
            const int sceneIndex = startIndex + i;
            
            if (m_runtime.engine() &&
                sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
                m_runtime.engine()->models[static_cast<size_t>(sceneIndex)] &&
                entry.activeAnimationClip.isEmpty())
            {
                const auto& clips = m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]->animationClips;
                if (!clips.empty())
                {
                    entry.activeAnimationClip = QString::fromStdString(clips.front().name);
                }
            }
            m_sceneEntries.push_back(entry);
        }
        
        auto totalEnd = std::chrono::high_resolution_clock::now();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();
        qDebug() << "[ViewportSceneController] PARALLEL load complete:" << items.size() << "models in" << totalMs << "ms";
    }
    else
    {
        // Sequential loading (single item or parallel disabled)
        qDebug() << "[ViewportSceneController] Sequential loading" << pendingEntries.size() << "models";
        
        for (auto entry : pendingEntries)
        {
            auto modelStart = std::chrono::high_resolution_clock::now();
            
            try
            {
                const int sceneIndex = m_sceneEntries.size();
                if (entry.visible)
                {
                    ViewportAssetLoader::loadModelIntoEngineSlot(m_runtime, sceneIndex, entry);
                }
                else
                {
                    ViewportAssetLoader::ensureModelSlot(m_runtime, sceneIndex);
                }

                auto modelEnd = std::chrono::high_resolution_clock::now();
                auto modelMs = std::chrono::duration_cast<std::chrono::milliseconds>(modelEnd - modelStart).count();
                qDebug() << "[ViewportSceneController] Sequential load:" << entry.name << "took" << modelMs << "ms";

                if (m_runtime.engine() &&
                    sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
                    m_runtime.engine()->models[static_cast<size_t>(sceneIndex)] &&
                    entry.activeAnimationClip.isEmpty())
                {
                    const auto& clips = m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]->animationClips;
                    if (!clips.empty())
                    {
                        entry.activeAnimationClip = QString::fromStdString(clips.front().name);
                    }
                }
                
                // Apply physics coupling mode if not the default
                if (!entry.animationPhysicsCoupling.isEmpty() && entry.animationPhysicsCoupling != QStringLiteral("AnimationOnly"))
                {
                    updateSceneItemAnimationPhysicsCoupling(sceneIndex, entry.animationPhysicsCoupling);
                }
                
                m_sceneEntries.push_back(entry);
            }
            catch (const std::exception& ex)
            {
                qWarning() << "[ViewportHost] Failed to restore scene asset:" << entry.sourcePath << ex.what();
            }
        }
        
        auto totalEnd = std::chrono::high_resolution_clock::now();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();
        qDebug() << "[ViewportSceneController] SEQUENTIAL load complete:" << pendingEntries.size() << "models in" << totalMs << "ms";
    }
}

}  // namespace motive::ui
