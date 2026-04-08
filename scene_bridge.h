#pragma once

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QWidget;

namespace motive::ui {

class ViewportSceneBridge
{
public:
    virtual ~ViewportSceneBridge() = default;

    virtual void attachToHost(QWidget* host) = 0;
    virtual void resizeViewport(int width, int height) = 0;
    virtual void renderFrame() = 0;

    virtual void mousePressEvent(QMouseEvent* event) = 0;
    virtual void mouseMoveEvent(QMouseEvent* event) = 0;
    virtual void mouseReleaseEvent(QMouseEvent* event) = 0;
    virtual void wheelEvent(QWheelEvent* event) = 0;
    virtual void keyPressEvent(QKeyEvent* event) = 0;
    virtual void keyReleaseEvent(QKeyEvent* event) = 0;
};

}  // namespace motive::ui
