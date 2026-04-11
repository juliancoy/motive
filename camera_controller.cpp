#include "camera_controller.h"

#include "viewport_internal_utils.h"
#include "viewport_runtime.h"
#include "scene_controller.h"

#include "camera.h"
#include "engine.h"
#include "model.h"

namespace motive::ui {

ViewportCameraController::ViewportCameraController(ViewportRuntime& runtime, ViewportSceneController& sceneController)
    : m_runtime(runtime)
    , m_sceneController(sceneController)
{
}

QVector3D ViewportCameraController::cameraPosition() const
{
    if (m_runtime.camera())
    {
        return QVector3D(m_runtime.camera()->cameraPos.x, m_runtime.camera()->cameraPos.y, m_runtime.camera()->cameraPos.z);
    }
    return QVector3D(0.0f, 0.0f, 3.0f);
}

QVector3D ViewportCameraController::cameraRotation() const
{
    if (m_runtime.camera())
    {
        return QVector3D(m_runtime.camera()->cameraRotation.y, m_runtime.camera()->cameraRotation.x, 0.0f);
    }
    return QVector3D(0.0f, 0.0f, 0.0f);
}

float ViewportCameraController::cameraSpeed() const
{
    return m_cameraSpeed;
}

void ViewportCameraController::setCameraPosition(const QVector3D& position)
{
    if (!m_runtime.camera())
    {
        return;
    }
    m_runtime.camera()->cameraPos = glm::vec3(position.x(), position.y(), position.z());
    m_runtime.camera()->update(0);
}

void ViewportCameraController::setCameraRotation(const QVector3D& rotation)
{
    if (!m_runtime.camera())
    {
        return;
    }
    m_runtime.camera()->cameraRotation = glm::vec2(rotation.y(), rotation.x());
    m_runtime.camera()->update(0);
}

void ViewportCameraController::setCameraSpeed(float speed)
{
    m_cameraSpeed = speed;
    if (m_runtime.camera())
    {
        m_runtime.camera()->moveSpeed = speed;
    }
}

void ViewportCameraController::resetCamera()
{
    if (m_runtime.camera())
    {
        m_runtime.camera()->reset();
    }
}

void ViewportCameraController::setPerspectiveNearFar(float near, float far)
{
    if (m_runtime.camera())
    {
        m_runtime.camera()->setPerspectiveNearFar(near, far);
    }
}

void ViewportCameraController::getPerspectiveNearFar(float& near, float& far) const
{
    if (m_runtime.camera())
    {
        near = m_runtime.camera()->getPerspectiveNear();
        far = m_runtime.camera()->getPerspectiveFar();
    }
    else
    {
        near = 0.1f;
        far = 100.0f;
    }
}

void ViewportCameraController::relocateSceneItemInFrontOfCamera(int index)
{
    if (index < 0 || index >= m_sceneController.loadedEntries().size() || !m_runtime.camera() || !m_runtime.engine())
    {
        return;
    }
    if (index >= static_cast<int>(m_runtime.engine()->models.size()) || !m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        return;
    }

    const auto& model = m_runtime.engine()->models[static_cast<size_t>(index)];
    const glm::vec3 front = detail::cameraForwardVector(m_runtime.camera()->cameraRotation);
    const float distance = detail::framingDistanceForModel(*model);
    const glm::vec3 desiredCenter = m_runtime.camera()->cameraPos + front * distance;
    const glm::vec3 currentCenter = model->boundsCenter;
    const glm::vec3 delta = desiredCenter - currentCenter;
    auto& entry = m_sceneController.loadedEntries()[index];
    const QVector3D translation = entry.translation + QVector3D(delta.x, delta.y, delta.z);
    m_sceneController.updateSceneItemTransform(index, translation, entry.rotation, entry.scale);
}

void ViewportCameraController::focusSceneItem(int index)
{
    if (index < 0 || index >= m_sceneController.loadedEntries().size() || !m_runtime.camera() || !m_runtime.engine())
    {
        return;
    }
    if (index >= static_cast<int>(m_runtime.engine()->models.size()) || !m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        return;
    }

    const auto& model = m_runtime.engine()->models[static_cast<size_t>(index)];
    const glm::vec3 worldCenter = model->boundsCenter;
    const float distance = detail::framingDistanceForModel(*model);
    const glm::vec3 toTarget = worldCenter - m_runtime.camera()->cameraPos;
    const glm::vec3 front = glm::length(toTarget) > 1e-6f
        ? glm::normalize(toTarget)
        : detail::cameraForwardVector(m_runtime.camera()->cameraRotation);
    m_runtime.camera()->cameraRotation = detail::cameraRotationForDirection(front);
    m_runtime.camera()->cameraPos = worldCenter - front * distance;
    m_runtime.camera()->update(0.0f);
}

}  // namespace motive::ui
