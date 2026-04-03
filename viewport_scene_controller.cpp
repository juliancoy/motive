#include "viewport_scene_controller.h"

#include "viewport_asset_loader.h"
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
        true
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

    for (auto entry : pendingEntries)
    {
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
        catch (const std::exception& ex)
        {
            qWarning() << "[ViewportHost] Failed to restore scene asset:" << entry.sourcePath << ex.what();
        }
    }
}

}  // namespace motive::ui
