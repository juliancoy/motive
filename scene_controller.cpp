#include "scene_controller.h"

#include "asset_loader.h"
#include "text_mesh_extrusion.h"
#include "viewport_internal_utils.h"
#include "viewport_runtime.h"

#include "engine.h"
#include "model.h"
#include "primitive.h"

#include <QColor>
#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>
#include <cstring>

namespace motive::ui {

namespace {

bool isTextOverlaySceneItem(const ViewportHostWidget::SceneItem& entry)
{
    return entry.sourcePath.startsWith(QStringLiteral("text://"), Qt::CaseInsensitive);
}

bool isCoordinatePlaneIndicatorSceneItem(const ViewportHostWidget::SceneItem& entry)
{
    return entry.sourcePath.startsWith(QStringLiteral("planes://"), Qt::CaseInsensitive);
}

void canonicalizeCoordinatePlaneIndicatorItem(ViewportHostWidget::SceneItem& item)
{
    item.translation = QVector3D(0.0f, 0.0f, 0.0f);
    item.rotation = QVector3D(0.0f, 0.0f, 0.0f);
    item.scale = QVector3D(1.0f, 1.0f, 1.0f);
}

void appendStrip(std::vector<Vertex>& vertices,
                 const glm::vec3& a,
                 const glm::vec3& b,
                 const glm::vec3& normal,
                 const glm::vec3& halfWidth)
{
    const glm::vec3 p0 = a - halfWidth;
    const glm::vec3 p1 = a + halfWidth;
    const glm::vec3 p2 = b + halfWidth;
    const glm::vec3 p3 = b - halfWidth;
    const glm::vec2 uv0(0.0f, 0.0f);
    const glm::vec2 uv1(1.0f, 0.0f);
    const glm::vec2 uv2(1.0f, 1.0f);
    const glm::vec2 uv3(0.0f, 1.0f);
    vertices.push_back(Vertex{p0, normal, uv0});
    vertices.push_back(Vertex{p1, normal, uv1});
    vertices.push_back(Vertex{p2, normal, uv2});
    vertices.push_back(Vertex{p0, normal, uv0});
    vertices.push_back(Vertex{p2, normal, uv2});
    vertices.push_back(Vertex{p3, normal, uv3});
}

std::vector<Vertex> buildCoordinatePlaneGrid(const QString& plane)
{
    std::vector<Vertex> vertices;
    constexpr float kExtent = 60.0f;
    constexpr int kDivisions = 24;
    constexpr float kThin = 0.035f;
    constexpr float kBold = 0.09f;

    auto makePoint = [&](float u, float v) {
        if (plane == QStringLiteral("xy"))
        {
            return glm::vec3(u, v, 0.0f);
        }
        if (plane == QStringLiteral("yz"))
        {
            return glm::vec3(0.0f, u, v);
        }
        return glm::vec3(u, 0.0f, v);
    };
    const glm::vec3 normal = plane == QStringLiteral("xy")
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : (plane == QStringLiteral("yz") ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 uHalfWidth = plane == QStringLiteral("xy")
        ? glm::vec3(0.0f, kThin, 0.0f)
        : (plane == QStringLiteral("yz") ? glm::vec3(0.0f, kThin, 0.0f) : glm::vec3(0.0f, 0.0f, kThin));
    const glm::vec3 vHalfWidth = plane == QStringLiteral("xy")
        ? glm::vec3(kThin, 0.0f, 0.0f)
        : (plane == QStringLiteral("yz") ? glm::vec3(0.0f, 0.0f, kThin) : glm::vec3(kThin, 0.0f, 0.0f));

    for (int i = -kDivisions; i <= kDivisions; ++i)
    {
        const float coord = (static_cast<float>(i) / static_cast<float>(kDivisions)) * kExtent;
        const float width = (i == 0) ? kBold : kThin;
        const glm::vec3 uWidth = uHalfWidth * (width / kThin);
        const glm::vec3 vWidth = vHalfWidth * (width / kThin);
        appendStrip(vertices, makePoint(-kExtent, coord), makePoint(kExtent, coord), normal, uWidth);
        appendStrip(vertices, makePoint(coord, -kExtent), makePoint(coord, kExtent), normal, vWidth);
    }
    return vertices;
}

void applyCoordinatePlaneIndicatorToModel(ViewportRuntime& runtime, int sceneIndex, const ViewportHostWidget::SceneItem& entry)
{
    if (!runtime.engine() || sceneIndex < 0)
    {
        return;
    }
    const QString plane = entry.sourcePath.mid(QStringLiteral("planes://").size()).toLower();
    const std::vector<Vertex> vertices = buildCoordinatePlaneGrid(plane);
    if (vertices.empty())
    {
        return;
    }

    ViewportAssetLoader::ensureModelSlot(runtime, sceneIndex);
    auto model = std::make_unique<Model>(vertices, runtime.engine());
    model->name = entry.name.toStdString();
    model->setSceneTransform(glm::vec3(0.0f),
                             glm::vec3(0.0f),
                             glm::vec3(1.0f));
    model->visible = entry.visible;
    const glm::vec3 color = plane == QStringLiteral("xy")
        ? glm::vec3(0.15f, 0.45f, 1.0f)
        : (plane == QStringLiteral("yz") ? glm::vec3(1.0f, 0.25f, 0.25f) : glm::vec3(0.2f, 0.9f, 0.35f));
    for (auto& mesh : model->meshes)
    {
        for (auto& primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }
            primitive->cullMode = PrimitiveCullMode::Disabled;
            primitive->alphaMode = PrimitiveAlphaMode::Blend;
            primitive->depthTestEnabled = true;
            primitive->depthWriteEnabled = false;
        }
    }
    model->setPaintOverride(true, color);
    runtime.engine()->models[static_cast<size_t>(sceneIndex)] = std::move(model);
}

ViewportHostWidget::SceneItem makeCoordinatePlaneIndicatorItem(const QString& plane, bool enabled)
{
    ViewportHostWidget::SceneItem item;
    item.name = plane.toUpper() + QStringLiteral(" Plane");
    item.sourcePath = QStringLiteral("planes://") + plane;
    item.meshConsolidationEnabled = false;
    canonicalizeCoordinatePlaneIndicatorItem(item);
    item.visible = enabled;
    item.paintOverrideEnabled = false;
    item.animationPlaying = false;
    return item;
}

glm::vec3 vec3ColorFromString(const QString& value, const QColor& fallback)
{
    QColor c(value);
    if (!c.isValid())
    {
        c = fallback;
    }
    return glm::vec3(static_cast<float>(c.redF()),
                     static_cast<float>(c.greenF()),
                     static_cast<float>(c.blueF()));
}

void applyTextSceneItemToModel(ViewportRuntime& runtime, int sceneIndex, const ViewportHostWidget::SceneItem& entry)
{
    if (!runtime.engine() || sceneIndex < 0)
    {
        return;
    }

    ViewportAssetLoader::ensureModelSlot(runtime, sceneIndex);
    if (sceneIndex >= static_cast<int>(runtime.engine()->models.size()))
    {
        return;
    }

    motive::text::FontRenderOptions fontOptions;
    fontOptions.fontPath = entry.textFontPath.toStdString();
    fontOptions.bold = entry.textBold;
    fontOptions.italic = entry.textItalic;
    fontOptions.letterSpacing = entry.textLetterSpacing;

    motive::text::ExtrudedTextOptions textOptions;
    textOptions.pixelHeight = static_cast<uint32_t>(std::max(8, entry.textPixelHeight));
    textOptions.meshSupersample = motive::text::kDefaultExtrudedTextMeshSupersample;
    textOptions.font = fontOptions;
    textOptions.depth = std::max(0.0f, entry.textExtrudeDepth);
    textOptions.bevelScale = motive::text::kDefaultExtrudedTextBevelScale;

    const std::vector<Vertex> textVertices =
        motive::text::buildExtrudedTextVerticesFromText(entry.textContent.toStdString(), textOptions);
    if (textVertices.empty())
    {
        return;
    }
    if (!runtime.engine()->models[static_cast<size_t>(sceneIndex)])
    {
        auto model = std::make_unique<Model>(textVertices, runtime.engine());
        model->name = entry.name.toStdString();
        runtime.engine()->models[static_cast<size_t>(sceneIndex)] = std::move(model);
    }
    Model* model = runtime.engine()->models[static_cast<size_t>(sceneIndex)].get();
    if (!model || model->meshes.empty() || model->meshes.front().primitives.empty())
    {
        return;
    }
    Primitive* primitive = model->meshes.front().primitives.front().get();
    if (!primitive)
    {
        return;
    }
    primitive->updateVertexData(textVertices);
    motive::text::applyExtrudedTextMaterial(*model,
                                            vec3ColorFromString(entry.textColor, QColor(Qt::white)),
                                            entry.textDepthTest,
                                            entry.textDepthWrite);

    model->setSceneTransform(glm::vec3(entry.translation.x(), entry.translation.y(), entry.translation.z()),
                             glm::vec3(entry.rotation.x(), entry.rotation.y(), entry.rotation.z()),
                             glm::vec3(entry.scale.x(), entry.scale.y(), entry.scale.z()));
    model->visible = entry.visible;
}

void applyAnimationSettingsToModel(const ViewportHostWidget::SceneItem& entry, Model& model)
{
    model.setAnimationPlaybackState(entry.activeAnimationClip.toStdString(),
                                    entry.animationPlaying,
                                    entry.animationLoop,
                                    entry.animationSpeed);
    model.setAnimationProcessingOptions(entry.animationCentroidNormalization,
                                        entry.animationTrimStartNormalized,
                                        entry.animationTrimEndNormalized);
    model.setAnimationPhysicsCoupling(entry.animationPhysicsCoupling.toStdString());
    model.setInverseColorEnabled(entry.characterAiEnabled && entry.characterAiUseInverseColors);
    model.character.isAiDriven = entry.characterAiEnabled;
    model.character.moveSpeed = entry.characterMoveSpeed;
    model.character.idleAnimSpeed = entry.characterIdleAnimationSpeed;
    model.character.walkAnimSpeed = entry.characterWalkAnimationSpeed;
    model.character.enableProceduralIdle = entry.characterProceduralIdleEnabled;
    if (!entry.characterIdleClip.isEmpty()) model.character.animIdle = entry.characterIdleClip.toStdString();
    if (!entry.characterComeToRestClip.isEmpty()) model.character.animComeToRest = entry.characterComeToRestClip.toStdString();
    if (!entry.characterWalkForwardClip.isEmpty()) model.character.animWalkForward = entry.characterWalkForwardClip.toStdString();
    if (!entry.characterWalkBackwardClip.isEmpty()) model.character.animWalkBackward = entry.characterWalkBackwardClip.toStdString();
    if (!entry.characterWalkLeftClip.isEmpty()) model.character.animWalkLeft = entry.characterWalkLeftClip.toStdString();
    if (!entry.characterWalkRightClip.isEmpty()) model.character.animWalkRight = entry.characterWalkRightClip.toStdString();
    if (!entry.characterRunClip.isEmpty()) model.character.animRun = entry.characterRunClip.toStdString();
    if (!entry.characterJumpClip.isEmpty()) model.character.animJump = entry.characterJumpClip.toStdString();
    if (!entry.characterFallClip.isEmpty()) model.character.animFall = entry.characterFallClip.toStdString();
    if (!entry.characterLandClip.isEmpty()) model.character.animLand = entry.characterLandClip.toStdString();
    model.character.enableRestPointOnMoveRelease = entry.characterRestPointOnReleaseEnabled;
    model.character.restPointNormalizedOnMoveRelease =
        std::clamp(entry.characterRestPointOnReleaseNormalized, 0.0f, 1.0f);
}

PrimitiveCullMode cullModeFromString(const QString& value)
{
    if (value == QStringLiteral("none"))
    {
        return PrimitiveCullMode::Disabled;
    }
    if (value == QStringLiteral("front"))
    {
        return PrimitiveCullMode::Front;
    }
    return PrimitiveCullMode::Back;
}

void applyPrimitiveOverridesToModel(const ViewportHostWidget::SceneItem& entry, Model& model)
{
    for (const QJsonValue& value : entry.primitiveOverrides)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject object = value.toObject();
        const int meshIndex = object.value(QStringLiteral("meshIndex")).toInt(-1);
        const int primitiveIndex = object.value(QStringLiteral("primitiveIndex")).toInt(-1);
        if (meshIndex < 0 || primitiveIndex < 0 ||
            meshIndex >= static_cast<int>(model.meshes.size()))
        {
            continue;
        }

        Mesh& mesh = model.meshes[static_cast<size_t>(meshIndex)];
        if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) ||
            !mesh.primitives[static_cast<size_t>(primitiveIndex)])
        {
            continue;
        }

        Primitive* primitive = mesh.primitives[static_cast<size_t>(primitiveIndex)].get();
        primitive->cullMode = cullModeFromString(object.value(QStringLiteral("cullMode")).toString(QStringLiteral("back")));
        primitive->forceAlphaOne = object.value(QStringLiteral("forceAlphaOne")).toBool(false);
        if (primitive->ObjectTransformUBOMapped)
        {
            const ObjectTransform updated = primitive->buildObjectTransformData();
            memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
        }
    }
}

}

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
        0.0f,
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

    for (auto item : items)
    {
        if (isCoordinatePlaneIndicatorSceneItem(item))
        {
            canonicalizeCoordinatePlaneIndicatorItem(item);
        }
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
            true,
            0.0f,
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
        if (!m_runtime.engine()->models.empty() && m_runtime.engine()->models.back())
        {
            applyPrimitiveOverridesToModel(entry, *m_runtime.engine()->models.back());
            applyAnimationSettingsToModel(entry, *m_runtime.engine()->models.back());
        }
        m_sceneEntries.push_back(entry);
    }
    catch (const std::exception& ex)
    {
        qWarning() << "[ViewportHost] Failed to add scene asset:" << path << ex.what();
    }
}

void ViewportSceneController::appendSceneItem(const ViewportHostWidget::SceneItem& item)
{
    m_sceneEntries.push_back(item);
    const int sceneIndex = m_sceneEntries.size() - 1;
    if (!m_runtime.isInitialized() || !m_runtime.engine())
    {
        return;
    }
    if (isTextOverlaySceneItem(item))
    {
        applyTextSceneItemToModel(m_runtime, sceneIndex, m_sceneEntries[sceneIndex]);
        return;
    }
    if (isCoordinatePlaneIndicatorSceneItem(item))
    {
        canonicalizeCoordinatePlaneIndicatorItem(m_sceneEntries[sceneIndex]);
        applyCoordinatePlaneIndicatorToModel(m_runtime, sceneIndex, m_sceneEntries[sceneIndex]);
        return;
    }
    if (ViewportAssetLoader::loadModelIntoEngineSlot(m_runtime, sceneIndex, m_sceneEntries[sceneIndex]) &&
        sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(sceneIndex)])
    {
        applyPrimitiveOverridesToModel(m_sceneEntries[sceneIndex], *m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]);
        applyAnimationSettingsToModel(m_sceneEntries[sceneIndex], *m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]);
    }
}

void ViewportSceneController::addTextOverlayItem()
{
    ViewportHostWidget::SceneItem entry;
    entry.name = QStringLiteral("Text");
    entry.sourcePath = QStringLiteral("text://overlay");
    entry.meshConsolidationEnabled = false;
    entry.translation = QVector3D(0.0f, 0.0f, 0.0f);
    entry.rotation = QVector3D(0.0f, 0.0f, 0.0f);
    entry.scale = QVector3D(1.0f, 1.0f, 1.0f);
    m_sceneEntries.push_back(entry);
    const int sceneIndex = m_sceneEntries.size() - 1;
    if (m_runtime.isInitialized() && m_runtime.engine())
    {
        applyTextSceneItemToModel(m_runtime, sceneIndex, m_sceneEntries[sceneIndex]);
    }
}

bool ViewportSceneController::coordinatePlaneIndicatorsEnabled() const
{
    auto containsVisiblePlane = [](const QList<ViewportHostWidget::SceneItem>& entries) {
        for (const auto& entry : entries)
        {
            if (isCoordinatePlaneIndicatorSceneItem(entry) && entry.visible)
            {
                return true;
            }
        }
        return false;
    };
    return containsVisiblePlane(m_sceneEntries) || containsVisiblePlane(m_pendingSceneEntries);
}

void ViewportSceneController::setCoordinatePlaneIndicatorsEnabled(bool enabled)
{
    bool found = false;
    auto updateExisting = [&](QList<ViewportHostWidget::SceneItem>& entries, bool loaded) {
        for (int i = 0; i < entries.size(); ++i)
        {
            if (!isCoordinatePlaneIndicatorSceneItem(entries[i]))
            {
                continue;
            }
            found = true;
            canonicalizeCoordinatePlaneIndicatorItem(entries[i]);
            entries[i].visible = enabled;
            if (loaded && m_runtime.isInitialized() && m_runtime.engine())
            {
                applyCoordinatePlaneIndicatorToModel(m_runtime, i, entries[i]);
            }
        }
    };

    updateExisting(m_sceneEntries, true);
    updateExisting(m_pendingSceneEntries, false);
    if (found)
    {
        return;
    }

    const QStringList planes{
        QStringLiteral("xy"),
        QStringLiteral("xz"),
        QStringLiteral("yz"),
    };
    for (const QString& plane : planes)
    {
        ViewportHostWidget::SceneItem item = makeCoordinatePlaneIndicatorItem(plane, enabled);
        const int sceneIndex = m_sceneEntries.size();
        if (m_runtime.isInitialized() && m_runtime.engine())
        {
            applyCoordinatePlaneIndicatorToModel(m_runtime, sceneIndex, item);
            m_sceneEntries.push_back(item);
        }
        else
        {
            m_pendingSceneEntries.push_back(item);
        }
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

    for (auto item : items)
    {
        if (isCoordinatePlaneIndicatorSceneItem(item))
        {
            canonicalizeCoordinatePlaneIndicatorItem(item);
        }
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
    if (isTextOverlaySceneItem(m_sceneEntries[index]))
    {
        applyTextSceneItemToModel(m_runtime, index, m_sceneEntries[index]);
        return;
    }
    if (isCoordinatePlaneIndicatorSceneItem(m_sceneEntries[index]))
    {
        canonicalizeCoordinatePlaneIndicatorItem(m_sceneEntries[index]);
        applyCoordinatePlaneIndicatorToModel(m_runtime, index, m_sceneEntries[index]);
        return;
    }

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
            if (m_runtime.engine()->models[static_cast<size_t>(index)])
            {
                applyPrimitiveOverridesToModel(m_sceneEntries[index], *m_runtime.engine()->models[static_cast<size_t>(index)]);
                applyAnimationSettingsToModel(m_sceneEntries[index], *m_runtime.engine()->models[static_cast<size_t>(index)]);
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

void ViewportSceneController::updateSceneItemAnimationProcessing(int index,
                                                                 bool centroidNormalizationEnabled,
                                                                 float trimStartNormalized,
                                                                 float trimEndNormalized)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const float trimStart = std::clamp(trimStartNormalized, 0.0f, 1.0f);
    const float trimEnd = std::clamp(trimEndNormalized, 0.0f, 1.0f);
    m_sceneEntries[index].animationCentroidNormalization = centroidNormalizationEnabled;
    m_sceneEntries[index].animationTrimStartNormalized = std::min(trimStart, trimEnd);
    m_sceneEntries[index].animationTrimEndNormalized = std::max(trimStart, trimEnd);

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->setAnimationProcessingOptions(
            m_sceneEntries[index].animationCentroidNormalization,
            m_sceneEntries[index].animationTrimStartNormalized,
            m_sceneEntries[index].animationTrimEndNormalized);
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

void ViewportSceneController::updateSceneItemCharacterTurnResponsiveness(int index, float responsiveness)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const float clamped = (responsiveness > 0.01f) ? responsiveness : 0.01f;
    m_sceneEntries[index].characterTurnResponsiveness = clamped;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        m_runtime.engine()->models[static_cast<size_t>(index)]->character.turnResponsiveness = clamped;
    }
}

void ViewportSceneController::updateSceneItemCharacterLocomotion(int index, float moveSpeed, float idleAnimationSpeed, float walkAnimationSpeed)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const float clampedMoveSpeed = std::clamp(moveSpeed, 0.0f, 50.0f);
    const float clampedIdleAnimationSpeed = std::clamp(idleAnimationSpeed, 0.0f, 10.0f);
    const float clampedWalkAnimationSpeed = std::clamp(walkAnimationSpeed, 0.0f, 10.0f);
    m_sceneEntries[index].characterMoveSpeed = clampedMoveSpeed;
    m_sceneEntries[index].characterIdleAnimationSpeed = clampedIdleAnimationSpeed;
    m_sceneEntries[index].characterWalkAnimationSpeed = clampedWalkAnimationSpeed;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        auto& character = m_runtime.engine()->models[static_cast<size_t>(index)]->character;
        character.moveSpeed = clampedMoveSpeed;
        character.idleAnimSpeed = clampedIdleAnimationSpeed;
        character.walkAnimSpeed = clampedWalkAnimationSpeed;
    }
}

void ViewportSceneController::updateSceneItemCharacterProceduralIdle(int index, bool enabled)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].characterProceduralIdleEnabled = enabled;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        auto& character = m_runtime.engine()->models[static_cast<size_t>(index)]->character;
        character.enableProceduralIdle = enabled;
    }
}

void ViewportSceneController::updateSceneItemCharacterAnimationBindings(int index, const ViewportHostWidget::SceneItem& bindingsSource)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    ViewportHostWidget::SceneItem& entry = m_sceneEntries[index];
    entry.characterIdleClip = bindingsSource.characterIdleClip;
    entry.characterComeToRestClip = bindingsSource.characterComeToRestClip;
    entry.characterWalkForwardClip = bindingsSource.characterWalkForwardClip;
    entry.characterWalkBackwardClip = bindingsSource.characterWalkBackwardClip;
    entry.characterWalkLeftClip = bindingsSource.characterWalkLeftClip;
    entry.characterWalkRightClip = bindingsSource.characterWalkRightClip;
    entry.characterRunClip = bindingsSource.characterRunClip;
    entry.characterJumpClip = bindingsSource.characterJumpClip;
    entry.characterFallClip = bindingsSource.characterFallClip;
    entry.characterLandClip = bindingsSource.characterLandClip;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        Model& model = *m_runtime.engine()->models[static_cast<size_t>(index)];
        applyAnimationSettingsToModel(entry, model);
    }
}

void ViewportSceneController::updateSceneItemCharacterRestPointOnRelease(int index, bool enabled, float normalized)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    m_sceneEntries[index].characterRestPointOnReleaseEnabled = enabled;
    m_sceneEntries[index].characterRestPointOnReleaseNormalized = clamped;

    if (m_runtime.engine() &&
        index < static_cast<int>(m_runtime.engine()->models.size()) &&
        m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        auto& character = m_runtime.engine()->models[static_cast<size_t>(index)]->character;
        character.enableRestPointOnMoveRelease = enabled;
        character.restPointNormalizedOnMoveRelease = clamped;
    }
}

void ViewportSceneController::updateSceneItemFocusSettings(int index, const QVector3D& focusPointOffset, float focusDistance)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].focusPointOffset = focusPointOffset;
    m_sceneEntries[index].focusDistance = focusDistance > 0.0f ? focusDistance : 0.0f;
}

void ViewportSceneController::updateSceneItemFocusCameraOffset(int index, const QVector3D& focusCameraOffset, bool valid)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }

    m_sceneEntries[index].focusCameraOffset = focusCameraOffset;
    m_sceneEntries[index].focusCameraOffsetValid = valid;
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
    if (isTextOverlaySceneItem(m_sceneEntries[index]))
    {
        applyTextSceneItemToModel(m_runtime, index, m_sceneEntries[index]);
        return;
    }
    if (isCoordinatePlaneIndicatorSceneItem(m_sceneEntries[index]))
    {
        canonicalizeCoordinatePlaneIndicatorItem(m_sceneEntries[index]);
        applyCoordinatePlaneIndicatorToModel(m_runtime, index, m_sceneEntries[index]);
        return;
    }
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
            if (m_runtime.engine()->models[static_cast<size_t>(index)])
            {
                applyPrimitiveOverridesToModel(m_sceneEntries[index], *m_runtime.engine()->models[static_cast<size_t>(index)]);
                applyAnimationSettingsToModel(m_sceneEntries[index], *m_runtime.engine()->models[static_cast<size_t>(index)]);
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

void ViewportSceneController::updateSceneItemTextOverlay(int index, const ViewportHostWidget::SceneItem& textProps)
{
    if (index < 0 || index >= m_sceneEntries.size())
    {
        return;
    }
    if (!isTextOverlaySceneItem(m_sceneEntries[index]))
    {
        return;
    }

    m_sceneEntries[index].textContent = textProps.textContent;
    m_sceneEntries[index].textFontPath = textProps.textFontPath;
    m_sceneEntries[index].textPixelHeight = textProps.textPixelHeight;
    m_sceneEntries[index].textBold = textProps.textBold;
    m_sceneEntries[index].textItalic = textProps.textItalic;
    m_sceneEntries[index].textShadow = textProps.textShadow;
    m_sceneEntries[index].textOutline = textProps.textOutline;
    m_sceneEntries[index].textLetterSpacing = textProps.textLetterSpacing;
    m_sceneEntries[index].textColor = textProps.textColor;
    m_sceneEntries[index].textBackgroundColor = textProps.textBackgroundColor;
    m_sceneEntries[index].textExtrudeDepth = textProps.textExtrudeDepth;
    m_sceneEntries[index].textExtrudeGlyphsOnly = textProps.textExtrudeGlyphsOnly;
    m_sceneEntries[index].textDepthTest = textProps.textDepthTest;
    m_sceneEntries[index].textDepthWrite = textProps.textDepthWrite;
    applyTextSceneItemToModel(m_runtime, index, m_sceneEntries[index]);
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
            auto entry = pendingEntries[i];
            if (isCoordinatePlaneIndicatorSceneItem(entry))
            {
                canonicalizeCoordinatePlaneIndicatorItem(entry);
            }
            items.push_back({startIndex + i, entry});
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
            if (isCoordinatePlaneIndicatorSceneItem(entry))
            {
                canonicalizeCoordinatePlaneIndicatorItem(entry);
            }
            const int sceneIndex = startIndex + i;
            
            if (m_runtime.engine() &&
                sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
                m_runtime.engine()->models[static_cast<size_t>(sceneIndex)])
            {
                Model& model = *m_runtime.engine()->models[static_cast<size_t>(sceneIndex)];
                if (entry.activeAnimationClip.isEmpty())
                {
                    const auto& clips = model.animationClips;
                    if (!clips.empty())
                    {
                        entry.activeAnimationClip = QString::fromStdString(clips.front().name);
                    }
                }
                applyPrimitiveOverridesToModel(entry, model);
                applyAnimationSettingsToModel(entry, model);
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
                if (isTextOverlaySceneItem(entry))
                {
                    applyTextSceneItemToModel(m_runtime, sceneIndex, entry);
                    m_sceneEntries.push_back(entry);
                    continue;
                }
                if (isCoordinatePlaneIndicatorSceneItem(entry))
                {
                    canonicalizeCoordinatePlaneIndicatorItem(entry);
                    applyCoordinatePlaneIndicatorToModel(m_runtime, sceneIndex, entry);
                    m_sceneEntries.push_back(entry);
                    continue;
                }
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
            if (m_runtime.engine() &&
                sceneIndex < static_cast<int>(m_runtime.engine()->models.size()) &&
                m_runtime.engine()->models[static_cast<size_t>(sceneIndex)])
            {
                applyPrimitiveOverridesToModel(entry, *m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]);
                applyAnimationSettingsToModel(entry, *m_runtime.engine()->models[static_cast<size_t>(sceneIndex)]);
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
