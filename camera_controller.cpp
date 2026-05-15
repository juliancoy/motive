#include "camera_controller.h"

#include "viewport_internal_utils.h"
#include "viewport_runtime.h"
#include "scene_controller.h"

#include "camera.h"
#include "engine.h"
#include "model.h"

#include <cmath>

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
        const glm::vec2 rotation = m_runtime.camera()->getEulerRotation();
        return QVector3D(rotation.x, rotation.y, 0.0f);
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
    m_runtime.camera()->setEulerRotation(glm::vec2(rotation.x(), rotation.y()));
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

void ViewportCameraController::relocateSceneItemInFrontOfCamera(int index, Camera* referenceCamera)
{
    Camera* camera = referenceCamera ? referenceCamera : m_runtime.camera();
    if (index < 0 || index >= m_sceneController.loadedEntries().size() || !camera || !m_runtime.engine())
    {
        return;
    }
    if (index >= static_cast<int>(m_runtime.engine()->models.size()) || !m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        return;
    }

    const auto& model = m_runtime.engine()->models[static_cast<size_t>(index)];
    const glm::vec3 front = camera->getForwardVector();
    const float distance = 5.0f;
    const glm::vec3 desiredCenter = camera->cameraPos + front * distance;
    const glm::vec3 currentCenter = model->boundsCenter;
    const glm::vec3 delta = desiredCenter - currentCenter;
    auto& entry = m_sceneController.loadedEntries()[index];
    const QVector3D translation = entry.translation + QVector3D(delta.x, delta.y, delta.z);
    m_sceneController.updateSceneItemTransform(index, translation, entry.rotation, entry.scale);
}

void ViewportCameraController::focusSceneItem(int index, Camera* targetCamera)
{
    Camera* camera = targetCamera ? targetCamera : m_runtime.camera();
    if (index < 0 || index >= m_sceneController.loadedEntries().size() || !camera || !m_runtime.engine())
    {
        return;
    }
    if (index >= static_cast<int>(m_runtime.engine()->models.size()) || !m_runtime.engine()->models[static_cast<size_t>(index)])
    {
        return;
    }

    const auto& model = m_runtime.engine()->models[static_cast<size_t>(index)];
    const auto& entry = m_sceneController.loadedEntries()[index];
    const glm::vec3 focusOffset(entry.focusPointOffset.x(), entry.focusPointOffset.y(), entry.focusPointOffset.z());
    glm::vec3 worldCenter = model->getFollowAnchorPosition() + focusOffset;
    if (!std::isfinite(worldCenter.x) || !std::isfinite(worldCenter.y) || !std::isfinite(worldCenter.z))
    {
        worldCenter = model->boundsCenter;
    }
    const float autoDistance = detail::framingDistanceForModel(*model);
    const float distance = entry.focusDistance > 0.0f ? entry.focusDistance : autoDistance;

    glm::vec3 cameraPosition = camera->cameraPos;
    if (entry.focusCameraOffsetValid)
    {
        const glm::vec3 storedOffset(entry.focusCameraOffset.x(), entry.focusCameraOffset.y(), entry.focusCameraOffset.z());
        if (std::isfinite(storedOffset.x) && std::isfinite(storedOffset.y) && std::isfinite(storedOffset.z) &&
            glm::length(storedOffset) > 1e-6f)
        {
            cameraPosition = worldCenter + storedOffset;
        }
    }

    const float desiredDistance = glm::max(distance, 0.05f);
    const glm::vec3 defaultFront = glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f));
    const glm::vec3 toTarget = worldCenter - cameraPosition;
    const float currentDistance = glm::length(toTarget);
    glm::vec3 front = (currentDistance > 1e-6f)
        ? glm::normalize(toTarget)
        : camera->getForwardVector();
    if (glm::length(front) <= 1e-6f)
    {
        front = defaultFront;
    }
    if (!std::isfinite(currentDistance) ||
        currentDistance > desiredDistance * 6.0f ||
        std::abs(front.y) > 0.97f)
    {
        front = defaultFront;
    }

    if (!entry.focusCameraOffsetValid)
    {
        cameraPosition = worldCenter - front * desiredDistance;
    }
    else
    {
        const float actualDistance = glm::length(worldCenter - cameraPosition);
        if (!std::isfinite(actualDistance) || actualDistance < 0.05f)
        {
            cameraPosition = worldCenter - front * desiredDistance;
        }
    }

    camera->setEulerRotation(detail::cameraRotationForDirection(front));
    camera->cameraPos = cameraPosition;
    const float requiredFarClip = desiredDistance + std::max(model->boundsRadius * 4.0f, 10.0f);
    if (std::isfinite(requiredFarClip) && requiredFarClip > camera->getPerspectiveFar())
    {
        camera->setPerspectiveNearFar(camera->getPerspectiveNear(), requiredFarClip);
    }
    camera->update(0.0f);
}

void ViewportCameraController::focusWorldPoint(const QVector3D& worldPoint,
                                               float desiredDistance,
                                               float boundsRadius,
                                               Camera* targetCamera)
{
    Camera* camera = targetCamera ? targetCamera : m_runtime.camera();
    if (!camera)
    {
        return;
    }

    const glm::vec3 target(worldPoint.x(), worldPoint.y(), worldPoint.z());
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z))
    {
        return;
    }

    glm::vec3 cameraPosition = camera->cameraPos;
    const glm::vec3 toTarget = target - cameraPosition;
    const float currentDistance = glm::length(toTarget);
    const glm::vec3 defaultFront = glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f));
    glm::vec3 front = currentDistance > 1e-6f
        ? glm::normalize(toTarget)
        : camera->getForwardVector();
    if (glm::length(front) <= 1e-6f)
    {
        front = defaultFront;
    }
    if (!std::isfinite(currentDistance) ||
        currentDistance > desiredDistance * 6.0f ||
        std::abs(front.y) > 0.97f)
    {
        front = defaultFront;
    }

    const float clampedDistance = glm::max(desiredDistance, 0.05f);
    cameraPosition = target - front * clampedDistance;
    camera->setEulerRotation(detail::cameraRotationForDirection(front));
    camera->cameraPos = cameraPosition;

    const float safeRadius = std::max(boundsRadius, 0.1f);
    const float requiredFarClip = clampedDistance + std::max(safeRadius * 8.0f, 10.0f);
    if (std::isfinite(requiredFarClip) && requiredFarClip > camera->getPerspectiveFar())
    {
        camera->setPerspectiveNearFar(camera->getPerspectiveNear(), requiredFarClip);
    }
    camera->update(0.0f);
}

}  // namespace motive::ui
