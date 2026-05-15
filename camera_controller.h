#pragma once

#include <QVector3D>

class Camera;

namespace motive::ui {

class ViewportRuntime;
class ViewportSceneController;

class ViewportCameraController
{
public:
    ViewportCameraController(ViewportRuntime& runtime, ViewportSceneController& sceneController);

    QVector3D cameraPosition() const;
    QVector3D cameraRotation() const;
    float cameraSpeed() const;

    void setCameraPosition(const QVector3D& position);
    void setCameraRotation(const QVector3D& rotation);
    void setCameraSpeed(float speed);
    void setPerspectiveNearFar(float near, float far);
    void getPerspectiveNearFar(float& near, float& far) const;
    void resetCamera();

    void relocateSceneItemInFrontOfCamera(int index, ::Camera* referenceCamera = nullptr);
    void focusSceneItem(int index, ::Camera* targetCamera = nullptr);
    void focusWorldPoint(const QVector3D& worldPoint,
                         float desiredDistance = 3.0f,
                         float boundsRadius = 0.25f,
                         ::Camera* targetCamera = nullptr);

private:
    ViewportRuntime& m_runtime;
    ViewportSceneController& m_sceneController;
    float m_cameraSpeed = 0.01f;
};

}  // namespace motive::ui
