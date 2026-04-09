#include "host_widget.h"

#include "camera.h"  // Include for Camera class
#include "asset_loader.h"
#include "camera_controller.h"
#include "hierarchy_builder.h"
#include "viewport_internal_utils.h"
#include "viewport_runtime.h"
#include "scene_controller.h"

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "light.h"
#include "model.h"
#include "object_transform.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QFrame>
#include <QDropEvent>
#include <QFocusEvent>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <vulkan/vulkan.h>

namespace motive::ui {

namespace {

bool couplingRequiresPhysics(const Model& model)
{
    return model.getAnimationPhysicsCoupling() != "AnimationOnly";
}

bool couplingUsesKinematicBody(const Model& model)
{
    return !model.character.isControllable && model.getAnimationPhysicsCoupling() == "Kinematic";
}

QJsonObject primitiveOverrideObject(int meshIndex, int primitiveIndex, const QString& cullMode, bool forceAlphaOne)
{
    return QJsonObject{
        {QStringLiteral("meshIndex"), meshIndex},
        {QStringLiteral("primitiveIndex"), primitiveIndex},
        {QStringLiteral("cullMode"), cullMode},
        {QStringLiteral("forceAlphaOne"), forceAlphaOne}
    };
}

void removePrimitiveOverrideIfDefault(QJsonArray& overrides, int meshIndex, int primitiveIndex, const QString& cullMode, bool forceAlphaOne)
{
    const bool isDefault = (cullMode == QStringLiteral("back")) && !forceAlphaOne;
    for (int i = overrides.size() - 1; i >= 0; --i)
    {
        if (!overrides.at(i).isObject())
        {
            continue;
        }

        const QJsonObject existing = overrides.at(i).toObject();
        if (existing.value(QStringLiteral("meshIndex")).toInt(-1) != meshIndex ||
            existing.value(QStringLiteral("primitiveIndex")).toInt(-1) != primitiveIndex)
        {
            continue;
        }

        if (isDefault)
        {
            overrides.removeAt(i);
        }
        else
        {
            overrides[i] = primitiveOverrideObject(meshIndex, primitiveIndex, cullMode, forceAlphaOne);
        }
        return;
    }

    if (!isDefault)
    {
        overrides.push_back(primitiveOverrideObject(meshIndex, primitiveIndex, cullMode, forceAlphaOne));
    }
}

glm::vec3 followAnchorPosition(const Model& model, const glm::vec3& targetOffset = glm::vec3(0.0f))
{
    if (model.character.isControllable)
    {
        return model.getCharacterPosition() + targetOffset;
    }
    return model.boundsCenter + targetOffset;
}

void reconfigurePhysicsBodyForMode(Model& model, motive::IPhysicsWorld& physicsWorld)
{
    if (!couplingRequiresPhysics(model))
    {
        if (model.getPhysicsBody() && !model.character.isControllable)
        {
            model.disablePhysics(physicsWorld);
        }
        return;
    }

    motive::PhysicsBodyConfig config;
    config.shapeType = model.character.isControllable
        ? motive::CollisionShapeType::Capsule
        : motive::CollisionShapeType::Box;
    config.mass = model.character.isControllable ? 70.0f : 1.0f;
    config.friction = model.character.isControllable ? 0.3f : 0.5f;
    config.restitution = 0.0f;
    config.useModelBounds = true;
    config.isCharacter = model.character.isControllable;
    config.isKinematic = couplingUsesKinematicBody(model);
    config.useGravity = model.getUseGravity();
    config.customGravity = model.getCustomGravity();

    model.enablePhysics(physicsWorld, config);
}

QString makeCameraId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

ViewportHostWidget::ViewportLayout normalizedViewportLayout(const ViewportHostWidget::ViewportLayout& layout)
{
    ViewportHostWidget::ViewportLayout normalized;
    normalized.count = std::clamp(layout.count, 1, 4);
    normalized.cameraIds = layout.cameraIds;
    while (normalized.cameraIds.size() < normalized.count)
    {
        normalized.cameraIds.append(QString());
    }
    while (normalized.cameraIds.size() > normalized.count)
    {
        normalized.cameraIds.removeLast();
    }
    return normalized;
}

}

ViewportHostWidget::ViewportHostWidget(QWidget* parent)
    : QWidget(parent)
    , m_runtime(std::make_unique<ViewportRuntime>())
    , m_sceneController(std::make_unique<ViewportSceneController>(*m_runtime))
    , m_cameraController(std::make_unique<ViewportCameraController>(*m_runtime, *m_sceneController))
    , m_hierarchyBuilder(std::make_unique<ViewportHierarchyBuilder>(*m_runtime, *m_sceneController, m_sceneLight))
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

    m_viewportLayout = normalizedViewportLayout(m_viewportLayout);
    syncViewportSelectorChoices();
}

ViewportHostWidget::~ViewportHostWidget()
{
    m_renderTimer.stop();
    m_initialized = false;
    if (m_runtime)
    {
        m_runtime->shutdown();
    }
}

void ViewportHostWidget::loadAssetFromPath(const QString& path)
{
    qDebug() << "[ViewportHost] loadAssetFromPath" << path;
    m_sceneController->loadAssetFromPath(path);
}

void ViewportHostWidget::loadSceneFromItems(const QList<SceneItem>& items)
{
    qDebug() << "[ViewportHost] loadSceneFromItems count=" << items.size();
    m_sceneController->loadSceneFromItems(items);
    notifySceneChanged();
}

void ViewportHostWidget::refresh()
{
    qDebug() << "[ViewportHost] Refreshing viewport";
    notifySceneChanged();
}

QString ViewportHostWidget::currentAssetPath() const
{
    return m_sceneController->currentAssetPath();
}

QList<ViewportHostWidget::SceneItem> ViewportHostWidget::sceneItems() const
{
    return m_sceneController->sceneItems();
}

QList<ViewportHostWidget::HierarchyNode> ViewportHostWidget::hierarchyItems() const
{
    return m_hierarchyBuilder->hierarchyItems();
}

QJsonArray ViewportHostWidget::hierarchyJson() const
{
    return m_hierarchyBuilder->hierarchyJson();
}

QJsonArray ViewportHostWidget::sceneProfileJson() const
{
    return m_hierarchyBuilder->sceneProfileJson();
}

QImage ViewportHostWidget::primitiveTexturePreview(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return {};
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return {};
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return {};
    }

    const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return {};
    }

    return mesh.primitives[static_cast<size_t>(primitiveIndex)]->texturePreviewImage;
}

QString ViewportHostWidget::animationExecutionMode(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return QStringLiteral("Static");
    }

    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    const auto classifyPrimitive = [](const Primitive* primitive) -> QString
    {
        if (!primitive)
        {
            return QStringLiteral("Static");
        }
        if (primitive->gpuSkinningEnabled && primitive->skinJointCount > 0)
        {
            return QStringLiteral("GPU skinning");
        }
        if (primitive->skinJointCount > 0)
        {
            return QStringLiteral("CPU skinning");
        }
        return QStringLiteral("Static");
    };

    if (meshIndex >= 0 && primitiveIndex >= 0)
    {
        if (meshIndex >= static_cast<int>(model->meshes.size()))
        {
            return QStringLiteral("Static");
        }
        const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
        if (primitiveIndex >= static_cast<int>(mesh.primitives.size()))
        {
            return QStringLiteral("Static");
        }
        return classifyPrimitive(mesh.primitives[static_cast<size_t>(primitiveIndex)].get());
    }

    bool sawCpu = false;
    for (const auto& mesh : model->meshes)
    {
        for (const auto& primitive : mesh.primitives)
        {
            const QString mode = classifyPrimitive(primitive.get());
            if (mode == QStringLiteral("GPU skinning"))
            {
                return mode;
            }
            if (mode == QStringLiteral("CPU skinning"))
            {
                sawCpu = true;
            }
        }
    }

    if (sawCpu)
    {
        return QStringLiteral("CPU skinning");
    }
    if (!model->animationClips.empty())
    {
        return QStringLiteral("Animated");
    }
    return QStringLiteral("Static");
}

QString ViewportHostWidget::primitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return QStringLiteral("back");
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return QStringLiteral("back");
    }
    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return QStringLiteral("back");
    }
    const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return QStringLiteral("back");
    }

    switch (mesh.primitives[static_cast<size_t>(primitiveIndex)]->cullMode)
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

bool ViewportHostWidget::primitiveForceAlphaOne(int sceneIndex, int meshIndex, int primitiveIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return false;
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return false;
    }
    const auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return false;
    }
    const auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return false;
    }
    return mesh.primitives[static_cast<size_t>(primitiveIndex)]->forceAlphaOne;
}

QStringList ViewportHostWidget::animationClipNames(int sceneIndex) const
{
    QStringList clips;
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return clips;
    }

    for (const auto& clip : m_runtime->engine()->models[static_cast<size_t>(sceneIndex)]->animationClips)
    {
        clips.push_back(QString::fromStdString(clip.name));
    }
    return clips;
}

bool ViewportHostWidget::hasSceneLight() const
{
    return m_sceneLight.exists;
}

ViewportHostWidget::SceneLight ViewportHostWidget::sceneLight() const
{
    return m_sceneLight;
}

QVector3D ViewportHostWidget::cameraPosition() const
{
    return m_cameraController->cameraPosition();
}

QVector3D ViewportHostWidget::cameraRotation() const
{
    return m_cameraController->cameraRotation();
}

float ViewportHostWidget::cameraSpeed() const
{
    return m_cameraController->cameraSpeed();
}

QString ViewportHostWidget::renderPath() const
{
    return m_runtime->use2DPipeline() ? QStringLiteral("flat2d") : QStringLiteral("forward3d");
}

bool ViewportHostWidget::meshConsolidationEnabled() const
{
    return m_sceneController->meshConsolidationEnabled();
}

ViewportHostWidget::PerformanceMetrics ViewportHostWidget::performanceMetrics() const
{
    PerformanceMetrics metrics;
    metrics.renderIntervalMs = m_renderTimer.interval();
    metrics.renderTimerActive = m_renderTimer.isActive();
    metrics.viewportWidth = width();
    metrics.viewportHeight = height();
    
    if (m_initialized && m_runtime && m_runtime->display())
    {
        metrics.currentFps = m_runtime->display()->getCurrentFps();
    }
    
    return metrics;
}

void ViewportHostWidget::enableCharacterControl(int sceneIndex, bool enabled)
{
    setCharacterControlState(sceneIndex, enabled, true);
}

void ViewportHostWidget::setCharacterControlState(int sceneIndex, bool enabled, bool repositionForCharacterMode)
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return;
    }
    
    auto& model = m_runtime->engine()->models[sceneIndex];
    if (!model)
    {
        return;
    }
    
    model->character.isControllable = enabled;
    
    // Only the explicit editor character-control action should reposition the model.
    if (enabled && repositionForCharacterMode)
    {
        glm::vec3 pos = glm::vec3(model->worldTransform[3]);
        pos.y = 5.0f;  // Start 5 units above ground (closer drop)
        model->worldTransform[3] = glm::vec4(pos, 1.0f);
        model->recomputeBounds();
        model->character.velocity = glm::vec3(0.0f);
        model->character.isGrounded = false;
        qDebug() << "[ViewportHost] Character teleported to Y=5 to drop by gravity";
        
        // Immediately position camera behind character
        if (m_runtime->camera())
        {
            const float followDist = 3.0f;
            const float heightOff = 0.8f;
            glm::vec3 camPos = pos;
            camPos.z += followDist;
            camPos.y += heightOff;
            m_runtime->camera()->cameraPos = camPos;
            
            // Calculate rotation to look at character
            glm::vec3 lookTarget = pos + glm::vec3(0.0f, 0.8f, 0.0f);
            glm::vec3 front = glm::normalize(lookTarget - camPos);
            float yaw = atan2(front.x, front.z) + 3.14159f;
            float pitch = -asin(glm::clamp(front.y, -1.0f, 1.0f));
            if (yaw > 3.14159f) yaw -= 2 * 3.14159f;
            m_runtime->camera()->cameraRotation = glm::vec2(yaw, pitch);
            m_runtime->camera()->update(0);
            
            qDebug() << "[ViewportHost] Camera positioned at:" << camPos.x << camPos.y << camPos.z;
        }
    }
    else if (!enabled)
    {
        model->character.velocity = glm::vec3(0.0f);
        model->character.inputDir = glm::vec3(0.0f);
        model->character.isGrounded = false;
        model->character.jumpRequested = false;
        model->character.keyW = false;
        model->character.keyA = false;
        model->character.keyS = false;
        model->character.keyD = false;
    }
    
    // Set as camera's character target
    if (m_runtime->camera())
    {
        if (enabled)
        {
            m_runtime->camera()->setCharacterTarget(model.get());
        }
        else if (m_runtime->camera()->getCharacterTarget() == model.get())
        {
            m_runtime->camera()->setCharacterTarget(nullptr);
        }
    }
    
    qDebug() << "[ViewportHost] Character control" << (enabled ? "enabled" : "disabled") << "for model" << sceneIndex;
}

bool ViewportHostWidget::isCharacterControlEnabled(int sceneIndex) const
{
    if (!m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return false;
    }
    
    const auto& model = m_runtime->engine()->models[sceneIndex];
    if (!model)
    {
        return false;
    }
    
    return model->character.isControllable;
}

motive::IPhysicsBody* ViewportHostWidget::getPhysicsBodyForSceneItem(int sceneIndex) const
{
    if (!m_runtime || !m_runtime->engine() || sceneIndex < 0 || sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()))
    {
        return nullptr;
    }
    
    const auto& model = m_runtime->engine()->models[sceneIndex];
    if (!model)
    {
        return nullptr;
    }
    
    return model->getPhysicsBody();
}

void ViewportHostWidget::setFreeFlyCameraEnabled(bool enabled)
{
    m_freeFlyCameraEnabled = enabled;
    qDebug() << "[ViewportHost] Free fly camera" << (enabled ? "enabled" : "disabled");
    
    // Update camera behavior based on mode
    if (m_runtime->camera())
    {
        if (enabled)
        {
            // Free fly mode: clear character target so WASD moves camera
            m_runtime->camera()->setCharacterTarget(nullptr);
        }
        else
        {
            // Character follow mode: restore character target and position camera
            for (auto& model : m_runtime->engine()->models)
            {
                if (model && model->character.isControllable)
                {
                    m_runtime->camera()->setCharacterTarget(model.get());
                    
                    // Immediately position camera behind character
                    glm::vec3 charPos = model->getCharacterPosition();
                    const float followDist = 3.0f;
                    const float heightOff = 0.8f;
                    glm::vec3 camPos = charPos;
                    camPos.z += followDist;
                    camPos.y += heightOff;
                    m_runtime->camera()->cameraPos = camPos;
                    
                    // Calculate rotation to look at character
                    glm::vec3 lookTarget = charPos + glm::vec3(0.0f, 0.8f, 0.0f);
                    glm::vec3 front = glm::normalize(lookTarget - camPos);
                    float yaw = atan2(front.x, front.z) + 3.14159f;
                    float pitch = -asin(glm::clamp(front.y, -1.0f, 1.0f));
                    if (yaw > 3.14159f) yaw -= 2 * 3.14159f;
                    m_runtime->camera()->cameraRotation = glm::vec2(yaw, pitch);
                    m_runtime->camera()->update(0);
                    
                    qDebug() << "[ViewportHost] Camera positioned behind character at:" << camPos.x << camPos.y << camPos.z;
                    break;
                }
            }
        }
    }
}

bool ViewportHostWidget::isFreeFlyCameraEnabled() const
{
    return m_freeFlyCameraEnabled;
}

QList<ViewportHostWidget::CameraConfig> ViewportHostWidget::cameraConfigs() const
{
    QList<CameraConfig> configs;
    
    if (!m_runtime->display()) {
        return m_pendingCameraConfigs;
    }
    
    for (Camera* camera : m_runtime->display()->cameras) {
        if (!camera) continue;
        if (camera->getCameraId().empty()) {
            camera->setCameraId(makeCameraId().toStdString());
        }
        
        CameraConfig config;
        config.id = QString::fromStdString(camera->getCameraId());
        config.name = QString::fromStdString(camera->getCameraName());
        
        // Check if this is a follow camera
        if (camera->isFollowModeEnabled() && camera->getFollowTargetIndex() >= 0) {
            config.type = CameraConfig::Type::Follow;
            config.followTargetIndex = camera->getFollowSceneIndex();
            config.position = QVector3D(camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z);
            config.rotation = QVector3D(
                glm::degrees(camera->cameraRotation.y),
                glm::degrees(camera->cameraRotation.x),
                0.0f
            );
            
            const FollowSettings& fs = camera->getFollowSettings();
            config.followDistance = fs.distance;
            config.followYaw = glm::degrees(fs.relativeYaw);
            config.followPitch = glm::degrees(fs.relativePitch);
            config.followSmoothSpeed = fs.smoothSpeed;
            config.followTargetOffset = QVector3D(fs.targetOffset.x, fs.targetOffset.y, fs.targetOffset.z);
        } else {
            config.type = CameraConfig::Type::Free;
            config.position = QVector3D(camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z);
            // Convert camera rotation (radians) to degrees
            config.rotation = QVector3D(
                glm::degrees(camera->cameraRotation.y),  // yaw
                glm::degrees(camera->cameraRotation.x),  // pitch
                0.0f
            );
        }
        
        configs.append(config);
    }
    
    return configs;
}

ViewportHostWidget::ViewportLayout ViewportHostWidget::viewportLayout() const
{
    return normalizedViewportLayout(m_viewportLayout);
}

void ViewportHostWidget::setViewportLayout(const ViewportLayout& layout)
{
    const ViewportLayout normalized = normalizedViewportLayout(layout);
    if (m_viewportLayout.count == normalized.count && m_viewportLayout.cameraIds == normalized.cameraIds)
    {
        return;
    }

    m_viewportLayout = normalized;
    updateViewportLayout();
    syncViewportSelectorChoices();
    layoutViewportSelectors();
    m_focusedViewportIndex = std::clamp(m_focusedViewportIndex, 0, std::max(0, viewportCount() - 1));
    updateViewportBorders();

    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }

    syncViewportSelectorChoices();
}

void ViewportHostWidget::setViewportCount(int count)
{
    ViewportLayout layout = viewportLayout();
    layout.count = count;
    setViewportLayout(layout);
}

int ViewportHostWidget::viewportCount() const
{
    return normalizedViewportLayout(m_viewportLayout).count;
}

// Helper function for creating follow cameras with full settings
static Camera* createFollowCameraInternal(Display* display, Engine* engine, int sceneIndex, 
                                           float distance, float yaw, float pitch, 
                                           float smoothSpeed, const QVector3D& targetOffset)
{
    if (!engine || sceneIndex < 0 || 
        sceneIndex >= static_cast<int>(engine->models.size()) ||
        !engine->models[static_cast<size_t>(sceneIndex)]) {
        qDebug() << "[ViewportHost] Cannot create follow camera (internal): invalid scene index" << sceneIndex;
        return nullptr;
    }
    
    if (!display) {
        qDebug() << "[ViewportHost] Cannot create follow camera (internal): display not initialized";
        return nullptr;
    }
    
    // Build a unique name for this follow camera
    QString cameraName = QStringLiteral("Follow Cam (Scene %1)").arg(sceneIndex);
    std::string cameraNameStd = cameraName.toStdString();
    
    // Get target model
    Model* targetModel = engine->models[static_cast<size_t>(sceneIndex)].get();
    if (!targetModel) {
        return nullptr;
    }
    
    // Calculate initial camera position based on settings
    glm::vec3 targetCenter = followAnchorPosition(
        *targetModel,
        glm::vec3(targetOffset.x(), targetOffset.y(), targetOffset.z()));
    float yawRad = glm::radians(yaw);
    float pitchRad = glm::radians(pitch);
    
    glm::vec3 offset;
    offset.x = sin(yawRad) * cos(pitchRad) * distance;
    offset.y = sin(pitchRad) * distance;
    offset.z = cos(yawRad) * cos(pitchRad) * distance;
    
    glm::vec3 initialPos = targetCenter + offset;
    
    // Calculate initial rotation to look at target
    glm::vec3 toTarget = targetCenter - initialPos;
    float initialYaw = atan2(toTarget.x, toTarget.z) + 3.14159f;
    float initialPitch = -asin(glm::clamp(toTarget.y / glm::length(toTarget), -1.0f, 1.0f));
    
    // Create the follow camera
    Camera* followCam = display->createCamera(
        cameraNameStd,
        initialPos,
        glm::vec2(initialYaw, initialPitch)
    );
    
    if (!followCam) {
        qDebug() << "[ViewportHost] Failed to create follow camera (internal)";
        return nullptr;
    }
    
    // Configure follow settings with all parameters
    FollowSettings followSettings;
    followSettings.relativeYaw = yawRad;
    followSettings.relativePitch = pitchRad;
    followSettings.distance = distance;
    followSettings.smoothSpeed = smoothSpeed;
    followSettings.targetOffset = glm::vec3(targetOffset.x(), targetOffset.y(), targetOffset.z());
    followSettings.enabled = true;
    
    followCam->setFollowTarget(sceneIndex, followSettings);
    followCam->setControlsEnabled(false);  // Follow cameras don't have direct controls
    followCam->setCameraId(makeCameraId().toStdString());
    
    qDebug() << "[ViewportHost] Created follow camera (internal) for scene" << sceneIndex 
             << "distance" << distance << "yaw" << yaw << "pitch" << pitch 
             << "smoothSpeed" << smoothSpeed;
    
    return followCam;
}

void ViewportHostWidget::setCameraConfigs(const QList<CameraConfig>& configs)
{
    m_pendingCameraConfigs = configs;

    if (!m_runtime->display() || !m_runtime->engine()) {
        return;
    }
    
    // Clear existing cameras (except we'll recreate them)
    auto& cameras = m_runtime->display()->cameras;
    while (!cameras.empty()) {
        m_runtime->display()->removeCamera(cameras.back());
    }
    
    // Recreate cameras from configs
    for (const auto& config : configs) {
        if (config.type == CameraConfig::Type::Follow && config.followTargetIndex >= 0) {
            // Check if target model exists
            if (config.followTargetIndex >= static_cast<int>(m_runtime->engine()->models.size()) ||
                !m_runtime->engine()->models[static_cast<size_t>(config.followTargetIndex)]) {
                qDebug() << "[ViewportHost] Deferring follow camera restore: target scene index"
                         << config.followTargetIndex << "not loaded yet";
                continue;
            }
            
            // Create follow camera with full settings
            Camera* followCam = createFollowCameraInternal(
                m_runtime->display(),
                m_runtime->engine(),
                config.followTargetIndex, 
                config.followDistance, 
                config.followYaw, 
                config.followPitch,
                config.followSmoothSpeed,
                config.followTargetOffset
            );
            if (followCam) {
                followCam->setCameraId((config.id.isEmpty() ? makeCameraId() : config.id).toStdString());
                followCam->setCameraName(config.name.toStdString());
            }
        } else {
            // Create free camera
            glm::vec3 pos(config.position.x(), config.position.y(), config.position.z());
            glm::vec2 rot(glm::radians(config.rotation.y()), glm::radians(config.rotation.x()));
            Camera* cam = m_runtime->display()->createCamera(config.name.toStdString(), pos, rot);
            if (cam) {
                cam->setCameraId((config.id.isEmpty() ? makeCameraId() : config.id).toStdString());
                cam->setControlsEnabled(true);
            }
        }
    }
    
    // Ensure we have at least one camera
    if (m_runtime->display()->cameras.empty()) {
        Camera* camera = m_runtime->display()->createCamera("Main Camera");
        if (camera) {
            camera->setCameraId(makeCameraId().toStdString());
        }
    }

    updateViewportLayout();
    syncViewportSelectorChoices();
}

int ViewportHostWidget::ensureFollowCamera(int sceneIndex, float distance, float yaw, float pitch)
{
    if (!m_runtime->engine() || sceneIndex < 0 || 
        sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) ||
        !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)]) {
        qDebug() << "[ViewportHost] Cannot create follow camera: invalid scene index" << sceneIndex;
        return -1;
    }
    
    if (!m_runtime->display()) {
        qDebug() << "[ViewportHost] Cannot create follow camera: display not initialized";
        return -1;
    }
    
    // Build a unique name for this follow camera
    QString cameraName = QStringLiteral("Follow Cam (Scene %1)").arg(sceneIndex);
    std::string cameraNameStd = cameraName.toStdString();
    
    // Check if a follow camera already exists for this scene
    Camera* existingCamera = m_runtime->display()->findCameraByName(cameraNameStd);
    if (existingCamera) {
        auto& cameras = m_runtime->display()->cameras;
        auto it = std::find(cameras.begin(), cameras.end(), existingCamera);
        if (it != cameras.end()) {
            const int existingIndex = static_cast<int>(std::distance(cameras.begin(), it));
            qDebug() << "[ViewportHost] Reusing existing follow camera for scene" << sceneIndex;
            return existingIndex;
        }
    }
    
    // Get target model
    Model* targetModel = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)].get();
    if (!targetModel) {
        return -1;
    }
    
    // Calculate initial camera position based on settings
    glm::vec3 targetCenter = followAnchorPosition(*targetModel);
    float yawRad = glm::radians(yaw);
    float pitchRad = glm::radians(pitch);
    
    glm::vec3 offset;
    offset.x = sin(yawRad) * cos(pitchRad) * distance;
    offset.y = sin(pitchRad) * distance;
    offset.z = cos(yawRad) * cos(pitchRad) * distance;
    
    glm::vec3 initialPos = targetCenter + offset;
    
    // Calculate initial rotation to look at target
    glm::vec3 toTarget = targetCenter - initialPos;
    float initialYaw = atan2(toTarget.x, toTarget.z) + 3.14159f;
    float initialPitch = -asin(glm::clamp(toTarget.y / glm::length(toTarget), -1.0f, 1.0f));
    
    // Create the follow camera
    Camera* followCam = m_runtime->display()->createCamera(
        cameraNameStd,
        initialPos,
        glm::vec2(initialYaw, initialPitch)
    );
    
    if (!followCam) {
        qDebug() << "[ViewportHost] Failed to create follow camera";
        return -1;
    }
    if (followCam->getCameraId().empty()) {
        followCam->setCameraId(makeCameraId().toStdString());
    }
    
    // Configure follow settings
    FollowSettings followSettings;
    followSettings.relativeYaw = yawRad;
    followSettings.relativePitch = pitchRad;
    followSettings.distance = distance;
    followSettings.smoothSpeed = 5.0f;  // Default smoothing
    followSettings.targetOffset = glm::vec3(0.0f);
    followSettings.enabled = true;
    
    followCam->setFollowTarget(sceneIndex, followSettings);
    followCam->setControlsEnabled(false);  // Follow cameras don't have direct controls
    
    qDebug() << "[ViewportHost] Created follow camera for scene" << sceneIndex 
             << "distance" << distance << "yaw" << yaw << "pitch" << pitch;

    updateViewportLayout();
    syncViewportSelectorChoices();
    return static_cast<int>(m_runtime->display()->cameras.size()) - 1;
}

void ViewportHostWidget::deleteCamera(int cameraIndex)
{
    if (!m_runtime->display()) {
        return;
    }
    
    auto& cameras = m_runtime->display()->cameras;
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameras.size())) {
        return;
    }
    
    Camera* camera = cameras[cameraIndex];
    m_runtime->display()->removeCamera(camera);
    
    // Ensure we have at least one camera
    if (cameras.empty() && m_runtime->engine()) {
        Camera* newCamera = m_runtime->display()->createCamera("Main Camera");
        if (newCamera) {
            newCamera->setCameraId(makeCameraId().toStdString());
        }
    }

    updateViewportLayout();
    syncViewportSelectorChoices();
}

int ViewportHostWidget::activeCameraIndex() const
{
    // The active camera is always at index 0 in our implementation
    return 0;
}

int ViewportHostWidget::cameraIndexForId(const QString& cameraId) const
{
    if (!m_runtime->display() || cameraId.isEmpty())
    {
        return -1;
    }

    const auto& cameras = m_runtime->display()->cameras;
    for (size_t i = 0; i < cameras.size(); ++i)
    {
        if (cameras[i] && QString::fromStdString(cameras[i]->getCameraId()) == cameraId)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QString ViewportHostWidget::activeCameraId() const
{
    if (!m_runtime->display())
    {
        return {};
    }

    Camera* camera = m_runtime->display()->getActiveCamera();
    return camera ? QString::fromStdString(camera->getCameraId()) : QString();
}

void ViewportHostWidget::setActiveCamera(int cameraIndex)
{
    if (!m_runtime->display()) {
        return;
    }
    
    auto& cameras = m_runtime->display()->cameras;
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameras.size())) {
        return;
    }
    
    const auto* models = m_runtime->engine() ? &m_runtime->engine()->models : nullptr;
    Camera* previousActiveCamera = cameras.empty() ? nullptr : cameras.front();
    const int previousTargetIndex = (previousActiveCamera && previousActiveCamera->isFollowModeEnabled())
        ? previousActiveCamera->getFollowTargetIndex()
        : -1;

    // Get the camera that will become active (before rotation)
    Camera* newActiveCamera = cameras[cameraIndex];

    if (newActiveCamera && viewportCount() == 1)
    {
        ViewportLayout layout = viewportLayout();
        layout.cameraIds[0] = QString::fromStdString(newActiveCamera->getCameraId());
        m_viewportLayout = normalizedViewportLayout(layout);
    }
    
    // Move the selected camera to the front (index 0) to make it active
    std::rotate(cameras.begin(), cameras.begin() + cameraIndex, cameras.begin() + cameraIndex + 1);
    updateViewportLayout();

    if (newActiveCamera)
    {
        // Keep viewport interaction mode aligned with the active camera type.
        m_freeFlyCameraEnabled = !newActiveCamera->isFollowModeEnabled();
    }

    if (previousActiveCamera && previousActiveCamera != newActiveCamera)
    {
        previousActiveCamera->setCharacterTarget(nullptr);
        previousActiveCamera->clearInputState();
    }
    
    if (previousTargetIndex >= 0 && previousTargetIndex != (newActiveCamera ? newActiveCamera->getFollowTargetIndex() : -1))
    {
        setCharacterControlState(previousTargetIndex, false, false);
    }

    // Now cameras[0] is the active camera.
    // Follow cameras explicitly opt the target into character control, but without teleporting
    // or sharing mutable camera state with other camera objects.
    if (newActiveCamera && newActiveCamera->isFollowModeEnabled()) {
        int targetIndex = newActiveCamera->getFollowTargetIndex();
        if (targetIndex >= 0 && m_runtime->engine() && 
            targetIndex < static_cast<int>(m_runtime->engine()->models.size()) &&
            m_runtime->engine()->models[static_cast<size_t>(targetIndex)]) {
            
            Model* targetModel = m_runtime->engine()->models[static_cast<size_t>(targetIndex)].get();
            if (targetModel) {
                setCharacterControlState(targetIndex, true, false);
                newActiveCamera->setCharacterTarget(targetModel);
                newActiveCamera->updateFollow(0.0f, *models);
                newActiveCamera->update(0);
                qDebug() << "[ViewportHost] Follow camera activated, character control enabled for scene" << targetIndex;
            }
        }
    } else if (newActiveCamera) {
        if (previousTargetIndex >= 0)
        {
            setCharacterControlState(previousTargetIndex, false, false);
        }
        newActiveCamera->setCharacterTarget(nullptr);
    }

    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
}

void ViewportHostWidget::updateCameraConfig(int cameraIndex, const CameraConfig& config)
{
    if (!m_runtime->display()) {
        return;
    }
    
    auto& cameras = m_runtime->display()->cameras;
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameras.size())) {
        return;
    }
    
    Camera* camera = cameras[cameraIndex];
    if (!camera) return;
    
    if (!config.id.isEmpty()) {
        camera->setCameraId(config.id.toStdString());
    }
    camera->setCameraName(config.name.toStdString());
    
    if (config.type == CameraConfig::Type::Follow && config.followTargetIndex >= 0) {
        // Set up follow settings
        FollowSettings fs;
        fs.relativeYaw = glm::radians(config.followYaw);
        fs.relativePitch = glm::radians(config.followPitch);
        fs.distance = config.followDistance;
        fs.smoothSpeed = config.followSmoothSpeed;
        fs.targetOffset = glm::vec3(config.followTargetOffset.x(), config.followTargetOffset.y(), config.followTargetOffset.z());
        fs.enabled = true;
        
        // Update follow target and settings
        camera->setFollowTarget(config.followTargetIndex, fs);
        camera->setControlsEnabled(false);  // Follow cameras don't have direct controls
    } else if (config.type == CameraConfig::Type::Free) {
        // Update position for free camera
        camera->cameraPos = glm::vec3(config.position.x(), config.position.y(), config.position.z());
        camera->cameraRotation = glm::vec2(glm::radians(config.rotation.y()), glm::radians(config.rotation.x()));
        
        // Disable follow mode if previously a follow camera
        if (camera->isFollowModeEnabled()) {
            FollowSettings fs;
            fs.enabled = false;
            camera->setFollowTarget(-1, fs);
        }
        camera->setControlsEnabled(true);
    }

    updateViewportLayout();
    syncViewportSelectorChoices();
}

void ViewportHostWidget::updateFollowCameras(float dt)
{
    if (!m_runtime->display() || !m_runtime->engine()) {
        return;
    }
    
    // Update all cameras that have follow mode enabled
    const auto& models = m_runtime->engine()->models;
    for (auto* camera : m_runtime->display()->cameras) {
        if (camera && camera->isFollowModeEnabled()) {
            camera->updateFollow(dt, models);
        }
    }
}

QString ViewportHostWidget::cameraIdForViewportIndex(int index) const
{
    const ViewportLayout layout = normalizedViewportLayout(m_viewportLayout);
    if (index < 0 || index >= layout.cameraIds.size())
    {
        return {};
    }
    return layout.cameraIds[index];
}

QRect ViewportHostWidget::viewportRectForIndex(int index) const
{
    const int w = std::max(1, width());
    const int h = std::max(1, height());
    switch (viewportCount())
    {
        case 2:
            return (index == 0) ? QRect(0, 0, w / 2, h) : QRect(w / 2, 0, w - (w / 2), h);
        case 3:
            if (index == 0) return QRect(0, 0, w / 2, h);
            if (index == 1) return QRect(w / 2, 0, w - (w / 2), h / 2);
            return QRect(w / 2, h / 2, w - (w / 2), h - (h / 2));
        case 4:
            if (index == 0) return QRect(0, 0, w / 2, h / 2);
            if (index == 1) return QRect(w / 2, 0, w - (w / 2), h / 2);
            if (index == 2) return QRect(0, h / 2, w / 2, h - (h / 2));
            return QRect(w / 2, h / 2, w - (w / 2), h - (h / 2));
        case 1:
        default:
            return QRect(0, 0, w, h);
    }
}

void ViewportHostWidget::layoutViewportSelectors()
{
    while (m_viewportCameraSelectors.size() < viewportCount())
    {
        auto* combo = new QComboBox(this);
        combo->setStyleSheet(QStringLiteral(
            "QComboBox { background: rgba(16,22,29,220); color: #edf2f7; border: 1px solid #2e3b4a; border-radius: 6px; padding: 3px 8px; }"
            "QComboBox QAbstractItemView { background: #1b2430; color: #edf2f7; border: 1px solid #2e3b4a; selection-background-color: #233142; }"));
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, combo](int idx)
        {
            if (idx < 0)
            {
                return;
            }
            const int viewportIndex = m_viewportCameraSelectors.indexOf(combo);
            if (viewportIndex < 0)
            {
                return;
            }
            ViewportLayout layout = viewportLayout();
            layout.cameraIds[viewportIndex] = combo->currentData().toString();
            setViewportLayout(layout);
            if (layout.count == 1)
            {
                const int cameraIndex = cameraIndexForId(layout.cameraIds[viewportIndex]);
                if (cameraIndex >= 0)
                {
                    setActiveCamera(cameraIndex);
                }
            }
        });
        combo->show();
        combo->raise();
        m_viewportCameraSelectors.append(combo);
    }

    for (int i = 0; i < m_viewportCameraSelectors.size(); ++i)
    {
        QComboBox* combo = m_viewportCameraSelectors[i];
        if (!combo)
        {
            continue;
        }
        combo->setVisible(i < viewportCount());
        if (!combo->isVisible())
        {
            continue;
        }
        const QRect rect = viewportRectForIndex(i);
        combo->setGeometry(rect.x() + 12, rect.y() + 12, std::min(220, std::max(160, rect.width() - 24)), 30);
        combo->raise();
    }

    updateViewportBorders();
}

void ViewportHostWidget::syncViewportSelectorChoices()
{
    const auto configs = cameraConfigs();
    const ViewportLayout layout = viewportLayout();
    layoutViewportSelectors();

    for (int i = 0; i < m_viewportCameraSelectors.size(); ++i)
    {
        QComboBox* combo = m_viewportCameraSelectors[i];
        if (!combo || i >= layout.count)
        {
            continue;
        }

        combo->blockSignals(true);
        combo->clear();
        for (const auto& config : configs)
        {
            combo->addItem(config.name.isEmpty() ? QStringLiteral("Camera") : config.name, config.id);
        }

        int selectedIndex = combo->findData(layout.cameraIds.value(i));
        if (selectedIndex < 0 && combo->count() > 0)
        {
            selectedIndex = std::min(i, combo->count() - 1);
        }
        if (selectedIndex >= 0)
        {
            combo->setCurrentIndex(selectedIndex);
        }
        combo->blockSignals(false);
    }
}

void ViewportHostWidget::updateViewportBorders()
{
    while (m_viewportBorders.size() < viewportCount())
    {
        auto* frame = new QFrame(this);
        frame->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        frame->setStyleSheet(QStringLiteral("QFrame { background: transparent; border: 2px solid #6b7280; }"));
        frame->show();
        frame->raise();
        m_viewportBorders.append(frame);
    }

    for (int i = 0; i < m_viewportBorders.size(); ++i)
    {
        QFrame* frame = m_viewportBorders[i];
        if (!frame)
        {
            continue;
        }
        frame->setVisible(viewportCount() > 1 && i < viewportCount());
        if (!frame->isVisible())
        {
            continue;
        }

        const QRect rect = viewportRectForIndex(i).adjusted(0, 0, -1, -1);
        frame->setGeometry(rect);
        const QString borderColor = (i == m_focusedViewportIndex) ? QStringLiteral("#22c55e")
                                                                  : QStringLiteral("#6b7280");
        frame->setStyleSheet(QStringLiteral(
            "QFrame { background: transparent; border: 2px solid %1; }").arg(borderColor));
        frame->raise();
    }

    for (QComboBox* combo : m_viewportCameraSelectors)
    {
        if (combo && combo->isVisible())
        {
            combo->raise();
        }
    }
}

int ViewportHostWidget::viewportIndexAt(const QPoint& position) const
{
    for (int i = 0; i < viewportCount(); ++i)
    {
        if (viewportRectForIndex(i).contains(position))
        {
            return i;
        }
    }
    return 0;
}

void ViewportHostWidget::setFocusedViewportIndex(int index)
{
    const int clampedIndex = std::clamp(index, 0, std::max(0, viewportCount() - 1));
    m_focusedViewportIndex = clampedIndex;
    updateViewportBorders();

    const int cameraIndex = cameraIndexForId(cameraIdForViewportIndex(clampedIndex));
    if (cameraIndex >= 0)
    {
        setActiveCamera(cameraIndex);
    }
}

void ViewportHostWidget::updateViewportLayout()
{
    if (!m_runtime->display())
    {
        return;
    }

    ViewportLayout layout = normalizedViewportLayout(m_viewportLayout);
    auto& cameras = m_runtime->display()->cameras;
    QStringList availableIds;
    for (Camera* camera : cameras)
    {
        if (camera)
        {
            availableIds.append(QString::fromStdString(camera->getCameraId()));
        }
    }

    for (int i = 0; i < layout.count; ++i)
    {
        if (!availableIds.contains(layout.cameraIds[i]))
        {
            layout.cameraIds[i] = (i < availableIds.size()) ? availableIds[i] : QString();
        }
    }
    m_viewportLayout = layout;

    for (Camera* camera : cameras)
    {
        if (!camera)
        {
            continue;
        }
        const QString id = QString::fromStdString(camera->getCameraId());
        const int viewportIndex = layout.cameraIds.indexOf(id);
        if (viewportIndex < 0)
        {
            camera->setFullscreenViewportEnabled(false);
            camera->setViewport(0.0f, 0.0f, 1.0f, 1.0f);
            continue;
        }

        const QRect rect = viewportRectForIndex(viewportIndex);
        camera->setFullscreenViewportEnabled(false);
        camera->setViewport(rect.x() + (rect.width() * 0.5f),
                            rect.y() + (rect.height() * 0.5f),
                            static_cast<float>(std::max(1, rect.width())),
                            static_cast<float>(std::max(1, rect.height())));
    }

    if (layout.count == 1)
    {
        const int cameraIndex = cameraIndexForId(layout.cameraIds.value(0));
        if (cameraIndex > 0 && cameraIndex < static_cast<int>(cameras.size()))
        {
            std::rotate(cameras.begin(), cameras.begin() + cameraIndex, cameras.begin() + cameraIndex + 1);
        }
    }
}

void ViewportHostWidget::setCameraPosition(const QVector3D& position)
{
    m_cameraController->setCameraPosition(position);
}

void ViewportHostWidget::setCameraRotation(const QVector3D& rotation)
{
    m_cameraController->setCameraRotation(rotation);
}

void ViewportHostWidget::setCameraSpeed(float speed)
{
    m_cameraController->setCameraSpeed(speed);
}

void ViewportHostWidget::resetCamera()
{
    m_cameraController->resetCamera();
}

void ViewportHostWidget::setBackgroundColor(const QColor& color)
{
    m_runtime->setBackgroundColor(color.redF(), color.greenF(), color.blueF());
}

void ViewportHostWidget::setRenderPath(const QString& renderPath)
{
    const bool use2d = renderPath.compare(QStringLiteral("flat2d"), Qt::CaseInsensitive) == 0;
    if (m_runtime->use2DPipeline() == use2d)
    {
        return;
    }

    const QList<SceneItem> items = sceneItems();
    const QVector3D savedCameraPos = cameraPosition();
    const QVector3D savedCameraRot = cameraRotation();
    const float savedCameraSpeed = cameraSpeed();

    m_runtime->setUse2DPipeline(use2d);
    m_renderTimer.stop();
    m_runtime->shutdown();
    m_initialized = false;

    m_sceneController->pendingEntries().clear();
    for (const SceneItem& item : items)
    {
        m_sceneController->pendingEntries().push_back(item);
    }

    ensureViewportInitialized();
    m_cameraController->setCameraSpeed(savedCameraSpeed);
    m_cameraController->setCameraPosition(savedCameraPos);
    m_cameraController->setCameraRotation(savedCameraRot);
}

void ViewportHostWidget::setMeshConsolidationEnabled(bool enabled)
{
    m_sceneController->setMeshConsolidationEnabled(enabled);
    notifySceneChanged();
}

void ViewportHostWidget::createSceneLight()
{
    if (m_sceneLight.exists)
    {
        return;
    }

    m_sceneLight.exists = true;
    applySceneLightToRuntime();
    notifySceneChanged();
}

void ViewportHostWidget::setSceneLight(const SceneLight& light)
{
    m_sceneLight = light;
    applySceneLightToRuntime();
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale)
{
    m_sceneController->updateSceneItemTransform(index, translation, rotation, scale);
    notifySceneChanged();
}

void ViewportHostWidget::setSceneItemMeshConsolidationEnabled(int index, bool enabled)
{
    m_sceneController->setSceneItemMeshConsolidationEnabled(index, enabled);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemPaintOverride(int index, bool enabled, const QVector3D& color)
{
    m_sceneController->updateSceneItemPaintOverride(index, enabled, color);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemAnimationState(int index, const QString& activeClip, bool playing, bool loop, float speed)
{
    m_sceneController->updateSceneItemAnimationState(index, activeClip, playing, loop, speed);
    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemAnimationPhysicsCoupling(int index, const QString& couplingMode)
{
    if (!m_sceneController)
    {
        qWarning() << "[ViewportHost] Cannot update coupling - scene controller is null";
        return;
    }
    m_sceneController->updateSceneItemAnimationPhysicsCoupling(index, couplingMode);

    if (m_runtime && m_runtime->engine() && m_runtime->engine()->getPhysicsWorld() &&
        index >= 0 &&
        index < static_cast<int>(m_runtime->engine()->models.size()) &&
        m_runtime->engine()->models[static_cast<size_t>(index)])
    {
        reconfigurePhysicsBodyForMode(
            *m_runtime->engine()->models[static_cast<size_t>(index)],
            *m_runtime->engine()->getPhysicsWorld());
    }

    notifySceneChanged();
}

void ViewportHostWidget::updateSceneItemPhysicsGravity(int index, bool useGravity, const QVector3D& customGravity)
{
    m_sceneController->updateSceneItemPhysicsGravity(index, useGravity, customGravity);
    notifySceneChanged();
}

void ViewportHostWidget::renameSceneItem(int index, const QString& name)
{
    m_sceneController->renameSceneItem(index, name);
    notifySceneChanged();
}

void ViewportHostWidget::setSceneItemVisible(int index, bool visible)
{
    m_sceneController->setSceneItemVisible(index, visible);
    notifySceneChanged();
}

void ViewportHostWidget::deleteSceneItem(int index)
{
    m_sceneController->deleteSceneItem(index);
    notifySceneChanged();
}

void ViewportHostWidget::setPrimitiveCullMode(int sceneIndex, int meshIndex, int primitiveIndex, const QString& cullMode)
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return;
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return;
    }
    auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return;
    }
    auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
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
    mesh.primitives[static_cast<size_t>(primitiveIndex)]->cullMode = mode;
    if (mesh.primitives[static_cast<size_t>(primitiveIndex)]->ObjectTransformUBOMapped)
    {
        const ObjectTransform updated = mesh.primitives[static_cast<size_t>(primitiveIndex)]->buildObjectTransformData();
        memcpy(mesh.primitives[static_cast<size_t>(primitiveIndex)]->ObjectTransformUBOMapped, &updated, sizeof(updated));
    }

    auto& entries = m_sceneController->loadedEntries();
    if (sceneIndex >= 0 && sceneIndex < entries.size())
    {
        removePrimitiveOverrideIfDefault(entries[sceneIndex].primitiveOverrides,
                                         meshIndex,
                                         primitiveIndex,
                                         cullMode,
                                         mesh.primitives[static_cast<size_t>(primitiveIndex)]->forceAlphaOne);
    }
}

void ViewportHostWidget::setPrimitiveForceAlphaOne(int sceneIndex, int meshIndex, int primitiveIndex, bool enabled)
{
    if (!m_runtime->engine() || sceneIndex < 0 || meshIndex < 0 || primitiveIndex < 0)
    {
        return;
    }
    if (sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) || !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)])
    {
        return;
    }
    auto& model = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)];
    if (meshIndex >= static_cast<int>(model->meshes.size()))
    {
        return;
    }
    auto& mesh = model->meshes[static_cast<size_t>(meshIndex)];
    if (primitiveIndex >= static_cast<int>(mesh.primitives.size()) || !mesh.primitives[static_cast<size_t>(primitiveIndex)])
    {
        return;
    }

    mesh.primitives[static_cast<size_t>(primitiveIndex)]->forceAlphaOne = enabled;
    if (mesh.primitives[static_cast<size_t>(primitiveIndex)]->ObjectTransformUBOMapped)
    {
        const ObjectTransform updated = mesh.primitives[static_cast<size_t>(primitiveIndex)]->buildObjectTransformData();
        memcpy(mesh.primitives[static_cast<size_t>(primitiveIndex)]->ObjectTransformUBOMapped, &updated, sizeof(updated));
    }

    auto& entries = m_sceneController->loadedEntries();
    if (sceneIndex >= 0 && sceneIndex < entries.size())
    {
        removePrimitiveOverrideIfDefault(entries[sceneIndex].primitiveOverrides,
                                         meshIndex,
                                         primitiveIndex,
                                         primitiveCullMode(sceneIndex, meshIndex, primitiveIndex),
                                         enabled);
    }
}

void ViewportHostWidget::relocateSceneItemInFrontOfCamera(int index)
{
    m_cameraController->relocateSceneItemInFrontOfCamera(index);
    notifySceneChanged();
}

void ViewportHostWidget::focusSceneItem(int index)
{
    m_cameraController->focusSceneItem(index);
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
    if (m_runtime)
    {
        m_runtime->resize(width(), height());
    }
    updateViewportLayout();
    layoutViewportSelectors();
    updateViewportBorders();
}

void ViewportHostWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    if (m_runtime)
    {
        m_runtime->focusNativeWindow(static_cast<unsigned long>(winId()));
    }
    updateViewportBorders();
}

void ViewportHostWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (m_runtime)
    {
        m_runtime->clearInputState();
    }
    if (m_cameraChangedCallback)
    {
        m_cameraChangedCallback();
    }
    updateViewportBorders();
}

void ViewportHostWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    setFocusedViewportIndex(viewportIndexAt(event->pos()));
    
    // Start orbiting on right-click (only in character follow mode)
    if (event->button() == Qt::RightButton && !m_freeFlyCameraEnabled)
    {
        m_orbiting = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    
    QWidget::mousePressEvent(event);
}

void ViewportHostWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_orbiting && !m_freeFlyCameraEnabled)
    {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        
        // Update orbit angles based on mouse movement
        const float sensitivity = 0.005f;
        m_orbitYaw -= delta.x() * sensitivity;    // Horizontal rotation
        m_orbitPitch -= delta.y() * sensitivity;  // Vertical rotation
        
        // Clamp pitch to prevent flipping
        m_orbitPitch = glm::clamp(m_orbitPitch, -0.5f, 1.0f);
    }
    
    QWidget::mouseMoveEvent(event);
}

void ViewportHostWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton)
    {
        m_orbiting = false;
        unsetCursor();
    }
    
    QWidget::mouseReleaseEvent(event);
}

void ViewportHostWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_freeFlyCameraEnabled)
    {
        // Zoom in/out by changing orbit distance
        const float zoomSpeed = 0.001f;
        m_orbitDistance -= event->angleDelta().y() * zoomSpeed;
        m_orbitDistance = glm::clamp(m_orbitDistance, kMinOrbitDistance, kMaxOrbitDistance);
    }
    
    QWidget::wheelEvent(event);
}

void ViewportHostWidget::keyPressEvent(QKeyEvent* event)
{
    qDebug() << "[ViewportHost] keyPressEvent:" << event->key();
    
    if (!m_runtime->camera())
    {
        QWidget::keyPressEvent(event);
        return;
    }
    
    int glfwKey = -1;
    switch (event->key())
    {
        case Qt::Key_W: glfwKey = GLFW_KEY_W; break;
        case Qt::Key_A: glfwKey = GLFW_KEY_A; break;
        case Qt::Key_S: glfwKey = GLFW_KEY_S; break;
        case Qt::Key_D: glfwKey = GLFW_KEY_D; break;
        case Qt::Key_Q: glfwKey = GLFW_KEY_Q; break;
        case Qt::Key_E: glfwKey = GLFW_KEY_E; break;
        case Qt::Key_R: glfwKey = GLFW_KEY_R; break;
        case Qt::Key_O: glfwKey = GLFW_KEY_O; break;
        case Qt::Key_P: glfwKey = GLFW_KEY_P; break;
    }
    
    if (glfwKey >= 0)
    {
        m_runtime->camera()->handleKey(glfwKey, 0, GLFW_PRESS, 0);
    }
    else
    {
        QWidget::keyPressEvent(event);
    }
}

void ViewportHostWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (!m_runtime->camera())
    {
        QWidget::keyReleaseEvent(event);
        return;
    }
    
    // Map Qt key to GLFW key and forward to camera
    int glfwKey = -1;
    switch (event->key())
    {
        case Qt::Key_W: glfwKey = GLFW_KEY_W; break;
        case Qt::Key_A: glfwKey = GLFW_KEY_A; break;
        case Qt::Key_S: glfwKey = GLFW_KEY_S; break;
        case Qt::Key_D: glfwKey = GLFW_KEY_D; break;
        case Qt::Key_Q: glfwKey = GLFW_KEY_Q; break;
        case Qt::Key_E: glfwKey = GLFW_KEY_E; break;
        case Qt::Key_R: glfwKey = GLFW_KEY_R; break;
        case Qt::Key_O: glfwKey = GLFW_KEY_O; break;
        case Qt::Key_P: glfwKey = GLFW_KEY_P; break;
    }
    
    if (glfwKey >= 0)
    {
        m_runtime->camera()->handleKey(glfwKey, 0, GLFW_RELEASE, 0);
    }
    else
    {
        QWidget::keyReleaseEvent(event);
    }
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
        if (url.isLocalFile() && detail::isRenderableAsset(url.toLocalFile()))
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
        if (path.isEmpty() || !detail::isRenderableAsset(path))
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
    if (m_initialized)
    {
        return;
    }

    try
    {
        m_runtime->initialize(width(), height(), m_runtime->use2DPipeline());
        m_cameraController->setCameraSpeed(m_cameraController->cameraSpeed());
        applySceneLightToRuntime();
        m_initialized = true;

        if (!m_sceneController->pendingEntries().isEmpty())
        {
            m_sceneController->restorePendingEntries();
            if (!m_pendingCameraConfigs.isEmpty())
            {
                setCameraConfigs(m_pendingCameraConfigs);
            }
            notifySceneChanged();
        }
        else
        {
            const std::filesystem::path scenePath = detail::defaultScenePath();
            if (!scenePath.empty())
            {
                const QString path = QString::fromStdString(scenePath.string());
                m_sceneController->addAssetToScene(path);
            }
            else if (!m_sceneController->currentAssetPath().isEmpty())
            {
                m_sceneController->addAssetToScene(m_sceneController->currentAssetPath());
            }
        }

        m_runtime->embedNativeWindow(static_cast<unsigned long>(winId()));
        m_runtime->resize(width(), height());
        updateViewportLayout();
        syncViewportSelectorChoices();
        layoutViewportSelectors();
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
        m_runtime->shutdown();
    }
}

void ViewportHostWidget::addAssetToScene(const QString& path)
{
    m_sceneController->addAssetToScene(path);
    notifySceneChanged();
}

void ViewportHostWidget::renderFrame()
{
    if (!m_initialized || !m_runtime || !m_runtime->display() || !m_runtime->display()->window)
    {
        return;
    }
    if (glfwWindowShouldClose(m_runtime->display()->window))
    {
        m_renderTimer.stop();
        return;
    }

    static auto lastFrameTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double deltaSeconds = std::chrono::duration<double>(now - lastFrameTime).count();
    lastFrameTime = now;
    
    const float dt = static_cast<float>(deltaSeconds);

    if (m_runtime->engine())
    {
        auto* physicsWorld = m_runtime->engine()->getPhysicsWorld();
        auto& entries = m_sceneController->loadedEntries();
        for (size_t i = 0; i < m_runtime->engine()->models.size() && i < static_cast<size_t>(entries.size()); ++i)
        {
            const auto& entry = entries[static_cast<int>(i)];
            if (m_runtime->engine()->models[i])
            {
                auto& model = m_runtime->engine()->models[i];

                if (physicsWorld && couplingRequiresPhysics(*model) && !model->getPhysicsBody())
                {
                    reconfigurePhysicsBodyForMode(*model, *physicsWorld);
                }

                // Update character/controller state before stepping the world.
                if (model->character.isControllable)
                {
                    if (physicsWorld && couplingRequiresPhysics(*model))
                    {
                        model->updateCharacterPhysics(dt, *physicsWorld);
                    }
                    else
                    {
                        model->updateCharacterPhysics(dt);
                    }
                }
                else if (physicsWorld && model->getPhysicsBody() && couplingUsesKinematicBody(*model))
                {
                    model->getPhysicsBody()->syncTransformToPhysics();
                }

                model->setAnimationPlaybackState(entry.activeAnimationClip.toStdString(),
                                                                         entry.animationPlaying,
                                                                         entry.animationLoop,
                                                                         entry.animationSpeed);
                model->updateAnimation(deltaSeconds);
            }
        }

        if (physicsWorld)
        {
            m_runtime->engine()->updatePhysics(dt);
        }
        
        // Update camera to follow character if enabled
        updateCameraFollowCharacter(dt);
        
        // Update follow cameras (for element tracking)
        updateFollowCameras(dt);
    }

    m_runtime->render();
    notifyCameraChangedIfNeeded();
}

void ViewportHostWidget::updateCameraFollowCharacter(float dt)
{
    // Only follow character in non-free-fly mode
    if (m_freeFlyCameraEnabled)
    {
        return;
    }
    
    if (!m_runtime->camera() || !m_runtime->engine())
    {
        qDebug() << "[CameraFollow] No camera or engine";
        return;
    }
    
    // Find the first controllable character
    Model* characterModel = nullptr;
    for (auto& model : m_runtime->engine()->models)
    {
        if (model && model->character.isControllable)
        {
            characterModel = model.get();
            break;
        }
    }
    
    if (!characterModel)
    {
        static bool logged = false;
        if (!logged)
        {
            qDebug() << "[CameraFollow] No controllable character found";
            logged = true;
        }
        return;
    }
    
    // Get character position
    const glm::vec3 charPos = characterModel->getCharacterPosition();
    
    // Dynamic orbit camera: position based on orbit angles and distance
    // Calculate camera position on sphere around character
    glm::vec3 camOffset;
    camOffset.x = sin(m_orbitYaw) * cos(m_orbitPitch) * m_orbitDistance;
    camOffset.y = sin(m_orbitPitch) * m_orbitDistance + 0.8f;  // Height offset
    camOffset.z = cos(m_orbitYaw) * cos(m_orbitPitch) * m_orbitDistance;
    
    glm::vec3 targetCamPos = charPos + camOffset;
    
    // Smoothly interpolate camera position
    const float followSpeed = 8.0f;
    m_runtime->camera()->cameraPos = glm::mix(m_runtime->camera()->cameraPos, targetCamPos, 
                                               glm::min(followSpeed * dt, 1.0f));
    
    // Camera always looks at character
    const glm::vec3 lookTarget = charPos + glm::vec3(0.0f, 0.8f, 0.0f);
    const glm::vec3 toTarget = lookTarget - m_runtime->camera()->cameraPos;
    
    if (glm::length(toTarget) > 0.001f)
    {
        const glm::vec3 front = glm::normalize(toTarget);
        float yaw = atan2(front.x, front.z) + 3.14159f;
        float pitch = -asin(glm::clamp(front.y, -1.0f, 1.0f));
        if (yaw > 3.14159f) yaw -= 2 * 3.14159f;
        
        m_runtime->camera()->cameraRotation.x = yaw;
        m_runtime->camera()->cameraRotation.y = pitch;
    }
    
    m_runtime->camera()->update(0);
}

void ViewportHostWidget::notifyCameraChangedIfNeeded()
{
    if (!m_runtime->camera() || !m_cameraChangedCallback)
    {
        return;
    }

    const QVector3D position(m_runtime->camera()->cameraPos.x, m_runtime->camera()->cameraPos.y, m_runtime->camera()->cameraPos.z);
    const QVector3D rotation(m_runtime->camera()->cameraRotation.y, m_runtime->camera()->cameraRotation.x, 0.0f);
    if (m_hasEmittedCameraState &&
        detail::vectorsNearlyEqual(position, m_lastEmittedCameraPosition) &&
        detail::vectorsNearlyEqual(rotation, m_lastEmittedCameraRotation))
    {
        return;
    }

    m_lastEmittedCameraPosition = position;
    m_lastEmittedCameraRotation = rotation;
    m_hasEmittedCameraState = true;
    m_cameraChangedCallback();
}

void ViewportHostWidget::notifySceneChanged()
{
    if (!m_sceneChangedCallback)
    {
        return;
    }
    
    // Defer to main thread if called from worker thread
    // This prevents crashes when parallel loader invokes callbacks
    QTimer::singleShot(0, this, [this]() {
        if (m_sceneChangedCallback)
        {
            m_sceneChangedCallback(sceneItems());
        }
    });
}

void ViewportHostWidget::applySceneLightToRuntime()
{
    if (!m_runtime->engine())
    {
        return;
    }

    if (m_sceneLight.exists)
    {
        const Light engineLight = detail::engineLightFromSceneLight(m_sceneLight);
        m_sceneLight.ambient = QVector3D(engineLight.ambient.x, engineLight.ambient.y, engineLight.ambient.z);
        m_sceneLight.diffuse = QVector3D(engineLight.diffuse.x, engineLight.diffuse.y, engineLight.diffuse.z);
        m_runtime->engine()->setLight(engineLight);
    }
    else
    {
        m_runtime->engine()->setLight(Light(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f)));
    }
}

}  // namespace motive::ui
