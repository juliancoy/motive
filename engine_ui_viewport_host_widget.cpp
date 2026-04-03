#include "engine_ui_viewport_host_widget.h"

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "model.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <mutex>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDebug>
#include <QVector3D>
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
    struct SceneEntry
    {
        QString name;
        QString sourcePath;
        QVector3D translation;
        QVector3D rotation;
        QVector3D scale;
        bool visible = true;
    };

    std::unique_ptr<Engine> engine;
    Display* display = nullptr;
    Camera* camera = nullptr;
    QString currentAssetPath;
    QList<SceneEntry> pendingSceneEntries;
    QList<SceneEntry> sceneEntries;
    bool use2DPipeline = false;
    float bgColorR = 0.2f;
    float bgColorG = 0.2f;
    float bgColorB = 0.8f;
    float cameraSpeed = 0.01f;
    mutable std::recursive_mutex mutex;
};

EmbeddedViewportState& viewportState()
{
    static EmbeddedViewportState state;
    return state;
}

bool isRenderableAsset(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("gltf") ||
           suffix == QStringLiteral("glb") ||
           suffix == QStringLiteral("fbx");
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

void loadModelIntoEngine(EmbeddedViewportState& state, const EmbeddedViewportState::SceneEntry& entry)
{
    if (!state.engine || entry.sourcePath.isEmpty() || !isRenderableAsset(entry.sourcePath))
    {
        qWarning() << "[ViewportHost] Skipping non-renderable scene path:" << entry.sourcePath;
        return;
    }

    qDebug() << "[ViewportHost] Loading model into scene:" << entry.sourcePath
             << "existingModels=" << static_cast<int>(state.engine->models.size());
    auto model = std::make_unique<Model>(entry.sourcePath.toStdString(), state.engine.get());
    model->resizeToUnitBox();
    model->scale(glm::vec3(entry.scale.x(), entry.scale.y(), entry.scale.z()));
    model->rotate(entry.rotation.x(), entry.rotation.y(), entry.rotation.z());
    model->translate(glm::vec3(entry.translation.x(), entry.translation.y(), entry.translation.z()));
    model->visible = entry.visible;
    state.engine->addModel(std::move(model));
}

}  // namespace

ViewportHostWidget::ViewportHostWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

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
    qDebug() << "[ViewportHost] loadAssetFromPath" << path;
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (path.isEmpty())
    {
        return;
    }

    if (state.engine)
    {
        state.engine->models.clear();
    }
    state.sceneEntries.clear();
    state.pendingSceneEntries = {{
        QFileInfo(path).completeBaseName(),
        QFileInfo(path).absoluteFilePath(),
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(-90.0f, 0.0f, 0.0f),
        QVector3D(1.0f, 1.0f, 1.0f),
        true
    }};
    state.currentAssetPath = path;
    if (!m_initialized || !state.engine)
    {
        qDebug() << "[ViewportHost] Deferring single-asset scene until viewport init";
        return;
    }
    addAssetToScene(path);
    state.pendingSceneEntries.clear();
}

void ViewportHostWidget::loadSceneFromItems(const QList<SceneItem>& items)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    qDebug() << "[ViewportHost] loadSceneFromItems count=" << items.size();

    if (state.engine)
    {
        state.engine->models.clear();
    }
    state.sceneEntries.clear();
    state.pendingSceneEntries.clear();
    for (const SceneItem& item : items)
    {
        state.pendingSceneEntries.push_back({item.name, item.sourcePath, item.translation, item.rotation, item.scale, item.visible});
    }
    state.currentAssetPath = items.isEmpty() ? QString() : items.back().sourcePath;

    if (!m_initialized || !state.engine)
    {
        qDebug() << "[ViewportHost] Deferring scene restore until viewport init";
        notifySceneChanged();
        return;
    }

    const QList<EmbeddedViewportState::SceneEntry> pendingEntries = state.pendingSceneEntries;
    state.pendingSceneEntries.clear();
    for (const auto& entry : pendingEntries)
    {
        try
        {
            loadModelIntoEngine(state, entry);
            state.sceneEntries.push_back(entry);
        }
        catch (const std::exception& ex)
        {
            qWarning() << "[ViewportHost] Failed to restore scene asset:" << entry.sourcePath << ex.what();
        }
    }
    notifySceneChanged();
}

QString ViewportHostWidget::currentAssetPath() const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    return state.currentAssetPath;
}

QList<ViewportHostWidget::SceneItem> ViewportHostWidget::sceneItems() const
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    QList<SceneItem> items;
    const auto& entries = state.sceneEntries;
    for (const auto& entry : entries)
    {
        items.push_back(SceneItem{entry.name, entry.sourcePath, entry.translation, entry.rotation, entry.scale, entry.visible});
    }
    const auto& pending = state.pendingSceneEntries;
    for (const auto& entry : pending)
    {
        items.push_back(SceneItem{entry.name, entry.sourcePath, entry.translation, entry.rotation, entry.scale, entry.visible});
    }
    return items;
}

QVector3D ViewportHostWidget::cameraPosition() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        return QVector3D(state.camera->cameraPos.x, state.camera->cameraPos.y, state.camera->cameraPos.z);
    }
    return QVector3D(0.0f, 0.0f, 3.0f);
}

QVector3D ViewportHostWidget::cameraRotation() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        return QVector3D(state.camera->cameraRotation.x, state.camera->cameraRotation.y, 0.0f);
    }
    return QVector3D(0.0f, 0.0f, 0.0f);
}

QString ViewportHostWidget::renderPath() const
{
    const auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    return state.use2DPipeline ? QStringLiteral("flat2d") : QStringLiteral("forward3d");
}

void ViewportHostWidget::setCameraPosition(const QVector3D& position)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        state.camera->cameraPos = glm::vec3(position.x(), position.y(), position.z());
        state.camera->update(0); // Update camera matrices
    }
}

void ViewportHostWidget::setCameraRotation(const QVector3D& rotation)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        state.camera->cameraRotation = glm::vec2(rotation.y(), rotation.x()); // Note: y,x order for glm::vec2
        state.camera->update(0); // Update camera matrices
    }
}

void ViewportHostWidget::setCameraSpeed(float speed)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    state.cameraSpeed = speed;
    if (state.camera) {
        state.camera->moveSpeed = speed;
    }
}

void ViewportHostWidget::resetCamera()
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.camera) {
        state.camera->reset();
    }
}

void ViewportHostWidget::setBackgroundColor(const QColor& color)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    state.bgColorR = color.redF();
    state.bgColorG = color.greenF();
    state.bgColorB = color.blueF();
    if (state.display) {
        state.display->setBackgroundColor(state.bgColorR, state.bgColorG, state.bgColorB);
    }
}

void ViewportHostWidget::setRenderPath(const QString& renderPath)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    const bool use2d = renderPath.compare(QStringLiteral("flat2d"), Qt::CaseInsensitive) == 0;
    if (state.use2DPipeline == use2d)
    {
        return;
    }

    state.use2DPipeline = use2d;
    const QList<SceneItem> items = sceneItems();
    const QVector3D savedCameraPos = cameraPosition();
    const QVector3D savedCameraRot = cameraRotation();

    m_renderTimer.stop();
    state.display = nullptr;
    state.camera = nullptr;
    state.engine.reset();
    m_initialized = false;

    state.pendingSceneEntries.clear();
    for (const SceneItem& item : items)
    {
        state.pendingSceneEntries.push_back({item.name, item.sourcePath, item.translation, item.rotation, item.scale, item.visible});
    }

    ensureViewportInitialized();
    if (state.camera)
    {
        state.camera->moveSpeed = state.cameraSpeed;
        state.camera->cameraPos = glm::vec3(savedCameraPos.x(), savedCameraPos.y(), savedCameraPos.z());
        state.camera->cameraRotation = glm::vec2(savedCameraRot.y(), savedCameraRot.x());
        state.camera->update(0);
    }
    if (state.display)
    {
        state.display->setBackgroundColor(state.bgColorR, state.bgColorG, state.bgColorB);
    }
}

void ViewportHostWidget::updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size()) {
        return;
    }

    state.sceneEntries[index].translation = translation;
    state.sceneEntries[index].rotation = rotation;
    state.sceneEntries[index].scale = scale;

    if (index < state.engine->models.size()) {
        auto& model = state.engine->models[index];
        glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(translation.x(), translation.y(), translation.z()));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(rotation.x()), glm::vec3(1.0f, 0.0f, 0.0f));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(rotation.y()), glm::vec3(0.0f, 1.0f, 0.0f));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(rotation.z()), glm::vec3(0.0f, 0.0f, 1.0f));
        modelMatrix = glm::scale(modelMatrix, glm::vec3(scale.x(), scale.y(), scale.z()));

        for (auto& mesh : model->meshes) {
            for (auto& primitive : mesh.primitives) {
                primitive->transform = modelMatrix;
                primitive->updateUniformBuffer(modelMatrix, state.camera->getViewMatrix(), state.camera->getProjectionMatrix());
            }
        }
    }
    notifySceneChanged();
}

void ViewportHostWidget::setSceneItemVisible(int index, bool visible)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size()) {
        return;
    }
    state.sceneEntries[index].visible = visible;
    if (index < state.engine->models.size()) {
        state.engine->models[index]->visible = visible;
    }
    notifySceneChanged();
}

void ViewportHostWidget::relocateSceneItemInFrontOfCamera(int index)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (index < 0 || index >= state.sceneEntries.size() || !state.camera) {
        return;
    }

    float yaw = state.camera->cameraRotation.x;
    float pitch = state.camera->cameraRotation.y;
    glm::vec3 front;
    front.x = cos(pitch) * sin(yaw);
    front.y = sin(pitch);
    front.z = -cos(pitch) * cos(yaw);
    front = glm::normalize(front);

    glm::vec3 newPos = state.camera->cameraPos + front * 3.0f;
    QVector3D translation(newPos.x, newPos.y, newPos.z);

    auto& entry = state.sceneEntries[index];
    updateSceneItemTransform(index, translation, entry.rotation, entry.scale);
}

void ViewportHostWidget::setSceneChangedCallback(std::function<void(const QList<SceneItem>&)> callback)
{
    m_sceneChangedCallback = std::move(callback);
}

void ViewportHostWidget::setCameraChangedCallback(std::function<void()> callback)
{
    m_cameraChangedCallback = std::move(callback);
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

void ViewportHostWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (m_cameraChangedCallback) {
        m_cameraChangedCallback();
    }
}

void ViewportHostWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    QWidget::mousePressEvent(event);
}

void ViewportHostWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        qDebug() << "[ViewportHost] dragEnterEvent ignored: no URLs";
        return;
    }

    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isRenderableAsset(url.toLocalFile()))
        {
            qDebug() << "[ViewportHost] dragEnterEvent accepted:" << url.toLocalFile();
            event->acceptProposedAction();
            return;
        }
    }

    qDebug() << "[ViewportHost] dragEnterEvent rejected URLs:" << event->mimeData()->urls();
}

void ViewportHostWidget::dropEvent(QDropEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        qDebug() << "[ViewportHost] dropEvent ignored: no URLs";
        return;
    }

    bool accepted = false;
    for (const QUrl& url : event->mimeData()->urls())
    {
        const QString path = url.toLocalFile();
        if (path.isEmpty() || !isRenderableAsset(path))
        {
            qDebug() << "[ViewportHost] dropEvent skipping non-renderable path:" << path;
            continue;
        }
        qDebug() << "[ViewportHost] dropEvent adding asset:" << path;
        addAssetToScene(path);
        accepted = true;
    }

    if (accepted)
    {
        event->acceptProposedAction();
    }
    else
    {
        qDebug() << "[ViewportHost] dropEvent accepted nothing";
    }
}

void ViewportHostWidget::ensureViewportInitialized()
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (m_initialized)
    {
        return;
    }

    try
    {
        state.engine = std::make_unique<Engine>();
        state.display = state.engine->createWindow(width(), height(), "Motive Embedded Viewport", false, state.use2DPipeline, true);
        state.camera = new Camera(state.engine.get(), state.display, glm::vec3(0.0f, 0.0f, 3.0f), glm::vec2(glm::radians(0.0f), 0.0f));
        state.camera->moveSpeed = state.cameraSpeed;
        state.display->addCamera(state.camera);
        state.display->setBackgroundColor(state.bgColorR, state.bgColorG, state.bgColorB);
        m_initialized = true;

        if (!state.pendingSceneEntries.isEmpty())
        {
            const QList<EmbeddedViewportState::SceneEntry> pendingEntries = state.pendingSceneEntries;
            state.pendingSceneEntries.clear();
            qDebug() << "[ViewportHost] Restoring pending scene entries after init:" << pendingEntries.size();
            QList<SceneItem> items;
            for (const auto& entry : pendingEntries)
            {
                items.push_back(SceneItem{entry.name, entry.sourcePath, entry.translation, entry.rotation, entry.scale, entry.visible});
            }
            loadSceneFromItems(items);
        }
        else
        {
            const std::filesystem::path scenePath = defaultScenePath();
            if (!scenePath.empty())
            {
                state.currentAssetPath = QString::fromStdString(scenePath.string());
                addAssetToScene(state.currentAssetPath);
            }
            else if (!state.currentAssetPath.isEmpty())
            {
                addAssetToScene(state.currentAssetPath);
            }
        }

        embedNativeWindow();
        syncEmbeddedWindowGeometry();
        if (m_statusLabel)
        {
            m_statusLabel->hide();
        }
        m_renderTimer.start();
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

void ViewportHostWidget::addAssetToScene(const QString& path)
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (path.isEmpty())
    {
        qWarning() << "[ViewportHost] addAssetToScene called with empty path";
        return;
    }

    state.currentAssetPath = path;
    if (!m_initialized || !state.engine)
    {
        qDebug() << "[ViewportHost] Queued scene asset before init:" << path;
        return;
    }

    try
    {
        EmbeddedViewportState::SceneEntry entry{
            QFileInfo(path).completeBaseName(),
            QFileInfo(path).absoluteFilePath(),
            QVector3D(static_cast<float>(state.engine->models.size()) * 1.6f, 0.0f, 0.0f),
            QVector3D(-90.0f, 0.0f, 0.0f),
            QVector3D(1.0f, 1.0f, 1.0f),
            true
        };
        loadModelIntoEngine(state, entry);
        state.sceneEntries.push_back(entry);
        qDebug() << "[ViewportHost] Scene asset added:" << path
                 << "sceneCount=" << state.sceneEntries.size();
        notifySceneChanged();
    }
    catch (const std::exception& ex)
    {
        qWarning() << "[ViewportHost] Failed to add scene asset:" << path << ex.what();
    }
}

void ViewportHostWidget::renderFrame()
{
    auto& state = viewportState();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
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
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
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
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
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

void ViewportHostWidget::notifySceneChanged()
{
    if (m_sceneChangedCallback)
    {
        m_sceneChangedCallback(sceneItems());
    }
}

}  // namespace motive::ui
