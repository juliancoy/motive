#pragma once

#include <QVector3D>
#include <QString>

class Engine;
class Display;
class Camera;
class InputRouter;

namespace motive::ui {

class ViewportRuntime
{
public:
    ViewportRuntime();
    ~ViewportRuntime();

    void initialize(int width, int height, bool use2DPipeline);
    void shutdown();

    void render();
    void resize(int width, int height);

    void setBackgroundColor(float r, float g, float b);
    void setUse2DPipeline(bool enabled);
    bool use2DPipeline() const;

    Engine* engine() const;
    Display* display() const;
    Camera* camera() const;
    void setCamera(Camera* camera);  // Set the active camera for input handling
    InputRouter* getInputRouter() const;  // Get input router for debugging

    bool isInitialized() const;

    void clearInputState();
    void focusNativeWindow(unsigned long parentWinId);
    void embedNativeWindow(unsigned long parentWinId);

private:
    Engine* m_engineRaw = nullptr;
    Display* m_display = nullptr;
    Camera* m_camera = nullptr;
    bool m_use2DPipeline = false;
    bool m_initialized = false;
    float m_bgColorR = 0.2f;
    float m_bgColorG = 0.2f;
    float m_bgColorB = 0.8f;
};

}  // namespace motive::ui
