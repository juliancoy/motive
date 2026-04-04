#include "viewport_host_widget.h"

#include "viewport_asset_loader.h"
#include "viewport_camera_controller.h"
#include "viewport_hierarchy_builder.h"
#include "viewport_internal_utils.h"
#include "viewport_runtime.h"
#include "viewport_scene_controller.h"

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "light.h"
#include "model.h"
#include "object_transform.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>
#include <filesystem>
#include <vulkan/vulkan.h>

namespace motive::ui {

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
    
    // When enabling, teleport character up so he drops down by gravity
    if (enabled)
    {
        glm::vec3 pos = glm::vec3(model->worldTransform[3]);
        pos.y = 5.0f;  // Start 5 units above ground (closer drop)
        model->worldTransform[3] = glm::vec4(pos, 1.0f);
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

bool ViewportHostWidget::createOrJumpToFollowCamera(int sceneIndex, const FollowCameraSettings& settings)
{
    if (!m_runtime->engine() || sceneIndex < 0 || 
        sceneIndex >= static_cast<int>(m_runtime->engine()->models.size()) ||
        !m_runtime->engine()->models[static_cast<size_t>(sceneIndex)]) {
        qDebug() << "[ViewportHost] Cannot create follow camera: invalid scene index" << sceneIndex;
        return false;
    }
    
    if (!m_runtime->display()) {
        qDebug() << "[ViewportHost] Cannot create follow camera: display not initialized";
        return false;
    }
    
    // Build a unique name for this follow camera
    QString cameraName = QStringLiteral("Follow Cam (Scene %1)").arg(sceneIndex);
    std::string cameraNameStd = cameraName.toStdString();
    
    // Check if a follow camera already exists for this scene
    Camera* existingCamera = m_runtime->display()->findCameraByName(cameraNameStd);
    if (existingCamera) {
        // Camera exists - find and activate it
        auto& cameras = m_runtime->display()->cameras;
        auto it = std::find(cameras.begin(), cameras.end(), existingCamera);
        if (it != cameras.end()) {
            // Move this camera to the front of the list (make it active)
            std::rotate(cameras.begin(), it, it + 1);
            qDebug() << "[ViewportHost] Switched to existing follow camera for scene" << sceneIndex;
            return true;
        }
    }
    
    // Get target model
    Model* targetModel = m_runtime->engine()->models[static_cast<size_t>(sceneIndex)].get();
    if (!targetModel) {
        return false;
    }
    
    // Calculate initial camera position based on settings
    glm::vec3 targetCenter = targetModel->boundsCenter;
    float yawRad = glm::radians(settings.relativeYaw);
    float pitchRad = glm::radians(settings.relativePitch);
    float dist = settings.distance;
    
    glm::vec3 offset;
    offset.x = sin(yawRad) * cos(pitchRad) * dist;
    offset.y = sin(pitchRad) * dist;
    offset.z = cos(yawRad) * cos(pitchRad) * dist;
    
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
        return false;
    }
    
    // Configure follow settings
    FollowSettings followSettings;
    followSettings.relativeYaw = yawRad;
    followSettings.relativePitch = pitchRad;
    followSettings.distance = dist;
    followSettings.smoothSpeed = settings.smoothSpeed;
    followSettings.targetOffset = glm::vec3(settings.targetOffset.x(), settings.targetOffset.y(), settings.targetOffset.z());
    followSettings.enabled = true;
    
    followCam->setFollowTarget(targetModel, followSettings);
    
    // Disable direct controls on follow camera (user controls the offset, not the camera directly)
    followCam->setControlsEnabled(false);
    
    qDebug() << "[ViewportHost] Created follow camera for scene" << sceneIndex 
             << "distance" << dist << "yaw" << settings.relativeYaw << "pitch" << settings.relativePitch;
    
    // Move the follow camera to be the active camera (first in list)
    auto& cameras = m_runtime->display()->cameras;
    if (cameras.size() > 1) {
        std::rotate(cameras.begin(), cameras.end() - 1, cameras.end());
    }
    
    return true;
}

bool ViewportHostWidget::hasFollowCamera(int sceneIndex) const
{
    if (!m_runtime->display()) {
        return false;
    }
    
    QString cameraName = QStringLiteral("Follow Cam (Scene %1)").arg(sceneIndex);
    return m_runtime->display()->findCameraByName(cameraName.toStdString()) != nullptr;
}

ViewportHostWidget::FollowCameraSettings ViewportHostWidget::currentFollowSettings() const
{
    // Try to get settings from the active follow camera
    if (m_runtime->display() && !m_runtime->display()->cameras.empty()) {
        Camera* activeCam = m_runtime->display()->cameras[0];
        if (activeCam && activeCam->isFollowModeEnabled()) {
            const FollowSettings& fs = activeCam->getFollowSettings();
            FollowCameraSettings result;
            result.relativeYaw = glm::degrees(fs.relativeYaw);
            result.relativePitch = glm::degrees(fs.relativePitch);
            result.distance = fs.distance;
            result.smoothSpeed = fs.smoothSpeed;
            result.targetOffset = QVector3D(fs.targetOffset.x, fs.targetOffset.y, fs.targetOffset.z);
            return result;
        }
    }
    return FollowCameraSettings();  // Return defaults
}

void ViewportHostWidget::setFollowCameraSettings(const FollowCameraSettings& settings)
{
    if (!m_runtime->display() || m_runtime->display()->cameras.empty()) {
        return;
    }
    
    Camera* activeCam = m_runtime->display()->cameras[0];
    if (activeCam && activeCam->isFollowModeEnabled()) {
        FollowSettings fs;
        fs.relativeYaw = glm::radians(settings.relativeYaw);
        fs.relativePitch = glm::radians(settings.relativePitch);
        fs.distance = settings.distance;
        fs.smoothSpeed = settings.smoothSpeed;
        fs.targetOffset = glm::vec3(settings.targetOffset.x(), settings.targetOffset.y(), settings.targetOffset.z());
        fs.enabled = true;
        
        activeCam->setFollowSettings(fs);
    }
}

void ViewportHostWidget::exitFollowCamera()
{
    if (!m_runtime->display()) {
        return;
    }
    
    // Find and remove any follow cameras, keeping the main camera
    auto& cameras = m_runtime->display()->cameras;
    std::vector<Camera*> camerasToRemove;
    
    for (auto* camera : cameras) {
        if (camera && camera->getCameraName().find("Follow Cam") != std::string::npos) {
            camerasToRemove.push_back(camera);
        }
    }
    
    for (auto* camera : camerasToRemove) {
        m_runtime->display()->removeCamera(camera);
    }
    
    // Ensure we have at least one camera (create main if needed)
    if (cameras.empty() && m_runtime->engine()) {
        m_runtime->display()->createCamera("Main Camera");
    }
}

void ViewportHostWidget::updateFollowCameras(float dt)
{
    if (!m_runtime->display()) {
        return;
    }
    
    // Update all cameras that have follow mode enabled
    for (auto* camera : m_runtime->display()->cameras) {
        if (camera && camera->isFollowModeEnabled()) {
            camera->updateFollow(dt);
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
}

void ViewportHostWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    if (m_runtime)
    {
        m_runtime->focusNativeWindow(static_cast<unsigned long>(winId()));
    }
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
}

void ViewportHostWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    
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
        auto& entries = m_sceneController->loadedEntries();
        for (size_t i = 0; i < m_runtime->engine()->models.size() && i < static_cast<size_t>(entries.size()); ++i)
        {
            const auto& entry = entries[static_cast<int>(i)];
            if (m_runtime->engine()->models[i])
            {
                auto& model = m_runtime->engine()->models[i];
                
                // Update character physics before animation
                if (model->character.isControllable)
                {
                    model->updateCharacterPhysics(dt);
                }
                
                model->setAnimationPlaybackState(entry.activeAnimationClip.toStdString(),
                                                                         entry.animationPlaying,
                                                                         entry.animationLoop,
                                                                         entry.animationSpeed);
                model->updateAnimation(deltaSeconds);
            }
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
    if (m_sceneChangedCallback)
    {
        m_sceneChangedCallback(sceneItems());
    }
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
