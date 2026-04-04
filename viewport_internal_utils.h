#pragma once

#include "viewport_host_widget.h"

#include "light.h"
#include "model.h"

#include <QFileInfo>
#include <QString>
#include <glm/glm.hpp>
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
    front.x = std::cos(pitch) * std::sin(yaw);
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
    const float yaw = std::atan2(normalized.x, -normalized.z);
    const float pitch = std::asin(glm::clamp(normalized.y, -1.0f, 1.0f));
    return glm::vec2(yaw, pitch);
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

}  // namespace motive::ui::detail
