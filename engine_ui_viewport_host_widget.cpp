#include "engine_ui_viewport_host_widget.h"

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "light.h"
#include "model.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <mutex>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonObject>
#include <QMimeData>
#include <QDebug>
#include <QVector3D>
#include <QVBoxLayout>
#include <QLabel>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>

#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#define Display X11Display
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#undef Display
#endif

namespace motive::ui {
namespace {

constexpr int kHierarchyCameraIndex = -1000;
constexpr int kHierarchyLightIndex = -1001;

Light engineLightFromSceneLight(const ViewportHostWidget::SceneLight& sceneLight)
{
    const glm::vec3 direction(sceneLight.direction.x(), sceneLight.direction.y(), sceneLight.direction.z());
    const glm::vec3 color(sceneLight.color.x(), sceneLight.color.y(), sceneLight.color.z());
    const float brightness = std::max(0.0f, sceneLight.brightness);

    glm::vec3 ambient(0.0f);
    glm::vec3 diffuse(0.0f);

    if (sceneLight.type == QStringLiteral("ambient"))
    {
        ambient = color * brightness;
    }
    else if (sceneLight.type == QStringLiteral("hemispherical"))
    {
        ambient = color * brightness * 0.45f;
        diffuse = color * brightness * 0.55f;
    }
    else
    {
        ambient = color * brightness * 0.10f;
        diffuse = color * brightness;
    }

    return Light(direction, ambient, diffuse);
}

QJsonObject hierarchyNodeToJson(const ViewportHostWidget::HierarchyNode& node)
{
    QJsonArray children;
    for (const auto& child : node.children)
    {
        children.append(hierarchyNodeToJson(child));
    }

    QString typeString = QStringLiteral("scene_item");
    switch (node.type)
    {
    case ViewportHostWidget::HierarchyNode::Type::Camera:
        typeString = QStringLiteral("camera");
        break;
    case ViewportHostWidget::HierarchyNode::Type::Light:
        typeString = QStringLiteral("light");
        break;
    case ViewportHostWidget::HierarchyNode::Type::SceneItem:
        typeString = QStringLiteral("scene_item");
        break;
    case ViewportHostWidget::HierarchyNode::Type::Mesh:
        typeString = QStringLiteral("mesh");
        break;
    case ViewportHostWidget::HierarchyNode::Type::Primitive:
        typeString = QStringLiteral("primitive");
        break;
    case ViewportHostWidget::HierarchyNode::Type::AnimationGroup:
        typeString = QStringLiteral("animation_group");
        break;
    case ViewportHostWidget::HierarchyNode::Type::AnimationClip:
        typeString = QStringLiteral("animation_clip");
        break;
    case ViewportHostWidget::HierarchyNode::Type::PendingSceneItem:
        typeString = QStringLiteral("pending_scene_item");
        break;
    }

    return QJsonObject{
        {QStringLiteral("label"), node.label},
        {QStringLiteral("type"), typeString},
        {QStringLiteral("sceneIndex"), node.sceneIndex},
        {QStringLiteral("meshIndex"), node.meshIndex},
        {QStringLiteral("primitiveIndex"), node.primitiveIndex},
        {QStringLiteral("clipName"), node.clipName},
        {QStringLiteral("children"), children}
    };
}

struct EmbeddedViewportState
{
    struct SceneEntry
    {
        QString name;
        QString sourcePath;
        QVector3D translation;
        QVector3D rotation;
        QVector3D scale;
        bool paintOverrideEnabled = false;
        QVector3D paintOverrideColor = QVector3D(1.0f, 0.0f, 1.0f);
        QString activeAnimationClip;
        bool animationPlaying = true;
        bool animationLoop = true;
        float animationSpeed = 1.0f;
        bool visible = true;
    };

    std::unique_ptr<Engine> engine;
    Display* display = nullptr;
    Camera* camera = nullptr;
    QString currentAssetPath;
    QList<SceneEntry> pendingSceneEntries;
    QList<SceneEntry> sceneEntries;
    bool use2DPipeline = false;
    float bgColorR = 0.2f;
    float bgColorG = 0.2f;
    float bgColorB = 0.8f;
    float cameraSpeed = 0.01f;
    bool meshConsolidationEnabled = true;
    ViewportHostWidget::SceneLight sceneLight;
    mutable std::recursive_mutex mutex;
};

EmbeddedViewportState& viewportState()
{
    static EmbeddedViewportState state;
    return state;
}

bool isRenderableAsset(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("gltf") ||
           suffix == QStringLiteral("glb") ||
           suffix == QStringLiteral("fbx");
}

bool vectorsNearlyEqual(const QVector3D& lhs, const QVector3D& rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs.x() - rhs.x()) <= epsilon &&
           std::abs(lhs.y() - rhs.y()) <= epsilon &&
           std::abs(lhs.z() - rhs.z()) <= epsilon;
}

glm::vec3 cameraForwardVector(const glm::vec2& cameraRotation)
{
    const float yaw = cameraRotation.x;
    const float pitch = cameraRotation.y;
    glm::vec3 front;
    front.x = std::cos(pitch) * std::sin(yaw);
    front.y = std::sin(pitch);
    front.z = -std::cos(pitch) * std::cos(yaw);
    if (glm::length(front) <= 1e-6f)
    {
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return glm::normalize(front);
}

glm::vec2 cameraRotationForDirection(const glm::vec3& direction)
{
    const glm::vec3 normalized = glm::normalize(direction);
    const float yaw = std::atan2(normalized.x, -normalized.z);
    const float pitch = std::asin(glm::clamp(normalized.y, -1.0f, 1.0f));
    return glm::vec2(yaw, pitch);
}

float framingDistanceForModel(const Model& model)
{
    return std::max(std::max(model.boundsRadius, 0.5f) * 3.0f, 2.0f);
}

std::filesystem::path defaultScenePath()
{
    const std::filesystem::path teapot = std::filesystem::path("the_utah_teapot.glb");
    if (std::filesystem::exists(teapot))
    {
        return teapot;
    }
    return {};
}

void loadModelIntoEngine(EmbeddedViewportState& state, const EmbeddedViewportState::SceneEntry& entry)
{
    if (!state.engine || entry.sourcePath.isEmpty() || !isRenderableAsset(entry.sourcePath))
    {
        qWarning() << "[ViewportHost] Skipping non-renderable scene path:" << entry.sourcePath;
        return;
    }

    qDebug() << "[ViewportHost] Loading model into scene:" << entry.sourcePath
             << "existingModels=" << static_cast<int>(state.engine->models.size());
    auto model = std::make_unique<Model>(entry.sourcePath.toStdString(), state.engine.get(), state.meshConsolidationEnabled);
    model->resizeToUnitBox();
    model->setSceneTransform(glm::vec3(entry.translation.x(), entry.translation.y(), entry.translation.z()),
                             glm::vec3(entry.rotation.x(), entry.rotation.y(), entry.rotation.z()),
                             glm::vec3(entry.scale.x(), entry.scale.y(), entry.scale.z()));
    model->setPaintOverride(entry.paintOverrideEnabled,
                            glm::vec3(entry.paintOverrideColor.x(), entry.paintOverrideColor.y(), entry.paintOverrideColor.z()));
    model->visible = entry.visible;
    state.engine->addModel(std::move(model));
}

}  // namespace

ViewportHostWidget::ViewportHostWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_statusLabel = new QLabel(QStringLiteral("Initializing viewport..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    m_renderTimer.setInterval(16);
    connect(&m_renderTimer, &QTimer::timeout, this, [this]() { renderFrame(); });
}

ViewportHostWidget::~ViewportHostWidget()
{
    m_renderTimer.stop();
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    m_initialized = false;
    if (state.display)
    {
        state.display->shutdown();
    }
    state.display = nullptr;
    state.camera = nullptr;
    state.engine.reset();
}

void ViewportHostWidget::loadAssetFromPath(const QString& path)
{
    qDebug() << "[ViewportHost] loadAssetFromPath" << path;
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (path.isEmpty())
    {
        return;
    }

    if (state.engine)
    {
        state.engine->models.clear();
    }
    state.sceneEntries.clear();
    state.pendingSceneEntries.clear();
    state.pendingSceneEntries.push_back(EmbeddedViewportState::SceneEntry{
        QFileInfo(path).completeBaseName(),
        QFileInfo(path).absoluteFilePath(),
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
    state.currentAssetPath = path;
    if (!m_initialized || !state.engine)
    {
        qDebug() << "[ViewportHost] Deferring single-asset scene until viewport init";
        return;
    }
    addAssetToScene(path);
    state.pendingSceneEntries.clear();
}

void ViewportHostWidget::loadSceneFromItems(const QList<SceneItem>& items)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    qDebug() << "[ViewportHost] loadSceneFromItems count=" << items.size();

    if (state.engine)
    {
        state.engine->models.clear();
    }
    state.sceneEntries.clear();
    state.pendingSceneEntries.clear();
    for (const SceneItem& item : items)
    {
        state.pendingSceneEntries.push_back({item.name, item.sourcePath, item.translation, item.rotation, item.scale,
                                             item.paintOverrideEnabled, item.paintOverrideColor,
                                             item.activeAnimationClip, item.animationPlaying, item.animationLoop, item.animationSpeed,
                                             item.visible});
    }
    state.currentAssetPath = items.isEmpty() ? QString() : items.back().sourcePath;

    if (!m_initialized || !state.engine)
    {
        qDebug() << "[ViewportHost] Deferring scene restore until viewport init";
        notifySceneChanged();
        return;
    }

    const QList<EmbeddedViewportState::SceneEntry> pendingEntries = state.pendingSceneEntries;
    state.pendingSceneEntries.clear();
    for (auto entry : pendingEntries)
    {
        try
        {
            loadModelIntoEngine(state, entry);
            if (state.engine && !state.engine->models.empty() && state.engine->models.back() && entry.activeAnimationClip.isEmpty())
            {
                const auto& clips = state.engine->models.back()->animationClips;
                if (!clips.empty())
                {
                    entry.activeAnimationClip = QString::fromStdString(clips.front().name);
                }
            }
            state.sceneEntries.push_back(entry);
        }
        catch (const std::exception& ex)
        {
            qWarning() << "[ViewportHost] Failed to restore scene asset:" << entry.sourcePath << ex.what();
        }
    }
    notifySceneChanged();
}

QString ViewportHostWidget::currentAssetPath() const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    return state.currentAssetPath;
}

QList<ViewportHostWidget::SceneItem> ViewportHostWidget::sceneItems() const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    QList<SceneItem> items;
    const auto& entries = state.sceneEntries;
    for (const auto& entry : entries)
    {
        items.push_back(SceneItem{entry.name, entry.sourcePath, entry.translation, entry.rotation, entry.scale,
                                  entry.paintOverrideEnabled, entry.paintOverrideColor,
                                  entry.activeAnimationClip, entry.animationPlaying, entry.animationLoop, entry.animationSpeed,
                                  entry.visible});
    }
    const auto& pending = state.pendingSceneEntries;
    for (const auto& entry : pending)
    {
        items.push_back(SceneItem{entry.name, entry.sourcePath, entry.translation, entry.rotation, entry.scale,
                                  entry.paintOverrideEnabled, entry.paintOverrideColor,
                                  entry.activeAnimationClip, entry.animationPlaying, entry.animationLoop, entry.animationSpeed,
                                  entry.visible});
    }
    return items;
}

QList<ViewportHostWidget::HierarchyNode> ViewportHostWidget::hierarchyItems() const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);

    QList<HierarchyNode> items;
    items.push_back(HierarchyNode{QStringLiteral("Camera"), HierarchyNode::Type::Camera, kHierarchyCameraIndex, -1, -1, QString(), {}});
    if (state.sceneLight.exists)
    {
        items.push_back(HierarchyNode{QStringLiteral("Directional Light"), HierarchyNode::Type::Light, kHierarchyLightIndex, -1, -1, QString(), {}});
    }
    const int loadedCount = state.engine ? static_cast<int>(state.engine->models.size()) : 0;

    for (int i = 0; i < state.sceneEntries.size(); ++i)
    {
        const auto& entry = state.sceneEntries[i];
        HierarchyNode sceneNode;
        sceneNode.label = entry.name;
        sceneNode.type = HierarchyNode::Type::SceneItem;
        sceneNode.sceneIndex = i;

        if (i < loadedCount && state.engine && state.engine->models[i])
        {
            const auto& model = state.engine->models[i];
            for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
            {
                const auto& mesh = model->meshes[meshIndex];
                HierarchyNode meshNode;
                meshNode.label = QStringLiteral("Mesh %1").arg(meshIndex);
                meshNode.type = HierarchyNode::Type::Mesh;
                meshNode.sceneIndex = i;
                meshNode.meshIndex = static_cast<int>(meshIndex);

                for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
                {
                    const auto& primitive = mesh.primitives[primitiveIndex];
                    HierarchyNode primitiveNode;
                    primitiveNode.label = QStringLiteral("Primitive %1 (%2 verts, %3 indices)")
                                              .arg(primitiveIndex)
                                              .arg(primitive ? primitive->vertexCount : 0)
                                              .arg(primitive ? primitive->indexCount : 0);
                    primitiveNode.type = HierarchyNode::Type::Primitive;
                    primitiveNode.sceneIndex = i;
                    primitiveNode.meshIndex = static_cast<int>(meshIndex);
                    primitiveNode.primitiveIndex = static_cast<int>(primitiveIndex);
                    meshNode.children.push_back(primitiveNode);
                }

                sceneNode.children.push_back(meshNode);
            }

            if (!model->animationClips.empty())
            {
                HierarchyNode animationGroupNode;
                animationGroupNode.label = QStringLiteral("Animations");
                animationGroupNode.type = HierarchyNode::Type::AnimationGroup;
                animationGroupNode.sceneIndex = i;

                for (const auto& clip : model->animationClips)
                {
                    HierarchyNode clipNode;
                    clipNode.label = QString::fromStdString(clip.name);
                    clipNode.type = HierarchyNode::Type::AnimationClip;
                    clipNode.sceneIndex = i;
                    clipNode.clipName = clipNode.label;
                    animationGroupNode.children.push_back(clipNode);
                }

                sceneNode.children.push_back(animationGroupNode);
            }
        }

        items.push_back(sceneNode);
    }

    for (int i = 0; i < state.pendingSceneEntries.size(); ++i)
    {
        const auto& entry = state.pendingSceneEntries[i];
        HierarchyNode pendingNode;
        pendingNode.label = entry.name + QStringLiteral(" (pending)");
        pendingNode.type = HierarchyNode::Type::PendingSceneItem;
        pendingNode.sceneIndex = state.sceneEntries.size() + i;
        items.push_back(pendingNode);
    }

    return items;
}

QJsonArray ViewportHostWidget::hierarchyJson() const
{
    QJsonArray array;
    for (const auto& node : hierarchyItems())
    {
        array.append(hierarchyNodeToJson(node));
    }
    return array;
}

QJsonArray ViewportHostWidget::sceneProfileJson() const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);

    auto alphaModeName = [](PrimitiveAlphaMode mode) -> QString
    {
        switch (mode)
        {
        case PrimitiveAlphaMode::Opaque:
            return QStringLiteral("opaque");
        case PrimitiveAlphaMode::Mask:
            return QStringLiteral("mask");
        case PrimitiveAlphaMode::Blend:
            return QStringLiteral("blend");
        }
        return QStringLiteral("unknown");
    };
    auto cullModeName = [](PrimitiveCullMode mode) -> QString
    {
        switch (mode)
        {
        case PrimitiveCullMode::Back:
            return QStringLiteral("back");
        case PrimitiveCullMode::Disabled:
            return QStringLiteral("none");
        case PrimitiveCullMode::Front:
            return QStringLiteral("front");
        }
        return QStringLiteral("unknown");
    };

    QJsonArray sceneItems;
    for (int sceneIndex = 0; sceneIndex < state.sceneEntries.size(); ++sceneIndex)
    {
        const auto& entry = state.sceneEntries[sceneIndex];
        QJsonObject sceneItem{
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("sourcePath"), entry.sourcePath},
            {QStringLiteral("translation"), QJsonArray{entry.translation.x(), entry.translation.y(), entry.translation.z()}},
            {QStringLiteral("rotation"), QJsonArray{entry.rotation.x(), entry.rotation.y(), entry.rotation.z()}},
            {QStringLiteral("scale"), QJsonArray{entry.scale.x(), entry.scale.y(), entry.scale.z()}},
            {QStringLiteral("paintOverrideEnabled"), entry.paintOverrideEnabled},
            {QStringLiteral("paintOverrideColor"), QJsonArray{entry.paintOverrideColor.x(), entry.paintOverrideColor.y(), entry.paintOverrideColor.z()}},
            {QStringLiteral("visible"), entry.visible},
            {QStringLiteral("activeAnimationClip"), entry.activeAnimationClip},
            {QStringLiteral("animationPlaying"), entry.animationPlaying},
            {QStringLiteral("animationLoop"), entry.animationLoop},
            {QStringLiteral("animationSpeed"), entry.animationSpeed}
        };

        QJsonArray meshArray;
        if (state.engine &&
            sceneIndex < static_cast<int>(state.engine->models.size()) &&
            state.engine->models[sceneIndex])
        {
            const auto& model = state.engine->models[sceneIndex];
            sceneItem.insert(QStringLiteral("boundsCenter"), QJsonArray{model->boundsCenter.x, model->boundsCenter.y, model->boundsCenter.z});
            sceneItem.insert(QStringLiteral("boundsRadius"), model->boundsRadius);

            for (int meshIndex = 0; meshIndex < static_cast<int>(model->meshes.size()); ++meshIndex)
            {
                const auto& mesh = model->meshes[meshIndex];
                QJsonObject meshObject{{QStringLiteral("meshIndex"), meshIndex}};
                QJsonArray primitiveArray;
                for (int primitiveIndex = 0; primitiveIndex < static_cast<int>(mesh.primitives.size()); ++primitiveIndex)
                {
                    const auto& primitive = mesh.primitives[primitiveIndex];
                    if (!primitive)
                    {
                        continue;
                    }
                    primitiveArray.append(QJsonObject{
                        {QStringLiteral("primitiveIndex"), primitiveIndex},
                        {QStringLiteral("vertexCount"), static_cast<int>(primitive->vertexCount)},
                        {QStringLiteral("indexCount"), static_cast<int>(primitive->indexCount)},
                        {QStringLiteral("alphaMode"), alphaModeName(primitive->alphaMode)},
                        {QStringLiteral("cullMode"), cullModeName(primitive->cullMode)},
                        {QStringLiteral("alphaCutoff"), primitive->alphaCutoff},
                        {QStringLiteral("textureWidth"), static_cast<int>(primitive->textureWidth)},
                        {QStringLiteral("textureHeight"), static_cast<int>(primitive->textureHeight)},
                        {QStringLiteral("hasTexturePreview"), !primitive->texturePreviewImage.isNull()},
                        {QStringLiteral("usesYuvTexture"), primitive->usesYuvTexture},
                        {QStringLiteral("descriptorAllocated"), primitive->primitiveDescriptorSet != VK_NULL_HANDLE}
                    });
                }
                meshObject.insert(QStringLiteral("primitives"), primitiveArray);
                meshArray.append(meshObject);
            }
        }

        sceneItem.insert(QStringLiteral("meshes"), meshArray);
        sceneItems.append(sceneItem);
    }

    return sceneItems;
}

QImage ViewportHostWidget::primitiveTexturePreview(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (!state.engine || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return {};
    }
    if (sceneIndex >= static_cast<int>(state.engine->models.size()) || !state.engine->models[sceneIndex])
    {
        return {};
    }

    const auto& model = state.engine->models[sceneIndex];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return {};
    }

    const auto& mesh = model->meshes[meshIndex];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[primitiveIndex])
    {
        return {};
    }

    return mesh.primitives[primitiveIndex]->texturePreviewImage;
}

QString ViewportHostWidget::primitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (!state.engine || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return QStringLiteral("back");
    }
    if (sceneIndex >= static_cast<int>(state.engine->models.size()) || !state.engine->models[sceneIndex])
    {
        return QStringLiteral("back");
    }
    const auto& model = state.engine->models[sceneIndex];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return QStringLiteral("back");
    }
    const auto& mesh = model->meshes[meshIndex];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[primitiveIndex])
    {
        return QStringLiteral("back");
    }

    switch (mesh.primitives[primitiveIndex]->cullMode)
    {
    case PrimitiveCullMode::Back:
        return QStringLiteral("back");
    case PrimitiveCullMode::Disabled:
        return QStringLiteral("none");
    case PrimitiveCullMode::Front:
        return QStringLiteral("front");
    }
    return QStringLiteral("back");
}

QStringList ViewportHostWidget::animationClipNames(int sceneIndex) const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    QStringList clips;
    if (!state.engine || sceneIndex < 0 || sceneIndex >= static_cast<int>(state.engine->models.size()) || !state.engine->models[sceneIndex])
    {
        return clips;
    }

    for (const auto& clip : state.engine->models[sceneIndex]->animationClips)
    {
        clips.push_back(QString::fromStdString(clip.name));
    }
    return clips;
}

bool ViewportHostWidget::hasSceneLight() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    return state.sceneLight.exists;
}

ViewportHostWidget::SceneLight ViewportHostWidget::sceneLight() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    return state.sceneLight;
}

QVector3D ViewportHostWidget::cameraPosition() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        return QVector3D(state.camera->cameraPos.x, state.camera->cameraPos.y, state.camera->cameraPos.z);
    }
    return QVector3D(0.0f, 0.0f, 3.0f);
}

QVector3D ViewportHostWidget::cameraRotation() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        return QVector3D(state.camera->cameraRotation.x, state.camera->cameraRotation.y, 0.0f);
    }
    return QVector3D(0.0f, 0.0f, 0.0f);
}

float ViewportHostWidget::cameraSpeed() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    return state.cameraSpeed;
}

QString ViewportHostWidget::renderPath() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    return state.use2DPipeline ? QStringLiteral("flat2d") : QStringLiteral("forward3d");
}

bool ViewportHostWidget::meshConsolidationEnabled() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    return state.meshConsolidationEnabled;
}

void ViewportHostWidget::setCameraPosition(const QVector3D& position)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        state.camera->cameraPos = glm::vec3(position.x(), position.y(), position.z());
        state.camera->update(0); // Update camera matrices
    }
}

void ViewportHostWidget::setCameraRotation(const QVector3D& rotation)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        state.camera->cameraRotation = glm::vec2(rotation.y(), rotation.x()); // Note: y,x order for glm::vec2
        state.camera->update(0); // Update camera matrices
    }
}

void ViewportHostWidget::setCameraSpeed(float speed)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    state.cameraSpeed = speed;
    if (state.camera) {
        state.camera->moveSpeed = speed;
    }
}

void ViewportHostWidget::resetCamera()
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        state.camera->reset();
    }
}

void ViewportHostWidget::setBackgroundColor(const QColor& color)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    state.bgColorR = color.redF();
    state.bgColorG = color.greenF();
    state.bgColorB = color.blueF();
    if (state.display) {
        state.display->setBackgroundColor(state.bgColorR, state.bgColorG, state.bgColorB);
    }
}

void ViewportHostWidget::setRenderPath(const QString& renderPath)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    const bool use2d = renderPath.compare(QStringLiteral("flat2d"), Qt::CaseInsensitive) == 0;
    if (state.use2DPipeline == use2d)
    {
        return;
    }

    state.use2DPipeline = use2d;
    const QList<SceneItem> items = sceneItems();
    const QVector3D savedCameraPos = cameraPosition();
    const QVector3D savedCameraRot = cameraRotation();

    m_renderTimer.stop();
    state.display = nullptr;
    state.camera = nullptr;
    state.engine.reset();
    m_initialized = false;

    state.pendingSceneEntries.clear();
    for (const SceneItem& item : items)
    {
        state.pendingSceneEntries.push_back({item.name, item.sourcePath, item.translation, item.rotation, item.scale,
                                             item.paintOverrideEnabled, item.paintOverrideColor,
                                             item.activeAnimationClip, item.animationPlaying, item.animationLoop, item.animationSpeed,
                                             item.visible});
    }

    ensureViewportInitialized();
    if (state.camera)
    {
        state.camera->moveSpeed = state.cameraSpeed;
        state.camera->cameraPos = glm::vec3(savedCameraPos.x(), savedCameraPos.y(), savedCameraPos.z());
        state.camera->cameraRotation = glm::vec2(savedCameraRot.y(), savedCameraRot.x());
        state.camera->update(0);
    }
    if (state.display)
    {
        state.display->setBackgroundColor(state.bgColorR, state.bgColorG, state.bgColorB);
    }
}

void ViewportHostWidget::setMeshConsolidationEnabled(bool enabled)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.meshConsolidationEnabled == enabled)
    {
        return;
    }

    state.meshConsolidationEnabled = enabled;
    const QList<SceneItem> items = sceneItems();

    if (state.engine)
    {
        state.engine->models.clear();
    }
    state.sceneEntries.clear();
    state.pendingSceneEntries.clear();
    for (const SceneItem& item : items)
    {
        state.pendingSceneEntries.push_back({item.name, item.sourcePath, item.translation, item.rotation, item.scale,
                                             item.paintOverrideEnabled, item.paintOverrideColor,
                                             item.activeAnimationClip, item.animationPlaying, item.animationLoop, item.animationSpeed,
                                             item.visible});
    }

    if (!m_initialized || !state.engine)
    {
        notifySceneChanged();
        return;
    }

    const QList<EmbeddedViewportState::SceneEntry> pendingEntries = state.pendingSceneEntries;
    state.pendingSceneEntries.clear();
    for (const auto& entry : pendingEntries)
    {
        try
        {
            loadModelIntoEngine(state, entry);
            state.sceneEntries.push_back(entry);
        }
        catch (const std::exception& ex)
        {
            qWarning() << "[ViewportHost] Failed to reload scene asset with mesh consolidation change:" << entry.sourcePath << ex.what();
        }
    }
    notifySceneChanged();
}

void ViewportHostWidget::createSceneLight()
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.sceneLight.exists)
    {
        return;
    }

    state.sceneLight.exists = true;
    if (state.engine)
    {
        const Light light = engineLightFromSceneLight(state.sceneLight);
        state.sceneLight.ambient = QVector3D(light.ambient.x, light.ambient.y, light.ambient.z);
        state.sceneLight.diffuse = QVector3D(light.diffuse.x, light.diffuse.y, light.diffuse.z);
        state.engine->setLight(light);
    }
    notifySceneChanged();
}

void ViewportHostWidget::setSceneLight(const SceneLight& light)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    state.sceneLight = light;
    if (state.engine)
    {
        if (state.sceneLight.exists)
        {
            const Light engineLight = engineLightFromSceneLight(state.sceneLight);
            state.sceneLight.ambient = QVector3D(engineLight.ambient.x, engineLight.ambient.y, engineLight.ambient.z);
            state.sceneLight.diffuse = QVector3D(engineLight.diffuse.x, engineLight.diffuse.y, engineLight.diffuse.z);
            state.engine->setLight(engineLight);
        }
        else
        {
            state.engine->setLight(Light(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f)));
        }
    }
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size()) {
        return;
    }

    state.sceneEntries[index].translation = translation;
    state.sceneEntries[index].rotation = rotation;
    state.sceneEntries[index].scale = scale;

    if (index < state.engine->models.size()) {
        auto& model = state.engine->models[index];
        model->setSceneTransform(glm::vec3(translation.x(), translation.y(), translation.z()),
                                 glm::vec3(rotation.x(), rotation.y(), rotation.z()),
                                 glm::vec3(scale.x(), scale.y(), scale.z()));
    }
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemPaintOverride(int index, bool enabled, const QVector3D& color)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size())
    {
        return;
    }

    state.sceneEntries[index].paintOverrideEnabled = enabled;
    state.sceneEntries[index].paintOverrideColor = color;

    if (index < state.engine->models.size() && state.engine->models[index])
    {
        state.engine->models[index]->setPaintOverride(enabled, glm::vec3(color.x(), color.y(), color.z()));
    }
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemAnimationState(int index, const QString& activeClip, bool playing, bool loop, float speed)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size())
    {
        return;
    }
    state.sceneEntries[index].activeAnimationClip = activeClip;
    state.sceneEntries[index].animationPlaying = playing;
    state.sceneEntries[index].animationLoop = loop;
    state.sceneEntries[index].animationSpeed = speed;
    notifySceneChanged();
}

void ViewportHostWidget::renameSceneItem(int index, const QString& name)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size())
    {
        return;
    }

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    state.sceneEntries[index].name = trimmed;
    notifySceneChanged();
}

void ViewportHostWidget::setSceneItemVisible(int index, bool visible)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size()) {
        return;
    }
    state.sceneEntries[index].visible = visible;
    if (index < state.engine->models.size()) {
        state.engine->models[index]->visible = visible;
    }
    notifySceneChanged();
}

void ViewportHostWidget::deleteSceneItem(int index)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size())
    {
        return;
    }

    state.sceneEntries.removeAt(index);
    if (state.engine && index < static_cast<int>(state.engine->models.size()))
    {
        if (state.engine->logicalDevice != VK_NULL_HANDLE)
        {
            vkQueueWaitIdle(state.engine->getGraphicsQueue());
            vkDeviceWaitIdle(state.engine->logicalDevice);
        }
        state.engine->models.erase(state.engine->models.begin() + index);
    }

    if (state.sceneEntries.isEmpty())
    {
        state.currentAssetPath.clear();
    }
    else if (index - 1 >= 0)
    {
        state.currentAssetPath = state.sceneEntries[index - 1].sourcePath;
    }
    else
    {
        state.currentAssetPath = state.sceneEntries.front().sourcePath;
    }
    notifySceneChanged();
}

void ViewportHostWidget::setPrimitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex, const QString& cullMode)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (!state.engine || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return;
    }
    if (sceneIndex >= static_cast<int>(state.engine->models.size()) || !state.engine->models[sceneIndex])
    {
        return;
    }
    auto& model = state.engine->models[sceneIndex];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return;
    }
    auto& mesh = model->meshes[meshIndex];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[primitiveIndex])
    {
        return;
    }

    PrimitiveCullMode mode = PrimitiveCullMode::Back;
    if (cullMode == QStringLiteral("none"))
    {
        mode = PrimitiveCullMode::Disabled;
    }
    else if (cullMode == QStringLiteral("front"))
    {
        mode = PrimitiveCullMode::Front;
    }
    mesh.primitives[primitiveIndex]->cullMode = mode;
}

void ViewportHostWidget::relocateSceneItemInFrontOfCamera(int index)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size() || !state.camera || !state.engine) {
        return;
    }
    if (index >= static_cast<int>(state.engine->models.size()) || !state.engine->models[index])
    {
        return;
    }

    const auto& model = state.engine->models[index];
    const glm::vec3 front = cameraForwardVector(state.camera->cameraRotation);
    const float distance = framingDistanceForModel(*model);
    const glm::vec3 desiredCenter = state.camera->cameraPos + front * distance;
    const glm::vec3 currentCenter = model->boundsCenter;
    const glm::vec3 delta = desiredCenter - currentCenter;
    auto& entry = state.sceneEntries[index];
    const QVector3D translation = entry.translation + QVector3D(delta.x, delta.y, delta.z);
    updateSceneItemTransform(index, translation, entry.rotation, entry.scale);
}

void ViewportHostWidget::focusSceneItem(int index)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size() || !state.camera || !state.engine)
    {
        return;
    }
    if (index >= static_cast<int>(state.engine->models.size()) || !state.engine->models[index])
    {
        return;
    }

    const auto& model = state.engine->models[index];
    const glm::vec3 worldCenter = model->boundsCenter;
    const float distance = framingDistanceForModel(*model);
    const glm::vec3 toTarget = worldCenter - state.camera->cameraPos;
    const glm::vec3 front = glm::length(toTarget) > 1e-6f
        ? glm::normalize(toTarget)
        : cameraForwardVector(state.camera->cameraRotation);
    state.camera->cameraRotation = cameraRotationForDirection(front);
    state.camera->cameraPos = worldCenter - front * distance;
    state.camera->update(0.0f);
    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
}

void ViewportHostWidget::setSceneChangedCallback(std::function<void(const QList<SceneItem>&)> callback)
{
    m_sceneChangedCallback = std::move(callback);
}

void ViewportHostWidget::setCameraChangedCallback(std::function<void()> callback)
{
    m_cameraChangedCallback = std::move(callback);
}

void ViewportHostWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_initialized && !m_initScheduled)
    {
        m_initScheduled = true;
        QTimer::singleShot(0, this, [this]()
        {
            m_initScheduled = false;
            ensureViewportInitialized();
        });
    }
}

void ViewportHostWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncEmbeddedWindowGeometry();
}

void ViewportHostWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
#ifdef __linux__
    auto& state = viewportState();
    if (state.display && state.display->window)
    {
        X11Display* xDisplay = glfwGetX11Display();
        ::Window child = glfwGetX11Window(state.display->window);
        if (xDisplay && child != 0)
        {
            XSetInputFocus(xDisplay, child, RevertToParent, CurrentTime);
            XFlush(xDisplay);
        }
    }
#endif
}

void ViewportHostWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera)
    {
        state.camera->clearInputState();
    }
    if (m_cameraChangedCallback) {
        m_cameraChangedCallback();
    }
}

void ViewportHostWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    QWidget::mousePressEvent(event);
}

void ViewportHostWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        qDebug() << "[ViewportHost] dragEnterEvent ignored: no URLs";
        return;
    }

    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isRenderableAsset(url.toLocalFile()))
        {
            qDebug() << "[ViewportHost] dragEnterEvent accepted:" << url.toLocalFile();
            event->acceptProposedAction();
            return;
        }
    }

    qDebug() << "[ViewportHost] dragEnterEvent rejected URLs:" << event->mimeData()->urls();
}

void ViewportHostWidget::dropEvent(QDropEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        qDebug() << "[ViewportHost] dropEvent ignored: no URLs";
        return;
    }

    bool accepted = false;
    for (const QUrl& url : event->mimeData()->urls())
    {
        const QString path = url.toLocalFile();
        if (path.isEmpty() || !isRenderableAsset(path))
        {
            qDebug() << "[ViewportHost] dropEvent skipping non-renderable path:" << path;
            continue;
        }
        qDebug() << "[ViewportHost] dropEvent adding asset:" << path;
        addAssetToScene(path);
        accepted = true;
    }

    if (accepted)
    {
        event->acceptProposedAction();
    }
    else
    {
        qDebug() << "[ViewportHost] dropEvent accepted nothing";
    }
}

void ViewportHostWidget::ensureViewportInitialized()
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (m_initialized)
    {
        return;
    }

    try
    {
        state.engine = std::make_unique<Engine>();
        state.display = state.engine->createWindow(width(), height(), "Motive Embedded Viewport", false, state.use2DPipeline, true);
        state.camera = new Camera(state.engine.get(), state.display, glm::vec3(0.0f, 0.0f, 3.0f), glm::vec2(glm::radians(0.0f), 0.0f));
        state.camera->moveSpeed = state.cameraSpeed;
        state.display->addCamera(state.camera);
        state.display->setBackgroundColor(state.bgColorR, state.bgColorG, state.bgColorB);
        if (state.sceneLight.exists)
        {
            const Light light = engineLightFromSceneLight(state.sceneLight);
            state.sceneLight.ambient = QVector3D(light.ambient.x, light.ambient.y, light.ambient.z);
            state.sceneLight.diffuse = QVector3D(light.diffuse.x, light.diffuse.y, light.diffuse.z);
            state.engine->setLight(light);
        }
        m_initialized = true;

        if (!state.pendingSceneEntries.isEmpty())
        {
            const QList<EmbeddedViewportState::SceneEntry> pendingEntries = state.pendingSceneEntries;
            state.pendingSceneEntries.clear();
            qDebug() << "[ViewportHost] Restoring pending scene entries after init:" << pendingEntries.size();
            QList<SceneItem> items;
            for (const auto& entry : pendingEntries)
            {
                items.push_back(SceneItem{entry.name, entry.sourcePath, entry.translation, entry.rotation, entry.scale,
                                          entry.paintOverrideEnabled, entry.paintOverrideColor,
                                          entry.activeAnimationClip, entry.animationPlaying, entry.animationLoop, entry.animationSpeed,
                                          entry.visible});
            }
            loadSceneFromItems(items);
        }
        else
        {
            const std::filesystem::path scenePath = defaultScenePath();
            if (!scenePath.empty())
            {
                state.currentAssetPath = QString::fromStdString(scenePath.string());
                addAssetToScene(state.currentAssetPath);
            }
            else if (!state.currentAssetPath.isEmpty())
            {
                addAssetToScene(state.currentAssetPath);
            }
        }

        embedNativeWindow();
        syncEmbeddedWindowGeometry();
        if (m_statusLabel)
        {
            m_statusLabel->hide();
        }
        m_renderTimer.start();
    }
    catch (const std::exception& ex)
    {
        if (m_statusLabel)
        {
            const QString baseMessage = QStringLiteral("Viewport unavailable:\n%1").arg(QString::fromUtf8(ex.what()));
            const QString helpText = QStringLiteral(
                "\n\nVulkan hardware device not found.\n"
                "Install drivers and retry:\n"
                "  - Intel/AMD: sudo apt-get install mesa-vulkan-drivers\n"
                "  - NVIDIA: install nvidia-driver-XXX\n"
                "Verify with: vulkaninfo");
            m_statusLabel->setText(baseMessage + helpText);
            m_statusLabel->show();
        }
        auto& state = viewportState();
        state.display = nullptr;
        state.camera = nullptr;
        state.engine.reset();
    }
}

void ViewportHostWidget::addAssetToScene(const QString& path)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (path.isEmpty())
    {
        qWarning() << "[ViewportHost] addAssetToScene called with empty path";
        return;
    }

    state.currentAssetPath = path;
    if (!m_initialized || !state.engine)
    {
        qDebug() << "[ViewportHost] Queued scene asset before init:" << path;
        return;
    }

    try
    {
        EmbeddedViewportState::SceneEntry entry{
            QFileInfo(path).completeBaseName(),
            QFileInfo(path).absoluteFilePath(),
            QVector3D(static_cast<float>(state.engine->models.size()) * 1.6f, 0.0f, 0.0f),
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
        loadModelIntoEngine(state, entry);
        if (!state.engine->models.empty() && state.engine->models.back() && entry.activeAnimationClip.isEmpty())
        {
            const auto& clips = state.engine->models.back()->animationClips;
            if (!clips.empty())
            {
                entry.activeAnimationClip = QString::fromStdString(clips.front().name);
            }
        }
        state.sceneEntries.push_back(entry);
        qDebug() << "[ViewportHost] Scene asset added:" << path
                 << "sceneCount=" << state.sceneEntries.size();
        notifySceneChanged();
    }
    catch (const std::exception& ex)
    {
        qWarning() << "[ViewportHost] Failed to add scene asset:" << path << ex.what();
    }
}

void ViewportHostWidget::renderFrame()
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (!m_initialized || !state.display || !state.display->window)
    {
        return;
    }
    if (glfwWindowShouldClose(state.display->window))
    {
        m_renderTimer.stop();
        return;
    }
    state.display->render();
    notifyCameraChangedIfNeeded();
}

void ViewportHostWidget::notifyCameraChangedIfNeeded()
{
    auto& state = viewportState();
    if (!state.camera || !m_cameraChangedCallback)
    {
        return;
    }

    const QVector3D position(state.camera->cameraPos.x, state.camera->cameraPos.y, state.camera->cameraPos.z);
    const QVector3D rotation(state.camera->cameraRotation.y, state.camera->cameraRotation.x, 0.0f);
    if (m_hasEmittedCameraState &&
        vectorsNearlyEqual(position, m_lastEmittedCameraPosition) &&
        vectorsNearlyEqual(rotation, m_lastEmittedCameraRotation))
    {
        return;
    }

    m_lastEmittedCameraPosition = position;
    m_lastEmittedCameraRotation = rotation;
    m_hasEmittedCameraState = true;
    m_cameraChangedCallback();
}

void ViewportHostWidget::embedNativeWindow()
{
#ifdef __linux__
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (!state.display || !state.display->window)
    {
        return;
    }

    X11Display* xDisplay = glfwGetX11Display();
    ::Window child = glfwGetX11Window(state.display->window);
    ::Window parent = static_cast<::Window>(winId());
    if (!xDisplay || child == 0 || parent == 0)
    {
        return;
    }

    XReparentWindow(xDisplay, child, parent, 0, 0);
    XMapWindow(xDisplay, child);
    XFlush(xDisplay);
    glfwShowWindow(state.display->window);
#endif
}

void ViewportHostWidget::syncEmbeddedWindowGeometry()
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (!m_initialized || !state.display || !state.display->window)
    {
        return;
    }

    const int targetWidth = std::max(1, width());
    const int targetHeight = std::max(1, height());
    glfwSetWindowSize(state.display->window, targetWidth, targetHeight);
    state.display->handleFramebufferResize(targetWidth, targetHeight);

#ifdef __linux__
    X11Display* xDisplay = glfwGetX11Display();
    ::Window child = glfwGetX11Window(state.display->window);
    if (xDisplay && child != 0)
    {
        XResizeWindow(xDisplay, child, static_cast<unsigned int>(targetWidth), static_cast<unsigned int>(targetHeight));
        XFlush(xDisplay);
    }
#endif
}

void ViewportHostWidget::notifySceneChanged()
{
    if (m_sceneChangedCallback)
    {
        m_sceneChangedCallback(sceneItems());
    }
}

}  // namespace motive::ui
