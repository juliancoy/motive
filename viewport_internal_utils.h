#pragma once

#include "host_widget.h"

#include "light.h"
#include "model.h"

#include <QFileInfo>
#include <QString>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace motive::ui::detail {

constexpr int kHierarchyCameraIndex = -1000;
constexpr int kHierarchyLightIndex = -1001;

inline bool isRenderableAsset(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("gltf") ||
           suffix == QStringLiteral("glb") ||
           suffix == QStringLiteral("fbx");
}

inline bool vectorsNearlyEqual(const QVector3D& lhs, const QVector3D& rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs.x() - rhs.x()) <= epsilon &&
           std::abs(lhs.y() - rhs.y()) <= epsilon &&
           std::abs(lhs.z() - rhs.z()) <= epsilon;
}

inline glm::vec3 cameraForwardVector(const glm::vec2& cameraRotation)
{
    const float yaw = cameraRotation.x;
    const float pitch = cameraRotation.y;
    glm::vec3 front;
    front.x = -std::cos(pitch) * std::sin(yaw);
    front.y = std::sin(pitch);
    front.z = -std::cos(pitch) * std::cos(yaw);
    if (glm::length(front) <= 1e-6f)
    {
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return glm::normalize(front);
}

inline glm::vec2 cameraRotationForDirection(const glm::vec3& direction)
{
    const glm::vec3 normalized = glm::normalize(direction);
    const float yaw = std::atan2(-normalized.x, -normalized.z);
    const float pitch = std::asin(glm::clamp(normalized.y, -1.0f, 1.0f));
    return glm::vec2(yaw, pitch);
}

inline glm::vec3 lightDirectionFromRotationDegrees(const QVector3D& rotationDegrees)
{
    const float pitch = glm::radians(rotationDegrees.x());
    const float yaw = glm::radians(rotationDegrees.y());
    glm::vec3 direction(std::sin(yaw) * std::cos(pitch),
                        -std::sin(pitch),
                        std::cos(yaw) * std::cos(pitch));
    if (glm::length(direction) <= 1e-6f)
    {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return glm::normalize(direction);
}

inline QVector3D lightRotationDegreesForDirection(const QVector3D& directionValue)
{
    glm::vec3 direction(directionValue.x(), directionValue.y(), directionValue.z());
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        glm::length(direction) <= 1e-6f)
    {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }

    direction = glm::normalize(direction);
    const float yaw = std::atan2(direction.x, direction.z);
    const float pitch = std::atan2(-direction.y, std::sqrt(direction.x * direction.x + direction.z * direction.z));
    return QVector3D(glm::degrees(pitch), glm::degrees(yaw), 0.0f);
}

inline float framingDistanceForModel(const Model& model)
{
    return std::max(std::max(model.boundsRadius, 0.5f) * 3.0f, 2.0f);
}

inline std::filesystem::path defaultScenePath()
{
    const std::filesystem::path teapot = std::filesystem::path("the_utah_teapot.glb");
    if (std::filesystem::exists(teapot))
    {
        return teapot;
    }
    return {};
}

inline Light engineLightFromSceneLight(const ViewportHostWidget::SceneLight& sceneLight)
{
    const glm::vec3 direction = lightDirectionFromRotationDegrees(sceneLight.rotation);
    const glm::vec3 color(sceneLight.color.x(), sceneLight.color.y(), sceneLight.color.z());
    const float brightness = std::max(0.0f, sceneLight.brightness);

    glm::vec3 ambient(0.0f);
    glm::vec3 diffuse(0.0f);

    if (sceneLight.type == QStringLiteral("ambient"))
    {
        ambient = color * brightness;
    }
    else if (sceneLight.type == QStringLiteral("point"))
    {
        ambient = color * brightness * 0.18f;
        diffuse = color * brightness * 0.82f;
    }
    else if (sceneLight.type == QStringLiteral("spot"))
    {
        ambient = color * brightness * 0.08f;
        diffuse = color * brightness * 0.92f;
    }
    else if (sceneLight.type == QStringLiteral("area"))
    {
        ambient = color * brightness * 0.30f;
        diffuse = color * brightness * 0.70f;
    }
    else if (sceneLight.type == QStringLiteral("hemispherical"))
    {
        ambient = color * brightness * 0.45f;
        diffuse = color * brightness * 0.55f;
    }
    else if (sceneLight.type == QStringLiteral("sun"))
    {
        ambient = color * brightness * 0.12f;
        diffuse = color * brightness;
    }
    else
    {
        ambient = color * brightness * 0.10f;
        diffuse = color * brightness;
    }

    return Light(direction, ambient, diffuse);
}

inline QString lightLabelFromSceneLight(const ViewportHostWidget::SceneLight& sceneLight)
{
    if (sceneLight.type == QStringLiteral("ambient"))
    {
        return QStringLiteral("Ambient Light");
    }
    if (sceneLight.type == QStringLiteral("point"))
    {
        return QStringLiteral("Point Light");
    }
    if (sceneLight.type == QStringLiteral("spot"))
    {
        return QStringLiteral("Spot Light");
    }
    if (sceneLight.type == QStringLiteral("area"))
    {
        return QStringLiteral("Area Light");
    }
    if (sceneLight.type == QStringLiteral("hemispherical"))
    {
        return QStringLiteral("Hemispherical Light");
    }
    if (sceneLight.type == QStringLiteral("sun"))
    {
        return QStringLiteral("Sun Light");
    }
    return QStringLiteral("Directional Light");
}

}  // namespace motive::ui::detail
