#include "engine_ui_viewport_host_widget.h"

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "model.h"

#include <filesystem>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QLabel>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>

#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#define Display X11Display
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#undef Display
#endif

namespace motive::ui {
namespace {

struct EmbeddedViewportState
{
    std::unique_ptr<Engine> engine;
    Display* display = nullptr;
    Camera* camera = nullptr;
};

EmbeddedViewportState& viewportState()
{
    static EmbeddedViewportState state;
    return state;
}

bool isRenderableAsset(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("gltf") || suffix == QStringLiteral("glb");
}

std::filesystem::path defaultScenePath()
{
    const std::filesystem::path teapot = std::filesystem::path("the_utah_teapot.glb");
    if (std::filesystem::exists(teapot))
    {
        return teapot;
    }
    return {};
}

void loadModelIntoEngine(EmbeddedViewportState& state, const QString& path)
{
    if (!state.engine || path.isEmpty() || !isRenderableAsset(path))
    {
        return;
    }

    auto model = std::make_unique<Model>(path.toStdString(), state.engine.get());
    model->resizeToUnitBox();
    model->rotate(-90.0f, 0.0f, 0.0f);
    state.engine->models.clear();
    state.engine->addModel(std::move(model));
}

}  // namespace

ViewportHostWidget::ViewportHostWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_statusLabel = new QLabel(QStringLiteral("Initializing viewport..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    m_renderTimer.setInterval(16);
    connect(&m_renderTimer, &QTimer::timeout, this, [this]() { renderFrame(); });
}

ViewportHostWidget::~ViewportHostWidget()
{
    m_renderTimer.stop();
    auto& state = viewportState();
    state.display = nullptr;
    state.camera = nullptr;
    state.engine.reset();
}

void ViewportHostWidget::loadAssetFromPath(const QString& path)
{
    auto& state = viewportState();
    if (!m_initialized || !state.engine)
    {
        return;
    }
    try
    {
        loadModelIntoEngine(state, path);
    }
    catch (const std::exception&)
    {
    }
}

void ViewportHostWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_initialized && !m_initScheduled)
    {
        m_initScheduled = true;
        QTimer::singleShot(0, this, [this]()
        {
            m_initScheduled = false;
            ensureViewportInitialized();
        });
    }
}

void ViewportHostWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncEmbeddedWindowGeometry();
}

void ViewportHostWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
#ifdef __linux__
    auto& state = viewportState();
    if (state.display && state.display->window)
    {
        X11Display* xDisplay = glfwGetX11Display();
        ::Window child = glfwGetX11Window(state.display->window);
        if (xDisplay && child != 0)
        {
            XSetInputFocus(xDisplay, child, RevertToParent, CurrentTime);
            XFlush(xDisplay);
        }
    }
#endif
}

void ViewportHostWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    QWidget::mousePressEvent(event);
}

void ViewportHostWidget::ensureViewportInitialized()
{
    if (m_initialized)
    {
        return;
    }

    try
    {
        auto& state = viewportState();
        state.engine = std::make_unique<Engine>();
        state.display = state.engine->createWindow(width(), height(), "Motive Embedded Viewport", false, false, true);
        state.camera = new Camera(state.engine.get(), state.display, glm::vec3(0.0f, 0.0f, 3.0f), glm::vec2(glm::radians(0.0f), 0.0f));
        state.display->addCamera(state.camera);

        const std::filesystem::path scenePath = defaultScenePath();
        if (!scenePath.empty())
        {
            loadModelIntoEngine(state, QString::fromStdString(scenePath.string()));
        }

        embedNativeWindow();
        syncEmbeddedWindowGeometry();
        if (m_statusLabel)
        {
            m_statusLabel->hide();
        }
        m_renderTimer.start();
        m_initialized = true;
    }
    catch (const std::exception& ex)
    {
        if (m_statusLabel)
        {
            m_statusLabel->setText(QStringLiteral("Viewport unavailable:\n%1").arg(QString::fromUtf8(ex.what())));
            m_statusLabel->show();
        }
        auto& state = viewportState();
        state.display = nullptr;
        state.camera = nullptr;
        state.engine.reset();
    }
}

void ViewportHostWidget::renderFrame()
{
    auto& state = viewportState();
    if (!m_initialized || !state.display || !state.display->window)
    {
        return;
    }
    if (glfwWindowShouldClose(state.display->window))
    {
        m_renderTimer.stop();
        return;
    }
    state.display->render();
}

void ViewportHostWidget::embedNativeWindow()
{
#ifdef __linux__
    auto& state = viewportState();
    if (!state.display || !state.display->window)
    {
        return;
    }

    X11Display* xDisplay = glfwGetX11Display();
    ::Window child = glfwGetX11Window(state.display->window);
    ::Window parent = static_cast<::Window>(winId());
    if (!xDisplay || child == 0 || parent == 0)
    {
        return;
    }

    XReparentWindow(xDisplay, child, parent, 0, 0);
    XMapWindow(xDisplay, child);
    XFlush(xDisplay);
    glfwShowWindow(state.display->window);
#endif
}

void ViewportHostWidget::syncEmbeddedWindowGeometry()
{
    auto& state = viewportState();
    if (!m_initialized || !state.display || !state.display->window)
    {
        return;
    }

    const int targetWidth = std::max(1, width());
    const int targetHeight = std::max(1, height());
    glfwSetWindowSize(state.display->window, targetWidth, targetHeight);
    state.display->handleFramebufferResize(targetWidth, targetHeight);

#ifdef __linux__
    X11Display* xDisplay = glfwGetX11Display();
    ::Window child = glfwGetX11Window(state.display->window);
    if (xDisplay && child != 0)
    {
        XResizeWindow(xDisplay, child, static_cast<unsigned int>(targetWidth), static_cast<unsigned int>(targetHeight));
        XFlush(xDisplay);
    }
#endif
}

}  // namespace motive::ui
