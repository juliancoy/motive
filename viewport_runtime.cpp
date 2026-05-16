#include "viewport_runtime.h"

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "input_router.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <sstream>
#include <unistd.h>

#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#define Display X11Display
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#undef Display
#endif

namespace motive::ui {

ViewportRuntime::ViewportRuntime() = default;

ViewportRuntime::~ViewportRuntime()
{
    shutdown();
}

void ViewportRuntime::initialize(int width, int height, bool use2DPipeline)
{
    if (m_initialized)
    {
        return;
    }

    m_engineRaw = new Engine();
    // Parallel loading setting is inherited from Engine's compile-time default
    std::ostringstream title;
    title << "Motive Embedded Viewport [pid=" << ::getpid() << "]";
    m_display = m_engineRaw->createWindow(width, height, title.str().c_str(), false, use2DPipeline, true);
    m_camera = new Camera(m_engineRaw, m_display, glm::vec3(0.0f, 0.0f, 3.0f), glm::vec2(glm::radians(0.0f), 0.0f));
    m_display->addCamera(m_camera);
    m_display->setBackgroundColor(m_bgColorR, m_bgColorG, m_bgColorB);
    m_use2DPipeline = use2DPipeline;
    m_initialized = true;
}

void ViewportRuntime::shutdown()
{
    if (m_display)
    {
        m_display->shutdown();
    }
    m_display = nullptr;
    m_camera = nullptr;

    delete m_engineRaw;
    m_engineRaw = nullptr;
    m_initialized = false;
}

void ViewportRuntime::render()
{
    if (m_display)
    {
        m_display->render();
    }
}

void ViewportRuntime::resize(int width, int height)
{
    if (!m_display || !m_display->window)
    {
        return;
    }

    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    glfwSetWindowSize(m_display->window, targetWidth, targetHeight);
    m_display->handleFramebufferResize(targetWidth, targetHeight);

#ifdef __linux__
    X11Display* xDisplay = glfwGetX11Display();
    ::Window child = glfwGetX11Window(m_display->window);
    if (xDisplay && child != 0)
    {
        XResizeWindow(xDisplay, child, static_cast<unsigned int>(targetWidth), static_cast<unsigned int>(targetHeight));
        XFlush(xDisplay);
    }
#endif
}

void ViewportRuntime::setBackgroundColor(float r, float g, float b)
{
    m_bgColorR = r;
    m_bgColorG = g;
    m_bgColorB = b;
    if (m_display)
    {
        m_display->setBackgroundColor(r, g, b);
    }
}

void ViewportRuntime::setUse2DPipeline(bool enabled)
{
    m_use2DPipeline = enabled;
}

bool ViewportRuntime::use2DPipeline() const
{
    return m_use2DPipeline;
}

Engine* ViewportRuntime::engine() const
{
    return m_engineRaw;
}

Display* ViewportRuntime::display() const
{
    return m_display;
}

Camera* ViewportRuntime::camera() const
{
    // Return the explicit active camera from display state.
    if (m_display) {
        if (Camera* active = m_display->getActiveCamera())
        {
            return active;
        }
    }
    return m_camera;
}

void ViewportRuntime::setCamera(Camera* camera)
{
    m_camera = camera;
}

InputRouter* ViewportRuntime::getInputRouter() const
{
    if (m_display) {
        return m_display->getInputRouter();
    }
    return nullptr;
}

bool ViewportRuntime::isInitialized() const
{
    return m_initialized;
}

void ViewportRuntime::clearInputState()
{
    Camera* cam = camera();  // Use the active camera, not m_camera directly
    if (cam)
    {
        cam->clearInputState();
    }
}

bool ViewportRuntime::focusNativeWindow(unsigned long parentWinId)
{
#ifdef __linux__
    if (!m_display || !m_display->window)
    {
        return false;
    }

    if (parentWinId != 0)
    {
        m_nativeParentWindowId = parentWinId;
    }

    X11Display* xDisplay = glfwGetX11Display();
    ::Window child = glfwGetX11Window(m_display->window);
    if (xDisplay && child != 0)
    {
        XRaiseWindow(xDisplay, child);
        XSetInputFocus(xDisplay, child, RevertToParent, CurrentTime);
        XFlush(xDisplay);
        return true;
    }
#endif
    return false;
}

void ViewportRuntime::embedNativeWindow(unsigned long parentWinId)
{
#ifdef __linux__
    if (!m_display || !m_display->window)
    {
        return;
    }

    X11Display* xDisplay = glfwGetX11Display();
    ::Window child = glfwGetX11Window(m_display->window);
    ::Window parent = static_cast<::Window>(parentWinId);
    if (!xDisplay || child == 0 || parent == 0)
    {
        return;
    }

    m_nativeParentWindowId = parentWinId;
    XReparentWindow(xDisplay, child, parent, 0, 0);
    XMapWindow(xDisplay, child);
    XFlush(xDisplay);
    glfwShowWindow(m_display->window);
    std::ostringstream title;
    title << "Motive Embedded Viewport [pid=" << ::getpid()
          << " child=0x" << std::hex << static_cast<unsigned long>(child)
          << " parent=0x" << static_cast<unsigned long>(parent) << "]";
    m_display->setDebugWindowTitle(title.str());
#else
    (void)parentWinId;
#endif
}

unsigned long ViewportRuntime::nativeWindowId() const
{
#ifdef __linux__
    if (!m_display || !m_display->window)
    {
        return 0;
    }
    return static_cast<unsigned long>(glfwGetX11Window(m_display->window));
#else
    return 0;
#endif
}

unsigned long ViewportRuntime::nativeParentWindowId() const
{
    return m_nativeParentWindowId;
}

QString ViewportRuntime::nativeWindowTitle() const
{
    if (!m_display)
    {
        return QString();
    }
    return QString::fromStdString(m_display->getWindowTitle());
}

}  // namespace motive::ui
