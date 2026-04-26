#include "control_command_service.h"

#include "host_widget.h"
#include "shell.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace motive::ui {

bool ControlCommandService::handleCamera(const QJsonObject& body, QJsonObject& result) const
{
    auto* viewport = m_window.viewportHost();
    if (!viewport)
    {
        result.insert(QStringLiteral("error"), QStringLiteral("viewport unavailable"));
        return false;
    }

    const QStringList validCameraModes = {
        QStringLiteral("FreeFly"),
        QStringLiteral("CharacterFollow"),
        QStringLiteral("OrbitFollow"),
        QStringLiteral("Fixed")
    };
    auto resolveCameraIndex = [&](const QString& indexField, const QString& idField) -> int
    {
        if (body.contains(idField))
        {
            const QString cameraId = body.value(idField).toString();
            return viewport->cameraIndexForId(cameraId);
        }
        return body.value(indexField).toInt(-1);
    };

    if (body.contains(QStringLiteral("freeFly")))
    {
        const bool enabled = body.value(QStringLiteral("freeFly")).toBool(true);
        viewport->setFreeFlyCameraEnabled(enabled);
        result.insert(QStringLiteral("freeFly"), enabled);
    }

    if (body.value(QStringLiteral("list")).toBool(false))
    {
        QJsonArray cameras;
        const auto configs = viewport->cameraConfigs();
        for (int i = 0; i < configs.size(); ++i)
        {
            QJsonObject cam;
            cam.insert(QStringLiteral("index"), i);
            cam.insert(QStringLiteral("id"), configs[i].id);
            cam.insert(QStringLiteral("name"), configs[i].name);
            cam.insert(QStringLiteral("mode"), configs[i].mode);
            cam.insert(QStringLiteral("type"), configs[i].isFollowCamera() ? QStringLiteral("follow") : QStringLiteral("free"));
            cam.insert(QStringLiteral("followTargetIndex"), configs[i].followTargetIndex);
            cam.insert(QStringLiteral("followDistance"), configs[i].followDistance);
            cam.insert(QStringLiteral("followYaw"), configs[i].followYaw);
            cam.insert(QStringLiteral("followPitch"), configs[i].followPitch);
            cam.insert(QStringLiteral("followSmoothSpeed"), configs[i].followSmoothSpeed);
            cam.insert(QStringLiteral("invertHorizontalDrag"), configs[i].invertHorizontalDrag);
            cam.insert(QStringLiteral("rotation"), QJsonArray{configs[i].rotation.x(), configs[i].rotation.y(), configs[i].rotation.z()});
            cam.insert(QStringLiteral("position"), QJsonArray{configs[i].position.x(), configs[i].position.y(), configs[i].position.z()});
            cameras.append(cam);
        }
        result.insert(QStringLiteral("cameras"), cameras);
        result.insert(QStringLiteral("count"), configs.size());
    }

    if (body.contains(QStringLiteral("createFollow")) || body.contains(QStringLiteral("createFollowName")))
    {
        int sceneIndex = -1;

        if (body.contains(QStringLiteral("createFollowName")))
        {
            const QString targetName = body.value(QStringLiteral("createFollowName")).toString();
            const auto items = viewport->sceneItems();
            for (int i = 0; i < items.size(); ++i)
            {
                if (items[i].name == targetName)
                {
                    sceneIndex = i;
                    break;
                }
            }
            if (sceneIndex < 0)
            {
                result.insert(QStringLiteral("error"), QStringLiteral("Scene item not found with name: %1").arg(targetName));
                return false;
            }
        }
        else
        {
            sceneIndex = body.value(QStringLiteral("createFollow")).toInt(-1);
        }

        const float distance = body.value(QStringLiteral("distance")).toDouble(5.0);
        const float yaw = body.value(QStringLiteral("yaw")).toDouble(0.0);
        const float pitch = body.value(QStringLiteral("pitch")).toDouble(20.0);
        const int camIndex = viewport->createFollowCamera(sceneIndex, distance, yaw, pitch);
        if (camIndex >= 0 && body.contains(QStringLiteral("smooth")))
        {
            auto configs = viewport->cameraConfigs();
            if (camIndex < configs.size())
            {
                configs[camIndex].followSmoothSpeed =
                    std::max(0.0f, static_cast<float>(body.value(QStringLiteral("smooth")).toDouble(configs[camIndex].followSmoothSpeed)));
                viewport->updateCameraConfig(camIndex, configs[camIndex]);
            }
        }
        result.insert(QStringLiteral("created"), camIndex >= 0);
        result.insert(QStringLiteral("cameraIndex"), camIndex);
        result.insert(QStringLiteral("sceneIndex"), sceneIndex);
    }

    if (body.contains(QStringLiteral("setActive")) || body.contains(QStringLiteral("setActiveId")))
    {
        const int cameraIndex = resolveCameraIndex(QStringLiteral("setActive"), QStringLiteral("setActiveId"));
        if (cameraIndex >= 0)
        {
            viewport->setActiveCamera(cameraIndex);
            result.insert(QStringLiteral("activeCamera"), cameraIndex);
            result.insert(QStringLiteral("activeCameraId"), viewport->activeCameraId());
        }
        else
        {
            result.insert(QStringLiteral("error"), QStringLiteral("Camera not found"));
            return false;
        }
    }

    const bool hasNavigatePayload =
        body.contains(QStringLiteral("navigate")) ||
        body.contains(QStringLiteral("navigateId")) ||
        body.contains(QStringLiteral("forward")) ||
        body.contains(QStringLiteral("right")) ||
        body.contains(QStringLiteral("up")) ||
        body.contains(QStringLiteral("distance")) ||
        body.contains(QStringLiteral("yawDelta")) ||
        body.contains(QStringLiteral("pitchDelta")) ||
        body.contains(QStringLiteral("forceFreeFly"));
    if (hasNavigatePayload)
    {
        int cameraIndex = resolveCameraIndex(QStringLiteral("navigate"), QStringLiteral("navigateId"));
        if (cameraIndex < 0)
        {
            cameraIndex = viewport->activeCameraIndex();
        }

        auto configs = viewport->cameraConfigs();
        if (cameraIndex < 0 || cameraIndex >= configs.size())
        {
            result.insert(QStringLiteral("error"), QStringLiteral("Camera not found"));
            return false;
        }

        auto& config = configs[cameraIndex];
        const bool forceFreeFly = body.value(QStringLiteral("forceFreeFly")).toBool(true);
        if (forceFreeFly)
        {
            config.type = ViewportHostWidget::CameraConfig::Type::Free;
            config.followTargetIndex = -1;
            config.freeFly = true;
            config.mode = QStringLiteral("FreeFly");
        }

        float yawDeg = config.rotation.x();
        float pitchDeg = config.rotation.y();
        yawDeg += static_cast<float>(body.value(QStringLiteral("yawDelta")).toDouble(0.0));
        pitchDeg += static_cast<float>(body.value(QStringLiteral("pitchDelta")).toDouble(0.0));
        pitchDeg = std::clamp(pitchDeg, -80.0f, 80.0f);
        config.rotation = QVector3D(yawDeg, pitchDeg, 0.0f);

        const float travelDistance = static_cast<float>(body.value(QStringLiteral("distance")).toDouble(1.0));
        const float forwardAmount = static_cast<float>(body.value(QStringLiteral("forward")).toDouble(0.0));
        const float rightAmount = static_cast<float>(body.value(QStringLiteral("right")).toDouble(0.0));
        const float upAmount = static_cast<float>(body.value(QStringLiteral("up")).toDouble(0.0));

        const float yaw = yawDeg * static_cast<float>(M_PI / 180.0);
        const float pitch = pitchDeg * static_cast<float>(M_PI / 180.0);

        QVector3D front(-std::cos(pitch) * std::sin(yaw),
                        std::sin(pitch),
                        -std::cos(pitch) * std::cos(yaw));
        if (front.lengthSquared() > 1e-8f)
        {
            front.normalize();
        }
        const QVector3D worldUp(0.0f, 1.0f, 0.0f);
        QVector3D rightVec = QVector3D::crossProduct(front, worldUp);
        if (rightVec.lengthSquared() > 1e-8f)
        {
            rightVec.normalize();
        }
        QVector3D upVec = QVector3D::crossProduct(rightVec, front);
        if (upVec.lengthSquared() > 1e-8f)
        {
            upVec.normalize();
        }

        QVector3D delta = (front * forwardAmount) + (rightVec * rightAmount) + (upVec * upAmount);
        if (delta.lengthSquared() > 1e-8f)
        {
            delta.normalize();
            delta *= travelDistance;
        }
        config.position += delta;

        viewport->updateCameraConfig(cameraIndex, config);
        if (viewport->activeCameraIndex() != cameraIndex)
        {
            viewport->setActiveCamera(cameraIndex);
        }

        result.insert(QStringLiteral("updated"), true);
        result.insert(QStringLiteral("cameraIndex"), cameraIndex);
        result.insert(QStringLiteral("cameraId"), config.id);
        result.insert(QStringLiteral("mode"), config.mode);
        result.insert(QStringLiteral("position"),
                      QJsonArray{config.position.x(), config.position.y(), config.position.z()});
        result.insert(QStringLiteral("rotation"),
                      QJsonArray{config.rotation.x(), config.rotation.y(), config.rotation.z()});
    }

    if (body.contains(QStringLiteral("updateCamera")) || body.contains(QStringLiteral("updateCameraId")))
    {
        const int cameraIndex = resolveCameraIndex(QStringLiteral("updateCamera"), QStringLiteral("updateCameraId"));
        if (cameraIndex >= 0)
        {
            auto configs = viewport->cameraConfigs();
            if (cameraIndex < configs.size())
            {
                auto& config = configs[cameraIndex];

                if (body.contains(QStringLiteral("followTargetIndex")) || body.contains(QStringLiteral("followTargetName")))
                {
                    int targetIndex = -1;
                    if (body.contains(QStringLiteral("followTargetName")))
                    {
                        const QString targetName = body.value(QStringLiteral("followTargetName")).toString();
                        const auto items = viewport->sceneItems();
                        for (int i = 0; i < items.size(); ++i)
                        {
                            if (items[i].name == targetName)
                            {
                                targetIndex = i;
                                break;
                            }
                        }
                        if (targetIndex < 0)
                        {
                            result.insert(QStringLiteral("error"), QStringLiteral("Scene item not found with name: %1").arg(targetName));
                            return false;
                        }
                    }
                    else
                    {
                        targetIndex = body.value(QStringLiteral("followTargetIndex")).toInt(-1);
                    }

                    config.followTargetIndex = targetIndex;
                    config.type = (config.followTargetIndex >= 0)
                        ? ViewportHostWidget::CameraConfig::Type::Follow
                        : ViewportHostWidget::CameraConfig::Type::Free;
                }

                if (body.contains(QStringLiteral("distance")))
                {
                    config.followDistance = body.value(QStringLiteral("distance")).toDouble(5.0);
                }
                if (body.contains(QStringLiteral("yaw")))
                {
                    config.followYaw = body.value(QStringLiteral("yaw")).toDouble(0.0);
                }
                if (body.contains(QStringLiteral("pitch")))
                {
                    config.followPitch = body.value(QStringLiteral("pitch")).toDouble(20.0);
                }
                if (body.contains(QStringLiteral("smooth")))
                {
                    config.followSmoothSpeed =
                        std::max(0.0f, static_cast<float>(body.value(QStringLiteral("smooth")).toDouble(config.followSmoothSpeed)));
                }
                if (body.contains(QStringLiteral("mode")))
                {
                    const QString mode = body.value(QStringLiteral("mode")).toString();
                    if (!validCameraModes.contains(mode))
                    {
                        result.insert(QStringLiteral("error"), QStringLiteral("Invalid camera mode: %1").arg(mode));
                        return false;
                    }
                    config.mode = mode;
                }
                if (body.contains(QStringLiteral("invertHorizontalDrag")))
                {
                    config.invertHorizontalDrag =
                        body.value(QStringLiteral("invertHorizontalDrag")).toBool(config.invertHorizontalDrag);
                }

                viewport->updateCameraConfig(cameraIndex, config);
                result.insert(QStringLiteral("updated"), true);
                result.insert(QStringLiteral("cameraIndex"), cameraIndex);
                result.insert(QStringLiteral("cameraId"), configs[cameraIndex].id);
            }
            else
            {
                result.insert(QStringLiteral("error"), QStringLiteral("Camera index out of range"));
                return false;
            }
        }
        else
        {
            result.insert(QStringLiteral("error"), QStringLiteral("Camera not found"));
            return false;
        }
    }

    if (body.contains(QStringLiteral("setMode")) || body.contains(QStringLiteral("setModeId")))
    {
        const int cameraIndex = resolveCameraIndex(QStringLiteral("setMode"), QStringLiteral("setModeId"));
        if (cameraIndex < 0)
        {
            result.insert(QStringLiteral("error"), QStringLiteral("Camera not found"));
            return false;
        }
        const QString mode = body.value(QStringLiteral("mode")).toString();
        if (!validCameraModes.contains(mode))
        {
            result.insert(QStringLiteral("error"), QStringLiteral("mode is required and must be one of: FreeFly, CharacterFollow, OrbitFollow, Fixed"));
            return false;
        }
        auto configs = viewport->cameraConfigs();
        if (cameraIndex >= configs.size())
        {
            result.insert(QStringLiteral("error"), QStringLiteral("Camera index out of range"));
            return false;
        }
        configs[cameraIndex].mode = mode;
        viewport->updateCameraConfig(cameraIndex, configs[cameraIndex]);
        result.insert(QStringLiteral("updated"), true);
        result.insert(QStringLiteral("cameraIndex"), cameraIndex);
        result.insert(QStringLiteral("cameraId"), configs[cameraIndex].id);
        result.insert(QStringLiteral("mode"), mode);
    }

    result.insert(QStringLiteral("freeFly"), viewport->isFreeFlyCameraEnabled());
    return true;
}

}  // namespace motive::ui
