#pragma once

#include <QColor>
#include <QTimer>
#include <QVector3D>
#include <QWidget>
#include <QString>
#include <QString>
#include <QStringList>
#include <functional>

class QLabel;
class QDragEnterEvent;
class QDropEvent;

namespace motive::ui {

class ViewportHostWidget : public QWidget
{
public:
    struct SceneItem
    {
        QString name;
        QString sourcePath;
        QVector3D translation;
        QVector3D rotation;
        QVector3D scale;
        bool visible = true;
    };

    explicit ViewportHostWidget(QWidget* parent = nullptr);
    ~ViewportHostWidget() override;

    void loadAssetFromPath(const QString& path);
    void loadSceneFromItems(const QList<SceneItem>& items);
    void addAssetToScene(const QString& path);
    QString currentAssetPath() const;
    QList<SceneItem> sceneItems() const;
    QVector3D cameraPosition() const;
    QVector3D cameraRotation() const;
    QString renderPath() const;
    void setCameraPosition(const QVector3D& position);
    void setCameraRotation(const QVector3D& rotation);
    void setCameraSpeed(float speed);
    void resetCamera();
    void setBackgroundColor(const QColor& color);
    void setRenderPath(const QString& renderPath);
    void updateSceneItemTransform(int index, const QVector3D& translation, const QVector3D& rotation, const QVector3D& scale);
    void setSceneItemVisible(int index, bool visible);
    void relocateSceneItemInFrontOfCamera(int index);
    void setSceneChangedCallback(std::function<void(const QList<SceneItem>&)> callback);
    void setCameraChangedCallback(std::function<void()> callback);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void ensureViewportInitialized();
    void renderFrame();
    void embedNativeWindow();
    void syncEmbeddedWindowGeometry();
    void notifySceneChanged();

    QTimer m_renderTimer;
    bool m_initialized = false;
    bool m_initScheduled = false;
    QLabel* m_statusLabel = nullptr;
    std::function<void(const QList<SceneItem>&)> m_sceneChangedCallback;
    std::function<void()> m_cameraChangedCallback;
};

}  // namespace motive::ui
