#pragma once

#include <QVector3D>

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
    void resetCamera();

    void relocateSceneItemInFrontOfCamera(int index);
    void focusSceneItem(int index);

private:
    ViewportRuntime& m_runtime;
    ViewportSceneController& m_sceneController;
    float m_cameraSpeed = 0.01f;
};

}  // namespace motive::ui
